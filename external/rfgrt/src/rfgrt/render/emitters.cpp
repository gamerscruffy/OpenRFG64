#include "render/emitters.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "core/active.h"
#include "core/config.h"
#include "core/log.h"
#include "core/session.h"

namespace emitters {
namespace {

bool g_on = false;
float g_scale = 1.0f;       // GI.EmitterLight: every emitter's light
float g_mesh_scale = 1.0f;  // the glowing meshes' (their material's strength is GI.Emissive's)
// OpenRFG64: GI.EmitterSaturation (as 1 - it, for the core) and GI.EmitterMinRadius. A blue corona came through as
// (0.13 0.67 10.6) and a blue-white screen at its texture's colour: the room around them went blue. Sparks 0.1 m across
// lit a bench 0.2 m away at 25 times their light (1 / d^2 down to their radius): a white blow-out.
float g_grey = 0.5f;
float g_min_radius = 0.3f;

// The game's emitters as its renderer pushed them (world space), until the next frame's scene takes them.
struct Pushed {
    float position[3];
    float color[3];
    float radius;
    float range;
};
std::mutex g_mutex;
std::unordered_map<const void*, Pushed> g_pushed;  // by the game's object: the last push of each this frame

// Held from frame to frame: an emitter the game stops drawing (behind the camera, culled) fades out over kHold scenes
// instead of going dark at once (its light on the walls around you went out as you turned away).
// One that moved since its last push (a beacon on a vehicle) is held only kHoldMoving scenes: held longer, its light
// stayed behind where it blinked off while the vehicle drove on. One the game drew switched off (a zero colour) goes at once.
constexpr uint32_t kHold = 30;
constexpr uint32_t kHoldMoving = 2;
constexpr float kMoved2 = 0.02f * 0.02f;  // squared metres between pushes that count as moving
struct Held {
    Pushed p;
    uint32_t age;  // scenes since its last push
    bool moving;
};
std::unordered_map<const void*, Held> g_held;  // the scene writer's alone

// The glowing meshes of the frame being snapshot (render-offset space), from instances.cpp; and for the log, their
// material's diffuse map (the core's texture id) and Self_Illumination.
std::vector<rfgrt_frame::Emitter> g_meshes;
std::vector<std::pair<uint32_t, float>> g_mesh_info;

std::atomic<uint64_t> g_pushes{0}, g_scenes{0}, g_written{0}, g_dropped{0}, g_mesh_total{0};
std::atomic<uint32_t> g_most{0};
std::atomic<bool> g_told{false};

float luminance(const float c[3]) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; }

}  // namespace

void install(const Config& cfg) {
    g_on = cfg.gi_emitters && cfg.gi_enabled && cfg.host_enabled;
    g_scale = float(cfg.gi_emitter_light > 0.0 ? cfg.gi_emitter_light : 0.0);
    g_mesh_scale = float(cfg.gi_emitter_mesh > 0.0 ? cfg.gi_emitter_mesh : 0.0);
    if (g_scale <= 0.0f) g_on = false;
    const double sat = cfg.gi_emitter_saturation;
    g_grey = float(1.0 - (sat < 0.0 ? 0.0 : sat > 1.0 ? 1.0 : sat));
    g_min_radius = float(cfg.gi_emitter_min_radius > 0.0 ? cfg.gi_emitter_min_radius : 0.0);
    LOG("emitters: %s (GI.Emitters %d, GI.EmitterLight %.2f, GI.EmitterMesh %.2f, GI.EmitterSaturation %.2f, "
        "GI.EmitterMinRadius %.2f m): additive particles, coronas and glowing meshes light the rays, the brightest %u near "
        "the camera a frame",
        g_on ? "on" : "off", int(cfg.gi_emitters), g_scale, g_mesh_scale, 1.0 - g_grey, g_min_radius, rfgrt_frame::kMaxEmitters);
    session::add_flush_handler(&report);
}

bool enabled() { return g_on; }

