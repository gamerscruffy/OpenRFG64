// OpenRFG: the Gravity Tether (see grav_tether.h). Not in rfg.exe.
//
// The tether is the EDF pistol while the tether is switched on (F4): control_gameplay skips the pistol's fire and
// reload for it (grav_tether_owns_weapon), and grav_tether_do_frame reads the buttons instead. What it holds is moved
// by setting velocities towards the hold point in front of the camera, so it still collides on the way:
// - bodies (vehicles, debris, props, loose building pieces): their Havok body's linear velocity, the spin damped;
// - humans: gravity off and the position set each frame (a throw or fast drop ragdolls them with that speed).
// Buildings are fixed bodies. Piece mode breaks every link of the piece under the crosshair, so the destruction
// code splits it off as a loose mover (as stress breaks do), then grabs it once it exists. Chunk mode releases the
// hit mover's world anchors and makes it a normal moving body.
// Freezing a body makes it unmovable (keyframed); a frozen human is held where it is each frame.

#include "volition/rfg/code/weapons/grav_tether.h"

#include "volition/rfg/code/camera/rfg_camera.h"
#include "volition/rfg/code/control/control_map.h"
#include "volition/rfg/code/destruction/rfg_dynamics.h"
#include "volition/rfg/code/destruction/rfg_mover.h"
#include "volition/rfg/code/destruction/rfg_shapes.h"
#include "volition/rfg/code/gamewide/gameseq.h"
#include "volition/rfg/code/havok/havok.h"
#include "volition/rfg/code/havok/havok_raycast.h"
#include "volition/rfg/code/hud/hud_message.h"
#include "volition/rfg/code/level/world.h"
#include "volition/rfg/code/multi/multi_core/include/multi.h"
#include "volition/rfg/code/objects/humans/human.h"
#include "volition/rfg/code/objects/inventory_item.h"
#include "volition/rfg/code/objects/humans/human_ragdoll.h"
#include "volition/rfg/code/objects/humans/player.h"
#include "volition/rfg/code/objects/object.h"
#include "volition/rfg/code/weapons/weapons.h"
#include "volition/vlib/code/input/key.h"
#include "volition/vlib/code/os/frametime.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void openrfg_log(const char*);

// Magic numbers: keen's keyboard ids of F3 and F4 (inputsystem_win32.cpp, s_buttonIdKeyMap).
static const unsigned int Grav_key_mode = 0x53;
static const unsigned int Grav_key_toggle = 0x54;
static const char* const Grav_weapon_name = "edf_pistol";
static const float Grav_range = 250.0f; // metres the tether reaches
static const float Grav_min_hold_dist = 3.0f;
// Held things are all but locked to the crosshair: each frame closes this share of the gap to the hold point (the
// rest keeps the motion physical, so held things still push and get pushed).
static const float Grav_follow_share = 0.85f;
static const float Grav_max_speed = 45.0f; // m/s while held: people, vehicles, debris, props, loose pieces
static const float Grav_max_speed_chunk = 300.0f; // m/s while held: a building chunk the tether tore loose
static const float Grav_spin_damping = 0.6f; // per frame, while held
static const float Grav_spin_damping_chunk = 0.3f; // per frame: held chunks barely turn, so they stay as they are put
static const float Grav_throw_speed = 55.0f; // m/s added along the view
static const float Grav_human_mass = 80.0f; // kg, for the ragdoll impulse of a dropped or thrown human
static const float Grav_human_ragdoll_speed = 6.0f; // m/s: a human let go faster than this falls as a ragdoll
static const int Grav_piece_grab_ms = 700; // how long a split piece is waited for

enum grav_mode
{
    GRAV_MODE_PIECE = 0,
    GRAV_MODE_CHUNK = 1,
};

enum grav_kind
{
    GRAV_NONE = 0,
    GRAV_BODY = 1,
    GRAV_HUMAN = 2,
};

struct grav_frozen
{
    unsigned int handle;
    grav_kind kind;
    vector pos;
    matrix orient;
};

