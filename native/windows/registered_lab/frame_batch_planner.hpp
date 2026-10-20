#pragma once
// Shared planning for frame batches. The driver and the worker run the same
// code so they agree on draw extents and on which texels a draw may sample.
// Planning projects vertices with the kernel's own arithmetic; it never shades.
#include "frame_kernel.hpp"
#include "frame_batch_protocol.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
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
struct CoordinateTap { uint32_t input = 0, lane_u = 0, lane_v = 0, divide_lane = 4; };
// All sample instructions reading one texture; direct only when every tap is a varying (or its perspective divide).
struct CoordinateSource { bool direct = false; std::vector<CoordinateTap> taps; };
struct DrawPlan {
    bool valid = false;                       // every vertex projected within the kernel's limits
    std::string rejection;
    Rect bounds;                              // target pixels touched, before scissor
    std::vector<core::nr_projected> projected;
    std::array<Rect, NR_FRAME_MAX_TEXTURES> texels{};   // sampled texel windows per command texture
    std::array<bool, NR_FRAME_MAX_TEXTURES> whole{};    // window could not be bounded: whole texture
};
// Packs decoded vertex registers for the kernel's vertex pool (declared inputs only).
inline std::vector<uint32_t> pack_vertices(const LoweredProgram &program, const std::vector<shader::VertexRegisters> &vertices) {
    std::vector<uint32_t> words; words.reserve(vertices.size() * 16);
    for (const auto &v : vertices) for (unsigned r = 0; r < 16; ++r) if (program.vs_inputs & (1u << r)) words.insert(words.end(), v[r].begin(), v[r].end());
    return words;
}
// Determines where each sampled texture's coordinates come from.
inline std::array<CoordinateSource, NR_FRAME_MAX_TEXTURES> coordinate_sources(const LoweredProgram &program) {
    std::array<CoordinateSource, NR_FRAME_MAX_TEXTURES> result{};
    // A temporary written by `div input, input.w` (perspective divide) before the sample is also direct.
    struct TempSource { bool valid = false; uint32_t input = 0, lane_u = 0, lane_v = 0, divide_lane = 4; };
    std::array<TempSource, 16> temps{};
    for (const auto &w : program.ps_words) {
        const uint32_t op = w[0], dest = w[1], src0 = w[2], src1 = w[3], extra = w[5];
        const uint32_t dest_kind = dest & 15u, dest_index = (dest >> 4) & 255u, mask = (dest >> 12) & 15u;
        if (op == core::NR_OP_SAMPLE) {
            const uint32_t texture = extra & 15u, kind = src0 & 15u, index = (src0 >> 4) & 255u, lanes = (src0 >> 12) & 255u;
            if (texture >= NR_FRAME_MAX_TEXTURES) continue;
            auto &c = result[texture];
            bool direct = false; CoordinateTap tap;
            if (kind == 1 && !(src0 >> 20)) { direct = true; tap.input = index; tap.lane_u = lanes & 3u; tap.lane_v = (lanes >> 2) & 3u; }
            else if (kind == 0 && temps[index].valid && !(src0 >> 20)) {
                const auto &t = temps[index];
                direct = true; tap.input = t.input; tap.divide_lane = t.divide_lane;
                // The temp holds (input.lane_u/w, input.lane_v/w) in lanes 0,1; the sample reads lanes of the temp.
                const uint32_t lu = lanes & 3u, lv = (lanes >> 2) & 3u;
                if (lu > 1 || lv > 1) direct = false; else { tap.lane_u = lu ? t.lane_v : t.lane_u; tap.lane_v = lv ? t.lane_v : t.lane_u; }
            }
            if (c.taps.empty()) c.direct = direct; else c.direct = c.direct && direct;
            if (direct) c.taps.push_back(tap);
            continue;
        }
        if (dest_kind == 0 && dest_index < 16) {
            TempSource t;
            const uint32_t kind0 = src0 & 15u, kind1 = src1 & 15u;
            if (op == core::NR_OP_DIV && kind0 == 1 && kind1 == 1 && ((src0 >> 4) & 255u) == ((src1 >> 4) & 255u) && (mask & 3u) == 3u && !(src0 >> 20) && !(src1 >> 20)) {
                const uint32_t l0 = (src0 >> 12) & 255u, l1 = (src1 >> 12) & 255u;
                if ((l1 & 3u) == ((l1 >> 2) & 3u)) { t.valid = true; t.input = (src0 >> 4) & 255u; t.lane_u = l0 & 3u; t.lane_v = (l0 >> 2) & 3u; t.divide_lane = l1 & 3u; }
            }
            temps[dest_index] = t;   // any other write invalidates the provenance
        }
    }
    return result;
}
// Projects a draw with the kernel's arithmetic and bounds its coverage and sampling.
inline DrawPlan plan_draw(const LoweredProgram &program, const std::vector<uint32_t> &packed_vertices, uint32_t vertex_count,
                          const std::vector<uint32_t> &constants, const std::array<float, 4> &viewport,
                          const std::array<uint32_t, NR_FRAME_MAX_TEXTURES> &texture_width, const std::array<uint32_t, NR_FRAME_MAX_TEXTURES> &texture_height,
                          const std::array<bool, NR_FRAME_MAX_TEXTURES> &point_filter) {
    DrawPlan plan;
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
        if (!src.direct || src.taps.empty() || !tw || !th) { plan.whole[t] = true; plan.texels[t] = {0, 0, int32_t(tw), int32_t(th)}; continue; }
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
    if (!source.direct || source.taps.empty()) return plan.texels[t];
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
}
