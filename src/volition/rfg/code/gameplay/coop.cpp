// OpenRFG (new code, not in rfg.exe): single player co-op over direct IP / LAN. See coop.h.
//
// Transport: one non-blocking UDP socket. The host binds Coop_port and answers guests; a guest sends to the host only.
// The host relays every player's state to every guest, so guests never talk to each other. All players run the same
// OpenRFG build: the packets are plain little-endian structs, checked by a magic number and a version.
//
// Packets: HELLO (guest -> host, until answered), WELCOME (the guest's id and the host's position), REJECT (busy in a
// mission, full, or another version), STATE (20 per second from every player; the host forwards guests' states) and
// BYE. A peer that sends nothing for 6 s is dropped.
//
// Remote players: a player object (World.create_object<player>, as Wrecking Crew and multiplayer make extra players)
// of the character the remote player picked in the multiplayer settings, or Mason. It exists only while the remote
// player stands in a zone this machine has streamed in, so each player can be anywhere in the world; it is removed
// (and made again) as the remote player leaves (and re-enters) the local streamed area. It walks to the received
// position with the player movement code (the run/walk/sprint animations come from that), snaps when it falls more
// than 4 m behind, crouches, jumps, swings and fires as the remote player does, and is hidden while the remote player
// is in a vehicle (vehicles are not shared yet).

// winsock2 before any windows.h
#include <winsock2.h>
#include <ws2tcpip.h>

#include "volition/rfg/code/gameplay/coop.h"

#include "volition/rfg/code/camera/rfg_camera.h"
#include "volition/rfg/code/cutscene/cutscene.h"
#include "volition/rfg/code/gameplay/activities/activity.h"
#include "volition/rfg/code/gameplay/missions.h"
#include "volition/rfg/code/gamewide/gameseq.h"
#include "volition/rfg/code/gamewide/player_profile.h"
#include "volition/rfg/code/hud/hud_message.h"
#include "volition/rfg/code/level/world.h"
#include "volition/rfg/code/multi/multi_core/include/multi.h"
#include "volition/rfg/code/objects/humans/human.h"
#include "volition/rfg/code/objects/humans/human_info.h"
#include "volition/rfg/code/objects/humans/human_move.h"
#include "volition/rfg/code/objects/humans/human_weapons.h"
#include "volition/rfg/code/objects/humans/player.h"
#include "volition/rfg/code/objects/inventory_item.h"
#include "volition/rfg/code/objects/object.h"
#include "volition/rfg/code/spawn/spawn_resource_manager.h"
#include "volition/rfg/code/weapons/weapons.h"
#include "volition/vlib/code/os/frametime.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

void openrfg_log(const char*);

namespace
{
    // Magic numbers: "RFGC", the protocol version (bump it when a packet changes), the most players in one session.
    const unsigned int COOP_MAGIC = 0x43474652;
    const unsigned char COOP_VERSION = 1;
    const int COOP_MAX_PLAYERS = 4;
    const unsigned int COOP_TIMEOUT_MS = 6000;
    const unsigned int COOP_HELLO_PERIOD_MS = 1000;
    const unsigned int COOP_STATE_PERIOD_MS = 50;
    // The remote character is made within this distance of the local player (and only in a streamed zone).
    const float COOP_PUPPET_RANGE = 600.0f;
    // A remote character further than this from where it should be is put there.
    const float COOP_SNAP_DISTANCE = 4.0f;

    enum coop_packet_type : unsigned char
    {
        CPT_HELLO = 1,
        CPT_WELCOME = 2,
        CPT_REJECT = 3,
        CPT_STATE = 4,
        CPT_BYE = 5,
    };

    enum coop_reject_reason : unsigned char
    {
        CRR_BUSY = 1, // the host is in a mission, activity or cutscene
        CRR_FULL = 2,
        CRR_VERSION = 3,
    };

    enum coop_action_bits : unsigned char
    {
        CAB_CROUCH = 0x01,
        CAB_SPRINT = 0x02,
        CAB_TRIGGER = 0x04,
        CAB_IN_VEHICLE = 0x08,
        CAB_DEAD = 0x10,
    };

#pragma pack(push, 1)
    struct coop_header
    {
        unsigned int magic;
        unsigned char version;
        unsigned char type;
        unsigned char sender_id;
        unsigned char pad;
    };

    struct coop_hello
    {
        coop_header h;
        char name[32];
        unsigned int character_crc;
    };

    struct coop_welcome
    {
        coop_header h;
        unsigned char your_id;
        unsigned char pad[3];
        float host_pos[3];
    };

    struct coop_reject
    {
        coop_header h;
        unsigned char reason;
    };

