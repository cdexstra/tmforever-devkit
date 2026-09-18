#include "sdk.hpp"

#include "abi.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace tmfdev {
namespace {

bool is_identifier_character(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0
        || c == '_';
}

std::string sanitize_identifier(std::string value)
{
    for (auto& c : value) {
        if (!is_identifier_character(c))
            c = '_';
    }

    if (value.empty()
        || std::isdigit(static_cast<unsigned char>(value.front())) != 0) {
        value.insert(value.begin(), '_');
    }

    return value;
}

std::string strip_elaborated_keywords(std::string text)
{
    constexpr std::string_view tokens[] = {
        "class ",
        "struct ",
        "enum ",
        "union ",
    };

    for (const auto token : tokens) {
        std::size_t pos = 0;
        while ((pos = text.find(token, pos)) != std::string::npos) {
            const bool boundary =
                pos == 0
                || !is_identifier_character(text[pos - 1]);

            if (boundary) {
                text.erase(pos, token.size());
            }
            else {
                pos += token.size();
            }
        }
    }

    return text;
}

bool is_builtin_base_type(const std::string& name)
{
    static const std::set<std::string> names = {
        "void",
        "bool",
        "char",
        "signed char",
        "unsigned char",
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
        "long long",
        "long long int",
        "signed long long",
        "signed long long int",
        "unsigned long long",
        "unsigned long long int",
        "__int64",
        "signed __int64",
        "unsigned __int64",
        "float",
        "double",
        "long double",
        "wchar_t",
        "...",
    };

    return names.contains(name);
}

bool contains_nested_type(const std::string& name)
{
    return name.find("::") != std::string::npos;
}

bool contains_template_type(const std::string& name)
{
    return name.find('<') != std::string::npos
        && name.find('>') != std::string::npos;
}

bool is_representable_owner(const std::string& owner)
{
    if (owner.empty())
        return true;

    if (owner == "std"
        || owner == "stdext"
        || owner.starts_with("std::")
        || owner.find('`') != std::string::npos
        || owner.find('\'') != std::string::npos
        || owner.find('"') != std::string::npos) {
        return false;
    }

    return contains_template_type(owner)
        || owner.find_first_of(" \t") == std::string::npos;
}

std::string template_surrogate_name(
    const std::string& name)
{
    return "OpaqueTemplate_"
        + sanitize_identifier(
            strip_elaborated_keywords(name)
        );
}

std::string nested_surrogate_name(
    const std::string& name)
{
    return "OpaqueNested_"
        + sanitize_identifier(
            strip_elaborated_keywords(name)
        );
}

std::string emit_type_declaration(
    const Fact<std::string>& declaration,
    const NativeTypeUsage& usage)
{
    if (!declaration.known())
        return {};

    std::string text =
        strip_elaborated_keywords(
            *declaration.value
        );

    if (!usage.base_type.known())
        return text;

    const auto& base =
        *usage.base_type.value;

    if (base.find_first_of("([") != std::string::npos)
        return text;

    const bool nested =
        contains_nested_type(base);

    const bool templated =
        contains_template_type(base);

    if (!nested && !templated)
        return text;

    const auto normalized_base =
        strip_elaborated_keywords(base);

    const auto pos =
        text.find(normalized_base);

    if (pos == std::string::npos)
        return text;

    const auto surrogate =
        templated
            ? template_surrogate_name(base)
            : nested_surrogate_name(base);

    text.replace(
        pos,
        normalized_base.size(),
        surrogate
    );

    return text;
}

const NativeType* find_type_by_id(
    const NativeModel& model,
    const std::string& id)
{
    const auto it = std::find_if(
        model.types.begin(),
        model.types.end(),
        [&](const NativeType& type) {
            return type.id == id;
        }
    );

    return it == model.types.end()
        ? nullptr
        : &*it;
}

std::string emit_known_type(const NativeType& type);

bool type_has_abi_verification(const NativeType& type)
{
    if (!type.size.known())
        return false;

    return std::any_of(
        type.size.verifications.begin(),
        type.size.verifications.end(),
        [](const Verification& verification) {
            return verification.kind == VerificationKind::abi;
        }
    );
}

std::string hex8(std::uint32_t value)
{
    std::ostringstream out;
    out << std::uppercase
        << std::hex
        << std::setw(8)
        << std::setfill('0')
        << value;
    return out.str();
}

std::string class_owner(const std::string& qualified_name);
std::string short_function_name(const std::string& qualified_name);

std::string function_alias_name(const NativeFunction& function)
{
    const auto owner = class_owner(function.qualified_name);
    const auto short_name = short_function_name(function.qualified_name);
    const auto rva = function.rva.known()
        ? *function.rva.value
        : 0;

    return sanitize_identifier(
        owner
        + "_"
        + short_name
        + "_"
        + hex8(rva)
    );
}

std::string json_escape(const std::string& value)
{
    std::ostringstream out;

    for (const auto c : value) {
        switch (c) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
                out << "\\u00" << std::hex << std::setw(2)
                    << std::setfill('0')
                    << static_cast<unsigned int>(
                        static_cast<unsigned char>(c)
                    )
                    << std::dec << std::setfill(' ');
            else
                out << c;
            break;
        }
    }

    return out.str();
}

std::string tsv_escape(const std::string& value)
{
    std::string result;

    for (const auto c : value) {
        switch (c) {
        case '\\':
            result += "\\\\";
            break;
        case '\t':
            result += "\\t";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\n':
            result += "\\n";
            break;
        default:
            result += c;
            break;
        }
    }

    return result;
}

void emit_json_string(
    std::ostringstream& out,
    const std::string& key,
    const std::string& value,
    bool& first)
{
    if (!first)
        out << ",";

    out << "\"" << key << "\":\""
        << json_escape(value)
        << "\"";
    first = false;
}

template <typename T>
void emit_json_number(
    std::ostringstream& out,
    const std::string& key,
    T value,
    bool& first)
{
    if (!first)
        out << ",";

    out << "\"" << key << "\":" << value;
    first = false;
}

void emit_json_bool(
    std::ostringstream& out,
    const std::string& key,
    bool value,
    bool& first)
{
    if (!first)
        out << ",";

    out << "\"" << key << "\":"
        << (value ? "true" : "false");
    first = false;
}

void emit_json_null(
    std::ostringstream& out,
    const std::string& key,
    bool& first)
{
    if (!first)
        out << ",";

    out << "\"" << key << "\":null";
    first = false;
}

void emit_json_evidence(
    std::ostringstream& out,
    const std::string& key,
    const std::vector<Evidence>& evidence,
    bool& first)
{
    if (!first)
        out << ",";

    out << "\"" << key << "\":[";
    bool first_evidence = true;

    for (const auto& item : evidence) {
        if (!first_evidence)
            out << ",";

        out << "{\"source\":\""
            << evidence_source_name(item.source)
            << "\",\"confidence\":\""
            << confidence_name(item.confidence)
            << "\",\"detail\":\""
            << json_escape(item.detail)
            << "\"}";
        first_evidence = false;
    }

    out << "]";
    first = false;
}

void emit_json_string_array(
    std::ostringstream& out,
    const std::string& key,
    const std::vector<std::string>& values,
    bool& first)
{
    if (!first)
        out << ",";

    out << "\"" << key << "\":[";

    bool first_value = true;
    for (const auto& value : values) {
        if (!first_value)
            out << ",";

        out << "\"" << json_escape(value) << "\"";
        first_value = false;
    }

    out << "]";
    first = false;
}

void emit_json_abi_issues(
    std::ostringstream& out,
    const std::vector<NativeAbiIssue>& issues,
    bool& first)
{
    if (!first)
        out << ",";

    out << "\"abi_issues\":[";

    bool first_issue = true;
    for (const auto& issue : issues) {
        if (!first_issue)
            out << ",";

        out << "{\"level\":\""
            << native_abi_issue_level_name(issue.level)
            << "\",\"subject\":\""
            << json_escape(issue.subject)
            << "\",\"detail\":\""
            << json_escape(issue.detail)
            << "\"}";
        first_issue = false;
    }

    out << "]";
    first = false;
}

template <typename T>
void append_fact_evidence(
    std::vector<Evidence>& destination,
    const Fact<T>& fact)
{
    destination.insert(
        destination.end(),
        fact.evidence.begin(),
        fact.evidence.end()
    );
}

void emit_json_reflection_descriptor(
    std::ostringstream& out,
    const NativeReflectionDescriptor& descriptor)
{
    out << "{";
    bool first = true;

    emit_json_string(out, "id", descriptor.id, first);
    emit_json_string(out, "owner_class_id", descriptor.owner_class_id, first);
    emit_json_string(out, "owner_class_name", descriptor.owner_class_name, first);
    emit_json_number(out, "index", descriptor.index, first);

    const auto emit_u32_fact = [&](const std::string& key,
                                   const Fact<std::uint32_t>& fact) {
        if (fact.known())
            emit_json_number(out, key, *fact.value, first);
        else
            emit_json_null(out, key, first);
    };
    const auto emit_i32_fact = [&](const std::string& key,
                                   const Fact<std::int32_t>& fact) {
        if (fact.known())
            emit_json_number(out, key, *fact.value, first);
        else
            emit_json_null(out, key, first);
    };
    const auto emit_string_fact = [&](const std::string& key,
                                      const Fact<std::string>& fact) {
        if (fact.known())
            emit_json_string(out, key, *fact.value, first);
        else
            emit_json_null(out, key, first);
    };

    emit_u32_fact("record_virtual_address", descriptor.record_virtual_address);
    emit_u32_fact("record_size", descriptor.record_size);
    emit_u32_fact("parameter_id", descriptor.parameter_id);
    emit_u32_fact("parameter_virtual_address", descriptor.parameter_virtual_address);
    emit_string_fact("name", descriptor.name);
    emit_u32_fact("type_code", descriptor.type_code);
    emit_string_fact("type_name", descriptor.type_name);
    emit_i32_fact("offset", descriptor.offset);
    emit_u32_fact("flags1", descriptor.flags1);
    emit_u32_fact("flags2", descriptor.flags2);

    if (descriptor.category.known())
        emit_json_string(
            out,
            "category",
            native_reflection_category_name(*descriptor.category.value),
            first
        );
    else
        emit_json_null(out, "category", first);

    if (descriptor.physical_storage.known())
        emit_json_bool(out, "physical_storage", *descriptor.physical_storage.value, first);
    else
        emit_json_null(out, "physical_storage", first);

    if (!first)
        out << ",";
    out << "\"specialization\":{";
    bool first_specialization = true;
    const auto& specialization = descriptor.specialization;

    if (specialization.kind.known())
        emit_json_string(
            out,
            "kind",
            native_reflection_specialized_kind_name(
                *specialization.kind.value
            ),
            first_specialization
        );
    else
        emit_json_null(out, "kind", first_specialization);

    const auto emit_specialized_u32 =
        [&](const std::string& key, const Fact<std::uint32_t>& fact) {
            if (fact.known())
                emit_json_number(out, key, *fact.value, first_specialization);
            else
                emit_json_null(out, key, first_specialization);
        };
    const auto emit_specialized_string =
        [&](const std::string& key, const Fact<std::string>& fact) {
            if (fact.known())
                emit_json_string(out, key, *fact.value, first_specialization);
            else
                emit_json_null(out, key, first_specialization);
        };

    emit_specialized_u32("name_virtual_address", specialization.name_virtual_address);
    emit_specialized_string("name", specialization.name);
    emit_specialized_u32("class_info_virtual_address", specialization.class_info_virtual_address);
    emit_specialized_u32("function_virtual_address", specialization.function_virtual_address);
    emit_specialized_u32("argument_count", specialization.argument_count);
    emit_specialized_u32("value_count", specialization.value_count);
    emit_specialized_u32("value_names_virtual_address", specialization.value_names_virtual_address);
    emit_specialized_u32("argument_class_ids_virtual_address", specialization.argument_class_ids_virtual_address);
    emit_specialized_u32("argument_names_virtual_address", specialization.argument_names_virtual_address);
    emit_specialized_u32("argument_flags_virtual_address", specialization.argument_flags_virtual_address);
    emit_specialized_u32("auxiliary0", specialization.auxiliary0);
    emit_specialized_u32("auxiliary1", specialization.auxiliary1);

    if (!first_specialization)
        out << ",";
    out << "\"components\":[";
    bool first_component = true;
    for (const auto& component : specialization.components) {
        if (!first_component)
            out << ",";
        first_component = false;
        out << "{";
        bool first_component_field = true;
        if (component.name_virtual_address.known())
            emit_json_number(
                out,
                "name_virtual_address",
                *component.name_virtual_address.value,
                first_component_field
            );
        else
            emit_json_null(out, "name_virtual_address", first_component_field);
        if (component.name.known())
            emit_json_string(out, "name", *component.name.value, first_component_field);
        else
            emit_json_null(out, "name", first_component_field);
        emit_json_evidence(
            out,
            "evidence",
            [&]() {
                std::vector<Evidence> evidence;
                append_fact_evidence(evidence, component.name_virtual_address);
                append_fact_evidence(evidence, component.name);
                return evidence;
            }(),
            first_component_field
        );
        out << "}";
    }
    out << "]";

    out << ",\"enum_values\":[";
    bool first_enum_value = true;
    for (const auto& enum_value : specialization.enum_values) {
        if (!first_enum_value)
            out << ",";
        first_enum_value = false;
        out << "{";
        bool first_enum_field = true;
        emit_json_number(out, "index", enum_value.index, first_enum_field);
        if (enum_value.name_virtual_address.known())
            emit_json_number(
                out,
                "name_virtual_address",
                *enum_value.name_virtual_address.value,
                first_enum_field
            );
        else
            emit_json_null(out, "name_virtual_address", first_enum_field);
        if (enum_value.name.known())
            emit_json_string(out, "name", *enum_value.name.value, first_enum_field);
        else
            emit_json_null(out, "name", first_enum_field);
        emit_json_evidence(
            out,
            "evidence",
            [&]() {
                std::vector<Evidence> evidence;
                append_fact_evidence(evidence, enum_value.name_virtual_address);
                append_fact_evidence(evidence, enum_value.name);
                return evidence;
            }(),
            first_enum_field
        );
        out << "}";
    }
    out << "]";

    out << ",\"procedure_arguments\":[";
    bool first_argument = true;
    for (const auto& argument : specialization.procedure_arguments) {
        if (!first_argument)
            out << ",";
        first_argument = false;
        out << "{";
        bool first_argument_field = true;
        emit_json_number(out, "index", argument.index, first_argument_field);
        if (argument.class_id.known())
            emit_json_number(out, "class_id", *argument.class_id.value, first_argument_field);
        else
            emit_json_null(out, "class_id", first_argument_field);
        if (argument.name_virtual_address.known())
            emit_json_number(
                out,
                "name_virtual_address",
                *argument.name_virtual_address.value,
                first_argument_field
            );
        else
            emit_json_null(out, "name_virtual_address", first_argument_field);
        if (argument.name.known())
            emit_json_string(out, "name", *argument.name.value, first_argument_field);
        else
            emit_json_null(out, "name", first_argument_field);
        if (argument.flags.known())
            emit_json_number(out, "flags", *argument.flags.value, first_argument_field);
        else
            emit_json_null(out, "flags", first_argument_field);
        std::vector<Evidence> argument_evidence;
        append_fact_evidence(argument_evidence, argument.class_id);
        append_fact_evidence(argument_evidence, argument.name_virtual_address);
        append_fact_evidence(argument_evidence, argument.name);
        append_fact_evidence(argument_evidence, argument.flags);
        emit_json_evidence(out, "evidence", argument_evidence, first_argument_field);
        out << "}";
    }
    out << "]";
    out << "}";

    std::vector<Evidence> evidence;
    append_fact_evidence(evidence, descriptor.record_virtual_address);
    append_fact_evidence(evidence, descriptor.record_size);
    append_fact_evidence(evidence, descriptor.parameter_id);
    append_fact_evidence(evidence, descriptor.parameter_virtual_address);
    append_fact_evidence(evidence, descriptor.name);
    append_fact_evidence(evidence, descriptor.type_code);
    append_fact_evidence(evidence, descriptor.type_name);
    append_fact_evidence(evidence, descriptor.offset);
    append_fact_evidence(evidence, descriptor.flags1);
    append_fact_evidence(evidence, descriptor.flags2);
    append_fact_evidence(evidence, descriptor.category);
    append_fact_evidence(evidence, descriptor.physical_storage);
    append_fact_evidence(evidence, specialization.kind);
    append_fact_evidence(evidence, specialization.name_virtual_address);
    append_fact_evidence(evidence, specialization.name);
    append_fact_evidence(evidence, specialization.class_info_virtual_address);
    append_fact_evidence(evidence, specialization.function_virtual_address);
    append_fact_evidence(evidence, specialization.argument_count);
    append_fact_evidence(evidence, specialization.value_count);
    append_fact_evidence(evidence, specialization.value_names_virtual_address);
    append_fact_evidence(evidence, specialization.argument_class_ids_virtual_address);
    append_fact_evidence(evidence, specialization.argument_names_virtual_address);
    append_fact_evidence(evidence, specialization.argument_flags_virtual_address);
    append_fact_evidence(evidence, specialization.auxiliary0);
    append_fact_evidence(evidence, specialization.auxiliary1);
    for (const auto& component : specialization.components) {
        append_fact_evidence(evidence, component.name_virtual_address);
        append_fact_evidence(evidence, component.name);
    }
    for (const auto& enum_value : specialization.enum_values) {
        append_fact_evidence(evidence, enum_value.name_virtual_address);
        append_fact_evidence(evidence, enum_value.name);
    }
    for (const auto& argument : specialization.procedure_arguments) {
        append_fact_evidence(evidence, argument.class_id);
        append_fact_evidence(evidence, argument.name_virtual_address);
        append_fact_evidence(evidence, argument.name);
        append_fact_evidence(evidence, argument.flags);
    }
    emit_json_evidence(out, "evidence", evidence, first);

    out << "}";
}

