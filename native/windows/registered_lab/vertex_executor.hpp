#pragma once
#include "shader_ir.hpp"
#include "shader_texture.hpp"
#include <memory>
#include <string>
namespace nr::shader {
using VertexBits = std::array<uint32_t, 4>;
using VertexRegisters = std::array<VertexBits, 16>;
using VertexConstants = std::array<std::vector<VertexBits>, 14>;
// Host input decoding preserves both FP32 and integer bit views. Arithmetic and
// indexed constant reads execute on the explicitly selected compiled provider.
// Shared vertex/pixel arithmetic. Inputs are already decoded/interpolated; this
// does not implement rasterization or interpolation. Texture sampling accepts
// only the explicit single-mip, edge-clamped contract in shader_texture.hpp.
class RegisterExecutor {
    struct State;
    std::unique_ptr<State> state;
public:
    // Validate lowering constraints after parse_vertex/parse_pixel, without choosing a
    // device or claiming that a complete graphics pipeline can execute.
    static void validate(const Program &program);
    // Bounded internal expansion for qualified shader specializations.
    static uint32_t maximum_batch(const Program &program);
    RegisterExecutor(const Program &program, uint32_t vertices, const std::string &backend,
                     const TextureShapes &textures = {}, const TextureSamplers &samplers = {});
    ~RegisterExecutor();
    RegisterExecutor(const RegisterExecutor &) = delete;
    RegisterExecutor &operator=(const RegisterExecutor &) = delete;
    struct Result { std::vector<VertexRegisters> vertices; std::string execution_device; uint64_t input_bytes = 0, output_bytes = 0; };
    Result execute(const std::vector<VertexRegisters> &vertices, const VertexConstants &constants,
                   const TexturePixels &textures = {});
};
using VertexExecutor = RegisterExecutor;
using PixelExecutor = RegisterExecutor;
}
