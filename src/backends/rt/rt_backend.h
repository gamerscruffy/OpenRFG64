// OpenRFG (VOM, new code): the path tracing backend (rt_backend.cpp). Built when CMake builds external/rfgrt
// (rt/CMakeLists.txt); backends.cpp then calls it.
#pragma once

// Loads rfgrt (backends\rfgrt\rfgrt.asi) and starts it as an ASI loader would (InitializeASI), on the calling thread,
// with path tracing on or off (`on`): it stays loaded either way, and openrfg_rt_set_active switches it later. Call it
// before the game creates its shaders and meshes (rl_init): the path tracer patches the shaders and mirrors the meshes
// as they are created (and keeps them, for a later start). `dlss_mode`: DLSS's (backends.h openrfg_dlss), in place of
// rfgrt.ini's DLSS.Mode; `frame_generation`: in place of its FrameGeneration.Enabled. false: it did not start (the log
// under backends\rfgrt says why).
bool openrfg_rt_start(bool on, int dlss_mode, bool frame_generation);
// Path tracing on or off, where no level is loaded (the main menu): off, the game's own rendering and the core
// stopped at once; on, the core starts on a thread of the plugin's. Returns whether path tracing is on.
bool openrfg_rt_set_active(bool on);
// DLSS's mode (backends.h openrfg_dlss), anywhere: a running core switches at its next frame, later ones start with it.
void openrfg_rt_set_dlss_mode(int mode);
// DLSS Frame Generation on or off, anywhere: from the next present (the plugin's presenter).
void openrfg_rt_set_frame_generation(bool on);
// OpenRFG64: a glowing thing lights the rays this frame (backends.h openrfg_backends_add_emitter); nothing with a plugin
// without emitters.
void openrfg_rt_add_emitter(const float position[3], const float color[3], float radius, float range, const void* key);
