// OpenRFG: the Dynamic Lights video option (no rfg.exe counterpart).
// Implementation: volition/render_lib/code/rl_scene_renderer_lighting.cpp
//
// The secondary lights (zone lights such as street lights, effect lights such as fire and explosions, and the vehicle
// headlight spots of vehicle_effects.cpp) are drawn by render_lighting_passes, one additive pass per light. The option
// level sets a budget: how many of them are drawn (the nearest ones) and how far from the camera, with a fade near that
// distance so lights do not pop. Level 0 turns the secondary lights off (RL_enable_secondary_lights).
#pragma once

#include "volition/vlib/code/math/vector3.h"

class rl_light;

// The option level of the frame: 0 off, 1 low, 2 medium, 3 high, 4 very high. Set by rl_dynamic_lights_apply_level.
extern int Rl_dynamic_lights_level;
// The number of secondary lights drawn at most (the nearest ones).
extern int Rl_dynamic_lights_max;
// How far from the camera (to the edge of a light's range, meters) secondary lights are drawn.
extern float Rl_dynamic_lights_distance;

// Sets the budget of a level (and turns the secondary lights on or off).
void rl_dynamic_lights_apply_level(int level);
// How much of a light is drawn from the camera position: 1 inside the distance, fading to 0 at its end. Directional
// lights are always 1.
float rl_dynamic_lights_fade(const rl_light& light, const vector& camera_pos);

// OpenRFG diagnostics: per-frame counts, logged to openrfg_crash.txt every 120 frames by rl_dynamic_lights_debug_report.
extern int Rl_dynamic_lights_dbg_found; // lights in the visible light sectors (before any test)
extern int Rl_dynamic_lights_dbg_in_range; // of those, inside the Dynamic Lights distance
extern int Rl_dynamic_lights_dbg_visible; // visible lights before the budget
extern int Rl_dynamic_lights_dbg_drawn; // light passes drawn
extern int Rl_dynamic_lights_dbg_headlights; // vehicle headlight spots lit (vehicle_effects.cpp)
extern int Rl_dynamic_lights_dbg_night; // vehicle_effects_headlights_should_be_on
extern int Rl_dynamic_lights_dbg_fx_updates; // effect updates (visible effects) since the last report
extern int Rl_dynamic_lights_dbg_fx_light_updates; // of those, effects with lights of their own
extern int Rl_dynamic_lights_dbg_vehicle_updates; // vehicle light updates since the last report
extern int Rl_dynamic_lights_dbg_vehicle_lit; // of those, with headlights lit (engine running, headlights on)
// scene_lights: every rl_light of the scene, shown or hidden (rl_scene::get_light_count).
void rl_dynamic_lights_debug_report(int scene_lights);
// Counts rendered frames (bumped by rl_dynamic_lights_debug_report, once per frame); effects use it to share per-frame state.
extern unsigned int Rl_dynamic_lights_frame;
// Called by render_lighting_passes for each light drawn.
void rl_dynamic_lights_debug_light(const rl_light& light, unsigned int vis_flags);
