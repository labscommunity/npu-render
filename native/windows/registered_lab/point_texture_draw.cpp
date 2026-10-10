#include "point_texture_draw.hpp"
#include <cmath>
#include <cstring>
#include <chrono>
namespace nr::raster {
namespace {
void require(bool value) { if (!value) throw std::invalid_argument("unsupported point-textured triangle draw"); }
bool plain(const shader::Operand &o, shader::Register kind, uint32_t index) {
    return o.kind == kind && o.index == index && !o.element && !o.relative.enabled && o.modifier == shader::Modifier::none;
}
bool identity(const shader::Operand &o) { return o.lanes == std::array<uint32_t,4>{0,1,2,3}; }
struct Pattern { uint32_t color, uv; };
Pattern pattern(const shader::Program &p) {
    using namespace shader;
    require(p.stage == Stage::pixel && p.instructions.size() == 3 && p.outputs[0] == 15 && p.position_input == 16);
    const auto &sample = p.instructions[0], &alpha = p.instructions[1], &multiply = p.instructions[2];
    require(sample.opcode == Opcode::sample && sample.source_count == 3 && !sample.saturate &&
        sample.destination.kind == Register::temporary && sample.destination.mask == 15 &&
        plain(sample.destination, Register::temporary, sample.destination.index) &&
        sample.sources[0].kind == Register::input && plain(sample.sources[0], Register::input, sample.sources[0].index) &&
        sample.sources[0].lanes[0] == 0 && sample.sources[0].lanes[1] == 1 &&
        plain(sample.sources[1], Register::texture, 0) && identity(sample.sources[1]) && plain(sample.sources[2], Register::sampler, 0));
    const auto temporary = sample.destination.index;
    require(alpha.opcode == Opcode::move && alpha.source_count == 1 && !alpha.saturate &&
        plain(alpha.destination, Register::temporary, temporary) && alpha.destination.mask == 8 &&
        alpha.sources[0].kind == Register::immediate && alpha.sources[0].modifier == Modifier::none &&
        !alpha.sources[0].relative.enabled && alpha.sources[0].literal == std::array<uint32_t,4>{0x3f800000,0x3f800000,0x3f800000,0x3f800000});
    require(multiply.opcode == Opcode::multiply && multiply.source_count == 2 && !multiply.saturate &&
        plain(multiply.destination, Register::output, 0) && multiply.destination.mask == 15 &&
        plain(multiply.sources[0], Register::temporary, temporary) && identity(multiply.sources[0]) &&
        multiply.sources[1].kind == Register::input && plain(multiply.sources[1], Register::input, multiply.sources[1].index) && identity(multiply.sources[1]));
    Pattern result{multiply.sources[1].index, sample.sources[0].index};
    require(result.color < 16 && result.uv < 16 && result.color != result.uv && p.textures[0] && p.samplers[0]);
    for (uint32_t i = 0; i < 16; ++i) {
        require(p.inputs[i] == (i == result.color ? 15u : (i == result.uv ? 3u : 0u)));
        if (p.inputs[i]) require(p.interpolation[i] == 2); // linear, single sample, uniform clip w below
        if (i) require(!p.textures[i] && !p.samplers[i] && !p.outputs[i]);
    }
    for (const auto count : p.constants) require(!count);
    return result;
}
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) { return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); }
}
struct PointTextureDrawExecutor::State {
    shader::Program vs; Pattern pixel;
    uint32_t width,height,source_width,source_height,source_format;
    shader::VertexExecutor vertex;
    ProjectionExecutor projection;
    TextureCropExecutor crop;
    State(const shader::Program &v, const shader::Program &p, uint32_t w, uint32_t h, uint32_t format,
          uint32_t sw, uint32_t sh, uint32_t sf, const std::string &backend)
        : vs(v), pixel(pattern(p)), width(w), height(h), source_width(sw), source_height(sh), source_format(sf),
          vertex(v,6,backend), projection(6,backend), crop(sw,sh,w,h,backend,format != sf) {
        require(vs.stage == shader::Stage::vertex && vs.position_output < 16 && (format == 28 || format == 87) && (sf == 28 || sf == 87));
        require((vs.outputs[pixel.color] & 15) == 15 && (vs.outputs[pixel.uv] & 3) == 3);
    }
};
void PointTextureDrawExecutor::validate_shader(const shader::Program &p) { (void)pattern(p); }
PointTextureDrawExecutor::PointTextureDrawExecutor(const shader::Program &v,const shader::Program &p,uint32_t w,uint32_t h,uint32_t format,
    uint32_t sw,uint32_t sh,uint32_t sf,const std::string &backend) : state(std::make_unique<State>(v,p,w,h,format,sw,sh,sf,backend)) {}
