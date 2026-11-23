// OpenRFG (VOM, new code): the PhysX backend (physx_backend.cpp). Built only when CMake is given the PhysX SDK
// (OPENRFG_PHYSX_INSTALL); backends.cpp then calls it.
#pragma once

// Installs rfgphys, once, on a worker thread (it reads OpenRFG.pdb for seconds): its hooks and modules, and with launch
// the PhysX core. false: it did not install (backends\rfgphys's log says why).
bool openrfg_physx_install(bool launch);

// After the install, on the game's simulation thread with no level loaded (the main menu). launch: starts the PhysX
// core, or restarts one that stopped, failed or was lost (false: it cannot start now). stop: asks it to stop and returns
// at once; the core releases PhysX on its own thread.
bool openrfg_physx_launch();
void openrfg_physx_stop();

// The core's state (pxbridge::State): 0 off, 1 starting, 2 running, 3 failed, 4 lost, 5 stopping.
enum
{
    OPENRFG_PHYSX_OFF = 0,
    OPENRFG_PHYSX_STARTING = 1,
    OPENRFG_PHYSX_RUNNING = 2,
    OPENRFG_PHYSX_FAILED = 3,
    OPENRFG_PHYSX_LOST = 4,
    OPENRFG_PHYSX_STOPPING = 5,
};
int openrfg_physx_state();
