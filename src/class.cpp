#include "class.hpp"
#include "demangle.hpp"
#include "vtable.hpp"

#include <set>
#include <stdexcept>
#include <string>

namespace tmfdev {

namespace {

bool belongs_to_class_context(
    const std::string& symbol_name,
    const std::string& class_name
)
{
    const std::string marker =
        "@" + class_name + "@@";

    return symbol_name.find(marker) != std::string::npos;
}

bool is_direct_class_symbol(
    const std::string& symbol_name,
    const std::string& class_name
)
{
    const std::string owner =
        "@" + class_name + "@@";

    const auto owner_position =
        symbol_name.find(owner);

    if (owner_position == std::string::npos) {
        return false;
    }

    if (symbol_name.rfind("??0" + class_name + "@@", 0) == 0) {
        return true;
    }

    if (symbol_name.rfind("??1" + class_name + "@@", 0) == 0) {
        return true;
    }

    if (symbol_name.rfind("??_G" + class_name + "@@", 0) == 0) {
        return true;
    }

    if (symbol_name.rfind("??_E" + class_name + "@@", 0) == 0) {
        return true;
    }

    if (symbol_name.empty() || symbol_name[0] != '?') {
        return false;
    }

    const auto first_at = symbol_name.find('@');

    return first_at != std::string::npos
        && first_at == owner_position;
}

bool is_compiler_generated_function(
    const std::string& decorated,
    const std::string& demangled
)
{
    if (
        decorated.rfind("??_G", 0) == 0
        || decorated.rfind("??_E", 0) == 0
    ) {
        return true;
    }

    if (demangled.empty()) {
        return false;
    }

    return demangled.find("`scalar deleting destructor'")
            != std::string::npos
        || demangled.find("`vector deleting destructor'")
            != std::string::npos
        || demangled.find("`vftable'")
            != std::string::npos
        || demangled.find("`RTTI")
            != std::string::npos
        || demangled.find("`local static guard'")
            != std::string::npos
        || demangled.find("`dynamic initializer for")
            != std::string::npos
        || demangled.find("`dynamic atexit destructor for")
            != std::string::npos;
}

ClassSymbolKind classify_symbol(const MapSymbol& symbol)
{
    if (symbol.is_function) {
        return ClassSymbolKind::map_function;
    }

    if (symbol.name.rfind("??_R", 0) == 0) {
        return ClassSymbolKind::rtti;
    }

    if (
        symbol.name.rfind("??_8", 0) == 0
        || symbol.name.rfind("??_C", 0) == 0
        || symbol.name.rfind("??_7", 0) == 0
    ) {
        return ClassSymbolKind::compiler_data;
    }

    return ClassSymbolKind::static_data;
}

std::string exact_vftable_name(const std::string& class_name)
{
    return "??_7" + class_name + "@@6B@";
}

} // namespace

ClassInfo inspect_class(
    const BuildInfo& build,
    const std::string& class_name
)
{
    if (!build.matches()) {
        throw std::runtime_error(
            "cannot inspect class from mismatched executable and map"
        );
    }

    ClassInfo result;
    result.name = class_name;

    std::set<std::uint32_t> unique_map_function_rvas;
    std::set<std::uint32_t> unique_usable_function_rvas;

    const auto vftable_name =
        exact_vftable_name(class_name);

    const auto owner_symbols =
        build.map.symbols_by_owner.find(class_name);

    const auto inspect_symbol = [&](const MapSymbol& map_symbol) {
        if (
            map_symbol.name == vftable_name
            && map_symbol.virtual_address
                >= build.executable.pe.image_base
        ) {
            return;
        }

        if (
            !belongs_to_class_context(
                map_symbol.name,
                class_name
            )
        ) {
            return;
        }

        if (
            map_symbol.virtual_address
            < build.executable.pe.image_base
        ) {
            return;
        }

        const auto rva =
            map_symbol.virtual_address
            - build.executable.pe.image_base;

        const auto demangled =
            demangle_msvc(map_symbol.name);

        const bool demangled_ok =
            !demangled.empty();

        const bool direct_owner =
            map_symbol.is_function
            && is_direct_class_symbol(
                map_symbol.name,
                class_name
            );

        const bool compiler_generated =
            map_symbol.is_function
            && is_compiler_generated_function(
                map_symbol.name,
                demangled
            );

        const bool usable_function =
            map_symbol.is_function
            && direct_owner
            && demangled_ok
            && !compiler_generated;

        result.symbols.push_back({
            map_symbol.name,
            demangled,
            map_symbol.virtual_address,
            rva,
            classify_symbol(map_symbol),
            map_symbol.is_function,
            demangled_ok,
            direct_owner,
            compiler_generated,
            usable_function,
        });

        if (map_symbol.is_function) {
            ++result.map_function_count;
            unique_map_function_rvas.insert(rva);

            if (demangled_ok) {
                ++result.demangled_map_function_count;
            }

            if (direct_owner) {
                ++result.direct_owner_function_count;
            }

            if (compiler_generated) {
                ++result.compiler_generated_function_count;
            }
        }

        if (usable_function) {
            ++result.usable_function_count;
            unique_usable_function_rvas.insert(rva);
        }
    };

    const auto vtable_symbols =
        build.map.symbols_by_name.find(vftable_name);

    if (vtable_symbols != build.map.symbols_by_name.end()) {
        for (const auto index : vtable_symbols->second)
            inspect_symbol(build.map.symbols[index]);
    }

    if (owner_symbols != build.map.symbols_by_owner.end()) {
        for (const auto index : owner_symbols->second)
            inspect_symbol(build.map.symbols[index]);
    }

    const auto vtable = inspect_vtable(build, class_name);
    switch (vtable.state) {
    case VtableInfo::State::absent:
        result.vtable_state = ClassVtableState::absent;
        break;

    case VtableInfo::State::valid:
        result.vtable_state = ClassVtableState::valid;
        result.vftable_virtual_address = vtable.virtual_address;
        result.vftable_rva = vtable.rva;
        break;

    case VtableInfo::State::malformed:
        result.vtable_state = ClassVtableState::malformed;
        result.vtable_error = vtable.error;
        break;
    }

    result.unique_map_function_rva_count =
        unique_map_function_rvas.size();

    result.unique_usable_function_rva_count =
        unique_usable_function_rvas.size();

    return result;
}

const char* class_symbol_kind_name(ClassSymbolKind kind)
{
    switch (kind) {
    case ClassSymbolKind::map_function:
        return "map-function";

    case ClassSymbolKind::static_data:
        return "static-data";

    case ClassSymbolKind::rtti:
        return "rtti";

    case ClassSymbolKind::compiler_data:
        return "compiler-data";
    }

    return "unknown";
}

} // namespace tmfdev
