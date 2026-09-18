#include "abi.hpp"
#include "build.hpp"
#include "class.hpp"
#include "map.hpp"
#include "model.hpp"
#include "model_builder.hpp"
#include "pe.hpp"
#include "reflection.hpp"
#include "rtti.hpp"
#include "sdk.hpp"
#include "vtable.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void print_hex(std::uint32_t value, int width)
{
    const auto flags = std::cout.flags();
    const auto fill = std::cout.fill();

    std::cout
        << "0x"
        << std::uppercase
        << std::hex
        << std::right
        << std::setw(width)
        << std::setfill('0')
        << value;

    std::cout.flags(flags);
    std::cout.fill(fill);
}

void print_executable(const tmfdev::ExecutableInfo& info)
{
    std::cout << "TMForever executable\n";
    std::cout << "====================\n\n";

    std::cout << "path:        " << info.path.string() << '\n';
    std::cout << "size:        " << info.file_size << " bytes\n";
    std::cout << "sha256:      " << info.sha256 << '\n';

    std::cout << "machine:     ";
    print_hex(info.pe.machine, 4);
    std::cout << '\n';

    std::cout << "timestamp:   ";
    print_hex(info.pe.timestamp, 8);
    std::cout << '\n';

    std::cout << "image base:  ";
    print_hex(info.pe.image_base, 8);
    std::cout << '\n';

    std::cout << "image size:  ";
    print_hex(info.pe.image_size, 8);
    std::cout << '\n';
}

void print_build(const tmfdev::BuildInfo& info)
{
    std::cout << "TMForever build\n";
    std::cout << "===============\n\n";

    std::cout << "sha256:           "
              << info.executable.sha256
              << '\n';

    std::cout << "timestamp match:  "
              << (info.timestamp_matches ? "YES" : "NO")
              << '\n';

    std::cout << "image base match: "
              << (info.image_base_matches ? "YES" : "NO")
              << '\n';

    std::cout << "build match:      "
              << (info.matches() ? "YES" : "NO")
              << '\n';
}

void print_class(const tmfdev::ClassInfo& info)
{
    std::cout << "TMForever class\n";
    std::cout << "===============\n\n";

    std::cout << "name:                   "
              << info.name
              << '\n';

    std::cout << "map functions:          "
              << info.map_function_count
              << '\n';

    std::cout << "direct-owner functions: "
              << info.direct_owner_function_count
              << '\n';

    std::cout << "usable functions:       "
              << info.usable_function_count
              << '\n';

    std::cout << "vtable evidence:        ";

    if (info.vtable_state == tmfdev::ClassVtableState::valid) {
        print_hex(
            info.vftable_virtual_address,
            8
        );

        std::cout << "  RVA ";

        print_hex(
            info.vftable_rva,
            8
        );
    }
    else if (info.vtable_state == tmfdev::ClassVtableState::malformed) {
        std::cout << "malformed";
    }
    else {
        std::cout << "absent";
    }

    std::cout << '\n';

    if (info.vtable_state == tmfdev::ClassVtableState::malformed) {
        std::cout << "vtable error:           "
                  << info.vtable_error
                  << '\n';
    }
}

void print_vtable(const tmfdev::VtableInfo& info)
{
    constexpr std::size_t max_aliases = 4;

    std::cout << "TMForever vtable\n";
    std::cout << "================\n\n";

    std::cout << "class:      "
              << info.class_name
              << '\n';

    std::cout << "state:      ";
    switch (info.state) {
    case tmfdev::VtableInfo::State::absent:
        std::cout << "absent";
        break;

    case tmfdev::VtableInfo::State::valid:
        std::cout << "valid";
        break;

    case tmfdev::VtableInfo::State::malformed:
        std::cout << "malformed";
        break;
    }
    std::cout << '\n';

    if (info.state == tmfdev::VtableInfo::State::malformed) {
        std::cout << "error:      "
                  << info.error
                  << '\n';
        return;
    }

    if (info.state == tmfdev::VtableInfo::State::absent) {
        std::cout << "no direct vtable symbol was present in the MAP\n";
        return;
    }

    std::cout << "address:    ";
    print_hex(info.virtual_address, 8);

    std::cout << "  RVA ";
    print_hex(info.rva, 8);
    std::cout << '\n';

    std::cout << "byte size:  "
              << info.byte_size
              << '\n';

    std::cout << "slots:      "
              << info.slots.size()
              << "\n\n";

    for (const auto& slot : info.slots) {
        std::cout
            << "slot "
            << std::right
            << std::setw(2)
            << slot.index
            << "  RVA ";

        print_hex(slot.target_rva, 8);

        std::cout
            << "  aliases "
            << slot.aliases.size()
            << '\n';

        const auto shown =
            std::min(
                max_aliases,
                slot.aliases.size()
            );

        for (std::size_t i = 0; i < shown; ++i) {
            const auto& alias =
                slot.aliases[i];

            std::cout << "    - ";

            if (!alias.demangled_name.empty()) {
                std::cout
                    << alias.demangled_name;
            }
            else {
                std::cout
                    << alias.decorated_name;
            }

            std::cout << '\n';
        }

        if (slot.aliases.size() > shown) {
            std::cout
                << "    ... "
                << (slot.aliases.size() - shown)
                << " more aliases\n";
        }
    }
}

void print_rtti(const tmfdev::RttiInfo& info)
{
    std::cout << "TMForever RTTI\n";
    std::cout << "===============\n\n";

    std::cout << "class:        "
              << info.class_name
              << '\n';

    std::cout << "object locator: ";
    print_hex(
        info.locator_virtual_address,
        8
    );

    std::cout << '\n';

    std::cout << "type:           "
              << info.raw_type_name
              << '\n';

    std::cout << "base classes:   "
              << info.bases.size()
              << "\n\n";

    for (std::size_t i = 0; i < info.bases.size(); ++i) {
        const auto& base =
            info.bases[i];

        std::cout
            << "base "
            << i
            << "  "
            << base.raw_type_name
            << '\n';

        std::cout
            << "  PMD: mdisp="
            << base.pmd.mdisp
            << " pdisp="
            << base.pmd.pdisp
            << " vdisp="
            << base.pmd.vdisp
            << '\n';
    }
}

void print_reflection(
    const tmfdev::ReflectionInfo& info)
{
    std::cout << "TMForever reflection\n";
    std::cout << "====================\n\n";

    std::cout << "class:          "
              << info.class_name
              << '\n';

    std::cout << "param infos:    ";
    print_hex(
        info.param_infos_virtual_address,
        8
    );
    std::cout << '\n';

    std::cout << "param count at: ";
    print_hex(
        info.param_count_virtual_address,
        8
    );
    std::cout << '\n';

    std::cout << "param count:    "
              << info.param_count
              << "\n\n";

    std::cout
        << "idx  id          size  type       param      offset  flags1     flags2     name\n";

    std::cout
        << "---------------------------------------------------------------------------------------\n";

    for (const auto& param : info.params) {
        std::cout
            << std::right
            << std::setw(3)
            << param.index
            << "  ";

        print_hex(param.id, 8);
        std::cout << "  ";

        if (param.has_known_size) {
            print_hex(param.record_size, 2);
        }
        else {
            std::cout << "  ?";
        }

        std::cout << "  ";
        print_hex(param.type_code, 8);
        std::cout << " ("
                  << tmfdev::reflection_param_type_name(param.type_code)
                  << ")";

        std::cout << "  ";
        print_hex(param.param_virtual_address, 8);

        std::cout << "  ";
        std::cout << std::setw(6)
                  << param.offset;

        std::cout << "  ";
        print_hex(param.flags1, 8);

        std::cout << "  ";
        print_hex(param.flags2, 8);

        std::cout << "  "
                  << param.name;

        if (param.type_code == 0)
            std::cout << "  [action]";
        else if (param.type_code == 65)
            std::cout << "  [procedure args="
                      << (param.specialized_argument_count.has_value()
                          ? std::to_string(
                              *param.specialized_argument_count
                          )
                          : "?")
                      << "]";
        else if (param.offset < 0)
            std::cout << "  [virtual-parameter]";
        else
            std::cout << "  [physical-member]";

        if (!param.specialized_name.empty())
            std::cout << "  specialized=" << param.specialized_name;
        if (!param.enum_values.empty())
            std::cout << "  enum-values=" << param.enum_values.size();
        if (!param.components.empty())
            std::cout << "  components=" << param.components.size();

        std::cout << '\n';

        if (!param.components.empty()) {
            std::cout << "     component names:";

            for (const auto& component : param.components) {
                std::cout << ' '
                          << (component.name.empty()
                              ? std::string{"<unnamed>"}
                              : component.name);
            }

            std::cout << '\n';
        }

        if (!param.enum_values.empty()) {
            std::cout << "     enum values:";

            for (const auto& value : param.enum_values) {
                std::cout << ' '
                          << value.index
                          << '='
                          << (value.name.empty()
                              ? std::string{"<unnamed>"}
                              : value.name);
            }

            std::cout << '\n';
        }

        if (!param.procedure_arguments.empty()) {
            std::cout << "     procedure arguments:\n";

            for (const auto& argument : param.procedure_arguments) {
                std::cout
                    << "       "
                    << argument.index
                    << "  name="
                    << (argument.name.empty()
                        ? std::string{"<unnamed>"}
                        : argument.name);

                if (argument.class_id.has_value()) {
                    std::cout << "  class-id=";
                    print_hex(*argument.class_id, 8);
                }

                if (argument.flags.has_value()) {
                    std::cout << "  flags=";
                    print_hex(*argument.flags, 8);
                }

                std::cout << '\n';
            }
        }
    }
}