std::string class_owner(const std::string& qualified_name)
{
    const auto pos = qualified_name.rfind("::");
    if (pos == std::string::npos)
        return {};

    return qualified_name.substr(0, pos);
}

std::string static_global_owner(const std::string& demangled_name)
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

std::string global_alias_name(const NativeGlobal& global)
{
    const auto rva = global.rva.known()
        ? *global.rva.value
        : 0;

    return sanitize_identifier(
        "Global_"
        + hex8(rva)
        + "_"
        + global.name
    );
}

std::string short_function_name(const std::string& qualified_name)
{
    const auto pos = qualified_name.rfind("::");
    if (pos == std::string::npos)
        return qualified_name;

    return qualified_name.substr(pos + 2);
}

bool call_wrapper_has_self(const NativeFunction& function)
{
    return (!function.is_static.known()
            || !*function.is_static.value)
        && function.calling_convention.known()
        && *function.calling_convention.value
            == CallingConvention::thiscall_;
}

bool call_wrapper_is_unsafe_runtime_operation(const NativeFunction& function)
{
    if (!function.decorated_name.known())
        return function.qualified_name == "CMwNod::CreateByMwClassId"
            || function.qualified_name == "CMwNod::MwNew"
            || function.qualified_name == "CMwNod::AddClass"
            || function.qualified_name == "CMwNod::MwBuildClassInfoTree";

    const auto& decorated = *function.decorated_name.value;
    if (decorated.starts_with("??0")
        || decorated.starts_with("??1")
        || decorated.starts_with("??_G")
        || decorated.starts_with("??_E")) {
        return true;
    }

    // These exact GameBox registry entry points can allocate or mutate global
    // class metadata. Their verified function-pointer resolvers remain
    // available, but a convenience call would imply a safety contract that
    // the evidence does not establish.
    return function.qualified_name == "CMwNod::CreateByMwClassId"
        || function.qualified_name == "CMwNod::MwNew"
        || function.qualified_name == "CMwNod::AddClass"
        || function.qualified_name == "CMwNod::MwBuildClassInfoTree";
}

std::string call_wrapper_name(const NativeFunction& function)
{
    return sanitize_identifier(
        short_function_name(function.qualified_name)
    );
}

std::string call_wrapper_signature_key(const NativeFunction& function)
{
    std::ostringstream key;
    key << call_wrapper_name(function) << '|';

    if (call_wrapper_has_self(function))
        key << "self|";

    for (const auto& parameter : function.parameters) {
        key << emit_type_declaration(
            parameter.type,
            parameter.usage
        ) << ';';
    }

    return key.str();
}

std::string search_index_function_signature(
    const NativeFunction& function)
{
    std::ostringstream details;
    details << ";return_type="
        << (function.return_type.known()
            ? *function.return_type.value
            : "unknown")
        << ";parameter_types=";

    for (std::size_t index = 0;
         index < function.parameters.size();
         ++index) {
        if (index != 0)
            details << ',';

        const auto& parameter = function.parameters[index];
        details << (parameter.type.known()
            ? *parameter.type.value
            : "unknown");
    }

    return details.str();
}

bool function_codegen_supported(
    const NativeModel& model,
    const NativeFunction& function,
    std::string& reason)
{
    const auto assessment =
        assess_native_function_abi(model, function);

    if (assessment.status != NativeAbiStatus::ready) {
        if (!assessment.issues.empty()) {
            const auto& issue = assessment.issues.front();
            reason = issue.subject + ": " + issue.detail;
        }
        else {
            reason = "ABI assessment is not ready";
        }
        return false;
    }

    if (!function.return_type.known()
        || !function.calling_convention.known()
        || !function.rva.known()) {
        reason = "missing return type, calling convention, or RVA";
        return false;
    }

    if (!function.return_usage.base_type.known()) {
        reason = "return type usage is not parsed";
        return false;
    }

    const auto check_declarator_shape =
        [&](const NativeTypeUsage& usage,
            const std::string& subject) -> bool {
        if (!usage.base_type.known())
            return true;

        const auto& base = *usage.base_type.value;

        if (base.find("::*") != std::string::npos) {
            reason = subject
                + " is a nontrivial member-pointer declarator";
            return false;
        }

        if (base.find('(') != std::string::npos
            || base.find('[') != std::string::npos) {
            reason = subject
                + " uses a complex callback or array declarator not yet emitted generically";
            return false;
        }

        return true;
    };

    if (!check_declarator_shape(function.return_usage, "return type"))
        return false;

    const auto check_usage = [&](const Fact<std::string>& declaration,
                                 const NativeTypeUsage& usage,
                                 const Fact<std::string>& referenced_type_id,
                                 const std::string& subject) -> bool {
        if (!usage.base_type.known()) {
            reason = subject + " has no parsed base type";
            return false;
        }

        const auto& base = *usage.base_type.value;

        if (!usage.pass_kind.known()) {
            reason = subject + " has no pass-kind information";
            return false;
        }

        const auto pass = *usage.pass_kind.value;

        if (pass == NativeTypePassKind::value) {
            const auto emitted_declaration =
                emit_type_declaration(declaration, usage);

            const bool opaque_surrogate =
                emitted_declaration.starts_with("OpaqueNested_")
                || emitted_declaration.starts_with("OpaqueTemplate_");

            if (opaque_surrogate) {
                const auto* type = referenced_type_id.known()
                    ? find_type_by_id(model, *referenced_type_id.value)
                    : nullptr;

                if (type == nullptr
                    || emit_known_type(*type).empty()) {
                    reason = subject
                        + " uses an opaque surrogate by value without an emitted ABI definition";
                    return false;
                }
            }
        }

        if (is_builtin_base_type(base))
            return true;

        if (referenced_type_id.known()) {
            const auto* type =
                find_type_by_id(model, *referenced_type_id.value);

            if (type == nullptr
                || !type_has_abi_verification(*type)) {
                reason = subject
                    + " references a canonical type without ABI-verified size";
                return false;
            }

            if (pass == NativeTypePassKind::value
                && emit_known_type(*type).empty()) {
                reason = subject
                    + " references a canonical type without an emitted ABI definition";
                return false;
            }

            return true;
        }

        if (contains_nested_type(base) || contains_template_type(base)) {
            if (pass == NativeTypePassKind::pointer
                || pass == NativeTypePassKind::lvalue_reference
                || pass == NativeTypePassKind::rvalue_reference) {
                return true;
            }

            reason = subject
                + " uses nested type '"
                + base
                + "' by value without a canonical ABI-verified layout";
            return false;
        }

        if (pass == NativeTypePassKind::pointer
            || pass == NativeTypePassKind::lvalue_reference
            || pass == NativeTypePassKind::rvalue_reference) {
            return true;
        }

        reason = subject
            + " uses non-builtin by-value type '"
            + base
            + "' without a canonical ABI-verified type";
        return false;
    };

    if (!check_usage(
            function.return_type,
            function.return_usage,
            function.return_type_id,
            "return type")) {
        return false;
    }

    for (const auto& parameter : function.parameters) {
        if (!check_declarator_shape(
                parameter.usage,
                "parameter " + std::to_string(parameter.index))) {
            return false;
        }

        if (!parameter.type.known()) {
            reason = "parameter "
                + std::to_string(parameter.index)
                + " has no declaration type";
            return false;
        }

        if (!check_usage(
                parameter.type,
                parameter.usage,
                parameter.referenced_type_id,
                "parameter " + std::to_string(parameter.index))) {
            return false;
        }
    }

    return true;
}

void collect_forward_declaration(
    const NativeModel& model,
    const NativeTypeUsage& usage,
    const Fact<std::string>& referenced_type_id,
    std::set<std::string>& names,
    std::set<std::string>& nested_names)
{
    if (!usage.base_type.known()
        || !usage.pass_kind.known()) {
        return;
    }

    if (referenced_type_id.known()) {
        const auto* type =
            find_type_by_id(model, *referenced_type_id.value);

        if (type != nullptr
            && (*usage.pass_kind.value == NativeTypePassKind::pointer
                || *usage.pass_kind.value == NativeTypePassKind::lvalue_reference
                || *usage.pass_kind.value == NativeTypePassKind::rvalue_reference)) {
            if (contains_nested_type(type->name)
                || contains_template_type(type->name)
                || contains_nested_type(*usage.base_type.value)
                || contains_template_type(*usage.base_type.value)) {
                nested_names.insert(
                    contains_nested_type(*usage.base_type.value)
                        || contains_template_type(*usage.base_type.value)
                        ? *usage.base_type.value
                        : type->name
                );
            }
            else {
                names.insert(type->name);
            }
        }

        return;
    }

    const auto& base =
        *usage.base_type.value;

    if (is_builtin_base_type(base))
        return;

    const auto normalized_base =
        strip_elaborated_keywords(base);

    if (is_builtin_base_type(normalized_base)
        || normalized_base == "std"
        || normalized_base == "stdext"
        || normalized_base.find_first_of("([") != std::string::npos) {
        return;
    }

    const auto pass =
        *usage.pass_kind.value;

    if (pass != NativeTypePassKind::pointer
        && pass != NativeTypePassKind::lvalue_reference
        && pass != NativeTypePassKind::rvalue_reference) {
        return;
    }

    if (contains_nested_type(base) || contains_template_type(base)) {
        nested_names.insert(base);
        return;
    }

    names.insert(base);
}

std::string owner_declaration_name(
    const std::string& owner,
    std::set<std::string>& names,
    std::set<std::string>& nested_names)
{
    if (owner.empty())
        return {};

    if (contains_nested_type(owner) || contains_template_type(owner)) {
        nested_names.insert(owner);
        return contains_template_type(owner)
            ? template_surrogate_name(owner)
            : nested_surrogate_name(owner);
    }

    names.insert(owner);
    return owner;
}

std::string emit_known_type(const NativeType& type)
{
    if (!type.kind.known()
        || !type.size.known()
        || !type_has_abi_verification(type)) {
        return {};
    }

    std::ostringstream out;

    const std::string emitted_name =
        contains_nested_type(type.name) || contains_template_type(type.name)
            ? (contains_template_type(type.name)
                ? template_surrogate_name(type.name)
                : nested_surrogate_name(type.name))
            : type.name;

    if (contains_nested_type(type.name) || contains_template_type(type.name)) {
        out << "// ABI-verified surrogate for "
            << strip_elaborated_keywords(type.name)
            << "\n";
    }

    const std::string guard =
        "TMFDEV_GENERATED_TYPE_"
        + sanitize_identifier(emitted_name);

    out << "#ifndef " << guard << "\n"
        << "#define " << guard << "\n";

    if (*type.kind.value == NativeTypeKind::enum_) {
        if (*type.size.value != 4)
            return {};

        out << "enum " << emitted_name
            << " : std::uint32_t;\n"
            << "#endif\n";
        return out.str();
    }

    if (*type.kind.value != NativeTypeKind::record
        && *type.kind.value != NativeTypeKind::union_) {
        return {};
    }

    out << (*type.kind.value == NativeTypeKind::union_
            ? "union "
            : "struct ")
        << emitted_name
        << " {\n";

    std::uint32_t cursor = 0;
    for (const auto& field : type.fields) {
        if (!field.offset.known()
            || !field.size.known()
            || *field.size.value != 4
            || *field.offset.value != cursor) {
            return {};
        }

        const std::string field_type =
            field.type.known()
                ? strip_elaborated_keywords(*field.type.value)
                : "std::uint32_t";

        out << "    " << field_type
            << " component" << field.index << ";\n";

        cursor += 4;
    }

    if (cursor != *type.size.value)
        return {};

    out << "};\n"
        << "static_assert(sizeof(" << emitted_name << ") == 0x"
        << std::hex << std::uppercase << *type.size.value
        << ");\n"
        << "#endif\n";

    return out.str();
}