static bool Grav_enabled;
static grav_mode Grav_mode = GRAV_MODE_PIECE;
static grav_kind Grav_held_kind = GRAV_NONE;
static unsigned int Grav_held_handle = 0xFFFFFFFF;
static float Grav_hold_dist;
static vector Grav_grab_local; // the grabbed point, in the object's frame
static vector Grav_human_velocity;
// Piece mode: a piece was split off and is waited for at this point.
static bool Grav_piece_pending;
static vector Grav_piece_pos;
static float Grav_piece_dist;
static timestamp Grav_piece_timer;
static const int Grav_max_frozen = 64;
static grav_frozen Grav_frozen[Grav_max_frozen];
static int Grav_num_frozen;
// The pistol was added by the tether, and the weapon that gave up its slot for it (back when the tether is off).
static bool Grav_added_pistol;
static const inv_item_info* Grav_replaced_info;
static int Grav_replaced_slot = -1;
// The building chunks Chunk mode tore loose (they get the chunk speed cap and spin damping when held again).
static const int Grav_max_chunks = 32;
static unsigned int Grav_chunks[Grav_max_chunks];
static int Grav_next_chunk;
static bool Grav_held_chunk;
// A freed building breaks apart from its own weight (stress: nothing under it holds it up any more) and from impacts.
// While a building mover is held or frozen, every intact link of it is raised to a strength nothing reaches; the
// strengths it had come back when it is let go, so a thrown one still breaks where it lands.
// Magic number: the most a link can hold. The damage code shifts a yield left by 8 in an int (rfg_dynamics.cpp,
// rfg_dyn_apply_damage_to_subpiece), and a bigger one wraps negative there and breaks the whole link.
static const int Grav_protected_yield = 0x7FFFFF;
struct grav_protected
{
    unsigned int handle;
    rfg_destroyable_shape* shape;
    int num_links;
    int* yields;
};
static const int Grav_max_protected = 16;
static grav_protected Grav_protected[Grav_max_protected];
static int Grav_num_protected;

static void grav_log(const char* fmt, ...);

static void grav_message(const char* text)
{
    hud_message_add_minor(text, 2.0f, false);
}

static vector grav_local_to_world(const matrix& orient, const vector& local)
{
    return orient.rvec * local.x + orient.uvec * local.y + orient.fvec * local.z;
}

static vector grav_world_to_local(const matrix& orient, const vector& world)
{
    return vector(world * orient.rvec, world * orient.uvec, world * orient.fvec);
}

static float grav_length(const vector& v)
{
    return sqrtf(v * v);
}

static bool grav_ray_filter(object* objp, void*)
{
    return objp != (object*)Player && !(objp->get_obj_type() == OT_ITEM && objp->get_sub_type() == OT_SUB_ITEM_WEAPON);
}

static bool grav_raycast(raycast_output& out)
{
    raycast_input in;
    in.start = Rfg_camera.real_pos;
    in.end = Rfg_camera.real_pos + Rfg_camera.real_orient.fvec * Grav_range;
    in.radius = 0.0f;
    in.filter_params = nullptr;
    in.filter_callback = grav_ray_filter;
    in.m_filter_info = COLLIDE_RAY_DEFAULT;
    in.m_hit_collector_type = raycast_input::COLLECT_CLOSEST_HIT;
    in.m_flags = 0;
    return havok_raycast(in, out) && out.m_hits.getSize() > 0;
}

static int grav_find_frozen(unsigned int handle)
{
    for (int i = 0; i < Grav_num_frozen; i++)
    {
        if (Grav_frozen[i].handle == handle)
        {
            return i;
        }
    }
    return -1;
}

static void grav_remove_frozen(int index)
{
    Grav_frozen[index] = Grav_frozen[Grav_num_frozen - 1];
    Grav_num_frozen--;
}

static human* grav_human(unsigned int handle)
{
    object* objp = World.object_from_handle(handle);
    return objp != nullptr && objp->get_obj_type() == OT_HUMAN ? (human*)objp : nullptr;
}

// The object's body when it has one Havok can move.
static unsigned int grav_body(object* objp)
{
    return objp != nullptr ? objp->havok_handle : 0xFFFFFFFF;
}

