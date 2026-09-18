#include <tmfdev/runtime.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <windows.h>
#include <bcrypt.h>

namespace tmf::runtime {
namespace {

bool calculate_sha256(
    const std::filesystem::path& path,
    std::string& result) noexcept
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<std::uint8_t> object;
    std::vector<std::uint8_t> digest;

    const auto failed = [](NTSTATUS status) {
        return status < 0;
    };

    DWORD object_size = 0;
    DWORD hash_size = 0;
    DWORD result_size = 0;

    if (failed(BCryptOpenAlgorithmProvider(
            &algorithm,
            BCRYPT_SHA256_ALGORITHM,
            nullptr,
            0))
        || failed(BCryptGetProperty(
            algorithm,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_size),
            sizeof(object_size),
            &result_size,
            0))
        || failed(BCryptGetProperty(
            algorithm,
            BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hash_size),
            sizeof(hash_size),
            &result_size,
            0))
    ) {
        if (algorithm)
            BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    object.resize(object_size);
    digest.resize(hash_size);

    if (failed(BCryptCreateHash(
            algorithm,
            &hash,
            object.data(),
            static_cast<ULONG>(object.size()),
            nullptr,
            0,
            0))) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    std::array<std::uint8_t, 64 * 1024> buffer{};
    while (input) {
        input.read(
            reinterpret_cast<char*>(buffer.data()),
            static_cast<std::streamsize>(buffer.size())
        );
        const auto count = input.gcount();
        if (count > 0
            && failed(BCryptHashData(
                hash,
                buffer.data(),
                static_cast<ULONG>(count),
                0))) {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return false;
        }
    }

    if (input.bad()
        || failed(BCryptFinishHash(
            hash,
            digest.data(),
            static_cast<ULONG>(digest.size()),
            0))) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);

    std::ostringstream text;
    text << std::hex << std::setfill('0');
    for (const auto byte : digest)
        text << std::setw(2) << static_cast<unsigned>(byte);

    result = text.str();
    return true;
}

bool read_memory(
    std::uintptr_t address,
    void* destination,
    std::size_t size) noexcept
{
    if (!address || !destination || !size
        || address > (std::numeric_limits<std::uintptr_t>::max)() - size)
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(
            reinterpret_cast<const void*>(address),
            &memory,
            sizeof(memory)) != sizeof(memory))
        return false;

    if (memory.State != MEM_COMMIT
        || !memory.Protect
        || (memory.Protect & PAGE_NOACCESS)
        || (memory.Protect & PAGE_GUARD))
        return false;

    const auto region_begin =
        reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    const auto region_size =
        static_cast<std::uintptr_t>(memory.RegionSize);

    if (region_begin > (std::numeric_limits<std::uintptr_t>::max)()
            - region_size)
        return false;

    const auto region_end = region_begin + region_size;
    const auto range_end = address + size;
    if (address < region_begin || range_end > region_end)
        return false;

    SIZE_T bytes_read = 0;
    return ReadProcessMemory(
                GetCurrentProcess(),
                reinterpret_cast<const void*>(address),
                destination,
                size,
                &bytes_read)
        != FALSE
        && bytes_read == size;
}

