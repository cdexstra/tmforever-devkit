#include "CTrackManiaControlPlayerInput.hpp"

#include <cstdint>

namespace {

constexpr std::uint32_t kImageBase = 0x00400000u;
constexpr std::uint32_t kTimestamp = 0x4D431494u;
constexpr std::uint32_t kImageSize = 0x00A2B000u;

}

int main()
{
    using StaticCall = void (*) (
        std::uintptr_t,
        tmf::OpaqueNested_CTrackManiaControlPlayerInput__SRaceInputs const &,
        tmf::CSceneMobil*
    );
    using MemberCall = void (*) (
        std::uintptr_t,
        tmf::CTrackManiaControlPlayerInput*,
        tmf::OpaqueNested_CTrackManiaControlPlayerInput__SRaceInputs const &
    );

    using MemberViewCall = void (
        tmf::view::CTrackManiaControlPlayerInputView::*
    )(
        tmf::OpaqueNested_CTrackManiaControlPlayerInput__SRaceInputs const &
    ) const noexcept;

    using StaticUpdate =
        tmf::native::CTrackManiaControlPlayerInput_UpdateVehicleStateFromInputs_000FE500Fn;
    using MemberUpdate =
        tmf::native::CTrackManiaControlPlayerInput_UpdateVehicleStateFromInputs_000FE7E0Fn;

    static_assert(
        tmf::native::CTrackManiaControlPlayerInput_UpdateVehicleStateFromInputs_000FE500Rva
            != tmf::native::CTrackManiaControlPlayerInput_UpdateVehicleStateFromInputs_000FE7E0Rva
    );

    if (!tmf::build::matches(
            0x014Cu,
            kTimestamp,
            kImageBase,
            kImageSize))
        return 1;

    const StaticUpdate static_update =
        tmf::api::CTrackManiaControlPlayerInputApi::resolve_CTrackManiaControlPlayerInput_UpdateVehicleStateFromInputs_000FE500(
            kImageBase
        );
    const MemberUpdate member_update =
        tmf::api::CTrackManiaControlPlayerInputApi::resolve_CTrackManiaControlPlayerInput_UpdateVehicleStateFromInputs_000FE7E0(
            kImageBase
        );
    const StaticCall static_call =
        &tmf::api::CTrackManiaControlPlayerInputApi::UpdateVehicleStateFromInputs;
    const MemberCall member_call =
        &tmf::api::CTrackManiaControlPlayerInputApi::UpdateVehicleStateFromInputs;
    const MemberViewCall member_view_call = static_cast<MemberViewCall>(
        &tmf::view::CTrackManiaControlPlayerInputView::UpdateVehicleStateFromInputs
    );

    if (!static_update
        || !member_update
        || !static_call
        || !member_call
        || !member_view_call)
        return 2;

    return 0;
}