static bool grav_is_chunk(unsigned int handle)
{
    for (int i = 0; i < Grav_max_chunks; i++)
    {
        if (Grav_chunks[i] == handle)
        {
            return true;
        }
    }
    return false;
}

static int grav_find_protected(unsigned int handle)
{
    for (int i = 0; i < Grav_num_protected; i++)
    {
        if (Grav_protected[i].handle == handle)
        {
            return i;
        }
    }
    return -1;
}

static void grav_protect(object* objp)
{
    if (objp->get_obj_type() != OT_MOVER || objp->get_sub_type() != OT_SUB_MOVER_RFG || grav_find_protected(objp->get_handle()) >= 0)
    {
        return;
    }
    rfg_destroyable_shape* shape = ((rfg_mover*)objp)->get_shape();
    if (shape == nullptr || shape->parent == nullptr || shape->parent->num_links <= 0 || Grav_num_protected >= Grav_max_protected)
    {
        return;
    }
    grav_protected& p = Grav_protected[Grav_num_protected++];
    p.handle = objp->get_handle();
    p.shape = shape;
    p.num_links = shape->parent->num_links;
    p.yields = new int[p.num_links];
    for (int i = 0; i < p.num_links; i++)
    {
        rfg_link* link = shape->get_link(i);
        p.yields[i] = link->yield_limit;
        if (link->yield_limit > 0 && link->yield_limit < Grav_protected_yield)
        {
            link->yield_limit = Grav_protected_yield; // broken links (below 1) stay broken
        }
    }
    grav_log("GRAV protect %08X: %d links", p.handle, p.num_links);
}

// Puts back the link strengths the mover had (the ones still intact); restore=false only drops the record.
static void grav_unprotect(unsigned int handle, bool restore)
{
    const int index = grav_find_protected(handle);
    if (index < 0)
    {
        return;
    }
    grav_protected& p = Grav_protected[index];
    object* objp = restore ? World.object_from_handle(handle) : nullptr;
    if (objp != nullptr && objp->get_obj_type() == OT_MOVER && objp->get_sub_type() == OT_SUB_MOVER_RFG && ((rfg_mover*)objp)->get_shape() == p.shape && p.shape->parent != nullptr && p.shape->parent->num_links == p.num_links)
    {
        for (int i = 0; i < p.num_links; i++)
        {
            rfg_link* link = p.shape->get_link(i);
            if (link->yield_limit > 0 && p.yields[i] > 0)
            {
                link->yield_limit = p.yields[i];
            }
        }
    }
    delete[] p.yields;
    Grav_protected[index] = Grav_protected[Grav_num_protected - 1];
    Grav_num_protected--;
}

static void grav_unfreeze(unsigned int handle)
{
    const int index = grav_find_frozen(handle);
    if (index < 0)
    {
        return;
    }
    if (Grav_frozen[index].kind == GRAV_BODY)
    {
        object* objp = World.object_from_handle(handle);
        if (grav_body(objp) != 0xFFFFFFFF)
        {
            havok_body_set_movable(objp->havok_handle, true);
            havok_body_force_activate(objp->havok_handle);
        }
    }
    else
    {
        human* hp = grav_human(handle);
        if (hp != nullptr)
        {
            human_enable_gravity(hp, true);
        }
    }
    grav_remove_frozen(index);
}

