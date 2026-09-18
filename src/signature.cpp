#include "signature.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace tmfdev {
namespace {

std::string trim(std::string value)
{
    const auto first =
        std::find_if_not(
            value.begin(),
            value.end(),
            [](unsigned char c) {
                return std::isspace(c) != 0;
            }
        );

    const auto last =
        std::find_if_not(
            value.rbegin(),
            value.rend(),
            [](unsigned char c) {
                return std::isspace(c) != 0;
            }
        ).base();

    if (first >= last)
        return {};

    return std::string(first, last);
}

bool consume_prefix(
    std::string& text,
    std::string_view prefix)
{
    if (!text.starts_with(prefix))
        return false;

    text.erase(0, prefix.size());
    text = trim(std::move(text));

    return true;
}

bool consume_suffix(
    std::string& text,
    std::string_view suffix)
{
    if (!text.ends_with(suffix))
        return false;

    text.resize(
        text.size() - suffix.size()
    );

    text = trim(std::move(text));

    return true;
}

bool consume_trailing_word(
    std::string& text,
    std::string_view word)
{
    if (!text.ends_with(word))
        return false;

    const auto start =
        text.size() - word.size();

    if (
        start > 0
        && (
            std::isalnum(
                static_cast<unsigned char>(
                    text[start - 1]
                )
            ) != 0
            || text[start - 1] == '_'
        )
    ) {
        return false;
    }

    text.resize(start);
    text = trim(std::move(text));

    return true;
}

bool has_top_level_pointer_or_reference(
    const std::string& text)
{
    int angle_depth = 0;

    for (const char c : text) {
        if (c == '<') {
            ++angle_depth;
            continue;
        }

        if (c == '>') {
            if (angle_depth > 0)
                --angle_depth;
            continue;
        }

        if (
            angle_depth == 0
            && (c == '*' || c == '&')
        ) {
            return true;
        }
    }

    return false;
}

bool is_function_pointer_declarator(
    const std::string& text)
{
    if (text.find("(*") != std::string::npos)
        return true;

    return text.find("(__") != std::string::npos
        && text.find('*') != std::string::npos;
}

bool is_member_pointer_declarator(
    const std::string& text)
{
    return text.find("::*") != std::string::npos;
}

bool is_array_reference_declarator(
    const std::string& text)
{
    const auto declarator = text.find("(&)");
    if (declarator == std::string::npos
        || declarator == 0) {
        return false;
    }

    const auto extent = text.substr(declarator + 3);
    if (extent.size() < 3
        || extent.front() != '['
        || extent.back() != ']') {
        return false;
    }

    return std::all_of(
        extent.begin() + 1,
        extent.end() - 1,
        [](unsigned char c) {
            return std::isdigit(c) != 0;
        }
    );
}

void strip_elaborated_prefix(
    std::string& text)
{
    constexpr std::string_view prefixes[] = {
        "class ",
        "struct ",
        "enum ",
        "union ",
    };

    for (const auto prefix : prefixes) {
        if (text.starts_with(prefix)) {
            text.erase(0, prefix.size());
            text = trim(std::move(text));
            return;
        }
    }
}

std::optional<std::pair<
    CallingConvention,
    std::string_view
>> find_calling_convention(
    const std::string& text)
{
    constexpr std::pair<
        CallingConvention,
        std::string_view
    > conventions[] = {
        {
            CallingConvention::thiscall_,
            "__thiscall"
        },
        {
            CallingConvention::cdecl_,
            "__cdecl"
        },
        {
            CallingConvention::stdcall_,
            "__stdcall"
        },
        {
            CallingConvention::fastcall_,
            "__fastcall"
        },
    };

    for (const auto& [kind, token] : conventions) {
        if (text.find(token) != std::string::npos) {
            return std::pair{
                kind,
                token
            };
        }
    }

    return std::nullopt;
}

std::size_t find_parameter_open(
    const std::string& text,
    std::size_t start)
{
    int angle_depth = 0;

    for (std::size_t i = start; i < text.size(); ++i) {
        switch (text[i]) {
        case '<':
            ++angle_depth;
            break;

        case '>':
            if (angle_depth > 0)
                --angle_depth;
            break;

        case '(':
            if (angle_depth == 0)
                return i;
            break;
        }
    }

    return std::string::npos;
}

std::size_t find_matching_close(
    const std::string& text,
    std::size_t open)
{
    int depth = 0;

    for (std::size_t i = open; i < text.size(); ++i) {
        if (text[i] == '(') {
            ++depth;
        }
        else if (text[i] == ')') {
            --depth;

            if (depth == 0)
                return i;
        }
    }

    return std::string::npos;
}

std::vector<std::string> split_parameters(
    const std::string& text)
{
    std::vector<std::string> result;

    const auto cleaned =
        trim(text);

    if (
        cleaned.empty()
        || cleaned == "void"
    ) {
        return result;
    }

    std::size_t start = 0;

    int angle_depth = 0;
    int paren_depth = 0;
    int bracket_depth = 0;

    for (std::size_t i = 0; i < cleaned.size(); ++i) {
        const char c = cleaned[i];

        switch (c) {
        case '<':
            ++angle_depth;
            break;

        case '>':
            if (angle_depth > 0)
                --angle_depth;
            break;

        case '(':
            ++paren_depth;
            break;

        case ')':
            if (paren_depth > 0)
                --paren_depth;
            break;

        case '[':
            ++bracket_depth;
            break;

        case ']':
            if (bracket_depth > 0)
                --bracket_depth;
            break;

        case ',':
            if (
                angle_depth == 0
                && paren_depth == 0
                && bracket_depth == 0
            ) {
                result.push_back(
                    trim(
                        cleaned.substr(
                            start,
                            i - start
                        )
                    )
                );

                start = i + 1;
            }

            break;
        }
    }

    result.push_back(
        trim(
            cleaned.substr(start)
        )
    );

    return result;
}

} // namespace

