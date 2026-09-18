#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace tmfdev {

struct PeSection {
    std::string name;
    std::uint32_t virtual_address;
    std::uint32_t virtual_size;
    std::uint32_t raw_offset;
    std::uint32_t raw_size;
};

struct PeInfo {
    std::uint16_t machine;
    std::uint32_t timestamp;
    std::uint32_t image_base;
    std::uint32_t image_size;
    std::vector<PeSection> sections;
};

struct ExecutableInfo {
    std::filesystem::path path;
    std::uint64_t file_size;
    std::string sha256;
    PeInfo pe;
    std::shared_ptr<const std::vector<std::uint8_t>> image_data;
};

ExecutableInfo inspect_executable(const std::filesystem::path& path);

std::string sha256_bytes(const std::vector<std::uint8_t>& data);

} // namespace tmfdev