static void grav_start_hold(object* objp, const vector& hit_pos)
{
    const unsigned int handle = objp->get_handle();
    grav_unfreeze(handle);
    Grav_hold_dist = grav_length(hit_pos - Rfg_camera.real_pos);
    if (Grav_hold_dist < Grav_min_hold_dist)
    {
        Grav_hold_dist = Grav_min_hold_dist;
    }
    Grav_held_handle = handle;
    if (objp->get_obj_type() == OT_HUMAN)
    {
        human* hp = (human*)objp;
        if (human_is_ragdolled(hp))
        {
            human_ragdoll_stop(hp, true);
        }
        human_enable_gravity(hp, false);
        Grav_held_kind = GRAV_HUMAN;
        Grav_grab_local = hp->pos - hit_pos; // world offset: humans stay upright
        Grav_human_velocity = ZERO_VECTOR;
        grav_log("GRAV grab human %08X dist=%.1f", handle, Grav_hold_dist);
        return;
    }
    const unsigned int body = grav_body(objp);
    if (body == 0xFFFFFFFF)
    {
        grav_log("GRAV grab: %08X (type %d/%d) has no body", handle, (int)objp->get_obj_type(), (int)objp->get_sub_type());
        Grav_held_handle = 0xFFFFFFFF;
        return;
    }
    if (!havok_body_is_movable(body))
    {
        havok_body_set_movable(body, true);
    }
    havok_body_force_activate(body);
    vector pos;
    matrix orient;
    havok_body_get_pos_orient(body, pos, orient);
    Grav_grab_local = grav_world_to_local(orient, hit_pos - pos);
    Grav_held_kind = GRAV_BODY;
    Grav_held_chunk = grav_is_chunk(handle);
    grav_protect(objp);
    grav_log("GRAV grab body %08X (type %d/%d) dist=%.1f movable=%d", handle, (int)objp->get_obj_type(), (int)objp->get_sub_type(), Grav_hold_dist, havok_body_is_movable(body) ? 1 : 0);
}

static void grav_end_hold()
{
    Grav_held_kind = GRAV_NONE;
    Grav_held_handle = 0xFFFFFFFF;
}

// Let go: a body keeps the speed it has; a human falls, as a ragdoll when it was moving fast.
static void grav_release(const vector& extra_velocity)
{
    if (Grav_held_kind == GRAV_HUMAN)
    {
        human* hp = grav_human(Grav_held_handle);
        if (hp != nullptr)
        {
            human_enable_gravity(hp, true);
            const vector velocity = Grav_human_velocity + extra_velocity;
            if (grav_length(velocity) > Grav_human_ragdoll_speed)
            {
                human_ragdoll_start_impulse(hp, velocity * Grav_human_mass, ZERO_VECTOR);
            }
        }
    }
    else if (Grav_held_kind == GRAV_BODY)
    {
        object* objp = World.object_from_handle(Grav_held_handle);
        if (grav_body(objp) != 0xFFFFFFFF)
        {
            vector velocity;
            havok_body_get_linear_velocity(objp->havok_handle, velocity);
            havok_body_set_linear_velocity(objp->havok_handle, velocity + extra_velocity);
        }
        grav_unprotect(Grav_held_handle, true);
    }
    grav_end_hold();
}

static void grav_freeze_held()
{
    object* objp = World.object_from_handle(Grav_held_handle);
    if (objp == nullptr || Grav_num_frozen >= Grav_max_frozen)
    {
        grav_end_hold();
        return;
    }
    grav_frozen& f = Grav_frozen[Grav_num_frozen++];
    f.handle = Grav_held_handle;
    f.kind = Grav_held_kind;
    f.pos = objp->pos;
    f.orient = objp->orient;
    if (Grav_held_kind == GRAV_BODY && grav_body(objp) != 0xFFFFFFFF)
    {
        havok_body_set_linear_velocity(objp->havok_handle, ZERO_VECTOR);
        havok_body_set_angular_velocity(objp->havok_handle, ZERO_VECTOR);
        havok_body_set_movable(objp->havok_handle, false);
    }
    grav_log("GRAV freeze %08X kind=%d (%d frozen)", Grav_held_handle, (int)Grav_held_kind, Grav_num_frozen);
    grav_message("FROZEN");
    grav_end_hold();
}

// Piece mode: every link of the hit piece breaks, so the destruction code splits it off as its own mover.
static bool grav_split_piece(rfg_mover* rm, unsigned int shape_key)
{
    rfg_destroyable_shape* shape = rm->get_shape();
    if (shape == nullptr || shape->parent == nullptr || shape_key >= (unsigned int)shape->parent->num_objects)
    {
        return false;
    }
    rfg_subpiece* sp = &shape->get_object_buffer()[shape_key];
    if (!shape->get_shape_state(sp))
    {
        return false;
    }
    int broken = 0;
    for (int i = 0; i < sp->num_links; i++)
    {
        rfg_link* link = shape->get_link(sp, i);
        if (link == nullptr)
        {
            continue;
        }
        const unsigned int link_index = (unsigned int)shape->get_link_index(link);
        if (rfg_dyn_add_pre_collide_link_broken(rm, 1, &link_index, 0, false))
        {
            broken++;
        }
    }
    grav_log("GRAV piece %u of mover %08X: %d of %d links broken, anchor_yield=%d", shape_key, rm->get_handle(), broken, (int)sp->num_links, sp->anchor_yield);
    return broken > 0;
}