void print_model(const tmfdev::NativeModel& model)
{
    std::cout << "TMForever native model\n";
    std::cout << "======================\n\n";

    if (model.build.sha256.known()) {
        std::cout << "build sha256:    "
                  << *model.build.sha256.value
                  << '\n';
    }

    if (model.build.image_base.known()) {
        std::cout << "image base:      ";
        print_hex(*model.build.image_base.value, 8);
        std::cout << '\n';
    }

    std::cout << "types:           "
              << model.types.size()
              << '\n';

    std::cout << "classes:         "
              << model.classes.size()
              << '\n';

    std::cout << "members:         "
              << model.members.size()
              << '\n';

    std::cout << "reflection descriptors: "
              << model.reflection_descriptors.size()
              << '\n';

    std::cout << "functions:       "
              << model.functions.size()
              << '\n';

    std::cout << "physical code:   "
              << model.physical_code.size()
              << '\n';

    std::cout << "vtables:         "
              << model.vtables.size()
              << '\n';

    std::cout << "address-only globals: "
              << model.globals.size()
              << '\n';

    std::size_t direct_owner_functions = model.functions.size();
    const auto abi_report =
        tmfdev::build_native_abi_report(model);
    tmfdev::NativeAbiReport direct_abi_report;
    bool has_direct_abi_report = false;

    if (model.classes.size() == 1) {
        const auto direct_prefix =
            model.classes.front().name + "::";
        tmfdev::NativeModel direct_model = model;

        direct_model.functions.erase(
            std::remove_if(
                direct_model.functions.begin(),
                direct_model.functions.end(),
                [&](const tmfdev::NativeFunction& function) {
                    return !function.qualified_name.starts_with(
                        direct_prefix
                    );
                }
            ),
            direct_model.functions.end()
        );

        direct_owner_functions = direct_model.functions.size();
        direct_abi_report =
            tmfdev::build_native_abi_report(direct_model);
        has_direct_abi_report = true;
    }

    std::cout << "direct-owner functions: "
              << direct_owner_functions
              << '\n';

    std::cout << "semantic rules:  "
              << model.semantic_rules.size()
              << "\n\n";

    std::cout << "ABI readiness (model)\n"
              << "-------------\n"
              << "  ready:       " << abi_report.ready << '\n'
              << "  conditional: " << abi_report.conditional << '\n'
              << "  blocked:     " << abi_report.blocked << '\n';

    if (has_direct_abi_report) {
        std::cout << "\nABI readiness (direct-owner)\n"
                  << "---------------------------\n"
                  << "  ready:       " << direct_abi_report.ready << '\n'
                  << "  conditional: " << direct_abi_report.conditional << '\n'
                  << "  blocked:     " << direct_abi_report.blocked << '\n';
    }

    if (model.classes.size() == 1) {
        const auto sdk =
            tmfdev::generate_native_sdk_header(
                model,
                model.classes.front().name
            );

        std::cout << "\nSDK surface\n"
                  << "-----------\n"
                  << "  emitted:     " << sdk.emitted_functions << '\n'
                  << "  skipped:     " << sdk.skipped_functions << '\n';
    }

    std::cout << '\n';

    for (const auto& type : model.types) {
        std::cout << "type "
                  << type.name
                  << '\n';

        std::cout << "  id:        "
                  << type.id
                  << '\n';

        std::cout << "  kind:      ";

        if (type.kind.known()) {
            std::cout
                << tmfdev::native_type_kind_name(
                    *type.kind.value
                );
        }
        else {
            std::cout << "unknown";
        }

        std::cout << '\n';

        std::cout << "  size:      ";

        if (type.size.known()) {
            print_hex(
                *type.size.value,
                2
            );
        }
        else {
            std::cout << "unknown";
        }

        std::cout << '\n';

        std::cout << "  alignment: ";

        if (type.alignment.known()) {
            std::cout
                << *type.alignment.value;
        }
        else {
            std::cout << "unknown";
        }

        std::cout << '\n';

        std::cout << "  fields:    "
                  << type.fields.size()
                  << '\n';

        for (const auto& field : type.fields) {
            std::cout
                << "    #"
                << field.index
                << "  offset=";

            if (field.offset.known()) {
                print_hex(
                    *field.offset.value,
                    2
                );
            }
            else {
                std::cout << "unknown";
            }

            std::cout << "  size=";

            if (field.size.known()) {
                std::cout
                    << *field.size.value;
            }
            else {
                std::cout << "unknown";
            }

            std::cout << "  name=";

            if (field.name.known()) {
                std::cout
                    << *field.name.value;
            }
            else {
                std::cout << "unknown";
            }

            std::cout << "  type=";

            if (field.type.known()) {
                std::cout
                    << *field.type.value;
            }
            else {
                std::cout << "unknown";
            }

            std::cout << '\n';
        }

        std::cout << '\n';
    }

    for (const auto& native_class : model.classes) {
        std::cout << "class "
                  << native_class.name
                  << '\n';

        std::cout << "  id:        "
                  << native_class.id
                  << '\n';

        std::cout << "  RTTI hierarchy: "
                  << native_class.rtti_hierarchy.size()
                  << '\n';

        for (const auto& base : native_class.rtti_hierarchy) {
            std::cout
                << "    - "
                << base.class_id;

            if (base.member_displacement.known()) {
                std::cout
                    << "  mdisp="
                    << *base.member_displacement.value;
            }

            std::cout << '\n';
        }

        std::cout << "  members:   "
                  << native_class.member_ids.size()
                  << '\n';

        for (const auto& member_id : native_class.member_ids) {
            const auto it =
                std::find_if(
                    model.members.begin(),
                    model.members.end(),
                    [&](const tmfdev::NativeMember& member) {
                        return member.id == member_id;
                    }
                );

            if (it == model.members.end())
                continue;

            std::cout
                << "    - "
                << it->name;

            if (it->offset.known()) {
                std::cout << "  offset ";
                print_hex(*it->offset.value, 8);
            }
            else {
                std::cout << "  offset <unknown>";
            }

            std::cout << '\n';
        }

        std::cout << "  functions: "
                  << native_class.function_ids.size()
                  << '\n';

        std::size_t abi_parsed = 0;
        std::size_t abi_failed = 0;

        std::size_t abi_thiscall = 0;
        std::size_t abi_cdecl = 0;
        std::size_t abi_stdcall = 0;
        std::size_t abi_fastcall = 0;

        std::size_t abi_const = 0;
        std::size_t abi_static = 0;
        std::size_t abi_virtual = 0;

        for (const auto& function_id :
             native_class.function_ids) {
            const auto function_it =
                std::find_if(
                    model.functions.begin(),
                    model.functions.end(),
                    [&](const tmfdev::NativeFunction& function) {
                        return function.id == function_id;
                    }
                );

            if (function_it == model.functions.end())
                continue;

            if (
                function_it->return_type.known()
                && function_it->calling_convention.known()
            ) {
                ++abi_parsed;

                switch (*function_it->calling_convention.value) {
                case tmfdev::CallingConvention::thiscall_:
                    ++abi_thiscall;
                    break;

                case tmfdev::CallingConvention::cdecl_:
                    ++abi_cdecl;
                    break;

                case tmfdev::CallingConvention::stdcall_:
                    ++abi_stdcall;
                    break;

                case tmfdev::CallingConvention::fastcall_:
                    ++abi_fastcall;
                    break;

                case tmfdev::CallingConvention::unknown:
                    break;
                }

                if (
                    function_it->is_const.known()
                    && *function_it->is_const.value
                ) {
                    ++abi_const;
                }

                if (
                    function_it->is_static.known()
                    && *function_it->is_static.value
                ) {
                    ++abi_static;
                }

                if (
                    function_it->is_virtual.known()
                    && *function_it->is_virtual.value
                ) {
                    ++abi_virtual;
                }
            }
            else {
                ++abi_failed;
            }
        }

        std::cout << "\n";
        std::cout << "  ABI coverage\n";
        std::cout << "  ------------\n";
        std::cout << "  parsed:     " << abi_parsed << '\n';
        std::cout << "  failed:     " << abi_failed << '\n';
        std::cout << "  thiscall:   " << abi_thiscall << '\n';
        std::cout << "  cdecl:      " << abi_cdecl << '\n';
        std::cout << "  stdcall:    " << abi_stdcall << '\n';
        std::cout << "  fastcall:   " << abi_fastcall << '\n';
        std::cout << "  const:      " << abi_const << '\n';
        std::cout << "  static:     " << abi_static << '\n';
        std::cout << "  virtual:    " << abi_virtual << '\n';

        if (abi_failed != 0) {
            std::cout << "\n";
            std::cout << "  ABI parse failures\n";
            std::cout << "  ------------------\n";

            for (const auto& function_id :
                 native_class.function_ids) {
                const auto function_it =
                    std::find_if(
                        model.functions.begin(),
                        model.functions.end(),
                        [&](const tmfdev::NativeFunction& function) {
                            return function.id == function_id;
                        }
                    );

                if (function_it == model.functions.end())
                    continue;

                if (
                    function_it->return_type.known()
                    && function_it->calling_convention.known()
                ) {
                    continue;
                }

                std::cout
                    << "    - "
                    << function_it->qualified_name
                    << '\n';

                if (function_it->signature_parse_error.known()) {
                    std::cout
                        << "      "
                        << *function_it->signature_parse_error.value
                        << '\n';
                }
                else {
                    std::cout
                        << "      <no parse error recorded>\n";
                }
            }
        }

        std::cout << "\n";
        std::cout << "  vtable:    ";

        if (native_class.vtable_state.known()) {
            std::cout
                << tmfdev::native_vtable_state_name(
                    *native_class.vtable_state.value
                );

            if (native_class.vtable_error.known()) {
                std::cout
                    << " ("
                    << *native_class.vtable_error.value
                    << ")";
            }

            std::cout << " ";
        }

        if (native_class.vtable_id.known()) {
            std::cout
                << *native_class.vtable_id.value;
        }
        else {
            std::cout
                << "<unknown>";
        }

        std::cout << "\n\n";
    }

    std::cout << "physical code aliases\n";
    std::cout << "---------------------\n";

    std::size_t aliased_code_count = 0;

    for (const auto& code : model.physical_code) {
        if (code.logical_function_ids.size() <= 1)
            continue;

        ++aliased_code_count;

        std::cout << code.id
                  << "  logical functions "
                  << code.logical_function_ids.size()
                  << '\n';

        for (const auto& function_id :
             code.logical_function_ids) {
            std::cout
                << "    - "
                << function_id
                << '\n';
        }
    }

    if (aliased_code_count == 0) {
        std::cout << "<none>\n";
    }

    std::cout << "\n";

    for (const auto& vtable : model.vtables) {
        std::cout << "vtable "
                  << vtable.id
                  << '\n';

        if (vtable.virtual_address.known()) {
            std::cout << "  address: ";
            print_hex(
                *vtable.virtual_address.value,
                8
            );
            std::cout << '\n';
        }

        std::cout << "  slots:   "
                  << vtable.slots.size()
                  << '\n';

        for (const auto& slot : vtable.slots) {
            std::cout
                << "    slot "
                << std::right
                << std::setw(2)
                << slot.index;

            if (slot.target_rva.known()) {
                std::cout << "  RVA ";
                print_hex(
                    *slot.target_rva.value,
                    8
                );
            }

            std::cout
                << "  candidates "
                << slot.candidate_function_ids.size()
                << '\n';
        }
    }
}

void print_abi_report(const tmfdev::NativeModel& model)
{
    const auto report =
        tmfdev::build_native_abi_report(model);

    std::cout << "TMForever native ABI report\n";
    std::cout << "===========================\n\n";

    std::cout << "logical functions: "
              << report.functions
              << '\n';
    std::cout << "ready:             "
              << report.ready
              << '\n';
    std::cout << "conditional:       "
              << report.conditional
              << '\n';
    std::cout << "blocked:           "
              << report.blocked
              << "\n\n";

    std::cout << "dominant ABI issues\n";
    std::cout << "-------------------\n";

    if (report.issue_summaries.empty()) {
        std::cout << "<none>\n";
    }
    else {
        constexpr std::size_t max_issues = 12;
        const auto count = std::min(
            max_issues,
            report.issue_summaries.size()
        );

        for (std::size_t i = 0; i < count; ++i) {
            const auto& issue =
                report.issue_summaries[i];

            std::cout
                << "["
                << tmfdev::native_abi_issue_level_name(
                       issue.level
                   )
                << "] functions="
                << issue.affected_functions
                << " occurrences="
                << issue.occurrences
                << "  "
                << issue.detail
                << '\n';
        }
    }

    std::cout << "\nmissing ABI type evidence\n";
    std::cout << "-------------------------\n";

    if (report.type_needs.empty()) {
        std::cout << "<none>\n";
        return;
    }

    constexpr std::size_t max_types = 20;
    const auto count = std::min(
        max_types,
        report.type_needs.size()
    );

    for (std::size_t i = 0; i < count; ++i) {
        const auto& need = report.type_needs[i];

        std::cout
            << need.type_name
            << "  kind="
            << tmfdev::native_type_kind_name(need.kind)
            << "  functions="
            << need.affected_functions
            << "  occurrences="
            << need.occurrences
            << '\n';
    }
}

