#pragma once
// Shared planning for frame batches. The driver and the worker run the same
// code so they agree on draw extents and on which texels a draw may sample.
// Planning projects vertices with the kernel's own arithmetic; it never shades.
#include "frame_kernel.hpp"
#include "frame_batch_protocol.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <cstdio>
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
    const auto &words = program.ps_words;
    for (size_t wi = 0; wi < words.size(); ++wi) {
        const auto &w = words[wi];
        const uint32_t op = w[0], dest = w[1], src0 = w[2], src1 = w[3], src2 = w[4], extra = w[5];
        const uint32_t dest_kind = dest & 15u, dest_index = (dest >> 4) & 255u, mask = (dest >> 12) & 15u;
        if (op == core::NR_OP_LOOP) {
            // Temporaries written inside the loop take unknown values throughout and after it.
            for (size_t m = wi + 1; m < words.size() && words[m][0] != core::NR_OP_ENDLOOP; ++m) {
                const uint32_t d = words[m][1]; if ((d & 15u) != 0 || ((d >> 4) & 255u) >= 16) continue;
                for (unsigned k = 0; k < 4; ++k) if ((d >> 12) & (1u << k)) temps[(d >> 4) & 255u][k] = Lane{};
            }
            continue;
        }
        if (op == core::NR_OP_BREAKC || op == core::NR_OP_ENDLOOP) continue;
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
// Affine arithmetic: every known value is c0 + sum(ci * s_i) over noise symbols
// s_i with known ranges. Symbols 0 and 1 are the pixel position across the
// extent; every non-linear result (a floor that crosses an integer, a product
// of two varying values, an undecided select) introduces a fresh symbol with
// the result's range. Linear arithmetic between correlated values stays exact,
// which is what keeps mirror and tiling coordinate math tight.
struct Extent { double x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
inline Extent &extent() { static thread_local Extent e; return e; }
inline std::vector<std::pair<double, double>> &symbols() { static thread_local std::vector<std::pair<double, double>> s; return s; }
// Symbols that do not vary with the pixel position (interpolation drift guards): their screen derivative is zero.
inline std::vector<bool> &pixel_independent() { static thread_local std::vector<bool> p; return p; }
inline bool &affine_enabled() { static bool enabled = true; return enabled; }   // diagnostics: plain intervals when false
inline FILE *&trace_sink() { static FILE *sink = nullptr; return sink; }           // diagnostics: per-instruction ranges
inline void reset_symbols() { symbols().clear(); pixel_independent().clear(); const auto &e = extent(); symbols().push_back({e.x0, e.x1}); symbols().push_back({e.y0, e.y1}); pixel_independent().assign(2, false); }
struct Val {
    bool known = false;                 // false: no information (any value)
    double lo = 0, hi = 0;              // range when `known`
    std::vector<uint32_t> set;          // exact 32-bit patterns when small (masks, integers)
    bool affine = false;                // c0 + sum(terms) when `known`
    double c0 = 0;
    std::vector<std::pair<uint32_t, double>> terms;   // symbol, coefficient (sorted by symbol)
    // Integer lanes (ftoi results and what they feed) as an int32 range, independent of the float
    // fields: such a lane is "any value" to every float operation and exact to the integer ones.
    bool integer = false; int64_t ilo = 0, ihi = 0;
    static Val any() { return Val{}; }
    void bound() {
        lo = hi = c0;
        for (const auto &[s, c] : terms) { const auto &r = symbols()[s]; lo += c >= 0 ? c * r.first : c * r.second; hi += c >= 0 ? c * r.second : c * r.first; }
        known = std::isfinite(lo) && std::isfinite(hi); if (!known) { affine = false; terms.clear(); }
    }
    static Val constant(double c) { Val v; if (!std::isfinite(c)) return v; v.affine = true; v.c0 = c; v.bound(); return v; }
    // A fresh symbol spanning [a, b].
    static Val range(double a, double b) {
        Val v; if (!std::isfinite(a) || !std::isfinite(b)) return v;
        if (a > b) std::swap(a, b);
        if (a == b) return constant(a);
        symbols().push_back({a, b}); pixel_independent().push_back(false); v.affine = true; v.terms.push_back({uint32_t(symbols().size() - 1), 1.0}); v.bound(); return v;
    }
    // A fresh symbol spanning [a, b] that is constant across the pixel position (a drift guard).
    static Val noise(double a, double b) { Val v = range(a, b); if (v.affine && !v.terms.empty()) pixel_independent()[v.terms.back().first] = true; return v; }
    static Val exact(uint32_t bits) { float f; std::memcpy(&f, &bits, 4); Val v = constant(f); v.set = {bits}; return v; }
    static Val form(double c0, double cx, double cy) {
        Val v; if (!std::isfinite(c0) || !std::isfinite(cx) || !std::isfinite(cy)) return v;
        if (!affine_enabled() && (cx != 0 || cy != 0)) {
            const auto &e = extent(); const double c[4]{c0 + cx * e.x0 + cy * e.y0, c0 + cx * e.x1 + cy * e.y0, c0 + cx * e.x0 + cy * e.y1, c0 + cx * e.x1 + cy * e.y1};
            return range(std::min({c[0], c[1], c[2], c[3]}), std::max({c[0], c[1], c[2], c[3]}));
        }
        v.affine = true; v.c0 = c0; if (cx != 0) v.terms.push_back({0u, cx}); if (cy != 0) v.terms.push_back({1u, cy}); v.bound(); return v;
    }
    std::shared_ptr<Val> alt;           // a second case: the value is in this OR *alt (wrap seams)
    bool point() const { return known && lo == hi; }
    bool constant_value() const { return affine && terms.empty(); }
    bool same_form(const Val &o) const { return affine && o.affine && c0 == o.c0 && terms == o.terms; }
    Val stripped() const { Val v = *this; v.alt.reset(); return v; }
};
inline std::vector<Val> cases(const Val &v) { std::vector<Val> c{v.stripped()}; if (v.alt) c.push_back(v.alt->stripped()); return c; }
inline Val hull(const Val &a, const Val &b);
// Two cases stay separate; more collapse to their hull.
inline Val from_set(std::vector<uint32_t> set);
inline Val join(std::vector<Val> results) {
    std::vector<Val> flat; for (auto &r : results) for (auto &c : cases(r)) flat.push_back(c);
    if (flat.empty()) return Val::any();
    bool all_sets = true; for (const auto &c : flat) all_sets = all_sets && !c.set.empty();
    if (all_sets) { std::vector<uint32_t> s; for (const auto &c : flat) s.insert(s.end(), c.set.begin(), c.set.end()); return from_set(s); }   // masks and integers merge by their patterns
    for (const auto &c : flat) if (!c.known) return Val::any();
    if (flat.size() == 1) return flat[0];
    if (flat.size() == 2) { if (flat[0].same_form(flat[1])) return flat[0]; Val v = flat[0]; v.alt = std::make_shared<Val>(flat[1]); return v; }
    Val h = flat[0]; for (size_t n = 1; n < flat.size(); ++n) h = hull(h, flat[n]); return h;
}
template <class F> Val lift1(const Val &a, F f) { if (!a.alt) return f(a); std::vector<Val> r; for (const auto &c : cases(a)) r.push_back(f(c)); return join(r); }
template <class F> Val lift2(const Val &a, const Val &b, F f) {
    if (!a.alt && !b.alt) return f(a, b);
    std::vector<Val> r; for (const auto &ca : cases(a)) for (const auto &cb : cases(b)) r.push_back(f(ca, cb)); return join(r);
}
inline Val from_set(std::vector<uint32_t> set) {
    if (set.empty() || set.size() > 16) return Val::any();
    std::sort(set.begin(), set.end()); set.erase(std::unique(set.begin(), set.end()), set.end());
    double lo = 1e300, hi = -1e300; bool finite = true;
    for (uint32_t b : set) { float f; std::memcpy(&f, &b, 4); if (!std::isfinite(f)) finite = false; lo = std::min(lo, double(f)); hi = std::max(hi, double(f)); }
    Val v;
    if (finite && set.size() == 2) {
        // Two finite patterns (a mask ANDed with a constant, a sign) stay two cases, not a continuum.
        v = Val::constant(lo); v.set = {set[0]}; Val other = Val::constant(hi); other.set = {set[1]};
        if (lo == hi) { v.set = set; return v; }
        float f0; std::memcpy(&f0, &set[0], 4); if (double(f0) != lo) std::swap(v.set, other.set);
        v.alt = std::make_shared<Val>(other); return v;
    }
    if (finite) v = Val::range(lo, hi);
    v.set = std::move(set);
    return v;
}
inline Val scaled(const Val &a, double k) {
    Val v; if (!a.known) return v;
    v.affine = true; v.c0 = a.c0 * k; for (const auto &[s, c] : a.terms) if (c * k != 0) v.terms.push_back({s, c * k}); v.bound(); return v;
}
inline Val neg(const Val &a) {
    if (!a.known) { Val v; for (uint32_t b : a.set) v.set.push_back(b ^ 0x80000000u); return v; }
    Val v = scaled(a, -1.0); for (uint32_t b : a.set) v.set.push_back(b ^ 0x80000000u); return v;
}
inline bool int_range(const Val &a, int64_t &lo, int64_t &hi);
inline Val make_int(int64_t lo, int64_t hi);
inline Val hull(const Val &a, const Val &b) {
    if (a.integer || b.integer) { int64_t al, ah, bl, bh; return int_range(a, al, ah) && int_range(b, bl, bh) ? make_int(std::min(al, bl), std::max(ah, bh)) : Val::any(); }
    if (!a.known || !b.known) return Val::any();
    if (a.same_form(b)) return a;
    Val v = Val::range(std::min(a.lo, b.lo), std::max(a.hi, b.hi));
    if (!a.set.empty() && !b.set.empty()) { auto s = a.set; s.insert(s.end(), b.set.begin(), b.set.end()); if (s.size() <= 16) { auto f = from_set(s); if (f.known) return f; } }
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
    Val v; v.affine = true; v.c0 = a.c0 + b.c0;
    size_t i = 0, j = 0;
    while (i < a.terms.size() || j < b.terms.size()) {
        if (j >= b.terms.size() || (i < a.terms.size() && a.terms[i].first < b.terms[j].first)) { v.terms.push_back(a.terms[i]); ++i; }
        else if (i >= a.terms.size() || b.terms[j].first < a.terms[i].first) { v.terms.push_back(b.terms[j]); ++j; }
        else { const double c = a.terms[i].second + b.terms[j].second; if (c != 0) v.terms.push_back({a.terms[i].first, c}); ++i; ++j; }
    }
    v.bound(); return v;
}
inline Val mul(const Val &a, const Val &b) {
    if (!a.known || !b.known) return Val::any();
    if (b.constant_value()) return scaled(a, b.c0);
    if (a.constant_value()) return scaled(b, a.c0);
    const double c[4]{a.lo * b.lo, a.lo * b.hi, a.hi * b.lo, a.hi * b.hi};
    return Val::range(std::min({c[0], c[1], c[2], c[3]}), std::max({c[0], c[1], c[2], c[3]}));
}
inline Val div(const Val &a, const Val &b) {
    if (!a.known || !b.known || (b.lo <= 0 && b.hi >= 0)) return Val::any();
    if (b.constant_value()) return scaled(a, 1.0 / b.c0);
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
    const double f = std::floor(a.lo), g = std::floor(a.hi);
    if (g == f) return Val::constant(f);
    if (g == f + 1) { Val v = Val::constant(f); v.alt = std::make_shared<Val>(Val::constant(g)); return v; }
    return Val::range(f, g);
}
inline Val frc(const Val &a) {
    if (!a.known) return a;
    const double f = std::floor(a.lo), g = std::floor(a.hi);
    if (g == f) return add(a, Val::constant(-f));
    if (g == f + 1) {
        // One wrap seam inside the range: the values below the seam and the values above it.
        Val below = Val::range(a.lo - f, 1.0), above = Val::range(0.0, a.hi - g);
        below.alt = std::make_shared<Val>(above); return below;
    }
    return Val::range(0, 1);
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
    const Val d = add(a, neg(b));   // correlated difference decides more than separate ranges
    switch (op) {
    case core::NR_OP_LT: decide(d.hi < 0, d.lo >= 0); break;
    case core::NR_OP_GE: decide(d.lo >= 0, d.hi < 0); break;
    case core::NR_OP_EQ: decide(d.point() && d.lo == 0, d.lo > 0 || d.hi < 0); break;
    case core::NR_OP_NE: decide(d.lo > 0 || d.hi < 0, d.point() && d.lo == 0); break;
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
// Integer lanes travel as small pattern sets (masks, immediates, short ranges) or as int32 ranges.
inline Val make_int(int64_t lo, int64_t hi) {
    Val v; if (lo > hi) std::swap(lo, hi);
    if (lo < INT32_MIN || hi > INT32_MAX) return v;   // wrapped around: any value
    v.integer = true; v.ilo = lo; v.ihi = hi;
    if (hi - lo < 16) for (int64_t x = lo; x <= hi; ++x) v.set.push_back(uint32_t(int32_t(x)));
    return v;
}
inline bool int_range(const Val &a, int64_t &lo, int64_t &hi) {
    if (!a.set.empty()) { lo = INT64_MAX; hi = INT64_MIN; for (uint32_t b : a.set) { lo = std::min<int64_t>(lo, int32_t(b)); hi = std::max<int64_t>(hi, int32_t(b)); } return true; }
    if (a.integer) { lo = a.ilo; hi = a.ihi; return true; }
    // A single exact float value (a constant-interpolated varying carrying integer bits, a literal) is
    // one bit pattern: an integer instruction reads those bits.
    if (a.known && a.constant_value() && a.lo == a.hi) { const float f = float(a.lo); if (double(f) == a.lo) { uint32_t bits; std::memcpy(&bits, &f, 4); lo = hi = int32_t(bits); return true; } }
    return false;
}
inline int32_t saturate_to_int(double v) { return std::isnan(v) ? 0 : v >= 2147483648.0 ? INT32_MAX : v <= -2147483648.0 ? INT32_MIN : int32_t(std::trunc(v)); }
inline Val ftoi(const Val &a) {
    if (!a.set.empty()) { std::vector<uint32_t> s; for (uint32_t b : a.set) { float f; std::memcpy(&f, &b, 4); s.push_back(uint32_t(saturate_to_int(f))); } return from_set(s); }
    if (!a.known) return Val::any();
    return make_int(saturate_to_int(a.lo), saturate_to_int(a.hi));   // truncation is monotonic
}
inline Val iadd(const Val &a, const Val &b) {
    if (!a.set.empty() && !b.set.empty()) {
        std::vector<uint32_t> s; for (uint32_t x : a.set) for (uint32_t y : b.set) s.push_back(x + y);
        if (s.size() <= 16) return from_set(s);
    }
    int64_t al, ah, bl, bh; if (int_range(a, al, ah) && int_range(b, bl, bh)) return make_int(al + bl, ah + bh);
    return Val::any();
}
inline Val imul(const Val &a, const Val &b) {
    if (!a.set.empty() && !b.set.empty()) {
        std::vector<uint32_t> s; for (uint32_t x : a.set) for (uint32_t y : b.set) s.push_back(x * y);
        if (s.size() <= 16) return from_set(s);
    }
    int64_t al, ah, bl, bh; if (!int_range(a, al, ah) || !int_range(b, bl, bh)) return Val::any();
    const int64_t c[4]{al * bl, al * bh, ah * bl, ah * bh};
    return make_int(std::min({c[0], c[1], c[2], c[3]}), std::max({c[0], c[1], c[2], c[3]}));
}
// Shift counts are lane 0 of the count operand (the kernel's and reference's convention), low five bits.
inline bool shift_count(const Val &b, uint32_t &s) { int64_t lo, hi; if (!int_range(b, lo, hi) || lo != hi) return false; s = uint32_t(lo) & 31u; return true; }
inline Val ishl(const Val &a, const Val &b) {
    uint32_t s; if (!shift_count(b, s)) return Val::any();
    if (!a.set.empty()) { std::vector<uint32_t> r; for (uint32_t x : a.set) r.push_back(x << s); return from_set(r); }
    int64_t lo, hi; if (!int_range(a, lo, hi)) return Val::any();
    return make_int(lo * (int64_t(1) << s), hi * (int64_t(1) << s));
}
inline int64_t floor_div_pow2(int64_t x, uint32_t s) { return x >= 0 ? (x >> s) : -((-x + (int64_t(1) << s) - 1) >> s); }
inline Val ishr(const Val &a, const Val &b) {
    uint32_t s; if (!shift_count(b, s)) return Val::any();
    if (!a.set.empty()) { std::vector<uint32_t> r; for (uint32_t x : a.set) r.push_back(uint32_t(int32_t(x) >> s)); return from_set(r); }
    int64_t lo, hi; if (!int_range(a, lo, hi)) return Val::any();
    return make_int(floor_div_pow2(lo, s), floor_div_pow2(hi, s));
}
inline Val ushr(const Val &a, const Val &b) {
    uint32_t s; if (!shift_count(b, s)) return Val::any();
    if (!a.set.empty()) { std::vector<uint32_t> r; for (uint32_t x : a.set) r.push_back(x >> s); return from_set(r); }
    int64_t lo, hi; if (!int_range(a, lo, hi) || lo < 0) return Val::any();
    return make_int(lo >> s, hi >> s);
}
inline Val itof(const Val &a) {
    if (!a.set.empty()) {
        std::vector<uint32_t> s; for (uint32_t x : a.set) { const float f = float(int32_t(x)); uint32_t b; std::memcpy(&b, &f, 4); s.push_back(b); }
        return from_set(s);
    }
    int64_t lo, hi; if (int_range(a, lo, hi)) return Val::range(double(float(lo)), double(float(hi)));
    return Val::any();
}
inline Val ige(const Val &a, const Val &b) {
    int64_t al, ah, bl, bh;
    if (!int_range(a, al, ah) || !int_range(b, bl, bh)) return from_set({0u, 0xffffffffu});
    if (!a.set.empty() && !b.set.empty() && a.set.size() * b.set.size() <= 256) {
        bool t = false, f = false; for (uint32_t x : a.set) for (uint32_t y : b.set) (int32_t(x) >= int32_t(y) ? t : f) = true;
        std::vector<uint32_t> s; if (f) s.push_back(0u); if (t) s.push_back(0xffffffffu);
        return from_set(s);
    }
    if (al >= bh) return from_set({0xffffffffu}); if (ah < bl) return from_set({0u});
    return from_set({0u, 0xffffffffu});
}
inline Val select(const Val &c, const Val &a, const Val &b) {
    bool t = false, f = false;
    if (c.set.empty()) t = f = true; else for (uint32_t x : c.set) (x ? t : f) = true;
    if (t && !f) return a; if (f && !t) return b; return hull(a, b);
}
using Lanes = std::array<Val, 4>;
// (u, v) coordinate boxes every sample of one texture reaches over the rectangle; a
// wrap seam yields two boxes per axis case.
struct Box { double umin, umax, vmin, vmax; };
// Texel ranges an ld instruction addresses (inclusive, in elements); an unknown axis reads anywhere on it.
struct TexelBox { int64_t x0 = 0, x1 = 0, y0 = 0, y1 = 0; bool x_known = false, y_known = false; };
struct Window { bool any = false, bounded = true; std::vector<Box> boxes; std::vector<TexelBox> texels; };
// `output`, when given, receives the values written to o0 (lanes the program never writes stay
// exactly zero, as the row shader initialises them).
// Mirror addressing folds a coordinate span onto [0, 1]: a span inside one integer cell maps
// exactly (reversed in odd cells); a span crossing a cell edge covers the whole axis.
inline std::pair<double, double> mirror_span(double lo, double hi, bool mirror) {
    if (!mirror) return {lo, hi};
    const double cell = std::floor(lo);
    if (std::floor(hi) != cell && hi != cell + 1) return {0.0, 1.0};
    const double a = lo - cell, b = hi - cell;
    const bool odd = std::fmod(std::fabs(cell), 2.0) == 1.0;
    return odd ? std::make_pair(1.0 - b, 1.0 - a) : std::make_pair(a, b);
}
inline void evaluate(const LoweredProgram &program, const std::vector<uint32_t> &constants, const std::array<Lanes, 8> &inputs, std::array<Window, NR_FRAME_MAX_TEXTURES> &windows, Lanes *output = nullptr) {
    std::array<Lanes, 16> temps{};
    for (auto &t : temps) for (auto &l : t) l = Val::exact(0);
    if (output) for (auto &l : *output) l = Val::exact(0);
    auto operand = [&](const Words &w, unsigned k) {
        const uint32_t src = w[2 + k], kind = src & 15u, idx = (src >> 4) & 255u, lanes = (src >> 12) & 255u, negate = (src >> 20) & 1u, absolute = (src >> 21) & 1u, slot = (src >> 22) & 15u;
        const uint32_t rel = detail::texture_op(w[0]) ? 0u : (w[5] >> (k * 10)) & 0x3ffu;
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
            if (absolute) v = lift1(v, abs);
            if (negate) {
                if (!detail::integer_op(w[0])) v = lift1(v, neg);
                else if (!v.set.empty()) v = from_set([&] { std::vector<uint32_t> s; for (uint32_t b : v.set) s.push_back(0u - b); return s; }());
                else { int64_t lo, hi; v = int_range(v, lo, hi) ? make_int(-hi, -lo) : Val::any(); }
            }
            out[c] = v;
        }
        return out;
    };
    const auto &words = program.ps_words; size_t loop_end = SIZE_MAX;
    for (size_t wi = 0; wi < words.size(); ++wi) {
        const auto &w = words[wi];
        const uint32_t op = w[0], dest = w[1], mask = (dest >> 12) & 15u, sat = (dest >> 16) & 1u, dest_kind = dest & 15u, dest_index = (dest >> 4) & 255u;
        if (op == core::NR_OP_LOOP) {
            // Every value the body writes is unknown for all iterations and after the loop; the body
            // is walked once for its samples (their windows) with those unknowns in place.
            loop_end = wi + 1; while (loop_end < words.size() && words[loop_end][0] != core::NR_OP_ENDLOOP) ++loop_end;
            for (size_t m = wi + 1; m < loop_end; ++m) {
                const uint32_t d = words[m][1], kind = d & 15u, index = (d >> 4) & 255u, written = (d >> 12) & 15u;
                if (kind == 0 && index < 16) for (unsigned c = 0; c < 4; ++c) if (written & (1u << c)) temps[index][c] = Val::any();
                if (kind != 0 && index == 0 && output) for (unsigned c = 0; c < 4; ++c) if (written & (1u << c)) (*output)[c] = Val::any();
            }
            continue;
        }
        if (op == core::NR_OP_BREAKC) continue;
        if (op == core::NR_OP_ENDLOOP) { loop_end = SIZE_MAX; continue; }
        const bool in_loop = loop_end != SIZE_MAX && wi < loop_end;
        const unsigned sources = detail::source_count(op);
        const Lanes a = operand(w, 0), b = sources >= 2 ? operand(w, 1) : Lanes{}, e = sources >= 3 ? operand(w, 2) : Lanes{};
        Lanes r;
        if (op == core::NR_OP_SAMPLE) {
            const uint32_t texture = w[5] & 15u;
            if (trace_sink()) std::fprintf(trace_sink(), "sample t%u u=[%.4g,%.4g]%s v=[%.4g,%.4g]%s known=%d%d\n", texture, a[0].lo, a[0].hi, a[0].alt ? "+alt" : "", a[1].lo, a[1].hi, a[1].alt ? "+alt" : "", a[0].known, a[1].known);
            if (texture < NR_FRAME_MAX_TEXTURES) {
                auto &win = windows[texture]; win.any = true;
                if (!a[0].known || !a[1].known) win.bounded = false;
                else for (const auto &u : cases(a[0])) for (const auto &v : cases(a[1])) {
                    const auto mu = mirror_span(u.lo, u.hi, texture < program.texture_mirror_u.size() && program.texture_mirror_u[texture]);
                    const auto mv = mirror_span(v.lo, v.hi, texture < program.texture_mirror_v.size() && program.texture_mirror_v[texture]);
                    win.boxes.push_back({mu.first, mu.second, mv.first, mv.second});
                }
            }
            for (auto &l : r) l = Val::range(0, 1);   // UNORM texel colors
            const uint32_t lanes = (w[5] >> 12) & 255u; Lanes swizzled; for (unsigned c = 0; c < 4; ++c) swizzled[c] = r[(lanes >> (2 * c)) & 3u]; r = swizzled;
        } else if (op == core::NR_OP_LD) {
            // Integer fetch: the address lanes bound the elements read; ld returns zero outside the resource,
            // so an unknown axis needs at most the whole axis (never an unbounded window).
            const uint32_t texture = w[5] & 15u; const bool buffer = (w[5] >> 20) & 1u, uint_texture = (w[5] >> 21) & 1u;
            if (texture < NR_FRAME_MAX_TEXTURES) {
                auto &win = windows[texture]; win.any = true;
                TexelBox box; box.x_known = int_range(a[0], box.x0, box.x1);
                if (buffer) { box.y_known = true; box.y0 = box.y1 = 0; } else box.y_known = int_range(a[1], box.y0, box.y1);
                win.texels.push_back(box);
            }
            if (uint_texture) { r[0] = Val::any(); r[1] = make_int(0, 0); r[2] = make_int(0, 0); r[3] = make_int(1, 1); }   // R32_UINT element: (x, 0, 0, 1)
            else for (auto &l : r) l = Val::range(0, 1);
            const uint32_t lanes = (w[5] >> 12) & 255u; Lanes swizzled; for (unsigned c = 0; c < 4; ++c) swizzled[c] = r[(lanes >> (2 * c)) & 3u]; r = swizzled;
        } else if (op == core::NR_OP_DDX || op == core::NR_OP_DDY) {
            // Screen derivative of a varying lane: the coefficient of the pixel position in its
            // affine form (exact for planar interpolation); with a perspective divide, the quotient rule.
            const uint32_t src = w[2], reg = (src >> 4) & 255u, lanes = (src >> 12) & 255u, divide = w[3];
            auto gradient = [&](const Val &v) {
                Val g; if (!v.affine) return g;
                double coef = 0; for (const auto &[s, c] : v.terms) { if (s == (op == core::NR_OP_DDX ? 0u : 1u)) coef = c; else if (s > 1 && !(s < pixel_independent().size() && pixel_independent()[s])) return g; }
                return Val::constant(coef);
            };
            for (unsigned c = 0; c < 4; ++c) {
                const uint32_t lane = (lanes >> (2 * c)) & 3u;
                if (reg >= 8) { r[c] = Val::any(); continue; }
                const Val va = inputs[reg][lane].stripped(), da = gradient(va);
                if (divide >= 4) { r[c] = da; continue; }
                const Val ww = inputs[reg][divide].stripped(), dw = gradient(ww);
                if (!da.known || !dw.known) { r[c] = Val::any(); continue; }
                r[c] = div(add(mul(da, ww), neg(mul(va, dw))), mul(ww, ww));
            }
        } else if (op == core::NR_OP_DP2 || op == core::NR_OP_DP3 || op == core::NR_OP_DP4) {
            const unsigned n = op == core::NR_OP_DP2 ? 2 : op == core::NR_OP_DP3 ? 3 : 4;
            Val sum = Val::exact(0); for (unsigned c = 0; c < n; ++c) sum = lift2(sum, lift2(a[c], b[c], mul), add);
            for (auto &l : r) l = sum;
        } else for (unsigned c = 0; c < 4; ++c) {
            switch (op) {
            case core::NR_OP_MOV: r[c] = a[c]; break;
            case core::NR_OP_ADD: r[c] = lift2(a[c], b[c], add); break;
            case core::NR_OP_MUL: r[c] = lift2(a[c], b[c], mul); break;
            case core::NR_OP_MAD: r[c] = lift2(lift2(a[c], b[c], mul), e[c], add); break;
            case core::NR_OP_DIV: r[c] = lift2(a[c], b[c], div); break;
            case core::NR_OP_MIN: r[c] = lift2(a[c], b[c], vmin); break;
            case core::NR_OP_MAX: r[c] = lift2(a[c], b[c], vmax); break;
            case core::NR_OP_FRC: r[c] = lift1(a[c], frc); break;
            case core::NR_OP_FLOOR: r[c] = lift1(a[c], floor_); break;
            case core::NR_OP_ITOF: r[c] = lift1(a[c], itof); break;
            case core::NR_OP_MOVC: {
                // The condition's cases merge by pattern; an unknown mask in any case keeps both branches.
                Val cond = a[c].alt ? join(cases(a[c])) : a[c].stripped(); if (a[c].alt && cond.set.empty()) cond = Val::any();
                r[c] = lift2(b[c], e[c], [&](const Val &x, const Val &y) { return select(cond, x, y); }); break;
            }
            case core::NR_OP_LT: case core::NR_OP_GE: case core::NR_OP_EQ: case core::NR_OP_NE: r[c] = lift2(a[c], b[c], [&](const Val &x, const Val &y) { return compare(x, y, op); }); break;
            case core::NR_OP_IGE: r[c] = lift2(a[c], b[c], ige); break;
            case core::NR_OP_AND: r[c] = lift2(a[c], b[c], [](const Val &x, const Val &y) { return bitwise(x, y, false); }); break;
            case core::NR_OP_OR: r[c] = lift2(a[c], b[c], [](const Val &x, const Val &y) { return bitwise(x, y, true); }); break;
            case core::NR_OP_IADD: r[c] = lift2(a[c], b[c], iadd); break;
            case core::NR_OP_FTOI: r[c] = lift1(a[c], ftoi); break;
            case core::NR_OP_IMAD: r[c] = lift2(lift2(a[c], b[c], imul), e[c], iadd); break;
            case core::NR_OP_ISHL: r[c] = lift2(a[c], b[c], ishl); break;
            case core::NR_OP_ISHR: r[c] = lift2(a[c], b[c], ishr); break;
            case core::NR_OP_USHR: r[c] = lift2(a[c], b[c], ushr); break;
            default: r[c] = Val::any();
            }
        }
        if (sat) for (auto &l : r) l = lift1(l, saturate);
        if (trace_sink()) {
            std::fprintf(trace_sink(), "op%u -> %s%u.m%u:", op, dest_kind ? "o" : "r", dest_index, mask);
            for (unsigned c = 0; c < 4; ++c) { const auto &l = r[c]; if (!l.known) std::fprintf(trace_sink(), " [?]"); else { std::fprintf(trace_sink(), " [%.4g,%.4g|%.3g", l.lo, l.hi, l.c0); for (const auto &[s, k] : l.terms) std::fprintf(trace_sink(), "%+.3gs%u", k, s); std::fprintf(trace_sink(), "]"); } }
            std::fprintf(trace_sink(), "\n");
        }
        if (in_loop) continue;   // loop-written values stay unknown
        if (dest_kind == 0 && dest_index < 16) for (unsigned c = 0; c < 4; ++c) if (mask & (1u << c)) temps[dest_index][c] = r[c];
        if (dest_kind != 0 && dest_index == 0 && output) for (unsigned c = 0; c < 4; ++c) if (mask & (1u << c)) (*output)[c] = r[c];
    }
}
// True when every case of `v` is exactly zero.
inline bool exactly_zero(const Val &v) { for (const auto &c : cases(v)) if (!c.known || c.lo != 0 || c.hi != 0) return false; return true; }
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
    interval::extent() = {cx[0], cy[0], cx[1], cy[1]}; interval::reset_symbols();
    for (unsigned r = 0, k = 0; r < 8; ++r) {
        if (!(program.ps_inputs & (1u << r))) continue;
        const uint32_t mode = (program.interpolation >> (r * 4)) & 15u;
        for (unsigned c = 0; c < 4; ++c) {
            const float q[3]{NR_V4_LANE(v[0]->varying[k], c), NR_V4_LANE(v[1]->varying[k], c), NR_V4_LANE(v[2]->varying[k], c)};
            if (!std::isfinite(q[0]) || !std::isfinite(q[1]) || !std::isfinite(q[2])) continue;
            if (mode == 1) { inputs[r][c] = interval::Val::form(q[0], 0, 0); continue; }
            // Three zero vertex values interpolate to exactly zero (every product is zero), with no drift guard.
            if (q[0] == 0.0f && q[1] == 0.0f && q[2] == 0.0f) { inputs[r][c] = interval::Val::exact(0); continue; }
            if (planar) {
                // The varying is a plane over the triangle: an affine form in the pixel
                // position, widened by the interpolation drift guard.
                const double a = ((q[1] - q[0]) * (y[2] - y[0]) - (q[2] - q[0]) * (y[1] - y[0])) / det, b = ((x[1] - x[0]) * (q[2] - q[0]) - (x[2] - x[0]) * (q[1] - q[0])) / det;
                auto form = interval::Val::form(q[0] - a * x[0] - b * y[0], a, b);
                if (form.known) {
                    const double margin = 1e-5 * (1.0 + std::fabs(form.hi - form.lo));
                    inputs[r][c] = interval::add(form, interval::Val::noise(-margin, margin)); continue;
                }
            }
            inputs[r][c] = interval::Val::range(std::min({q[0], q[1], q[2]}), std::max({q[0], q[1], q[2]}));
        }
        ++k;
    }
    return inputs;
}
// Up to two texel windows of texture `t` over `rect` from interval evaluation of the
// pixel stage. The sample boxes of every triangle and case are merged into one
// rectangle, or two when a gap along one axis (a wrap seam) makes two rectangles
// smaller than their hull. Returns false when a coordinate is unbounded.
using Windows = std::array<Rect, 2>;
inline Rect box_rect(const interval::Box &b, uint32_t tw, uint32_t th, bool point) {
    auto clamp01 = [](double x) { return x < 0 ? 0.0 : (x > 1 ? 1.0 : x); };
    const double half = point ? 0.0 : 0.5;
    return {std::max(0, int32_t(std::floor(clamp01(b.umin) * double(tw) - half)) - 1), std::max(0, int32_t(std::floor(clamp01(b.vmin) * double(th) - half)) - 1),
            std::min(int32_t(tw), int32_t(std::floor(clamp01(b.umax) * double(tw) - half)) + 3), std::min(int32_t(th), int32_t(std::floor(clamp01(b.vmax) * double(th) - half)) + 3)};
}
inline int64_t area(const Rect &r) { return r.empty() ? 0 : int64_t(r.x1 - r.x0) * int64_t(r.y1 - r.y0); }
inline Windows merge_rects(std::vector<Rect> rects) {
    rects.erase(std::remove_if(rects.begin(), rects.end(), [](const Rect &r) { return r.empty(); }), rects.end());
    if (rects.empty()) return {Rect{0, 0, 0, 0}, Rect{0, 0, 0, 0}};
    Rect hull = rects[0]; for (const auto &r : rects) hull = unite(hull, r);
    Windows best{hull, Rect{0, 0, 0, 0}}; int64_t best_area = area(hull);
    // Try splitting along the widest gap in x, then in y.
    for (int axis = 0; axis < 2; ++axis) {
        auto lo = [&](const Rect &r) { return axis ? r.y0 : r.x0; }; auto hi = [&](const Rect &r) { return axis ? r.y1 : r.x1; };
        std::sort(rects.begin(), rects.end(), [&](const Rect &a, const Rect &b) { return lo(a) < lo(b); });
        int32_t reach = hi(rects[0]); int32_t gap = 0; size_t cut = 0;
        for (size_t n = 1; n < rects.size(); ++n) { if (lo(rects[n]) - reach > gap) { gap = lo(rects[n]) - reach; cut = n; } reach = std::max(reach, hi(rects[n])); }
        if (!cut) continue;
        Rect a = rects[0], b = rects[cut]; for (size_t n = 0; n < rects.size(); ++n) { if (n < cut) a = unite(a, rects[n]); else b = unite(b, rects[n]); }
        if (area(a) + area(b) < best_area) { best = {a, b}; best_area = area(a) + area(b); }
    }
    return best;
}
// Textures read with ld have no sampler: their windows are point windows whatever the binding says.
inline bool loaded_texture(const LoweredProgram &program, unsigned t) { return t < program.texture_loaded.size() && program.texture_loaded[t]; }
inline bool interval_windows(const LoweredProgram &program, const DrawPlan &plan, Rect rect, unsigned t, uint32_t tw, uint32_t th, bool point, Windows &out) {
    std::vector<Rect> rects; point = point || loaded_texture(program, t);
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
        for (const auto &b : w.boxes) rects.push_back(box_rect(b, tw, th, point));
        for (const auto &b : w.texels) {
            const int64_t x0 = b.x_known ? std::max<int64_t>(0, b.x0) : 0, x1 = b.x_known ? std::min<int64_t>(tw, b.x1 + 1) : tw;
            const int64_t y0 = b.y_known ? std::max<int64_t>(0, b.y0) : 0, y1 = b.y_known ? std::min<int64_t>(th, b.y1 + 1) : th;
            if (x0 < x1 && y0 < y1) rects.push_back({int32_t(x0), int32_t(y0), int32_t(x1), int32_t(y1)});
        }
    }
    out = merge_rects(rects);
    return true;
}
// True when the pixel stage writes exactly (0, 0, 0, 0) at every pixel of `rect`: under
// source-over blending such a draw leaves the target untouched (0 * 255 + dst * (1 - 0) = dst).
inline bool transparent_output(const LoweredProgram &program, const DrawPlan &plan, Rect rect) {
    for (size_t first = 0; first + 2 < plan.projected.size(); first += 3) {
        const core::nr_projected *v[3]{&plan.projected[first], &plan.projected[first + 1], &plan.projected[first + 2]};
        const Rect bbox{int32_t(std::floor(std::min({v[0]->x, v[1]->x, v[2]->x}))), int32_t(std::floor(std::min({v[0]->y, v[1]->y, v[2]->y}))),
                        int32_t(std::ceil(std::max({v[0]->x, v[1]->x, v[2]->x}))) + 1, int32_t(std::ceil(std::max({v[0]->y, v[1]->y, v[2]->y}))) + 1};
        const Rect clipped = intersect(bbox, rect);
        if (clipped.empty()) continue;
        std::array<interval::Window, NR_FRAME_MAX_TEXTURES> windows{}; interval::Lanes output;
        interval::evaluate(program, plan.constants, varying_intervals(program, plan, first, clipped), windows, &output);
        for (const auto &l : output) if (!interval::exactly_zero(l)) return false;
    }
    return true;
}
inline bool interval_window(const LoweredProgram &program, const DrawPlan &plan, Rect rect, unsigned t, uint32_t tw, uint32_t th, bool point, Rect &out) {
    Windows w; if (!interval_windows(program, plan, rect, t, tw, th, point, w)) return false;
    out = unite(w[0], w[1]); return true;
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
inline Rect texel_window(const LoweredProgram &program, const DrawPlan &plan, Rect rect, unsigned t, uint32_t tw, uint32_t th, bool point);
// One or two texel windows for texture `t` over `rect`; the second is empty unless a seam splits the taps.
inline Windows texel_windows(const LoweredProgram &program, const DrawPlan &plan, Rect rect, unsigned t, uint32_t tw, uint32_t th, bool point) {
    if (plan.whole[t] || !tw || !th) return {Rect{0, 0, int32_t(tw), int32_t(th)}, Rect{0, 0, 0, 0}};
    const auto sources = coordinate_sources(program); const auto &source = sources[t];
    if (!source.direct || source.taps.empty()) {
        Windows w;
        if (interval_windows(program, plan, rect, t, tw, th, point, w)) return {intersect(w[0], plan.texels[t]), intersect(w[1], plan.texels[t])};
        return {plan.texels[t], Rect{0, 0, 0, 0}};
    }
    return {texel_window(program, plan, rect, t, tw, th, point), Rect{0, 0, 0, 0}};
}
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
// Bytes one span needs for this draw alone inside tile `tile` (its pixels plus the texel windows of the clipped draw).
inline size_t tile_span_bytes(const LoweredProgram &program, const DrawPlan &plan, Rect bounds, Rect tile, size_t entry_bytes, uint32_t vertex_words, uint32_t constant_words,
                              const std::array<uint32_t, NR_FRAME_MAX_TEXTURES> &tw, const std::array<uint32_t, NR_FRAME_MAX_TEXTURES> &th, const std::array<bool, NR_FRAME_MAX_TEXTURES> &point) {
    const Rect clip = intersect(bounds, tile);
    size_t bytes = NR_FRAME_HEADER_BYTES + NR_FRAME_COMMAND_BYTES + entry_bytes + 64 + size_t(vertex_words) * 4 + size_t(constant_words) * 4 + 4 + size_t(tile.x1 - tile.x0) * size_t(tile.y1 - tile.y0) * 4;
    if (clip.empty()) return bytes;
    for (size_t t = 0; t < program.texture_slots.size() && t < NR_FRAME_MAX_TEXTURES; ++t) {
        const auto w = texel_windows(program, plan, clip, unsigned(t), tw[t], th[t], point[t]);
        bytes += NR_FRAME_WINDOW_BYTES + (w[0].empty() ? 4 : size_t(w[0].x1 - w[0].x0) * size_t(w[0].y1 - w[0].y0) * 4);
        if (!w[1].empty()) bytes += NR_FRAME_WINDOW_BYTES + size_t(w[1].x1 - w[1].x0) * size_t(w[1].y1 - w[1].y0) * 4;
    }
    return bytes;
}
inline uint32_t fitting_tile(const LoweredProgram &program, const DrawPlan &plan, Rect bounds, size_t entry_bytes, uint32_t vertex_words, uint32_t constant_words,
                             const std::array<uint32_t, NR_FRAME_MAX_TEXTURES> &tw, const std::array<uint32_t, NR_FRAME_MAX_TEXTURES> &th, const std::array<bool, NR_FRAME_MAX_TEXTURES> &point) {
    if (bounds.empty()) return 128;
    for (uint32_t edge : {128u, 64u, 32u}) {
        bool fits = true;
        for (int32_t ty = bounds.y0 / int32_t(edge) * int32_t(edge); ty < bounds.y1 && fits; ty += int32_t(edge))
            for (int32_t tx = bounds.x0 / int32_t(edge) * int32_t(edge); tx < bounds.x1 && fits; tx += int32_t(edge)) {
                const Rect tile{tx, ty, tx + int32_t(edge), ty + int32_t(edge)};
                if (intersect(bounds, tile).empty()) continue;
                if (tile_span_bytes(program, plan, bounds, tile, entry_bytes, vertex_words, constant_words, tw, th, point) > span_bytes - 1024) fits = false;
            }
        if (fits) return edge;
    }
    return 0;
}
// Splits a tile in halves along its longer side; the executor splits the same way.
inline std::pair<Rect, Rect> split_tile(const Rect &tile) {
    const int32_t w = tile.x1 - tile.x0, h = tile.y1 - tile.y0;
    if (w >= h) { const int32_t mid = tile.x0 + w / 2; return {{tile.x0, tile.y0, mid, tile.y1}, {mid, tile.y0, tile.x1, tile.y1}}; }
    const int32_t mid = tile.y0 + h / 2; return {{tile.x0, tile.y0, tile.x1, mid}, {tile.x0, mid, tile.x1, tile.y1}};
}
constexpr int32_t min_adaptive_tile = 8;
// A draw whose 128-pixel tiles do not all fit may still batch when every tile can be
// bisected (down to 8 pixels) into sub-tiles that fit: a texture seam then confines
// the wide window to a few small sub-tiles. The target extent bounds partial tiles.
inline bool adaptive_fit(const LoweredProgram &program, const DrawPlan &plan, Rect bounds, Rect target, size_t entry_bytes, uint32_t vertex_words, uint32_t constant_words,
                         const std::array<uint32_t, NR_FRAME_MAX_TEXTURES> &tw, const std::array<uint32_t, NR_FRAME_MAX_TEXTURES> &th, const std::array<bool, NR_FRAME_MAX_TEXTURES> &point) {
    if (bounds.empty()) return true;
    unsigned budget = 4096;   // sub-tiles examined; a hopeless draw gives up early
    std::function<bool(Rect)> fits = [&](Rect tile) -> bool {
        if (intersect(bounds, tile).empty()) return true;
        if (!budget--) return false;
        if (tile_span_bytes(program, plan, bounds, tile, entry_bytes, vertex_words, constant_words, tw, th, point) <= span_bytes - 1024) return true;
        if (tile.x1 - tile.x0 <= min_adaptive_tile && tile.y1 - tile.y0 <= min_adaptive_tile) return false;
        const auto halves = split_tile(tile);
        return fits(halves.first) && fits(halves.second);
    };
    for (int32_t ty = bounds.y0 / 128 * 128; ty < bounds.y1; ty += 128)
        for (int32_t tx = bounds.x0 / 128 * 128; tx < bounds.x1; tx += 128)
            if (!fits(intersect({tx, ty, tx + 128, ty + 128}, target))) return false;
    return true;
}
}
