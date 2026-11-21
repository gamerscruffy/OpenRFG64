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
