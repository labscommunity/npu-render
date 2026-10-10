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
void test_branches(const std::string &backend) {
    const char *text = R"(
cbuffer Mode {float4 mode;};
struct O {float4 p:SV_Position;float4 n:NORMAL;};
O main(float4 v:POSITION) {
    O o; float3 q;
    [branch] if(mode.w>0.5) {
        q=float3(dot(v.xy,mode.xy),dot(v,float4(1,2,3,4)),min(max(sign(v.w),-0.5),0.5));
        [branch] if(v.x<0) q=q/2; else q=q*2;
    } else {q=v.xyz/float3(2,4,8);}
    o.p=float4(q,1);o.n=float4(normalize(v.xyz),sign(v.w));return o;
})";
    const auto words = nr::registered_lab::shader_tokens(text, "vs_4_0");
    const auto program = parse_vertex(words.data(), words.size());
    unsigned branches = 0, dot2 = 0;
    for (const auto &instruction : program.instructions) {
        if (instruction.opcode == Opcode::begin_if) ++branches;
        if (instruction.opcode == Opcode::dot2) ++dot2;
    }
    test(branches >= 2 && dot2);
    for (size_t i = 0; i < words.size(); ++i) {
        std::vector<uint32_t> prefix(words.begin(), words.begin() + i);
        if (i > 1) prefix[1] = uint32_t(i); rejects(prefix);
    }
    std::vector<VertexRegisters> input(32);
    for (unsigned i = 0; i < input.size(); ++i)
        input[i][0] = {as_bits(i % 2 ? -3.0f : 3.0f), as_bits(4.0f), as_bits(0.0f), as_bits(float(int(i % 3) - 1))};
    VertexExecutor executor(program, uint32_t(input.size()), backend);
    VertexConstants constants; constants[0] = {{as_bits(0.25f), as_bits(0.5f), as_bits(0.125f), 0}};
    float maximum_error = 0;
    for (unsigned pass = 0; pass < 2; ++pass) {
        constants[0][0][3] = as_bits(float(pass));
        const auto result = executor.execute(input, constants);
        for (unsigned i = 0; i < input.size(); ++i) {
            const auto expected = reference_vertex(program, input[i], constants);
            for (unsigned r = 0; r < 16; ++r) for (unsigned c = 0; c < 4; ++c) if (program.outputs[r] & (1u << c)) {
                const auto actual = as_float(result.vertices[i][r][c]), wanted = as_float(expected[r][c]);
                const auto error = std::abs(actual - wanted); maximum_error = (std::max)(maximum_error, error);
                if (!std::isfinite(actual) || error > 1e-6f * (1 + std::abs(wanted))) {
                    std::fprintf(stderr, "branch mismatch pass=%u vertex=%u output=%u lane=%u actual=%.9g expected=%.9g\n",
                        pass, i, r, c, double(actual), double(wanted)); test(false);
                }
            }
            if (!pass) test(as_float(result.vertices[i][program.position_output][0]) == (i % 2 ? -1.5f : 1.5f));
        }
    }
    std::printf("{\"conditional_vertex_program\":true,\"vertices\":64,\"branches\":%u,\"maximum_absolute_error\":%.9g,\"execution_device\":\"%s\",\"desktop_offload_tested\":false}\n",
        branches, double(maximum_error), backend.c_str());
}
void test_math_precision(const std::string &backend) {
    const auto words = nr::registered_lab::shader_tokens(
        "float4 main(float4 v:POSITION):SV_Position {float q=v.z*v.w;return float4(v.x*v.y-q,min(v.x,v.z),max(v.x,v.z),v.w);}", "vs_4_0");
    const auto program = parse_vertex(words.data(), words.size());
    std::vector<VertexRegisters> input(256);
    for (unsigned i = 0; i < input.size(); ++i) {
        const float value = 9285.84375f + float(i) * 31.125f;
        input[i][0] = {as_bits(i % 2 ? -value : value), as_bits(0.125f), as_bits(0.07123f), as_bits(1.0f)};
    }
    VertexExecutor executor(program, uint32_t(input.size()), backend);
    const auto result = executor.execute(input, {});
    for (unsigned i = 0; i < input.size(); ++i) {
        const auto expected = reference_vertex(program, input[i], {});
        for (unsigned c = 0; c < 4; ++c) if (result.vertices[i][program.position_output][c] != expected[program.position_output][c]) {
            std::fprintf(stderr, "math precision mismatch vertex=%u lane=%u actual=%08x expected=%08x\n",
                i, c, result.vertices[i][program.position_output][c], expected[program.position_output][c]); test(false);
        }
    }
    std::printf("{\"math_precision\":true,\"vertices\":256,\"exact_bits_verified\":true,\"execution_device\":\"%s\",\"desktop_offload_tested\":false}\n", backend.c_str());
}
}
int main(int argc, char **argv) {
    try {
        std::string backend = "CPU";
        if (argc == 3 && !std::strcmp(argv[1], "--backend")) backend = argv[2];
        else test(argc == 1);
        test_branches(backend);
        test_math_precision(backend);
        const auto modifier_tokens = nr::registered_lab::shader_tokens(
            "struct O {float4 p:SV_Position;float4 a:COLOR;};O main(float4 v:POSITION){O o;o.p=-v;o.a=-abs(v);return o;}", "vs_4_0");
        const auto modifier_program = parse_vertex(modifier_tokens.data(), modifier_tokens.size());
        std::vector<VertexRegisters> modifier_vertices(16);
        for (unsigned i = 0; i < modifier_vertices.size(); ++i)
            modifier_vertices[i][0] = {as_bits(0.0f), as_bits(-0.0f), as_bits(-float(i + 1) / 8), as_bits(float(i + 1) / 16)};
        VertexExecutor modifier_executor(modifier_program, uint32_t(modifier_vertices.size()), backend);
        const auto modified = modifier_executor.execute(modifier_vertices, {});
        for (unsigned i = 0; i < modifier_vertices.size(); ++i) {
            const auto expected = reference_vertex(modifier_program, modifier_vertices[i], {});
            for (unsigned slot = 0; slot < 16; ++slot) if (modifier_program.outputs[slot]) {
                if (modified.vertices[i][slot] != expected[slot]) {
                    for (unsigned c = 0; c < 4; ++c) std::fprintf(stderr,
                        "modifier mismatch vertex=%u output=%u lane=%u actual=%08x expected=%08x\n",
                        i, slot, c, modified.vertices[i][slot][c], expected[slot][c]);
                    test(false);
                }
            }
        }
        std::printf("{\"source_modifiers\":true,\"vertices\":16,\"execution_device\":\"%s\",\"signed_zero_verified\":true,\"desktop_offload_tested\":false}\n", modified.execution_device.c_str());
        const auto code = nr::registered_lab::shader_tokens(source, "vs_4_0");
        const auto program = parse_vertex(code.data(), code.size());
        for (const char *unsupported : {
                 "float4 main(float4 p:POSITION):SV_Position{return saturate(p);}",
                 "cbuffer C {float4 data[250];}; float4 main(float4 p:POSITION):SV_Position{return data[asuint(p.x*2)];}"}) {
            const auto tokens = nr::registered_lab::shader_tokens(unsupported, "vs_4_0");
            const auto parsed = parse_vertex(tokens.data(), tokens.size());
            bool lowering_rejected = false;
            try { VertexExecutor::validate(parsed); }
            catch (const std::invalid_argument &) { lowering_rejected = true; }
            test(lowering_rejected);
        }
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
        // Screen transforms and palette values not representable in FP16 catch
        // silent lowering even when an NPU graph declares f32 input/output.
        constants[0][0] = {as_bits(2.0f / 1920), as_bits(-1.0f), as_bits(2.0f / 1080), as_bits(-1.0f)};
        for (unsigned i = 0; i < 4096; ++i)
            constants[1][i] = {as_bits(float(i % 251) / 251), as_bits(0.1234567f), as_bits(0.7654321f), as_bits(1.0f)};
        vertices.resize(4096);
        for (unsigned i = 0; i < vertices.size(); ++i) {
            vertices[i][0] = {as_bits(float(2 * i) / 8), as_bits(float(2 * i + 1) / 8), 0, 0};
            vertices[i][1][0] = i;
        }
        VertexExecutor precise_executor(large_program, uint32_t(vertices.size()), backend);
        const auto precise = precise_executor.execute(vertices, constants);
        for (unsigned i = 0; i < vertices.size(); ++i) {
            test(precise.vertices[i][1] == constants[1][i]);
            const auto &position = precise.vertices[i][large_program.position_output];
            for (unsigned c = 0; c < 2; ++c) {
                const auto x = as_float(vertices[i][0][c]), scale = as_float(constants[0][0][c * 2]);
                volatile float product = x * scale;
                const auto separate = as_bits(product - 1.0f), fused = as_bits(std::fma(x, scale, -1.0f));
                if (position[c] != separate && position[c] != fused) {
                    std::fprintf(stderr, "precision mismatch vertex=%u lane=%u actual=%08x separate=%08x fused=%08x\n", i, c, position[c], separate, fused);
                    test(false);
                }
            }
            test(position[2] == as_bits(0.5f) && position[3] == as_bits(1.0f));
        }
        std::printf("{\"fp32_screen_vertices\":%zu,\"execution_device\":\"%s\",\"palette_bits_exact\":true,\"position_matches_fp32_mad\":true,\"desktop_offload_tested\":false}\n",
            vertices.size(), precise.execution_device.c_str());
        std::puts("vertex parser, reference and explicit provider checks passed");
        return 0;
    } catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
