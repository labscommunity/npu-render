#include "triangle_texture_draw.hpp"
#include "triangle_regions.hpp"
#include "shader_contract.hpp"
#include "shader_reference.hpp"
#include "derivative_fixture.hpp"
#include <algorithm>
#include <cstdio>
using namespace nr::raster;
using namespace nr::shader;
#define TEST(v) do{if(!(v))throw std::runtime_error(#v);}while(false)
int main(int argc,char **argv)try{
    TEST(argc<=2);const std::string backend=argc==2?argv[1]:"CPU";
    uint64_t shader_components=0,draw_components=0;
    const auto words=nr::registered_lab::shader_tokens(R"(
float4 main(float4 p:SV_Position,noperspective float4 v:TEXCOORD):SV_Target{
float z=v.x*v.y;return float4(ddx(z)*32,ddy(z)*32,ddx(ddy(z))*1024,1);})","ps_4_0");
    const auto ps=parse_pixel(words.data(),words.size());TEST(quad_derivatives(ps));
    {
        PixelExecutor executor(ps,16,backend);std::vector<VertexRegisters> input(16);
        for(unsigned q=0;q<4;++q)for(unsigned lane=0;lane<4;++lane){
            const float x=float(2*q+(lane&1))+.5f,y=float(6+2*q+(lane>>1))+.5f;
            for(unsigned reg=0;reg<16;++reg)if(ps.inputs[reg])input[q*4+lane][reg]={as_bits(x/32),as_bits(y/32),as_bits(1),as_bits(1)};
        }
        const auto result=executor.execute(input,{});TEST(result.execution_device.rfind(backend,0)==0);
        for(unsigned q=0;q<4;++q){std::array<Registers,4> quad;for(unsigned lane=0;lane<4;++lane)quad[lane]=input[q*4+lane];
            const auto reference=reference_pixel_quad(ps,quad,{});
            for(unsigned lane=0;lane<4;++lane)for(unsigned c=0;c<4;++c){
                TEST(result.vertices[q*4+lane][0][c]==reference[lane][0][c]);++shader_components;
            }
        }
        bool rejected=false;try{PixelExecutor invalid(ps,3,backend);}catch(const std::invalid_argument &){rejected=true;}TEST(rejected);
        auto vertex=ps;vertex.stage=Stage::vertex;rejected=false;try{RegisterExecutor::validate(vertex);}catch(const std::invalid_argument &){rejected=true;}TEST(rejected);
    }
    // Real DWM shader: division, two derivative directions, min/max and four
    // filtered samples. Compare every lane with the separate scalar quad oracle.
    {
        const auto &captured=nr::registered_lab::test_fixture::derivative_pixel_words;
        const auto program=parse_pixel(captured,std::size(captured));TEST(quad_derivatives(program));
        TextureShapes shapes{};shapes[0]={4,4,15};TextureSamplers samplers{};samplers[0].filter=TextureFilter::linear;
        TexturePixels textures{};textures[0].resize(16);
        for(unsigned n=0;n<16;++n)textures[0][n]={float(n%4)/4,float(n/4)/4,float(n)/16,1};
        VertexConstants constants;constants[0].push_back({as_bits(0),as_bits(0),as_bits(1),as_bits(1)});
        std::vector<VertexRegisters> input(16);
        for(unsigned q=0;q<4;++q)for(unsigned lane=0;lane<4;++lane){
            const float x=(float(2*(q%2)+(lane&1))+.5f)/4,y=(float(2*(q/2)+(lane>>1))+.5f)/4;
            for(unsigned reg=0;reg<16;++reg)if(program.inputs[reg])input[q*4+lane][reg]={as_bits(x),as_bits(y),as_bits(1),as_bits(1)};
        }
        PixelExecutor executor(program,16,backend,shapes,samplers);const auto result=executor.execute(input,constants,textures);
        TEST(result.execution_device.rfind(backend,0)==0);
        for(unsigned q=0;q<4;++q){std::array<Registers,4> quad;for(unsigned lane=0;lane<4;++lane)quad[lane]=input[q*4+lane];
            const auto reference=reference_pixel_quad(program,quad,constants,shapes,samplers,textures);
            for(unsigned lane=0;lane<4;++lane)for(unsigned c=0;c<4;++c){
                const float actual=as_float(result.vertices[q*4+lane][0][c]),expected=as_float(reference[lane][0][c]);
                if(std::abs(actual-expected)>1e-5f)std::fprintf(stderr,"captured quad=%u lane=%u channel=%u actual=%.9g expected=%.9g\n",q,lane,c,actual,expected);
                TEST(std::abs(actual-expected)<=1e-5f);++shader_components;
            }
        }
    }
    const auto vw=nr::registered_lab::shader_tokens(R"(
struct O{float4 p:SV_Position;noperspective float4 uv:TEXCOORD;};
O main(float4 p:POSITION,float4 uv:TEXCOORD){O o;o.p=p;o.uv=uv;return o;})","vs_4_0");
    const auto vs=parse_vertex(vw.data(),vw.size());constexpr unsigned width=31,height=29;
    const auto edge=[](Point a,Point b,Point p){return int64_t(b[0]-a[0])*(p[1]-a[1])-int64_t(b[1]-a[1])*(p[0]-a[0]);};
    for(const bool tiny:{false,true})for(const uint32_t format:{28u,87u})for(const uint32_t batch:{16u,64u}){
        const std::array<std::array<float,2>,3> positions=tiny?
            std::array<std::array<float,2>,3>{{{3.25f,5.25f},{4.25f,5.25f},{3.25f,6.25f}}}:
            std::array<std::array<float,2>,3>{{{0,0},{31,0},{0,29}}};
        std::vector<VertexRegisters> input(3);std::array<Point,3> triangle;
        for(unsigned n=0;n<3;++n){const auto x=positions[n][0],y=positions[n][1];
            input[n][0]={as_bits(x*2/width-1),as_bits(1-y*2/height),as_bits(.5f),as_bits(1)};
            input[n][1]={as_bits(x/32),as_bits(y/32),as_bits(1),as_bits(1)};
            triangle[n]={int32_t(x*256),int32_t(y*256)};
        }
        const uint8_t mask=format==87?5:15;const PackedColor old{4,19,53,211};const std::vector<PackedColor> background(width*height,old);
        TriangleTextureDrawExecutor executor(vs,ps,3,width,height,format,PackedResources{},TextureSamplers{},backend,BlendMode::source_copy,mask,CullMode::none,false,batch);
        for(const std::array<int32_t,4> clip:{std::array<int32_t,4>{0,0,width,height},{3,5,29,27},{3,5,4,6},{7,9,7,12}}){
            const auto result=executor.execute(input,{}, {},{0,0,width,height,0,1},PackedResources{},background,clip);
            TEST(result.execution_device.rfind(backend,0)==0);uint64_t covered=0;
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
                bool inside=int32_t(x)>=clip[0] && int32_t(x)<clip[2] && int32_t(y)>=clip[1] && int32_t(y)<clip[3];
                const Point point{int32_t(x*256+128),int32_t(y*256+128)};
                for(unsigned e=0;e<3;++e){const auto a=triangle[e],b=triangle[(e+1)%3];const auto value=edge(a,b,point);const bool top=b[1]<a[1] || (b[1]==a[1] && b[0]>a[0]);if(value<0 || (!value&&!top))inside=false;}
                covered+=inside;
                // Analytic derivatives of (x/32)*(y/32), independent of the
                // quad scheduler and scalar instruction interpreter.
                const double color[]{(y+.5)/32,(x+.5)/32,1,1};
                for(unsigned c=0;c<4;++c){const auto lane=format==87 && c<3?2-c:c;
                    const double expected=inside && (mask&(1u<<lane))?color[lane]*255:old[c];
                    if(std::abs(result.pixels[y*width+x][c]-expected)>.501)std::fprintf(stderr,"draw tiny=%u format=%u batch=%u x=%u y=%u c=%u actual=%u expected=%.9g\n",unsigned(tiny),format,batch,x,y,c,unsigned(result.pixels[y*width+x][c]),expected);
                    TEST(std::abs(result.pixels[y*width+x][c]-expected)<=.501);++draw_components;
                }
            }
            TEST(result.covered_samples==covered);
        }
    }
    // Two separated primitives share a batch, with different UV origins.
    // Each keeps a complete helper quad; derivatives cannot cross primitives.
    {
        std::vector<VertexRegisters> input(6);
        const float offsets[3][2]{{0,0},{1,0},{0,1}};
        for(unsigned t=0;t<2;++t)for(unsigned v=0;v<3;++v){
            const float x=3.25f+float(t)*6+offsets[v][0],y=5.25f+offsets[v][1];
            input[t*3+v][0]={as_bits(x*2/width-1),as_bits(1-y*2/height),as_bits(.5f),as_bits(1)};
            input[t*3+v][1]={as_bits((x+float(t)*8)/32),as_bits(y/32),as_bits(1),as_bits(1)};
        }
        const PackedColor old{4,19,53,211};const std::vector<PackedColor> background(width*height,old);
        TriangleTextureDrawExecutor executor(vs,ps,6,width,height,28,PackedResources{},TextureSamplers{},backend,BlendMode::source_copy,15,CullMode::none,false,64);
        const auto result=executor.execute(input,{}, {},{0,0,width,height,0,1},PackedResources{},background);
        TEST(result.execution_device.rfind(backend,0)==0 && result.covered_samples==2 && result.tile_executions==1);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)for(unsigned c=0;c<4;++c){
            const bool inside=y==5 && (x==3 || x==9);
            const double color[4]{(y+.5)/32,(x+.5+(x==9?8:0))/32,1,1};
            const double expected=inside?color[c]*255:old[c];
            TEST(std::abs(result.pixels[y*width+x][c]-expected)<=.501);++draw_components;
        }
    }
    // Inactive raster lanes have no pixel shader invocation. A zero-filled
    // UV/w payload would create 0/0 and reject the whole otherwise valid draw.
    {
        const auto divide_words=nr::registered_lab::shader_tokens(R"(
Texture2D t:register(t0);SamplerState s:register(s0);
float4 main(float4 p:SV_Position,noperspective float4 uv:TEXCOORD):SV_Target{return t.Sample(s,uv.xy/uv.z);})","ps_4_0");
        const auto divide=parse_pixel(divide_words.data(),divide_words.size());TEST(!quad_derivatives(divide));
        std::vector<VertexRegisters> input(3);const float xy[3][2]{{-1,1},{1,1},{-1,-1}};
        for(unsigned n=0;n<3;++n){input[n][0]={as_bits(xy[n][0]),as_bits(xy[n][1]),as_bits(.5f),as_bits(1)};
            input[n][1]={as_bits(.25f),as_bits(.25f),as_bits(1),as_bits(1)};}
        const PackedColor old{4,19,53,211},color{37,91,153,255};const PackedTexture source{1,1,28,{color}};
        const std::vector<PackedColor> background(1024,old);
        TriangleTextureDrawExecutor executor(vs,divide,3,32,32,28,1,1,28,backend,false,15,TextureFilter::point,CullMode::none,false,256);
        const auto result=executor.execute(input,{}, {},{0,0,32,32,0,1},source,background,std::array<int32_t,4>{3,5,27,29});
        TEST(result.execution_device.rfind(backend,0)==0);uint64_t covered=0;
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){const bool inside=x>=3 && x<27 && y>=5 && y<29 && x+y<31;
            TEST(result.pixels[y*32+x]==(inside?color:old));covered+=inside;draw_components+=4;}
        TEST(result.covered_samples==covered);
    }
    std::printf("{\"derivative_shader_components\":%llu,\"quad_draw_components\":%llu,\"execution_device\":\"%s\",\"odd_target_sizes\":true,\"helper_pixels\":true,\"partial_tiles\":true,\"scissor_and_masks\":true,\"desktop_offload_tested\":false}\n",static_cast<unsigned long long>(shader_components),static_cast<unsigned long long>(draw_components),backend.c_str());return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