std::string calling_convention_token(CallingConvention convention)
{
    switch (convention) {
    case CallingConvention::cdecl_:
        return "__cdecl";
    case CallingConvention::stdcall_:
        return "__stdcall";
    case CallingConvention::thiscall_:
        return "__thiscall";
    case CallingConvention::fastcall_:
        return "__fastcall";
    case CallingConvention::unknown:
        break;
    }

    return {};
}

struct RuntimeBlockEditorEvidence {
    const NativeFunction* function = nullptr;
    const NativeVtable* trackmania_vtable = nullptr;
};

struct RuntimePlaygroundEvidence {
    const NativeFunction* function = nullptr;
    const NativeVtable* trackmania_vtable = nullptr;
};

std::vector<std::uint32_t> runtime_vtable_rvas_for_base(
    const NativeModel& model,
    std::string_view base_name)
{
    std::set<std::uint32_t> result_rvas;
    const auto base_id = "class:" + std::string(base_name);

    for (const auto& native_class : model.classes) {
        const bool is_base_or_derived =
            native_class.name == base_name
            || std::any_of(
                native_class.rtti_hierarchy.begin(),
                native_class.rtti_hierarchy.end(),
                [&](const NativeHierarchyEntry& entry) {
                    return entry.class_id == base_id;
                }
            );
        if (!is_base_or_derived
            || !native_class.vtable_state.known()
            || *native_class.vtable_state.value
                != NativeVtableState::valid
            || !native_class.vtable_id.known())
            continue;

        const auto vtable = std::find_if(
            model.vtables.begin(),
            model.vtables.end(),
            [&](const NativeVtable& candidate) {
                return candidate.id == *native_class.vtable_id.value
                    && candidate.rva.known();
            }
        );
        if (vtable != model.vtables.end())
            result_rvas.insert(*vtable->rva.value);
    }

    return std::vector<std::uint32_t>(
        result_rvas.begin(),
        result_rvas.end()
    );
}

RuntimeBlockEditorEvidence find_runtime_block_editor_evidence(
    const NativeModel& model)
{
    const auto function = std::find_if(
        model.functions.begin(),
        model.functions.end(),
        [](const NativeFunction& candidate) {
            return candidate.qualified_name
                == "CTrackMania::GetTmBlockEditor";
        }
    );

    if (function == model.functions.end())
        return {};

    std::string reason;
    if (!function_codegen_supported(model, *function, reason)
        || !function->calling_convention.known()
        || *function->calling_convention.value != CallingConvention::thiscall_
        || !function->is_static.known()
        || *function->is_static.value
        || !function->parameters.empty()
        || !function->return_usage.base_type.known()
        || *function->return_usage.base_type.value != "CTrackManiaEditor"
        || !function->return_usage.pass_kind.known()
        || *function->return_usage.pass_kind.value != NativeTypePassKind::pointer
        || !function->return_usage.declarator_kind.known()
        || *function->return_usage.declarator_kind.value
            != NativeTypeDeclaratorKind::plain
        || !function->return_usage.pointer_depth.known()
        || *function->return_usage.pointer_depth.value != 1
        || !function->rva.known()) {
        return {};
    }

    const auto trackmania = std::find_if(
        model.classes.begin(),
        model.classes.end(),
        [](const NativeClass& candidate) {
            return candidate.name == "CTrackMania";
        }
    );

    if (trackmania == model.classes.end()
        || !trackmania->vtable_state.known()
        || *trackmania->vtable_state.value != NativeVtableState::valid
        || !trackmania->vtable_id.known()) {
        return {};
    }

    const auto vtable = std::find_if(
        model.vtables.begin(),
        model.vtables.end(),
        [&](const NativeVtable& candidate) {
            return candidate.id == *trackmania->vtable_id.value
                && candidate.rva.known();
        }
    );

    if (vtable == model.vtables.end())
        return {};

    return {&*function, &*vtable};
}

RuntimePlaygroundEvidence find_runtime_playground_evidence(
    const NativeModel& model)
{
    const auto function = std::find_if(
        model.functions.begin(),
        model.functions.end(),
        [](const NativeFunction& candidate) {
            return candidate.qualified_name
                == "CTrackMania::GetPlayground";
        }
    );

    if (function == model.functions.end())
        return {};

    std::string reason;
    if (!function_codegen_supported(model, *function, reason)
        || !function->calling_convention.known()
        || *function->calling_convention.value != CallingConvention::thiscall_
        || !function->is_static.known()
        || *function->is_static.value
        || !function->parameters.empty()
        || !function->return_usage.base_type.known()
        || *function->return_usage.base_type.value != "CGamePlayground"
        || !function->return_usage.pass_kind.known()
        || *function->return_usage.pass_kind.value != NativeTypePassKind::pointer
        || !function->return_usage.declarator_kind.known()
        || *function->return_usage.declarator_kind.value
            != NativeTypeDeclaratorKind::plain
        || !function->return_usage.pointer_depth.known()
        || *function->return_usage.pointer_depth.value != 1
        || !function->rva.known()) {
        return {};
    }

    const auto trackmania = std::find_if(
        model.classes.begin(),
        model.classes.end(),
        [](const NativeClass& candidate) {
            return candidate.name == "CTrackMania";
        }
    );

    if (trackmania == model.classes.end()
        || !trackmania->vtable_state.known()
        || *trackmania->vtable_state.value != NativeVtableState::valid
        || !trackmania->vtable_id.known()) {
        return {};
    }

    const auto vtable = std::find_if(
        model.vtables.begin(),
        model.vtables.end(),
        [&](const NativeVtable& candidate) {
            return candidate.id == *trackmania->vtable_id.value
                && candidate.rva.known();
        }
    );

    if (vtable == model.vtables.end())
        return {};

    return {&*function, &*vtable};
}

} // namespace

NativeSdkFunctionAssessment assess_native_sdk_function(
    const NativeModel& model,
    const NativeFunction& function)
{
    NativeSdkFunctionAssessment result;
    result.emitted = function_codegen_supported(
        model,
        function,
        result.reason
    );
    return result;
}

