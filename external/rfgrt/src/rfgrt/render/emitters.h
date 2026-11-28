#pragma once
// OpenRFG64: the glowing things that light the path tracer's rays (frame_scene.h Emitter): the game's additive particle
// emitters and coronas, pushed from its renderer (rfgrt_add_emitter, while it draws them), and the meshes whose
// material is an emitter (mirror/instances.cpp, from the frame's instances). Each frame's scene gets the brightest
// rfgrt_frame::kMaxEmitters near the camera; the GI samples them (gi_trace.hlsl).
#include <cstdint>

#include "frame_scene.h"

struct Config;

namespace emitters {

void install(const Config& cfg);
bool enabled();
// A glowing mesh this frame (render-offset space): its material's light (textures.cpp: its colour times its strength).
// (texture: its glowing material's diffuse map, the core's id; self_lit: its Self_Illumination; for the log)
void add_mesh(const float position[3], const float color[3], uint32_t texture = 0, float self_lit = 0.0f);
// The frame's emitters into its scene, the camera at `camera` (render-offset space), the render offset `offset`.
void write(rfgrt_frame::SlotHeader* h, const float offset[3], const float camera[3]);
void report(bool at_exit);

}  // namespace emitters
