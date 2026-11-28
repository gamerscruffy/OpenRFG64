#include "patch/filmic.h"

#include <cmath>
#include <cstring>

#include "patch/dxbc.h"

namespace filmic {
namespace {

using dxbc::Instruction;
using dxbc::Operand;

constexpr uint32_t kDiv = 14, kExp = 25, kLog = 47;  // D3D10_SB_OPCODE_*
constexpr uint32_t kXyz = 0x7;
constexpr uint32_t kImm4 = 0x00004002u;  // a 4-component immediate32 operand
uint32_t bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}
// rN.xyz as a destination; rN.xyzw as a source (optionally negated: an extended operand token)
void dst(std::vector<uint32_t>& t, uint32_t r) { t.insert(t.end(), {0x00100002u | (kXyz << 4), r}); }
void src(std::vector<uint32_t>& t, uint32_t r, bool neg = false) {
    if (neg) t.insert(t.end(), {0x80100E46u, 0x00000041u, r});
    else t.insert(t.end(), {0x00100E46u, r});
}
void imm(std::vector<uint32_t>& t, float v) { t.insert(t.end(), {kImm4, bits(v), bits(v), bits(v), bits(v)}); }
// one instruction: its opcode token, then what `body` writes
template <class F>
void op(std::vector<uint32_t>& t, uint32_t opcode, F body) {
    const size_t at = t.size();
    t.push_back(0);
    body();
    t[at] = opcode | uint32_t(t.size() - at) << 24;
}

float aces(float x) { return (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f); }

bool is_identity_xyz(const Operand& o) {
    return o.components == 4 && o.mode == 1 && o.swizzle[0] == 0 && o.swizzle[1] == 1 && o.swizzle[2] == 2;
}

}  // namespace