NativeSdkOutput generate_native_sdk_header(
    const NativeModel& model,
    const std::string& requested_class)
{
    NativeSdkOutput result;

    std::vector<const NativeFunction*> emitted;
    std::set<std::string> forward_declarations;
    std::set<std::string> nested_forward_declarations;
    std::set<std::string> emitted_known_type_ids;

    const bool emit_all_classes = requested_class.empty();

    if (!emit_all_classes)
        owner_declaration_name(
            requested_class,
            forward_declarations,
            nested_forward_declarations
        );

    for (const auto& function : model.functions) {
        if (!emit_all_classes
            && class_owner(function.qualified_name) != requested_class) {
            continue;
        }

        std::string reason;
        if (!function_codegen_supported(model, function, reason)) {
            ++result.skipped_functions;
            result.skips.push_back({
                function.qualified_name,
                std::move(reason)
            });
            continue;
        }

        const auto owner = class_owner(function.qualified_name);
        if (emit_all_classes && !is_representable_owner(owner)) {
            ++result.skipped_functions;
            result.skips.push_back({
                function.qualified_name,
                "owner is not a representable generated C++ class"
            });
            continue;
        }

        emitted.push_back(&function);
        ++result.emitted_functions;

        const auto owner_name = owner_declaration_name(
            owner,
            forward_declarations,
            nested_forward_declarations
        );

        collect_forward_declaration(
            model,
            function.return_usage,
            function.return_type_id,
            forward_declarations,
            nested_forward_declarations
        );

        if (function.return_type_id.known())
            emitted_known_type_ids.insert(*function.return_type_id.value);

        for (const auto& parameter : function.parameters) {
            collect_forward_declaration(
                model,
                parameter.usage,
                parameter.referenced_type_id,
                forward_declarations,
                nested_forward_declarations
            );

            if (parameter.referenced_type_id.known()) {
                emitted_known_type_ids.insert(
                    *parameter.referenced_type_id.value
                );
            }
        }
    }

    std::vector<std::string> known_type_definitions;
    std::set<std::string> emitted_type_names;

    for (const auto& id : emitted_known_type_ids) {
        const auto* type = find_type_by_id(model, id);
        if (type == nullptr)
            continue;

        const auto definition = emit_known_type(*type);
        if (definition.empty())
            continue;

        const auto emitted_name =
            contains_nested_type(type->name)
                || contains_template_type(type->name)
                ? (contains_template_type(type->name)
                    ? template_surrogate_name(type->name)
                    : nested_surrogate_name(type->name))
                : type->name;

        emitted_type_names.insert(emitted_name);
        forward_declarations.erase(type->name);
        nested_forward_declarations.erase(type->name);
        known_type_definitions.push_back(definition);
    }

    for (const auto& name : emitted_type_names) {
        forward_declarations.erase(name);
        nested_forward_declarations.erase(name);
    }

    std::ostringstream out;
    out << "#pragma once\n\n"
        << "#include <cstddef>\n"
        << "#include <cstdint>\n\n"
        << "// Generated by tmfdev from ABI-ready facts only.\n"
        << "// Target: 32-bit MSVC TMForever build.\n";

    if (model.build.sha256.known()) {
        out << "// Executable SHA-256: "
            << *model.build.sha256.value << "\n";
    }

    out << "\nnamespace tmf {\n\n"
        << "#ifndef TMFDEV_GENERATED_BUILD_IDENTITY\n"
        << "#define TMFDEV_GENERATED_BUILD_IDENTITY\n"
        << "#define TMFDEV_GENERATED_BUILD_SHA256 \""
        << (model.build.sha256.known()
            ? json_escape(*model.build.sha256.value)
            : "")
        << "\"\n"
        << "namespace build {\n"
        << "inline constexpr std::uint64_t ExecutableFileSize = "
        << (model.build.file_size.known()
            ? *model.build.file_size.value
            : 0)
        << "ull;\n"
        << "inline constexpr std::uint16_t Machine = 0x"
        << std::hex << std::uppercase
        << (model.build.machine.known()
            ? *model.build.machine.value
            : 0)
        << "u;\n"
        << "inline constexpr std::uint32_t Timestamp = 0x"
        << (model.build.timestamp.known()
            ? *model.build.timestamp.value
            : 0)
        << "u;\n"
        << "inline constexpr std::uint32_t ImageBase = 0x"
        << (model.build.image_base.known()
            ? *model.build.image_base.value
            : 0)
        << "u;\n"
        << "inline constexpr std::uint32_t ImageSize = 0x"
        << (model.build.image_size.known()
            ? *model.build.image_size.value
            : 0)
        << "u;\n";

    out << "inline constexpr char ExecutableSha256[] = \""
        << (model.build.sha256.known()
            ? json_escape(*model.build.sha256.value)
            : "")
        << "\";\n"
        << "constexpr bool sha256_matches(const char* actual) noexcept\n"
        << "{\n"
        << "    for (std::size_t index = 0;\n"
        << "         actual[index] != '\\0' || ExecutableSha256[index] != '\\0';\n"
        << "         ++index) {\n"
        << "        if (actual[index] != ExecutableSha256[index])\n"
        << "            return false;\n"
        << "    }\n"
        << "    return true;\n"
        << "}\n";

    out << "inline constexpr bool matches(\n"
        << "    std::uint16_t machine,\n"
        << "    std::uint32_t timestamp,\n"
        << "    std::uint32_t image_base,\n"
        << "    std::uint32_t image_size) noexcept\n"
        << "{\n"
        << "    return machine == Machine\n"
        << "        && timestamp == Timestamp\n"
        << "        && image_base == ImageBase\n"
        << "        && image_size == ImageSize;\n"
        << "}\n"
        << "} // namespace build\n"
        << "#else\n"
        << "#ifndef TMFDEV_GENERATED_BUILD_SHA256\n"
        << "#define TMFDEV_GENERATED_BUILD_SHA256 \"\"\n"
        << "#endif\n"
        << "namespace build {\n"
        << "#ifndef TMFDEV_GENERATED_BUILD_IDENTITY_CHECK\n"
        << "#define TMFDEV_GENERATED_BUILD_IDENTITY_CHECK\n"
        << "constexpr bool sha256_matches(\n"
        << "    const char* actual,\n"
        << "    const char* expected) noexcept\n"
        << "{\n"
        << "    for (std::size_t index = 0;\n"
        << "         actual[index] != '\\0' || expected[index] != '\\0';\n"
        << "         ++index) {\n"
        << "        if (actual[index] != expected[index])\n"
        << "            return false;\n"
        << "    }\n"
        << "    return true;\n"
        << "}\n"
        << "#endif\n"
        << "static_assert(\n"
        << std::dec
        << "    ExecutableFileSize == "
        << (model.build.file_size.known()
            ? *model.build.file_size.value
            : 0)
        << "ull\n"
        << "    && Machine == 0x"
        << std::hex << std::uppercase
        << (model.build.machine.known()
            ? *model.build.machine.value
            : 0)
        << "u\n"
        << "    && Timestamp == 0x"
        << (model.build.timestamp.known()
            ? *model.build.timestamp.value
            : 0)
        << "u\n"
        << "    && ImageBase == 0x"
        << (model.build.image_base.known()
            ? *model.build.image_base.value
            : 0)
        << "u\n"
        << "    && ImageSize == 0x"
        << (model.build.image_size.known()
            ? *model.build.image_size.value
            : 0)
        << "u\n"
        << "    && sha256_matches(\n"
        << "        TMFDEV_GENERATED_BUILD_SHA256,\n"
        << "        \""
        << (model.build.sha256.known()
            ? json_escape(*model.build.sha256.value)
            : "")
        << "\"\n"
        << "    ),\n"
        << "    \"tmfdev generated headers target different executable builds\"\n"
        << ");\n"
        << "} // namespace build\n"
        << "#endif\n\n"
        << std::dec
        << "#ifndef TMFDEV_GENERATED_RESOLVE_NATIVE\n"
        << "#define TMFDEV_GENERATED_RESOLVE_NATIVE\n"
        << "template <typename Fn>\n"
        << "inline Fn resolve_native(\n"
        << "    std::uintptr_t module_base,\n"
        << "    std::uint32_t rva) noexcept\n"
        << "{\n"
        << "    return reinterpret_cast<Fn>(module_base + rva);\n"
        << "}\n"
        << "#endif\n\n";

    for (const auto& name : forward_declarations) {
        out << "class " << name << ";\n";
    }

    for (const auto& name : nested_forward_declarations) {
        out << "// Opaque ABI surrogate for "
            << strip_elaborated_keywords(name)
            << "\n";
        out << "struct "
            << (
                contains_template_type(name)
                    ? template_surrogate_name(name)
                    : nested_surrogate_name(name)
            )
            << ";\n";
    }

    if (!forward_declarations.empty()
        || !nested_forward_declarations.empty()) {
        out << '\n';
    }

    for (const auto& definition : known_type_definitions)
        out << definition << '\n';

    out << "namespace native {\n\n";

    std::set<std::string> emitted_class_id_aliases;
    for (const auto& native_class : model.classes) {
        if (!emit_all_classes
            && native_class.name != requested_class) {
            continue;
        }

        if (emit_all_classes
            && !is_representable_owner(native_class.name)) {
            continue;
        }

        if (!native_class.mw_class_id.known())
            continue;

        const auto alias = sanitize_identifier(
            native_class.name + "_MwClassId"
        );
        if (!emitted_class_id_aliases.insert(alias).second)
            continue;

        out << "// Exact-build GameBox class identity; this is not a C++ layout or lifecycle claim.\n"
            << "inline constexpr std::uint32_t "
            << alias
            << " = 0x"
            << hex8(*native_class.mw_class_id.value)
            << "u;\n";
    }

    if (!emitted_class_id_aliases.empty())
        out << '\n';

    std::set<std::string> emitted_member_aliases;

    for (const auto& native_class : model.classes) {
        if (!emit_all_classes
            && native_class.name != requested_class) {
            continue;
        }

        if (emit_all_classes
            && !is_representable_owner(native_class.name)) {
            continue;
        }

        for (const auto& member_id : native_class.member_ids) {
            const auto member = std::find_if(
                model.members.begin(),
                model.members.end(),
                [&](const NativeMember& candidate) {
                    return candidate.id == member_id;
                }
            );

            if (member == model.members.end()
                || !member->offset.known()) {
                continue;
            }

            const auto alias = sanitize_identifier(
                native_class.name
                + "_"
                + member->name
                + "_Offset_"
                + member->id
            );

            if (!emitted_member_aliases.insert(alias).second)
                continue;

            out << "inline constexpr std::uint32_t "
                << alias
                << " = 0x"
                << hex8(*member->offset.value)
                << "u;\n";
        }
    }

    if (!emitted_member_aliases.empty())
        out << '\n';

    if (!model.globals.empty()) {
        out << "// Address-only static-data evidence; no C++ type or layout claim.\n";

        std::set<std::string> emitted_global_aliases;
        for (const auto& global : model.globals) {
            if (!global.rva.known())
                continue;

            if (!emit_all_classes
                && static_global_owner(global.name) != requested_class) {
                continue;
            }

            const auto alias = global_alias_name(global);

            if (!emitted_global_aliases.insert(alias).second)
                continue;

            out << "inline constexpr std::uint32_t "
                << alias
                << "Rva = 0x"
                << hex8(*global.rva.value)
                << "u;\n"
                << "inline std::uintptr_t resolve_"
                << alias
                << "(std::uintptr_t module_base) noexcept\n"
                << "{\n"
                << "    return module_base + "
                << alias
                << "Rva;\n"
                << "}\n";
        }

        out << '\n';
    }

    std::set<std::string> emitted_vtable_aliases;
    for (const auto& native_class : model.classes) {
        if (!emit_all_classes
            && native_class.name != requested_class) {
            continue;
        }

        if (emit_all_classes
            && !is_representable_owner(native_class.name)) {
            continue;
        }

        if (!native_class.vtable_state.known()
            || *native_class.vtable_state.value
                != NativeVtableState::valid
            || !native_class.vtable_id.known()) {
            continue;
        }

        const auto vtable_it =
            std::find_if(
                model.vtables.begin(),
                model.vtables.end(),
                [&](const NativeVtable& candidate) {
                    return candidate.id
                        == *native_class.vtable_id.value;
                }
            );

        if (vtable_it == model.vtables.end()
            || !vtable_it->rva.known()) {
            continue;
        }

        const auto alias =
            sanitize_identifier(native_class.name + "_Vtable");

        if (!emitted_vtable_aliases.insert(alias).second)
            continue;

        out << "// Verified vtable address evidence; absent or malformed vtables do not emit.\n"
            << "inline constexpr std::uint32_t "
            << alias
            << "Rva = 0x"
            << hex8(*vtable_it->rva.value)
            << "u;\n"
            << "inline std::uintptr_t resolve_"
            << alias
            << "(std::uintptr_t module_base) noexcept\n"
            << "{\n"
            << "    return module_base + "
            << alias
            << "Rva;\n"
            << "}\n";
    }

    if (!emitted_vtable_aliases.empty())
        out << '\n';

    for (const auto* function : emitted) {
        const auto owner = class_owner(function->qualified_name);
        const auto owner_name =
            (contains_nested_type(owner) || contains_template_type(owner))
                ? (contains_template_type(owner)
                    ? template_surrogate_name(owner)
                    : nested_surrogate_name(owner))
                : owner;
        const auto short_name = short_function_name(function->qualified_name);
        const auto rva = *function->rva.value;
        const auto convention =
            calling_convention_token(*function->calling_convention.value);

        if (convention.empty())
            continue;

        const auto alias = function_alias_name(*function);

        out << "using " << alias << "Fn = "
            << emit_type_declaration(
                function->return_type,
                function->return_usage
            )
            << " (" << convention << " *)(";

        bool first = true;
        const bool is_static =
            function->is_static.known()
            && *function->is_static.value;

        if (!is_static
            && *function->calling_convention.value == CallingConvention::thiscall_) {
            out << owner_name << "*";
            first = false;
        }

        for (const auto& parameter : function->parameters) {
            if (!first)
                out << ", ";

            out << emit_type_declaration(
                parameter.type,
                parameter.usage
            );
            first = false;
        }

        out << ");\n"
            << "inline constexpr std::uint32_t " << alias
            << "Rva = 0x" << hex8(rva) << "u;\n";

        if (function->virtual_slot.known()) {
            out << "inline constexpr std::size_t " << alias
                << "VtableSlot = " << *function->virtual_slot.value << ";\n";
        }

        out << '\n';
    }

    out << "} // namespace native\n\n";

    std::map<std::string, std::vector<const NativeFunction*>> api_functions;
    for (const auto* function : emitted)
        api_functions[class_owner(function->qualified_name)].push_back(function);

    if (!api_functions.empty())
        out << "namespace api {\n\n";

    for (const auto& [owner, functions] : api_functions) {
        const auto api_name = owner.empty()
            ? std::string{"GlobalApi"}
            : sanitize_identifier(owner) + "Api";

        std::map<std::string, std::size_t> call_wrapper_counts;
        if (!requested_class.empty()) {
            for (const auto* function : functions) {
                if (call_wrapper_is_unsafe_runtime_operation(*function))
                    continue;

                ++call_wrapper_counts[
                    call_wrapper_signature_key(*function)
                ];
            }
        }

        out << "struct " << api_name << " {\n";

        for (const auto* function : functions) {
            const auto alias = function_alias_name(*function);
            const auto method_name = "resolve_" + alias;

            out << "    using " << alias << "Fn = native::"
                << alias << "Fn;\n"
                << "    static constexpr std::uint32_t " << alias
                << "Rva = native::" << alias << "Rva;\n"
                << "    static " << alias << "Fn " << method_name
                << "(std::uintptr_t module_base) noexcept\n"
                << "    {\n"
                << "        return resolve_native<" << alias << "Fn>(\n"
                << "            module_base, " << alias << "Rva\n"
                << "        );\n"
                << "    }\n\n";
        }

        if (!requested_class.empty()) {
            for (const auto* function : functions) {
                if (call_wrapper_is_unsafe_runtime_operation(*function))
                    continue;

                const auto call_key =
                    call_wrapper_signature_key(*function);

                if (call_wrapper_counts[call_key] != 1)
                    continue;

                const auto method_name = call_wrapper_name(*function);
                const auto alias = function_alias_name(*function);
                const auto return_declaration = emit_type_declaration(
                    function->return_type,
                    function->return_usage
                );

                if (method_name.empty()
                    || return_declaration.empty()) {
                    continue;
                }

                out << "    // ABI verified; logical: "
                    << function->qualified_name
                    << " @ 0x"
                    << hex8(*function->rva.value);

                if (function->physical_code_id.known()) {
                    out << "; physical: "
                        << *function->physical_code_id.value;
                }

                out << "; layout opaque.\n"
                    << "    static " << return_declaration
                    << " " << method_name << "(\n"
                    << "        std::uintptr_t module_base";

                if (call_wrapper_has_self(*function)) {
                    const auto owner_name =
                        (contains_nested_type(owner)
                         || contains_template_type(owner))
                            ? (contains_template_type(owner)
                                ? template_surrogate_name(owner)
                                : nested_surrogate_name(owner))
                            : owner;

                    out << ",\n        "
                        << owner_name
                        << "* self";
                }

                for (std::size_t index = 0;
                     index < function->parameters.size();
                     ++index) {
                    const auto& parameter =
                        function->parameters[index];

                    out << ",\n        "
                        << emit_type_declaration(
                            parameter.type,
                            parameter.usage
                        )
                        << " arg" << index;
                }

                out << "\n    ) noexcept\n"
                    << "    {\n";

                out << "        "
                    << (return_declaration == "void"
                        ? ""
                        : "return ")
                    << "resolve_native<" << alias << "Fn>(\n"
                    << "            module_base, " << alias << "Rva\n"
                    << "        )(";

                bool first_argument = true;
                if (call_wrapper_has_self(*function)) {
                    out << "self";
                    first_argument = false;
                }

                for (std::size_t index = 0;
                     index < function->parameters.size();
                     ++index) {
                    if (!first_argument)
                        out << ", ";

                    out << "arg" << index;
                    first_argument = false;
                }

                out << ");\n"
                    << "    }\n\n";
            }
        }

        out << "};\n\n";
    }

    if (!api_functions.empty())
        out << "} // namespace api\n\n";

    if (!requested_class.empty()) {
        bool emitted_views = false;

        for (const auto& [owner, functions] : api_functions) {
            if (owner.empty())
                continue;

            std::map<std::string, std::size_t> api_call_counts;
            for (const auto* function : functions) {
                if (!call_wrapper_is_unsafe_runtime_operation(*function))
                    ++api_call_counts[
                        call_wrapper_signature_key(*function)
                    ];
            }

            std::vector<const NativeFunction*> view_functions;
            for (const auto* function : functions) {
                if (call_wrapper_is_unsafe_runtime_operation(*function)
                    || !call_wrapper_has_self(*function)
                    || api_call_counts[
                        call_wrapper_signature_key(*function)
                    ] != 1) {
                    continue;
                }

                view_functions.push_back(function);
            }

            if (view_functions.empty())
                continue;

            if (!emitted_views) {
                out << "namespace view {\n\n";
                emitted_views = true;
            }

            const auto view_name = sanitize_identifier(owner) + "View";
            const auto owner_name =
                (contains_nested_type(owner) || contains_template_type(owner))
                    ? (contains_template_type(owner)
                        ? template_surrogate_name(owner)
                        : nested_surrogate_name(owner))
                    : owner;

            out << "// Non-owning ABI view; layout and lifetime remain opaque.\n"
                << "struct " << view_name << " {\n"
                << "    constexpr " << view_name << "(\n"
                << "        std::uintptr_t module_base,\n"
                << "        " << owner_name << "* self\n"
                << "    ) noexcept\n"
                << "        : module_base_(module_base), self_(self) {}\n\n"
                << "    explicit constexpr operator bool() const noexcept\n"
                << "    {\n"
                << "        return module_base_ != 0 && self_ != nullptr;\n"
                << "    }\n\n";

            for (const auto* function : view_functions) {
                const auto method_name = call_wrapper_name(*function);
                const auto return_declaration = emit_type_declaration(
                    function->return_type,
                    function->return_usage
                );

                if (method_name.empty() || return_declaration.empty())
                    continue;

                out << "    // ABI verified; logical: "
                    << function->qualified_name
                    << " @ 0x"
                    << hex8(*function->rva.value)
                    << "; caller owns lifetime.\n"
                    << "    " << return_declaration << " "
                    << method_name << "(";

                for (std::size_t index = 0;
                     index < function->parameters.size();
                     ++index) {
                    if (index != 0)
                        out << ", ";

                    out << emit_type_declaration(
                        function->parameters[index].type,
                        function->parameters[index].usage
                    ) << " arg" << index;
                }

                out << ") const noexcept\n"
                    << "    {\n"
                    << "        ";

                if (return_declaration != "void")
                    out << "return ";

                out << "api::"
                    << sanitize_identifier(owner)
                    << "Api::"
                    << method_name
                    << "(module_base_, self_";

                for (std::size_t index = 0;
                     index < function->parameters.size();
                     ++index) {
                    out << ", arg" << index;
                }

                out << ");\n"
                    << "    }\n\n";
            }

            out << "private:\n"
                << "    std::uintptr_t module_base_ = 0;\n"
                << "    " << owner_name << "* self_ = nullptr;\n"
                << "};\n\n";
        }

        if (emitted_views)
            out << "} // namespace view\n\n";
    }

    out << "} // namespace tmf\n";

    result.text = out.str();
    return result;
}

