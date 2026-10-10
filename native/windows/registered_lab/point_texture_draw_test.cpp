#include "point_texture_draw.hpp"
#include "point_texture_fixture.hpp"
#include "shader_contract.hpp"
#include "shader_reference.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
using namespace nr::raster;
using namespace nr::shader;
namespace {
void test(bool v) { if (!v) throw std::runtime_error("point texture draw contract failed"); }

}
int main(int argc,char **argv) try {
    const std::string backend = argc > 1 ? argv[1] : "CPU";
    const auto words = nr::registered_lab::shader_tokens(R"(
struct O {float4 p:SV_Position;float4 c:COLOR0;float2 uv:TEXCOORD0;};
O main(float4 p:POSITION,float4 c:COLOR0,float2 uv:TEXCOORD0){O o;o.p=p;o.c=c;o.uv=uv;return o;})","vs_4_0");
    const auto vs = parse_vertex(words.data(),words.size()), ps = parse_pixel(nr::registered_lab::test_fixture::point_texture_pixel_words,std::size(nr::registered_lab::test_fixture::point_texture_pixel_words));
    uint64_t checked = 0; uint32_t rejected = 0;
    for (const bool swap : {false,true}) {
        PackedTexture source{96,80,87,{}}; source.pixels.resize(96*80);
        PointTextureDrawExecutor executor(vs,ps,64,64,swap ? 28u : 87u,96,80,87,backend);
        for (uint32_t frame = 0; frame < 4; ++frame) {
            const uint32_t ox = frame & 1 ? 32u : 0u, oy = frame & 2 ? 16u : 0u;
            for (size_t i = 0; i < source.pixels.size(); ++i) for (uint32_t c = 0; c < 4; ++c)
                source.pixels[i][c] = uint8_t((i*17+c*71+frame*37)&255);
            const uint32_t corners[6][2]{{0,0},{64,0},{64,64},{64,64},{0,64},{0,0}};
            std::vector<VertexRegisters> inputs(6);
            for (size_t i = 0; i < 6; ++i) {
                inputs[i][0] = {as_bits(float(corners[i][0])/32-1),as_bits(1-float(corners[i][1])/32),as_bits(.5f),as_bits(1.f)};
                inputs[i][1] = {as_bits(1.f),as_bits(1.f),as_bits(1.f),as_bits(1.f)};
                inputs[i][2] = {as_bits(float(ox+corners[i][0])/96),as_bits(float(oy+corners[i][1])/80),0,0};
            }
            const Viewport view{0,0,64,64,0,1};
            if (!frame) for (uint32_t bad = 0; bad < 4; ++bad) {
                auto invalid = inputs;
                switch(bad) {
                case 0: invalid[1][1][0] = as_bits(.5f); break;
                case 1: invalid[2][2][0] = as_bits(.5f); break;
                case 2: invalid[1][0][0] = as_bits(.5f); break;
                case 3: invalid[3][0][3] = as_bits(2.f); break;
                }
                bool failed = false; try { (void)executor.execute(invalid,{},view,source); }
                catch(const std::invalid_argument &) { failed = true; } test(failed); ++rejected;
            }
            const auto out = executor.execute(inputs,{},view,source);
            test(out.covered_samples == 4096 && out.provider_requests == 3 && out.pixels.size() == 4096 && out.execution_device.rfind(backend,0) == 0);
            for (uint32_t y = 0; y < 64; ++y) for (uint32_t x = 0; x < 64; ++x) for (uint32_t c = 0; c < 4; ++c) {
                const auto expected = c == 3 ? uint8_t(255) : source.pixels[size_t(oy+y)*96+ox+x][swap ? 2-c : c];
                test(out.pixels[size_t(y)*64+x][c] == expected); ++checked;
            }
            std::printf("{\"point_texture_draw\":true,\"frame\":%u,\"swap\":%s,\"draw_ms\":%.3f,\"execution_device\":\"%s\"}\n",frame,swap ? "true" : "false",out.timings.total_ms,out.execution_device.c_str());
        }
    }
    // Captured Start-menu backdrop: the transformed +1/-1 endpoints are
    // 0.9999998808/-0.9999998808, but snap to the exact 642x660 rectangle.
    {
        constexpr uint32_t w=642,h=660,sw=1024,sh=768,ox=191,oy=48;
        PackedTexture source{sw,sh,87,{}};source.pixels.resize(size_t(sw)*sh);
        for(size_t i=0;i<source.pixels.size();++i)for(unsigned c=0;c<4;++c)source.pixels[i][c]=uint8_t((i*17+c*71)&255);
        PointTextureDrawExecutor executor(vs,ps,w,h,87,sw,sh,87,backend);
        const Viewport view{0,0,float(w),float(h),0,1};
        const uint32_t corners[6][2]{{0,0},{w,0},{w,h},{w,h},{0,h},{0,0}};
        for(const float edge:{0.99999988079071044921875f,1.00000011920928955078125f}){
            std::vector<VertexRegisters> inputs(6);
            for(unsigned n=0;n<6;++n){
                inputs[n][0]={as_bits(corners[n][0]?edge:-1.f),as_bits(corners[n][1]?-edge:1.f),as_bits(0.f),as_bits(1.f)};
                inputs[n][1]={as_bits(1.f),as_bits(1.f),as_bits(1.f),as_bits(1.f)};
                inputs[n][2]={as_bits(float(ox+corners[n][0])/sw),as_bits(float(oy+corners[n][1])/sh),0,0};
            }
            auto invalid=inputs;invalid[1][0][0]=as_bits(1.f-1.f/256.f);
            bool failed=false;try{(void)executor.execute(invalid,{},view,source);}catch(const std::invalid_argument&){failed=true;}test(failed);++rejected;
            const auto result=executor.execute(inputs,{},view,source);
            test(result.covered_samples==uint64_t(w)*h && result.execution_device.rfind(backend,0)==0);
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)for(unsigned c=0;c<4;++c){
                test(result.pixels[size_t(y)*w+x][c]==(c==3?255:source.pixels[size_t(y+oy)*sw+x+ox][c]));++checked;
            }
        }
    }
    for (uint32_t bad = 0; bad < 4; ++bad) {
        auto p = ps;
        switch(bad) { case 0:p.instructions[1].sources[0].literal.fill(0);break;case 1:p.instructions[0].sources[1].index=1;break;
            case 2:p.instructions[2].saturate=true;break;case 3:p.interpolation[2]=4;break; }
        bool failed=false;try {PointTextureDrawExecutor::validate_shader(p);} catch(const std::invalid_argument &){failed=true;} test(failed);++rejected;
    }
    std::printf("{\"verified_components\":%llu,\"invalid_draws_rejected\":%u,\"reused_after_rejection\":true,\"desktop_offload_tested\":false,\"displayed_frames\":0}\n",static_cast<unsigned long long>(checked),rejected);
    return 0;
} catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what());return 1; }
