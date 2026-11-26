// OpenRFG: the Gravity Tether, a physics manipulator in the spirit of a physics gun (not in rfg.exe).
// Implementation: volition/rfg/code/weapons/grav_tether.cpp
//
// F4 hands the player an EDF pistol and turns it into the tether (F4 again turns it back into a pistol); F3 cycles
// Piece mode (pulls single pieces out of buildings) and Chunk mode (tears whole building chunks off their anchors).
// While the tether is out: hold fire to grab what the crosshair is on (humans, vehicles, debris, props, building
// pieces) and carry it at the distance it was grabbed; let go to drop it with the speed it was moving, reload to
// throw it, zoom to freeze it in mid-air (grab it again to unfreeze it).
#pragma once

class weapon;

void grav_tether_do_frame();
// Whether the weapon is the tether: its fire and reload are the tether's controls, not the pistol's.
bool grav_tether_owns_weapon(const weapon* wp);
void grav_tether_level_init();
