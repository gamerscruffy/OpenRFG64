// OpenRFG (VOM, new code): the path tracing backend (rt_backend.h).
//
// rfgrt, the path tracing mod's code (external/rfgrt, a git subtree of its repository), is built as the mod builds its
// x64 plugin: a DLL with the ray tracing core inside, backends\rfgrt\rfgrt.asi next to OpenRFG.exe, with its settings
// (rfgrt.ini) and DLSS's runtime. It attaches to the game the way it does in OpenRFG64 (names from OpenRFG.pdb,
// MinHook); seams in the game's source come later.

#include "backends/rt/rt_backend.h"

#include <windows.h>

#include <string>

bool openrfg_rt_start()
{
    wchar_t exe[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe, n);
    dir = dir.substr(0, dir.find_last_of(L"\\/")) + L"\\backends\\rfgrt";
    HMODULE module = LoadLibraryExW((dir + L"\\rfgrt.asi").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module == nullptr)
    {
        return false;
    }
    const auto initialize = reinterpret_cast<void (*)()>(reinterpret_cast<void*>(GetProcAddress(module, "InitializeASI")));
    const auto state = reinterpret_cast<int (*)()>(reinterpret_cast<void*>(GetProcAddress(module, "rfgrt_state")));
    if (initialize == nullptr || state == nullptr)
    {
        return false;
    }
    initialize();
    return state() == 2;
}
