#include <windows.h>
#include <tlhelp32.h>

#include <iostream>

static DWORD FindProcess(const wchar_t* expectedExePath)
{
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    DWORD result = 0;

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"TmForever.exe") != 0)
                continue;

            HANDLE process = OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION,
                FALSE,
                entry.th32ProcessID
            );

            if (!process)
                continue;

            wchar_t path[32768]{};
            DWORD pathLength = static_cast<DWORD>(std::size(path));

            if (QueryFullProcessImageNameW(process, 0, path, &pathLength)) {
                if (_wcsicmp(path, expectedExePath) == 0) {
                    result = entry.th32ProcessID;
                    CloseHandle(process);
                    break;
                }
            }

            CloseHandle(process);

        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return result;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3)
    {
        std::wcerr
            << L"usage: tmf_probe_injector <TmForever.exe> <probe.dll>\n";
        return 2;
    }

    const wchar_t* expectedExePath = argv[1];
    const wchar_t* dllPath = argv[2];
    const DWORD pid = FindProcess(expectedExePath);

    if (!pid) {
        std::wcerr
            << L"REFUSED: target TmForever.exe is not running.\n"
            << L"Expected: " << expectedExePath << L"\n";
        return 1;
    }

    HANDLE process = OpenProcess(
        PROCESS_CREATE_THREAD |
        PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE |
        PROCESS_VM_READ,
        FALSE,
        pid
    );

    if (!process) {
        std::wcerr << L"OpenProcess failed: " << GetLastError() << L"\n";
        return 1;
    }

    const SIZE_T bytes = (wcslen(dllPath) + 1) * sizeof(wchar_t);

    void* remoteMemory = VirtualAllocEx(
        process,
        nullptr,
        bytes,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );

    if (!remoteMemory) {
        std::wcerr << L"VirtualAllocEx failed: " << GetLastError() << L"\n";
        CloseHandle(process);
        return 1;
    }

    if (!WriteProcessMemory(
            process,
            remoteMemory,
            dllPath,
            bytes,
            nullptr)) {
        std::wcerr << L"WriteProcessMemory failed: " << GetLastError() << L"\n";
        VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(process);
        return 1;
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");

    auto loadLibraryW = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(kernel32, "LoadLibraryW")
    );

    if (!loadLibraryW) {
        std::wcerr << L"Could not resolve LoadLibraryW.\n";
        VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(process);
        return 1;
    }

    HANDLE thread = CreateRemoteThread(
        process,
        nullptr,
        0,
        loadLibraryW,
        remoteMemory,
        0,
        nullptr
    );

    if (!thread) {
        std::wcerr << L"CreateRemoteThread failed: " << GetLastError() << L"\n";
        VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(process);
        return 1;
    }

    WaitForSingleObject(thread, INFINITE);

    DWORD loadResult = 0;
    GetExitCodeThread(thread, &loadResult);

    CloseHandle(thread);
    VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
    CloseHandle(process);

    if (!loadResult) {
        std::wcerr << L"LoadLibraryW failed inside sandbox TMForever.\n";
        return 1;
    }

    std::wcout
        << L"PASS: runtime probe loaded into sandbox PID "
        << pid << L".\n";

    return 0;
}
