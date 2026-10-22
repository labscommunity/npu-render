/* Frame batch rendering core shared by the SHAVE kernel and the host reference.
 *
 * Pure C, no library calls, no fused multiply-add. The same source renders one
 * tile on the NPU and on the host so outputs compare exactly. Shader programs
 * are compiled ahead of time: frame_programs.h (generated from the captured
 * DWM inventory by frame_kernel_test --emit) holds one native function per
 * stage, dispatched by the program's native index. Every offset in the span is
 * validated before any access.
 *
 * Span input layout (NR_FRAME_SPAN bytes, little-endian u32 fields, 4-aligned):
 *   0   magic 'NRF1'            4   version 1
 *   8   tile_x  12 tile_y       16  tile_w  20 tile_h   (tile_w*tile_h*4 <= 65536)
 *   24  target_format (28 RGBA8, 87 BGRA8)
 *   28  command_count (<= 64)   32  commands_offset
 *   36  programs_offset (u32 count, then entry offsets, padded to 16)  40 program_count (<= 16)
 *   44  vertices_offset (16-aligned)   48  vertices_bytes
 *   52  atlas_offset            56  atlas_bytes
 *   60  background_offset       64  constants_offset (16-aligned)   68 constants_bytes   72 flags (0)
 * Command (NR_FRAME_COMMAND_BYTES):
 *   0   kind (0 draw, 1 fill)   4   program
 *   8   vertex_offset (bytes from vertices_offset, 16-aligned)   12 vertex_count (multiple of 3)
 *   16  clip x0,y0,x1,y1 (tile-relative pixels)
 *   32  blend_mode  36 write_mask  40 cull (0 none,1 front,2 back)  44 front_ccw
 *   48  blend_constant f32[4]
 *   64  fill_color (packed, target byte order)   68 constants_offset (16-aligned bytes from
 *       constants_offset; vertex-stage float4s then pixel-stage float4s, counts from the program)
 *   72  viewport x,y,w,h f32    88  depth min,max f32
 *   96  textures[4]: u32 (window index | sampler bits << 16); window 0xffff = none
 *   112 textures2[4]: u32 second window index of the same texture (0xffff = none); a tap
 *       outside the first window is served from the second before it counts as an escape
 * Program entry (NR_FRAME_PROGRAM_BYTES header at a 16-aligned offset, then data):
 *   0   native index (frame_programs.h)   4 vs_input_mask   8 ps_input_mask (varying registers)
 *   12  position_output   16 interpolation (4 bits per PS input register 0..7)   20 varying_count
 *   24  vs_constant_count   28 ps_constant_count   32 texture_count
 *   36  vs_immediate_count   40 ps_immediate_count   44..63 reserved (0)
 *   64  vertex-stage immediates (float4 each), then pixel-stage immediates
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
/* MSVC hosts include this header in C++: inline linkage keeps unused helpers from warning. */
#define NR_INLINE inline __forceinline
#define NR_NOINLINE inline __declspec(noinline)
#endif
/* Code-size knobs for the SHAVE window: the vertex, triangle and sampling
 * routines are out of line by default. */
#ifndef NR_EXECUTE_ATTR
#define NR_EXECUTE_ATTR NR_NOINLINE
#endif
/* Identity of the compiled kernel (set per blob by tools/frame_blobs.py from the blob header's sha256);
 * written to every output row so the host can verify which kernel the device ran. */
#ifndef NR_KERNEL_IDENTITY
#define NR_KERNEL_IDENTITY 0u
#endif
#ifndef NR_SAMPLE_ATTR
#define NR_SAMPLE_ATTR NR_NOINLINE
#endif
#ifndef NR_HELPER_ATTR
#define NR_HELPER_ATTR NR_NOINLINE
#endif
#define NR_FRAME_SPAN 264192u
#define NR_FRAME_HEADER_BYTES 128u
#define NR_FRAME_COMMAND_BYTES 128u
#define NR_FRAME_PROGRAM_BYTES 64u
#define NR_FRAME_WINDOW_BYTES 32u
#define NR_FRAME_OUTPUT_PIXELS 64u
#define NR_FRAME_MAGIC 0x4e524631u
#define NR_FRAME_OUTPUT_MAGIC 0x4e52464fu
#define NR_FRAME_MAX_COMMANDS 64u
#define NR_FRAME_MAX_PROGRAMS 16u
#define NR_FRAME_MAX_VARYINGS 8u
#define NR_FRAME_MAX_TEXTURES 4u
#define NR_FRAME_MAX_IMMEDIATES 64u
#define NR_FRAME_TILE_BYTES 65536u
#define NR_FRAME_NO_WINDOW 0xffffu
/* Instruction encoding shared with the host lowering (frame_kernel.hpp). */
enum {
    NR_OP_MOV = 0, NR_OP_ADD, NR_OP_MUL, NR_OP_MAD, NR_OP_DIV, NR_OP_MIN, NR_OP_MAX, NR_OP_DP2, NR_OP_DP3, NR_OP_DP4,
    NR_OP_FRC, NR_OP_FLOOR, NR_OP_RSQ, NR_OP_MOVC, NR_OP_LT, NR_OP_GE, NR_OP_EQ, NR_OP_NE, NR_OP_AND, NR_OP_OR,
    NR_OP_IADD, NR_OP_ITOF, NR_OP_FTOI, NR_OP_IGE, NR_OP_IMAD, NR_OP_ISHL, NR_OP_ISHR, NR_OP_USHR, NR_OP_SAMPLE, NR_OP_DDX, NR_OP_DDY, NR_OP_LOOP, NR_OP_BREAKC, NR_OP_ENDLOOP,
    NR_OP_LD,                         /* SM4 ld: integer texel fetch at mip 0 (appended: earlier values stay stable) */
    NR_OP_COUNT
};
enum { NR_STATUS_OK = 0, NR_STATUS_HEADER = 1, NR_STATUS_COMMAND = 2, NR_STATUS_PROGRAM = 3, NR_STATUS_VERTEX = 4,
       NR_STATUS_ATLAS = 5, NR_STATUS_ARITHMETIC = 6 };
