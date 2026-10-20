#pragma once
// Host side of the frame batch kernel: SM4 lowering, native stage emission,
// span assembly and the exact host reference. The reference shares
// frame_core.h (and the generated frame_programs.h) with the SHAVE kernel;
// the host never renders for the desktop, it plans and verifies.
#include "shader_ir.hpp"
#include "shader_texture.hpp"
#include "output_merger.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
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
using core::NR_OP_GE; using core::NR_OP_EQ; using core::NR_OP_NE; using core::NR_OP_AND; using core::NR_OP_OR; using core::NR_OP_IADD; using core::NR_OP_ITOF; using core::NR_OP_IGE; using core::NR_OP_SAMPLE;
constexpr uint32_t span_bytes = NR_FRAME_SPAN, tile_pixels = 128, output_pixels_offset = NR_FRAME_OUTPUT_PIXELS;
inline void require(bool value, const char *message = "frame kernel contract") { if (!value) throw std::invalid_argument(message); }
inline uint32_t bits(float value) { uint32_t result; std::memcpy(&result, &value, 4); return result; }
using Words = std::array<uint32_t, 8>;          // op, dest, src0, src1, src2, extra, 0, 0
using Immediate = std::array<uint32_t, 4>;

// A lowered vertex/pixel shader pair: validated instruction words per stage, the
// program entry for the span, and the texture slot mapping it needs. The entry's
// native index is assigned by whoever generated frame_programs.h (set_native).
struct LoweredProgram {
    std::vector<uint8_t> entry;              // NR_FRAME_PROGRAM_BYTES header + immediates
    std::vector<Words> vs_words, ps_words;
    std::vector<Immediate> vs_immediates, ps_immediates;
    uint32_t vs_inputs = 0, ps_inputs = 0, position_output = 0, interpolation = 0, varyings = 0, vs_constants = 0, ps_constants = 0;
    std::vector<uint32_t> texture_slots;    // command texture index -> SM4 texture slot
    std::vector<uint32_t> sampler_slots;    // command texture index -> SM4 sampler slot
    std::array<uint32_t, 14> vs_constant_base{}, ps_constant_base{}, vs_constant_count{}, ps_constant_count{};
    std::string rejection;                   // empty when eligible
    bool eligible() const { return rejection.empty(); }
    uint32_t native() const { uint32_t v; std::memcpy(&v, entry.data(), 4); return v; }
    void set_native(uint32_t index) { std::memcpy(entry.data(), &index, 4); }
};
namespace detail {
inline uint32_t encode_lanes(const std::array<uint32_t, 4> &lanes) {
    uint32_t packed = 0; for (unsigned c = 0; c < 4; ++c) { require(lanes[c] < 4); packed |= lanes[c] << (c * 2); } return packed;
}
inline bool flatten_constants(const shader::Program &p, std::array<uint32_t, 14> &base, std::array<uint32_t, 14> &count, uint32_t &total) {
    total = 0;
    // Dynamically indexed declarations are fine; relative operands are checked per instruction.
    for (unsigned i = 0; i < 14; ++i) { base[i] = total; count[i] = p.constants[i]; total += p.constants[i]; }
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
    if (!detail::flatten_constants(vs, out.vs_constant_base, out.vs_constant_count, vs_total) ||
        !detail::flatten_constants(ps, out.ps_constant_base, out.ps_constant_count, ps_total)) return reject("constant buffers");
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
    std::vector<Immediate> *immediates = nullptr;
    auto immediate_index = [&](const Immediate &literal) {
        for (size_t i = 0; i < immediates->size(); ++i) if ((*immediates)[i] == literal) return uint32_t(i);
        require(immediates->size() < NR_FRAME_MAX_IMMEDIATES, "immediate table"); immediates->push_back(literal); return uint32_t(immediates->size() - 1);
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
        if (index >= 16 && kind != 2) { why = "operand index"; return std::nullopt; }
        if (index >= 256) { why = "operand index"; return std::nullopt; }
        const uint32_t negate = (o.modifier == shader::Modifier::negate || o.modifier == shader::Modifier::absolute_negate) ? 1u : 0u;
        const uint32_t absolute = (o.modifier == shader::Modifier::absolute || o.modifier == shader::Modifier::absolute_negate) ? 1u : 0u;
        return kind | (index << 4) | (detail::encode_lanes(o.lanes) << 12) | (negate << 20) | (absolute << 21) | slot_bits;
    };
    std::string detail_reason;
    auto lower_stage = [&](const shader::Program &program, bool vertex, std::vector<Words> &words) -> const char * {
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
            Words w{}; w[0] = op;
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
            if (words.size() > 1024) return "instruction count";
        }
        return nullptr;
    };
    immediates = &out.vs_immediates;
    if (const char *why = lower_stage(vs, true, out.vs_words)) return reject((std::string("vertex ") + why + (detail_reason.empty() ? "" : ": " + detail_reason)).c_str());
    immediates = &out.ps_immediates;
    if (const char *why = lower_stage(ps, false, out.ps_words)) return reject((std::string("pixel ") + why + (detail_reason.empty() ? "" : ": " + detail_reason)).c_str());
    if (out.vs_words.empty() || out.ps_words.empty()) return reject("empty stage");
    out.vs_constants = vs_total; out.ps_constants = ps_total; out.ps_inputs = ps_inputs; out.position_output = vs.position_output;
    out.interpolation = interpolation; out.varyings = varyings;
    out.entry.assign(NR_FRAME_PROGRAM_BYTES + (out.vs_immediates.size() + out.ps_immediates.size()) * 16, 0);
    auto put = [&](size_t at, uint32_t v) { std::memcpy(out.entry.data() + at, &v, 4); };
    put(0, 0xffffffffu); put(4, out.vs_inputs); put(8, ps_inputs); put(12, vs.position_output); put(16, interpolation); put(20, varyings);
    put(24, vs_total); put(28, ps_total); put(32, uint32_t(out.texture_slots.size())); put(36, uint32_t(out.vs_immediates.size())); put(40, uint32_t(out.ps_immediates.size()));
    size_t at = NR_FRAME_PROGRAM_BYTES;
    for (const auto &table : {out.vs_immediates, out.ps_immediates}) for (const auto &imm : table) { for (unsigned k = 0; k < 4; ++k) put(at + k * 4, imm[k]); at += 16; }
    return out;
}

