#pragma once
// Host side of the frame batch kernel: SM4 lowering, span assembly and the exact
// host reference. The reference shares frame_core.h with the SHAVE kernel; the
// host never renders for the desktop, it plans and verifies.
#include "shader_ir.hpp"
#include "shader_texture.hpp"
#include "output_merger.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
namespace nr::frame {
namespace core {
#include "../ptl_kernels/frame_core.h"
}
using core::NR_OP_MOV; using core::NR_OP_ADD; using core::NR_OP_MUL; using core::NR_OP_MAD; using core::NR_OP_DIV; using core::NR_OP_MIN; using core::NR_OP_MAX;
using core::NR_OP_DP2; using core::NR_OP_DP3; using core::NR_OP_DP4; using core::NR_OP_FRC; using core::NR_OP_FLOOR; using core::NR_OP_MOVC; using core::NR_OP_LT;
using core::NR_OP_GE; using core::NR_OP_EQ; using core::NR_OP_NE; using core::NR_OP_AND; using core::NR_OP_IADD; using core::NR_OP_ITOF; using core::NR_OP_IGE; using core::NR_OP_SAMPLE;
constexpr uint32_t span_bytes = NR_FRAME_SPAN, tile_pixels = 128, output_pixels_offset = NR_FRAME_OUTPUT_PIXELS;
inline void require(bool value, const char *message = "frame kernel contract") { if (!value) throw std::invalid_argument(message); }
inline uint32_t bits(float value) { uint32_t result; std::memcpy(&result, &value, 4); return result; }

// A lowered vertex/pixel shader pair plus the texture slot mapping it needs.
struct LoweredProgram {
    std::vector<uint8_t> entry;              // program entry bytes (header + instructions + immediates)
    uint32_t vs_inputs = 0, vs_constants = 0, ps_constants = 0;
    std::vector<uint32_t> texture_slots;    // command texture index -> SM4 texture slot
    std::vector<uint32_t> sampler_slots;    // command texture index -> SM4 sampler slot
    std::array<uint32_t, 14> vs_constant_base{}, ps_constant_base{};
    std::string rejection;                   // empty when eligible
    bool eligible() const { return rejection.empty(); }
};
namespace detail {
inline uint32_t encode_lanes(const std::array<uint32_t, 4> &lanes) {
    uint32_t packed = 0; for (unsigned c = 0; c < 4; ++c) { require(lanes[c] < 4); packed |= lanes[c] << (c * 2); } return packed;
}
inline bool flatten_constants(const shader::Program &p, std::array<uint32_t, 14> &base, uint32_t &total) {
    total = 0;
    // Dynamically indexed declarations are fine; relative operands are rejected per instruction.
    for (unsigned i = 0; i < 14; ++i) { base[i] = total; total += p.constants[i]; }
    return total <= 256;
}
}
// Lower one shader pair. Rejections are explicit reasons; callers route those draws elsewhere.
inline LoweredProgram lower(const shader::Program &vs, const shader::Program &ps, const shader::TextureSamplers &samplers) {
    LoweredProgram out;
    auto reject = [&](const char *why) { out.rejection = why; return out; };
    if (vs.stage != shader::Stage::vertex || ps.stage != shader::Stage::pixel) return reject("stage");
    if (vs.position_output >= 8 || ps.outputs[0] != 15) return reject("outputs");
    for (unsigned i = 1; i < 16; ++i) if (ps.outputs[i]) return reject("multiple pixel outputs");
    if (vs.vertex_id_input < 16 || vs.instance_id_input < 16) return reject("system vertex inputs");
    for (unsigned i = 0; i < 16; ++i) if (!vs.literal_tables[i].empty() || !ps.literal_tables[i].empty()) return reject("indexable temporaries");
    uint32_t vs_total = 0, ps_total = 0;
    if (!detail::flatten_constants(vs, out.vs_constant_base, vs_total) || !detail::flatten_constants(ps, out.ps_constant_base, ps_total)) return reject("constant buffers");
    uint32_t ps_inputs = 0, interpolation = 0, varyings = 0;
    for (unsigned r = 0; r < 16; ++r) {
        if (!ps.inputs[r] || r == ps.position_input) continue;
        if (r >= 8) return reject("pixel input register");
        if ((vs.outputs[r] & ps.inputs[r]) != ps.inputs[r]) return reject("vertex output coverage");
        const auto mode = ps.interpolation[r];
        uint32_t lowered = mode == 1 ? 1u : (mode == 2 || mode == 3 || mode == 6) ? 2u : (mode == 4 || mode == 5 || mode == 7) ? 4u : 0u;
        if (!lowered) return reject("interpolation");
        ps_inputs |= 1u << r; interpolation |= lowered << (r * 4); ++varyings;
    }
    if (ps_inputs & (1u << vs.position_output)) return reject("position register reused");
    for (unsigned r = 0; r < 16; ++r) if (vs.inputs[r]) out.vs_inputs |= 1u << r;
    if (!out.vs_inputs) return reject("vertex inputs");
    std::vector<std::array<uint32_t, 4>> immediates;
    std::vector<std::array<uint32_t, 8>> words;
    auto immediate_index = [&](const std::array<uint32_t, 4> &literal) {
        for (size_t i = 0; i < immediates.size(); ++i) if (immediates[i] == literal) return uint32_t(i);
        require(immediates.size() < 64, "immediate table"); immediates.push_back(literal); return uint32_t(immediates.size() - 1);
    };
    auto operand = [&](const shader::Operand &o, const shader::Program &program, bool vertex, std::string &why, uint32_t &relative) -> std::optional<uint32_t> {
        uint32_t kind, index, slot_bits = 0;
        relative = 0;
        if (o.relative.enabled) {
            if (o.kind != shader::Register::constant) { why = "relative addressing of a non-constant"; return std::nullopt; }
            if (o.relative.kind != shader::Register::temporary && o.relative.kind != shader::Register::input) { why = "relative register"; return std::nullopt; }
            if (!vertex && o.relative.kind == shader::Register::input && o.relative.index == program.position_input) { why = "position input read"; return std::nullopt; }
            relative = 1u | ((o.relative.kind == shader::Register::input ? 1u : 0u) << 1) | ((o.relative.index & 15u) << 2) | ((o.relative.component & 3u) << 6);
        }
        if (o.kind == shader::Register::temporary) { kind = 0; index = o.index; }
        else if (o.kind == shader::Register::input) {
            if (!vertex && o.index == program.position_input) { why = "position input read"; return std::nullopt; }
            kind = 1; index = o.index;
        }
        else if (o.kind == shader::Register::immediate) { kind = 3; index = immediate_index(o.literal); }
        else if (o.kind == shader::Register::constant) {
            if (o.index >= 14 || o.element >= program.constants[o.index]) { why = "constant element"; return std::nullopt; }
            kind = 2; index = o.element; slot_bits = o.index << 22;
        }
        else { why = o.kind == shader::Register::indexable ? "indexable temporary" : "operand kind"; return std::nullopt; }
        if (index >= 256) { why = "operand index"; return std::nullopt; }
        const uint32_t negate = (o.modifier == shader::Modifier::negate || o.modifier == shader::Modifier::absolute_negate) ? 1u : 0u;
        const uint32_t absolute = (o.modifier == shader::Modifier::absolute || o.modifier == shader::Modifier::absolute_negate) ? 1u : 0u;
        return kind | (index << 4) | (detail::encode_lanes(o.lanes) << 12) | (negate << 20) | (absolute << 21) | slot_bits;
    };
    std::string detail_reason;
    auto lower_stage = [&](const shader::Program &program, bool vertex) -> const char * {
        using shader::Opcode;
        for (const auto &i : program.instructions) {
            detail_reason.clear();
            uint32_t op;
            switch (i.opcode) {
            case Opcode::move: op = NR_OP_MOV; break; case Opcode::add: op = NR_OP_ADD; break; case Opcode::multiply: op = NR_OP_MUL; break;
            case Opcode::mad: op = NR_OP_MAD; break; case Opcode::divide: op = NR_OP_DIV; break; case Opcode::minimum: op = NR_OP_MIN; break;
            case Opcode::maximum: op = NR_OP_MAX; break; case Opcode::dot2: op = NR_OP_DP2; break; case Opcode::dot3: op = NR_OP_DP3; break;
            case Opcode::dot4: op = NR_OP_DP4; break; case Opcode::fraction: op = NR_OP_FRC; break; case Opcode::floor: op = NR_OP_FLOOR; break;
            case Opcode::select: op = NR_OP_MOVC; break; case Opcode::less: op = NR_OP_LT; break; case Opcode::greater_equal: op = NR_OP_GE; break;
            case Opcode::equal: op = NR_OP_EQ; break; case Opcode::not_equal: op = NR_OP_NE; break; case Opcode::bitwise_and: op = NR_OP_AND; break;
            case Opcode::integer_add: op = NR_OP_IADD; break; case Opcode::integer_to_float: op = NR_OP_ITOF; break;
            case Opcode::integer_greater_equal: op = NR_OP_IGE; break; case Opcode::sample: op = NR_OP_SAMPLE; break;
            default: return "opcode";
            }
            if (vertex && op == NR_OP_SAMPLE) return "vertex sampling";
            std::array<uint32_t, 8> w{}; w[0] = op;
            const auto &d = i.destination;
            uint32_t dest_kind = d.kind == shader::Register::temporary ? 0u : d.kind == shader::Register::output ? 1u : 2u;
            if (dest_kind > 1 || (dest_kind == 1 && d.index >= 8) || (dest_kind == 0 && d.index >= 16) || !d.mask) return "destination";
            if (dest_kind == 1 && !vertex && d.index != 0) return "pixel output register";
            w[1] = dest_kind | (d.index << 4) | (d.mask << 12) | ((i.saturate ? 1u : 0u) << 16);
            std::string why;
            if (op == NR_OP_SAMPLE) {
                uint32_t relative = 0;
                const auto coordinate = operand(i.sources[0], program, vertex, why, relative); if (!coordinate || relative) { detail_reason = why; return "sample coordinate"; }
                const auto &texture = i.sources[1], &sampler = i.sources[2];
                if (texture.kind != shader::Register::texture || texture.index >= 16 || !program.textures[texture.index] || program.texture_uint[texture.index] ||
                    program.texture_dimensions[texture.index] != 3 || sampler.kind != shader::Register::sampler || !program.samplers[sampler.index]) return "sample resources";
                const auto &s = samplers[sampler.index];
                if (s.u != shader::TextureAddress::clamp || s.v != shader::TextureAddress::clamp) return "texture addressing";
                uint32_t slot_index = UINT32_MAX;
                for (size_t n = 0; n < out.texture_slots.size(); ++n) if (out.texture_slots[n] == texture.index) {
                    if (out.sampler_slots[n] != sampler.index) return "texture sampled with two samplers";
                    slot_index = uint32_t(n);
                }
                if (slot_index == UINT32_MAX) {
                    if (out.texture_slots.size() >= NR_FRAME_MAX_TEXTURES) return "texture count";
                    out.texture_slots.push_back(texture.index); out.sampler_slots.push_back(sampler.index); slot_index = uint32_t(out.texture_slots.size() - 1);
                }
                w[2] = *coordinate; w[5] = slot_index | (detail::encode_lanes(texture.lanes) << 12);
            } else {
                for (unsigned k = 0; k < i.source_count; ++k) {
                    uint32_t relative = 0;
                    const auto code = operand(i.sources[k], program, vertex, why, relative); if (!code) { detail_reason = why; return "operand"; }
                    w[2 + k] = *code; w[5] |= relative << (k * 10);
                }
            }
            words.push_back(w);
            if (words.size() > NR_FRAME_MAX_INSTRUCTIONS) return "instruction count";
        }
        return nullptr;
    };
    const auto vs_begin = words.size();
    if (const char *why = lower_stage(vs, true)) return reject((std::string("vertex ") + why + (detail_reason.empty() ? "" : ": " + detail_reason)).c_str());
    const uint32_t vs_count = uint32_t(words.size() - vs_begin);
    if (const char *why = lower_stage(ps, false)) return reject((std::string("pixel ") + why + (detail_reason.empty() ? "" : ": " + detail_reason)).c_str());
    const uint32_t ps_count = uint32_t(words.size() - vs_count);
    if (!vs_count || !ps_count) return reject("empty stage");
    // Vertex outputs beyond the position must only feed declared varyings.
    out.vs_constants = vs_total; out.ps_constants = ps_total;
    const uint32_t vs_layout = NR_FRAME_PROGRAM_BYTES, ps_layout = vs_layout + 64, instructions_offset = ps_layout + 64;
    const uint32_t immediates_offset = instructions_offset + uint32_t(words.size()) * NR_FRAME_INSTRUCTION_BYTES;
    out.entry.assign(immediates_offset + immediates.size() * 16, 0);
    auto put = [&](size_t at, uint32_t v) { std::memcpy(out.entry.data() + at, &v, 4); };
    put(0, vs_count); put(4, ps_count); put(8, out.vs_inputs); put(12, vs_total); put(16, vs_layout); put(20, ps_inputs); put(24, vs.position_output);
    put(28, interpolation); put(32, varyings); put(36, ps_total); put(40, ps_layout); put(44, instructions_offset); put(48, immediates_offset); put(52, uint32_t(immediates.size()));
    for (unsigned slot = 0; slot < 14; ++slot) {
        put(vs_layout + slot * 4, out.vs_constant_base[slot] | (vs.constants[slot] << 16));
        put(ps_layout + slot * 4, out.ps_constant_base[slot] | (ps.constants[slot] << 16));
    }
    for (size_t n = 0; n < words.size(); ++n) for (unsigned k = 0; k < 8; ++k) put(instructions_offset + n * NR_FRAME_INSTRUCTION_BYTES + k * 4, words[n][k]);
    for (size_t n = 0; n < immediates.size(); ++n) for (unsigned k = 0; k < 4; ++k) put(immediates_offset + n * 16 + k * 4, immediates[n][k]);
    return out;
}
// Texture window packed by the host for one tile: a sub-rectangle of a texture.
struct Window { uint32_t w, h, origin_x, origin_y, texture_w, texture_h, format; std::vector<uint32_t> pixels; };
struct TextureBinding { uint32_t window = NR_FRAME_NO_WINDOW; bool point = false; };
struct Command {
    uint32_t kind = 0, program = 0;
    std::vector<uint32_t> vertices;          // packed vertex pool words for this command
    uint32_t vertex_count = 0;
    std::array<int32_t, 4> clip{};
    uint32_t blend_mode = 0, write_mask = 15, cull = 0, front_ccw = 0, fill_color = 0;
    std::array<float, 4> blend_constant{1, 1, 1, 1}, viewport{0, 0, 0, 0};
    std::array<float, 2> depth{0, 1};
    std::array<TextureBinding, 4> textures{};
    std::vector<uint32_t> constants;         // vertex then pixel float4 words
};
// Assembles one span. Throws when the content does not fit the span budget.
class SpanBuilder {
    std::vector<uint8_t> data_;
    static void put(std::vector<uint8_t> &v, size_t at, uint32_t x) { std::memcpy(v.data() + at, &x, 4); }
public:
    static std::vector<uint8_t> build(uint32_t tile_x, uint32_t tile_y, uint32_t tile_w, uint32_t tile_h, uint32_t format,
                                      const std::vector<std::vector<uint8_t>> &programs, const std::vector<Command> &commands,
                                      const std::vector<Window> &windows, const uint32_t *background) {
        require(tile_w && tile_h && tile_w <= tile_pixels && tile_h <= tile_pixels && (format == 28 || format == 87) &&
                commands.size() <= NR_FRAME_MAX_COMMANDS && programs.size() <= NR_FRAME_MAX_PROGRAMS && windows.size() <= 256);
        std::vector<uint8_t> v(span_bytes, 0);
        size_t at = NR_FRAME_HEADER_BYTES;
        const uint32_t commands_offset = uint32_t(at); at += commands.size() * NR_FRAME_COMMAND_BYTES;
        const uint32_t programs_offset = uint32_t(at); at += 4 + programs.size() * 4;
        std::vector<uint32_t> entries;
        for (const auto &p : programs) { require(p.size() % 4 == 0 && p.size() >= NR_FRAME_PROGRAM_BYTES); entries.push_back(uint32_t(at)); at += p.size(); }
        const uint32_t vertices_offset = uint32_t(at);
        std::vector<uint32_t> vertex_offsets;
        for (const auto &c : commands) { vertex_offsets.push_back(uint32_t(at - vertices_offset)); at += c.vertices.size() * 4; }
        const uint32_t vertices_bytes = uint32_t(at - vertices_offset);
        const uint32_t constants_offset = uint32_t(at);
        std::vector<uint32_t> constant_offsets;
        for (const auto &c : commands) { constant_offsets.push_back(uint32_t(at - constants_offset)); at += c.constants.size() * 4; }
        const uint32_t constants_bytes = uint32_t(at - constants_offset);
        const uint32_t atlas_offset = uint32_t(at); at += 4 + windows.size() * NR_FRAME_WINDOW_BYTES;
        std::vector<uint32_t> window_data;
        for (const auto &w : windows) { require(w.pixels.size() == size_t(w.w) * w.h); window_data.push_back(uint32_t(at - atlas_offset)); at += w.pixels.size() * 4; }
        const uint32_t atlas_bytes = uint32_t(at - atlas_offset);
        const uint32_t background_offset = uint32_t(at); at += size_t(tile_w) * tile_h * 4;
        require(at <= span_bytes, "span budget exceeded");
        put(v, 0, NR_FRAME_MAGIC); put(v, 4, 1); put(v, 8, tile_x); put(v, 12, tile_y); put(v, 16, tile_w); put(v, 20, tile_h); put(v, 24, format);
        put(v, 28, uint32_t(commands.size())); put(v, 32, commands_offset); put(v, 36, programs_offset); put(v, 40, uint32_t(programs.size()));
        put(v, 44, vertices_offset); put(v, 48, vertices_bytes); put(v, 52, atlas_offset); put(v, 56, atlas_bytes); put(v, 60, background_offset);
        put(v, 64, constants_offset); put(v, 68, constants_bytes);
        put(v, programs_offset, uint32_t(programs.size()));
        for (size_t n = 0; n < programs.size(); ++n) { put(v, programs_offset + 4 + n * 4, entries[n]); std::memcpy(v.data() + entries[n], programs[n].data(), programs[n].size()); }
        for (size_t n = 0; n < commands.size(); ++n) {
            const auto &c = commands[n]; uint8_t *rec = v.data() + commands_offset + n * NR_FRAME_COMMAND_BYTES;
            auto field = [&](size_t off, uint32_t x) { std::memcpy(rec + off, &x, 4); };
            field(0, c.kind); field(4, c.program); field(8, vertex_offsets[n]); field(12, c.vertex_count);
            for (unsigned k = 0; k < 4; ++k) field(16 + k * 4, uint32_t(c.clip[k]));
            field(32, c.blend_mode); field(36, c.write_mask); field(40, c.cull); field(44, c.front_ccw);
            for (unsigned k = 0; k < 4; ++k) field(48 + k * 4, bits(c.blend_constant[k]));
            field(64, c.fill_color); field(68, constant_offsets[n]);
            for (unsigned k = 0; k < 4; ++k) field(72 + k * 4, bits(c.viewport[k]));
            field(88, bits(c.depth[0])); field(92, bits(c.depth[1]));
            for (unsigned k = 0; k < 4; ++k) field(96 + k * 4, c.textures[k].window | ((c.textures[k].point ? 1u : 0u) << 16));
            if (!c.vertices.empty()) std::memcpy(v.data() + vertices_offset + vertex_offsets[n], c.vertices.data(), c.vertices.size() * 4);
            if (!c.constants.empty()) std::memcpy(v.data() + constants_offset + constant_offsets[n], c.constants.data(), c.constants.size() * 4);
        }
        put(v, atlas_offset, uint32_t(windows.size()));
        for (size_t n = 0; n < windows.size(); ++n) {
            const auto &w = windows[n]; uint8_t *rec = v.data() + atlas_offset + 4 + n * NR_FRAME_WINDOW_BYTES;
            const uint32_t fields[8]{window_data[n], w.w, w.h, w.origin_x, w.origin_y, w.texture_w, w.texture_h, w.format};
            std::memcpy(rec, fields, 32);
            std::memcpy(v.data() + atlas_offset + window_data[n], w.pixels.data(), w.pixels.size() * 4);
        }
        std::memcpy(v.data() + background_offset, background, size_t(tile_w) * tile_h * 4);
        return v;
    }
};
// Exact host reference for one span; output has the same layout the kernel writes.
inline uint32_t render_reference(const std::vector<uint8_t> &input, std::vector<uint8_t> &output) {
    require(input.size() == span_bytes);
    output.assign(span_bytes, 0);
    return core::nr_render_span(input.data(), output.data());
}
inline uint32_t pack_clear_color(const std::array<float, 4> &color, uint32_t format) {
    uint32_t packed = 0;
    for (unsigned c = 0; c < 4; ++c) {
        const float clamped = color[c] < 0 ? 0.f : (color[c] > 1 ? 1.f : color[c]);
        const float scaled = clamped * 255.f; const uint32_t base = uint32_t(scaled); const float fraction = scaled - float(base);
        const uint32_t q = base + (fraction > .5f || (fraction == .5f && (base & 1u)));
        const unsigned byte = (format == 87 && c < 3) ? 2 - c : c;
        packed |= q << (byte * 8);
    }
    return packed;
}
}
