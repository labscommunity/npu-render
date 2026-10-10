#include "linear_texture_draw.hpp"
#include "linear_texture_fixture.hpp"
#include "point_texture_fixture.hpp"
#include "shader_contract.hpp"
#include "shader_reference.hpp"
#include <algorithm>
#include <cstdio>
#include <limits>
using namespace nr::raster;
using namespace nr::shader;
#define TEST(v) do {if(!(v))throw std::runtime_error(#v);}while(false)
int main(int argc,char **argv)try{
    const std::string backend=argc>1?argv[1]:"CPU";
    TEST(argc<=3 && (argc<3 || !std::strcmp(argv[2],"--point") || !std::strcmp(argv[2],"--point-opaque")));
    const bool point=argc==3,opaque=argc==3 && !std::strcmp(argv[2],"--point-opaque");
    const auto words=nr::registered_lab::shader_tokens(R"(
struct O{float4 p:SV_Position;float4 c:COLOR0;float2 uv:TEXCOORD0;};
O main(float4 p:POSITION,float4 c:COLOR0,float2 uv:TEXCOORD0){O o;o.p=p;o.c=c;o.uv=uv;return o;})","vs_4_0");
    const auto vs=parse_vertex(words.data(),words.size()),ps=opaque ? parse_pixel(nr::registered_lab::test_fixture::point_texture_pixel_words,std::size(nr::registered_lab::test_fixture::point_texture_pixel_words)) : parse_pixel(nr::registered_lab::test_fixture::linear_texture_pixel_words,std::size(nr::registered_lab::test_fixture::linear_texture_pixel_words));
    uint64_t checked=0,different=0;unsigned rejected=0;double maximum=0;
    for(const bool desktop:{false,true})for(const bool blend:{false,true})for(const bool swap:{false,true})for(const uint32_t sf:{28u,87u}){
        if(desktop && (!blend || swap || sf!=87))continue;
        const uint32_t w=desktop?1024u:64u,h=desktop?768u:64u,sw=desktop?w:96u,sh=desktop?h:80u;
        const Viewport view{0,0,float(w),float(h),0,1};
        const uint32_t format=swap?(sf==87?28u:87u):sf;
        PackedTexture source{sw,sh,sf,std::vector<PackedColor>(size_t(sw)*sh)};std::vector<PackedColor> bg(size_t(w)*h);
        LinearTextureDrawExecutor executor(vs,ps,w,h,format,sw,sh,sf,backend,blend,point?TextureFilter::point:TextureFilter::linear);
        for(unsigned frame=0;frame<(desktop?6u:9u);++frame){
            for(size_t n=0;n<source.pixels.size();++n){const auto alpha=frame==0?0u:frame==1?255u:unsigned((n*37+frame*17)%256);
                for(unsigned c=0;c<4;++c)source.pixels[n][c]=uint8_t(c==3?alpha:(n*71+c*51+frame*19)%(alpha+1));}
            for(size_t n=0;n<bg.size();++n)for(unsigned c=0;c<4;++c)bg[n][c]=uint8_t((n*13+c*37+frame*31)%256);
            // A narrow fractional quad amplifies a pre-snap interpolation bug.
            const std::array<float,4> rectangle=frame==4?std::array<float,4>{12.0009f,13.0011f,20.0482f,29.0519f}:
                std::array<float,4>{2.07861328125f,1.5589599609375f,float(w)-2.07861328125f,float(h)-1.5589599609375f};
            const std::array<float,4> uv{frame==3?-.125f:4.912726581096649e-08f,0,1.0000001192092896f,frame==3?1.25f:1.0000001192092896f};
            const std::array<float,4> color=frame==4?std::array<float,4>{221.f/255,221.f/255,221.f/255,221.f/255}:
                frame==5?std::array<float,4>{.125f,.75f,.375f,.8125f}:frame==6?std::array<float,4>{1,.5f,.25f,0}:
                frame==7?std::array<float,4>{0,0,0,0}:frame==8?std::array<float,4>{.25f,.5f,.75f,1}:std::array<float,4>{1,1,1,1};
            const uint32_t orders[2][6]{{0,1,2,2,3,0},{0,1,2,0,2,3}};std::vector<VertexRegisters> inputs(6);
            for(size_t n=0;n<6;++n){const auto corner=orders[frame&1][n];const bool right=corner==1||corner==2,bottom=corner>=2;
                inputs[n][0]={as_bits(rectangle[right?2:0]/(float(w)*.5f)-1),as_bits(1-rectangle[bottom?3:1]/(float(h)*.5f)),as_bits(.5f),as_bits(1.f)};
                for(unsigned c=0;c<4;++c)inputs[n][1][c]=as_bits(color[c]);inputs[n][2]={as_bits(uv[right?2:0]),as_bits(uv[bottom?3:1]),0,0};}
            const std::array<int32_t,4> clip=frame==2?std::array<int32_t,4>{5,7,int32_t(w)-9,int32_t(h)-11}:std::array<int32_t,4>{0,0,int32_t(w),int32_t(h)};
            if(!frame && !desktop)for(unsigned bad=0;bad<8;++bad){auto invalid=inputs;
                if(bad==0)invalid[1][1][0]=as_bits(.5f);if(bad==1)invalid[2][2][0]=as_bits(.5f);if(bad==2)invalid[1][0][0]=as_bits(.5f);if(bad==3)invalid[3][0][3]=as_bits(2.f);
                if(bad>=4){const float value=bad==4?-.01f:bad==5?1.01f:bad==6?std::numeric_limits<float>::quiet_NaN():std::numeric_limits<float>::infinity();
                    for(auto &v:invalid)v[1][0]=as_bits(value);}
                bool failed=false;try{(void)executor.execute(invalid,{},view,source,bg,clip);}catch(const std::invalid_argument&){failed=true;}TEST(failed);++rejected;}
            const auto result=executor.execute(inputs,{},view,source,bg,clip);TEST(result.execution_device.rfind(backend,0)==0);
            // Independent scalar VS, projection and double bilinear/source-over oracle.
            const auto a=reference_vertex(vs,inputs[0],{}),b=reference_vertex(vs,inputs[2],{});
            const std::array<float,4> screen{(as_float(a[0][0])+1)*(float(w)*.5f),(1-as_float(a[0][1]))*(float(h)*.5f),(as_float(b[0][0])+1)*(float(w)*.5f),(1-as_float(b[0][1]))*(float(h)*.5f)};
            std::array<int32_t,4> snapped{};for(unsigned c=0;c<4;++c)snapped[c]=int32_t(std::nearbyint(double(screen[c])*256));
            uint64_t covered=0;
            for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){
                const auto at=size_t(y)*w+x;const int32_t px=int32_t(x)*256+128,py=int32_t(y)*256+128;
                const bool inside=px>=snapped[0]&&px<snapped[2]&&py>=snapped[1]&&py<snapped[3]&&int32_t(x)>=clip[0]&&int32_t(y)>=clip[1]&&int32_t(x)<clip[2]&&int32_t(y)<clip[3];
                if(!inside){TEST(result.pixels[at]==bg[at]);checked+=4;continue;}++covered;
                const double u=double(uv[0])+(x+.5-double(snapped[0])/256)/(double(snapped[2]-snapped[0])/256)*(double(uv[2])-uv[0]);
                const double v=double(uv[1])+(y+.5-double(snapped[1])/256)/(double(snapped[3]-snapped[1])/256)*(double(uv[3])-uv[1]);
                const double tx=u*sw-(point?0:.5),ty=v*sh-(point?0:.5);const auto ix=int64_t(std::floor(tx)),iy=int64_t(std::floor(ty));const double fx=point?0:tx-double(ix),fy=point?0:ty-double(iy);
                double sample[4]{};for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){
                    const auto sx=std::clamp(ix+dx,int64_t(0),int64_t(sw)-1),sy=std::clamp(iy+dy,int64_t(0),int64_t(sh)-1);
                    for(unsigned c=0;c<4;++c)sample[c]+=source.pixels[size_t(sy)*sw+size_t(sx)][c]*(dx?fx:1-fx)*(dy?fy:1-fy);}
                if(opaque)sample[3]=255;
                for(unsigned c=0;c<4;++c){const auto lane=swap&&c<3?2-c:c;const double ideal=std::clamp(sample[lane]*color[format==87&&c<3?2-c:c]+(blend?bg[at][c]*(1-sample[3]*color[3]/255):0),0.,255.);
                    const auto actual=result.pixels[at][c];const double error=std::abs(actual-ideal);maximum=(std::max)(maximum,error);
                    if(error>.6){std::fprintf(stderr,"mismatch w=%u blend=%u swap=%u frame=%u x=%u y=%u c=%u got=%u ideal=%.9g\n",w,unsigned(blend),unsigned(swap),frame,x,y,c,unsigned(actual),ideal);TEST(false);}
                    if(actual!=uint8_t(std::nearbyint(ideal)))++different;++checked;}
            }
            TEST(result.covered_samples==covered);
            std::printf("{\"modulated_texture_draw\":true,\"point_filter\":%s,\"width\":%u,\"height\":%u,\"source_format\":%u,\"blend\":%s,\"swap\":%s,\"frame\":%u,\"draw_ms\":%.3f,\"covered_samples\":%llu,\"execution_device\":\"%s\"}\n",point?"true":"false",w,h,sf,blend?"true":"false",swap?"true":"false",frame,result.timings.total_ms,static_cast<unsigned long long>(covered),result.execution_device.c_str());std::fflush(stdout);
        }
    }
    std::printf("{\"checked_components\":%llu,\"rounded_oracle_differences\":%llu,\"maximum_byte_error\":%.9g,\"invalid_draws_rejected\":%u,\"desktop_offload_tested\":false,\"displayed_frames\":0}\n",static_cast<unsigned long long>(checked),static_cast<unsigned long long>(different),maximum,rejected);return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
