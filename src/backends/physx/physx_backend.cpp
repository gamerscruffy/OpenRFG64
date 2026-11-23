// OpenRFG (VOM, new code): the PhysX backend (physx_backend.h).
//
// rfgphys, the PhysX mod's code (external/rfgphys, a git subtree of its repository), is built into the game. Its start
// is the plugin's own (core/startup.cpp), run from backends\ next to OpenRFG.exe: rfgphys.ini there, and rfgphysx\ with
// the PhysX core (rfgphysx_core.dll: the PhysX host's code, which steps PhysX on a thread of the game) and PhysX's GPU
// module (PhysXGpu_64.dll). The plugin's game code attaches to the game the way it does in OpenRFG64 (names from
// OpenRFG.pdb, MinHook); seams in the game's source come later.

#include "physx_backend.h"

#include <windows.h>

#include <string>

#include "core/startup.h"
#include "physx/bridge.h"

bool openrfg_physx_install(bool launch)
{
    wchar_t exe[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe, n);
    dir = dir.substr(0, dir.find_last_of(L"\\/")) + L"\\backends";
    startup::run(dir, launch);
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
