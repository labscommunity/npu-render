#define NOMINMAX
#include "indexed_pipeline.hpp"
#include "ptl_point_rectangle.hpp"
#include "provider_compile_statistics.hpp"

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
    ex::require(argc==4||argc==5);const std::filesystem::path out=argv[3];ex::require(std::filesystem::is_directory(out));
    std::ifstream input(argv[1],std::ios::binary|std::ios::ate);ex::require(bool(input));const auto length=input.tellg();ex::require(length>0 && length<=64*1024*1024);std::vector<uint8_t> bytes(static_cast<size_t>(length));input.seekg(0);input.read(reinterpret_cast<char *>(bytes.data()),length);ex::require(bool(input));auto r=ex::decode(bytes);
    if(!(r.header.flags&0x2000)){r.resources[0]=r.texture;r.samplers[0].filter=(r.header.flags&16)?TextureFilter::point:TextureFilter::linear;}


    const auto reason=[&]() -> const char* {
     if(r.header.flags&0x3000)return "generic_pipeline";
     if(!(r.header.flags&16)||ex::payload_abi(r.header)!=5||!ex::shape_valid(r.header))return "point_rectangle_only";
     if(!PtlPointRectangle::eligible(r.pixel))return "shader";
     return "accepted";
    };
    if(argc==5){ex::require(std::string(argv[4])=="--inspect");const auto why=reason();std::printf("{\"inspection_only\":true,\"accepted\":%s,\"reason\":\"%s\",\"flags\":%u,\"vertices\":%u,\"npu_executed\":false}\n",std::strcmp(why,"accepted")==0?"true":"false",why,r.header.flags,r.header.vertices);return 0;}
    ex::require(std::strcmp(reason(),"accepted")==0);
    auto blob=PtlPointRectangle::verified_blob(argv[2]);blob[35000]^=1;save(out/"tampered.blob",blob.data(),blob.size());bool rejected=false;try{PtlPointRectangle::verified_blob(out/"tampered.blob");}catch(const std::invalid_argument &){rejected=true;}ex::require(rejected);std::filesystem::remove(out/"tampered.blob");
    auto wrong=r.pixel;wrong.instructions[0].saturate=true;ex::require(!PtlPointRectangle::eligible(wrong));
    const auto setup=Clock::now();
    PtlPointRectangle custom(r.vertex,r.pixel,r.header.width,r.header.height,r.header.format,r.texture,(r.header.flags&1)!=0,argv[2]);
    PtlPointRectangle shared(r.vertex,r.pixel,r.header.width,r.header.height,r.header.format,r.texture,(r.header.flags&1)!=0,argv[2]);
    ex::Pipeline baseline;
    LinearTextureDrawExecutor reference(r.vertex,r.pixel,r.header.width,r.header.height,r.header.format,r.texture.width,r.texture.height,r.texture.format,"CPU",(r.header.flags&1)!=0,TextureFilter::point);
    std::printf("{\"kind\":\"setup\",\"candidate_setup_ms\":%.6f,\"negative_checks\":2,\"cpu_reference_test_only\":true,\"desktop_changed\":false}\n",ms(setup));std::fflush(stdout);
    for(unsigned iteration=0;iteration<4;++iteration){
        // CPU creates deterministic test pixels; all shading and blending remain on NPU.
        unsigned random=0x6d2b79f5u+iteration*0x9e3779b9u;
        auto random_word=[&](){random=random*1664525u+1013904223u;return random;};
        auto fill=[&](auto &pixels){auto *p=reinterpret_cast<unsigned char*>(pixels.data());for(size_t at=0;at<pixels.size();++at){
            const unsigned alpha=iteration==0?255u:iteration==1?0u:iteration==2?128u:random_word()>>24;
            for(unsigned c=0;c<3;++c)p[at*4+c]=static_cast<unsigned char>(iteration<3?((at+c*71u)%(alpha+1u)):(random_word()>>16)%(alpha+1u));
            p[at*4+3]=static_cast<unsigned char>(alpha);
        }};
        if(iteration){fill(r.background);for(auto &resource:r.resources)if(!resource.pixels.empty())fill(resource.pixels);if(!(r.header.flags&0x2000))r.texture=r.resources[0];}
        UniformDrawExecutor::Result ordinary{};std::vector<PackedColor> candidate(r.background.size());double baseline_ms=0,candidate_ms=0,prepare_ms=0,raster_ms=0;unsigned calls=0;
        auto run_custom=[&]{const auto began=Clock::now();auto result=custom.execute(r.inputs,r.vertex_constants,r.viewport,r.texture,r.background,r.scissor);ex::require(result.execution_device.rfind("NPU",0)==0);candidate=std::move(result.pixels);calls=unsigned(result.provider_requests);candidate_ms=ms(began);prepare_ms=result.timings.shader_ms+result.timings.projection_ms;raster_ms=result.timings.merger_ms;};
        auto run_base=[&]{const auto began=Clock::now();ordinary=baseline.execute(r);baseline_ms=ms(began);ex::require(ordinary.execution_device.rfind("NPU",0)==0);};
        if(iteration&1){run_custom();run_base();}else{run_base();run_custom();}
        const auto *base=reinterpret_cast<const unsigned char*>(ordinary.pixels.data()),*next=reinterpret_cast<const unsigned char*>(candidate.data());uint64_t different=0;unsigned max_error=0;
        for(size_t i=0;i<candidate.size()*4;++i){const unsigned error=unsigned(std::abs(int(base[i])-int(next[i])));if(error){++different;if(different<=16)std::printf("{\"kind\":\"difference\",\"iteration\":%u,\"pixel\":%llu,\"channel\":%u,\"baseline\":%u,\"custom\":%u}\n",iteration,static_cast<unsigned long long>(i/4),unsigned(i%4),unsigned(base[i]),unsigned(next[i]));}max_error=std::max(max_error,error);}
        if(iteration==0){save(out/"baseline.rgba",ordinary.pixels.data(),ordinary.pixels.size()*4);save(out/"candidate.rgba",candidate.data(),candidate.size()*4);}
        std::printf("{\"kind\":\"pair\",\"iteration\":%u,\"order\":\"%s\",\"baseline_full_ms\":%.6f,\"candidate_full_ms\":%.6f,\"candidate_shader_and_projection_ms\":%.6f,\"candidate_merger_ms\":%.6f,\"baseline_calls\":%llu,\"candidate_calls\":%u,\"components\":%llu,\"different_components\":%llu,\"maximum_byte_error\":%u,\"exact\":%s}\n",iteration,iteration&1?"custom-first":"baseline-first",baseline_ms,candidate_ms,prepare_ms,raster_ms,static_cast<unsigned long long>(ordinary.provider_requests),calls,static_cast<unsigned long long>(candidate.size()*4),static_cast<unsigned long long>(different),max_error,different?"false":"true");std::fflush(stdout);

        const auto expected=reference.execute(r.inputs,r.vertex_constants,r.viewport,r.texture,r.background,r.scissor);const auto *p=reinterpret_cast<const unsigned char*>(expected.pixels.data());uint64_t reference_differences=0;
        for(size_t i=0;i<candidate.size()*4;++i)reference_differences+=p[i]!=next[i];
        save(out/("reference-"+std::to_string(iteration)+".rgba"),expected.pixels.data(),expected.pixels.size()*4);
        std::printf("{\"kind\":\"reference_check\",\"iteration\":%u,\"reference\":\"original rectangle graph on CPU\",\"cpu_test_reference_only\":true,\"candidate_rendering_device\":\"NPU\",\"different_components\":%llu,\"exact\":%s}\n",iteration,static_cast<unsigned long long>(reference_differences),reference_differences?"false":"true");std::fflush(stdout);
        if(different||reference_differences)return 2;

    }
    std::printf("{\"kind\":\"complete\",\"passed\":true,\"desktop_tested\":false,\"tahoma_tested\":false}\n");return 0;
}catch(const std::exception &e){std::fprintf(stderr,"point rectangle comparison: %s\n",e.what());return 1;}
