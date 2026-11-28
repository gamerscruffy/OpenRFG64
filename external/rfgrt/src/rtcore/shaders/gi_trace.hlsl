// Global illumination (research/docs/04 section 6.1), pass 1 of 3: irradiance by path tracing. From each pixel's
// surface (position and normal from the game's depth) a few cosine-weighted rays a frame: one that escapes sees the
// sky, one that hits a surface sees the light that surface sends back (trace_common.hlsli: what the game drew there,
// else a diffuse surface under the sky and the sun). The irradiance goes to `traced` in the game's units: an open
// surface facing up gets the game's own ambient colour, so that the patched shaders, which use it as their ambient,
// keep the game's balance. Passes 2 and 3 average it over frames and neighbours (or DLSS Ray Reconstruction, which with
// a mode below DLAA gets the rays at its render size, each pixel tracing from the frame's pixel under a jittered point
// of it: gi_source, and upscales them).

#include "trace_common.hlsli"

// The GI census (Telemetry.GiCensus, the core's log): on every 8th pixel in x and y, by the kind of surface (facing up,
// sideways or down; in the sun or not), sums of luminances into the frame's counters from uint 100, five per class: the
// pixels, the traced irradiance (this pass's, before the denoiser: noisy, its mean unbiased), the game's lit colour
// there (the previous frame's), the sun's irradiance (a shadow ray) and the game's own hemispherical ambient for that
// normal (what the patch replaced, without its fake bounce). x 256, each value capped at 64.
RWStructuredBuffer<uint> frame_counts : register(u6);

void census_add(uint cls, float e, float lit, float sun_e, float game) {
    const uint at = 100u + cls * 5u;
    InterlockedAdd(frame_counts[at], 1u);
    InterlockedAdd(frame_counts[at + 1u], uint(clamp(e, 0.0, 64.0) * 256.0));
    InterlockedAdd(frame_counts[at + 2u], uint(clamp(lit, 0.0, 64.0) * 256.0));
    InterlockedAdd(frame_counts[at + 3u], uint(clamp(sun_e, 0.0, 64.0) * 256.0));
    InterlockedAdd(frame_counts[at + 4u], uint(clamp(game, 0.0, 64.0) * 256.0));
}

// Classes: 0 up in the sun, 1 up in shade, 2 sideways in the sun, 3 sideways in shade, 4 down (ceilings, the undersides
// of things: in shade).
void census(int2 px, float3 n, float3 origin, float3 e) {
    const float3 lw = float3(0.2126, 0.7152, 0.0722);
    const float sun_l = dot(sun_at(origin, n), lw);
    const float lit_l = (flags & 16) != 0 ? dot(max(radiance[px].rgb, 0.0), lw) : 0.0;
    const float game_l = dot(lerp(back_ambient.rgb, ambient.rgb, saturate(0.5 + 0.5 * n.y)), lw);
    uint cls;
    if (n.y > 0.7)
        cls = sun_l > 0.0 ? 0u : 1u;
    else if (n.y < -0.7)
        cls = 4u;
    else
        cls = sun_l > 0.0 ? 2u : 3u;
    census_add(cls, dot(e, lw), lit_l, sun_l, game_l);
}

// OpenRFG64: the frame's emitters (frame_scene.h Emitter; render/emitters.h): additive particles, coronas, glowing
// meshes. x: how many; then two float4 each: position and radius, light colour and range.
cbuffer Emitters : register(b2) {
    uint4 emitter_head;
    float4 emitter_data[128];
    uint4 emitter_tex[16];  // each emitter's texture id + 1 (0: none), four to a row
};

// How much emitter i may give the surface at p facing n (its light's luminance x cos / d^2, faded out toward its range):
// the odds it is picked.
float emitter_weight(uint i, float3 p, float3 n) {
    const float4 pr = emitter_data[i * 2u], cr = emitter_data[i * 2u + 1u];
    const float3 v = pr.xyz - p;
    const float d2 = dot(v, v);
    if (d2 >= cr.w * cr.w) return 0.0;
    const float c = dot(n, v) * rsqrt(max(d2, 1e-8));
    if (c <= 0.0) return 0.0;
    const float f = 1.0 - (d2 * d2) / (cr.w * cr.w * cr.w * cr.w);  // (1 - (d/range)^4)
    return dot(cr.rgb, float3(0.2126, 0.7152, 0.0722)) * c * f * f / max(d2, pr.w * pr.w);
}

