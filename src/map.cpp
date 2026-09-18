#include "map.hpp"

#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace tmfdev {

namespace {

bool parse_hex(std::string_view text, std::uint32_t& value)
{
    if (text.empty()) {
        return false;
    }

    const auto result = std::from_chars(
        text.data(),
        text.data() + text.size(),
        value,
        16
    );

    return result.ec == std::errc{}
        && result.ptr == text.data() + text.size();
}

bool parse_hex16(std::string_view text, std::uint16_t& value)
{
    std::uint32_t parsed = 0;

    if (!parse_hex(text, parsed) || parsed > 0xFFFF) {
        return false;
    }

    value = static_cast<std::uint16_t>(parsed);
    return true;
}

bool parse_header_hex(
    const std::string& line,
    const std::string& marker,
    std::uint32_t& value
)
{
    const auto marker_position = line.find(marker);

    if (marker_position == std::string::npos) {
        return false;
    }

    std::istringstream stream(
        line.substr(marker_position + marker.size())
    );

    std::string token;

    while (stream >> token) {
        while (!token.empty()) {
            const char last = token.back();

            if (
                last == ','
                || last == ')'
                || last == ']'
                || last == ';'
            ) {
                token.pop_back();
            }
            else {
                break;
            }
        }

        if (parse_hex(token, value)) {
            return true;
        }
    }

    return false;
}

bool parse_symbol_line(const std::string& line, MapSymbol& symbol)
{
    std::istringstream stream(line);

    std::string address;
    std::string name;
    std::string virtual_address;

    if (!(stream >> address >> name >> virtual_address)) {
        return false;
    }

    const auto colon = address.find(':');

    if (colon == std::string::npos) {
        return false;
    }

    const auto section_text =
        std::string_view(address).substr(0, colon);

    const auto offset_text =
        std::string_view(address).substr(colon + 1);

    std::uint16_t section = 0;
    std::uint32_t section_offset = 0;
    std::uint32_t va = 0;

    if (
        !parse_hex16(section_text, section)
        || !parse_hex(offset_text, section_offset)
        || !parse_hex(virtual_address, va)
    ) {
        return false;
    }

    bool is_function = false;

    std::string remaining_token;

    while (stream >> remaining_token) {
        if (remaining_token == "f") {
            is_function = true;
            break;
        }
    }

    symbol = {
        section,
        section_offset,
        va,
        std::move(name),
        is_function,
    };

    return true;
}

} // namespace

MapInfo inspect_map(const std::filesystem::path& path)
{
    std::ifstream file(path);

    if (!file) {
        throw std::runtime_error("failed to open map file");
    }

    MapInfo info;
    info.path = path;

    std::string line;

    while (std::getline(file, line)) {
        if (!info.has_timestamp) {
            std::uint32_t value = 0;

            if (parse_header_hex(line, "Timestamp is", value)) {
                info.timestamp = value;
                info.has_timestamp = true;
            }
        }

        if (!info.has_preferred_load_address) {
            std::uint32_t value = 0;

            if (
                parse_header_hex(
                    line,
                    "Preferred load address is",
                    value
                )
            ) {
                info.preferred_load_address = value;
                info.has_preferred_load_address = true;
            }
        }

        MapSymbol symbol;

        if (parse_symbol_line(line, symbol)) {
            info.symbols.push_back(std::move(symbol));
            const auto index = info.symbols.size() - 1;
            info.symbols_by_name[info.symbols[index].name].push_back(index);
            info.symbols_by_virtual_address[
                info.symbols[index].virtual_address
            ].push_back(index);
            info.symbols_by_section[
                info.symbols[index].section
            ].push_back(index);

            const auto& name = info.symbols[index].name;
            const auto first_at = name.find('@');
            std::size_t owner_start =
                first_at == std::string::npos
                    ? std::string::npos
                    : first_at + 1;

            if (name.rfind("??0", 0) == 0
                || name.rfind("??1", 0) == 0) {
                owner_start = 3;
            }
            else if (name.rfind("??_G", 0) == 0
                || name.rfind("??_E", 0) == 0) {
                owner_start = 4;
            }

            if (owner_start != std::string::npos) {
                const auto owner_end = name.find("@@", owner_start);
                if (owner_end != std::string::npos
                    && owner_end > owner_start) {
                    info.symbols_by_owner[
                        name.substr(owner_start, owner_end - owner_start)
                    ].push_back(index);
                }
            }
        }
    }

    if (!file.eof()) {
        throw std::runtime_error("failed while reading map file");
    }

    return info;
}

} // namespace tmfdev
