#pragma once
#include "point_texture_draw.hpp"
namespace nr::raster {
// Checked fusion of a white-modulated SAMPLE shader on an axis-aligned,
// unit-clip-w quad. Coverage, affine interpolation, bilinear clamp sampling,
// source-over and byte conversion all execute on the selected provider.
class LinearTextureDrawExecutor {
    struct State;
    std::unique_ptr<State> state;
public:
    static void validate_shader(const shader::Program &pixel);
    LinearTextureDrawExecutor(const shader::Program &vertex, const shader::Program &pixel,
        uint32_t width, uint32_t height, uint32_t format, uint32_t source_width,
        uint32_t source_height, uint32_t source_format, const std::string &backend, bool blend);
    ~LinearTextureDrawExecutor();
    LinearTextureDrawExecutor(const LinearTextureDrawExecutor &) = delete;
    LinearTextureDrawExecutor &operator=(const LinearTextureDrawExecutor &) = delete;
    UniformDrawExecutor::Result execute(const std::vector<shader::VertexRegisters> &inputs,
        const shader::VertexConstants &constants, const Viewport &viewport, const PackedTexture &source,
        const std::vector<PackedColor> &background,
        const std::optional<std::array<int32_t,4>> &scissor = std::nullopt);
};
}
