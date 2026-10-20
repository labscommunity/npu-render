#pragma once
#include "npu_turbo_import.hpp"
#include "indexed_exchange_protocol.hpp"
#include "ptl_solid_shaders.hpp"
#include <openvino/openvino.hpp>
#include <windows.h>
#include <bcrypt.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cmath>

namespace nr::registered_lab::indexed_exchange {
// Experimental provider for one verified shader and geometry contract. All
// vertex, projection, coverage, sampling and blending operations run on NPU.
// The host validates admission, copies strips and assembles byte outputs.
class PtlSolid {
    using Clock=std::chrono::steady_clock;
    ov::Core core;
    ov::CompiledModel model;
    ov::InferRequest inference;
    std::unique_ptr<shader::VertexExecutor> vertex;
    std::unique_ptr<raster::ProjectionExecutor> projection;
    std::string device;
    bool failed=false;
    unsigned vertices=0;
    static double elapsed(Clock::time_point start) {
        return std::chrono::duration<double,std::milli>(Clock::now()-start).count();
    }
    static void put(unsigned char *p,unsigned value) { std::memcpy(p,&value,4); }
public:
    struct Attempt { bool accepted=false; raster::UniformDrawExecutor::Result result{}; };
    static bool eligible(const Request &r) {
        const auto &h=r.header;
        if((h.abi!=1 && h.abi!=3) || (h.flags!=3840 && h.flags!=3841 && h.flags!=3844 && h.flags!=3845) ||
           !h.width || h.width>8192 || !h.height || h.height>8192 || h.format!=87 ||
           h.vertices<=6 || h.vertices>168 || h.vertices%6 || r.inputs.size()!=h.vertices ||
           r.background.size()!=size_t(h.width)*h.height ||
           r.viewport.x!=0 || r.viewport.y!=0 || r.viewport.width!=float(h.width) ||
           r.viewport.height!=float(h.height) || r.viewport.min_depth!=0 || r.viewport.max_depth!=1 ||
           r.vertex.bytecode.size()!=ptl_solid_fixture::vertex.size() || r.pixel.bytecode.size()!=ptl_solid_fixture::pixel.size()) return false;
        const auto sc=r.scissor.value_or(std::array<int32_t,4>{0,0,int32_t(h.width),int32_t(h.height)});
        if(sc[0]<0 || sc[1]<0 || sc[2]<sc[0] || sc[3]<sc[1] || sc[2]>int(h.width) || sc[3]>int(h.height))return false;
        if(!std::equal(r.vertex.bytecode.begin(),r.vertex.bytecode.end(),ptl_solid_fixture::vertex.begin()) ||
           !std::equal(r.pixel.bytecode.begin(),r.pixel.bytecode.end(),ptl_solid_fixture::pixel.begin())) return false;
        for(unsigned n=0;n<16;++n) if(r.resources[n].width || r.resources[n].height || !r.resources[n].pixels.empty()) return false;
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
        constexpr unsigned char expected[]{0x21,0x36,0x03,0x21,0xc2,0x1d,0x4a,0x02,0x53,0xb2,0x60,0xce,0x62,0x42,0x0f,0xfe,0xf4,0x7e,0xc0,0x3d,0xaa,0xe8,0xb8,0x9e,0x6c,0xf0,0x5b,0xd4,0xc5,0x41,0xee,0x8f};
        require(std::equal(digest.begin(),digest.end(),std::begin(expected)));
        return bytes;
    }
    PtlSolid(const Request &r,const std::filesystem::path &path) {
        require(eligible(r));
        const auto bytes=verified_blob(path); std::istringstream graph(bytes,std::ios::binary);
        model=nr::raster::import_ptl_turbo(core,graph,"solid");
        const auto devices=model.get_property(ov::execution_devices);
        require(devices.size()==1 && (devices[0]=="NPU" || devices[0]=="NPU.0")); device=devices[0];
        inference=model.create_infer_request();
        require(model.inputs().size()==1 && model.outputs().size()==1 &&
                inference.get_input_tensor().get_element_type()==ov::element::f16 &&
                inference.get_input_tensor().get_shape()==ov::Shape({6,131072}) &&
                inference.get_output_tensor().get_element_type()==ov::element::f16 &&
                inference.get_output_tensor().get_shape()==ov::Shape({6,131072}));
        vertices=r.header.vertices;vertex=std::make_unique<shader::VertexExecutor>(r.vertex,vertices,"NPU");
        projection=std::make_unique<raster::ProjectionExecutor>(vertices,"NPU");
    }
    Attempt execute(const Request &r) {
        require(!failed && eligible(r) && r.header.vertices==vertices); failed=true;
        const auto start=Clock::now(); Attempt attempt; auto &result=attempt.result;
        auto shaded=vertex->execute(r.inputs,r.vertex_constants);
        require(shaded.execution_device==device); result.execution_device=device;
        result.input_bytes=shaded.input_bytes; result.output_bytes=shaded.output_bytes;
        result.provider_requests=1; result.timings.shader_ms=elapsed(start);
        std::vector<raster::ClipPosition> clips(vertices);
        for(size_t n=0;n<vertices;++n) std::memcpy(clips[n].data(),shaded.vertices[n][r.vertex.position_output].data(),16);
        const auto projection_start=Clock::now(); auto projected=projection->execute(clips,r.viewport);
        require(projected.execution_device==device); ++result.provider_requests;
        result.input_bytes+=uint64_t(vertices)*16+24; result.output_bytes+=uint64_t(vertices)*24+uint64_t(vertices/3)*16;
        result.timings.projection_ms=elapsed(projection_start);
        const auto color=shaded.vertices[0][1];float color_f[4];std::memcpy(color_f,color.data(),16);
        bool admitted=true;for(float f:color_f)admitted=admitted && f>=0 && f<=1;
        for(unsigned base=0;base<vertices;base+=6){
            const auto &a=projected.vertices[base],&c=projected.vertices[base+2];
            const unsigned order0[6]{0,1,2,2,3,0},order1[6]{0,1,2,0,2,3};
            const unsigned *order=projected.vertices[base+3].snapped==a.snapped?order1:order0;
            admitted=admitted && a.snapped[0]<c.snapped[0] && a.snapped[1]<c.snapped[1];
            for(unsigned n=0;n<6;++n){const auto &v=projected.vertices[base+n];const unsigned k=order[n];
                admitted=admitted && v.snapped==raster::Point{k==1||k==2?c.snapped[0]:a.snapped[0],k>=2?c.snapped[1]:a.snapped[1]} &&
                    v.inverse_w==1 && shaded.vertices[base+n][1]==color;
                for(int coordinate:v.snapped)admitted=admitted && coordinate>=-8388608 && coordinate<=8388608;
            }
        }
        if(!admitted){result.timings.total_ms=elapsed(start);failed=false;return attempt;}
        const unsigned w=r.header.width,h=r.header.height,total=w*h;
        const auto sc=r.scissor.value_or(std::array<int32_t,4>{0,0,int32_t(w),int32_t(h)});
        result.vertices=std::move(shaded.vertices);result.fragment=color;result.pixels.resize(total);
        auto *data=static_cast<unsigned char*>(inference.get_input_tensor().data());std::memset(data,0,1572864);
        const unsigned fields[]{0x4e525332,1,w,h,0,0,vertices/6,r.header.flags&1,87,15,unsigned(sc[0]*256),unsigned(sc[1]*256),unsigned(sc[2]*256),unsigned(sc[3]*256),0,262144};
        std::memcpy(data,fields,64);
        for(unsigned base=0;base<vertices;base+=6){auto *p=data+64+(base/6)*32;const auto &a=projected.vertices[base],&c=projected.vertices[base+2];
            const int bounds[]{a.snapped[0],a.snapped[1],c.snapped[0],c.snapped[1]};std::memcpy(p,bounds,16);std::memcpy(p+16,color.data(),16);
        }
        const auto raster_start=Clock::now();
        for(unsigned group=0;group<total;group+=6*65280){
            for(unsigned plane=0;plane<6;++plane){auto *payload=data+plane*262144;if(plane)std::memcpy(payload,data,1024);
                const unsigned first=std::min(total,group+plane*65280),count=std::min(65280u,total-first);put(payload+16,first);put(payload+20,count);
                std::memcpy(payload+1024,r.background.data()+first,size_t(count)*4);
            }
            inference.infer();++result.provider_requests;++result.tile_executions;result.input_bytes+=1572864;result.output_bytes+=1572864;
            const auto tensor=inference.get_output_tensor();const auto *output=static_cast<const unsigned char*>(tensor.data());
            for(unsigned plane=0;plane<6;++plane){const unsigned first=std::min(total,group+plane*65280),count=std::min(65280u,total-first);const auto *p=output+plane*262144;
                unsigned header[4];std::memcpy(header,p,16);require(header[0]==0x4e52534f && header[1]==count && header[2]<=count*(vertices/6) && header[3]==0);
                result.covered_samples+=header[2];std::memcpy(result.pixels.data()+first,p+16,size_t(count)*4);
            }
        }
        result.timings.merger_ms=elapsed(raster_start);result.timings.total_ms=elapsed(start);
        result.used_rectangle=true;attempt.accepted=true;failed=false;
        return attempt;
    }
};
}
