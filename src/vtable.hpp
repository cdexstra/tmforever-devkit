#pragma once

#include "build.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace tmfdev {

struct VtableAlias {
    std::string decorated_name;
    std::string demangled_name;
};

struct VtableSlot {
    std::size_t index = 0;

    std::uint32_t target_virtual_address = 0;
    std::uint32_t target_rva = 0;

    std::vector<VtableAlias> aliases;
};

struct VtableInfo {
    enum class State { absent, valid, malformed };
    State state = State::absent;
    std::string class_name;
    std::string error;

    std::uint32_t virtual_address = 0;
    std::uint32_t rva = 0;

    std::uint16_t map_section = 0;
    std::uint32_t map_section_offset = 0;

    std::uint32_t byte_size = 0;

    std::vector<VtableSlot> slots;
};

VtableInfo inspect_vtable(
    const BuildInfo& build,
    const std::string& class_name
);

VtableInfo inspect_vtable_symbol(
    const BuildInfo& build,
    const MapSymbol& vftable,
    const std::string& class_name,
    bool resolve_aliases = true
);

} // namespace tmfdev
