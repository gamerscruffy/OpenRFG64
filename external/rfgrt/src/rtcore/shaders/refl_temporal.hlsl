// Reflections, pass 2 of 3 (OpenRFG64): the traced reflections accumulated over frames. GI's temporal pass
// (gi_temporal.hlsl), used for them before, follows the surface: right for the light on it, wrong for what it reflects,
// which moves across a mirror as the camera does (the reflection smeared and lagged), and its cut where the light
// changed, tuned for the irradiance, fired at the edges of every reflected object (the history kept 3 frames there:
// flicker). Here:
//  - the history is looked up where the reflected point was (the virtual point: the surface's point carried on along the
//    view ray by the hit's distance, traced.a), else where the surface was; either only where the previous frame's
//    depth there agrees with the surface's
//  - and clamped to this frame's traced values around the pixel (3x3: their mean +- 1.5 standard deviations), so what
//    moved in the reflection or a light that changed lets go of the old value at once, without cutting the history
//    short elsewhere
//  - over up to gi.w frames (Reflections.History).
// The bindings and constants are gi_temporal's (the same descriptor table layout).
//
// history: rgb the accumulated reflection, a the number of frames in it.

Texture2D<float4> traced : register(t0);
Texture2D<float> depth : register(t1);
Texture2D<float> prev_depth : register(t2);
Texture2D<float4> prev_history : register(t3);
RWTexture2D<float4> history : register(u0);

cbuffer Frame : register(b0) {
    float4 cam_right;
    float4 cam_up;
    float4 cam_forward;
    float4 cam_origin;
    float4 proj;
    float4 range;
    float4 sun;
    uint2 size;
    float tolerance;
    uint flags;           // 2: reversed depth; 8: no history
    uint4 shadow;
    float4 reproject[4];  // render-offset space now -> the previous frame's clip space (row-vector)
    float4 prev;          // the previous frame's P22, P32, depth range end
    float4 ambient;
    float4 back_ambient;
    float4 sun_color;
    float4 gi;            // w: max frames averaged
};

float3 view_ray(int2 p) {
    const float2 ndc = float2((p.x + 0.5) / size.x * 2.0 - 1.0, 1.0 - (p.y + 0.5) / size.y * 2.0);
    return (ndc.x / proj.x) * cam_right.xyz + (ndc.y / proj.y) * cam_up.xyz + cam_forward.xyz;
}

// Where `w` (render-offset space) was on screen in the previous frame: false off screen or behind the camera.
bool previous_pixel(float3 w, out uint2 q, out float cw) {
    q = uint2(0, 0);
    const float4 c = w.x * reproject[0] + w.y * reproject[1] + w.z * reproject[2] + reproject[3];
    cw = c.w;
    if (c.w <= 1e-3) return false;
    const float2 ndc = c.xy / c.w;
    const float2 pix = float2((ndc.x + 1.0) * 0.5 * size.x, (1.0 - ndc.y) * 0.5 * size.y);
    if (any(pix < 0.0) || any(pix >= float2(size))) return false;
    q = uint2(pix);
    return true;
}

// The previous frame's view depth at q (1e9: sky).
float previous_depth(uint2 q) {
    const float pd = prev_depth[q] / prev.z;
    const bool reversed = (flags & 2) != 0;
    return (reversed ? pd <= 0.0 : pd >= 1.0) ? 1e9 : prev.y / (pd - prev.x);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= size)) return;
    const float4 t0 = traced[id.xy];
    const float3 v = t0.rgb;
    const float d = depth[id.xy] / range.x;
    const bool reversed = (flags & 2) != 0;
    const bool surface = !(reversed ? d <= 0.0 : d >= 1.0) && t0.a > 0.0;
    float4 h = float4(v, 1.0);
    if (surface && !(flags & 8)) {
        const float z = proj.w / (d - proj.z);
        const float3 ray = view_ray(int2(id.xy));
        const float3 p = cam_origin.xyz + ray * z;
        uint2 qs, qv;
        float ws, wv;
        const bool on_s = previous_pixel(p, qs, ws);
        // the surface's previous view depth (ws) is what the previous frame's depth must show wherever the history is
        // taken from: the same surface
        const float tol = max(ws * 0.03, 0.05);
        bool found = false;
        uint2 q = qs;
        if (on_s && previous_pixel(p + normalize(ray) * t0.a, qv, wv) && abs(previous_depth(qv) - ws) <= tol * 2.0) {
            q = qv;
            found = true;
        } else if (on_s && abs(previous_depth(qs) - ws) <= tol) {
            found = true;
        }
        if (found) {
            float4 old = prev_history[q];
            // this frame's neighbourhood: the old value clamped to it
            float3 m1 = 0.0, m2 = 0.0;
            float k = 0.0;
            [unroll] for (int dy = -1; dy <= 1; ++dy) {
                [unroll] for (int dx = -1; dx <= 1; ++dx) {
                    const int2 s = clamp(int2(id.xy) + int2(dx, dy), int2(0, 0), int2(size) - 1);
                    const float4 t = traced[uint2(s)];
                    if (t.a > 0.0) {
                        m1 += t.rgb;
                        m2 += t.rgb * t.rgb;
                        k += 1.0;
                    }
                }
            }
            if (k > 0.0) {
                const float3 mean = m1 / k;
                const float3 sd = sqrt(max(m2 / k - mean * mean, 0.0));
                const float3 lo = mean - 1.5 * sd, hi = mean + 1.5 * sd;
                const float3 kept = clamp(old.rgb, lo, hi);
                // clamped hard: what is in the history no longer there, its count starts over (most of it)
                const float3 lw = float3(0.2126, 0.7152, 0.0722);
                const float moved = abs(dot(kept - old.rgb, lw)) / (dot(max(kept, old.rgb), lw) + 1e-3);
                old.rgb = kept;
                if (moved > 0.5) old.a = min(old.a, 2.0);
            }
            const float n = min(old.a + 1.0, max(gi.w, 1.0));
            h = float4(lerp(old.rgb, v, 1.0 / n), n);
        }
    }
    if (!all(isfinite(h))) h = float4(all(isfinite(v)) ? v : float3(0.0, 0.0, 0.0), 1.0);
    history[id.xy] = h;
}