    struct coop_state
    {
        coop_header h;
        unsigned char player_id;
        unsigned char flags; // coop_action_bits
        unsigned char jump_id;
        unsigned char melee_id; // bumped per swing; the low 2 bits of melee_type say which
        unsigned char melee_type;
        unsigned char pad[3];
        unsigned int character_crc;
        float pos[3];
        float vel[3];
        float heading; // the body's facing, radians
        float aim[3]; // what the player aims at
        char weapon[32]; // the equipped weapon's info name ("" for none)
        char name[32];
    };
#pragma pack(pop)

    struct coop_peer
    {
        bool used;
        bool have_state;
        sockaddr_in addr; // host side: the guest's address (unused on a guest)
        unsigned int last_recv_ms;
        coop_state state;
        // the remote character
        unsigned int puppet_handle;
        const human_info* puppet_info;
        char puppet_weapon[32];
        unsigned char applied_jump_id;
        unsigned char applied_melee_id;
        bool puppet_hidden;
        bool wants_special_slot;
    };

    SOCKET Socket = INVALID_SOCKET;
    coop_mode Mode = COOP_MODE_OFF;
    sockaddr_in Host_addr;
    // Index = player id: 0 is the host. On a guest, My_id is set by WELCOME.
    coop_peer Peers[COOP_MAX_PLAYERS];
    int My_id = -1;
    bool Joined = false;
    unsigned int Last_hello_ms = 0;
    unsigned int Last_state_ms = 0;
    unsigned char Local_jump_id = 0;
    unsigned char Local_melee_id = 0;
    unsigned char Local_melee_type = 0;
    // A guest that joined is moved next to the host: held there until the world around it has streamed in.
    bool Teleport_pending = false;
    vector Teleport_pos;
    unsigned int Teleport_streamed_ms = 0;
    bool Reject_shown = false;
    bool Wsa_started = false;
    // The special human slot reserved for a remote character's model (one at a time).
    const human_info* Reserved_special_info = nullptr;

    unsigned int now_ms()
    {
        return (unsigned int)GetTickCount64();
    }

    void coop_log(const char* format, ...)
    {
        char text[256];
        va_list args;
        va_start(args, format);
        vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        char line[280];
        snprintf(line, sizeof(line), "COOP %s", text);
        openrfg_log(line);
    }

    void coop_message(const char* text)
    {
        hud_message_add_minor(text, 3.0f, false);
    }

    void fill_header(coop_header& h, coop_packet_type type)
    {
        h.magic = COOP_MAGIC;
        h.version = COOP_VERSION;
        h.type = type;
        h.sender_id = (unsigned char)(My_id < 0 ? 0xFF : My_id);
        h.pad = 0;
    }

    void send_to(const sockaddr_in& addr, const void* data, int size)
    {
        if (Socket != INVALID_SOCKET)
        {
            sendto(Socket, (const char*)data, size, 0, (const sockaddr*)&addr, sizeof(addr));
        }
    }

    bool same_addr(const sockaddr_in& a, const sockaddr_in& b)
    {
        return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
    }