Result patch(const void* bytes, size_t size, float strength, float exposure, std::vector<uint8_t>* out, Report* report) {
    Report local;
    Report& r = report ? *report : local;
    auto fail = [&](const std::string& why) {
        r.result = Result::Unsupported;
        r.reason = why;
        return r.result;
    };
    if (!(strength > 0.0f)) return r.result = Result::NotComposite;
    dxbc::Container c;
    std::string error;
    if (!dxbc::parse(bytes, size, &c, &error)) return fail(error);
    dxbc::Chunk* shdr = c.find("SHDR");
    if (!shdr) shdr = c.find("SHEX");
    if (!shdr) return r.result = Result::NotPixelShader;
    dxbc::Program p;
    if (!dxbc::decode(*shdr, &p, &error)) return fail("decode: " + error);
    if ((p.version >> 16) != 0) return r.result = Result::NotPixelShader;

    bool particles = false, adapt = false;
    for (const dxbc::Binding& b : dxbc::bindings(c)) {
        if (b.name.find("Particle_sampler") != std::string::npos) particles = true;
        if (b.name.find("Lum_adapt_sampler") != std::string::npos) adapt = true;
    }
    if (!particles || !adapt) return r.result = Result::NotComposite;

    // the curve: exp rE.xyz, ... then add D.xyz, -rE.xyz, l(1, 1, 1, *) (either source order)
    int site = -1;
    uint32_t temps = 0;
    int temps_at = -1;
    for (size_t i = 0; i < p.code.size(); ++i) {
        const Instruction& in = p.code[i];
        if (in.opcode == dxbc::kDclTemps && in.length == 2) {
            temps = p.tokens[in.offset + 1];
            temps_at = int(i);
            continue;
        }
        if (in.opcode != kExp || in.operands.size() != 2 || i + 1 >= p.code.size()) continue;
        const Operand& e = in.operands[0];
        if (e.type != dxbc::kTemp || e.mode != 0 || (e.mask & kXyz) != kXyz) continue;
        const Instruction& a = p.code[i + 1];
        if (a.opcode != dxbc::kAdd || a.operands.size() != 3 || (p.tokens[a.offset] >> 31)) continue;
        const Operand& d = a.operands[0];
        if ((d.type != dxbc::kTemp && d.type != dxbc::kOutput) || d.mode != 0 || d.mask != kXyz || d.relative ||
            d.length != 2)
            continue;
        for (int k = 1; k <= 2; ++k) {
            const Operand& neg = a.operands[size_t(k)];
            const Operand& one = a.operands[size_t(3 - k)];
            if (neg.type != dxbc::kTemp || !neg.neg || neg.abs || neg.index[0] != e.index[0] || !is_identity_xyz(neg)) continue;
            if (one.type != dxbc::kImmediate32 || one.length != 5) continue;
            const uint32_t* v = p.tokens.data() + a.offset + one.first + 1;
            if (v[0] != bits(1.0f) || v[1] != bits(1.0f) || v[2] != bits(1.0f)) continue;
            site = int(i + 1);
            break;
        }
        if (site >= 0) break;
    }
    if (site < 0) return r.result = Result::NotComposite;
    if (temps_at < 0) return fail("no temps declared");

    // the constants: s matches the game's y at 0.3 (e = -ln 0.7), white keeps y 0.999 at 1
    const float e_mid = -std::log(0.7f);
    float lo = 0.0f, hi = 4.0f;
    for (int k = 0; k < 40; ++k) {
        const float m = 0.5f * (lo + hi);
        (aces(m) < 0.3f ? lo : hi) = m;
    }
    const float s = 0.5f * (lo + hi) / e_mid * (exposure > 0.0f ? exposure : 1.0f);
    const float white = 1.0f / aces(s * -std::log(0.001f));
    const float k = strength < 1.0f ? strength : 1.0f;

    const Instruction& add = p.code[size_t(site)];
    const Operand& d = add.operands[0];
    const bool to_output = d.type == dxbc::kOutput;
    const uint32_t T = temps, U = temps + 1, V = temps + 2;
    const uint32_t Y = to_output ? temps + 3 : d.index[0];
    const uint32_t new_temps = temps + (to_output ? 4 : 3);

    std::vector<uint32_t> t;
    t.reserve(p.tokens.size() + 128);
    const uint32_t length = p.tokens[1];
    uint32_t added = 0;
    for (size_t i = 0; i < p.code.size(); ++i) {
        const Instruction& in = p.code[i];
        const uint32_t* tk = p.tokens.data() + in.offset;
        if (int(i) != site) {
            t.insert(t.end(), tk, tk + in.length);
            if (int(i) == temps_at) t[t.size() - 1] = new_temps;
            continue;
        }
        // the game's add, into Y when it wrote the output
        if (to_output) {
            std::vector<uint32_t> a(tk, tk + in.length);
            a[d.first] = 0x00100002u | (kXyz << 4);
            a[d.first + 1] = Y;
            t.insert(t.end(), a.begin(), a.end());
        } else {
            t.insert(t.end(), tk, tk + in.length);
        }
        // e = -ln(1 - clamp(y)), times s
        op(t, dxbc::kMax, [&] { dst(t, T); src(t, Y); imm(t, 0.0f); });
        op(t, dxbc::kMin, [&] { dst(t, T); src(t, T); imm(t, 0.9999f); });
        op(t, dxbc::kAdd, [&] { dst(t, T); src(t, T, true); imm(t, 1.0f); });
        op(t, kLog, [&] { dst(t, T); src(t, T); });
        op(t, dxbc::kMul, [&] { dst(t, T); src(t, T); imm(t, -0.69314718f * s); });
        // aces: x (2.51 x + 0.03) / (x (2.43 x + 0.59) + 0.14)
        op(t, dxbc::kMad, [&] { dst(t, U); src(t, T); imm(t, 2.51f); imm(t, 0.03f); });
        op(t, dxbc::kMul, [&] { dst(t, U); src(t, U); src(t, T); });
        op(t, dxbc::kMad, [&] { dst(t, V); src(t, T); imm(t, 2.43f); imm(t, 0.59f); });
        op(t, dxbc::kMad, [&] { dst(t, V); src(t, V); src(t, T); imm(t, 0.14f); });
        op(t, kDiv, [&] { dst(t, U); src(t, U); src(t, V); });
        // y' = y + k (white x aces - y)
        op(t, dxbc::kMad, [&] { dst(t, U); src(t, U); imm(t, white); src(t, Y, true); });
        op(t, dxbc::kMad, [&] { dst(t, Y); src(t, U); imm(t, k); src(t, Y); });
        added = 12;
        if (to_output) {  // mov oN.xyz, Y
            const uint32_t* dt = tk + d.first;
            op(t, dxbc::kMov, [&] { t.insert(t.end(), dt, dt + 2); src(t, Y); });
            ++added;
        }
    }
    std::vector<uint32_t> program;
    program.push_back(p.tokens[0]);
    program.push_back(uint32_t(2 + t.size()));
    program.insert(program.end(), t.begin(), t.end());
    for (size_t i = length; i < p.tokens.size(); ++i) program.push_back(p.tokens[i]);
    shdr->data.assign(reinterpret_cast<const uint8_t*>(program.data()),
                      reinterpret_cast<const uint8_t*>(program.data()) + program.size() * 4);
    dxbc::Program check;
    if (!dxbc::decode(*shdr, &check, &error)) return fail("the rewritten program does not decode: " + error);
    if (check.code.size() != p.code.size() + added) return fail("the rewritten program has the wrong instruction count");
    if (out) *out = dxbc::serialize(c);
    r.result = Result::Patched;
    return r.result;
}

}  // namespace filmic