NativeSdkOutput generate_native_runtime_header(
    const NativeModel& model)
{
    NativeSdkOutput result;
    const auto block_editor =
        find_runtime_block_editor_evidence(model);
    const auto playground =
        find_runtime_playground_evidence(model);

    const auto* trackmania_vtable =
        block_editor.trackmania_vtable != nullptr
            ? block_editor.trackmania_vtable
            : playground.trackmania_vtable;

    const auto game_app_vtable_rvas =
        runtime_vtable_rvas_for_base(model, "CGameApp");
    const auto audio_port_vtable_rvas =
        runtime_vtable_rvas_for_base(model, "CAudioPort");
    const auto input_port_vtable_rvas =
        runtime_vtable_rvas_for_base(model, "CInputPort");
    const auto block_editor_vtable_rvas =
        runtime_vtable_rvas_for_base(model, "CTrackManiaEditor");
    const auto playground_vtable_rvas =
        runtime_vtable_rvas_for_base(model, "CGamePlayground");

    const auto global = std::find_if(
        model.globals.begin(),
        model.globals.end(),
        [](const NativeGlobal& candidate) {
            return candidate.decorated_name.known()
                && *candidate.decorated_name.value
                    == "?s_TheGame@CGameApp@@2PAV1@A";
        }
    );

    const auto audio_port_member = std::find_if(
        model.members.begin(),
        model.members.end(),
        [](const NativeMember& candidate) {
            return candidate.id == "member:CGameApp::AudioPort#3"
                && candidate.offset.known();
        }
    );

    const auto input_port_member = std::find_if(
        model.members.begin(),
        model.members.end(),
        [](const NativeMember& candidate) {
            return candidate.id == "member:CGameApp::InputPort#4"
                && candidate.offset.known();
        }
    );

    if (global == model.globals.end() || !global->rva.known())
        return result;

    std::ostringstream out;
    out << "#pragma once\n\n"
        << "#include <array>\n"
        << "#include <cstddef>\n"
        << "#include <cstdint>\n\n"
        << "// Generated by tmfdev for one exact TMForever build.\n"
        << "// This context does not own or retain game objects.\n"
        << "static_assert(sizeof(void*) == 4,\n"
        << "    \"tmfdev runtime requires a 32-bit consumer\");\n\n"
        << "namespace tmf {\n";

    out
        << "#ifndef TMFDEV_GENERATED_BUILD_IDENTITY\n"
        << "#define TMFDEV_GENERATED_BUILD_IDENTITY\n"
        << "#define TMFDEV_GENERATED_BUILD_SHA256 \""
        << (model.build.sha256.known()
            ? json_escape(*model.build.sha256.value)
            : "")
        << "\"\n"
        << "namespace build {\n"
        << "inline constexpr std::uint64_t ExecutableFileSize = "
        << (model.build.file_size.known()
            ? *model.build.file_size.value
            : 0)
        << "ull;\n"
        << "inline constexpr std::uint16_t Machine = 0x"
        << std::hex << std::uppercase
        << (model.build.machine.known()
            ? *model.build.machine.value
            : 0)
        << "u;\n"
        << "inline constexpr std::uint32_t Timestamp = 0x"
        << (model.build.timestamp.known()
            ? *model.build.timestamp.value
            : 0)
        << "u;\n"
        << "inline constexpr std::uint32_t ImageBase = 0x"
        << (model.build.image_base.known()
            ? *model.build.image_base.value
            : 0)
        << "u;\n"
        << "inline constexpr std::uint32_t ImageSize = 0x"
        << (model.build.image_size.known()
            ? *model.build.image_size.value
            : 0)
        << "u;\n"
        << "inline constexpr char ExecutableSha256[] = \""
        << (model.build.sha256.known()
            ? json_escape(*model.build.sha256.value)
            : "")
        << "\";\n"
        << "constexpr bool sha256_matches(const char* actual) noexcept\n"
        << "{\n"
        << "    for (std::size_t index = 0;\n"
        << "         actual[index] != '\\0' || ExecutableSha256[index] != '\\0';\n"
        << "         ++index) {\n"
        << "        if (actual[index] != ExecutableSha256[index])\n"
        << "            return false;\n"
        << "    }\n"
        << "    return true;\n"
        << "}\n"
        << "inline constexpr bool matches(\n"
        << "    std::uint16_t machine,\n"
        << "    std::uint32_t timestamp,\n"
        << "    std::uint32_t image_base,\n"
        << "    std::uint32_t image_size) noexcept\n"
        << "{\n"
        << "    return machine == Machine\n"
        << "        && timestamp == Timestamp\n"
        << "        && image_base == ImageBase\n"
        << "        && image_size == ImageSize;\n"
        << "}\n"
        << "} // namespace build\n"
        << "#else\n"
        << "#ifndef TMFDEV_GENERATED_BUILD_SHA256\n"
        << "#define TMFDEV_GENERATED_BUILD_SHA256 \"\"\n"
        << "#endif\n"
        << "namespace build {\n"
        << "#ifndef TMFDEV_GENERATED_BUILD_IDENTITY_CHECK\n"
        << "#define TMFDEV_GENERATED_BUILD_IDENTITY_CHECK\n"
        << "constexpr bool sha256_matches(\n"
        << "    const char* actual,\n"
        << "    const char* expected) noexcept\n"
        << "{\n"
        << "    for (std::size_t index = 0;\n"
        << "         actual[index] != '\\0' || expected[index] != '\\0';\n"
        << "         ++index) {\n"
        << "        if (actual[index] != expected[index])\n"
        << "            return false;\n"
        << "    }\n"
        << "    return true;\n"
        << "}\n"
        << "#endif\n"
        << "static_assert(\n"
        << std::dec
        << "    ExecutableFileSize == "
        << (model.build.file_size.known()
            ? *model.build.file_size.value
            : 0)
        << "ull\n"
        << "    && Machine == 0x"
        << std::hex << std::uppercase
        << (model.build.machine.known()
            ? *model.build.machine.value
            : 0)
        << "u\n"
        << "    && Timestamp == 0x"
        << (model.build.timestamp.known()
            ? *model.build.timestamp.value
            : 0)
        << "u\n"
        << "    && ImageBase == 0x"
        << (model.build.image_base.known()
            ? *model.build.image_base.value
            : 0)
        << "u\n"
        << "    && ImageSize == 0x"
        << (model.build.image_size.known()
            ? *model.build.image_size.value
            : 0)
        << "u\n"
        << "    && sha256_matches(\n"
        << "        TMFDEV_GENERATED_BUILD_SHA256,\n"
        << "        \""
        << (model.build.sha256.known()
            ? json_escape(*model.build.sha256.value)
            : "")
        << "\"\n"
        << "    ),\n"
        << "    \"tmfdev generated headers target different executable builds\"\n"
        << ");\n"
        << "} // namespace build\n"
        << "#endif\n\n"
        << std::dec;

    out
        << "class CGameApp;\n";

    if (audio_port_member != model.members.end())
        out << "class CAudioPort;\n";
    if (input_port_member != model.members.end())
        out << "class CInputPort;\n";
    if (block_editor.function != nullptr)
        out << "class CTrackManiaEditor;\n";
    if (playground.function != nullptr)
        out << "class CGamePlayground;\n";

    out
        << "} // namespace tmf\n\n"
        << "namespace tmf::runtime {\n\n";

    const auto emit_vtable_rvas =
        [&](std::string_view name,
            const std::vector<std::uint32_t>& rvas) {
            out << "inline constexpr std::array<std::uint32_t, "
                << rvas.size() << "> " << name << " = {\n";
            for (const auto rva : rvas)
                out << "    0x" << hex8(rva) << "u,\n";
            out << "};\n\n";
        };

    emit_vtable_rvas("GameAppVtableRvas", game_app_vtable_rvas);
    emit_vtable_rvas("AudioPortVtableRvas", audio_port_vtable_rvas);
    emit_vtable_rvas("InputPortVtableRvas", input_port_vtable_rvas);
    emit_vtable_rvas("BlockEditorVtableRvas", block_editor_vtable_rvas);
    emit_vtable_rvas("PlaygroundVtableRvas", playground_vtable_rvas);

    out
        << "enum class Status : std::uint8_t {\n"
        << "    ready,\n"
        << "    module_not_found,\n"
        << "    invalid_image,\n"
        << "    unsupported_build,\n"
        << "    sha256_unavailable,\n"
        << "    sha256_mismatch,\n"
        << "};\n\n"
        << "struct ModuleIdentity {\n"
        << "    std::uint16_t machine = 0;\n"
        << "    std::uint32_t timestamp = 0;\n"
        << "    std::uint32_t image_base = 0;\n"
        << "    std::uint32_t image_size = 0;\n"
        << "};\n\n"
        << "const char* status_name(Status status) noexcept;\n\n"
        << "class Context {\n"
        << "public:\n"
        << "    // The identity must describe the already-loaded target module.\n"
        << "    // The PE header at module_base must match it; without\n"
        << "    // loader_sha256, only this in-memory identity is checked.\n"
        << "    static Context from_module(\n"
        << "        std::uintptr_t module_base,\n"
        << "        ModuleIdentity identity,\n"
        << "        const char* loader_sha256 = nullptr) noexcept;\n"
        << "    // Validates the current process main module and its on-disk SHA-256.\n"
        << "    static Context current_process() noexcept;\n\n"
        << "    bool ready() const noexcept\n"
        << "    {\n"
        << "        return status_ == Status::ready;\n"
        << "    }\n\n"
        << "    explicit operator bool() const noexcept\n"
        << "    {\n"
        << "        return ready();\n"
        << "    }\n\n"
        << "    Status status() const noexcept\n"
        << "    {\n"
        << "        return status_;\n"
        << "    }\n\n"
        << "    bool file_sha256_verified() const noexcept\n"
        << "    {\n"
        << "        return file_sha256_verified_;\n"
        << "    }\n\n"
        << "    std::uintptr_t module_base() const noexcept\n"
        << "    {\n"
        << "        return module_base_;\n"
        << "    }\n\n"
        << "    ModuleIdentity identity() const noexcept\n"
        << "    {\n"
        << "        return identity_;\n"
        << "    }\n\n"
        << "    // Returns zero unless the context is ready and the RVA is in-image.\n"
        << "    std::uintptr_t resolve_rva(\n"
        << "        std::uint32_t rva) const noexcept;\n"
        << "    template <typename Fn>\n"
        << "    Fn resolve_function(std::uint32_t rva) const noexcept\n"
        << "    {\n"
        << "        return reinterpret_cast<Fn>(resolve_rva(rva));\n"
        << "    }\n"
        << "\n"
        << "    // Game-owned global; read again on each call, nullable and non-owning.\n"
        << "    std::uintptr_t game_app_slot() const noexcept;\n"
        << "    CGameApp* game_app() const noexcept;\n";

    if (playground.function != nullptr) {
        out << "    // Exact CTrackMania::GetPlayground view; nullable and non-owning.\n"
            << "    // Reacquire on use: lifecycle sampling observed null and replacement.\n"
            << "    CGamePlayground* playground() const noexcept;\n";
    }

    if (block_editor.function != nullptr) {
        out << "    // Exact CTrackMania::GetTmBlockEditor view; nullable and\n"
            << "    // non-owning. This does not claim active-editor semantics.\n"
            << "    CTrackManiaEditor* block_editor() const noexcept;\n";
    }

    if (audio_port_member != model.members.end()) {
        out << "    // Game-owned service; nullable and non-owning.\n"
            << "    CAudioPort* audio_port() const noexcept;\n";
    }
    if (input_port_member != model.members.end()) {
        out << "    // Game-owned input service; nullable and non-owning.\n"
            << "    CInputPort* input_port() const noexcept;\n";
    }

    out
        << "\n"
        << "private:\n"
        << "    Status status_ = Status::module_not_found;\n"
        << "    bool file_sha256_verified_ = false;\n"
        << "    std::uintptr_t module_base_ = 0;\n"
        << "    ModuleIdentity identity_{};\n"
        << "};\n\n"
        << "inline constexpr std::uint32_t GameAppGlobalRva = 0x"
        << hex8(*global->rva.value)
        << "u;\n";

    if (audio_port_member != model.members.end())
        out << "inline constexpr std::uint32_t GameAppAudioPortOffset = 0x"
            << hex8(*audio_port_member->offset.value)
            << "u;\n";
    if (input_port_member != model.members.end())
        out << "inline constexpr std::uint32_t GameAppInputPortOffset = 0x"
            << hex8(*input_port_member->offset.value)
            << "u;\n";
    if (trackmania_vtable != nullptr) {
        out << "inline constexpr std::uint32_t TrackManiaVtableRva = 0x"
            << hex8(*trackmania_vtable->rva.value)
            << "u;\n";
    }
    if (block_editor.function != nullptr)
        out << "inline constexpr std::uint32_t TrackManiaGetTmBlockEditorRva = 0x"
            << hex8(*block_editor.function->rva.value)
            << "u;\n";
    if (playground.function != nullptr)
        out << "inline constexpr std::uint32_t TrackManiaGetPlaygroundRva = 0x"
            << hex8(*playground.function->rva.value)
            << "u;\n";

    out << "\n} // namespace tmf::runtime\n";

    result.text = out.str();
    return result;
}

NativeSdkOutput generate_native_runtime_source(
    const NativeModel& model)
{
    NativeSdkOutput result;
    const auto block_editor =
        find_runtime_block_editor_evidence(model);
    const auto playground =
        find_runtime_playground_evidence(model);

    const auto audio_port_member = std::find_if(
        model.members.begin(),
        model.members.end(),
        [](const NativeMember& candidate) {
            return candidate.id == "member:CGameApp::AudioPort#3"
                && candidate.offset.known();
        }
    );

    const auto input_port_member = std::find_if(
        model.members.begin(),
        model.members.end(),
        [](const NativeMember& candidate) {
            return candidate.id == "member:CGameApp::InputPort#4"
                && candidate.offset.known();
        }
    );

    result.text = R"tmf(#include <tmfdev/runtime.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <windows.h>
#include <bcrypt.h>

namespace tmf::runtime {
namespace {

bool calculate_sha256(
    const std::filesystem::path& path,
    std::string& result) noexcept
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<std::uint8_t> object;
    std::vector<std::uint8_t> digest;

    const auto failed = [](NTSTATUS status) {
        return status < 0;
    };

    DWORD object_size = 0;
    DWORD hash_size = 0;
    DWORD result_size = 0;

    if (failed(BCryptOpenAlgorithmProvider(
            &algorithm,
            BCRYPT_SHA256_ALGORITHM,
            nullptr,
            0))
        || failed(BCryptGetProperty(
            algorithm,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_size),
            sizeof(object_size),
            &result_size,
            0))
        || failed(BCryptGetProperty(
            algorithm,
            BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hash_size),
            sizeof(hash_size),
            &result_size,
            0))
    ) {
        if (algorithm)
            BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    object.resize(object_size);
    digest.resize(hash_size);

    if (failed(BCryptCreateHash(
            algorithm,
            &hash,
            object.data(),
            static_cast<ULONG>(object.size()),
            nullptr,
            0,
            0))) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    std::array<std::uint8_t, 64 * 1024> buffer{};
    while (input) {
        input.read(
            reinterpret_cast<char*>(buffer.data()),
            static_cast<std::streamsize>(buffer.size())
        );
        const auto count = input.gcount();
        if (count > 0
            && failed(BCryptHashData(
                hash,
                buffer.data(),
                static_cast<ULONG>(count),
                0))) {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return false;
        }
    }

    if (input.bad()
        || failed(BCryptFinishHash(
            hash,
            digest.data(),
            static_cast<ULONG>(digest.size()),
            0))) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);

    std::ostringstream text;
    text << std::hex << std::setfill('0');
    for (const auto byte : digest)
        text << std::setw(2) << static_cast<unsigned>(byte);

    result = text.str();
    return true;
}

