#include "rtti.hpp"

#include "vtable.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
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
            "failed to open executable for RTTI inspection"
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

std::size_t rva_to_file_offset(
    const PeInfo& pe,
    std::uint32_t rva
)
{
    for (const auto& section : pe.sections) {
        const auto extent =
            std::max(
                section.virtual_size,
                section.raw_size
            );

        const auto start =
            section.virtual_address;

        const auto end =
            start + extent;

        if (rva < start || rva >= end) {
            continue;
        }

        const auto relative =
            rva - start;

        if (relative >= section.raw_size) {
            throw std::runtime_error(
                "RTTI RVA is not backed by raw file data"
            );
        }

        return static_cast<std::size_t>(
            section.raw_offset + relative
        );
    }

    throw std::runtime_error(
        "RTTI RVA does not belong to a PE section"
    );
}

std::size_t va_to_file_offset(
    const PeInfo& pe,
    std::uint32_t virtual_address
)
{
    if (virtual_address < pe.image_base) {
        throw std::runtime_error(
            "RTTI VA is below executable image base"
        );
    }

    return rva_to_file_offset(
        pe,
        virtual_address - pe.image_base
    );
}

std::uint32_t read_u32(
    const std::vector<std::uint8_t>& data,
    std::size_t offset
)
{
    if (offset + 4 > data.size()) {
        throw std::runtime_error(
            "unexpected end of executable while reading RTTI"
        );
    }

    return static_cast<std::uint32_t>(
        data[offset]
        | (static_cast<std::uint32_t>(data[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(data[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(data[offset + 3]) << 24)
    );
}

std::int32_t read_i32(
    const std::vector<std::uint8_t>& data,
    std::size_t offset
)
{
    return static_cast<std::int32_t>(
        read_u32(data, offset)
    );
}

std::string read_c_string(
    const std::vector<std::uint8_t>& data,
    std::size_t offset
)
{
    if (offset >= data.size()) {
        throw std::runtime_error(
            "RTTI string offset is outside executable"
        );
    }

    std::string result;

    while (offset < data.size()) {
        const char value =
            static_cast<char>(data[offset++]);

        if (value == '\0') {
            return result;
        }

        result.push_back(value);

        if (result.size() > 4096) {
            throw std::runtime_error(
                "RTTI type name is unreasonably long"
            );
        }
    }

    throw std::runtime_error(
        "unterminated RTTI type name"
    );
}

std::string read_type_name(
    const PeInfo& pe,
    const std::vector<std::uint8_t>& data,
    std::uint32_t type_descriptor_va
)
{
    const auto offset =
        va_to_file_offset(
            pe,
            type_descriptor_va
        );

    // MSVC x86 TypeDescriptor:
    // +0x00 vftable
    // +0x04 spare
    // +0x08 decorated type name
    return read_c_string(
        data,
        offset + 8
    );
}

} // namespace

RttiInfo inspect_rtti(
    const BuildInfo& build,
    const std::string& class_name
)
{
    return inspect_rtti(
        build,
        class_name,
        inspect_vtable(build, class_name)
    );
}

RttiInfo inspect_rtti(
    const BuildInfo& build,
    const std::string& class_name,
    const VtableInfo& vtable
)
{
    if (!build.matches()) {
        throw std::runtime_error(
            "cannot inspect RTTI from mismatched executable and map"
        );
    }

    if (vtable.state != VtableInfo::State::valid)
        return {};

    const auto& data =
        *build.executable.image_data;

    if (vtable.virtual_address < 4) {
        throw std::runtime_error(
            "invalid vftable address"
        );
    }

    RttiInfo result;
    result.class_name = class_name;
    result.vftable_virtual_address =
        vtable.virtual_address;

    result.locator_pointer_location =
        vtable.virtual_address - 4;

    const auto locator_pointer_offset =
        va_to_file_offset(
            build.executable.pe,
            result.locator_pointer_location
        );

    result.locator_virtual_address =
        read_u32(
            data,
            locator_pointer_offset
        );

    const auto locator_offset =
        va_to_file_offset(
            build.executable.pe,
            result.locator_virtual_address
        );

    // MSVC x86 CompleteObjectLocator
    result.signature =
        read_u32(data, locator_offset + 0);

    result.object_offset =
        read_u32(data, locator_offset + 4);

    result.constructor_displacement_offset =
        read_u32(data, locator_offset + 8);

    result.type_descriptor_virtual_address =
        read_u32(data, locator_offset + 12);

    result.hierarchy_virtual_address =
        read_u32(data, locator_offset + 16);

    result.raw_type_name =
        read_type_name(
            build.executable.pe,
            data,
            result.type_descriptor_virtual_address
        );

    const auto hierarchy_offset =
        va_to_file_offset(
            build.executable.pe,
            result.hierarchy_virtual_address
        );

    // MSVC x86 ClassHierarchyDescriptor
    result.hierarchy_signature =
        read_u32(data, hierarchy_offset + 0);

    result.hierarchy_attributes =
        read_u32(data, hierarchy_offset + 4);

    const auto base_count =
        read_u32(data, hierarchy_offset + 8);

    result.base_class_array_virtual_address =
        read_u32(data, hierarchy_offset + 12);

    if (base_count > 4096) {
        throw std::runtime_error(
            "RTTI base class count is unreasonable"
        );
    }

    const auto base_array_offset =
        va_to_file_offset(
            build.executable.pe,
            result.base_class_array_virtual_address
        );

    result.bases.reserve(base_count);

    for (std::uint32_t i = 0; i < base_count; ++i) {
        const auto descriptor_va =
            read_u32(
                data,
                base_array_offset + i * 4
            );

        const auto descriptor_offset =
            va_to_file_offset(
                build.executable.pe,
                descriptor_va
            );

        RttiBaseClass base;

        base.descriptor_virtual_address =
            descriptor_va;

        base.type_descriptor_virtual_address =
            read_u32(
                data,
                descriptor_offset + 0
            );

        base.contained_bases =
            read_u32(
                data,
                descriptor_offset + 4
            );

        base.pmd.mdisp =
            read_i32(
                data,
                descriptor_offset + 8
            );

        base.pmd.pdisp =
            read_i32(
                data,
                descriptor_offset + 12
            );

        base.pmd.vdisp =
            read_i32(
                data,
                descriptor_offset + 16
            );

        base.attributes =
            read_u32(
                data,
                descriptor_offset + 20
            );

        base.raw_type_name =
            read_type_name(
                build.executable.pe,
                data,
                base.type_descriptor_virtual_address
            );

        result.bases.push_back(
            std::move(base)
        );
    }

    return result;
}

} // namespace tmfdev
