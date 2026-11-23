// OpenRFG (VOM, new code): the PhysX backend (physx_backend.cpp). Built only when CMake is given the PhysX SDK
// (OPENRFG_PHYSX_INSTALL); backends.cpp then calls it.
#pragma once

// Starts rfgphys (once): false when it did not start (backends\rfgphys's log says why). Stopping it again within one run
// of the game is not supported yet.
bool openrfg_physx_start();
