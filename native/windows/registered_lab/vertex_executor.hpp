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
// only the explicit single-mip addressing contract in shader_texture.hpp.
class RegisterExecutor {
public:
    class TextureBatch;
private:
    TextureBatch *active_batch=nullptr;
    struct State;
    struct Configuration;
    std::unique_ptr<State> state;
    std::unique_ptr<Configuration> configuration;
public:
    // Validate lowering constraints after parse_vertex/parse_pixel, without choosing a
    // device or claiming that a complete graphics pipeline can execute.
    static void validate(const Program &program);
    // Bounded internal expansion for qualified shader specializations.
    static uint32_t maximum_batch(const Program &program);
    // Preferred packed raster shape, independently of the arithmetic ceiling.
    static uint32_t preferred_raster_batch(const Program &program);
    RegisterExecutor(const Program &program, uint32_t vertices, const std::string &backend,
                     const TextureShapes &textures = {}, const TextureSamplers &samplers = {});
    ~RegisterExecutor();
    RegisterExecutor(const RegisterExecutor &) = delete;
    RegisterExecutor &operator=(const RegisterExecutor &) = delete;
    struct Result { std::vector<VertexRegisters> vertices; std::string execution_device; uint64_t input_bytes = 0, output_bytes = 0, texture_upload_bytes=0, texture_reused_bytes=0; };
    Result execute(const std::vector<VertexRegisters> &vertices, const VertexConstants &constants,
                   const TexturePixels &textures = {});
    // Textures must remain immutable and outlive the scope. One scope per
    // executor; all calls are synchronous and occur on the owning caller.
    TextureBatch bind_textures(const TexturePixels &textures,bool reuse=true);
private:
    Result execute_internal(const std::vector<VertexRegisters> &vertices,const VertexConstants &constants,const TexturePixels &textures,bool reuse);
};
class RegisterExecutor::TextureBatch {
    friend class RegisterExecutor;
    RegisterExecutor &owner;const TexturePixels &textures;bool reuse;
    TextureBatch(RegisterExecutor &executor,const TexturePixels &resources,bool reuse);
public:
    ~TextureBatch();
    TextureBatch(const TextureBatch &)=delete;TextureBatch &operator=(const TextureBatch &)=delete;
    TextureBatch(TextureBatch &&)=delete;TextureBatch &operator=(TextureBatch &&)=delete;
    Result execute(const std::vector<VertexRegisters> &vertices,const VertexConstants &constants);
};
using VertexExecutor = RegisterExecutor;
using PixelExecutor = RegisterExecutor;
}
