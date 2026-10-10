#include "triangle_texture_draw.hpp"
#include "shader_contract.hpp"
#include "shader_reference.hpp"
#include <algorithm>
#include <cstdio>
using namespace nr::raster;
using namespace nr::shader;
#define TEST(v) do{if(!(v))throw std::runtime_error(#v);}while(false)
int main(int argc,char **argv)try{
    TEST(argc<=2);const std::string backend=argc==2?argv[1]:"CPU";
    const auto vw=nr::registered_lab::shader_tokens(R"(
struct O{float4 p:SV_Position;noperspective float4 c:COLOR;noperspective float2 uv:TEXCOORD;};
O main(float4 p:POSITION,float4 c:COLOR,float2 uv:TEXCOORD){O o;o.p=p;o.c=c;o.uv=uv;return o;})","vs_4_0");
    const auto pw=nr::registered_lab::shader_tokens(R"(
Texture2D t:register(t0);SamplerState s:register(s0);
float4 main(float4 p:SV_Position,noperspective float4 c:COLOR,noperspective float2 uv:TEXCOORD):SV_Target{return t.Sample(s,uv)*c;})","ps_4_0");
    const auto vs=parse_vertex(vw.data(),vw.size()),ps=parse_pixel(pw.data(),pw.size());
    // Three overlapping triangles: two front-facing halves of a square plus
    // an oppositely wound copy. The third must be culled; shared edges once.
    const unsigned corners[9]{0,1,2,0,2,3,0,2,1};
    const float positions[4][2]{{4,4},{28,4},{28,28},{4,28}};
    std::vector<VertexRegisters> inputs(9);
    for(unsigned i=0;i<9;++i){const auto x=positions[corners[i]][0],y=positions[corners[i]][1];
        inputs[i][0]={as_bits(x/16-1),as_bits(1-y/16),as_bits(.5f),as_bits(1)};
        inputs[i][1]={as_bits(x/24),as_bits(y/24),as_bits(.4f),as_bits(.75f+x/48)};
        inputs[i][2]={as_bits((x-4)/24),as_bits((y-4)/24),0,0};}
    uint64_t checked=0;unsigned maximum=0;
    for(const auto filter:{TextureFilter::point,TextureFilter::linear})for(const uint32_t format:{28u,87u}){
        const uint8_t mask=format==87?13:15;
        PackedTexture texture{3,2,87,{{20,40,80,255},{40,80,120,192},{100,50,10,255},{0,70,90,128},{40,20,10,255},{30,90,120,240}}};
        const Viewport viewport{0,0,32,32,0,1};const std::array<int32_t,4> clip{7,8,26,24};
        std::vector<PackedColor> bg(1024);for(size_t n=0;n<bg.size();++n)bg[n]={uint8_t(n),31,211,97};
        TriangleTextureDrawExecutor executor(vs,ps,9,32,32,format,3,2,87,backend,true,mask,filter,CullMode::back,false,1024);
        const auto result=executor.execute(inputs,{}, {},viewport,texture,bg,clip);
        TEST(result.execution_device.rfind(backend,0)==0 && result.covered_samples==19*16 && result.tile_executions==3);
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){const auto n=size_t(y)*32+x;auto expected=bg[n];
            if(x>=7 && x<26 && y>=8 && y<24){
                const double tx=(x+.5-4)/24*3-(filter==TextureFilter::linear?.5:0),ty=(y+.5-4)/24*2-(filter==TextureFilter::linear?.5:0);
                const int ix=int(std::floor(tx)),iy=int(std::floor(ty));const double fx=filter==TextureFilter::linear?tx-ix:0,fy=filter==TextureFilter::linear?ty-iy:0;
                double sampled[4]{};
                for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)for(unsigned c=0;c<4;++c)
                    sampled[c]+=texture.pixels[size_t(std::clamp(iy+dy,0,1))*3+size_t(std::clamp(ix+dx,0,2))][c<3?2-c:c]/255.*(dx?fx:1-fx)*(dy?fy:1-fy);
                const double modulation[4]{(x+.5)/24,(y+.5)/24,.4,.75+(x+.5)/48};
                const auto alpha=std::clamp(sampled[3]*modulation[3],0.,1.);
                for(unsigned c=0;c<4;++c){const auto lane=format==87 && c<3?2-c:c;if(!(mask&(1u<<lane)))continue;
                    expected[c]=uint8_t(std::nearbyint(std::clamp(std::clamp(sampled[lane]*modulation[lane],0.,1.)*255+bg[n][c]*(1-alpha),0.,255.)));}
            }
            for(unsigned c=0;c<4;++c){const auto error=unsigned(std::abs(int(expected[c])-int(result.pixels[n][c])));maximum=(std::max)(maximum,error);if(error>1)std::fprintf(stderr,"x=%u y=%u lane=%u actual=%u expected=%u filter=%u format=%u\n",x,y,c,unsigned(result.pixels[n][c]),unsigned(expected[c]),unsigned(filter),format);TEST(error<=1);++checked;}
        }
    }
    std::printf("{\"textured_triangle_components\":%llu,\"maximum_byte_error\":%u,\"execution_device\":\"%s\",\"coverage_exact\":true,\"desktop_offload_tested\":false}\n",static_cast<unsigned long long>(checked),maximum,backend.c_str());return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
