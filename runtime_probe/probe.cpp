#include <windows.h>
#include <MinHook.h>

#include "../generated/tmfdev/runtime.hpp"
#include "../generated/CGameApp.hpp"
#include "../generated/CAudioPort.hpp"
#include "../generated/CMwNod.hpp"
#include "../generated/CTrackManiaEditor.hpp"
#include "../generated/CTrackManiaControlPlayerInput.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_set>

static std::wstring g_logPath;

using PlayPlugSoundSpatialFn =
    tmf::native::CAudioPort_PlayPlugSound_0039EA20Fn;

using UpdateVehicleStateFromInputsFn =
    tmf::native::CTrackManiaControlPlayerInput_UpdateVehicleStateFromInputs_000FE7E0Fn;

static PlayPlugSoundSpatialFn g_originalPlayPlugSoundSpatial = nullptr;
static UpdateVehicleStateFromInputsFn g_originalUpdateVehicleStateFromInputs = nullptr;
static tmf::native::CTrackManiaControlPlayerInput_GetConstructionPlayer_000FE3D0Fn
    g_getConstructionPlayer = nullptr;
static const tmf::runtime::Context* g_runtime = nullptr;

static tmf::CAudioPort* g_expectedAudioPort = nullptr;

static volatile LONG g_armed = 0;
static volatile LONG g_seen = 0;
static volatile LONG g_suppressed = 0;
static volatile LONG g_inputCallCount = 0;
static volatile LONG g_inputFirstObject = 0;
static volatile LONG g_inputLastObject = 0;
static volatile LONG g_inputFirstVtable = 0;
static volatile LONG g_inputLastVtable = 0;
static volatile LONG g_inputFirstPlayer = 0;
static volatile LONG g_inputLastPlayer = 0;
static volatile LONG g_inputFirstPlayerVtable = 0;
static volatile LONG g_inputLastPlayerVtable = 0;
static volatile LONG g_inputFirstPlayground = 0;
static volatile LONG g_inputLastPlayground = 0;
static volatile LONG g_inputFirstPlaygroundVtable = 0;
static volatile LONG g_inputLastPlaygroundVtable = 0;
static volatile LONG g_inputThreadId = 0;

static bool InitializeLogPath()
{
    wchar_t processPath[32768]{};
    const DWORD length = GetModuleFileNameW(
        nullptr,
        processPath,
        _countof(processPath)
    );

    if (!length || length == _countof(processPath))
        return false;

    const std::wstring fullPath(processPath, length);
    const auto separator = fullPath.find_last_of(L"\\/");

    if (separator == std::wstring::npos)
        return false;

    g_logPath = fullPath.substr(0, separator + 1)
        + L"tmf_runtime_probe.log";
    return true;
}

static void Log(const std::string& text)
{
    if (g_logPath.empty())
        return;

    std::ofstream file{
        std::filesystem::path(g_logPath),
        std::ios::app
    };

    if (file)
        file << text << '\n';
}

static std::string Hex(std::uintptr_t value)
{
    std::ostringstream out;

    out << "0x"
        << std::hex
        << std::uppercase
        << std::setw(8)
        << std::setfill('0')
        << value;

    return out.str();
}

static bool ReadPointer(
    std::uintptr_t address,
    std::uintptr_t& value)
{
    MEMORY_BASIC_INFORMATION mbi{};

    if (VirtualQuery(
            reinterpret_cast<void*>(address),
            &mbi,
            sizeof(mbi)) != sizeof(mbi))
        return false;

    if (mbi.State != MEM_COMMIT ||
        (mbi.Protect & PAGE_NOACCESS) ||
        (mbi.Protect & PAGE_GUARD))
        return false;

    value = *reinterpret_cast<std::uintptr_t*>(address);
    return true;
}

static bool ReadU32(
    std::uintptr_t address,
    std::uint32_t& value)
{
    std::uintptr_t raw = 0;

    if (!ReadPointer(address, raw))
        return false;

    value = static_cast<std::uint32_t>(raw);
    return true;
}

static bool ReadCString(
    std::uintptr_t address,
    std::string& value)
{
    value.clear();

    for (std::size_t index = 0; index != 128; ++index)
    {
        MEMORY_BASIC_INFORMATION mbi{};

        if (VirtualQuery(
                reinterpret_cast<void*>(address + index),
                &mbi,
                sizeof(mbi)) != sizeof(mbi))
            return false;

        if (mbi.State != MEM_COMMIT ||
            (mbi.Protect & PAGE_NOACCESS) ||
            (mbi.Protect & PAGE_GUARD))
            return false;

        const auto character =
            *reinterpret_cast<const unsigned char*>(address + index);

        if (character == 0)
            return true;

        if (character < 0x20 || character > 0x7E)
            return false;

        value.push_back(static_cast<char>(character));
    }

    return false;
}

static const char* KnownPlayerVtable(
    std::uintptr_t moduleBase,
    std::uintptr_t vtable)
{
    if (vtable == moduleBase + 0x007664C4)
        return "CGamePlayer";

    if (vtable == moduleBase + 0x0075021C)
        return "CTrackManiaPlayer";

    return nullptr;
}

static const char* KnownControlVtable(
    std::uintptr_t moduleBase,
    std::uintptr_t vtable)
{
    if (vtable == moduleBase + 0x00750154)
        return "CTrackManiaControlPlayerInput";

    if (vtable == moduleBase + 0x00785F44)
        return "CGameControlPlayer";

    return nullptr;
}

static const char* KnownPlaygroundVtable(
    std::uintptr_t moduleBase,
    std::uintptr_t vtable)
{
    if (vtable == moduleBase + 0x0076664C)
        return "CGamePlayground";

    return nullptr;
}

