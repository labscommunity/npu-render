#pragma once
#include "indexed_exchange_protocol.hpp"
#include "ptl_four_tap_shaders.hpp"
#include <openvino/openvino.hpp>
#include <windows.h>
#include <bcrypt.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace nr::registered_lab::indexed_exchange {
// Experimental provider for one verified shader and geometry contract. All
// vertex, projection, coverage, sampling and blending operations run on NPU.
// The host validates admission, copies strips and assembles byte outputs.
class PtlFourTap {
    using Clock=std::chrono::steady_clock;
    ov::Core core;
    ov::CompiledModel model;
    ov::InferRequest inference;
    std::unique_ptr<shader::VertexExecutor> vertex;
    std::unique_ptr<raster::ProjectionExecutor> projection;
    std::string device;
    bool failed=false;
    static double elapsed(Clock::time_point start) {
        return std::chrono::duration<double,std::milli>(Clock::now()-start).count();
    }
    static void put(unsigned char *p,unsigned value) { std::memcpy(p,&value,4); }
public:
    struct Attempt { bool accepted=false; raster::UniformDrawExecutor::Result result{}; };
    static bool eligible(const Request &r) {
        const auto &h=r.header;
        if(h.abi!=19 || h.flags!=12039 || h.width!=1024 || h.height!=768 ||
           h.format!=87 || h.vertices!=6 || r.inputs.size()!=6 ||
           r.background.size()!=1024*768 || r.resources[0].width!=2880 ||
           r.resources[0].height!=1800 || r.resources[0].format!=87 ||
           r.resources[0].pixels.size()!=2880*1800 ||
           r.scissor!=std::optional<std::array<int32_t,4>>({0,64,1024,704}) ||
           r.viewport.x!=0 || r.viewport.y!=0 || r.viewport.width!=1024 ||
           r.viewport.height!=768 || r.viewport.min_depth!=0 || r.viewport.max_depth!=1 ||
           r.samplers[0].filter!=shader::TextureFilter::linear ||
           unsigned(r.samplers[0].u)!=0 || unsigned(r.samplers[0].v)!=0 ||
           r.vertex.bytecode.size()!=ptl_fixture::vertex.size() ||
           r.pixel.bytecode.size()!=ptl_fixture::pixel.size()) return false;
        if(!std::equal(r.vertex.bytecode.begin(),r.vertex.bytecode.end(),ptl_fixture::vertex.begin()) ||
           !std::equal(r.pixel.bytecode.begin(),r.pixel.bytecode.end(),ptl_fixture::pixel.begin())) return false;
        for(unsigned n=1;n<16;++n) if(r.resources[n].width || r.resources[n].height || !r.resources[n].pixels.empty()) return false;
        return true;
    }
    static std::string verified_blob(const std::filesystem::path &path) {
        std::ifstream input(path,std::ios::binary|std::ios::ate);
        require(bool(input) && input.tellg()==73857);
        std::string bytes(73857,'\0'); input.seekg(0); input.read(bytes.data(),std::streamsize(bytes.size())); require(bool(input));
        BCRYPT_ALG_HANDLE algorithm=nullptr;
        require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0);
        std::array<unsigned char,32> digest{};
        const auto status=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),digest.data(),ULONG(digest.size()));
        BCryptCloseAlgorithmProvider(algorithm,0); require(status>=0);
        constexpr unsigned char expected[]{0xe0,0xac,0xa5,0x57,0x41,0x5d,0xba,0x5f,0xc6,0x51,0xe1,0xb4,0x7e,0x67,0xa3,0x3e,0xc2,0x8c,0xc6,0xe2,0x0b,0xe7,0x15,0xba,0x70,0x66,0x45,0x7f,0x8e,0xba,0x4c,0xba};
        require(std::equal(digest.begin(),digest.end(),std::begin(expected)));
        return bytes;
    }
    PtlFourTap(const Request &r,const std::filesystem::path &path) {
        require(eligible(r));
        const auto bytes=verified_blob(path); std::istringstream graph(bytes,std::ios::binary);
        model=core.import_model(graph,"NPU");
        const auto devices=model.get_property(ov::execution_devices);
        require(devices.size()==1 && (devices[0]=="NPU" || devices[0]=="NPU.0")); device=devices[0];
        inference=model.create_infer_request();
        require(model.inputs().size()==1 && model.outputs().size()==1 &&
                inference.get_input_tensor().get_element_type()==ov::element::f16 &&
                inference.get_input_tensor().get_shape()==ov::Shape({6,131072}) &&
                inference.get_output_tensor().get_element_type()==ov::element::f16 &&
                inference.get_output_tensor().get_shape()==ov::Shape({6,131072}));
        vertex=std::make_unique<shader::VertexExecutor>(r.vertex,6,"NPU");
        projection=std::make_unique<raster::ProjectionExecutor>(6,"NPU");
    }
    Attempt execute(const Request &r) {
        require(!failed && eligible(r)); failed=true;
        const auto start=Clock::now(); Attempt attempt; auto &result=attempt.result;
        auto shaded=vertex->execute(r.inputs,r.vertex_constants);
        require(shaded.execution_device==device); result.execution_device=device;
        result.input_bytes=shaded.input_bytes; result.output_bytes=shaded.output_bytes;
        result.provider_requests=1; result.timings.shader_ms=elapsed(start);
        std::vector<raster::ClipPosition> clips(6);
        for(size_t n=0;n<6;++n) std::memcpy(clips[n].data(),shaded.vertices[n][r.vertex.position_output].data(),16);
        const auto projection_start=Clock::now(); auto projected=projection->execute(clips,r.viewport);
        require(projected.execution_device==device); ++result.provider_requests;
        result.input_bytes+=6*16+24; result.output_bytes+=6*16+6*8+2*16;
        result.timings.projection_ms=elapsed(projection_start);
        const unsigned corners[6]{0,1,2,2,3,0};
        const auto &a=projected.vertices[0],&c=projected.vertices[2];
        bool admitted=a.snapped==raster::Point{0,64*256} && c.snapped==raster::Point{1024*256,704*256} &&
            a.screen==std::array<float,2>{0,64.f+1.f/131072} && c.screen==std::array<float,2>{1024,704};
        for(unsigned n=0;n<6;++n) {
            const unsigned k=corners[n]; const bool right=k==1||k==2,bottom=k>=2;
            const auto &v=projected.vertices[n];
            admitted=admitted && v.screen==std::array<float,2>{right?c.screen[0]:a.screen[0],bottom?c.screen[1]:a.screen[1]} &&
                v.snapped==raster::Point{right?c.snapped[0]:a.snapped[0],bottom?c.snapped[1]:a.snapped[1]} && v.inverse_w==1.0f &&
                shaded.vertices[n][1]==shader::VertexBits{0x3f800000u,0x3f800000u,0x3f800000u,0x3f800000u} &&
                shaded.vertices[n][2]==shader::VertexBits{right?0x3f800000u:0u,bottom?0x3f800000u:0u,0,0};
        }
        if(!admitted) { result.timings.total_ms=elapsed(start); failed=false; return attempt; }
        result.vertices=std::move(shaded.vertices); result.pixels.resize(r.background.size());
        auto *data=static_cast<unsigned char*>(inference.get_input_tensor().data());
        std::memset(data,0,1572864);
        const unsigned fields[]{0x4e525231,3,2880,1800,1024,768,0,4096,1,1,87,87,0,15,262144,0};
        std::memcpy(data,fields,sizeof(fields));
        const float color[]{1,1,1,1},uv[]{0,0,1,1},screen[]{a.screen[0],a.screen[1],c.screen[0],c.screen[1]};
        const int bounds[]{a.snapped[0],a.snapped[1],c.snapped[0],c.snapped[1]},scissor[]{0,64*256,1024*256,704*256};
        std::memcpy(data+64,color,16); std::memcpy(data+80,uv,16); std::memcpy(data+96,bounds,16);
        std::memcpy(data+112,scissor,16); std::memcpy(data+128,screen,16); put(data+144,1);
        const auto raster_start=Clock::now();
        for(unsigned group=0;group<1024*768;group+=6*4096) {
            for(unsigned plane=0;plane<6;++plane) {
                auto *payload=data+plane*262144; if(plane) std::memcpy(payload,data,192);
                const unsigned first=group+plane*4096; put(payload+24,first);
                const int y=int(first/1024);
                const unsigned top=(y<64 || y>=704)?0u:unsigned(std::max(0,(y-64)*45/16-4));
                const unsigned bottom=(y<64 || y>=704)?1u:unsigned(std::min(1800,(y+4-64)*45/16+4));
                const size_t source_bytes=size_t(bottom-top)*2880*4;
                require(192+source_bytes+16384<=262144);
                put(payload+160,0); put(payload+164,top); put(payload+168,2880); put(payload+172,bottom-top);
                std::memcpy(payload+192,r.resources[0].pixels.data()+top*2880,source_bytes);
                std::memcpy(payload+192+source_bytes,r.background.data()+first,16384);
            }
            inference.infer(); ++result.provider_requests; ++result.tile_executions;
            result.input_bytes+=1572864; result.output_bytes+=1572864;
            const auto tensor=inference.get_output_tensor(); const auto *values=tensor.data<const ov::float16>();
            for(unsigned plane=0;plane<6;++plane) {
                auto *dest=reinterpret_cast<unsigned char*>(result.pixels.data()+group+plane*4096);
                for(size_t at=0;at<16384;++at) {
                    const float value=float(values[plane*131072+at]);
                    require(value>=0 && value<=255 && float(static_cast<unsigned char>(value))==value);
                    dest[at]=static_cast<unsigned char>(value);
                }
            }
        }
        result.timings.merger_ms=elapsed(raster_start); result.timings.total_ms=elapsed(start);
        result.covered_samples=1024*640; result.used_rectangle=true; attempt.accepted=true; failed=false;
        return attempt;
    }
};
}
