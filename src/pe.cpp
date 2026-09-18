#include "pe.hpp"

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <windows.h>
#include <bcrypt.h>

namespace tmfdev {

namespace {

std::vector<std::uint8_t> read_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);

    if (!file) {
        throw std::runtime_error("failed to open executable");
    }

    file.seekg(0, std::ios::end);
    const auto size = file.tellg();

    if (size < 0) {
        throw std::runtime_error("failed to get executable size");
    }

    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));

    file.seekg(0, std::ios::beg);

    if (!data.empty()) {
        file.read(
            reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(data.size())
        );
    }

    if (!file) {
        throw std::runtime_error("failed to read executable");
    }

    return data;
}

std::uint16_t read_u16(
    const std::vector<std::uint8_t>& data,
    std::size_t offset
)
{
    if (offset + 2 > data.size()) {
        throw std::runtime_error("unexpected end of executable");
    }

    return static_cast<std::uint16_t>(
        data[offset]
        | (static_cast<std::uint16_t>(data[offset + 1]) << 8)
    );
}

std::uint32_t read_u32(
    const std::vector<std::uint8_t>& data,
    std::size_t offset
)
{
    if (offset + 4 > data.size()) {
        throw std::runtime_error("unexpected end of executable");
    }

    return static_cast<std::uint32_t>(
        data[offset]
        | (static_cast<std::uint32_t>(data[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(data[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(data[offset + 3]) << 24)
    );
}

std::string calculate_sha256(const std::vector<std::uint8_t>& data)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;

    auto check = [](NTSTATUS status, const char* message) {
        if (status < 0) {
            throw std::runtime_error(message);
        }
    };

    check(
        BCryptOpenAlgorithmProvider(
            &algorithm,
            BCRYPT_SHA256_ALGORITHM,
            nullptr,
            0
        ),
        "failed to initialize SHA-256"
    );

    try {
        DWORD object_size = 0;
        DWORD hash_size = 0;
        DWORD result_size = 0;

        check(
            BCryptGetProperty(
                algorithm,
                BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&object_size),
                sizeof(object_size),
                &result_size,
                0
            ),
            "failed to get SHA-256 object size"
        );

        check(
            BCryptGetProperty(
                algorithm,
                BCRYPT_HASH_LENGTH,
                reinterpret_cast<PUCHAR>(&hash_size),
                sizeof(hash_size),
                &result_size,
                0
            ),
            "failed to get SHA-256 digest size"
        );

        std::vector<std::uint8_t> hash_object(object_size);
        std::vector<std::uint8_t> digest(hash_size);

        check(
            BCryptCreateHash(
                algorithm,
                &hash,
                hash_object.data(),
                static_cast<ULONG>(hash_object.size()),
                nullptr,
                0,
                0
            ),
            "failed to create SHA-256 hash"
        );

        if (!data.empty()) {
            check(
                BCryptHashData(
                    hash,
                    const_cast<PUCHAR>(
                        reinterpret_cast<const UCHAR*>(data.data())
                    ),
                    static_cast<ULONG>(data.size()),
                    0
                ),
                "failed to hash executable"
            );
        }

        check(
            BCryptFinishHash(
                hash,
                digest.data(),
                static_cast<ULONG>(digest.size()),
                0
            ),
            "failed to finish SHA-256 hash"
        );

        BCryptDestroyHash(hash);
        hash = nullptr;

        BCryptCloseAlgorithmProvider(algorithm, 0);
        algorithm = nullptr;

        std::ostringstream output;
        output << std::hex << std::setfill('0');

        for (const auto byte : digest) {
            output << std::setw(2) << static_cast<unsigned>(byte);
        }

        return output.str();
    }
    catch (...) {
        if (hash != nullptr) {
            BCryptDestroyHash(hash);
        }

        if (algorithm != nullptr) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }

        throw;
    }
}

PeInfo parse_pe(const std::vector<std::uint8_t>& data)
{
    if (data.size() < 0x40 || data[0] != 'M' || data[1] != 'Z') {
        throw std::runtime_error("not a valid PE file: missing MZ header");
    }

    const auto pe_offset = read_u32(data, 0x3C);

    if (static_cast<std::size_t>(pe_offset) + 24 > data.size()) {
        throw std::runtime_error("invalid PE header offset");
    }

    if (
        data[pe_offset] != 'P'
        || data[pe_offset + 1] != 'E'
        || data[pe_offset + 2] != 0
        || data[pe_offset + 3] != 0
    ) {
        throw std::runtime_error("not a valid PE file: missing PE signature");
    }

    const auto coff = static_cast<std::size_t>(pe_offset) + 4;

    const auto machine = read_u16(data, coff);
    const auto section_count = read_u16(data, coff + 2);
    const auto timestamp = read_u32(data, coff + 4);
    const auto optional_header_size = read_u16(data, coff + 16);

    const auto optional = coff + 20;

    if (read_u16(data, optional) != 0x10B) {
        throw std::runtime_error("only PE32 executables are supported");
    }

    const auto image_base = read_u32(data, optional + 28);
    const auto image_size = read_u32(data, optional + 56);

    const auto section_table = optional + optional_header_size;

    std::vector<PeSection> sections;
    sections.reserve(section_count);

    for (std::uint16_t i = 0; i < section_count; ++i) {
        const auto offset =
            section_table + static_cast<std::size_t>(i) * 40;

        if (offset + 40 > data.size()) {
            throw std::runtime_error("truncated PE section table");
        }

        std::array<char, 9> name{};

        for (std::size_t j = 0; j < 8; ++j) {
            name[j] = static_cast<char>(data[offset + j]);
        }

        sections.push_back({
            std::string(name.data()),
            read_u32(data, offset + 12),
            read_u32(data, offset + 8),
            read_u32(data, offset + 20),
            read_u32(data, offset + 16),
        });
    }

    return {
        machine,
        timestamp,
        image_base,
        image_size,
        std::move(sections),
    };
}

} // namespace

std::string sha256_bytes(const std::vector<std::uint8_t>& data)
{
    return calculate_sha256(data);
}

ExecutableInfo inspect_executable(const std::filesystem::path& path)
{
    const auto data = read_file(path);
    const auto image_data =
        std::make_shared<const std::vector<std::uint8_t>>(data);

    return {
        path,
        static_cast<std::uint64_t>(data.size()),
        calculate_sha256(data),
        parse_pe(data),
        image_data,
    };
}

} // namespace tmfdev