bool read_memory(
    std::uintptr_t address,
    void* destination,
    std::size_t size) noexcept
{
    if (!address || !destination || !size
        || address > (std::numeric_limits<std::uintptr_t>::max)() - size)
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(
            reinterpret_cast<const void*>(address),
            &memory,
            sizeof(memory)) != sizeof(memory))
        return false;

    if (memory.State != MEM_COMMIT
        || !memory.Protect
        || (memory.Protect & PAGE_NOACCESS)
        || (memory.Protect & PAGE_GUARD))
        return false;

    const auto region_begin =
        reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    const auto region_size =
        static_cast<std::uintptr_t>(memory.RegionSize);

    if (region_begin > (std::numeric_limits<std::uintptr_t>::max)()
            - region_size)
        return false;

    const auto region_end = region_begin + region_size;
    const auto range_end = address + size;
    if (address < region_begin || range_end > region_end)
        return false;

    SIZE_T bytes_read = 0;
    return ReadProcessMemory(
                GetCurrentProcess(),
                reinterpret_cast<const void*>(address),
                destination,
                size,
                &bytes_read)
        != FALSE
        && bytes_read == size;
}

bool read_process_identity(
    std::uintptr_t module_base,
    ModuleIdentity& identity) noexcept
{
    IMAGE_DOS_HEADER dos{};
    if (!read_memory(module_base, &dos, sizeof(dos))
        || dos.e_magic != IMAGE_DOS_SIGNATURE
        || dos.e_lfanew < static_cast<LONG>(sizeof(IMAGE_DOS_HEADER))
        || dos.e_lfanew > 0x00100000)
        return false;

    if (module_base > (std::numeric_limits<std::uintptr_t>::max)()
            - static_cast<std::uintptr_t>(dos.e_lfanew))
        return false;

    const auto nt_address = module_base
        + static_cast<std::uintptr_t>(dos.e_lfanew);
    IMAGE_NT_HEADERS32 nt{};
    if (!read_memory(nt_address, &nt, sizeof(nt))
        || nt.Signature != IMAGE_NT_SIGNATURE
        || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        return false;

    identity.machine = nt.FileHeader.Machine;
    identity.timestamp = nt.FileHeader.TimeDateStamp;
    identity.image_base = nt.OptionalHeader.ImageBase;
    identity.image_size = nt.OptionalHeader.SizeOfImage;
    return identity.image_size != 0;
}

bool read_pointer(
    std::uintptr_t address,
    std::uintptr_t& value) noexcept
{
    return read_memory(address, &value, sizeof(value));
}

bool valid_object_pointer(
    const Context& context,
    std::uintptr_t value,
    const std::uint32_t* expected_vtable_rvas,
    std::size_t expected_vtable_count) noexcept
{
    if (!value || (value % alignof(void*)) != 0)
        return false;

    std::uintptr_t vtable = 0;
    if (!read_pointer(value, vtable))
        return false;

    const auto image_first = context.resolve_rva(0);
    const auto image_last = context.resolve_rva(
        context.identity().image_size - 1
    );

    if (!(image_first
        && image_last
        && vtable >= image_first
        && vtable <= image_last))
        return false;

    if (expected_vtable_rvas && expected_vtable_count) {
        for (std::size_t index = 0;
             index < expected_vtable_count;
             ++index) {
            if (vtable == context.resolve_rva(expected_vtable_rvas[index]))
                return true;
        }
        return false;
    }

    return true;
}

bool read_object_pointer(
    const Context& context,
    std::uintptr_t address,
    std::uintptr_t& value,
    const std::uint32_t* expected_vtable_rvas = nullptr,
    std::size_t expected_vtable_count = 0) noexcept
{
    return read_pointer(address, value)
        && valid_object_pointer(
            context,
            value,
            expected_vtable_rvas,
            expected_vtable_count
        );
}

} // namespace

const char* status_name(Status status) noexcept
{
    switch (status) {
    case Status::ready:
        return "ready";
    case Status::module_not_found:
        return "module_not_found";
    case Status::invalid_image:
        return "invalid_image";
    case Status::unsupported_build:
        return "unsupported_build";
    case Status::sha256_unavailable:
        return "sha256_unavailable";
    case Status::sha256_mismatch:
        return "sha256_mismatch";
    }

    return "unknown";
}

Context Context::from_module(
    std::uintptr_t module_base,
    ModuleIdentity identity,
    const char* loader_sha256) noexcept
{
    Context result;
    result.module_base_ = module_base;
    result.identity_ = identity;

    if (!module_base || !identity.image_size) {
        result.status_ = Status::invalid_image;
        return result;
    }

    ModuleIdentity actual_identity;
    if (!read_process_identity(module_base, actual_identity)) {
        result.status_ = Status::invalid_image;
        return result;
    }

    if (actual_identity.machine != identity.machine
        || actual_identity.timestamp != identity.timestamp
        || actual_identity.image_base != identity.image_base
        || actual_identity.image_size != identity.image_size) {
        result.identity_ = actual_identity;
        result.status_ = Status::invalid_image;
        return result;
    }

    result.identity_ = actual_identity;

    if (!tmf::build::matches(
            actual_identity.machine,
            actual_identity.timestamp,
            actual_identity.image_base,
            actual_identity.image_size)) {
        result.status_ = Status::unsupported_build;
        return result;
    }

    if (loader_sha256) {
        if (!tmf::build::sha256_matches(loader_sha256)) {
            result.status_ = Status::sha256_mismatch;
            return result;
        }
        result.file_sha256_verified_ = true;
    }

    result.status_ = Status::ready;
    return result;
}

Context Context::current_process() noexcept
{
#ifdef _WIN32
    const auto module = GetModuleHandleW(nullptr);
    if (!module) {
        Context result;
        result.status_ = Status::module_not_found;
        return result;
    }

    const auto module_base = reinterpret_cast<std::uintptr_t>(module);
    ModuleIdentity identity;
    if (!read_process_identity(module_base, identity)) {
        Context result;
        result.module_base_ = module_base;
        result.status_ = Status::invalid_image;
        return result;
    }

    std::array<wchar_t, 32768> path{};
    const auto length = GetModuleFileNameW(
        nullptr,
        path.data(),
        static_cast<DWORD>(path.size())
    );
    if (!length || length >= path.size()) {
        Context result;
        result.module_base_ = module_base;
        result.identity_ = identity;
        result.status_ = Status::sha256_unavailable;
        return result;
    }

    std::string sha256;
    if (!calculate_sha256(
            std::filesystem::path(std::wstring(path.data(), length)),
            sha256)) {
        Context result;
        result.module_base_ = module_base;
        result.identity_ = identity;
        result.status_ = Status::sha256_unavailable;
        return result;
    }

    return from_module(module_base, identity, sha256.c_str());
#else
    Context result;
    result.status_ = Status::module_not_found;
    return result;
#endif
}

std::uintptr_t Context::resolve_rva(std::uint32_t rva) const noexcept
{
    if (!ready()
        || rva >= identity_.image_size
        || module_base_ > (std::numeric_limits<std::uintptr_t>::max)() - rva)
        return 0;

    return module_base_ + rva;
}

std::uintptr_t Context::game_app_slot() const noexcept
{
    return resolve_rva(GameAppGlobalRva);
}

CGameApp* Context::game_app() const noexcept
{
    const auto slot = game_app_slot();
    if (!slot)
        return nullptr;

    std::uintptr_t pointer = 0;
    if (!read_object_pointer(
            *this,
            slot,
            pointer,
            GameAppVtableRvas.data(),
            GameAppVtableRvas.size()))
        return nullptr;

    return reinterpret_cast<CGameApp*>(pointer);
}

)tmf";

    if (playground.function != nullptr) {
        result.text +=
            "CGamePlayground* Context::playground() const noexcept\n"
            "{\n"
            "    const auto game = game_app();\n"
            "    if (!game)\n"
            "        return nullptr;\n\n"
            "    std::uintptr_t game_vtable = 0;\n"
            "    if (!read_pointer(\n"
            "            reinterpret_cast<std::uintptr_t>(game),\n"
            "            game_vtable)\n"
            "        || game_vtable != resolve_rva(TrackManiaVtableRva))\n"
            "        return nullptr;\n\n"
            "    const auto getter_address =\n"
            "        resolve_rva(TrackManiaGetPlaygroundRva);\n"
            "    if (!getter_address)\n"
            "        return nullptr;\n\n"
            "    using GetPlaygroundFn =\n"
            "        CGamePlayground* (__thiscall *)(CGameApp*);\n"
            "    const auto getter = reinterpret_cast<GetPlaygroundFn>(\n"
            "        getter_address\n"
            "    );\n"
            "    const auto playground = getter(game);\n"
            "    const auto playground_address =\n"
            "        reinterpret_cast<std::uintptr_t>(playground);\n"
            "    if (!playground\n"
            "            || !valid_object_pointer(\n"
            "                *this,\n"
            "                playground_address,\n"
            "                PlaygroundVtableRvas.data(),\n"
            "                PlaygroundVtableRvas.size()))\n"
            "        return nullptr;\n\n"
            "    return playground;\n"
            "}\n\n";
    }

    if (audio_port_member != model.members.end()) {
        result.text +=
            "CAudioPort* Context::audio_port() const noexcept\n"
            "{\n"
            "    const auto game = game_app();\n"
            "    if (!game)\n"
            "        return nullptr;\n\n"
            "    const auto address = reinterpret_cast<std::uintptr_t>(game);\n"
            "    if (address > (std::numeric_limits<std::uintptr_t>::max)()\n"
            "            - GameAppAudioPortOffset)\n"
            "        return nullptr;\n\n"
            "    std::uintptr_t pointer = 0;\n"
            "    if (!read_object_pointer(\n"
            "            *this,\n"
            "            address + GameAppAudioPortOffset,\n"
            "            pointer,\n"
            "            AudioPortVtableRvas.data(),\n"
            "            AudioPortVtableRvas.size()))\n"
            "        return nullptr;\n\n"
            "    return reinterpret_cast<CAudioPort*>(pointer);\n"
            "}\n\n";
    }

    if (input_port_member != model.members.end()) {
        result.text +=
            "CInputPort* Context::input_port() const noexcept\n"
            "{\n"
            "    const auto game = game_app();\n"
            "    if (!game)\n"
            "        return nullptr;\n\n"
            "    const auto address = reinterpret_cast<std::uintptr_t>(game);\n"
            "    if (address > (std::numeric_limits<std::uintptr_t>::max)()\n"
            "            - GameAppInputPortOffset)\n"
            "        return nullptr;\n\n"
            "    std::uintptr_t pointer = 0;\n"
            "    if (!read_object_pointer(\n"
            "            *this,\n"
            "            address + GameAppInputPortOffset,\n"
            "            pointer,\n"
            "            InputPortVtableRvas.data(),\n"
            "            InputPortVtableRvas.size()))\n"
            "        return nullptr;\n\n"
            "    return reinterpret_cast<CInputPort*>(pointer);\n"
            "}\n\n";
    }

    if (block_editor.function != nullptr) {
        result.text +=
            "CTrackManiaEditor* Context::block_editor() const noexcept\n"
            "{\n"
            "    const auto game = game_app();\n"
            "    if (!game)\n"
            "        return nullptr;\n\n"
            "    std::uintptr_t game_vtable = 0;\n"
            "    if (!read_pointer(\n"
            "            reinterpret_cast<std::uintptr_t>(game),\n"
            "            game_vtable)\n"
            "        || game_vtable != resolve_rva(TrackManiaVtableRva))\n"
            "        return nullptr;\n\n"
            "    const auto getter_address =\n"
            "        resolve_rva(TrackManiaGetTmBlockEditorRva);\n"
            "    if (!getter_address)\n"
            "        return nullptr;\n\n"
            "    using GetTmBlockEditorFn =\n"
            "        CTrackManiaEditor* (__thiscall *)(CGameApp*);\n"
            "    const auto getter = reinterpret_cast<GetTmBlockEditorFn>(\n"
            "        getter_address\n"
            "    );\n"
            "    const auto editor = getter(game);\n"
            "    const auto editor_address =\n"
            "        reinterpret_cast<std::uintptr_t>(editor);\n"
            "    if (!editor\n"
            "            || !valid_object_pointer(\n"
            "                *this,\n"
            "                editor_address,\n"
            "                BlockEditorVtableRvas.data(),\n"
            "                BlockEditorVtableRvas.size()))\n"
            "        return nullptr;\n\n"
            "    return editor;\n"
            "}\n\n";
    }

    result.text += "} // namespace tmf::runtime\n";
    return result;
}

