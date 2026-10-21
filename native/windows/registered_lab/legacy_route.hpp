#pragma once
// The per-draw route (abi, flags) a draw takes through the qualified indexed pipeline.
// The driver records it into every batched draw so the worker can run a command that
// cannot execute on the frame kernel through the same per-draw executor the driver would
// have chosen; the worker derives it with the same function for a command that arrived
// without one (captures recorded before the field existed). One definition keeps both
// ends on the same executor.
#include "linear_texture_draw.hpp"
#include "output_merger.hpp"
#include "point_texture_draw.hpp"
#include "raster_executor.hpp"
#include "shader_ir.hpp"
#include <cstdint>
#include <stdexcept>
#include <utility>
namespace nr::registered_lab {
struct LegacyRouteInputs {
    const shader::Program *pixel = nullptr;
    bool textured = false;        // the pixel program samples at least one texture
    bool addressed = false;       // a bound sampler uses mirror or border addressing
    bool linear = false;          // sampler slot 0 is bound with a single-mip linear filter
    bool source_smaller = false;  // the slot-0 texture is smaller than the target
    bool scissored = false;       // the scissor rectangle differs from the full target
    uint32_t vertex_count = 0, write_mask = 15;
    raster::BlendMode blend_mode = raster::BlendMode::source_copy;
    raster::CullMode cull = raster::CullMode::none;
    bool front_ccw = false;
};
inline std::pair<uint32_t, uint32_t> legacy_route(const LegacyRouteInputs &in) {
    const auto &ps = *in.pixel;
    // Draws that need the general resource path: derivatives, several or non-2D or integer
    // textures, samplers beyond slot 0, buffer loads, and mirror or border addressing.
    bool resources = shader::quad_derivatives(ps) || in.addressed;
    for (unsigned slot = 0; slot < 16; ++slot) resources = resources ||
        (ps.textures[slot] && (slot || ps.texture_dimensions[slot] != 3 || ps.texture_uint[slot])) || (slot && ps.samplers[slot]);
    for (const auto &instruction : ps.instructions) resources = resources || instruction.opcode == shader::Opcode::load;
    const bool blend = in.blend_mode != raster::BlendMode::source_copy;
    bool lin = false, mod = false, general = false;
    if (in.textured && !resources) {
        lin = in.linear;
        mod = lin || ps.instructions.size() == 2 || blend || in.scissored || in.source_smaller;
        general = raster::general_blend(in.blend_mode) || in.vertex_count != 6 || in.cull != raster::CullMode::none || in.write_mask != 15;
        if (!general) {
            try {
                if (mod) raster::LinearTextureDrawExecutor::validate_shader(ps);
                else raster::PointTextureDrawExecutor::validate_shader(ps);
            } catch (const std::invalid_argument &) { general = true; }
        }
    }
    uint32_t abi = 1, flags = (in.write_mask << 8) | uint32_t(blend);
    if (in.textured && !resources) { abi = 2; flags |= 2; }
    if (in.scissored) { abi = 3; flags |= 4; }
    if (mod) { abi = lin ? 4 : 5; flags |= lin ? 8 : 16; }
    if (in.cull != raster::CullMode::none) { abi = 6; flags |= (uint32_t(in.cull) << 5) | (in.front_ccw ? 128u : 0u); }
    if (general) { abi = 7; flags |= 0x1000 | (lin ? 8u : 16u); }
    if (resources) { abi = 8; flags |= 0x2002; }
    if (in.blend_mode == raster::BlendMode::destination_in) { abi = 9; flags |= 0x4000; }
    if (in.blend_mode == raster::BlendMode::destination_out) { abi = 10; flags |= 0x8000; }
    if (raster::extended_blend(in.blend_mode)) { abi = 11; flags |= 0x10000 | (uint32_t(in.blend_mode) << 17); }
    if (in.blend_mode == raster::BlendMode::component_coverage) abi = 15;
    return {abi, flags};
}
}
