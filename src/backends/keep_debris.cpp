// OpenRFG (VOM, new code): KEEP DEBRIS in the game's source (keep_debris.h). Ported from OpenRFG64-memtest's
// memtest_debris.cpp (its keep_debris and zone budget parts).

#include "backends/keep_debris.h"

#include "backends/backends.h"
#include "volition/rfg/code/camera/rfg_camera.h"
#include "volition/rfg/code/gamewide/game_render.h"
#include "volition/rfg/code/havok/havok.h"
#include "volition/rfg/code/level/world_internal.h"
#include "volition/rfg/code/multi/multi_core/include/multi.h"
#include "volition/rfg/code/objects/movers/general_mover.h"
#include "volition/rfg/code/objects/movers/object_mover.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <stdio.h>
#include <vector>

void openrfg_log(const char* text); // crash_early.cpp

namespace
{
    const int Zone_handles = 7280; // handle_manager<7280> in every world_zone
    const int Zone_reserve = 1200; // handles kept free in the fullest zone (rfgphys's ZoneReserve)
    const int Recycle_batch = 64; // recycle at least this many at a time

    std::atomic<int> Zone_free{Zone_handles}; // free handles in the fullest zone, less the pieces kept since
    int Zone_used; // the fullest zone's handles in use at the last frame's end
    thread_local int Fade_scope; // > 0: inside a game cleanup whose fades go through
    bool Recycling; // sim thread: our own fades
    bool Recycle_logged;

    std::atomic<unsigned int> Fades_blocked, Deletes_kept, Deletes_gated, Vaporize_refused, Recycled;
    unsigned long long Next_log_ms;
    unsigned int Logged_sum;

    bool keep_on()
    {
        return openrfg_backends_keep_debris_now() && !Multiplayer;
    }

    bool object_visible(object_mover* mover)
    {
        if (!object_mover_is_visible(mover))
        {
            return false;
        }
        vector bmin;
        vector bmax;
        mover->get_bbox(bmin, bmax);
        return game_render_is_object_visible(bmin, bmax);
    }

    struct candidate
    {
        float d2;
        general_mover* gm;
    };
    std::vector<candidate> Candidates; // sim thread
    unsigned int Rng = 0x9E3779B9u;

    // Frees `need` handles in the zone with the given gid: the debris farthest from the camera first (pieces already
    // fading or destroyed count).
    void recycle(unsigned short gid, int need)
    {
        const int n = World.get_num_objects<general_mover>();
        int in_zone = 0;
        int leaving = 0;
        Candidates.clear();
        const vector& cam = Rfg_camera.real_pos;
        for (int i = 0; i < n; i++)
        {
            general_mover* gm = World.get_object<general_mover>(i);
            if (gm == nullptr || (gm->get_handle() >> 16) != gid)
            {
                continue;
            }
            in_zone++;
            if ((gm->mover_flags & 0x4) == 0) // load balanced: debris the governor may delete
            {
                continue;
            }
            if (gm->obj_flags.destroyed || object_mover_is_fading_for_delete(gm))
            {
                leaving++;
                continue;
            }
            const float dx = gm->pos.x - cam.x;
            const float dy = gm->pos.y - cam.y;
            const float dz = gm->pos.z - cam.z;
            Candidates.push_back({dx * dx + dy * dy + dz * dz, gm});
        }
        if (!Recycle_logged)
        {
            Recycle_logged = true;
            char msg[256];
            snprintf(msg, sizeof(msg), "KEEPDEBRIS world zone %u is %d handles short of its reserve: %d general movers in it, %d of them debris (%d already leaving); recycling the farthest",
                     (unsigned int)gid, need, in_zone, (int)Candidates.size() + leaving, leaving);
            openrfg_log(msg);
        }
        need -= leaving;
        if (need <= 0 || Candidates.empty())
        {
            return;
        }
        const size_t take = (size_t)need < Candidates.size() ? (size_t)need : Candidates.size();
        std::nth_element(Candidates.begin(), Candidates.begin() + (ptrdiff_t)(take - 1), Candidates.end(),
                         [](const candidate& a, const candidate& b) { return a.d2 > b.d2; });
        // Havok out of memory: the game removes instead of fading (rfgl_find_and_delete_object_mover).
        const bool remove_all = havok_get_status(HAVOK_SYSTEM_MEMORY) == HAVOK_STATUS_ERROR;
        unsigned int done = 0;
        Recycling = true;
        for (size_t i = 0; i < take; i++)
        {
            general_mover* gm = Candidates[i].gm;
            if (gm->has_script_point_effect(0xFFFFFFFF, nullptr))
            {
                continue;
            }
            if (!remove_all && object_visible(gm))
            {
                Rng = Rng * 1664525u + 1013904223u;
                object_mover_mark_for_fade(gm, (int)((Rng >> 8) % 2000u)); // like the game: within 2 s
            }
            else
            {
                World.destroy_object(gm, false);
            }
            done++;
        }
        Recycling = false;
        Recycled.fetch_add(done, std::memory_order_relaxed);
    }

