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
    auto gradient=quad(1,false);VertexConstants white;white[0].push_back({as_bits(1.f),as_bits(1.f),as_bits(1.f),as_bits(1.f)});
    for(auto &v:gradient)v[1]={as_bits((as_float(v[0][0])+1)*.25f),as_bits((1-as_float(v[0][1]))*.125f),as_bits(.125f),as_bits(.5f)};
    const std::array<int32_t,4> clip{8,4,56,28};
    const auto shaded=executor.execute(gradient,white,{},viewport,background,clip);
    test(!shaded.used_rectangle && shaded.covered_samples==48*24);
    double max_error=0;
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)for(unsigned c=0;c<4;++c){
        const bool inside=x>=8 && x<56 && y>=4 && y<28;
        const double rgba[]{(x+.5)/128,(y+.5)/256,.125,.5};const auto lane=c<3?2-c:c;
        const double expected=inside?rgba[lane]*255+old[c]*.5:old[c];
        const auto error=std::abs(shaded.pixels[size_t(y)*64+x][c]-expected);max_error=(std::max)(max_error,error);test(error<=.51);++checked;
    }
    // Arbitrary nonuniform pixel programs remain rejected until their complete
    // interpolation and shader path is implemented, with no target mutation.
    const auto bad_words=nr::registered_lab::shader_tokens("float4 main(float4 c:COLOR):SV_Target{return c*c;}","ps_4_0");
    const auto bad_ps=parse_pixel(bad_words.data(),bad_words.size());
    UniformDrawExecutor unsupported(vs,bad_ps,6,64,64,backend,true,true,15,256);
    bool rejected=false;try{(void)unsupported.execute(gradient,white,{},viewport,background,clip);}catch(const std::invalid_argument &){rejected=true;}
    test(rejected && background==std::vector<PackedColor>(4096,old));
    for(const auto cull:{CullMode::front,CullMode::back})for(const bool ccw:{false,true}){
        UniformDrawExecutor faces(vs,ps,6,64,64,backend,false,true,15,256,cull,ccw);
        const auto result=faces.execute(quad(1,false),white,{},viewport,background,clip);
        const bool visible=(cull==CullMode::back)!=ccw;
        test(result.covered_samples==(visible?48*24:0));
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)
            test(result.pixels[size_t(y)*64+x]==(visible && x>=8 && x<56 && y>=4 && y<28?PackedColor{255,255,255,255}:old));
    }
    // Same projected square, different clip-space w: test perspective correction
    // against analytic barycentric weights, then explicitly disable correction.
    const float weights[]{1,2,4,4,3,1};
    auto perspective=gradient;
    for(size_t v=0;v<perspective.size();++v)for(unsigned c=0;c<4;++c)
        perspective[v][0][c]=as_bits(as_float(perspective[v][0][c])*weights[v]);
    const auto np_words=nr::registered_lab::shader_tokens("float4 main(float4 p:SV_Position,noperspective float4 c:COLOR):SV_Target{return c;}","ps_4_0");
    const auto np=parse_pixel(np_words.data(),np_words.size());
    UniformDrawExecutor affine(vs,np,6,64,64,backend,true,true,15,256);
    for(const bool correction:{true,false}){
        const auto result=(correction?executor:affine).execute(perspective,white,{},viewport,background,clip);
        test(result.covered_samples==48*24);
        for(unsigned y=4;y<28;++y)for(unsigned x=8;x<56;++x){
            const double px=(x+.5)/64,py=(y+.5)/64;
            const bool first=py<=px;const size_t base=first?0u:3u;
            double bary[3]{first?1-px:px,first?px-py:py-px,first?py:1-py};
            double denominator=0;for(unsigned v=0;v<3;++v){if(correction)bary[v]/=weights[base+v];denominator+=bary[v];}
            for(unsigned c=0;c<4;++c){
                const auto lane=c<3?2-c:c;double value=0;
                for(unsigned v=0;v<3;++v)value+=bary[v]*as_float(gradient[base+v][1][lane]);
                const double expected=value/denominator*255+old[c]*.5;
                const auto error=std::abs(result.pixels[size_t(y)*64+x][c]-expected);
                max_error=(std::max)(max_error,error);test(error<=.51);++checked;
            }
        }
    }
    std::printf("{\"checked_components\":%llu,\"gradient_max_byte_error\":%.9g,\"dynamic_geometry_and_constants\":true,\"unsupported_nonuniform_program_rejected\":true,\"background_unchanged\":true}\n", static_cast<unsigned long long>(checked),max_error);
    return 0;
} catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