bool read_process_identity(
    std::uintptr_t module_base,
    ModuleIdentity& identity) noexcept
{
    IMAGE_DOS_HEADER dos{};
    if (!read_memory(module_base, &dos, sizeof(dos))
        || dos.e_magic != IMAGE_DOS_SIGNATURE
        || dos.e_lfanew < static_cast<LONG>(sizeof(IMAGE_DOS_HEADER))
        || dos.e_lfanew > 0x00100000)
        return false;

    if (module_base > (std::numeric_limits<std::uintptr_t>::max)()
            - static_cast<std::uintptr_t>(dos.e_lfanew))
        return false;

    const auto nt_address = module_base
        + static_cast<std::uintptr_t>(dos.e_lfanew);
    IMAGE_NT_HEADERS32 nt{};
    if (!read_memory(nt_address, &nt, sizeof(nt))
        || nt.Signature != IMAGE_NT_SIGNATURE
        || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        return false;

    identity.machine = nt.FileHeader.Machine;
    identity.timestamp = nt.FileHeader.TimeDateStamp;
    identity.image_base = nt.OptionalHeader.ImageBase;
    identity.image_size = nt.OptionalHeader.SizeOfImage;
    return identity.image_size != 0;
}

bool read_pointer(
    std::uintptr_t address,
    std::uintptr_t& value) noexcept
{
    return read_memory(address, &value, sizeof(value));
}

bool valid_object_pointer(
    const Context& context,
    std::uintptr_t value,
    const std::uint32_t* expected_vtable_rvas,
    std::size_t expected_vtable_count) noexcept
{
    if (!value || (value % alignof(void*)) != 0)
        return false;

    std::uintptr_t vtable = 0;
    if (!read_pointer(value, vtable))
        return false;

    const auto image_first = context.resolve_rva(0);
    const auto image_last = context.resolve_rva(
        context.identity().image_size - 1
    );

    if (!(image_first
        && image_last
        && vtable >= image_first
        && vtable <= image_last))
        return false;

    if (expected_vtable_rvas && expected_vtable_count) {
        for (std::size_t index = 0;
             index < expected_vtable_count;
             ++index) {
            if (vtable == context.resolve_rva(expected_vtable_rvas[index]))
                return true;
        }
        return false;
    }

    return true;
}

bool read_object_pointer(
    const Context& context,
    std::uintptr_t address,
    std::uintptr_t& value,
    const std::uint32_t* expected_vtable_rvas = nullptr,
    std::size_t expected_vtable_count = 0) noexcept
{
    return read_pointer(address, value)
        && valid_object_pointer(
            context,
            value,
            expected_vtable_rvas,
            expected_vtable_count
        );
}

} // namespace

const char* status_name(Status status) noexcept
{
    switch (status) {
    case Status::ready:
        return "ready";
    case Status::module_not_found:
        return "module_not_found";
    case Status::invalid_image:
        return "invalid_image";
    case Status::unsupported_build:
        return "unsupported_build";
    case Status::sha256_unavailable:
        return "sha256_unavailable";
    case Status::sha256_mismatch:
        return "sha256_mismatch";
    }

    return "unknown";
}

Context Context::from_module(
    std::uintptr_t module_base,
    ModuleIdentity identity,
    const char* loader_sha256) noexcept
{
    Context result;
    result.module_base_ = module_base;
    result.identity_ = identity;

    if (!module_base || !identity.image_size) {
        result.status_ = Status::invalid_image;
        return result;
    }

    ModuleIdentity actual_identity;
    if (!read_process_identity(module_base, actual_identity)) {
        result.status_ = Status::invalid_image;
        return result;
    }

    if (actual_identity.machine != identity.machine
        || actual_identity.timestamp != identity.timestamp
        || actual_identity.image_base != identity.image_base
        || actual_identity.image_size != identity.image_size) {
        result.identity_ = actual_identity;
        result.status_ = Status::invalid_image;
        return result;
    }

    result.identity_ = actual_identity;

    if (!tmf::build::matches(
            actual_identity.machine,
            actual_identity.timestamp,
            actual_identity.image_base,
            actual_identity.image_size)) {
        result.status_ = Status::unsupported_build;
        return result;
    }

    if (loader_sha256) {
        if (!tmf::build::sha256_matches(loader_sha256)) {
            result.status_ = Status::sha256_mismatch;
            return result;
        }
        result.file_sha256_verified_ = true;
    }

    result.status_ = Status::ready;
    return result;
}

Context Context::current_process() noexcept
{
#ifdef _WIN32
    const auto module = GetModuleHandleW(nullptr);
    if (!module) {
        Context result;
        result.status_ = Status::module_not_found;
        return result;
    }

    const auto module_base = reinterpret_cast<std::uintptr_t>(module);
    ModuleIdentity identity;
    if (!read_process_identity(module_base, identity)) {
        Context result;
        result.module_base_ = module_base;
        result.status_ = Status::invalid_image;
        return result;
    }

    std::array<wchar_t, 32768> path{};
    const auto length = GetModuleFileNameW(
        nullptr,
        path.data(),
        static_cast<DWORD>(path.size())
    );
    if (!length || length >= path.size()) {
        Context result;
        result.module_base_ = module_base;
        result.identity_ = identity;
        result.status_ = Status::sha256_unavailable;
        return result;
    }

    std::string sha256;
    if (!calculate_sha256(
            std::filesystem::path(std::wstring(path.data(), length)),
            sha256)) {
        Context result;
        result.module_base_ = module_base;
        result.identity_ = identity;
        result.status_ = Status::sha256_unavailable;
        return result;
    }

    return from_module(module_base, identity, sha256.c_str());
#else
    Context result;
    result.status_ = Status::module_not_found;
    return result;
#endif
}