    // What keep debris did, every minute while it changes.
    void log_counters()
    {
        const unsigned long long now = GetTickCount64();
        if (now < Next_log_ms)
        {
            return;
        }
        Next_log_ms = now + 60000;
        const unsigned int kept = Deletes_kept.load(std::memory_order_relaxed);
        const unsigned int gated = Deletes_gated.load(std::memory_order_relaxed);
        const unsigned int fades = Fades_blocked.load(std::memory_order_relaxed);
        const unsigned int vaporize = Vaporize_refused.load(std::memory_order_relaxed);
        const unsigned int recycled = Recycled.load(std::memory_order_relaxed);
        const unsigned int sum = kept + gated + fades + vaporize + recycled;
        if (sum == Logged_sum)
        {
            return;
        }
        Logged_sum = sum;
        char msg[256];
        snprintf(msg, sizeof(msg), "KEEPDEBRIS pieces kept from shatter deletes %u (%u still deleted: zone reserve), fades refused %u, vaporizing refused %u, recycled %u; the fullest zone uses %d of %d handles",
                 kept, gated, fades, vaporize, recycled, Zone_used, Zone_handles);
        openrfg_log(msg);
    }
}

bool openrfg_keep_debris_keeping()
{
    return keep_on() && Zone_free.load(std::memory_order_relaxed) > Zone_reserve / 2;
}

bool openrfg_keep_debris_fade_allowed()
{
    if (Recycling || Fade_scope > 0 || !openrfg_keep_debris_keeping())
    {
        return true;
    }
    Fades_blocked.fetch_add(1, std::memory_order_relaxed);
    return false;
}

openrfg_keep_debris_fade_scope::openrfg_keep_debris_fade_scope()
{
    Fade_scope++;
}

openrfg_keep_debris_fade_scope::~openrfg_keep_debris_fade_scope()
{
    Fade_scope--;
}

// rfgl_desired_values: the pieces a shatter deletes at once. delete_count is not capped at the pieces (after a big
// shatter it is several times their number), so only up to `pieces` of it is real. The pieces the game creates anyway,
// then the extra ones that fit while half the zone reserve stays free, are kept.
int openrfg_keep_debris_shatter_deletes(int pieces, int deletes)
{
    if (!keep_on())
    {
        return deletes;
    }
    const int n = pieces > 0 ? pieces : 0;
    const int real = deletes < 0 ? 0 : deletes > n ? n : deletes;
    const int created = n - real;
    const int room = Zone_free.load(std::memory_order_relaxed) - Zone_reserve / 2 - created;
    const int kept = room <= 0 ? 0 : room < real ? room : real;
    Zone_free.fetch_sub(created + kept, std::memory_order_relaxed);
    if (kept > 0)
    {
        Deletes_kept.fetch_add((unsigned int)kept, std::memory_order_relaxed);
    }
    if (real > kept)
    {
        Deletes_gated.fetch_add((unsigned int)(real - kept), std::memory_order_relaxed);
    }
    return real - kept;
}

bool openrfg_keep_debris_refuse_vaporize()
{
    if (!openrfg_keep_debris_keeping())
    {
        return false;
    }
    Vaporize_refused.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void openrfg_keep_debris_sim_frame()
{
    int used = 0;
    const world_zone* zone = World.openrfg_fullest_zone(&used);
    Zone_used = used;
    const int free = Zone_handles - used;
    Zone_free.store(free, std::memory_order_relaxed);
    if (keep_on() && zone != nullptr && free < Zone_reserve)
    {
        recycle(zone->get_gid(), Zone_reserve + Recycle_batch - free);
    }
    if (keep_on())
    {
        log_counters();
    }
}