void print_function(
    const tmfdev::NativeModel& model,
    const std::string& class_name,
    const std::string& function_name)
{
    const std::string wanted =
        class_name + "::" + function_name;

    std::vector<const tmfdev::NativeFunction*> matches;

    for (const auto& function : model.functions) {
        if (function.qualified_name == wanted) {
            matches.push_back(&function);
        }
    }

    if (matches.empty()) {
        throw std::runtime_error(
            "function not found in canonical model: "
            + wanted
        );
    }

    std::cout << "TMForever native function\n";
    std::cout << "=========================\n\n";

    std::cout << "matches:       "
              << matches.size()
              << "\n\n";

    for (std::size_t match_index = 0;
         match_index < matches.size();
         ++match_index) {
        const auto& function =
            *matches[match_index];

        if (matches.size() > 1) {
            std::cout
                << "match "
                << (match_index + 1)
                << '\n';

            std::cout
                << "-------\n";
        }

        std::cout << "name:          "
                  << function.qualified_name
                  << '\n';

        if (function.decorated_name.known()) {
            std::cout << "decorated:     "
                      << *function.decorated_name.value
                      << '\n';
        }

        std::cout << "RVA:           ";

        if (function.rva.known()) {
            print_hex(
                *function.rva.value,
                8
            );
        }
        else {
            std::cout << "<unknown>";
        }

        std::cout << '\n';

        std::cout << "VA:            ";

        if (function.virtual_address.known()) {
            print_hex(
                *function.virtual_address.value,
                8
            );
        }
        else {
            std::cout << "<unknown>";
        }

        std::cout << '\n';

        std::cout << "return:        ";

        if (function.return_type.known()) {
            std::cout
                << *function.return_type.value;
        }
        else {
            std::cout << "<unknown>";
        }

        std::cout << '\n';

        if (function.return_abi.known()) {
            std::cout << "return ABI:    "
                      << tmfdev::native_return_abi_name(
                          *function.return_abi.value
                      )
                      << '\n';
        }

        std::cout << "calling conv:  ";

        if (function.calling_convention.known()) {
            std::cout
                << tmfdev::calling_convention_name(
                    *function.calling_convention.value
                );
        }
        else {
            std::cout << "<unknown>";
        }

        std::cout << '\n';

        std::cout << "access:        ";

        if (function.access.known()) {
            std::cout
                << tmfdev::native_access_name(
                    *function.access.value
                );
        }
        else {
            std::cout << "<unknown>";
        }

        std::cout << '\n';

        std::cout << "virtual:       ";

        if (function.is_virtual.known()) {
            std::cout
                << (*function.is_virtual.value
                    ? "yes"
                    : "no");
        }
        else {
            std::cout << "<unknown>";
        }

        std::cout << '\n';

        std::cout << "static:        ";

        if (function.is_static.known()) {
            std::cout
                << (*function.is_static.value
                    ? "yes"
                    : "no");
        }
        else {
            std::cout << "<unknown>";
        }

        std::cout << '\n';

        std::cout << "const:         ";

        if (function.is_const.known()) {
            std::cout
                << (*function.is_const.value
                    ? "yes"
                    : "no");
        }
        else {
            std::cout << "<unknown>";
        }

        std::cout << '\n';

        std::cout << "physical code: ";

        if (function.physical_code_id.known()) {
            std::cout
                << *function.physical_code_id.value;
        }
        else {
            std::cout << "<unknown>";
        }

        std::cout << '\n';

        if (function.virtual_slot.known()) {
            std::cout << "virtual slot:  "
                      << *function.virtual_slot.value
                      << '\n';
        }

        std::cout << "parameters:    "
                  << function.parameters.size()
                  << '\n';

        for (const auto& parameter :
             function.parameters) {
            std::cout
                << "  "
                << parameter.index
                << "  ";

            if (parameter.type.known()) {
                std::cout
                    << *parameter.type.value;
            }
            else {
                std::cout << "<unknown>";
            }

            std::cout << '\n';

            if (parameter.usage.base_type.known()) {
                std::cout
                    << "     base type:      "
                    << *parameter.usage.base_type.value
                    << '\n';
            }

            if (parameter.usage.pass_kind.known()) {
                std::cout
                    << "     passing:        "
                    << tmfdev::native_type_pass_kind_name(
                        *parameter.usage.pass_kind.value
                    )
                    << '\n';
            }

            if (parameter.usage.pointer_depth.known()) {
                std::cout
                    << "     pointer depth:  "
                    << *parameter.usage.pointer_depth.value
                    << '\n';
            }

            if (parameter.usage.is_const.known()) {
                std::cout
                    << "     const:          "
                    << (
                        *parameter.usage.is_const.value
                            ? "yes"
                            : "no"
                    )
                    << '\n';
            }

            if (parameter.referenced_type_id.known()) {
                std::cout
                    << "     canonical type: "
                    << *parameter.referenced_type_id.value
                    << '\n';
            }

            if (parameter.usage.parse_error.known()) {
                std::cout
                    << "     usage error:    "
                    << *parameter.usage.parse_error.value
                    << '\n';
            }
        }

        std::vector<std::size_t> candidate_slots;

        for (const auto& vtable : model.vtables) {
            for (const auto& slot : vtable.slots) {
                const auto candidate =
                    std::find(
                        slot.candidate_function_ids.begin(),
                        slot.candidate_function_ids.end(),
                        function.id
                    );

                if (
                    candidate
                    != slot.candidate_function_ids.end()
                ) {
                    candidate_slots.push_back(
                        slot.index
                    );
                }
            }
        }

        const auto abi =
            tmfdev::assess_native_function_abi(
                model,
                function
            );

        std::cout << "ABI status:     "
                  << tmfdev::native_abi_status_name(
                         abi.status
                     )
                  << '\n';

        std::cout << "ABI issues:     ";

        if (abi.issues.empty()) {
            std::cout << "<none>\n";
        }
        else {
            std::cout << abi.issues.size() << '\n';

            for (const auto& issue : abi.issues) {
                std::cout
                    << "  ["
                    << tmfdev::native_abi_issue_level_name(
                           issue.level
                       )
                    << "] "
                    << issue.subject
                    << ": "
                    << issue.detail
                    << '\n';
            }
        }

        const auto sdk =
            tmfdev::assess_native_sdk_function(
                model,
                function
            );

        std::cout << "SDK surface:    "
                  << (sdk.emitted ? "emitted" : "skipped")
                  << '\n';

        if (!sdk.emitted && !sdk.reason.empty()) {
            std::cout << "SDK skip:       "
                      << sdk.reason
                      << '\n';
        }

        std::cout << "candidate slots:";

        if (candidate_slots.empty()) {
            std::cout << " <none>";
        }
        else {
            for (const auto slot :
                 candidate_slots) {
                std::cout
                    << ' '
                    << slot;
            }
        }

        std::cout << '\n';

        if (function.signature_parse_error.known()) {
            std::cout << "parse error:   "
                      << *function.signature_parse_error.value
                      << '\n';
        }

        if (match_index + 1 < matches.size()) {
            std::cout << '\n';
        }
    }
}

enum class SearchKind {
    all,
    class_,
    function,
    type,
    global,
    reflection,
};

struct SearchMatch {
    SearchKind kind = SearchKind::all;
    std::size_t index = 0;
};

const char* search_kind_name(SearchKind kind)
{
    switch (kind) {
    case SearchKind::all:
        return "all";
    case SearchKind::class_:
        return "class";
    case SearchKind::function:
        return "function";
    case SearchKind::type:
        return "type";
    case SearchKind::global:
        return "global";
    case SearchKind::reflection:
        return "reflection";
    }

    return "unknown";
}

SearchKind parse_search_kind(std::string_view value)
{
    if (value == "all")
        return SearchKind::all;
    if (value == "class" || value == "classes")
        return SearchKind::class_;
    if (value == "function" || value == "functions")
        return SearchKind::function;
    if (value == "type" || value == "types")
        return SearchKind::type;
    if (value == "global" || value == "globals")
        return SearchKind::global;
    if (value == "reflection" || value == "reflections"
        || value == "descriptor" || value == "descriptors")
        return SearchKind::reflection;

    throw std::runtime_error(
        "search kind must be all, class, function, type, global, or reflection"
    );
}

std::string lowercase_ascii(std::string_view value)
{
    std::string result(value);

    for (auto& c : result) {
        c = static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))
        );
    }

    return result;
}

bool search_matches(
    std::string_view value,
    std::string_view query)
{
    return lowercase_ascii(value).find(
        lowercase_ascii(query)
    ) != std::string::npos;
}

std::string search_match_name(
    const tmfdev::NativeModel& model,
    const SearchMatch& match)
{
    switch (match.kind) {
    case SearchKind::class_:
        return model.classes[match.index].name;
    case SearchKind::function:
        return model.functions[match.index].qualified_name;
    case SearchKind::type:
        return model.types[match.index].name;
    case SearchKind::global:
        return model.globals[match.index].name;
    case SearchKind::reflection: {
        const auto& descriptor = model.reflection_descriptors[match.index];
        return descriptor.name.known()
            ? *descriptor.name.value
            : (descriptor.type_name.known()
                ? *descriptor.type_name.value
                : std::string{});
    }
    case SearchKind::all:
        break;
    }

    return {};
}

std::string search_match_id(
    const tmfdev::NativeModel& model,
    const SearchMatch& match)
{
    switch (match.kind) {
    case SearchKind::class_:
        return model.classes[match.index].id;
    case SearchKind::function:
        return model.functions[match.index].id;
    case SearchKind::type:
        return model.types[match.index].id;
    case SearchKind::global:
        return model.globals[match.index].id;
    case SearchKind::reflection:
        return model.reflection_descriptors[match.index].id;
    case SearchKind::all:
        break;
    }

    return {};
}

void append_search_matches(
    std::vector<SearchMatch>& matches,
    SearchKind requested_kind,
    SearchKind actual_kind,
    const tmfdev::NativeModel& model,
    std::string_view query,
    std::size_t count)
{
    if (requested_kind != SearchKind::all
        && requested_kind != actual_kind) {
        return;
    }

    for (std::size_t index = 0; index < count; ++index) {
        const SearchMatch match{actual_kind, index};
        bool is_match =
            search_matches(search_match_name(model, match), query)
            || search_matches(search_match_id(model, match), query);

        if (!is_match && actual_kind == SearchKind::reflection) {
            const auto& descriptor = model.reflection_descriptors[index];
            is_match = (descriptor.type_name.known()
                    && search_matches(*descriptor.type_name.value, query))
                || (descriptor.category.known()
                    && search_matches(
                        tmfdev::native_reflection_category_name(
                            *descriptor.category.value
                        ),
                        query
                    ))
                || (descriptor.specialization.kind.known()
                    && search_matches(
                        tmfdev::native_reflection_specialized_kind_name(
                            *descriptor.specialization.kind.value
                        ),
                        query
                    ));
        }

        if (is_match) {
            matches.push_back(match);
        }
    }
}

std::string function_owner_name(const std::string& qualified_name)
{
    const auto separator = qualified_name.rfind("::");
    return separator == std::string::npos
        ? std::string{}
        : qualified_name.substr(0, separator);
}

