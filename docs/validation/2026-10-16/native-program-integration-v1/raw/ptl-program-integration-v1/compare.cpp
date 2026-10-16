#define NOMINMAX
#include "indexed_pipeline.hpp"
#include "ptl_program.hpp"
#include "ptl_program_triangle_draw.hpp"
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
    ex::require(argc==4||argc==5);const std::filesystem::path out=argv[3];ex::require(std::filesystem::is_directory(out));
    std::ifstream input(argv[1],std::ios::binary|std::ios::ate);ex::require(bool(input));const auto length=input.tellg();ex::require(length>0 && length<=64*1024*1024);std::vector<uint8_t> bytes(static_cast<size_t>(length));input.seekg(0);input.read(reinterpret_cast<char *>(bytes.data()),length);ex::require(bool(input));auto r=ex::decode(bytes);
    if(argc==5){ex::require(std::string(argv[4])=="--inspect");std::printf("{\"inspection_only\":true,\"accepted\":%s,\"npu_executed\":false}\n",PtlProgramRaster::eligible(r.pixel,r.resources,r.samplers)?"true":"false");return 0;}
    auto blob=PtlProgramRaster::verified_blob(argv[2]);blob[35000]^=1;save(out/"tampered.blob",blob.data(),blob.size());
    bool rejected=false;try{PtlProgramRaster::verified_blob(out/"tampered.blob");}catch(const std::invalid_argument &){rejected=true;}ex::require(rejected);std::filesystem::remove(out/"tampered.blob");
    ex::require(PtlProgramRaster::eligible(r.pixel,r.resources,r.samplers));
    unsigned negative_checks=1;
    auto wrong_shader=r.pixel;wrong_shader.instructions[0].opcode=Opcode::add;ex::require(!PtlProgramRaster::eligible(wrong_shader,r.resources,r.samplers));++negative_checks;
    wrong_shader=r.pixel;wrong_shader.instructions[0].destination.mask=0;ex::require(!PtlProgramRaster::eligible(wrong_shader,r.resources,r.samplers));++negative_checks;
    wrong_shader=r.pixel;wrong_shader.instructions[0].sources[0].modifier=Modifier::negate;ex::require(!PtlProgramRaster::eligible(wrong_shader,r.resources,r.samplers));++negative_checks;
    wrong_shader=r.pixel;wrong_shader.outputs[1]=15;ex::require(!PtlProgramRaster::eligible(wrong_shader,r.resources,r.samplers));++negative_checks;
    wrong_shader=r.pixel;wrong_shader.instructions.resize(9);ex::require(!PtlProgramRaster::eligible(wrong_shader,r.resources,r.samplers));++negative_checks;
    const auto lowered=PtlProgram::lower(r.pixel);
    if(!lowered.samples.empty()){
     const auto &sample=lowered.samples[0];auto wrong_resources=r.resources;wrong_resources[sample.texture].format=0;ex::require(!PtlProgramRaster::eligible(r.pixel,wrong_resources,r.samplers));++negative_checks;
     auto wrong_samplers=r.samplers;wrong_samplers[sample.sampler].filter=TextureFilter::point;ex::require(!PtlProgramRaster::eligible(r.pixel,r.resources,wrong_samplers));++negative_checks;
     wrong_samplers=r.samplers;wrong_samplers[sample.sampler].u=TextureAddress::mirror;ex::require(!PtlProgramRaster::eligible(r.pixel,r.resources,wrong_samplers));++negative_checks;
    }
    const auto setup=Clock::now();unsigned capacity=3;while(capacity<r.header.vertices)capacity=std::min(4095u,capacity*2);
    const auto batch=(std::min)(12288u,ex::triangle_batch(r,RegisterExecutor::preferred_raster_batch(r.pixel)));
    PtlProgramTriangleDrawExecutor custom(r.vertex,r.pixel,capacity,r.header.width,r.header.height,r.header.format,r.resources,r.samplers,"NPU",ex::blend_mode(r.header),uint8_t((r.header.flags>>8)&15),static_cast<CullMode>((r.header.flags>>5)&3),(r.header.flags&128)!=0,batch);ex::Pipeline baseline;
    PtlProgramRaster shared_check(r.pixel,r.resources,r.samplers,static_cast<CullMode>((r.header.flags>>5)&3),(r.header.flags&128)!=0,r.header.format,ex::blend_mode(r.header),uint8_t((r.header.flags>>8)&15),"NPU");
    std::printf("{\"kind\":\"admission\",\"passed\":true,\"negative_checks\":%u,\"bounded_program\":true,\"instructions\":%zu,\"samples\":%zu}\n",negative_checks,lowered.instructions.size(),lowered.samples.size());
    std::printf("{\"kind\":\"setup\",\"candidate_setup_ms\":%.6f,\"batch\":%u,\"desktop_changed\":false}\n",ms(setup),batch);std::fflush(stdout);
    for(unsigned iteration=0;iteration<4;++iteration){
        // CPU creates deterministic test pixels; all shading and blending remain on NPU.
        unsigned random=0x6d2b79f5u+iteration*0x9e3779b9u;
        auto random_word=[&](){random=random*1664525u+1013904223u;return random;};
        auto fill=[&](auto &pixels){auto *p=reinterpret_cast<unsigned char*>(pixels.data());for(size_t at=0;at<pixels.size();++at){
            const unsigned alpha=iteration==0?255u:iteration==1?0u:iteration==2?128u:random_word()>>24;
            for(unsigned c=0;c<3;++c)p[at*4+c]=static_cast<unsigned char>(iteration<3?((at+c*71u)%(alpha+1u)):(random_word()>>16)%(alpha+1u));
            p[at*4+3]=static_cast<unsigned char>(alpha);
        }};
        if(iteration){fill(r.background);for(auto &resource:r.resources)if(!resource.pixels.empty())fill(resource.pixels);}
        UniformDrawExecutor::Result ordinary{};std::vector<PackedColor> candidate(r.background.size());double baseline_ms=0,candidate_ms=0,prepare_ms=0,raster_ms=0;unsigned calls=0;
        auto run_custom=[&]{const auto began=Clock::now();auto result=custom.execute(r.inputs,r.vertex_constants,r.pixel_constants,r.viewport,r.resources,r.background,r.scissor,r.blend_constant);ex::require(result.execution_device.rfind("NPU",0)==0);candidate=std::move(result.pixels);calls=unsigned(result.provider_requests);candidate_ms=ms(began);prepare_ms=result.timings.shader_ms+result.timings.projection_ms;raster_ms=result.timings.merger_ms;};
        auto run_base=[&]{const auto began=Clock::now();ordinary=baseline.execute(r);baseline_ms=ms(began);ex::require(ordinary.execution_device.rfind("NPU",0)==0);};
        if(iteration&1){run_custom();run_base();}else{run_base();run_custom();}
        const auto *base=reinterpret_cast<const unsigned char*>(ordinary.pixels.data()),*next=reinterpret_cast<const unsigned char*>(candidate.data());uint64_t different=0;unsigned max_error=0;
        for(size_t i=0;i<candidate.size()*4;++i){const unsigned error=unsigned(std::abs(int(base[i])-int(next[i])));if(error){++different;if(different<=16)std::printf("{\"kind\":\"difference\",\"iteration\":%u,\"pixel\":%llu,\"channel\":%u,\"baseline\":%u,\"custom\":%u}\n",iteration,static_cast<unsigned long long>(i/4),unsigned(i%4),unsigned(base[i]),unsigned(next[i]));}max_error=std::max(max_error,error);}
        if(iteration==0){save(out/"baseline.rgba",ordinary.pixels.data(),ordinary.pixels.size()*4);save(out/"candidate.rgba",candidate.data(),candidate.size()*4);}
        std::printf("{\"kind\":\"pair\",\"iteration\":%u,\"order\":\"%s\",\"baseline_full_ms\":%.6f,\"candidate_full_ms\":%.6f,\"candidate_shader_and_projection_ms\":%.6f,\"candidate_merger_ms\":%.6f,\"baseline_calls\":%llu,\"candidate_calls\":%u,\"components\":%llu,\"different_components\":%llu,\"maximum_byte_error\":%u,\"exact\":%s}\n",iteration,iteration&1?"custom-first":"baseline-first",baseline_ms,candidate_ms,prepare_ms,raster_ms,static_cast<unsigned long long>(ordinary.provider_requests),calls,static_cast<unsigned long long>(candidate.size()*4),static_cast<unsigned long long>(different),max_error,different?"false":"true");std::fflush(stdout);
        if(different)return 2;
    }
    std::printf("{\"kind\":\"complete\",\"passed\":true,\"desktop_tested\":false,\"tahoma_tested\":false}\n");return 0;
}catch(const std::exception &e){std::fprintf(stderr,"bounded program comparison: %s\n",e.what());return 1;}
