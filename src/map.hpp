#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace tmfdev {

struct MapSymbol {
    std::uint16_t section;
    std::uint32_t section_offset;
    std::uint32_t virtual_address;

    std::string name;

    bool is_function = false;
};

struct MapInfo {
    std::filesystem::path path;

    bool has_timestamp = false;
    std::uint32_t timestamp = 0;

    bool has_preferred_load_address = false;
    std::uint32_t preferred_load_address = 0;

    std::vector<MapSymbol> symbols;

    // Built once while parsing the MAP so evidence readers do not rescan
    // the complete symbol vector for exact-name or address lookups.
    std::unordered_map<std::string, std::vector<std::size_t>> symbols_by_name;
    std::unordered_map<std::uint32_t, std::vector<std::size_t>> symbols_by_virtual_address;
    std::unordered_map<std::uint16_t, std::vector<std::size_t>> symbols_by_section;
    std::unordered_map<std::string, std::vector<std::size_t>> symbols_by_owner;
};

MapInfo inspect_map(const std::filesystem::path& path);

} // namespace tmfdev