std::size_t parse_search_limit(std::string_view value)
{
    try {
        const auto limit = std::stoull(std::string(value));
        if (limit == 0 || limit > 1000)
            throw std::runtime_error("search limit must be between 1 and 1000");
        return static_cast<std::size_t>(limit);
    }
    catch (const std::invalid_argument&) {
        throw std::runtime_error("search limit must be an integer");
    }
    catch (const std::out_of_range&) {
        throw std::runtime_error("search limit is too large");
    }
}

void write_package_file(
    const std::filesystem::path& path,
    const std::string& text)
{
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error(
            "failed to open package output: "
            + path.string()
        );
    }

    output << text;
    output.close();
}

std::string read_package_file(
    const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "failed to open package file: "
            + path.string()
        );
    }

    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

std::size_t package_json_value_start(
    std::string_view json,
    std::string_view key)
{
    const std::string needle = "\"" + std::string(key) + "\"";
    const auto key_position = json.find(needle);
    if (key_position == std::string_view::npos) {
        throw std::runtime_error(
            "package metadata is missing field: "
            + std::string(key)
        );
    }

    const auto colon_position = json.find(':', key_position + needle.size());
    if (colon_position == std::string_view::npos) {
        throw std::runtime_error(
            "package metadata has malformed field: "
            + std::string(key)
        );
    }

    auto value_position = colon_position + 1;
    while (value_position < json.size()
           && std::isspace(static_cast<unsigned char>(json[value_position]))) {
        ++value_position;
    }

    return value_position;
}

std::string package_json_string(
    std::string_view json,
    std::string_view key)
{
    auto position = package_json_value_start(json, key);
    if (position >= json.size() || json[position] != '"') {
        throw std::runtime_error(
            "package metadata field is not a string: "
            + std::string(key)
        );
    }

    ++position;
    std::string result;

    while (position < json.size()) {
        const auto c = json[position++];
        if (c == '"')
            return result;

        if (c != '\\') {
            result += c;
            continue;
        }

        if (position >= json.size())
            break;

        const auto escaped = json[position++];
        switch (escaped) {
        case '"':
        case '\\':
        case '/':
            result += escaped;
            break;
        case 'b':
            result += '\b';
            break;
        case 'f':
            result += '\f';
            break;
        case 'n':
            result += '\n';
            break;
        case 'r':
            result += '\r';
            break;
        case 't':
            result += '\t';
            break;
        default:
            throw std::runtime_error(
                "package metadata contains an unsupported string escape"
            );
        }
    }

    throw std::runtime_error(
        "package metadata contains an unterminated string: "
        + std::string(key)
    );
}

std::uint64_t package_json_number(
    std::string_view json,
    std::string_view key)
{
    const auto position = package_json_value_start(json, key);
    auto end = position;

    while (end < json.size()
           && std::isdigit(static_cast<unsigned char>(json[end]))) {
        ++end;
    }

    if (end == position) {
        throw std::runtime_error(
            "package metadata field is not an unsigned number: "
            + std::string(key)
        );
    }

    try {
        return std::stoull(std::string(json.substr(position, end - position)));
    }
    catch (const std::exception&) {
        throw std::runtime_error(
            "package metadata number is out of range: "
            + std::string(key)
        );
    }
}

std::string package_class_include_name(
    std::string_view class_name)
{
    std::string result;
    result.reserve(class_name.size());

    for (const auto c : class_name) {
        const auto byte = static_cast<unsigned char>(c);
        if (std::isalnum(byte) || c == '_')
            result += c;
        else
            result += '_';
    }

    return result.empty() ? std::string{"Class"} : result;
}

std::string package_readme(
    const tmfdev::NativeModel& model,
    const std::string& requested_class,
    bool has_runtime)
{
    const auto class_scope = requested_class.empty()
        ? std::string{"whole-model"}
        : "class `" + requested_class + "`";
    const auto sdk_name = requested_class.empty()
        ? std::string{"TMForever_all.hpp"}
        : std::string{"TMForever.hpp"};

    std::ostringstream out;
    out << "# TMForever DevKit package\n\n"
        << "This package was generated for one exact TMForever executable build.\n"
        << "Scope: " << class_scope << ".\n"
        << "Do not mix its declarations or addresses with another game build.\n\n"
        << "## Contents\n\n"
        << "- `include/" << sdk_name << "` - ABI-ready native declarations and resolvers.\n"
        << "- `metadata/TMForever_manifest.json` - machine-readable classes, functions, types, globals, vtables, reflection, evidence, and skips.\n"
        << "- `metadata/search-index.tsv` - compact deterministic index for fast offline discovery.\n"
        << "- `package.json` - package schema, build fingerprint, and artifact summary.\n"
        << "- `tmfdev.cmake` - CMake interface target for the generated include directory.\n\n"
        << "The generated header enforces the build identity at compile time.\n"
        << "Use the manifest and search index to discover the supported surface.\n\n"
        << "Build fingerprint:\n\n"
        << "- SHA-256: `"
        << (model.build.sha256.known()
            ? *model.build.sha256.value
            : "unknown")
        << "`\n"
        << "- Machine: `0x"
        << std::hex << std::uppercase
        << (model.build.machine.known()
            ? *model.build.machine.value
            : 0)
        << "`\n"
        << std::dec;

    if (has_runtime) {
        out << "\nThe package also contains `include/tmfdev/runtime.hpp` and its small "
            << "linked runtime support. Include it from an explicit mod startup "
            << "callback, not from `DllMain`; it validates the exact executable "
            << "build and provides checked RVA resolution, including typed "
            << "`Context::resolve_function<Fn>(rva)`. `Context::game_app()` "
            << "returns a nullable, non-owning pointer to the game-owned global "
            << "after checking the exact build's known CGameApp vtable family. "
            << "`Context::audio_port()` reads the exact reflection-backed "
            << "`CGameApp::AudioPort` field and is also nullable and non-owning; "
            << "`Context::input_port()` likewise reads the exact "
            << "reflection-backed `CGameApp::InputPort` field and is nullable "
            << "and non-owning; "
             << "`Context::block_editor()` invokes the exact, read-only "
             << "`CTrackMania::GetTmBlockEditor` view only when the published "
             << "root has the verified `CTrackMania` vtable; it is nullable and "
             << "does not claim active-editor semantics; "
             << "`Context::playground()` likewise invokes the exact, read-only "
             << "`CTrackMania::GetPlayground` view and accepts verified "
             << "`CGamePlayground`-family vtables; it is nullable, non-owning, "
             << "and should be reacquired rather than cached because the exact "
             << "lifecycle sampler observed both null results and pointer "
             << "replacement across labeled menu/editor/race transitions; "
             << "callers can inspect `file_sha256_verified()` to distinguish a "
            << "matching file hash from in-memory identity-only loader setup. "
            << "`Context::from_module()` also rejects an invalid module base or "
            << "identity inconsistent with the loaded PE header. Neither "
            << "accessor claims a stable lifetime or thread safety. A ready "
            << "context can still return null when a game-owned pointer is "
            << "currently unavailable or fails the runtime readability/vtable "
            << "sanity check; `status()` reports initialization failures such as "
            << "`unsupported_build` and `sha256_mismatch`. The runtime header is "
            << "intentionally lightweight and only forward-declares engine "
            << "classes; include the generated class or broad SDK header "
            << "separately when calling their methods.\n";
    }

    if (!requested_class.empty()) {
        out << "\nThe class header is also available as:\n\n"
            << "```cpp\n"
            << "#include <tmfdev/"
            << package_class_include_name(requested_class)
            << ".hpp>\n"
            << "```\n\n"
            << "The corresponding `tmf::api::<Class>Api` exposes RVA resolvers "
            << "and unambiguous ABI-verified convenience calls. These calls "
            << "accept a native object pointer when required; they do not claim "
            << "a safe C++ object layout or lifecycle.\n";
    }

    return out.str();
}

std::string package_metadata(
    const tmfdev::NativeModel& model,
    const tmfdev::NativeSdkOutput& sdk,
    const std::string& requested_class,
    const std::array<std::string, 5>& artifact_hashes,
    const std::string& class_sdk_hash,
    const std::string& runtime_hash,
    const std::string& runtime_source_hash)
{
    const auto sdk_name = requested_class.empty()
        ? std::string{"TMForever_all.hpp"}
        : std::string{"TMForever.hpp"};

    std::ostringstream out;
    out << "{\n"
        << "  \"schema\": 2,\n"
        << "  \"tool\": \"tmfdev\",\n"
        << "  \"requested_class\": ";

    if (requested_class.empty())
        out << "null,\n";
    else
        out << "\"" << requested_class << "\",\n";

    out
        << "  \"build\": {\n"
        << "    \"sha256\": \""
        << (model.build.sha256.known()
            ? *model.build.sha256.value
            : "")
        << "\",\n"
        << "    \"file_size\": "
        << (model.build.file_size.known()
            ? *model.build.file_size.value
            : 0)
        << ",\n"
        << "    \"machine\": "
        << (model.build.machine.known()
            ? *model.build.machine.value
            : 0)
        << ",\n"
        << "    \"timestamp\": "
        << (model.build.timestamp.known()
            ? *model.build.timestamp.value
            : 0)
        << ",\n"
        << "    \"image_base\": "
        << (model.build.image_base.known()
            ? *model.build.image_base.value
            : 0)
        << ",\n"
        << "    \"image_size\": "
        << (model.build.image_size.known()
            ? *model.build.image_size.value
            : 0)
        << "\n"
        << "  },\n"
        << "  \"artifacts\": {\n"
        << "    \"sdk\": \"include/" << sdk_name << "\",\n"
        << "    \"manifest\": \"metadata/TMForever_manifest.json\",\n"
        << "    \"search_index\": \"metadata/search-index.tsv\",\n"
        << "    \"cmake\": \"tmfdev.cmake\",\n"
        << "    \"readme\": \"README.md\"";

    if (!requested_class.empty()) {
        out << ",\n"
            << "    \"class_sdk\": \"include/tmfdev/"
            << package_class_include_name(requested_class)
            << ".hpp\"";
    }

    if (!runtime_hash.empty()) {
        out << ",\n"
            << "    \"runtime\": \"include/tmfdev/runtime.hpp\",\n"
            << "    \"runtime_source\": \"src/tmfdev_runtime.cpp\"";
    }

    out
        << "\n  },\n"
        << "  \"artifact_sha256\": {\n"
        << "    \"sdk_file\": \"" << artifact_hashes[0] << "\",\n"
        << "    \"manifest_file\": \"" << artifact_hashes[1] << "\",\n"
        << "    \"search_index_file\": \"" << artifact_hashes[2] << "\",\n"
        << "    \"cmake_file\": \"" << artifact_hashes[3] << "\",\n"
        << "    \"readme_file\": \"" << artifact_hashes[4] << "\"";

    if (!class_sdk_hash.empty()) {
        out << ",\n"
            << "    \"class_sdk_file\": \""
            << class_sdk_hash
            << "\"";
    }

    if (!runtime_hash.empty()) {
        out << ",\n"
            << "    \"runtime_file\": \""
            << runtime_hash
            << "\",\n"
            << "    \"runtime_source_file\": \""
            << runtime_source_hash
            << "\"";
    }

    out
        << "\n  },\n"
        << "  \"sdk\": {\n"
        << "    \"emitted_functions\": " << sdk.emitted_functions << ",\n"
        << "    \"skipped_functions\": " << sdk.skipped_functions << "\n"
        << "  }\n"
        << "}\n";

    return out.str();
}

