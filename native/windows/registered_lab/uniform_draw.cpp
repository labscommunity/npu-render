#include "uniform_draw.hpp"
#include "solid_rectangle.hpp"
#include "color_interpolator.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
namespace nr::raster {
namespace {
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::time_point begin) { return std::chrono::duration<double, std::milli>(Clock::now() - begin).count(); }
void require(bool value) { if (!value) throw std::invalid_argument("unsupported uniform triangle draw"); }
}
struct UniformDrawExecutor::State {
    shader::Program vs, ps;
    uint32_t count, width, height, batch;
    bool blending, bgra;
    CullMode cull;
    bool front_ccw;
    uint32_t color_input = 16;
    std::string backend;
    std::unique_ptr<SolidRectangleExecutor> rectangle, blended_rectangle;
    std::unique_ptr<shader::VertexExecutor> vertex;
    std::unique_ptr<shader::PixelExecutor> pixel;
    std::unique_ptr<ProjectionExecutor> projection;
    std::unique_ptr<CoverageExecutor> coverage;
    std::unique_ptr<OutputMerger> merger;
    std::unique_ptr<ColorInterpolator> interpolator;
    State(const shader::Program &v, const shader::Program &p, uint32_t n, uint32_t w, uint32_t h,
          const std::string &device, bool blend, bool swizzle, uint8_t mask, uint32_t b, CullMode c, bool ccw)
        : vs(v), ps(p), count(n), width(w), height(h), batch(b), blending(blend), bgra(swizzle), cull(c), front_ccw(ccw), backend(device) {
        require(vs.stage == shader::Stage::vertex && ps.stage == shader::Stage::pixel && count && count <= 65536 && !(count % 3));
        require(width && width <= 8192 && height && height <= 8192 && uint64_t(width) * height <= 8 * 1024 * 1024);
        require(batch && batch <= 4096 && !(uint64_t(width) * height % batch) && vs.position_output < 16 && ps.outputs[0] == 15);
        for (size_t i = 1; i < ps.outputs.size(); ++i) require(!ps.outputs[i]);
        for (const auto used : ps.textures) require(!used);
        for (const auto &instruction : ps.instructions) for (unsigned i = 0; i < instruction.source_count; ++i)
            require(instruction.sources[i].kind != shader::Register::input || instruction.sources[i].index != ps.position_input);
        for (unsigned r = 0; r < 16; ++r) if (r != ps.position_input)
            require((vs.outputs[r] & ps.inputs[r]) == ps.inputs[r]);
        vertex = std::make_unique<shader::VertexExecutor>(vs, count, backend);
        projection = std::make_unique<ProjectionExecutor>(count, backend);
        pixel = std::make_unique<shader::PixelExecutor>(ps, 1, backend);
        coverage = std::make_unique<CoverageExecutor>(batch, backend, cull, front_ccw);
        merger = std::make_unique<OutputMerger>(batch, backend, blend, bgra, mask);
        if (ps.instructions.size() == 1) {
            const auto &i=ps.instructions[0];const auto &a=i.sources[0];
            if(i.opcode==shader::Opcode::move && !i.saturate && i.source_count==1 &&
               i.destination.kind==shader::Register::output && !i.destination.index && i.destination.mask==15 &&
               a.kind==shader::Register::input && a.index!=ps.position_input && a.modifier==shader::Modifier::none &&
               !a.relative.enabled && a.lanes==std::array<uint32_t,4>{0,1,2,3} &&
               (ps.interpolation[a.index]==2 || ps.interpolation[a.index]==4)) color_input=a.index;
        }
        if (count == 6 && mask == 15 && !(width % 4) && uint64_t(width) * height <= 4 * 1024 * 1024)
            rectangle = std::make_unique<SolidRectangleExecutor>(width, height, backend, bgra);
    }
};
UniformDrawExecutor::UniformDrawExecutor(const shader::Program &vs, const shader::Program &ps, uint32_t count,
    uint32_t width, uint32_t height, const std::string &backend, bool blend, bool bgra, uint8_t mask, uint32_t batch, CullMode cull, bool front_ccw)
    : state(std::make_unique<State>(vs, ps, count, width, height, backend, blend, bgra, mask, batch, cull, front_ccw)) {}