NativeSdkManifestOutput generate_native_sdk_manifest_json(
    const NativeModel& model,
    const std::string& requested_class)
{
    NativeSdkManifestOutput result;
    std::ostringstream out;

    const bool emit_all_classes = requested_class.empty();
    std::size_t emitted = 0;
    std::size_t skipped = 0;

    std::unordered_map<std::string, const NativeMember*> members_by_id;
    for (const auto& member : model.members)
        members_by_id.emplace(member.id, &member);

    std::unordered_map<std::string, const NativeVtable*> vtables_by_id;
    for (const auto& vtable : model.vtables)
        vtables_by_id.emplace(vtable.id, &vtable);

    out << "{\n  \"schema\": 2,\n  \"build\": {";
    bool first = true;

    if (model.build.file_size.known())
        emit_json_number(out, "file_size", *model.build.file_size.value, first);
    if (model.build.sha256.known())
        emit_json_string(out, "sha256", *model.build.sha256.value, first);
    if (model.build.machine.known())
        emit_json_number(out, "machine", *model.build.machine.value, first);
    if (model.build.timestamp.known())
        emit_json_number(out, "timestamp", *model.build.timestamp.value, first);
    if (model.build.image_base.known())
        emit_json_number(out, "image_base", *model.build.image_base.value, first);
    if (model.build.image_size.known())
        emit_json_number(out, "image_size", *model.build.image_size.value, first);

    out << "},\n  \"requested_class\":";
    if (requested_class.empty())
        out << "null";
    else
        out << "\"" << json_escape(requested_class) << "\"";

    out << ",\n  \"functions\": [\n";
    bool first_function = true;

    for (const auto& function : model.functions) {
        const auto owner = class_owner(function.qualified_name);
        if (!emit_all_classes && owner != requested_class)
            continue;

        std::string reason;
        const auto abi = assess_native_function_abi(model, function);
        const bool supported =
            function_codegen_supported(model, function, reason)
            && (emit_all_classes ? is_representable_owner(owner) : true);

        if (supported)
            ++emitted;
        else
            ++skipped;

        if (!first_function)
            out << ",\n";
        first_function = false;

        out << "    {";
        bool first_field = true;
        emit_json_string(out, "id", function.id, first_field);
        emit_json_string(out, "qualified_name", function.qualified_name, first_field);
        emit_json_string(out, "owner", owner, first_field);

        if (function.rva.known())
            emit_json_number(out, "rva", *function.rva.value, first_field);
        else
            emit_json_null(out, "rva", first_field);

        if (function.physical_code_id.known())
            emit_json_string(out, "physical_code_id", *function.physical_code_id.value, first_field);
        else
            emit_json_null(out, "physical_code_id", first_field);

        emit_json_string(out, "abi_status", native_abi_status_name(abi.status), first_field);
        emit_json_bool(out, "emitted", supported, first_field);

        emit_json_abi_issues(out, abi.issues, first_field);

        if (!first_field)
            out << ",";
        out << "\"signature\":{";
        bool first_signature_field = true;

        if (function.return_type.known())
            emit_json_string(
                out,
                "return_type",
                *function.return_type.value,
                first_signature_field
            );
        else
            emit_json_null(
                out,
                "return_type",
                first_signature_field
            );

        if (function.return_type_id.known())
            emit_json_string(
                out,
                "return_type_id",
                *function.return_type_id.value,
                first_signature_field
            );
        else
            emit_json_null(
                out,
                "return_type_id",
                first_signature_field
            );

        if (function.return_abi.known())
            emit_json_string(
                out,
                "return_abi",
                native_return_abi_name(
                    *function.return_abi.value
                ),
                first_signature_field
            );
        else
            emit_json_null(
                out,
                "return_abi",
                first_signature_field
            );

        if (function.calling_convention.known())
            emit_json_string(
                out,
                "calling_convention",
                calling_convention_name(
                    *function.calling_convention.value
                ),
                first_signature_field
            );
        else
            emit_json_null(
                out,
                "calling_convention",
                first_signature_field
            );

        if (function.access.known())
            emit_json_string(
                out,
                "access",
                native_access_name(*function.access.value),
                first_signature_field
            );
        else
            emit_json_null(
                out,
                "access",
                first_signature_field
            );

        if (function.is_virtual.known())
            emit_json_bool(
                out,
                "is_virtual",
                *function.is_virtual.value,
                first_signature_field
            );
        else
            emit_json_null(
                out,
                "is_virtual",
                first_signature_field
            );

        if (function.is_static.known())
            emit_json_bool(
                out,
                "is_static",
                *function.is_static.value,
                first_signature_field
            );
        else
            emit_json_null(
                out,
                "is_static",
                first_signature_field
            );

        if (function.is_const.known())
            emit_json_bool(
                out,
                "is_const",
                *function.is_const.value,
                first_signature_field
            );
        else
            emit_json_null(
                out,
                "is_const",
                first_signature_field
            );

        if (function.return_usage.base_type.known())
            emit_json_string(
                out,
                "return_base_type",
                *function.return_usage.base_type.value,
                first_signature_field
            );
        else
            emit_json_null(
                out,
                "return_base_type",
                first_signature_field
            );

        if (function.return_usage.pass_kind.known())
            emit_json_string(
                out,
                "return_pass_kind",
                native_type_pass_kind_name(
                    *function.return_usage.pass_kind.value
                ),
                first_signature_field
            );
        else
            emit_json_null(
                out,
                "return_pass_kind",
                first_signature_field
            );

        if (function.return_usage.declarator_kind.known())
            emit_json_string(
                out,
                "return_declarator_kind",
                native_type_declarator_kind_name(
                    *function.return_usage.declarator_kind.value
                ),
                first_signature_field
            );
        else
            emit_json_null(
                out,
                "return_declarator_kind",
                first_signature_field
            );

        if (function.return_usage.pointer_depth.known())
            emit_json_number(
                out,
                "return_pointer_depth",
                *function.return_usage.pointer_depth.value,
                first_signature_field
            );
        else
            emit_json_null(
                out,
                "return_pointer_depth",
                first_signature_field
            );

        if (function.return_usage.is_const.known())
            emit_json_bool(
                out,
                "return_const",
                *function.return_usage.is_const.value,
                first_signature_field
            );
        else
            emit_json_null(
                out,
                "return_const",
                first_signature_field
            );

        if (function.return_usage.parse_error.known())
            emit_json_string(
                out,
                "return_parse_error",
                *function.return_usage.parse_error.value,
                first_signature_field
            );
        else
            emit_json_null(
                out,
                "return_parse_error",
                first_signature_field
            );

        if (!first_signature_field)
            out << ",";
        out << "\"parameters\":[";
        bool first_parameter = true;

        for (const auto& parameter : function.parameters) {
            if (!first_parameter)
                out << ",";

            out << "{";
            bool first_parameter_field = true;
            emit_json_number(
                out,
                "index",
                parameter.index,
                first_parameter_field
            );

            if (parameter.type.known())
                emit_json_string(
                    out,
                    "type",
                    *parameter.type.value,
                    first_parameter_field
                );
            else
                emit_json_null(
                    out,
                    "type",
                    first_parameter_field
                );

            if (parameter.referenced_type_id.known())
                emit_json_string(
                    out,
                    "referenced_type_id",
                    *parameter.referenced_type_id.value,
                    first_parameter_field
                );
            else
                emit_json_null(
                    out,
                    "referenced_type_id",
                    first_parameter_field
                );

            if (parameter.usage.base_type.known())
                emit_json_string(
                    out,
                    "base_type",
                    *parameter.usage.base_type.value,
                    first_parameter_field
                );
            else
                emit_json_null(
                    out,
                    "base_type",
                    first_parameter_field
                );

            if (parameter.usage.pass_kind.known())
                emit_json_string(
                    out,
                    "pass_kind",
                    native_type_pass_kind_name(
                        *parameter.usage.pass_kind.value
                    ),
                    first_parameter_field
                );
            else
                emit_json_null(
                    out,
                    "pass_kind",
                    first_parameter_field
                );

            if (parameter.usage.declarator_kind.known())
                emit_json_string(
                    out,
                    "declarator_kind",
                    native_type_declarator_kind_name(
                        *parameter.usage.declarator_kind.value
                    ),
                    first_parameter_field
                );
            else
                emit_json_null(
                    out,
                    "declarator_kind",
                    first_parameter_field
                );

            if (parameter.usage.pointer_depth.known())
                emit_json_number(
                    out,
                    "pointer_depth",
                    *parameter.usage.pointer_depth.value,
                    first_parameter_field
                );
            else
                emit_json_null(
                    out,
                    "pointer_depth",
                    first_parameter_field
                );

            if (parameter.usage.is_const.known())
                emit_json_bool(
                    out,
                    "const",
                    *parameter.usage.is_const.value,
                    first_parameter_field
                );
            else
                emit_json_null(
                    out,
                    "const",
                    first_parameter_field
                );

            if (parameter.usage.parse_error.known())
                emit_json_string(
                    out,
                    "parse_error",
                    *parameter.usage.parse_error.value,
                    first_parameter_field
                );
            else
                emit_json_null(
                    out,
                    "parse_error",
                    first_parameter_field
                );

            out << "}";
            first_parameter = false;
        }

        out << "]}";
        first_field = false;

        std::vector<Evidence> evidence;
        append_fact_evidence(evidence, function.decorated_name);
        append_fact_evidence(evidence, function.return_type);
        append_fact_evidence(evidence, function.return_abi);
        append_fact_evidence(evidence, function.calling_convention);
        append_fact_evidence(evidence, function.rva);
        append_fact_evidence(evidence, function.physical_code_id);
        emit_json_evidence(out, "evidence", evidence, first_field);

        if (function.virtual_slot.known())
            emit_json_number(out, "virtual_slot", *function.virtual_slot.value, first_field);
        else
            emit_json_null(out, "virtual_slot", first_field);

        if (!supported)
            emit_json_string(out, "reason", reason.empty() ? "owner is not a representable generated C++ class" : reason, first_field);

        out << "}";
    }

    out << "\n  ],\n  \"classes\": [\n";
    bool first_class = true;

    for (const auto& native_class : model.classes) {
        if (!emit_all_classes
            && native_class.name != requested_class) {
            continue;
        }

        if (!first_class)
            out << ",\n";
        first_class = false;

        out << "    {";
        bool first_field = true;
        emit_json_string(out, "id", native_class.id, first_field);
        emit_json_string(out, "name", native_class.name, first_field);
        if (native_class.mw_class_id.known())
            emit_json_number(
                out,
                "mw_class_id",
                *native_class.mw_class_id.value,
                first_field
            );
        else
            emit_json_null(out, "mw_class_id", first_field);

        std::vector<Evidence> class_evidence;
        append_fact_evidence(
            class_evidence,
            native_class.mw_class_id
        );
        emit_json_evidence(
            out,
            "class_evidence",
            class_evidence,
            first_field
        );
        emit_json_string_array(
            out,
            "direct_function_ids",
            native_class.function_ids,
            first_field
        );

        if (!first_field)
            out << ",";
        out << "\"rtti_hierarchy\":[";
        bool first_base = true;
        for (const auto& base : native_class.rtti_hierarchy) {
            if (!first_base)
                out << ",";
            first_base = false;

            out << "{";
            bool first_base_field = true;
            emit_json_string(
                out,
                "class_id",
                base.class_id,
                first_base_field
            );

            const std::string class_prefix = "class:";
            const auto base_name = base.class_id.starts_with(class_prefix)
                ? base.class_id.substr(class_prefix.size())
                : base.class_id;
            emit_json_string(
                out,
                "class_name",
                base_name,
                first_base_field
            );
            emit_json_number(
                out,
                "contained_bases",
                base.contained_bases,
                first_base_field
            );

            if (base.member_displacement.known())
                emit_json_number(
                    out,
                    "member_displacement",
                    *base.member_displacement.value,
                    first_base_field
                );
            else
                emit_json_null(
                    out,
                    "member_displacement",
                    first_base_field
                );

            if (base.vbtable_displacement.known())
                emit_json_number(
                    out,
                    "vbtable_displacement",
                    *base.vbtable_displacement.value,
                    first_base_field
                );
            else
                emit_json_null(
                    out,
                    "vbtable_displacement",
                    first_base_field
                );

            if (base.displacement_inside_vbtable.known())
                emit_json_number(
                    out,
                    "displacement_inside_vbtable",
                    *base.displacement_inside_vbtable.value,
                    first_base_field
                );
            else
                emit_json_null(
                    out,
                    "displacement_inside_vbtable",
                    first_base_field
                );

            std::vector<Evidence> base_evidence;
            append_fact_evidence(
                base_evidence,
                base.member_displacement
            );
            append_fact_evidence(
                base_evidence,
                base.vbtable_displacement
            );
            append_fact_evidence(
                base_evidence,
                base.displacement_inside_vbtable
            );
            emit_json_evidence(
                out,
                "evidence",
                base_evidence,
                first_base_field
            );

            out << "}";
        }
        out << "]";

        out << ",\"members\":[";
        bool first_member = true;
        for (const auto& member_id : native_class.member_ids) {
            const auto member_it = members_by_id.find(member_id);
            if (member_it == members_by_id.end())
                continue;

            if (!first_member)
                out << ",";
            first_member = false;

            const auto& member = *member_it->second;
            out << "{";
            bool first_member_field = true;
            emit_json_string(out, "id", member.id, first_member_field);
            emit_json_string(out, "name", member.name, first_member_field);

            if (member.type.known())
                emit_json_string(
                    out,
                    "type",
                    *member.type.value,
                    first_member_field
                );
            else
                emit_json_null(out, "type", first_member_field);

            if (member.offset.known())
                emit_json_number(
                    out,
                    "offset",
                    *member.offset.value,
                    first_member_field
                );
            else
                emit_json_null(out, "offset", first_member_field);

            std::vector<Evidence> member_evidence;
            append_fact_evidence(member_evidence, member.type);
            append_fact_evidence(member_evidence, member.offset);
            emit_json_evidence(
                out,
                "evidence",
                member_evidence,
                first_member_field
            );
            out << "}";
        }
        out << "]";

        emit_json_string_array(
            out,
            "reflection_descriptor_ids",
            native_class.reflection_descriptor_ids,
            first_field
        );

        out << ",\"vtable\":{";
        bool first_vtable_field = true;

        if (native_class.vtable_state.known())
            emit_json_string(
                out,
                "state",
                native_vtable_state_name(
                    *native_class.vtable_state.value
                ),
                first_vtable_field
            );
        else
            emit_json_null(out, "state", first_vtable_field);

        if (native_class.vtable_id.known())
            emit_json_string(
                out,
                "id",
                *native_class.vtable_id.value,
                first_vtable_field
            );
        else
            emit_json_null(out, "id", first_vtable_field);

        if (native_class.vtable_error.known())
            emit_json_string(
                out,
                "error",
                *native_class.vtable_error.value,
                first_vtable_field
            );
        else
            emit_json_null(out, "error", first_vtable_field);

        const NativeVtable* vtable = nullptr;
        if (native_class.vtable_id.known()) {
            const auto vtable_it = vtables_by_id.find(
                *native_class.vtable_id.value
            );
            if (vtable_it != vtables_by_id.end())
                vtable = vtable_it->second;
        }

        if (vtable != nullptr && vtable->rva.known())
            emit_json_number(
                out,
                "rva",
                *vtable->rva.value,
                first_vtable_field
            );
        else
            emit_json_null(out, "rva", first_vtable_field);

        if (vtable != nullptr && vtable->virtual_address.known())
            emit_json_number(
                out,
                "virtual_address",
                *vtable->virtual_address.value,
                first_vtable_field
            );
        else
            emit_json_null(out, "virtual_address", first_vtable_field);

        std::vector<Evidence> vtable_evidence;
        append_fact_evidence(
            vtable_evidence,
            native_class.vtable_state
        );
        append_fact_evidence(
            vtable_evidence,
            native_class.vtable_id
        );
        append_fact_evidence(
            vtable_evidence,
            native_class.vtable_error
        );
        if (vtable != nullptr) {
            append_fact_evidence(vtable_evidence, vtable->rva);
            append_fact_evidence(
                vtable_evidence,
                vtable->virtual_address
            );
        }
        emit_json_evidence(
            out,
            "evidence",
            vtable_evidence,
            first_vtable_field
        );

        if (!first_vtable_field)
            out << ",";
        out << "\"slots\":[";
        bool first_slot = true;

        if (vtable != nullptr) {
            for (const auto& slot : vtable->slots) {
                if (!first_slot)
                    out << ",";
                first_slot = false;

                out << "{";
                bool first_slot_field = true;
                emit_json_number(
                    out,
                    "index",
                    slot.index,
                    first_slot_field
                );

                if (slot.target_rva.known())
                    emit_json_number(
                        out,
                        "target_rva",
                        *slot.target_rva.value,
                        first_slot_field
                    );
                else
                    emit_json_null(
                        out,
                        "target_rva",
                        first_slot_field
                    );

                if (slot.physical_code_id.known())
                    emit_json_string(
                        out,
                        "physical_code_id",
                        *slot.physical_code_id.value,
                        first_slot_field
                    );
                else
                    emit_json_null(
                        out,
                        "physical_code_id",
                        first_slot_field
                    );

                emit_json_string_array(
                    out,
                    "candidate_function_ids",
                    slot.candidate_function_ids,
                    first_slot_field
                );

                if (slot.resolved_function_id.known())
                    emit_json_string(
                        out,
                        "resolved_function_id",
                        *slot.resolved_function_id.value,
                        first_slot_field
                    );
                else
                    emit_json_null(
                        out,
                        "resolved_function_id",
                        first_slot_field
                    );

                std::vector<Evidence> slot_evidence;
                append_fact_evidence(slot_evidence, slot.target_rva);
                append_fact_evidence(
                    slot_evidence,
                    slot.physical_code_id
                );
                append_fact_evidence(
                    slot_evidence,
                    slot.resolved_function_id
                );
                emit_json_evidence(
                    out,
                    "evidence",
                    slot_evidence,
                    first_slot_field
                );
                out << "}";
            }
        }
        out << "]}";
        out << "}";
    }

    out << "\n  ],\n  \"reflection_descriptors\": [\n";
    bool first_reflection_descriptor = true;
    std::vector<const NativeReflectionDescriptor*> reflection_descriptors;
    reflection_descriptors.reserve(model.reflection_descriptors.size());
    for (const auto& descriptor : model.reflection_descriptors) {
        if (!emit_all_classes
            && descriptor.owner_class_name != requested_class) {
            continue;
        }
        reflection_descriptors.push_back(&descriptor);
    }
    std::sort(
        reflection_descriptors.begin(),
        reflection_descriptors.end(),
        [](const NativeReflectionDescriptor* left,
           const NativeReflectionDescriptor* right) {
            return left->id < right->id;
        }
    );
    for (const auto* descriptor : reflection_descriptors) {
        if (!first_reflection_descriptor)
            out << ",\n";
        first_reflection_descriptor = false;
        out << "    ";
        emit_json_reflection_descriptor(out, *descriptor);
    }

    out << "\n  ],\n  \"types\": [\n";
    bool first_type = true;

    for (const auto& type : model.types) {
        if (!first_type)
            out << ",\n";
        first_type = false;

        out << "    {";
        bool first_type_field = true;
        emit_json_string(out, "id", type.id, first_type_field);
        emit_json_string(out, "name", type.name, first_type_field);

        if (type.kind.known())
            emit_json_string(
                out,
                "kind",
                native_type_kind_name(*type.kind.value),
                first_type_field
            );
        else
            emit_json_null(out, "kind", first_type_field);

        if (type.size.known())
            emit_json_number(
                out,
                "size",
                *type.size.value,
                first_type_field
            );
        else
            emit_json_null(out, "size", first_type_field);

        if (type.alignment.known())
            emit_json_number(
                out,
                "alignment",
                *type.alignment.value,
                first_type_field
            );
        else
            emit_json_null(out, "alignment", first_type_field);

        std::vector<Evidence> type_evidence;
        append_fact_evidence(type_evidence, type.kind);
        append_fact_evidence(type_evidence, type.size);
        append_fact_evidence(type_evidence, type.alignment);
        for (const auto& field : type.fields) {
            append_fact_evidence(type_evidence, field.name);
            append_fact_evidence(type_evidence, field.type);
            append_fact_evidence(type_evidence, field.offset);
            append_fact_evidence(type_evidence, field.size);
        }
        emit_json_evidence(
            out,
            "evidence",
            type_evidence,
            first_type_field
        );

        out << ",\"fields\":[";
        bool first_type_field_entry = true;
        for (const auto& field : type.fields) {
            if (!first_type_field_entry)
                out << ",";
            first_type_field_entry = false;

            out << "{";
            bool first_field_entry = true;
            emit_json_number(
                out,
                "index",
                field.index,
                first_field_entry
            );

            if (field.name.known())
                emit_json_string(
                    out,
                    "name",
                    *field.name.value,
                    first_field_entry
                );
            else
                emit_json_null(out, "name", first_field_entry);

            if (field.type.known())
                emit_json_string(
                    out,
                    "type",
                    *field.type.value,
                    first_field_entry
                );
            else
                emit_json_null(out, "type", first_field_entry);

            if (field.offset.known())
                emit_json_number(
                    out,
                    "offset",
                    *field.offset.value,
                    first_field_entry
                );
            else
                emit_json_null(out, "offset", first_field_entry);

            if (field.size.known())
                emit_json_number(
                    out,
                    "size",
                    *field.size.value,
                    first_field_entry
                );
            else
                emit_json_null(out, "size", first_field_entry);

            std::vector<Evidence> field_evidence;
            append_fact_evidence(field_evidence, field.name);
            append_fact_evidence(field_evidence, field.type);
            append_fact_evidence(field_evidence, field.offset);
            append_fact_evidence(field_evidence, field.size);
            emit_json_evidence(
                out,
                "evidence",
                field_evidence,
                first_field_entry
            );
            out << "}";
        }
        out << "]}";
    }

    out << "\n  ],\n  \"globals\": [\n";
    bool first_global = true;

    for (const auto& global : model.globals) {
        if (!emit_all_classes
            && static_global_owner(global.name) != requested_class) {
            continue;
        }

        if (!first_global)
            out << ",\n";
        first_global = false;

        out << "    {";
        bool first_global_field = true;
        emit_json_string(out, "id", global.id, first_global_field);
        emit_json_string(out, "name", global.name, first_global_field);

        if (global.decorated_name.known())
            emit_json_string(
                out,
                "decorated_name",
                *global.decorated_name.value,
                first_global_field
            );
        else
            emit_json_null(
                out,
                "decorated_name",
                first_global_field
            );

        if (global.type.known())
            emit_json_string(
                out,
                "type",
                *global.type.value,
                first_global_field
            );
        else
            emit_json_null(out, "type", first_global_field);

        if (global.rva.known())
            emit_json_number(
                out,
                "rva",
                *global.rva.value,
                first_global_field
            );
        else
            emit_json_null(out, "rva", first_global_field);

        if (global.virtual_address.known())
            emit_json_number(
                out,
                "virtual_address",
                *global.virtual_address.value,
                first_global_field
            );
        else
            emit_json_null(
                out,
                "virtual_address",
                first_global_field
            );

        std::vector<Evidence> global_evidence;
        append_fact_evidence(global_evidence, global.decorated_name);
        append_fact_evidence(global_evidence, global.type);
        append_fact_evidence(global_evidence, global.rva);
        append_fact_evidence(global_evidence, global.virtual_address);
        emit_json_evidence(
            out,
            "evidence",
            global_evidence,
            first_global_field
        );
        out << "}";
    }

    out << "\n  ],\n  \"counts\": {\n"
        << "    \"model_functions\": " << model.functions.size() << ",\n"
        << "    \"model_classes\": " << model.classes.size() << ",\n"
        << "    \"model_types\": " << model.types.size() << ",\n"
        << "    \"model_members\": " << model.members.size() << ",\n"
        << "    \"model_reflection_descriptors\": " << model.reflection_descriptors.size() << ",\n"
        << "    \"model_globals\": " << model.globals.size() << ",\n"
        << "    \"physical_code_targets\": " << model.physical_code.size() << ",\n"
        << "    \"emitted_functions\": " << emitted << ",\n"
        << "    \"skipped_functions\": " << skipped << "\n"
        << "  }\n}\n";

    result.text = out.str();
    return result;
}