void add_mesh(const float position[3], const float color[3], uint32_t texture, float self_lit) {
    if (!g_on || g_mesh_scale <= 0.0f || g_meshes.size() >= 1024) return;
    g_mesh_info.emplace_back(texture, self_lit);
    rfgrt_frame::Emitter e{};
    for (int k = 0; k < 3; ++k) {
        e.position[k] = position[k];
        e.color[k] = color[k] * g_mesh_scale;
    }
    e.radius = 0.25f;
    e.range = 12.0f;
    g_meshes.push_back(e);
}

void write(rfgrt_frame::SlotHeader* h, const float offset[3], const float camera[3]) {
    if (!h) return;
    h->emitter_count = 0;
    h->emitters_dropped = 0;
    h->emitter_grey = g_grey;
    h->pad6 = 0;
    std::vector<rfgrt_frame::Emitter> meshes;
    meshes.swap(g_meshes);
    std::vector<std::pair<uint32_t, float>> mesh_info;
    mesh_info.swap(g_mesh_info);
    if (!g_on || !active::on()) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_pushed.clear();
        g_held.clear();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& [key, held] : g_held) ++held.age;
        for (const auto& [key, p] : g_pushed) {
            if (!(luminance(p.color) > 0.0f)) {  // drawn switched off: no light, nothing to hold
                g_held.erase(key);
                continue;
            }
            const auto was = g_held.find(key);
            bool moving = false;
            if (was != g_held.end()) {
                const float dx = p.position[0] - was->second.p.position[0], dy = p.position[1] - was->second.p.position[1],
                            dz = p.position[2] - was->second.p.position[2];
                moving = dx * dx + dy * dy + dz * dz > kMoved2;
            }
            g_held[key] = Held{p, 0, moving};
        }
        g_pushed.clear();
    }
    for (auto it = g_held.begin(); it != g_held.end();)
        it = it->second.age >= (it->second.moving ? kHoldMoving : kHold) ? g_held.erase(it) : std::next(it);

    struct Cand {
        rfgrt_frame::Emitter e;
        float score;
        int what;  // -1 pushed by the game (particles, coronas), else the glowing mesh's index (mesh_info)
    };
    static std::vector<Cand> cands;
    cands.clear();
    int what = -1;
    auto consider = [&](const rfgrt_frame::Emitter& e) {
        const float dx = e.position[0] - camera[0], dy = e.position[1] - camera[1], dz = e.position[2] - camera[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        const float reach = e.range + 30.0f;  // what it lights may be in view though it is not
        if (d2 > reach * reach) return;
        const float l = luminance(e.color);
        if (!(l > 1e-4f) || !std::isfinite(l)) return;
        cands.push_back(Cand{e, l / std::max(d2, 1.0f), what});
    };
    for (const auto& [key, held] : g_held) {
        rfgrt_frame::Emitter e{};
        const float fade = 1.0f - float(held.age) / float(kHold);
        for (int k = 0; k < 3; ++k) {
            e.position[k] = held.p.position[k] - offset[k];
            e.color[k] = held.p.color[k] * g_scale * fade;
        }
        e.radius = std::max({held.p.radius, 0.05f, g_min_radius});
        e.range = std::max(held.p.range, e.radius * 2.0f);
        consider(e);
    }
    g_mesh_total += meshes.size();
    for (size_t i = 0; i < meshes.size(); ++i) {
        rfgrt_frame::Emitter& e = meshes[i];
        for (float& c : e.color) c *= g_scale;
        e.radius = std::max(e.radius, g_min_radius);
        what = i < mesh_info.size() ? int(i) : -2;
        consider(e);
    }
    what = -1;
    const size_t take = std::min<size_t>(cands.size(), rfgrt_frame::kMaxEmitters);
    std::partial_sort(cands.begin(), cands.begin() + take, cands.end(),
                      [](const Cand& a, const Cand& b) { return a.score > b.score; });
    for (size_t i = 0; i < take; ++i) {
        h->emitters[i] = cands[i].e;
        // a glowing mesh's diffuse map (id + 1) tints its light in the core; particles and coronas have their colour
        const uint32_t t = cands[i].what >= 0 ? mesh_info[size_t(cands[i].what)].first : 0xFFFFFFFFu;
        h->emitter_texture[i] = t < 0xFFFFu ? t + 1u : 0u;
    }
    h->emitter_count = uint32_t(take);
    h->emitters_dropped = uint32_t(cands.size() - take);
    ++g_scenes;
    g_written += take;
    g_dropped += cands.size() - take;
    if (take > g_most.load()) g_most = uint32_t(take);
    // OpenRFG64 diagnosis: every 600 scenes, the brightest lights within 4 m of the camera and what they are (a light
    // that follows the player is one of these)
    if (take && g_scenes.load() % 600 == 0) {
        char line[1024];
        size_t at = 0;
        int shown = 0;
        for (size_t i = 0; i < take && shown < 4 && at < sizeof(line) - 200; ++i) {
            const Cand& c = cands[i];
            const float dx = c.e.position[0] - camera[0], dy = c.e.position[1] - camera[1], dz = c.e.position[2] - camera[2];
            const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d > 4.0f) continue;
            char kind[96];
            if (c.what >= 0)
                snprintf(kind, sizeof(kind), "glowing mesh (texture %u, strength %.2f)", mesh_info[size_t(c.what)].first,
                         mesh_info[size_t(c.what)].second);
            else if (c.what == -1)
                snprintf(kind, sizeof(kind), "%s", c.e.range == 15.0f ? "corona" : "particles");
            else
                snprintf(kind, sizeof(kind), "glowing mesh");
            at += size_t(snprintf(line + at, sizeof(line) - at, "%s%s %.1f m away (%.1f m up), light (%.2f %.2f %.2f), radius %.2f m",
                                  shown ? "; " : "", kind, d, dy, c.e.color[0], c.e.color[1], c.e.color[2], c.e.radius));
            ++shown;
        }
        if (shown) LOG("emitters: near the camera (the brightest %d within 4 m): %s", shown, line);
    }
    if (take && !g_told.exchange(true))
        LOG("emitters: the first scene with emitters: %zu of %zu, the brightest (%.2f %.2f %.2f), radius %.2f m, range %.1f m",
            take, cands.size(), cands[0].e.color[0], cands[0].e.color[1], cands[0].e.color[2], cands[0].e.radius,
            cands[0].e.range);
}

