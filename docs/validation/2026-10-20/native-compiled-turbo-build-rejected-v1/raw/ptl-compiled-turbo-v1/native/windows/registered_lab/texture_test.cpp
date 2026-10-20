#include "shader_contract.hpp"
#include "shader_reference.hpp"
#include "vertex_executor.hpp"
#include <cstdio>
#include <fstream>
#include <limits>
#include <string>
namespace {
using namespace nr::shader;
void test(bool value) { if (!value) throw std::runtime_error("texture program test failed"); }
void run(const std::vector<uint32_t> &words, const std::string &backend, bool captured) {
    const auto program = parse_pixel(words.data(), words.size());
    unsigned uv = 16, color = 16, texture = 16, sampler = 16;
    for (const auto &instruction : program.instructions) if (instruction.opcode == Opcode::sample) {
        test(uv == 16 && instruction.sources[0].kind == Register::input);
        uv = instruction.sources[0].index; texture = instruction.sources[1].index; sampler = instruction.sources[2].index;
    }
    for (unsigned i = 0; i < 16; ++i) if (program.inputs[i] == 15 && i != uv) { test(color == 16); color = i; }
    test(uv < 16 && color < 16 && texture < 16 && sampler < 16);
    for (size_t n = 0; n < words.size(); ++n) {
        auto truncated = std::vector<uint32_t>(words.begin(), words.begin() + n);
        if (n > 1) truncated[1] = uint32_t(n);
        bool rejected = false;
        try { (void)parse_pixel(truncated.data(), truncated.size()); } catch (const std::invalid_argument &) { rejected = true; }
        test(rejected);
    }
    TextureShapes shapes{}; shapes[texture] = {17, 13};
    TexturePixels textures{}; textures[texture].resize(17 * 13);
    std::vector<VertexRegisters> inputs(256);
    const float coordinates[] = {-.25f, 0, .5f / 17, 1.0f / 17, .125f, .25f, .33333334f, .5f, .75f, .999f, 1, 1.25f,
        (std::numeric_limits<float>::max)(), -(std::numeric_limits<float>::max)()};
    constexpr unsigned coordinate_count = unsigned(sizeof(coordinates) / sizeof(coordinates[0]));
    for (unsigned i = 0; i < inputs.size(); ++i) {
        inputs[i][uv] = {as_bits(coordinates[i % coordinate_count]), as_bits(coordinates[(i / coordinate_count) % coordinate_count]), 0, 0};
        inputs[i][color] = {as_bits(.5f), as_bits(.25f), as_bits(.75f), as_bits(1.0f)};
    }
    bool rejected = false;
    try { PixelExecutor missing(program, uint32_t(inputs.size()), backend); } catch (const std::invalid_argument &) { rejected = true; }
    test(rejected);
    for (const auto filter : {TextureFilter::point, TextureFilter::linear}) {
        TextureSamplers samplers{}; samplers[sampler].filter = filter;
        PixelExecutor executor(program, uint32_t(inputs.size()), backend, shapes, samplers);
        for (unsigned pass = 0; pass < 2; ++pass) {
            for (unsigned t = 0; t < textures[texture].size(); ++t)
                for (unsigned c = 0; c < 4; ++c) textures[texture][t][c] = float((t * 37 + c * 59 + pass * 83) % 256) / 255;
            const auto result = executor.execute(inputs, {}, textures);
            float maximum_error = 0;
            for (unsigned i = 0; i < inputs.size(); ++i) {
                const auto expected = reference_vertex(program, inputs[i], {}, shapes, samplers, textures);
                for (unsigned r = 0; r < 16; ++r) for (unsigned c = 0; c < 4; ++c) if (program.outputs[r] & (1u << c)) {
                    const auto actual = as_float(result.vertices[i][r][c]), wanted = as_float(expected[r][c]);
                    const auto error = std::abs(actual - wanted); maximum_error = (std::max)(maximum_error, error);
                    if (!std::isfinite(actual) || error > 1e-6f || (filter == TextureFilter::point && result.vertices[i][r][c] != expected[r][c])) {
                        std::fprintf(stderr, "texture mismatch pass=%u sample=%u lane=%u actual=%.9g expected=%.9g\n", pass, i, c, double(actual), double(wanted));
                        test(false);
                    }
                }
            }
            std::printf("{\"texture_sampling\":true,\"captured_stream\":%s,\"shader_words\":%zu,\"filter\":\"%s\",\"pass\":%u,\"samples\":256,\"maximum_absolute_error\":%.9g,\"execution_device\":\"%s\",\"desktop_offload_tested\":false}\n",
                captured ? "true" : "false", words.size(), filter == TextureFilter::point ? "point" : "linear", pass, double(maximum_error), result.execution_device.c_str());
        }
        // The provider cannot silently sanitize active nonfinite coordinates.
        auto invalid = inputs; invalid[3][uv][0] = as_bits(std::numeric_limits<float>::quiet_NaN());
        rejected = false;
        try { (void)executor.execute(invalid, {}, textures); } catch (const std::runtime_error &) { rejected = true; }
        test(rejected);
        PixelExecutor infinity_executor(program, uint32_t(inputs.size()), backend, shapes, samplers);
        invalid = inputs; invalid[7][uv][1] = as_bits(std::numeric_limits<float>::infinity());
        rejected = false;
        try { (void)infinity_executor.execute(invalid, {}, textures); } catch (const std::runtime_error &) { rejected = true; }
        test(rejected);
        std::printf("{\"nonfinite_coordinates_rejected\":true,\"filter\":\"%s\",\"execution_device\":\"%s\"}\n", filter == TextureFilter::point ? "point" : "linear", backend.c_str());
    }
}
void branch_test(const std::string &backend) {
    const auto words = nr::registered_lab::shader_tokens(
        "cbuffer Mode:register(b0){float4 mode;}Texture2D image:register(t0);SamplerState filter:register(s0);"
        "float4 main(float2 uv:TEXCOORD):SV_Target{float4 result;[branch] if(mode.x>.5) result=image.Sample(filter,uv);else result=float4(.125,.25,.5,1);return result;}", "ps_4_0");
    const auto program = parse_pixel(words.data(), words.size());
    unsigned branches = 0; for (const auto &i : program.instructions) if (i.opcode == Opcode::begin_if) ++branches;
    test(branches != 0);
    TextureShapes shapes{}; shapes[0] = {8, 4}; TexturePixels textures{}; textures[0].resize(32, {.75f, .5f, .25f, 1});
    std::vector<VertexRegisters> inputs(32); VertexConstants constants; constants[0].resize(1);
    PixelExecutor executor(program, uint32_t(inputs.size()), backend, shapes);
    for (unsigned pass = 0; pass < 2; ++pass) {
        constants[0][0][0] = as_bits(float(pass));
        for (auto &input : inputs) input[0] = {as_bits(pass ? .25f : std::numeric_limits<float>::quiet_NaN()),
            as_bits(pass ? .75f : std::numeric_limits<float>::infinity()), 0, 0};
        const auto result = executor.execute(inputs, constants, textures);
        for (unsigned i = 0; i < inputs.size(); ++i)
            test(result.vertices[i][0] == reference_vertex(program, inputs[i], constants, shapes, {}, textures)[0]);
    }
    std::printf("{\"conditional_texture_sampling\":true,\"inactive_nonfinite_coordinates_ignored\":true,\"samples\":64,\"execution_device\":\"%s\",\"desktop_offload_tested\":false}\n", backend.c_str());
}
}
int main(int argc, char **argv) {
    try {
        const std::string backend = argc >= 3 && std::string(argv[1]) == "--backend" ? argv[2] : "CPU";
        test(argc == 1 || argc == 3 || (argc == 5 && std::string(argv[3]) == "--stream"));
        const auto words = nr::registered_lab::shader_tokens(
            "Texture2D image:register(t0);SamplerState filter:register(s0);float4 main(float2 uv:TEXCOORD,float4 color:COLOR):SV_Target{return image.Sample(filter,uv)*color;}", "ps_4_0");
        run(words, backend, false);
        branch_test(backend);
        if (argc == 5) {
            std::ifstream file(argv[4], std::ios::binary | std::ios::ate); test(bool(file));
            const auto size = file.tellg(); test(size >= 12 && size <= 65536 && size % 4 == 0);
            std::vector<uint32_t> captured(size_t(size) / 4); file.seekg(0); file.read(reinterpret_cast<char *>(captured.data()), size); test(bool(file));
            run(captured, backend, true);
        }
        return 0;
    } catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