    bool open_socket(unsigned short bind_port)
    {
        if (!Wsa_started)
        {
            WSADATA data;
            if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            {
                coop_log("WSAStartup failed");
                return false;
            }
            Wsa_started = true;
        }
        Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (Socket == INVALID_SOCKET)
        {
            coop_log("socket() failed: %d", WSAGetLastError());
            return false;
        }
        u_long nonblocking = 1;
        ioctlsocket(Socket, FIONBIO, &nonblocking);
        sockaddr_in local = {};
        local.sin_family = AF_INET;
        local.sin_port = htons(bind_port);
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(Socket, (const sockaddr*)&local, sizeof(local)) != 0)
        {
            coop_log("bind(port %u) failed: %d", (unsigned int)bind_port, WSAGetLastError());
            closesocket(Socket);
            Socket = INVALID_SOCKET;
            return false;
        }
        return true;
    }

    // "a.b.c.d" or "a.b.c.d:port" (the port defaults to Coop_port).
    bool parse_address(const char* text, sockaddr_in& out)
    {
        char host[64];
        strncpy_s(host, text, _TRUNCATE);
        int port = Coop_port;
        char* colon = strchr(host, ':');
        if (colon != nullptr)
        {
            *colon = '\0';
            port = atoi(colon + 1);
        }
        if (port < 1 || port > 65535)
        {
            return false;
        }
        out = {};
        out.sin_family = AF_INET;
        out.sin_port = htons((unsigned short)port);
        return inet_pton(AF_INET, host, &out.sin_addr) == 1;
    }

    // The character picked in the multiplayer settings (free for all), 0 when none was picked.
    unsigned int local_character_crc()
    {
        const player_profile_data* profile = player_profile_get("coop", 0);
        return profile != nullptr ? profile->mp_ffa_player_crc : 0;
    }

    const human_info* mason_info()
    {
        const human_info* hi = human_info_find_internal("rfg_PC_DLC");
        return hi != nullptr ? hi : human_info_find_internal("rfg_PC");
    }

    // As multi_game_player.cpp's (file static there): the ammo types that fire with the trigger.
    bool coop_fires_with_trigger(const weapon_info* w_info)
    {
        const weapon_ammo_type type = w_info->ammo_type;
        return type == WEAPON_AMMO_TYPE_BULLET || type == WEAPON_AMMO_TYPE_PROJECTILE || type == WEAPON_AMMO_TYPE_ELECTRICITY ||
               type == WEAPON_AMMO_TYPE_REPAIR || type == WEAPON_AMMO_TYPE_BLANKS || type == WEAPON_AMMO_TYPE_FLAME;
    }

    // ---- remote characters
    player* puppet_of(coop_peer& peer)
    {
        if (peer.puppet_handle == 0xFFFFFFFF)
        {
            return nullptr;
        }
        object* objp = World.object_from_handle(peer.puppet_handle);
        if (objp == nullptr || objp->get_obj_type() != OT_HUMAN || objp->get_sub_type() != OT_SUB_HUMAN_PLAYER)
        {
            peer.puppet_handle = 0xFFFFFFFF;
            return nullptr;
        }
        return (player*)objp;
    }

    // Outside gameplay (back at the main menu) the level is gone: its objects and spawn slots are not touched.
    bool in_gameplay()
    {
        return gameseq_get_state() == GS_GAMEPLAY;
    }

    void release_special_slot()
    {
        if (Reserved_special_info != nullptr && in_gameplay())
        {
            srm_release_slot(SPAWN_SLOT_HUMAN_SPECIAL, RESOURCE_SLOT_USER_ID_SPECIAL, Reserved_special_info);
        }
        Reserved_special_info = nullptr;
    }

    void destroy_puppet(coop_peer& peer)
    {
        if (peer.puppet_handle != 0xFFFFFFFF && in_gameplay() && World.object_from_handle(peer.puppet_handle) != nullptr)
        {
            World.destroy_object(peer.puppet_handle, false);
        }
        peer.puppet_handle = 0xFFFFFFFF;
        peer.puppet_info = nullptr;
        peer.puppet_weapon[0] = '\0';
        peer.puppet_hidden = false;
    }

    // The model to use: the remote player's multiplayer character once its resources are streamed in (asked for in
    // the special human slot when free), else Mason.
    const human_info* pick_puppet_info(coop_peer& peer)
    {
        const human_info* mason = mason_info();
        if (peer.state.character_crc == 0)
        {
            return mason;
        }
        const human_info* wanted = human_info_find(peer.state.character_crc);
        if (wanted == nullptr || wanted == mason)
        {
            return mason;
        }
        if (srm_human_info_loaded(wanted))
        {
            return wanted;
        }
        if (Reserved_special_info == nullptr && srm_no_owner(SPAWN_SLOT_HUMAN_SPECIAL))
        {
            if (srm_reserve_slot(SPAWN_SLOT_HUMAN_SPECIAL, RESOURCE_SLOT_USER_ID_SPECIAL, wanted))
            {
                Reserved_special_info = wanted;
                coop_log("streaming character %s for player %d", wanted->name, (int)peer.state.player_id);
            }
        }
        return mason;
    }

    void create_puppet(coop_peer& peer, const vector& pos)
    {
        const human_info* hi = pick_puppet_info(peer);
        if (hi == nullptr)
        {
            return;
        }
        matrix orient;
        orient.set(0.0f, 0.0f, peer.state.heading);
        obj_human_props props(hi, pos, orient, nullptr);
        player* pp = World.create_object<player>(props, 0xFFFFFFFF);
        if (pp == nullptr && hi != mason_info())
        {
            hi = mason_info();
            obj_human_props mason_props(hi, pos, orient, nullptr);
            pp = World.create_object<player>(mason_props, 0xFFFFFFFF);
        }
        if (pp == nullptr)
        {
            coop_log("could not create the character of player %d", (int)peer.state.player_id);
            return;
        }
        human_set_state(pp, HS_STANDARD, HMS_IDLE, -1.0f);
        pp->update_pos_and_orient(pos, orient, true);
        pp->hflags.pushes_other_humans = 0;
        peer.puppet_handle = pp->get_handle();
        peer.puppet_info = hi;
        peer.puppet_weapon[0] = '\0';
        peer.applied_jump_id = peer.state.jump_id;
        peer.applied_melee_id = peer.state.melee_id;
        peer.puppet_hidden = false;
        coop_log("player %d (%s) appears as %s", (int)peer.state.player_id, peer.state.name, hi->name);
    }

    void puppet_set_weapon(coop_peer& peer, player* pp)
    {
        if (strncmp(peer.puppet_weapon, peer.state.weapon, sizeof(peer.puppet_weapon)) == 0)
        {
            return;
        }
        strncpy_s(peer.puppet_weapon, peer.state.weapon, _TRUNCATE);
        human_delete_all_items_from_inventory(pp, false, false);
        if (peer.puppet_weapon[0] == '\0')
        {
            return;
        }
        // Magic number: plenty of ammo; the remote player's own ammo is what limits the shots.
        inventory_item* iip = human_add_item_to_inventory(pp, peer.puppet_weapon, 1, 999, -1, false, false, false);
        if (iip != nullptr)
        {
            human_equip_inventory_item(pp, iip);
        }
    }

    void update_puppet(coop_peer& peer)
    {
        const coop_state& st = peer.state;
        const vector target(st.pos[0], st.pos[1], st.pos[2]);
        player* local = player_get_local_player();
        const bool in_range = local != nullptr && (target - local->pos).mag() < COOP_PUPPET_RANGE;
        const bool wanted = in_range && World.is_zone_streamed(target) && (st.flags & CAB_DEAD) == 0;
        player* pp = puppet_of(peer);
        if (!wanted)
        {
            if (pp != nullptr)
            {
                coop_log("player %d left the loaded area", (int)st.player_id);
                destroy_puppet(peer);
            }
            return;
        }
        // A better model streamed in: make the character again with it.
        if (pp != nullptr && peer.puppet_info == mason_info() && st.character_crc != 0)
        {
            const human_info* wanted_info = human_info_find(st.character_crc);
            if (wanted_info != nullptr && wanted_info != peer.puppet_info && srm_human_info_loaded(wanted_info))
            {
                destroy_puppet(peer);
                pp = nullptr;
            }
        }
        if (pp == nullptr)
        {
            create_puppet(peer, target);
            pp = puppet_of(peer);
            if (pp == nullptr)
            {
                return;
            }
        }
        // In a vehicle: hidden, carried along.
        const bool in_vehicle = (st.flags & CAB_IN_VEHICLE) != 0;
        if (in_vehicle != peer.puppet_hidden)
        {
            object_set_visibility_plus_children(pp, !in_vehicle);
            peer.puppet_hidden = in_vehicle;
        }
        if (in_vehicle)
        {
            human_controller_set_pos(pp, target);
            human_controller_set_vel(pp, vector(0.0f, 0.0f, 0.0f));
            return;
        }
        // The body follows the received position: the remote velocity plus a pull toward where it should be.
        vector to_target = target - pp->pos;
        if (to_target.mag() > COOP_SNAP_DISTANCE)
        {
            matrix orient;
            orient.set(0.0f, 0.0f, st.heading);
            pp->update_pos_and_orient(target, orient, true);
            human_controller_set_pos(pp, target);
            to_target = vector(0.0f, 0.0f, 0.0f);
        }
        const vector remote_vel(st.vel[0], 0.0f, st.vel[2]);
        vector move(remote_vel.x + to_target.x * 2.0f, 0.0f, remote_vel.z + to_target.z * 2.0f);
        float speed = 0.0f;
        float max_speed = 0.0f;
        float max_speed_base = 0.0f;
        player_get_max_movement_speeds(pp, max_speed, max_speed_base);
        const float move_len = move.mag();
        // Magic number: under 0.3 m/s is standing still.
        if (move_len > 0.3f && max_speed > 0.01f)
        {
            speed = move_len / max_speed;
            if (speed > 1.0f)
            {
                speed = 1.0f;
            }
        }
        const vector dest = pp->pos + move;
        pp->pflags.do_sprint = (st.flags & CAB_SPRINT) != 0 ? 1 : 0;
        player_process_normal_movement(pp, dest, speed);
        // The movement animation: the movement in the body's own frame as stick values.
        float stick_forward = 0.0f;
        float stick_side = 0.0f;
        if (speed > 0.0f)
        {
            vector dir = move;
            dir.normalize_safe();
            stick_forward = dir.x * pp->orient.fvec.x + dir.z * pp->orient.fvec.z;
            stick_side = dir.x * pp->orient.rvec.x + dir.z * pp->orient.rvec.z;
            stick_forward *= speed;
            stick_side *= speed;
        }
        player_move_set_anim_direct(pp, stick_side, stick_forward, (st.flags & CAB_SPRINT) != 0, speed <= 0.0f);
        // Aim
        pp->aim_pos = vector(st.aim[0], st.aim[1], st.aim[2]);
        pp->aim_target = 0xFFFFFFFF;
        pp->mp_camera_pos = pp->pos;
        vector aim_dir = pp->aim_pos - pp->pos;
        aim_dir.normalize_safe();
        if (!aim_dir.is_null())
        {
            const float pitch = -asinf(aim_dir.y < -1.0f ? -1.0f : (aim_dir.y > 1.0f ? 1.0f : aim_dir.y));
            pp->mp_camera_orient.set(pitch, 0.0f, atan2f(aim_dir.x, aim_dir.z));
        }
        // Crouch
        const bool crouch = (st.flags & CAB_CROUCH) != 0;
        if (crouch && !human_move_is_crouching(pp))
        {
            human_move_start_crouching(pp);
        }
        else if (!crouch && human_move_is_crouching(pp))
        {
            human_move_stop_crouching(pp);
        }
        // Jump
        if (st.jump_id != peer.applied_jump_id)
        {
            peer.applied_jump_id = st.jump_id;
            human_jump(pp, false, false, false);
        }
        // Weapon, swings, trigger
        puppet_set_weapon(peer, pp);
        if (st.melee_id != peer.applied_melee_id)
        {
            peer.applied_melee_id = st.melee_id;
            static const human_melee_attack_types types[4] = {MELEE_ATTACK_PRIMARY, MELEE_ATTACK_SECONDARY, MELEE_ATTACK_TERTIARY, MELEE_ATTACK_FIREARM};
            human_melee_attack(pp, types[st.melee_type & 3], true, false);
        }
        weapon* wp = pp->get_equipped_weapon();
        if (wp != nullptr && coop_fires_with_trigger(wp->w_info))
        {
            if ((st.flags & CAB_TRIGGER) != 0)
            {
                if (wp->get_rounds_in_magazine() < 1)
                {
                    wp->set_rounds_in_magazine(1);
                }
                human_weapons_firearm_pull_trigger(pp);
            }
            else if (human_weapons_is_trigger_pulled(pp))
            {
                human_weapons_firearm_release_trigger(pp);
            }
        }
    }

    // ---- the local player's state
    bool build_local_state(coop_state& st)
    {
        player* pp = player_get_local_player();
        if (pp == nullptr || My_id < 0)
        {
            return false;
        }
        memset(&st, 0, sizeof(st));
        fill_header(st.h, CPT_STATE);
        st.player_id = (unsigned char)My_id;
        if (human_move_is_crouching(pp))
        {
            st.flags |= CAB_CROUCH;
        }
        if (pp->pflags.do_sprint)
        {
            st.flags |= CAB_SPRINT;
        }
        if (human_weapons_is_trigger_pulled(pp))
        {
            st.flags |= CAB_TRIGGER;
        }
        if (human_in_vehicle(pp) != nullptr)
        {
            st.flags |= CAB_IN_VEHICLE;
        }
        if (pp->is_killed())
        {
            st.flags |= CAB_DEAD;
        }
        st.jump_id = Local_jump_id;
        st.melee_id = Local_melee_id;
        st.melee_type = Local_melee_type;
        st.character_crc = local_character_crc();
        st.pos[0] = pp->pos.x;
        st.pos[1] = pp->pos.y;
        st.pos[2] = pp->pos.z;
        st.vel[0] = pp->velocity.x;
        st.vel[1] = pp->velocity.y;
        st.vel[2] = pp->velocity.z;
        st.heading = atan2f(pp->orient.fvec.x, pp->orient.fvec.z);
        st.aim[0] = pp->aim_pos.x;
        st.aim[1] = pp->aim_pos.y;
        st.aim[2] = pp->aim_pos.z;
        const weapon_info* wi = human_weapons_get_weapon_info(pp);
        if (wi != nullptr && wi->name != nullptr)
        {
            strncpy_s(st.weapon, wi->name, _TRUNCATE);
        }
        const char* name = getenv("USERNAME");
        strncpy_s(st.name, name != nullptr ? name : "player", _TRUNCATE);
        return true;
    }

    void remove_peer(int id, const char* why)
    {
        coop_peer& peer = Peers[id];
        if (!peer.used)
        {
            return;
        }
        char text[96];
        snprintf(text, sizeof(text), "%s %s", peer.have_state ? peer.state.name : "A player", why);
        coop_message(text);
        coop_log("player %d removed: %s", id, why);
        destroy_puppet(peer);
        memset(&peer, 0, sizeof(peer));
        peer.puppet_handle = 0xFFFFFFFF;
    }

    void reset_peers()
    {
        for (int i = 0; i < COOP_MAX_PLAYERS; i++)
        {
            destroy_puppet(Peers[i]);
            memset(&Peers[i], 0, sizeof(Peers[i]));
            Peers[i].puppet_handle = 0xFFFFFFFF;
        }
    }

    // ---- receiving
    void host_receive(const char* data, int size, const sockaddr_in& from)
    {
        const coop_header* h = (const coop_header*)data;
        int id = -1;
        for (int i = 1; i < COOP_MAX_PLAYERS; i++)
        {
            if (Peers[i].used && same_addr(Peers[i].addr, from))
            {
                id = i;
                break;
            }
        }
        switch (h->type)
        {
        case CPT_HELLO:
        {
            if (size < (int)sizeof(coop_hello))
            {
                return;
            }
            const coop_hello* hello = (const coop_hello*)data;
            coop_reject reject;
            fill_header(reject.h, CPT_REJECT);
            if (h->version != COOP_VERSION)
            {
                reject.reason = CRR_VERSION;
                send_to(from, &reject, sizeof(reject));
                return;
            }
            if (id < 0)
            {
                // Joining only in free roam.
                if (!coop_is_free_roam())
                {
                    reject.reason = CRR_BUSY;
                    send_to(from, &reject, sizeof(reject));
                    return;
                }
                for (int i = 1; i < COOP_MAX_PLAYERS && id < 0; i++)
                {
                    if (!Peers[i].used)
                    {
                        id = i;
                    }
                }
                if (id < 0)
                {
                    reject.reason = CRR_FULL;
                    send_to(from, &reject, sizeof(reject));
                    return;
                }
                coop_peer& peer = Peers[id];
                memset(&peer, 0, sizeof(peer));
                peer.used = true;
                peer.addr = from;
                peer.puppet_handle = 0xFFFFFFFF;
                strncpy_s(peer.state.name, hello->name, _TRUNCATE);
                peer.state.character_crc = hello->character_crc;
                char text[96];
                snprintf(text, sizeof(text), "%s joined your game", peer.state.name);
                coop_message(text);
                coop_log("player %d joined from %s", id, inet_ntoa(from.sin_addr));
            }
            Peers[id].last_recv_ms = now_ms();
            coop_welcome welcome = {};
            fill_header(welcome.h, CPT_WELCOME);
            welcome.your_id = (unsigned char)id;
            player* pp = player_get_local_player();
            if (pp != nullptr)
            {
                welcome.host_pos[0] = pp->pos.x;
                welcome.host_pos[1] = pp->pos.y;
                welcome.host_pos[2] = pp->pos.z;
            }
            send_to(from, &welcome, sizeof(welcome));
            break;
        }
        case CPT_STATE:
        {
            if (id < 0 || size < (int)sizeof(coop_state))
            {
                return;
            }
            coop_peer& peer = Peers[id];
            memcpy(&peer.state, data, sizeof(coop_state));
            peer.state.player_id = (unsigned char)id;
            peer.state.name[31] = '\0';
            peer.state.weapon[31] = '\0';
            peer.have_state = true;
            peer.last_recv_ms = now_ms();
            // Relay to the other guests.
            for (int i = 1; i < COOP_MAX_PLAYERS; i++)
            {
                if (i != id && Peers[i].used)
                {
                    send_to(Peers[i].addr, &peer.state, sizeof(coop_state));
                }
            }
            break;
        }
        case CPT_BYE:
            if (id > 0)
            {
                remove_peer(id, "left the game");
            }
            break;
        default:
            break;
        }
    }

    void guest_receive(const char* data, int size, const sockaddr_in& from)
    {
        if (!same_addr(from, Host_addr))
        {
            return;
        }
        const coop_header* h = (const coop_header*)data;
        switch (h->type)
        {
        case CPT_WELCOME:
        {
            if (size < (int)sizeof(coop_welcome) || Joined)
            {
                return;
            }
            const coop_welcome* welcome = (const coop_welcome*)data;
            My_id = welcome->your_id;
            Joined = true;
            Peers[0].used = true;
            Peers[0].last_recv_ms = now_ms();
            Peers[0].puppet_handle = 0xFFFFFFFF;
            // Next to the host (3 m to the side), held there until the world there has streamed in.
            Teleport_pos = vector(welcome->host_pos[0] + 3.0f, welcome->host_pos[1] + 0.5f, welcome->host_pos[2]);
            Teleport_pending = true;
            Teleport_streamed_ms = 0;
            coop_message("Joined the co-op game");
            coop_log("joined as player %d", My_id);
            break;
        }
        case CPT_REJECT:
        {
            if (size < (int)sizeof(coop_reject) || Joined)
            {
                return;
            }
            const coop_reject* reject = (const coop_reject*)data;
            if (!Reject_shown || reject->reason != CRR_BUSY)
            {
                coop_message(reject->reason == CRR_BUSY ? "The host is in a mission: waiting for free roam"
                             : reject->reason == CRR_FULL ? "The co-op game is full"
                                                          : "The host runs another OpenRFG version");
                Reject_shown = true;
            }
            if (reject->reason != CRR_BUSY)
            {
                coop_stop();
            }
            break;
        }
        case CPT_STATE:
        {
            if (size < (int)sizeof(coop_state) || !Joined)
            {
                return;
            }
            const coop_state* st = (const coop_state*)data;
            const int id = st->player_id;
            if (id < 0 || id >= COOP_MAX_PLAYERS || id == My_id)
            {
                return;
            }
            coop_peer& peer = Peers[id];
            if (!peer.used)
            {
                memset(&peer, 0, sizeof(peer));
                peer.used = true;
                peer.puppet_handle = 0xFFFFFFFF;
            }
            memcpy(&peer.state, st, sizeof(coop_state));
            peer.state.name[31] = '\0';
            peer.state.weapon[31] = '\0';
            if (!peer.have_state)
            {
                peer.applied_jump_id = st->jump_id;
                peer.applied_melee_id = st->melee_id;
            }
            peer.have_state = true;
            peer.last_recv_ms = now_ms();
            // Anything from the host keeps the host alive.
            Peers[0].last_recv_ms = now_ms();
            break;
        }
        case CPT_BYE:
            coop_message("The host ended the co-op game");
            coop_stop();
            break;
        default:
            break;
        }
    }

    void receive_all()
    {
        char buffer[512];
        for (int guard = 0; guard < 256 && Socket != INVALID_SOCKET; guard++)
        {
            sockaddr_in from = {};
            int from_len = sizeof(from);
            const int size = recvfrom(Socket, buffer, sizeof(buffer), 0, (sockaddr*)&from, &from_len);
            if (size <= 0)
            {
                break;
            }
            if (size < (int)sizeof(coop_header) || ((const coop_header*)buffer)->magic != COOP_MAGIC)
            {
                continue;
            }
            if (Mode == COOP_MODE_HOST)
            {
                host_receive(buffer, size, from);
            }
            else if (Mode == COOP_MODE_JOIN)
            {
                guest_receive(buffer, size, from);
            }
        }
    }

    void process_teleport()
    {
        player* pp = player_get_local_player();
        if (!Teleport_pending || pp == nullptr)
        {
            return;
        }
        human_controller_set_pos(pp, Teleport_pos);
        human_controller_set_vel(pp, vector(0.0f, 0.0f, 0.0f));
        if (!World.is_zone_streamed(Teleport_pos))
        {
            Teleport_streamed_ms = 0;
            return;
        }
        if (Teleport_streamed_ms == 0)
        {
            Teleport_streamed_ms = now_ms();
        }
        // Magic number: 3 s more for the ground's physics and the buildings around.
        if (now_ms() - Teleport_streamed_ms > 3000)
        {
            Teleport_pending = false;
            coop_log("arrived next to the host");
        }
    }
}

