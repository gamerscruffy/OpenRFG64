// OpenRFG (VOM, new code): KEEP DEBRIS in the game's source, for the PhysX backend (backends.h). Ported from
// OpenRFG64-memtest's port of rfgphys's keep_debris (memtest_debris.cpp). While PhysX runs with KEEP DEBRIS on, in
// single player:
// - the load governor deletes nothing (rfgl_maybe_delete_objects), and a shatter keeps its pieces while half the reserve
//   of object handles is free in the fullest world zone (rfgl_desired_values);
// - pieces do not fade (object_mover_mark_for_fade) except in the game's own cleanups (a fade scope: Havok out of
//   memory, AI vehicles clearing their way), and none are vaporized (rfg_dyn_maybe_delete);
// - at each frame's end a zone short of its reserve of free object handles (7,280 a zone) recycles its farthest debris.
// So a shattered building's pieces live the 2 s they wait in Havok before PhysX takes them (in PhysX they hold no game
// handles). Compiled into the rfg library (it uses the game's types); the switch comes from backends.cpp.
#pragma once

bool openrfg_keep_debris_keeping(); // the governor stands down, fades and vaporizing are refused
bool openrfg_keep_debris_fade_allowed(); // object_mover_mark_for_fade
int openrfg_keep_debris_shatter_deletes(int pieces, int deletes); // rfgl_desired_values: the deletes that remain
bool openrfg_keep_debris_refuse_vaporize(); // rfg_dyn_maybe_delete
void openrfg_keep_debris_sim_frame(); // the end of gamestate_gp_simulate_frame (sim thread, gameplay)

// The game's own cleanups fade kept debris too: one of these on the stack lets the fades through.
struct openrfg_keep_debris_fade_scope
{
    openrfg_keep_debris_fade_scope();
    ~openrfg_keep_debris_fade_scope();
};
