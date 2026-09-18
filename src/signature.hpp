#pragma once

#include "model.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace tmfdev {

enum class AccessLevel {
    unknown,
    public_,
    protected_,
    private_,
};

struct ParsedTypeUsage {
    bool parsed = false;

    std::string base_type;

    NativeTypePassKind pass_kind =
        NativeTypePassKind::unknown;

    NativeTypeDeclaratorKind declarator_kind =
        NativeTypeDeclaratorKind::plain;

    std::size_t pointer_depth = 0;
    bool is_const = false;

    std::string error;
};

struct ParsedFunctionSignature {
    bool parsed = false;

    AccessLevel access = AccessLevel::unknown;

    bool is_virtual = false;
    bool is_static = false;
    bool is_const = false;

    CallingConvention calling_convention =
        CallingConvention::unknown;

    std::string return_type;
    std::string qualified_name;

    std::vector<std::string> parameter_types;

    std::string error;
};

ParsedTypeUsage parse_type_usage(
    const std::string& declaration
);

ParsedFunctionSignature parse_function_signature(
    const std::string& demangled
);

const char* access_level_name(AccessLevel access);

} // namespace tmfdev
