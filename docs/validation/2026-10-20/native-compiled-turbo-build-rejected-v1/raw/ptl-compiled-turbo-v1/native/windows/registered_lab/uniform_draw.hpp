#pragma once
#include "vertex_executor.hpp"
#include "projection_executor.hpp"
#include "output_merger.hpp"
#include <optional>
namespace nr::raster {
// Reusable triangle pipeline: uniform shader inputs or an interpolated float4
// passthrough pixel shader. Unsupported shader interpolation fails closed.
// Host code supplies decoded vertex data and copies bytes; all five rendering
// stages execute on the requested provider. No display or transport ownership.
class UniformDrawExecutor {
    struct State;
    std::unique_ptr<State> state;
public:
    // Region tails are padded and clipped on device. Surface area need not be
    // divisible by the raster batch; tiny surfaces only reduce unused capacity.
    static uint32_t raster_batch(uint32_t width, uint32_t height, uint32_t maximum = 4096);
    UniformDrawExecutor(const shader::Program &vertex, const shader::Program &pixel,
                        uint32_t vertices, uint32_t width, uint32_t height,
                        const std::string &backend, bool blend, bool bgra, uint8_t write_mask,
                        uint32_t batch = 4096, CullMode cull = CullMode::none, bool front_ccw = false);
    UniformDrawExecutor(const shader::Program &vertex, const shader::Program &pixel,
                        uint32_t vertices, uint32_t width, uint32_t height,
                        const std::string &backend, BlendMode blend, bool bgra, uint8_t write_mask,
                        uint32_t batch = 4096, CullMode cull = CullMode::none, bool front_ccw = false);
    ~UniformDrawExecutor();
    UniformDrawExecutor(const UniformDrawExecutor &) = delete;
    UniformDrawExecutor &operator=(const UniformDrawExecutor &) = delete;
    struct Timings { double shader_ms = 0, projection_ms = 0, coverage_ms = 0, merger_ms = 0, total_ms = 0; };
    struct Result {
        std::vector<PackedColor> pixels;
        std::vector<shader::VertexRegisters> vertices;
        shader::VertexBits fragment;
        std::string execution_device;
        uint64_t covered_samples = 0, tile_executions = 0;
        Timings timings;
        bool used_rectangle = false;
        uint64_t input_bytes = 0, output_bytes = 0, provider_requests = 0;
    };
    Result execute(const std::vector<shader::VertexRegisters> &vertices,
                   const shader::VertexConstants &vertex_constants,
                   const shader::VertexConstants &pixel_constants,
                   const Viewport &viewport, const std::vector<PackedColor> &background,
                   const std::optional<std::array<int32_t,4>> &scissor = std::nullopt,
                   const Color &blend_constant = Color{1,1,1,1});
};
}
