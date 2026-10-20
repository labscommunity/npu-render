// Frame kernel host checks: the shared core against the scalar shader reference
// on the captured DWM shader pairs, plus NPU fixture generation. CPU only.
#include "frame_kernel.hpp"
#include "shader_reference.hpp"
#include "blend_reference.hpp"
#include "captured_dwm_shaders.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
using namespace nr;
namespace {
struct Pair { const uint32_t *vs; size_t vs_words; const uint32_t *ps; size_t ps_words; const char *sha; };
#define NR_PAIR(n, sha) Pair{vertex##n, std::size(vertex##n), pixel##n, std::size(pixel##n), sha}
const Pair pairs[]{
    NR_PAIR(0, "d8e0b497"), NR_PAIR(1, "595296ba"), NR_PAIR(2, "0a7b1c69"), NR_PAIR(3, "c71638aa"), NR_PAIR(4, "39799859"), NR_PAIR(5, "e3ebf67c"),
    NR_PAIR(6, "b92ae2a7"), NR_PAIR(7, "4e5ed0f8"), NR_PAIR(8, "fbe68f57"), NR_PAIR(9, "b7f4e6d0"), NR_PAIR(10, "95b1c4fb"), NR_PAIR(11, "d056c6dc"),
    NR_PAIR(12, "d305ca5e"), NR_PAIR(13, "7c5bc4d8"), NR_PAIR(14, "e455f7a5"), NR_PAIR(15, "ec1e65f6"), NR_PAIR(16, "20e56de0"), NR_PAIR(17, "3e6229d4"),
    NR_PAIR(18, "2a28d3fe"), NR_PAIR(19, "691da1da"), NR_PAIR(20, "c58da199"), NR_PAIR(21, "0daddc23"), NR_PAIR(22, "5a764502"), NR_PAIR(23, "7823741a")};
std::mt19937_64 rng(2026);
float uniform(float lo, float hi) { return lo + (hi - lo) * float(std::uniform_real_distribution<double>(0, 1)(rng)); }
uint32_t fbits(float v) { uint32_t b; std::memcpy(&b, &v, 4); return b; }
float ffloat(uint32_t b) { float v; std::memcpy(&v, &b, 4); return v; }
struct Textures { frame::Window windows[4]; shader::TextureShapes shapes{}; shader::TexturePixels pixels; shader::TextureSamplers samplers{}; };
Textures make_textures(const frame::LoweredProgram &lowered, const shader::TextureSamplers &samplers, uint32_t size) {
    Textures t; t.samplers = samplers;
    for (size_t n = 0; n < lowered.texture_slots.size(); ++n) {
        auto &w = t.windows[n]; w.w = w.texture_w = size; w.h = w.texture_h = size; w.origin_x = w.origin_y = 0; w.format = n & 1 ? 87u : 28u;
        w.pixels.resize(size_t(size) * size);
        const auto slot = lowered.texture_slots[n];
        t.shapes[slot] = {size, size, 15}; t.pixels[slot].resize(w.pixels.size());
        for (size_t i = 0; i < w.pixels.size(); ++i) {
            w.pixels[i] = uint32_t(rng());
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
struct SpanContext { std::vector<uint8_t> input; frame::core::nr_span span{}; frame::core::nr_command command{}; frame::core::nr_program program{}; };
SpanContext context_for(const std::vector<uint8_t> &program_entry, const frame::Command &command, const std::vector<frame::Window> &windows, uint32_t format = 28) {
    SpanContext c; std::vector<uint32_t> background(128 * 128, 0xff000000u);
    c.input = frame::SpanBuilder::build(0, 0, 128, 128, format, {program_entry}, {command}, windows, background.data());
    static std::vector<uint8_t> scratch(frame::span_bytes);
    frame::require(frame::core::nr_span_load(&c.span, c.input.data(), scratch.data()), "span load");
    frame::require(frame::core::nr_command_load(&c.span, 0, &c.command), "command load");
    frame::require(frame::core::nr_program_load(&c.span, 0, &c.program), "program load");
    return c;
}
shader::Constants random_constants(const shader::Program &p) {
    shader::Constants constants;
    for (unsigned i = 0; i < 14; ++i) { constants[i].resize(p.constants[i]); for (auto &v : constants[i]) for (auto &lane : v) lane = fbits(uniform(-2, 2)); }
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
    for (size_t n = 0; n < lowered.texture_slots.size(); ++n) { c.textures[n].window = uint32_t(n); c.textures[n].point = samplers[lowered.sampler_slots[n]].filter == shader::TextureFilter::point; }
    (void)textures; return c;
}
struct Stats { unsigned vs_cases = 0, vs_exact = 0, ps_cases = 0, ps_exact = 0, ps_within_one = 0, ps_far = 0; };
Stats compare_stage(const Pair &pair, const shader::Program &vs, const shader::Program &ps, const shader::TextureSamplers &samplers, const frame::LoweredProgram &lowered) {
    Stats stats;
    for (unsigned trial = 0; trial < 64; ++trial) {
        auto textures = make_textures(lowered, samplers, 8);
        auto vc = random_constants(vs), pc = random_constants(ps);
        frame::Command command = base_command(lowered, textures, samplers);
        command.constants = flatten(vs, vc, ps, pc);
        shader::Registers vertex_inputs{};
        for (unsigned r = 0; r < 16; ++r) if (vs.inputs[r]) for (unsigned c = 0; c < 4; ++c) vertex_inputs[r][c] = fbits(uniform(-1.5f, 1.5f));
        command.vertex_count = 3;
        for (unsigned v = 0; v < 3; ++v) for (unsigned r = 0; r < 16; ++r) if (vs.inputs[r]) for (unsigned c = 0; c < 4; ++c) command.vertices.push_back(vertex_inputs[r][c]);
        std::vector<frame::Window> windows(textures.windows, textures.windows + lowered.texture_slots.size());
        auto ctx = context_for(lowered.entry, command, windows);
        frame::core::nr_registers regs; frame::core::nr_registers_reset(&ctx.span, &ctx.command, &ctx.program, &regs);
        for (unsigned r = 0; r < 16; ++r) for (unsigned c = 0; c < 4; ++c) regs.in[r][c] = vertex_inputs[r][c];
        uint32_t escapes = 0;
        if (frame::core::nr_execute(&ctx.span, &ctx.command, &ctx.program, &regs, 0, &escapes)) {
            const auto expected = shader::reference_vertex(vs, vertex_inputs, vc);
            bool exact = true;
            for (unsigned r = 0; r < 8; ++r) if (vs.outputs[r]) for (unsigned c = 0; c < 4; ++c) if ((vs.outputs[r] & (1u << c)) && expected[r][c] != regs.out[r][c]) exact = false;
            ++stats.vs_cases; stats.vs_exact += exact;
        }
        // Pixel stage with random varyings.
        shader::Registers pixel_inputs{};
        for (unsigned r = 0; r < 16; ++r) if (ps.inputs[r] && r != ps.position_input) for (unsigned c = 0; c < 4; ++c) pixel_inputs[r][c] = fbits(c < 2 ? uniform(0, 1) : uniform(0.25f, 2));
        frame::core::nr_registers_reset(&ctx.span, &ctx.command, &ctx.program, &regs);
        for (unsigned r = 0; r < 16; ++r) for (unsigned c = 0; c < 4; ++c) regs.in[r][c] = pixel_inputs[r][c];
        if (frame::core::nr_execute(&ctx.span, &ctx.command, &ctx.program, &regs, 1, &escapes)) {
            const auto expected = shader::reference_vertex(ps, pixel_inputs, pc, textures.shapes, textures.samplers, textures.pixels);
            bool exact = true; unsigned far = 0;
            for (unsigned c = 0; c < 4; ++c) {
                if (expected[0][c] == regs.out[0][c]) continue; exact = false;
                const auto a = std::lround(std::fmin(std::fmax(ffloat(expected[0][c]), 0.f), 1.f) * 255), b = std::lround(std::fmin(std::fmax(ffloat(regs.out[0][c]), 0.f), 1.f) * 255);
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
    for (uint32_t format : {28u, 87u}) for (uint32_t mode = 0; mode <= 13; ++mode) for (unsigned trial = 0; trial < 200; ++trial) {
        frame::core::nr_span span{}; span.format = format;
        frame::core::nr_command cmd{}; cmd.blend_mode = mode; cmd.write_mask = 1 + (trial % 15);
        for (unsigned c = 0; c < 4; ++c) cmd.blend_constant[c] = uniform(-0.2f, 1.2f);
        const uint32_t background = uint32_t(rng());
        float color[4]; for (auto &c : color) c = uniform(-0.2f, 1.2f);
        uint8_t pixel[4]; std::memcpy(pixel, &background, 4);
        frame::core::nr_blend_store(&span, &cmd, pixel, color);
        uint32_t actual; std::memcpy(&actual, pixel, 4);
        const float alpha = std::fmin(std::fmax(color[3], 0.f), 1.f);
        for (unsigned c = 0; c < 4; ++c) {
            const unsigned logical = (format == 87 && c < 3) ? 2 - c : c;
            uint32_t expected_byte = (background >> (c * 8)) & 255u;
            if (cmd.write_mask & (1u << logical)) {
                const float fg = std::fmin(std::fmax(color[logical], 0.f), 1.f), dst = float((background >> (c * 8)) & 255u), dst_alpha = float((background >> 24) & 255u);
                const float constant = std::fmin(std::fmax(cmd.blend_constant[logical], 0.f), 1.f);
                const auto terms = raster::reference_blend_terms<float>(raster::BlendMode(mode), fg, alpha, dst, dst_alpha, constant);
                float value = terms[0] + terms[1]; value = std::fmin(std::fmax(value, 0.f), 255.f);
                const uint32_t base = uint32_t(value); const float fraction = value - float(base);
                expected_byte = base + (fraction > .5f || (fraction == .5f && (base & 1u)));
            }
            if (((actual >> (c * 8)) & 255u) != expected_byte) { ++failures; if (failures < 5) std::printf("{\"blend_mismatch\":true,\"mode\":%u,\"format\":%u}\n", mode, format); }
        }
    }
    std::printf("{\"blend_modes_checked\":14,\"formats\":2,\"trials\":200,\"blend_failures\":%u}\n", failures);
    return failures;
}
int main(int argc, char **argv) {
    const char *fixtures_path = argc >= 3 && std::string(argv[1]) == "--fixtures" ? argv[2] : nullptr;
    FixtureFile fixtures; unsigned eligible = 0, failures = check_blend_modes();
    std::vector<std::pair<std::vector<uint8_t>, std::string>> entries;
    for (size_t n = 0; n < std::size(pairs); ++n) {
        const auto &pair = pairs[n];
        shader::Program vs, ps;
        try { vs = shader::parse_vertex(pair.vs, pair.vs_words); ps = shader::parse_pixel(pair.ps, pair.ps_words); }
        catch (const std::exception &e) { std::printf("{\"shader\":\"%s\",\"parse_failed\":\"%s\"}\n", pair.sha, e.what()); continue; }
        shader::TextureSamplers samplers{};
        for (unsigned s = 0; s < 16; ++s) samplers[s].filter = (n & 1) ? shader::TextureFilter::point : shader::TextureFilter::linear;
        auto lowered = frame::lower(vs, ps, samplers);
        if (!lowered.eligible()) { std::printf("{\"shader\":\"%s\",\"eligible\":false,\"reason\":\"%s\"}\n", pair.sha, lowered.rejection.c_str()); continue; }
        ++eligible;
        const auto stats = compare_stage(pair, vs, ps, samplers, lowered);
        const bool samples = !lowered.texture_slots.empty();
        const bool ok = stats.vs_exact == stats.vs_cases && stats.ps_far == 0 && (samples || stats.ps_exact == stats.ps_cases);
        if (!ok) ++failures;
        std::printf("{\"shader\":\"%s\",\"eligible\":true,\"vertex_exact\":%u,\"vertex_cases\":%u,\"pixel_exact\":%u,\"pixel_within_one_byte\":%u,\"pixel_far\":%u,\"pixel_cases\":%u,\"textures\":%zu,\"passed\":%s}\n",
            pair.sha, stats.vs_exact, stats.vs_cases, stats.ps_exact, stats.ps_within_one, stats.ps_far, stats.ps_cases, lowered.texture_slots.size(), ok ? "true" : "false");
        if (!fixtures_path) continue;
        // Fixture A: real vertex program, random constants and vertices.
        for (unsigned format : {28u, 87u}) {
            auto textures = make_textures(lowered, samplers, 16);
            frame::Command command = base_command(lowered, textures, samplers);
            command.constants = flatten(vs, random_constants(vs), ps, random_constants(ps));
            command.vertex_count = 6; command.blend_mode = uint32_t(n % 14); command.write_mask = 15 - uint32_t(n % 3);
            for (unsigned v = 0; v < 6; ++v) for (unsigned r = 0; r < 16; ++r) if (vs.inputs[r]) for (unsigned c = 0; c < 4; ++c) command.vertices.push_back(fbits(uniform(-1.5f, 1.5f)));
            std::vector<frame::Window> windows(textures.windows, textures.windows + lowered.texture_slots.size());
            auto background = random_background(128 * 128);
            append_fixture(fixtures, std::string("real-vs-") + pair.sha + (format == 28 ? "-rgba" : "-bgra"), frame::SpanBuilder::build(0, 0, 128, 128, format, {lowered.entry}, {command}, windows, background.data()));
        }
        // Fixture B: passthrough vertex program covering the tile with two triangles.
        uint32_t position_register = 16; auto pass = passthrough_vertex(ps, position_register);
        auto pass_lowered = frame::lower(pass, ps, samplers);
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
