#pragma once

#include "build.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tmfdev {

struct ReflectionEnumValue {
    std::size_t index = 0;

    std::optional<std::uint32_t> name_virtual_address;
    std::string name;
};

struct ReflectionComponent {
    std::optional<std::uint32_t> name_virtual_address;
    std::string name;
};

struct ReflectionProcedureArgument {
    std::size_t index = 0;

    std::optional<std::uint32_t> class_id;
    std::optional<std::uint32_t> name_virtual_address;
    std::string name;
    std::optional<std::uint32_t> flags;
};

struct ReflectionParam {
    std::size_t index = 0;

    std::uint32_t record_virtual_address = 0;

    bool has_known_size = false;
    std::uint32_t record_size = 0;

    std::uint32_t type_code = 0;
    std::uint32_t id = 0;
    std::uint32_t param_virtual_address = 0;
    std::int32_t offset = 0;

    std::uint32_t name_virtual_address = 0;
    std::string name;

    std::uint32_t flags1 = 0;
    std::uint32_t flags2 = 0;

    std::optional<std::uint32_t> specialized_name_virtual_address;
    std::string specialized_name;

    std::optional<std::uint32_t> specialized_class_info_virtual_address;
    std::optional<std::uint32_t> specialized_function_virtual_address;
    std::optional<std::uint32_t> specialized_argument_count;

    std::optional<std::uint32_t> specialized_value_count;
    std::optional<std::uint32_t> specialized_value_names_virtual_address;

    std::optional<std::uint32_t> specialized_argument_class_ids_virtual_address;
    std::optional<std::uint32_t> specialized_argument_names_virtual_address;
    std::optional<std::uint32_t> specialized_argument_flags_virtual_address;

    std::optional<std::uint32_t> specialized_auxiliary0;
    std::optional<std::uint32_t> specialized_auxiliary1;

    std::vector<ReflectionComponent> components;
    std::vector<ReflectionEnumValue> enum_values;
    std::vector<ReflectionProcedureArgument> procedure_arguments;
};

struct ReflectionInfo {
    std::string class_name;

    std::uint32_t param_infos_virtual_address = 0;
    std::uint32_t param_count_virtual_address = 0;

    std::uint32_t param_count = 0;

    std::vector<ReflectionParam> params;
};

ReflectionInfo inspect_reflection(
    const BuildInfo& build,
    const std::string& class_name
);

// TMNF's reflection type code is a GameBox descriptor tag, not a C++ ABI
// type or an enum-width claim. Unknown codes remain explicitly unknown.
const char* reflection_param_type_name(std::uint32_t type_code);

} // namespace tmfdev
