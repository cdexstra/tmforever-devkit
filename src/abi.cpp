#include "abi.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string_view>

namespace tmfdev {
namespace {

constexpr std::uint16_t image_file_machine_i386 = 0x014C;

void add_issue(
    NativeAbiAssessment& assessment,
    NativeAbiIssueLevel level,
    std::string subject,
    std::string detail)
{
    assessment.issues.push_back({
        level,
        std::move(subject),
        std::move(detail),
    });

    if (level == NativeAbiIssueLevel::blocker) {
        assessment.status = NativeAbiStatus::blocked;
    }
    else if (assessment.status == NativeAbiStatus::ready) {
        assessment.status = NativeAbiStatus::conditional;
    }
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

bool is_record_declaration(const std::string& declaration)
{
    return starts_with_word(declaration, "class")
        || starts_with_word(declaration, "struct")
        || starts_with_word(declaration, "union");
}

bool is_enum_declaration(const std::string& declaration)
{
    return starts_with_word(declaration, "enum");
}

bool is_builtin_value_type(const std::string& type)
{
    constexpr std::string_view builtins[] = {
        "void",
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
        "__int64",
        "signed __int64",
        "unsigned __int64",
        "float",
        "double",
        "long double",
        "...",
    };

    return std::find(
        std::begin(builtins),
        std::end(builtins),
        type
    ) != std::end(builtins);
}

const NativeType* find_type(
    const NativeModel& model,
    const std::string& id)
{
    const auto found =
        std::find_if(
            model.types.begin(),
            model.types.end(),
            [&](const NativeType& type) {
                return type.id == id;
            }
        );

    if (found == model.types.end())
        return nullptr;

    return &*found;
}

bool has_abi_verification(
    const Fact<NativeReturnAbi>& fact)
{
    if (!fact.known())
        return false;

    return std::any_of(
        fact.verifications.begin(),
        fact.verifications.end(),
        [](const Verification& verification) {
            return verification.kind
                == VerificationKind::abi;
        }
    );
}

bool has_abi_verification(const NativeType& type)
{
    if (!type.size.known())
        return false;

    return std::any_of(
        type.size.verifications.begin(),
        type.size.verifications.end(),
        [](const Verification& verification) {
            return verification.kind
                == VerificationKind::abi;
        }
    );
}


NativeTypeKind inferred_type_kind(const std::string& declaration)
{
    if (is_enum_declaration(declaration))
        return NativeTypeKind::enum_;

    if (starts_with_word(declaration, "union"))
        return NativeTypeKind::union_;

    if (is_record_declaration(declaration))
        return NativeTypeKind::record;

    return NativeTypeKind::unknown;
}

bool type_usage_needs_more_evidence(
    const NativeModel& model,
    const Fact<std::string>& declaration,
    const NativeTypeUsage& usage,
    const Fact<std::string>& referenced_type_id)
{
    if (!declaration.known()
        || !usage.base_type.known()
        || !usage.pass_kind.known()
        || *usage.pass_kind.value != NativeTypePassKind::value
        || is_builtin_value_type(*usage.base_type.value)) {
        return false;
    }

    if (!referenced_type_id.known())
        return true;

    const auto* type =
        find_type(model, *referenced_type_id.value);

    if (type == nullptr || !type->size.known())
        return true;

    return !has_abi_verification(*type);
}

void assess_value_type(
    NativeAbiAssessment& assessment,
    const NativeModel& model,
    const std::string& subject,
    const std::string& declaration,
    const NativeTypeUsage& usage,
    const Fact<std::string>& referenced_type_id,
    bool is_return,
    const Fact<NativeReturnAbi>* return_abi)
{
    if (!usage.base_type.known()) {
        add_issue(
            assessment,
            NativeAbiIssueLevel::blocker,
            subject,
            "base type is unknown"
        );
        return;
    }

    const auto& base_type = *usage.base_type.value;

    if (is_builtin_value_type(base_type))
        return;

    if (is_enum_declaration(declaration)) {
        if (!referenced_type_id.known()) {
            add_issue(
                assessment,
                NativeAbiIssueLevel::conditional,
                subject,
                "enum is passed by value but its underlying ABI representation is not modeled"
            );
            return;
        }

        const auto* type =
            find_type(
                model,
                *referenced_type_id.value
            );

        if (type == nullptr || !type->size.known()) {
            add_issue(
                assessment,
                NativeAbiIssueLevel::conditional,
                subject,
                "enum is passed by value but its canonical ABI size is unavailable"
            );
            return;
        }

        if (!has_abi_verification(*type)) {
            add_issue(
                assessment,
                NativeAbiIssueLevel::conditional,
                subject,
                "enum ABI size has no independent verification"
            );
        }

        return;
    }

    if (is_record_declaration(declaration)) {
        if (!referenced_type_id.known()) {
            add_issue(
                assessment,
                NativeAbiIssueLevel::blocker,
                subject,
                "record is passed by value but no canonical layout is linked"
            );
            return;
        }

        const auto* type =
            find_type(
                model,
                *referenced_type_id.value
            );

        if (type == nullptr) {
            add_issue(
                assessment,
                NativeAbiIssueLevel::blocker,
                subject,
                "canonical type link does not resolve to a model type"
            );
            return;
        }

        if (!type->size.known()) {
            add_issue(
                assessment,
                NativeAbiIssueLevel::blocker,
                subject,
                "record is passed by value but its size is unknown"
            );
            return;
        }

        if (!has_abi_verification(*type)) {
            add_issue(
                assessment,
                NativeAbiIssueLevel::conditional,
                subject,
                "record layout has no independent ABI verification"
            );
        }

        if (is_return) {
            const bool verified_hidden_result =
                return_abi != nullptr
                && return_abi->known()
                && *return_abi->value
                    == NativeReturnAbi::hidden_result_pointer
                && has_abi_verification(*return_abi);

            if (!verified_hidden_result) {
                add_issue(
                    assessment,
                    NativeAbiIssueLevel::conditional,
                    subject,
                    "by-value record return may require a hidden return convention that is not modeled"
                );
            }
        }

        return;
    }

    if (referenced_type_id.known()) {
        const auto* type =
            find_type(
                model,
                *referenced_type_id.value
            );

        if (type == nullptr || !type->size.known()) {
            add_issue(
                assessment,
                NativeAbiIssueLevel::blocker,
                subject,
                "linked value type does not have a usable canonical size"
            );
            return;
        }

        if (!has_abi_verification(*type)) {
            add_issue(
                assessment,
                NativeAbiIssueLevel::conditional,
                subject,
                "linked value type has no independent ABI verification"
            );
        }

        return;
    }

    add_issue(
        assessment,
        NativeAbiIssueLevel::conditional,
        subject,
        "unclassified non-primitive value type"
    );
}

void assess_type_usage(
    NativeAbiAssessment& assessment,
    const NativeModel& model,
    const std::string& subject,
    const Fact<std::string>& declaration,
    const NativeTypeUsage& usage,
    const Fact<std::string>& referenced_type_id,
    bool is_return,
    const Fact<NativeReturnAbi>* return_abi)
{
    if (!declaration.known()) {
        add_issue(
            assessment,
            NativeAbiIssueLevel::blocker,
            subject,
            "type declaration is unknown"
        );
        return;
    }

    if (usage.parse_error.known()) {
        add_issue(
            assessment,
            NativeAbiIssueLevel::blocker,
            subject,
            "type usage could not be parsed: "
                + *usage.parse_error.value
        );
        return;
    }

    if (!usage.pass_kind.known()) {
        add_issue(
            assessment,
            NativeAbiIssueLevel::blocker,
            subject,
            "type pass mode is unknown"
        );
        return;
    }

    if (usage.declarator_kind.known()
        && *usage.declarator_kind.value
            == NativeTypeDeclaratorKind::member_pointer) {
        add_issue(
            assessment,
            NativeAbiIssueLevel::blocker,
            subject,
            "member-pointer declarator ABI representation is not modeled"
        );
        return;
    }

    switch (*usage.pass_kind.value) {
    case NativeTypePassKind::pointer:
    case NativeTypePassKind::lvalue_reference:
    case NativeTypePassKind::rvalue_reference:
        return;

    case NativeTypePassKind::value:
        assess_value_type(
            assessment,
            model,
            subject,
            *declaration.value,
            usage,
            referenced_type_id,
            is_return,
            return_abi
        );
        return;

    case NativeTypePassKind::unknown:
        add_issue(
            assessment,
            NativeAbiIssueLevel::blocker,
            subject,
            "type pass mode is unknown"
        );
        return;
    }
}

} // namespace

NativeAbiAssessment assess_native_function_abi(
    const NativeModel& model,
    const NativeFunction& function)
{
    NativeAbiAssessment assessment;

    if (!model.build.machine.known()) {
        add_issue(
            assessment,
            NativeAbiIssueLevel::conditional,
            "build",
            "PE machine is unknown"
        );
    }
    else if (*model.build.machine.value
             != image_file_machine_i386) {
        add_issue(
            assessment,
            NativeAbiIssueLevel::blocker,
            "build",
            "ABI assessment currently supports only 32-bit x86 TMForever builds"
        );
    }

    if (function.signature_parse_error.known()) {
        add_issue(
            assessment,
            NativeAbiIssueLevel::blocker,
            "signature",
            "function signature could not be parsed: "
                + *function.signature_parse_error.value
        );
        return assessment;
    }

    if (!function.calling_convention.known()
        || *function.calling_convention.value
            == CallingConvention::unknown) {
        add_issue(
            assessment,
            NativeAbiIssueLevel::blocker,
            "calling convention",
            "calling convention is unknown"
        );
    }

    assess_type_usage(
        assessment,
        model,
        "return type",
        function.return_type,
        function.return_usage,
        function.return_type_id,
        true,
        &function.return_abi
    );

    for (const auto& parameter : function.parameters) {
        assess_type_usage(
            assessment,
            model,
            "parameter " + std::to_string(parameter.index),
            parameter.type,
            parameter.usage,
            parameter.referenced_type_id,
            false,
            nullptr
        );
    }

    return assessment;
}

NativeAbiReport build_native_abi_report(
    const NativeModel& model)
{
    NativeAbiReport report;

    struct IssueAccumulator {
        NativeAbiIssueLevel level =
            NativeAbiIssueLevel::conditional;
        std::size_t occurrences = 0;
        std::set<std::string> functions;
    };

    struct TypeAccumulator {
        NativeTypeKind kind = NativeTypeKind::unknown;
        std::size_t occurrences = 0;
        std::set<std::string> functions;
    };

    std::map<std::pair<int, std::string>, IssueAccumulator> issues;
    std::map<std::string, TypeAccumulator> type_needs;

    const auto add_type_need = [&](
        const NativeFunction& function,
        const Fact<std::string>& declaration,
        const NativeTypeUsage& usage,
        const Fact<std::string>& referenced_type_id)
    {
        if (!type_usage_needs_more_evidence(
                model,
                declaration,
                usage,
                referenced_type_id)) {
            return;
        }

        const auto& name = *usage.base_type.value;
        auto& need = type_needs[name];
        ++need.occurrences;
        need.functions.insert(function.id);

        if (need.kind == NativeTypeKind::unknown) {
            need.kind = inferred_type_kind(
                *declaration.value
            );
        }
    };

    for (const auto& function : model.functions) {
        ++report.functions;

        const auto assessment =
            assess_native_function_abi(
                model,
                function
            );

        switch (assessment.status) {
        case NativeAbiStatus::ready:
            ++report.ready;
            break;
        case NativeAbiStatus::conditional:
            ++report.conditional;
            break;
        case NativeAbiStatus::blocked:
            ++report.blocked;
            break;
        }

        for (const auto& issue : assessment.issues) {
            const auto key = std::pair{
                static_cast<int>(issue.level),
                issue.detail
            };

            auto& summary = issues[key];
            summary.level = issue.level;
            ++summary.occurrences;
            summary.functions.insert(function.id);
        }

        add_type_need(
            function,
            function.return_type,
            function.return_usage,
            function.return_type_id
        );

        for (const auto& parameter : function.parameters) {
            add_type_need(
                function,
                parameter.type,
                parameter.usage,
                parameter.referenced_type_id
            );
        }
    }

    for (const auto& [key, value] : issues) {
        report.issue_summaries.push_back({
            value.level,
            key.second,
            value.functions.size(),
            value.occurrences,
        });
    }

    std::sort(
        report.issue_summaries.begin(),
        report.issue_summaries.end(),
        [](const NativeAbiIssueSummary& left,
           const NativeAbiIssueSummary& right) {
            if (left.affected_functions
                != right.affected_functions) {
                return left.affected_functions
                    > right.affected_functions;
            }

            if (left.occurrences != right.occurrences)
                return left.occurrences > right.occurrences;

            return left.detail < right.detail;
        }
    );

    for (const auto& [name, value] : type_needs) {
        report.type_needs.push_back({
            name,
            value.kind,
            value.functions.size(),
            value.occurrences,
        });
    }

    std::sort(
        report.type_needs.begin(),
        report.type_needs.end(),
        [](const NativeAbiTypeNeed& left,
           const NativeAbiTypeNeed& right) {
            if (left.affected_functions
                != right.affected_functions) {
                return left.affected_functions
                    > right.affected_functions;
            }

            if (left.occurrences != right.occurrences)
                return left.occurrences > right.occurrences;

            return left.type_name < right.type_name;
        }
    );

    return report;
}

const char* native_abi_status_name(NativeAbiStatus status)
{
    switch (status) {
    case NativeAbiStatus::ready:
        return "ready";
    case NativeAbiStatus::conditional:
        return "conditional";
    case NativeAbiStatus::blocked:
        return "blocked";
    }

    return "blocked";
}

const char* native_abi_issue_level_name(NativeAbiIssueLevel level)
{
    switch (level) {
    case NativeAbiIssueLevel::conditional:
        return "conditional";
    case NativeAbiIssueLevel::blocker:
        return "blocker";
    }

    return "blocker";
}

} // namespace tmfdev
