#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
namespace nr::provider::profile {
inline bool enabled(){static const bool value=[](){char *text=nullptr;size_t length=0;
    if(_dupenv_s(&text,&length,"NR_RENDER_INFER_PROFILE"))throw std::runtime_error("cannot read provider profile setting");
    if(!text)return false;const std::string setting(text);std::free(text);
    if(setting!="0" && setting!="1")throw std::invalid_argument("provider profile requires 0 or 1");return setting=="1";}();return value;}
struct Site{const char *name=nullptr;uint64_t calls=0,failed=0,nanoseconds=0;};
struct Totals{std::array<Site,32> sites{};uint64_t overflow=0;};
inline Totals &totals(){static thread_local Totals value;return value;}
inline void reset(){if(enabled())totals()={};}
inline void record(const char *file,std::chrono::steady_clock::time_point began,bool failed)noexcept{
    const auto ended=std::chrono::steady_clock::now();const char *site=std::strrchr(file,'\\');if(!site)site=std::strrchr(file,'/');site=site?site+1:file;
    auto &value=totals();for(auto &entry:value.sites){if(!entry.name || !std::strcmp(entry.name,site)){
        entry.name=site;++entry.calls;entry.failed+=failed?1u:0u;entry.nanoseconds+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(ended-began).count());return;}}
    ++value.overflow;
}
inline void emit(uint32_t sequence,uint32_t ticket,uint64_t input_bytes,uint64_t output_bytes,
                 double shader,double projection,double coverage,double merger,double draw){
    if(!enabled())return;const auto &value=totals();
    std::printf("{\"provider_draw_profile\":true,\"sequence\":%u,\"dispatch_ticket\":%u,\"input_bytes\":%llu,\"output_bytes\":%llu,\"shader_ms\":%.6f,\"projection_ms\":%.6f,\"coverage_ms\":%.6f,\"merger_ms\":%.6f,\"draw_ms\":%.6f,\"unclassified_inferences\":%llu,\"sites\":[",sequence,ticket,static_cast<unsigned long long>(input_bytes),static_cast<unsigned long long>(output_bytes),shader,projection,coverage,merger,draw,static_cast<unsigned long long>(value.overflow));
    bool comma=false;for(const auto &entry:value.sites){if(!entry.name)continue;
        std::printf("%s{\"site\":\"%s\",\"calls\":%llu,\"failed\":%llu,\"inference_ms\":%.6f}",comma?",":"",entry.name,static_cast<unsigned long long>(entry.calls),static_cast<unsigned long long>(entry.failed),double(entry.nanoseconds)/1e6);comma=true;}
    std::printf("]}\n");std::fflush(stdout);
}
}