std::string package_cmake(bool has_runtime)
{
    std::ostringstream out;
    out
        << "# Generated by tmfdev.\n"
        "if(NOT TARGET tmfdev::sdk)\n"
        "    add_library(tmfdev_sdk INTERFACE)\n"
        "    target_include_directories(tmfdev_sdk INTERFACE\n"
        "        \"${CMAKE_CURRENT_LIST_DIR}/include\"\n"
        "    )\n"
        "    add_library(tmfdev::sdk ALIAS tmfdev_sdk)\n"
        "endif()\n";

    if (has_runtime) {
        out
            << "\n"
            << "if(NOT WIN32)\n"
            << "    message(FATAL_ERROR \"tmfdev runtime requires Windows\")\n"
            << "endif()\n"
            << "if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)\n"
            << "    message(FATAL_ERROR \"tmfdev runtime requires a 32-bit consumer\")\n"
            << "endif()\n"
            << "if(NOT TARGET tmfdev::runtime)\n"
            << "    add_library(tmfdev_runtime STATIC\n"
            << "        \"${CMAKE_CURRENT_LIST_DIR}/src/tmfdev_runtime.cpp\"\n"
            << "    )\n"
            << "    target_include_directories(tmfdev_runtime PUBLIC\n"
            << "        \"${CMAKE_CURRENT_LIST_DIR}/include\"\n"
            << "    )\n"
            << "    target_link_libraries(tmfdev_runtime PUBLIC bcrypt)\n"
            << "    add_library(tmfdev::runtime ALIAS tmfdev_runtime)\n"
            << "endif()\n";
    }

    return out.str();
}

void generate_package(
    const std::filesystem::path& output_directory,
    const tmfdev::NativeModel& model,
    const std::string& requested_class)
{
    std::filesystem::create_directories(
        output_directory / "include"
    );
    std::filesystem::create_directories(
        output_directory / "metadata"
    );

    const auto sdk = tmfdev::generate_native_sdk_header(model, requested_class);
    const auto runtime = tmfdev::generate_native_runtime_header(model);
    const auto runtime_source = tmfdev::generate_native_runtime_source(model);
    const bool has_runtime = !runtime.text.empty();
    const auto manifest = tmfdev::generate_native_sdk_manifest_json(model, requested_class);
    const auto search_index =
        tmfdev::generate_native_sdk_search_index_tsv(model);

    const auto cmake_text = package_cmake(has_runtime);
    const auto readme_text = package_readme(model, requested_class, has_runtime);
    const auto hash_text = [](const std::string& text) {
        return tmfdev::sha256_bytes(
            std::vector<std::uint8_t>(text.begin(), text.end())
        );
    };

    const auto sdk_filename = requested_class.empty()
        ? std::string{"TMForever_all.hpp"}
        : std::string{"TMForever.hpp"};

    write_package_file(
        output_directory / "include" / sdk_filename,
        sdk.text
    );

    if (!requested_class.empty()) {
        const auto class_include_directory =
            output_directory / "include" / "tmfdev";
        std::filesystem::create_directories(class_include_directory);

        write_package_file(
            class_include_directory
                / (package_class_include_name(requested_class) + ".hpp"),
            sdk.text
        );
    }

    if (has_runtime) {
        const auto runtime_include_directory =
            output_directory / "include" / "tmfdev";
        const auto runtime_source_directory =
            output_directory / "src";
        std::filesystem::create_directories(runtime_include_directory);
        std::filesystem::create_directories(runtime_source_directory);

        write_package_file(
            runtime_include_directory / "runtime.hpp",
            runtime.text
        );
        write_package_file(
            runtime_source_directory / "tmfdev_runtime.cpp",
            runtime_source.text
        );
    }
    write_package_file(
        output_directory / "metadata" / "TMForever_manifest.json",
        manifest.text
    );
    write_package_file(
        output_directory / "metadata" / "search-index.tsv",
        search_index.text
    );
    write_package_file(
        output_directory / "package.json",
        package_metadata(
            model,
            sdk,
            requested_class,
            {{
                hash_text(sdk.text),
                hash_text(manifest.text),
                hash_text(search_index.text),
                hash_text(cmake_text),
                hash_text(readme_text),
            }},
            requested_class.empty() ? std::string{} : hash_text(sdk.text),
            has_runtime ? hash_text(runtime.text) : std::string{},
            has_runtime ? hash_text(runtime_source.text) : std::string{}
        )
    );
    write_package_file(
        output_directory / "tmfdev.cmake",
        cmake_text
    );
    write_package_file(
        output_directory / "README.md",
        readme_text
    );

    std::cout
        << "generated tmfdev package: "
        << output_directory.string() << '\n'
        << "SDK:     include/" << sdk_filename << " ("
        << sdk.emitted_functions << " emitted / "
        << sdk.skipped_functions << " skipped)\n"
        << "manifest: metadata/TMForever_manifest.json\n";

    if (has_runtime)
        std::cout << "runtime:  include/tmfdev/runtime.hpp + src/tmfdev_runtime.cpp\n";
}

void verify_package(
    const std::filesystem::path& package_directory,
    const std::filesystem::path& executable_path,
    const std::filesystem::path& map_path)
{
    if (!std::filesystem::is_directory(package_directory)) {
        throw std::runtime_error(
            "package directory does not exist: "
            + package_directory.string()
        );
    }

    const auto package_json = read_package_file(
        package_directory / "package.json"
    );

    if (package_json_number(package_json, "schema") != 2) {
        throw std::runtime_error(
            "unsupported tmfdev package schema"
        );
    }

    const auto package_sha256 = package_json_string(
        package_json,
        "sha256"
    );
    const auto package_file_size = package_json_number(
        package_json,
        "file_size"
    );
    const auto package_machine = package_json_number(
        package_json,
        "machine"
    );
    const auto package_timestamp = package_json_number(
        package_json,
        "timestamp"
    );
    const auto package_image_base = package_json_number(
        package_json,
        "image_base"
    );
    const auto package_image_size = package_json_number(
        package_json,
        "image_size"
    );

    const auto build = tmfdev::inspect_build(
        executable_path,
        map_path
    );

    if (!build.matches()) {
        print_build(build);
        throw std::runtime_error(
            "supplied executable and MAP do not describe a matching build"
        );
    }

    std::vector<std::string> mismatches;
    if (build.executable.sha256 != package_sha256)
        mismatches.push_back("sha256");
    if (build.executable.file_size != package_file_size)
        mismatches.push_back("file_size");
    if (build.executable.pe.machine != package_machine)
        mismatches.push_back("machine");
    if (build.executable.pe.timestamp != package_timestamp)
        mismatches.push_back("timestamp");
    if (build.executable.pe.image_base != package_image_base)
        mismatches.push_back("image_base");
    if (build.executable.pe.image_size != package_image_size)
        mismatches.push_back("image_size");

    if (!mismatches.empty()) {
        std::ostringstream message;
        message << "package build mismatch:";
        for (const auto& mismatch : mismatches)
            message << ' ' << mismatch;
        throw std::runtime_error(message.str());
    }

    const auto broad_header = package_directory
        / "include" / "TMForever_all.hpp";
    const auto class_header = package_directory
        / "include" / "TMForever.hpp";
    const bool has_broad_header =
        std::filesystem::is_regular_file(broad_header);
    const bool has_class_header =
        std::filesystem::is_regular_file(class_header);

    if (has_broad_header == has_class_header) {
        throw std::runtime_error(
            "package must contain exactly one SDK header: "
            "include/TMForever_all.hpp or include/TMForever.hpp"
        );
    }

    std::optional<std::filesystem::path> class_include_header;
    if (has_class_header) {
        const auto requested_class = package_json_string(
            package_json,
            "requested_class"
        );
        class_include_header = package_directory
            / "include" / "tmfdev"
            / (package_class_include_name(requested_class) + ".hpp");

        if (!std::filesystem::is_regular_file(*class_include_header)) {
            throw std::runtime_error(
                "class package is missing its tmfdev include header: "
                + class_include_header->string()
            );
        }
    }

    const auto runtime_header = package_directory
        / "include" / "tmfdev" / "runtime.hpp";
    const auto runtime_source = package_directory
        / "src" / "tmfdev_runtime.cpp";
    const bool has_runtime_header =
        std::filesystem::is_regular_file(runtime_header);
    const bool has_runtime_source =
        std::filesystem::is_regular_file(runtime_source);

    if (has_runtime_header != has_runtime_source) {
        throw std::runtime_error(
            "package runtime support is incomplete"
        );
    }

    if (has_runtime_header) {
        if (package_json_string(package_json, "runtime")
                != "include/tmfdev/runtime.hpp"
            || package_json_string(package_json, "runtime_source")
                != "src/tmfdev_runtime.cpp") {
            throw std::runtime_error(
                "package runtime metadata does not match its files"
            );
        }
    }

    constexpr std::array<std::string_view, 5> required_artifacts = {
        "README.md",
        "tmfdev.cmake",
        "metadata/TMForever_manifest.json",
        "metadata/search-index.tsv",
        "package.json",
    };

    for (const auto artifact : required_artifacts) {
        if (!std::filesystem::is_regular_file(
                package_directory / artifact)) {
            throw std::runtime_error(
                "package artifact is missing: "
                + (package_directory / artifact).string()
            );
        }
    }

    struct ArtifactHashCheck {
        std::string_view key;
        std::filesystem::path path;
    };

    std::vector<ArtifactHashCheck> artifact_checks = {
        {"sdk_file", has_broad_header ? broad_header : class_header},
        {"manifest_file", package_directory / "metadata" / "TMForever_manifest.json"},
        {"search_index_file", package_directory / "metadata" / "search-index.tsv"},
        {"cmake_file", package_directory / "tmfdev.cmake"},
        {"readme_file", package_directory / "README.md"},
    };

    if (class_include_header.has_value())
        artifact_checks.push_back({"class_sdk_file", *class_include_header});

    if (has_runtime_header) {
        artifact_checks.push_back({"runtime_file", runtime_header});
        artifact_checks.push_back({"runtime_source_file", runtime_source});
    }

    std::vector<std::string> artifact_mismatches;
    for (const auto& artifact : artifact_checks) {
        const auto expected_hash = package_json_string(
            package_json,
            artifact.key
        );
        const auto artifact_text = read_package_file(artifact.path);
        const std::vector<std::uint8_t> artifact_bytes(
            artifact_text.begin(),
            artifact_text.end()
        );
        if (tmfdev::sha256_bytes(artifact_bytes) != expected_hash)
            artifact_mismatches.push_back(std::string(artifact.key));
    }

    if (!artifact_mismatches.empty()) {
        std::ostringstream message;
        message << "package artifact hash mismatch:";
        for (const auto& mismatch : artifact_mismatches)
            message << ' ' << mismatch;
        throw std::runtime_error(message.str());
    }

    std::cout << "TMForever package verification\n";
    std::cout << "==============================\n\n";
    std::cout << "package:      " << package_directory.string() << '\n';
    std::cout << "scope:        "
              << (has_broad_header ? "whole-model" : "class")
              << '\n';
    std::cout << "build sha256:  " << package_sha256 << '\n';
    std::cout << "artifacts:    "
              << (required_artifacts.size() + 1
                  + (class_include_header.has_value() ? 1 : 0)
                  + (has_runtime_header ? 2 : 0))
              << " present\n";
    std::cout << "integrity:    artifact hashes verified\n";
    std::cout << "status:       verified\n";
}

