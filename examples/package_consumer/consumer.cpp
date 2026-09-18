#include <tmfdev/CSceneMobil.hpp>

static_assert(tmf::native::CSceneMobil_VtableRva == 0x0079E5D4u);
static_assert(tmf::build::sha256_matches(tmf::build::ExecutableSha256));

int main()
{
    const auto set_quality = &tmf::api::CSceneMobilApi::SetQuality;
    const auto resolver =
        tmf::api::CSceneMobilApi::resolve_CSceneMobil_SetQuality_00561DD0(
            tmf::build::ImageBase
        );
    const auto view_call =
        &tmf::view::CSceneMobilView::SetQuality;
    const tmf::view::CSceneMobilView unbound_scene{
        tmf::build::ImageBase,
        nullptr,
    };

    return set_quality != nullptr
        && resolver != nullptr
        && view_call != nullptr
        && !static_cast<bool>(unbound_scene)
        ? 0
        : 1;
}