ParsedTypeUsage parse_type_usage(
    const std::string& declaration)
{
    ParsedTypeUsage result;

    std::string text =
        trim(declaration);

    if (text.empty()) {
        result.error =
            "empty type declaration";
        return result;
    }

    bool lvalue_reference = false;
    bool rvalue_reference = false;

    if (consume_suffix(text, "&&")) {
        rvalue_reference = true;
    }
    else if (consume_suffix(text, "&")) {
        lvalue_reference = true;
    }

    bool consumed_declarator = true;

    while (consumed_declarator) {
        consumed_declarator = false;

        if (consume_trailing_word(text, "const")) {
            result.is_const = true;
            consumed_declarator = true;
            continue;
        }

        if (consume_suffix(text, "*")) {
            ++result.pointer_depth;
            consumed_declarator = true;
        }
    }

    if (text.starts_with("const ")) {
        text.erase(0, 6);
        text = trim(std::move(text));
        result.is_const = true;
    }

    if (is_array_reference_declarator(text)) {
        const auto declarator = text.find("(&)");
        const auto array_type = trim(
            text.substr(0, declarator)
            + " "
            + text.substr(declarator + 3)
        );

        if (!array_type.empty()) {
            result.base_type = array_type;
            result.pass_kind = NativeTypePassKind::lvalue_reference;
            result.declarator_kind =
                NativeTypeDeclaratorKind::array_reference;
            result.parsed = true;
            return result;
        }
    }

    if (has_top_level_pointer_or_reference(text)) {
        if (is_member_pointer_declarator(text)
            || is_function_pointer_declarator(text)) {
            result.base_type = text;
            result.pass_kind = NativeTypePassKind::pointer;
            result.declarator_kind =
                is_member_pointer_declarator(text)
                    ? NativeTypeDeclaratorKind::member_pointer
                    : NativeTypeDeclaratorKind::function_pointer;
            result.parsed = true;
            return result;
        }

        result.error =
            "unsupported complex type declarator";
        return result;
    }

    strip_elaborated_prefix(text);

    if (text.empty()) {
        result.error =
            "base type is empty";
        return result;
    }

    result.base_type = text;

    if (rvalue_reference) {
        result.pass_kind =
            NativeTypePassKind::rvalue_reference;
    }
    else if (lvalue_reference) {
        result.pass_kind =
            NativeTypePassKind::lvalue_reference;
    }
    else if (result.pointer_depth > 0) {
        result.pass_kind =
            NativeTypePassKind::pointer;
    }
    else {
        result.pass_kind =
            NativeTypePassKind::value;
    }

    result.parsed = true;
    return result;
}

