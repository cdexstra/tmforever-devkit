#include "CTrackManiaEditor.hpp"

#include <cstdint>

namespace {

constexpr std::uint32_t kImageBase = 0x00400000u;
constexpr std::uint32_t kTimestamp = 0x4D431494u;
constexpr std::uint32_t kImageSize = 0x00A2B000u;

}

int main()
{
    static_assert(sizeof(tmf::GmNat3) == 0x0Cu);
    static_assert(
        tmf::native::CTrackManiaEditor_PlaceBlock_000AE110VtableSlot == 67
    );
    static_assert(
        tmf::native::CTrackManiaEditor_PlaceBlock_000AE110Rva == 0x000AE110u
    );
    static_assert(
        tmf::native::CTrackManiaEditor_VtableRva == 0x0074114Cu
    );

    if (!tmf::build::matches(
            0x014Cu,
            kTimestamp,
            kImageBase,
            kImageSize))
        return 1;

    const auto place_block_call =
        &tmf::api::CTrackManiaEditorApi::PlaceBlock;
    const auto place_block_view_call =
        &tmf::view::CTrackManiaEditorView::PlaceBlock;
    const auto place_block_resolver =
        tmf::api::CTrackManiaEditorApi::resolve_CTrackManiaEditor_PlaceBlock_000AE110(
            kImageBase
        );

    const tmf::view::CTrackManiaEditorView unbound_editor{
        kImageBase,
        nullptr,
    };

    if (!place_block_call
        || !place_block_view_call
        || !place_block_resolver
        || static_cast<bool>(unbound_editor))
        return 2;

    const auto vtable =
        tmf::native::resolve_CTrackManiaEditor_Vtable(kImageBase);

    if (!vtable)
        return 3;

    return 0;
}
