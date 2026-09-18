#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tmfdev {

enum class EvidenceSource {
    executable,
    map,
    demangler,
    rtti,
    reflection,
    vtable,
    disassembly,
    external_research,
    runtime,
    manual,
};

enum class Confidence {
    unknown,
    low,
    medium,
    high,
    verified,
};

enum class VerificationKind {
    static_analysis,
    abi,
    runtime,
    behavior,
};

struct Evidence {
    EvidenceSource source = EvidenceSource::manual;
    Confidence confidence = Confidence::unknown;
    std::string detail;
};

struct Verification {
    VerificationKind kind = VerificationKind::static_analysis;
    std::string detail;
};

template <typename T>
struct Fact {
    std::optional<T> value;

    std::vector<Evidence> evidence;
    std::vector<Verification> verifications;

    bool known() const
    {
        return value.has_value();
    }
};

struct NativeBuild {
    Fact<std::uint64_t> file_size;
    Fact<std::string> sha256;

    Fact<std::uint16_t> machine;
    Fact<std::uint32_t> timestamp;
    Fact<std::uint32_t> image_base;
    Fact<std::uint32_t> image_size;
};

enum class CallingConvention {
    unknown,
    cdecl_,
    stdcall_,
    thiscall_,
    fastcall_,
};

enum class NativeAccess {
    unknown,
    public_,
    protected_,
    private_,
};

enum class NativeTypeKind {
    unknown,
    record,
    union_,
    enum_,
    primitive,
};

enum class NativeTypePassKind {
    unknown,
    value,
    pointer,
    lvalue_reference,
    rvalue_reference,
};

enum class NativeTypeDeclaratorKind {
    plain,
    function_pointer,
    member_pointer,
    array_reference,
};

enum class NativeReturnAbi {
    unknown,
    hidden_result_pointer,
};

struct NativeTypeUsage {
    Fact<std::string> base_type;
    Fact<NativeTypePassKind> pass_kind;
    Fact<NativeTypeDeclaratorKind> declarator_kind;
    Fact<std::size_t> pointer_depth;
    Fact<bool> is_const;
    Fact<std::string> parse_error;
};

struct NativeTypeField {
    std::size_t index = 0;

    Fact<std::string> name;
    Fact<std::string> type;

    Fact<std::uint32_t> offset;
    Fact<std::uint32_t> size;
};

struct NativeType {
    std::string id;
    std::string name;

    Fact<NativeTypeKind> kind;
    Fact<std::uint32_t> size;
    Fact<std::uint32_t> alignment;

    std::vector<NativeTypeField> fields;
};

struct NativeParameter {
    std::size_t index = 0;

    Fact<std::string> name;
    Fact<std::string> type;
    Fact<std::string> referenced_type_id;

    NativeTypeUsage usage;
};

struct NativeFunction {
    std::string id;
    std::string qualified_name;

    Fact<std::string> decorated_name;

    Fact<std::string> return_type;
    Fact<std::string> return_type_id;
    NativeTypeUsage return_usage;
    Fact<NativeReturnAbi> return_abi;

    Fact<CallingConvention> calling_convention;
    Fact<NativeAccess> access;

    Fact<bool> is_virtual;
    Fact<bool> is_static;
    Fact<bool> is_const;

    std::vector<NativeParameter> parameters;

    Fact<std::uint32_t> rva;
    Fact<std::uint32_t> virtual_address;

    Fact<std::size_t> virtual_slot;

    Fact<std::string> physical_code_id;

    Fact<std::string> signature_parse_error;
};

struct PhysicalCode {
    std::string id;

    Fact<std::uint32_t> rva;
    Fact<std::uint32_t> virtual_address;

    std::vector<std::string> logical_function_ids;
};

struct NativeMember {
    std::string id;
    std::string name;

    Fact<std::string> type;
    Fact<std::uint32_t> offset;
};

enum class NativeReflectionCategory {
    unknown,
    physical_member,
    action,
    procedure,
    virtual_parameter,
};

enum class NativeReflectionSpecializedKind {
    none,
    class_,
    enum_,
    array,
    action,
    procedure,
    range,
    vec2,
    vec3,
    vec4,
    unknown,
};

struct NativeReflectionEnumValue {
    std::size_t index = 0;

    Fact<std::uint32_t> name_virtual_address;
    Fact<std::string> name;
};

struct NativeReflectionComponent {
    Fact<std::uint32_t> name_virtual_address;
    Fact<std::string> name;
};

struct NativeReflectionProcedureArgument {
    std::size_t index = 0;

    Fact<std::uint32_t> class_id;
    Fact<std::uint32_t> name_virtual_address;
    Fact<std::string> name;
    Fact<std::uint32_t> flags;
};

struct NativeReflectionSpecialization {
    Fact<NativeReflectionSpecializedKind> kind;

