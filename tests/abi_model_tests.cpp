#include "abi.hpp"
#include "sdk.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

namespace {

template <typename T>
tmfdev::Fact<T> known(T value)
{
    tmfdev::Fact<T> fact;
    fact.value = std::move(value);
    return fact;
}

tmfdev::NativeModel base_model()
{
    tmfdev::NativeModel model;
    model.build.machine = known<std::uint16_t>(0x014Cu);
    return model;
}

tmfdev::NativeFunction base_function(const std::string& name)
{
    tmfdev::NativeFunction function;
    function.id = "function:" + name;
    function.qualified_name = name;
    function.return_type = known(std::string{"void"});
    function.return_usage.base_type = known(std::string{"void"});
    function.return_usage.pass_kind = known(tmfdev::NativeTypePassKind::value);
    function.calling_convention = known(tmfdev::CallingConvention::cdecl_);
    function.rva = known<std::uint32_t>(0x1000u);
    return function;
}

tmfdev::NativeParameter integer_parameter(std::size_t index)
{
    tmfdev::NativeParameter parameter;
    parameter.index = index;
    parameter.type = known(std::string{"int"});
    parameter.usage.base_type = known(std::string{"int"});
    parameter.usage.pass_kind =
        known(tmfdev::NativeTypePassKind::value);
    return parameter;
}

tmfdev::NativeType verified_four_byte_record()
{
    tmfdev::NativeType type;
    type.id = "type:VerifiedRecord";
    type.name = "struct VerifiedRecord";
    type.kind = known(tmfdev::NativeTypeKind::record);
    type.size = known<std::uint32_t>(4u);
    type.size.verifications.push_back({
        tmfdev::VerificationKind::abi,
        "synthetic ABI conformance fixture",
    });
    return type;
}

void require(bool condition, const char* message)
{
    if (condition)
        return;

    std::cerr << "abi_model_tests: " << message << '\n';
    std::exit(1);
}

void test_logical_aliases_are_counted_separately()
{
    auto model = base_model();

    auto first = base_function("AliasOwner::First");
    auto second = base_function("AliasOwner::Second");

    first.physical_code_id = known(std::string{"code:00001000"});
    second.physical_code_id = known(std::string{"code:00001000"});

    model.functions.push_back(first);
    model.functions.push_back(second);

    tmfdev::PhysicalCode physical;
    physical.id = "code:00001000";
    physical.rva = known<std::uint32_t>(0x1000u);
    physical.logical_function_ids = {
        first.id,
        second.id,
    };
    model.physical_code.push_back(std::move(physical));

    const auto report = tmfdev::build_native_abi_report(model);
    require(report.functions == 2, "folded logical functions were collapsed");
    require(report.ready == 2, "folded logical functions lost ABI readiness");
    require(model.physical_code.size() == 1, "physical alias target was duplicated");
    require(model.physical_code.front().logical_function_ids.size() == 2,
            "physical target lost one logical alias");
}

void test_member_pointer_is_blocked()
{
    auto model = base_model();
    auto function = base_function("CallbackOwner::SetCallback");

    tmfdev::NativeParameter parameter;
    parameter.index = 0;
    parameter.type = known(
        std::string{"void (__thiscall CMwNod::*)(void)"}
    );
    parameter.usage.base_type = known(
        std::string{"void (__thiscall CMwNod::*)(void)"}
    );
    parameter.usage.pass_kind = known(tmfdev::NativeTypePassKind::value);
    parameter.usage.declarator_kind = known(
        tmfdev::NativeTypeDeclaratorKind::member_pointer
    );
    function.parameters.push_back(std::move(parameter));

    const auto assessment =
        tmfdev::assess_native_function_abi(model, function);
    require(
        assessment.status == tmfdev::NativeAbiStatus::blocked,
        "member-pointer ABI was treated as callable"
    );
}

void test_enum_requires_independent_width_evidence()
{
    auto model = base_model();
    auto function = base_function("EnumOwner::SetMode");

    tmfdev::NativeParameter parameter;
    parameter.index = 0;
    parameter.type = known(std::string{"enum EnumOwner::Mode"});
    parameter.usage.base_type = known(std::string{"EnumOwner::Mode"});
    parameter.usage.pass_kind = known(tmfdev::NativeTypePassKind::value);
    function.parameters.push_back(parameter);

    auto assessment = tmfdev::assess_native_function_abi(model, function);
    require(
        assessment.status == tmfdev::NativeAbiStatus::conditional,
        "unverified enum width was treated as ready"
    );

    auto enum_type = tmfdev::NativeType{};
    enum_type.id = "type:EnumOwner::Mode";
    enum_type.name = "EnumOwner::Mode";
    enum_type.kind = known(tmfdev::NativeTypeKind::enum_);
    enum_type.size = known<std::uint32_t>(4u);
    enum_type.size.verifications.push_back({
        tmfdev::VerificationKind::abi,
        "synthetic enum-width fixture",
    });
    model.types.push_back(std::move(enum_type));
    function.parameters.front().referenced_type_id =
        known(std::string{"type:EnumOwner::Mode"});

    assessment = tmfdev::assess_native_function_abi(model, function);
    require(
        assessment.status == tmfdev::NativeAbiStatus::ready,
        "independently verified enum width remained conditional"
    );
}

void test_record_layout_and_hidden_return_are_separate_facts()
{
    auto model = base_model();
    model.types.push_back(verified_four_byte_record());

    auto function = base_function("RecordOwner::Make");
    function.return_type = known(std::string{"struct VerifiedRecord"});
    function.return_usage.base_type = known(std::string{"VerifiedRecord"});
    function.return_usage.pass_kind = known(tmfdev::NativeTypePassKind::value);
    function.return_type_id = known(std::string{"type:VerifiedRecord"});

    auto assessment = tmfdev::assess_native_function_abi(model, function);
    require(
        assessment.status == tmfdev::NativeAbiStatus::conditional,
        "record layout evidence incorrectly implied hidden-return ABI"
    );

    function.return_abi = known(tmfdev::NativeReturnAbi::hidden_result_pointer);
    function.return_abi.verifications.push_back({
        tmfdev::VerificationKind::abi,
        "synthetic hidden-return fixture",
    });

    assessment = tmfdev::assess_native_function_abi(model, function);
    require(
        assessment.status == tmfdev::NativeAbiStatus::ready,
        "verified record layout and hidden return did not compose"
    );
}

void test_class_view_preserves_ambiguous_logical_aliases()
{
    auto model = base_model();

    auto first = base_function("CollisionOwner::DoThing");
    first.calling_convention =
        known(tmfdev::CallingConvention::thiscall_);
    first.rva = known<std::uint32_t>(0x1000u);
    first.physical_code_id = known(std::string{"code:00001000"});

    auto second = first;
    second.id = "function:CollisionOwner::DoThing:second";
    second.rva = known<std::uint32_t>(0x2000u);
    second.physical_code_id = known(std::string{"code:00002000"});

    auto distinct = base_function("CollisionOwner::DoThing");
    distinct.id = "function:CollisionOwner::DoThing:int";
    distinct.calling_convention =
        known(tmfdev::CallingConvention::thiscall_);
    distinct.rva = known<std::uint32_t>(0x3000u);
    distinct.parameters.push_back(integer_parameter(0));
    distinct.physical_code_id = known(std::string{"code:00003000"});

    auto folded = base_function("CollisionOwner::AlsoDoThing");
    folded.calling_convention =
        known(tmfdev::CallingConvention::thiscall_);
    folded.rva = known<std::uint32_t>(0x1000u);
    folded.physical_code_id = known(std::string{"code:00001000"});

    model.functions = {first, second, distinct, folded};

    const auto output =
        tmfdev::generate_native_sdk_header(model, "CollisionOwner");

    require(output.emitted_functions == 4,
            "ambiguous class-view fixture unexpectedly skipped a resolver");
    require(output.skipped_functions == 0,
            "ambiguous class-view fixture unexpectedly became unrepresentable");
    require(output.text.find("CollisionOwner_DoThing_00001000")
                != std::string::npos,
            "first logical alias lost its RVA-specific resolver");
    require(output.text.find("CollisionOwner_DoThing_00002000")
                != std::string::npos,
            "second logical alias lost its RVA-specific resolver");
    require(output.text.find("CollisionOwner_DoThing_00003000")
                != std::string::npos,
            "distinct overload lost its RVA-specific resolver");
    require(output.text.find("CollisionOwner_AlsoDoThing_00001000")
                != std::string::npos,
            "folded logical function lost its RVA-specific resolver");
    require(output.text.find("struct CollisionOwnerView")
                != std::string::npos,
            "class view was not generated for the unique overload");
    const auto view_method = output.text.find(
        "    void DoThing(int arg0)"
    );
    require(view_method != std::string::npos,
            "unique overload was not exposed through the class view");

    require(output.text.find("    void AlsoDoThing()")
                != std::string::npos,
            "distinct logical alias sharing a physical target lost its view method");

    const auto ambiguous_method = output.text.find(
        "    void DoThing()"
    );
    require(ambiguous_method == std::string::npos,
            "ambiguous aliased method was emitted through the class view");
}

void test_class_generation_preserves_direct_owner_boundary()
{
    auto model = base_model();

    tmfdev::NativeClass base;
    base.id = "class:BaseOwner";
    base.name = "BaseOwner";

    tmfdev::NativeClass derived;
    derived.id = "class:DerivedOwner";
    derived.name = "DerivedOwner";
    derived.rtti_hierarchy.push_back({
        base.id,
        1,
    });

    auto base_function = ::base_function("BaseOwner::Tick");
    base_function.rva = known<std::uint32_t>(0x4000u);
    base.function_ids.push_back(base_function.id);

    auto derived_function = ::base_function("DerivedOwner::Tick");
    derived_function.calling_convention =
        known(tmfdev::CallingConvention::thiscall_);
    derived_function.rva = known<std::uint32_t>(0x5000u);
    derived.function_ids.push_back(derived_function.id);

    model.classes = {base, derived};
    model.functions = {base_function, derived_function};

    const auto output =
        tmfdev::generate_native_sdk_header(model, "DerivedOwner");

    require(output.emitted_functions == 1,
            "derived class package inherited a base-owned resolver");
    require(output.skipped_functions == 0,
            "direct-owner fixture unexpectedly skipped its derived function");
    require(output.text.find("DerivedOwner_Tick_00005000")
                != std::string::npos,
            "derived direct-owned resolver was not emitted");
    require(output.text.find("BaseOwner_Tick_00004000")
                == std::string::npos,
            "base-owned resolver leaked into the derived class package");
    require(output.text.find("struct DerivedOwnerView")
                != std::string::npos,
            "derived direct-owned view was not emitted");
    require(output.text.find("struct BaseOwnerView")
                == std::string::npos,
            "base view leaked into the derived class package");
}

}

int main()
{
    test_logical_aliases_are_counted_separately();
    test_member_pointer_is_blocked();
    test_enum_requires_independent_width_evidence();
    test_record_layout_and_hidden_return_are_separate_facts();
    test_class_view_preserves_ambiguous_logical_aliases();
    test_class_generation_preserves_direct_owner_boundary();

    std::cout << "abi_model_tests: all invariants passed\n";
    return 0;
}
