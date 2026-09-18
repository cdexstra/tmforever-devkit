#include "CAudioPort.hpp"
#include "CGameApp.hpp"

#include <cstdint>

namespace {

constexpr std::uint32_t kImageBase = 0x00400000u;
constexpr std::uint32_t kTimestamp = 0x4D431494u;
constexpr std::uint32_t kImageSize = 0x00A2B000u;

using SpatialViewCall = void (tmf::view::CAudioPortView::*)(
    tmf::CPlugSound*,
    tmf::GmVec3 const &,
    tmf::GmVec3 const &,
    int
) const noexcept;

using CueViewCall = void (tmf::view::CAudioPortView::*)(
    tmf::CPlugSound*,
    int
) const noexcept;

}

int main()
{
    static_assert(
        tmf::native::CAudioPort_PlayPlugSound_0039EA20Rva == 0x0039EA20u
    );
    static_assert(
        tmf::native::CAudioPort_PlayPlugSound_0039F3C0Rva == 0x0039F3C0u
    );
    static_assert(
        tmf::native::CGameApp_AudioPort_Offset_member_CGameApp__AudioPort_3
            == 0x68u
    );
    static_assert(
        tmf::build::matches(
            0x014Cu,
            kTimestamp,
            kImageBase,
            kImageSize
        )
    );

    const auto spatial_resolver =
        tmf::api::CAudioPortApi::resolve_CAudioPort_PlayPlugSound_0039EA20(
            kImageBase
        );
    const auto cue_resolver =
        tmf::api::CAudioPortApi::resolve_CAudioPort_PlayPlugSound_0039F3C0(
            kImageBase
        );
    const auto spatial_view = static_cast<SpatialViewCall>(
        &tmf::view::CAudioPortView::PlayPlugSound
    );
    const auto cue_view = static_cast<CueViewCall>(
        &tmf::view::CAudioPortView::PlayPlugSound
    );
    const auto audio_vtable =
        tmf::native::resolve_CAudioPort_Vtable(kImageBase);

    return spatial_resolver != nullptr
        && cue_resolver != nullptr
        && spatial_view != nullptr
        && cue_view != nullptr
        && audio_vtable != 0
        ? 0
        : 1;
}
