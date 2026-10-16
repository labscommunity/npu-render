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
#include <cmath>

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
        const auto &t=r.resources[0];
        const auto sc=r.scissor.value_or(std::array<int32_t,4>{0,0,int32_t(h.width),int32_t(h.height)});
        if((h.abi!=8 && h.abi!=19) || (h.flags!=12039 && h.flags!=12035) ||
           !h.width || h.width>4096 || !h.height || h.height>4096 ||
           h.format!=87 || h.vertices!=6 || r.inputs.size()!=6 ||
           r.background.size()!=size_t(h.width)*h.height || !t.width || t.width>4096 ||
           !t.height || t.height>4096 || t.format!=87 || t.pixels.size()!=size_t(t.width)*t.height ||
           sc[0]<0 || sc[1]<0 ||
           sc[2]<sc[0] || sc[3]<sc[1] ||
           sc[2]>int(h.width) || sc[3]>int(h.height) ||
           r.viewport.x!=0 || r.viewport.y!=0 || r.viewport.width!=float(h.width) ||
           r.viewport.height!=float(h.height) || r.viewport.min_depth!=0 || r.viewport.max_depth!=1 ||
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
        constexpr unsigned char expected[]{0x71,0x0f,0x6e,0x55,0x86,0xda,0x59,0xf4,0x04,0x02,0x13,0x43,0x5c,0x5e,0xcc,0x13,0xbb,0x66,0x9e,0x6b,0x1b,0xe6,0x68,0xf8,0x38,0xed,0xd0,0x0b,0xca,0x5d,0xd6,0x97};
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
        const auto &a=projected.vertices[0],&c=projected.vertices[2];
        const bool second_order=projected.vertices[3].screen==a.screen;
        const unsigned order0[6]{0,1,2,2,3,0},order1[6]{0,1,2,0,2,3};
        const unsigned *corners=second_order?order1:order0;
        const auto color_bits=shaded.vertices[0][1];
        const auto uv0=shaded.vertices[0][2],uv2=shaded.vertices[2][2];
        float color[4],uv[4],screen[]{a.screen[0],a.screen[1],c.screen[0],c.screen[1]};
        std::memcpy(color,color_bits.data(),16);
        const unsigned uv_bits[]{uv0[0],uv0[1],uv2[0],uv2[1]};std::memcpy(uv,uv_bits,16);
        bool admitted=a.screen[0]<c.screen[0] && a.screen[1]<c.screen[1] &&
            a.snapped[0]<c.snapped[0] && a.snapped[1]<c.snapped[1];
        for(unsigned k=0;k<4;++k) admitted=admitted && color[k]>=0 && color[k]<=1 &&
            uv[k]>=-16 && uv[k]<=16 && screen[k]>=0 && screen[k]<=4096;
        for(unsigned n=0;n<6;++n) {
            const unsigned k=corners[n];const bool right=k==1||k==2,bottom=k>=2;
            const auto &v=projected.vertices[n];
            admitted=admitted && v.screen==std::array<float,2>{right?c.screen[0]:a.screen[0],bottom?c.screen[1]:a.screen[1]} &&
                v.snapped==raster::Point{right?c.snapped[0]:a.snapped[0],bottom?c.snapped[1]:a.snapped[1]} && v.inverse_w==1.0f &&
                shaded.vertices[n][1]==color_bits &&
                shaded.vertices[n][2]==shader::VertexBits{right?uv2[0]:uv0[0],bottom?uv2[1]:uv0[1],0,0};
        }
        if(!admitted) { result.timings.total_ms=elapsed(start); failed=false; return attempt; }
        const unsigned w=r.header.width,h=r.header.height,sw=r.resources[0].width,sh=r.resources[0].height;
        const auto sc=r.scissor.value_or(std::array<int32_t,4>{0,0,int32_t(w),int32_t(h)});
        struct Job {unsigned first,count,top,bottom;};std::vector<Job> jobs;
        const unsigned y0=unsigned(std::max(sc[1],std::min(int(h),int(std::floor(a.snapped[1]/256.0)))));
        const unsigned y1=unsigned(std::max(sc[1],std::min(sc[3],int(std::ceil(c.snapped[1]/256.0)))));
        for(unsigned y=y0;y<y1;) {
            unsigned rows=std::min(4096/w,y1-y),top=0,bottom=sh;
            for(;;) {
                // A conservative source-row envelope for the complete destination
                // band and helper lanes. The device rejects any uncovered texel.
                const double lo=uv[1]+((double(y)-.25-screen[1])/(screen[3]-screen[1]))*(uv[3]-uv[1]);
                const double hi=uv[1]+((double(y+rows)+.25-screen[1])/(screen[3]-screen[1]))*(uv[3]-uv[1]);
                top=unsigned(std::clamp(int(std::floor(std::min(lo,hi)*sh))-3,0,int(sh)-1));
                bottom=unsigned(std::clamp(int(std::ceil(std::max(lo,hi)*sh))+3,int(top)+1,int(sh)));
                if(192+size_t(bottom-top)*sw*4+size_t(rows)*w*4<=262144)break;
                if(rows==1){std::printf("{\"ptl_geometry_declined\":\"source_band_capacity\",\"source_rows\":%u}\n",bottom-top);std::fflush(stdout);result.timings.total_ms=elapsed(start);failed=false;return attempt;}
                --rows;
            }
            jobs.push_back({y*w,rows*w,top,bottom});y+=rows;
        }
        result.vertices=std::move(shaded.vertices);result.pixels=r.background;
        auto *data=static_cast<unsigned char*>(inference.get_input_tensor().data());std::memset(data,0,1572864);
        const unsigned fields[]{0x4e525231,4,sw,sh,w,h,0,0,1,1,87,87,0,15,262144,0};
        std::memcpy(data,fields,sizeof(fields));
        const int bounds[]{a.snapped[0],a.snapped[1],c.snapped[0],c.snapped[1]},scissor[]{sc[0]*256,sc[1]*256,sc[2]*256,sc[3]*256};
        std::memcpy(data+64,color,16);std::memcpy(data+80,uv,16);std::memcpy(data+96,bounds,16);
        std::memcpy(data+112,scissor,16);std::memcpy(data+128,screen,16);put(data+144,1);put(data+148,second_order?1:0);
        const auto raster_start=Clock::now();
        for(size_t group=0;group<jobs.size();group+=6) {
            for(unsigned plane=0;plane<6;++plane) {
                auto *payload=data+plane*262144;if(plane)std::memcpy(payload,data,192);
                const Job j=group+plane<jobs.size()?jobs[group+plane]:Job{0,0,0,1};
                put(payload+24,j.first);put(payload+28,j.count);
                const size_t source_bytes=size_t(j.bottom-j.top)*sw*4;
                require(192+source_bytes+size_t(j.count)*4<=262144);
                put(payload+160,0);put(payload+164,j.top);put(payload+168,sw);put(payload+172,j.bottom-j.top);
                std::memcpy(payload+192,r.resources[0].pixels.data()+size_t(j.top)*sw,source_bytes);
                std::memcpy(payload+192+source_bytes,r.background.data()+j.first,size_t(j.count)*4);
            }
            inference.infer();++result.provider_requests;++result.tile_executions;
            result.input_bytes+=1572864;result.output_bytes+=1572864;
            const auto tensor=inference.get_output_tensor();const auto *values=tensor.data<const ov::float16>();
            for(unsigned plane=0;plane<6 && group+plane<jobs.size();++plane) {
                const auto &j=jobs[group+plane];auto *dest=reinterpret_cast<unsigned char*>(result.pixels.data()+j.first);
                for(size_t at=0;at<size_t(j.count)*4;++at) {
                    const float value=float(values[plane*131072+at]);
                    require(value>=0 && value<=255 && float(static_cast<unsigned char>(value))==value);
                    dest[at]=static_cast<unsigned char>(value);
                }
            }
        }
        result.timings.merger_ms=elapsed(raster_start); result.timings.total_ms=elapsed(start);
        auto first_sample=[](int fixed){return int(std::ceil((fixed-128)/256.0));};
        const int left=std::max(sc[0],first_sample(a.snapped[0])),right=std::min(sc[2],first_sample(c.snapped[0]));
        const int top=std::max(sc[1],first_sample(a.snapped[1])),bottom=std::min(sc[3],first_sample(c.snapped[1]));
        result.covered_samples=uint64_t(std::max(0,right-left))*unsigned(std::max(0,bottom-top)); result.used_rectangle=true; attempt.accepted=true; failed=false;
        return attempt;
    }
};
}
