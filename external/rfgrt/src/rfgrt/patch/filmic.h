#pragma once
// OpenRFG64: a filmic tone curve over the game's (Tonemap.Filmic). rl_hdr's final composites (composite_tonemapped_bloom,
// composite_bloom_after_tonemapping, composite_tonemapping_no_bloom: the shaders that bind Particle_sampler and
// Lum_adapt_sampler) end their colour with
//   exp  rE.xyz, -k x (scene + bloom) + log(1 - particles)
//   add  D.xyz, -rE.xyz, l(1, 1, 1, *)          y = 1 - (1 - particles) e^(-k x)
// a per-channel exponential with no toe: flat shadows and mid-tones. After that add (D a temp or the output) the patch
// takes back the exposed value e = -ln(1 - y), runs it through the ACES fit (Narkowicz) at s e, scaled so white stays
// white (y 0.999 -> 1), and blends it over the game's y by `strength`:
//   y' = y + strength x (white x aces(s e) - y)
// s matches the game's y at 0.3 (dark mid-tones keep their brightness), so the curve adds contrast: deeper shadows,
// brighter mids, the same highlights. The bright pass (Brightpass_threshold) and the adaptation are other shaders; the
// particles' fog colour pass (no Particle_sampler) is left alone.
#include <cstdint>
#include <string>
#include <vector>

namespace filmic {

enum class Result {
    NotPixelShader,
    NotComposite,  // not one of rl_hdr's final composites, or its curve not found
    Patched,
    Unsupported,   // `reason` says why
};
struct Report {
    Result result = Result::Unsupported;
    std::string reason;
};

// strength 0..1 (0: not patched); exposure scales s (1: the match above).
Result patch(const void* bytes, size_t size, float strength, float exposure, std::vector<uint8_t>* out, Report* report = nullptr);

}  // namespace filmic