    // Enum CppName and array TypeName are both names attached to the
    // specialized descriptor payload. They are kept separate from the
    // native C++ type model because reflection naming is not ABI proof.
    Fact<std::uint32_t> name_virtual_address;
    Fact<std::string> name;

    Fact<std::uint32_t> class_info_virtual_address;
    Fact<std::uint32_t> function_virtual_address;
    Fact<std::uint32_t> argument_count;

    Fact<std::uint32_t> value_count;
    Fact<std::uint32_t> value_names_virtual_address;

    Fact<std::uint32_t> argument_class_ids_virtual_address;
    Fact<std::uint32_t> argument_names_virtual_address;
    Fact<std::uint32_t> argument_flags_virtual_address;

    Fact<std::uint32_t> auxiliary0;
    Fact<std::uint32_t> auxiliary1;

    std::vector<NativeReflectionComponent> components;
    std::vector<NativeReflectionEnumValue> enum_values;
    std::vector<NativeReflectionProcedureArgument> procedure_arguments;
};

struct NativeReflectionDescriptor {
    std::string id;
    std::string owner_class_id;
    std::string owner_class_name;
    std::size_t index = 0;

    Fact<std::uint32_t> record_virtual_address;
    Fact<std::uint32_t> record_size;
    Fact<std::uint32_t> parameter_id;
    Fact<std::uint32_t> parameter_virtual_address;
    Fact<std::string> name;
    Fact<std::uint32_t> type_code;
    Fact<std::string> type_name;
    Fact<std::int32_t> offset;
    Fact<std::uint32_t> flags1;
    Fact<std::uint32_t> flags2;

    Fact<NativeReflectionCategory> category;
    Fact<bool> physical_storage;

    NativeReflectionSpecialization specialization;
};

struct NativeGlobal {
    std::string id;
    std::string name;

    Fact<std::string> decorated_name;
    Fact<std::string> type;
    Fact<std::uint32_t> rva;
    Fact<std::uint32_t> virtual_address;
};

struct NativeHierarchyEntry {
    std::string class_id;

    std::uint32_t contained_bases = 0;

    Fact<std::int32_t> member_displacement;
    Fact<std::int32_t> vbtable_displacement;
    Fact<std::int32_t> displacement_inside_vbtable;
};

struct NativeVtableSlot {
    std::size_t index = 0;

    Fact<std::uint32_t> target_rva;
    Fact<std::string> physical_code_id;

    std::vector<std::string> candidate_function_ids;
    Fact<std::string> resolved_function_id;
};

struct NativeVtable {
    std::string id;

    Fact<std::uint32_t> rva;
    Fact<std::uint32_t> virtual_address;

    std::vector<NativeVtableSlot> slots;
};

enum class NativeVtableState {
    absent,
    valid,
    malformed,
};

struct NativeClass {
    std::string id;
    std::string name;

    // GameBox's numeric class identity is distinct from tmfdev's synthetic
    // class ID and is only populated when the exact executable exposes a
    // directly verifiable GetMwClassId implementation.
    Fact<std::uint32_t> mw_class_id;

    std::vector<NativeHierarchyEntry> rtti_hierarchy;

    std::vector<std::string> member_ids;
    std::vector<std::string> reflection_descriptor_ids;
    std::vector<std::string> function_ids;

    Fact<NativeVtableState> vtable_state;
    Fact<std::string> vtable_id;
    Fact<std::string> vtable_error;
};

struct SemanticRule {
    std::string id;
    std::string subject_id;

    std::string description;

    std::vector<Evidence> evidence;
    std::vector<Verification> verifications;
};

struct NativeModel {
    NativeBuild build;

    std::vector<NativeType> types;
    std::vector<NativeClass> classes;
    std::vector<NativeMember> members;
    std::vector<NativeReflectionDescriptor> reflection_descriptors;
    std::vector<NativeGlobal> globals;
    std::vector<NativeFunction> functions;
    std::vector<PhysicalCode> physical_code;
    std::vector<NativeVtable> vtables;
    std::vector<SemanticRule> semantic_rules;
};

const char* evidence_source_name(EvidenceSource source);
const char* confidence_name(Confidence confidence);
const char* verification_kind_name(VerificationKind kind);
const char* calling_convention_name(CallingConvention convention);
const char* native_access_name(NativeAccess access);
const char* native_type_kind_name(NativeTypeKind kind);
const char* native_type_pass_kind_name(NativeTypePassKind kind);
const char* native_type_declarator_kind_name(
    NativeTypeDeclaratorKind kind
);
const char* native_reflection_category_name(
    NativeReflectionCategory category
);
const char* native_reflection_specialized_kind_name(
    NativeReflectionSpecializedKind kind
);
const char* native_vtable_state_name(NativeVtableState state);
const char* native_return_abi_name(NativeReturnAbi kind);

} // namespace tmfdev