UniformDrawExecutor::~UniformDrawExecutor() = default;
UniformDrawExecutor::Result UniformDrawExecutor::execute(const std::vector<shader::VertexRegisters> &inputs,
    const shader::VertexConstants &vc, const shader::VertexConstants &pc, const Viewport &viewport,
    const std::vector<PackedColor> &background, const std::optional<std::array<int32_t,4>> &scissor) {
    auto &s = *state; const auto start = Clock::now(); Result result{};
    require(inputs.size() == s.count && background.size() == size_t(s.width) * s.height &&
        viewport.x == 0 && viewport.y == 0 && viewport.width == float(s.width) && viewport.height == float(s.height));
    const std::array<int32_t,4> full{0,0,int32_t(s.width),int32_t(s.height)};
    const auto clip = scissor.value_or(full);
    require(clip[0] >= 0 && clip[1] >= 0 && clip[0] <= clip[2] && clip[1] <= clip[3] && clip[2] <= full[2] && clip[3] <= full[3]);
    auto began = Clock::now(); auto shaded = s.vertex->execute(inputs, vc);
    result.timings.shader_ms = milliseconds(began); result.execution_device = shaded.execution_device;
    result.input_bytes = shaded.input_bytes; result.output_bytes = shaded.output_bytes; ++result.provider_requests;
    std::vector<ClipPosition> clips(s.count);
    for (size_t i = 0; i < clips.size(); ++i) std::memcpy(clips[i].data(), shaded.vertices[i][s.vs.position_output].data(), 16);
    began = Clock::now(); const auto projected = s.projection->execute(clips, viewport);
    result.input_bytes += uint64_t(s.count) * 16 + 24; result.output_bytes += uint64_t(s.count) * 24 + uint64_t(s.count/3)*16; ++result.provider_requests;
    result.timings.projection_ms = milliseconds(began); require(projected.execution_device == result.execution_device);
    require(projected.triangle_bounds.size()==s.count/3);
    shader::VertexRegisters uniform{};
    bool uniform_inputs=true;
    for (unsigned r = 0; r < 16; ++r) if (s.ps.inputs[r] && r != s.ps.position_input) {
        uniform[r] = shaded.vertices[0][r];
        for (const auto &v : shaded.vertices) for (unsigned c = 0; c < 4; ++c) if (s.ps.inputs[r] & (1u << c))
            uniform_inputs = uniform_inputs && v[r][c] == uniform[r][c];
    }
    if(uniform_inputs) {
        began = Clock::now(); const auto fragment = s.pixel->execute(std::vector<shader::VertexRegisters>(1, uniform), pc);
        result.timings.shader_ms += milliseconds(began); require(fragment.execution_device == result.execution_device);
        result.input_bytes += fragment.input_bytes; result.output_bytes += fragment.output_bytes; ++result.provider_requests;
        result.fragment = fragment.vertices[0][0];
    } else {
        require(s.color_input<16);
        if(!s.interpolator)s.interpolator=std::make_unique<ColorInterpolator>(s.batch,s.backend,s.ps.interpolation[s.color_input]==2);
    }
    Color color{}; std::memcpy(color.data(), result.fragment.data(), 16);
    const auto &p = projected.vertices;
    // Admit precisely the two nonoverlapping triangles of an axis-aligned quad.
    // This is a shape check. Pixel coverage and all color writes remain on device.
    const bool needs_blend = s.blending && color[3] != 1;
    const bool first_order = s.count == 6 && p[0].snapped == p[5].snapped && p[2].snapped == p[3].snapped;
    const bool second_order = s.count == 6 && p[0].snapped == p[3].snapped && p[2].snapped == p[4].snapped;
    const size_t bottom_left = second_order ? 5 : 4;
    const bool clockwise_survives = s.cull==CullMode::none || ((s.cull==CullMode::back)!=s.front_ccw);
    if (uniform_inputs && clockwise_survives && s.rectangle && (!needs_blend || SolidRectangleExecutor::supports_blend_color(color)) && (first_order || second_order) &&
        std::all_of(color.begin(), color.end(), [](float c) { return c >= 0 && c <= 1; }) &&
        p[0].snapped[1] == p[1].snapped[1] && p[1].snapped[0] == p[2].snapped[0] &&
        p[2].snapped[1] == p[bottom_left].snapped[1] && p[bottom_left].snapped[0] == p[0].snapped[0] &&
        p[0].snapped[0] < p[2].snapped[0] && p[0].snapped[1] < p[2].snapped[1]) {
        began = Clock::now();
        if (needs_blend && !s.blended_rectangle)
            s.blended_rectangle = std::make_unique<SolidRectangleExecutor>(s.width,s.height,s.backend,s.bgra,true);
        auto &executor = needs_blend ? s.blended_rectangle : s.rectangle;
        auto rect = executor->execute({p[0].snapped[0], p[0].snapped[1], p[2].snapped[0], p[2].snapped[1]}, color, background, clip);
        result.timings.merger_ms = milliseconds(began); require(rect.execution_device == result.execution_device);
        result.pixels = std::move(rect.pixels); result.covered_samples = rect.covered_samples;
        result.input_bytes += uint64_t(background.size()) * 4 + 48; result.output_bytes += uint64_t(background.size()) * 5; ++result.provider_requests;
        result.tile_executions = 1; result.used_rectangle = true; result.vertices = std::move(shaded.vertices);
        result.timings.total_ms = milliseconds(start); return result;
    }
    std::vector<Color> colors(s.batch, color);
    const std::array<int32_t,4> fixed_clip{clip[0]*256,clip[1]*256,clip[2]*256,clip[3]*256};
    // The caller's target is unchanged unless it explicitly commits this result.
    result.pixels = background;
    std::vector<Sample> samples(s.batch); std::vector<PackedColor> tile(s.batch);std::vector<Point> points(s.batch);
    // Pack the NPU's triangle bounds intersected with the explicit scissor.
    // Exact coverage, clipping and culling remain device operations.
    for (size_t t = 0; t < clips.size(); t += 3) {
      const auto &bounds=projected.triangle_bounds[t/3];
      const std::array<int32_t,4> region{(std::max)(clip[0],bounds[0]),(std::max)(clip[1],bounds[1]),
          (std::min)(clip[2],bounds[2]),(std::min)(clip[3],bounds[3])};
      if(region[0]>=region[2] || region[1]>=region[3])continue;
      const size_t region_width=size_t(region[2]-region[0]),region_pixels=region_width*size_t(region[3]-region[1]);
      for (size_t first = 0; first < region_pixels; first += s.batch) {
        const auto valid=(std::min)(size_t(s.batch),region_pixels-first);
        const auto pixel_index=[&](size_t n){const auto i=first+n;return (size_t(region[1])+i/region_width)*s.width+size_t(region[0])+i%region_width;};
        for (size_t i = 0; i < s.batch; ++i) {
            for (size_t v = 0; v < 3; ++v) samples[i].triangle[v] = projected.vertices[t + v].snapped;
            if(i<valid){const auto index=pixel_index(i);samples[i].point={int32_t(index%s.width)*256+128,int32_t(index/s.width)*256+128};}
            else samples[i].point={-128,-128};
            points[i]=samples[i].point;
        }
        began = Clock::now(); const auto selected = s.coverage->execute(samples,fixed_clip);
        result.input_bytes += uint64_t(s.batch) * 32 + 16; result.output_bytes += uint64_t(s.batch) * 4; ++result.provider_requests;
        result.timings.coverage_ms += milliseconds(began); require(selected.execution_device == result.execution_device);
        for (const auto value : selected.covered) result.covered_samples += value;
        if(std::none_of(selected.covered.begin(),selected.covered.end(),[](auto v){return v!=0;})){++result.tile_executions;continue;}
        for(size_t i=0;i<s.batch;++i)tile[i]=i<valid?result.pixels[pixel_index(i)]:PackedColor{};
        if(!uniform_inputs) {
            std::array<Color,3> vertex_colors{};
            for(size_t v=0;v<3;++v)std::memcpy(vertex_colors[v].data(),shaded.vertices[t+v][s.color_input].data(),16);
            began=Clock::now();const auto interpolated=s.interpolator->execute({p[t],p[t+1],p[t+2]},vertex_colors,points,selected.covered);
            require(interpolated.execution_device==result.execution_device);colors=interpolated.colors;
            result.timings.shader_ms+=milliseconds(began);result.input_bytes+=uint64_t(s.batch)*12+84;
            result.output_bytes+=uint64_t(s.batch)*16;++result.provider_requests;
        }
        began = Clock::now(); const auto merged = s.merger->execute(colors, tile, selected.covered);
        result.input_bytes += uint64_t(s.batch) * 24; result.output_bytes += uint64_t(s.batch) * 4; ++result.provider_requests;
        result.timings.merger_ms += milliseconds(began); require(merged.execution_device == result.execution_device);
        for(size_t i=0;i<valid;++i)result.pixels[pixel_index(i)]=merged.pixels[i];
        ++result.tile_executions;
      }
    }
    result.vertices = std::move(shaded.vertices); result.timings.total_ms = milliseconds(start); return result;
}
}
