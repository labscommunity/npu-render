#include "compact_coverage.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
using namespace nr::raster;
namespace {
void test(bool v){if(!v)throw std::runtime_error("compact coverage oracle failed");}
int64_t edge(Point a,Point b,Point p){return (int64_t(b[0])-a[0])*(int64_t(p[1])-a[1])-(int64_t(b[1])-a[1])*(int64_t(p[0])-a[0]);}
bool reference(Sample s,const std::array<int32_t,4> &clip,CullMode cull,bool ccw){
    auto &v=s.triangle;const auto area=edge(v[0],v[1],v[2]);if(!area)return false;const bool front=(area<0)==ccw;
    if((cull==CullMode::back && !front) || (cull==CullMode::front && front))return false;
    if(s.point[0]<clip[0] || s.point[1]<clip[1] || s.point[0]>=clip[2] || s.point[1]>=clip[3])return false;
    if(area<0)std::swap(v[1],v[2]);for(unsigned i=0;i<3;++i){const auto a=v[i],b=v[(i+1)%3];const auto e=edge(a,b,s.point);const bool top=b[1]<a[1] || (b[1]==a[1] && b[0]>a[0]);if(e<0 || (!e && !top))return false;}return true;
}
uint32_t random32(){static uint32_t x=0x145791ad;x^=x<<13;x^=x>>17;x^=x<<5;return x;}
void fill(std::vector<Sample> &samples,unsigned frame){
    for(size_t i=0;i<samples.size();++i){auto &s=samples[i];for(auto &p:s.triangle)p={int32_t(random32()%4096),int32_t(random32()%4096)};s.point={int32_t(random32()%4096),int32_t(random32()%4096)};
        if(frame==0){s.triangle={{{0,0},{4095,0},{0,4095}}};s.point={int32_t(i%64)*64+32,int32_t((i/64)%64)*64+32};}
        if(frame==2){s.triangle={{{0,0},{4095,4094},{4094,4093}}};s.point={2048,2048-int32_t(i%3)};test(edge(s.triangle[0],s.triangle[1],s.triangle[2])==-1);}
        if(frame==3)s.point=s.triangle[i%3];if(frame==4)s.triangle[2]=s.triangle[1];
        if(frame==8){for(auto &p:s.triangle)p[1]=317;s.point[1]=317;}
        if(frame==9){s.triangle={{{0,0},{4095,0},{4095,4095}}};s.point={0,4095};}
        if(i&1)std::swap(s.triangle[0],s.triangle[1]);
        int64_t sx=1,sy=1,ox=0,oy=0;
        if(frame==5){sx=sy=65536;ox=oy=coordinate_limit-4095*sx;}
        if(frame==6){sx=sy=65536;ox=oy=-coordinate_limit;}
        if(frame==7){sx=128;sy=1024;ox=-17;oy=31;}
        if(frame==10){ox=-2048;oy=-2048;}
        if(frame==11){sx=256;sy=128;ox=100000;oy=-100000;}
        for(auto &p:s.triangle){p[0]=int32_t(int64_t(p[0])*sx+ox);p[1]=int32_t(int64_t(p[1])*sy+oy);}s.point[0]=int32_t(int64_t(s.point[0])*sx+ox);s.point[1]=int32_t(int64_t(s.point[1])*sy+oy);
    }
}
uint64_t verify(const CoverageExecutor::Result &r,const std::vector<Sample> &samples,const std::array<int32_t,4> &clip,CullMode cull,bool ccw,const std::string &backend){
    test(r.covered.size()==samples.size() && r.execution_device.rfind(backend,0)==0);
    for(size_t i=0;i<samples.size();++i){const auto want=reference(samples[i],clip,cull,ccw);if(bool(r.covered[i])!=want){std::fprintf(stderr,"coverage mismatch sample=%zu actual=%u expected=%u\n",i,r.covered[i],unsigned(want));test(false);}}return samples.size();
}
using Clock=std::chrono::steady_clock;double elapsed(Clock::time_point s){return std::chrono::duration<double,std::milli>(Clock::now()-s).count();}
}
int main(int argc,char **argv)try{
    test(argc<=2);const std::string backend=argc==2?argv[1]:"CPU";test(backend=="CPU" || backend=="NPU" || backend=="GPU");constexpr uint32_t count=4096;uint64_t checked=0;std::vector<Sample> samples(count);const std::array<int32_t,4> full{-coordinate_limit,-coordinate_limit,coordinate_limit+1,coordinate_limit+1};
    for(const auto cull:{CullMode::none,CullMode::front,CullMode::back})for(const bool ccw:{false,true}){
        compact::Executor fast(count,backend,cull,ccw);
        for(unsigned frame=0;frame<12;++frame){fill(samples,frame);auto clip=full;if(frame==0)clip={5,7,3001,2999};if(frame==5)clip={coordinate_limit-100000,coordinate_limit-200000,coordinate_limit,coordinate_limit};if(frame==6)clip={-coordinate_limit+1,-coordinate_limit+1,-coordinate_limit+200000,-coordinate_limit+100000};if(frame==11)clip={100001,-99999,200003,100005};
            checked+=verify(fast.execute(samples,clip),samples,clip,cull,ccw,backend);
        }
    }
    std::printf("{\"compact_oracle\":true,\"verified_values\":%llu,\"cull_winding_modes\":6,\"geometry_frames_per_mode\":12,\"execution_device\":\"%s\"}\n",static_cast<unsigned long long>(checked),backend.c_str());std::fflush(stdout);
    {
        compact::Executor fast(count,backend);fill(samples,0);const auto good=samples[0];
        samples[0]={{{{0,0},{4096,0},{0,4096}}},{1,1}};bool denied=false;try{fast.execute(samples,full);}catch(const compact::NotApplicable &){denied=true;}test(denied);
        samples[0]=good;samples[0].point[0]=INT32_MAX;denied=false;try{fast.execute(samples,full);}catch(const std::invalid_argument &){denied=true;}test(denied);
        samples[0]=good;checked+=verify(fast.execute(samples,full),samples,full,CullMode::none,false,backend);
        CoverageExecutor general(count,backend);
        for(unsigned round=0;round<12;++round){fill(samples,round%2);CoverageExecutor::Result a,b;double fast_ms=0,wide_ms=0;
            const auto run_fast=[&]{auto start=Clock::now();a=fast.execute(samples,full);fast_ms=elapsed(start);};const auto run_wide=[&]{auto start=Clock::now();b=general.execute(samples,full);wide_ms=elapsed(start);};if(round&1){run_fast();run_wide();}else{run_wide();run_fast();}
            test(a.covered==b.covered && b.execution_device.rfind(backend,0)==0);checked+=verify(a,samples,full,CullMode::none,false,backend);
            std::printf("{\"profile\":true,\"round\":%u,\"compact_first\":%s,\"compact_ms\":%.3f,\"wide_ms\":%.3f,\"execution_device\":\"%s\",\"coverage_exact\":true}\n",round,round&1?"true":"false",fast_ms,wide_ms,backend.c_str());std::fflush(stdout);
        }
    }
    std::printf("{\"passed\":true,\"total_verified_values\":%llu,\"domain_and_invalid_rejections\":2,\"renderer_integrated\":false}\n",static_cast<unsigned long long>(checked));return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