static void LogPlayersObservation(
    const tmf::runtime::Context& runtime,
    std::uintptr_t game,
    const char* state)
{
    constexpr auto kPlayersOffset =
        tmf::native::CGameApp_Players_Offset_member_CGameApp__Players_18;
    constexpr auto kPlayerControlOffset = 0x24u;
    constexpr std::size_t kRawWordCount = 11;

    if (!game || game > UINTPTR_MAX - kPlayersOffset)
        return;

    std::uint32_t words[kRawWordCount]{};

    for (std::size_t index = 0; index != kRawWordCount; ++index)
    {
        if (!ReadU32(
                game + kPlayersOffset + index * sizeof(std::uint32_t),
                words[index]))
            return;
    }

    static bool previousReady = false;
    static std::uint32_t previous[kRawWordCount]{};
    static std::string previousState;
    bool changed = !previousReady || previousState != state;

    for (std::size_t index = 0; index != kRawWordCount; ++index)
    {
        if (previous[index] != words[index])
            changed = true;
        previous[index] = words[index];
    }

    previousReady = true;
    previousState = state;

    if (!changed)
        return;

    std::ostringstream raw;
    raw << "players_raw state=" << state
        << " address=" << Hex(game + kPlayersOffset);

    for (std::size_t index = 0; index != kRawWordCount; ++index)
        raw << " word" << index << "=" << Hex(words[index]);

    Log(raw.str());

    const auto imageFirst = runtime.resolve_rva(0);
    const auto imageLast = runtime.resolve_rva(
        tmf::build::ImageSize - 1
    );

    const auto inImage = [imageFirst, imageLast](std::uintptr_t address) {
        return imageFirst
            && imageLast
            && address >= imageFirst
            && address <= imageLast;
    };

    for (std::size_t index = 0; index != kRawWordCount; ++index)
    {
        const auto rawWord = static_cast<std::uintptr_t>(words[index]);

        if (!rawWord || (rawWord % alignof(void*)) != 0)
            continue;

        std::uintptr_t directVtable = 0;

        if (ReadPointer(rawWord, directVtable) && inImage(directVtable))
        {
            std::ostringstream candidate;
            candidate << "players_object_candidate word" << index
                      << " object=" << Hex(rawWord)
                      << " vtable=" << Hex(directVtable);

            if (const auto name = KnownPlayerVtable(
                    runtime.module_base(),
                    directVtable))
                candidate << " type=" << name;

            Log(candidate.str());
        }

        std::uintptr_t element = 0;
        std::uintptr_t elementVtable = 0;

        if (!ReadPointer(rawWord, element)
            || !element
            || (element % alignof(void*)) != 0
            || !ReadPointer(element, elementVtable)
            || !inImage(elementVtable))
            continue;

        std::ostringstream dataCandidate;
        dataCandidate << "players_data_candidate word" << index
                      << " data=" << Hex(rawWord)
                      << " element0=" << Hex(element)
                      << " element0_vtable=" << Hex(elementVtable);

        if (const auto name = KnownPlayerVtable(
                runtime.module_base(),
                elementVtable))
            dataCandidate << " element0_type=" << name;

        Log(dataCandidate.str());

        const auto playerName = KnownPlayerVtable(
            runtime.module_base(),
            elementVtable
        );

        if (!playerName || element > UINTPTR_MAX - kPlayerControlOffset)
            continue;

        std::uintptr_t control = 0;
        std::uintptr_t controlVtable = 0;

        if (!ReadPointer(element + kPlayerControlOffset, control)
            || !control
            || (control % alignof(void*)) != 0
            || !ReadPointer(control, controlVtable)
            || !inImage(controlVtable))
            continue;

        std::ostringstream controlCandidate;
        controlCandidate << "players_control_candidate player="
                         << Hex(element)
                         << " control=" << Hex(control)
                         << " vtable=" << Hex(controlVtable);

        if (const auto name = KnownControlVtable(
                runtime.module_base(),
                controlVtable))
            controlCandidate << " type=" << name;

        Log(controlCandidate.str());
    }
}

static bool ProbeParamInfo(
    std::uintptr_t moduleBase,
    std::uint32_t expectedParamId,
    std::uint32_t expectedType,
    std::int32_t expectedOffset,
    const char* expectedName)
{
    const auto getParamInfo =
        tmf::api::CMwNodApi::resolve_CMwNod_GetParamInfoFromParamId_00523EE0(
            moduleBase
        );

    if (!getParamInfo)
    {
        Log("REFUSED: CMwNod::GetParamInfoFromParamId resolver unavailable.");
        return false;
    }

    const auto paramInfo = getParamInfo(expectedParamId);

    if (!paramInfo)
    {
        Log("REFUSED: reflection parameter descriptor unavailable.");
        return false;
    }

    // These SMwParamInfo offsets are established by the target reflection
    // parser and the independently inspected public descriptor definition.
    constexpr std::uintptr_t kTypeOffset = 0x00;
    constexpr std::uintptr_t kIdOffset = 0x04;
    constexpr std::uintptr_t kStorageOffset = 0x0C;
    constexpr std::uintptr_t kNameOffset = 0x10;

    const auto address =
        reinterpret_cast<std::uintptr_t>(paramInfo);

    std::uint32_t type = 0;
    std::uint32_t id = 0;
    std::uint32_t storageOffset = 0;
    std::uintptr_t name = 0;

    if (!ReadU32(address + kTypeOffset, type)
        || !ReadU32(address + kIdOffset, id)
        || !ReadU32(address + kStorageOffset, storageOffset)
        || !ReadPointer(address + kNameOffset, name))
    {
        Log("REFUSED: reflection parameter descriptor fields unreadable.");
        return false;
    }

    std::string observedName;

    if (!ReadCString(name, observedName)
        || type != expectedType
        || id != expectedParamId
        || static_cast<std::int32_t>(storageOffset) != expectedOffset
        || observedName != expectedName)
    {
        Log(
            std::string("REFUSED: reflection parameter mismatch for ")
            + expectedName
        );
        return false;
    }

    Log(
        std::string("PASS: reflection parameter resolved for ")
        + observedName
        + " -> "
        + Hex(address)
    );
    Log("reflection parameter ID -> " + Hex(id));
    Log("reflection parameter type -> " + Hex(type));
    Log("reflection parameter storage offset -> " + Hex(storageOffset));

    return true;
}