// The emitters' light at p (irradiance, the game's units): one picked by its weight, a shadow ray toward a point within
// its sphere (stopping short of it: a lamp's own mesh holds it), its light over the odds it was picked. Converges a
// frame at a time with the GI's history and its denoiser, where a ray happening on a small light flickered.
float3 emitter_light(float3 p, float3 n, uint seed) {
    const uint count = min(emitter_head.x, 64u);
    if (count == 0u) return 0.0;
    float total = 0.0;
    for (uint i = 0; i < count; ++i) total += emitter_weight(i, p, n);
    if (!(total > 0.0)) return 0.0;
    const float pick = unit(seed) * total;
    float run = 0.0;
    uint j = count - 1u;
    float wj = 0.0;
    for (uint k = 0; k < count; ++k) {
        const float w = emitter_weight(k, p, n);
        run += w;
        if (w > 0.0 && pick <= run) {
            j = k;
            wj = w;
            break;
        }
    }
    if (!(wj > 0.0)) {
        wj = emitter_weight(j, p, n);
        if (!(wj > 0.0)) return 0.0;
    }
    const float4 pr = emitter_data[j * 2u], cr = emitter_data[j * 2u + 1u];
    const uint s1 = hash(seed ^ 0x5bd1e995u), s2 = hash(s1 + 0x9E3779B9u), s3 = hash(s2 + 0x7F4A7C15u);
    const float3 jitter = (float3(unit(s1), unit(s2), unit(s3)) * 2.0 - 1.0) * (0.5 * pr.w);
    const float3 target = pr.xyz + jitter;
    const float3 v = target - p;
    const float dist = length(v);
    if (!(dist > 1e-3)) return 0.0;
    RayDesc ray;
    ray.Origin = p;
    ray.Direction = v / dist;
    ray.TMin = 0.0;
    ray.TMax = max(dist - pr.w, 0.0);
    if (ray.TMax > 0.0) {
        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
        q.TraceRayInline(scene, RAY_FLAG_NONE, 0x01, ray);
        RT_PROCEED(q);
        if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) return 0.0;
    }
    // E / (wj / total): the picked emitter's light x cos x fade / d^2 is wj x its colour over its luminance
    const float l = dot(cr.rgb, float3(0.2126, 0.7152, 0.0722));
    float3 e = cr.rgb * (total / max(l, 1e-6));
    // a glowing mesh's light in its texture's average colour (a screen's picture; its last mip), not its material's
    // plain colour; a texture the core has not got yet reads all zero: the colour alone then
    const uint tex = emitter_tex[j >> 2u][j & 3u];
    if (tex != 0u && tex <= kMaterialTextures) {
        const float4 m = material_textures[NonUniformResourceIndex(tex - 1u)].SampleLevel(material_sampler, float2(0.5, 0.5), 16.0);
        if (any(m != 0.0) && all(m == m)) e *= saturate(m.rgb);
    }
    return all(isfinite(e)) ? e : 0.0;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= gi_size())) return;
    float3 p, n;
    if (!surface_at(gi_source(id.xy), p, n)) {
        traced[id.xy] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    const float dist = length(p - cam_origin.xyz);
    const float3 origin = p + n * (0.02 + 0.002 * dist);
    const uint rays = max(uint(gi.x), 1u);
    float3 sum = 0.0;
    for (uint i = 0; i < rays; ++i) {
        const uint h1 = hash((id.x * 73856093u) ^ (id.y * 19349663u) ^ ((shadow.w * 16u + i + 7u) * 83492791u));
        const uint h2 = hash(h1 + 0x9E3779B9u);
        const float3 dir = cosine_dir(n, unit(h1), unit(h2));
        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = dir;
        ray.TMin = 0.0;
        ray.TMax = gi.y;
        RayQuery<RAY_FLAG_NONE> q;
        q.TraceRayInline(scene, RAY_FLAG_NONE, 0x01, ray);
        Panes panes = panes_none();
        RT_PROCEED_PANES(q, panes, kDiffuseSpread);
        const bool hit = q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
        // the clear glass it crossed tints what it brings back (GI.WindowTint)
        const float3 through = panes_through(panes, hit ? q.CommittedRayT() : gi.y);
        float3 l = panes_glow(panes, hit ? q.CommittedRayT() : gi.y);  // OpenRFG64: the glow halos it crossed (GI.Glow)
        if (!hit)
            l += through * gi2.y * sky_radiance(dir);  // GI.Sky: the sky as a light
        else
            l += through * gi2.x * shade_hit(origin, dir, q.CommittedRayT(), q.CommittedInstanceID(), q.CommittedGeometryIndex(),
                                               q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(),
                                               q.CommittedObjectToWorld3x4(), h2 ^ 0x68E31DA4u);
        sum += ray_clamp(l);  // OpenRFG64: GI.Clamp
    }
    float3 e = PI * sum / float(rays);  // cosine-weighted: E = pi * the mean radiance
    // OpenRFG64: the emitters' light, sampled toward them (GI.Emitters), at most GI.Clamp x pi
    e += PI * ray_clamp(emitter_light(origin, n, hash((id.x * 2654435761u) ^ (id.y * 40503u) ^ (shadow.w * 977u) ^ 0xA511E9B3u)) / PI);
    // OpenRFG64: nothing not finite goes on into the history and the denoiser's (it stayed there, the light tinted)
    if (!all(isfinite(e))) e = float3(0.0, 0.0, 0.0);
    traced[id.xy] = float4(e, 1.0);
    if ((flags & 128) != 0 && all((id.xy & 7u) == 0u)) census(gi_source(id.xy), n, origin, e);
}
