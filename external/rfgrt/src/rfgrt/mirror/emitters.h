#pragma once
// The particles that give light (GI.Emitters; research/docs/05-particle-emission-plan.md in OpenRFG64 VOM). The game's
// particle renderer hands the ones it draws (rfgrt_particle_emitters: VOM's, for the screens' particles) while it draws
// the frame's particles, after that frame's scene was taken: they go to the next frame's scene (its slot's emitters,
// frame_scene.h), their bitmaps to the core as the materials' go (mirror/textures.h), and the core's GI rays pick up
// their light where they cross them (rtcore trace_common.hlsli emitters_along).
#include <cstdint>

struct rfgrt_emitter_quad;
namespace rfgrt_frame {
struct SlotHeader;
}

namespace emitters {

// GI.Emitters above 0 with the GI on (main.cpp): quads are taken; off, none are.
void set_enabled(bool on);
bool enabled();
// The render thread, while the game draws its particles: quads in world space.
void add(const rfgrt_emitter_quad* quads, uint32_t count);
// rl_scene_renderer::render's start (render thread, before the frame scene's worker starts): what was drawn since the
// last one becomes this frame's.
void collect();
// The frame scene's worker: this frame's into its slot, in its render-offset space.
void write(rfgrt_frame::SlotHeader* h);
void log_stats();

}  // namespace emitters
