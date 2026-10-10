#include "uniform_draw.hpp"
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
    std::unique_ptr<shader::VertexExecutor> vertex;
    std::unique_ptr<shader::PixelExecutor> pixel;
    std::unique_ptr<ProjectionExecutor> projection;
    std::unique_ptr<CoverageExecutor> coverage;
    std::unique_ptr<OutputMerger> merger;
    State(const shader::Program &v, const shader::Program &p, uint32_t n, uint32_t w, uint32_t h,
          const std::string &backend, bool blend, bool bgra, uint8_t mask, uint32_t b)
        : vs(v), ps(p), count(n), width(w), height(h), batch(b) {
        require(vs.stage == shader::Stage::vertex && ps.stage == shader::Stage::pixel && count && count <= 65536 && !(count % 3));
        require(width && width <= 8192 && height && height <= 8192 && uint64_t(width) * height <= 8 * 1024 * 1024);
        require(batch && batch <= 65536 && !(uint64_t(width) * height % batch) && vs.position_output < 16 && ps.outputs[0] == 15);
        for (size_t i = 1; i < ps.outputs.size(); ++i) require(!ps.outputs[i]);
        for (const auto used : ps.textures) require(!used);
        for (const auto &instruction : ps.instructions) for (unsigned i = 0; i < instruction.source_count; ++i)
            require(instruction.sources[i].kind != shader::Register::input || instruction.sources[i].index != ps.position_input);
        for (unsigned r = 0; r < 16; ++r) if (r != ps.position_input)
            require((vs.outputs[r] & ps.inputs[r]) == ps.inputs[r]);
        vertex = std::make_unique<shader::VertexExecutor>(vs, count, backend);
        projection = std::make_unique<ProjectionExecutor>(count, backend);
        pixel = std::make_unique<shader::PixelExecutor>(ps, batch, backend);
        coverage = std::make_unique<CoverageExecutor>(batch, backend);
        merger = std::make_unique<OutputMerger>(batch, backend, blend, bgra, mask);
    }
};
UniformDrawExecutor::UniformDrawExecutor(const shader::Program &vs, const shader::Program &ps, uint32_t count,
    uint32_t width, uint32_t height, const std::string &backend, bool blend, bool bgra, uint8_t mask, uint32_t batch)
    : state(std::make_unique<State>(vs, ps, count, width, height, backend, blend, bgra, mask, batch)) {}
UniformDrawExecutor::~UniformDrawExecutor() = default;
UniformDrawExecutor::Result UniformDrawExecutor::execute(const std::vector<shader::VertexRegisters> &inputs,
    const shader::VertexConstants &vc, const shader::VertexConstants &pc, const Viewport &viewport,
    const std::vector<PackedColor> &background) {
    auto &s = *state; const auto start = Clock::now(); Result result{};
    require(inputs.size() == s.count && background.size() == size_t(s.width) * s.height &&
        viewport.x == 0 && viewport.y == 0 && viewport.width == float(s.width) && viewport.height == float(s.height));
    auto began = Clock::now(); auto shaded = s.vertex->execute(inputs, vc);
    result.timings.shader_ms = milliseconds(began); result.execution_device = shaded.execution_device;
    std::vector<ClipPosition> clips(s.count);
    for (size_t i = 0; i < clips.size(); ++i) std::memcpy(clips[i].data(), shaded.vertices[i][s.vs.position_output].data(), 16);
    began = Clock::now(); const auto projected = s.projection->execute(clips, viewport);
    result.timings.projection_ms = milliseconds(began); require(projected.execution_device == result.execution_device);
    shader::VertexRegisters uniform{};
    for (unsigned r = 0; r < 16; ++r) if (s.ps.inputs[r] && r != s.ps.position_input) {
        uniform[r] = shaded.vertices[0][r];
        for (const auto &v : shaded.vertices) for (unsigned c = 0; c < 4; ++c) if (s.ps.inputs[r] & (1u << c)) require(v[r][c] == uniform[r][c]);
    }
    began = Clock::now(); const auto fragment = s.pixel->execute(std::vector<shader::VertexRegisters>(s.batch, uniform), pc);
    result.timings.shader_ms += milliseconds(began); require(fragment.execution_device == result.execution_device);
    result.fragment = fragment.vertices[0][0];
    std::vector<Color> colors(s.batch);
    for (size_t i = 0; i < s.batch; ++i) {
        require(fragment.vertices[i][0] == result.fragment); std::memcpy(colors[i].data(), fragment.vertices[i][0].data(), 16);
    }
    // The caller's target is unchanged unless it explicitly commits this result.
    result.pixels = background;
    std::vector<Sample> samples(s.batch); std::vector<PackedColor> tile(s.batch);
    for (size_t t = 0; t < clips.size(); t += 3) for (size_t first = 0; first < result.pixels.size(); first += s.batch) {
        for (size_t i = 0; i < s.batch; ++i) {
            for (size_t v = 0; v < 3; ++v) samples[i].triangle[v] = projected.vertices[t + v].snapped;
            const auto index = first + i; samples[i].point = {int32_t(index % s.width) * 256 + 128, int32_t(index / s.width) * 256 + 128};
        }
        std::copy_n(result.pixels.data() + first, s.batch, tile.data());
        began = Clock::now(); const auto selected = s.coverage->execute(samples);
        result.timings.coverage_ms += milliseconds(began); require(selected.execution_device == result.execution_device);
        for (const auto value : selected.covered) result.covered_samples += value;
        began = Clock::now(); const auto merged = s.merger->execute(colors, tile, selected.covered);
        result.timings.merger_ms += milliseconds(began); require(merged.execution_device == result.execution_device);
        std::copy(merged.pixels.begin(), merged.pixels.end(), result.pixels.begin() + first); ++result.tile_executions;
    }
    result.vertices = std::move(shaded.vertices); result.timings.total_ms = milliseconds(start); return result;
}
}
