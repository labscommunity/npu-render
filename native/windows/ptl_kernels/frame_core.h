/* Frame batch rendering core shared by the SHAVE kernel and the host reference.
 *
 * Pure C, no library calls, no vector extensions, no fused multiply-add. The
 * same source renders one tile on the NPU and on the host so outputs compare
 * exactly. Every offset in the span is validated before any access.
 *
 * Span input layout (NR_FRAME_SPAN bytes, little-endian u32 fields, 4-aligned):
 *   0   magic 'NRF1'            4   version 1
 *   8   tile_x  12 tile_y       16  tile_w  20 tile_h   (tile_w*tile_h*4 <= 65536)
 *   24  target_format (28 RGBA8, 87 BGRA8)
 *   28  command_count (<= 64)   32  commands_offset
 *   36  programs_offset (u32 entry offsets[program_count])  40 program_count (<= 16)
 *   44  vertices_offset         48  vertices_bytes
 *   52  atlas_offset            56  atlas_bytes
 *   60  background_offset       64  constants_offset   68 constants_bytes   72 flags (0)
 * Command (NR_FRAME_COMMAND_BYTES):
 *   0   kind (0 draw, 1 fill)   4   program
 *   8   vertex_offset (bytes from vertices_offset)   12 vertex_count (multiple of 3)
 *   16  clip x0,y0,x1,y1 (tile-relative pixels)
 *   32  blend_mode  36 write_mask  40 cull (0 none,1 front,2 back)  44 front_ccw
 *   48  blend_constant f32[4]
 *   64  fill_color (packed, target byte order)   68 constants_offset (bytes from constants_offset;
 *       vertex-stage float4s then pixel-stage float4s, counts from the program)
 *   72  viewport x,y,w,h f32    88  depth min,max f32
 *   96  textures[4]: u32 (window index | sampler bits << 16); window 0xffff = none
 *   112 reserved
 * Program entry (NR_FRAME_PROGRAM_BYTES header, followed by data):
 *   0   vs_count   4 ps_count   8 vs_input_mask   12 vs_constant_count
 *   16  vs_layout_offset   20 ps_input_mask (varying registers)  24 position_output
 *   28  interpolation (4 bits per PS input register 0..7)  32 varying_count
 *   36  ps_constant_count  40 ps_layout_offset  44 instructions_offset
 *   48  immediates_offset  52 immediate_count  56 flags  60 reserved
 *   A layout is 16 u32: per constant buffer slot, first float4 index | count << 16.
 *   Offsets are relative to the program entry. Instructions: vertex program
 *   first, then the pixel program, NR_FRAME_INSTRUCTION_BYTES each.
 * Instruction: 0 opcode  4 dest  8 src0  12 src1  16 src2  20 extra  24,28 reserved
 *   dest: kind(0 temp,1 output) | index<<4 | mask<<12 | saturate<<16
 *   src:  kind(0 temp,1 input,2 constant,3 immediate) | index<<4 | lanes<<12 (2 bits each) | negate<<20 | abs<<21
 *         constants: index is the element, bits 22..25 the constant buffer slot
 *   extra (sample): texture index 0..3 | lanes of the sampled color << 12
 *   extra (other): per source k, 10 bits at k*10: relative<<0 | input<<1 | register<<2 | lane<<6
 *         (constant element += register lane value; out-of-range reads return zero)
 * Atlas: u32 window_count; windows[] (NR_FRAME_WINDOW_BYTES): data_offset (from
 *   atlas_offset), w, h, origin_x, origin_y, texture_w, texture_h, format; then pixels.
 * Vertex pool: per command, vertex_count records of popcount(vs_input_mask) float4.
 * Output span: 0 magic 'NRFO'  4 status  8 commands  12 covered  16 escapes
 *   20 reserved; NR_FRAME_OUTPUT_PIXELS tile pixels (tile_w*tile_h*4 bytes).
 */
#ifndef NR_FRAME_CORE_H
#define NR_FRAME_CORE_H
#ifdef __clang__
#pragma STDC FP_CONTRACT OFF
#define NR_INLINE static __attribute__((always_inline)) inline
#define NR_NOINLINE static __attribute__((noinline))
#else
#define NR_INLINE static __forceinline
#define NR_NOINLINE static __declspec(noinline)
#endif
#define NR_FRAME_SPAN 264192u
#define NR_FRAME_HEADER_BYTES 128u
#define NR_FRAME_COMMAND_BYTES 128u
#define NR_FRAME_PROGRAM_BYTES 64u
#define NR_FRAME_INSTRUCTION_BYTES 32u
#define NR_FRAME_WINDOW_BYTES 32u
#define NR_FRAME_OUTPUT_PIXELS 64u
#define NR_FRAME_MAGIC 0x4e524631u
#define NR_FRAME_OUTPUT_MAGIC 0x4e52464fu
#define NR_FRAME_MAX_COMMANDS 64u
#define NR_FRAME_MAX_PROGRAMS 16u
#define NR_FRAME_MAX_INSTRUCTIONS 256u
#define NR_FRAME_MAX_VARYINGS 8u
#define NR_FRAME_MAX_TEXTURES 4u
#define NR_FRAME_TILE_BYTES 65536u
#define NR_FRAME_NO_WINDOW 0xffffu
enum {
    NR_OP_MOV = 0, NR_OP_ADD, NR_OP_MUL, NR_OP_MAD, NR_OP_DIV, NR_OP_MIN, NR_OP_MAX, NR_OP_DP2, NR_OP_DP3, NR_OP_DP4,
    NR_OP_FRC, NR_OP_FLOOR, NR_OP_RSQ, NR_OP_MOVC, NR_OP_LT, NR_OP_GE, NR_OP_EQ, NR_OP_NE, NR_OP_AND, NR_OP_OR,
    NR_OP_IADD, NR_OP_ITOF, NR_OP_FTOI, NR_OP_IGE, NR_OP_IMAD, NR_OP_ISHL, NR_OP_ISHR, NR_OP_USHR, NR_OP_SAMPLE,
    NR_OP_COUNT
};
enum { NR_STATUS_OK = 0, NR_STATUS_HEADER = 1, NR_STATUS_COMMAND = 2, NR_STATUS_PROGRAM = 3, NR_STATUS_VERTEX = 4,
       NR_STATUS_ATLAS = 5, NR_STATUS_ARITHMETIC = 6 };
