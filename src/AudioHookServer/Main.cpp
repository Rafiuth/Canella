#include <memory>
#include "Server.h"
#include "Log.h"

#if _WIN32

#include <unordered_set>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <TlHelp32.h>
#include <psapi.h>

DWORD FindMainWindowProcessId(LPCWSTR exeName)
{
    struct Data {
        std::unordered_set<DWORD> ProcIds;
        DWORD MainProcId;
    };
    Data data = {};

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 proc = {};
    proc.dwSize = sizeof(proc);

    if (Process32First(snapshot, &proc)) {
        do {
            if (_wcsicmp(proc.szExeFile, exeName) == 0) {
                data.ProcIds.emplace(proc.th32ProcessID);
            }
        } while (Process32Next(snapshot, &proc));
    }
    CloseHandle(snapshot);

    //Find the main process
    EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
        auto data = (Data*)param;

        DWORD procId;
        GetWindowThreadProcessId(hwnd, &procId);

        if (data->ProcIds.find(procId) != data->ProcIds.end()) {
            data->MainProcId = procId;
            return false;
        }
        return true;
    }, (LPARAM)&data);

    return data.MainProcId;
}

bool IsMainWindowProcess()
{
    bool result = false;

    EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
        DWORD procId;
        GetWindowThreadProcessId(hwnd, &procId);

        if (procId == GetCurrentProcessId()) {
            *(bool*)param = true;
            return false;
        }
        return true;
    }, (LPARAM)&result);

    return result;
}

#if _WINDLL
#include "WinDllProxy.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reasonForCall, LPVOID lpReserved)
{
    if (reasonForCall == DLL_PROCESS_ATTACH) {
        auto init = [](LPVOID param) -> DWORD {
            if (IsMainWindowProcess()) {
                Server::Run(GetCurrentProcessId());
            }
            return 0;
        };
        CreateThread(NULL, 0, init, hModule, 0, NULL);
    }
    else if (reasonForCall == DLL_PROCESS_DETACH) {
        Server::Stop();
    }
    return TRUE;
}
#else

int wmain(int argc, wchar_t* argv[])
{
    int procId = (int)FindMainWindowProcessId(argc > 1 ? argv[1] : L"Spotify.exe");
    if (!procId) {
        LogInfo("Process not found");
        return 1;
    }
    LogInfo("Server will target process {}", procId);
    Server::Run(procId);
    return 0;
}

#endif //_WINDLL

#endif //_WIN32