static bool ProbeClassRegistry(std::uintptr_t moduleBase)
{
    // CMwEngineManager::First is an exact-build address-only global from the
    // target MAP. CMwClassInfo::Next is the independently inspected +0x18
    // descriptor field. This function only reads the already-built list.
    constexpr std::uintptr_t kFirstRva = 0x00973BA8;
    constexpr std::uintptr_t kIdOffset = 0x04;
    constexpr std::uintptr_t kNameOffset = 0x14;
    constexpr std::uintptr_t kNextOffset = 0x18;
    constexpr std::size_t kMaxDescriptors = 4096;

    const auto firstAddress = moduleBase + kFirstRva;
    std::uintptr_t current = 0;

    if (!ReadPointer(firstAddress, current) || !current)
    {
        Log("REFUSED: CMwEngineManager::First registry head unavailable.");
        return false;
    }

    struct ExpectedClass {
        std::uint32_t id;
        const char* name;
        bool found = false;
    };

    ExpectedClass expected[] = {
        {tmf::native::CMwNod_MwClassId, "CMwNod"},
        {tmf::native::CGameApp_MwClassId, "CGameApp"},
        {tmf::native::CAudioPort_MwClassId, "CAudioPort"},
        {tmf::native::CTrackManiaEditor_MwClassId, "CTrackManiaEditor"},
        {
            tmf::native::CTrackManiaControlPlayerInput_MwClassId,
            "CTrackManiaControlPlayerInput"
        }
    };

    std::unordered_set<std::uintptr_t> seen;
    std::size_t descriptorCount = 0;

    while (current)
    {
        if (descriptorCount == kMaxDescriptors
            || !seen.insert(current).second)
        {
            Log("REFUSED: class registry traversal was cyclic or too long.");
            return false;
        }

        std::uint32_t id = 0;
        std::uintptr_t namePointer = 0;
        std::uintptr_t next = 0;

        if (!ReadU32(current + kIdOffset, id)
            || !ReadPointer(current + kNameOffset, namePointer)
            || !ReadPointer(current + kNextOffset, next))
        {
            Log("REFUSED: class registry descriptor fields unreadable.");
            return false;
        }

        std::string name;

        if (!ReadCString(namePointer, name))
        {
            Log("REFUSED: class registry descriptor name unreadable.");
            return false;
        }

        for (auto& candidate : expected)
        {
            if (candidate.id == id && name == candidate.name)
                candidate.found = true;
        }

        ++descriptorCount;
        current = next;
    }

    for (const auto& candidate : expected)
    {
        if (!candidate.found)
        {
            Log(
                std::string("REFUSED: expected class missing from registry: ")
                + candidate.name
            );
            return false;
        }
    }

    Log(
        "PASS: class registry traversed; descriptors -> "
        + std::to_string(descriptorCount)
    );

    Log("class registry expected identities -> all present");
    return true;
}

static bool ProbeLiveGameApp(const tmf::runtime::Context& runtime)
{
    const auto moduleBase = runtime.module_base();

    // The global is declared as CGameApp*, but the exact target constructs a
    // CTrackMania object in that slot. These are target-evidenced vtable and
    // virtual GetMwClassId facts, not a generated C++ inheritance claim.
    constexpr std::uintptr_t kTrackManiaVtableRva = 0x0073E074;
    constexpr std::uintptr_t kTrackManiaGetMwClassIdRva = 0x00086F00;
    constexpr std::size_t kGetMwClassIdVtableSlot = 3;
    constexpr std::uint32_t kTrackManiaClassId = 0x24001000u;

    if (!runtime.game_app_slot())
    {
        Log("REFUSED: live CGameApp global is unavailable.");
        return false;
    }

    const auto gameObject = runtime.game_app();
    if (!gameObject)
    {
        Log("REFUSED: live CGameApp object is unavailable.");
        return false;
    }

    const auto game = reinterpret_cast<std::uintptr_t>(gameObject);

    std::uintptr_t vtable = 0;

    if (!ReadPointer(game, vtable)
        || vtable < moduleBase
        || vtable >= moduleBase + tmf::build::ImageSize)
    {
        Log("REFUSED: live CGameApp vtable is not a readable in-module address.");
        return false;
    }

    const auto getMwClassId =
        tmf::api::CGameAppApi::resolve_CGameApp_GetMwClassId_0019E4F0(
            moduleBase
        );

    if (!getMwClassId)
    {
        Log("REFUSED: live CGameApp identity method resolver unavailable.");
        return false;
    }

    const auto observedClassId =
        getMwClassId(reinterpret_cast<tmf::CGameApp*>(game));

    if (observedClassId != tmf::native::CGameApp_MwClassId)
    {
        Log("REFUSED: live CGameApp identity method disagrees with class ID.");
        return false;
    }

    const auto audioObject = runtime.audio_port();
    const auto audioPort =
        reinterpret_cast<std::uintptr_t>(audioObject);

    if (!audioObject)
    {
        Log("REFUSED: live CGameApp::AudioPort is unavailable.");
        return false;
    }

    Log("PASS: existing CGameApp object acquired through verified global.");
    Log("live CGameApp -> " + Hex(game));
    Log("live CGameApp vtable -> " + Hex(vtable));
    Log(
        "CGameApp evidence vtable -> "
        + Hex(tmf::native::resolve_CGameApp_Vtable(moduleBase))
    );
    if (vtable != tmf::native::resolve_CGameApp_Vtable(moduleBase))
        Log("live CGameApp uses a derived/different vtable; base pointer remains opaque");
    Log("live CGameApp class ID -> " + Hex(tmf::native::CGameApp_MwClassId));
    Log("live CGameApp AudioPort -> " + Hex(audioPort));

    if (vtable == moduleBase + kTrackManiaVtableRva)
    {
        std::uintptr_t virtualGetMwClassId = 0;

        if (!ReadPointer(
                vtable + kGetMwClassIdVtableSlot * sizeof(std::uintptr_t),
                virtualGetMwClassId)
            || virtualGetMwClassId
                != moduleBase + kTrackManiaGetMwClassIdRva)
        {
            Log("REFUSED: CTrackMania vtable identity method is unexpected.");
            return false;
        }

        using GetMwClassIdFn = unsigned long (__thiscall *)(tmf::CGameApp*);
        const auto getMwClassId =
            reinterpret_cast<GetMwClassIdFn>(virtualGetMwClassId);

        if (getMwClassId(reinterpret_cast<tmf::CGameApp*>(game))
            != kTrackManiaClassId)
        {
            Log("REFUSED: live CTrackMania identity method disagrees.");
            return false;
        }

        Log("live CGameApp dynamic vtable type -> CTrackMania");
        Log("live CGameApp dynamic class ID -> " + Hex(kTrackManiaClassId));
    }

    Log("PASS: live object identity and AudioPort offset agree.");
    return true;
}