typedef unsigned nr_u32;
typedef int nr_i32;
typedef unsigned char nr_u8;
typedef float nr_f32;
typedef long long nr_i64;
NR_INLINE nr_u32 nr_ld(const nr_u8 *p) { return *(const nr_u32 *)p; }
NR_INLINE void nr_st(nr_u8 *p, nr_u32 v) { *(nr_u32 *)p = v; }
NR_INLINE nr_f32 nr_float(nr_u32 v) { union { nr_u32 u; nr_f32 f; } x; x.u = v; return x.f; }
NR_INLINE nr_u32 nr_bits(nr_f32 v) { union { nr_u32 u; nr_f32 f; } x; x.f = v; return x.u; }
NR_INLINE nr_i32 nr_finite(nr_f32 f) { return (nr_bits(f) & 0x7f800000u) != 0x7f800000u; }
NR_INLINE nr_i32 nr_isnan(nr_f32 f) { return (nr_bits(f) & 0x7fffffffu) > 0x7f800000u; }
NR_INLINE nr_i32 nr_subnormal(nr_f32 f) { nr_u32 m = nr_bits(f) & 0x7fffffffu; return m && m < 0x00800000u; }
NR_INLINE nr_f32 nr_flush(nr_f32 f) { return nr_subnormal(f) ? nr_float(nr_bits(f) & 0x80000000u) : f; }
NR_NOINLINE nr_f32 nr_floor(nr_f32 a) {
    nr_u32 bits = nr_bits(a), mag = bits & 0x7fffffffu;
    if (mag < 0x00800000u) return nr_float(bits & 0x80000000u);
    if (mag >= 0x4b000000u) return a;
    { nr_i32 i = (nr_i32)a; nr_f32 b = (nr_f32)i; return b > a ? b - 1.0f : b; }
}
NR_INLINE nr_f32 nr_fmin(nr_f32 a, nr_f32 b) { if (nr_isnan(a)) return b; if (nr_isnan(b)) return a; return a < b ? a : b; }
NR_INLINE nr_f32 nr_fmax(nr_f32 a, nr_f32 b) { if (nr_isnan(a)) return b; if (nr_isnan(b)) return a; return a < b ? b : a; }
NR_INLINE nr_f32 nr_clamp01(nr_f32 v) { return (nr_isnan(v) || nr_subnormal(v) || v < 0) ? 0.0f : (v > 1 ? 1.0f : v); }
NR_NOINLINE nr_u32 nr_quantize(nr_f32 v) {
    nr_u32 base; nr_f32 fraction;
    v = v < 0 ? 0.0f : (v > 255.0f ? 255.0f : v);
    base = (nr_u32)v; fraction = v - (nr_f32)base;
    return base + (fraction > 0.5f || (fraction == 0.5f && (base & 1u)));
}
NR_INLINE nr_i64 nr_edge(nr_i32 ax, nr_i32 ay, nr_i32 bx, nr_i32 by, nr_i32 px, nr_i32 py) {
    return (nr_i64)(bx - ax) * (py - ay) - (nr_i64)(by - ay) * (px - ax);
}
NR_INLINE nr_f32 nr_weight(nr_f32 ax, nr_f32 ay, nr_f32 bx, nr_f32 by, nr_f32 px, nr_f32 py) {
    nr_f32 left = (bx - ax) * (py - ay), right = (by - ay) * (px - ax); return left - right;
}
NR_NOINLINE nr_i32 nr_snap(nr_f32 screen) {
    nr_f32 scaled = screen * 256.0f, magnitude = scaled < 0 ? -scaled : scaled, fraction;
    nr_i32 base = (nr_i32)magnitude; nr_i32 increment;
    fraction = magnitude - (nr_f32)base;
    increment = fraction > 0.5f || (fraction == 0.5f && (base & 1));
    return scaled < 0 ? -(base + increment) : base + increment;
}
typedef struct {
    nr_u32 vs_count, ps_count, vs_inputs, vs_constants, ps_inputs, position_output;
    nr_u32 interpolation, varying_count, ps_constants, instructions_offset;
    nr_u32 immediates_offset, immediate_count, vs_layout, ps_layout;
    const nr_u8 *base;
} nr_program;
typedef struct {
    nr_u32 kind, program, vertex_offset, vertex_count;
    nr_i32 clip[4];
    nr_u32 blend_mode, write_mask, cull, front_ccw, fill_color, constants_offset;
    nr_f32 blend_constant[4], viewport[4], depth[2];
    nr_u32 textures[4];
} nr_command;
typedef struct { nr_u32 data_offset, w, h, origin_x, origin_y, texture_w, texture_h, format; } nr_window;
typedef struct {
    const nr_u8 *data; nr_u8 *output;
    nr_u32 tile_x, tile_y, tile_w, tile_h, format, command_count, commands_offset, programs_offset, program_count;
    nr_u32 vertices_offset, vertices_bytes, atlas_offset, atlas_bytes, background_offset, constants_offset, constants_bytes, window_count;
    nr_u32 covered, escapes;
} nr_span;
typedef struct { nr_u32 r[16][4], in[16][4], out[8][4]; const nr_u8 *vs_constants, *ps_constants; } nr_registers;
typedef struct { nr_i32 sx, sy; nr_f32 x, y, inverse_w; nr_u32 varying[NR_FRAME_MAX_VARYINGS][4]; } nr_projected;

