#pragma once
#include "shader_ir.hpp"
#include <memory>
#include <string>
namespace nr::shader {
using VertexBits = std::array<uint32_t, 4>;
using VertexRegisters = std::array<VertexBits, 16>;
using VertexConstants = std::array<std::vector<VertexBits>, 14>;
// Host input decoding preserves both FP32 and integer bit views. Arithmetic and
// indexed constant reads execute on the explicitly selected compiled provider.
// This is a vertex stage, not a rasterizer or a completed desktop renderer.
class VertexExecutor {
    struct State;
    std::unique_ptr<State> state;
public:
    // Validate lowering constraints after parse_vertex, without choosing a
    // device or claiming that a complete graphics pipeline can execute.
    static void validate(const Program &program);
    VertexExecutor(const Program &program, uint32_t vertices, const std::string &backend);
    ~VertexExecutor();
    VertexExecutor(const VertexExecutor &) = delete;
    VertexExecutor &operator=(const VertexExecutor &) = delete;
    struct Result { std::vector<VertexRegisters> vertices; std::string execution_device; };
    Result execute(const std::vector<VertexRegisters> &vertices, const VertexConstants &constants);
};
}
