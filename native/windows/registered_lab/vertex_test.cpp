#include "shader_contract.hpp"
#include "shader_reference.hpp"
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
int main() {
    try {
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
        std::puts("vertex parser and bit-preserving reference passed; no NPU execution tested");
        return 0;
    } catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