NR_INLINE nr_i32 nr_region(const nr_span *s, nr_u32 offset, nr_u32 bytes) {
    (void)s;
    return offset <= NR_FRAME_SPAN && bytes <= NR_FRAME_SPAN - offset && !(offset & 3u);
}
NR_NOINLINE nr_i32 nr_program_load(const nr_span *s, nr_u32 index, nr_program *p) {
    nr_u32 entry, total, instructions, n, m;
    if (index >= s->program_count) return 0;
    entry = nr_ld(s->data + s->programs_offset + 4 + index * 4);
    if (!nr_region(s, entry, NR_FRAME_PROGRAM_BYTES)) return 0;
    p->base = s->data + entry;
    p->vs_count = nr_ld(p->base); p->ps_count = nr_ld(p->base + 4); p->vs_inputs = nr_ld(p->base + 8);
    p->vs_constants = nr_ld(p->base + 12);
    p->ps_inputs = nr_ld(p->base + 20); p->position_output = nr_ld(p->base + 24);
    p->interpolation = nr_ld(p->base + 28); p->varying_count = nr_ld(p->base + 32);
    p->ps_constants = nr_ld(p->base + 36);
    p->instructions_offset = nr_ld(p->base + 44); p->immediates_offset = nr_ld(p->base + 48);
    p->immediate_count = nr_ld(p->base + 52); p->vs_layout = nr_ld(p->base + 16); p->ps_layout = nr_ld(p->base + 40);
    if (nr_ld(p->base + 56) || nr_ld(p->base + 60)) return 0;
    instructions = p->vs_count + p->ps_count;
    if (!p->vs_count || !p->ps_count || instructions > NR_FRAME_MAX_INSTRUCTIONS || p->vs_inputs > 0xffffu ||
        p->ps_inputs > 0xffu || p->position_output >= 8 || p->varying_count > NR_FRAME_MAX_VARYINGS ||
        p->vs_constants > 256 || p->ps_constants > 256 || p->immediate_count > 64) return 0;
    for (n = 0, m = 0; n < 8; ++n) if (p->ps_inputs & (1u << n)) ++m;
    if (m != p->varying_count || (p->ps_inputs & (1u << p->position_output))) return 0;
    if (!nr_region(s, entry + p->instructions_offset, instructions * NR_FRAME_INSTRUCTION_BYTES) ||
        !nr_region(s, entry + p->immediates_offset, p->immediate_count * 16) ||
        !nr_region(s, entry + p->vs_layout, 64) || !nr_region(s, entry + p->ps_layout, 64)) return 0;
    for (n = 0; n < 2; ++n) {
        const nr_u8 *layout = p->base + (n ? p->ps_layout : p->vs_layout); nr_u32 total = n ? p->ps_constants : p->vs_constants, slot;
        for (slot = 0; slot < 16; ++slot) {
            nr_u32 word = nr_ld(layout + slot * 4), first = word & 0xffffu, count = word >> 16;
            if (slot >= 14 && word) return 0;
            if (count && (first > total || count > total - first)) return 0;
        }
    }
    total = instructions * NR_FRAME_INSTRUCTION_BYTES;
    for (n = 0; n < instructions; ++n) {
        const nr_u8 *i = p->base + p->instructions_offset + n * NR_FRAME_INSTRUCTION_BYTES;
        nr_u32 op = nr_ld(i), dest = nr_ld(i + 4), src, k;
        if (op >= NR_OP_COUNT || (dest & 15u) > 1 || ((dest >> 4) & 255u) >= ((dest & 15u) ? 8u : 16u) ||
            !((dest >> 12) & 15u) || (dest >> 17) || nr_ld(i + 24) || nr_ld(i + 28)) return 0;
        for (k = 0; k < 3; ++k) {
            nr_u32 kind, index;
            src = nr_ld(i + 8 + k * 4); kind = src & 15u; index = (src >> 4) & 255u;
            if (op == NR_OP_SAMPLE && k) { if (src) return 0; continue; }
            if (kind > 3) return 0;
            if (kind == 0 && index >= 16) return 0;
            if (kind == 1 && index >= 16) return 0;
            if (kind == 2) {
                nr_u32 slot = (src >> 22) & 15u, word;
                if (slot >= 14 || (src >> 26)) return 0;
                word = nr_ld(p->base + (n < p->vs_count ? p->vs_layout : p->ps_layout) + slot * 4);
                if (index >= (word >> 16)) return 0;
            } else if (src >> 22) return 0;
            if (kind == 3 && index >= p->immediate_count) return 0;
        }
        if (op == NR_OP_SAMPLE) {
            if (n < p->vs_count || (nr_ld(i + 20) & 15u) >= NR_FRAME_MAX_TEXTURES || (nr_ld(i + 20) >> 20)) return 0;
        } else {
            nr_u32 extra = nr_ld(i + 20);
            if (extra >> 30) return 0;
            for (k = 0; k < 3; ++k) {
                nr_u32 rel = (extra >> (k * 10)) & 0x3ffu;
                if (!rel) continue;
                if (!(rel & 1u) || (rel >> 8) || ((nr_ld(i + 8 + k * 4) & 15u) != 2)) return 0;
            }
        }
    }
    (void)total;
    return 1;
}
NR_NOINLINE void nr_operand(const nr_registers *regs, const nr_program *p, nr_u32 code, nr_u32 integer, nr_u32 relative, nr_u32 out[4]) {
    nr_u32 kind = code & 15u, index = (code >> 4) & 255u, lanes = (code >> 12) & 255u, c;
    const nr_u32 *raw;
    nr_u32 constant[4] = {0, 0, 0, 0};
    if (kind == 0) raw = regs->r[index];
    else if (kind == 1) raw = regs->in[index];
    else if (kind == 2) {
        nr_u32 vertex = (integer >> 8) & 1u, slot = (code >> 22) & 15u;
        nr_u32 word = nr_ld(p->base + (vertex ? p->vs_layout : p->ps_layout) + slot * 4), first = word & 0xffffu, count = word >> 16;
        if (relative & 1u) index += ((relative & 2u) ? regs->in : regs->r)[(relative >> 2) & 15u][(relative >> 6) & 3u];
        if (index < count) { const nr_u8 *at = (vertex ? regs->vs_constants : regs->ps_constants) + (first + index) * 16; for (c = 0; c < 4; ++c) constant[c] = nr_ld(at + c * 4); }
        raw = constant;
    } else {
        const nr_u8 *at = p->base + p->immediates_offset + index * 16;
        for (c = 0; c < 4; ++c) constant[c] = nr_ld(at + c * 4);
        raw = constant;
    }
    for (c = 0; c < 4; ++c) {
        nr_u32 v = raw[(lanes >> (c * 2)) & 3u];
        if (code & (1u << 21)) v &= 0x7fffffffu;
        if (code & (1u << 20)) v = (integer & 1u) ? 0u - v : v ^ 0x80000000u;
        out[c] = v;
    }
}
/* Executes one program stage. `stage` 0 vertex (constants from the vertex table), 1 pixel.
 * Sampling reads through the command's texture windows. Returns 0 on arithmetic faults. */
