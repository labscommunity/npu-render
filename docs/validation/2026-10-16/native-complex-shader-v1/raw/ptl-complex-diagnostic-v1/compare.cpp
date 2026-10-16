#define NOMINMAX
#include "indexed_pipeline.hpp"
#include "ptl_complex_shader.hpp"
#include "ptl_complex_triangle_draw.hpp"
#include "provider_compile_statistics.hpp"
#include "reference_triangle.hpp"
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
    ex::require(argc==4||argc==5);const bool cpu_reference=argc==5;ex::require(!cpu_reference||std::string(argv[4])=="--cpu-reference");const std::filesystem::path out=argv[3];ex::require(std::filesystem::is_directory(out));
    std::ifstream input(argv[1],std::ios::binary|std::ios::ate);ex::require(bool(input));const auto length=input.tellg();ex::require(length>0 && length<=64*1024*1024);std::vector<uint8_t> bytes(static_cast<size_t>(length));input.seekg(0);input.read(reinterpret_cast<char *>(bytes.data()),length);ex::require(bool(input));auto r=ex::decode(bytes);
    if(!(r.header.flags&0x2000)){r.resources[0]=r.texture;r.samplers[0].filter=(r.header.flags&16)?TextureFilter::point:TextureFilter::linear;}


    std::printf("{\"inspection\":true,\"flags\":%u,\"vertices\":%u,\"pixel_words\":%zu,\"expected_words\":%zu,\"shader_equal\":%s,\"eligible\":%s}\n",r.header.flags,r.header.vertices,r.pixel.bytecode.size(),ptl_complex_fixture::pixel.size(),r.pixel.bytecode.size()==ptl_complex_fixture::pixel.size()&&std::equal(r.pixel.bytecode.begin(),r.pixel.bytecode.end(),ptl_complex_fixture::pixel.begin())?"true":"false",PtlComplexShader::eligible(r.pixel,r.resources,r.samplers)?"true":"false");
    for(unsigned i=0;i<16;++i){const auto &resource=r.resources[i];const auto &sampler=r.samplers[i];if(resource.width||resource.height||resource.format)std::printf("{\"resource\":%u,\"width\":%u,\"height\":%u,\"format\":%u,\"pixels\":%zu,\"filter\":%u,\"u\":%u,\"v\":%u}\n",i,resource.width,resource.height,resource.format,resource.pixels.size(),unsigned(sampler.filter),unsigned(sampler.u),unsigned(sampler.v));}std::fflush(stdout);
    ex::require(PtlComplexShader::eligible(r.pixel,r.resources,r.samplers));
    unsigned negative_checks=0;
    auto wrong=r.pixel;wrong.bytecode[10]^=1;ex::require(!PtlComplexShader::eligible(wrong,r.resources,r.samplers));++negative_checks;
    auto invalid=r.resources;invalid[2].width=1;ex::require(!PtlComplexShader::eligible(r.pixel,invalid,r.samplers));++negative_checks;
    auto bad_sampler=r.samplers;bad_sampler[0].u=TextureAddress::border;ex::require(!PtlComplexShader::eligible(r.pixel,r.resources,bad_sampler));++negative_checks;
    auto bad_blob=PtlComplexShader::verified_blob(argv[2]);bad_blob[35000]^=1;save(out/"tampered.blob",bad_blob.data(),bad_blob.size());
    bool rejected=false;try{PtlComplexShader::verified_blob(out/"tampered.blob");}catch(const std::invalid_argument &){rejected=true;}ex::require(rejected);std::filesystem::remove(out/"tampered.blob");++negative_checks;
    const auto setup=Clock::now();unsigned capacity=3;while(capacity<r.header.vertices)capacity=std::min(4095u,capacity*2);
    unsigned batch=RegisterExecutor::preferred_raster_batch(r.pixel);
    const auto clip=r.scissor.value_or(std::array<int32_t,4>{0,0,int32_t(r.header.width),int32_t(r.header.height)});
    const auto area=uint64_t(clip[2]-clip[0])*uint64_t(clip[3]-clip[1]);while(batch>384&&area<=batch/2)batch/=2;
    PtlComplexTriangleDrawExecutor custom(r.vertex,r.pixel,capacity,r.header.width,r.header.height,r.header.format,r.resources,r.samplers,"NPU",ex::blend_mode(r.header),uint8_t((r.header.flags>>8)&15),static_cast<CullMode>((r.header.flags>>5)&3),(r.header.flags&128)!=0,batch);ex::Pipeline baseline;
    std::unique_ptr<ReferenceTriangleDrawExecutor> reference;
    if(cpu_reference)reference=std::make_unique<ReferenceTriangleDrawExecutor>(r.vertex,r.pixel,capacity,r.header.width,r.header.height,r.header.format,r.resources,r.samplers,"NPU",ex::blend_mode(r.header),uint8_t((r.header.flags>>8)&15),static_cast<CullMode>((r.header.flags>>5)&3),(r.header.flags&128)!=0,batch);
    std::printf("{\"kind\":\"comparison_reference\",\"cpu_shader_test_oracle\":%s,\"candidate_shader_backend\":\"NPU\",\"cpu_fallback_in_candidate\":false}\n",cpu_reference?"true":"false");
    std::printf("{\"kind\":\"admission\",\"passed\":true,\"negative_checks\":%u,\"shader_exactly_pinned\":true}\n",negative_checks);
    std::printf("{\"kind\":\"setup\",\"candidate_setup_ms\":%.6f,\"batch\":%u,\"desktop_changed\":false}\n",ms(setup),batch);std::fflush(stdout);
    for(unsigned iteration=0;iteration<1;++iteration){
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
        auto run_custom=[&]{const auto began=Clock::now();auto result=custom.execute(r.inputs,r.vertex_constants,r.pixel_constants,r.viewport,r.resources,r.background,r.scissor,r.blend_constant);ex::require(result.execution_device.rfind("NPU",0)==0);candidate=std::move(result.pixels);calls=unsigned(result.provider_requests);candidate_ms=ms(began);prepare_ms=result.timings.shader_ms+result.timings.projection_ms;raster_ms=result.timings.merger_ms;save(out/("candidate-"+std::to_string(iteration)+".rgba"),candidate.data(),candidate.size()*4);std::printf("{\"kind\":\"candidate_completed\",\"iteration\":%u,\"elapsed_ms\":%.6f,\"provider_requests\":%u}\n",iteration,candidate_ms,calls);std::fflush(stdout);};
        auto run_base=[&]{const auto began=Clock::now();ordinary=reference?reference->execute(r.inputs,r.vertex_constants,r.pixel_constants,r.viewport,r.resources,r.background,r.scissor,r.blend_constant):baseline.execute(r);baseline_ms=ms(began);ex::require(ordinary.execution_device.rfind("NPU",0)==0);};
        if(!(iteration&1)){run_custom();run_base();}else{run_base();run_custom();}
        const auto *base=reinterpret_cast<const unsigned char*>(ordinary.pixels.data()),*next=reinterpret_cast<const unsigned char*>(candidate.data());uint64_t different=0;unsigned max_error=0;
        for(size_t i=0;i<candidate.size()*4;++i){const unsigned error=unsigned(std::abs(int(base[i])-int(next[i])));if(error){++different;if(different<=16)std::printf("{\"kind\":\"difference\",\"iteration\":%u,\"pixel\":%llu,\"channel\":%u,\"baseline\":%u,\"custom\":%u}\n",iteration,static_cast<unsigned long long>(i/4),unsigned(i%4),unsigned(base[i]),unsigned(next[i]));}max_error=std::max(max_error,error);}
        if(iteration==0){save(out/"baseline.rgba",ordinary.pixels.data(),ordinary.pixels.size()*4);save(out/"candidate.rgba",candidate.data(),candidate.size()*4);}
        std::printf("{\"kind\":\"pair\",\"iteration\":%u,\"order\":\"%s\",\"baseline_full_ms\":%.6f,\"candidate_full_ms\":%.6f,\"candidate_shader_and_projection_ms\":%.6f,\"candidate_merger_ms\":%.6f,\"baseline_calls\":%llu,\"candidate_calls\":%u,\"components\":%llu,\"different_components\":%llu,\"maximum_byte_error\":%u,\"exact\":%s}\n",iteration,iteration&1?"baseline-first":"custom-first",baseline_ms,candidate_ms,prepare_ms,raster_ms,static_cast<unsigned long long>(ordinary.provider_requests),calls,static_cast<unsigned long long>(candidate.size()*4),static_cast<unsigned long long>(different),max_error,different?"false":"true");std::fflush(stdout);
        if(different)return 2;
    }
    std::printf("{\"kind\":\"complete\",\"passed\":true,\"desktop_tested\":false,\"tahoma_tested\":false}\n");return 0;
}catch(const std::exception &e){std::fprintf(stderr,"complex shader comparison: %s\n",e.what());return 1;}
