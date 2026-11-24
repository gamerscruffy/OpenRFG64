// OpenRFG (VOM, new code): the path tracing backend (rt_backend.cpp). Built when CMake builds external/rfgrt
// (rt/CMakeLists.txt); backends.cpp then calls it.
#pragma once

// Loads rfgrt (backends\rfgrt\rfgrt.asi) and starts it as an ASI loader would (InitializeASI), on the calling thread.
// Call it before the game creates its shaders and meshes (rl_init): the path tracer patches the shaders and mirrors the
// meshes as they are created. Its start reads OpenRFG.pdb for seconds. false: it did not start (the log under
// backends\rfgrt says why).
bool openrfg_rt_start();