NR_NOINLINE nr_i32 nr_sample(const nr_span *s, const nr_command *cmd, nr_u32 texture, nr_u32 u_bits, nr_u32 v_bits, nr_f32 out[4], nr_u32 *escapes) {
    nr_u32 binding = cmd->textures[texture], index = binding & 0xffffu, point = (binding >> 16) & 1u;
    nr_window w; const nr_u8 *rec; nr_f32 u = nr_float(u_bits), v = nr_float(v_bits), x, y, tx, ty, half;
    nr_u32 ix, iy, jx, jy, tap, c;
    nr_f32 taps[4][4];
    if (index >= s->window_count) return 0;
    rec = s->data + s->atlas_offset + 4 + index * NR_FRAME_WINDOW_BYTES;
    w.data_offset = nr_ld(rec); w.w = nr_ld(rec + 4); w.h = nr_ld(rec + 8); w.origin_x = nr_ld(rec + 12); w.origin_y = nr_ld(rec + 16);
    w.texture_w = nr_ld(rec + 20); w.texture_h = nr_ld(rec + 24); w.format = nr_ld(rec + 28);
    if (!nr_finite(u) || !nr_finite(v)) return 0;
    half = point ? 0.0f : 0.5f;
    u = nr_clamp01(u) * (nr_f32)w.texture_w - half; v = nr_clamp01(v) * (nr_f32)w.texture_h - half;
    x = u < 0 ? 0.0f : (u > (nr_f32)(w.texture_w - 1) ? (nr_f32)(w.texture_w - 1) : u);
    y = v < 0 ? 0.0f : (v > (nr_f32)(w.texture_h - 1) ? (nr_f32)(w.texture_h - 1) : v);
    ix = (nr_u32)x; iy = (nr_u32)y;
    jx = !point && ix + 1 < w.texture_w ? ix + 1 : ix; jy = !point && iy + 1 < w.texture_h ? iy + 1 : iy;
    tx = point ? 0.0f : x - (nr_f32)ix; ty = point ? 0.0f : y - (nr_f32)iy;
    for (tap = 0; tap < (point ? 1u : 4u); ++tap) {
        nr_u32 px = (tap & 1) ? jx : ix, py = (tap & 2) ? jy : iy, rx, ry, packed;
        if (px < w.origin_x || px >= w.origin_x + w.w || py < w.origin_y || py >= w.origin_y + w.h) {
            ++*escapes;
            rx = px < w.origin_x ? 0 : (px >= w.origin_x + w.w ? w.w - 1 : px - w.origin_x);
            ry = py < w.origin_y ? 0 : (py >= w.origin_y + w.h ? w.h - 1 : py - w.origin_y);
        } else { rx = px - w.origin_x; ry = py - w.origin_y; }
        packed = nr_ld(s->data + s->atlas_offset + w.data_offset + (ry * w.w + rx) * 4);
        for (c = 0; c < 4; ++c) {
            nr_u32 source = (w.format == 87 && c < 3) ? 2 - c : c;
            taps[tap][c] = (nr_f32)((packed >> (source * 8)) & 255u) / 255.0f;
        }
    }
    for (c = 0; c < 4; ++c) {
        if (point) out[c] = taps[0][c];
        else {
            nr_f32 top = taps[0][c] * (1.0f - tx) + taps[1][c] * tx, bottom = taps[2][c] * (1.0f - tx) + taps[3][c] * tx;
            out[c] = top * (1.0f - ty) + bottom * ty;
        }
    }
    return 1;
}
NR_NOINLINE nr_i32 nr_execute(const nr_span *s, const nr_command *cmd, const nr_program *p, nr_registers *regs, nr_u32 stage, nr_u32 *escapes) {
    nr_u32 first = stage ? p->vs_count : 0, last = stage ? p->vs_count + p->ps_count : p->vs_count, n, c;
    nr_u32 integer_flag = stage ? 0u : 0x100u;
    for (n = first; n < last; ++n) {
        const nr_u8 *i = p->base + p->instructions_offset + n * NR_FRAME_INSTRUCTION_BYTES;
        nr_u32 op = nr_ld(i), dest = nr_ld(i + 4), mask = (dest >> 12) & 15u, saturate = (dest >> 16) & 1u;
        nr_u32 a[4], b[4], d[4], value[4], integer = (op == NR_OP_IADD || op == NR_OP_IMAD) ? 1u : 0u, extra = nr_ld(i + 20);
        nr_u32 *target = (dest & 15u) ? regs->out[(dest >> 4) & 7u] : regs->r[(dest >> 4) & 15u];
        nr_operand(regs, p, nr_ld(i + 8), integer | integer_flag, op == NR_OP_SAMPLE ? 0u : extra & 0x3ffu, a);
        if (op != NR_OP_SAMPLE) { nr_operand(regs, p, nr_ld(i + 12), integer | integer_flag, (extra >> 10) & 0x3ffu, b); nr_operand(regs, p, nr_ld(i + 16), integer | integer_flag, (extra >> 20) & 0x3ffu, d); }
        if (op == NR_OP_SAMPLE) {
            nr_f32 sampled[4]; nr_u32 lanes = (extra >> 12) & 255u;
            if (!nr_sample(s, cmd, extra & 15u, a[0], a[1], sampled, escapes)) return 0;
            for (c = 0; c < 4; ++c) value[c] = nr_bits(sampled[(lanes >> (c * 2)) & 3u]);
        } else for (c = 0; c < 4; ++c) {
            nr_f32 fa = nr_float(a[c]), fb = nr_float(b[c]), v = 0.0f; nr_u32 bits = 0;
            if (!(mask & (1u << c))) continue;
            switch (op) {
            case NR_OP_MOV: bits = a[c]; break;
            case NR_OP_ADD: v = fa + fb; bits = nr_bits(v); break;
            case NR_OP_MUL: v = fa * fb; bits = nr_bits(v); break;
            case NR_OP_MAD: { volatile nr_f32 product = fa * fb; v = product + nr_float(d[c]); bits = nr_bits(v); break; }
            case NR_OP_DIV: v = fa / fb; bits = nr_bits(v); break;
            case NR_OP_MIN: bits = nr_bits(nr_fmin(fa, fb)); break;
            case NR_OP_MAX: bits = nr_bits(nr_fmax(fa, fb)); break;
            case NR_OP_DP2: case NR_OP_DP3: case NR_OP_DP4: {
                nr_u32 count = op == NR_OP_DP2 ? 2u : (op == NR_OP_DP3 ? 3u : 4u), k;
                v = nr_float(a[0]) * nr_float(b[0]);
                for (k = 1; k < count; ++k) { volatile nr_f32 product = nr_float(a[k]) * nr_float(b[k]); v += product; }
                bits = nr_bits(v); break;
            }
            case NR_OP_FRC: bits = nr_bits(nr_subnormal(fa) ? 0.0f : fa - nr_floor(fa)); break;
            case NR_OP_FLOOR: bits = nr_bits(nr_subnormal(fa) ? nr_float(a[c] & 0x80000000u) : nr_floor(fa)); break;
            case NR_OP_MOVC: bits = a[c] ? b[c] : d[c]; break;
            case NR_OP_LT: bits = fa < fb ? 0xffffffffu : 0u; break;
            case NR_OP_GE: bits = nr_flush(fa) >= nr_flush(fb) ? 0xffffffffu : 0u; break;
            case NR_OP_EQ: bits = nr_flush(fa) == nr_flush(fb) ? 0xffffffffu : 0u; break;
            case NR_OP_NE: bits = nr_flush(fa) != nr_flush(fb) ? 0xffffffffu : 0u; break;
            case NR_OP_AND: bits = a[c] & b[c]; break;
            case NR_OP_OR: bits = a[c] | b[c]; break;
            case NR_OP_IADD: bits = a[c] + b[c]; break;
            case NR_OP_ITOF: bits = nr_bits((nr_f32)(nr_i32)a[c]); break;
            case NR_OP_IGE: bits = (nr_i32)a[c] >= (nr_i32)b[c] ? 0xffffffffu : 0u; break;
            default: return 0;
            }
            value[c] = bits;
        }
        for (c = 0; c < 4; ++c) {
            if (!(mask & (1u << c))) continue;
            if (saturate) value[c] = nr_bits(nr_clamp01(nr_float(value[c])));
            target[c] = value[c];
        }
    }
    return 1;
}
NR_INLINE void nr_registers_reset(const nr_span *s, const nr_command *cmd, const nr_program *p, nr_registers *regs) {
    nr_u32 n, c;
    for (n = 0; n < 16; ++n) for (c = 0; c < 4; ++c) { regs->r[n][c] = 0; regs->in[n][c] = 0; }
    for (n = 0; n < 8; ++n) for (c = 0; c < 4; ++c) regs->out[n][c] = 0;
    regs->vs_constants = s->data + s->constants_offset + cmd->constants_offset;
    regs->ps_constants = regs->vs_constants + p->vs_constants * 16;
}
NR_NOINLINE nr_i32 nr_vertex(const nr_span *s, const nr_command *cmd, const nr_program *p, nr_u32 index, nr_projected *out) {
    nr_registers regs; nr_u32 n, c, slot = 0, k; const nr_u8 *v; nr_f32 clip[4], w, x, y;
    nr_u32 stride = 0;
    for (n = 0; n < 16; ++n) if (p->vs_inputs & (1u << n)) ++stride;
    v = s->data + s->vertices_offset + cmd->vertex_offset + index * stride * 16;
    nr_registers_reset(s, cmd, p, &regs);
    for (n = 0; n < 16; ++n) if (p->vs_inputs & (1u << n)) { for (c = 0; c < 4; ++c) regs.in[n][c] = nr_ld(v + slot * 16 + c * 4); ++slot; }
    if (!nr_execute(s, cmd, p, &regs, 0, 0)) return 0;
    for (c = 0; c < 4; ++c) { clip[c] = nr_float(regs.out[p->position_output][c]); if (!nr_finite(clip[c])) return 0; }
    w = clip[3];
    if (!(w >= 1.0f / 65536 && w <= 65536.0f)) return 0;
    x = (clip[0] / w + 1.0f) * (cmd->viewport[2] * 0.5f) + cmd->viewport[0];
    y = (1.0f - clip[1] / w) * (cmd->viewport[3] * 0.5f) + cmd->viewport[1];
    if (!nr_finite(x) || !nr_finite(y) || x < -1048576.0f || x > 1048576.0f || y < -1048576.0f || y > 1048576.0f) return 0;
    out->x = x; out->y = y; out->inverse_w = 1.0f / w; out->sx = nr_snap(x); out->sy = nr_snap(y);
    for (n = 0, k = 0; n < 8; ++n) if (p->ps_inputs & (1u << n)) { for (c = 0; c < 4; ++c) out->varying[k][c] = regs.out[n][c]; ++k; }
    return 1;
}
NR_NOINLINE void nr_blend_store(const nr_span *s, const nr_command *cmd, nr_u8 *pixel, const nr_f32 color[4]) {
    /* Porter-Duff factors in BlendMode order: source scale 0 zero, 1 255, 2 dstA, 3 255-dstA;
     * destination scale 0 zero, 1 one, 2 alpha, 3 1-alpha. Mode 13 is component coverage. */
    static const nr_u8 source_factor[13] = {1, 1, 0, 0, 3, 2, 3, 2, 3, 3, 1, 0, 0};
    static const nr_u8 destination_factor[13] = {0, 3, 2, 3, 1, 0, 0, 3, 2, 3, 1, 1, 0};
    nr_u32 background = nr_ld(pixel), packed = background, c, mode = cmd->blend_mode;
    nr_f32 alpha = nr_clamp01(color[3]), destination_alpha = (nr_f32)((background >> 24) & 255u), scale_source = 0, scale_destination = 0;
    if (mode < 13) {
        nr_u32 sf = source_factor[mode], df = destination_factor[mode];
        scale_source = sf == 0 ? 0.0f : (sf == 1 ? 255.0f : (sf == 2 ? destination_alpha : 255.0f - destination_alpha));
        scale_destination = df == 0 ? 0.0f : (df == 1 ? 1.0f : (df == 2 ? alpha : 1.0f - alpha));
    }
    for (c = 0; c < 4; ++c) {
        nr_u32 logical = (s->format == 87 && c < 3) ? 2 - c : c, quantized;
        nr_f32 fg, destination, value;
        if (!(cmd->write_mask & (1u << logical))) continue;
        fg = nr_clamp01(color[logical]); destination = (nr_f32)((background >> (c * 8)) & 255u);
        if (mode == 13) { nr_f32 constant = nr_clamp01(cmd->blend_constant[logical]); value = fg * (constant * 255.0f) + destination * (1.0f - fg); }
        else if (scale_source == 0.0f) value = destination * scale_destination;
        else if (scale_destination == 0.0f) value = fg * scale_source;
        else value = fg * scale_source + destination * scale_destination;
        quantized = nr_quantize(value);
        packed = (packed & ~(255u << (c * 8))) | (quantized << (c * 8));
    }
    nr_st(pixel, packed);
}
NR_NOINLINE nr_i32 nr_draw_triangle(nr_span *s, const nr_command *cmd, const nr_program *p, const nr_projected v[3]) {
    nr_i64 area = nr_edge(v[0].sx, v[0].sy, v[1].sx, v[1].sy, v[2].sx, v[2].sy);
    nr_i32 x0, y0, x1, y1, px, py, n;
    nr_i32 minx, miny, maxx, maxy;
    if (!area) return 1;
    if (cmd->cull && !(((cmd->cull == 2) != (cmd->front_ccw != 0)) ? area > 0 : area < 0)) return 1;
    minx = v[0].sx; maxx = v[0].sx; miny = v[0].sy; maxy = v[0].sy;
    for (n = 1; n < 3; ++n) {
        if (v[n].sx < minx) minx = v[n].sx; if (v[n].sx > maxx) maxx = v[n].sx;
        if (v[n].sy < miny) miny = v[n].sy; if (v[n].sy > maxy) maxy = v[n].sy;
    }
    /* Pixel centers at +128 in 1/256 units; pixel i covers [i*256, i*256+256). */
    x0 = (minx - 128) >> 8; if (x0 < 0) x0 = 0; y0 = (miny - 128) >> 8; if (y0 < 0) y0 = 0;
    x1 = ((maxx - 128) >> 8) + 1; y1 = ((maxy - 128) >> 8) + 1;
    x0 -= (nr_i32)s->tile_x; x1 -= (nr_i32)s->tile_x; y0 -= (nr_i32)s->tile_y; y1 -= (nr_i32)s->tile_y;
    if (x0 < cmd->clip[0]) x0 = cmd->clip[0]; if (y0 < cmd->clip[1]) y0 = cmd->clip[1];
    if (x1 > cmd->clip[2]) x1 = cmd->clip[2]; if (y1 > cmd->clip[3]) y1 = cmd->clip[3];
    for (py = y0; py < y1; ++py) for (px = x0; px < x1; ++px) {
        nr_i32 cx = ((nr_i32)s->tile_x + px) * 256 + 128, cy = ((nr_i32)s->tile_y + py) * 256 + 128, a, covered = 1;
        nr_f32 sx, sy, wgt[3], denominator, weights[3];
        nr_registers regs; nr_u32 c, reg, interp_index, color_bits[4]; nr_f32 color[4];
        for (a = 0; a < 3; ++a) { nr_i32 b = (a + 1) % 3, c2 = (a + 2) % 3; wgt[a] = nr_weight(v[b].x, v[b].y, v[c2].x, v[c2].y, sx, sy); }
        nr_registers_reset(s, cmd, p, &regs);
        for (reg = 0, interp_index = 0; reg < 8; ++reg) {
            nr_u32 mode;
            if (!(p->ps_inputs & (1u << reg))) continue;
            mode = (p->interpolation >> (reg * 4)) & 15u;
            if (mode == 1) { for (c = 0; c < 4; ++c) regs.in[reg][c] = v[0].varying[interp_index][c]; }
            else {
                for (a = 0; a < 3; ++a) weights[a] = mode == 2 ? wgt[a] * v[a].inverse_w : wgt[a];
                denominator = (weights[0] + weights[1]) + weights[2];
                denominator = denominator + (denominator == 0 ? 1.0f : 0.0f);
                for (c = 0; c < 4; ++c) {
                    nr_f32 sum = (weights[0] * nr_float(v[0].varying[interp_index][c]) + weights[1] * nr_float(v[1].varying[interp_index][c])) + weights[2] * nr_float(v[2].varying[interp_index][c]);
                    nr_f32 value = sum / denominator;
                    if (!nr_finite(value)) return 0;
                    regs.in[reg][c] = nr_bits(value);
                }
            }
            ++interp_index;
        }
        if (!nr_execute(s, cmd, p, &regs, 1, &s->escapes)) return 0;
        for (c = 0; c < 4; ++c) { color_bits[c] = regs.out[0][c]; color[c] = nr_float(color_bits[c]); if (!nr_finite(color[c])) return 0; }
        nr_blend_store(s, cmd, s->output + NR_FRAME_OUTPUT_PIXELS + ((nr_u32)py * s->tile_w + (nr_u32)px) * 4, color);
    }
    return 1;
}
NR_NOINLINE nr_i32 nr_command_load(const nr_span *s, nr_u32 index, nr_command *cmd) {
    const nr_u8 *c = s->data + s->commands_offset + index * NR_FRAME_COMMAND_BYTES; nr_u32 n;
    cmd->kind = nr_ld(c); cmd->program = nr_ld(c + 4); cmd->vertex_offset = nr_ld(c + 8); cmd->vertex_count = nr_ld(c + 12);
    for (n = 0; n < 4; ++n) cmd->clip[n] = (nr_i32)nr_ld(c + 16 + n * 4);
    cmd->blend_mode = nr_ld(c + 32); cmd->write_mask = nr_ld(c + 36); cmd->cull = nr_ld(c + 40); cmd->front_ccw = nr_ld(c + 44);
    for (n = 0; n < 4; ++n) cmd->blend_constant[n] = nr_float(nr_ld(c + 48 + n * 4));
    cmd->fill_color = nr_ld(c + 64); cmd->constants_offset = nr_ld(c + 68);
    for (n = 0; n < 4; ++n) cmd->viewport[n] = nr_float(nr_ld(c + 72 + n * 4));
    cmd->depth[0] = nr_float(nr_ld(c + 88)); cmd->depth[1] = nr_float(nr_ld(c + 92));
    for (n = 0; n < 4; ++n) cmd->textures[n] = nr_ld(c + 96 + n * 4);
    if (cmd->kind > 1 || cmd->blend_mode > 13 || !cmd->write_mask || cmd->write_mask > 15 || cmd->cull > 2 || cmd->front_ccw > 1) return 0;
    if (cmd->clip[0] < 0 || cmd->clip[1] < 0 || cmd->clip[0] > cmd->clip[2] || cmd->clip[1] > cmd->clip[3] ||
        cmd->clip[2] > (nr_i32)s->tile_w || cmd->clip[3] > (nr_i32)s->tile_h) return 0;
    for (n = 112; n < NR_FRAME_COMMAND_BYTES; n += 4) if (nr_ld(c + n)) return 0;
    for (n = 0; n < 4; ++n) {
        if (!nr_finite(cmd->blend_constant[n]) || !nr_finite(cmd->viewport[n])) return 0;
        if ((cmd->textures[n] >> 17) || ((cmd->textures[n] & 0xffffu) != NR_FRAME_NO_WINDOW && (cmd->textures[n] & 0xffffu) >= s->window_count)) return 0;
    }
    if (cmd->kind == 0) {
        nr_u32 stride = 0; nr_program p;
        if (!nr_program_load(s, cmd->program, &p)) return 0;
        for (n = 0; n < 16; ++n) if (p.vs_inputs & (1u << n)) ++stride;
        if (!cmd->vertex_count || cmd->vertex_count % 3 || cmd->vertex_count > 4095 || !stride ||
            cmd->vertex_offset > s->vertices_bytes || cmd->vertex_count * stride * 16 > s->vertices_bytes - cmd->vertex_offset ||
            (cmd->vertex_offset & 3u) || (cmd->constants_offset & 3u) || cmd->constants_offset > s->constants_bytes ||
            (p.vs_constants + p.ps_constants) * 16 > s->constants_bytes - cmd->constants_offset) return 0;
        if (!(cmd->viewport[2] > 0 && cmd->viewport[2] <= 8192 && cmd->viewport[3] > 0 && cmd->viewport[3] <= 8192 &&
              cmd->viewport[0] >= -8192 && cmd->viewport[0] <= 8192 && cmd->viewport[1] >= -8192 && cmd->viewport[1] <= 8192)) return 0;
    } else if (cmd->program || cmd->vertex_offset || cmd->vertex_count || cmd->constants_offset) return 0;
    return 1;
}
NR_NOINLINE nr_i32 nr_span_load(nr_span *s, const nr_u8 *data, nr_u8 *output) {
    nr_u32 n;
    s->data = data; s->output = output; s->covered = 0; s->escapes = 0;
    if (nr_ld(data) != NR_FRAME_MAGIC || nr_ld(data + 4) != 1) return 0;
    s->tile_x = nr_ld(data + 8); s->tile_y = nr_ld(data + 12); s->tile_w = nr_ld(data + 16); s->tile_h = nr_ld(data + 20);
    s->format = nr_ld(data + 24); s->command_count = nr_ld(data + 28); s->commands_offset = nr_ld(data + 32);
    s->programs_offset = nr_ld(data + 36); s->program_count = nr_ld(data + 40); s->vertices_offset = nr_ld(data + 44);
    s->vertices_bytes = nr_ld(data + 48); s->atlas_offset = nr_ld(data + 52); s->atlas_bytes = nr_ld(data + 56);
    s->background_offset = nr_ld(data + 60); s->constants_offset = nr_ld(data + 64); s->constants_bytes = nr_ld(data + 68);
    if (nr_ld(data + 72)) return 0;
    for (n = 76; n < NR_FRAME_HEADER_BYTES; n += 4) if (nr_ld(data + n)) return 0;
    if (!s->tile_w || !s->tile_h || s->tile_w > 128 || s->tile_h > 128 || s->tile_w * s->tile_h * 4 > NR_FRAME_TILE_BYTES ||
        s->tile_x > 8192 || s->tile_y > 8192 || (s->format != 28 && s->format != 87) ||
        s->command_count > NR_FRAME_MAX_COMMANDS || s->program_count > NR_FRAME_MAX_PROGRAMS) return 0;
    if (!nr_region(s, s->commands_offset, s->command_count * NR_FRAME_COMMAND_BYTES) ||
        !nr_region(s, s->programs_offset, 4 + s->program_count * 4) ||
        !nr_region(s, s->vertices_offset, s->vertices_bytes) ||
        !nr_region(s, s->atlas_offset, s->atlas_bytes) || s->atlas_bytes < 4 ||
        !nr_region(s, s->constants_offset, s->constants_bytes) ||
        !nr_region(s, s->background_offset, s->tile_w * s->tile_h * 4)) return 0;
    if (nr_ld(data + s->programs_offset) != s->program_count) return 0;
    s->window_count = nr_ld(data + s->atlas_offset);
    if (s->window_count > 256 || 4 + s->window_count * NR_FRAME_WINDOW_BYTES > s->atlas_bytes) return 0;
    for (n = 0; n < s->window_count; ++n) {
        const nr_u8 *rec = data + s->atlas_offset + 4 + n * NR_FRAME_WINDOW_BYTES;
        nr_u32 off = nr_ld(rec), w = nr_ld(rec + 4), h = nr_ld(rec + 8), ox = nr_ld(rec + 12), oy = nr_ld(rec + 16), tw = nr_ld(rec + 20), th = nr_ld(rec + 24), fmt = nr_ld(rec + 28);
        if (!w || !h || w > 8192 || h > 8192 || (off & 3u) || off > s->atlas_bytes || w * h > (s->atlas_bytes - off) / 4 ||
            !tw || !th || tw > 8192 || th > 8192 || ox > tw - w || oy > th - h || (fmt != 28 && fmt != 87)) return 0;
    }
    return 1;
}
/* Renders the span into `output`, writing the output header. Returns the status code. */
NR_INLINE nr_u32 nr_render_span(const nr_u8 *data, nr_u8 *output) {
    nr_span s; nr_u32 n, status = NR_STATUS_OK, executed = 0, bytes;
    nr_u8 *pixels = output + NR_FRAME_OUTPUT_PIXELS;
    for (n = 0; n < NR_FRAME_OUTPUT_PIXELS; n += 4) nr_st(output + n, 0);
    if (!nr_span_load(&s, data, output)) { nr_st(output, NR_FRAME_OUTPUT_MAGIC); nr_st(output + 4, NR_STATUS_HEADER); return NR_STATUS_HEADER; }
    bytes = s.tile_w * s.tile_h * 4;
    for (n = 0; n < bytes; n += 4) nr_st(pixels + n, nr_ld(data + s.background_offset + n));
    for (n = 0; n < s.command_count; ++n) {
        nr_command cmd;
        if (!nr_command_load(&s, n, &cmd)) { status = NR_STATUS_COMMAND; break; }
        if (cmd.kind == 1) {
            nr_i32 px, py;
            for (py = cmd.clip[1]; py < cmd.clip[3]; ++py) for (px = cmd.clip[0]; px < cmd.clip[2]; ++px) {
                nr_u8 *pixel = pixels + ((nr_u32)py * s.tile_w + (nr_u32)px) * 4;
                nr_u32 background = nr_ld(pixel), packed = background, c;
                for (c = 0; c < 4; ++c) {
                    nr_u32 logical = (s.format == 87 && c < 3) ? 2 - c : c;
                    if (cmd.write_mask & (1u << logical)) packed = (packed & ~(255u << (c * 8))) | (cmd.fill_color & (255u << (c * 8)));
                }
                nr_st(pixel, packed); ++s.covered;
            }
        } else {
            nr_program p; nr_u32 t; nr_i32 ok = 1;
            if (!nr_program_load(&s, cmd.program, &p)) { status = NR_STATUS_PROGRAM; break; }
            for (t = 0; t < cmd.vertex_count && ok; t += 3) {
                nr_projected v[3]; nr_u32 k;
                for (k = 0; k < 3; ++k) if (!nr_vertex(&s, &cmd, &p, t + k, &v[k])) { ok = 0; break; }
                if (!ok) { status = NR_STATUS_VERTEX; break; }
                if (!nr_draw_triangle(&s, &cmd, &p, v)) { ok = 0; status = NR_STATUS_ARITHMETIC; break; }
            }
            if (!ok) break;
        }
        ++executed;
    }
    nr_st(output, NR_FRAME_OUTPUT_MAGIC); nr_st(output + 4, status); nr_st(output + 8, executed);
    nr_st(output + 12, s.covered); nr_st(output + 16, s.escapes);
    return status;
}
#endif
