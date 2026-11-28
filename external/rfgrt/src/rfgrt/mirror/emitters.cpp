#include "mirror/emitters.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

#include "core/log.h"
#include "frame_scene.h"
#include "hooks/binding.h"
#include "mirror/textures.h"

namespace emitters {

namespace {

std::atomic<bool> g_enabled{false};
std::mutex g_mutex;
std::vector<rfgrt_emitter_quad> g_drawn;  // since the last collect
std::vector<rfgrt_emitter_quad> g_frame;  // the frame's, for its scene's worker

// Totals for the log.
uint64_t g_frames = 0, g_frames_with = 0, g_written = 0, g_textured = 0, g_waiting = 0, g_no_texture = 0, g_dropped = 0,
         g_over = 0, g_most = 0;

}  // namespace

void set_enabled(bool on) { g_enabled = on; }
bool enabled() { return g_enabled.load(); }

void add(const rfgrt_emitter_quad* quads, uint32_t count) {
    if (!g_enabled.load() || !quads || !count) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    // (a frame's more than twice what a slot holds: left out here already)
    if (g_drawn.size() + count > 2 * rfgrt_frame::kMaxEmitters) {
        g_over += count;
        return;
    }
    g_drawn.insert(g_drawn.end(), quads, quads + count);
}

void collect() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_frame.swap(g_drawn);
    g_drawn.clear();
}

void write(rfgrt_frame::SlotHeader* h) {
    h->emitter_count = 0;
    h->emitters_dropped = 0;
    h->pad6[0] = h->pad6[1] = 0;
    if (!g_enabled.load()) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    uint32_t n = 0, dropped = 0;
    for (const rfgrt_emitter_quad& q : g_frame) {
        uint32_t texture = 0;
        if (q.bitmap >= 0) {
            // its bitmap as the materials' textures go to the core; one not there yet waits (a frame or two)
            const uint32_t id = textures::id_of(uint32_t(q.bitmap));
            if (id == textures::kNone) {
                ++g_no_texture;
                continue;
            }
            if (textures::state(id) != textures::kSent) {
                ++g_waiting;
                continue;
            }
            texture = id + 1;
            ++g_textured;
        }
        if (n >= rfgrt_frame::kMaxEmitters) {
            ++dropped;
            continue;
        }
        rfgrt_frame::Emitter& e = h->emitters[n++];
        for (int k = 0; k < 3; ++k) {
            e.center[k] = q.center[k] - h->render_offset[k];
            e.axis_u[k] = q.axis_u[k];
            e.axis_v[k] = q.axis_v[k];
            e.color[k] = q.color[k];
        }
        e.texture = texture;
        e.alpha = q.alpha;
        e.flags = q.flags;
        e.pad = 0.0f;
        memcpy(e.uv, q.uv, sizeof(e.uv));
    }
    h->emitter_count = n;
    h->emitters_dropped = dropped;
    ++g_frames;
    g_frames_with += n ? 1 : 0;
    g_written += n;
    g_dropped += dropped;
    g_most = n > g_most ? n : g_most;
}

void log_stats() {
    if (!g_enabled.load()) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    LOG("emitters (GI.Emitters): the particles that give light in %llu of %llu frame scenes, %.1f a frame (%llu at most), "
        "%llu of them with their bitmap; left out: their bitmap on its way to the core %llu, no bitmap for the core %llu, past "
        "the slot's %u %llu, past twice that drawn in a frame %llu",
        static_cast<unsigned long long>(g_frames_with), static_cast<unsigned long long>(g_frames),
        g_frames ? double(g_written) / double(g_frames) : 0.0, static_cast<unsigned long long>(g_most),
        static_cast<unsigned long long>(g_textured), static_cast<unsigned long long>(g_waiting),
        static_cast<unsigned long long>(g_no_texture), rfgrt_frame::kMaxEmitters, static_cast<unsigned long long>(g_dropped),
        static_cast<unsigned long long>(g_over));
}

}  // namespace emitters