ParsedFunctionSignature parse_function_signature(
    const std::string& demangled)
{
    ParsedFunctionSignature result;

    std::string text =
        trim(demangled);

    if (text.empty()) {
        result.error =
            "empty demangled declaration";
        return result;
    }

    if (consume_prefix(text, "public:")) {
        result.access = AccessLevel::public_;
    }
    else if (consume_prefix(text, "protected:")) {
        result.access = AccessLevel::protected_;
    }
    else if (consume_prefix(text, "private:")) {
        result.access = AccessLevel::private_;
    }

    bool consumed_modifier = true;

    while (consumed_modifier) {
        consumed_modifier = false;

        if (consume_prefix(text, "virtual")) {
            result.is_virtual = true;
            consumed_modifier = true;
        }

        if (consume_prefix(text, "static")) {
            result.is_static = true;
            consumed_modifier = true;
        }
    }

    const auto convention =
        find_calling_convention(text);

    if (!convention.has_value()) {
        result.error =
            "calling convention not found";
        return result;
    }

    result.calling_convention =
        convention->first;

    const std::string token(
        convention->second
    );

    const auto convention_pos =
        text.find(token);

    if (convention_pos == std::string::npos) {
        result.error =
            "calling convention position not found";
        return result;
    }

    result.return_type =
        trim(
            text.substr(
                0,
                convention_pos
            )
        );

    const auto name_start =
        convention_pos
        + token.size();

    const auto parameter_open =
        find_parameter_open(
            text,
            name_start
        );

    if (parameter_open == std::string::npos) {
        result.error =
            "parameter list not found";
        return result;
    }

    result.qualified_name =
        trim(
            text.substr(
                name_start,
                parameter_open - name_start
            )
        );

    if (result.qualified_name.empty()) {
        result.error =
            "qualified function name is empty";
        return result;
    }

    // MSVC demangles constructors and destructors without a C++ return
    // declaration. The native x86 callable surface still needs an explicit
    // ABI return type for uniform downstream assessment; model these as the
    // void-returning constructor/destructor entry points they expose.
    if (result.return_type.empty()) {
        result.return_type = "void";
    }

    const auto parameter_close =
        find_matching_close(
            text,
            parameter_open
        );

    if (parameter_close == std::string::npos) {
        result.error =
            "unterminated parameter list";
        return result;
    }

    result.parameter_types =
        split_parameters(
            text.substr(
                parameter_open + 1,
                parameter_close
                    - parameter_open
                    - 1
            )
        );

    const auto suffix =
        trim(
            text.substr(
                parameter_close + 1
            )
        );

    if (suffix == "const") {
        result.is_const = true;
    }
    else if (!suffix.empty()) {
        result.error =
            "unsupported function suffix: "
            + suffix;
        return result;
    }

    result.parsed = true;
    return result;
}

const char* access_level_name(AccessLevel access)
{
    switch (access) {
    case AccessLevel::unknown:
        return "unknown";

    case AccessLevel::public_:
        return "public";

    case AccessLevel::protected_:
        return "protected";

    case AccessLevel::private_:
        return "private";
    }

    return "unknown";
}

} // namespace tmfdev
