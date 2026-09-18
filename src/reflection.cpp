#include "reflection.hpp"

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
            "failed to open executable for reflection inspection"
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
                "reflection RVA is not backed by raw file data"
            );
        }

        return static_cast<std::size_t>(
            section.raw_offset + relative
        );
    }

    throw std::runtime_error(
        "reflection RVA does not belong to a PE section"
    );
}

std::size_t va_to_file_offset(
    const PeInfo& pe,
    std::uint32_t virtual_address
)
{
    if (virtual_address < pe.image_base) {
        throw std::runtime_error(
            "reflection VA is below executable image base"
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
            "unexpected end of executable while reading reflection data"
        );
    }

    return static_cast<std::uint32_t>(
        data[offset]
        | (static_cast<std::uint32_t>(data[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(data[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(data[offset + 3]) << 24)
    );
}

std::string read_c_string(
    const std::vector<std::uint8_t>& data,
    std::size_t offset
)
{
    if (offset >= data.size()) {
        throw std::runtime_error(
            "reflection string offset is outside executable"
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
                "reflection string is unreasonably long"
            );
        }
    }

    throw std::runtime_error(
        "unterminated reflection string"
    );
}

bool record_has_bytes(
    const ReflectionParam& param,
    std::size_t relative_offset,
    std::size_t size)
{
    if (!param.has_known_size)
        return true;

    return relative_offset <= param.record_size
        && size <= param.record_size - relative_offset;
}

std::optional<std::uint32_t> read_record_u32(
    const std::vector<std::uint8_t>& data,
    std::size_t record_offset,
    const ReflectionParam& param,
    std::size_t relative_offset)
{
    if (!record_has_bytes(param, relative_offset, 4))
        return std::nullopt;

    try {
        return read_u32(data, record_offset + relative_offset);
    }
    catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<std::uint32_t> read_pointer_array_u32(
    const std::vector<std::uint8_t>& data,
    const PeInfo& pe,
    std::uint32_t array_virtual_address,
    std::size_t index)
{
    if (array_virtual_address == 0)
        return std::nullopt;

    try {
        const auto array_offset = va_to_file_offset(
            pe,
            array_virtual_address
        );
        return read_u32(data, array_offset + index * 4);
    }
    catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<std::string> try_read_string(
    const std::vector<std::uint8_t>& data,
    const PeInfo& pe,
    std::uint32_t virtual_address)
{
    if (virtual_address == 0)
        return std::nullopt;

    try {
        return read_c_string(
            data,
            va_to_file_offset(pe, virtual_address)
        );
    }
    catch (const std::exception&) {
        return std::nullopt;
    }
}

bool reflection_type_is_array(std::uint32_t type_code)
{
    switch (type_code) {
    case 2: case 3: case 4:
    case 6: case 7: case 8:
    case 10: case 11: case 12:
    case 15: case 16: case 17:
    case 20: case 21: case 22:
    case 24: case 25: case 26:
    case 28: case 29: case 30:
    case 32: case 33: case 34:
    case 37: case 38: case 39:
    case 42: case 43: case 44:
    case 46: case 47: case 48:
    case 50: case 51: case 52:
    case 54: case 55: case 56:
    case 58: case 59: case 60:
    case 62: case 63: case 64:
        return true;
    default:
        return false;
    }
}

bool reflection_type_is_range(std::uint32_t type_code)
{
    return type_code == 18
        || type_code == 35
        || type_code == 40;
}

std::size_t reflection_vector_component_count(std::uint32_t type_code)
{
    switch (type_code) {
    case 49: return 2;
    case 53: return 3;
    case 57: return 4;
    default: return 0;
    }
}

const MapSymbol& find_class_static(
    const MapInfo& map,
    const std::string& class_name,
    const std::string& member_name
)
{
    const std::string marker =
        member_name + "@" + class_name + "@@";

    const MapSymbol* match = nullptr;

    const auto owner_symbols = map.symbols_by_owner.find(class_name);
    if (owner_symbols != map.symbols_by_owner.end()) {
        for (const auto index : owner_symbols->second) {
            const auto& symbol = map.symbols[index];
            if (symbol.name.find(marker) == std::string::npos)
                continue;

            if (match != nullptr) {
                throw std::runtime_error(
                    "multiple map symbols matched class static: "
                    + member_name
                );
            }

            match = &symbol;
        }
    }

    if (match == nullptr) {
        throw std::runtime_error(
            "map symbol not found for class static: "
            + member_name
        );
    }

    return *match;
}

} // namespace

ReflectionInfo inspect_reflection(
    const BuildInfo& build,
    const std::string& class_name
)
{
    if (!build.matches()) {
        throw std::runtime_error(
            "cannot inspect reflection from mismatched executable and map"
        );
    }

    const auto& param_infos_symbol =
        find_class_static(
            build.map,
            class_name,
            "m_ParamInfos"
        );

    const auto& param_count_symbol =
        find_class_static(
            build.map,
            class_name,
            "m_ParamInfoCount"
        );

    const auto& data =
        *build.executable.image_data;

    ReflectionInfo result;
    result.class_name = class_name;

    result.param_infos_virtual_address =
        param_infos_symbol.virtual_address;

    result.param_count_virtual_address =
        param_count_symbol.virtual_address;

    const auto count_offset =
        va_to_file_offset(
            build.executable.pe,
            result.param_count_virtual_address
        );

    result.param_count =
        read_u32(
            data,
            count_offset
        );

    if (result.param_count > 4096) {
        throw std::runtime_error(
            "reflection parameter count is unreasonable"
        );
    }

    const auto table_offset =
        va_to_file_offset(
            build.executable.pe,
            result.param_infos_virtual_address
        );

    std::vector<std::uint32_t> record_addresses;
    record_addresses.reserve(result.param_count);

    for (
        std::uint32_t i = 0;
        i < result.param_count;
        ++i
    ) {
        record_addresses.push_back(
            read_u32(
                data,
                table_offset + i * 4
            )
        );
    }

    result.params.reserve(result.param_count);

    for (
        std::uint32_t i = 0;
        i < result.param_count;
        ++i
    ) {
        const auto record_va =
            record_addresses[i];

        const auto record_offset =
            va_to_file_offset(
                build.executable.pe,
                record_va
            );

        ReflectionParam param;

        param.index = i;
        param.record_virtual_address =
            record_va;

        if (i + 1 < record_addresses.size()) {
            const auto next_va =
                record_addresses[i + 1];

            if (next_va > record_va) {
                param.has_known_size = true;
                param.record_size =
                    next_va - record_va;
            }
        }

        param.type_code =
            read_u32(
                data,
                record_offset + 0x00
            );

        param.id =
            read_u32(
                data,
                record_offset + 0x04
            );

        param.param_virtual_address =
            read_u32(
                data,
                record_offset + 0x08
            );

        param.offset =
            static_cast<std::int32_t>(read_u32(
                data,
                record_offset + 0x0C
            ));

        param.name_virtual_address =
            read_u32(
                data,
                record_offset + 0x10
            );

        param.flags1 =
            read_u32(
                data,
                record_offset + 0x14
            );

        param.flags2 =
            read_u32(
                data,
                record_offset + 0x18
            );

        if (param.name_virtual_address != 0) {
            param.name =
                read_c_string(
                    data,
                    va_to_file_offset(
                        build.executable.pe,
                        param.name_virtual_address
                    )
                );
        }

        const auto read_specialized_name = [&](std::size_t relative_offset) {
            const auto name_virtual_address = read_record_u32(
                data,
                record_offset,
                param,
                relative_offset
            );
            if (!name_virtual_address.has_value())
                return;

            param.specialized_name_virtual_address =
                *name_virtual_address;

            if (const auto name = try_read_string(
                    data,
                    build.executable.pe,
                    *name_virtual_address
                ); name.has_value()) {
                param.specialized_name = *name;
            }
        };

        const auto read_components = [&](std::size_t count) {
            for (std::size_t component = 0;
                 component < count;
                 ++component) {
                const auto name_virtual_address = read_record_u32(
                    data,
                    record_offset,
                    param,
                    0x1C + component * 4
                );
                if (!name_virtual_address.has_value())
                    continue;

                const auto name = try_read_string(
                    data,
                    build.executable.pe,
                    *name_virtual_address
                );
                ReflectionComponent component;
                component.name_virtual_address = *name_virtual_address;
                if (name.has_value())
                    component.name = *name;
                param.components.push_back(std::move(component));
            }
        };

        switch (param.type_code) {
        case 0:
            param.specialized_auxiliary0 = read_record_u32(
                data,
                record_offset,
                param,
                0x1C
            );
            break;

        case 5:
            param.specialized_class_info_virtual_address = read_record_u32(
                data,
                record_offset,
                param,
                0x1C
            );
            param.specialized_auxiliary0 = read_record_u32(
                data,
                record_offset,
                param,
                0x20
            );
            break;

        case 13:
            read_specialized_name(0x1C);
            param.specialized_value_count = read_record_u32(
                data,
                record_offset,
                param,
                0x20
            );
            param.specialized_value_names_virtual_address = read_record_u32(
                data,
                record_offset,
                param,
                0x24
            );

            if (param.specialized_value_count.has_value()
                && param.specialized_value_names_virtual_address.has_value()
                && *param.specialized_value_count <= 4096) {
                for (std::uint32_t value_index = 0;
                     value_index < *param.specialized_value_count;
                     ++value_index) {
                    ReflectionEnumValue value;
                    value.index = value_index;
                    value.name_virtual_address = read_pointer_array_u32(
                        data,
                        build.executable.pe,
                        *param.specialized_value_names_virtual_address,
                        value_index
                    );
                    if (value.name_virtual_address.has_value()) {
                        if (const auto name = try_read_string(
                                data,
                                build.executable.pe,
                                *value.name_virtual_address
                            ); name.has_value()) {
                            value.name = *name;
                        }
                    }
                    param.enum_values.push_back(std::move(value));
                }
            }
            break;

        case 65:
            param.specialized_function_virtual_address = read_record_u32(
                data,
                record_offset,
                param,
                0x1C
            );
            param.specialized_argument_count = read_record_u32(
                data,
                record_offset,
                param,
                0x20
            );
            param.specialized_argument_class_ids_virtual_address =
                read_record_u32(data, record_offset, param, 0x24);
            param.specialized_argument_names_virtual_address =
                read_record_u32(data, record_offset, param, 0x28);
            param.specialized_argument_flags_virtual_address =
                read_record_u32(data, record_offset, param, 0x2C);

            if (param.specialized_argument_count.has_value()
                && *param.specialized_argument_count <= 4096) {
                for (std::uint32_t argument_index = 0;
                     argument_index < *param.specialized_argument_count;
                     ++argument_index) {
                    ReflectionProcedureArgument argument;
                    argument.index = argument_index;

                    if (param.specialized_argument_class_ids_virtual_address
                        .has_value()) {
                        argument.class_id = read_pointer_array_u32(
                            data,
                            build.executable.pe,
                            *param.specialized_argument_class_ids_virtual_address,
                            argument_index
                        );
                    }
                    if (param.specialized_argument_names_virtual_address
                        .has_value()) {
                        argument.name_virtual_address = read_pointer_array_u32(
                            data,
                            build.executable.pe,
                            *param.specialized_argument_names_virtual_address,
                            argument_index
                        );
                    }
                    if (argument.name_virtual_address.has_value()) {
                        if (const auto name = try_read_string(
                                data,
                                build.executable.pe,
                                *argument.name_virtual_address
                            ); name.has_value()) {
                            argument.name = *name;
                        }
                    }
                    if (param.specialized_argument_flags_virtual_address
                        .has_value()) {
                        argument.flags = read_pointer_array_u32(
                            data,
                            build.executable.pe,
                            *param.specialized_argument_flags_virtual_address,
                            argument_index
                        );
                    }
                    param.procedure_arguments.push_back(std::move(argument));
                }
            }
            break;

        default:
            if (reflection_type_is_array(param.type_code)) {
                param.specialized_auxiliary0 = read_record_u32(
                    data,
                    record_offset,
                    param,
                    0x1C
                );
                read_specialized_name(0x20);
                param.specialized_auxiliary1 = read_record_u32(
                    data,
                    record_offset,
                    param,
                    0x24
                );
                param.specialized_class_info_virtual_address = read_record_u32(
                    data,
                    record_offset,
                    param,
                    0x28
                );
            }
            else if (reflection_type_is_range(param.type_code)) {
                param.specialized_auxiliary0 = read_record_u32(
                    data,
                    record_offset,
                    param,
                    0x1C
                );
                param.specialized_auxiliary1 = read_record_u32(
                    data,
                    record_offset,
                    param,
                    0x20
                );
            }
            else if (const auto component_count =
                         reflection_vector_component_count(param.type_code);
                     component_count != 0) {
                read_components(component_count);
            }
            break;
        }

        result.params.push_back(
            std::move(param)
        );
    }

    return result;
}

const char* reflection_param_type_name(
    std::uint32_t type_code)
{
    switch (type_code) {
    case 0: return "Action";
    case 1: return "Bool";
    case 2: return "BoolArray";
    case 3: return "BoolBuffer";
    case 4: return "BoolBufferCat";
    case 5: return "Class";
    case 6: return "ClassArray";
    case 7: return "ClassBuffer";
    case 8: return "ClassBufferCat";
    case 9: return "Color";
    case 10: return "ColorArray";
    case 11: return "ColorBuffer";
    case 12: return "ColorBufferCat";
    case 13: return "Enum";
    case 14: return "Int";
    case 15: return "IntArray";
    case 16: return "IntBuffer";
    case 17: return "IntBufferCat";
    case 18: return "IntRange";
    case 19: return "Iso4";
    case 20: return "Iso4Array";
    case 21: return "Iso4Buffer";
    case 22: return "Iso4BufferCat";
    case 23: return "Iso3";
    case 24: return "Iso3Array";
    case 25: return "Iso3Buffer";
    case 26: return "Iso3BufferCat";
    case 27: return "Id";
    case 28: return "IdArray";
    case 29: return "IdBuffer";
    case 30: return "IdBufferCat";
    case 31: return "Natural";
    case 32: return "NaturalArray";
    case 33: return "NaturalBuffer";
    case 34: return "NaturalBufferCat";
    case 35: return "NaturalRange";
    case 36: return "Real";
    case 37: return "RealArray";
    case 38: return "RealBuffer";
    case 39: return "RealBufferCat";
    case 40: return "RealRange";
    case 41: return "String";
    case 42: return "StringArray";
    case 43: return "StringBuffer";
    case 44: return "StringBufferCat";
    case 45: return "StringInt";
    case 46: return "StringIntArray";
    case 47: return "StringIntBuffer";
    case 48: return "StringIntBufferCat";
    case 49: return "Vec2";
    case 50: return "Vec2Array";
    case 51: return "Vec2Buffer";
    case 52: return "Vec2BufferCat";
    case 53: return "Vec3";
    case 54: return "Vec3Array";
    case 55: return "Vec3Buffer";
    case 56: return "Vec3BufferCat";
    case 57: return "Vec4";
    case 58: return "Vec4Array";
    case 59: return "Vec4Buffer";
    case 60: return "Vec4BufferCat";
    case 61: return "Int3";
    case 62: return "Int3Array";
    case 63: return "Int3Buffer";
    case 64: return "Int3BufferCat";
    case 65: return "Proc";
    default: return "unknown";
    }
}

} // namespace tmfdev
