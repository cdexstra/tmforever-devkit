#include "model.hpp"

namespace tmfdev {

const char* evidence_source_name(EvidenceSource source)
{
    switch (source) {
    case EvidenceSource::executable:
        return "executable";
    case EvidenceSource::map:
        return "map";
    case EvidenceSource::demangler:
        return "demangler";
    case EvidenceSource::rtti:
        return "rtti";
    case EvidenceSource::reflection:
        return "reflection";
    case EvidenceSource::vtable:
        return "vtable";
    case EvidenceSource::disassembly:
        return "disassembly";
    case EvidenceSource::external_research:
        return "external-research";
    case EvidenceSource::runtime:
        return "runtime";
    case EvidenceSource::manual:
        return "manual";
    }

    return "unknown";
}

const char* confidence_name(Confidence confidence)
{
    switch (confidence) {
    case Confidence::unknown:
        return "unknown";
    case Confidence::low:
        return "low";
    case Confidence::medium:
        return "medium";
    case Confidence::high:
        return "high";
    case Confidence::verified:
        return "verified";
    }

    return "unknown";
}

const char* verification_kind_name(VerificationKind kind)
{
    switch (kind) {
    case VerificationKind::static_analysis:
        return "static-analysis";
    case VerificationKind::abi:
        return "abi";
    case VerificationKind::runtime:
        return "runtime";
    case VerificationKind::behavior:
        return "behavior";
    }

    return "unknown";
}

const char* calling_convention_name(CallingConvention convention)
{
    switch (convention) {
    case CallingConvention::unknown:
        return "unknown";
    case CallingConvention::cdecl_:
        return "cdecl";
    case CallingConvention::stdcall_:
        return "stdcall";
    case CallingConvention::thiscall_:
        return "thiscall";
    case CallingConvention::fastcall_:
        return "fastcall";
    }

    return "unknown";
}

const char* native_access_name(NativeAccess access)
{
    switch (access) {
    case NativeAccess::unknown:
        return "unknown";
    case NativeAccess::public_:
        return "public";
    case NativeAccess::protected_:
        return "protected";
    case NativeAccess::private_:
        return "private";
    }

    return "unknown";
}

const char* native_type_kind_name(NativeTypeKind kind)
{
    switch (kind) {
    case NativeTypeKind::unknown:
        return "unknown";
    case NativeTypeKind::record:
        return "record";
    case NativeTypeKind::union_:
        return "union";
    case NativeTypeKind::enum_:
        return "enum";
    case NativeTypeKind::primitive:
        return "primitive";
    }

    return "unknown";
}

const char* native_type_pass_kind_name(NativeTypePassKind kind)
{
    switch (kind) {
    case NativeTypePassKind::unknown:
        return "unknown";
    case NativeTypePassKind::value:
        return "value";
    case NativeTypePassKind::pointer:
        return "pointer";
    case NativeTypePassKind::lvalue_reference:
        return "lvalue-reference";
    case NativeTypePassKind::rvalue_reference:
        return "rvalue-reference";
    }

    return "unknown";
}

const char* native_type_declarator_kind_name(
    NativeTypeDeclaratorKind kind)
{
    switch (kind) {
    case NativeTypeDeclaratorKind::plain:
        return "plain";
    case NativeTypeDeclaratorKind::function_pointer:
        return "function-pointer";
    case NativeTypeDeclaratorKind::member_pointer:
        return "member-pointer";
    case NativeTypeDeclaratorKind::array_reference:
        return "array-reference";
    }

    return "unknown";
}

const char* native_reflection_category_name(
    NativeReflectionCategory category)
{
    switch (category) {
    case NativeReflectionCategory::unknown:
        return "unknown";
    case NativeReflectionCategory::physical_member:
        return "physical-member";
    case NativeReflectionCategory::action:
        return "action";
    case NativeReflectionCategory::procedure:
        return "procedure";
    case NativeReflectionCategory::virtual_parameter:
        return "virtual-parameter";
    }

    return "unknown";
}

const char* native_reflection_specialized_kind_name(
    NativeReflectionSpecializedKind kind)
{
    switch (kind) {
    case NativeReflectionSpecializedKind::none:
        return "none";
    case NativeReflectionSpecializedKind::class_:
        return "class";
    case NativeReflectionSpecializedKind::enum_:
        return "enum";
    case NativeReflectionSpecializedKind::array:
        return "array";
    case NativeReflectionSpecializedKind::action:
        return "action";
    case NativeReflectionSpecializedKind::procedure:
        return "procedure";
    case NativeReflectionSpecializedKind::range:
        return "range";
    case NativeReflectionSpecializedKind::vec2:
        return "vec2";
    case NativeReflectionSpecializedKind::vec3:
        return "vec3";
    case NativeReflectionSpecializedKind::vec4:
        return "vec4";
    case NativeReflectionSpecializedKind::unknown:
        return "unknown";
    }

    return "unknown";
}

const char* native_return_abi_name(NativeReturnAbi kind)
{
    switch (kind) {
    case NativeReturnAbi::unknown:
        return "unknown";
    case NativeReturnAbi::hidden_result_pointer:
        return "hidden-result-pointer";
    }

    return "unknown";
}

const char* native_vtable_state_name(NativeVtableState state)
{
    switch (state) {
    case NativeVtableState::absent:
        return "absent";
    case NativeVtableState::valid:
        return "valid";
    case NativeVtableState::malformed:
        return "malformed";
    }

    return "unknown";
}

} // namespace tmfdev