struct PackageIndexRow {
    std::vector<std::string> fields;
};

struct PackageSearchFilters {
    std::string owner;
    std::string abi_status;
    std::string sdk_status;
};

std::vector<std::string> parse_tsv_line(std::string_view line)
{
    std::vector<std::string> fields;
    std::string field;

    for (std::size_t index = 0; index < line.size(); ++index) {
        const auto c = line[index];

        if (c == '\t') {
            fields.push_back(std::move(field));
            field.clear();
            continue;
        }

        if (c == '\\' && index + 1 < line.size()) {
            const auto escaped = line[++index];
            switch (escaped) {
            case '\\':
                field += '\\';
                break;
            case 't':
                field += '\t';
                break;
            case 'r':
                field += '\r';
                break;
            case 'n':
                field += '\n';
                break;
            default:
                field += escaped;
                break;
            }
            continue;
        }

        field += c;
    }

    fields.push_back(std::move(field));
    return fields;
}

std::vector<PackageIndexRow> read_package_index(
    const std::filesystem::path& package_directory)
{
    const auto index_path =
        package_directory / "metadata" / "search-index.tsv";
    std::ifstream input(index_path, std::ios::binary);

    if (!input) {
        throw std::runtime_error(
            "package search index is missing: "
            + index_path.string()
            + "; run generate-package first"
        );
    }

    std::vector<PackageIndexRow> rows;
    std::string line;

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.empty() || line.starts_with('#') || line.starts_with("kind\t"))
            continue;

        auto fields = parse_tsv_line(line);
        if (fields.size() != 9) {
            throw std::runtime_error(
                "package search index has an invalid row"
            );
        }

        rows.push_back({std::move(fields)});
    }

    return rows;
}

void print_package_search_results(
    const std::filesystem::path& package_directory,
    SearchKind kind,
    std::string_view query,
    std::size_t limit,
    const PackageSearchFilters& filters)
{
    const auto rows = read_package_index(package_directory);
    std::vector<const PackageIndexRow*> matches;

    for (const auto& row : rows) {
        const auto row_kind = parse_search_kind(row.fields[0]);
        if (kind != SearchKind::all && kind != row_kind)
            continue;

        if (!filters.owner.empty() && row.fields[3] != filters.owner)
            continue;
        if (!filters.abi_status.empty() && row.fields[6] != filters.abi_status)
            continue;
        if (!filters.sdk_status.empty() && row.fields[7] != filters.sdk_status)
            continue;

        if (search_matches(row.fields[1], query)
            || search_matches(row.fields[2], query)
            || search_matches(row.fields[3], query)
            || search_matches(row.fields[6], query)
            || search_matches(row.fields[7], query)
            || search_matches(row.fields[8], query)) {
            matches.push_back(&row);
        }
    }

    std::sort(
        matches.begin(),
        matches.end(),
        [](const PackageIndexRow* left, const PackageIndexRow* right) {
            const auto left_name = lowercase_ascii(left->fields[2]);
            const auto right_name = lowercase_ascii(right->fields[2]);

            if (left_name != right_name)
                return left_name < right_name;
            if (left->fields[0] != right->fields[0])
                return left->fields[0] < right->fields[0];
            return left->fields[1] < right->fields[1];
        }
    );

    const auto shown = std::min(limit, matches.size());

    std::cout << "TMForever package search\n";
    std::cout << "========================\n\n";
    std::cout << "package:  " << package_directory.string() << '\n';
    std::cout << "kind:     " << search_kind_name(kind) << '\n';
    std::cout << "query:    " << query << '\n';
    if (!filters.owner.empty())
        std::cout << "owner:    " << filters.owner << '\n';
    if (!filters.abi_status.empty())
        std::cout << "ABI:      " << filters.abi_status << '\n';
    if (!filters.sdk_status.empty())
        std::cout << "SDK:      " << filters.sdk_status << '\n';
    std::cout << "matches:  " << matches.size()
              << " (showing " << shown << ")\n\n";

    for (std::size_t position = 0; position < shown; ++position) {
        const auto& fields = matches[position]->fields;
        std::cout << "[" << fields[0] << "] " << fields[2] << '\n'
                  << "  id:       " << fields[1] << '\n';

        if (!fields[3].empty())
            std::cout << "  owner:    " << fields[3] << '\n';
        if (!fields[4].empty())
            std::cout << "  RVA:      " << fields[4] << '\n';
        if (!fields[5].empty())
            std::cout << "  physical: " << fields[5] << '\n';
        if (!fields[6].empty())
            std::cout << "  ABI:      " << fields[6] << '\n';
        if (!fields[7].empty())
            std::cout << "  SDK:      " << fields[7] << '\n';
        if (!fields[8].empty())
            std::cout << "  details:  " << fields[8] << '\n';

        if (position + 1 < shown)
            std::cout << '\n';
    }
}

std::uint32_t parse_rva(std::string_view value)
{
    try {
        const auto text = std::string(value);
        std::size_t consumed = 0;
        const auto number = std::stoull(
            text,
            &consumed,
            text.starts_with("0x") || text.starts_with("0X")
                ? 16
                : 10
        );

        if (consumed != text.size() || number > 0xFFFFFFFFull)
            throw std::runtime_error("RVA must be a 32-bit hexadecimal or decimal value");

        return static_cast<std::uint32_t>(number);
    }
    catch (const std::invalid_argument&) {
        throw std::runtime_error(
            "RVA must be a 32-bit hexadecimal or decimal value"
        );
    }
    catch (const std::out_of_range&) {
        throw std::runtime_error(
            "RVA is too large"
        );
    }
}

void print_physical_code(
    const tmfdev::NativeModel& model,
    std::uint32_t rva)
{
    const auto code_it = std::find_if(
        model.physical_code.begin(),
        model.physical_code.end(),
        [&](const tmfdev::PhysicalCode& code) {
            return code.rva.known() && *code.rva.value == rva;
        }
    );

    if (code_it == model.physical_code.end()) {
        throw std::runtime_error(
            "physical code target not found in canonical model: "
            + std::to_string(rva)
        );
    }

    std::cout << "TMForever physical code target\n";
    std::cout << "==============================\n\n";
    std::cout << "id:                 " << code_it->id << '\n';
    std::cout << "RVA:                ";
    print_hex(rva, 8);
    std::cout << '\n';

    if (code_it->virtual_address.known()) {
        std::cout << "preferred VA:       ";
        print_hex(*code_it->virtual_address.value, 8);
        std::cout << '\n';
    }

    std::cout << "logical functions:  "
              << code_it->logical_function_ids.size()
              << '\n';

    std::vector<const tmfdev::NativeFunction*> logical_functions;
    logical_functions.reserve(code_it->logical_function_ids.size());

    for (const auto& function_id : code_it->logical_function_ids) {
        const auto function_it = std::find_if(
            model.functions.begin(),
            model.functions.end(),
            [&](const tmfdev::NativeFunction& function) {
                return function.id == function_id;
            }
        );

        if (function_it == model.functions.end())
            continue;

        logical_functions.push_back(&*function_it);
    }

    std::sort(
        logical_functions.begin(),
        logical_functions.end(),
        [](const tmfdev::NativeFunction* left,
           const tmfdev::NativeFunction* right) {
            return left->id < right->id;
        }
    );

    for (const auto* function : logical_functions) {

        const auto abi = tmfdev::assess_native_function_abi(
            model,
            *function
        );
        const auto sdk = tmfdev::assess_native_sdk_function(
            model,
            *function
        );

        std::cout << "\n  " << function->qualified_name << '\n'
                  << "    id:       " << function->id << '\n'
                  << "    ABI:      "
                  << tmfdev::native_abi_status_name(abi.status) << '\n'
                  << "    SDK:      "
                  << (sdk.emitted ? "emitted" : "skipped")
                  << '\n';

        if (!sdk.emitted && !sdk.reason.empty()) {
            std::cout << "    SDK skip: "
                      << sdk.reason
                      << '\n';
        }
    }

    std::cout << "\nvtable slots targeting this RVA\n";
    std::cout << "--------------------------------\n";
    struct VtableSlotReference {
        const tmfdev::NativeVtable* vtable = nullptr;
        const tmfdev::NativeVtableSlot* slot = nullptr;
    };
    std::vector<VtableSlotReference> vtable_slots;

    for (const auto& vtable : model.vtables) {
        for (const auto& slot : vtable.slots) {
            if (!slot.target_rva.known()
                || *slot.target_rva.value != rva) {
                continue;
            }

            vtable_slots.push_back({&vtable, &slot});
        }
    }

    std::sort(
        vtable_slots.begin(),
        vtable_slots.end(),
        [](const VtableSlotReference& left,
           const VtableSlotReference& right) {
            if (left.vtable->id != right.vtable->id)
                return left.vtable->id < right.vtable->id;
            return left.slot->index < right.slot->index;
        }
    );

    for (const auto& reference : vtable_slots) {
        const auto& vtable = *reference.vtable;
        const auto& slot = *reference.slot;

        std::cout << "  " << vtable.id
                  << " slot " << slot.index;

        if (slot.resolved_function_id.known()) {
            std::cout << " -> "
                      << *slot.resolved_function_id.value;
        }
        else if (!slot.candidate_function_ids.empty()) {
            std::cout << " ("
                      << slot.candidate_function_ids.size()
                      << " candidate logical functions)";
        }

        std::cout << '\n';
    }

    if (vtable_slots.empty())
        std::cout << "<none>\n";
}

