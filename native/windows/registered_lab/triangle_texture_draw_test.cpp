#include "triangle_texture_draw.hpp"
#include "triangle_regions.hpp"
#include "shader_contract.hpp"
#include "shader_reference.hpp"
#include <algorithm>
#include <cstdio>
using namespace nr::raster;
using namespace nr::shader;
#define TEST(v) do{if(!(v))throw std::runtime_error(#v);}while(false)
int main(int argc,char **argv)try{
    TEST(argc<=2);const std::string backend=argc==2?argv[1]:"CPU";
    const std::array<int32_t,4> full{0,0,1024,1024};
    TEST(bounded_triangle_regions(std::vector<std::array<int32_t,4>>(16,full),full,4096).size()==16);
    bool rejected=false;try{bounded_triangle_regions(std::vector<std::array<int32_t,4>>(17,full),full,4096);}catch(const std::invalid_argument &){rejected=true;}TEST(rejected);
    TEST(bounded_triangle_regions(std::vector<std::array<int32_t,4>>(70,{4,4,6,6}),full,256).size()==70);
    TEST(bounded_triangle_regions(std::vector<std::array<int32_t,4>>(70,full),{0,0,0,0},1).size()==70);
    for(const std::array<int32_t,4> bad:{std::array<int32_t,4>{-1,0,2,2},{0,0,8193,2},{2,2,1,1}}){
        rejected=false;try{bounded_triangle_regions({bad},full,4096);}catch(const std::invalid_argument &){rejected=true;}TEST(rejected);
    }
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
        const Viewport viewport{0,0,32,32,0,1};
        std::vector<PackedColor> bg(1024);for(size_t n=0;n<bg.size();++n)bg[n]={uint8_t(n),31,211,97};
        TriangleTextureDrawExecutor executor(vs,ps,9,32,32,format,3,2,87,backend,true,mask,filter,CullMode::back,false,256);
        for(const std::array<int32_t,4> clip: {std::array<int32_t,4>{7,8,26,24}, {7,8,26,9}, {0,0,32,32}, {31,31,32,32}, {17,0,17,32}, {0,17,32,17}}){
        const auto result=executor.execute(inputs,{}, {},viewport,texture,bg,clip);
        const auto covered=unsigned((std::max)(0,(std::min)(28,clip[2])-(std::max)(4,clip[0])))*unsigned((std::max)(0,(std::min)(28,clip[3])-(std::max)(4,clip[1])));
        TEST(result.execution_device.rfind(backend,0)==0 && result.covered_samples==covered && result.tile_executions==3*((covered+255)/256));
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){const auto n=size_t(y)*32+x;auto expected=bg[n];
            if(x>=unsigned(clip[0]) && x<unsigned(clip[2]) && y>=unsigned(clip[1]) && y<unsigned(clip[3]) && x>=4 && x<28 && y>=4 && y<28){
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
    }
    // Seventy small triangles share a full-size scissor. The old estimate
    // charged 4480 tiles; device bounds require only 70, preserving the cap.
    std::vector<VertexRegisters> sparse(210);const float sparse_positions[3][2]{{4,4},{6,4},{6,6}};
    for(size_t i=0;i<sparse.size();++i){
        sparse[i][0]={as_bits(sparse_positions[i%3][0]/64-1),as_bits(1-sparse_positions[i%3][1]/64),as_bits(.5f),as_bits(1)};
        sparse[i][1]={as_bits(1),as_bits(1),as_bits(1),as_bits(1)};
        sparse[i][2]={as_bits(.5f),as_bits(.5f),0,0};
    }
    const PackedTexture tiny{1,1,28,{{23,51,79,255}}};const std::vector<PackedColor> bg(128*128,{1,2,3,4});
    TriangleTextureDrawExecutor sparse_executor(vs,ps,210,128,128,28,1,1,28,backend,false,15,TextureFilter::point,CullMode::none,false,256);
    const auto sparse_result=sparse_executor.execute(sparse,{}, {},{0,0,128,128,0,1},tiny,bg);
    TEST(sparse_result.execution_device.rfind(backend,0)==0 && sparse_result.tile_executions==70 && sparse_result.covered_samples==210);
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x)
        TEST(sparse_result.pixels[y*128+x]==(x>=4 && x<6 && y>=4 && y<=x?tiny.pixels[0]:bg[0]));
    // Fifty disjoint glyph-sized quads, each followed by a blended overwrite
    // of its first half. Collating disjoint regions should need three batches
    // while preserving every overlapping primitive's submission order.
    uint64_t collated_components=0;
    {
        constexpr unsigned quads=50,width=128,height=64;
        const unsigned corner[9]{0,1,2,0,2,3,0,1,2};
        const unsigned xy[4][2]{{0,0},{2,0},{2,2},{0,2}};
        std::vector<VertexRegisters> glyphs(quads*9);
        for(unsigned q=0;q<quads;++q)for(unsigned v=0;v<9;++v){
            const float x=float(4+(q%10)*10+xy[corner[v]][0]),y=float(4+(q/10)*10+xy[corner[v]][1]);
            const float tint=float(1+(q%3))*.25f,red=v>=6?.75f:tint;
            glyphs[q*9+v][0]={as_bits(x/64-1),as_bits(1-y/32),as_bits(.5f),as_bits(1)};
            glyphs[q*9+v][1]={as_bits(red),as_bits(.25f),as_bits(.5f),as_bits(.5f)};
            glyphs[q*9+v][2]={as_bits(.5f),as_bits(.5f),0,0};
        }
        const PackedColor old{16,32,48,64};const PackedTexture source{1,1,28,{{64,128,192,255}}};
        const std::vector<PackedColor> background(width*height,old);
        TriangleTextureDrawExecutor executor(vs,ps,quads*9,width,height,28,1,1,28,backend,true,15,TextureFilter::point,CullMode::none,false,256);
        const auto result=executor.execute(glyphs,{}, {},{0,0,width,height,0,1},source,background);
        TEST(result.execution_device.rfind(backend,0)==0 && result.covered_samples==quads*7 && result.tile_executions==3);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){auto expected=old;
            if(x>=4 && y>=4 && (x-4)/10<10 && (y-4)/10<5 && (x-4)%10<2 && (y-4)%10<2){
                const auto q=((y-4)/10)*10+(x-4)/10;
                const double tint[4]{double(1+(q%3))*.25,.25,.5,.5};
                for(unsigned c=0;c<4;++c)expected[c]=uint8_t(std::nearbyint(source.pixels[0][c]*tint[c]+old[c]*.5));
                if((y-4)%10<=(x-4)%10){const double over[4]{.75,.25,.5,.5};
                    for(unsigned c=0;c<4;++c)expected[c]=uint8_t(std::nearbyint(source.pixels[0][c]*over[c]+expected[c]*.5));}
            }
            TEST(result.pixels[y*width+x]==expected);collated_components+=4;
        }
        std::printf("{\"collated_triangles\":150,\"raster_batches\":%llu,\"provider_requests\":%llu,\"ordered_blending_exact\":true,\"execution_device\":\"%s\"}\n",
            static_cast<unsigned long long>(result.tile_executions),static_cast<unsigned long long>(result.provider_requests),result.execution_device.c_str());
    }
    TEST(collated_components==32768);
    uint64_t odd_components=0;
    for(const auto extent:{std::array<uint32_t,2>{31,29},{1,1}}){
        const auto w=extent[0],h=extent[1];const unsigned corner[6]{0,1,2,0,2,3};
        const float xy[4][2]{{-1,1},{1,1},{1,-1},{-1,-1}};
        std::vector<VertexRegisters> quad(6);
        for(unsigned v=0;v<6;++v){
            quad[v][0]={as_bits(xy[corner[v]][0]),as_bits(xy[corner[v]][1]),as_bits(.5f),as_bits(1)};
            quad[v][1]={as_bits(1),as_bits(1),as_bits(1),as_bits(1)};
            quad[v][2]={as_bits(.5f),as_bits(.5f),0,0};
        }
        const PackedColor old{1,2,3,4};const std::vector<PackedColor> background(size_t(w)*h,old);
        TriangleTextureDrawExecutor executor(vs,ps,6,w,h,28,1,1,28,backend,false,15,TextureFilter::point,CullMode::none,false,256);
        for(const auto clip:{std::array<int32_t,4>{0,0,int32_t(w),int32_t(h)},
            {int32_t(w>3?w-3:0),int32_t(h>3?h-3:0),int32_t(w),int32_t(h)},
            {int32_t(w),int32_t(h),int32_t(w),int32_t(h)}}){
            const auto result=executor.execute(quad,{}, {},{0,0,float(w),float(h),0,1},tiny,background,clip);
            const auto area=uint64_t(clip[2]-clip[0])*uint64_t(clip[3]-clip[1]);
            TEST(result.execution_device.rfind(backend,0)==0 && result.covered_samples==area && result.tile_executions==2*((area+255)/256));
            for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){
                const bool covered=int32_t(x)>=clip[0] && int32_t(y)>=clip[1] && int32_t(x)<clip[2] && int32_t(y)<clip[3];
                TEST(result.pixels[size_t(y)*w+x]==(covered?tiny.pixels[0]:old));odd_components+=4;
            }
        }
    }
    std::printf("{\"odd_target_components\":%llu,\"partial_batch_padding\":true,\"one_pixel_target\":true,\"execution_device\":\"%s\"}\n",
        static_cast<unsigned long long>(odd_components),backend.c_str());
    std::printf("{\"textured_triangle_components\":%llu,\"maximum_byte_error\":%u,\"execution_device\":\"%s\",\"coverage_exact\":true,\"packed_scissor_cases\":6,\"partial_batch_and_empty_regions\":true,\"sparse_triangle_tiles\":70,\"raster_tile_budget\":4096,\"desktop_offload_tested\":false}\n",static_cast<unsigned long long>(checked),maximum,backend.c_str());return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