void report(bool) {
    const uint64_t scenes = g_scenes.load();
    LOG("emitters: %llu pushed by the game, %llu glowing meshes; %llu scenes, %.1f emitters a scene (at most %u), %llu "
        "left out (past %u)",
        (unsigned long long)g_pushes.load(), (unsigned long long)g_mesh_total.load(), (unsigned long long)scenes,
        scenes ? double(g_written.load()) / double(scenes) : 0.0, g_most.load(), (unsigned long long)g_dropped.load(),
        rfgrt_frame::kMaxEmitters);
}

}  // namespace emitters

// OpenRFG64: the game's renderer (src/backends/rt/rt_backend.cpp openrfg_rt_add_emitter) pushes what it draws that glows.
extern "C" __declspec(dllexport) void rfgrt_add_emitter(const float* position, const float* color, float radius, float range,
                                                        const void* key) {
    using namespace emitters;
    if (!g_on || !position || !color || !active::on()) return;
    if (!std::isfinite(position[0] + position[1] + position[2] + color[0] + color[1] + color[2] + radius + range)) return;
    // a zero colour is still pushed: it is drawn switched off (a blinking beacon), and its held light goes out (write)
    Pushed p;
    memcpy(p.position, position, sizeof(p.position));
    for (int k = 0; k < 3; ++k) p.color[k] = std::max(color[k], 0.0f);
    p.radius = radius;
    p.range = range;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_pushed.size() < 4096) g_pushed[key] = p;
    g_pushes.fetch_add(1, std::memory_order_relaxed);
}
