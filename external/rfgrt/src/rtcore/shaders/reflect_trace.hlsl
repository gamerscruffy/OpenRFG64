// Reflections (research/docs/04 section 6.1), pass 1 of 3: what each pixel's specular reflects. The game's lit shaders
// take their specular ambient as its hemispherical sky looked up along the reflection vector (lerp(backAmbient, ambient,
// R.up), the fake bounce blended in), times the specular map, the fresnel and indirectLightParams.z; patched
// (patch/ambient.h, patch_reflection), they read this instead at t16: what a ray along the reflection vector meets, in
// the same units, pi times its radiance (the sky the game's own value, a surface the game drew its lit colour).
//
// One ray a pixel and frame, about the mirror direction off the surface's geometric normal (the normal map's detail
// stays the shader's own, in its fresnel and specular map), spread over a cosine-power lobe of exponent gi.x; passes 2
// and 3 (refl_temporal, gi_filter) average it over frames and neighbours.
// OpenRFG64: the normal is the triangle's under the pixel (a ray from the camera to the depth's point), the depth's
// neighbours' only where that ray meets something else (the TLAS and the depth disagree: skinning, LODs): from the
// depth alone it changed with DLAA's jitter at every edge and far away (its precision), and the reflection flickered.
// What the ray hits is shaded as a mirror shows it (mirror_radiance: what the game drew there, no random ray, no
// bounce floor), not as the GI's rays are (shade_hit: a random bounce, the albedo floors GI.SunBounceAlbedo and
// GI.ShadeBounceAlbedo): those made the reflected surfaces brighter than the game draws them and noisy from frame to
// frame, so the reflections stood out from the shaders around them and flickered.
// traced.a: the hit's distance along the ray (gi.y where it gets out to the sky), for refl_temporal's reprojection of
// the reflected point; 0 where there is no surface.

#include "trace_common.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= size)) return;
    float3 p, n;
    if (!surface_at(int2(id.xy), p, n)) {
        traced[id.xy] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    const float3 v = normalize(p - cam_origin.xyz);
    {
        const float zd = length(p - cam_origin.xyz);
        RayDesc pr;
        pr.Origin = cam_origin.xyz;
        pr.Direction = v;
        pr.TMin = 0.0;
        pr.TMax = zd * 1.05 + 0.2;
        RayQuery<RAY_FLAG_NONE> pq;
        pq.TraceRayInline(scene, RAY_FLAG_NONE, 0x01, pr);
        RT_PROCEED(pq);
        if (pq.CommittedStatus() == COMMITTED_TRIANGLE_HIT && abs(pq.CommittedRayT() - zd) <= max(zd * 0.03, 0.1)) {
            const float3 nt = hit_normal(pq.CommittedInstanceID(), pq.CommittedGeometryIndex(), pq.CommittedPrimitiveIndex(),
                                         pq.CommittedObjectToWorld3x4(), v);
            if (dot(nt, -v) > 0.0) n = nt;
        }
    }
    const float3 r0 = reflect(v, n);
    const uint h1 = hash((id.x * 73856093u) ^ (id.y * 19349663u) ^ ((shadow.w * 16u + 11u) * 83492791u));
    const uint h2 = hash(h1 + 0x9E3779B9u);
    const float u1 = float(h1 & 0xFFFFFFu) / 16777216.0, u2 = float(h2 & 0xFFFFFFu) / 16777216.0;
    const float e = max(gi.x, 1.0);
    const float cos_t = pow(max(u1, 1e-6), 1.0 / (e + 1.0)), sin_t = sqrt(max(1.0 - cos_t * cos_t, 0.0));
    const float phi = 6.2831853 * u2;
    const float3 t1 = normalize(abs(r0.y) < 0.99 ? cross(r0, float3(0, 1, 0)) : cross(r0, float3(1, 0, 0)));
    const float3 b1 = cross(r0, t1);
    float3 dir = normalize(t1 * (sin_t * cos(phi)) + b1 * (sin_t * sin(phi)) + r0 * cos_t);
    if (dot(dir, n) <= 0.0) dir = r0;  // below the surface: the mirror direction itself
    const float dist = length(p - cam_origin.xyz);
    const float3 origin = p + n * (0.02 + 0.002 * dist);
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = 0.0;
    ray.TMax = gi.y;
    RayQuery<RAY_FLAG_NONE> q;
    q.TraceRayInline(scene, RAY_FLAG_NONE, 0x01, ray);
    Panes panes = panes_none();
    RT_PROCEED_PANES(q, panes, kDiffuseSpread);
    float3 env;
    const bool hit = q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    const float3 through = panes_through(panes, hit ? q.CommittedRayT() : gi.y);  // the clear glass it crossed (GI.WindowTint)
    if (!hit)
        env = PI * sky_drawn(dir, lerp(back_ambient.rgb, ambient.rgb, saturate(dir.y * 0.5 + 0.5)) / PI, ambient.rgb / PI);  // the sky
    else  // the lobe's spread (about 1/sqrt(e/2) radians) blurs the hit's texture as much as the reflection is blurred
        env = PI * mirror_radiance(origin, dir, q.CommittedRayT(), q.CommittedInstanceID(), q.CommittedGeometryIndex(),
                                   q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(), q.CommittedObjectToWorld3x4(),
                                   float2(0.002 * dist, 0.5 * rsqrt(max(e * 0.5, 1.0))));
    // OpenRFG64: the glow halos it crossed on the way (GI.Glow); nothing not finite kept for the history
    const float3 r = ray_clamp(through * env + PI * panes_glow(panes, hit ? q.CommittedRayT() : gi.y));  // OpenRFG64: GI.Clamp
    traced[id.xy] = float4(all(isfinite(r)) ? r : float3(0.0, 0.0, 0.0), max(hit ? q.CommittedRayT() : gi.y, 1e-3));
}
