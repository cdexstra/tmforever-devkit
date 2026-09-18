#include "vtable.hpp"

#include "demangle.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace tmfdev {

namespace {

std::vector<std::uint8_t> read_file(
    const std::filesystem::path& path
)
{
    std::ifstream file(path, std::ios::binary);

    if (!file) {
        throw std::runtime_error(
            "failed to open executable for vtable inspection"
        );
    }

    file.seekg(0, std::ios::end);
    const auto size = file.tellg();

    if (size < 0) {
        throw std::runtime_error(
            "failed to get executable size"
        );
    }

    std::vector<std::uint8_t> data(
        static_cast<std::size_t>(size)
    );

    file.seekg(0, std::ios::beg);

    if (!data.empty()) {
        file.read(
            reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(data.size())
        );
    }

    if (!file) {
        throw std::runtime_error(
            "failed to read executable"
        );
    }

    return data;
}

std::uint32_t read_u32(
    const std::vector<std::uint8_t>& data,
    std::size_t offset
)
{
    if (offset + 4 > data.size()) {
        throw std::runtime_error(
            "unexpected end of executable while reading vtable"
        );
    }

    return static_cast<std::uint32_t>(
        data[offset]
        | (static_cast<std::uint32_t>(data[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(data[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(data[offset + 3]) << 24)
    );
}

std::size_t rva_to_file_offset(
    const PeInfo& pe,
    std::uint32_t rva
)
{
    for (const auto& section : pe.sections) {
        const auto section_start =
            section.virtual_address;

        const auto section_extent =
            std::max(
                section.virtual_size,
                section.raw_size
            );

        const auto section_end =
            section_start + section_extent;

        if (
            rva >= section_start
            && rva < section_end
        ) {
            const auto relative =
                rva - section_start;

            if (relative >= section.raw_size) {
                throw std::runtime_error(
                    "RVA exists in virtual section but not in raw file data"
                );
            }

            return static_cast<std::size_t>(
                section.raw_offset + relative
            );
        }
    }

    throw std::runtime_error(
        "RVA does not belong to a PE section"
    );
}

std::string exact_vftable_name(
    const std::string& class_name
)
{
    return "??_7" + class_name + "@@6B@";
}

const MapSymbol* find_vftable_symbol(
    const MapInfo& map,
    const std::string& class_name
)
{
    const auto expected =
        exact_vftable_name(class_name);

    const auto found = map.symbols_by_name.find(expected);
    if (found != map.symbols_by_name.end()
        && !found->second.empty()) {
        return &map.symbols[found->second.front()];
    }

    return nullptr;
}

std::uint32_t find_vftable_end_offset(
    const MapInfo& map,
    const MapSymbol& vftable
)
{
    std::uint32_t next_offset =
        std::numeric_limits<std::uint32_t>::max();

    const auto section_symbols = map.symbols_by_section.find(vftable.section);
    if (section_symbols == map.symbols_by_section.end())
        throw std::runtime_error("vftable section has no indexed symbols");

    for (const auto index : section_symbols->second) {
        const auto& symbol = map.symbols[index];
        if (symbol.section != vftable.section) {
            continue;
        }

        if (symbol.section_offset <= vftable.section_offset) {
            continue;
        }

        if (symbol.section_offset < next_offset) {
            next_offset = symbol.section_offset;
        }
    }

    if (
        next_offset
        == std::numeric_limits<std::uint32_t>::max()
    ) {
        throw std::runtime_error(
            "could not determine end of vftable from map"
        );
    }

    return next_offset;
}

std::vector<VtableAlias> find_aliases(
    const MapInfo& map,
    std::uint32_t virtual_address
)
{
    std::vector<VtableAlias> aliases;

    const auto found = map.symbols_by_virtual_address.find(virtual_address);
    if (found == map.symbols_by_virtual_address.end())
        return aliases;

    for (const auto index : found->second) {
        const auto& symbol = map.symbols[index];
        aliases.push_back({
            symbol.name,
            demangle_msvc(symbol.name),
        });
    }

    return aliases;
}

} // namespace

VtableInfo inspect_vtable(
    const BuildInfo& build,
    const std::string& class_name
)
{
    if (!build.matches()) {
        throw std::runtime_error(
            "cannot inspect vtable from mismatched executable and map"
        );
    }

    const auto* vftable =
        find_vftable_symbol(
            build.map,
            class_name
        );

    if (!vftable) {
        VtableInfo result;
        result.class_name = class_name;
        return result;
    }

    return inspect_vtable_symbol(
        build,
        *vftable,
        class_name,
        true
    );
}

VtableInfo inspect_vtable_symbol(
    const BuildInfo& build,
    const MapSymbol& vftable,
    const std::string& class_name,
    bool resolve_aliases
)
{
    if (!build.matches()) {
        throw std::runtime_error(
            "cannot inspect vtable from mismatched executable and map"
        );
    }

    VtableInfo result;
    result.class_name = class_name;

    try {

    if (
        vftable.virtual_address
        < build.executable.pe.image_base
    ) {
        throw std::runtime_error(
            "vftable VA is below executable image base"
        );
    }

    const auto end_offset =
        find_vftable_end_offset(
            build.map,
            vftable
        );

    const auto byte_size =
        end_offset - vftable.section_offset;

    if (byte_size == 0 || byte_size % 4 != 0) {
        throw std::runtime_error(
            "vftable size inferred from map is not pointer-aligned"
        );
    }

    const auto slot_count =
        static_cast<std::size_t>(byte_size / 4);

    const auto vftable_rva =
        vftable.virtual_address
        - build.executable.pe.image_base;

    const auto& executable_data =
        *build.executable.image_data;

    const auto file_offset =
        rva_to_file_offset(
            build.executable.pe,
            vftable_rva
        );

    result.state = VtableInfo::State::valid;
    result.virtual_address =
        vftable.virtual_address;
    result.rva =
        vftable_rva;
    result.map_section =
        vftable.section;
    result.map_section_offset =
        vftable.section_offset;
    result.byte_size =
        byte_size;

    result.slots.reserve(slot_count);

    for (std::size_t i = 0; i < slot_count; ++i) {
        const auto target_va =
            read_u32(
                executable_data,
                file_offset + i * 4
            );

        std::uint32_t target_rva = 0;

        if (
            target_va
            >= build.executable.pe.image_base
        ) {
            target_rva =
                target_va
                - build.executable.pe.image_base;
        }

        result.slots.push_back({
            i,
            target_va,
            target_rva,
            resolve_aliases
                ? find_aliases(build.map, target_va)
                : std::vector<VtableAlias>{},
        });
    }

    return result;
    }
    catch (const std::exception& error) {
        result.state = VtableInfo::State::malformed;
        result.error = error.what();
        result.virtual_address = 0;
        result.rva = 0;
        result.byte_size = 0;
        result.slots.clear();
        return result;
    }
}

} // namespace tmfdev
