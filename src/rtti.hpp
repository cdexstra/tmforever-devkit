#pragma once

#include "build.hpp"
#include "vtable.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace tmfdev {

struct RttiPmd {
    std::int32_t mdisp = 0;
    std::int32_t pdisp = 0;
    std::int32_t vdisp = 0;
};

struct RttiBaseClass {
    std::uint32_t descriptor_virtual_address = 0;
    std::uint32_t type_descriptor_virtual_address = 0;

    std::string raw_type_name;

    std::uint32_t contained_bases = 0;

    RttiPmd pmd;

    std::uint32_t attributes = 0;
};

struct RttiInfo {
    std::string class_name;

    std::uint32_t vftable_virtual_address = 0;

    std::uint32_t locator_pointer_location = 0;
    std::uint32_t locator_virtual_address = 0;

    std::uint32_t signature = 0;
    std::uint32_t object_offset = 0;
    std::uint32_t constructor_displacement_offset = 0;

    std::uint32_t type_descriptor_virtual_address = 0;
    std::string raw_type_name;

    std::uint32_t hierarchy_virtual_address = 0;
    std::uint32_t hierarchy_signature = 0;
    std::uint32_t hierarchy_attributes = 0;

    std::uint32_t base_class_array_virtual_address = 0;

    std::vector<RttiBaseClass> bases;
};

RttiInfo inspect_rtti(
    const BuildInfo& build,
    const std::string& class_name
);

RttiInfo inspect_rtti(
    const BuildInfo& build,
    const std::string& class_name,
    const VtableInfo& vtable
);

} // namespace tmfdev
