// Contract test for the frame batch planner's triangle groups: a draw of small quads whose
// texels lie far apart in one texture binds one small window per group instead of the
// hull of all of them, and fits the span budget at the 128-pixel tile.
#include "frame_kernel.hpp"
#include "frame_batch_planner.hpp"
#include "frame_native_inventory.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace nr;
#define TEST(v) do { if (!(v)) throw std::runtime_error(#v); } while (false)
namespace {
uint32_t bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
// Pair 1 of the inventory (595296ba): v0 position, v1 colour, v2 texture coordinate, v3 unused;
// the vertex stage scales by cb0 = (2/w, -2/h, -1, 1) and the pixel stage samples t0 at v2.
shader::VertexRegisters vertex(float x, float y, float u, float v) {
    shader::VertexRegisters r{};
    r[0] = {bits(x), bits(y), bits(1.f), bits(1.f)}; r[1] = {bits(1.f), bits(1.f), bits(1.f), bits(1.f)};
    r[2] = {bits(u), bits(v), bits(0.f), bits(1.f)}; r[3] = {0u, 0u, 0u, bits(1.f)};
    return r;
}
// A quad of two triangles covering [x0, x1) x [y0, y1) with texture coordinates [u0, u1) x [v0, v1).
void quad(std::vector<shader::VertexRegisters> &out, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1) {
    out.push_back(vertex(x0, y0, u0, v0)); out.push_back(vertex(x1, y0, u1, v0)); out.push_back(vertex(x1, y1, u1, v1));
    out.push_back(vertex(x0, y0, u0, v0)); out.push_back(vertex(x1, y1, u1, v1)); out.push_back(vertex(x0, y1, u0, v1));
}
struct Case { frame::LoweredProgram program; frame::DrawPlan plan; std::vector<uint32_t> constants; uint32_t vertices = 0; };
Case plan_case(size_t pair, const std::vector<shader::VertexRegisters> &inputs, float width, float height, const std::array<uint32_t, 4> &tw, const std::array<uint32_t, 4> &th, const std::array<bool, 4> &point) {
    const auto &p = frame::inventory_pairs[pair];
    const auto vs = shader::parse_vertex(p.vs, p.vs_words), ps = shader::parse_pixel(p.ps, p.ps_words);
    Case c; c.program = frame::lower(vs, ps, shader::TextureSamplers{}); TEST(c.program.eligible());
    frame::NativeInventory inventory; const auto *identity = inventory.find(c.program); TEST(identity != nullptr); c.program.set_native(identity->host_index);
    c.constants = {bits(2.f / width), bits(-2.f / height), bits(-1.f), bits(1.f)};
    for (uint32_t n = 0; n < c.program.ps_constants * 4; ++n) c.constants.push_back(0u);
    c.vertices = uint32_t(inputs.size());
    c.plan = frame::plan_draw(c.program, frame::pack_vertices(c.program, inputs), c.vertices, c.constants, {0.f, 0.f, width, height}, tw, th, point);
    TEST(c.plan.valid);
    return c;
}
int64_t window_area(const frame::Windows &w) { return frame::area(w[0]) + frame::area(w[1]); }
}
int main() {
    const std::array<uint32_t, 4> tw{1536u, 0u, 0u, 0u}, th{1006u, 0u, 0u, 0u}; const std::array<bool, 4> point{true, false, false, false};
    {
        // Four 2x2-pixel quads reading the four corners of a 1536x1006 texture (probe v120's 64x64 mosaic).
        std::vector<shader::VertexRegisters> inputs;
        const float du = 6.f / 1536.f, dv = 6.f / 1006.f;
        quad(inputs, 0, 0, 2, 2, 0.01f, 0.01f, 0.01f + du, 0.01f + dv);
        quad(inputs, 4, 0, 6, 2, 0.98f, 0.01f, 0.98f + du, 0.01f + dv);
        quad(inputs, 0, 4, 2, 6, 0.01f, 0.98f, 0.01f + du, 0.98f + dv);
        quad(inputs, 4, 4, 6, 6, 0.98f, 0.98f, 0.98f + du, 0.98f + dv);
        auto c = plan_case(1, inputs, 64.f, 64.f, tw, th, point);
        std::printf("{\"stage\":\"mosaic\",\"bounds\":[%d,%d,%d,%d]}\n", c.plan.bounds.x0, c.plan.bounds.y0, c.plan.bounds.x1, c.plan.bounds.y1);
        // The union window of the whole draw is the hull of the corners: larger than a span.
        const auto hull = frame::texel_windows(c.program, c.plan, c.plan.bounds, 0, tw[0], th[0], point[0]);
        TEST(window_area(hull) * 4 > int64_t(frame::span_bytes));
        // Two corners share a command through its second window; the third corner starts a new group.
        const auto groups = frame::group_triangles(c.program, c.plan, c.plan.bounds, tw, th, point);
        TEST(groups.size() == 2);
        for (const auto &g : groups) { TEST(g.count == 4); TEST(!g.windows[0][1].empty()); TEST(window_area(g.windows[0]) <= 2 * 12 * 12); }
        TEST(groups[0].first == 0 && groups[1].first == 4);
        const auto bytes = frame::tile_span_bytes(c.program, c.plan, c.plan.bounds, {0, 0, 128, 128}, c.program.entry.size(), c.vertices * 64, uint32_t(c.constants.size()), tw, th, point);
        TEST(bytes < frame::span_bytes - 1024);
        TEST(frame::fitting_tile(c.program, c.plan, c.plan.bounds, c.program.entry.size(), c.vertices * 64, uint32_t(c.constants.size()), tw, th, point) == 128);
        // A tile that holds only the first two quads groups only them.
        const auto left = frame::group_triangles(c.program, c.plan, {0, 0, 3, 3}, tw, th, point);
        TEST(left.size() == 1 && left[0].first == 0 && left[0].count == 2);
    }
    {
        // A run of 16 adjacent glyph quads reading one atlas row in order: neighbours merge.
        std::vector<shader::VertexRegisters> inputs;
        for (unsigned g = 0; g < 16; ++g) quad(inputs, 10.f + 4.f * g, 20.f, 14.f + 4.f * g, 26.f, 0.2f + g * (5.f / 1536.f), 0.3f, 0.2f + (g + 1) * (5.f / 1536.f), 0.3f + 7.f / 1006.f);
        auto c = plan_case(1, inputs, 1024.f, 768.f, tw, th, point);
        const auto groups = frame::group_triangles(c.program, c.plan, c.plan.bounds, tw, th, point);
        std::printf("{\"stage\":\"glyph-run\",\"groups\":%zu}\n", groups.size());
        TEST(groups.size() <= 2);
        uint32_t covered = 0; for (const auto &g : groups) covered += g.count; TEST(covered == 32);
        TEST(frame::fitting_tile(c.program, c.plan, c.plan.bounds, c.program.entry.size(), c.vertices * 64, uint32_t(c.constants.size()), tw, th, point) == 128);
    }
    {
        // Two quads at opposite corners of the texture: one command, its two windows far apart.
        std::vector<shader::VertexRegisters> inputs;
        quad(inputs, 0, 0, 8, 8, 0.f, 0.f, 8.f / 1536.f, 8.f / 1006.f);
        quad(inputs, 8, 0, 16, 8, 0.99f, 0.99f, 0.99f + 8.f / 1536.f, 0.99f + 8.f / 1006.f);
        auto c = plan_case(1, inputs, 64.f, 64.f, tw, th, point);
        const auto groups = frame::group_triangles(c.program, c.plan, c.plan.bounds, tw, th, point);
        TEST(groups.size() == 1 && groups[0].count == 4 && !groups[0].windows[0][1].empty() && window_area(groups[0].windows[0]) <= 2 * 12 * 12);
        // A triangle that does not reach the clip (past its one-pixel bounding-box slack) is left out of every group.
        const auto right = frame::group_triangles(c.program, c.plan, {10, 0, 16, 8}, tw, th, point);
        TEST(right.size() == 1 && right[0].first == 2 && right[0].count == 2 && right[0].windows[0][1].empty());
    }
    std::printf("{\"frame_plan_test\":true,\"passed\":true}\n");
    return 0;
}