// What the tether takes hold of: no zones, terrain, nodes or turrets.
static bool grav_can_grab(const object* objp)
{
    switch (objp->get_obj_type())
    {
    case OT_HUMAN:
    case OT_VEHICLE:
    case OT_DEBRIS:
    case OT_MOVER:
    case OT_ITEM:
        return true;
    default:
        return false;
    }
}

// Chunk mode: the mover lets go of its world anchors and becomes a moving body. Its Havok body was made fixed, so it
// gets dynamic mass properties first (a box over its bounds, its own mass), as Havok will not move it without them.
static bool grav_make_chunk_dynamic(rfg_mover* rm)
{
    const unsigned int body = rm->havok_handle;
    if (body == 0xFFFFFFFF)
    {
        return false;
    }
    rm->release_anchor_points();
    rm->set_move_type(RFGM_NORMAL, true);
    vector pos;
    matrix orient;
    havok_body_get_pos_orient(body, pos, orient);
    vector bmin;
    vector bmax;
    if (!havok_body_get_aabb(body, pos, orient, bmin, bmax))
    {
        bmin = pos - 5.0f;
        bmax = pos + 5.0f;
    }
    const vector center_local = grav_world_to_local(orient, (bmin + bmax) * 0.5f - pos);
    const vector ext = (bmax - bmin) * 0.5f;
    // The world box's half extents, seen along the body's axes.
    const vector half(fabsf(orient.rvec.x) * ext.x + fabsf(orient.rvec.y) * ext.y + fabsf(orient.rvec.z) * ext.z,
                      fabsf(orient.uvec.x) * ext.x + fabsf(orient.uvec.y) * ext.y + fabsf(orient.uvec.z) * ext.z,
                      fabsf(orient.fvec.x) * ext.x + fabsf(orient.fvec.y) * ext.y + fabsf(orient.fvec.z) * ext.z);
    float mass = rm->get_mass();
    // Magic numbers: a mover's own mass can be zero or absurd for a fixed building; keep it in a range that pushes
    // vehicles and people aside without launching them.
    if (!(mass > 2000.0f))
    {
        mass = 2000.0f;
    }
    if (mass > 2000000.0f)
    {
        mass = 2000000.0f;
    }
    vector com;
    matrix inertia;
    havok_body_calc_mass_props(center_local - half, center_local + half, mass, com, inertia);
    const bool ok = havok_body_openrfg_make_dynamic(body, mass, com, inertia);
    if (ok && !grav_is_chunk(rm->get_handle()))
    {
        Grav_chunks[Grav_next_chunk] = rm->get_handle();
        Grav_next_chunk = (Grav_next_chunk + 1) % Grav_max_chunks;
    }
    grav_log("GRAV chunk: mover %08X mass=%.0f half=(%.1f,%.1f,%.1f) move_type=%d dynamic=%d", rm->get_handle(), mass, half.x, half.y, half.z, (int)rm->get_move_type(), ok ? 1 : 0);
    return ok;
}

// Fire pressed: grab what the crosshair is on, or in Piece mode split a building piece off and wait for it.
static void grav_try_grab()
{
    raycast_output out;
    if (!grav_raycast(out))
    {
        return;
    }
    const raycast_output::hit_info& hit = out.m_hits[0];
    object* objp = World.object_from_handle(hit.hit_object_handle);
    if (objp == nullptr || !grav_can_grab(objp))
    {
        return;
    }
    if (objp->get_obj_type() == OT_MOVER && objp->get_sub_type() == OT_SUB_MOVER_RFG && grav_find_frozen(objp->get_handle()) < 0)
    {
        rfg_mover* rm = (rfg_mover*)objp;
        if (rm->get_move_type() == RFGM_FIXED || !havok_body_is_movable(rm->havok_handle))
        {
            if (Grav_mode == GRAV_MODE_CHUNK)
            {
                if (!grav_make_chunk_dynamic(rm))
                {
                    grav_message("THIS CHUNK WILL NOT BUDGE");
                    return;
                }
            }
            else
            {
                if (grav_split_piece(rm, hit.hit_havok_shape_key))
                {
                    Grav_piece_pending = true;
                    Grav_piece_pos = hit.hit_pos;
                    Grav_piece_dist = grav_length(hit.hit_pos - Rfg_camera.real_pos);
                    Grav_piece_timer.set(Grav_piece_grab_ms);
                }
                return;
            }
        }
    }
    grav_start_hold(objp, hit.hit_pos);
}