typedef unsigned nr_u32;
typedef int nr_i32;
typedef unsigned char nr_u8;
typedef float nr_f32;
typedef long long nr_i64;
typedef unsigned long long nr_u64;
NR_INLINE nr_u32 nr_ld(const nr_u8 *p) { return *(const nr_u32 *)p; }
NR_INLINE void nr_st(nr_u8 *p, nr_u32 v) { *(nr_u32 *)p = v; }
NR_INLINE nr_f32 nr_float(nr_u32 v) { union { nr_u32 u; nr_f32 f; } x; x.u = v; return x.f; }
NR_INLINE nr_u32 nr_bits(nr_f32 v) { union { nr_u32 u; nr_f32 f; } x; x.f = v; return x.u; }
NR_INLINE nr_i32 nr_finite(nr_f32 f) { return (nr_bits(f) & 0x7f800000u) != 0x7f800000u; }
NR_INLINE nr_i32 nr_isnan(nr_f32 f) { return (nr_bits(f) & 0x7fffffffu) > 0x7f800000u; }
NR_INLINE nr_i32 nr_subnormal(nr_f32 f) { nr_u32 m = nr_bits(f) & 0x7fffffffu; return m && m < 0x00800000u; }
NR_INLINE nr_f32 nr_flush(nr_f32 f) { return nr_subnormal(f) ? nr_float(nr_bits(f) & 0x80000000u) : f; }
NR_INLINE nr_f32 nr_floor(nr_f32 a) {
    nr_u32 bits = nr_bits(a), mag = bits & 0x7fffffffu;
    if (mag < 0x00800000u) return nr_float(bits & 0x80000000u);
    if (mag >= 0x4b000000u) return a;
    { nr_i32 i = (nr_i32)a; nr_f32 b = (nr_f32)i; return b > a ? b - 1.0f : b; }
}
NR_INLINE nr_f32 nr_fmin(nr_f32 a, nr_f32 b) { if (nr_isnan(a)) return b; if (nr_isnan(b)) return a; return a < b ? a : b; }
NR_INLINE nr_f32 nr_fmax(nr_f32 a, nr_f32 b) { if (nr_isnan(a)) return b; if (nr_isnan(b)) return a; return a < b ? b : a; }
NR_INLINE nr_f32 nr_clamp01(nr_f32 v) { return (nr_isnan(v) || nr_subnormal(v) || v < 0) ? 0.0f : (v > 1 ? 1.0f : v); }
/* SM4 frc and floor on one lane: subnormal inputs flush to a signed zero. */
NR_INLINE nr_f32 nr_frc_lane(nr_f32 a) { return nr_subnormal(a) ? 0.0f : a - nr_floor(a); }
NR_INLINE nr_f32 nr_floor_lane(nr_f32 a) { return nr_subnormal(a) ? nr_float(nr_bits(a) & 0x80000000u) : nr_floor(a); }
NR_INLINE nr_u32 nr_quantize(nr_f32 v) {
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
NR_INLINE nr_i32 nr_snap(nr_f32 screen) {
    nr_f32 scaled = screen * 256.0f, magnitude = scaled < 0 ? -scaled : scaled, fraction;
    nr_i32 base = (nr_i32)magnitude; nr_i32 increment;
    fraction = magnitude - (nr_f32)base;
    increment = fraction > 0.5f || (fraction == 0.5f && (base & 1));
    return scaled < 0 ? -(base + increment) : base + increment;
}
/* Doubles a 64-bit value k times without a multiply (the target has no 64-bit
 * division or multiplication runtime). */
NR_INLINE nr_i64 nr_shl64(nr_i64 v, nr_u32 k) { return (nr_i64)((nr_u64)v << k); }

/* Four-lane vectors. clang (host and SHAVE) uses ext_vector_type, where
 * compares, selects and conversions lower to single CMU/VAU operations; MSVC
 * uses structs with scalar loops. Generated shader code only uses this API. */
#ifdef __clang__
typedef float nr_v4 __attribute__((ext_vector_type(4)));
typedef unsigned nr_uv4 __attribute__((ext_vector_type(4)));
typedef int nr_iv4 __attribute__((ext_vector_type(4)));
#define NR_V4_LANE(a, i) ((a)[i])
#define NR_UV4_LANE(a, i) ((a)[i])
/* Swizzles are spelled as lane extraction, not __builtin_shufflevector: moviCompile's
 * shuffle lowering (CMU.CMB combines) returns wrong lanes inside the long generated
 * shaders (2026-10-21, blobs 7-9 of the frame kernel; every fixture passes with this
 * form at the same code size, and the assembler's CMB write-port hazard disappears). */
#define NR_V4_SWZ(a, x, y, z, w) nr_v4_make(NR_V4_LANE((a), (x)), NR_V4_LANE((a), (y)), NR_V4_LANE((a), (z)), NR_V4_LANE((a), (w)))
#define NR_V4_SET(d, i, s) ((d)[i] = (s)[i])
NR_INLINE nr_v4 nr_v4_make(nr_f32 x, nr_f32 y, nr_f32 z, nr_f32 w) { nr_v4 r; r.x = x; r.y = y; r.z = z; r.w = w; return r; }
NR_INLINE nr_uv4 nr_uv4_make(nr_u32 x, nr_u32 y, nr_u32 z, nr_u32 w) { nr_uv4 r; r.x = x; r.y = y; r.z = z; r.w = w; return r; }
NR_INLINE nr_uv4 nr_v4_asu(nr_v4 a) { return (nr_uv4)a; }
NR_INLINE nr_v4 nr_uv4_asf(nr_uv4 a) { return (nr_v4)a; }
NR_INLINE nr_v4 nr_v4_add(nr_v4 a, nr_v4 b) { return a + b; }
NR_INLINE nr_v4 nr_v4_sub(nr_v4 a, nr_v4 b) { return a - b; }
NR_INLINE nr_v4 nr_v4_mul(nr_v4 a, nr_v4 b) { return a * b; }
NR_INLINE nr_v4 nr_v4_div_raw(nr_v4 a, nr_v4 b) { return a / b; }
NR_INLINE nr_uv4 nr_uv4_and(nr_uv4 a, nr_uv4 b) { return a & b; }
NR_INLINE nr_uv4 nr_uv4_or(nr_uv4 a, nr_uv4 b) { return a | b; }
NR_INLINE nr_uv4 nr_uv4_xor(nr_uv4 a, nr_uv4 b) { return a ^ b; }
NR_INLINE nr_uv4 nr_uv4_not(nr_uv4 a) { return ~a; }
NR_INLINE nr_uv4 nr_uv4_add(nr_uv4 a, nr_uv4 b) { return a + b; }
NR_INLINE nr_uv4 nr_uv4_sub(nr_uv4 a, nr_uv4 b) { return a - b; }
NR_INLINE nr_uv4 nr_uv4_mul(nr_uv4 a, nr_uv4 b) { return a * b; }
NR_INLINE nr_uv4 nr_uv4_shl(nr_uv4 a, nr_uv4 s) { return a << s; }
NR_INLINE nr_uv4 nr_uv4_shr(nr_uv4 a, nr_uv4 s) { return a >> s; }
NR_INLINE nr_uv4 nr_uv4_gt(nr_uv4 a, nr_uv4 b) { return (nr_uv4)(a > b); }
NR_INLINE nr_uv4 nr_uv4_lt(nr_uv4 a, nr_uv4 b) { return (nr_uv4)(a < b); }
NR_INLINE nr_uv4 nr_uv4_eq(nr_uv4 a, nr_uv4 b) { return (nr_uv4)(a == b); }
NR_INLINE nr_uv4 nr_iv4_ge(nr_uv4 a, nr_uv4 b) { return (nr_uv4)((nr_iv4)a >= (nr_iv4)b); }
NR_INLINE nr_uv4 nr_v4_lt(nr_v4 a, nr_v4 b) { return (nr_uv4)(a < b); }
NR_INLINE nr_uv4 nr_v4_gt(nr_v4 a, nr_v4 b) { return (nr_uv4)(a > b); }
NR_INLINE nr_uv4 nr_v4_ge(nr_v4 a, nr_v4 b) { return (nr_uv4)(a >= b); }
NR_INLINE nr_uv4 nr_v4_eq(nr_v4 a, nr_v4 b) { return (nr_uv4)(a == b); }
NR_INLINE nr_uv4 nr_v4_ne(nr_v4 a, nr_v4 b) { return (nr_uv4)(a != b); }
NR_INLINE nr_uv4 nr_v4_to_u(nr_v4 a) { return __builtin_convertvector(a, nr_uv4); }
NR_INLINE nr_iv4 nr_v4_to_i(nr_v4 a) { return __builtin_convertvector(a, nr_iv4); }
NR_INLINE nr_uv4 nr_iv4_asu(nr_iv4 a) { return (nr_uv4)a; }
NR_INLINE nr_v4 nr_uv4_to_f(nr_uv4 a) { return __builtin_convertvector(a, nr_v4); }
NR_INLINE nr_v4 nr_iv4_to_f(nr_iv4 a) { return __builtin_convertvector(a, nr_v4); }
NR_INLINE nr_v4 nr_v4_load(const nr_u8 *p) { return *(const nr_v4 *)p; }
#else
typedef struct { nr_f32 v[4]; } nr_v4;
typedef struct { nr_u32 v[4]; } nr_uv4;
typedef struct { nr_i32 v[4]; } nr_iv4;
#define NR_V4_LANE(a, i) ((a).v[i])
#define NR_UV4_LANE(a, i) ((a).v[i])
NR_INLINE nr_v4 nr_v4_swz(nr_v4 a, nr_u32 x, nr_u32 y, nr_u32 z, nr_u32 w) { nr_v4 r; r.v[0] = a.v[x]; r.v[1] = a.v[y]; r.v[2] = a.v[z]; r.v[3] = a.v[w]; return r; }
#define NR_V4_SWZ(a, x, y, z, w) nr_v4_swz((a), (x), (y), (z), (w))
#define NR_V4_SET(d, i, s) ((d).v[i] = (s).v[i])
NR_INLINE nr_v4 nr_v4_make(nr_f32 x, nr_f32 y, nr_f32 z, nr_f32 w) { nr_v4 r; r.v[0] = x; r.v[1] = y; r.v[2] = z; r.v[3] = w; return r; }
NR_INLINE nr_uv4 nr_uv4_make(nr_u32 x, nr_u32 y, nr_u32 z, nr_u32 w) { nr_uv4 r; r.v[0] = x; r.v[1] = y; r.v[2] = z; r.v[3] = w; return r; }
NR_INLINE nr_uv4 nr_v4_asu(nr_v4 a) { return nr_uv4_make(nr_bits(a.v[0]), nr_bits(a.v[1]), nr_bits(a.v[2]), nr_bits(a.v[3])); }
NR_INLINE nr_v4 nr_uv4_asf(nr_uv4 a) { return nr_v4_make(nr_float(a.v[0]), nr_float(a.v[1]), nr_float(a.v[2]), nr_float(a.v[3])); }
#define NR_V4_OP(name, type, expr) NR_INLINE type name(type a, type b) { type r; nr_u32 i; for (i = 0; i < 4; ++i) r.v[i] = (expr); return r; }
NR_V4_OP(nr_v4_add, nr_v4, a.v[i] + b.v[i])
NR_V4_OP(nr_v4_sub, nr_v4, a.v[i] - b.v[i])
NR_V4_OP(nr_v4_mul, nr_v4, a.v[i] * b.v[i])
NR_V4_OP(nr_v4_div_raw, nr_v4, a.v[i] / b.v[i])
NR_V4_OP(nr_uv4_and, nr_uv4, a.v[i] & b.v[i])
NR_V4_OP(nr_uv4_or, nr_uv4, a.v[i] | b.v[i])
NR_V4_OP(nr_uv4_xor, nr_uv4, a.v[i] ^ b.v[i])
NR_V4_OP(nr_uv4_add, nr_uv4, a.v[i] + b.v[i])
NR_V4_OP(nr_uv4_sub, nr_uv4, a.v[i] - b.v[i])
NR_V4_OP(nr_uv4_mul, nr_uv4, a.v[i] * b.v[i])
NR_V4_OP(nr_uv4_shl, nr_uv4, a.v[i] << b.v[i])
NR_V4_OP(nr_uv4_shr, nr_uv4, a.v[i] >> b.v[i])
NR_V4_OP(nr_uv4_gt, nr_uv4, a.v[i] > b.v[i] ? 0xffffffffu : 0u)
NR_V4_OP(nr_uv4_lt, nr_uv4, a.v[i] < b.v[i] ? 0xffffffffu : 0u)
NR_V4_OP(nr_uv4_eq, nr_uv4, a.v[i] == b.v[i] ? 0xffffffffu : 0u)
NR_V4_OP(nr_iv4_ge, nr_uv4, (nr_i32)a.v[i] >= (nr_i32)b.v[i] ? 0xffffffffu : 0u)
#undef NR_V4_OP
NR_INLINE nr_uv4 nr_uv4_not(nr_uv4 a) { nr_uv4 r; nr_u32 i; for (i = 0; i < 4; ++i) r.v[i] = ~a.v[i]; return r; }
#define NR_V4_CMP(name, op) NR_INLINE nr_uv4 name(nr_v4 a, nr_v4 b) { nr_uv4 r; nr_u32 i; for (i = 0; i < 4; ++i) r.v[i] = (a.v[i] op b.v[i]) ? 0xffffffffu : 0u; return r; }
NR_V4_CMP(nr_v4_lt, <)
NR_V4_CMP(nr_v4_gt, >)
NR_V4_CMP(nr_v4_ge, >=)
NR_V4_CMP(nr_v4_eq, ==)
NR_V4_CMP(nr_v4_ne, !=)
#undef NR_V4_CMP
NR_INLINE nr_uv4 nr_v4_to_u(nr_v4 a) { nr_uv4 r; nr_u32 i; for (i = 0; i < 4; ++i) r.v[i] = (nr_u32)a.v[i]; return r; }
NR_INLINE nr_iv4 nr_v4_to_i(nr_v4 a) { nr_iv4 r; nr_u32 i; for (i = 0; i < 4; ++i) r.v[i] = (nr_i32)a.v[i]; return r; }
NR_INLINE nr_uv4 nr_iv4_asu(nr_iv4 a) { nr_uv4 r; nr_u32 i; for (i = 0; i < 4; ++i) r.v[i] = (nr_u32)a.v[i]; return r; }
NR_INLINE nr_v4 nr_uv4_to_f(nr_uv4 a) { nr_v4 r; nr_u32 i; for (i = 0; i < 4; ++i) r.v[i] = (nr_f32)a.v[i]; return r; }
NR_INLINE nr_v4 nr_iv4_to_f(nr_iv4 a) { nr_v4 r; nr_u32 i; for (i = 0; i < 4; ++i) r.v[i] = (nr_f32)a.v[i]; return r; }
NR_INLINE nr_v4 nr_v4_load(const nr_u8 *p) { return nr_uv4_asf(nr_uv4_make(nr_ld(p), nr_ld(p + 4), nr_ld(p + 8), nr_ld(p + 12))); }
#endif
NR_INLINE nr_v4 nr_v4_splat(nr_f32 x) { return nr_v4_make(x, x, x, x); }
NR_INLINE nr_uv4 nr_uv4_splat(nr_u32 x) { return nr_uv4_make(x, x, x, x); }
NR_INLINE nr_v4 nr_v4_zero(void) { return nr_v4_splat(0.0f); }
NR_INLINE nr_u32 nr_v4_bits(nr_v4 a, nr_u32 i) { return NR_UV4_LANE(nr_v4_asu(a), i); }
NR_INLINE nr_v4 nr_v4_from_bits(nr_u32 x, nr_u32 y, nr_u32 z, nr_u32 w) { return nr_uv4_asf(nr_uv4_make(x, y, z, w)); }
/* Lane select: m lanes are all ones (a) or all zeros (b). */
NR_INLINE nr_uv4 nr_uv4_sel(nr_uv4 m, nr_uv4 a, nr_uv4 b) { return nr_uv4_or(nr_uv4_and(a, m), nr_uv4_and(b, nr_uv4_not(m))); }
NR_INLINE nr_v4 nr_v4_sel(nr_uv4 m, nr_v4 a, nr_v4 b) { return nr_uv4_asf(nr_uv4_sel(m, nr_v4_asu(a), nr_v4_asu(b))); }
NR_INLINE nr_v4 nr_v4_and(nr_v4 a, nr_v4 b) { return nr_uv4_asf(nr_uv4_and(nr_v4_asu(a), nr_v4_asu(b))); }
NR_INLINE nr_v4 nr_v4_or(nr_v4 a, nr_v4 b) { return nr_uv4_asf(nr_uv4_or(nr_v4_asu(a), nr_v4_asu(b))); }
NR_INLINE nr_v4 nr_v4_iadd(nr_v4 a, nr_v4 b) { return nr_uv4_asf(nr_uv4_add(nr_v4_asu(a), nr_v4_asu(b))); }
NR_INLINE nr_v4 nr_v4_abs(nr_v4 a) { return nr_uv4_asf(nr_uv4_and(nr_v4_asu(a), nr_uv4_splat(0x7fffffffu))); }
NR_INLINE nr_v4 nr_v4_neg(nr_v4 a) { return nr_uv4_asf(nr_uv4_xor(nr_v4_asu(a), nr_uv4_splat(0x80000000u))); }
NR_INLINE nr_v4 nr_v4_ineg(nr_v4 a) { return nr_uv4_asf(nr_uv4_sub(nr_uv4_splat(0u), nr_v4_asu(a))); }
NR_INLINE nr_uv4 nr_v4_isnan(nr_v4 a) { return nr_uv4_gt(nr_uv4_and(nr_v4_asu(a), nr_uv4_splat(0x7fffffffu)), nr_uv4_splat(0x7f800000u)); }
/* IEEE/SM4 division whatever the target's divider does at the edges: x / 0 is an infinity
 * with the quotient's sign, 0 / 0 and a NaN operand give NaN. Out of line: one copy per blob. */
NR_HELPER_ATTR nr_v4 nr_v4_div(nr_v4 a, nr_v4 b) {
    nr_v4 q = nr_v4_div_raw(a, b);
    nr_uv4 mag_mask = nr_uv4_splat(0x7fffffffu), zero = nr_uv4_splat(0u);
    nr_uv4 b_zero = nr_uv4_eq(nr_uv4_and(nr_v4_asu(b), mag_mask), zero), a_zero = nr_uv4_eq(nr_uv4_and(nr_v4_asu(a), mag_mask), zero);
    nr_uv4 nan = nr_uv4_or(nr_uv4_or(nr_v4_isnan(a), nr_v4_isnan(b)), nr_uv4_and(b_zero, a_zero));
    nr_uv4 inf = nr_uv4_or(nr_uv4_and(nr_uv4_xor(nr_v4_asu(a), nr_v4_asu(b)), nr_uv4_splat(0x80000000u)), nr_uv4_splat(0x7f800000u));
    nr_uv4 bits = nr_uv4_sel(b_zero, inf, nr_v4_asu(q));
    return nr_uv4_asf(nr_uv4_sel(nan, nr_uv4_splat(0x7fc00000u), bits));
}
NR_INLINE nr_uv4 nr_v4_issubnormal(nr_v4 a) {
    nr_uv4 m = nr_uv4_and(nr_v4_asu(a), nr_uv4_splat(0x7fffffffu));
    return nr_uv4_and(nr_uv4_lt(m, nr_uv4_splat(0x00800000u)), nr_uv4_not(nr_uv4_eq(m, nr_uv4_splat(0u))));
}
/* Subnormals flush to a signed zero (SM4 compare and floor inputs). */
NR_INLINE nr_v4 nr_v4_flush(nr_v4 a) { return nr_v4_sel(nr_v4_issubnormal(a), nr_uv4_asf(nr_uv4_and(nr_v4_asu(a), nr_uv4_splat(0x80000000u))), a); }
/* Branch-free lane helpers with SM4 NaN and subnormal semantics. */
NR_HELPER_ATTR nr_v4 nr_v4_sat(nr_v4 a) {
    nr_uv4 bits = nr_v4_asu(a);
    nr_uv4 bad = nr_uv4_or(nr_uv4_gt(bits, nr_uv4_splat(0x7f800000u)), nr_v4_issubnormal(a));   /* NaN, negatives, -0, subnormals */
    bits = nr_uv4_sel(nr_uv4_gt(bits, nr_uv4_splat(0x3f800000u)), nr_uv4_splat(0x3f800000u), bits);
    return nr_uv4_asf(nr_uv4_and(bits, nr_uv4_not(bad)));
}
/* Blend input clamp: negatives, NaN and -0 map to 0; positive subnormals pass
 * through and blend and quantize exactly like zero. */
NR_INLINE nr_v4 nr_v4_clamp01_fast(nr_v4 a) {
    nr_uv4 bits = nr_v4_asu(a);
    nr_uv4 bad = nr_uv4_gt(bits, nr_uv4_splat(0x7f800000u));
    bits = nr_uv4_sel(nr_uv4_gt(bits, nr_uv4_splat(0x3f800000u)), nr_uv4_splat(0x3f800000u), bits);
    return nr_uv4_asf(nr_uv4_and(bits, nr_uv4_not(bad)));
}
/* SM4 min/max: a NaN operand yields the other operand (both NaN yields NaN). */
NR_HELPER_ATTR nr_v4 nr_v4_min(nr_v4 a, nr_v4 b) { return nr_v4_sel(nr_v4_isnan(a), b, nr_v4_sel(nr_v4_isnan(b), a, nr_v4_sel(nr_v4_lt(a, b), a, b))); }
NR_HELPER_ATTR nr_v4 nr_v4_max(nr_v4 a, nr_v4 b) { return nr_v4_sel(nr_v4_isnan(a), b, nr_v4_sel(nr_v4_isnan(b), a, nr_v4_sel(nr_v4_lt(a, b), b, a))); }
NR_HELPER_ATTR nr_v4 nr_v4_floor(nr_v4 a) {
    nr_uv4 mag = nr_uv4_and(nr_v4_asu(a), nr_uv4_splat(0x7fffffffu));
    nr_uv4 tiny = nr_uv4_lt(mag, nr_uv4_splat(0x00800000u)), big = nr_uv4_or(nr_uv4_gt(mag, nr_uv4_splat(0x4affffffu)), nr_uv4_eq(mag, nr_uv4_splat(0x4b000000u)));
    nr_v4 b = nr_iv4_to_f(nr_v4_to_i(a));
    b = nr_v4_sel(nr_v4_gt(b, a), nr_v4_sub(b, nr_v4_splat(1.0f)), b);
    b = nr_v4_sel(big, a, b);
    return nr_v4_sel(tiny, nr_uv4_asf(nr_uv4_and(nr_v4_asu(a), nr_uv4_splat(0x80000000u))), b);
}
NR_HELPER_ATTR nr_v4 nr_v4_frc(nr_v4 a) { return nr_v4_sel(nr_v4_issubnormal(a), nr_v4_zero(), nr_v4_sub(a, nr_v4_floor(a))); }
/* SM4 mirror addressing on a normalised coordinate: reflect at every integer so [0, 1] maps onto
 * itself and odd cells reverse; equivalent to mirroring the integer taps (the reference's method)
 * because the edge taps of the reduced coordinate coincide with the clamped ones. */
NR_HELPER_ATTR nr_f32 nr_mirror01(nr_f32 t) {   /* out of line: sampling sites stay small */
    nr_f32 m;
    if (!nr_finite(t)) return t;
    m = t - 2.0f * NR_V4_LANE(nr_v4_floor(nr_v4_splat(t * 0.5f)), 0);
    return m > 1.0f ? 2.0f - m : m;
}
#ifdef __clang__
NR_INLINE nr_v4 nr_v4_itof(nr_v4 a) { return nr_iv4_to_f((nr_iv4)a); }
#else
NR_INLINE nr_v4 nr_v4_itof(nr_v4 a) { nr_v4 r; nr_u32 i; for (i = 0; i < 4; ++i) r.v[i] = (nr_f32)(nr_i32)nr_bits(a.v[i]); return r; }
#endif
NR_HELPER_ATTR nr_v4 nr_v4_movc(nr_v4 a, nr_v4 b, nr_v4 e) { return nr_v4_sel(nr_uv4_not(nr_uv4_eq(nr_v4_asu(a), nr_uv4_splat(0u))), b, e); }
/* SM4 comparisons are IEEE ordered: a NaN operand makes lt/ge/eq false and ne true, whatever
 * the target's compare instruction does with unordered lanes. */
NR_INLINE nr_uv4 nr_v4_unordered(nr_v4 a, nr_v4 b) { return nr_uv4_or(nr_v4_isnan(a), nr_v4_isnan(b)); }
NR_HELPER_ATTR nr_v4 nr_v4_cmp_lt(nr_v4 a, nr_v4 b) { return nr_uv4_asf(nr_uv4_and(nr_v4_lt(a, b), nr_uv4_not(nr_v4_unordered(a, b)))); }
NR_HELPER_ATTR nr_v4 nr_v4_cmp_ge(nr_v4 a, nr_v4 b) { return nr_uv4_asf(nr_uv4_and(nr_v4_ge(nr_v4_flush(a), nr_v4_flush(b)), nr_uv4_not(nr_v4_unordered(a, b)))); }
NR_HELPER_ATTR nr_v4 nr_v4_cmp_eq(nr_v4 a, nr_v4 b) { return nr_uv4_asf(nr_uv4_and(nr_v4_eq(nr_v4_flush(a), nr_v4_flush(b)), nr_uv4_not(nr_v4_unordered(a, b)))); }
NR_HELPER_ATTR nr_v4 nr_v4_cmp_ne(nr_v4 a, nr_v4 b) { return nr_uv4_asf(nr_uv4_or(nr_v4_ne(nr_v4_flush(a), nr_v4_flush(b)), nr_v4_unordered(a, b))); }
NR_HELPER_ATTR nr_v4 nr_v4_cmp_ige(nr_v4 a, nr_v4 b) { return nr_uv4_asf(nr_iv4_ge(nr_v4_asu(a), nr_v4_asu(b))); }
/* SM4 integer arithmetic on bit-pattern lanes. Shift counts come from lane 0 of the count
 * operand (the reference's convention, low five bits); ftoi saturates and maps NaN to 0. */
NR_INLINE nr_v4 nr_v4_imad(nr_v4 a, nr_v4 b, nr_v4 e) { return nr_uv4_asf(nr_uv4_add(nr_uv4_mul(nr_v4_asu(a), nr_v4_asu(b)), nr_v4_asu(e))); }
NR_INLINE nr_v4 nr_v4_ishl(nr_v4 a, nr_v4 b) { return nr_uv4_asf(nr_uv4_shl(nr_v4_asu(a), nr_uv4_splat(nr_v4_bits(b, 0) & 31u))); }
NR_INLINE nr_v4 nr_v4_ushr(nr_v4 a, nr_v4 b) { return nr_uv4_asf(nr_uv4_shr(nr_v4_asu(a), nr_uv4_splat(nr_v4_bits(b, 0) & 31u))); }
#ifndef NR_FRAME_NO_LD   /* blobs without a fetching program leave the ld support out (code window) */
NR_HELPER_ATTR nr_v4 nr_v4_ishr(nr_v4 a, nr_v4 b) {
    nr_u32 s = nr_v4_bits(b, 0) & 31u; nr_uv4 u = nr_v4_asu(a);
    nr_uv4 fill = nr_uv4_and(nr_uv4_splat(s ? (0xffffffffu << (32u - s)) : 0u), nr_uv4_gt(u, nr_uv4_splat(0x7fffffffu)));   /* sign extension */
    return nr_uv4_asf(nr_uv4_or(nr_uv4_shr(u, nr_uv4_splat(s)), fill));
}
NR_HELPER_ATTR nr_v4 nr_v4_ftoi(nr_v4 a) {
    /* `above`/`below`, not `big`/`small`: Windows headers define `small` as a macro for host builds. */
    nr_uv4 unordered = nr_v4_isnan(a), above = nr_v4_ge(a, nr_v4_splat(2147483648.0f)), below = nr_v4_lt(a, nr_v4_splat(-2147483648.0f));
    nr_v4 clamped = nr_v4_sel(unordered, nr_v4_zero(), nr_v4_sel(above, nr_v4_splat(2147483520.0f), nr_v4_sel(below, nr_v4_splat(-2147483648.0f), a)));
    nr_uv4 r = nr_uv4_sel(above, nr_uv4_splat(0x7fffffffu), nr_iv4_asu(nr_v4_to_i(clamped)));
    return nr_uv4_asf(nr_uv4_sel(unordered, nr_uv4_splat(0u), r));
}
#endif
/* Ordered dot products: left to right, no contraction. */
NR_HELPER_ATTR nr_v4 nr_v4_dp(nr_v4 a, nr_v4 b, nr_u32 n) {
    nr_f32 d = NR_V4_LANE(a, 0) * NR_V4_LANE(b, 0); nr_u32 c;
    for (c = 1; c < n; ++c) { nr_f32 product = NR_V4_LANE(a, c) * NR_V4_LANE(b, c); d = d + product; }
    return nr_v4_splat(d);
}
/* Relatively addressed constant: out-of-range elements read as zero. */
NR_INLINE nr_v4 nr_v4_cb(const nr_u8 *cb, nr_u32 first, nr_u32 count, nr_u32 index) { return index < count ? nr_v4_load(cb + (first + index) * 16u) : nr_v4_zero(); }

typedef struct {
    nr_u32 native, vs_inputs, ps_inputs, position_output, interpolation, varying_count;
    nr_u32 vs_constants, ps_constants, texture_count, vs_immediate_count, ps_immediate_count;
    nr_u32 signature;                 /* hash of the metadata above, checked against the compiled program */
    const nr_u8 *vs_immediates, *ps_immediates;
} nr_program;
NR_INLINE nr_u32 nr_program_signature(const nr_program *p) {
    nr_u32 a = p->vs_inputs | (p->ps_inputs << 16) | (p->position_output << 24) | (p->varying_count << 28);
    nr_u32 b = p->vs_constants | (p->ps_constants << 9) | (p->texture_count << 18) | (p->vs_immediate_count << 21) | (p->ps_immediate_count << 27);
    return (a * 0x9e3779b1u) ^ (b * 0x85ebca77u) ^ (p->interpolation * 0xc2b2ae3du);
}
typedef struct {
    nr_u32 kind, program, vertex_offset, vertex_count;
    nr_i32 clip[4];
    nr_u32 blend_mode, write_mask, cull, front_ccw, fill_color, constants_offset;
    nr_f32 blend_constant[4], viewport[4], depth[2];
    nr_u32 textures[4]; nr_u32 textures2[4];
} nr_command;
typedef struct {
    const nr_u8 *data; nr_u8 *output;
    nr_u32 tile_x, tile_y, tile_w, tile_h, format, command_count, commands_offset, programs_offset, program_count;
    nr_u32 vertices_offset, vertices_bytes, atlas_offset, atlas_bytes, background_offset, constants_offset, constants_bytes, window_count;
    nr_u32 covered, escapes, loop_overflows;   /* loop_overflows: shader loops stopped at the iteration budget */
} nr_span;
/* Shader stage registers: inputs, outputs and the stage's flat constant and immediate tables. */
typedef struct { nr_v4 in[16], out[8], dx[8], dy[8]; const nr_u8 *constants, *immediates; nr_u32 *escapes, *overflows; } nr_shade;   /* dx/dy: screen derivatives of inputs 0..7 for stage evaluation */
typedef struct { nr_i32 sx, sy; nr_f32 x, y, inverse_w; nr_v4 varying[NR_FRAME_MAX_VARYINGS]; } nr_projected;
/* Per-command state resolved once: texture windows, blend factors, constants, vertex stride. */
typedef struct {
    const nr_u8 *window_data[NR_FRAME_MAX_TEXTURES];
    nr_u32 window_w[NR_FRAME_MAX_TEXTURES], window_h[NR_FRAME_MAX_TEXTURES], origin_x[NR_FRAME_MAX_TEXTURES], origin_y[NR_FRAME_MAX_TEXTURES];
    const nr_u8 *window2_data[NR_FRAME_MAX_TEXTURES];                     /* optional second window of the same texture */
    nr_u32 window2_w[NR_FRAME_MAX_TEXTURES], window2_h[NR_FRAME_MAX_TEXTURES], origin2_x[NR_FRAME_MAX_TEXTURES], origin2_y[NR_FRAME_MAX_TEXTURES], bound2[NR_FRAME_MAX_TEXTURES];
    nr_u32 texture_w[NR_FRAME_MAX_TEXTURES], texture_h[NR_FRAME_MAX_TEXTURES], format[NR_FRAME_MAX_TEXTURES], point[NR_FRAME_MAX_TEXTURES], bound[NR_FRAME_MAX_TEXTURES];
    nr_u32 mirror_u[NR_FRAME_MAX_TEXTURES], mirror_v[NR_FRAME_MAX_TEXTURES];                /* SM4 mirror addressing per axis */
    nr_f32 half[NR_FRAME_MAX_TEXTURES], lerp[NR_FRAME_MAX_TEXTURES];     /* 0.5/1 bilinear, 0/0 point */
    nr_uv4 swap_mask_texture[NR_FRAME_MAX_TEXTURES];                      /* all ones for BGRA windows */
    nr_u32 source_factor, destination_factor, mode, mask_bytes[4], byte_lane[4];
    nr_f32 constant[4];
    nr_f32 src_const, src_da, src_1mda, dst_const, dst_a, dst_1ma, src_a;   /* blend factor selectors as 0/1 weights */
    nr_uv4 byte_shifts, mask_lanes, constant_mask, swap_mask, invert_mask; /* (0,8,16,24); written bytes; mode 13; BGRA target; mode 15 */
    nr_v4 constant255;                                                     /* blend constant * 255 in byte order */
    nr_u32 vs_stride;
    const nr_u8 *vs_constants, *ps_constants;
} nr_command_state;
/* One scanline of one triangle handed to a generated row shader. */
typedef struct {
    const nr_command_state *st; const nr_projected *v; nr_u8 *pixels; nr_u32 *escapes, *overflows;
    const nr_u8 *constants, *immediates;
    nr_i32 count; nr_f32 wgt[3], dwgt[3], dwgt_y[3];   /* edge weights at the row start and their x and y steps */
} nr_row;

NR_INLINE nr_i32 nr_region(nr_u32 offset, nr_u32 bytes) {
    return offset <= NR_FRAME_SPAN && bytes <= NR_FRAME_SPAN - offset && !(offset & 3u);
}
/* Address of one texel tap: the first window, else the second window of the same
 * texture, else the nearest texel of the first window counted as an escape. */
NR_HELPER_ATTR const nr_u8 *nr_tap(const nr_command_state *st, nr_u32 t, nr_i32 x, nr_i32 y, nr_u32 *escapes) {
    nr_i32 rx = x - (nr_i32)st->origin_x[t], ry = y - (nr_i32)st->origin_y[t];
    nr_u32 ww = st->window_w[t], wh = st->window_h[t];
    if (rx >= 0 && ry >= 0 && rx < (nr_i32)ww && ry < (nr_i32)wh) return st->window_data[t] + ((nr_u32)ry * ww + (nr_u32)rx) * 4;
    if (st->bound2[t]) {
        nr_i32 bx = x - (nr_i32)st->origin2_x[t], by = y - (nr_i32)st->origin2_y[t];
        if (bx >= 0 && by >= 0 && bx < (nr_i32)st->window2_w[t] && by < (nr_i32)st->window2_h[t]) return st->window2_data[t] + ((nr_u32)by * st->window2_w[t] + (nr_u32)bx) * 4;
    }
    *escapes += 1u;
    rx = rx < 0 ? 0 : (rx >= (nr_i32)ww ? (nr_i32)ww - 1 : rx); ry = ry < 0 ? 0 : (ry >= (nr_i32)wh ? (nr_i32)wh - 1 : ry);
    return st->window_data[t] + ((nr_u32)ry * ww + (nr_u32)rx) * 4;
}
/* Bilinear or point sample of a texture: taps are served from one of up to two
 * host-packed windows; a tap outside both clamps to the first and counts as an escape. */
NR_SAMPLE_ATTR nr_v4 nr_sample(const nr_command_state *st, nr_u32 texture, nr_u32 u_bits, nr_u32 v_bits, nr_u32 *escapes) {
    nr_f32 tw = (nr_f32)st->texture_w[texture], th = (nr_f32)st->texture_h[texture], half = st->half[texture], lerp = st->lerp[texture];
    nr_f32 u = NR_V4_LANE(nr_v4_clamp01_fast(nr_v4_splat(st->mirror_u[texture] ? nr_mirror01(nr_float(u_bits)) : nr_float(u_bits))), 0) * tw - half;
    nr_f32 v = NR_V4_LANE(nr_v4_clamp01_fast(nr_v4_splat(st->mirror_v[texture] ? nr_mirror01(nr_float(v_bits)) : nr_float(v_bits))), 0) * th - half;
    nr_f32 xmax = tw - 1.0f, ymax = th - 1.0f, x, y, tx, ty;
    nr_u32 ix, iy, jx, jy, ww = st->window_w[texture], wh = st->window_h[texture];
    nr_i32 ox = (nr_i32)st->origin_x[texture], oy = (nr_i32)st->origin_y[texture], rx0, rx1, ry0, ry1, miss;
    const nr_u8 *window = st->window_data[texture];
    nr_uv4 shifts = st->byte_shifts, swap = st->swap_mask_texture[texture], ff = nr_uv4_splat(255u);
    nr_v4 t0, t1, t2, t3, top, bottom, one_tx, one_ty, scale = nr_v4_splat(1.0f / 255.0f);
    x = u < 0.0f ? 0.0f : u; x = x > xmax ? xmax : x;
    y = v < 0.0f ? 0.0f : v; y = y > ymax ? ymax : y;
    ix = (nr_u32)x; iy = (nr_u32)y;
    jx = ix + ((ix + 1u < st->texture_w[texture]) & (lerp != 0.0f)); jy = iy + ((iy + 1u < st->texture_h[texture]) & (lerp != 0.0f));
    tx = (x - (nr_f32)ix) * lerp; ty = (y - (nr_f32)iy) * lerp;
    (void)rx0; (void)rx1; (void)ry0; (void)ry1; (void)miss;
    /* Taps inside the first window take the inline path; the second window and escapes go through nr_tap. */
#define NR_TAP_PTR(x, y) (((nr_u32)((nr_i32)(x) - ox) < ww && (nr_u32)((nr_i32)(y) - oy) < wh) ? window + ((nr_u32)((nr_i32)(y) - oy) * ww + (nr_u32)((nr_i32)(x) - ox)) * 4 : nr_tap(st, texture, (nr_i32)(x), (nr_i32)(y), escapes))
#define NR_TAP(x, y) nr_v4_mul(nr_uv4_to_f(nr_uv4_and(nr_uv4_shr(nr_uv4_splat(nr_ld(NR_TAP_PTR(x, y))), shifts), ff)), scale)
    t0 = NR_TAP(ix, iy);
    /* Point filtering and texel-aligned blits weight the other three taps by zero: one load. */
    if (tx == 0.0f && ty == 0.0f) return nr_v4_sel(swap, NR_V4_SWZ(t0, 2, 1, 0, 3), t0);
    t1 = NR_TAP(jx, iy); t2 = NR_TAP(ix, jy); t3 = NR_TAP(jx, jy);
#undef NR_TAP
#undef NR_TAP_PTR
    t0 = nr_v4_sel(swap, NR_V4_SWZ(t0, 2, 1, 0, 3), t0); t1 = nr_v4_sel(swap, NR_V4_SWZ(t1, 2, 1, 0, 3), t1);
    t2 = nr_v4_sel(swap, NR_V4_SWZ(t2, 2, 1, 0, 3), t2); t3 = nr_v4_sel(swap, NR_V4_SWZ(t3, 2, 1, 0, 3), t3);
    one_tx = nr_v4_splat(1.0f - tx); one_ty = nr_v4_splat(1.0f - ty);
    top = nr_v4_add(nr_v4_mul(t0, one_tx), nr_v4_mul(t1, nr_v4_splat(tx)));
    bottom = nr_v4_add(nr_v4_mul(t2, one_tx), nr_v4_mul(t3, nr_v4_splat(tx)));
    return nr_v4_add(nr_v4_mul(top, one_ty), nr_v4_mul(bottom, nr_v4_splat(ty)));
}
/* Integer texel fetch (SM4 ld): element (x, y) of the texture at mip 0, zero for any coordinate
 * outside the resource. R32_UINT windows (buffers) return the raw element as (x, 0, 0, 1); UNORM
 * windows unpack like a point tap. Taps outside the bound windows clamp and count as escapes. */
#ifndef NR_FRAME_NO_LD
NR_SAMPLE_ATTR nr_v4 nr_load(const nr_command_state *st, nr_u32 texture, nr_u32 x, nr_u32 y, nr_u32 mip, nr_u32 *escapes) {
    nr_u32 word; nr_v4 t;
    if (x >= st->texture_w[texture] || y >= st->texture_h[texture] || mip) return nr_v4_zero();
    word = nr_ld(nr_tap(st, texture, (nr_i32)x, (nr_i32)y, escapes));
    if (st->format[texture] == 42u) return nr_v4_from_bits(word, 0u, 0u, 1u);
    t = nr_v4_mul(nr_uv4_to_f(nr_uv4_and(nr_uv4_shr(nr_uv4_splat(word), st->byte_shifts), nr_uv4_splat(255u))), nr_v4_splat(1.0f / 255.0f));
    return nr_v4_sel(st->swap_mask_texture[texture], NR_V4_SWZ(t, 2, 1, 0, 3), t);
}
#endif
/* Blends one shaded color into a target pixel: all four bytes as vector lanes. */
/* Blobs without a dual-source program (NR_FRAME_NO_DUAL) keep the single-output blend: the blend
 * factors read the colour's own alpha. Otherwise `factor_alpha` is the alpha the factors read: the
 * colour's own, or the second output's for dual-source blending (mode 14, SRC1_ALPHA / INV_SRC1_ALPHA). */
#ifdef NR_FRAME_NO_DUAL
NR_HELPER_ATTR void nr_blend_pixel(const nr_command_state *st, nr_u8 *pixel, nr_v4 color) {
#define NR_BLEND_ALPHA_TERMS ss = st->src_const + st->src_da * da + st->src_1mda * (255.0f - da);
    nr_f32 alpha;
#else
NR_HELPER_ATTR void nr_blend_pixel_alpha(const nr_command_state *st, nr_u8 *pixel, nr_v4 color, nr_f32 factor_alpha) {
#define NR_BLEND_ALPHA_TERMS ss = st->src_const + st->src_da * da + st->src_1mda * (255.0f - da) + st->src_a * 255.0f * alpha;
    nr_f32 alpha = NR_V4_LANE(nr_v4_clamp01_fast(nr_v4_splat(factor_alpha)), 0);
#endif
    nr_u32 background = nr_ld(pixel), packed;
    nr_uv4 bg = nr_uv4_and(nr_uv4_shr(nr_uv4_splat(background), st->byte_shifts), nr_uv4_splat(255u));
    nr_v4 dst = nr_uv4_to_f(bg), fg = nr_v4_clamp01_fast(color), fg_b, blended, constant_value, value, fraction;
    nr_f32 da = NR_V4_LANE(dst, 3), ss, sd;
    nr_uv4 base, up, q, shifted;
#ifdef NR_FRAME_NO_DUAL
    alpha = NR_V4_LANE(fg, 3);
#endif
    fg_b = nr_v4_sel(st->swap_mask, NR_V4_SWZ(fg, 2, 1, 0, 3), fg);
    NR_BLEND_ALPHA_TERMS
#undef NR_BLEND_ALPHA_TERMS
    sd = st->dst_const + st->dst_a * alpha + st->dst_1ma * (1.0f - alpha);
    blended = nr_v4_add(nr_v4_mul(fg_b, nr_v4_splat(ss)), nr_v4_mul(dst, nr_v4_splat(sd)));
    constant_value = nr_v4_add(nr_v4_mul(fg_b, st->constant255), nr_v4_mul(dst, nr_v4_sub(nr_v4_splat(1.0f), fg_b)));
    value = nr_v4_sel(st->constant_mask, constant_value, blended);
#ifndef NR_FRAME_NO_INVERT_BLEND
    /* Mode 15 (INV_DEST_COLOR / INV_SRC_ALPHA, alpha ZERO / ONE): colour = source * (255 - destination) +
     * destination * (1 - alpha), alpha lane = destination. Blobs of large programs leave it out (window). */
    value = nr_v4_sel(st->invert_mask, nr_v4_sel(nr_uv4_make(0u, 0u, 0u, 0xffffffffu), dst, nr_v4_add(nr_v4_mul(fg_b, nr_v4_sub(nr_v4_splat(255.0f), dst)), nr_v4_mul(dst, nr_v4_splat(1.0f - alpha)))), value);
#endif
    value = nr_v4_sel(nr_v4_gt(value, nr_v4_splat(255.0f)), nr_v4_splat(255.0f), value);
    base = nr_v4_to_u(value); fraction = nr_v4_sub(value, nr_uv4_to_f(base));
    up = nr_uv4_or(nr_v4_gt(fraction, nr_v4_splat(0.5f)), nr_uv4_and(nr_v4_eq(fraction, nr_v4_splat(0.5f)), nr_uv4_eq(nr_uv4_and(base, nr_uv4_splat(1u)), nr_uv4_splat(1u))));
    q = nr_uv4_sub(base, up);
    q = nr_uv4_sel(st->mask_lanes, q, bg);
    shifted = nr_uv4_shl(q, st->byte_shifts);
    packed = NR_UV4_LANE(shifted, 0) | NR_UV4_LANE(shifted, 1) | NR_UV4_LANE(shifted, 2) | NR_UV4_LANE(shifted, 3);
    nr_st(pixel, packed);
}
#ifndef NR_FRAME_NO_DUAL
NR_INLINE void nr_blend_pixel(const nr_command_state *st, nr_u8 *pixel, nr_v4 color) { nr_blend_pixel_alpha(st, pixel, color, NR_V4_LANE(color, 3)); }
/* Dual-source blending: the factors read the second output's alpha (clamped), the colour is the first output. */
NR_INLINE void nr_blend_pixel_dual(const nr_command_state *st, nr_u8 *pixel, nr_v4 color, nr_v4 second) { nr_blend_pixel_alpha(st, pixel, color, NR_V4_LANE(second, 3)); }
#endif
/* Vertex stage that copies inputs to outputs; fixture programs share it. */
NR_NOINLINE nr_i32 nr_stage_passthrough(const nr_command_state *st, nr_shade *sh) {
    nr_u32 r; (void)st;
    for (r = 0; r < 8; ++r) sh->out[r] = sh->in[r];
    return 1;
}
/* Generated native shaders: nr_native_vs, nr_native_row (nr_native_ps on the host), nr_native_check, NR_NATIVE_COUNT. */
#ifdef NR_FRAME_PROGRAMS_HEADER
#include NR_FRAME_PROGRAMS_HEADER
#else
#include "frame_programs.h"
#endif

NR_NOINLINE nr_i32 nr_program_load(const nr_span *s, nr_u32 index, nr_program *p) {
    nr_u32 entry, n, m; const nr_u8 *e;
    if (index >= s->program_count) return 0;
    entry = nr_ld(s->data + s->programs_offset + 4 + index * 4);
    if ((entry & 15u) || !nr_region(entry, NR_FRAME_PROGRAM_BYTES)) return 0;
    e = s->data + entry;
    p->native = nr_ld(e); p->vs_inputs = nr_ld(e + 4); p->ps_inputs = nr_ld(e + 8); p->position_output = nr_ld(e + 12);
    p->interpolation = nr_ld(e + 16); p->varying_count = nr_ld(e + 20); p->vs_constants = nr_ld(e + 24); p->ps_constants = nr_ld(e + 28);
    p->texture_count = nr_ld(e + 32); p->vs_immediate_count = nr_ld(e + 36); p->ps_immediate_count = nr_ld(e + 40);
    for (n = 44; n < NR_FRAME_PROGRAM_BYTES; n += 4) if (nr_ld(e + n)) return 0;
    if (!p->vs_inputs || p->vs_inputs > 0xffffu || p->ps_inputs > 0xffu || p->position_output >= 8 ||
        p->varying_count > NR_FRAME_MAX_VARYINGS || p->vs_constants > 256 || p->ps_constants > 256 ||
        p->texture_count > NR_FRAME_MAX_TEXTURES || p->vs_immediate_count > NR_FRAME_MAX_IMMEDIATES || p->ps_immediate_count > NR_FRAME_MAX_IMMEDIATES) return 0;
    for (n = 0, m = 0; n < 8; ++n) if (p->ps_inputs & (1u << n)) ++m;
    if (m != p->varying_count || (p->ps_inputs & (1u << p->position_output))) return 0;
    if (!nr_region(entry + NR_FRAME_PROGRAM_BYTES, (p->vs_immediate_count + p->ps_immediate_count) * 16)) return 0;
    p->vs_immediates = e + NR_FRAME_PROGRAM_BYTES; p->ps_immediates = p->vs_immediates + p->vs_immediate_count * 16;
    p->signature = nr_program_signature(p);
    return p->native < NR_NATIVE_COUNT && nr_native_check(p);
}
NR_EXECUTE_ATTR nr_i32 nr_vertex(const nr_span *s, const nr_command *cmd, const nr_command_state *st, const nr_program *p, nr_u32 index, nr_shade *sh, nr_projected *out) {
    nr_u32 n, c, slot = 0, k; const nr_u8 *v; nr_f32 clip[4], w, x, y;
    v = s->data + s->vertices_offset + cmd->vertex_offset + index * st->vs_stride * 16;
    for (n = 0; n < 16; ++n) if (p->vs_inputs & (1u << n)) { sh->in[n] = nr_v4_load(v + slot * 16); ++slot; }
    sh->constants = st->vs_constants; sh->immediates = p->vs_immediates;
    if (!nr_native_vs(p->native, st, sh)) return 0;
    for (c = 0; c < 4; ++c) { clip[c] = NR_V4_LANE(sh->out[p->position_output], c); if (!nr_finite(clip[c])) return 0; }
    w = clip[3];
    if (!(w >= 1.0f / 65536 && w <= 65536.0f)) return 0;
    x = (clip[0] / w + 1.0f) * (cmd->viewport[2] * 0.5f) + cmd->viewport[0];
    y = (1.0f - clip[1] / w) * (cmd->viewport[3] * 0.5f) + cmd->viewport[1];
    if (!nr_finite(x) || !nr_finite(y) || x < -1048576.0f || x > 1048576.0f || y < -1048576.0f || y > 1048576.0f) return 0;
    out->x = x; out->y = y; out->inverse_w = 1.0f / w; out->sx = nr_snap(x); out->sy = nr_snap(y);
    for (n = 0, k = 0; n < 8; ++n) if (p->ps_inputs & (1u << n)) {
        for (c = 0; c < 4; ++c) {
            nr_f32 value = NR_V4_LANE(sh->out[n], c);
            if (!(value >= -65536.0f && value <= 65536.0f)) return 0;
        }
        out->varying[k] = sh->out[n];
        ++k;
    }
    return 1;
}
/* Narrows [xs, xe) within the row [x0, x0 + width) to the pixels where
 * e0 + step * (x - x0) is positive (strict) or nonnegative. The crossing is
 * found by a power-of-two descent on exact 64-bit sums: no division. */
NR_NOINLINE void nr_edge_interval(nr_i64 e0, nr_i64 step, nr_i32 strict, nr_i32 x0, nr_i32 width, nr_i32 *xs, nr_i32 *xe) {
    nr_i64 threshold = strict ? 1 : 0, e = e0;
    nr_i32 q = 0, k;
    if (step == 0) { if (e0 < threshold) *xe = *xs; return; }
    if (step > 0) {
        if (e < threshold) {
            for (k = 7; k >= 0; --k) { nr_i64 s = nr_shl64(step, (nr_u32)k); nr_i32 j = 1 << k; if (q + j <= width - 1 && e + s < threshold) { q += j; e += s; } }
            ++q;
        }
        if (x0 + q > *xs) *xs = x0 + q;
    } else {
        if (e >= threshold) {
            for (k = 7; k >= 0; --k) { nr_i64 s = nr_shl64(step, (nr_u32)k); nr_i32 j = 1 << k; if (q + j <= width - 1 && e + s >= threshold) { q += j; e += s; } }
            ++q;
        }
        if (x0 + q < *xe) *xe = x0 + q;
    }
}
NR_EXECUTE_ATTR nr_i32 nr_draw_triangle(nr_span *s, const nr_command *cmd, const nr_command_state *st, const nr_program *p, const nr_projected v[3]) {
    nr_i64 area = nr_edge(v[0].sx, v[0].sy, v[1].sx, v[1].sy, v[2].sx, v[2].sy);
    nr_i32 x0, y0, x1, y1, py, n, sign;
    nr_i32 minx, miny, maxx, maxy;
    nr_i32 top_left[3]; nr_i64 step[3];
    nr_row row;
    if (!area) return 1;
    if (cmd->cull && !(((cmd->cull == 2) != (cmd->front_ccw != 0)) ? area > 0 : area < 0)) return 1;
    sign = area > 0 ? 1 : -1;
    minx = v[0].sx; maxx = v[0].sx; miny = v[0].sy; maxy = v[0].sy;
    for (n = 1; n < 3; ++n) {
        if (v[n].sx < minx) minx = v[n].sx; if (v[n].sx > maxx) maxx = v[n].sx;
        if (v[n].sy < miny) miny = v[n].sy; if (v[n].sy > maxy) maxy = v[n].sy;
    }
    x0 = (minx - 128) >> 8; if (x0 < 0) x0 = 0; y0 = (miny - 128) >> 8; if (y0 < 0) y0 = 0;
    x1 = ((maxx - 128) >> 8) + 1; y1 = ((maxy - 128) >> 8) + 1;
    x0 -= (nr_i32)s->tile_x; x1 -= (nr_i32)s->tile_x; y0 -= (nr_i32)s->tile_y; y1 -= (nr_i32)s->tile_y;
    if (x0 < cmd->clip[0]) x0 = cmd->clip[0]; if (y0 < cmd->clip[1]) y0 = cmd->clip[1];
    if (x1 > cmd->clip[2]) x1 = cmd->clip[2]; if (y1 > cmd->clip[3]) y1 = cmd->clip[3];
    if (x0 >= x1 || y0 >= y1) return 1;
    for (n = 0; n < 3; ++n) {
        nr_i32 b = (n + 1) % 3, dx = v[b].sx - v[n].sx, dy = v[b].sy - v[n].sy;
        nr_i32 top = dy < 0 || (dy == 0 && dx > 0), reverse = dy > 0 || (dy == 0 && dx < 0);
        top_left[n] = sign > 0 ? top : reverse;
        step[n] = nr_shl64((nr_i64)(-dy), 8); if (sign < 0) step[n] = -step[n];
    }
    row.st = st; row.v = v; row.escapes = &s->escapes; row.overflows = &s->loop_overflows; row.constants = st->ps_constants; row.immediates = p->ps_immediates;
    for (py = y0; py < y1; ++py) {
        nr_i32 cy = ((nr_i32)s->tile_y + py) * 256 + 128, cx0 = ((nr_i32)s->tile_x + x0) * 256 + 128, xs = x0, xe = x1;
        nr_f32 sy = (nr_f32)cy * (1.0f / 256.0f);
        for (n = 0; n < 3; ++n) {
            nr_i32 b = (n + 1) % 3;
            nr_i64 e0 = nr_edge(v[n].sx, v[n].sy, v[b].sx, v[b].sy, cx0, cy);
            if (sign < 0) e0 = -e0;
            nr_edge_interval(e0, step[n], !top_left[n], x0, x1 - x0, &xs, &xe);
            if (xs >= xe) break;
        }
        if (xs >= xe) continue;
        for (n = 0; n < 3; ++n) {
            nr_i32 b = (n + 1) % 3, c2 = (n + 2) % 3;
            nr_f32 sx = (nr_f32)(((nr_i32)s->tile_x + xs) * 256 + 128) * (1.0f / 256.0f);
            row.wgt[n] = nr_weight(v[b].x, v[b].y, v[c2].x, v[c2].y, sx, sy);
            row.dwgt[n] = v[b].y - v[c2].y; row.dwgt_y[n] = v[c2].x - v[b].x;
        }
        s->covered += (nr_u32)(xe - xs);
        row.pixels = s->output + NR_FRAME_OUTPUT_PIXELS + ((nr_u32)py * s->tile_w + (nr_u32)xs) * 4;
        row.count = xe - xs;
        nr_native_row(p->native, &row);
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
    for (n = 0; n < 4; ++n) { cmd->textures[n] = nr_ld(c + 96 + n * 4); cmd->textures2[n] = nr_ld(c + 112 + n * 4); }
#ifdef NR_FRAME_NO_INVERT_BLEND
    if (cmd->blend_mode > 14) return 0;   /* mode 15 is not compiled into this blob */
#endif
    if (cmd->kind > 1 || cmd->blend_mode > 15 || !cmd->write_mask || cmd->write_mask > 15 || cmd->cull > 2 || cmd->front_ccw > 1) return 0;   /* 14: dual-source over, 15: invert destination over */
    if (cmd->clip[0] < 0 || cmd->clip[1] < 0 || cmd->clip[0] > cmd->clip[2] || cmd->clip[1] > cmd->clip[3] ||
        cmd->clip[2] > (nr_i32)s->tile_w || cmd->clip[3] > (nr_i32)s->tile_h) return 0;
    for (n = 0; n < 4; ++n) {
        if (!nr_finite(cmd->blend_constant[n]) || !nr_finite(cmd->viewport[n])) return 0;
        if ((cmd->textures[n] >> 19) || ((cmd->textures[n] & 0xffffu) != NR_FRAME_NO_WINDOW && (cmd->textures[n] & 0xffffu) >= s->window_count)) return 0;
        if ((cmd->textures2[n] >> 16) || (cmd->textures2[n] != NR_FRAME_NO_WINDOW && (cmd->textures2[n] >= s->window_count || (cmd->textures[n] & 0xffffu) == NR_FRAME_NO_WINDOW))) return 0;
    }
    if (cmd->kind == 0) {
        if (!cmd->vertex_count || cmd->vertex_count % 3 || cmd->vertex_count > 4095 || (cmd->vertex_offset & 15u) || (cmd->constants_offset & 15u) ||
            cmd->vertex_offset > s->vertices_bytes || cmd->constants_offset > s->constants_bytes) return 0;
        if (!(cmd->viewport[2] > 0 && cmd->viewport[2] <= 8192 && cmd->viewport[3] > 0 && cmd->viewport[3] <= 8192 &&
              cmd->viewport[0] >= -8192 && cmd->viewport[0] <= 8192 && cmd->viewport[1] >= -8192 && cmd->viewport[1] <= 8192)) return 0;
    } else if (cmd->program || cmd->vertex_offset || cmd->vertex_count || cmd->constants_offset) return 0;
    return 1;
}
/* Resolves the blend factors, write mask bytes and byte lanes for one command and target format. */
NR_INLINE void nr_blend_state_load(nr_u32 format, const nr_command *cmd, nr_command_state *st) {
    const nr_u32 source_codes = 0x001fbb05u, destination_codes = 0x005ec1ecu; nr_u32 n;
    st->mode = cmd->blend_mode;
    st->source_factor = (source_codes >> (st->mode * 2)) & 3u; st->destination_factor = (destination_codes >> (st->mode * 2)) & 3u;
    for (n = 0; n < 4; ++n) {
        nr_u32 logical = (format == 87 && n < 3) ? 2 - n : n;
        st->byte_lane[n] = logical; st->mask_bytes[n] = (cmd->write_mask >> logical) & 1u; st->constant[n] = nr_clamp01(cmd->blend_constant[n]);
    }
    st->src_const = st->source_factor == 1 ? 255.0f : 0.0f; st->src_da = st->source_factor == 2 ? 1.0f : 0.0f; st->src_1mda = st->source_factor == 3 ? 1.0f : 0.0f;
    st->dst_const = st->destination_factor == 1 ? 1.0f : 0.0f; st->dst_a = st->destination_factor == 2 ? 1.0f : 0.0f; st->dst_1ma = st->destination_factor == 3 ? 1.0f : 0.0f;
#ifndef NR_FRAME_NO_DUAL
    /* Mode 14 (dual-source over): source * alpha + destination * (1 - alpha) with the alpha the row shader supplies. */
    st->src_a = st->mode == 14u ? 1.0f : 0.0f; if (st->mode == 14u) st->dst_1ma = 1.0f;
#endif
    st->byte_shifts = nr_uv4_make(0u, 8u, 16u, 24u);
    st->mask_lanes = nr_uv4_make(st->mask_bytes[0] ? 0xffffffffu : 0u, st->mask_bytes[1] ? 0xffffffffu : 0u, st->mask_bytes[2] ? 0xffffffffu : 0u, st->mask_bytes[3] ? 0xffffffffu : 0u);
    st->constant_mask = nr_uv4_splat(st->mode == 13 ? 0xffffffffu : 0u);
#ifndef NR_FRAME_NO_INVERT_BLEND
    st->invert_mask = nr_uv4_splat(st->mode == 15 ? 0xffffffffu : 0u);
#endif
    st->swap_mask = nr_uv4_splat(format == 87 ? 0xffffffffu : 0u);
    st->constant255 = nr_v4_make(st->constant[st->byte_lane[0]] * 255.0f, st->constant[st->byte_lane[1]] * 255.0f, st->constant[st->byte_lane[2]] * 255.0f, st->constant[st->byte_lane[3]] * 255.0f);
}
/* Resolves per-command state once: vertex stride, constant extents, windows and blend factors. */
NR_NOINLINE nr_i32 nr_command_state_load(const nr_span *s, const nr_command *cmd, const nr_program *p, nr_command_state *st) {
    nr_u32 n, stride = 0;
    for (n = 0; n < 16; ++n) if (p->vs_inputs & (1u << n)) ++stride;
    st->vs_stride = stride;
    if (!stride || cmd->vertex_count * stride * 16 > s->vertices_bytes - cmd->vertex_offset ||
        (p->vs_constants + p->ps_constants) * 16 > s->constants_bytes - cmd->constants_offset) return 0;
    st->vs_constants = s->data + s->constants_offset + cmd->constants_offset;
    st->ps_constants = st->vs_constants + p->vs_constants * 16;
    st->byte_shifts = nr_uv4_make(0u, 8u, 16u, 24u);
    for (n = 0; n < NR_FRAME_MAX_TEXTURES; ++n) {
        nr_u32 binding = cmd->textures[n], index = binding & 0xffffu; const nr_u8 *rec;
        st->bound[n] = index != NR_FRAME_NO_WINDOW; st->point[n] = (binding >> 16) & 1u; st->mirror_u[n] = (binding >> 17) & 1u; st->mirror_v[n] = (binding >> 18) & 1u;
        st->half[n] = st->point[n] ? 0.0f : 0.5f; st->lerp[n] = st->point[n] ? 0.0f : 1.0f;
        if (!st->bound[n]) {
            if (n < p->texture_count) return 0;     /* the program samples an unbound texture */
            st->window_data[n] = s->data; st->window_w[n] = st->window_h[n] = 1; st->origin_x[n] = st->origin_y[n] = 0; st->texture_w[n] = st->texture_h[n] = 1; st->format[n] = 28;
            st->swap_mask_texture[n] = nr_uv4_splat(0u);
            st->bound2[n] = 0; st->window2_data[n] = s->data; st->window2_w[n] = st->window2_h[n] = 0; st->origin2_x[n] = st->origin2_y[n] = 0;
            if (cmd->textures2[n] != NR_FRAME_NO_WINDOW) return 0;
            continue;
        }
        rec = s->data + s->atlas_offset + 4 + index * NR_FRAME_WINDOW_BYTES;
        st->window_data[n] = s->data + s->atlas_offset + nr_ld(rec); st->window_w[n] = nr_ld(rec + 4); st->window_h[n] = nr_ld(rec + 8);
        st->origin_x[n] = nr_ld(rec + 12); st->origin_y[n] = nr_ld(rec + 16); st->texture_w[n] = nr_ld(rec + 20); st->texture_h[n] = nr_ld(rec + 24); st->format[n] = nr_ld(rec + 28);
        st->swap_mask_texture[n] = nr_uv4_splat(st->format[n] == 87 ? 0xffffffffu : 0u);
        st->bound2[n] = cmd->textures2[n] != NR_FRAME_NO_WINDOW;
        if (st->bound2[n]) {
            const nr_u8 *rec2 = s->data + s->atlas_offset + 4 + cmd->textures2[n] * NR_FRAME_WINDOW_BYTES;
            st->window2_data[n] = s->data + s->atlas_offset + nr_ld(rec2); st->window2_w[n] = nr_ld(rec2 + 4); st->window2_h[n] = nr_ld(rec2 + 8);
            st->origin2_x[n] = nr_ld(rec2 + 12); st->origin2_y[n] = nr_ld(rec2 + 16);
            if (nr_ld(rec2 + 20) != st->texture_w[n] || nr_ld(rec2 + 24) != st->texture_h[n] || nr_ld(rec2 + 28) != st->format[n]) return 0;
        } else { st->window2_data[n] = st->window_data[n]; st->window2_w[n] = st->window2_h[n] = 0; st->origin2_x[n] = st->origin2_y[n] = 0; }
    }
    nr_blend_state_load(s->format, cmd, st);
    return 1;
}
NR_NOINLINE nr_i32 nr_span_load(nr_span *s, const nr_u8 *data, nr_u8 *output) {
    nr_u32 n;
    s->data = data; s->output = output; s->covered = 0; s->escapes = 0; s->loop_overflows = 0;
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
    if ((s->vertices_offset & 15u) || (s->constants_offset & 15u)) return 0;
    if (!nr_region(s->commands_offset, s->command_count * NR_FRAME_COMMAND_BYTES) ||
        !nr_region(s->programs_offset, 4 + s->program_count * 4) ||
        !nr_region(s->vertices_offset, s->vertices_bytes) ||
        !nr_region(s->atlas_offset, s->atlas_bytes) || s->atlas_bytes < 4 ||
        !nr_region(s->constants_offset, s->constants_bytes) ||
        !nr_region(s->background_offset, s->tile_w * s->tile_h * 4)) return 0;
    if (nr_ld(data + s->programs_offset) != s->program_count) return 0;
    s->window_count = nr_ld(data + s->atlas_offset);
    if (s->window_count > 256 || 4 + s->window_count * NR_FRAME_WINDOW_BYTES > s->atlas_bytes) return 0;
    for (n = 0; n < s->window_count; ++n) {
        const nr_u8 *rec = data + s->atlas_offset + 4 + n * NR_FRAME_WINDOW_BYTES;
        nr_u32 off = nr_ld(rec), w = nr_ld(rec + 4), h = nr_ld(rec + 8), ox = nr_ld(rec + 12), oy = nr_ld(rec + 16), tw = nr_ld(rec + 20), th = nr_ld(rec + 24), fmt = nr_ld(rec + 28);
#ifndef NR_FRAME_NO_LD
        if (!w || !h || !tw || !th || (off & 3u) || off > s->atlas_bytes) return 0;
        if (fmt == 42u) { if (h != 1 || th != 1 || w > 4194304u || tw > 4194304u) return 0; }   /* R32_UINT buffer elements: one row, read with ld */
        else if (w > 8192 || h > 8192 || tw > 8192 || th > 8192 || (fmt != 28 && fmt != 87)) return 0;
        if (w * h > (s->atlas_bytes - off) / 4 || ox > tw - w || oy > th - h) return 0;
#else
        if (!w || !h || w > 8192 || h > 8192 || (off & 3u) || off > s->atlas_bytes || w * h > (s->atlas_bytes - off) / 4 ||
            !tw || !th || tw > 8192 || th > 8192 || ox > tw - w || oy > th - h || (fmt != 28 && fmt != 87)) return 0;
#endif
    }
    return 1;
}
/* Renders the span into `output`, writing the output header. Returns the status code. */
NR_INLINE nr_u32 nr_render_span(const nr_u8 *data, nr_u8 *output) {
    nr_span s; nr_u32 n, status = NR_STATUS_OK, executed = 0, bytes, loaded_program = 0xffffffffu;
    nr_u8 *pixels = output + NR_FRAME_OUTPUT_PIXELS;
    nr_program p; nr_command_state st; nr_shade sh;
    for (n = 0; n < NR_FRAME_OUTPUT_PIXELS; n += 4) nr_st(output + n, 0);
    if (!nr_span_load(&s, data, output)) { nr_st(output, NR_FRAME_OUTPUT_MAGIC); nr_st(output + 4, NR_STATUS_HEADER); return NR_STATUS_HEADER; }
    bytes = s.tile_w * s.tile_h * 4;
    for (n = 0; n < bytes; n += 4) nr_st(pixels + n, nr_ld(data + s.background_offset + n));
    for (n = 0; n < 16; ++n) sh.in[n] = nr_v4_zero();
    for (n = 0; n < 8; ++n) sh.out[n] = nr_v4_zero();
    sh.escapes = &s.escapes; sh.overflows = &s.loop_overflows;
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
            nr_u32 t; nr_i32 ok = 1;
            if (cmd.program != loaded_program) {
                if (!nr_program_load(&s, cmd.program, &p)) { status = NR_STATUS_PROGRAM; break; }
                loaded_program = cmd.program;
            }
            if (!nr_command_state_load(&s, &cmd, &p, &st)) { status = NR_STATUS_COMMAND; break; }
            for (t = 0; t < cmd.vertex_count && ok; t += 3) {
                nr_projected v[3]; nr_u32 k;
                for (k = 0; k < 3; ++k) if (!nr_vertex(&s, &cmd, &st, &p, t + k, &sh, &v[k])) { ok = 0; break; }
                if (!ok) { status = NR_STATUS_VERTEX; break; }
                if (!nr_draw_triangle(&s, &cmd, &st, &p, v)) { ok = 0; status = NR_STATUS_ARITHMETIC; break; }
            }
            if (!ok) break;
        }
        ++executed;
    }
    nr_st(output, NR_FRAME_OUTPUT_MAGIC); nr_st(output + 4, status); nr_st(output + 8, executed);
    nr_st(output + 12, s.covered); nr_st(output + 16, s.escapes); nr_st(output + 20, s.loop_overflows); nr_st(output + 24, NR_KERNEL_IDENTITY);
    return status;
}
#endif
