#include "shader_contract.hpp"
#include "shader_reference.hpp"
#include "vertex_executor.hpp"
#include <cstdio>
#include <cmath>
namespace {
using namespace nr::shader;
void test(bool value) { if (!value) throw std::runtime_error("vertex program test failed"); }
constexpr const char *source = R"(
cbuffer Screen : register(b0) { float4 transform; };
cbuffer Palette : register(b1) { float4 colors[250]; };
struct Vertex { float2 xy : POSITION; uint index : COLOR; };
struct Output { float4 position : SV_Position; float4 color : COLOR; };
Output main(Vertex v) {
    Output o; o.position = float4(v.xy * transform.xz + transform.yw, 0.5, 1.0);
    o.color = colors[v.index]; return o;
})";
void rejects(const std::vector<uint32_t> &code) {
    bool rejected = false;
    try { (void)parse_vertex(code.data(), code.size()); }
    catch (const std::invalid_argument &) { rejected = true; }
    test(rejected);
}
}
int main(int argc, char **argv) {
    try {
        std::string backend = "CPU";
        if (argc == 3 && !std::strcmp(argv[1], "--backend")) backend = argv[2];
        else test(argc == 1);
        const auto code = nr::registered_lab::shader_tokens(source, "vs_4_0");
        const auto program = parse_vertex(code.data(), code.size());
        test(program.constants[0] == 1 && program.constants[1] == 250 && program.dynamic_constants[1]);
        Constants constants; constants[0] = {{as_bits(0.25f), as_bits(-1.0f), as_bits(-0.5f), as_bits(1.0f)}};
        constants[1].resize(250);
        for (unsigned i = 0; i < 250; ++i)
            constants[1][i] = {as_bits(float(i) / 256), as_bits(0.25f), as_bits(0.5f), as_bits(1.0f)};
        for (unsigned i : {0u, 1u, 249u, 250u, 0xffffffffu}) {
            Registers inputs{};
            inputs[0] = {as_bits(6.0f), as_bits(1.5f), 0, 0}; inputs[1][0] = i;
            const auto output = reference_vertex(program, inputs, constants);
            test(output[program.position_output] == Bits{as_bits(0.5f), as_bits(0.25f), as_bits(0.5f), as_bits(1.0f)});
            test(output[1] == (i < 250 ? constants[1][i] : Bits{}));
        }
        // Every truncated prefix and mismatched declared length must fail before
        // reading beyond the supplied range, including the last instruction.
        for (size_t i = 0; i < code.size(); ++i) {
            std::vector<uint32_t> truncated(code.begin(), code.begin() + i);
            if (i > 1) truncated[1] = uint32_t(i);
            rejects(truncated);
        }
        auto malformed = code; malformed[0] = 0x40; rejects(malformed); // Pixel stage.
        malformed = code; malformed[2] |= 0x80000000u; rejects(malformed);
        malformed = code; malformed[2] &= 0x00ffffffu; rejects(malformed); // Zero-length instruction.
        malformed = code; malformed.back() = 0x010000ffu; rejects(malformed); // Unknown opcode.
        malformed = code; malformed.push_back(0x0100003eu); malformed[1] = uint32_t(malformed.size()); rejects(malformed);
        auto too_short = constants; too_short[1].resize(249);
        bool rejected = false;
        try { (void)reference_vertex(program, {}, too_short); }
        catch (const std::invalid_argument &) { rejected = true; }
        test(rejected);
        std::vector<VertexRegisters> vertices(256);
        for (unsigned i = 0; i < vertices.size(); ++i) {
            vertices[i][0] = {as_bits(float(i) / 16), as_bits(-float(i) / 8), 0, 0};
            vertices[i][1][0] = i % 31 == 0 ? 0xffffffffu : i;
        }
        VertexExecutor executor(program, uint32_t(vertices.size()), backend);
        for (unsigned pass = 0; pass < 2; ++pass) {
            if (pass) constants[0][0][1] = as_bits(2.0f);
            const auto result = executor.execute(vertices, constants);
            test(result.vertices.size() == vertices.size());
            for (unsigned i = 0; i < vertices.size(); ++i) {
                const auto expected = reference_vertex(program, vertices[i], constants);
                for (unsigned slot = 0; slot < 16; ++slot) if (program.outputs[slot]) {
                    if (result.vertices[i][slot] != expected[slot]) {
                        for (unsigned c = 0; c < 4; ++c) std::fprintf(stderr,
                            "vertex mismatch pass=%u vertex=%u output=%u lane=%u actual=%08x expected=%08x actual_float=%.9g expected_float=%.9g\n",
                            pass, i, slot, c, result.vertices[i][slot][c], expected[slot][c],
                            double(as_float(result.vertices[i][slot][c])), double(as_float(expected[slot][c])));
                        test(false);
                    }
                }
                test(result.vertices[i][program.position_output][0] == as_bits(float(i) / 64 + (pass ? 2.0f : -1.0f)));
                test(result.vertices[i][program.position_output][1] == as_bits(float(i) / 16 + 1.0f));
            }
            std::printf("{\"vertex_pass\":%u,\"vertices\":%zu,\"execution_device\":\"%s\",\"exact_bits_verified\":true,\"desktop_offload_tested\":false}\n",
                pass, vertices.size(), result.execution_device.c_str());
        }
        // Exercise the largest admitted buffer, including indices that would
        // round incorrectly if bounds comparisons were lowered through fp16.
        std::string large_source(source);
        large_source.replace(large_source.find("[250]"), 5, "[4096]");
        const auto large_code = nr::registered_lab::shader_tokens(large_source.c_str(), "vs_4_0");
        const auto large_program = parse_vertex(large_code.data(), large_code.size());
        constants[1].resize(4096);
        for (unsigned i = 0; i < 4096; ++i)
            constants[1][i] = {as_bits(float(i % 256) / 256), as_bits(0.25f), as_bits(0.5f), as_bits(1.0f)};
        const uint32_t edge_indices[] = {0, 1, 2047, 2048, 2049, 4094, 4095, 4096,
            65535, 0x7fffffff, 0x80000000, 0xffffffff};
        for (unsigned i = 0; i < vertices.size(); ++i)
            vertices[i][1][0] = edge_indices[i % (sizeof(edge_indices) / sizeof(edge_indices[0]))];
        VertexExecutor large_executor(large_program, uint32_t(vertices.size()), backend);
        const auto large_result = large_executor.execute(vertices, constants);
        for (unsigned i = 0; i < vertices.size(); ++i) {
            const auto expected = reference_vertex(large_program, vertices[i], constants);
            for (unsigned slot = 0; slot < 16; ++slot) if (large_program.outputs[slot]) {
                if (large_result.vertices[i][slot] != expected[slot]) {
                    std::fprintf(stderr, "large-buffer mismatch vertex=%u index=%08x output=%u\n", i, vertices[i][1][0], slot);
                    test(false);
                }
            }
        }
        std::printf("{\"constant_buffer_rows\":4096,\"vertices\":%zu,\"execution_device\":\"%s\",\"exact_bits_verified\":true,\"desktop_offload_tested\":false}\n",
            vertices.size(), large_result.execution_device.c_str());
        std::puts("vertex parser, reference and explicit provider checks passed");
        return 0;
    } catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
