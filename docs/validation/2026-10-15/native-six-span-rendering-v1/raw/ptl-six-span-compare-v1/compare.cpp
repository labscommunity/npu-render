#define NOMINMAX
#include "indexed_pipeline.hpp"
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
    std::ifstream input(argv[1],std::ios::binary|std::ios::ate);ex::require(bool(input));const auto length=input.tellg();ex::require(length>0 && length<=64*1024*1024);std::vector<uint8_t> bytes(static_cast<size_t>(length));input.seekg(0);input.read(reinterpret_cast<char *>(bytes.data()),length);ex::require(bool(input));const auto r=ex::decode(bytes);
    ex::require(r.inputs.size()==6 && r.header.width==1024 && r.header.height==768 && r.header.format==87 && r.header.flags==12039 && r.resources[0].width==2880 && r.resources[0].height==1800 && r.resources[0].format==87 && r.pixel.instructions.size()==15 && r.scissor==std::optional<std::array<int32_t,4>>({0,64,1024,704}));
    ex::require(r.samplers[0].filter==TextureFilter::linear && unsigned(r.samplers[0].u)==0 && unsigned(r.samplers[0].v)==0);
    for(unsigned n=1;n<16;++n)ex::require(!r.resources[n].width);
    const auto setup=Clock::now();VertexExecutor vertex(r.vertex,6,"NPU");ProjectionExecutor projection(6,"NPU");
    ov::Core core;std::ifstream graph(argv[2],std::ios::binary);ex::require(bool(graph));auto model=core.import_model(graph,"NPU");const auto devices=model.get_property(ov::execution_devices);ex::require(devices.size()==1 && devices[0].rfind("NPU",0)==0);auto request=model.create_infer_request();ex::require(request.get_input_tensor(0).get_byte_size()==1572864 && request.get_output_tensor().get_size()==786432);
    ex::Pipeline baseline;std::printf("{\"kind\":\"setup\",\"candidate_setup_ms\":%.6f,\"candidate_device\":\"%s\",\"desktop_changed\":false}\n",ms(setup),devices[0].c_str());std::fflush(stdout);
    for(unsigned iteration=0;iteration<3;++iteration){
        UniformDrawExecutor::Result ordinary{};std::vector<PackedColor> candidate(r.background.size());double baseline_ms=0,candidate_ms=0,prepare_ms=0,raster_ms=0;unsigned calls=0;
        auto run_custom=[&]{const auto began=Clock::now();auto shaded=vertex.execute(r.inputs,r.vertex_constants);ex::require(shaded.execution_device.rfind("NPU",0)==0);std::vector<ClipPosition> clips(6);for(size_t n=0;n<6;++n)std::memcpy(clips[n].data(),shaded.vertices[n][r.vertex.position_output].data(),16);auto projected=projection.execute(clips,r.viewport);ex::require(projected.execution_device==shaded.execution_device);calls=2;
            const unsigned corners[6]{0,1,2,2,3,0};const auto &a=projected.vertices[0],&c=projected.vertices[2];
            for(unsigned n=0;n<6;++n){const unsigned k=corners[n];const bool right=k==1||k==2,bottom=k>=2;const auto &v=projected.vertices[n];
                ex::require(v.screen==std::array<float,2>{right?c.screen[0]:a.screen[0],bottom?c.screen[1]:a.screen[1]} && v.snapped==Point{right?c.snapped[0]:a.snapped[0],bottom?c.snapped[1]:a.snapped[1]} && v.inverse_w==1.0f);
                ex::require(shaded.vertices[n][1]==VertexBits{as_bits(1.f),as_bits(1.f),as_bits(1.f),as_bits(1.f)} && shaded.vertices[n][2]==VertexBits{as_bits(right?1.f:0.f),as_bits(bottom?1.f:0.f),0,0});}
            auto *data=static_cast<unsigned char *>(request.get_input_tensor(0).data());std::memset(data,0,1572864);const unsigned fields[]{0x4e525231,3,2880,1800,1024,768,0,4096,1,1,87,87,0,15,262144,0};std::memcpy(data,fields,sizeof(fields));const float color[]{1,1,1,1},uv[]{0,0,1,1},screen[]{a.screen[0],a.screen[1],c.screen[0],c.screen[1]};const int bounds[]{a.snapped[0],a.snapped[1],c.snapped[0],c.snapped[1]},scissor[]{0,64*256,1024*256,704*256};std::memcpy(data+64,color,16);std::memcpy(data+80,uv,16);std::memcpy(data+96,bounds,16);std::memcpy(data+112,scissor,16);std::memcpy(data+128,screen,16);put(data+144,1);
            ex::require(a.snapped==Point{0,64*256} && c.snapped==Point{1024*256,704*256} && a.screen==std::array<float,2>{0,64.f+1.f/131072} && c.screen==std::array<float,2>{1024,704});prepare_ms=ms(began);const auto raster=Clock::now();
            for(unsigned group=0;group<1024*768;group+=6*4096){
                for(unsigned plane=0;plane<6;++plane){
                    auto *payload=data+plane*262144;if(plane)std::memcpy(payload,data,192);const unsigned first=group+plane*4096;put(payload+24,first);
                    const int y=int(first/1024);const unsigned top=(y<64 || y>=704)?0u:unsigned(std::max(0,(y-64)*45/16-4));const unsigned bottom=(y<64 || y>=704)?1u:unsigned(std::min(1800,(y+4-64)*45/16+4));const size_t source_bytes=size_t(bottom-top)*2880*4;
                    ex::require(192+source_bytes+16384<=262144);put(payload+160,0);put(payload+164,top);put(payload+168,2880);put(payload+172,bottom-top);
                    std::memcpy(payload+192,r.resources[0].pixels.data()+top*2880,source_bytes);std::memcpy(payload+192+source_bytes,r.background.data()+first,16384);
                }
                if(iteration==0 && group==49152)save(out/"actual-input.bin",data,1572864);request.infer();++calls;
                const auto output=request.get_output_tensor();const auto *values=output.data<const ov::float16>();
                for(unsigned plane=0;plane<6;++plane){auto *dest=reinterpret_cast<unsigned char*>(candidate.data()+group+plane*4096);
                    for(size_t at=0;at<16384;++at){const float v=float(values[plane*131072+at]);ex::require(v>=0 && v<=255 && float(static_cast<unsigned char>(v))==v);dest[at]=static_cast<unsigned char>(v);}}
            }
            raster_ms=ms(raster);candidate_ms=ms(began);
        };
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