// Piece mode: the split piece is a new mover some frames later; grab the loose one at the point.
static void grav_check_piece_pending()
{
    if (!Grav_piece_pending)
    {
        return;
    }
    if (Grav_piece_timer.elapsed())
    {
        Grav_piece_pending = false;
        grav_log("GRAV piece: no loose piece showed up");
        return;
    }
    raycast_output out;
    if (!grav_raycast(out))
    {
        return;
    }
    const raycast_output::hit_info& hit = out.m_hits[0];
    object* objp = World.object_from_handle(hit.hit_object_handle);
    if (objp == nullptr || objp->get_obj_type() == OT_HUMAN || !grav_can_grab(objp))
    {
        return;
    }
    const unsigned int body = grav_body(objp);
    const bool fixed_mover = objp->get_obj_type() == OT_MOVER && objp->get_sub_type() == OT_SUB_MOVER_RFG && ((rfg_mover*)objp)->get_move_type() == RFGM_FIXED;
    if (body == 0xFFFFFFFF || fixed_mover)
    {
        return;
    }
    Grav_piece_pending = false;
    grav_start_hold(objp, hit.hit_pos);
    Grav_hold_dist = Grav_piece_dist;
}

static void grav_hold_frame(float dt)
{
    const vector hold_point = Rfg_camera.real_pos + Rfg_camera.real_orient.fvec * Grav_hold_dist;
    if (Grav_held_kind == GRAV_HUMAN)
    {
        human* hp = grav_human(Grav_held_handle);
        if (hp == nullptr || hp->is_killed())
        {
            if (hp != nullptr)
            {
                human_enable_gravity(hp, true);
            }
            grav_end_hold();
            return;
        }
        const vector target = hold_point + Grav_grab_local;
        const vector step = target - hp->pos;
        Grav_human_velocity = dt > 0.0f ? step / dt : ZERO_VECTOR;
        hp->update_pos_and_orient(hp->pos + step, hp->orient, true);
        hp->velocity = ZERO_VECTOR;
        return;
    }
    object* objp = World.object_from_handle(Grav_held_handle);
    const unsigned int body = grav_body(objp);
    if (body == 0xFFFFFFFF)
    {
        grav_unprotect(Grav_held_handle, false);
        grav_end_hold();
        return;
    }
    vector pos;
    matrix orient;
    havok_body_get_pos_orient(body, pos, orient);
    const vector grab_world = pos + grav_local_to_world(orient, Grav_grab_local);
    if (!havok_body_is_movable(body))
    {
        havok_body_set_movable(body, true); // the game fixed it again (a re-anchored building); it has its dynamic motion now
    }
    const float inv_dt = dt > 0.001f ? 1.0f / dt : 60.0f;
    vector velocity = (hold_point - grab_world) * (Grav_follow_share * inv_dt);
    const float max_speed = Grav_held_chunk ? Grav_max_speed_chunk : Grav_max_speed;
    const float speed = grav_length(velocity);
    if (speed > max_speed)
    {
        velocity = velocity * (max_speed / speed);
    }
    havok_body_force_activate(body);
    havok_body_set_linear_velocity(body, velocity);
    vector spin;
    havok_body_get_angular_velocity(body, spin);
    havok_body_set_angular_velocity(body, spin * (Grav_held_chunk ? Grav_spin_damping_chunk : Grav_spin_damping));
}

