#pragma once
// Shared planning for frame batches. The driver and the worker run the same
// code so they agree on draw extents and on which texels a draw may sample.
// Planning projects vertices with the kernel's own arithmetic; it never shades.
#include "frame_kernel.hpp"
#include "frame_batch_protocol.hpp"
#include <algorithm>
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
struct CoordinateSource { bool direct = false; uint32_t input = 0, lane_u = 0, lane_v = 0, divide_lane = 4; };
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
    const uint8_t *entry = program.entry.data();
    auto word = [&](size_t at) { uint32_t v; std::memcpy(&v, entry + at, 4); return v; };
    const uint32_t vs_count = word(0), ps_count = word(4), instructions_offset = word(44);
    // A temporary written by `div input, input.w` (perspective divide) before the sample is also direct.
    struct TempSource { bool valid = false; uint32_t input = 0, lane_u = 0, lane_v = 0, divide_lane = 4; };
    std::array<TempSource, 16> temps{};
    for (uint32_t n = vs_count; n < vs_count + ps_count; ++n) {
        const size_t at = instructions_offset + n * NR_FRAME_INSTRUCTION_BYTES;
        const uint32_t op = word(at), dest = word(at + 4), src0 = word(at + 8), src1 = word(at + 12), extra = word(at + 20);
        const uint32_t dest_kind = dest & 15u, dest_index = (dest >> 4) & 255u, mask = (dest >> 12) & 15u;
        if (op == core::NR_OP_SAMPLE) {
            const uint32_t texture = extra & 15u, kind = src0 & 15u, index = (src0 >> 4) & 255u, lanes = (src0 >> 12) & 255u;
            CoordinateSource c;
            if (kind == 1 && !(src0 >> 20)) { c.direct = true; c.input = index; c.lane_u = lanes & 3u; c.lane_v = (lanes >> 2) & 3u; }
            else if (kind == 0 && temps[index].valid && !(src0 >> 20)) {
                const auto &t = temps[index];
                c.direct = true; c.input = t.input; c.divide_lane = t.divide_lane;
                // The temp holds (input.lane_u/w, input.lane_v/w) in lanes 0,1; the sample reads lanes of the temp.
                const uint32_t lu = lanes & 3u, lv = (lanes >> 2) & 3u;
                if (lu > 1 || lv > 1) c.direct = false; else { c.lane_u = lu ? t.lane_v : t.lane_u; c.lane_v = lv ? t.lane_v : t.lane_u; }
            }
            if (texture < NR_FRAME_MAX_TEXTURES) result[texture] = c;
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
    for (unsigned t = 0; t < NR_FRAME_MAX_TEXTURES; ++t) command.textures[t].window = NR_FRAME_NO_WINDOW;
    const uint32_t background = 0;
    std::vector<uint8_t> span;
    try { span = SpanBuilder::build(0, 0, 1, 1, 28, {program.entry}, {command}, {}, &background); }
    catch (const std::exception &e) { plan.rejection = std::string("planning span: ") + e.what(); return plan; }
    static thread_local std::vector<uint8_t> scratch(span_bytes);
    core::nr_span s{}; core::nr_command c{}; core::nr_program p{};
    if (!core::nr_span_load(&s, span.data(), scratch.data()) || !core::nr_command_load(&s, 0, &c) || !core::nr_program_load(&s, 0, &p)) { plan.rejection = "planning contract"; return plan; }
    plan.projected.resize(vertex_count);
    int32_t minx = INT32_MAX, miny = INT32_MAX, maxx = INT32_MIN, maxy = INT32_MIN;
    for (uint32_t v = 0; v < vertex_count; ++v) {
        if (!core::nr_vertex(&s, &c, &p, v, &plan.projected[v])) { plan.rejection = "vertex projection limits"; return plan; }
        const auto &q = plan.projected[v];
        minx = std::min(minx, q.sx); maxx = std::max(maxx, q.sx); miny = std::min(miny, q.sy); maxy = std::max(maxy, q.sy);
    }
    plan.bounds = {std::max(0, (minx - 128) >> 8), std::max(0, (miny - 128) >> 8), std::max(0, ((maxx - 128) >> 8) + 1), std::max(0, ((maxy - 128) >> 8) + 1)};
    const auto sources = coordinate_sources(program);
    for (unsigned t = 0; t < program.texture_slots.size() && t < NR_FRAME_MAX_TEXTURES; ++t) {
        const auto &src = sources[t];
        const uint32_t tw = texture_width[t], th = texture_height[t];
        if (!src.direct || !tw || !th) { plan.whole[t] = true; plan.texels[t] = {0, 0, int32_t(tw), int32_t(th)}; continue; }
        // Varying index of the input register inside the projected varyings.
        uint32_t varying = 0; bool found = false;
        for (unsigned r = 0, k = 0; r < 8; ++r) { if (!(p.ps_inputs & (1u << r))) continue; if (r == src.input) { varying = k; found = true; break; } ++k; }
        if (!found) { plan.whole[t] = true; plan.texels[t] = {0, 0, int32_t(tw), int32_t(th)}; continue; }
        float umin = 1e30f, umax = -1e30f, vmin = 1e30f, vmax = -1e30f; bool finite = true;
        for (uint32_t v = 0; v < vertex_count; ++v) {
            const auto &var = plan.projected[v].varying[varying];
            float u = core::nr_float(var[src.lane_u]), vv = core::nr_float(var[src.lane_v]);
            if (src.divide_lane < 4) { const float w = core::nr_float(var[src.divide_lane]); if (!(w != 0.f) || !std::isfinite(w)) { finite = false; break; } u /= w; vv /= w; }
            if (!std::isfinite(u) || !std::isfinite(vv)) { finite = false; break; }
            umin = std::min(umin, u); umax = std::max(umax, u); vmin = std::min(vmin, vv); vmax = std::max(vmax, vv);
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
}