// --- Kernel blobs --------------------------------------------------------------
// The SHAVE code window holds a few programs at a time, so the inventory is
// split into blobs by pixel-stage instruction count. A program's blob and its
// index inside the blob follow from this rule on both the host and the kernel.
constexpr std::array<std::pair<uint32_t, uint32_t>, 7> blob_ranges{{{0, 1}, {2, 3}, {4, 12}, {13, 64}, {65, 115}, {116, 120}, {121, 100000}}};
inline uint32_t blob_of(uint32_t ps_instructions) {
    for (uint32_t k = 0; k < blob_ranges.size(); ++k) if (ps_instructions >= blob_ranges[k].first && ps_instructions <= blob_ranges[k].second) return k;
    return UINT32_MAX;
}
// --- Native code emission ----------------------------------------------------
// Each lowered stage becomes C over the nr_v4 API in frame_core.h. Vertex
// stages are functions over nr_shade; pixel stages are emitted twice: as a
// host-only stage function (checked against the SM4 reference) and inside a
// per-program row shader that interpolates, shades and blends one scanline.
namespace detail {
inline std::string hex(uint32_t v) { char b[16]; std::snprintf(b, sizeof b, "0x%08xu", v); return b; }
inline std::string dec(uint32_t v) { return std::to_string(v) + "u"; }
inline unsigned source_count(uint32_t op) {
    return op == NR_OP_SAMPLE || op == NR_OP_MOV || op == NR_OP_FRC || op == NR_OP_FLOOR || op == NR_OP_ITOF ? 1u : (op == NR_OP_MAD || op == NR_OP_MOVC) ? 3u : 2u;
}
// How the emitted instructions name registers and tables.
struct Names {
    std::function<std::string(uint32_t)> input, output;
    std::string constants, immediates, escapes, st;
};
inline std::array<bool, 16> used_temps(const std::vector<Words> &words) {
    std::array<bool, 16> used{};
    for (const auto &w : words) {
        const uint32_t op = w[0];
        if ((w[1] & 15u) == 0) used[(w[1] >> 4) & 15u] = true;
        for (unsigned k = 0; k < source_count(op); ++k) {
            const uint32_t src = w[2 + k], rel = op == NR_OP_SAMPLE ? 0u : (w[5] >> (k * 10)) & 0x3ffu;
            if ((src & 15u) == 0) used[(src >> 4) & 15u] = true;
            if ((rel & 1u) && !(rel & 2u)) used[(rel >> 2) & 15u] = true;
        }
    }
    return used;
}
inline std::string emit_instructions(const std::vector<Words> &words, const std::array<uint32_t, 14> &base, const std::array<uint32_t, 14> &count, const Names &names, const std::string &indent) {
    std::string s;
    for (const auto &w : words) {
        const uint32_t op = w[0], dest = w[1], mask = (dest >> 12) & 15u, saturate = (dest >> 16) & 1u;
        const std::string D = (dest & 15u) ? names.output((dest >> 4) & 255u) : "r" + std::to_string((dest >> 4) & 15u);
        auto operand = [&](unsigned k) {
            const uint32_t src = w[2 + k], kind = src & 15u, idx = (src >> 4) & 255u, lanes = (src >> 12) & 255u, neg = (src >> 20) & 1u, abs = (src >> 21) & 1u, slot = (src >> 22) & 15u;
            const uint32_t rel = op == NR_OP_SAMPLE ? 0u : (w[5] >> (k * 10)) & 0x3ffu;
            std::string e;
            if (kind == 0) e = "r" + std::to_string(idx);
            else if (kind == 1) e = names.input(idx);
            else if (kind == 3) e = "nr_v4_load(" + names.immediates + " + " + dec(idx * 16) + ")";
            else {
                require(slot < 14 && idx < count[slot], "constant element");
                if (rel & 1u) {
                    const std::string reg = (rel & 2u) ? names.input((rel >> 2) & 15u) : "r" + std::to_string((rel >> 2) & 15u);
                    e = "nr_v4_cb(" + names.constants + ", " + dec(base[slot]) + ", " + dec(count[slot]) + ", " + dec(idx) + " + nr_v4_bits(" + reg + ", " + dec((rel >> 6) & 3u) + "))";
                } else e = "nr_v4_load(" + names.constants + " + " + dec((base[slot] + idx) * 16) + ")";
            }
            if (lanes != 0xe4u) e = "NR_V4_SWZ(" + e + ", " + std::to_string(lanes & 3u) + ", " + std::to_string((lanes >> 2) & 3u) + ", " + std::to_string((lanes >> 4) & 3u) + ", " + std::to_string((lanes >> 6) & 3u) + ")";
            if (abs) e = "nr_v4_abs(" + e + ")";
            if (neg) e = (op == NR_OP_IADD ? "nr_v4_ineg(" : "nr_v4_neg(") + e + ")";
            return e;
        };
        const unsigned sources = source_count(op);
        const std::string in = indent + "    ";
        s += indent + "{\n" + in + "nr_v4 t;\n" + in + "nr_v4 a = " + operand(0) + ";\n";
        if (sources >= 2) s += in + "nr_v4 b = " + operand(1) + ";\n";
        if (sources >= 3) s += in + "nr_v4 e = " + operand(2) + ";\n";
        switch (op) {
        case NR_OP_MOV: s += in + "t = a;\n"; break;
        case NR_OP_ADD: s += in + "t = nr_v4_add(a, b);\n"; break;
        case NR_OP_MUL: s += in + "t = nr_v4_mul(a, b);\n"; break;
        case NR_OP_DIV: s += in + "t = nr_v4_div(a, b);\n"; break;
        case NR_OP_MAD: s += in + "t = nr_v4_add(nr_v4_mul(a, b), e);\n"; break;
        case NR_OP_AND: s += in + "t = nr_v4_and(a, b);\n"; break;
        case NR_OP_OR: s += in + "t = nr_v4_or(a, b);\n"; break;
        case NR_OP_IADD: s += in + "t = nr_v4_iadd(a, b);\n"; break;
        case NR_OP_DP2: s += in + "t = nr_v4_dp(a, b, 2u);\n"; break;
        case NR_OP_DP3: s += in + "t = nr_v4_dp(a, b, 3u);\n"; break;
        case NR_OP_DP4: s += in + "t = nr_v4_dp(a, b, 4u);\n"; break;
        case NR_OP_MIN: s += in + "t = nr_v4_min(a, b);\n"; break;
        case NR_OP_MAX: s += in + "t = nr_v4_max(a, b);\n"; break;
        case NR_OP_FRC: s += in + "t = nr_v4_frc(a);\n"; break;
        case NR_OP_FLOOR: s += in + "t = nr_v4_floor(a);\n"; break;
        case NR_OP_ITOF: s += in + "t = nr_v4_itof(a);\n"; break;
        case NR_OP_MOVC: s += in + "t = nr_v4_movc(a, b, e);\n"; break;
        case NR_OP_LT: s += in + "t = nr_v4_cmp_lt(a, b);\n"; break;
        case NR_OP_GE: s += in + "t = nr_v4_cmp_ge(a, b);\n"; break;
        case NR_OP_EQ: s += in + "t = nr_v4_cmp_eq(a, b);\n"; break;
        case NR_OP_NE: s += in + "t = nr_v4_cmp_ne(a, b);\n"; break;
        case NR_OP_IGE: s += in + "t = nr_v4_cmp_ige(a, b);\n"; break;
        case NR_OP_SAMPLE: {
            const uint32_t texture = w[5] & 15u, lanes = (w[5] >> 12) & 255u;
            s += in + "t = nr_sample(" + names.st + ", " + dec(texture) + ", nr_v4_bits(a, 0), nr_v4_bits(a, 1), " + names.escapes + ");\n";
            if (lanes != 0xe4u) s += in + "t = NR_V4_SWZ(t, " + std::to_string(lanes & 3u) + ", " + std::to_string((lanes >> 2) & 3u) + ", " + std::to_string((lanes >> 4) & 3u) + ", " + std::to_string((lanes >> 6) & 3u) + ");\n";
            break;
        }
        default: require(false, "emit opcode");
        }
        if (saturate) s += in + "t = nr_v4_sat(t);\n";
        if (mask == 15) s += in + D + " = t;\n";
        else for (unsigned c = 0; c < 4; ++c) if ((mask >> c) & 1u) s += in + "NR_V4_SET(" + D + ", " + std::to_string(c) + ", t);\n";
        s += indent + "}\n";
    }
    return s;
}
inline std::string emit_stage(const std::vector<Words> &words, const std::array<uint32_t, 14> &base, const std::array<uint32_t, 14> &count, const std::string &name) {
    Names names{[](uint32_t r) { return "sh->in[" + std::to_string(r) + "]"; }, [](uint32_t r) { return "sh->out[" + std::to_string(r) + "]"; }, "sh->constants", "sh->immediates", "sh->escapes", "st"};
    std::string s = "NR_NOINLINE nr_i32 " + name + "(const nr_command_state *st, nr_shade *sh) {\n    (void)st;\n";
    const auto used = used_temps(words);
    for (unsigned r = 0; r < 16; ++r) if (used[r]) s += "    nr_v4 r" + std::to_string(r) + " = nr_v4_zero();\n";
    s += emit_instructions(words, base, count, names, "    ");
    return s + "    return 1;\n}\n";
}
// A row shader: interpolates this program's varyings, runs its pixel stage and
// blends, for `count` pixels of one scanline. Varying modes are compile-time.
inline std::string emit_row(const LoweredProgram &p, const std::string &name) {
    Names names{[](uint32_t r) { return "i" + std::to_string(r); }, [](uint32_t) { return std::string("o0"); }, "row->constants", "row->immediates", "row->escapes", "st"};
    std::string s = "NR_NOINLINE void " + name + "(const nr_row *row) {\n";
    s += "    const nr_command_state *st = row->st; nr_u8 *pixel = row->pixels; nr_i32 px;\n";
    s += "    nr_f32 w0 = row->wgt[0], w1 = row->wgt[1], w2 = row->wgt[2], dw0 = row->dwgt[0], dw1 = row->dwgt[1], dw2 = row->dwgt[2];\n";
    bool linear = false, perspective = false;
    // Only varyings the pixel stage reads are interpolated.
    std::array<bool, 16> read{};
    for (const auto &w : p.ps_words) for (unsigned k = 0; k < detail::source_count(w[0]); ++k) {
        const uint32_t src = w[2 + k], rel = w[0] == NR_OP_SAMPLE ? 0u : (w[5] >> (k * 10)) & 0x3ffu;
        if ((src & 15u) == 1) read[(src >> 4) & 15u] = true;
        if ((rel & 3u) == 3u) read[(rel >> 2) & 15u] = true;
    }
    std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> varyings;   // register, ordinal, mode
    for (uint32_t r = 0, c = 0; r < 8; ++r) if (p.ps_inputs & (1u << r)) { const uint32_t mode = (p.interpolation >> (r * 4)) & 15u, ordinal = c++; if (!read[r]) continue; varyings.emplace_back(r, ordinal, mode); linear |= mode == 4; perspective |= mode == 2; }
    if (perspective) s += "    const nr_f32 iw0 = row->v[0].inverse_w, iw1 = row->v[1].inverse_w, iw2 = row->v[2].inverse_w;\n";
    for (const auto &[r, c, mode] : varyings) {
        const std::string R = std::to_string(r), C = std::to_string(c);
        if (mode == 1) s += "    const nr_v4 i" + R + " = row->v[0].varying[" + C + "];\n";
        else s += "    nr_v4 i" + R + " = nr_v4_zero();\n    const nr_v4 va" + R + " = row->v[0].varying[" + C + "], vb" + R + " = row->v[1].varying[" + C + "], vc" + R + " = row->v[2].varying[" + C + "];\n";
    }
    s += "    nr_v4 o0 = nr_v4_zero();\n";
    const auto used = used_temps(p.ps_words);
    for (unsigned r = 0; r < 16; ++r) if (used[r]) s += "    nr_v4 r" + std::to_string(r) + " = nr_v4_zero();\n";
    // The edge weights sum to a row constant, so the linear reciprocal is hoisted; the
    // perspective reciprocal is hoisted too when every vertex has w = 1 (every 2D draw).
    if (linear) s += "    const nr_f32 dl = (w0 + w1) + w2;\n    if (!(dl > 1e-20f || dl < -1e-20f)) return;\n    const nr_f32 rl = 1.0f / dl;\n";
    if (perspective) s += "    const nr_i32 affine = (iw0 == 1.0f) & (iw1 == 1.0f) & (iw2 == 1.0f);\n    const nr_f32 dpa = (w0 + w1) + w2;\n    const nr_f32 rpa = (dpa > 1e-20f || dpa < -1e-20f) ? 1.0f / dpa : 0.0f;\n";
    s += "    (void)st;\n    for (px = 0; px < row->count; ++px, pixel += 4, w0 += dw0, w1 += dw1, w2 += dw2) {\n";
    if (perspective) s += "        const nr_f32 p0 = w0 * iw0, p1 = w1 * iw1, p2 = w2 * iw2, dp = (p0 + p1) + p2;\n        if (!(dp > 1e-20f || dp < -1e-20f)) continue;\n        const nr_f32 rp = affine ? rpa : 1.0f / dp;\n";
    for (const auto &[r, c, mode] : varyings) {
        if (mode == 1) continue;
        const std::string R = std::to_string(r), W0 = mode == 2 ? "p0" : "w0", W1 = mode == 2 ? "p1" : "w1", W2 = mode == 2 ? "p2" : "w2", RC = mode == 2 ? "rp" : "rl";
        s += "        i" + R + " = nr_v4_mul(nr_v4_add(nr_v4_add(nr_v4_mul(nr_v4_splat(" + W0 + "), va" + R + "), nr_v4_mul(nr_v4_splat(" + W1 + "), vb" + R + ")), nr_v4_mul(nr_v4_splat(" + W2 + "), vc" + R + ")), nr_v4_splat(" + RC + "));\n";
    }
    s += "#ifndef NR_EXP_NO_PS\n" + emit_instructions(p.ps_words, p.ps_constant_base, p.ps_constant_count, names, "        ") + "#endif\n";
    s += "#ifndef NR_EXP_NO_BLEND\n        nr_blend_pixel(st, pixel, o0);\n#else\n        *row->escapes += nr_v4_bits(o0, 0) & 1u;\n#endif\n";
    s += "    }\n}\n";
    return s;
}
// Balanced if-tree over [lo, hi) so the SHAVE compiler never lowers a switch.
inline std::string emit_tree(const std::vector<std::string> &leaves, size_t lo, size_t hi, const std::string &indent) {
    if (hi - lo == 1) return indent + leaves[lo] + "\n";
    const size_t mid = (lo + hi) / 2;
    return indent + "if (native < " + dec(uint32_t(mid)) + ") {\n" + emit_tree(leaves, lo, mid, indent + "    ") + indent + "} else {\n" + emit_tree(leaves, mid, hi, indent + "    ") + indent + "}\n";
}
}
// Emits frame_programs.h for a list of programs (native index = position). `codes`
// identifies each program for consistency checks (nr_native_inventory).
inline std::string emit_native_header(const std::vector<const LoweredProgram *> &programs, const std::vector<uint32_t> &codes) {
    require(programs.size() == codes.size() && programs.size() <= 256, "native program list");
    std::string out = "/* Generated by frame_kernel_test --emit from captured_dwm_shaders.hpp. Do not edit. */\n";
    out += "#define NR_NATIVE_COUNT " + detail::dec(uint32_t(programs.size())) + "\n";
    std::map<std::string, std::string> stage_names, row_names; std::vector<std::pair<std::string, std::string>> pair_names; std::vector<std::string> rows;
    std::string host_only;
    for (const auto *p : programs) {
        std::array<std::string, 2> names;
        for (unsigned stage = 0; stage < 2; ++stage) {
            const auto &words = stage ? p->ps_words : p->vs_words; const auto &base = stage ? p->ps_constant_base : p->vs_constant_base; const auto &count = stage ? p->ps_constant_count : p->vs_constant_count;
            std::string key = stage ? "ps" : "vs";
            for (const auto &w : words) for (auto v : w) key += detail::hex(v);
            for (unsigned i = 0; i < 14; ++i) key += detail::hex(base[i]) + detail::hex(count[i]);
            auto found = stage_names.find(key);
            if (found == stage_names.end() && !stage) {
                // A vertex stage that only copies inputs to outputs shares the runtime's passthrough.
                bool passthrough = !words.empty();
                for (const auto &w : words) {
                    const uint32_t src = w[2];
                    if (w[0] != NR_OP_MOV || (w[1] & 15u) != 1 || ((w[1] >> 12) & 31u) != 15u || (src & 15u) != 1 || ((src >> 12) & 0x3ffu) != 0xe4u || ((w[1] >> 4) & 255u) != ((src >> 4) & 255u) || ((src >> 4) & 255u) >= 8 || w[5]) passthrough = false;
                }
                if (passthrough) found = stage_names.emplace(key, "nr_stage_passthrough").first;
            }
            if (found == stage_names.end()) {
                char name[64]; std::snprintf(name, sizeof name, "nr_stage_%s_%zu", stage ? "ps" : "vs", stage_names.size());
                found = stage_names.emplace(key, name).first;
                (stage ? host_only : out) += detail::emit_stage(words, base, count, name);
            }
            names[stage] = found->second;
            if (stage) {
                const std::string row_key = key + detail::hex(p->ps_inputs) + detail::hex(p->interpolation);
                auto row = row_names.find(row_key);
                if (row == row_names.end()) {
                    char name[64]; std::snprintf(name, sizeof name, "nr_row_%zu", row_names.size());
                    row = row_names.emplace(row_key, name).first;
                    out += detail::emit_row(*p, name);
                }
                rows.push_back(row->second);
            }
        }
        pair_names.emplace_back(names[0], names[1]);
    }
    // Programs are listed real pairs first, then their passthrough-vertex twins in
    // the same order, so the vertex dispatch folds the twins onto one stage and
    // the row and pixel dispatch onto the real pair's code.
    size_t real = 0;
    while (real < codes.size() && !(codes[real] & 0x10000u)) ++real;
    const bool twins = codes.size() == 2 * real;
    for (size_t j = 0; twins && j < real; ++j) require(codes[real + j] == (0x10000u | codes[j]) && pair_names[real + j].first == "nr_stage_passthrough" && rows[real + j] == rows[j], "passthrough twin order");
    require(twins || real == codes.size(), "passthrough twins");
    out += "NR_NOINLINE nr_i32 nr_native_check(const nr_program *p) {\n    switch (p->native) {\n";
    for (size_t n = 0; n < programs.size(); ++n) {
        core::nr_program p{};
        const auto *q = programs[n];
        p.vs_inputs = q->vs_inputs; p.ps_inputs = q->ps_inputs; p.position_output = q->position_output; p.varying_count = q->varyings; p.interpolation = q->interpolation;
        p.vs_constants = q->vs_constants; p.ps_constants = q->ps_constants; p.texture_count = uint32_t(q->texture_slots.size());
        p.vs_immediate_count = uint32_t(q->vs_immediates.size()); p.ps_immediate_count = uint32_t(q->ps_immediates.size());
        out += "    case " + detail::dec(uint32_t(n)) + ": return p->signature == " + detail::hex(core::nr_program_signature(&p)) + ";\n";
    }
    out += "    default: return 0;\n    }\n}\n";
    out += "NR_INLINE nr_u32 nr_native_inventory(nr_u32 native) {\n    switch (native) {\n";
    for (size_t n = 0; n < codes.size(); ++n) out += "    case " + detail::dec(uint32_t(n)) + ": return " + detail::hex(codes[n]) + ";\n";
    out += "    default: return 0xffffffffu;\n    }\n}\n";
    std::vector<std::string> vs_leaves, row_leaves, ps_leaves;
    for (size_t n = 0; n < real; ++n) { vs_leaves.push_back("return " + pair_names[n].first + "(st, sh);"); row_leaves.push_back(rows[n] + "(row); return;"); ps_leaves.push_back("return " + pair_names[n].second + "(st, sh);"); }
    const std::string fold = twins && real ? "    if (native >= " + detail::dec(uint32_t(real)) + ") native -= " + detail::dec(uint32_t(real)) + ";\n" : "";
    out += "NR_NOINLINE nr_i32 nr_native_vs(nr_u32 native, const nr_command_state *st, nr_shade *sh) {\n";
    if (twins && real) out += "    if (native >= " + detail::dec(uint32_t(real)) + ") return nr_stage_passthrough(st, sh);\n";
    out += real ? detail::emit_tree(vs_leaves, 0, real, "    ") : "    (void)native; (void)st; (void)sh; return 0;\n";
    out += "}\n";
    out += "NR_NOINLINE void nr_native_row(nr_u32 native, const nr_row *row) {\n" + fold;
    out += real ? detail::emit_tree(row_leaves, 0, real, "    ") : "    (void)native; (void)row;\n";
    out += "}\n";
    out += "#ifndef NR_SHAVE\n/* Host-only pixel stages, checked against the SM4 reference. */\n" + host_only;
    out += "NR_NOINLINE nr_i32 nr_native_ps(nr_u32 native, const nr_command_state *st, nr_shade *sh) {\n" + fold;
    out += real ? detail::emit_tree(ps_leaves, 0, real, "    ") : "    (void)native; (void)st; (void)sh; return 0;\n";
    out += "}\n#endif\n";
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
    static size_t align16(size_t at) { return (at + 15) & ~size_t(15); }
public:
    static std::vector<uint8_t> build(uint32_t tile_x, uint32_t tile_y, uint32_t tile_w, uint32_t tile_h, uint32_t format,
                                      const std::vector<std::vector<uint8_t>> &programs, const std::vector<Command> &commands,
                                      const std::vector<Window> &windows, const uint32_t *background) {
        require(tile_w && tile_h && tile_w <= tile_pixels && tile_h <= tile_pixels && (format == 28 || format == 87) &&
                commands.size() <= NR_FRAME_MAX_COMMANDS && programs.size() <= NR_FRAME_MAX_PROGRAMS && windows.size() <= 256);
        std::vector<uint8_t> v(span_bytes, 0);
        size_t at = NR_FRAME_HEADER_BYTES;
        const uint32_t commands_offset = uint32_t(at); at += commands.size() * NR_FRAME_COMMAND_BYTES;
        const uint32_t programs_offset = uint32_t(at); at = align16(at + 4 + programs.size() * 4);
        std::vector<uint32_t> entries;
        for (const auto &p : programs) { require(p.size() % 16 == 0 && p.size() >= NR_FRAME_PROGRAM_BYTES); entries.push_back(uint32_t(at)); at += p.size(); }
        const uint32_t vertices_offset = uint32_t(at);
        std::vector<uint32_t> vertex_offsets;
        for (const auto &c : commands) { require(c.vertices.size() % 4 == 0); vertex_offsets.push_back(uint32_t(at - vertices_offset)); at += c.vertices.size() * 4; }
        const uint32_t vertices_bytes = uint32_t(at - vertices_offset);
        const uint32_t constants_offset = uint32_t(at);
        std::vector<uint32_t> constant_offsets;
        for (const auto &c : commands) { require(c.constants.size() % 4 == 0); constant_offsets.push_back(uint32_t(at - constants_offset)); at += c.constants.size() * 4; }
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