void print_search_results(
    const tmfdev::NativeModel& model,
    SearchKind kind,
    std::string_view query,
    std::size_t limit)
{
    std::vector<SearchMatch> matches;

    append_search_matches(
        matches, kind, SearchKind::class_, model, query, model.classes.size()
    );
    append_search_matches(
        matches, kind, SearchKind::function, model, query, model.functions.size()
    );
    append_search_matches(
        matches, kind, SearchKind::type, model, query, model.types.size()
    );
    append_search_matches(
        matches, kind, SearchKind::global, model, query, model.globals.size()
    );
    append_search_matches(
        matches,
        kind,
        SearchKind::reflection,
        model,
        query,
        model.reflection_descriptors.size()
    );

    std::sort(
        matches.begin(),
        matches.end(),
        [&](const SearchMatch& left, const SearchMatch& right) {
            const auto left_name = lowercase_ascii(
                search_match_name(model, left)
            );
            const auto right_name = lowercase_ascii(
                search_match_name(model, right)
            );

            if (left_name != right_name)
                return left_name < right_name;

            const auto left_kind = static_cast<int>(left.kind);
            const auto right_kind = static_cast<int>(right.kind);
            if (left_kind != right_kind)
                return left_kind < right_kind;

            return search_match_id(model, left)
                < search_match_id(model, right);
        }
    );

    const auto shown = std::min(limit, matches.size());

    std::cout << "TMForever search\n";
    std::cout << "================\n\n";
    std::cout << "kind:     " << search_kind_name(kind) << '\n';
    std::cout << "query:    " << query << '\n';
    std::cout << "matches:  " << matches.size()
              << " (showing " << shown << ")\n\n";

    for (std::size_t position = 0; position < shown; ++position) {
        const auto& match = matches[position];
        std::cout << "[" << search_kind_name(match.kind) << "] ";

        switch (match.kind) {
        case SearchKind::class_: {
            const auto& native_class = model.classes[match.index];
            std::cout << native_class.name << '\n'
                      << "  id:                " << native_class.id << '\n'
                      << "  GameBox class ID:  ";

            if (native_class.mw_class_id.known())
                print_hex(*native_class.mw_class_id.value, 8);
            else
                std::cout << "<unknown>";

            std::cout << '\n'
                      << "  direct functions:  " << native_class.function_ids.size() << '\n'
                      << "  reflection members: " << native_class.member_ids.size() << '\n'
                      << "  reflection descriptors: "
                      << native_class.reflection_descriptor_ids.size() << '\n'
                      << "  RTTI entries:       " << native_class.rtti_hierarchy.size() << '\n'
                      << "  vtable:             ";

            if (native_class.vtable_state.known()) {
                std::cout << tmfdev::native_vtable_state_name(
                    *native_class.vtable_state.value
                );
            }
            else {
                std::cout << "unknown";
            }

            std::cout << '\n';
            break;
        }

        case SearchKind::function: {
            const auto& function = model.functions[match.index];
            std::cout << function.qualified_name << '\n'
                      << "  id:       " << function.id << '\n'
                      << "  owner:    " << function_owner_name(function.qualified_name) << '\n'
                      << "  RVA:      ";

            if (function.rva.known())
                print_hex(*function.rva.value, 8);
            else
                std::cout << "<unknown>";

            std::cout << '\n'
                      << "  physical: ";

            if (function.physical_code_id.known())
                std::cout << *function.physical_code_id.value;
            else
                std::cout << "<unknown>";

            const auto abi = tmfdev::assess_native_function_abi(
                model,
                function
            );
            const auto sdk = tmfdev::assess_native_sdk_function(
                model,
                function
            );

            std::cout << '\n'
                      << "  ABI:      "
                      << tmfdev::native_abi_status_name(abi.status) << '\n'
                      << "  SDK:      "
                      << (sdk.emitted ? "emitted" : "skipped");

            if (!sdk.emitted && !sdk.reason.empty())
                std::cout << " (" << sdk.reason << ")";

            std::cout << '\n';
            break;
        }

        case SearchKind::type: {
            const auto& type = model.types[match.index];
            std::cout << type.name << '\n'
                      << "  id:     " << type.id << '\n'
                      << "  kind:   ";

            if (type.kind.known())
                std::cout << tmfdev::native_type_kind_name(*type.kind.value);
            else
                std::cout << "unknown";

            std::cout << '\n'
                      << "  size:   ";

            if (type.size.known())
                std::cout << *type.size.value << " bytes";
            else
                std::cout << "unknown";

            std::cout << '\n'
                      << "  fields: " << type.fields.size() << '\n';
            break;
        }

        case SearchKind::global: {
            const auto& global = model.globals[match.index];
            std::cout << global.name << '\n'
                      << "  id:   " << global.id << '\n'
                      << "  type: ";

            if (global.type.known())
                std::cout << *global.type.value;
            else
                std::cout << "address-only / unknown";

            std::cout << '\n'
                      << "  RVA:  ";

            if (global.rva.known())
                print_hex(*global.rva.value, 8);
            else
                std::cout << "<unknown>";

            std::cout << '\n';
            break;
        }

        case SearchKind::reflection: {
            const auto& descriptor = model.reflection_descriptors[match.index];
            std::cout
                << (descriptor.name.known()
                    ? *descriptor.name.value
                    : (descriptor.type_name.known()
                        ? *descriptor.type_name.value
                        : std::string{"<unnamed>"}))
                << '\n'
                << "  id:          " << descriptor.id << '\n'
                << "  owner:       " << descriptor.owner_class_name << '\n'
                << "  index:       " << descriptor.index << '\n'
                << "  type:        ";

            if (descriptor.type_name.known())
                std::cout << *descriptor.type_name.value;
            else
                std::cout << "unknown";

            std::cout << '\n'
                      << "  category:    ";
            if (descriptor.category.known())
                std::cout << tmfdev::native_reflection_category_name(
                    *descriptor.category.value
                );
            else
                std::cout << "unknown";

            std::cout << '\n'
                      << "  offset:      ";
            if (descriptor.offset.known())
                std::cout << *descriptor.offset.value;
            else
                std::cout << "unknown";

            std::cout << '\n'
                      << "  physical:    ";
            if (descriptor.physical_storage.known())
                std::cout << (*descriptor.physical_storage.value
                    ? "yes"
                    : "no");
            else
                std::cout << "unknown";

            std::cout << '\n'
                      << "  specialized: ";
            if (descriptor.specialization.kind.known())
                std::cout << tmfdev::native_reflection_specialized_kind_name(
                    *descriptor.specialization.kind.value
                );
            else
                std::cout << "unknown";
            std::cout << '\n';
            break;
        }

        case SearchKind::all:
            break;
        }

        if (position + 1 < shown)
            std::cout << '\n';
    }
}

