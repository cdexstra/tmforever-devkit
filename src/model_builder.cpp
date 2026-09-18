#include "model_builder.hpp"

#include "class.hpp"
#include "demangle.hpp"
#include "reflection.hpp"
#include "rtti.hpp"
#include "signature.hpp"
#include "vtable.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <set>

namespace tmfdev {
namespace {

template <typename T>
Fact<T> make_fact(
    T value,
    EvidenceSource source,
    Confidence confidence,
    std::string detail)
{
    Fact<T> fact;

    fact.value = std::move(value);

    fact.evidence.push_back({
        source,
        confidence,
        std::move(detail),
    });

    return fact;
}

std::string hex_id(std::uint32_t value)
{
    std::ostringstream out;

    out
        << std::hex
        << std::uppercase
        << std::setw(8)
        << std::setfill('0')
        << value;

    return out.str();
}

std::string class_id(const std::string& name)
{
    return "class:" + name;
}

std::string function_id(
    const std::string& name,
    std::uint32_t rva)
{
    return "function:"
        + name
        + "@"
        + hex_id(rva);
}

std::string physical_code_id(std::uint32_t rva)
{
    return "code:" + hex_id(rva);
}

std::string direct_owner_from_qualified_name(
    const std::string& qualified_name)
{
    const auto separator = qualified_name.rfind("::");
    if (separator == std::string::npos)
        return {};

    const auto owner_start = qualified_name.rfind(' ', separator);
    return qualified_name.substr(
        owner_start == std::string::npos
            ? 0
            : owner_start + 1,
        separator - (
            owner_start == std::string::npos
                ? 0
                : owner_start + 1
        )
    );
}

std::string vtable_owner_from_symbol(
    const MapSymbol& symbol)
{
    const auto demangled = demangle_msvc(symbol.name);
    const auto marker = "::`vftable'";
    const auto marker_position = demangled.rfind(marker);

    if (marker_position == std::string::npos)
        return {};

    auto owner = demangled.substr(0, marker_position);

    while (owner.starts_with("const ")
        || owner.starts_with("volatile ")
        || owner.starts_with("struct ")
        || owner.starts_with("class ")
        || owner.starts_with("union ")) {
        const auto space = owner.find(' ');
        owner.erase(0, space + 1);
    }

    while (!owner.empty()
        && std::isspace(
            static_cast<unsigned char>(owner.back())
        ) != 0) {
        owner.pop_back();
    }

    return owner;
}

std::string reflection_owner_from_symbol(
    const MapSymbol& symbol)
{
    const std::string_view marker = "m_ParamInfos@";
    const auto marker_position = symbol.name.find(marker);
    if (marker_position == std::string::npos)
        return {};

    const auto owner_start = marker_position + marker.size();
    const auto owner_end = symbol.name.find("@@", owner_start);
    if (owner_end == std::string::npos
        || owner_end <= owner_start) {
        return {};
    }

    return symbol.name.substr(
        owner_start,
        owner_end - owner_start
    );
}

std::string demangled_reflection_owner(
    const MapSymbol& symbol)
{
    const auto demangled = demangle_msvc(symbol.name);
    const auto marker = "::m_ParamInfos";
    const auto marker_position = demangled.rfind(marker);
    if (marker_position == std::string::npos)
        return {};

    const auto prefix = demangled.substr(0, marker_position);
    const auto space = prefix.rfind(' ');
    if (space == std::string::npos)
        return prefix;

    return prefix.substr(space + 1);
}

std::string member_id(
    const std::string& class_name,
    const std::string& member_name,
    std::size_t index)
{
    return "member:"
        + class_name
        + "::"
        + member_name
        + "#"
        + std::to_string(index);
}

std::string reflection_descriptor_id(
    const std::string& class_name,
    std::uint32_t parameter_id,
    std::size_t index)
{
    return "reflection:"
        + class_name
        + "@"
        + hex_id(parameter_id)
        + "#"
        + std::to_string(index);
}

NativeReflectionCategory reflection_category(
    const ReflectionParam& param)
{
    if (param.type_code == 0)
        return NativeReflectionCategory::action;

    if (param.type_code == 65)
        return NativeReflectionCategory::procedure;

    if (param.offset >= 0)
        return NativeReflectionCategory::physical_member;

    if (std::string_view(reflection_param_type_name(param.type_code))
        != "unknown") {
        return NativeReflectionCategory::virtual_parameter;
    }

    return NativeReflectionCategory::unknown;
}

NativeReflectionSpecializedKind reflection_specialized_kind(
    std::uint32_t type_code)
{
    switch (type_code) {
    case 0: return NativeReflectionSpecializedKind::action;
    case 5: return NativeReflectionSpecializedKind::class_;
    case 13: return NativeReflectionSpecializedKind::enum_;
    case 65: return NativeReflectionSpecializedKind::procedure;
    case 18:
    case 35:
    case 40:
        return NativeReflectionSpecializedKind::range;
    case 49: return NativeReflectionSpecializedKind::vec2;
    case 53: return NativeReflectionSpecializedKind::vec3;
    case 57: return NativeReflectionSpecializedKind::vec4;
    default:
        break;
    }

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
        return NativeReflectionSpecializedKind::array;
    default:
        break;
    }

    if (type_code <= 65)
        return NativeReflectionSpecializedKind::none;