bool coop_start_host()
{
    coop_stop();
    if (!open_socket((unsigned short)Coop_port))
    {
        return false;
    }
    reset_peers();
    Mode = COOP_MODE_HOST;
    My_id = 0;
    Joined = true;
    coop_log("hosting on UDP port %d", Coop_port);
    return true;
}

bool coop_start_join()
{
    coop_stop();
    if (!parse_address(Coop_join_address, Host_addr))
    {
        coop_log("bad join address \"%s\"", Coop_join_address);
        return false;
    }
    // Any free local port.
    if (!open_socket(0))
    {
        return false;
    }
    reset_peers();
    Mode = COOP_MODE_JOIN;
    My_id = -1;
    Joined = false;
    Reject_shown = false;
    Last_hello_ms = 0;
    coop_log("joining %s", Coop_join_address);
    return true;
}

void coop_stop()
{
    if (Socket != INVALID_SOCKET)
    {
        coop_header bye;
        fill_header(bye, CPT_BYE);
        if (Mode == COOP_MODE_HOST)
        {
            for (int i = 1; i < COOP_MAX_PLAYERS; i++)
            {
                if (Peers[i].used)
                {
                    send_to(Peers[i].addr, &bye, sizeof(bye));
                }
            }
        }
        else if (Mode == COOP_MODE_JOIN && Joined)
        {
            send_to(Host_addr, &bye, sizeof(bye));
        }
        closesocket(Socket);
        Socket = INVALID_SOCKET;
    }
    reset_peers();
    release_special_slot();
    Mode = COOP_MODE_OFF;
    My_id = -1;
    Joined = false;
    Teleport_pending = false;
}

