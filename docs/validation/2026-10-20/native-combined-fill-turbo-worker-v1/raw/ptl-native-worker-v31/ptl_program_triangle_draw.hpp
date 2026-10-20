#pragma once
#include "packed_resources.hpp"
namespace nr::raster {
// General triangle-list coverage with a bounded, single-mip texture shader.
// Vertex count is a capacity; execution accepts complete triangle lists up to
// that capacity and validated target dimensions supplied in the viewport.
// Vertex arithmetic, projection, interpolation, sampling and merging run on
// the selected provider. Flat attributes are transported as unchanged bits.
class PtlProgramTriangleDrawExecutor {
    struct State;
    std::unique_ptr<State> state;
public:
    static void validate_shader(const shader::Program &pixel);
    PtlProgramTriangleDrawExecutor(const shader::Program &vertex,const shader::Program &pixel,
        uint32_t vertices,uint32_t width,uint32_t height,uint32_t format,
        uint32_t source_width,uint32_t source_height,uint32_t source_format,
        const std::string &backend,bool blend,uint8_t mask,shader::TextureFilter filter,
        CullMode cull,bool front_ccw,uint32_t batch=4096);
    PtlProgramTriangleDrawExecutor(const shader::Program &vertex,const shader::Program &pixel,
        uint32_t vertices,uint32_t width,uint32_t height,uint32_t format,
        uint32_t source_width,uint32_t source_height,uint32_t source_format,
        const std::string &backend,BlendMode blend,uint8_t mask,shader::TextureFilter filter,
        CullMode cull,bool front_ccw,uint32_t batch=4096);
    PtlProgramTriangleDrawExecutor(const shader::Program &vertex,const shader::Program &pixel,
        uint32_t vertices,uint32_t width,uint32_t height,uint32_t format,
        const PackedResources &resources,const shader::TextureSamplers &samplers,
        const std::string &backend,bool blend,uint8_t mask,CullMode cull,bool front_ccw,uint32_t batch=4096);
    PtlProgramTriangleDrawExecutor(const shader::Program &vertex,const shader::Program &pixel,
        uint32_t vertices,uint32_t width,uint32_t height,uint32_t format,
        const PackedResources &resources,const shader::TextureSamplers &samplers,
        const std::string &backend,BlendMode blend,uint8_t mask,CullMode cull,bool front_ccw,uint32_t batch=4096);
    ~PtlProgramTriangleDrawExecutor();
    UniformDrawExecutor::Result execute(const std::vector<shader::VertexRegisters> &vertices,
        const shader::VertexConstants &vertex_constants,const shader::VertexConstants &pixel_constants,
        const Viewport &viewport,const PackedResources &resources,const std::vector<PackedColor> &background,
        const std::optional<std::array<int32_t,4>> &scissor=std::nullopt,
        const Color &blend_constant=Color{1,1,1,1});
    UniformDrawExecutor::Result execute(const std::vector<shader::VertexRegisters> &vertices,
        const shader::VertexConstants &vertex_constants,const shader::VertexConstants &pixel_constants,
        const Viewport &viewport,const PackedTexture &source,const std::vector<PackedColor> &background,
        const std::optional<std::array<int32_t,4>> &scissor=std::nullopt,
        const Color &blend_constant=Color{1,1,1,1});
};
}
