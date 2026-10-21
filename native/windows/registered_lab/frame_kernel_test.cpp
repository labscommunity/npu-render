// Frame kernel host checks: the shared core against the scalar shader reference
// on the captured DWM shader pairs, plus NPU fixture generation. CPU only.
#include "frame_kernel.hpp"
#include "frame_batch_planner.hpp"
#include "shader_reference.hpp"
#include "blend_reference.hpp"
#include "frame_inventory.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
using namespace nr;
namespace {
using Pair = frame::InventoryPair;
const auto &pairs = frame::inventory_pairs;
std::mt19937_64 rng(2026);
float uniform(float lo, float hi) { return lo + (hi - lo) * float(std::uniform_real_distribution<double>(0, 1)(rng)); }
uint32_t fbits(float v) { uint32_t b; std::memcpy(&b, &v, 4); return b; }
float ffloat(uint32_t b) { float v; std::memcpy(&v, &b, 4); return v; }
struct Textures { frame::Window windows[4]; shader::TextureShapes shapes{}; shader::TexturePixels pixels; shader::TextureSamplers samplers{}; };
// zero_alpha: every texel transparent, the case DWM hands the acrylic shader (division by a
// zero alpha then min/max over NaN) and where SM4 NaN semantics decide the result.
Textures make_textures(const frame::LoweredProgram &lowered, const shader::TextureSamplers &samplers, uint32_t size, bool zero_alpha = false) {
    Textures t; t.samplers = samplers;
    for (size_t n = 0; n < lowered.texture_slots.size(); ++n) {
        auto &w = t.windows[n]; w.w = w.texture_w = size; w.h = w.texture_h = size; w.origin_x = w.origin_y = 0; w.format = n & 1 ? 87u : 28u;
        w.pixels.resize(size_t(size) * size);
        const auto slot = lowered.texture_slots[n];
        t.shapes[slot] = {size, size, 15}; t.pixels[slot].resize(w.pixels.size());
        for (size_t i = 0; i < w.pixels.size(); ++i) {
            w.pixels[i] = uint32_t(rng()) & (zero_alpha ? 0x00ffffffu : 0xffffffffu);
            for (unsigned c = 0; c < 4; ++c) { const unsigned byte = (w.format == 87 && c < 3) ? 2 - c : c; t.pixels[slot][i][c] = float((w.pixels[i] >> (byte * 8)) & 255u) / 255.f; }
        }
    }
    return t;
}
// Passthrough vertex program feeding the pixel program's declared inputs from vertex inputs.
shader::Program passthrough_vertex(const shader::Program &ps, uint32_t &position_register) {
    shader::Program vs; vs.stage = shader::Stage::vertex;
    position_register = ps.position_input < 8 ? ps.position_input : 16;
    if (position_register == 16) for (unsigned r = 0; r < 8; ++r) if (!ps.inputs[r]) { position_register = r; break; }
    frame::require(position_register < 8, "no free position register");
    vs.position_output = position_register; vs.inputs[position_register] = 15; vs.outputs[position_register] = 15;
    auto mov = [&](unsigned r) {
        shader::Instruction i; i.opcode = shader::Opcode::move; i.source_count = 1;
        i.destination.kind = shader::Register::output; i.destination.index = r; i.destination.mask = 15;
        i.sources[0].kind = shader::Register::input; i.sources[0].index = r; vs.instructions.push_back(i);
    };
    mov(position_register);
    for (unsigned r = 0; r < 16; ++r) if (ps.inputs[r] && r != ps.position_input) { vs.inputs[r] = 15; vs.outputs[r] = 15; mov(r); }
    return vs;
}
struct SpanContext { std::vector<uint8_t> input; frame::core::nr_span span{}; frame::core::nr_command command{}; frame::core::nr_program program{}; frame::core::nr_command_state state{}; };
SpanContext context_for(const std::vector<uint8_t> &program_entry, const frame::Command &command, const std::vector<frame::Window> &windows, uint32_t format = 28) {
    SpanContext c; std::vector<uint32_t> background(128 * 128, 0xff000000u);
    c.input = frame::SpanBuilder::build(0, 0, 128, 128, format, {program_entry}, {command}, windows, background.data());
    static std::vector<uint8_t> scratch(frame::span_bytes);
    frame::require(frame::core::nr_span_load(&c.span, c.input.data(), scratch.data()), "span load");
    frame::require(frame::core::nr_command_load(&c.span, 0, &c.command), "command load");
    frame::require(frame::core::nr_program_load(&c.span, 0, &c.program), "program load");
    frame::require(frame::core::nr_command_state_load(&c.span, &c.command, &c.program, &c.state), "state load");
    return c;
}
// Stage registers for one native stage call: inputs from SM4 registers, constants and immediates from the span.
void load_stage(const SpanContext &ctx, bool vertex, const shader::Registers &inputs, frame::core::nr_shade &sh, uint32_t &escapes) {
    for (unsigned r = 0; r < 16; ++r) sh.in[r] = frame::core::nr_v4_from_bits(inputs[r][0], inputs[r][1], inputs[r][2], inputs[r][3]);
    for (unsigned r = 0; r < 8; ++r) sh.out[r] = frame::core::nr_v4_zero();
    sh.constants = vertex ? ctx.state.vs_constants : ctx.state.ps_constants;
    sh.immediates = vertex ? ctx.program.vs_immediates : ctx.program.ps_immediates;
    sh.escapes = &escapes; static uint32_t overflows = 0; sh.overflows = &overflows;
}
shader::Constants random_constants(const shader::Program &p, bool unit = false) {
    shader::Constants constants;
    for (unsigned i = 0; i < 14; ++i) { constants[i].resize(p.constants[i]); for (auto &v : constants[i]) for (auto &lane : v) lane = fbits(unit ? uniform(0, 1) : uniform(-2, 2)); }
    return constants;
}
std::vector<uint32_t> flatten(const shader::Program &vs, const shader::Constants &vc, const shader::Program &ps, const shader::Constants &pc) {
    std::vector<uint32_t> out;
    for (unsigned i = 0; i < 14; ++i) for (uint32_t e = 0; e < vs.constants[i]; ++e) for (auto lane : vc[i][e]) out.push_back(lane);
    for (unsigned i = 0; i < 14; ++i) for (uint32_t e = 0; e < ps.constants[i]; ++e) for (auto lane : pc[i][e]) out.push_back(lane);
    return out;
}
frame::Command base_command(const frame::LoweredProgram &lowered, const Textures &textures, const shader::TextureSamplers &samplers) {
    frame::Command c; c.viewport = {0, 0, 128, 128}; c.clip = {0, 0, 128, 128};
    for (size_t n = 0; n < lowered.texture_slots.size(); ++n) { c.textures[n].window = uint32_t(n); c.textures[n].point = samplers[lowered.sampler_slots[n]].filter == shader::TextureFilter::point; c.textures[n].mirror_u = samplers[lowered.sampler_slots[n]].u == shader::TextureAddress::mirror; c.textures[n].mirror_v = samplers[lowered.sampler_slots[n]].v == shader::TextureAddress::mirror; }
    (void)textures; return c;
}
struct Stats { unsigned vs_cases = 0, vs_exact = 0, ps_cases = 0, ps_exact = 0, ps_within_one = 0, ps_far = 0; };
Stats compare_stage(const Pair &pair, const shader::Program &vs, const shader::Program &ps, const shader::TextureSamplers &samplers, const frame::LoweredProgram &lowered) {
    Stats stats;
    // Trials 64.. use transparent textures and unit-range constants (the captured DWM operating point).
    for (unsigned trial = 0; trial < 96; ++trial) {
        const bool zero_alpha = trial >= 64;
        auto textures = make_textures(lowered, samplers, 8, zero_alpha);
        auto vc = random_constants(vs, zero_alpha), pc = random_constants(ps, zero_alpha);
        frame::Command command = base_command(lowered, textures, samplers);
        command.constants = flatten(vs, vc, ps, pc);
        shader::Registers vertex_inputs{};
        for (unsigned r = 0; r < 16; ++r) if (vs.inputs[r]) for (unsigned c = 0; c < 4; ++c) vertex_inputs[r][c] = fbits(uniform(-1.5f, 1.5f));
        command.vertex_count = 3;
        for (unsigned v = 0; v < 3; ++v) for (unsigned r = 0; r < 16; ++r) if (vs.inputs[r]) for (unsigned c = 0; c < 4; ++c) command.vertices.push_back(vertex_inputs[r][c]);
        std::vector<frame::Window> windows(textures.windows, textures.windows + lowered.texture_slots.size());
        auto ctx = context_for(lowered.entry, command, windows);
        frame::core::nr_shade sh{}; uint32_t escapes = 0;
        load_stage(ctx, true, vertex_inputs, sh, escapes);
        if (frame::core::nr_native_vs(ctx.program.native, &ctx.state, &sh)) {
            const auto expected = shader::reference_vertex(vs, vertex_inputs, vc);
            bool exact = true;
            for (unsigned r = 0; r < 8; ++r) if (vs.outputs[r]) for (unsigned c = 0; c < 4; ++c) if ((vs.outputs[r] & (1u << c)) && expected[r][c] != frame::core::nr_v4_bits(sh.out[r], c)) exact = false;
            ++stats.vs_cases; stats.vs_exact += exact;
        }
        // Pixel stage with random varyings.
        shader::Registers pixel_inputs{};
        for (unsigned r = 0; r < 16; ++r) if (ps.inputs[r] && r != ps.position_input) for (unsigned c = 0; c < 4; ++c) pixel_inputs[r][c] = fbits(c < 2 ? uniform(-0.5f, 1.5f) : uniform(0.25f, 2));   // coordinates reach clamp and mirror addressing
        load_stage(ctx, false, pixel_inputs, sh, escapes);
        // The single-pixel SM4 reference has no quad to differentiate; derivative programs are
        // checked through their fixtures (host kernel against the NPU) instead.
        if (frame::detail::uses_derivatives(lowered.ps_words)) continue;
        if (frame::core::nr_native_ps(ctx.program.native, &ctx.state, &sh)) {
            shader::Registers expected;
            // Random constants can drive a shader loop past the reference's iteration budget: no reference for that case.
            try { expected = shader::reference_vertex(ps, pixel_inputs, pc, textures.shapes, textures.samplers, textures.pixels); }
            catch (const std::runtime_error &) { continue; }
            bool exact = true; unsigned far = 0;
            for (unsigned c = 0; c < 4; ++c) {
                const uint32_t actual = frame::core::nr_v4_bits(sh.out[0], c);
                if (expected[0][c] == actual) continue; exact = false;
                const auto a = std::lround(std::fmin(std::fmax(ffloat(expected[0][c]), 0.f), 1.f) * 255), b = std::lround(std::fmin(std::fmax(ffloat(actual), 0.f), 1.f) * 255);
                if (std::labs(a - b) > 1) ++far;
            }
            ++stats.ps_cases; stats.ps_exact += exact; stats.ps_within_one += !exact && !far; stats.ps_far += far > 0;
        }
    }
    (void)pair; return stats;
}
struct FixtureFile { std::vector<uint8_t> bytes; uint32_t count = 0; };
void append_fixture(FixtureFile &file, const std::string &name, const std::vector<uint8_t> &input) {
    std::vector<uint8_t> expected; frame::render_reference(input, expected);
    const uint32_t length = uint32_t(name.size()); const uint8_t *l = reinterpret_cast<const uint8_t *>(&length);
    file.bytes.insert(file.bytes.end(), l, l + 4); file.bytes.insert(file.bytes.end(), name.begin(), name.end());
    while (file.bytes.size() % 4) file.bytes.push_back(0);
    file.bytes.insert(file.bytes.end(), input.begin(), input.end()); file.bytes.insert(file.bytes.end(), expected.begin(), expected.end()); ++file.count;
}
void write_fixtures(const FixtureFile &file, const char *path) {
    std::ofstream out(path, std::ios::binary); const uint64_t span = frame::span_bytes; const uint32_t version = 1;
    out.write("NRFFIX01", 8); out.write(reinterpret_cast<const char *>(&version), 4); out.write(reinterpret_cast<const char *>(&file.count), 4); out.write(reinterpret_cast<const char *>(&span), 8);
    out.write(reinterpret_cast<const char *>(file.bytes.data()), std::streamsize(file.bytes.size()));
}
std::vector<uint32_t> random_background(uint32_t count) { std::vector<uint32_t> v(count); for (auto &p : v) p = uint32_t(rng()); return v; }
}
// Every blend mode and both formats against the repo's scalar blend reference.
unsigned check_blend_modes() {
    unsigned failures = 0;
    for (uint32_t format : {28u, 87u}) for (uint32_t mode = 0; mode <= 13; ++mode) for (unsigned trial = 0; trial < 400; ++trial) {
        frame::core::nr_command cmd{}; cmd.blend_mode = mode; cmd.write_mask = 1 + (trial % 15);
        for (unsigned c = 0; c < 4; ++c) cmd.blend_constant[c] = uniform(-0.2f, 1.2f);
        frame::core::nr_command_state state{}; frame::core::nr_blend_state_load(format, &cmd, &state);
        const uint32_t background = uint32_t(rng());
        float color[4]; for (auto &c : color) c = uniform(-0.2f, 1.2f);
        if (trial % 4 == 3) {
            // Special values the GPU would flush or clamp: NaN, infinities, subnormals, negative zero, huge.
            static const uint32_t specials[]{0x7fc00000u, 0xffc00000u, 0x7f800000u, 0xff800000u, 0x00000001u, 0x807fffffu, 0x80000000u, 0x7e000000u, 0x00400000u};
            const unsigned lane = trial % 4u, which = (trial / 4u) % unsigned(std::size(specials));
            color[(lane + trial) % 4u] = ffloat(specials[which]);
            if (trial % 8 == 7) color[3] = ffloat(specials[(which + 1) % unsigned(std::size(specials))]);
        }
        uint8_t pixel[4]; std::memcpy(pixel, &background, 4);
        frame::core::nr_blend_pixel(&state, pixel, frame::core::nr_v4_make(color[0], color[1], color[2], color[3]));
        uint32_t actual; std::memcpy(&actual, pixel, 4);
        auto clamp_ref = [](float v) { return (std::isnan(v) || v < 0.f) ? 0.f : (v > 1.f ? 1.f : v); };
        const float alpha = clamp_ref(color[3]);
        for (unsigned c = 0; c < 4; ++c) {
            const unsigned logical = (format == 87 && c < 3) ? 2 - c : c;
            uint32_t expected_byte = (background >> (c * 8)) & 255u;
            if (cmd.write_mask & (1u << logical)) {
                const float fg = clamp_ref(color[logical]), dst = float((background >> (c * 8)) & 255u), dst_alpha = float((background >> 24) & 255u);
                const float constant = std::fmin(std::fmax(cmd.blend_constant[logical], 0.f), 1.f);
                const auto terms = raster::reference_blend_terms<float>(raster::BlendMode(mode), fg, alpha, dst, dst_alpha, constant);
                float value = terms[0] + terms[1]; value = std::fmin(std::fmax(value, 0.f), 255.f);
                const uint32_t base = uint32_t(value); const float fraction = value - float(base);
                expected_byte = base + (fraction > .5f || (fraction == .5f && (base & 1u)));
            }
            if (((actual >> (c * 8)) & 255u) != expected_byte) { ++failures; if (failures < 5) std::printf("{\"blend_mismatch\":true,\"mode\":%u,\"format\":%u}\n", mode, format); }
        }
    }
    std::printf("{\"blend_modes_checked\":14,\"formats\":2,\"trials\":400,\"blend_failures\":%u}\n", failures);
    return failures;
}
int main(int argc, char **argv) {
    const char *fixtures_path = argc >= 3 && std::string(argv[1]) == "--fixtures" ? argv[2] : nullptr;
    const char *emit_path = argc >= 3 && std::string(argv[1]) == "--emit" ? argv[2] : nullptr;
    // --emit path [--max-ps N | --blob K]: all eligible pairs, those whose pixel
    // stage has at most N instructions, or the pairs of kernel blob K.
    uint32_t max_ps = UINT32_MAX, blob = UINT32_MAX;
    for (int a = 3; a + 1 < argc; ++a) { if (std::string(argv[a]) == "--max-ps") max_ps = uint32_t(std::stoul(argv[a + 1])); if (std::string(argv[a]) == "--blob") blob = uint32_t(std::stoul(argv[a + 1])); }
    std::vector<uint32_t> compiled_codes;
    if (!emit_path) for (uint32_t k = 0; k < NR_NATIVE_COUNT; ++k) compiled_codes.push_back(frame::core::nr_native_inventory(k));
    auto included = [&](uint32_t code, uint32_t ps_instructions) {
        if (emit_path) return ps_instructions <= max_ps && (blob == UINT32_MAX || frame::blob_of(ps_instructions) == blob);
        return std::find(compiled_codes.begin(), compiled_codes.end(), code) != compiled_codes.end();
    };
    // Pass 1: parse and lower every captured pair plus its passthrough-vertex
    // variant; eligible programs get native indices in this order.
    struct Entry { size_t pair; shader::Program vs, ps, pass; shader::TextureSamplers samplers{}; frame::LoweredProgram lowered, pass_lowered; uint32_t pass_position = 16; bool parsed = false; std::string parse_error; };
    std::vector<Entry> entries(std::size(pairs));
    std::vector<const frame::LoweredProgram *> natives; std::vector<uint32_t> codes;
    for (size_t n = 0; n < std::size(pairs); ++n) {
        auto &e = entries[n]; e.pair = n;
        try { e.vs = shader::parse_vertex(pairs[n].vs, pairs[n].vs_words); e.ps = shader::parse_pixel(pairs[n].ps, pairs[n].ps_words); e.parsed = true; }
        catch (const std::exception &x) { e.parse_error = x.what(); continue; }
        for (unsigned s = 0; s < 16; ++s) { e.samplers[s].filter = (n & 1) ? shader::TextureFilter::point : shader::TextureFilter::linear; if (n % 3 == 0) e.samplers[s].u = e.samplers[s].v = shader::TextureAddress::mirror; }
        e.lowered = frame::lower(e.vs, e.ps, e.samplers);
        if (!e.lowered.eligible()) continue;
        if (!included(uint32_t(n), uint32_t(e.lowered.ps_words.size()))) { e.lowered.rejection = "not in the compiled program set"; continue; }
        try { e.pass = passthrough_vertex(e.ps, e.pass_position); e.pass_lowered = frame::lower(e.pass, e.ps, e.samplers); }
        catch (const std::exception &x) { e.pass_lowered.rejection = x.what(); }
        if (!e.pass_lowered.eligible()) { e.lowered.rejection = "no passthrough twin: " + e.pass_lowered.rejection; continue; }
        e.lowered.set_native(uint32_t(natives.size())); natives.push_back(&e.lowered); codes.push_back(uint32_t(n));
    }
    // Passthrough twins follow all real pairs in the same order (the kernel's dispatch folds them).
    const size_t real_count = natives.size();
    for (size_t k = 0; k < real_count; ++k) {
        auto &e = entries[codes[k]];
        e.pass_lowered.set_native(uint32_t(natives.size())); natives.push_back(&e.pass_lowered); codes.push_back(0x10000u | uint32_t(e.pair));
    }
    if (emit_path) {
        std::ofstream out(emit_path, std::ios::binary); out << frame::emit_native_header(natives, codes);
        std::printf("{\"native_programs\":%zu,\"emitted\":\"%s\"}\n", natives.size(), emit_path);
        return out ? 0 : 1;
    }
    bool stale = natives.size() != NR_NATIVE_COUNT;
    for (size_t k = 0; k < natives.size() && !stale; ++k) stale = frame::core::nr_native_inventory(uint32_t(k)) != codes[k];
    if (stale) { std::printf("{\"error\":\"frame_programs.h is stale: rerun --emit\",\"native_programs\":%zu,\"compiled\":%u}\n", natives.size(), unsigned(NR_NATIVE_COUNT)); return 1; }
    FixtureFile fixtures; unsigned eligible = 0, failures = check_blend_modes();
    for (auto &e : entries) {
        const auto &pair = pairs[e.pair]; const size_t n = e.pair;
        if (!e.parsed) { std::printf("{\"shader\":\"%s\",\"parse_failed\":\"%s\"}\n", pair.sha, e.parse_error.c_str()); continue; }
        const auto &lowered = e.lowered; const auto &vs = e.vs; const auto &ps = e.ps; const auto &samplers = e.samplers;
        if (!lowered.eligible()) { std::printf("{\"shader\":\"%s\",\"eligible\":false,\"reason\":\"%s\"}\n", pair.sha, lowered.rejection.c_str()); continue; }
        ++eligible;
        const auto stats = compare_stage(pair, vs, ps, samplers, lowered);
        const bool samples = !lowered.texture_slots.empty();
        const bool ok = stats.vs_exact == stats.vs_cases && stats.ps_far == 0 && (samples || stats.ps_exact == stats.ps_cases);
        if (!ok) ++failures;
        std::printf("{\"shader\":\"%s\",\"eligible\":true,\"native\":%u,\"vertex_exact\":%u,\"vertex_cases\":%u,\"pixel_exact\":%u,\"pixel_within_one_byte\":%u,\"pixel_far\":%u,\"pixel_cases\":%u,\"textures\":%zu,\"passed\":%s}\n",
            pair.sha, lowered.native(), stats.vs_exact, stats.vs_cases, stats.ps_exact, stats.ps_within_one, stats.ps_far, stats.ps_cases, lowered.texture_slots.size(), ok ? "true" : "false");
        if (!fixtures_path) continue;
        // Fixture A: real vertex program, random constants and vertices. Retry
        // until the draw projects within the kernel's limits and covers pixels.
        for (unsigned format : {28u, 87u}) {
            std::vector<uint8_t> span; uint32_t covered = 0;
            for (unsigned attempt = 0; attempt < 64; ++attempt) {
                auto textures = make_textures(lowered, samplers, 16);
                frame::Command command = base_command(lowered, textures, samplers);
                command.constants = flatten(vs, random_constants(vs), ps, random_constants(ps));
                command.vertex_count = 6; command.blend_mode = uint32_t(n % 14); command.write_mask = 15 - uint32_t(n % 3);
                for (unsigned v = 0; v < 6; ++v) for (unsigned r = 0; r < 16; ++r) if (vs.inputs[r]) for (unsigned c = 0; c < 4; ++c) command.vertices.push_back(fbits(uniform(-1.5f, 1.5f)));
                std::vector<frame::Window> windows(textures.windows, textures.windows + lowered.texture_slots.size());
                auto background = random_background(128 * 128);
                span = frame::SpanBuilder::build(0, 0, 128, 128, format, {lowered.entry}, {command}, windows, background.data());
                std::vector<uint8_t> expected; frame::render_reference(span, expected);
                uint32_t status; std::memcpy(&status, expected.data() + 4, 4); std::memcpy(&covered, expected.data() + 12, 4);
                if (status == 0 && covered) break;
            }
            append_fixture(fixtures, std::string("real-vs-") + pair.sha + (format == 28 ? "-rgba" : "-bgra"), span);
        }
        // Fixture B: passthrough vertex program covering the tile with two triangles.
        const auto &pass = e.pass; const auto &pass_lowered = e.pass_lowered; const uint32_t position_register = e.pass_position;
        if (pass_lowered.eligible()) {
            for (unsigned variant = 0; variant < 2; ++variant) {
                const uint32_t format = variant ? 87u : 28u;
                auto textures = make_textures(pass_lowered, samplers, 24);
                frame::Command command = base_command(pass_lowered, textures, samplers);
                command.constants = flatten(pass, shader::Constants{}, ps, random_constants(ps));
                command.blend_mode = uint32_t((n + variant) % 14); command.write_mask = 15; command.vertex_count = 6;
                command.clip = {4, 2, 120, 126};
                const float quad[6][2]{{-0.9f, 0.95f}, {0.9f, 0.95f}, {0.9f, -0.9f}, {-0.9f, 0.95f}, {0.9f, -0.9f}, {-0.9f, -0.9f}};
                const float corner[6][2]{{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
                for (unsigned v = 0; v < 6; ++v) for (unsigned r = 0; r < 16; ++r) if (pass.inputs[r]) {
                    if (r == position_register) { command.vertices.insert(command.vertices.end(), {fbits(quad[v][0]), fbits(quad[v][1]), fbits(0.5f), fbits(1.0f)}); }
                    else command.vertices.insert(command.vertices.end(), {fbits(corner[v][0]), fbits(corner[v][1]), fbits(uniform(0.5f, 1.0f)), fbits(1.0f)});
                }
                std::vector<frame::Window> windows(textures.windows, textures.windows + pass_lowered.texture_slots.size());
                auto background = random_background(128 * 128);
                append_fixture(fixtures, std::string("quad-") + pair.sha + (format == 28 ? "-rgba" : "-bgra"), frame::SpanBuilder::build(0, 0, 128, 128, format, {pass_lowered.entry}, {command}, windows, background.data()));
                // Planner soundness on the same quad: the plan must be valid, its sampled windows bounded
                // (coordinates stay in [0, 1]), and a quad with these varyings must never prove transparent.
                {
                    std::array<uint32_t, 4> tw{}, th{}; std::array<bool, 4> point{};
                    for (size_t k = 0; k < pass_lowered.texture_slots.size(); ++k) { tw[k] = textures.windows[k].texture_w; th[k] = textures.windows[k].texture_h; point[k] = command.textures[k].point; }
                    const auto plan = frame::plan_draw(pass_lowered, command.vertices, 6, command.constants, {0, 0, 128, 128}, tw, th, point);
                    frame::require(plan.valid, "quad plan must be valid");
                    if (frame::transparent_output(pass_lowered, plan, {4, 2, 120, 126})) {
                        // The proof must agree with the host kernel: under source-over the quad leaves every pixel as it was.
                        frame::Command check = command; check.blend_mode = 1;
                        auto span = frame::SpanBuilder::build(0, 0, 128, 128, format, {pass_lowered.entry}, {check}, windows, background.data());
                        std::vector<uint8_t> rendered; frame::render_reference(span, rendered);
                        uint32_t status, covered; std::memcpy(&status, rendered.data() + 4, 4); std::memcpy(&covered, rendered.data() + 12, 4);
                        const bool unchanged = status == 0 && covered && std::memcmp(rendered.data() + NR_FRAME_OUTPUT_PIXELS, background.data(), size_t(128) * 128 * 4) == 0;
                        std::printf("{\"shader\":\"%s\",\"quad_proved_transparent\":true,\"kernel_unchanged\":%s}\n", pair.sha, unchanged ? "true" : "false");
                        frame::require(unchanged, (std::string("transparent proof disagrees with the host kernel: ") + pair.sha).c_str());
                    }
                }
                // Fixture C: the same quad with every texture split into two windows (left and
                // right halves), so taps are served from the second window without escapes.
                if (variant == 0 && !pass_lowered.texture_slots.empty()) {
                    std::vector<frame::Window> halves;
                    for (size_t k = 0; k < pass_lowered.texture_slots.size(); ++k) {
                        const auto &full = textures.windows[k];
                        for (unsigned half = 0; half < 2; ++half) {
                            frame::Window w = full; w.w = half ? full.w - full.w / 2 : full.w / 2; w.origin_x = half ? full.w / 2 : 0; w.pixels.clear();
                            for (uint32_t y = 0; y < full.h; ++y) for (uint32_t x = 0; x < w.w; ++x) w.pixels.push_back(full.pixels[size_t(y) * full.w + w.origin_x + x]);
                            halves.push_back(std::move(w));
                        }
                        command.textures[k].window = uint32_t(2 * k); command.textures[k].window2 = uint32_t(2 * k + 1);
                    }
                    auto span = frame::SpanBuilder::build(0, 0, 128, 128, format, {pass_lowered.entry}, {command}, halves, background.data());
                    std::vector<uint8_t> expected; frame::render_reference(span, expected);
                    uint32_t status, covered, escapes; std::memcpy(&status, expected.data() + 4, 4); std::memcpy(&covered, expected.data() + 12, 4); std::memcpy(&escapes, expected.data() + 16, 4);
                    frame::require(status == 0 && covered && !escapes, "split-window fixture must cover the tile without escapes");
                    append_fixture(fixtures, std::string("split-windows-") + pair.sha, span);
                }
                // Fixture D: the quad over transparent textures with unit-range constants under
                // source-over blending: the captured DWM operating point where division by a zero
                // alpha and SM4 NaN semantics in min/max decide the result on the device.
                if (variant == 0 && !pass_lowered.texture_slots.empty()) {
                    auto clear = make_textures(pass_lowered, samplers, 24, true);
                    frame::Command transparent = command; transparent.blend_mode = 1; transparent.constants = flatten(pass, shader::Constants{}, ps, random_constants(ps, true));
                    for (size_t k = 0; k < pass_lowered.texture_slots.size(); ++k) { transparent.textures[k].window = uint32_t(k); transparent.textures[k].window2 = frame::TextureBinding{}.window2; }   // one window per texture again (fixture C split them)
                    std::vector<frame::Window> clear_windows(clear.windows, clear.windows + pass_lowered.texture_slots.size());
                    auto span = frame::SpanBuilder::build(0, 0, 128, 128, format, {pass_lowered.entry}, {transparent}, clear_windows, background.data());
                    std::vector<uint8_t> expected; frame::render_reference(span, expected);
                    uint32_t status, covered; std::memcpy(&status, expected.data() + 4, 4); std::memcpy(&covered, expected.data() + 12, 4);
                    if (status != 0 || !covered) { std::printf("{\"shader\":\"%s\",\"transparent_fixture_status\":%u,\"covered\":%u}\n", pair.sha, status, covered); }
                    frame::require(status == 0 && covered, (std::string("transparent-texture fixture must cover the tile: ") + pair.sha + " status " + std::to_string(status)).c_str());
                    append_fixture(fixtures, std::string("transparent-textures-") + pair.sha, span);
                }
            }
        }
    }
    if (fixtures_path) {
        // Fills in both formats with partial masks, an invalid header and a window escape.
        for (unsigned format : {28u, 87u}) {
            frame::Command fill; fill.kind = 1; fill.clip = {3, 5, 100, 90}; fill.write_mask = format == 28 ? 15 : 5; fill.fill_color = frame::pack_clear_color({0.2f, 0.6f, 0.9f, 1.0f}, format);
            frame::Command fill2; fill2.kind = 1; fill2.clip = {0, 0, 128, 128}; fill2.write_mask = 8; fill2.fill_color = frame::pack_clear_color({0, 0, 0, 0.5f}, format);
            auto background = random_background(128 * 128);
            append_fixture(fixtures, format == 28 ? "fills-rgba" : "fills-bgra", frame::SpanBuilder::build(0, 0, 128, 128, format, {}, {fill, fill2}, {}, background.data()));
        }
        {
            auto background = random_background(64 * 48);
            auto bad = frame::SpanBuilder::build(100, 200, 64, 48, 28, {}, {}, {}, background.data());
            bad[4] = 2; // unsupported version
            append_fixture(fixtures, "invalid-version", bad);
        }
        write_fixtures(fixtures, fixtures_path);
        std::printf("{\"fixtures_written\":%u,\"path\":\"%s\"}\n", fixtures.count, fixtures_path);
    }
    std::printf("{\"eligible_programs\":%u,\"captured_programs\":%zu,\"failures\":%u,\"passed\":%s}\n", eligible, std::size(pairs), failures, failures ? "false" : "true");
    return failures ? 1 : 0;
}