coop_mode coop_get_mode()
{
    return Mode;
}

bool coop_is_free_roam()
{
    return !mission_is_active() && activity_none() && !cutscene_is_playing();
}

void coop_get_join_target_text(char* buffer, int buffer_size)
{
    if (strchr(Coop_join_address, ':') != nullptr)
    {
        snprintf(buffer, (size_t)buffer_size, "%s", Coop_join_address);
    }
    else
    {
        snprintf(buffer, (size_t)buffer_size, "%s:%d", Coop_join_address, Coop_port);
    }
}

bool coop_is_valid_join_address(const char* text)
{
    sockaddr_in addr;
    return text != nullptr && text[0] != '\0' && parse_address(text, addr);
}

// Every IPv4 address of this machine except loopback: the LAN card and virtual LAN adapters (Radmin VPN, Hamachi,
// ZeroTier, Tailscale) alike, so the host can tell a friend which one to type.
void coop_get_local_addresses(char* buffer, int buffer_size)
{
    if (buffer_size <= 0)
    {
        return;
    }
    buffer[0] = '\0';
    if (!Wsa_started)
    {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            return;
        }
        Wsa_started = true;
    }
    char host[256];
    if (gethostname(host, sizeof(host)) != 0)
    {
        return;
    }
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* list = nullptr;
    if (getaddrinfo(host, nullptr, &hints, &list) != 0)
    {
        return;
    }
    int used = 0;
    for (addrinfo* it = list; it != nullptr; it = it->ai_next)
    {
        const sockaddr_in* in = (const sockaddr_in*)it->ai_addr;
        char text[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &in->sin_addr, text, sizeof(text)) == nullptr || strncmp(text, "127.", 4) == 0)
        {
            continue;
        }
        const int written = snprintf(buffer + used, (size_t)(buffer_size - used), "%s%s", used > 0 ? ", " : "", text);
        if (written < 0 || written >= buffer_size - used)
        {
            break;
        }
        used += written;
    }
    freeaddrinfo(list);
}