void print_usage()
{
    std::cerr
        << "usage:\n"
        << "  tmfdev inspect-exe <TmForever.exe>\n"
        << "  tmfdev inspect-build <TmForever.exe> <TmForever.map>\n"
        << "  tmfdev search <kind> <query> <TmForever.exe> <TmForever.map> [--limit <n>]\n"
        << "  tmfdev search-package <package-directory> <kind> <query> [--limit <n>] [--owner <class>] [--abi <status>] [--sdk <status>]\n"
        << "  tmfdev inspect-physical <rva> <TmForever.exe> <TmForever.map>\n"
        << "  tmfdev inspect-class <class> <TmForever.exe> <TmForever.map>\n"
        << "  tmfdev inspect-vtable <class> <TmForever.exe> <TmForever.map>\n"
        << "  tmfdev inspect-rtti <class> <TmForever.exe> <TmForever.map>\n"
        << "  tmfdev inspect-reflection <class> <TmForever.exe> <TmForever.map>\n"
        << "  tmfdev inspect-model <class> <TmForever.exe> <TmForever.map>\n"
        << "  tmfdev inspect-abi <class> <TmForever.exe> <TmForever.map>\n"
        << "  tmfdev audit-all <TmForever.exe> <TmForever.map>\n"
        << "  tmfdev verify-package <package-directory> <TmForever.exe> <TmForever.map>\n"
        << "  tmfdev generate-package <output-directory> <TmForever.exe> <TmForever.map>\n"
        << "  tmfdev generate-class-package <class> <output-directory> <TmForever.exe> <TmForever.map>\n"
        << "  tmfdev generate-sdk <class> <output.hpp> <TmForever.exe> <TmForever.map>\n"
        << "  tmfdev generate-sdk-cached <class> <output.hpp> <TmForever.exe> <TmForever.map>\n"
        << "  tmfdev generate-sdk-all-cached <output.hpp> <TmForever.exe> <TmForever.map>\n"
        << "  tmfdev generate-runtime <output-directory> <TmForever.exe> <TmForever.map>\n"
        << "  tmfdev generate-manifest <class> <output.json> <TmForever.exe> <TmForever.map>\n"
        << "  tmfdev generate-manifest-all-cached <output.json> <TmForever.exe> <TmForever.map>\n"
        << "  tmfdev inspect-function <class> <function> <TmForever.exe> <TmForever.map>\n";
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc < 2) {
            print_usage();
            return 1;
        }

        const std::string_view command =
            argv[1];

        if (command == "inspect-exe") {
            if (argc != 3) {
                print_usage();
                return 1;
            }

            print_executable(
                tmfdev::inspect_executable(
                    argv[2]
                )
            );

            return 0;
        }

        if (command == "search") {
            if (argc != 6
                && (argc != 8 || std::string_view(argv[6]) != "--limit")) {
                print_usage();
                return 1;
            }

            const auto kind = parse_search_kind(argv[2]);
            const auto limit = argc == 8
                ? parse_search_limit(argv[7])
                : 25;

            const auto build =
                tmfdev::inspect_build(
                    argv[4],
                    argv[5]
                );

            const auto model =
                tmfdev::build_native_model_all_fast(build);

            print_search_results(
                model,
                kind,
                argv[3],
                limit
            );

            return 0;
        }

        if (command == "search-package") {
            if (argc < 5) {
                print_usage();
                return 1;
            }

            const auto kind = parse_search_kind(argv[3]);
            std::size_t limit = 25;
            PackageSearchFilters filters;

            for (int index = 5; index < argc; ++index) {
                const auto option = std::string_view(argv[index]);
                if (index + 1 >= argc) {
                    throw std::runtime_error(
                        "search-package option requires a value: "
                        + std::string(option)
                    );
                }

                const auto value = std::string(argv[++index]);
                if (option == "--limit") {
                    limit = parse_search_limit(value);
                }
                else if (option == "--owner") {
                    filters.owner = value;
                }
                else if (option == "--abi") {
                    filters.abi_status = value;
                }
                else if (option == "--sdk") {
                    filters.sdk_status = value;
                }
                else {
                    throw std::runtime_error(
                        "unknown search-package option: "
                        + std::string(option)
                    );
                }
            }

            print_package_search_results(
                std::filesystem::path(argv[2]),
                kind,
                argv[4],
                limit,
                filters
            );

            return 0;
        }

        if (command == "inspect-physical") {
            if (argc != 5) {
                print_usage();
                return 1;
            }

            const auto build =
                tmfdev::inspect_build(
                    argv[3],
                    argv[4]
                );
            const auto model =
                tmfdev::build_native_model_all_fast(build);

            print_physical_code(
                model,
                parse_rva(argv[2])
            );

            return 0;
        }

        if (command == "audit-all") {
            if (argc != 4
                && (argc != 5 || std::string_view(argv[4]) != "--details")) {
                print_usage();
                return 1;
            }

            const bool print_details = argc == 5;

            const auto build =
                tmfdev::inspect_build(
                    argv[2],
                    argv[3]
                );

            const auto model =
                tmfdev::build_native_model_all_fast(build);

            std::cout << "TMForever whole-model audit\n";
            std::cout << "===========================\n\n";
            std::cout << "classes:                 " << model.classes.size() << '\n';
            std::cout << "logical functions:       " << model.functions.size() << '\n';
            std::cout << "physical code targets:   " << model.physical_code.size() << '\n';

            std::size_t vtable_valid = 0;
            std::size_t vtable_malformed = 0;
            std::size_t vtable_absent = 0;
            std::size_t rtti_classes = 0;
            std::size_t reflection_classes = 0;
            std::size_t reflection_physical_members = 0;
            std::size_t reflection_actions = 0;
            std::size_t reflection_procedures = 0;
            std::size_t reflection_virtual_parameters = 0;

            for (const auto& native_class : model.classes) {
                if (native_class.vtable_state.known()) {
                    switch (*native_class.vtable_state.value) {
                    case tmfdev::NativeVtableState::valid:
                        ++vtable_valid;
                        break;
                    case tmfdev::NativeVtableState::malformed:
                        ++vtable_malformed;
                        break;
                    case tmfdev::NativeVtableState::absent:
                        ++vtable_absent;
                        break;
                    }
                }

                if (!native_class.rtti_hierarchy.empty())
                    ++rtti_classes;

                if (!native_class.member_ids.empty())
                    ++reflection_classes;
            }

            for (const auto& descriptor : model.reflection_descriptors) {
                if (!descriptor.category.known())
                    continue;

                switch (*descriptor.category.value) {
                case tmfdev::NativeReflectionCategory::physical_member:
                    ++reflection_physical_members;
                    break;
                case tmfdev::NativeReflectionCategory::action:
                    ++reflection_actions;
                    break;
                case tmfdev::NativeReflectionCategory::procedure:
                    ++reflection_procedures;
                    break;
                case tmfdev::NativeReflectionCategory::virtual_parameter:
                    ++reflection_virtual_parameters;
                    break;
                case tmfdev::NativeReflectionCategory::unknown:
                    break;
                }
            }

            std::cout << "vtable evidence:        "
                      << vtable_valid << " valid / "
                      << vtable_malformed << " malformed / "
                      << vtable_absent << " absent\n";
            std::cout << "RTTI hierarchies:       " << rtti_classes << " classes\n";
            std::cout << "reflection members:     " << reflection_classes << " classes / "
                      << model.members.size() << " members\n";
            std::cout << "reflection descriptors: "
                      << model.reflection_descriptors.size()
                      << " total ("
                      << reflection_physical_members << " physical, "
                      << reflection_actions << " actions, "
                      << reflection_procedures << " procedures, "
                      << reflection_virtual_parameters
                      << " virtual parameters)\n\n";
            print_abi_report(model);

            if (print_details) {
                std::cout << "\nfunction ABI details\n";
                std::cout << "---------------------\n";

                for (const auto& function : model.functions) {
                    const auto assessment =
                        tmfdev::assess_native_function_abi(model, function);

                    for (const auto& issue : assessment.issues) {
                        std::cout
                            << "["
                            << tmfdev::native_abi_issue_level_name(issue.level)
                            << "] "
                            << function.id
                            << "  "
                            << issue.subject
                            << ": "
                            << issue.detail;

                        if (issue.subject == "return type"
                            && function.return_type.known()) {
                            std::cout
                                << "  declaration='"
                                << *function.return_type.value
                                << "'";
                        }
                        else if (issue.subject.starts_with("parameter ")) {
                            const auto index_text = issue.subject.substr(10);
                            try {
                                const auto index = static_cast<std::size_t>(
                                    std::stoul(index_text)
                                );

                                if (index < function.parameters.size()
                                    && function.parameters[index].type.known()) {
                                    std::cout
                                        << "  declaration='"
                                        << *function.parameters[index].type.value
                                        << "'";
                                }
                            }
                            catch (const std::exception&) {
                                // The issue subject is diagnostic output only.
                            }
                        }

                        std::cout << '\n';
                    }
                }
            }

            return 0;
        }

        if (command == "inspect-build") {
            if (argc != 4) {
                print_usage();
                return 1;
            }

            const auto info =
                tmfdev::inspect_build(
                    argv[2],
                    argv[3]
                );

            print_build(info);

            return info.matches()
                ? 0
                : 2;
        }

        if (command == "generate-package") {
            if (argc != 5) {
                print_usage();
                return 1;
            }

            const auto output_directory =
                std::filesystem::path(argv[2]);

            if (std::filesystem::exists(output_directory)
                && !std::filesystem::is_directory(output_directory)) {
                throw std::runtime_error(
                    "package output path is not a directory: "
                    + output_directory.string()
                );
            }

            const auto build =
                tmfdev::inspect_build(
                    argv[3],
                    argv[4]
                );

            const auto model =
                tmfdev::build_native_model_all_fast(build);

            generate_package(output_directory, model, {});
            return 0;
        }

        if (command == "verify-package") {
            if (argc != 5) {
                print_usage();
                return 1;
            }

            verify_package(
                argv[2],
                argv[3],
                argv[4]
            );
            return 0;
        }

        if (command == "generate-class-package") {
            if (argc != 6) {
                print_usage();
                return 1;
            }

            const auto output_directory =
                std::filesystem::path(argv[3]);

            if (std::filesystem::exists(output_directory)
                && !std::filesystem::is_directory(output_directory)) {
                throw std::runtime_error(
                    "package output path is not a directory: "
                    + output_directory.string()
                );
            }

            const auto build =
                tmfdev::inspect_build(
                    argv[4],
                    argv[5]
                );

            const auto model =
                tmfdev::build_native_model(
                    build,
                    argv[2]
                );

            generate_package(
                output_directory,
                model,
                argv[2]
            );
            return 0;
        }

        if (command == "generate-sdk"
            || command == "generate-sdk-cached") {
            if (argc != 6) {
                print_usage();
                return 1;
            }

            const bool use_cached_model =
                command == "generate-sdk-cached";

            const auto build =
                tmfdev::inspect_build(
                    argv[4],
                    argv[5]
                );

            const auto model = use_cached_model
                ? tmfdev::build_native_model_all_fast(build)
                : tmfdev::build_native_model(build, argv[2]);

            const auto sdk =
                tmfdev::generate_native_sdk_header(
                    model,
                    argv[2]
                );

            std::ofstream output(
                argv[3],
                std::ios::binary
            );

            if (!output) {
                throw std::runtime_error(
                    std::string{"failed to open SDK output: "}
                    + argv[3]
                );
            }

            output << sdk.text;
            output.close();

            std::cout
                << "generated SDK header: " << argv[3] << '\n'
                << "emitted functions:    " << sdk.emitted_functions << '\n'
                << "skipped functions:    " << sdk.skipped_functions << '\n';

            if (!sdk.skips.empty()) {
                std::cout << "\nskipped\n-------\n";

                for (const auto& skip : sdk.skips) {
                    std::cout
                        << skip.function_name
                        << ": "
                        << skip.reason
                        << '\n';
                }
            }

            return 0;
        }

        if (command == "generate-sdk-all-cached") {
            if (argc != 5) {
                print_usage();
                return 1;
            }

            const auto build =
                tmfdev::inspect_build(
                    argv[3],
                    argv[4]
                );

            const auto model =
                tmfdev::build_native_model_all_fast(build);

            const auto sdk =
                tmfdev::generate_native_sdk_header(
                    model,
                    {}
                );

            std::ofstream output(
                argv[2],
                std::ios::binary
            );

            if (!output) {
                throw std::runtime_error(
                    std::string{"failed to open SDK output: "}
                    + argv[2]
                );
            }

            output << sdk.text;
            output.close();

            std::cout
                << "generated SDK header: " << argv[2] << '\n'
                << "emitted functions:    " << sdk.emitted_functions << '\n'
                << "skipped functions:    " << sdk.skipped_functions << '\n';

            if (!sdk.skips.empty()) {
                std::cout << "\nskipped\n-------\n";

                for (const auto& skip : sdk.skips) {
                    std::cout
                        << skip.function_name
                        << ": "
                        << skip.reason
                        << '\n';
                }
            }

            return 0;
        }

        if (command == "generate-runtime") {
            if (argc != 5) {
                print_usage();
                return 1;
            }

            const auto output_directory =
                std::filesystem::path(argv[2]);
            const auto build =
                tmfdev::inspect_build(argv[3], argv[4]);
            const auto model =
                tmfdev::build_native_model_all_fast(build);
            const auto runtime = tmfdev::generate_native_runtime_header(model);

            if (runtime.text.empty()) {
                throw std::runtime_error(
                    "the model has no verified CGameApp global for runtime support"
                );
            }

            const auto runtime_source =
                tmfdev::generate_native_runtime_source(model);
            std::filesystem::create_directories(
                output_directory / "tmfdev"
            );

            std::ofstream header(
                output_directory / "tmfdev" / "runtime.hpp",
                std::ios::binary
            );
            if (!header)
                throw std::runtime_error("failed to open runtime header output");
            header << runtime.text;

            std::ofstream source(
                output_directory / "tmfdev_runtime.cpp",
                std::ios::binary
            );
            if (!source)
                throw std::runtime_error("failed to open runtime source output");
            source << runtime_source.text;

            std::cout
                << "generated runtime support: "
                << output_directory.string() << '\n'
                << "header: "
                << (output_directory / "tmfdev" / "runtime.hpp").string()
                << '\n'
                << "source: "
                << (output_directory / "tmfdev_runtime.cpp").string()
                << '\n';
            return 0;
        }

        if (command == "generate-manifest"
            || command == "generate-manifest-all-cached") {
            const bool use_cached_model =
                command == "generate-manifest-all-cached";

            if ((!use_cached_model && argc != 6)
                || (use_cached_model && argc != 5)) {
                print_usage();
                return 1;
            }

            const auto output_path = use_cached_model ? argv[2] : argv[3];
            const auto exe_path = use_cached_model ? argv[3] : argv[4];
            const auto map_path = use_cached_model ? argv[4] : argv[5];

            const auto build =
                tmfdev::inspect_build(exe_path, map_path);

            const auto model = use_cached_model
                ? tmfdev::build_native_model_all_fast(build)
                : tmfdev::build_native_model(build, argv[2]);

            const auto manifest =
                tmfdev::generate_native_sdk_manifest_json(
                    model,
                    use_cached_model ? std::string{} : argv[2]
                );

            std::ofstream output(output_path, std::ios::binary);
            if (!output) {
                throw std::runtime_error(
                    std::string{"failed to open manifest output: "}
                    + output_path
                );
            }

            output << manifest.text;
            output.close();

            std::cout
                << "generated SDK manifest: " << output_path << '\n';

            return 0;
        }

        if (command == "inspect-function") {
            if (argc != 6) {
                print_usage();
                return 1;
            }

            const auto build =
                tmfdev::inspect_build(
                    argv[4],
                    argv[5]
                );

            const auto model =
                tmfdev::build_native_model(
                    build,
                    argv[2]
                );

            print_function(
                model,
                argv[2],
                argv[3]
            );

            return 0;
        }

        if (
            command == "inspect-class"
            || command == "inspect-vtable"
            || command == "inspect-rtti"
            || command == "inspect-reflection"
            || command == "inspect-model"
            || command == "inspect-abi"
        ) {
            if (argc != 5) {
                print_usage();
                return 1;
            }

            const auto build =
                tmfdev::inspect_build(
                    argv[3],
                    argv[4]
                );

            if (command == "inspect-class") {
                print_class(
                    tmfdev::inspect_class(
                        build,
                        argv[2]
                    )
                );
            }
            else if (command == "inspect-vtable") {
                print_vtable(
                    tmfdev::inspect_vtable(
                        build,
                        argv[2]
                    )
                );
            }
            else if (command == "inspect-rtti") {
                print_rtti(
                    tmfdev::inspect_rtti(
                        build,
                        argv[2]
                    )
                );
            }
            else if (command == "inspect-reflection") {
                print_reflection(
                    tmfdev::inspect_reflection(
                        build,
                        argv[2]
                    )
                );
            }
            else {
                const auto model =
                    tmfdev::build_native_model(
                        build,
                        argv[2]
                    );

                if (command == "inspect-abi") {
                    print_abi_report(model);
                }
                else {
                    print_model(model);
                }
            }

            return 0;
        }

        std::cerr
            << "unknown command: "
            << command
            << "\n\n";

        print_usage();
        return 1;
    }
    catch (const std::exception& e) {
        std::cerr
            << "error: "
            << e.what()
            << '\n';

        return 1;
    }
}
