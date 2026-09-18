#pragma once

#include "build.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace tmfdev {

enum class ClassSymbolKind {
    map_function,
    static_data,
    rtti,
    compiler_data,
};

enum class ClassVtableState {
    absent,
    valid,
    malformed,
};

struct ClassSymbol {
    std::string decorated_name;
    std::string demangled_name;

    std::uint32_t virtual_address = 0;
    std::uint32_t rva = 0;

    ClassSymbolKind kind = ClassSymbolKind::static_data;

    bool map_function_flag = false;
    bool demangled = false;
    bool direct_owner = false;
    bool compiler_generated = false;
    bool usable_function = false;
};

struct ClassInfo {
    std::string name;

    std::vector<ClassSymbol> symbols;

    std::size_t map_function_count = 0;
    std::size_t unique_map_function_rva_count = 0;

    std::size_t demangled_map_function_count = 0;
    std::size_t direct_owner_function_count = 0;
    std::size_t compiler_generated_function_count = 0;

    std::size_t usable_function_count = 0;
    std::size_t unique_usable_function_rva_count = 0;

    ClassVtableState vtable_state = ClassVtableState::absent;
    std::string vtable_error;
    std::uint32_t vftable_virtual_address = 0;
    std::uint32_t vftable_rva = 0;
};

ClassInfo inspect_class(
    const BuildInfo& build,
    const std::string& class_name
);

const char* class_symbol_kind_name(ClassSymbolKind kind);

} // namespace tmfdev