// The clipboard's text (empty when it has none).
void coop_get_clipboard_text(char* buffer, int buffer_size)
{
    if (buffer_size <= 0)
    {
        return;
    }
    buffer[0] = '\0';
    if (!OpenClipboard(nullptr))
    {
        return;
    }
    HANDLE data = GetClipboardData(CF_TEXT);
    if (data != nullptr)
    {
        const char* text = (const char*)GlobalLock(data);
        if (text != nullptr)
        {
            strncpy_s(buffer, (size_t)buffer_size, text, _TRUNCATE);
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
}

// From control_gameplay.cpp: the local player jumped (action 0) or swung (1 primary, 2 secondary, 3 tertiary, 4 firearm).
void coop_note_local_action(int action)
{
    if (Mode == COOP_MODE_OFF)
    {
        return;
    }
    if (action == 0)
    {
        Local_jump_id++;
    }
    else
    {
        Local_melee_type = (unsigned char)((action - 1) & 3);
        Local_melee_id++;
    }
}

void coop_do_frame()
{
    if (Mode == COOP_MODE_OFF || Multiplayer || Socket == INVALID_SOCKET)
    {
        return;
    }
    receive_all();
    if (Mode == COOP_MODE_OFF)
    {
        return;
    }
    const unsigned int now = now_ms();
    if (Mode == COOP_MODE_JOIN && !Joined)
    {
        if (now - Last_hello_ms >= COOP_HELLO_PERIOD_MS && player_get_local_player() != nullptr)
        {
            Last_hello_ms = now;
            coop_hello hello = {};
            fill_header(hello.h, CPT_HELLO);
            const char* name = getenv("USERNAME");
            strncpy_s(hello.name, name != nullptr ? name : "player", _TRUNCATE);
            hello.character_crc = local_character_crc();
            send_to(Host_addr, &hello, sizeof(hello));
        }
        return;
    }
    process_teleport();
    // Send the local state.
    if (now - Last_state_ms >= COOP_STATE_PERIOD_MS)
    {
        Last_state_ms = now;
        coop_state st;
        if (build_local_state(st))
        {
            if (Mode == COOP_MODE_HOST)
            {
                for (int i = 1; i < COOP_MAX_PLAYERS; i++)
                {
                    if (Peers[i].used)
                    {
                        send_to(Peers[i].addr, &st, sizeof(st));
                    }
                }
            }
            else
            {
                send_to(Host_addr, &st, sizeof(st));
            }
        }
    }
    // Timeouts
    for (int i = 0; i < COOP_MAX_PLAYERS; i++)
    {
        if (!Peers[i].used || i == My_id)
        {
            continue;
        }
        if (now - Peers[i].last_recv_ms > COOP_TIMEOUT_MS)
        {
            if (Mode == COOP_MODE_JOIN && i == 0)
            {
                coop_message("Lost the connection to the host");
                coop_stop();
                return;
            }
            remove_peer(i, "lost the connection");
        }
    }
    // The other players' characters.
    for (int i = 0; i < COOP_MAX_PLAYERS; i++)
    {
        if (Peers[i].used && Peers[i].have_state && i != My_id)
        {
            update_puppet(Peers[i]);
        }
    }
}