PointTextureDrawExecutor::~PointTextureDrawExecutor() = default;
UniformDrawExecutor::Result PointTextureDrawExecutor::execute(const std::vector<shader::VertexRegisters> &inputs,
    const shader::VertexConstants &constants, const Viewport &viewport, const PackedTexture &source) {
    auto &s = *state; const auto start = Clock::now();
    require(inputs.size() == 6 && source.width == s.source_width && source.height == s.source_height && source.format == s.source_format &&
        source.pixels.size() == size_t(source.width)*source.height && viewport.x == 0 && viewport.y == 0 &&
        viewport.width == float(s.width) && viewport.height == float(s.height));
    auto began = Clock::now(); auto shaded = s.vertex.execute(inputs, constants);
    UniformDrawExecutor::Result result{}; result.timings.shader_ms = elapsed(began); result.execution_device = shaded.execution_device;
    result.input_bytes = shaded.input_bytes; result.output_bytes = shaded.output_bytes; ++result.provider_requests;
    std::vector<ClipPosition> clips(6);
    for (size_t i = 0; i < 6; ++i) {
        require(shaded.vertices[i][s.pixel.color] == shader::VertexBits{0x3f800000,0x3f800000,0x3f800000,0x3f800000});
        std::memcpy(clips[i].data(), shaded.vertices[i][s.vs.position_output].data(), 16); require(clips[i][3] == 1.f);
    }
    began = Clock::now(); const auto projected = s.projection.execute(clips, viewport); result.timings.projection_ms = elapsed(began);
    require(projected.execution_device == result.execution_device);
    result.input_bytes += 6*16+24; result.output_bytes += 6*24+2*16; ++result.provider_requests;
    const std::array<std::array<uint32_t,2>,6> corners{{{0,0},{s.width,0},{s.width,s.height},{s.width,s.height},{0,s.height},{0,0}}};
    std::array<double,2> origin{};
    for (size_t i = 0; i < 6; ++i) {
        const auto &p = projected.vertices[i];
        for (size_t c = 0; c < 2; ++c) {
            require(p.screen[c] == float(corners[i][c]) && p.snapped[c] == int32_t(corners[i][c]*256));
            float uv = 0; std::memcpy(&uv, &shaded.vertices[i][s.pixel.uv][c], 4); require(std::isfinite(uv));
            const double texel = double(uv) * (c ? source.height : source.width);
            if (!i) origin[c] = std::round(texel);
            // The affine quad has unit clip w. Endpoint error <= 1/1024 texel
            // and bounded FP32 interpolation stay far from half-texel cell
            // boundaries, so point selection equals this integer crop.
            require(std::abs(texel - (origin[c]+corners[i][c])) <= 1.0/1024.0);
        }
    }
    require(origin[0] >= 0 && origin[1] >= 0 && origin[0]+s.width <= source.width && origin[1]+s.height <= source.height);
    began = Clock::now(); auto cropped = s.crop.execute(uint32_t(origin[0]),uint32_t(origin[1]),source.pixels);
    result.timings.merger_ms = elapsed(began); require(cropped.execution_device == result.execution_device);
    result.input_bytes += uint64_t(source.pixels.size())*4; result.output_bytes += uint64_t(cropped.pixels.size())*4; ++result.provider_requests;
    result.pixels = std::move(cropped.pixels); result.vertices = std::move(shaded.vertices);
    result.covered_samples = uint64_t(s.width)*s.height; result.tile_executions = 1; result.used_rectangle = true; result.timings.total_ms = elapsed(start); return result;
}
}
