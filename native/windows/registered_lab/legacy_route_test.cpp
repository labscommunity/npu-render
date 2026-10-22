// Host contract for the shared per-draw route selection in legacy_route.hpp.
// The driver records legacy_route's (abi, flags) into every batched draw and the
// worker derives the same pair for a fallback; the per-draw encoder in
// indexed_exchange_protocol.hpp then has to accept the packet built from it.
// Trial 10 (docs/research/frame-batch-live-trials-2026-10-20.md) stopped when a
// mirror-addressed blur draw fell back with abi 11: the route lacked the abi 12/14
// addressing upgrades and encode() rejected the mirror sampler word. This test
// routes a matrix of draw states through legacy_route and requires that every
// result names a header shape_valid() accepts and a packet encode()/decode()
// round-trips, plus the exact pairs for the regressions. It is a CPU consistency
// check between two host functions; it is not NPU evidence.
#include "legacy_route.hpp"
#include "indexed_exchange_protocol.hpp"
#include "linear_texture_fixture.hpp"
#include "point_texture_fixture.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
namespace raster = nr::raster;
namespace shader = nr::shader;
namespace ex = nr::registered_lab::indexed_exchange;
using nr::registered_lab::LegacyRouteInputs;
using nr::registered_lab::legacy_route;
namespace {
unsigned failures = 0, reported = 0;
void check(bool value, const char *what, const std::string &where) {
    if (value) return;
    ++failures;
    if (reported < 32) { ++reported; std::fprintf(stderr, "legacy route: %s [%s]\n", what, where.c_str()); }
}
// vs_4_0: position, colour and texture coordinates pass through unchanged, the
// linkage the two captured DWM pixel fixtures expect (v1 colour, v2 uv).
constexpr uint32_t vertex_words[]{
    0x00010040u, 0x00000025u,
    0x0300005fu, 0x001010f2u, 0x00000000u,                            // dcl_input v0.xyzw
    0x0300005fu, 0x001010f2u, 0x00000001u,                            // dcl_input v1.xyzw
    0x0300005fu, 0x00101032u, 0x00000002u,                            // dcl_input v2.xy
    0x04000067u, 0x001020f2u, 0x00000000u, 0x00000001u,               // dcl_output_siv o0.xyzw, position
    0x03000065u, 0x001020f2u, 0x00000001u,                            // dcl_output o1.xyzw
    0x03000065u, 0x00102032u, 0x00000002u,                            // dcl_output o2.xy
    0x05000036u, 0x001020f2u, 0x00000000u, 0x00101e46u, 0x00000000u,  // mov o0.xyzw, v0.xyzw
    0x05000036u, 0x001020f2u, 0x00000001u, 0x00101e46u, 0x00000001u,  // mov o1.xyzw, v1.xyzw
    0x05000036u, 0x00102032u, 0x00000002u, 0x00101046u, 0x00000002u,  // mov o2.xy, v2.xyxx
    0x0100003eu};                                                     // ret
// ps_4_0: one solid colour, the untextured shape of DWM's fills.
constexpr uint32_t solid_pixel_words[]{
    0x00000040u, 0x0000000eu,
    0x03000065u, 0x001020f2u, 0x00000000u,                            // dcl_output o0.xyzw
    0x08000036u, 0x001020f2u, 0x00000000u, 0x00004002u,
    0x3e800000u, 0x3f000000u, 0x3f400000u, 0x3f800000u,               // mov o0.xyzw, l(.25, .5, .75, 1)
    0x0100003eu};                                                     // ret
static_assert(std::size(vertex_words) == vertex_words[1], "vertex program length token");
static_assert(std::size(solid_pixel_words) == solid_pixel_words[1], "pixel program length token");
constexpr uint32_t target_width = 16, target_height = 12, target_format = 87;
struct Fixture { const char *name; shader::Program program; bool textured; };
struct Case {
    const Fixture *fixture = nullptr;
    shader::TextureAddress u = shader::TextureAddress::clamp, v = shader::TextureAddress::clamp;
    bool linear = false, smaller = false, scissored = false, front_ccw = false;
    raster::BlendMode blend = raster::BlendMode::source_copy;
    raster::CullMode cull = raster::CullMode::none;
    uint32_t write_mask = 15, vertices = 6;
};
std::string describe(const Case &c) {
    char text[160];
    std::snprintf(text, sizeof text, "program=%s u=%u v=%u linear=%u smaller=%u scissor=%u blend=%u cull=%u ccw=%u mask=%u vertices=%u",
        c.fixture->name, unsigned(c.u), unsigned(c.v), unsigned(c.linear), unsigned(c.smaller), unsigned(c.scissored),
        unsigned(c.blend), unsigned(c.cull), unsigned(c.front_ccw), c.write_mask, c.vertices);
    return text;
}
// The driver derives addressed/bordered from the samplers bound to the pixel program's
// sampler slots and the worker from the batch's texture bindings; an untextured
// program binds none, while the slot-0 filter and source size are state the driver
// reads regardless of the program.
LegacyRouteInputs inputs_of(const Case &c) {
    LegacyRouteInputs in; in.pixel = &c.fixture->program; in.textured = c.fixture->textured;
    if (in.textured) {
        in.addressed = c.u != shader::TextureAddress::clamp || c.v != shader::TextureAddress::clamp;
        in.bordered = c.u == shader::TextureAddress::border || c.v == shader::TextureAddress::border;
    }
    in.linear = c.linear; in.source_smaller = c.smaller; in.scissored = c.scissored;
    in.vertex_count = c.vertices; in.write_mask = c.write_mask;
    in.blend_mode = c.blend; in.cull = c.cull; in.front_ccw = c.front_ccw;
    return in;
}
uint32_t bits(float value) { uint32_t u = 0; std::memcpy(&u, &value, sizeof u); return u; }
std::vector<shader::VertexRegisters> triangle_list(uint32_t count) {
    std::vector<shader::VertexRegisters> inputs(count);
    const float xy[6][2]{{-1, 1}, {1, 1}, {1, -1}, {1, -1}, {-1, -1}, {-1, 1}};
    for (uint32_t n = 0; n < count; ++n) {
        const float shrink = 1.f / float(1 + n / 6);  // later quads nest inside the first
        inputs[n][0] = {bits(xy[n % 6][0] * shrink), bits(xy[n % 6][1] * shrink), bits(.5f), bits(1.f)};
        inputs[n][1] = {bits(1.f), bits(1.f), bits(1.f), bits(1.f)};
        inputs[n][2] = {bits((xy[n % 6][0] + 1) * .5f), bits((1 - xy[n % 6][1]) * .5f), 0, 0};
    }
    return inputs;
}
// The header the worker fills from a route (frame_batch_executor.hpp): the route's
// pair plus the target, the vertex count and the two token counts.
ex::Header header_of(const Case &c, const shader::Program &vs, uint32_t abi, uint32_t flags) {
    ex::Header h; h.abi = abi; h.flags = flags; h.width = target_width; h.height = target_height;
    h.vertices = c.vertices; h.format = target_format;
    h.vs_words = uint32_t(vs.bytecode.size()); h.ps_words = uint32_t(c.fixture->program.bytecode.size());
    h.sequence = 1; h.client_pid = 42; h.nonce[0] = 0x12345678u; h.nonce[1] = 0xabcdefu;
    return h;
}
// A request consistent with the draw state: the texture travels in the resource
// table with its sampler on the resource route and as the single source otherwise;
// the border colour is carried only for border addressing, as both request builders do.
ex::Request request_of(const Case &c, const shader::Program &vs, uint32_t abi, uint32_t flags) {
    ex::Request r; r.header = header_of(c, vs, abi, flags);
    r.vertex = vs; r.pixel = c.fixture->program;
    r.viewport = {0, 0, float(target_width), float(target_height), 0, 1};
    r.inputs = triangle_list(c.vertices);
    r.background.assign(size_t(target_width) * target_height, raster::PackedColor{7, 11, 13, 255});
    if (c.scissored) r.scissor = std::array<int32_t, 4>{2, 3, 13, 10};
    if (c.blend == raster::BlendMode::component_coverage) r.blend_constant = {.25f, .5f, .75f, 1.f};
    if (c.fixture->textured) {
        raster::PackedTexture texture{c.smaller ? target_width / 2 : target_width, c.smaller ? target_height / 2 : target_height, target_format, {}};
        texture.pixels.assign(size_t(texture.width) * texture.height, raster::PackedColor{1, 2, 3, 255});
        if (flags & 0x2000) {
            r.resources[0] = texture;
            auto &sampler = r.samplers[0];
            sampler.filter = c.linear ? shader::TextureFilter::linear : shader::TextureFilter::point;
            sampler.u = c.u; sampler.v = c.v;
            if (c.u == shader::TextureAddress::border || c.v == shader::TextureAddress::border) sampler.border = {0, .5f, 1, 1};
        } else r.texture = texture;
    }
    return r;
}
bool encode_rejects(ex::Request &r) {
    try { (void)ex::encode(r); } catch (const std::invalid_argument &) { return true; }
    return false;
}
uint64_t packets = 0;
void route_case(const Case &c, const shader::Program &vs) {
    const auto where = describe(c);
    const auto in = inputs_of(c);
    const auto route = legacy_route(in);
    const auto abi = route.first, flags = route.second;
    const bool coverage = c.blend == raster::BlendMode::component_coverage;
    // (1) The header the worker would send passes the encoder's shape rule.
    const auto header = header_of(c, vs, abi, flags);
    check(ex::shape_valid(header), "shape_valid rejects the routed header", where);
    // (2) The packet built from the route encodes and round-trips with its route and addressing intact.
    auto request = request_of(c, vs, abi, flags);
    std::vector<uint8_t> packet;
    try { packet = ex::encode(request); ++packets; } catch (const std::invalid_argument &) { check(false, "encode rejects the routed packet", where); }
    if (!packet.empty()) {
        const auto decoded = ex::decode(packet);
        check(decoded.header.abi == abi && decoded.header.flags == flags, "the packet does not preserve the route", where);
        check(ex::blend_mode(decoded.header) == c.blend, "the packet does not preserve the blend mode", where);
        if (flags & 0x2000) {
            const auto &sampler = decoded.samplers[0];
            check(sampler.u == c.u && sampler.v == c.v && sampler.filter == request.samplers[0].filter && sampler.border == request.samplers[0].border,
                  "the packet loses the sampler addressing", where);
        }
    }
    // The route's own contract, read back from the protocol's flag layout.
    check(ex::blend_mode(header) == c.blend, "flags do not encode the blend mode", where);
    check(((flags >> 8) & 15u) == c.write_mask, "flags do not carry the write mask", where);
    check(((flags & 4u) != 0) == c.scissored, "scissor flag disagrees with the scissor state", where);
    check(!in.addressed || (flags & 0x2000), "an addressed draw must take the resource route", where);
    check((abi == 12) == (in.addressed && !in.bordered && !coverage), "abi 12 is exactly the mirror-addressed route without border or component coverage", where);
    check((abi == 14) == (in.bordered && !coverage), "abi 14 is exactly the border-addressed route without component coverage", where);
    check((abi == 15) == coverage, "abi 15 is exactly component coverage", where);
    if (raster::extended_blend(c.blend)) check((flags & 0x10000) && ((flags >> 17) & 15u) == uint32_t(c.blend), "an extended blend must set flag 0x10000 with its mode in bits 17..20", where);
    else check(!(flags & 0x1f0000), "a non-extended blend must leave bits 16..20 clear", where);
    check(((flags & 0x4000) != 0) == (c.blend == raster::BlendMode::destination_in) && ((flags & 0x8000) != 0) == (c.blend == raster::BlendMode::destination_out),
          "destination blends must select their flag", where);
    if (c.cull != raster::CullMode::none) check(((flags >> 5) & 3u) == uint32_t(c.cull) && ((flags & 128u) != 0) == c.front_ccw, "cull flags disagree with the cull state", where);
    else check(!(flags & 0xe0), "an unculled draw must leave the cull bits clear", where);
    if (!c.fixture->textured) {
        check(!(flags & 2) && !(flags & 0x2000), "an untextured draw must not take a textured route", where);
        auto bare = c; bare.linear = false; bare.smaller = false;
        check(legacy_route(inputs_of(bare)) == route, "an untextured route must ignore the slot-0 filter and source size", where);
    }
}
void expect_route(const Case &c, uint32_t abi, uint32_t flags, const char *what) {
    const auto route = legacy_route(inputs_of(c));
    if (route.first != abi || route.second != flags) {
        char text[96]; std::snprintf(text, sizeof text, "expected abi %u flags 0x%x, routed abi %u flags 0x%x", abi, flags, route.first, route.second);
        check(false, what, describe(c) + " " + text);
    }
}
void regressions(const Fixture &solid, const Fixture &point, const Fixture &linear, const shader::Program &vs) {
    Case base; base.fixture = &point;
    // Trial 10: the blur draws (192x64 targets, mirror u/v, scissored, blend plus, 6 vertices)
    // recorded abi 11 with flags 0x152f07; the worker must derive abi 12 with the same flags.
    auto trial10 = base; trial10.u = trial10.v = shader::TextureAddress::mirror; trial10.linear = true; trial10.scissored = true; trial10.blend = raster::BlendMode::plus;
    expect_route(trial10, 12, 0x152f07, "trial 10 mirror-addressed blur draw with blend plus");
    {
        // The pair the driver recorded before the fix: its header alone passes shape_valid,
        // and the encoder rejects the mirror sampler word under abi 11. That is the failure trial 10 hit.
        auto recorded = request_of(trial10, vs, 11, 0x152f07);
        check(ex::shape_valid(recorded.header), "the pre-fix trial 10 header should pass shape_valid on its own", "trial 10 recorded route");
        check(encode_rejects(recorded), "the encoder should reject a mirror sampler under abi 11", "trial 10 recorded route");
    }
    auto mirror = base; mirror.u = shader::TextureAddress::mirror;
    expect_route(mirror, 12, 0x2f02, "mirror on one axis routes to abi 12");
    { auto pre_fix = request_of(mirror, vs, 8, 0x2f02); check(encode_rejects(pre_fix), "the encoder should reject a mirror sampler under abi 8", "mirror recorded as abi 8"); }
    auto border = base; border.v = shader::TextureAddress::border;
    expect_route(border, 14, 0x2f02, "border on one axis routes to abi 14");
    { auto pre_fix = request_of(border, vs, 8, 0x2f02); check(encode_rejects(pre_fix), "the encoder should reject a border sampler under abi 8", "border recorded as abi 8"); }
    { auto as_mirror = request_of(border, vs, 12, 0x2f02); check(encode_rejects(as_mirror), "the encoder should reject a border sampler under abi 12", "border recorded as abi 12"); }
    auto both = border; both.u = shader::TextureAddress::mirror;
    expect_route(both, 14, 0x2f02, "mirror and border together route to abi 14");
    auto mirror_in = mirror; mirror_in.blend = raster::BlendMode::destination_in;
    expect_route(mirror_in, 12, 0x6f03, "mirror with destination_in keeps the blend flag under abi 12");
    auto border_out = border; border_out.blend = raster::BlendMode::destination_out;
    expect_route(border_out, 14, 0xaf03, "border with destination_out keeps the blend flag under abi 14");
    auto mirror_over = mirror; mirror_over.blend = raster::BlendMode::destination_over;
    expect_route(mirror_over, 12, 0x2f03 | 0x10000 | (4u << 17), "mirror with an extended blend keeps bits 16..20 under abi 12");
    Case solid_case; solid_case.fixture = &solid;
    auto coverage = solid_case; coverage.blend = raster::BlendMode::component_coverage;
    expect_route(coverage, 15, 0xf01 | 0x10000 | (13u << 17), "component coverage routes to abi 15");
    auto mirror_coverage = mirror; mirror_coverage.blend = raster::BlendMode::component_coverage;
    expect_route(mirror_coverage, 15, 0x2f03 | 0x10000 | (13u << 17), "component coverage with mirror addressing routes to abi 15 on the resource route");
    // The per-draw executors the fixtures were captured for.
    expect_route(base, 2, 0xf02, "the point fixture at 1:1 takes the point texture route");
    auto filtered = base; filtered.linear = true;
    expect_route(filtered, 4, 0xf0a, "a linear filter takes the linear texture route");
    Case modulated; modulated.fixture = &linear;
    expect_route(modulated, 5, 0xf12, "the modulated fixture takes the modulated point route");
    auto nine = base; nine.vertices = 9;
    expect_route(nine, 7, 0x1f12, "a textured draw beyond one quad takes the general triangle route");
    auto masked = base; masked.write_mask = 5;
    expect_route(masked, 7, 0x1512, "a partial write mask takes the general triangle route");
    auto sparse = solid_case; sparse.vertices = 66;
    expect_route(sparse, 1, 0xf00, "an untextured 66-vertex draw takes the uniform route");
    auto clipped = solid_case; clipped.scissored = true;
    expect_route(clipped, 3, 0xf04, "an untextured scissored draw takes the scissor route");
    auto culled = solid_case; culled.cull = raster::CullMode::back; culled.front_ccw = true;
    expect_route(culled, 6, 0xfc0, "an untextured culled draw takes the cull route");
}
}
int main() try {
    const Fixture solid{"solid", shader::parse_pixel(solid_pixel_words, std::size(solid_pixel_words)), false};
    const Fixture point{"point", shader::parse_pixel(nr::registered_lab::test_fixture::point_texture_pixel_words, std::size(nr::registered_lab::test_fixture::point_texture_pixel_words)), true};
    const Fixture linear{"linear", shader::parse_pixel(nr::registered_lab::test_fixture::linear_texture_pixel_words, std::size(nr::registered_lab::test_fixture::linear_texture_pixel_words)), true};
    const auto textured = [](const Fixture &f) { return std::any_of(f.program.textures.begin(), f.program.textures.end(), [](bool t) { return t; }); };
    if (textured(solid) || !textured(point) || !textured(linear)) throw std::runtime_error("fixture programs do not have the expected texture bindings");
    const auto vs = shader::parse_vertex(vertex_words, std::size(vertex_words));
    uint64_t cases = 0;
    for (const Fixture *fixture : {&solid, &point, &linear})
    for (uint32_t address = 0; address < (fixture->textured ? 9u : 1u); ++address)
    for (const bool linear_filter : {false, true}) for (const bool smaller : {false, true}) for (const bool scissored : {false, true})
    for (uint32_t mode = 0; mode <= uint32_t(raster::BlendMode::component_coverage); ++mode)
    for (uint32_t cull = 0; cull < 5; ++cull) for (const uint32_t mask : {15u, 5u}) for (const uint32_t vertices : {6u, 9u, 66u}) {
        Case c; c.fixture = fixture;
        c.u = shader::TextureAddress(address % 3); c.v = shader::TextureAddress(address / 3);
        c.linear = linear_filter; c.smaller = smaller; c.scissored = scissored;
        c.blend = raster::BlendMode(mode);
        c.cull = cull == 0 ? raster::CullMode::none : cull <= 2 ? raster::CullMode::back : raster::CullMode::front;
        c.front_ccw = cull == 2 || cull == 4;
        c.write_mask = mask; c.vertices = vertices;
        route_case(c, vs); ++cases;
    }
    regressions(solid, point, linear, vs);
    std::printf("{\"legacy_route_contract\":%s,\"cases\":%llu,\"encoded_packets\":%llu,\"failures\":%u,\"trial_10_pair\":[12,%u],\"execution_device\":\"host\",\"npu_evidence\":false}\n",
        failures ? "false" : "true", static_cast<unsigned long long>(cases), static_cast<unsigned long long>(packets), failures, 0x152f07u);
    return failures ? 1 : 0;
} catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