// Frozen humans stay where they were frozen (bodies are unmovable instead); gone objects leave the list.
static void grav_frozen_frame()
{
    for (int i = Grav_num_frozen - 1; i >= 0; i--)
    {
        object* objp = World.object_from_handle(Grav_frozen[i].handle);
        if (objp == nullptr)
        {
            grav_unprotect(Grav_frozen[i].handle, false);
            grav_remove_frozen(i);
            continue;
        }
        if (Grav_frozen[i].kind == GRAV_HUMAN)
        {
            human* hp = (human*)objp;
            if (hp->is_killed())
            {
                human_enable_gravity(hp, true);
                grav_remove_frozen(i);
                continue;
            }
            hp->update_pos_and_orient(Grav_frozen[i].pos, hp->orient, true);
            hp->velocity = ZERO_VECTOR;
        }
    }
}

// The pistol's inventory entry: by its weapon name, or the table entry whose weapon it is.
static const inv_item_info* grav_pistol_info()
{
    const inv_item_info* info = inv_item_get_info(Grav_weapon_name);
    for (int i = 0; info == nullptr && i < Num_inv_item_info; i++)
    {
        if (Inv_item_info[i].w_info != nullptr && strcmp(Inv_item_info[i].w_info->name, Grav_weapon_name) == 0)
        {
            info = &Inv_item_info[i];
        }
    }
    return info;
}

static void grav_restore_replaced(player* pp)
{
    if (Grav_replaced_info == nullptr)
    {
        return;
    }
    if (human_inventory_slot_is_available(pp, Grav_replaced_slot))
    {
        human_add_item_to_inventory(pp, Grav_replaced_info, 1, -1, Grav_replaced_slot, false, false, false);
    }
    Grav_replaced_info = nullptr;
    Grav_replaced_slot = -1;
}

static bool grav_give_tether(player* pp)
{
    inventory_item* first = pp->inventory;
    inventory_item* iip = first;
    inventory_item* found = nullptr;
    while (iip != nullptr)
    {
        if (iip->info != nullptr && iip->info->w_info != nullptr && strcmp(iip->info->w_info->name, Grav_weapon_name) == 0)
        {
            found = iip;
            break;
        }
        iip = iip->next;
        if (iip == first)
        {
            break;
        }
    }
    if (found == nullptr)
    {
        const inv_item_info* info = grav_pistol_info();
        if (info == nullptr)
        {
            grav_log("GRAV: no inventory item for %s", Grav_weapon_name);
            return false;
        }
        // Force it in: with every weapon slot taken, the weapon in hand gives up its slot (the last slot when the
        // hand is empty or holds the hammer, which keeps slot 0). Turning the tether off gives it back.
        int slot = -1;
        for (int s = 0; s < 4 && slot < 0; s++)
        {
            if (human_inventory_slot_is_available(pp, s))
            {
                slot = s;
            }
        }
        if (slot < 0)
        {
            inventory_item* equipped = pp->get_equipped_inv_item();
            slot = equipped != nullptr && equipped->selection_slot > 0 && equipped->selection_slot < 4 ? equipped->selection_slot : 3;
            inventory_item* victim = inv_item_get_item_in_slot(pp, slot);
            if (victim != nullptr)
            {
                Grav_replaced_info = victim->info;
                Grav_replaced_slot = slot;
                grav_log("GRAV: %s gives up slot %d to the tether", victim->info != nullptr ? victim->info->name : "?", slot);
                if (equipped == victim)
                {
                    human_unequip_inventory_item(pp);
                }
                human_delete_item_from_inventory(pp, victim, false, false);
            }
        }
        found = human_add_item_to_inventory(pp, info, 1, -1, slot, false, false, false);
        Grav_added_pistol = found != nullptr;
    }
    if (found == nullptr)
    {
        grav_log("GRAV: could not add %s to the inventory", Grav_weapon_name);
        grav_restore_replaced(pp);
        return false;
    }
    player_equip_item(pp, found, nullptr, false);
    return true;
}

