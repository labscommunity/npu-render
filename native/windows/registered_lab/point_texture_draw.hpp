#pragma once
#include "uniform_draw.hpp"
#include "texture_crop.hpp"
namespace nr::raster {
struct PackedTexture {
    uint32_t width = 0, height = 0, format = 0;
    std::vector<PackedColor> pixels;
};
// Checked fusion of SAMPLE, alpha=1, multiply-by-white on a full-target 1:1
// quad. It rejects scaling, blending, partial coverage and other shader forms.
class PointTextureDrawExecutor {
    struct State;
    std::unique_ptr<State> state;
public:
    static void validate_shader(const shader::Program &pixel);
    PointTextureDrawExecutor(const shader::Program &vertex, const shader::Program &pixel,
        uint32_t width, uint32_t height, uint32_t format, uint32_t source_width,
        uint32_t source_height, uint32_t source_format, const std::string &backend);
    ~PointTextureDrawExecutor();
    PointTextureDrawExecutor(const PointTextureDrawExecutor &) = delete;
    PointTextureDrawExecutor &operator=(const PointTextureDrawExecutor &) = delete;
    UniformDrawExecutor::Result execute(const std::vector<shader::VertexRegisters> &inputs,
        const shader::VertexConstants &constants, const Viewport &viewport, const PackedTexture &source);
};
}
