#include "uniform_draw.hpp"
#include "shader_contract.hpp"
#include "shader_reference.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace nr::raster;
using namespace nr::shader;
namespace {
void test(bool value) { if (!value) throw std::runtime_error("uniform draw contract failed"); }
std::vector<VertexRegisters> quad(float end, bool degenerate) {
    const float xy[4][2]{{-1, 1}, {end, 1}, {end, -end}, {-1, -end}};
    const unsigned order[]{0, 1, 2, 2, 3, 0}; std::vector<VertexRegisters> result(6);
    for (unsigned i = 0; i < 6; ++i) {
        const auto index = degenerate ? 0 : order[i];
        result[i][0] = {as_bits(xy[index][0]), as_bits(xy[index][1]), as_bits(.5f), as_bits(1.f)};
        result[i][1] = {as_bits(1.f), as_bits(1.f), as_bits(1.f), as_bits(1.f)};
    }
    return result;
}
}
int main(int argc, char **argv) try {
    const std::string backend = argc > 1 ? argv[1] : "CPU";
    const auto vw = nr::registered_lab::shader_tokens(R"(
cbuffer C {float4 color;};
struct O {float4 p:SV_Position; float4 c:COLOR;};
O main(float4 p:POSITION,float4 c:COLOR) {O o;o.p=p;o.c=c*color;return o;})", "vs_4_0");
    const auto pw = nr::registered_lab::shader_tokens("float4 main(float4 p:SV_Position,float4 c:COLOR):SV_Target{return c;}", "ps_4_0");
    const auto vs = parse_vertex(vw.data(), vw.size()), ps = parse_pixel(pw.data(), pw.size());
    const Viewport viewport{0, 0, 64, 64, 0, 1};
    const PackedColor old{8, 20, 80, 100}; const std::vector<PackedColor> background(4096, old);
    UniformDrawExecutor executor(vs, ps, 6, 64, 64, backend, true, true, 15, 256);
    uint64_t checked = 0;
    for (unsigned frame = 0; frame < 6; ++frame) {
        const bool partial = frame == 2 || frame == 5, empty = frame == 3;
        const Color color = frame == 0 ? Color{0, 0, 0, 1} : (frame == 4 ? Color{.5f, .25f, .125f, 1} : Color{.25f, .125f, .0625f, .5f});
        VertexConstants cb; cb[0].resize(1); std::memcpy(cb[0][0].data(), color.data(), 16);
        const auto inputs = quad(partial ? 0.f : 1.f, empty);
        const auto result = executor.execute(inputs, cb, {}, viewport, background);
        test(result.used_rectangle == (frame == 0 || frame == 4));
        test(result.covered_samples == (empty ? 0u : (partial ? 1024u : 4096u)));
        for (unsigned y = 0; y < 64; ++y) for (unsigned x = 0; x < 64; ++x) for (unsigned c = 0; c < 4; ++c) {
            const bool covered = !empty && (!partial || (x < 32 && y < 32)); const auto source = c < 3 ? 2 - c : c;
            const auto expected = covered ? uint8_t(std::nearbyint(double(color[source]) * 255 + double(old[c]) * (1 - double(color[3])))) : old[c];
            test(result.pixels[size_t(y) * 64 + x][c] == expected); ++checked;
        }
        test(background == std::vector<PackedColor>(4096, old));
        if (frame == 1) {
            auto nonuniform = inputs; nonuniform[2][1][0] = as_bits(0.f); bool rejected = false;
            try { (void)executor.execute(nonuniform, cb, {}, viewport, background); } catch (const std::invalid_argument &) { rejected = true; }
            test(rejected && background == std::vector<PackedColor>(4096, old));
        }
        std::printf("{\"uniform_draw\":true,\"frame\":%u,\"covered_samples\":%llu,\"draw_ms\":%.3f,\"execution_device\":\"%s\",\"desktop_offload_tested\":false}\n",
            frame, static_cast<unsigned long long>(result.covered_samples), result.timings.total_ms, result.execution_device.c_str());
    }
    // Alternate indexed triangle order, nontrivial alpha and partial scissor.
    for(const unsigned alpha : {0u,128u,255u}) {
        auto inputs=quad(1,false);const auto original=inputs;
        inputs[3]=original[0];inputs[4]=original[2];inputs[5]=original[4];
        const Color color{float((std::min)(17u,alpha))/255,float((std::min)(73u,alpha))/255,float((std::min)(123u,alpha))/255,float(alpha)/255};
        VertexConstants cb;cb[0].resize(1);std::memcpy(cb[0][0].data(),color.data(),16);
        const std::array<int32_t,4> clip{5,7,61,59};
        const auto result=executor.execute(inputs,cb,{},viewport,background,clip);
        test(result.used_rectangle && result.covered_samples==56*52);
        for(unsigned y=0;y<64;++y) for(unsigned x=0;x<64;++x) for(unsigned c=0;c<4;++c) {
            const bool inside=x>=5 && x<61 && y>=7 && y<59;const auto lane=c<3 ? 2-c : c;
            const unsigned source=unsigned(std::round(color[lane]*255));
            const auto expected=inside ? uint8_t((source*255+unsigned(old[c])*(255-alpha)+127)/255) : old[c];
            test(result.pixels[size_t(y)*64+x][c]==expected);++checked;
        }
    }
    std::printf("{\"checked_components\":%llu,\"dynamic_geometry_and_constants\":true,\"nonuniform_rejected\":true,\"background_unchanged\":true}\n", static_cast<unsigned long long>(checked));
    return 0;
} catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
