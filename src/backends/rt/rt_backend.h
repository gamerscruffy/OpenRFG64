// OpenRFG (VOM, new code): the path tracing backend (rt_backend.cpp). Built when CMake builds external/rfgrt
// (rt/CMakeLists.txt); backends.cpp then calls it.
#pragma once

// Loads rfgrt (backends\rfgrt\rfgrt.asi) and starts it as an ASI loader would (InitializeASI), on the calling thread,
// with path tracing on or off (`on`): it stays loaded either way, and openrfg_rt_set_active switches it later. Call it
// before the game creates its shaders and meshes (rl_init): the path tracer patches the shaders and mirrors the meshes
// as they are created (and keeps them, for a later start). Its start reads OpenRFG.pdb for seconds. false: it did not
// start (the log under backends\rfgrt says why).
bool openrfg_rt_start(bool on);
// Path tracing on or off, where no level is loaded (the main menu): off, the game's own rendering and the core
// stopped at once; on, the core starts on a thread of the plugin's. Returns whether path tracing is on.
bool openrfg_rt_set_active(bool on);
