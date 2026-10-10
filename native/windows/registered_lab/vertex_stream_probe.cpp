// Qualify a captured token stream without embedding system shader binaries.
#include "vertex_executor.hpp"
#include "shader_reference.hpp"
#include <cstdio>
#include <fstream>
#include <cstring>
int main(int argc, char **argv) {
    using namespace nr::shader;
    try {
        if (argc != 5 || std::strcmp(argv[1], "--shader") || std::strcmp(argv[3], "--backend"))
            throw std::invalid_argument("expected --shader <token-stream> --backend <CPU|GPU|NPU>");
        std::ifstream file(argv[2], std::ios::binary | std::ios::ate);
        if (!file) throw std::runtime_error("shader stream unavailable");
        const auto bytes = file.tellg();
        if (bytes < 12 || bytes > 65536 || bytes % 4) throw std::invalid_argument("invalid shader extent");
        std::vector<uint32_t> words(size_t(bytes) / 4);
        file.seekg(0); file.read(reinterpret_cast<char *>(words.data()), std::streamsize(bytes));
        if (!file) throw std::runtime_error("shader stream read failed");
        const auto program = parse_vertex(words.data(), words.size());
        VertexConstants constants;
        for (unsigned b = 0; b < 14; ++b) {
            constants[b].resize(program.constants[b]);
            for (unsigned row = 0; row < constants[b].size(); ++row)
                for (unsigned c = 0; c < 4; ++c)
                    constants[b][row][c] = as_bits(float(int((b * 13 + row * 23 + c * 7) % 256) - 128) / 128);
        }
        std::vector<VertexRegisters> vertices(256);
        for (unsigned v = 0; v < vertices.size(); ++v)
            for (unsigned r = 0; r < 16; ++r)
                for (unsigned c = 0; c < 4; ++c)
                    vertices[v][r][c] = as_bits(float(int((v * 13 + r * 29 + c * 7) % 1024) - 512) / 8);
        VertexExecutor executor(program, uint32_t(vertices.size()), argv[4]);
        for (unsigned pass = 0; pass < 2; ++pass) {
            if (pass) for (auto &buffer : constants) for (auto &row : buffer) for (auto &value : row)
                value = as_bits(as_float(value) + 1.0f / 64);
            const auto result = executor.execute(vertices, constants);
            size_t checked = 0;
            for (unsigned v = 0; v < vertices.size(); ++v) {
                const auto expected = reference_vertex(program, vertices[v], constants);
                for (unsigned r = 0; r < 16; ++r) for (unsigned c = 0; c < 4; ++c)
                    if (program.outputs[r] & (1u << c)) {
                        if (result.vertices[v][r][c] != expected[r][c]) {
                            std::fprintf(stderr, "vertex mismatch pass=%u vertex=%u register=%u lane=%u actual=%08x expected=%08x\n",
                                pass, v, r, c, result.vertices[v][r][c], expected[r][c]);
                            return 1;
                        }
                        ++checked;
                    }
            }
            std::printf("{\"pass\":%u,\"vertices\":%zu,\"shader_words\":%zu,\"instructions\":%zu,\"checked_components\":%zu,"
                        "\"execution_device\":\"%s\",\"exact_bits_verified\":true,\"desktop_offload_tested\":false}\n",
                        pass, vertices.size(), words.size(), program.instructions.size(), checked, result.execution_device.c_str());
        }
        return 0;
    } catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
