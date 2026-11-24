// OpenRFG (VOM, new code): the path tracing backend (rt_backend.h).
//
// rfgrt, the path tracing mod's code (external/rfgrt, a git subtree of its repository), is built as the mod builds its
// x64 plugin: a DLL with the ray tracing core inside, backends\rfgrt\rfgrt.asi next to OpenRFG.exe, with its settings
// (rfgrt.ini) and DLSS's runtime. It reads the game the way it does in OpenRFG64 (names from OpenRFG.pdb) and attaches
// to it through seams in the game's source (backends/seams.h): before its start, the plugin gets this binding
// (rfgrt_set_binding), and its hooks offer every hook to it first, by the function's name in the plugin's names table;
// only a hook whose function has no seam becomes a MinHook detour (its test harness's). It switches with the game
// running (rfgrt_set_active): loaded at boot either way, it starts with path tracing on or off.

#include "backends/rt/rt_backend.h"

#include <windows.h>

#include <string>

#include "backends/seams.h"
#include "../../../external/rfgrt/src/rfgrt/hooks/binding.h"

namespace
{
    // rfgrt's hooks: the hook on the seam of the function `name` (`params`: the overload's, from the plugin's names
    // table; null for the D3D11 device's methods). `now`: a hook placed once the game has made the object, at the first
    // shader the game loads: rfgphys may still be attaching its own hooks then (its install runs on a worker thread),
    // so only this seam is turned on.
    bool seam_attach(const char* name, const char* params, void* detour, void** original, bool now)
    {
        openrfg_seam* seam = openrfg_seam_find(name, params);
        if (seam == nullptr)
        {
            return false;
        }
        openrfg_seam_attach(seam, detour, original);
        if (now)
        {
            openrfg_seam_enable(seam);
        }
        return true;
    }

    int (*g_set_active)(int) = nullptr;
    void (*g_set_dlss_mode)(int) = nullptr; // (a plugin without DLSS's modes has none)
    void (*g_set_frame_generation)(int) = nullptr; // (nor one without frame generation)
}

bool openrfg_rt_start(bool on, int dlss_mode, bool frame_generation)
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
    const auto set_binding = reinterpret_cast<void (*)(const rfgrt_binding*)>(reinterpret_cast<void*>(GetProcAddress(module, "rfgrt_set_binding")));
    const auto initialize = reinterpret_cast<void (*)()>(reinterpret_cast<void*>(GetProcAddress(module, "InitializeASI")));
    const auto state = reinterpret_cast<int (*)()>(reinterpret_cast<void*>(GetProcAddress(module, "rfgrt_state")));
    g_set_active = reinterpret_cast<int (*)(int)>(reinterpret_cast<void*>(GetProcAddress(module, "rfgrt_set_active")));
    g_set_dlss_mode = reinterpret_cast<void (*)(int)>(reinterpret_cast<void*>(GetProcAddress(module, "rfgrt_set_dlss_mode")));
    g_set_frame_generation = reinterpret_cast<void (*)(int)>(reinterpret_cast<void*>(GetProcAddress(module, "rfgrt_set_frame_generation")));
    if (set_binding == nullptr || initialize == nullptr || state == nullptr || g_set_active == nullptr)
    {
        return false;
    }
    const rfgrt_binding binding = {&seam_attach, &openrfg_seams_enable, &openrfg_seam_call_site};
    set_binding(&binding);
    g_set_active(on ? 1 : 0); // before its start: it switches, and starts on or off
    openrfg_rt_set_dlss_mode(dlss_mode);
    openrfg_rt_set_frame_generation(frame_generation);
    initialize();
    return state() == 2;
}

bool openrfg_rt_set_active(bool on)
{
    return g_set_active != nullptr && g_set_active(on ? 1 : 0) != 0;
}

void openrfg_rt_set_dlss_mode(int mode)
{
    if (g_set_dlss_mode != nullptr)
    {
        g_set_dlss_mode(mode);
    }
}

void openrfg_rt_set_frame_generation(bool on)
{
    if (g_set_frame_generation != nullptr)
    {
        g_set_frame_generation(on ? 1 : 0);
    }
}