std::uintptr_t Context::resolve_rva(std::uint32_t rva) const noexcept
{
    if (!ready()
        || rva >= identity_.image_size
        || module_base_ > (std::numeric_limits<std::uintptr_t>::max)() - rva)
        return 0;

    return module_base_ + rva;
}

std::uintptr_t Context::game_app_slot() const noexcept
{
    return resolve_rva(GameAppGlobalRva);
}

CGameApp* Context::game_app() const noexcept
{
    const auto slot = game_app_slot();
    if (!slot)
        return nullptr;

    std::uintptr_t pointer = 0;
    if (!read_object_pointer(
            *this,
            slot,
            pointer,
            GameAppVtableRvas.data(),
            GameAppVtableRvas.size()))
        return nullptr;

    return reinterpret_cast<CGameApp*>(pointer);
}

CGamePlayground* Context::playground() const noexcept
{
    const auto game = game_app();
    if (!game)
        return nullptr;

    std::uintptr_t game_vtable = 0;
    if (!read_pointer(
            reinterpret_cast<std::uintptr_t>(game),
            game_vtable)
        || game_vtable != resolve_rva(TrackManiaVtableRva))
        return nullptr;

    const auto getter_address =
        resolve_rva(TrackManiaGetPlaygroundRva);
    if (!getter_address)
        return nullptr;

    using GetPlaygroundFn =
        CGamePlayground* (__thiscall *)(CGameApp*);
    const auto getter = reinterpret_cast<GetPlaygroundFn>(
        getter_address
    );
    const auto playground = getter(game);
    const auto playground_address =
        reinterpret_cast<std::uintptr_t>(playground);
    if (!playground
            || !valid_object_pointer(
                *this,
                playground_address,
                PlaygroundVtableRvas.data(),
                PlaygroundVtableRvas.size()))
        return nullptr;

    return playground;
}

CAudioPort* Context::audio_port() const noexcept
{
    const auto game = game_app();
    if (!game)
        return nullptr;

    const auto address = reinterpret_cast<std::uintptr_t>(game);
    if (address > (std::numeric_limits<std::uintptr_t>::max)()
            - GameAppAudioPortOffset)
        return nullptr;

    std::uintptr_t pointer = 0;
    if (!read_object_pointer(
            *this,
            address + GameAppAudioPortOffset,
            pointer,
            AudioPortVtableRvas.data(),
            AudioPortVtableRvas.size()))
        return nullptr;

    return reinterpret_cast<CAudioPort*>(pointer);
}

CInputPort* Context::input_port() const noexcept
{
    const auto game = game_app();
    if (!game)
        return nullptr;

    const auto address = reinterpret_cast<std::uintptr_t>(game);
    if (address > (std::numeric_limits<std::uintptr_t>::max)()
            - GameAppInputPortOffset)
        return nullptr;

    std::uintptr_t pointer = 0;
    if (!read_object_pointer(
            *this,
            address + GameAppInputPortOffset,
            pointer,
            InputPortVtableRvas.data(),
            InputPortVtableRvas.size()))
        return nullptr;

    return reinterpret_cast<CInputPort*>(pointer);
}

CTrackManiaEditor* Context::block_editor() const noexcept
{
    const auto game = game_app();
    if (!game)
        return nullptr;

    std::uintptr_t game_vtable = 0;
    if (!read_pointer(
            reinterpret_cast<std::uintptr_t>(game),
            game_vtable)
        || game_vtable != resolve_rva(TrackManiaVtableRva))
        return nullptr;

    const auto getter_address =
        resolve_rva(TrackManiaGetTmBlockEditorRva);
    if (!getter_address)
        return nullptr;

    using GetTmBlockEditorFn =
        CTrackManiaEditor* (__thiscall *)(CGameApp*);
    const auto getter = reinterpret_cast<GetTmBlockEditorFn>(
        getter_address
    );
    const auto editor = getter(game);
    const auto editor_address =
        reinterpret_cast<std::uintptr_t>(editor);
    if (!editor
            || !valid_object_pointer(
                *this,
                editor_address,
                BlockEditorVtableRvas.data(),
                BlockEditorVtableRvas.size()))
        return nullptr;

    return editor;
}

} // namespace tmf::runtime