    return NativeReflectionSpecializedKind::unknown;
}

NativeReflectionDescriptor make_reflection_descriptor(
    const std::string& class_name,
    const ReflectionParam& param)
{
    const auto category = reflection_category(param);
    NativeReflectionDescriptor descriptor;

    descriptor.id = reflection_descriptor_id(
        class_name,
        param.id,
        param.index
    );
    descriptor.owner_class_id = class_id(class_name);
    descriptor.owner_class_name = class_name;
    descriptor.index = param.index;

    descriptor.record_virtual_address = make_fact(
        param.record_virtual_address,
        EvidenceSource::reflection,
        Confidence::verified,
        "GameBox reflection parameter record address"
    );
    if (param.has_known_size) {
        descriptor.record_size = make_fact(
            param.record_size,
            EvidenceSource::reflection,
            Confidence::high,
            "distance to the next reflection parameter record"
        );
    }
    descriptor.parameter_id = make_fact(
        param.id,
        EvidenceSource::reflection,
        Confidence::verified,
        "GameBox reflection record +0x04"
    );
    descriptor.parameter_virtual_address = make_fact(
        param.param_virtual_address,
        EvidenceSource::reflection,
        Confidence::verified,
        "GameBox reflection record +0x08"
    );
    if (!param.name.empty()) {
        descriptor.name = make_fact(
            param.name,
            EvidenceSource::reflection,
            Confidence::verified,
            "GameBox reflection record name"
        );
    }
    descriptor.type_code = make_fact(
        param.type_code,
        EvidenceSource::reflection,
        Confidence::verified,
        "GameBox reflection type code"
    );
    descriptor.type_name = make_fact(
        std::string(reflection_param_type_name(param.type_code)),
        EvidenceSource::reflection,
        Confidence::high,
        "GameBox reflection type-code table"
    );
    descriptor.offset = make_fact(
        param.offset,
        EvidenceSource::reflection,
        Confidence::verified,
        "GameBox reflection record +0x0C"
    );
    descriptor.flags1 = make_fact(
        param.flags1,
        EvidenceSource::reflection,
        Confidence::verified,
        "GameBox reflection record +0x14"
    );
    descriptor.flags2 = make_fact(
        param.flags2,
        EvidenceSource::reflection,
        Confidence::verified,
        "GameBox reflection record +0x18"
    );
    descriptor.category = make_fact(
        category,
        EvidenceSource::reflection,
        Confidence::high,
        category == NativeReflectionCategory::physical_member
            ? "nonnegative reflection offset denotes physical storage"
            : "reflection type and offset classify a non-field descriptor"
    );
    descriptor.physical_storage = make_fact(
        category == NativeReflectionCategory::physical_member,
        EvidenceSource::reflection,
        Confidence::high,
        "only named nonnegative reflection members project to physical storage"
    );

    const auto specialized_kind = reflection_specialized_kind(param.type_code);
    descriptor.specialization.kind = make_fact(
        specialized_kind,
        EvidenceSource::reflection,
        Confidence::high,
        "GameBox reflection specialized record layout"
    );

    if (param.specialized_name_virtual_address.has_value()) {
        descriptor.specialization.name_virtual_address = make_fact(
            *param.specialized_name_virtual_address,
            EvidenceSource::reflection,
            Confidence::verified,
            "specialized reflection payload name pointer"
        );
    }
    if (!param.specialized_name.empty()) {
        descriptor.specialization.name = make_fact(
            param.specialized_name,
            EvidenceSource::reflection,
            Confidence::verified,
            "specialized reflection payload name"
        );
    }
    if (param.specialized_class_info_virtual_address.has_value()) {
        descriptor.specialization.class_info_virtual_address = make_fact(
            *param.specialized_class_info_virtual_address,
            EvidenceSource::reflection,
            Confidence::verified,
            "specialized reflection class-info pointer"
        );
    }
    if (param.specialized_function_virtual_address.has_value()) {
        descriptor.specialization.function_virtual_address = make_fact(
            *param.specialized_function_virtual_address,
            EvidenceSource::reflection,
            Confidence::verified,
            "specialized reflection procedure function pointer"
        );
    }
    if (param.specialized_argument_count.has_value()) {
        descriptor.specialization.argument_count = make_fact(
            *param.specialized_argument_count,
            EvidenceSource::reflection,
            Confidence::verified,
            "specialized reflection procedure argument count"
        );
    }
    if (param.specialized_value_count.has_value()) {
        descriptor.specialization.value_count = make_fact(
            *param.specialized_value_count,
            EvidenceSource::reflection,
            Confidence::verified,
            "specialized reflection enum value count"
        );
    }
    if (param.specialized_value_names_virtual_address.has_value()) {
        descriptor.specialization.value_names_virtual_address = make_fact(
            *param.specialized_value_names_virtual_address,
            EvidenceSource::reflection,
            Confidence::verified,
            "specialized reflection enum value-name table"
        );
    }
    if (param.specialized_argument_class_ids_virtual_address.has_value()) {
        descriptor.specialization.argument_class_ids_virtual_address = make_fact(
            *param.specialized_argument_class_ids_virtual_address,
            EvidenceSource::reflection,
            Confidence::verified,
            "specialized reflection procedure class-id table"
        );
    }
    if (param.specialized_argument_names_virtual_address.has_value()) {
        descriptor.specialization.argument_names_virtual_address = make_fact(
            *param.specialized_argument_names_virtual_address,
            EvidenceSource::reflection,
            Confidence::verified,
            "specialized reflection procedure argument-name table"
        );
    }
    if (param.specialized_argument_flags_virtual_address.has_value()) {
        descriptor.specialization.argument_flags_virtual_address = make_fact(
            *param.specialized_argument_flags_virtual_address,
            EvidenceSource::reflection,
            Confidence::verified,
            "specialized reflection procedure argument-flags table"
        );
    }
    if (param.specialized_auxiliary0.has_value()) {
        descriptor.specialization.auxiliary0 = make_fact(
            *param.specialized_auxiliary0,
            EvidenceSource::reflection,
            Confidence::verified,
            "specialized reflection payload field +0x1C"
        );
    }
    if (param.specialized_auxiliary1.has_value()) {
        descriptor.specialization.auxiliary1 = make_fact(
            *param.specialized_auxiliary1,
            EvidenceSource::reflection,
            Confidence::verified,
            "specialized reflection payload field +0x20 or +0x24"
        );
    }

    for (const auto& component : param.components) {
        NativeReflectionComponent native_component;
        if (component.name_virtual_address.has_value()) {
            native_component.name_virtual_address = make_fact(
                *component.name_virtual_address,
                EvidenceSource::reflection,
                Confidence::verified,
                "specialized reflection vector component name pointer"
            );
        }
        if (!component.name.empty()) {
            native_component.name = make_fact(
                component.name,
                EvidenceSource::reflection,
                Confidence::verified,
                "specialized reflection vector component name"
            );
        }
        descriptor.specialization.components.push_back(
            std::move(native_component)
        );
    }
    for (const auto& value : param.enum_values) {
        NativeReflectionEnumValue enum_value;
        enum_value.index = value.index;
        if (value.name_virtual_address.has_value()) {
            enum_value.name_virtual_address = make_fact(
                *value.name_virtual_address,
                EvidenceSource::reflection,
                Confidence::verified,
                "specialized reflection enum value name pointer"
            );
        }
        if (!value.name.empty()) {
            enum_value.name = make_fact(
                value.name,
                EvidenceSource::reflection,
                Confidence::verified,
                "specialized reflection enum value name"
            );
        }
        descriptor.specialization.enum_values.push_back(std::move(enum_value));
    }
    for (const auto& argument : param.procedure_arguments) {
        NativeReflectionProcedureArgument procedure_argument;
        procedure_argument.index = argument.index;
        if (argument.class_id.has_value()) {
            procedure_argument.class_id = make_fact(
                *argument.class_id,
                EvidenceSource::reflection,
                Confidence::verified,
                "specialized reflection procedure argument class ID"
            );
        }
        if (argument.name_virtual_address.has_value()) {
            procedure_argument.name_virtual_address = make_fact(
                *argument.name_virtual_address,
                EvidenceSource::reflection,
                Confidence::verified,
                "specialized reflection procedure argument name pointer"
            );
        }
        if (!argument.name.empty()) {
            procedure_argument.name = make_fact(
                argument.name,
                EvidenceSource::reflection,
                Confidence::verified,
                "specialized reflection procedure argument name"
            );
        }
        if (argument.flags.has_value()) {
            procedure_argument.flags = make_fact(
                *argument.flags,
                EvidenceSource::reflection,
                Confidence::verified,
                "specialized reflection procedure argument flags"
            );
        }
        descriptor.specialization.procedure_arguments.push_back(
            std::move(procedure_argument)
        );
    }

    return descriptor;
}

void append_reflection_data(
    NativeModel& model,
    NativeClass& native_class,
    const std::string& class_name,
    const ReflectionInfo& reflection)
{
    for (const auto& param : reflection.params) {
        auto descriptor = make_reflection_descriptor(class_name, param);
        if (std::find(
                native_class.reflection_descriptor_ids.begin(),
                native_class.reflection_descriptor_ids.end(),
                descriptor.id
            ) == native_class.reflection_descriptor_ids.end()) {
            native_class.reflection_descriptor_ids.push_back(descriptor.id);
            model.reflection_descriptors.push_back(std::move(descriptor));
        }

        if (param.name.empty()
            || param.offset < 0
            || reflection_category(param)
                != NativeReflectionCategory::physical_member) {
            continue;
        }

        NativeMember member;
        member.id = member_id(class_name, param.name, param.index);
        member.name = param.name;
        member.offset = make_fact(
            static_cast<std::uint32_t>(param.offset),
            EvidenceSource::reflection,
            Confidence::high,
            "GameBox reflection record +0x0C"
        );

        if (std::find(
                native_class.member_ids.begin(),
                native_class.member_ids.end(),
                member.id
            ) == native_class.member_ids.end()) {
            native_class.member_ids.push_back(member.id);
            model.members.push_back(std::move(member));
        }
    }
}

std::string vtable_id(const std::string& class_name)
{
    return "vtable:" + class_name;
}

std::string clean_rtti_name(std::string name)
{
    if (name.size() >= 4
        && name[0] == '.'
        && name[1] == '?'
        && name[2] == 'A'
        && (name[3] == 'V' || name[3] == 'U')) {
        name.erase(0, 4);
    }

    if (name.size() >= 2
        && name.ends_with("@@")) {
        name.resize(name.size() - 2);
    }

    return name;
}

std::string alias_name(const VtableAlias& alias)
{
    if (!alias.demangled_name.empty())
        return alias.demangled_name;

    return alias.decorated_name;
}

void resolve_unique_vtable_slot(
    NativeModel& model,
    NativeVtableSlot& slot)
{
    // A slot can legitimately have several logical aliases at one physical
    // RVA.  Only attach a direct function identity when the evidence leaves
    // exactly one candidate for this class slot.
    if (slot.candidate_function_ids.size() != 1)
        return;

    const auto& candidate_id = slot.candidate_function_ids.front();
    const auto function_it = std::find_if(
        model.functions.begin(),
        model.functions.end(),
        [&](const NativeFunction& function) {
            return function.id == candidate_id;
        }
    );

    if (function_it == model.functions.end())
        return;

    if (function_it->virtual_slot.known()
        && *function_it->virtual_slot.value != slot.index) {
        return;
    }

    slot.resolved_function_id = make_fact(
        candidate_id,
        EvidenceSource::vtable,
        Confidence::verified,
        "vtable slot has one direct-owner logical function candidate"
    );

    if (!function_it->virtual_slot.known()) {
        // The vector is not mutated here, so the iterator remains valid.
        function_it->virtual_slot = make_fact(
            slot.index,
            EvidenceSource::vtable,
            Confidence::verified,
            "logical function resolves to one direct-owner vtable slot"
        );
    }
}

bool belongs_to_hierarchy(
    const std::string& demangled_name,
    const std::unordered_set<std::string>& owners)
{
    if (demangled_name.empty())
        return false;

    for (const auto& owner : owners) {
        const auto needle =
            owner + "::";

        if (demangled_name.find(needle)
            != std::string::npos) {
            return true;
        }
    }

    return false;
}

void add_build(
    NativeModel& model,
    const BuildInfo& build)
{
    model.build.file_size = make_fact(
        build.executable.file_size,
        EvidenceSource::executable,
        Confidence::verified,
        "PE input file size"
    );

    model.build.sha256 = make_fact(
        build.executable.sha256,
        EvidenceSource::executable,
        Confidence::verified,
        "SHA-256 of analyzed executable"
    );

    model.build.machine = make_fact(
        build.executable.pe.machine,
        EvidenceSource::executable,
        Confidence::verified,
        "PE machine field"
    );

    model.build.timestamp = make_fact(
        build.executable.pe.timestamp,
        EvidenceSource::executable,
        Confidence::verified,
        "PE timestamp"
    );

    model.build.image_base = make_fact(
        build.executable.pe.image_base,
        EvidenceSource::executable,
        Confidence::verified,
        "PE preferred image base"
    );

    model.build.image_size = make_fact(
        build.executable.pe.image_size,
        EvidenceSource::executable,
        Confidence::verified,
        "PE SizeOfImage"
    );
}

void apply_type_usage(
    NativeTypeUsage& target,
    const ParsedTypeUsage& parsed)
{
    if (!parsed.parsed) {
        target.parse_error =
            make_fact(
                parsed.error,
                EvidenceSource::demangler,
                Confidence::high,
                "type declarator parser rejected declaration"
            );

        return;
    }

    target.base_type =
        make_fact(
            parsed.base_type,
            EvidenceSource::demangler,
            Confidence::high,
            "parsed from MSVC demangled type declaration"
        );

    target.pass_kind =
        make_fact(
            parsed.pass_kind,
            EvidenceSource::demangler,
            Confidence::high,
            "parsed from MSVC demangled type declaration"
        );

    target.declarator_kind =
        make_fact(
            parsed.declarator_kind,
            EvidenceSource::demangler,
            Confidence::high,
            "parsed from MSVC demangled type declaration"
        );

    target.pointer_depth =
        make_fact(
            parsed.pointer_depth,
            EvidenceSource::demangler,
            Confidence::high,
            "parsed from MSVC demangled type declaration"
        );

    target.is_const =
        make_fact(
            parsed.is_const,
            EvidenceSource::demangler,
            Confidence::high,
            "parsed from MSVC demangled type declaration"
        );
}
bool fact_mentions(
    const Fact<std::string>& fact,
    const std::string& name)
{
    return fact.known()
        && fact.value->find(name)
            != std::string::npos;
}

bool model_references_type(
    const NativeModel& model,
    const std::string& name)
{
    for (const auto& function : model.functions) {
        if (fact_mentions(
                function.return_type,
                name)) {
            return true;
        }

        for (const auto& parameter :
             function.parameters) {
            if (fact_mentions(
                    parameter.type,
                    name)) {
                return true;
            }
        }
    }

    return false;
}

void apply_known_return_abi(
    NativeFunction& function,
    const std::string& decorated_name)
{
    constexpr std::string_view hidden_result_functions[] = {
        "?GetCameraTargetPos@CTrackManiaEditor@@QAE?AVGmVec3@@XZ",
        "?GetGridLocation@CTrackManiaEditor@@UAE?AVGmIso4@@XZ",
        "?GetMouseCoordsAtLevel@CTrackManiaEditor@@QAE?AVGmNat3@@K@Z",
        "?GetMouseCoordsOnTerrain@CTrackManiaEditor@@QAE?AVGmNat3@@K@Z",
    };

    const bool known_hidden_result = std::any_of(
        std::begin(hidden_result_functions),
        std::end(hidden_result_functions),
        [&](std::string_view candidate) {
            return candidate == decorated_name;
        }
    );

    if (!known_hidden_result)
        return;

    function.return_abi = make_fact(
        NativeReturnAbi::hidden_result_pointer,
        EvidenceSource::disassembly,
        Confidence::high,
        "function writes its record result through a hidden caller-provided pointer and returns that pointer in EAX"
    );

    function.return_abi.verifications.push_back({
        VerificationKind::abi,
        "x86 disassembly shows a hidden result pointer stack argument, result-pointer return in EAX, and callee stack cleanup including that hidden argument"
    });
}

bool starts_with_word(
    const std::string& text,
    std::string_view word)
{
    if (!text.starts_with(word))
        return false;

    return text.size() == word.size()
        || text[word.size()] == ' ';
}

bool is_record_return_declaration(
    const std::string& declaration)
{
    return starts_with_word(declaration, "class")
        || starts_with_word(declaration, "struct")
        || starts_with_word(declaration, "union");
}

std::optional<std::size_t> fixed_stack_parameter_width(
    const NativeParameter& parameter)
{
    if (!parameter.type.known()
        || !parameter.usage.pass_kind.known()) {
        return std::nullopt;
    }

    const auto pass_kind = *parameter.usage.pass_kind.value;
    if (pass_kind == NativeTypePassKind::pointer
        || pass_kind == NativeTypePassKind::lvalue_reference
        || pass_kind == NativeTypePassKind::rvalue_reference) {
        return std::size_t{4};
    }

    if (pass_kind != NativeTypePassKind::value
        || !parameter.usage.base_type.known()) {
        return std::nullopt;
    }

    // This is the x86 stack slot width, not a claim about the source type's
    // sizeof(bool/char/short).  Enum and record arguments stay conservative:
    // their underlying representation is precisely the evidence this pass is
    // not allowed to invent.
    const auto& type = *parameter.usage.base_type.value;
    if (type == "double"
        || type == "long double"
        || type == "__int64"
        || type == "signed __int64"
        || type == "unsigned __int64") {
        return std::size_t{8};
    }

    constexpr std::string_view four_byte_values[] = {
        "bool",
        "char",
        "signed char",
        "unsigned char",
        "wchar_t",
        "short",
        "short int",
        "signed short",
        "signed short int",
        "unsigned short",
        "unsigned short int",
        "int",
        "signed",
        "signed int",
        "unsigned",
        "unsigned int",
        "long",
        "long int",
        "signed long",
        "signed long int",
        "unsigned long",
        "unsigned long int",
        "float",
    };

    if (std::find(
            std::begin(four_byte_values),
            std::end(four_byte_values),
            type
        ) != std::end(four_byte_values)) {
        return std::size_t{4};
    }

    return std::nullopt;
}

std::optional<std::size_t> rva_to_file_offset(
    const BuildInfo& build,
    std::uint32_t rva)
{
    const auto& image = *build.executable.image_data;

    for (const auto& section : build.executable.pe.sections) {
        const auto section_start =
            static_cast<std::uint64_t>(section.virtual_address);
        const auto section_extent = std::max(
            section.virtual_size,
            section.raw_size
        );
        const auto section_end =
            section_start + section_extent;

        if (rva < section_start || rva >= section_end)
            continue;

        const auto relative =
            static_cast<std::uint64_t>(rva) - section_start;
        if (relative >= section.raw_size)
            return std::nullopt;

        const auto file_offset =
            static_cast<std::uint64_t>(section.raw_offset) + relative;
        if (file_offset >= image.size())
            return std::nullopt;

        return static_cast<std::size_t>(file_offset);
    }

    return std::nullopt;
}

std::optional<Fact<std::uint32_t>> infer_mw_class_id(
    const BuildInfo& build,
    const std::string& class_name)
{
    const auto decorated_name =
        "?GetMwClassId@"
        + class_name
        + "@@UBEKXZ";

    const auto make_fact_from_body = [&] (
        std::uint32_t value,
        const std::string& executable_detail,
        const std::string& map_detail,
        const std::string& verification_detail)
    {
        auto fact = make_fact(
            value,
            EvidenceSource::executable,
            Confidence::verified,
            executable_detail
        );
        fact.evidence.push_back({
            EvidenceSource::map,
            Confidence::verified,
            map_detail
        });
        fact.verifications.push_back({
            VerificationKind::static_analysis,
            verification_detail
        });
        return fact;
    };

    const auto symbol_it =
        build.map.symbols_by_name.find(decorated_name);

    if (symbol_it != build.map.symbols_by_name.end()
        && symbol_it->second.size() == 1) {
        const auto& symbol =
            build.map.symbols[symbol_it->second.front()];

        if (!symbol.is_function
            || symbol.virtual_address < build.executable.pe.image_base) {
            return std::nullopt;
        }

        const auto rva =
            symbol.virtual_address
            - build.executable.pe.image_base;

        const auto start = rva_to_file_offset(build, rva);
        if (!start.has_value())
            return std::nullopt;

        const auto& image = *build.executable.image_data;
        if (*start + 6 > image.size()
            || image[*start] != 0xB8
            || image[*start + 5] != 0xC3) {
            // Do not infer a class ID from a function body that is not the
            // exact direct-return form. A more complex body needs its own
            // evidence provider and must remain unknown here.
            return std::nullopt;
        }

        const auto value =
            static_cast<std::uint32_t>(image[*start + 1])
            | (static_cast<std::uint32_t>(image[*start + 2]) << 8)
            | (static_cast<std::uint32_t>(image[*start + 3]) << 16)
            | (static_cast<std::uint32_t>(image[*start + 4]) << 24);

        return make_fact_from_body(
            value,
            "exact GetMwClassId body returns the numeric GameBox class ID",
            "GetMwClassId function identity came from TmForever.map",
            "x86 mov eax, immediate; ret at the exact target RVA"
        );
    }

    // Some GameBox interfaces have a CMwClassInfo descriptor but no virtual
    // GetMwClassId method. Their compiler-emitted dynamic initializer passes
    // the class ID directly to CMwClassInfo's constructor. Recognize only
    // the exact five-instruction argument sequence used by this build:
    //
    //   push constructor-or-null
    //   push member-info table
    //   push class-name
    //   push class-id
    //   mov ecx, &m_MwClassInfo_Class; call CMwClassInfo::CMwClassInfo
    //
    // This is a fallback for identity only. It does not infer a C++ layout,
    // inheritance declaration, constructor safety, or member semantics.
    const auto owner_symbols =
        build.map.symbols_by_owner.find(class_name);
    if (owner_symbols == build.map.symbols_by_owner.end())
        return std::nullopt;

    const auto initializer_prefix =
        "??__E?m_MwClassInfo_" + class_name + "@";
    const MapSymbol* initializer = nullptr;
    const MapSymbol* class_info_static = nullptr;

    for (const auto index : owner_symbols->second) {
        const auto& candidate = build.map.symbols[index];

        if (candidate.name.starts_with(initializer_prefix)) {
            if (initializer != nullptr || !candidate.is_function)
                return std::nullopt;
            initializer = &candidate;
        }

        const auto static_prefix =
            "?m_MwClassInfo_" + class_name + "@";
        if (candidate.name.starts_with(static_prefix)) {
            if (class_info_static != nullptr || candidate.is_function)
                return std::nullopt;
            class_info_static = &candidate;
        }
    }

    if (initializer == nullptr || class_info_static == nullptr)
        return std::nullopt;

    const auto ctor_name =
        "??0CMwClassInfo@@QAE@KPAV0@PADP6APAVCMwNod@@XZ@Z";
    const auto ctor_it = build.map.symbols_by_name.find(ctor_name);
    if (ctor_it == build.map.symbols_by_name.end()
        || ctor_it->second.size() != 1) {
        return std::nullopt;
    }

    const auto& image = *build.executable.image_data;
    if (initializer->virtual_address < build.executable.pe.image_base)
        return std::nullopt;

    const auto initializer_rva =
        initializer->virtual_address
        - build.executable.pe.image_base;
    const auto start = rva_to_file_offset(build, initializer_rva);
    if (!start.has_value())
        return std::nullopt;

    std::array<std::uint32_t, 4> pushes{};
    std::size_t cursor = 0;
    for (std::size_t index = 0; index < pushes.size(); ++index) {
        if (*start + cursor >= image.size())
            return std::nullopt;

        const auto opcode = image[*start + cursor];
        if (opcode == 0x68) {
            if (*start + cursor + 5 > image.size())
                return std::nullopt;
            pushes[index] =
                static_cast<std::uint32_t>(image[*start + cursor + 1])
                | (static_cast<std::uint32_t>(image[*start + cursor + 2]) << 8)
                | (static_cast<std::uint32_t>(image[*start + cursor + 3]) << 16)
                | (static_cast<std::uint32_t>(image[*start + cursor + 4]) << 24);
            cursor += 5;
        }
        else if (opcode == 0x6A) {
            if (*start + cursor + 2 > image.size())
                return std::nullopt;
            pushes[index] = image[*start + cursor + 1];
            cursor += 2;
        }
        else {
            return std::nullopt;
        }
    }

    if (*start + cursor + 10 > image.size()
        || image[*start + cursor] != 0xB9
        || image[*start + cursor + 5] != 0xE8) {
        return std::nullopt;
    }

    const auto class_info_va =
        static_cast<std::uint32_t>(image[*start + cursor + 1])
        | (static_cast<std::uint32_t>(image[*start + cursor + 2]) << 8)
        | (static_cast<std::uint32_t>(image[*start + cursor + 3]) << 16)
        | (static_cast<std::uint32_t>(image[*start + cursor + 4]) << 24);
    if (class_info_va != class_info_static->virtual_address)
        return std::nullopt;

    const auto relative =
        static_cast<std::int32_t>(
            static_cast<std::uint32_t>(image[*start + cursor + 6])
            | (static_cast<std::uint32_t>(image[*start + cursor + 7]) << 8)
            | (static_cast<std::uint32_t>(image[*start + cursor + 8]) << 16)
            | (static_cast<std::uint32_t>(image[*start + cursor + 9]) << 24)
        );
    const auto call_target =
        static_cast<std::int64_t>(initializer->virtual_address)
        + static_cast<std::int64_t>(cursor)
        + 10
        + static_cast<std::int64_t>(relative);
    if (call_target < 0
        || call_target > std::numeric_limits<std::uint32_t>::max()
        || static_cast<std::uint32_t>(call_target)
            != build.map.symbols[ctor_it->second.front()].virtual_address) {
        return std::nullopt;
    }

    return make_fact_from_body(
        pushes[3],
        "exact CMwClassInfo dynamic initializer passes the numeric GameBox class ID",
        "class-info initializer, static descriptor, and CMwClassInfo constructor came from TmForever.map",
        "x86 push immediate; mov ecx, class-info static; call CMwClassInfo constructor"
    );
}

bool is_code_padding(std::uint8_t byte)
{
    return byte == 0x00
        || byte == 0x90
        || byte == 0xCC;
}

std::optional<std::uint16_t> trailing_ret_cleanup(
    const BuildInfo& build,
    std::uint32_t rva,
    const std::vector<std::uint32_t>& function_rvas)
{
    const auto next = std::upper_bound(
        function_rvas.begin(),
        function_rvas.end(),
        rva
    );
    if (next == function_rvas.end() || *next <= rva)
        return std::nullopt;

    const auto start = rva_to_file_offset(build, rva);
    const auto end = rva_to_file_offset(build, *next);
    if (!start.has_value() || !end.has_value() || *end <= *start)
        return std::nullopt;

    const auto& image = *build.executable.image_data;
    const auto scan_begin =
        *end > *start + 256 ? *end - 256 : *start;

    for (std::size_t index = *end; index-- > scan_begin;) {
        if (image[index] != 0xC2 || index + 2 >= *end)
            continue;

        const auto cleanup = static_cast<std::uint16_t>(
            image[index + 1]
            | (static_cast<std::uint16_t>(image[index + 2]) << 8)
        );

        bool padding_after = true;
        for (std::size_t after = index + 3; after < *end; ++after) {
            if (!is_code_padding(image[after])) {
                padding_after = false;
                break;
            }
        }

        if (padding_after)
            return cleanup;
    }

    return std::nullopt;
}

bool has_bytes(
    const std::vector<std::uint8_t>& image,
    std::size_t offset,
    std::initializer_list<std::uint8_t> expected)
{
    if (offset + expected.size() > image.size())
        return false;

    std::size_t index = 0;
    for (const auto byte : expected) {
        if (image[offset + index] != byte)
            return false;
        ++index;
    }

    return true;
}

bool has_simple_cdecl_hidden_result_evidence(
    const BuildInfo& build,
    std::uint32_t rva,
    const std::vector<std::uint32_t>& function_rvas)
{
    const auto next = std::upper_bound(
        function_rvas.begin(),
        function_rvas.end(),
        rva
    );
    if (next == function_rvas.end() || *next <= rva)
        return false;

    const auto start = rva_to_file_offset(build, rva);
    const auto end = rva_to_file_offset(build, *next);
    if (!start.has_value()
        || !end.has_value()
        || *end <= *start) {
        return false;
    }

    const auto& image = *build.executable.image_data;
    const auto length = std::min<std::size_t>(
        *end - *start,
        256
    );

    // This deliberately recognizes only the leaf-function form where the
    // hidden result pointer is loaded from the first cdecl stack argument.
    // More involved prologues need a real instruction decoder before they
    // can be promoted to verified ABI evidence.
    if (!has_bytes(
            image,
            *start,
            {0x8B, 0x44, 0x24, 0x04})) {
        return false;
    }

    std::set<std::uint8_t> stored_offsets;
    std::optional<std::size_t> return_offset;

    for (std::size_t cursor = 4; cursor < length; ++cursor) {
        if (image[*start + cursor] == 0xC3) {
            bool padding_after = true;
            for (std::size_t after = cursor + 1;
                 after < length;
                 ++after) {
                if (!is_code_padding(image[*start + after])) {
                    padding_after = false;
                    break;
                }
            }

            if (padding_after) {
                return_offset = cursor;
                break;
            }
        }

        // Reject the common encodings that overwrite EAX.  Keeping EAX
        // unchanged after the initial stack load is what proves that the
        // callee returns the same hidden result pointer.
        if ((image[*start + cursor] >= 0xB8
                && image[*start + cursor] <= 0xBF)
            || (cursor + 1 < length
                && image[*start + cursor] == 0x33
                && image[*start + cursor + 1] == 0xC0)
            || (cursor + 1 < length
                && image[*start + cursor] == 0x31
                && image[*start + cursor + 1] == 0xC0)
            || (cursor + 1 < length
                && image[*start + cursor] == 0x8B
                && image[*start + cursor + 1] >= 0xC0
                && image[*start + cursor + 1] <= 0xC7)) {
            return false;
        }

        if (cursor + 1 < length
            && image[*start + cursor] == 0x89) {
            const auto modrm = image[*start + cursor + 1];
            if (modrm == 0x08) {
                stored_offsets.insert(0);
            }
            else if ((modrm == 0x48 || modrm == 0x50)
                && cursor + 2 < length) {
                stored_offsets.insert(
                    image[*start + cursor + 2]
                );
            }
        }

        if (cursor + 2 < length
            && image[*start + cursor] == 0xC7
            && image[*start + cursor + 1] == 0x40) {
            stored_offsets.insert(
                image[*start + cursor + 2]
            );
        }
    }

    return return_offset.has_value()
        && stored_offsets.contains(0)
        && stored_offsets.contains(4);
}

bool has_cdecl_hidden_result_prologue_evidence(
    const BuildInfo& build,
    std::uint32_t rva,
    const std::vector<std::uint32_t>& function_rvas,
    std::size_t result_size)
{
    if (result_size == 0 || result_size % 4 != 0)
        return false;

    const auto next = std::upper_bound(
        function_rvas.begin(),
        function_rvas.end(),
        rva
    );
    if (next == function_rvas.end() || *next <= rva)
        return false;

    const auto start = rva_to_file_offset(build, rva);
    const auto end = rva_to_file_offset(build, *next);
    if (!start.has_value()
        || !end.has_value()
        || *end <= *start) {
        return false;
    }

    const auto& image = *build.executable.image_data;
    const auto length = std::min<std::size_t>(
        *end - *start,
        256
    );

    // Track only the compiler's simple register-save prologue.  The hidden
    // cdecl result pointer is the first original stack argument, so its
    // displacement is four bytes beyond the current stack delta.
    std::size_t cursor = 0;
    std::size_t stack_delta = 0;
    while (cursor < length
        && image[*start + cursor] >= 0x50
        && image[*start + cursor] <= 0x57) {
        stack_delta += 4;
        ++cursor;
    }

    if (cursor + 3 <= length
        && image[*start + cursor] == 0x83
        && image[*start + cursor + 1] == 0xEC) {
        stack_delta += image[*start + cursor + 2];
        cursor += 3;
    }
    else if (cursor + 6 <= length
        && image[*start + cursor] == 0x81
        && image[*start + cursor + 1] == 0xEC) {
        const auto amount = static_cast<std::uint32_t>(
            image[*start + cursor + 2]
            | (static_cast<std::uint32_t>(image[*start + cursor + 3]) << 8)
            | (static_cast<std::uint32_t>(image[*start + cursor + 4]) << 16)
            | (static_cast<std::uint32_t>(image[*start + cursor + 5]) << 24)
        );
        if (amount > 0x1000)
            return false;
        stack_delta += amount;
        cursor += 6;
    }

    std::optional<std::uint8_t> result_register;
    std::size_t result_load_end = 0;

    for (std::size_t probe = cursor;
         probe + 3 < length && probe <= cursor + 32;
         ++probe) {
        if (image[*start + probe] != 0x8B)
            continue;

        const auto modrm = image[*start + probe + 1];
        const auto mode = modrm >> 6;
        const auto register_index =
            static_cast<std::uint8_t>((modrm >> 3) & 0x07);
        const auto base = static_cast<std::uint8_t>(modrm & 0x07);

        if (mode != 1 || base != 4
            || probe + 3 >= length
            || image[*start + probe + 2] != 0x24) {
            continue;
        }

        const auto displacement = image[*start + probe + 3];
        if (displacement != stack_delta + 4
            || register_index == 4) {
            continue;
        }

        result_register = register_index;
        result_load_end = probe + 4;
        break;
    }

    if (!result_register.has_value())
        return false;

    std::set<std::size_t> stored_offsets;
    std::optional<std::size_t> return_offset;

    const auto record_store = [&](std::size_t probe) {
        if (probe + 1 >= length
            || image[*start + probe] != 0x89) {
            return;
        }

        const auto modrm = image[*start + probe + 1];
        const auto mode = modrm >> 6;
        const auto base = static_cast<std::uint8_t>(modrm & 0x07);
        if (mode == 3 || base != *result_register || base == 4)
            return;

        if (mode == 0) {
            if (base != 5) {
                stored_offsets.insert(0);
            }
        }
        else if (mode == 1 && probe + 2 < length) {
            stored_offsets.insert(
                image[*start + probe + 2]
            );
        }
        else if (mode == 2 && probe + 5 < length) {
            const auto displacement = static_cast<std::uint32_t>(
                image[*start + probe + 2]
                | (static_cast<std::uint32_t>(image[*start + probe + 3]) << 8)
                | (static_cast<std::uint32_t>(image[*start + probe + 4]) << 16)
                | (static_cast<std::uint32_t>(image[*start + probe + 5]) << 24)
            );
            if (displacement <= 0x1000)
                stored_offsets.insert(displacement);
        }
    };

    for (std::size_t probe = result_load_end;
         probe + 1 < length;
         ++probe) {
        record_store(probe);

        if (image[*start + probe] != 0x8B
            || image[*start + probe + 1]
                != static_cast<std::uint8_t>(0xC0 | *result_register)) {
            continue;
        }

        std::size_t epilogue = probe + 2;
        while (epilogue < length) {
            const auto byte = image[*start + epilogue];

            if (byte >= 0x58 && byte <= 0x5F) {
                ++epilogue;
                continue;
            }

            if (byte == 0x83
                && epilogue + 2 < length
                && image[*start + epilogue + 1] == 0xC4) {
                epilogue += 3;
                continue;
            }

            if (byte == 0x81
                && epilogue + 5 < length
                && image[*start + epilogue + 1] == 0xC4) {
                epilogue += 6;
                continue;
            }

            if (byte == 0xC9) {
                ++epilogue;
                continue;
            }

            if (byte == 0xC3) {
                return_offset = epilogue;
                break;
            }

            break;
        }

        if (return_offset.has_value())
            break;
    }

    if (!return_offset.has_value())
        return false;

    for (std::size_t after = *return_offset + 1;
         after < length;
         ++after) {
        if (!is_code_padding(image[*start + after]))
            return false;
    }

    for (std::size_t offset = 0;
         offset < result_size;
         offset += 4) {
        if (!stored_offsets.contains(offset))
            return false;
    }

    return true;
}

void apply_inferred_hidden_result_abi(
    NativeFunction& function,
    const BuildInfo& build,
    const std::vector<std::uint32_t>& function_rvas)
{
    if (function.return_abi.known()
        || !function.return_type.known()
        || !function.return_usage.pass_kind.known()
        || *function.return_usage.pass_kind.value
            != NativeTypePassKind::value
        || !is_record_return_declaration(*function.return_type.value)
        || !function.calling_convention.known()) {
        return;
    }

    const auto convention = *function.calling_convention.value;
    if (convention == CallingConvention::cdecl_
        && has_simple_cdecl_hidden_result_evidence(
            build,
            *function.rva.value,
            function_rvas
        )) {
        function.return_abi = make_fact(
            NativeReturnAbi::hidden_result_pointer,
            EvidenceSource::disassembly,
            Confidence::high,
            "simple cdecl body writes through its first stack argument and returns that hidden record-result pointer"
        );

        function.return_abi.verifications.push_back({
            VerificationKind::abi,
            "x86 disassembly loads the first cdecl stack argument as the result buffer, writes through it, preserves EAX, and returns with C3"
        });
        return;
    }

    if (convention != CallingConvention::thiscall_
        && convention != CallingConvention::stdcall_) {
        return;
    }

    std::size_t explicit_stack_bytes = 0;
    for (const auto& parameter : function.parameters) {
        const auto width = fixed_stack_parameter_width(parameter);
        if (!width.has_value())
            return;

        explicit_stack_bytes += *width;
    }

    const auto cleanup = trailing_ret_cleanup(
        build,
        *function.rva.value,
        function_rvas
    );
    if (!cleanup.has_value()
        || *cleanup != explicit_stack_bytes + 4) {
        return;
    }

    function.return_abi = make_fact(
        NativeReturnAbi::hidden_result_pointer,
        EvidenceSource::disassembly,
        Confidence::high,
        "function-end x86 ret cleanup accounts for a hidden caller-provided record-result pointer"
    );

    function.return_abi.verifications.push_back({
        VerificationKind::abi,
        "the callee-cleaned x86 stack includes exactly one hidden result pointer beyond its explicitly modeled arguments"
    });
}

bool is_enum_value_declaration(
    const NativeParameter& parameter)
{
    return parameter.type.known()
        && parameter.usage.pass_kind.known()
        && *parameter.usage.pass_kind.value
            == NativeTypePassKind::value
        && starts_with_word(*parameter.type.value, "enum")
        && parameter.usage.base_type.known();
}

bool model_has_type_name(
    const NativeModel& model,
    const std::string& name)
{
    return std::any_of(
        model.types.begin(),
        model.types.end(),
        [&](const NativeType& type) {
            return type.name == name;
        }
    );
}

using AbiTypeWidths = std::unordered_map<std::string, std::size_t>;

AbiTypeWidths build_abi_type_widths(const NativeModel& model)
{
    AbiTypeWidths widths;

    for (const auto& type : model.types) {
        if (!type.size.known()
            || !std::any_of(
                type.size.verifications.begin(),
                type.size.verifications.end(),
                [](const Verification& verification) {
                    return verification.kind == VerificationKind::abi;
                }
            )) {
            continue;
        }

        const auto width = std::max<std::size_t>(
            4,
            (*type.size.value + 3) & ~std::size_t{3}
        );
        const auto found = widths.find(type.name);
        if (found == widths.end()) {
            widths.emplace(type.name, width);
        }
        else if (found->second != width) {
            widths.erase(found);
        }
    }

    return widths;
}

std::optional<std::size_t> model_stack_parameter_width(
    const NativeParameter& parameter,
    const AbiTypeWidths& abi_type_widths)
{
    const auto fixed_width = fixed_stack_parameter_width(parameter);
    if (fixed_width.has_value())
        return fixed_width;

    if (!parameter.type.known()
        || !parameter.usage.pass_kind.known()
        || *parameter.usage.pass_kind.value
            != NativeTypePassKind::value
        || !parameter.usage.base_type.known()) {
        return std::nullopt;
    }

    const auto type = abi_type_widths.find(
        *parameter.usage.base_type.value
    );
    if (type == abi_type_widths.end()) {
        return std::nullopt;
    }

    return type->second;
}

void infer_enum_abi_types(
    NativeModel& model,
    const BuildInfo& build,
    const std::vector<std::uint32_t>& function_rvas)
{
    std::unordered_map<std::string, std::string> observations;
    const auto abi_type_widths = build_abi_type_widths(model);

    for (const auto& function : model.functions) {
        if (!function.calling_convention.known()
            || !function.rva.known()) {
            continue;
        }

        const auto convention = *function.calling_convention.value;
        if (convention != CallingConvention::thiscall_
            && convention != CallingConvention::stdcall_) {
            continue;
        }

        std::vector<const NativeParameter*> enum_parameters;
        std::size_t fixed_stack_bytes = 0;
        bool usable = true;

        for (const auto& parameter : function.parameters) {
            if (is_enum_value_declaration(parameter)) {
                enum_parameters.push_back(&parameter);
                continue;
            }

            const auto width = model_stack_parameter_width(
                parameter,
                abi_type_widths
            );
            if (!width.has_value()) {
                usable = false;
                break;
            }

            fixed_stack_bytes += *width;
        }

        if (!usable || enum_parameters.empty())
            continue;

        std::size_t hidden_result_bytes = 0;
        if (function.return_type.known()
            && function.return_usage.pass_kind.known()
            && *function.return_usage.pass_kind.value
                == NativeTypePassKind::value
            && is_record_return_declaration(*function.return_type.value)) {
            if (!function.return_abi.known()
                || *function.return_abi.value
                    != NativeReturnAbi::hidden_result_pointer) {
                continue;
            }

            hidden_result_bytes = 4;
        }

        const auto cleanup = trailing_ret_cleanup(
            build,
            *function.rva.value,
            function_rvas
        );
        if (!cleanup.has_value()
            || *cleanup != fixed_stack_bytes
                + enum_parameters.size() * 4
                + hidden_result_bytes) {
            continue;
        }

        for (const auto* enum_parameter : enum_parameters) {
            observations.emplace(
                *enum_parameter->usage.base_type.value,
                function.id
            );
        }
    }

    for (const auto& [enum_name, function_id] : observations) {
        if (enum_name.empty() || model_has_type_name(model, enum_name))
            continue;

        NativeType type;
        type.id = "type:" + enum_name;
        type.name = enum_name;
        type.kind = make_fact(
            NativeTypeKind::enum_,
            EvidenceSource::demangler,
            Confidence::high,
            "MSVC function signatures identify "
            + enum_name
            + " as an enum type"
        );
        type.size = make_fact(
            std::uint32_t{4},
            EvidenceSource::disassembly,
            Confidence::high,
            "callee-cleaned x86 function evidence identifies a 4-byte enum ABI slot"
        );
        type.size.verifications.push_back({
            VerificationKind::abi,
            "function "
            + function_id
            + " has only fixed non-enum stack arguments and ret cleanup for this enum's 4-byte x86 stack slot"
        });

        model.types.push_back(std::move(type));
    }
}

bool has_simple_enum_return_evidence(
    const BuildInfo& build,
    std::uint32_t rva,
    const std::vector<std::uint32_t>& function_rvas)
{
    const auto next = std::upper_bound(
        function_rvas.begin(),
        function_rvas.end(),
        rva
    );
    if (next == function_rvas.end() || *next <= rva)
        return false;

    const auto start = rva_to_file_offset(build, rva);
    const auto end = rva_to_file_offset(build, *next);
    if (!start.has_value()
        || !end.has_value()
        || *end <= *start) {
        return false;
    }

    const auto& image = *build.executable.image_data;
    const auto length = std::min<std::size_t>(
        *end - *start,
        32
    );

    std::size_t ret_offset = 0;
    if (length >= 6
        && image[*start] >= 0xB8
        && image[*start] <= 0xBF
        && image[*start + 5] == 0xC3) {
        ret_offset = 5;
    }
    else if (length >= 3
        && ((image[*start] == 0x33
                && image[*start + 1] == 0xC0)
            || (image[*start] == 0x31
                && image[*start + 1] == 0xC0)
            || (image[*start] == 0x8B
                && image[*start + 1] == 0x01))
        && image[*start + 2] == 0xC3) {
        ret_offset = 2;
    }
    else if (length >= 4
        && image[*start] == 0x8B
        && (image[*start + 1] == 0x40
            || image[*start + 1] == 0x41)
        && image[*start + 3] == 0xC3) {
        ret_offset = 3;
    }
    else if (length >= 6
        && image[*start] == 0xA1
        && image[*start + 5] == 0xC3) {
        ret_offset = 5;
    }
    else {
        return false;
    }

    for (std::size_t after = ret_offset + 1;
         after < length;
         ++after) {
        if (!is_code_padding(image[*start + after]))
            return false;
    }

    return true;
}

void infer_enum_return_types(
    NativeModel& model,
    const BuildInfo& build,
    const std::vector<std::uint32_t>& function_rvas)
{
    std::unordered_map<std::string, std::string> observations;

    for (const auto& function : model.functions) {
        if (!function.return_type.known()
            || !function.return_usage.pass_kind.known()
            || *function.return_usage.pass_kind.value
                != NativeTypePassKind::value
            || !starts_with_word(*function.return_type.value, "enum")
            || !function.return_usage.base_type.known()
            || !function.rva.known()) {
            continue;
        }

        if (!has_simple_enum_return_evidence(
                build,
                *function.rva.value,
                function_rvas
            )) {
            continue;
        }

        observations.emplace(
            *function.return_usage.base_type.value,
            function.id
        );
    }

    for (const auto& [enum_name, function_id] : observations) {
        if (enum_name.empty() || model_has_type_name(model, enum_name))
            continue;

        NativeType type;
        type.id = "type:" + enum_name;
        type.name = enum_name;
        type.kind = make_fact(
            NativeTypeKind::enum_,
            EvidenceSource::demangler,
            Confidence::high,
            "MSVC function signatures identify "
            + enum_name
            + " as an enum type"
        );
        type.size = make_fact(
            std::uint32_t{4},
            EvidenceSource::disassembly,
            Confidence::high,
            "leaf enum-return body writes a 32-bit value to EAX before returning"
        );
        type.size.verifications.push_back({
            VerificationKind::abi,
            "function "
            + function_id
            + " uses an unbranched 32-bit EAX return followed by a direct x86 return"
        });

        model.types.push_back(std::move(type));
    }
}

void apply_model_inferred_hidden_result_abi(
    NativeModel& model,
    const BuildInfo& build,
    const std::vector<std::uint32_t>& function_rvas)
{
    const auto abi_type_widths = build_abi_type_widths(model);

    for (auto& function : model.functions) {
        if (function.return_abi.known()
            || !function.return_type.known()
            || !function.return_usage.pass_kind.known()
            || *function.return_usage.pass_kind.value
                != NativeTypePassKind::value
            || !is_record_return_declaration(*function.return_type.value)
            || !function.calling_convention.known()
            || !function.rva.known()) {
            continue;
        }

        const auto convention = *function.calling_convention.value;
        if (convention == CallingConvention::cdecl_
            && function.return_usage.base_type.known()) {
            const auto type = std::find_if(
                model.types.begin(),
                model.types.end(),
                [&](const NativeType& candidate) {
                    return candidate.name
                        == *function.return_usage.base_type.value;
                }
            );

            const bool has_verified_size =
                type != model.types.end()
                && type->size.known()
                && std::any_of(
                    type->size.verifications.begin(),
                    type->size.verifications.end(),
                    [](const Verification& verification) {
                        return verification.kind
                            == VerificationKind::abi;
                    }
                );

            if (has_verified_size
                && has_cdecl_hidden_result_prologue_evidence(
                    build,
                    *function.rva.value,
                    function_rvas,
                    *type->size.value
                )) {
                function.return_abi = make_fact(
                    NativeReturnAbi::hidden_result_pointer,
                    EvidenceSource::disassembly,
                    Confidence::high,
                    "cdecl prologue loads the first stack argument as the ABI-verified record result buffer and returns that pointer"
                );
                function.return_abi.verifications.push_back({
                    VerificationKind::abi,
                    "x86 disassembly shows a compiler register-save prologue, a first-argument result-buffer load, complete DWORD stores for the ABI-verified record size, and a result-pointer return"
                });
            }

            continue;
        }

        if (convention != CallingConvention::thiscall_
            && convention != CallingConvention::stdcall_) {
            continue;
        }

        std::size_t explicit_stack_bytes = 0;
        bool usable = true;
        for (const auto& parameter : function.parameters) {
            const auto width = model_stack_parameter_width(
                parameter,
                abi_type_widths
            );
            if (!width.has_value()) {
                usable = false;
                break;
            }

            explicit_stack_bytes += *width;
        }

        if (!usable)
            continue;

        const auto cleanup = trailing_ret_cleanup(
            build,
            *function.rva.value,
            function_rvas
        );
        if (!cleanup.has_value()
            || *cleanup != explicit_stack_bytes + 4) {
            continue;
        }

        function.return_abi = make_fact(
            NativeReturnAbi::hidden_result_pointer,
            EvidenceSource::disassembly,
            Confidence::high,
            "callee-cleaned x86 stack cleanup matches ABI-verified model widths plus one hidden record-result pointer"
        );
        function.return_abi.verifications.push_back({
            VerificationKind::abi,
            "the callee-cleaned x86 stack includes exactly one hidden result pointer beyond parameter widths established by ABI-verified type sizes"
        });
    }
}

struct TrivialCopyLayout {
    std::string type_name;
    std::size_t size = 0;
    std::uint32_t rva = 0;
    std::string decorated_name;
};

std::optional<TrivialCopyLayout> inspect_trivial_copy_layout(
    const BuildInfo& build,
    const MapSymbol& symbol,
    const std::vector<std::uint32_t>& function_rvas)
{
    const bool copy_constructor = symbol.name.starts_with("??0");
    const bool copy_assignment = symbol.name.starts_with("??4");
    if (!symbol.is_function
        || (!copy_constructor && !copy_assignment)) {
        return std::nullopt;
    }

    const auto demangled = demangle_msvc(symbol.name);
    if (demangled.empty())
        return std::nullopt;

    const auto parsed = parse_function_signature(demangled);
    if (!parsed.parsed
        || parsed.parameter_types.size() != 1
        || parsed.calling_convention != CallingConvention::thiscall_) {
        return std::nullopt;
    }

    const auto separator = parsed.qualified_name.rfind("::");
    if (separator == std::string::npos)
        return std::nullopt;

    const auto owner = parsed.qualified_name.substr(0, separator);
    if (owner.empty())
        return std::nullopt;

    const auto parameter = parse_type_usage(
        parsed.parameter_types.front()
    );
    if (!parameter.parsed
        || parameter.base_type != owner
        || (parameter.pass_kind != NativeTypePassKind::lvalue_reference
            && parameter.pass_kind != NativeTypePassKind::value)) {
        return std::nullopt;
    }

    if (symbol.virtual_address < build.executable.pe.image_base)
        return std::nullopt;

    const auto rva =
        symbol.virtual_address - build.executable.pe.image_base;
    const auto next = std::upper_bound(
        function_rvas.begin(),
        function_rvas.end(),
        rva
    );
    if (next == function_rvas.end() || *next <= rva)
        return std::nullopt;

    const auto start = rva_to_file_offset(build, rva);
    const auto end = rva_to_file_offset(build, *next);
    if (!start.has_value()
        || !end.has_value()
        || *end <= *start) {
        return std::nullopt;
    }

    const auto& image = *build.executable.image_data;
    const auto length = std::min<std::size_t>(
        *end - *start,
        128
    );

    std::size_t cursor = 0;
    if (!has_bytes(image, *start + cursor, {0x8B, 0xC1}))
        return std::nullopt;
    cursor += 2;

    if (!has_bytes(
            image,
            *start + cursor,
            {0x8B, 0x4C, 0x24, 0x04})) {
        return std::nullopt;
    }
    cursor += 4;

    std::size_t copied = 0;
    while (cursor < length) {
        if (copied == 0
            && has_bytes(
                image,
                *start + cursor,
                {0x8B, 0x11, 0x89, 0x10})) {
            cursor += 4;
            copied += 4;
            continue;
        }

        if (copied > 0
            && copied <= 0xFF
            && cursor + 6 <= length
            && image[*start + cursor] == 0x8B
            && (image[*start + cursor + 1] == 0x51
                || image[*start + cursor + 1] == 0x49)
            && image[*start + cursor + 2]
                == static_cast<std::uint8_t>(copied)
            && image[*start + cursor + 3] == 0x89
            && (image[*start + cursor + 4] == 0x50
                || image[*start + cursor + 4] == 0x48)
            && image[*start + cursor + 5]
                == static_cast<std::uint8_t>(copied)) {
            cursor += 6;
            copied += 4;
            continue;
        }

        break;
    }

    if (copied == 0
        || (!has_bytes(image, *start + cursor, {0xC2, 0x04, 0x00})
            && !has_bytes(image, *start + cursor, {0xC3}))) {
        return std::nullopt;
    }

    const auto ret_length =
        has_bytes(image, *start + cursor, {0xC3})
            ? std::size_t{1}
            : std::size_t{3};

    for (std::size_t after = cursor + ret_length;
         after < length;
         ++after) {
        if (!is_code_padding(image[*start + after]))
            return std::nullopt;
    }

    return TrivialCopyLayout{
        owner,
        copied,
        rva,
        symbol.name,
    };
}

void infer_trivial_record_layouts(
    NativeModel& model,
    const BuildInfo& build,
    const std::vector<std::uint32_t>& function_rvas)
{
    std::unordered_map<std::string, TrivialCopyLayout> layouts;
    std::unordered_set<std::string> disagreements;

    for (const auto& symbol : build.map.symbols) {
        const auto layout = inspect_trivial_copy_layout(
            build,
            symbol,
            function_rvas
        );
        if (!layout.has_value())
            continue;

        const auto found = layouts.find(layout->type_name);
        if (found == layouts.end()) {
            layouts.emplace(layout->type_name, *layout);
        }
        else if (found->second.size != layout->size) {
            disagreements.insert(layout->type_name);
        }
    }

    for (const auto& [name, layout] : layouts) {
        if (disagreements.contains(name)
            || model_has_type_name(model, name)
            || !model_references_type(model, name)) {
            continue;
        }

        NativeType type;
        type.id = "type:" + name;
        type.name = name;
        type.kind = make_fact(
            NativeTypeKind::record,
            EvidenceSource::demangler,
            Confidence::high,
            "MSVC copy operation identifies "
            + name
            + " as a C++ record type"
        );
        type.size = make_fact(
            static_cast<std::uint32_t>(layout.size),
            EvidenceSource::disassembly,
            Confidence::high,
            "compiler-generated copy operation transfers a contiguous "
            + std::to_string(layout.size / 4)
            + "-DWORD record footprint"
        );
        type.size.verifications.push_back({
            VerificationKind::abi,
            "copy operation "
            + layout.decorated_name
            + " at RVA "
            + hex_id(layout.rva)
            + " transfers every DWORD from offset 0 through the record footprint"
        });

        for (std::size_t offset = 0;
             offset < layout.size;
             offset += 4) {
            NativeTypeField field;
            field.index = offset / 4;
            field.offset = make_fact(
                static_cast<std::uint32_t>(offset),
                EvidenceSource::disassembly,
                Confidence::high,
                "copy operation transfers the DWORD at this record offset"
            );
            field.size = make_fact(
                std::uint32_t{4},
                EvidenceSource::disassembly,
                Confidence::high,
                "copy operation uses a 4-byte load/store pair"
            );
            type.fields.push_back(std::move(field));
        }

        model.types.push_back(std::move(type));
    }
}

std::string buffer_element_type(
    const std::string& owner)
{
    const std::string prefix = "CFastBuffer<";
    if (!owner.starts_with(prefix)
        || owner.back() != '>') {
        return {};
    }

    auto element = owner.substr(
        prefix.size(),
        owner.size() - prefix.size() - 1
    );

    while (element.starts_with("class ")
        || element.starts_with("struct ")
        || element.starts_with("union ")) {
        element.erase(0, element.find(' ') + 1);
    }

    return element;
}

std::optional<TrivialCopyLayout> inspect_buffer_add_layout(
    const BuildInfo& build,
    const MapSymbol& symbol,
    const std::vector<std::uint32_t>& function_rvas)
{
    if (!symbol.is_function
        || !symbol.name.starts_with("?Add@?$CFastBuffer@")) {
        return std::nullopt;
    }

    const auto demangled = demangle_msvc(symbol.name);
    if (demangled.empty())
        return std::nullopt;

    const auto parsed = parse_function_signature(demangled);
    if (!parsed.parsed
        || parsed.parameter_types.size() != 1
        || parsed.calling_convention != CallingConvention::thiscall_) {
        return std::nullopt;
    }

    const auto separator = parsed.qualified_name.rfind("::");
    if (separator == std::string::npos)
        return std::nullopt;

    const auto owner = parsed.qualified_name.substr(0, separator);
    const auto element = buffer_element_type(owner);
    if (element.empty())
        return std::nullopt;

    const auto parameter = parse_type_usage(
        parsed.parameter_types.front()
    );
    if (!parameter.parsed
        || parameter.base_type != element
        || parameter.pass_kind != NativeTypePassKind::lvalue_reference) {
        return std::nullopt;
    }

    const auto rva =
        symbol.virtual_address - build.executable.pe.image_base;
    const auto next = std::upper_bound(
        function_rvas.begin(),
        function_rvas.end(),
        rva
    );
    if (next == function_rvas.end() || *next <= rva)
        return std::nullopt;

    const auto start = rva_to_file_offset(build, rva);
    const auto end = rva_to_file_offset(build, *next);
    if (!start.has_value()
        || !end.has_value()
        || *end <= *start) {
        return std::nullopt;
    }

    const auto& image = *build.executable.image_data;
    const auto length = std::min<std::size_t>(
        *end - *start,
        256
    );

    for (std::size_t cursor = 0; cursor + 6 < length; ++cursor) {
        if (image[*start + cursor] != 0x89
            || image[*start + cursor + 1] != 0x1C) {
            continue;
        }

        const auto sib = image[*start + cursor + 2];
        if ((sib & 0x07) == 0x04
            || ((sib >> 3) & 0x07) == 0x04) {
            continue;
        }

        const auto scale =
            std::size_t{1} << ((sib >> 6) & 0x03);
        if (scale <= 4)
            continue;

        for (std::size_t after = cursor + 3;
             after + 3 < length && after <= cursor + 24;
             ++after) {
            if (image[*start + after] != 0x89
                || image[*start + after + 2] != sib
                || image[*start + after + 3] != 0x04) {
                continue;
            }

            return TrivialCopyLayout{
                element,
                scale,
                rva,
                symbol.name,
            };
        }
    }

    const auto has_contiguous_copy = [
        &image,
        start = *start,
        length
    ](
        std::size_t cursor,
        std::size_t dword_count
    ) {
        if (dword_count == 0 || cursor >= length)
            return false;

        if (!has_bytes(
                image,
                start + cursor,
                {0x8B, 0x11, 0x89, 0x10})) {
            return false;
        }
        cursor += 4;

        for (std::size_t offset = 4;
             offset < dword_count * 4;
             offset += 4) {
            if (cursor + 6 > length
                || image[start + cursor] != 0x8B
                || (image[start + cursor + 1] != 0x51
                    && image[start + cursor + 1] != 0x49)
                || image[start + cursor + 2]
                    != static_cast<std::uint8_t>(offset)
                || image[start + cursor + 3] != 0x89
                || (image[start + cursor + 4] != 0x50
                    && image[start + cursor + 4] != 0x48)
                || image[start + cursor + 5]
                    != static_cast<std::uint8_t>(offset)) {
                return false;
            }
            cursor += 6;
        }

        return true;
    };

    const auto has_contiguous_store_tail = [
        &image,
        start = *start,
        length
    ](
        std::size_t cursor,
        std::size_t dword_count
    ) {
        if (dword_count == 0
            || !has_bytes(image, start + cursor, {0x89, 0x10})) {
            return false;
        }
        cursor += 2;

        for (std::size_t offset = 4;
             offset < dword_count * 4;
             offset += 4) {
            if (cursor + 6 > length
                || image[start + cursor] != 0x8B
                || (image[start + cursor + 1] != 0x51
                    && image[start + cursor + 1] != 0x49)
                || image[start + cursor + 2]
                    != static_cast<std::uint8_t>(offset)
                || image[start + cursor + 3] != 0x89
                || (image[start + cursor + 4] != 0x50
                    && image[start + cursor + 4] != 0x48)
                || image[start + cursor + 5]
                    != static_cast<std::uint8_t>(offset)) {
                return false;
            }
            cursor += 6;
        }

        return true;
    };

    const auto has_count_update_and_return = [
        &image,
        start = *start,
        length
    ](std::size_t cursor) {
        for (std::size_t after = cursor;
             after + 1 < length && after <= cursor + 8;
             ++after) {
            if (!has_bytes(image, start + after, {0x89, 0x3E}))
                continue;

            for (std::size_t ret = after + 2;
                 ret + 2 < length && ret <= after + 8;
                 ++ret) {
                if (has_bytes(
                        image,
                        start + ret,
                        {0xC2, 0x04, 0x00})) {
                    return true;
                }
            }
        }

        return false;
    };

    for (std::size_t cursor = 0; cursor + 8 < length; ++cursor) {
        if (image[*start + cursor] != 0xC1
            || image[*start + cursor + 1] != 0xE0
            || image[*start + cursor + 2] < 3
            || image[*start + cursor + 2] > 7) {
            continue;
        }

        const auto stride =
            std::size_t{1} << image[*start + cursor + 2];
        const auto copy_start = cursor + 3;

        std::size_t data_add = copy_start;
        while (data_add + 3 < length
               && data_add <= copy_start + 16) {
            if (has_bytes(
                    image,
                    *start + data_add,
                    {0x03, 0x46, 0x04})) {
                break;
            }
            ++data_add;
        }

        if (data_add + 3 >= length
            || data_add > copy_start + 16
            || stride < 8
            || stride % 4 != 0) {
            continue;
        }

        std::size_t copy = data_add + 3;
        while (copy < length && copy <= data_add + 32) {
            const auto dword_count = stride / 4;
            bool copied = has_contiguous_copy(
                copy,
                dword_count
            );
            std::size_t after_copy =
                copy + 4 + (dword_count - 1) * 6;

            if (!copied) {
                copied = has_contiguous_store_tail(
                    copy,
                    dword_count
                );
                after_copy = copy + 2 + (dword_count - 1) * 6;
            }

            if (copied) {
                if (has_count_update_and_return(after_copy)) {
                    return TrivialCopyLayout{
                        element,
                        stride,
                        rva,
                        symbol.name,
                    };
                }
            }
            ++copy;
        }
    }

    for (std::size_t cursor = 0; cursor + 6 < length; ++cursor) {
        if (!has_bytes(
                image,
                *start + cursor,
                {0x8D, 0x04, 0x40})) {
            continue;
        }

        const auto base = cursor + 3;
        if (base + 6 >= length
            || !has_bytes(
                image,
                *start + base,
                {0x8D, 0x04, 0x81})) {
            continue;
        }

        std::size_t copy = base + 3;
        while (copy < length && copy <= base + 24) {
            if (has_contiguous_copy(copy, 3)) {
                const auto after_copy = copy + 4 + 2 * 6;
                if (has_count_update_and_return(after_copy)) {
                    return TrivialCopyLayout{
                        element,
                        std::size_t{12},
                        rva,
                        symbol.name,
                    };
                }
            }
            ++copy;
        }
    }

    for (std::size_t cursor = 0; cursor + 11 < length; ++cursor) {
        if (!has_bytes(
                image,
                *start + cursor,
                {0x8B, 0x0E})) {
            continue;
        }

        std::size_t data_load = cursor + 2;
        while (data_load + 3 < length
               && data_load <= cursor + 24) {
            if (has_bytes(
                    image,
                    *start + data_load,
                    {0x8B, 0x56, 0x04})) {
                break;
            }
            ++data_load;
        }

        if (data_load + 3 >= length
            || data_load > cursor + 24) {
            continue;
        }

        std::size_t address = data_load + 3;
        while (address + 2 < length
               && address <= data_load + 32) {
            if (has_bytes(
                    image,
                    *start + address,
                    {0x8D, 0x0C, 0xCA})) {
                break;
            }
            ++address;
        }

        if (address + 2 >= length
            || address > data_load + 32) {
            continue;
        }

        bool has_source_push = false;
        for (std::size_t before = data_load;
             before < address;
             ++before) {
            if (image[*start + before] >= 0x50
                && image[*start + before] <= 0x57) {
                has_source_push = true;
                break;
            }
        }

        if (!has_source_push)
            continue;

        bool has_copy_call = false;
        for (std::size_t after = address + 3;
             after + 4 < length && after <= address + 24;
             ++after) {
            if (image[*start + after] == 0xE8) {
                has_copy_call = true;
                break;
            }
        }

        if (!has_copy_call)
            continue;

        for (std::size_t after = address + 3;
             after + 4 < length && after <= address + 48;
             ++after) {
            if (has_bytes(
                    image,
                    *start + after,
                    {0x89, 0x3E, 0x5F, 0x5E, 0x83})) {
                return TrivialCopyLayout{
                    element,
                    std::size_t{8},
                    rva,
                    symbol.name,
                };
            }
        }
    }

    for (std::size_t cursor = 5; cursor + 1 < length; ++cursor) {
        if (image[*start + cursor] != 0xF3
            || image[*start + cursor + 1] != 0xA5
            || image[*start + cursor - 5] != 0xB9) {
            continue;
        }

        const auto dword_count = static_cast<std::size_t>(
            image[*start + cursor - 4]
            | (static_cast<std::uint32_t>(image[*start + cursor - 3]) << 8)
            | (static_cast<std::uint32_t>(image[*start + cursor - 2]) << 16)
            | (static_cast<std::uint32_t>(image[*start + cursor - 1]) << 24)
        );
        if (dword_count == 0 || dword_count > 0x100)
            continue;

        for (std::size_t after = cursor + 2;
             after + 2 < length;
             ++after) {
            if (!has_bytes(
                    image,
                    *start + after,
                    {0xC2, 0x04, 0x00})) {
                continue;
            }

            bool padding_after = true;
            for (std::size_t tail = after + 3;
                 tail < length;
                 ++tail) {
                if (!is_code_padding(image[*start + tail])) {
                    padding_after = false;
                    break;
                }
            }

            if (padding_after) {
                return TrivialCopyLayout{
                    element,
                    dword_count * 4,
                    rva,
                    symbol.name,
                };
            }
        }
    }

    return std::nullopt;
}

void infer_buffer_element_layouts(
    NativeModel& model,
    const BuildInfo& build,
    const std::vector<std::uint32_t>& function_rvas)
{
    std::unordered_map<std::string, TrivialCopyLayout> layouts;
    std::unordered_set<std::string> disagreements;

    for (const auto& symbol : build.map.symbols) {
        const auto layout = inspect_buffer_add_layout(
            build,
            symbol,
            function_rvas
        );
        if (!layout.has_value())
            continue;

        const auto found = layouts.find(layout->type_name);
        if (found == layouts.end()) {
            layouts.emplace(layout->type_name, *layout);
        }
        else if (found->second.size != layout->size) {
            disagreements.insert(layout->type_name);
        }
    }

    for (const auto& [name, layout] : layouts) {
        if (disagreements.contains(name)
            || model_has_type_name(model, name)
            || !model_references_type(model, name)) {
            continue;
        }

        NativeType type;
        type.id = "type:" + name;
        type.name = name;
        type.kind = make_fact(
            NativeTypeKind::record,
            EvidenceSource::demangler,
            Confidence::high,
            "CFastBuffer specialization identifies "
            + name
            + " as a C++ record type"
        );
        type.size = make_fact(
            static_cast<std::uint32_t>(layout.size),
            EvidenceSource::disassembly,
            Confidence::high,
            "CFastBuffer::Add computes this element's exact byte stride"
        );
        type.size.verifications.push_back({
            VerificationKind::abi,
            "specialized Add operation "
            + layout.decorated_name
            + " at RVA "
            + hex_id(layout.rva)
            + " copies the element into an indexed destination with this stride"
        });

        for (std::size_t offset = 0;
             offset < layout.size;
             offset += 4) {
            NativeTypeField field;
            field.index = offset / 4;
            field.offset = make_fact(
                static_cast<std::uint32_t>(offset),
                EvidenceSource::disassembly,
                Confidence::high,
                "CFastBuffer::Add copies a DWORD at this element offset"
            );
            field.size = make_fact(
                std::uint32_t{4},
                EvidenceSource::disassembly,
                Confidence::high,
                "CFastBuffer::Add uses a 4-byte element copy"
            );
            type.fields.push_back(std::move(field));
        }

        model.types.push_back(std::move(type));
    }
}

struct StaticDataLayout {
    std::string type_name;
    NativeTypeKind kind = NativeTypeKind::record;
    const MapSymbol* first = nullptr;
    const MapSymbol* second = nullptr;
};

bool is_data_symbol(
    const BuildInfo& build,
    const MapSymbol& symbol)
{
    if (symbol.is_function
        || symbol.virtual_address < build.executable.pe.image_base) {
        return false;
    }

    const auto rva =
        symbol.virtual_address - build.executable.pe.image_base;

    for (const auto& section : build.executable.pe.sections) {
        const auto section_start =
            static_cast<std::uint64_t>(section.virtual_address);
        const auto section_end =
            section_start
            + std::max(
                section.virtual_size,
                section.raw_size
            );

        if (rva >= section_start
            && rva < section_end) {
            return section.name == ".data";
        }
    }

    return false;
}

std::uint32_t read_image_u32(
    const std::vector<std::uint8_t>& image,
    std::size_t offset)
{
    return static_cast<std::uint32_t>(
        image[offset]
        | (static_cast<std::uint32_t>(image[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(image[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(image[offset + 3]) << 24)
    );
}

std::unordered_set<std::uint32_t> index_direct_dword_store_targets(
    const BuildInfo& build)
{
    std::unordered_set<std::uint32_t> targets;
    const auto& image = *build.executable.image_data;

    for (const auto& section : build.executable.pe.sections) {
        if (!section.name.starts_with(".text"))
            continue;

        const auto begin = static_cast<std::size_t>(section.raw_offset);
        const auto end = std::min<std::size_t>(
            image.size(),
            static_cast<std::size_t>(section.raw_offset)
                + section.raw_size
        );

        for (std::size_t cursor = begin; cursor < end; ++cursor) {
            if (cursor + 10 <= end
                && image[cursor] == 0xC7
                && image[cursor + 1] == 0x05) {
                targets.insert(read_image_u32(image, cursor + 2));
                continue;
            }

            if (cursor + 5 <= end
                && image[cursor] == 0xA3) {
                targets.insert(read_image_u32(image, cursor + 1));
                continue;
            }

            if (cursor + 6 <= end
                && image[cursor] == 0x89) {
                const auto modrm = image[cursor + 1];
                const auto register_direct =
                    modrm == 0x05
                    || modrm == 0x0D
                    || modrm == 0x15
                    || modrm == 0x1D
                    || modrm == 0x25
                    || modrm == 0x2D
                    || modrm == 0x35
                    || modrm == 0x3D;

                if (register_direct)
                    targets.insert(read_image_u32(image, cursor + 2));
            }
        }
    }

    return targets;
}

std::optional<std::pair<std::string, NativeTypeKind>> static_data_type(
    const std::string& decorated_name)
{
    const auto marker = decorated_name.find("@@3");
    if (marker == std::string::npos
        || marker + 4 > decorated_name.size()) {
        return std::nullopt;
    }

    const auto tag = decorated_name[marker + 3];
    if (tag != 'U' && tag != 'V' && tag != 'T')
        return std::nullopt;

    const auto type_begin = marker + 4;
    const auto type_end = decorated_name.find("@@A", type_begin);
    if (type_end == std::string::npos
        || type_end <= type_begin) {
        return std::nullopt;
    }

    const auto type_name = decorated_name.substr(
        type_begin,
        type_end - type_begin
    );

    // Keep this index conservative and fast: simple unqualified record names
    // are enough for the current data-object proof.  Nested and template
    // encodings need a full data-symbol demangler and are intentionally left
    // as missing evidence here.
    if (type_name.find_first_of("@$?") != std::string::npos)
        return std::nullopt;

    return std::pair{
        type_name,
        tag == 'T'
            ? NativeTypeKind::union_
            : NativeTypeKind::record,
    };
}

void infer_static_data_layouts(
    NativeModel& model,
    const BuildInfo& build)
{
    const auto direct_store_targets =
        index_direct_dword_store_targets(build);

    std::unordered_set<std::string> referenced_types;
    for (const auto& function : model.functions) {
        if (function.return_usage.base_type.known())
            referenced_types.insert(*function.return_usage.base_type.value);

        for (const auto& parameter : function.parameters) {
            if (parameter.usage.base_type.known())
                referenced_types.insert(*parameter.usage.base_type.value);
        }
    }

    std::unordered_map<
        std::string,
        std::vector<const MapSymbol*>
    > candidates;

    for (const auto& symbol : build.map.symbols) {
        if (!is_data_symbol(build, symbol)
            || !direct_store_targets.contains(symbol.virtual_address)) {
            continue;
        }

        const auto type = static_data_type(symbol.name);
        if (!type.has_value()
            || type->first.empty()
            || !referenced_types.contains(type->first)) {
            continue;
        }

        candidates[type->first].push_back(&symbol);
    }

    std::vector<StaticDataLayout> layouts;

    for (auto& [type_name, symbols] : candidates) {
        std::sort(
            symbols.begin(),
            symbols.end(),
            [](const auto* left, const auto* right) {
                return left->virtual_address < right->virtual_address;
            }
        );

        for (std::size_t index = 1; index < symbols.size(); ++index) {
            if (symbols[index]->virtual_address
                    - symbols[index - 1]->virtual_address
                != 4) {
                continue;
            }

            const auto type = static_data_type(symbols[index]->name);
            if (!type.has_value())
                continue;

            layouts.push_back({
                type_name,
                type->second,
                symbols[index - 1],
                symbols[index],
            });
            break;
        }
    }

    for (const auto& layout : layouts) {
        if (model_has_type_name(model, layout.type_name))
            continue;

        NativeType type;
        type.id = "type:" + layout.type_name;
        type.name = layout.type_name;
        type.kind = make_fact(
            layout.kind,
            EvidenceSource::demangler,
            Confidence::high,
            "MSVC static data symbols identify "
            + layout.type_name
            + " as a record type"
        );
        type.size = make_fact(
            std::uint32_t{4},
            EvidenceSource::disassembly,
            Confidence::medium,
            "distinct same-type data objects are four bytes apart and each has a direct 32-bit executable store"
        );
        type.size.verifications.push_back({
            VerificationKind::abi,
            "static symbols "
            + layout.first->name
            + " and "
            + layout.second->name
            + " are adjacent at four-byte alignment, while executable code directly stores a DWORD to both addresses"
        });

        NativeTypeField field;
        field.index = 0;
        field.offset = make_fact(
            std::uint32_t{0},
            EvidenceSource::disassembly,
            Confidence::medium,
            "direct executable stores address the first DWORD of the object"
        );
        field.size = make_fact(
            std::uint32_t{4},
            EvidenceSource::disassembly,
            Confidence::medium,
            "direct executable store uses a four-byte width"
        );
        type.fields.push_back(std::move(field));
        model.types.push_back(std::move(type));
    }
}

std::string static_global_owner(
    const std::string& demangled_name)
{
    const auto member_separator =
        demangled_name.rfind("::");

    if (member_separator == std::string::npos)
        return {};

    const auto owner_end =
        demangled_name.substr(0, member_separator);
    const auto owner_start = owner_end.rfind(' ');

    return owner_start == std::string::npos
        ? owner_end
        : owner_end.substr(owner_start + 1);
}

void index_static_globals(
    NativeModel& model,
    const BuildInfo& build,
    const std::string& requested_owner = {})
{
    for (const auto& symbol : build.map.symbols) {
        if (!is_data_symbol(build, symbol))
            continue;

        const auto storage_marker = symbol.name.find("@@");
        if (storage_marker == std::string::npos
            || storage_marker + 2 >= symbol.name.size()
            || symbol.name[storage_marker + 2] < '0'
            || symbol.name[storage_marker + 2] > '3') {
            continue;
        }

        const auto rva =
            symbol.virtual_address - build.executable.pe.image_base;

        NativeGlobal global;
        global.id =
            "global:" + symbol.name + "@" + hex_id(rva);
        global.name = demangle_msvc(symbol.name);
        if (global.name.empty())
            global.name = symbol.name;

        if (!requested_owner.empty()
            && static_global_owner(global.name) != requested_owner) {
            continue;
        }

        global.decorated_name = make_fact(
            symbol.name,
            EvidenceSource::map,
            Confidence::verified,
            "static-data symbol from TmForever.map"
        );
        global.rva = make_fact(
            rva,
            EvidenceSource::map,
            Confidence::verified,
            "static-data RVA from TmForever.map"
        );
        global.virtual_address = make_fact(
            symbol.virtual_address,
            EvidenceSource::map,
            Confidence::verified,
            "static-data preferred virtual address from TmForever.map"
        );

        const auto inferred_type = static_data_type(symbol.name);
        if (inferred_type.has_value()) {
            global.type = make_fact(
                inferred_type->first,
                EvidenceSource::demangler,
                Confidence::high,
                "simple MSVC static-data decoration identifies the declared record type"
            );
        }

        model.globals.push_back(std::move(global));
    }
}

bool has_special_member_functions(
    const NativeModel& model,
    const std::string& type_name)
{
    const auto type_separator = type_name.rfind("::");
    const auto leaf = type_name.substr(
        type_separator == std::string::npos
            ? 0
            : type_separator + 2
    );

    for (const auto& function : model.functions) {
        if (direct_owner_from_qualified_name(function.qualified_name)
            != type_name) {
            continue;
        }

        const auto function_separator =
            function.qualified_name.rfind("::");
        if (function_separator == std::string::npos)
            continue;

        const auto member_name = function.qualified_name.substr(
            function_separator + 2
        );
        if (member_name == leaf
            || member_name == "~" + leaf
            || member_name.starts_with("operator=")) {
            return true;
        }
    }

    return false;
}

std::optional<std::uint32_t> inspect_cdecl_record_forwarder(
    const BuildInfo& build,
    std::uint32_t rva,
    const std::vector<std::uint32_t>& function_rvas)
{
    const auto next = std::upper_bound(
        function_rvas.begin(),
        function_rvas.end(),
        rva
    );
    if (next == function_rvas.end() || *next <= rva)
        return std::nullopt;

    const auto start = rva_to_file_offset(build, rva);
    const auto end = rva_to_file_offset(build, *next);
    if (!start.has_value()
        || !end.has_value()
        || *end <= *start) {
        return std::nullopt;
    }

    const auto& image = *build.executable.image_data;
    constexpr std::size_t body_size = 17;
    if (*end - *start < body_size
        || !has_bytes(
            image,
            *start,
            {
                0x33, 0xC0, 0x50,
                0x8B, 0x44, 0x24, 0x08,
                0x50, 0xE8
            }
        )
        || !has_bytes(
            image,
            *start + 13,
            {0x83, 0xC4, 0x08, 0xC3}
        )) {
        return std::nullopt;
    }

    const auto displacement_bits =
        static_cast<std::uint32_t>(image[*start + 9])
        | (static_cast<std::uint32_t>(image[*start + 10]) << 8)
        | (static_cast<std::uint32_t>(image[*start + 11]) << 16)
        | (static_cast<std::uint32_t>(image[*start + 12]) << 24);
    const auto displacement =
        static_cast<std::int32_t>(displacement_bits);
    const auto target = static_cast<std::int64_t>(rva)
        + static_cast<std::int64_t>(body_size - 4)
        + displacement;

    if (target < 0
        || target > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }

    const auto length = std::min<std::size_t>(
        *end - *start,
        32
    );
    for (std::size_t after = body_size;
         after < length;
         ++after) {
        if (!is_code_padding(image[*start + after]))
            return std::nullopt;
    }

    return static_cast<std::uint32_t>(target);
}

struct CdeclRecordForwardLayout {
    std::string type_name;
    NativeTypeKind kind = NativeTypeKind::record;
    std::uint32_t wrapper_rva = 0;
    std::uint32_t target_rva = 0;
};

void infer_cdecl_record_forwarders(
    NativeModel& model,
    const BuildInfo& build,
    const std::vector<std::uint32_t>& function_rvas)
{
    std::unordered_map<
        std::string,
        CdeclRecordForwardLayout
    > layouts;
    std::unordered_set<std::string> disagreements;

    for (const auto& function : model.functions) {
        if (!function.calling_convention.known()
            || *function.calling_convention.value
                != CallingConvention::cdecl_
            || !function.return_type.known()
            || *function.return_type.value != "void"
            || function.parameters.size() != 1
            || !function.rva.known()) {
            continue;
        }

        const auto& parameter = function.parameters.front();
        if (!parameter.type.known()
            || !parameter.usage.pass_kind.known()
            || *parameter.usage.pass_kind.value
                != NativeTypePassKind::value
            || !parameter.usage.base_type.known()
            || !is_record_return_declaration(*parameter.type.value)
            || parameter.usage.base_type.value->starts_with("std::")
            || model_has_type_name(
                model,
                *parameter.usage.base_type.value
            )) {
            continue;
        }

        const auto target_rva = inspect_cdecl_record_forwarder(
            build,
            *function.rva.value,
            function_rvas
        );
        if (!target_rva.has_value())
            continue;

        std::vector<const NativeFunction*> targets;
        for (const auto& candidate : model.functions) {
            if (candidate.rva.known()
                && *candidate.rva.value == *target_rva) {
                targets.push_back(&candidate);
            }
        }

        if (targets.empty())
            continue;

        bool matches = true;
        for (const auto* target : targets) {
            if (!target->calling_convention.known()
                || *target->calling_convention.value
                    != CallingConvention::cdecl_
                || target->parameters.size() != 2) {
                matches = false;
                break;
            }

            for (const auto& target_parameter : target->parameters) {
                if (!target_parameter.type.known()
                    || !target_parameter.usage.pass_kind.known()
                    || *target_parameter.usage.pass_kind.value
                        != NativeTypePassKind::value
                    || !target_parameter.usage.base_type.known()
                    || *target_parameter.usage.base_type.value
                        != *parameter.usage.base_type.value) {
                    matches = false;
                    break;
                }
            }

            if (!matches)
                break;
        }

        if (!matches)
            continue;

        const auto& type_name = *parameter.usage.base_type.value;
        const auto kind = starts_with_word(
            *parameter.type.value,
            "union"
        )
            ? NativeTypeKind::union_
            : NativeTypeKind::record;

        const auto found = layouts.find(type_name);
        if (found == layouts.end()) {
            layouts.emplace(
                type_name,
                CdeclRecordForwardLayout{
                    type_name,
                    kind,
                    *function.rva.value,
                    *target_rva,
                }
            );
        }
        else if (found->second.kind != kind
            || found->second.target_rva != *target_rva) {
            disagreements.insert(type_name);
        }
    }

    for (const auto& [name, layout] : layouts) {
        if (disagreements.contains(name)
            || model_has_type_name(model, name)) {
            continue;
        }

        NativeType type;
        type.id = "type:" + name;
        type.name = name;
        type.kind = make_fact(
            layout.kind,
            EvidenceSource::demangler,
            Confidence::high,
            "MSVC signatures identify "
            + name
            + " as a C++ record type"
        );
        type.size = make_fact(
            std::uint32_t{4},
            EvidenceSource::disassembly,
            Confidence::high,
            "a cdecl wrapper forwards this record through one 4-byte stack slot"
        );
        type.size.verifications.push_back({
            VerificationKind::abi,
            "wrapper RVA "
            + hex_id(layout.wrapper_rva)
            + " loads its one by-value record parameter as one DWORD and forwards it to the two-record cdecl target at RVA "
            + hex_id(layout.target_rva)
        });

        NativeTypeField field;
        field.index = 0;
        field.offset = make_fact(
            std::uint32_t{0},
            EvidenceSource::disassembly,
            Confidence::high,
            "cdecl forwarding wrapper carries the record at offset 0"
        );
        field.size = make_fact(
            std::uint32_t{4},
            EvidenceSource::disassembly,
            Confidence::high,
            "cdecl forwarding wrapper loads the complete record stack slot as a DWORD"
        );
        type.fields.push_back(std::move(field));
        model.types.push_back(std::move(type));
    }
}

struct RecordStackLayout {
    std::string type_name;
    std::size_t size = 0;
    NativeTypeKind kind = NativeTypeKind::record;
    bool has_strict_observation = false;
    std::vector<std::string> observations;
};

void infer_record_argument_layouts(
    NativeModel& model,
    const BuildInfo& build,
    const std::vector<std::uint32_t>& function_rvas)
{
    const auto abi_type_widths = build_abi_type_widths(model);
    std::unordered_map<std::string, RecordStackLayout> layouts;
    std::unordered_set<std::string> disagreements;

    for (const auto& function : model.functions) {
        if (!function.calling_convention.known()
            || !function.rva.known()) {
            continue;
        }

        const auto convention = *function.calling_convention.value;
        if (convention != CallingConvention::thiscall_
            && convention != CallingConvention::stdcall_) {
            continue;
        }

        std::vector<const NativeParameter*> unknown_records;
        std::size_t fixed_stack_bytes = 0;
        bool strict_single_observation = true;
        bool usable = true;

        for (const auto& parameter : function.parameters) {
            const bool record_value =
                parameter.type.known()
                && parameter.usage.pass_kind.known()
                && *parameter.usage.pass_kind.value
                    == NativeTypePassKind::value
                && is_record_return_declaration(*parameter.type.value)
                && parameter.usage.base_type.known()
                && !parameter.usage.base_type.value->starts_with("std::");

            if (record_value
                && !model_has_type_name(
                    model,
                    *parameter.usage.base_type.value
                )) {
                unknown_records.push_back(&parameter);
                continue;
            }

            if (!parameter.usage.pass_kind.known()
                || (*parameter.usage.pass_kind.value
                    == NativeTypePassKind::value
                    && (!parameter.type.known()
                        || !parameter.usage.base_type.known()
                        || is_record_return_declaration(
                            *parameter.type.value
                        )
                        || starts_with_word(
                            *parameter.type.value,
                            "enum"
                        )))) {
                strict_single_observation = false;
            }

            const auto width = model_stack_parameter_width(
                parameter,
                abi_type_widths
            );
            if (!width.has_value()) {
                usable = false;
                break;
            }

            fixed_stack_bytes += *width;
        }

        if (!usable || unknown_records.size() != 1)
            continue;

        const auto& record_name =
            *unknown_records.front()->usage.base_type.value;
        const auto record_kind = starts_with_word(
            *unknown_records.front()->type.value,
            "union"
        )
            ? NativeTypeKind::union_
            : NativeTypeKind::record;
        if (has_special_member_functions(model, record_name))
            continue;

        std::size_t hidden_result_bytes = 0;
        if (function.return_type.known()
            && function.return_usage.pass_kind.known()
            && *function.return_usage.pass_kind.value
                == NativeTypePassKind::value
            && is_record_return_declaration(*function.return_type.value)) {
            if (!function.return_abi.known()
                || *function.return_abi.value
                    != NativeReturnAbi::hidden_result_pointer) {
                continue;
            }

            hidden_result_bytes = 4;
        }

        const auto cleanup = trailing_ret_cleanup(
            build,
            *function.rva.value,
            function_rvas
        );
        if (!cleanup.has_value()
            || *cleanup < fixed_stack_bytes + hidden_result_bytes) {
            continue;
        }

        const auto record_size = static_cast<std::size_t>(
            *cleanup - fixed_stack_bytes - hidden_result_bytes
        );
        if (record_size < 4
            || record_size > 0x100
            || record_size % 4 != 0) {
            continue;
        }

        auto found = layouts.find(record_name);
        if (found == layouts.end()) {
            found = layouts.emplace(
                record_name,
                RecordStackLayout{
                    record_name,
                    record_size,
                    record_kind,
                    strict_single_observation,
                    {}
                }
            ).first;
        }
        else if (found->second.size != record_size
            || found->second.kind != record_kind) {
            disagreements.insert(record_name);
            continue;
        }

        found->second.has_strict_observation =
            found->second.has_strict_observation
            || strict_single_observation;
        found->second.observations.push_back(
            function.id
            + " ret="
            + hex_id(*cleanup)
            + " residual="
            + hex_id(static_cast<std::uint32_t>(record_size))
        );
    }

    for (const auto& [name, layout] : layouts) {
        if (disagreements.contains(name)
            || (layout.observations.size() < 2
                && !layout.has_strict_observation)
            || model_has_type_name(model, name)) {
            continue;
        }

        NativeType type;
        type.id = "type:" + name;
        type.name = name;
        type.kind = make_fact(
            layout.kind,
            EvidenceSource::demangler,
            Confidence::medium,
            "MSVC signatures identify "
            + name
            + " as a C++ record type"
        );
        type.size = make_fact(
            static_cast<std::uint32_t>(layout.size),
            EvidenceSource::disassembly,
            Confidence::medium,
            "multiple callee-cleaned x86 functions reserve a consistent "
            + std::to_string(layout.size / 4)
            + "-DWORD by-value argument carrier"
        );
        type.size.verifications.push_back({
            VerificationKind::abi,
            "independent thiscall/stdcall functions have ret cleanup whose residual after all other modeled arguments is "
            + hex_id(static_cast<std::uint32_t>(layout.size))
            + ": "
            + layout.observations.front()
            + " and "
            + layout.observations.back()
        });

        for (std::size_t offset = 0;
             offset < layout.size;
             offset += 4) {
            NativeTypeField field;
            field.index = offset / 4;
            field.offset = make_fact(
                static_cast<std::uint32_t>(offset),
                EvidenceSource::disassembly,
                Confidence::medium,
                "opaque ABI carrier DWORD at this verified stack-layout offset"
            );
            field.size = make_fact(
                std::uint32_t{4},
                EvidenceSource::disassembly,
                Confidence::medium,
                "x86 stack carrier is aligned to a 4-byte ABI slot"
            );
            type.fields.push_back(std::move(field));
        }

        model.types.push_back(std::move(type));
    }
}

void add_referenced_known_types(
    NativeModel& model)
{
    if (model_references_type(model, "GmNat3")) {
        NativeType type;
        type.id = "type:GmNat3";
        type.name = "GmNat3";

        type.kind = make_fact(
            NativeTypeKind::record,
            EvidenceSource::demangler,
            Confidence::high,
            "MSVC function signatures identify GmNat3 as a C++ record type"
        );

        type.size = make_fact(
            std::uint32_t{0x0C},
            EvidenceSource::runtime,
            Confidence::high,
            "editor PlaceBlock feasibility probe validated a 12-byte by-value GmNat3 layout"
        );

        type.size.verifications.push_back({
            VerificationKind::abi,
            "GmNat3 was passed by value through the native PlaceBlock ABI"
        });

        type.size.verifications.push_back({
            VerificationKind::behavior,
            "modifying the first 32-bit component moved the placed block by one grid cell"
        });

        for (std::size_t i = 0; i < 3; ++i) {
            NativeTypeField field;
            field.index = i;

            field.offset = make_fact(
                static_cast<std::uint32_t>(i * 4),
                EvidenceSource::runtime,
                Confidence::high,
                "three consecutive 32-bit components observed in the editor PlaceBlock probe"
            );

            field.size = make_fact(
                std::uint32_t{4},
                EvidenceSource::runtime,
                Confidence::high,
                "three consecutive 32-bit components observed in the editor PlaceBlock probe"
            );

            if (i == 0) {
                field.offset.verifications.push_back({
                    VerificationKind::behavior,
                    "incrementing this component changed block placement by one grid cell"
                });
            }

            type.fields.push_back(
                std::move(field)
            );
        }

        model.types.push_back(
            std::move(type)
        );
    }

    if (model_references_type(model, "GmVec3")) {
        NativeType type;
        type.id = "type:GmVec3";
        type.name = "GmVec3";

        type.kind = make_fact(
            NativeTypeKind::record,
            EvidenceSource::demangler,
            Confidence::high,
            "MSVC function signatures identify GmVec3 as a C++ record type"
        );

        type.size = make_fact(
            std::uint32_t{0x0C},
            EvidenceSource::runtime,
            Confidence::high,
            "audio feasibility probe received native GmVec3 references using a 12-byte three-float layout"
        );
        type.size.verifications.push_back({
            VerificationKind::abi,
            "spatial CAudioPort::PlayPlugSound was intercepted with two native GmVec3 const& arguments using the 12-byte runtime probe layout"
        });
        type.size.verifications.push_back({
            VerificationKind::behavior,
            "the native spatial sound call was suppressed and resumed successfully while the GmVec3 arguments were passed through unchanged"
        });

        for (std::size_t i = 0; i < 3; ++i) {
            NativeTypeField field;
            field.index = i;
            field.offset = make_fact(
                static_cast<std::uint32_t>(i * 4),
                EvidenceSource::runtime,
                Confidence::high,
                "runtime audio probe modeled three consecutive 32-bit float components"
            );
            field.size = make_fact(
                std::uint32_t{4},
                EvidenceSource::runtime,
                Confidence::high,
                "runtime audio probe modeled three consecutive 32-bit float components"
            );
            field.type = make_fact(
                std::string{"float"},
                EvidenceSource::runtime,
                Confidence::high,
                "runtime audio probe used float components for the native GmVec3 ABI"
            );
            type.fields.push_back(std::move(field));
        }

        model.types.push_back(std::move(type));
    }

    if (model_references_type(model, "GmIso4")) {
        NativeType type;
        type.id = "type:GmIso4";
        type.name = "GmIso4";

        type.kind = make_fact(
            NativeTypeKind::record,
            EvidenceSource::demangler,
            Confidence::high,
            "MSVC function signatures identify GmIso4 as a C++ record type"
        );

        type.size = make_fact(
            std::uint32_t{0x30},
            EvidenceSource::disassembly,
            Confidence::high,
            "CFastBuffer<GmIso4>::Add uses a 48-byte element stride and copies 12 DWORDs"
        );
        type.size.verifications.push_back({
            VerificationKind::abi,
            "CFastBuffer<GmIso4>::Add at VA 0x004B32B0 computes index * 48 and copies ECX=0x0C DWORDs into each element"
        });
        type.size.verifications.push_back({
            VerificationKind::static_analysis,
            "CTrackManiaEditor::GetGridLocation writes the final three 32-bit components at offsets 0x24, 0x28 and 0x2C of its GmIso4 result"
        });

        for (std::size_t i = 0; i < 12; ++i) {
            NativeTypeField field;
            field.index = i;
            field.offset = make_fact(
                static_cast<std::uint32_t>(i * 4),
                EvidenceSource::disassembly,
                Confidence::high,
                "GmIso4 is copied as 12 consecutive DWORD components"
            );
            field.size = make_fact(
                std::uint32_t{4},
                EvidenceSource::disassembly,
                Confidence::high,
                "GmIso4 is copied as 12 consecutive DWORD components"
            );
            type.fields.push_back(std::move(field));
        }

        model.types.push_back(std::move(type));
    }

    if (model_references_type(model, "ECardinalDir")) {
        NativeType type;
        type.id = "type:ECardinalDir";
        type.name = "ECardinalDir";

        type.kind = make_fact(
            NativeTypeKind::enum_,
            EvidenceSource::demangler,
            Confidence::high,
            "MSVC function signatures identify ECardinalDir as an enum type"
        );

        type.size = make_fact(
            std::uint32_t{0x04},
            EvidenceSource::disassembly,
            Confidence::high,
            "CGameCtnBlockInfoRectAsym enum-reference helpers read ECardinalDir through 32-bit memory operands"
        );

        type.size.verifications.push_back({
            VerificationKind::abi,
            "GetIndexTShaped at VA 0x006FEBB0 dereferences ECardinalDir& with a DWORD load; CGameCtnUtils enum helpers also pass and return it in 32-bit x86 operands"
        });

        model.types.push_back(
            std::move(type)
        );
    }

    if (model_references_type(model, "CAudioPort::EBalanceGroup")) {
        NativeType type;
        type.id = "type:CAudioPort::EBalanceGroup";
        type.name = "CAudioPort::EBalanceGroup";

        type.kind = make_fact(
            NativeTypeKind::enum_,
            EvidenceSource::demangler,
            Confidence::high,
            "MSVC function signatures identify CAudioPort::EBalanceGroup as an enum type"
        );

        type.size = make_fact(
            std::uint32_t{0x04},
            EvidenceSource::disassembly,
            Confidence::high,
            "CAudioPort::AddSound reads EBalanceGroup from a 32-bit x86 stack slot"
        );
        type.size.verifications.push_back({
            VerificationKind::abi,
            "CAudioPort::AddSound at VA 0x0079E970 uses DWORD stack operands for EBalanceGroup and returns with ret 0x0C for its three 32-bit arguments"
        });

        model.types.push_back(std::move(type));
    }

    if (model_references_type(model, "CSceneMobil::EDoMobilPtrVersion")) {
        NativeType type;
        type.id = "type:CSceneMobil::EDoMobilPtrVersion";
        type.name = "CSceneMobil::EDoMobilPtrVersion";

        type.kind = make_fact(
            NativeTypeKind::enum_,
            EvidenceSource::demangler,
            Confidence::high,
            "MSVC function signatures identify CSceneMobil::EDoMobilPtrVersion as an enum type"
        );

        type.size = make_fact(
            std::uint32_t{0x04},
            EvidenceSource::disassembly,
            Confidence::high,
            "CSceneMobil::DoMobilPtr consumes EDoMobilPtrVersion through a 32-bit x86 stack operand"
        );

        type.size.verifications.push_back({
            VerificationKind::abi,
            "CSceneMobil::DoMobilPtr at VA 0x007B27A0 begins with cmp dword ptr [esp+0x0C], 0 for its third cdecl argument EDoMobilPtrVersion"
        });

        model.types.push_back(std::move(type));
    }

    if (model_references_type(model, "ESceneMobilQuality")) {
        NativeType type;
        type.id = "type:ESceneMobilQuality";
        type.name = "ESceneMobilQuality";

        type.kind = make_fact(
            NativeTypeKind::enum_,
            EvidenceSource::demangler,
            Confidence::high,
            "MSVC function signatures identify ESceneMobilQuality as an enum type"
        );

        type.size = make_fact(
            std::uint32_t{0x04},
            EvidenceSource::disassembly,
            Confidence::high,
            "CSceneMobil::SetQuality uses one 4-byte explicit x86 stack argument"
        );

        type.size.verifications.push_back({
            VerificationKind::abi,
            "CSceneMobil::SetQuality resolves to the folded stub at VA 0x00961DD0 containing ret 0x04, confirming a 4-byte explicit argument slot"
        });

        model.types.push_back(std::move(type));
    }
    if (model_references_type(model, "ESceneMobil_TimeBase")) {
        NativeType type;
        type.id = "type:ESceneMobil_TimeBase";
        type.name = "ESceneMobil_TimeBase";

        type.kind = make_fact(
            NativeTypeKind::enum_,
            EvidenceSource::demangler,
            Confidence::high,
            "MSVC function signatures identify ESceneMobil_TimeBase as an enum type"
        );

        type.size = make_fact(
            std::uint32_t{0x04},
            EvidenceSource::disassembly,
            Confidence::high,
            "CMotionCmdBase::SetBaseTimer reads ESceneMobil_TimeBase from a 32-bit x86 stack slot"
        );

        type.size.verifications.push_back({
            VerificationKind::abi,
            "CMotionCmdBase::SetBaseTimer at VA 0x004390B0 uses mov eax, dword ptr [esp+0x04], stores eax as a DWORD, and returns with ret 0x04"
        });

        model.types.push_back(std::move(type));
    }
}

bool identifier_character(char value)
{
    return std::isalnum(
        static_cast<unsigned char>(value)
    ) != 0
        || value == '_';
}

bool declaration_mentions_type(
    const std::string& declaration,
    const std::string& type_name)
{
    std::size_t position = 0;

    while (
        (position = declaration.find(
            type_name,
            position
        )) != std::string::npos
    ) {
        const auto end =
            position + type_name.size();

        const bool left_ok =
            position == 0
            || (
                !identifier_character(
                    declaration[position - 1]
                )
                && declaration[position - 1] != ':'
            );

        const bool right_ok =
            end == declaration.size()
            || (
                !identifier_character(
                    declaration[end]
                )
                && declaration[end] != ':'
            );

        if (left_ok && right_ok)
            return true;

        position = end;
    }

    return false;
}

void resolve_type_references(
    NativeModel& model)
{
    for (auto& function : model.functions) {
        if (function.return_type.known()) {
            for (const auto& type : model.types) {
                if (!declaration_mentions_type(
                        *function.return_type.value,
                        type.name)) {
                    continue;
                }

                function.return_type_id =
                    make_fact(
                        type.id,
                        EvidenceSource::demangler,
                        Confidence::high,
                        "declaration matched canonical native type"
                    );

                break;
            }
        }

        for (auto& parameter :
             function.parameters) {
            if (!parameter.type.known())
                continue;

            for (const auto& type : model.types) {
                if (!declaration_mentions_type(
                        *parameter.type.value,
                        type.name)) {
                    continue;
                }

                parameter.referenced_type_id =
                    make_fact(
                        type.id,
                        EvidenceSource::demangler,
                        Confidence::high,
                        "declaration matched canonical native type"
                    );

                break;
            }
        }
    }
}
} // namespace

NativeModel build_native_model(
    const BuildInfo& build,
    const std::string& requested_class_name)
{
    NativeModel model;

    add_build(model, build);
    index_static_globals(model, build, requested_class_name);

    const auto class_info =
        inspect_class(
            build,
            requested_class_name
        );

    const auto rtti =
        inspect_rtti(
            build,
            requested_class_name
        );

    const auto count_class_static =
        [&](const std::string& member_name)
    {
        const std::string marker =
            member_name
            + "@"
            + requested_class_name
            + "@@";

        std::size_t count = 0;

        for (const auto& symbol :
             build.map.symbols) {
            if (
                symbol.name.find(marker)
                != std::string::npos
            ) {
                ++count;
            }
        }

        return count;
    };

    const auto param_infos_matches =
        count_class_static(
            "m_ParamInfos"
        );

    const auto param_count_matches =
        count_class_static(
            "m_ParamInfoCount"
        );

    if (
        param_infos_matches > 1
        || param_count_matches > 1
    ) {
        throw std::runtime_error(
            "multiple reflection statics matched class: "
            + requested_class_name
        );
    }

    if (
        param_infos_matches
        != param_count_matches
    ) {
        throw std::runtime_error(
            "incomplete reflection metadata for class: "
            + requested_class_name
        );
    }

    ReflectionInfo reflection;

    if (param_infos_matches == 1) {
        reflection =
            inspect_reflection(
                build,
                requested_class_name
            );
    }
    VtableInfo vtable;

    if (class_info.vtable_state != ClassVtableState::absent) {
        try {
            vtable =
                inspect_vtable(
                    build,
                    requested_class_name
                );
        }
        catch (const std::exception& error) {
            throw std::runtime_error(
                "vtable inspection failed for class "
                + requested_class_name
                + ": "
                + error.what()
            );
        }
    }

    NativeClass native_class;
    native_class.id = class_id(requested_class_name);
    native_class.name = requested_class_name;

    if (const auto mw_class_id = infer_mw_class_id(
            build,
            requested_class_name
        ); mw_class_id.has_value()) {
        native_class.mw_class_id = std::move(*mw_class_id);
    }

    const auto vtable_state =
        vtable.state == VtableInfo::State::valid
            ? NativeVtableState::valid
            : vtable.state == VtableInfo::State::malformed
                ? NativeVtableState::malformed
                : NativeVtableState::absent;

    native_class.vtable_state = make_fact(
        vtable_state,
        EvidenceSource::vtable,
        vtable_state == NativeVtableState::malformed
            ? Confidence::low
            : Confidence::verified,
        vtable_state == NativeVtableState::valid
            ? "direct vftable evidence was parsed"
            : vtable_state == NativeVtableState::malformed
                ? "direct vftable symbol was present but its contents were not parseable"
                : "no direct vftable symbol was present in the MAP"
    );

    if (vtable_state == NativeVtableState::malformed) {
        native_class.vtable_error = make_fact(
            vtable.error,
            EvidenceSource::vtable,
            Confidence::low,
            "vftable parser diagnostic"
        );
    }

    std::unordered_set<std::string>
        hierarchy_owners;

    hierarchy_owners.insert(
        requested_class_name
    );

    for (const auto& base : rtti.bases) {
        const auto name =
            clean_rtti_name(
                base.raw_type_name
            );

        if (!name.empty())
            hierarchy_owners.insert(name);

        if (name.empty()
            || name == requested_class_name) {
            continue;
        }

        NativeHierarchyEntry entry;

        entry.class_id =
            class_id(name);

        entry.contained_bases =
            base.contained_bases;

        entry.member_displacement =
            make_fact(
                base.pmd.mdisp,
                EvidenceSource::rtti,
                Confidence::verified,
                "MSVC RTTI PMD.mdisp"
            );

        entry.vbtable_displacement =
            make_fact(
                base.pmd.pdisp,
                EvidenceSource::rtti,
                Confidence::verified,
                "MSVC RTTI PMD.pdisp"
            );

        entry.displacement_inside_vbtable =
            make_fact(
                base.pmd.vdisp,
                EvidenceSource::rtti,
                Confidence::verified,
                "MSVC RTTI PMD.vdisp"
            );

        native_class.rtti_hierarchy.push_back(
            std::move(entry)
        );
    }

    std::unordered_map<
        std::uint32_t,
        std::size_t
    > physical_by_rva;

    std::unordered_map<
        std::uint32_t,
        std::vector<std::string>
    > functions_by_rva;

    std::unordered_set<std::string>
        known_function_ids;

    const auto ensure_physical_code =
        [&](std::uint32_t rva,
            std::uint32_t virtual_address,
            EvidenceSource source,
            Confidence confidence,
            const std::string& detail) -> std::size_t
    {
        const auto found =
            physical_by_rva.find(rva);

        if (found != physical_by_rva.end())
            return found->second;

        PhysicalCode code;

        code.id =
            physical_code_id(rva);

        code.rva = make_fact(
            rva,
            source,
            confidence,
            detail
        );

        if (virtual_address != 0) {
            code.virtual_address = make_fact(
                virtual_address,
                source,
                confidence,
                "physical code preferred virtual address"
            );
        }

        const auto index =
            model.physical_code.size();

        physical_by_rva.emplace(
            rva,
            index
        );

        model.physical_code.push_back(
            std::move(code)
        );

        return index;
    };

    const auto add_logical_function =
        [&](const std::string& name,
            const std::string& decorated_name,
            std::uint32_t rva,
            std::uint32_t virtual_address,
            EvidenceSource source,
            Confidence confidence,
            const std::string& detail)
            -> std::string
    {
        const auto id =
            function_id(
                name,
                rva
            );

        if (!known_function_ids.contains(id)) {
            NativeFunction function;

            function.id = id;
            function.qualified_name = name;

            if (!decorated_name.empty()) {
                function.decorated_name =
                    make_fact(
                        decorated_name,
                        source,
                        confidence,
                        detail
                    );
            }

            const auto parsed =
                parse_function_signature(name);

            if (parsed.parsed) {
                function.qualified_name =
                    parsed.qualified_name;

                function.return_type =
                    make_fact(
                        parsed.return_type,
                        EvidenceSource::demangler,
                        Confidence::high,
                        "parsed from complete MSVC demangled declaration"
                    );

                apply_type_usage(
                    function.return_usage,
                    parse_type_usage(
                        parsed.return_type
                    )
                );

                function.calling_convention =
                    make_fact(
                        parsed.calling_convention,
                        EvidenceSource::demangler,
                        Confidence::high,
                        "parsed from complete MSVC demangled declaration"
                    );

                NativeAccess access =
                    NativeAccess::unknown;

                switch (parsed.access) {
                case AccessLevel::public_:
                    access = NativeAccess::public_;
                    break;

                case AccessLevel::protected_:
                    access = NativeAccess::protected_;
                    break;

                case AccessLevel::private_:
                    access = NativeAccess::private_;
                    break;

                case AccessLevel::unknown:
                    break;
                }

                function.access =
                    make_fact(
                        access,
                        EvidenceSource::demangler,
                        Confidence::high,
                        "parsed from complete MSVC demangled declaration"
                    );

                function.is_virtual =
                    make_fact(
                        parsed.is_virtual,
                        EvidenceSource::demangler,
                        Confidence::high,
                        "parsed from complete MSVC demangled declaration"
                    );

                function.is_static =
                    make_fact(
                        parsed.is_static,
                        EvidenceSource::demangler,
                        Confidence::high,
                        "parsed from complete MSVC demangled declaration"
                    );

                function.is_const =
                    make_fact(
                        parsed.is_const,
                        EvidenceSource::demangler,
                        Confidence::high,
                        "parsed from complete MSVC demangled declaration"
                    );

                for (std::size_t i = 0;
                     i < parsed.parameter_types.size();
                     ++i) {
                    NativeParameter parameter;

                    parameter.index = i;

                    parameter.type =
                        make_fact(
                            parsed.parameter_types[i],
                            EvidenceSource::demangler,
                            Confidence::high,
                            "parsed from complete MSVC demangled declaration"
                        );

                    apply_type_usage(
                        parameter.usage,
                        parse_type_usage(
                            parsed.parameter_types[i]
                        )
                    );

                    function.parameters.push_back(
                        std::move(parameter)
                    );
                }
            }
            else {
                function.signature_parse_error =
                    make_fact(
                        parsed.error,
                        EvidenceSource::demangler,
                        Confidence::high,
                        "structured signature parser rejected declaration"
                    );
            }

            apply_known_return_abi(
                function,
                decorated_name
            );

            function.rva = make_fact(
                rva,
                source,
                confidence,
                detail
            );

            if (virtual_address != 0) {
                function.virtual_address =
                    make_fact(
                        virtual_address,
                        source,
                        confidence,
                        "preferred virtual address"
                    );
            }

            const auto code_id =
                physical_code_id(rva);

            function.physical_code_id =
                make_fact(
                    code_id,
                    source,
                    confidence,
                    "logical function maps to this physical RVA"
                );

            model.functions.push_back(
                std::move(function)
            );

            known_function_ids.insert(id);
        }

        auto& ids =
            functions_by_rva[rva];

        bool already_linked = false;

        for (const auto& existing : ids) {
            if (existing == id) {
                already_linked = true;
                break;
            }
        }

        if (!already_linked)
            ids.push_back(id);

        const auto code_index =
            ensure_physical_code(
                rva,
                virtual_address,
                source,
                confidence,
                detail
            );

        auto& code =
            model.physical_code[code_index];

        bool code_has_function = false;

        for (const auto& existing :
             code.logical_function_ids) {
            if (existing == id) {
                code_has_function = true;
                break;
            }
        }

        if (!code_has_function)
            code.logical_function_ids.push_back(id);

        return id;
    };

    for (const auto& symbol : class_info.symbols) {
        if (!symbol.usable_function
            || !symbol.direct_owner) {
            continue;
        }

        const std::string name =
            !symbol.demangled_name.empty()
                ? symbol.demangled_name
                : symbol.decorated_name;

        const auto id =
            add_logical_function(
                name,
                symbol.decorated_name,
                symbol.rva,
                symbol.virtual_address,
                EvidenceSource::map,
                Confidence::verified,
                "direct-owner function from TmForever.map"
            );

        native_class.function_ids.push_back(id);
    }

    append_reflection_data(
        model,
        native_class,
        requested_class_name,
        reflection
    );

    if (vtable.virtual_address != 0) {
        NativeVtable native_vtable;

        native_vtable.id =
            vtable_id(requested_class_name);

        native_vtable.rva = make_fact(
            vtable.rva,
            EvidenceSource::vtable,
            Confidence::verified,
            "vftable RVA"
        );

        native_vtable.virtual_address =
            make_fact(
                vtable.virtual_address,
                EvidenceSource::vtable,
                Confidence::verified,
                "vftable preferred virtual address"
            );

        for (const auto& slot : vtable.slots) {
            NativeVtableSlot native_slot;

            native_slot.index = slot.index;

            native_slot.target_rva =
                make_fact(
                    slot.target_rva,
                    EvidenceSource::vtable,
                    Confidence::verified,
                    "vtable entry target RVA"
                );

            if (slot.target_rva != 0) {
                native_slot.physical_code_id =
                    make_fact(
                        physical_code_id(
                            slot.target_rva
                        ),
                        EvidenceSource::vtable,
                        Confidence::verified,
                        "vtable slot targets this physical RVA"
                    );

                const std::uint32_t target_va =
                    build.executable.pe.image_base
                    + slot.target_rva;

                ensure_physical_code(
                    slot.target_rva,
                    target_va,
                    EvidenceSource::vtable,
                    Confidence::verified,
                    "physical code observed through vtable"
                );

                for (const auto& alias :
                     slot.aliases) {
                    if (!belongs_to_hierarchy(
                            alias.demangled_name,
                            hierarchy_owners)) {
                        continue;
                    }

                    const auto name =
                        alias_name(alias);

                    if (name.empty())
                        continue;

                    const auto id =
                        add_logical_function(
                            name,
                            alias.decorated_name,
                            slot.target_rva,
                            target_va,
                            EvidenceSource::vtable,
                            Confidence::high,
                            "hierarchy-compatible logical identity from vtable alias resolution"
                        );

                    bool already_in_slot = false;

                    for (const auto& existing :
                         native_slot.candidate_function_ids) {
                        if (existing == id) {
                            already_in_slot = true;
                            break;
                        }
                    }

                    if (!already_in_slot) {
                        native_slot.candidate_function_ids.push_back(
                            id
                        );
                    }
                }

                if (native_slot.candidate_function_ids.empty()) {
                    const auto found =
                        functions_by_rva.find(
                            slot.target_rva
                        );

                    if (found != functions_by_rva.end()) {
                        for (const auto& id :
                             found->second) {
                            bool hierarchy_match = false;

                            for (const auto& function :
                                 model.functions) {
                                if (function.id != id)
                                    continue;

                                hierarchy_match =
                                    belongs_to_hierarchy(
                                        function.qualified_name,
                                        hierarchy_owners
                                    );

                                break;
                            }

                            if (hierarchy_match) {
                                native_slot.candidate_function_ids.push_back(
                                    id
                                );
                            }
                        }
                    }
                }
            }

            resolve_unique_vtable_slot(
                model,
                native_slot
            );

            native_vtable.slots.push_back(
                std::move(native_slot)
            );
        }

        native_class.vtable_id =
            make_fact(
                native_vtable.id,
                EvidenceSource::vtable,
                Confidence::verified,
                "direct class vftable"
            );

        model.vtables.push_back(
            std::move(native_vtable)
        );
    }

    model.classes.push_back(
        std::move(native_class)
    );

    add_referenced_known_types(model);
    resolve_type_references(model);

    return model;
}

NativeModel build_native_model_all(
    const BuildInfo& build)
{
    if (!build.matches()) {
        throw std::runtime_error(
            "cannot build whole native model from mismatched executable and map"
        );
    }

    std::set<std::string> class_names;

    for (const auto& symbol : build.map.symbols) {
        if (!symbol.is_function)
            continue;

        const auto first_at = symbol.name.find('@');
        std::size_t owner_start =
            first_at == std::string::npos
                ? std::string::npos
                : first_at + 1;

        if (symbol.name.rfind("??0", 0) == 0
            || symbol.name.rfind("??1", 0) == 0) {
            owner_start = 3;
        }
        else if (symbol.name.rfind("??_G", 0) == 0
            || symbol.name.rfind("??_E", 0) == 0) {
            owner_start = 4;
        }

        if (owner_start == std::string::npos)
            continue;

        const auto owner_end =
            symbol.name.find("@@", owner_start);

        if (owner_end == std::string::npos
            || owner_end <= owner_start)
            continue;

        const auto class_name =
            symbol.name.substr(
                owner_start,
                owner_end - owner_start
            );

        if (!class_name.empty()
            && class_name.find('@') == std::string::npos) {
            class_names.insert(class_name);
        }
    }

    NativeModel result;
    add_build(result, build);
    index_static_globals(result, build);

    const auto has_id = [](const auto& values, const std::string& id) {
        return std::any_of(
            values.begin(),
            values.end(),
            [&](const auto& value) { return value.id == id; }
        );
    };

    for (const auto& class_name : class_names) {
        NativeModel part;

        try {
            part = build_native_model(build, class_name);
        }
        catch (const std::exception& error) {
            // Some compiler-generated or malformed MAP ownership contexts
            // cannot be reconstructed as standalone classes. They are
            // intentionally omitted from this first inventory pass; direct
            // MAP symbol ingestion remains the follow-up path for them.
            continue;
        }

        for (auto& type : part.types) {
            if (!has_id(result.types, type.id))
                result.types.push_back(std::move(type));
        }

        for (auto& native_class : part.classes) {
            const auto found = std::find_if(
                result.classes.begin(),
                result.classes.end(),
                [&](const auto& existing) {
                    return existing.id == native_class.id;
                }
            );
            if (found == result.classes.end()) {
                result.classes.push_back(std::move(native_class));
            }
            else {
                for (const auto& descriptor_id :
                     native_class.reflection_descriptor_ids) {
                    if (std::find(
                            found->reflection_descriptor_ids.begin(),
                            found->reflection_descriptor_ids.end(),
                            descriptor_id
                        ) == found->reflection_descriptor_ids.end()) {
                        found->reflection_descriptor_ids.push_back(
                            descriptor_id
                        );
                    }
                }
            }
        }

        for (auto& member : part.members) {
            if (!has_id(result.members, member.id))
                result.members.push_back(std::move(member));
        }

        for (auto& descriptor : part.reflection_descriptors) {
            if (!has_id(result.reflection_descriptors, descriptor.id))
                result.reflection_descriptors.push_back(std::move(descriptor));
        }

        for (auto& global : part.globals) {
            if (!has_id(result.globals, global.id))
                result.globals.push_back(std::move(global));
        }

        for (auto& function : part.functions) {
            if (!has_id(result.functions, function.id))
                result.functions.push_back(std::move(function));
        }

        for (auto& code : part.physical_code) {
            const auto found = std::find_if(
                result.physical_code.begin(),
                result.physical_code.end(),
                [&](const auto& existing) { return existing.id == code.id; }
            );

            if (found == result.physical_code.end()) {
                result.physical_code.push_back(std::move(code));
            }
            else {
                for (const auto& function_id : code.logical_function_ids) {
                    if (std::find(
                            found->logical_function_ids.begin(),
                            found->logical_function_ids.end(),
                            function_id
                        ) == found->logical_function_ids.end()) {
                        found->logical_function_ids.push_back(function_id);
                    }
                }
            }
        }

        for (auto& vtable : part.vtables) {
            if (!has_id(result.vtables, vtable.id))
                result.vtables.push_back(std::move(vtable));
        }
    }

    resolve_type_references(result);
    return result;
}

NativeModel build_native_model_all_fast(
    const BuildInfo& build)
{
    if (!build.matches()) {
        throw std::runtime_error(
            "cannot build whole native model from mismatched executable and map"
        );
    }

    NativeModel model;
    add_build(model, build);
    index_static_globals(model, build);

    std::unordered_set<std::uint32_t> physical_seen;
    std::unordered_set<std::string> function_seen;
    std::unordered_map<std::uint32_t, std::size_t> physical_indices;
    std::unordered_map<std::string, std::size_t> function_indices;
    std::unordered_map<std::string, std::size_t> class_indices;

    std::vector<std::uint32_t> function_rvas;
    function_rvas.reserve(build.map.symbols.size());
    for (const auto& symbol : build.map.symbols) {
        if (symbol.is_function
            && symbol.virtual_address >= build.executable.pe.image_base) {
            function_rvas.push_back(
                symbol.virtual_address
                - build.executable.pe.image_base
            );
        }
    }
    std::sort(function_rvas.begin(), function_rvas.end());
    function_rvas.erase(
        std::unique(function_rvas.begin(), function_rvas.end()),
        function_rvas.end()
    );

    const auto ensure_class = [&](const std::string& name) -> NativeClass& {
        const auto found = class_indices.find(name);
        if (found != class_indices.end())
            return model.classes[found->second];

        const auto index = model.classes.size();
        class_indices.emplace(name, index);
        NativeClass native_class;
        native_class.id = class_id(name);
        native_class.name = name;
        if (const auto mw_class_id = infer_mw_class_id(
                build,
                name
            ); mw_class_id.has_value()) {
            native_class.mw_class_id = std::move(*mw_class_id);
        }
        native_class.vtable_state = make_fact(
            NativeVtableState::absent,
            EvidenceSource::vtable,
            Confidence::verified,
            "no direct vftable symbol was present in the MAP"
        );
        model.classes.push_back(std::move(native_class));
        return model.classes.back();
    };

    for (const auto& symbol : build.map.symbols) {
        if (!symbol.is_function
            || symbol.virtual_address < build.executable.pe.image_base) {
            continue;
        }

        const auto demangled = demangle_msvc(symbol.name);
        if (demangled.empty())
            continue;

        const auto parsed = parse_function_signature(demangled);
        if (!parsed.parsed)
            continue;

        const auto rva =
            symbol.virtual_address - build.executable.pe.image_base;
        const auto id = function_id(parsed.qualified_name, rva);

        std::size_t function_index = 0;

        if (function_seen.insert(id).second) {
            NativeFunction function;
            function.id = id;
            function.qualified_name = parsed.qualified_name;
            function.decorated_name = make_fact(
                symbol.name,
                EvidenceSource::map,
                Confidence::verified,
                "function symbol from TmForever.map"
            );
            function.return_type = make_fact(
                parsed.return_type,
                EvidenceSource::demangler,
                Confidence::high,
                "parsed from complete MSVC demangled declaration"
            );
            apply_type_usage(
                function.return_usage,
                parse_type_usage(parsed.return_type)
            );
            function.calling_convention = make_fact(
                parsed.calling_convention,
                EvidenceSource::demangler,
                Confidence::high,
                "parsed from complete MSVC demangled declaration"
            );
            function.access = make_fact(
                parsed.access == AccessLevel::public_
                    ? NativeAccess::public_
                    : parsed.access == AccessLevel::protected_
                        ? NativeAccess::protected_
                        : parsed.access == AccessLevel::private_
                            ? NativeAccess::private_
                            : NativeAccess::unknown,
                EvidenceSource::demangler,
                Confidence::high,
                "parsed from complete MSVC demangled declaration"
            );
            function.is_virtual = make_fact(parsed.is_virtual, EvidenceSource::demangler, Confidence::high, "parsed from complete MSVC demangled declaration");
            function.is_static = make_fact(parsed.is_static, EvidenceSource::demangler, Confidence::high, "parsed from complete MSVC demangled declaration");
            function.is_const = make_fact(parsed.is_const, EvidenceSource::demangler, Confidence::high, "parsed from complete MSVC demangled declaration");

            for (std::size_t i = 0; i < parsed.parameter_types.size(); ++i) {
                NativeParameter parameter;
                parameter.index = i;
                parameter.type = make_fact(
                    parsed.parameter_types[i],
                    EvidenceSource::demangler,
                    Confidence::high,
                    "parsed from complete MSVC demangled declaration"
                );
                apply_type_usage(
                    parameter.usage,
                    parse_type_usage(parsed.parameter_types[i])
                );
                function.parameters.push_back(std::move(parameter));
            }

            function.rva = make_fact(rva, EvidenceSource::map, Confidence::verified, "function RVA from TmForever.map");
            function.virtual_address = make_fact(symbol.virtual_address, EvidenceSource::map, Confidence::verified, "function VA from TmForever.map");
            apply_known_return_abi(function, symbol.name);
            apply_inferred_hidden_result_abi(
                function,
                build,
                function_rvas
            );
            function.physical_code_id = make_fact(physical_code_id(rva), EvidenceSource::map, Confidence::verified, "logical function maps to this physical RVA");
            function_index = model.functions.size();
            function_indices.emplace(id, function_index);
            model.functions.push_back(std::move(function));
        }
        else {
            function_index = function_indices.at(id);
        }

        const auto owner =
            direct_owner_from_qualified_name(
                parsed.qualified_name
            );

        if (!owner.empty()) {
            auto& native_class = ensure_class(owner);
            const auto& function_id_value =
                model.functions[function_index].id;

            if (std::find(
                    native_class.function_ids.begin(),
                    native_class.function_ids.end(),
                    function_id_value
                ) == native_class.function_ids.end()) {
                native_class.function_ids.push_back(function_id_value);
            }
        }

        if (physical_seen.insert(rva).second) {
            PhysicalCode code;
            code.id = physical_code_id(rva);
            code.rva = make_fact(rva, EvidenceSource::map, Confidence::verified, "physical code RVA from TmForever.map");
            code.virtual_address = make_fact(symbol.virtual_address, EvidenceSource::map, Confidence::verified, "physical code VA from TmForever.map");
            physical_indices.emplace(rva, model.physical_code.size());
            model.physical_code.push_back(std::move(code));
        }

        const auto code_index = physical_indices.at(rva);
        auto& code = model.physical_code[code_index];
        const auto& logical_id = model.functions[function_index].id;

        if (std::find(
                code.logical_function_ids.begin(),
                code.logical_function_ids.end(),
                logical_id
            ) == code.logical_function_ids.end()) {
            code.logical_function_ids.push_back(logical_id);
        }
    }

    add_referenced_known_types(model);
    infer_trivial_record_layouts(model, build, function_rvas);
    infer_buffer_element_layouts(model, build, function_rvas);
    infer_static_data_layouts(model, build);
    infer_enum_abi_types(model, build, function_rvas);
    infer_enum_return_types(model, build, function_rvas);
    infer_cdecl_record_forwarders(model, build, function_rvas);
    infer_record_argument_layouts(model, build, function_rvas);
    apply_model_inferred_hidden_result_abi(model, build, function_rvas);

    std::unordered_map<
        std::uint32_t,
        std::vector<std::string>
    > function_ids_by_rva;

    for (const auto& function : model.functions) {
        function_ids_by_rva[*function.rva.value].push_back(function.id);
    }

    for (auto& native_class : model.classes) {
        native_class.vtable_state = make_fact(
            NativeVtableState::absent,
            EvidenceSource::vtable,
            Confidence::verified,
            "no direct vftable symbol was present in the MAP"
        );
    }

    std::unordered_map<
        std::string,
        std::vector<const MapSymbol*>
    > vtable_symbols_by_owner;

    for (const auto& symbol : build.map.symbols) {
        if (!symbol.name.starts_with("??_7")
            || !symbol.name.ends_with("@@6B@")) {
            continue;
        }

        const auto owner = vtable_owner_from_symbol(symbol);
        if (!owner.empty())
            vtable_symbols_by_owner[owner].push_back(&symbol);
    }

    for (const auto& [owner, symbols] : vtable_symbols_by_owner) {
        ensure_class(owner);
        const auto owner_index = class_indices.at(owner);

        if (symbols.size() != 1) {
            model.classes[owner_index].vtable_state = make_fact(
                NativeVtableState::malformed,
                EvidenceSource::vtable,
                Confidence::low,
                "multiple direct vftable symbols resolved to the same class"
            );
            model.classes[owner_index].vtable_error = make_fact(
                std::string{
                    "multiple direct vftable symbols resolved to the same class"
                },
                EvidenceSource::vtable,
                Confidence::low,
                "vftable index detected an ambiguous class mapping"
            );
            continue;
        }

        const auto vtable = inspect_vtable_symbol(
            build,
            *symbols.front(),
            owner,
            false
        );

        const auto state =
            vtable.state == VtableInfo::State::valid
                ? NativeVtableState::valid
                : NativeVtableState::malformed;

        model.classes[owner_index].vtable_state = make_fact(
            state,
            EvidenceSource::vtable,
            state == NativeVtableState::valid
                ? Confidence::verified
                : Confidence::low,
            state == NativeVtableState::valid
                ? "direct vftable evidence was parsed"
                : "direct vftable symbol was present but its contents were not parseable"
        );

        if (state == NativeVtableState::malformed) {
            model.classes[owner_index].vtable_error = make_fact(
                vtable.error,
                EvidenceSource::vtable,
                Confidence::low,
                "vftable parser diagnostic"
            );
            continue;
        }

        NativeVtable native_vtable;
        native_vtable.id = vtable_id(owner);
        native_vtable.rva = make_fact(
            vtable.rva,
            EvidenceSource::vtable,
            Confidence::verified,
            "vftable RVA"
        );
        native_vtable.virtual_address = make_fact(
            vtable.virtual_address,
            EvidenceSource::vtable,
            Confidence::verified,
            "vftable preferred virtual address"
        );

        for (const auto& slot : vtable.slots) {
            NativeVtableSlot native_slot;
            native_slot.index = slot.index;
            native_slot.target_rva = make_fact(
                slot.target_rva,
                EvidenceSource::vtable,
                Confidence::verified,
                "vtable entry target RVA"
            );

            if (slot.target_rva != 0) {
                const auto target_va =
                    build.executable.pe.image_base
                    + slot.target_rva;

                if (!physical_indices.contains(slot.target_rva)) {
                    PhysicalCode code;
                    code.id = physical_code_id(slot.target_rva);
                    code.rva = make_fact(
                        slot.target_rva,
                        EvidenceSource::vtable,
                        Confidence::verified,
                        "physical code RVA observed through vtable"
                    );
                    code.virtual_address = make_fact(
                        target_va,
                        EvidenceSource::vtable,
                        Confidence::verified,
                        "physical code VA observed through vtable"
                    );
                    physical_indices.emplace(
                        slot.target_rva,
                        model.physical_code.size()
                    );
                    model.physical_code.push_back(std::move(code));
                }

                const auto code_index = physical_indices.at(slot.target_rva);
                auto& code = model.physical_code[code_index];
                code.logical_function_ids.reserve(
                    code.logical_function_ids.size()
                    + slot.aliases.size()
                );

                for (const auto& alias : slot.aliases) {
                    if (alias.demangled_name.empty())
                        continue;

                    const auto parsed_alias =
                        parse_function_signature(alias.demangled_name);
                    if (!parsed_alias.parsed)
                        continue;

                    const auto alias_id = function_id(
                        parsed_alias.qualified_name,
                        slot.target_rva
                    );

                    const auto function_it = function_indices.find(alias_id);
                    if (function_it == function_indices.end())
                        continue;

                    const auto& function =
                        model.functions[function_it->second];
                    const auto function_owner =
                        direct_owner_from_qualified_name(
                            function.qualified_name
                        );

                    if (function_owner != owner)
                        continue;

                    if (std::find(
                            native_slot.candidate_function_ids.begin(),
                            native_slot.candidate_function_ids.end(),
                            alias_id
                        ) == native_slot.candidate_function_ids.end()) {
                        native_slot.candidate_function_ids.push_back(alias_id);
                    }

                    if (std::find(
                            code.logical_function_ids.begin(),
                            code.logical_function_ids.end(),
                            alias_id
                        ) == code.logical_function_ids.end()) {
                        code.logical_function_ids.push_back(alias_id);
                    }
                }

                if (native_slot.candidate_function_ids.empty()) {
                    const auto function_ids =
                        function_ids_by_rva.find(slot.target_rva);
                    if (function_ids != function_ids_by_rva.end()) {
                        for (const auto& candidate_id : function_ids->second) {
                            const auto function_it =
                                function_indices.find(candidate_id);
                            if (function_it == function_indices.end())
                                continue;

                            const auto& function =
                                model.functions[function_it->second];
                            if (direct_owner_from_qualified_name(
                                    function.qualified_name
                                ) != owner) {
                                continue;
                            }

                            native_slot.candidate_function_ids.push_back(
                                candidate_id
                            );
                        }
                    }
                }

                native_slot.physical_code_id = make_fact(
                    physical_code_id(slot.target_rva),
                    EvidenceSource::vtable,
                    Confidence::verified,
                    "vtable slot targets this physical RVA"
                );
            }

            resolve_unique_vtable_slot(
                model,
                native_slot
            );
            native_vtable.slots.push_back(std::move(native_slot));
        }

        model.classes[owner_index].vtable_id = make_fact(
            native_vtable.id,
            EvidenceSource::vtable,
            Confidence::verified,
            "direct class vftable"
        );
        model.vtables.push_back(std::move(native_vtable));

        try {
            const auto rtti = inspect_rtti(
                build,
                owner,
                vtable
            );

            for (const auto& base : rtti.bases) {
                const auto base_name = clean_rtti_name(base.raw_type_name);
                if (base_name.empty() || base_name == owner)
                    continue;

                ensure_class(base_name);

                NativeHierarchyEntry entry;
                entry.class_id = class_id(base_name);
                entry.contained_bases = base.contained_bases;
                entry.member_displacement = make_fact(
                    base.pmd.mdisp,
                    EvidenceSource::rtti,
                    Confidence::verified,
                    "MSVC RTTI PMD.mdisp"
                );
                entry.vbtable_displacement = make_fact(
                    base.pmd.pdisp,
                    EvidenceSource::rtti,
                    Confidence::verified,
                    "MSVC RTTI PMD.pdisp"
                );
                entry.displacement_inside_vbtable = make_fact(
                    base.pmd.vdisp,
                    EvidenceSource::rtti,
                    Confidence::verified,
                    "MSVC RTTI PMD.vdisp"
                );
                model.classes[owner_index].rtti_hierarchy.push_back(
                    std::move(entry)
                );
            }
        }
        catch (const std::exception&) {
            // A valid vtable is still useful when its RTTI graph is
            // malformed. Leave the hierarchy unlinked rather than inventing
            // an inheritance relationship.
        }
    }

    std::unordered_map<std::string, const MapSymbol*> reflection_symbols;

    for (const auto& symbol : build.map.symbols) {
        if (!symbol.name.starts_with("?m_ParamInfos@"))
            continue;

        const auto owner = reflection_owner_from_symbol(symbol);
        if (!owner.empty())
            reflection_symbols.emplace(owner, &symbol);
    }

    for (const auto& [decorated_owner, param_infos_symbol] : reflection_symbols) {
        const auto count_marker =
            "m_ParamInfoCount@"
            + decorated_owner
            + "@@";

        std::size_t count_symbol_matches = 0;
        const auto owner_symbols = build.map.symbols_by_owner.find(
            decorated_owner
        );

        if (owner_symbols != build.map.symbols_by_owner.end()) {
            for (const auto index : owner_symbols->second) {
                if (build.map.symbols[index].name.find(count_marker)
                    != std::string::npos) {
                    ++count_symbol_matches;
                }
            }
        }

        if (count_symbol_matches != 1) {
            continue;
        }

        ReflectionInfo reflection;
        try {
            reflection = inspect_reflection(build, decorated_owner);
        }
        catch (const std::exception&) {
            // Reflection is optional evidence. Do not turn a malformed
            // metadata table into a claim about the class layout.
            continue;
        }

        auto display_owner = demangled_reflection_owner(*param_infos_symbol);
        if (display_owner.empty()
            || !class_indices.contains(display_owner)) {
            display_owner = decorated_owner;
        }

        auto& native_class = ensure_class(display_owner);

        append_reflection_data(
            model,
            native_class,
            display_owner,
            reflection
        );
    }

    resolve_type_references(model);
    return model;
}

} // namespace tmfdev