static bool MetadataOnlyRequested()
{
    wchar_t value[16]{};

    return GetEnvironmentVariableW(
               L"TMFDEV_METADATA_ONLY",
               value,
               _countof(value))
        != 0;
}

static bool LifecycleOnlyRequested()
{
    wchar_t value[16]{};

    return GetEnvironmentVariableW(
               L"TMFDEV_LIFECYCLE_ONLY",
               value,
               _countof(value))
        != 0;
}

static bool LifecycleMarkedRequested()
{
    wchar_t value[16]{};

    return GetEnvironmentVariableW(
               L"TMFDEV_LIFECYCLE_MARKED",
               value,
               _countof(value))
        != 0;
}

static bool InputHookOnlyRequested()
{
    wchar_t value[16]{};

    return GetEnvironmentVariableW(
               L"TMFDEV_INPUT_HOOK_ONLY",
               value,
               _countof(value))
        != 0;
}

static bool LifecycleKeyDown(int virtualKey)
{
    return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0
        && (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0
        && (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

static void LogLifecycleSnapshot(
    const tmf::runtime::Context& runtime,
    std::size_t sample,
    const char* state)
{
    const auto gameObject = runtime.game_app();
    const auto game = reinterpret_cast<std::uintptr_t>(gameObject);

    std::uintptr_t vtable = 0;
    std::uintptr_t audioPort = 0;
    std::uintptr_t inputPort = 0;
    std::uintptr_t inputPortVtable = 0;
    std::uintptr_t editor = 0;
    std::uintptr_t editorVtable = 0;
    std::uintptr_t editorField = 0;
    std::uintptr_t editorFieldVtable = 0;
    std::uintptr_t playground = 0;
    std::uintptr_t playgroundVtable = 0;
    bool validVtable = false;
    bool validInputPortVtable = false;
    bool validEditorVtable = false;
    bool validEditorFieldVtable = false;
    bool validPlaygroundVtable = false;

    if (gameObject
        && ReadPointer(game, vtable)) {
        const auto first = runtime.resolve_rva(0);
        const auto last = runtime.resolve_rva(
            tmf::build::ImageSize - 1
        );
        validVtable = first
            && last
            && vtable >= first
            && vtable <= last;

        if (validVtable)
        {
            LogPlayersObservation(runtime, game, state);

            audioPort = reinterpret_cast<std::uintptr_t>(
                runtime.audio_port()
            );

            // CGameApp::InputPort is an exact reflection-backed pointer
            // member. Read it as an observation only; its concrete type,
            // ownership and active-player semantics are not established.
            constexpr auto kInputPortOffset =
                tmf::native::CGameApp_InputPort_Offset_member_CGameApp__InputPort_4;
            if (game <= UINTPTR_MAX - kInputPortOffset
                && ReadPointer(game + kInputPortOffset, inputPort)
                && inputPort
                && ReadPointer(inputPort, inputPortVtable))
            {
                const auto first = runtime.resolve_rva(0);
                const auto last = runtime.resolve_rva(
                    tmf::build::ImageSize - 1
                );
                validInputPortVtable = first
                    && last
                    && inputPortVtable >= first
                    && inputPortVtable <= last;
            }

            // The generated accessor performs the exact CTrackMania root
            // check and invokes the read-only getter only for that type.
            if (vtable == runtime.resolve_rva(
                    tmf::runtime::TrackManiaVtableRva))
            {
                // CTrackMania::Editor is an exact reflection-backed pointer
                // member at +0x414. Read it separately from GetTmBlockEditor
                // so the probe can distinguish the root field from the
                // changing block-editor component.
                constexpr std::uintptr_t kEditorFieldOffset = 0x00000414;
                if (game <= UINTPTR_MAX - kEditorFieldOffset
                    && ReadPointer(game + kEditorFieldOffset, editorField)
                    && editorField
                    && ReadPointer(editorField, editorFieldVtable))
                {
                    const auto first = runtime.resolve_rva(0);
                    const auto last = runtime.resolve_rva(
                        tmf::build::ImageSize - 1
                    );
                    validEditorFieldVtable = first
                        && last
                        && editorFieldVtable >= first
                        && editorFieldVtable <= last;
                }

                const auto editorObject = runtime.block_editor();
                editor = reinterpret_cast<std::uintptr_t>(editorObject);
                if (editorObject && ReadPointer(editor, editorVtable))
                {
                    const auto first = runtime.resolve_rva(0);
                    const auto last = runtime.resolve_rva(
                        tmf::build::ImageSize - 1
                    );
                    validEditorVtable = first
                        && last
                        && editorVtable >= first
                        && editorVtable <= last;
                }

                const auto playgroundObject = runtime.playground();
                playground = reinterpret_cast<std::uintptr_t>(playgroundObject);
                if (playgroundObject && ReadPointer(playground, playgroundVtable))
                {
                    const auto first = runtime.resolve_rva(0);
                    const auto last = runtime.resolve_rva(
                        tmf::build::ImageSize - 1
                    );
                    validPlaygroundVtable = first
                        && last
                        && playgroundVtable >= first
                        && playgroundVtable <= last;
                }
            }
        }
    }

    std::ostringstream line;
    line << "snapshot=" << sample
         << " state=" << state
         << " game=" << (gameObject ? Hex(game) : "<null>")
         << " vtable="
         << (validVtable ? Hex(vtable) : "<unreadable-or-out-of-image>")
         << " audio_port="
         << (audioPort ? Hex(audioPort) : "<null-or-unreadable>")
         << " input_port="
         << (inputPort ? Hex(inputPort) : "<null-or-unreadable>")
         << " input_port_vtable="
         << (validInputPortVtable
                 ? Hex(inputPortVtable)
                 : "<unreadable-or-out-of-image>")
         << " block_editor="
         << (editor ? Hex(editor) : "<null-or-unavailable>")
         << " editor_vtable="
         << (validEditorVtable ? Hex(editorVtable) : "<unreadable-or-out-of-image>")
         << " editor_field="
         << (editorField ? Hex(editorField) : "<null-or-unavailable>")
         << " editor_field_vtable="
         << (validEditorFieldVtable
                 ? Hex(editorFieldVtable)
                 : "<unreadable-or-out-of-image>")
          << " editor_field_matches_block_accessor="
          << (editor && editorField
                  ? (editor == editorField ? "yes" : "no")
                  : "unknown")
          << " playground="
          << (playground ? Hex(playground) : "<null-or-unavailable>")
          << " playground_vtable="
          << (validPlaygroundVtable
                  ? Hex(playgroundVtable)
                  : "<unreadable-or-out-of-image>");
    Log(line.str());
}

static bool ProbeClassInfo(
    std::uintptr_t moduleBase,
    std::uint32_t expectedClassId,
    const char* className)
{
    const auto getClassInfo =
        tmf::api::CMwNodApi::resolve_CMwNod_StaticGetClassInfo_00523D50(
            moduleBase
        );

    if (!getClassInfo)
    {
        Log(
            std::string("REFUSED: CMwNod::StaticGetClassInfo resolver unavailable for ")
            + className
        );
        return false;
    }

    const auto classInfo =
        getClassInfo(expectedClassId);

    if (!classInfo)
    {
        Log(
            std::string("REFUSED: class descriptor unavailable for ")
            + className
        );
        return false;
    }

    // These descriptor offsets are independently established by the exact
    // target CMwClassInfo constructor. This probe reads metadata only; it
    // never calls a factory, constructor, registry mutator, or destructor.
    constexpr std::uintptr_t kIdOffset = 0x04;
    constexpr std::uintptr_t kParentOffset = 0x08;
    constexpr std::uintptr_t kNameOffset = 0x14;
    constexpr std::uintptr_t kParamInfosOffset = 0x20;
    constexpr std::uintptr_t kParamCountOffset = 0x24;

    const auto address =
        reinterpret_cast<std::uintptr_t>(classInfo);

    std::uint32_t observedClassId = 0;

    if (!ReadU32(address + kIdOffset, observedClassId)
        || observedClassId != expectedClassId)
    {
        Log(
            std::string("REFUSED: class descriptor ID mismatch for ")
            + className
        );
        return false;
    }

    std::uintptr_t parent = 0;
    std::uintptr_t name = 0;
    std::uintptr_t paramInfos = 0;
    std::uint32_t paramCount = 0;

    const bool parentReadable =
        ReadPointer(address + kParentOffset, parent);
    const bool nameReadable =
        ReadPointer(address + kNameOffset, name);
    const bool paramInfosReadable =
        ReadPointer(address + kParamInfosOffset, paramInfos);
    const bool paramCountReadable =
        ReadU32(address + kParamCountOffset, paramCount);

    Log(
        std::string("PASS: class descriptor resolved for ")
        + className
        + " -> "
        + Hex(address)
    );

    Log(
        "class descriptor ID -> "
        + Hex(observedClassId)
    );

    Log(
        "class descriptor parent -> "
        + (parentReadable ? Hex(parent) : std::string{"unreadable"})
    );

    std::uint32_t parentClassId = 0;
    const bool parentClassIdReadable =
        parentReadable
        && ReadU32(parent + kIdOffset, parentClassId);

    Log(
        "class descriptor parent ID -> "
        + (parentClassIdReadable
            ? Hex(parentClassId)
            : std::string{"unreadable"})
    );

    std::string observedName;
    const bool observedNameReadable =
        nameReadable
        && ReadCString(name, observedName);

    if (!observedNameReadable || observedName != className)
    {
        Log(
            std::string("REFUSED: class descriptor name mismatch for ")
            + className
            + " (observed: "
            + (observedNameReadable
                ? observedName
                : std::string{"unreadable"})
            + ")"
        );
        return false;
    }

    Log(
        "class descriptor name pointer -> "
        + (nameReadable ? Hex(name) : std::string{"unreadable"})
    );

    Log("class descriptor name -> " + observedName);

    Log(
        "class descriptor param infos -> "
        + (paramInfosReadable ? Hex(paramInfos) : std::string{"unreadable"})
    );

    Log(
        "class descriptor param count -> "
        + (paramCountReadable
            ? std::to_string(paramCount)
            : std::string{"unreadable"})
    );

    return true;
}

static void __fastcall HookPlayPlugSoundSpatial(
    tmf::CAudioPort* audioPort,
    void*,
    tmf::CPlugSound* plugSound,
    const tmf::GmVec3& position,
    const tmf::GmVec3& velocity,
    int value)
{
    if (!g_originalPlayPlugSoundSpatial)
        return;

    if (audioPort != g_expectedAudioPort)
    {
        g_originalPlayPlugSoundSpatial(
            audioPort,
            plugSound,
            position,
            velocity,
            value
        );

        return;
    }

    InterlockedExchange(&g_seen, 1);

    if (InterlockedCompareExchange(
            &g_armed,
            0,
            0) != 0 &&
        InterlockedCompareExchange(
            &g_suppressed,
            1,
            0) == 0)
    {
        Log(
            "real CAudioPort -> " +
            Hex(reinterpret_cast<std::uintptr_t>(audioPort))
        );

        Log(
            "real CPlugSound -> " +
            Hex(reinterpret_cast<std::uintptr_t>(plugSound))
        );

        std::ostringstream pos;

        pos
            << "position -> { "
            << position.component0 << ", "
            << position.component1 << ", "
            << position.component2 << " }";

        Log(pos.str());

        std::ostringstream vel;

        vel
            << "velocity -> { "
            << velocity.component0 << ", "
            << velocity.component1 << ", "
            << velocity.component2 << " }";

        Log(vel.str());

        Log(
            "integer argument -> " +
            std::to_string(value)
        );

        Log("TEST: suppressed this one spatial PlayPlugSound call.");
        return;
    }

    g_originalPlayPlugSoundSpatial(
        audioPort,
        plugSound,
        position,
        velocity,
        value
    );
}

static void __fastcall HookUpdateVehicleStateFromInputs(
    tmf::CTrackManiaControlPlayerInput* input,
    void*,
    const tmf::OpaqueNested_CTrackManiaControlPlayerInput__SRaceInputs& raceInputs)
{
    const auto object = reinterpret_cast<std::uintptr_t>(input);
    std::uintptr_t vtable = 0;

    if (object)
        ReadPointer(object, vtable);

    if (object) {
        InterlockedCompareExchange(
            &g_inputFirstObject,
            static_cast<LONG>(object),
            0
        );
        InterlockedExchange(
            &g_inputLastObject,
            static_cast<LONG>(object)
        );
    }

    if (vtable) {
        InterlockedCompareExchange(
            &g_inputFirstVtable,
            static_cast<LONG>(vtable),
            0
        );
        InterlockedExchange(
            &g_inputLastVtable,
            static_cast<LONG>(vtable)
        );
    }

    if (g_getConstructionPlayer) {
        const auto player = g_getConstructionPlayer(input);
        const auto playerAddress = reinterpret_cast<std::uintptr_t>(player);
        std::uintptr_t playerVtable = 0;

        if (playerAddress)
            ReadPointer(playerAddress, playerVtable);

        if (playerAddress) {
            InterlockedCompareExchange(
                &g_inputFirstPlayer,
                static_cast<LONG>(playerAddress),
                0
            );
            InterlockedExchange(
                &g_inputLastPlayer,
                static_cast<LONG>(playerAddress)
            );
        }

        if (playerVtable) {
            InterlockedCompareExchange(
                &g_inputFirstPlayerVtable,
                static_cast<LONG>(playerVtable),
                0
            );
            InterlockedExchange(
                &g_inputLastPlayerVtable,
                static_cast<LONG>(playerVtable)
            );
        }
    }

    if (g_runtime) {
        const auto playground = reinterpret_cast<std::uintptr_t>(
            g_runtime->playground()
        );
        std::uintptr_t playgroundVtable = 0;

        if (playground)
            ReadPointer(playground, playgroundVtable);

        if (playground) {
            InterlockedCompareExchange(
                &g_inputFirstPlayground,
                static_cast<LONG>(playground),
                0
            );
            InterlockedExchange(
                &g_inputLastPlayground,
                static_cast<LONG>(playground)
            );
        }

        if (playgroundVtable) {
            InterlockedCompareExchange(
                &g_inputFirstPlaygroundVtable,
                static_cast<LONG>(playgroundVtable),
                0
            );
            InterlockedExchange(
                &g_inputLastPlaygroundVtable,
                static_cast<LONG>(playgroundVtable)
            );
        }
    }

    InterlockedCompareExchange(
        &g_inputThreadId,
        static_cast<LONG>(GetCurrentThreadId()),
        0
    );
    InterlockedIncrement(&g_inputCallCount);

    if (g_originalUpdateVehicleStateFromInputs)
        g_originalUpdateVehicleStateFromInputs(input, raceInputs);
}

static DWORD WINAPI ProbeThread(void*)
{
    if (!InitializeLogPath())
        return 0;

    DeleteFileW(g_logPath.c_str());

    Log("TMForever runtime probe");
    Log("=======================");

    const auto runtime = tmf::runtime::Context::current_process();
    if (!runtime)
    {
        Log(
            "REFUSED: runtime bootstrap failed: "
            + std::string(tmf::runtime::status_name(runtime.status()))
        );
        return 0;
    }

    if (!runtime.file_sha256_verified())
    {
        Log("REFUSED: runtime bootstrap did not verify the executable SHA-256.");
        return 0;
    }

    const auto moduleBase = runtime.module_base();
    g_runtime = &runtime;

    Log("module base -> " + Hex(moduleBase));

    Log("PASS: runtime bootstrap validated exact build and SHA-256.");

    if (!ProbeClassInfo(
            moduleBase,
            tmf::native::CMwNod_MwClassId,
            "CMwNod"))
    {
        return 0;
    }

    if (!ProbeClassInfo(
            moduleBase,
            tmf::native::CGameApp_MwClassId,
            "CGameApp"))
    {
        return 0;
    }

    if (!ProbeClassInfo(
            moduleBase,
            tmf::native::CAudioPort_MwClassId,
            "CAudioPort"))
    {
        return 0;
    }

    if (!ProbeClassInfo(
            moduleBase,
            tmf::native::CTrackManiaEditor_MwClassId,
            "CTrackManiaEditor"))
    {
        return 0;
    }

    if (!ProbeClassInfo(
            moduleBase,
            tmf::native::CTrackManiaControlPlayerInput_MwClassId,
            "CTrackManiaControlPlayerInput"))
    {
        return 0;
    }

    if (InputHookOnlyRequested())
    {
        Log("TEST: input call-site observation mode active.");
        Log("TEST: drive in an active race during the next 60 seconds.");

        const auto targetFunction =
            tmf::api::CTrackManiaControlPlayerInputApi::
                resolve_CTrackManiaControlPlayerInput_UpdateVehicleStateFromInputs_000FE7E0(
                    moduleBase
                );
        void* target = reinterpret_cast<void*>(targetFunction);

        if (!target)
        {
            Log("REFUSED: input update target could not be resolved.");
            return 0;
        }

        if (MH_Initialize() != MH_OK)
        {
            Log("REFUSED: MH_Initialize failed.");
            return 0;
        }

        g_getConstructionPlayer =
            tmf::api::CTrackManiaControlPlayerInputApi::
                resolve_CTrackManiaControlPlayerInput_GetConstructionPlayer_000FE3D0(
                    moduleBase
                );

        if (!g_getConstructionPlayer)
        {
            Log("REFUSED: construction-player getter could not be resolved.");
            MH_Uninitialize();
            return 0;
        }

        if (MH_CreateHook(
                target,
                &HookUpdateVehicleStateFromInputs,
                reinterpret_cast<void**>(
                    &g_originalUpdateVehicleStateFromInputs)) != MH_OK)
        {
            Log("REFUSED: input MH_CreateHook failed.");
            MH_Uninitialize();
            return 0;
        }

        if (MH_EnableHook(target) != MH_OK)
        {
            Log("REFUSED: input MH_EnableHook failed.");
            MH_RemoveHook(target);
            MH_Uninitialize();
            return 0;
        }

        Log("PASS: input update observation hook installed.");

        for (std::size_t sample = 0; sample != 120; ++sample)
            Sleep(500);

        MH_DisableHook(target);
        MH_RemoveHook(target);
        MH_Uninitialize();

        const auto callCount = InterlockedCompareExchange(
            &g_inputCallCount,
            0,
            0
        );
        const auto firstObject = static_cast<std::uintptr_t>(
            static_cast<std::uint32_t>(InterlockedCompareExchange(
                &g_inputFirstObject,
                0,
                0
            ))
        );
        const auto lastObject = static_cast<std::uintptr_t>(
            static_cast<std::uint32_t>(InterlockedCompareExchange(
                &g_inputLastObject,
                0,
                0
            ))
        );
        const auto firstVtable = static_cast<std::uintptr_t>(
            static_cast<std::uint32_t>(InterlockedCompareExchange(
                &g_inputFirstVtable,
                0,
                0
            ))
        );
        const auto lastVtable = static_cast<std::uintptr_t>(
            static_cast<std::uint32_t>(InterlockedCompareExchange(
                &g_inputLastVtable,
                0,
                0
            ))
        );
        const auto firstPlayer = static_cast<std::uintptr_t>(
            static_cast<std::uint32_t>(InterlockedCompareExchange(
                &g_inputFirstPlayer,
                0,
                0
            ))
        );
        const auto lastPlayer = static_cast<std::uintptr_t>(
            static_cast<std::uint32_t>(InterlockedCompareExchange(
                &g_inputLastPlayer,
                0,
                0
            ))
        );
        const auto firstPlayerVtable = static_cast<std::uintptr_t>(
            static_cast<std::uint32_t>(InterlockedCompareExchange(
                &g_inputFirstPlayerVtable,
                0,
                0
            ))
        );
        const auto lastPlayerVtable = static_cast<std::uintptr_t>(
            static_cast<std::uint32_t>(InterlockedCompareExchange(
                &g_inputLastPlayerVtable,
                0,
                0
            ))
        );
        const auto firstPlayground = static_cast<std::uintptr_t>(
            static_cast<std::uint32_t>(InterlockedCompareExchange(
                &g_inputFirstPlayground,
                0,
                0
            ))
        );
        const auto lastPlayground = static_cast<std::uintptr_t>(
            static_cast<std::uint32_t>(InterlockedCompareExchange(
                &g_inputLastPlayground,
                0,
                0
            ))
        );
        const auto firstPlaygroundVtable = static_cast<std::uintptr_t>(
            static_cast<std::uint32_t>(InterlockedCompareExchange(
                &g_inputFirstPlaygroundVtable,
                0,
                0
            ))
        );
        const auto lastPlaygroundVtable = static_cast<std::uintptr_t>(
            static_cast<std::uint32_t>(InterlockedCompareExchange(
                &g_inputLastPlaygroundVtable,
                0,
                0
            ))
        );
        const auto threadId = InterlockedCompareExchange(
            &g_inputThreadId,
            0,
            0
        );

        Log("input_hook_calls -> " + std::to_string(callCount));
        Log("input_first_object -> " + Hex(firstObject));
        Log("input_last_object -> " + Hex(lastObject));
        Log("input_first_vtable -> " + Hex(firstVtable));
        Log("input_last_vtable -> " + Hex(lastVtable));
        Log("input_first_player -> " + Hex(firstPlayer));
        Log("input_last_player -> " + Hex(lastPlayer));
        Log("input_first_player_vtable -> " + Hex(firstPlayerVtable));
        Log("input_last_player_vtable -> " + Hex(lastPlayerVtable));
        Log("input_first_playground -> " + Hex(firstPlayground));
        Log("input_last_playground -> " + Hex(lastPlayground));
        Log("input_first_playground_vtable -> " + Hex(firstPlaygroundVtable));
        Log("input_last_playground_vtable -> " + Hex(lastPlaygroundVtable));
        Log("input_thread_id -> " + std::to_string(threadId));

        if (firstVtable >= moduleBase
            && firstVtable - moduleBase < runtime.identity().image_size)
            Log("input_first_vtable_rva -> " + Hex(firstVtable - moduleBase));
        if (lastVtable >= moduleBase
            && lastVtable - moduleBase < runtime.identity().image_size)
            Log("input_last_vtable_rva -> " + Hex(lastVtable - moduleBase));
        if (firstPlayerVtable >= moduleBase
            && firstPlayerVtable - moduleBase < runtime.identity().image_size) {
            Log(
                "input_first_player_vtable_rva -> "
                + Hex(firstPlayerVtable - moduleBase)
            );
            if (const auto name = KnownPlayerVtable(moduleBase, firstPlayerVtable))
                Log(std::string("input_first_player_vtable_name -> ") + name);
        }
        if (lastPlayerVtable >= moduleBase
            && lastPlayerVtable - moduleBase < runtime.identity().image_size) {
            Log(
                "input_last_player_vtable_rva -> "
                + Hex(lastPlayerVtable - moduleBase)
            );
            if (const auto name = KnownPlayerVtable(moduleBase, lastPlayerVtable))
                Log(std::string("input_last_player_vtable_name -> ") + name);
        }
        if (firstPlaygroundVtable >= moduleBase
            && firstPlaygroundVtable - moduleBase < runtime.identity().image_size) {
            Log(
                "input_first_playground_vtable_rva -> "
                + Hex(firstPlaygroundVtable - moduleBase)
            );
            if (const auto name = KnownPlaygroundVtable(
                    moduleBase,
                    firstPlaygroundVtable))
                Log(std::string("input_first_playground_vtable_name -> ") + name);
        }
        if (lastPlaygroundVtable >= moduleBase
            && lastPlaygroundVtable - moduleBase < runtime.identity().image_size) {
            Log(
                "input_last_playground_vtable_rva -> "
                + Hex(lastPlaygroundVtable - moduleBase)
            );
            if (const auto name = KnownPlaygroundVtable(
                    moduleBase,
                    lastPlaygroundVtable))
                Log(std::string("input_last_playground_vtable_name -> ") + name);
        }

        Log("PASS: bounded read-only input call-site observation complete.");
        return 0;
    }

    if (MetadataOnlyRequested())
    {
        if (!ProbeParamInfo(
                moduleBase,
                0x03005003u,
                0x00000005u,
                0x00000068,
                "AudioPort"))
        {
            return 0;
        }

        if (!ProbeParamInfo(
                moduleBase,
                0x03005000u,
                0x00000000u,
                -1,
                "Start"))
        {
            return 0;
        }

        if (!ProbeParamInfo(
                moduleBase,
                0x0300500Cu,
                0x00000041u,
                -1,
                "ShowMenu"))
        {
            return 0;
        }

        if (!ProbeClassRegistry(moduleBase))
            return 0;

        if (!ProbeLiveGameApp(runtime))
            return 0;

        Log("PASS: metadata-only runtime probe complete.");
        return 0;
    }

    if (LifecycleOnlyRequested())
    {
        Log("PASS: lifecycle sampler bootstrap complete.");

        if (LifecycleMarkedRequested())
        {
            Log("TEST: marked lifecycle mode active.");
            Log("TEST: CTRL+SHIFT+F6 = main_menu.");
            Log("TEST: CTRL+SHIFT+F7 = editor.");
            Log("TEST: CTRL+SHIFT+F8 = race.");
            Log("TEST: CTRL+SHIFT+F9 = stop probe.");
            Log("TEST: marker keys are sampled every 25 ms.");

            const char* state = "unmarked";
            bool previousMarker = false;

            for (std::size_t sample = 0; sample != 240; ++sample)
            {
                bool stopRequested = false;

                for (std::size_t poll = 0; poll != 20; ++poll)
                {
                    const bool menu = LifecycleKeyDown(VK_F6);
                    const bool editor = LifecycleKeyDown(VK_F7);
                    const bool race = LifecycleKeyDown(VK_F8);
                    const bool stop = LifecycleKeyDown(VK_F9);
                    const bool marker = menu || editor || race;

                    if (marker && !previousMarker)
                    {
                        if (menu)
                            state = "main_menu";
                        else if (editor)
                            state = "editor";
                        else if (race)
                            state = "race";

                        Log(std::string("MARK state=") + state);
                    }

                    if (stop)
                    {
                        Log("STOP probe requested.");
                        stopRequested = true;
                        break;
                    }

                    previousMarker = marker;
                    Sleep(25);
                }

                if (stopRequested)
                    break;

                LogLifecycleSnapshot(runtime, sample, state);
            }

            Log("PASS: marked read-only lifecycle sampler complete.");
            return 0;
        }

        Log("TEST: keep the game in each requested state during the samples.");

        for (std::size_t sample = 0; sample != 120; ++sample)
        {
            LogLifecycleSnapshot(runtime, sample, "scheduled");
            Sleep(500);
        }

        Log("PASS: read-only lifecycle sampler complete.");
        return 0;
    }

    const auto gameObject = runtime.game_app();
    if (!gameObject)
    {
        Log("REFUSED: CGameApp unavailable.");
        return 0;
    }

    const auto game = reinterpret_cast<std::uintptr_t>(gameObject);

    Log("CGameApp -> " + Hex(game));

    const auto audioObject = runtime.audio_port();
    const auto audioPort =
        reinterpret_cast<std::uintptr_t>(audioObject);

    if (!audioObject)
    {
        Log("REFUSED: CGameApp::AudioPort unavailable.");
        return 0;
    }

    Log("AudioPort -> " + Hex(audioPort));
    Log("PASS: live CGameApp::AudioPort recovered through +0x68.");

    g_expectedAudioPort = audioObject;

    const auto target_function =
        tmf::api::CAudioPortApi::resolve_CAudioPort_PlayPlugSound_0039EA20(
            moduleBase
        );
    void* target = reinterpret_cast<void*>(target_function);

    Log(
        "spatial PlayPlugSound target -> " +
        Hex(reinterpret_cast<std::uintptr_t>(target))
    );

    if (MH_Initialize() != MH_OK)
    {
        Log("REFUSED: MH_Initialize failed.");
        return 0;
    }

    if (MH_CreateHook(
            target,
            &HookPlayPlugSoundSpatial,
            reinterpret_cast<void**>(
                &g_originalPlayPlugSoundSpatial)) != MH_OK)
    {
        Log("REFUSED: MH_CreateHook failed.");
        MH_Uninitialize();
        return 0;
    }

    if (MH_EnableHook(target) != MH_OK)
    {
        Log("REFUSED: MH_EnableHook failed.");
        MH_RemoveHook(target);
        MH_Uninitialize();
        return 0;
    }

    Log("PASS: spatial PlayPlugSound hook installed.");
    Log("");
    Log("TEST: open/stay in horn selection.");
    Log("TEST: press CTRL+F10 to arm.");
    Log("TEST: then select ONE horn.");
    Log("TEST: that next spatial sound should be silent.");

    bool previousF10 = false;
    DWORD waited = 0;

    while (waited < 60000)
    {
        const bool ctrlDown =
            (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

        const bool f10Down =
            (GetAsyncKeyState(VK_F10) & 0x8000) != 0;

        if (ctrlDown &&
            f10Down &&
            !previousF10 &&
            InterlockedCompareExchange(
                &g_armed,
                1,
                0) == 0)
        {
            Log("");
            Log("ARMED: CTRL+F10 detected.");
            Log("Select one horn now.");
        }

        previousF10 = f10Down;

        if (InterlockedCompareExchange(
                &g_suppressed,
                0,
                0) != 0)
        {
            Sleep(500);
            break;
        }

        Sleep(20);
        waited += 20;
    }

    MH_DisableHook(target);
    MH_RemoveHook(target);
    MH_Uninitialize();

    Log("");
    Log("hook removed.");

    if (!InterlockedCompareExchange(
            &g_armed,
            0,
            0))
    {
        Log("FAIL: CTRL+F10 was never detected.");
        return 0;
    }

    if (!InterlockedCompareExchange(
            &g_suppressed,
            0,
            0))
    {
        Log("FAIL: no armed spatial PlayPlugSound call was suppressed.");
        return 0;
    }

    Log("PASS: real spatial CAudioPort::PlayPlugSound call intercepted.");
    Log("PASS: real CPlugSound supplied by TMForever.");
    Log("PASS: real GmVec3 references received.");
    Log("PASS: exactly one armed native sound call suppressed.");
    Log("PASS: bounded audio hook removed.");

    return 0;
}

BOOL WINAPI DllMain(
    HINSTANCE instance,
    DWORD reason,
    LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);

        HANDLE thread = CreateThread(
            nullptr,
            0,
            ProbeThread,
            nullptr,
            0,
            nullptr
        );

        if (thread)
            CloseHandle(thread);
    }

    return TRUE;
}
