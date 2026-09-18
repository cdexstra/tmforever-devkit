#include <tmfdev/runtime.hpp>
#include <TMForever_all.hpp>

#include <array>
#include <cstring>
#include <windows.h>

int main()
{
    const auto current_process =
        tmf::runtime::Context::current_process();
    using NetworkFn =
        tmf::native::CGameApp_GetNetwork_00058D00Fn;

    alignas(void*) std::array<std::uint8_t, 0x80> fake_game_storage{};
    alignas(void*) std::array<std::uint8_t, 0x10> fake_audio_storage{};
    alignas(void*) std::array<std::uint8_t, 0x10> fake_input_storage{};
    alignas(void*) std::array<std::uint8_t, 0x10> fake_editor_storage{};
    alignas(void*) std::array<std::uint8_t, 0x10> fake_playground_storage{};
    const auto fake_module = VirtualAlloc(
        nullptr,
        tmf::build::ImageSize,
        MEM_RESERVE | MEM_COMMIT,
        PAGE_READWRITE
    );

    if (!fake_module)
        return 1;

    const auto fake_base =
        reinterpret_cast<std::uintptr_t>(fake_module);

    IMAGE_DOS_HEADER fake_dos{};
    fake_dos.e_magic = IMAGE_DOS_SIGNATURE;
    fake_dos.e_lfanew = sizeof(IMAGE_DOS_HEADER);
    std::memcpy(fake_module, &fake_dos, sizeof(fake_dos));

    IMAGE_NT_HEADERS32 fake_nt{};
    fake_nt.Signature = IMAGE_NT_SIGNATURE;
    fake_nt.FileHeader.Machine = tmf::build::Machine;
    fake_nt.FileHeader.TimeDateStamp = tmf::build::Timestamp;
    fake_nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    fake_nt.OptionalHeader.ImageBase = tmf::build::ImageBase;
    fake_nt.OptionalHeader.SizeOfImage = tmf::build::ImageSize;
    std::memcpy(
        reinterpret_cast<std::uint8_t*>(fake_module)
            + fake_dos.e_lfanew,
        &fake_nt,
        sizeof(fake_nt)
    );

    const auto accepted = tmf::runtime::Context::from_module(
        fake_base,
        {
            tmf::build::Machine,
            tmf::build::Timestamp,
            tmf::build::ImageBase,
            tmf::build::ImageSize,
        },
        tmf::build::ExecutableSha256
    );
    const auto wrong_build = tmf::runtime::Context::from_module(
        current_process.module_base(),
        current_process.identity()
    );
    const auto wrong_sha = tmf::runtime::Context::from_module(
        fake_base,
        {
            tmf::build::Machine,
            tmf::build::Timestamp,
            tmf::build::ImageBase,
            tmf::build::ImageSize,
        },
        "not-the-supported-build"
    );
    const auto identity_only = tmf::runtime::Context::from_module(
        fake_base,
        {
            tmf::build::Machine,
            tmf::build::Timestamp,
            tmf::build::ImageBase,
            tmf::build::ImageSize,
        }
    );
    const auto inconsistent_identity = tmf::runtime::Context::from_module(
        fake_base,
        {
            tmf::build::Machine,
            tmf::build::Timestamp + 1u,
            tmf::build::ImageBase,
            tmf::build::ImageSize,
        }
    );

    const auto invalid_module = tmf::runtime::Context::from_module(
        0x1000u,
        {
            tmf::build::Machine,
            tmf::build::Timestamp,
            tmf::build::ImageBase,
            tmf::build::ImageSize,
        }
    );
    const auto fake_game = reinterpret_cast<tmf::CGameApp*>(
        fake_game_storage.data()
    );
    const auto fake_audio = reinterpret_cast<tmf::CAudioPort*>(
        fake_audio_storage.data()
    );
    const auto fake_input = reinterpret_cast<tmf::CInputPort*>(
        fake_input_storage.data()
    );
    const auto fake_editor = reinterpret_cast<tmf::CTrackManiaEditor*>(
        fake_editor_storage.data()
    );
    const auto fake_playground = reinterpret_cast<tmf::CGamePlayground*>(
        fake_playground_storage.data()
    );

    *reinterpret_cast<std::uintptr_t*>(fake_game_storage.data()) =
        fake_base + tmf::runtime::GameAppVtableRvas[1];
    *reinterpret_cast<std::uintptr_t*>(fake_audio_storage.data()) =
        fake_base + tmf::runtime::AudioPortVtableRvas[0];
    *reinterpret_cast<std::uintptr_t*>(fake_input_storage.data()) =
        fake_base + tmf::runtime::InputPortVtableRvas[0];
    *reinterpret_cast<std::uintptr_t*>(fake_editor_storage.data()) =
        fake_base + tmf::runtime::BlockEditorVtableRvas[0];
    *reinterpret_cast<std::uintptr_t*>(fake_playground_storage.data()) =
        fake_base + tmf::runtime::PlaygroundVtableRvas[0];

    const auto getter_address =
        fake_base + tmf::runtime::TrackManiaGetTmBlockEditorRva;
    const auto editor_address =
        static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(fake_editor)
        );
    auto* getter_bytes = reinterpret_cast<std::uint8_t*>(getter_address);
    getter_bytes[0] = 0xB8; // mov eax, imm32
    std::memcpy(getter_bytes + 1, &editor_address, sizeof(editor_address));
    getter_bytes[5] = 0xC3; // ret

    DWORD old_protection = 0;
    if (!VirtualProtect(
            getter_bytes,
            6,
            PAGE_EXECUTE_READWRITE,
            &old_protection))
        return 1;

    const auto playground_getter_address =
        fake_base + tmf::runtime::TrackManiaGetPlaygroundRva;
    const auto playground_address =
        static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(fake_playground)
        );
    auto* playground_getter_bytes =
        reinterpret_cast<std::uint8_t*>(playground_getter_address);
    playground_getter_bytes[0] = 0xB8; // mov eax, imm32
    std::memcpy(
        playground_getter_bytes + 1,
        &playground_address,
        sizeof(playground_address)
    );
    playground_getter_bytes[5] = 0xC3; // ret

    if (!VirtualProtect(
            playground_getter_bytes,
            6,
            PAGE_EXECUTE_READWRITE,
            &old_protection))
        return 1;

    *reinterpret_cast<tmf::CGameApp**>(
        fake_base + tmf::runtime::GameAppGlobalRva
    ) = fake_game;
    *reinterpret_cast<tmf::CAudioPort**>(
        fake_game_storage.data()
            + tmf::runtime::GameAppAudioPortOffset
    ) = fake_audio;
    *reinterpret_cast<tmf::CInputPort**>(
        fake_game_storage.data()
            + tmf::runtime::GameAppInputPortOffset
    ) = fake_input;

    const auto synthetic = tmf::runtime::Context::from_module(
        fake_base,
        {
            tmf::build::Machine,
            tmf::build::Timestamp,
            tmf::build::ImageBase,
            tmf::build::ImageSize,
        },
        tmf::build::ExecutableSha256
    );

    const auto synthetic_game = synthetic.game_app();
    const auto synthetic_audio = synthetic.audio_port();
    const auto synthetic_input = synthetic.input_port();

    *reinterpret_cast<std::uintptr_t*>(fake_audio_storage.data()) =
        fake_base + 0x100u;
    const auto invalid_audio_vtable = synthetic.audio_port();
    *reinterpret_cast<std::uintptr_t*>(fake_audio_storage.data()) =
        fake_base + tmf::runtime::AudioPortVtableRvas[0];

    *reinterpret_cast<std::uintptr_t*>(fake_input_storage.data()) =
        fake_base + 0x100u;
    const auto invalid_input_vtable = synthetic.input_port();
    *reinterpret_cast<std::uintptr_t*>(fake_input_storage.data()) =
        fake_base + tmf::runtime::InputPortVtableRvas[0];

    *reinterpret_cast<std::uintptr_t*>(fake_game_storage.data()) =
        fake_base + tmf::runtime::TrackManiaVtableRva;
    const auto synthetic_editor = synthetic.block_editor();
    const auto synthetic_playground = synthetic.playground();

    *reinterpret_cast<std::uintptr_t*>(fake_game_storage.data()) =
        fake_base + 0x100u;
    const auto wrong_dynamic_editor = synthetic.block_editor();
    const auto wrong_dynamic_playground = synthetic.playground();

    *reinterpret_cast<std::uintptr_t*>(fake_game_storage.data()) =
        fake_base + tmf::runtime::TrackManiaVtableRva;
    *reinterpret_cast<std::uintptr_t*>(fake_editor_storage.data()) = 0x1000u;
    const auto invalid_editor_vtable = synthetic.block_editor();
    *reinterpret_cast<std::uintptr_t*>(fake_editor_storage.data()) =
        fake_base + tmf::runtime::BlockEditorVtableRvas[0];

    *reinterpret_cast<std::uintptr_t*>(fake_playground_storage.data()) = 0x1000u;
    const auto invalid_playground_vtable = synthetic.playground();
    *reinterpret_cast<std::uintptr_t*>(fake_playground_storage.data()) =
        fake_base + tmf::runtime::PlaygroundVtableRvas[0];

    *reinterpret_cast<tmf::CGameApp**>(
        fake_base + tmf::runtime::GameAppGlobalRva
    ) = reinterpret_cast<tmf::CGameApp*>(0x1000u);
    const auto invalid_audio = synthetic.audio_port();

    *reinterpret_cast<tmf::CGameApp**>(
        fake_base + tmf::runtime::GameAppGlobalRva
    ) = fake_game;
    *reinterpret_cast<std::uintptr_t*>(fake_game_storage.data()) = 0x1000u;
    const auto invalid_game_vtable = synthetic.game_app();

    const auto freed = VirtualFree(fake_module, 0, MEM_RELEASE);

    if (!accepted
        || accepted.status() != tmf::runtime::Status::ready
        || !accepted.file_sha256_verified()
        || accepted.resolve_rva(0x1000u)
            != fake_base + 0x1000u
        || !accepted.resolve_function<NetworkFn>(
            tmf::native::CGameApp_GetNetwork_00058D00Rva)
        || wrong_build.resolve_function<NetworkFn>(
            tmf::native::CGameApp_GetNetwork_00058D00Rva)
        || identity_only.resolve_function<NetworkFn>(
            tmf::native::CGameApp_GetNetwork_00058D00Rva) == nullptr
        || accepted.resolve_rva(tmf::build::ImageSize) != 0
        || accepted.game_app_slot()
            != fake_base + tmf::runtime::GameAppGlobalRva
        || wrong_build.status() != tmf::runtime::Status::unsupported_build
        || wrong_sha.status() != tmf::runtime::Status::sha256_mismatch
        || inconsistent_identity.status()
            != tmf::runtime::Status::invalid_image
        || invalid_module.status() != tmf::runtime::Status::invalid_image
        || identity_only.status() != tmf::runtime::Status::ready
        || identity_only.file_sha256_verified()
        || current_process.status()
            != tmf::runtime::Status::unsupported_build
        || synthetic.status() != tmf::runtime::Status::ready
        || synthetic_game != fake_game
        || synthetic_audio != fake_audio
        || synthetic_input != fake_input
        || synthetic_editor != fake_editor
        || synthetic_playground != fake_playground
        || invalid_audio_vtable != nullptr
        || invalid_input_vtable != nullptr
        || wrong_dynamic_editor != nullptr
        || wrong_dynamic_playground != nullptr
        || invalid_editor_vtable != nullptr
        || invalid_playground_vtable != nullptr
        || invalid_audio != nullptr
        || invalid_game_vtable != nullptr
        || freed == 0)
        return 1;

    return 0;
}