// Turning the tether off: the pistol it was handed goes, and the weapon that gave up its slot comes back.
static void grav_take_tether(player* pp)
{
    if (Grav_added_pistol)
    {
        for (inventory_item* iip = pp->inventory; iip != nullptr;)
        {
            if (iip->info != nullptr && iip->info->w_info != nullptr && strcmp(iip->info->w_info->name, Grav_weapon_name) == 0)
            {
                if (iip == pp->get_equipped_inv_item())
                {
                    human_unequip_inventory_item(pp);
                }
                human_delete_item_from_inventory(pp, iip, false, false);
                break;
            }
            iip = iip->next;
            if (iip == pp->inventory)
            {
                break;
            }
        }
        Grav_added_pistol = false;
    }
    grav_restore_replaced(pp);
}

bool grav_tether_owns_weapon(const weapon* wp)
{
    return Grav_enabled && wp != nullptr && wp->w_info != nullptr && Player != nullptr && wp->owner_handle == Player->get_handle() && strcmp(wp->w_info->name, Grav_weapon_name) == 0;
}

void grav_tether_level_init()
{
    Grav_held_kind = GRAV_NONE;
    Grav_held_handle = 0xFFFFFFFF;
    Grav_piece_pending = false;
    Grav_num_frozen = 0;
    // The old level's movers are gone: only the records go.
    while (Grav_num_protected > 0)
    {
        grav_unprotect(Grav_protected[0].handle, false);
    }
    for (int i = 0; i < Grav_max_chunks; i++)
    {
        Grav_chunks[i] = 0xFFFFFFFF;
    }
}

void grav_tether_do_frame()
{
    player* pp = Player;
    if (Multiplayer || pp == nullptr || gameseq_get_state() != GS_GAMEPLAY)
    {
        return;
    }
    const float dt = Frametime;
    if (key_just_pressed(Grav_key_toggle))
    {
        if (!Grav_enabled)
        {
            Grav_enabled = grav_give_tether(pp);
            grav_message(Grav_enabled ? (Grav_mode == GRAV_MODE_PIECE ? "GRAVITY TETHER - PIECE MODE" : "GRAVITY TETHER - CHUNK MODE") : "GRAVITY TETHER UNAVAILABLE");
        }
        else
        {
            if (Grav_held_kind != GRAV_NONE)
            {
                grav_release(ZERO_VECTOR);
            }
            Grav_enabled = false;
            grav_take_tether(pp);
            grav_message("GRAVITY TETHER OFF");
        }
    }
    grav_frozen_frame();
    if (!Grav_enabled)
    {
        return;
    }
    if (key_just_pressed(Grav_key_mode))
    {
        Grav_mode = Grav_mode == GRAV_MODE_PIECE ? GRAV_MODE_CHUNK : GRAV_MODE_PIECE;
        grav_message(Grav_mode == GRAV_MODE_PIECE ? "PIECE MODE" : "CHUNK MODE");
    }
    const bool holding_tether = grav_tether_owns_weapon(pp->get_equipped_weapon()) && !pp->is_killed() && human_in_vehicle(pp) == nullptr;
    const bool fire_down = holding_tether && controls_is_button_down(CBA_OFC_ATTACK_PRIMARY, Player_controller_index);
    if (Grav_held_kind == GRAV_NONE)
    {
        if (!fire_down)
        {
            Grav_piece_pending = false;
            return;
        }
        if (Grav_piece_pending)
        {
            grav_check_piece_pending();
        }
        else if (controls_is_button_just_pressed(CBA_OFC_ATTACK_PRIMARY, Player_controller_index))
        {
            grav_try_grab();
        }
        return;
    }
    if (!fire_down)
    {
        grav_release(ZERO_VECTOR);
        return;
    }
    if (controls_is_button_just_pressed(CBA_OFC_RELOAD, Player_controller_index))
    {
        grav_log("GRAV throw %08X", Grav_held_handle);
        grav_release(Rfg_camera.real_orient.fvec * Grav_throw_speed);
        return;
    }
    if (controls_is_button_just_pressed(CBA_OFC_ZOOM, Player_controller_index))
    {
        grav_freeze_held();
        return;
    }
    grav_hold_frame(dt);
}

#include <stdarg.h>
static void grav_log(const char* fmt, ...)
{
    char msg[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
#ifdef OPENRFG_X64
    openrfg_log(msg);
#endif
}
