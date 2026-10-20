#pragma once
// Shared planning for frame batches. The driver and the worker run the same
// code so they agree on draw extents and on which texels a draw may sample.
// Planning projects vertices with the kernel's own arithmetic; it never shades.
#include "frame_kernel.hpp"
#include "frame_batch_protocol.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
namespace nr::frame {
struct Rect { int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0; bool empty() const { return x0 >= x1 || y0 >= y1; } };
inline Rect intersect(const Rect &a, const Rect &b) { return {std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1)}; }
inline Rect unite(const Rect &a, const Rect &b) {
    if (a.empty()) return b; if (b.empty()) return a;
    return {std::min(a.x0, b.x0), std::min(a.y0, b.y0), std::max(a.x1, b.x1), std::max(a.y1, b.y1)};
}
// Per-texture coordinate provenance recorded at lowering: direct varyings give
// exact extents from the vertex attributes; anything else needs the whole texture.
struct CoordinateTap { uint32_t input = 0, lane_u = 0, lane_v = 0, divide_lane = 4; float margin = 0; };
// All sample instructions reading one texture; direct only when every tap is a varying (or its perspective divide).
struct CoordinateSource { bool direct = false; std::vector<CoordinateTap> taps; };
struct DrawPlan {
    bool valid = false;                       // every vertex projected within the kernel's limits
    std::string rejection;
    Rect bounds;                              // target pixels touched, before scissor
    std::vector<core::nr_projected> projected;
    std::array<Rect, NR_FRAME_MAX_TEXTURES> texels{};   // sampled texel windows per command texture
    std::array<bool, NR_FRAME_MAX_TEXTURES> whole{};    // window could not be bounded: whole texture
    std::vector<uint32_t> constants;          // vertex then pixel constant words, for interval evaluation
};
// Packs decoded vertex registers for the kernel's vertex pool (declared inputs only).
inline std::vector<uint32_t> pack_vertices(const LoweredProgram &program, const std::vector<shader::VertexRegisters> &vertices) {
    std::vector<uint32_t> words; words.reserve(vertices.size() * 16);
    for (const auto &v : vertices) for (unsigned r = 0; r < 16; ++r) if (program.vs_inputs & (1u << r)) words.insert(words.end(), v[r].begin(), v[r].end());
    return words;
}
// Determines where each sampled texture's coordinates come from, lane by lane.
// A lane is a varying lane (optionally divided by another lane of the same
// varying), a value independent of the pixel (constants, immediates), or
// unknown. Moves keep provenance; min/max against pixel-independent values keep
// it too, since clamping only narrows the texel range the kernel can reach.
inline std::array<CoordinateSource, NR_FRAME_MAX_TEXTURES> coordinate_sources(const LoweredProgram &program) {
    std::array<CoordinateSource, NR_FRAME_MAX_TEXTURES> result{};
    struct Lane { bool valid = false, constant = false; uint32_t input = 0, lane = 0, divide_lane = 4; float margin = 0; };
    std::array<std::array<Lane, 4>, 16> temps{};
    auto operand_lane = [&](uint32_t src, unsigned k) {
        const uint32_t kind = src & 15u, index = (src >> 4) & 255u, lane = (src >> (12 + 2 * k)) & 3u, modifier = (src >> 20) & 3u;
        Lane l;
        if (kind == 2 || kind == 3) { l.valid = true; l.constant = true; return l; }
        if (modifier) return l;
        if (kind == 1) { l.valid = true; l.input = index; l.lane = lane; return l; }
        if (kind == 0 && index < 16) return temps[index][lane];
        return l;
    };
    for (const auto &w : program.ps_words) {
        const uint32_t op = w[0], dest = w[1], src0 = w[2], src1 = w[3], src2 = w[4], extra = w[5];
        const uint32_t dest_kind = dest & 15u, dest_index = (dest >> 4) & 255u, mask = (dest >> 12) & 15u;
        if (op == core::NR_OP_SAMPLE) {
            const uint32_t texture = extra & 15u;
            if (texture >= NR_FRAME_MAX_TEXTURES) continue;
            auto &c = result[texture];
            const Lane u = operand_lane(src0, 0), v = operand_lane(src0, 1);
            bool direct = u.valid && v.valid && !u.constant && !v.constant && u.input == v.input && u.divide_lane == v.divide_lane;
            CoordinateTap tap;
            if (direct) { tap.input = u.input; tap.lane_u = u.lane; tap.lane_v = v.lane; tap.divide_lane = u.divide_lane; tap.margin = std::max(u.margin, v.margin); }
            if (c.taps.empty()) c.direct = direct; else c.direct = c.direct && direct;
            if (direct) c.taps.push_back(tap);
            continue;
        }
        if (dest_kind != 0 || dest_index >= 16) continue;
        for (unsigned k = 0; k < 4; ++k) {
            if (!(mask & (1u << k))) continue;
            Lane out;
            if (op == core::NR_OP_MOV) out = operand_lane(src0, k);
            else if (op == core::NR_OP_DIV) {
                const Lane a = operand_lane(src0, k), b = operand_lane(src1, k);
                if (a.valid && b.valid && !a.constant && !b.constant && a.divide_lane == 4 && b.divide_lane == 4 && a.input == b.input) { out = a; out.divide_lane = b.lane; }
                else if (a.valid && b.valid && a.constant && b.constant) { out.valid = true; out.constant = true; }
            } else if (op == core::NR_OP_MIN || op == core::NR_OP_MAX) {
                const Lane a = operand_lane(src0, k), b = operand_lane(src1, k);
                if (a.valid && b.valid && a.constant && b.constant) { out.valid = true; out.constant = true; }
                else if (a.valid && b.valid && b.constant) out = a;
                else if (a.valid && b.valid && a.constant) out = b;
            } else if (op == core::NR_OP_MUL || op == core::NR_OP_ADD || op == core::NR_OP_MAD) {
                const Lane a = operand_lane(src0, k), b = operand_lane(src1, k), c2 = op == core::NR_OP_MAD ? operand_lane(src2, k) : Lane{true, true};
                if (a.valid && b.valid && c2.valid && a.constant && b.constant && c2.constant) { out.valid = true; out.constant = true; }
            }
            temps[dest_index][k] = out;   // any other write makes the lane unknown
        }
    }
    return result;
}
// --- Interval evaluation of a pixel stage over a screen rectangle -----------
// When a texture's coordinates are not a plain varying, the pixel stage is
// evaluated over intervals: every varying lane spans the values it takes across
// the rectangle, constants are points, and each instruction maps intervals to a
// conservative result. Comparison masks and integer lanes travel as small sets
// of bit patterns. The (u, v) intervals reaching each sample instruction bound
// the texels the kernel can touch; an unbounded lane means the whole texture.
namespace interval {
// The rectangle an evaluation covers; affine forms take their range over it.
struct Extent { double x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
inline Extent &extent() { static thread_local Extent e; return e; }
struct Val {
    bool known = false;                 // false: no information (any value)
    double lo = 0, hi = 0;              // float interpretation when `known`
    std::vector<uint32_t> set;          // exact 32-bit patterns when small and known (masks, integers)
    bool affine = false;                // value = c0 + cx * px + cy * py across the extent (pixel centers)
    double c0 = 0, cx = 0, cy = 0;
    static Val any() { return Val{}; }
    static Val range(double a, double b) { Val v; v.known = std::isfinite(a) && std::isfinite(b); v.lo = std::min(a, b); v.hi = std::max(a, b); return v; }
    static Val exact(uint32_t bits) { float f; std::memcpy(&f, &bits, 4); Val v; v.known = std::isfinite(f); v.lo = v.hi = f; v.set = {bits}; if (v.known) { v.affine = true; v.c0 = f; } return v; }
    static Val form(double c0, double cx, double cy) {
        Val v; v.affine = std::isfinite(c0) && std::isfinite(cx) && std::isfinite(cy); if (!v.affine) return v;
        v.c0 = c0; v.cx = cx; v.cy = cy; const auto &e = extent();
        const double corners[4]{c0 + cx * e.x0 + cy * e.y0, c0 + cx * e.x1 + cy * e.y0, c0 + cx * e.x0 + cy * e.y1, c0 + cx * e.x1 + cy * e.y1};
        v.known = true; v.lo = std::min({corners[0], corners[1], corners[2], corners[3]}); v.hi = std::max({corners[0], corners[1], corners[2], corners[3]});
        if (!std::isfinite(v.lo) || !std::isfinite(v.hi)) return Val{};
        return v;
    }
    bool point() const { return known && lo == hi; }
    bool constant() const { return affine && cx == 0 && cy == 0; }
};
inline Val from_set(std::vector<uint32_t> set) {
    if (set.empty() || set.size() > 16) return Val::any();
    std::sort(set.begin(), set.end()); set.erase(std::unique(set.begin(), set.end()), set.end());
    double lo = 1e300, hi = -1e300; bool finite = true;
    for (uint32_t b : set) { float f; std::memcpy(&f, &b, 4); if (!std::isfinite(f)) finite = false; lo = std::min(lo, double(f)); hi = std::max(hi, double(f)); }
    Val v; v.set = std::move(set); if (finite) { v.known = true; v.lo = lo; v.hi = hi; if (v.set.size() == 1) { v.affine = true; v.c0 = lo; } }
    return v;
}
inline Val hull(const Val &a, const Val &b) {
    if (!a.known || !b.known) return Val::any();
    if (a.affine && b.affine && a.c0 == b.c0 && a.cx == b.cx && a.cy == b.cy) return a;
    Val v = Val::range(std::min(a.lo, b.lo), std::max(a.hi, b.hi));
    if (!a.set.empty() && !b.set.empty()) { auto s = a.set; s.insert(s.end(), b.set.begin(), b.set.end()); if (s.size() <= 16) { auto f = from_set(s); if (f.known) return f; } }
    return v;
}
inline Val neg(const Val &a) {
    if (!a.known) return a;
    Val v = a.affine ? Val::form(-a.c0, -a.cx, -a.cy) : Val::range(-a.hi, -a.lo);
    for (uint32_t b : a.set) v.set.push_back(b ^ 0x80000000u);
    return v;
}
inline Val abs(const Val &a) {
    if (!a.known) return a;
    Val v = a.lo >= 0 ? a : a.hi <= 0 ? neg(a) : Val::range(0, std::max(-a.lo, a.hi));
    v.set.clear(); for (uint32_t b : a.set) v.set.push_back(b & 0x7fffffffu);
    return v;
}
inline Val add(const Val &a, const Val &b) {
    if (!a.known || !b.known) return Val::any();
    if (a.affine && b.affine) return Val::form(a.c0 + b.c0, a.cx + b.cx, a.cy + b.cy);
    return Val::range(a.lo + b.lo, a.hi + b.hi);
}
inline Val mul(const Val &a, const Val &b) {
    if (!a.known || !b.known) return Val::any();
    if (a.affine && b.constant()) return Val::form(a.c0 * b.c0, a.cx * b.c0, a.cy * b.c0);
    if (b.affine && a.constant()) return Val::form(b.c0 * a.c0, b.cx * a.c0, b.cy * a.c0);
    const double c[4]{a.lo * b.lo, a.lo * b.hi, a.hi * b.lo, a.hi * b.hi};
    return Val::range(std::min({c[0], c[1], c[2], c[3]}), std::max({c[0], c[1], c[2], c[3]}));
}
inline Val div(const Val &a, const Val &b) {
    if (!a.known || !b.known || (b.lo <= 0 && b.hi >= 0)) return Val::any();
    if (a.affine && b.constant()) return Val::form(a.c0 / b.c0, a.cx / b.c0, a.cy / b.c0);
    return mul(a, Val::range(1.0 / b.lo, 1.0 / b.hi));
}
inline Val vmin(const Val &a, const Val &b) {
    if (!a.known || !b.known) return Val::any();
    if (a.hi <= b.lo) return a; if (b.hi <= a.lo) return b;
    return Val::range(std::min(a.lo, b.lo), std::min(a.hi, b.hi));
}
inline Val vmax(const Val &a, const Val &b) {
    if (!a.known || !b.known) return Val::any();
    if (a.lo >= b.hi) return a; if (b.lo >= a.hi) return b;
    return Val::range(std::max(a.lo, b.lo), std::max(a.hi, b.hi));
}
inline Val floor_(const Val &a) {
    if (!a.known) return a;
    const double f = std::floor(a.lo);
    if (std::floor(a.hi) == f) return Val::form(f, 0, 0);
    return Val::range(f, std::floor(a.hi));
}
inline Val frc(const Val &a) {
    if (!a.known) return a;
    const double f = std::floor(a.lo);
    if (std::floor(a.hi) != f) return Val::range(0, 1);
    if (a.affine) return Val::form(a.c0 - f, a.cx, a.cy);
    return Val::range(a.lo - f, a.hi - f);
}
inline Val saturate(const Val &a) {
    if (!a.known) return Val::range(0, 1);
    if (a.lo >= 0 && a.hi <= 1) return a;
    return Val::range(std::min(1.0, std::max(0.0, a.lo)), std::min(1.0, std::max(0.0, a.hi)));
}
// Comparisons yield the SM4 masks 0 / 0xffffffff.
inline Val compare(const Val &a, const Val &b, uint32_t op) {
    if (!a.known || !b.known) return from_set({0u, 0xffffffffu});
    bool t = false, f = false;
    auto decide = [&](bool all_true, bool all_false) { t = !all_false; f = !all_true; };
    switch (op) {
    case core::NR_OP_LT: decide(a.hi < b.lo, a.lo >= b.hi); break;
    case core::NR_OP_GE: decide(a.lo >= b.hi, a.hi < b.lo); break;
    case core::NR_OP_EQ: decide(a.point() && b.point() && a.lo == b.lo, a.hi < b.lo || b.hi < a.lo); break;
    case core::NR_OP_NE: decide(a.hi < b.lo || b.hi < a.lo, a.point() && b.point() && a.lo == b.lo); break;
    default: t = f = true;
    }
    std::vector<uint32_t> s; if (f) s.push_back(0u); if (t) s.push_back(0xffffffffu);
    return from_set(s);
}
inline Val bitwise(const Val &a, const Val &b, bool is_or) {
    if (a.set.empty() || b.set.empty()) return Val::any();
    std::vector<uint32_t> s;
    for (uint32_t x : a.set) for (uint32_t y : b.set) s.push_back(is_or ? (x | y) : (x & y));
    return from_set(s);
}
inline Val iadd(const Val &a, const Val &b) {
    if (a.set.empty() || b.set.empty()) return Val::any();
    std::vector<uint32_t> s; for (uint32_t x : a.set) for (uint32_t y : b.set) s.push_back(x + y);
    return from_set(s);
}
inline Val itof(const Val &a) {
    if (a.set.empty()) return Val::any();
    std::vector<uint32_t> s; for (uint32_t x : a.set) { const float f = float(int32_t(x)); uint32_t b; std::memcpy(&b, &f, 4); s.push_back(b); }
    return from_set(s);
}
inline Val ige(const Val &a, const Val &b) {
    if (a.set.empty() || b.set.empty()) return from_set({0u, 0xffffffffu});
    bool t = false, f = false; for (uint32_t x : a.set) for (uint32_t y : b.set) (int32_t(x) >= int32_t(y) ? t : f) = true;
    std::vector<uint32_t> s; if (f) s.push_back(0u); if (t) s.push_back(0xffffffffu);
    return from_set(s);
}
inline Val select(const Val &c, const Val &a, const Val &b) {
    bool t = false, f = false;
    if (c.set.empty()) t = f = true; else for (uint32_t x : c.set) (x ? t : f) = true;
    if (t && !f) return a; if (f && !t) return b; return hull(a, b);
}
using Lanes = std::array<Val, 4>;
// (u, v) coordinate intervals every sample of one texture reaches over the rectangle.
struct Window { bool any = false, bounded = true; double umin = 1e300, umax = -1e300, vmin = 1e300, vmax = -1e300; };
inline void evaluate(const LoweredProgram &program, const std::vector<uint32_t> &constants, const std::array<Lanes, 8> &inputs, std::array<Window, NR_FRAME_MAX_TEXTURES> &windows) {
    std::array<Lanes, 16> temps{};
    for (auto &t : temps) for (auto &l : t) l = Val::exact(0);
    auto operand = [&](const Words &w, unsigned k) {
        const uint32_t src = w[2 + k], kind = src & 15u, idx = (src >> 4) & 255u, lanes = (src >> 12) & 255u, negate = (src >> 20) & 1u, absolute = (src >> 21) & 1u, slot = (src >> 22) & 15u;
        const uint32_t rel = w[0] == core::NR_OP_SAMPLE ? 0u : (w[5] >> (k * 10)) & 0x3ffu;
        Lanes out;
        for (unsigned c = 0; c < 4; ++c) {
            const uint32_t lane = (lanes >> (2 * c)) & 3u;
            Val v;
            if (kind == 0) v = idx < 16 ? temps[idx][lane] : Val::any();
            else if (kind == 1) v = idx < 8 ? inputs[idx][lane] : Val::any();
            else if (kind == 3) v = idx < program.ps_immediates.size() ? Val::exact(program.ps_immediates[idx][lane]) : Val::any();
            else if (rel & 1u) v = Val::any();
            else {
                const size_t word = (size_t(program.vs_constants) + program.ps_constant_base[slot] + idx) * 4 + lane;
                v = slot < 14 && idx < program.ps_constant_count[slot] && word < constants.size() ? Val::exact(constants[word]) : Val::any();
            }
            if (absolute) v = abs(v);
            if (negate) v = w[0] == core::NR_OP_IADD ? (v.set.empty() ? Val::any() : from_set([&] { std::vector<uint32_t> s; for (uint32_t b : v.set) s.push_back(0u - b); return s; }())) : neg(v);
            out[c] = v;
        }
        return out;
    };
    for (const auto &w : program.ps_words) {
        const uint32_t op = w[0], dest = w[1], mask = (dest >> 12) & 15u, sat = (dest >> 16) & 1u, dest_kind = dest & 15u, dest_index = (dest >> 4) & 255u;
        const unsigned sources = detail::source_count(op);
        const Lanes a = operand(w, 0), b = sources >= 2 ? operand(w, 1) : Lanes{}, e = sources >= 3 ? operand(w, 2) : Lanes{};
        Lanes r;
        if (op == core::NR_OP_SAMPLE) {
            const uint32_t texture = w[5] & 15u;
            if (texture < NR_FRAME_MAX_TEXTURES) {
                auto &win = windows[texture]; win.any = true;
                if (!a[0].known || !a[1].known) win.bounded = false;
                else { win.umin = std::min(win.umin, a[0].lo); win.umax = std::max(win.umax, a[0].hi); win.vmin = std::min(win.vmin, a[1].lo); win.vmax = std::max(win.vmax, a[1].hi); }
            }
            for (auto &l : r) l = Val::range(0, 1);   // UNORM texel colors
            const uint32_t lanes = (w[5] >> 12) & 255u; Lanes swizzled; for (unsigned c = 0; c < 4; ++c) swizzled[c] = r[(lanes >> (2 * c)) & 3u]; r = swizzled;
        } else if (op == core::NR_OP_DP2 || op == core::NR_OP_DP3 || op == core::NR_OP_DP4) {
            const unsigned n = op == core::NR_OP_DP2 ? 2 : op == core::NR_OP_DP3 ? 3 : 4;
            Val sum = Val::exact(0); for (unsigned c = 0; c < n; ++c) sum = add(sum, mul(a[c], b[c]));
            for (auto &l : r) l = sum;
        } else for (unsigned c = 0; c < 4; ++c) {
            switch (op) {
            case core::NR_OP_MOV: r[c] = a[c]; break;
            case core::NR_OP_ADD: r[c] = add(a[c], b[c]); break;
            case core::NR_OP_MUL: r[c] = mul(a[c], b[c]); break;
            case core::NR_OP_MAD: r[c] = add(mul(a[c], b[c]), e[c]); break;
            case core::NR_OP_DIV: r[c] = div(a[c], b[c]); break;
            case core::NR_OP_MIN: r[c] = vmin(a[c], b[c]); break;
            case core::NR_OP_MAX: r[c] = vmax(a[c], b[c]); break;
            case core::NR_OP_FRC: r[c] = frc(a[c]); break;
            case core::NR_OP_FLOOR: r[c] = floor_(a[c]); break;
            case core::NR_OP_ITOF: r[c] = itof(a[c]); break;
            case core::NR_OP_MOVC: r[c] = select(a[c], b[c], e[c]); break;
            case core::NR_OP_LT: case core::NR_OP_GE: case core::NR_OP_EQ: case core::NR_OP_NE: r[c] = compare(a[c], b[c], op); break;
            case core::NR_OP_IGE: r[c] = ige(a[c], b[c]); break;
            case core::NR_OP_AND: r[c] = bitwise(a[c], b[c], false); break;
            case core::NR_OP_OR: r[c] = bitwise(a[c], b[c], true); break;
            case core::NR_OP_IADD: r[c] = iadd(a[c], b[c]); break;
            default: r[c] = Val::any();
            }
        }
        if (sat) for (auto &l : r) l = saturate(l);
        if (dest_kind == 0 && dest_index < 16) for (unsigned c = 0; c < 4; ++c) if (mask & (1u << c)) temps[dest_index][c] = r[c];
    }
}
}
// Values each pixel-stage input register takes over `rect` for one triangle,
// following the row shader's interpolation: constant, linear or perspective.
inline std::array<interval::Lanes, 8> varying_intervals(const LoweredProgram &program, const DrawPlan &plan, size_t first, Rect rect) {
    std::array<interval::Lanes, 8> inputs;
    for (auto &l : inputs) for (auto &v : l) v = interval::Val::any();
    const core::nr_projected *v[3]{&plan.projected[first], &plan.projected[first + 1], &plan.projected[first + 2]};
    const float x[3]{v[0]->x, v[1]->x, v[2]->x}, y[3]{v[0]->y, v[1]->y, v[2]->y};
    const float det = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
    const bool planar = v[0]->inverse_w == 1.0f && v[1]->inverse_w == 1.0f && v[2]->inverse_w == 1.0f && std::fabs(det) > 1e-12f;
    const float cx[2]{float(rect.x0) + 0.5f, float(rect.x1) - 0.5f}, cy[2]{float(rect.y0) + 0.5f, float(rect.y1) - 0.5f};
    interval::extent() = {cx[0], cy[0], cx[1], cy[1]};
    for (unsigned r = 0, k = 0; r < 8; ++r) {
        if (!(program.ps_inputs & (1u << r))) continue;
        const uint32_t mode = (program.interpolation >> (r * 4)) & 15u;
        for (unsigned c = 0; c < 4; ++c) {
            const float q[3]{NR_V4_LANE(v[0]->varying[k], c), NR_V4_LANE(v[1]->varying[k], c), NR_V4_LANE(v[2]->varying[k], c)};
            if (!std::isfinite(q[0]) || !std::isfinite(q[1]) || !std::isfinite(q[2])) continue;
            if (mode == 1) { inputs[r][c] = interval::Val::form(q[0], 0, 0); continue; }
            if (planar) {
                // The varying is a plane over the triangle: an affine form in the pixel
                // position, widened by the interpolation drift guard.
                const double a = ((q[1] - q[0]) * (y[2] - y[0]) - (q[2] - q[0]) * (y[1] - y[0])) / det, b = ((x[1] - x[0]) * (q[2] - q[0]) - (x[2] - x[0]) * (q[1] - q[0])) / det;
                auto form = interval::Val::form(q[0] - a * x[0] - b * y[0], a, b);
                if (form.known) {
                    const double margin = 1e-5 * (1.0 + std::fabs(form.hi - form.lo));
                    form.lo -= margin; form.hi += margin;
                    inputs[r][c] = form; continue;
                }
            }
            inputs[r][c] = interval::Val::range(std::min({q[0], q[1], q[2]}), std::max({q[0], q[1], q[2]}));
        }
        ++k;
    }
    return inputs;
}
// Texel window of texture `t` over `rect` from interval evaluation of the pixel stage; empty when unbounded.
inline bool interval_window(const LoweredProgram &program, const DrawPlan &plan, Rect rect, unsigned t, uint32_t tw, uint32_t th, bool point, Rect &out) {
    double umin = 1e300, umax = -1e300, vmin = 1e300, vmax = -1e300; bool any = false;
    for (size_t first = 0; first + 2 < plan.projected.size(); first += 3) {
        const core::nr_projected *v[3]{&plan.projected[first], &plan.projected[first + 1], &plan.projected[first + 2]};
        const Rect bbox{int32_t(std::floor(std::min({v[0]->x, v[1]->x, v[2]->x}))), int32_t(std::floor(std::min({v[0]->y, v[1]->y, v[2]->y}))),
                        int32_t(std::ceil(std::max({v[0]->x, v[1]->x, v[2]->x}))) + 1, int32_t(std::ceil(std::max({v[0]->y, v[1]->y, v[2]->y}))) + 1};
        const Rect clipped = intersect(bbox, rect);
        if (clipped.empty()) continue;
        std::array<interval::Window, NR_FRAME_MAX_TEXTURES> windows{};
        interval::evaluate(program, plan.constants, varying_intervals(program, plan, first, clipped), windows);
        const auto &w = windows[t];
        if (!w.any) continue;
        if (!w.bounded) return false;
        umin = std::min(umin, w.umin); umax = std::max(umax, w.umax); vmin = std::min(vmin, w.vmin); vmax = std::max(vmax, w.vmax); any = true;
    }
    if (!any) { out = {0, 0, 0, 0}; return true; }
    auto clamp01 = [](double x) { return x < 0 ? 0.0 : (x > 1 ? 1.0 : x); };
    const double half = point ? 0.0 : 0.5;
    out = {std::max(0, int32_t(std::floor(clamp01(umin) * double(tw) - half)) - 1), std::max(0, int32_t(std::floor(clamp01(vmin) * double(th) - half)) - 1),
           std::min(int32_t(tw), int32_t(std::floor(clamp01(umax) * double(tw) - half)) + 3), std::min(int32_t(th), int32_t(std::floor(clamp01(vmax) * double(th) - half)) + 3)};
    return true;
}
// Projects a draw with the kernel's arithmetic and bounds its coverage and sampling.
inline DrawPlan plan_draw(const LoweredProgram &program, const std::vector<uint32_t> &packed_vertices, uint32_t vertex_count,
                          const std::vector<uint32_t> &constants, const std::array<float, 4> &viewport,
                          const std::array<uint32_t, NR_FRAME_MAX_TEXTURES> &texture_width, const std::array<uint32_t, NR_FRAME_MAX_TEXTURES> &texture_height,
                          const std::array<bool, NR_FRAME_MAX_TEXTURES> &point_filter) {
    DrawPlan plan; plan.constants = constants;
    Command command; command.program = 0; command.vertices = packed_vertices; command.vertex_count = vertex_count; command.constants = constants;
    command.viewport = viewport; command.clip = {0, 0, 1, 1};
    // Planning never samples, but the kernel contract binds every texture the program declares: one-texel windows stand in.
    std::vector<Window> windows;
    for (unsigned t = 0; t < NR_FRAME_MAX_TEXTURES; ++t) {
        command.textures[t].window = NR_FRAME_NO_WINDOW;
        if (t < program.texture_slots.size()) { windows.push_back(Window{1, 1, 0, 0, 1, 1, 28, {0u}}); command.textures[t].window = uint32_t(windows.size() - 1); command.textures[t].point = point_filter[t]; }
    }
    const uint32_t background = 0;
    std::vector<uint8_t> span;
    try { span = SpanBuilder::build(0, 0, 1, 1, 28, {program.entry}, {command}, windows, &background); }
    catch (const std::exception &e) { plan.rejection = std::string("planning span: ") + e.what(); return plan; }
    static thread_local std::vector<uint8_t> scratch(span_bytes);
    core::nr_span s{}; core::nr_command c{}; core::nr_program p{}; core::nr_command_state st{}; core::nr_shade sh{}; uint32_t escapes = 0;
    if (!core::nr_span_load(&s, span.data(), scratch.data()) || !core::nr_command_load(&s, 0, &c) ||
        !core::nr_program_load(&s, 0, &p) || !core::nr_command_state_load(&s, &c, &p, &st)) { plan.rejection = "planning contract"; return plan; }
    sh.escapes = &escapes;
    plan.projected.resize(vertex_count);
    int32_t minx = INT32_MAX, miny = INT32_MAX, maxx = INT32_MIN, maxy = INT32_MIN;
    for (uint32_t v = 0; v < vertex_count; ++v) {
        if (!core::nr_vertex(&s, &c, &st, &p, v, &sh, &plan.projected[v])) { plan.rejection = "vertex projection limits"; return plan; }
        const auto &q = plan.projected[v];
        minx = std::min(minx, q.sx); maxx = std::max(maxx, q.sx); miny = std::min(miny, q.sy); maxy = std::max(maxy, q.sy);
    }
    plan.bounds = {std::max(0, (minx - 128) >> 8), std::max(0, (miny - 128) >> 8), std::max(0, ((maxx - 128) >> 8) + 1), std::max(0, ((maxy - 128) >> 8) + 1)};
    const auto sources = coordinate_sources(program);
    for (unsigned t = 0; t < program.texture_slots.size() && t < NR_FRAME_MAX_TEXTURES; ++t) {
        const auto &src = sources[t];
        const uint32_t tw = texture_width[t], th = texture_height[t];
        if (!tw || !th) { plan.whole[t] = true; plan.texels[t] = {0, 0, int32_t(tw), int32_t(th)}; continue; }
        if (!src.direct || src.taps.empty()) {
            Rect window;
            if (interval_window(program, plan, plan.bounds, t, tw, th, point_filter[t], window)) { plan.texels[t] = window; continue; }
            plan.whole[t] = true; plan.texels[t] = {0, 0, int32_t(tw), int32_t(th)}; continue;
        }
        float umin = 1e30f, umax = -1e30f, vmin = 1e30f, vmax = -1e30f; bool finite = true;
        for (const auto &tap : src.taps) {
            // Varying index of the input register inside the projected varyings.
            uint32_t varying = 0; bool found = false;
            for (unsigned r = 0, k = 0; r < 8; ++r) { if (!(p.ps_inputs & (1u << r))) continue; if (r == tap.input) { varying = k; found = true; break; } ++k; }
            if (!found) { finite = false; break; }
            for (uint32_t v = 0; v < vertex_count && finite; ++v) {
                const auto &var = plan.projected[v].varying[varying];
                float u = NR_V4_LANE(var, tap.lane_u), vv = NR_V4_LANE(var, tap.lane_v);
                if (tap.divide_lane < 4) { const float w = NR_V4_LANE(var, tap.divide_lane); if (!(w != 0.f) || !std::isfinite(w)) { finite = false; break; } u /= w; vv /= w; }
                if (!std::isfinite(u) || !std::isfinite(vv)) { finite = false; break; }
                umin = std::min(umin, u); umax = std::max(umax, u); vmin = std::min(vmin, vv); vmax = std::max(vmax, vv);
            }
        }
        if (!finite) { plan.whole[t] = true; plan.texels[t] = {0, 0, int32_t(tw), int32_t(th)}; continue; }
        auto clamp01 = [](float x) { return x < 0 ? 0.f : (x > 1 ? 1.f : x); };
        const float half = point_filter[t] ? 0.f : 0.5f;
        // Texel addresses the kernel can touch for coordinates in [umin, umax], plus one texel of slack.
        const int32_t x0 = std::max(0, int32_t(std::floor(clamp01(umin) * float(tw) - half)) - 1), x1 = std::min(int32_t(tw), int32_t(std::floor(clamp01(umax) * float(tw) - half)) + 3);
        const int32_t y0 = std::max(0, int32_t(std::floor(clamp01(vmin) * float(th) - half)) - 1), y1 = std::min(int32_t(th), int32_t(std::floor(clamp01(vmax) * float(th) - half)) + 3);
        plan.texels[t] = {x0, y0, x1, y1};
    }
    plan.valid = true;
    return plan;
}
// Texel window texture `t` of a planned draw may sample inside `rect` (target
// pixels): each triangle's varying is an affine plane over the screen when the
// vertices share w, so its extremes lie on the corners of the clipped bounding
// box. Falls back to the draw-wide window when a plane cannot be fitted.
inline Rect texel_window(const LoweredProgram &program, const DrawPlan &plan, Rect rect, unsigned t, uint32_t tw, uint32_t th, bool point) {
    if (plan.whole[t] || !tw || !th) return {0, 0, int32_t(tw), int32_t(th)};
    const auto sources = coordinate_sources(program); const auto &source = sources[t];
    if (!source.direct || source.taps.empty()) {
        Rect window;
        if (interval_window(program, plan, rect, t, tw, th, point, window)) return intersect(window, plan.texels[t]);
        return plan.texels[t];
    }
    float umin = 1e30f, umax = -1e30f, vmin = 1e30f, vmax = -1e30f; bool any = false;
    for (const auto &src : source.taps) {
    uint32_t varying = 0; bool found = false;
    for (unsigned r = 0, k = 0; r < 8; ++r) { if (!(program.ps_inputs & (1u << r))) continue; if (r == src.input) { varying = k; found = true; break; } ++k; }
    if (!found) return plan.texels[t];
    for (size_t first = 0; first + 2 < plan.projected.size(); first += 3) {
        const core::nr_projected *v[3]{&plan.projected[first], &plan.projected[first + 1], &plan.projected[first + 2]};
        float x[3], y[3], uu[3], vv[3]; bool fit = true;
        for (unsigned i = 0; i < 3; ++i) {
            const auto &var = v[i]->varying[varying];
            x[i] = v[i]->x; y[i] = v[i]->y; uu[i] = NR_V4_LANE(var, src.lane_u); vv[i] = NR_V4_LANE(var, src.lane_v);
            if (src.divide_lane < 4) { const float w = NR_V4_LANE(var, src.divide_lane); if (!(w != 0.f) || !std::isfinite(w)) return plan.texels[t]; uu[i] /= w; vv[i] /= w; }
            if (v[i]->inverse_w != 1.0f) fit = false;
        }
        Rect bbox{int32_t(std::floor(std::min({x[0], x[1], x[2]}))), int32_t(std::floor(std::min({y[0], y[1], y[2]}))), int32_t(std::ceil(std::max({x[0], x[1], x[2]}))) + 1, int32_t(std::ceil(std::max({y[0], y[1], y[2]}))) + 1};
        const Rect clipped = intersect(bbox, rect);
        if (clipped.empty()) continue;
        const float det = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
        if (!fit || !(std::fabs(det) > 1e-12f)) {
            for (unsigned i = 0; i < 3; ++i) { umin = std::min(umin, uu[i]); umax = std::max(umax, uu[i]); vmin = std::min(vmin, vv[i]); vmax = std::max(vmax, vv[i]); }
            any = true; continue;
        }
        auto plane = [&](const float *q, float px, float py) {
            const float a = ((q[1] - q[0]) * (y[2] - y[0]) - (q[2] - q[0]) * (y[1] - y[0])) / det, b = ((x[1] - x[0]) * (q[2] - q[0]) - (x[2] - x[0]) * (q[1] - q[0])) / det;
            return q[0] + a * (px - x[0]) + b * (py - y[0]);
        };
        const float cx[2]{float(clipped.x0) + 0.5f, float(clipped.x1) - 0.5f}, cy[2]{float(clipped.y0) + 0.5f, float(clipped.y1) - 0.5f};
        for (float px : cx) for (float py : cy) {
            const float su = plane(uu, px, py), sv = plane(vv, px, py);
            if (!std::isfinite(su) || !std::isfinite(sv)) return plan.texels[t];
            umin = std::min(umin, su); umax = std::max(umax, su); vmin = std::min(vmin, sv); vmax = std::max(vmax, sv);
        }
        any = true;
    }
    }
    if (!any) return {0, 0, 0, 0};
    // Guard incremental interpolation drift with a relative margin before the texel slack.
    const float margin_u = 1e-5f * (1.0f + std::fabs(umax - umin)), margin_v = 1e-5f * (1.0f + std::fabs(vmax - vmin));
    umin -= margin_u; umax += margin_u; vmin -= margin_v; vmax += margin_v;
    auto clamp01 = [](float x) { return x < 0 ? 0.f : (x > 1 ? 1.f : x); };
    const float half = point ? 0.f : 0.5f;
    Rect r{std::max(0, int32_t(std::floor(clamp01(umin) * float(tw) - half)) - 1), std::max(0, int32_t(std::floor(clamp01(vmin) * float(th) - half)) - 1),
           std::min(int32_t(tw), int32_t(std::floor(clamp01(umax) * float(tw) - half)) + 3), std::min(int32_t(th), int32_t(std::floor(clamp01(vmax) * float(th) - half)) + 3)};
    return intersect(r, plan.texels[t]);
}
// Tile edge (128, 64 or 32) at which every tile of a planned draw fits one span
// with its texture windows, or 0 when the draw needs the per-draw route.
inline uint32_t fitting_tile(const LoweredProgram &program, const DrawPlan &plan, Rect bounds, size_t entry_bytes, uint32_t vertex_words, uint32_t constant_words,
                             const std::array<uint32_t, NR_FRAME_MAX_TEXTURES> &tw, const std::array<uint32_t, NR_FRAME_MAX_TEXTURES> &th, const std::array<bool, NR_FRAME_MAX_TEXTURES> &point) {
    if (bounds.empty()) return 128;
    for (uint32_t edge : {128u, 64u, 32u}) {
        bool fits = true;
        for (int32_t ty = bounds.y0 / int32_t(edge) * int32_t(edge); ty < bounds.y1 && fits; ty += int32_t(edge))
            for (int32_t tx = bounds.x0 / int32_t(edge) * int32_t(edge); tx < bounds.x1 && fits; tx += int32_t(edge)) {
                const Rect clip = intersect(bounds, {tx, ty, tx + int32_t(edge), ty + int32_t(edge)});
                if (clip.empty()) continue;
                size_t bytes = NR_FRAME_HEADER_BYTES + NR_FRAME_COMMAND_BYTES + entry_bytes + 64 + size_t(vertex_words) * 4 + size_t(constant_words) * 4 + 4 + size_t(edge) * edge * 4;
                for (size_t t = 0; t < program.texture_slots.size() && t < NR_FRAME_MAX_TEXTURES; ++t) {
                    const auto r = texel_window(program, plan, clip, unsigned(t), tw[t], th[t], point[t]);
                    bytes += NR_FRAME_WINDOW_BYTES + (r.empty() ? 4 : size_t(r.x1 - r.x0) * size_t(r.y1 - r.y0) * 4);
                }
                if (bytes > span_bytes - 1024) fits = false;
            }
        if (fits) return edge;
    }
    return 0;
}
}
