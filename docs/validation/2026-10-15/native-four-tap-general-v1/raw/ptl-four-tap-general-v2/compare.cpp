#define NOMINMAX
#include "indexed_pipeline.hpp"
#include "ptl_four_tap.hpp"
#include "provider_compile_statistics.hpp"
#include "shader_reference.hpp"
#include <openvino/openvino.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cstdio>
namespace ex=nr::registered_lab::indexed_exchange;
using namespace nr::shader;using namespace nr::raster;
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
void put(unsigned char *p,unsigned value){std::memcpy(p,&value,4);}
void save(const std::filesystem::path &path,const void *data,size_t size){std::ofstream f(path,std::ios::binary);ex::require(bool(f));f.write(static_cast<const char*>(data),std::streamsize(size));ex::require(bool(f));}
int main(int argc,char **argv)try{
    ex::require(argc==4);const std::filesystem::path out=argv[3];ex::require(std::filesystem::is_directory(out));
    std::ifstream input(argv[1],std::ios::binary|std::ios::ate);ex::require(bool(input));const auto length=input.tellg();ex::require(length>0 && length<=64*1024*1024);std::vector<uint8_t> bytes(static_cast<size_t>(length));input.seekg(0);input.read(reinterpret_cast<char *>(bytes.data()),length);ex::require(bool(input));auto r=ex::decode(bytes);
    std::printf("{\"kind\":\"static_admission\",\"eligible\":%s,\"explicit_scissor\":%s}\n",ex::PtlFourTap::eligible(r)?"true":"false",r.scissor?"true":"false");std::fflush(stdout);
    ex::require(ex::PtlFourTap::eligible(r));
    const auto saved_flags=r.header.flags;r.header.flags^=1;ex::require(!ex::PtlFourTap::eligible(r));r.header.flags=saved_flags;
    const auto saved_word=r.pixel.bytecode[40];r.pixel.bytecode[40]^=1;ex::require(!ex::PtlFourTap::eligible(r));r.pixel.bytecode[40]=saved_word;
    const auto saved_scissor=r.scissor;r.scissor=std::array<int32_t,4>{-1,0,int(r.header.width),int(r.header.height)};ex::require(!ex::PtlFourTap::eligible(r));r.scissor=saved_scissor;
    auto bad_blob=ex::PtlFourTap::verified_blob(argv[2]);bad_blob[35000]^=1;save(out/"tampered.blob",bad_blob.data(),bad_blob.size());
    bool rejected=false;try{ex::PtlFourTap::verified_blob(out/"tampered.blob");}catch(const std::invalid_argument &){rejected=true;}ex::require(rejected);std::filesystem::remove(out/"tampered.blob");
    const auto setup=Clock::now();ex::PtlFourTap custom(r,argv[2]);ex::Pipeline baseline;
    const auto original_color=r.inputs[0][1][0];r.inputs[0][1][0]=as_bits(.5f);const auto denied=custom.execute(r);ex::require(!denied.accepted && denied.result.provider_requests==2 && denied.result.pixels.empty());r.inputs[0][1][0]=original_color;
    std::printf("{\"kind\":\"admission\",\"passed\":true,\"negative_checks\":5,\"geometry_checked_on_npu\":true}\n");
    std::printf("{\"kind\":\"setup\",\"candidate_setup_ms\":%.6f,\"desktop_changed\":false}\n",ms(setup));std::fflush(stdout);
    for(unsigned iteration=0;iteration<4;++iteration){
        // CPU creates deterministic test pixels; all shading and blending remain on NPU.
        unsigned random=0x6d2b79f5u+iteration*0x9e3779b9u;
        auto random_word=[&](){random=random*1664525u+1013904223u;return random;};
        auto fill=[&](auto &pixels){auto *p=reinterpret_cast<unsigned char*>(pixels.data());for(size_t at=0;at<pixels.size();++at){
            const unsigned alpha=iteration==0?255u:iteration==1?0u:iteration==2?128u:random_word()>>24;
            for(unsigned c=0;c<3;++c)p[at*4+c]=static_cast<unsigned char>(iteration<4?((at+c*71u)%(alpha+1u)):(random_word()>>16)%(alpha+1u));
            p[at*4+3]=static_cast<unsigned char>(alpha);
        }};
        if(iteration){fill(r.resources[0].pixels);fill(r.background);}
        UniformDrawExecutor::Result ordinary{};std::vector<PackedColor> candidate(r.background.size());double baseline_ms=0,candidate_ms=0,prepare_ms=0,raster_ms=0;unsigned calls=0;
        auto run_custom=[&]{const auto began=Clock::now();auto attempt=custom.execute(r);ex::require(attempt.accepted && attempt.result.provider_requests>=2 && attempt.result.used_rectangle);candidate=std::move(attempt.result.pixels);calls=unsigned(attempt.result.provider_requests);candidate_ms=ms(began);prepare_ms=attempt.result.timings.shader_ms+attempt.result.timings.projection_ms;raster_ms=attempt.result.timings.merger_ms;};
        auto run_base=[&]{const auto began=Clock::now();ordinary=baseline.execute(r);baseline_ms=ms(began);ex::require(ordinary.execution_device.rfind("NPU",0)==0);};
        if(iteration&1){run_custom();run_base();}else{run_base();run_custom();}
        const auto *base=reinterpret_cast<const unsigned char*>(ordinary.pixels.data()),*next=reinterpret_cast<const unsigned char*>(candidate.data());uint64_t different=0;unsigned max_error=0;
        for(size_t i=0;i<candidate.size()*4;++i){const unsigned error=unsigned(std::abs(int(base[i])-int(next[i])));if(error){++different;if(different<=16)std::printf("{\"kind\":\"difference\",\"iteration\":%u,\"pixel\":%llu,\"channel\":%u,\"baseline\":%u,\"custom\":%u}\n",iteration,static_cast<unsigned long long>(i/4),unsigned(i%4),unsigned(base[i]),unsigned(next[i]));}max_error=std::max(max_error,error);}
        if(iteration==0){save(out/"baseline.rgba",ordinary.pixels.data(),ordinary.pixels.size()*4);save(out/"candidate.rgba",candidate.data(),candidate.size()*4);}
        std::printf("{\"kind\":\"pair\",\"iteration\":%u,\"order\":\"%s\",\"baseline_full_ms\":%.6f,\"candidate_full_ms\":%.6f,\"candidate_prepare_ms\":%.6f,\"candidate_raster_ms\":%.6f,\"baseline_calls\":%llu,\"candidate_calls\":%u,\"components\":%llu,\"different_components\":%llu,\"maximum_byte_error\":%u,\"exact\":%s}\n",iteration,iteration&1?"custom-first":"baseline-first",baseline_ms,candidate_ms,prepare_ms,raster_ms,static_cast<unsigned long long>(ordinary.provider_requests),calls,static_cast<unsigned long long>(candidate.size()*4),static_cast<unsigned long long>(different),max_error,different?"false":"true");std::fflush(stdout);
        if(different)return 2;
    }
    std::printf("{\"kind\":\"complete\",\"passed\":true,\"desktop_tested\":false,\"tahoma_tested\":false}\n");return 0;
}catch(const std::exception &e){std::fprintf(stderr,"four tap comparison: %s\n",e.what());return 1;}
