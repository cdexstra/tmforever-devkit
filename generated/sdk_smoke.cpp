#include "CTrackManiaEditor.hpp"
#include "CAudioPort.hpp"
#include "CTrackManiaControlPlayerInput.hpp"
#include "CSceneMobil.hpp"
#include "CGameApp.hpp"

static_assert(
    tmf::native::CGameApp_AudioPort_Offset_member_CGameApp__AudioPort_3 == 0x68u
);
static_assert(
    tmf::native::CGameApp_VtableRva == 0x00760DFCu
);

static_assert(tmf::build::matches(
    0x014Cu,
    0x4D431494u,
    0x00400000u,
    0x00A2B000u
));
static_assert(!tmf::build::matches(
    0x014Cu,
    0x00000000u,
    0x00400000u,
    0x00A2B000u
));
static_assert(tmf::build::sha256_matches(tmf::build::ExecutableSha256));
static_assert(!tmf::build::sha256_matches("not-this-build"));

int main()
{
    const auto editor =
        tmf::api::CTrackManiaEditorApi::resolve_CTrackManiaEditor_PlaceBlock_000AE110(
            0x00400000u
        );
    const auto audio =
        tmf::api::CAudioPortApi::resolve_CAudioPort_PlayPlugSound_0039EA20(
            0x00400000u
        );
    const auto input =
        tmf::api::CTrackManiaControlPlayerInputApi::resolve_CTrackManiaControlPlayerInput_UpdateVehicleStateFromInputs_000FE500(
            0x00400000u
        );
    const auto scene =
        tmf::api::CSceneMobilApi::resolve_CSceneMobil_SetQuality_00561DD0(
            0x00400000u
        );
    const auto game =
        tmf::api::CGameAppApi::resolve_CGameApp_GetNetwork_00058D00(
            0x00400000u
        );
    const auto game_global =
        tmf::native::resolve_Global_00968C44_public__static_class_CGameApp___CGameApp__s_TheGame(
            0x00400000u
        );
    const auto game_vtable =
        tmf::native::resolve_CGameApp_Vtable(0x00400000u);

    (void)editor;
    (void)audio;
    (void)input;
    (void)scene;
    (void)game;
    (void)game_global;
    (void)game_vtable;
    return 0;
}
