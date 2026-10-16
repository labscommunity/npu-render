#include "clear_executor.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
using namespace nr::raster;
namespace {void test(bool value){if(!value)throw std::runtime_error("color clear contract failed");}}
int main(int argc,char **argv)try{
    const std::string backend=argc>1?argv[1]:"CPU";uint64_t checked=0;unsigned rejected=0;
    std::vector<Color> colors{{-2,0,1,3},{.5f,.25f,.75f,1},{1.f/255,127.f/255,128.f/255,254.f/255},
        {-(std::numeric_limits<float>::max)(),(std::numeric_limits<float>::max)(),std::numeric_limits<float>::denorm_min(),-std::numeric_limits<float>::denorm_min()}};
    for(unsigned k=0;k<255;++k){const float x=(float(k)+.5f)/255;
        colors.push_back({std::nextafter(x,-std::numeric_limits<float>::infinity()),x,std::nextafter(x,std::numeric_limits<float>::infinity()),1});}
    for(const auto shape:{std::array<uint32_t,2>{1,1},{65,7},{1024,768},{1025,769}})for(const auto format:{28u,87u}){
        ClearExecutor executor(shape[0],shape[1],format,backend);
        for(const auto bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}){
            bool failed=false;try{(void)executor.execute({bad,0,0,1});}catch(const std::invalid_argument&){failed=true;}test(failed);++rejected;
        }
        // Exhaust every UNORM half-byte transition on the small image. The
        // larger outputs exercise image generation, odd dimensions and swizzle.
        const auto count=shape[0]==1?colors.size():size_t(16);
        for(size_t n=0;n<count;++n){const auto &color=colors[n];const auto out=executor.execute(color);
            test(out.execution_device==backend || out.execution_device.rfind(backend+".",0)==0);
            test(out.pixels.size()==size_t(shape[0])*shape[1]);PackedColor expected{};
            for(unsigned c=0;c<4;++c){const auto slot=format==87 && c<3?2-c:c;expected[c]=uint8_t(std::nearbyint(std::clamp(color[slot],0.f,1.f)*255));}
            for(const auto &pixel:out.pixels){
                if(pixel!=expected){std::fprintf(stderr,"clear mismatch width=%u height=%u format=%u color_index=%zu input=%.9g,%.9g,%.9g,%.9g actual=%u,%u,%u,%u expected=%u,%u,%u,%u\n",shape[0],shape[1],format,n,color[0],color[1],color[2],color[3],pixel[0],pixel[1],pixel[2],pixel[3],expected[0],expected[1],expected[2],expected[3]);}
                test(pixel==expected);checked+=4;
            }
        }
    }
    for(const auto shape:{std::array<uint32_t,3>{0,1,28},{8193,1,28},{8192,8192,28},{1,1,65}}){
        bool failed=false;try{ClearExecutor invalid(shape[0],shape[1],shape[2],backend);}catch(const std::invalid_argument&){failed=true;}test(failed);++rejected;
    }
    std::printf("{\"color_clear\":true,\"verified_components\":%llu,\"invalid_requests_rejected\":%u,\"execution_device\":\"%s\",\"desktop_offload_tested\":false}\n",static_cast<unsigned long long>(checked),rejected,backend.c_str());return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