NativeSdkSearchIndexOutput generate_native_sdk_search_index_tsv(
    const NativeModel& model)
{
    NativeSdkSearchIndexOutput result;
    std::ostringstream out;

    out << "# tmfdev search index schema 1\n"
        << "kind\tid\tname\towner\trva\tphysical_code_id\tabi_status\tsdk_status\tdetails\n";

    const auto emit_row = [&](const std::vector<std::string>& fields) {
        for (std::size_t index = 0; index < fields.size(); ++index) {
            if (index != 0)
                out << '\t';

            out << tsv_escape(fields[index]);
        }

        out << '\n';
    };

    std::vector<const NativeClass*> classes;
    classes.reserve(model.classes.size());
    for (const auto& native_class : model.classes)
        classes.push_back(&native_class);

    std::sort(
        classes.begin(),
        classes.end(),
        [](const NativeClass* left, const NativeClass* right) {
            return left->id < right->id;
        }
    );

    for (const auto* native_class : classes) {
        const auto vtable_state = native_class->vtable_state.known()
            ? native_vtable_state_name(*native_class->vtable_state.value)
            : "unknown";

        emit_row({
            "class",
            native_class->id,
            native_class->name,
            {},
            {},
            {},
            {},
            {},
            "direct_functions="
                + std::to_string(native_class->function_ids.size())
                + ";members="
                + std::to_string(native_class->member_ids.size())
                + ";reflection_descriptors="
                + std::to_string(
                    native_class->reflection_descriptor_ids.size()
                )
                + ";rtti="
                + std::to_string(native_class->rtti_hierarchy.size())
                + ";vtable="
                + vtable_state
                + ";mw_class_id="
                + (native_class->mw_class_id.known()
                    ? "0x" + hex8(*native_class->mw_class_id.value)
                    : "unknown"),
        });
    }

    std::vector<const NativeFunction*> functions;
    functions.reserve(model.functions.size());
    for (const auto& function : model.functions)
        functions.push_back(&function);

    std::sort(
        functions.begin(),
        functions.end(),
        [](const NativeFunction* left, const NativeFunction* right) {
            return left->id < right->id;
        }
    );

    for (const auto* function : functions) {
        const auto abi = assess_native_function_abi(model, *function);
        const auto sdk = assess_native_sdk_function(model, *function);
        const auto convention = function->calling_convention.known()
            ? calling_convention_name(*function->calling_convention.value)
            : "unknown";

        emit_row({
            "function",
            function->id,
            function->qualified_name,
            class_owner(function->qualified_name),
            function->rva.known()
                ? "0x" + hex8(*function->rva.value)
                : std::string{},
            function->physical_code_id.known()
                ? *function->physical_code_id.value
                : std::string{},
            native_abi_status_name(abi.status),
            sdk.emitted ? "emitted" : "skipped",
            "calling_convention="
                + std::string(convention)
                + ";parameters="
                + std::to_string(function->parameters.size())
                + search_index_function_signature(*function)
                + (sdk.emitted || sdk.reason.empty()
                    ? std::string{}
                    : ";skip_reason=" + sdk.reason),
        });
    }

    std::vector<const NativeType*> types;
    types.reserve(model.types.size());
    for (const auto& type : model.types)
        types.push_back(&type);

    std::sort(
        types.begin(),
        types.end(),
        [](const NativeType* left, const NativeType* right) {
            return left->id < right->id;
        }
    );

    for (const auto* type : types) {
        emit_row({
            "type",
            type->id,
            type->name,
            {},
            {},
            {},
            {},
            {},
            "kind="
                + std::string(type->kind.known()
                    ? native_type_kind_name(*type->kind.value)
                    : "unknown")
                + ";size="
                + (type->size.known()
                    ? std::to_string(*type->size.value)
                    : std::string{"unknown"})
                + ";fields="
                + std::to_string(type->fields.size()),
        });
    }

    std::vector<const NativeGlobal*> globals;
    globals.reserve(model.globals.size());
    for (const auto& global : model.globals)
        globals.push_back(&global);

    std::sort(
        globals.begin(),
        globals.end(),
        [](const NativeGlobal* left, const NativeGlobal* right) {
            return left->id < right->id;
        }
    );

    for (const auto* global : globals) {
        emit_row({
            "global",
            global->id,
            global->name,
            static_global_owner(global->name),
            global->rva.known()
                ? "0x" + hex8(*global->rva.value)
                : std::string{},
            {},
            {},
            {},
            "type="
                + (global->type.known()
                    ? *global->type.value
                    : std::string{"address-only / unknown"}),
        });
    }

    std::vector<const NativeReflectionDescriptor*> reflection_descriptors;
    reflection_descriptors.reserve(model.reflection_descriptors.size());
    for (const auto& descriptor : model.reflection_descriptors)
        reflection_descriptors.push_back(&descriptor);

    std::sort(
        reflection_descriptors.begin(),
        reflection_descriptors.end(),
        [](const NativeReflectionDescriptor* left,
           const NativeReflectionDescriptor* right) {
            return left->id < right->id;
        }
    );

    for (const auto* descriptor : reflection_descriptors) {
        const auto name = descriptor->name.known()
            ? *descriptor->name.value
            : (descriptor->type_name.known()
                ? *descriptor->type_name.value
                : std::string{"<unnamed>"});
        const auto category = descriptor->category.known()
            ? native_reflection_category_name(*descriptor->category.value)
            : "unknown";
        const auto type_name = descriptor->type_name.known()
            ? *descriptor->type_name.value
            : "unknown";
        const auto offset = descriptor->offset.known()
            ? std::to_string(*descriptor->offset.value)
            : "unknown";
        const auto specialized = descriptor->specialization.kind.known()
            ? native_reflection_specialized_kind_name(
                *descriptor->specialization.kind.value
            )
            : "unknown";

        emit_row({
            "reflection",
            descriptor->id,
            name,
            descriptor->owner_class_name,
            {},
            {},
            {},
            "metadata",
            "category=" + std::string(category)
                + ";type=" + type_name
                + ";type_code="
                + (descriptor->type_code.known()
                    ? std::to_string(*descriptor->type_code.value)
                    : std::string{"unknown"})
                + ";offset=" + offset
                + ";physical_storage="
                + (descriptor->physical_storage.known()
                    && *descriptor->physical_storage.value
                    ? "true"
                    : "false")
                + ";specialized=" + specialized
                + ";index=" + std::to_string(descriptor->index),
        });
    }

    result.text = out.str();
    return result;
}

} // namespace tmfdev
