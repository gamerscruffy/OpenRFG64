// OpenRFG (VOM, new code): the PhysX backend (physx_backend.h).
//
// rfgphys, the PhysX mod's code (external/rfgphys, a git subtree of its repository), is built into the game. Its start
// is the plugin's own (core/startup.cpp), run from backends\ next to OpenRFG.exe: rfgphys.ini there, and rfgphysx\ with
// the PhysX core (rfgphysx_core.dll: the PhysX host's code, which steps PhysX on a thread of the game) and PhysX's GPU
// module (PhysXGpu_64.dll). The plugin's game code reads the game the way it does in OpenRFG64 (names from
// OpenRFG.pdb), and attaches to it through seams in the game's source (backends/seams.h): rfgphys's hooks::create asks
// this binding first, and only a function without a seam gets a MinHook detour (the record/replay harness's).

#include "physx_backend.h"

#include <windows.h>

#include <string>

#include "../seams.h"
#include "core/log.h"
#include "core/startup.h"
#include "game/rfg.h"
#include "hooks/hooks.h"
#include "openrfg64_column.h"
#include "physx/bridge.h"

namespace
{
    int g_seamed = 0;

    // rfgphys's hooks::create: the hook on the seam of the function its names table resolved to `rva`.
    bool seam_create(uint32_t rva, void* detour, void** original, const char* name)
    {
        const openrfg64::Name* symbol = rfg::symbol_of(rva);
        openrfg_seam* seam = symbol != nullptr ? openrfg_seam_find(symbol->name, symbol->signature) : nullptr;
        if (seam == nullptr)
        {
            return false; // rfgphys's hooks say what it does instead
        }
        openrfg_seam_attach(seam, detour, original);
        ++g_seamed;
        return true;
    }

    // Only handlers reached through seams ask (rfgphys's frametime, zone check and impulse handlers): while a seam's
    // handler runs, its call site; otherwise the handler's own return address (a detour's).
    const void* seam_caller(const void* return_address)
    {
        return openrfg_seam_caller != nullptr ? openrfg_seam_caller : return_address;
    }
}

bool openrfg_physx_install(bool launch)
{
    hooks::set_binding({&seam_create, &openrfg_seams_enable, &seam_caller});
    wchar_t exe[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe, n);
    dir = dir.substr(0, dir.find_last_of(L"\\/")) + L"\\backends";
    startup::run(dir, launch);
    LOG("OpenRFG64 VOM: %d hooks on seams in the game's source", g_seamed);
    // With launch the core is starting now (or did not load: openrfg_physx_state says failed); whether PhysX comes up
    // is told later in rfgphys.log.
    return pxbridge::installed();
}

bool openrfg_physx_launch()
{
    return pxbridge::start();
}

void openrfg_physx_stop()
{
    pxbridge::stop();
}

int openrfg_physx_state()
{
    return (int)pxbridge::state();
}
