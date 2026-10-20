#pragma once
#include "npu_turbo_import.hpp"
#include "linear_texture_draw.hpp"
#include "opaque_affine_crop.hpp"
#include "triangle_regions.hpp"
#include "projection_executor.hpp"
#include "vertex_executor.hpp"
#include <openvino/openvino.hpp>
#include <windows.h>
#include <bcrypt.h>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <mutex>
#include <cstdio>
namespace nr::raster {
class PtlPointRectangle {
 static void require(bool v){if(!v)throw std::invalid_argument("PTL point rectangle contract");}
 static void put(unsigned char *p,unsigned v){std::memcpy(p,&v,4);}
 static unsigned get(const unsigned char *p){unsigned v;std::memcpy(&v,p,4);return v;}
 using Clock=std::chrono::steady_clock;
 static double elapsed(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
 struct Provider {
  ov::Core core;ov::CompiledModel model;ov::InferRequest inference;std::string device;std::mutex mutex;bool failed=false;
  explicit Provider(const std::string &bytes){
   std::istringstream graph(bytes,std::ios::binary);model=nr::raster::import_ptl_turbo(core,graph,"point_rectangle");auto devices=model.get_property(ov::execution_devices);require(devices.size()==1&&(devices[0]=="NPU"||devices[0]=="NPU.0"));device=devices[0];inference=model.create_infer_request();
   std::printf("{\"ptl_rectangle_provider_created\":true,\"single_import_shared\":true}\n");std::fflush(stdout);
   require(model.inputs().size()==1&&model.outputs().size()==1&&inference.get_input_tensor().get_element_type()==ov::element::f16&&inference.get_output_tensor().get_element_type()==ov::element::f16&&inference.get_input_tensor().get_shape()==ov::Shape({6,131072})&&inference.get_output_tensor().get_shape()==ov::Shape({6,131072}));
  }
 };
 static std::shared_ptr<Provider> acquire(const std::string &bytes){
  static std::mutex mutex;static std::weak_ptr<Provider> cached;std::lock_guard<std::mutex> lock(mutex);
  auto p=cached.lock();if(!p){p=std::make_shared<Provider>(bytes);cached=p;}return p;
 }
 std::shared_ptr<Provider> provider;

 shader::Program vs;unsigned width,height,sw,sh,sf,target_format,color_slot,uv_slot;bool opaque,blend,failed=false;
 shader::VertexExecutor vertex;ProjectionExecutor projection;std::string device;std::unique_ptr<OpaqueAffineCrop> crop;
public:
 static std::string verified_blob(const std::filesystem::path &path){
  std::ifstream f(path,std::ios::binary|std::ios::ate);require(bool(f)&&f.tellg()==73857);std::string bytes(73857,'\0');f.seekg(0);f.read(bytes.data(),std::streamsize(bytes.size()));require(bool(f));
  BCRYPT_ALG_HANDLE algorithm=nullptr;require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0);std::array<unsigned char,32> digest{};
  auto status=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),digest.data(),ULONG(digest.size()));BCryptCloseAlgorithmProvider(algorithm,0);require(status>=0);
  constexpr unsigned char expected[]{0xd6,0x51,0xd6,0x10,0x1d,0xbc,0xcc,0x57,0x96,0x87,0x3d,0xc2,0x01,0x6e,0xc9,0xe2,0x3d,0x26,0x2c,0xd9,0x55,0xe9,0xbc,0x4b,0xee,0x6d,0x56,0xb8,0x67,0x56,0xeb,0x2d};
  require(std::equal(digest.begin(),digest.end(),std::begin(expected)));return bytes;
 }

 static bool eligible(const shader::Program &ps){try{LinearTextureDrawExecutor::validate_shader(ps);return true;}catch(const std::invalid_argument &){return false;}}
 PtlPointRectangle(const shader::Program &v,const shader::Program &p,unsigned w,unsigned h,unsigned format,const PackedTexture &source,bool source_over,const std::filesystem::path &blob):
  vs(v),width(w),height(h),sw(source.width),sh(source.height),sf(source.format),target_format(format),color_slot(0),uv_slot(0),opaque(false),blend(source_over),vertex(v,6,"NPU"),projection(6,"NPU"){
  require(eligible(p)&&w&&w<=8192&&h&&h<=8192&&uint64_t(w)*h<=8388576&&sw&&sw<=8192&&sh&&sh<=8192&&uint64_t(sw)*sh<=8388608&&(format==28||format==87)&&(sf==28||sf==87));
  color_slot=p.instructions.back().sources[1].index;uv_slot=p.instructions.front().sources[0].index;opaque=p.instructions.size()==3;
  require(vs.stage==shader::Stage::vertex&&vs.position_output<16&&(vs.outputs[color_slot]&15)==15&&(vs.outputs[uv_slot]&3)==3);
  crop=std::make_unique<OpaqueAffineCrop>(sw,sh,w,h,format!=sf,"NPU",opaque,blend);
  provider=acquire(verified_blob(blob));device=provider->device;
 }
 UniformDrawExecutor::Result execute(const std::vector<shader::VertexRegisters> &inputs,const shader::VertexConstants &constants,const Viewport &viewport,const PackedTexture &source,const std::vector<PackedColor> &background,const std::optional<std::array<int32_t,4>> &scissor){
    auto &s=*this;const auto start=Clock::now();
    require(!s.failed && inputs.size()==6 && source.width==s.sw && source.height==s.sh && source.format==s.sf && source.pixels.size()==size_t(s.sw)*s.sh &&
        background.size()==size_t(s.width)*s.height && viewport.x==0 && viewport.y==0 && viewport.width==float(s.width) && viewport.height==float(s.height));
    auto clip=scissor.value_or(std::array<int32_t,4>{0,0,int32_t(s.width),int32_t(s.height)});
    require(clip[0]>=0 && clip[1]>=0 && clip[0]<=clip[2] && clip[1]<=clip[3] && clip[2]<=int32_t(s.width) && clip[3]<=int32_t(s.height));
    auto began=Clock::now();auto shaded=s.vertex.execute(inputs,constants);UniformDrawExecutor::Result result{};
    result.execution_device=shaded.execution_device;result.timings.shader_ms=elapsed(began);require(result.execution_device==s.device);
    result.input_bytes=shaded.input_bytes;result.output_bytes=shaded.output_bytes;++result.provider_requests;
    std::vector<ClipPosition> positions(6);
    const auto color_bits=shaded.vertices[0][s.color_slot];std::array<float,4> color{};
    std::memcpy(color.data(),color_bits.data(),16);
    for(const auto v:color)require(std::isfinite(v) && v>=0 && v<=1);
    for(size_t n=0;n<6;++n){
        require(shaded.vertices[n][s.color_slot]==color_bits);
        std::memcpy(positions[n].data(),shaded.vertices[n][s.vs.position_output].data(),16);require(positions[n][3]==1.f);
    }
    began=Clock::now();const auto projected=s.projection.execute(positions,viewport);result.timings.projection_ms=elapsed(began);require(projected.execution_device==s.device);
    result.input_bytes+=6*16+24;result.output_bytes+=6*24+2*16;++result.provider_requests;
    const auto &a=projected.vertices[0],&b=projected.vertices[2];
    const std::array<float,4> rect{a.screen[0],a.screen[1],b.screen[0],b.screen[1]};
    const std::array<int32_t,4> bounds{a.snapped[0],a.snapped[1],b.snapped[0],b.snapped[1]};
    require(rect[0]<rect[2] && rect[1]<rect[3] && bounds[0]<bounds[2] && bounds[1]<bounds[3]);
    std::array<float,4> uv{};for(unsigned c=0;c<2;++c){std::memcpy(&uv[c],&shaded.vertices[0][s.uv_slot][c],4);std::memcpy(&uv[c+2],&shaded.vertices[2][s.uv_slot][c],4);}
    for(auto v:uv)require(std::isfinite(v) && std::abs(v)<=16);
    const uint32_t orders[2][6]{{0,1,2,2,3,0},{0,1,2,0,2,3}};bool accepted=false;
    for(const auto &order:orders){bool matches=true;for(size_t n=0;n<6;++n){const auto corner=order[n];const bool right=corner==1 || corner==2,bottom=corner>=2;
        const std::array<float,2> expected{rect[right?2:0],rect[bottom?3:1]};const Point snapped{bounds[right?2:0],bounds[bottom?3:1]};
        matches &= projected.vertices[n].screen==expected && projected.vertices[n].snapped==snapped;
        float u=0,v=0;std::memcpy(&u,&shaded.vertices[n][s.uv_slot][0],4);std::memcpy(&v,&shaded.vertices[n][s.uv_slot][1],4);
        matches &= u==uv[right?2:0] && v==uv[bottom?3:1];}accepted |= matches;}
    require(accepted);
    // The provider has already projected and bounded both triangles. Schedule
    // only row blocks intersecting those bounds and the scissor; the graph
    // still decides exact sample coverage, sampling and blending.
    const auto regions=bounded_triangle_regions(projected.triangle_bounds,clip,130992,false,true);
    uint32_t first_row=s.height,end_row=0;
    for(const auto &region:regions)if(region[0]<region[2] && region[1]<region[3]){
        first_row=(std::min)(first_row,uint32_t(region[1]));end_row=(std::max)(end_row,uint32_t(region[3]));
    }
    result.pixels=background;
    if(first_row>=end_row){
        result.timings.total_ms=elapsed(start);result.used_rectangle=true;result.vertices=std::move(shaded.vertices);return result;
    }

    // Preserve the existing qualified NPU crop whenever its own mapping proof succeeds.
    // Unsupported mappings fall through to the direct NPU rectangle kernel.
    began=Clock::now();
    const unsigned crop_rows=(std::max)(1u,65535u/(std::max)(sw,width));
    const bool prefer_crop=!blend||opaque||(end_row-first_row<=crop_rows);
    std::printf("{\"ptl_rectangle_route_policy\":true,\"blend\":%s,\"opaque_alpha\":%s,\"bounded_rows\":%u,\"crop_rows_per_request\":%u,\"prefer_crop\":%s}\n",blend?"true":"false",opaque?"true":"false",end_row-first_row,crop_rows,prefer_crop?"true":"false");std::fflush(stdout);
    try{auto cropped=prefer_crop?crop->execute(bounds,clip,uv,color,source,background,true):std::optional<OpaqueAffineCrop::Result>{};if(cropped){
     require(cropped->device==device);result.timings.merger_ms=elapsed(began);result.pixels=std::move(cropped->pixels);result.covered_samples=cropped->covered;
     result.input_bytes+=cropped->input_bytes;result.output_bytes+=cropped->output_bytes;result.provider_requests+=cropped->provider_calls;result.tile_executions=cropped->provider_calls;result.used_rectangle=true;result.vertices=std::move(shaded.vertices);result.timings.total_ms=elapsed(start);
     std::printf("{\"ptl_rectangle_preserved_crop\":true,\"provider_calls\":%llu,\"execution_device\":\"NPU\"}\n",static_cast<unsigned long long>(cropped->provider_calls));std::fflush(stdout);return result;
    }}catch(...){failed=true;throw;}
    for(const auto v:bounds)require(v>=-8388608&&v<=8388608);
    for(auto &v:clip)v*=256;
    std::lock_guard<std::mutex> lock(provider->mutex);require(!provider->failed);provider->failed=true;failed=true;
    auto &request=provider->inference;auto *data=static_cast<unsigned char*>(request.get_input_tensor().data());
    const auto initialize=[&](unsigned char *p,unsigned phase,unsigned count,unsigned first){
     put(p,0x4e525231);put(p+4,1);put(p+8,phase);put(p+12,count);put(p+16,262144);put(p+20,first);put(p+24,width);put(p+28,height);put(p+32,sw);put(p+36,sh);put(p+40,target_format);put(p+44,sf);put(p+48,blend?1u:0u);put(p+52,opaque?1u:0u);
     std::memcpy(p+64,bounds.data(),16);std::memcpy(p+80,clip.data(),16);std::memcpy(p+96,uv.data(),16);std::memcpy(p+112,color.data(),16);
    };
    const auto invoke=[&]{request.start_async();if(!request.wait_for(std::chrono::milliseconds(10000)))throw std::runtime_error("PTL rectangle timeout");++result.provider_requests;++result.tile_executions;result.input_bytes+=1572864;result.output_bytes+=1572864;};
    began=Clock::now();const unsigned end=end_row*width;
    for(unsigned first=first_row*width;first<end;first+=130992){
     unsigned counts[6]{},begins[6]{},hits[6]{};std::memset(data,0,1572864);
     for(unsigned plane=0;plane<6;++plane){begins[plane]=(std::min)(end,first+plane*21832);counts[plane]=(std::min)(21832u,end-begins[plane]);initialize(data+plane*262144,0,counts[plane],begins[plane]);}
     invoke();const auto *plan=static_cast<const unsigned char*>(request.get_output_tensor().data());std::memset(data,0,1572864);unsigned total_hits=0;
     for(unsigned plane=0;plane<6;++plane){const auto *o=plan+plane*262144;auto *p=data+plane*262144;require(get(o)==0x4e52524f&&get(o+4)==0&&get(o+8)==counts[plane]&&get(o+12)<=counts[plane]);initialize(p,1,counts[plane],begins[plane]);
      for(unsigned n=0;n<counts[plane];++n){const auto coverage=get(o+16+n*8),index=get(o+20+n*8);require(coverage<=1);auto *record=p+128+n*12;std::memcpy(record,background[begins[plane]+n].data(),4);put(record+8,coverage);hits[plane]+=coverage;
       if(coverage){require(index<source.pixels.size());std::memcpy(record+4,source.pixels[index].data(),4);}else require(index==0);
      }require(hits[plane]==get(o+12));total_hits+=hits[plane];
     }
     result.covered_samples+=total_hits;if(!total_hits)continue;
     invoke();const auto *pixels=static_cast<const unsigned char*>(request.get_output_tensor().data());
     for(unsigned plane=0;plane<6;++plane){const auto *o=pixels+plane*262144;require(get(o)==0x4e52524f&&get(o+4)==1&&get(o+8)==counts[plane]&&get(o+12)==hits[plane]);std::memcpy(result.pixels.data()+begins[plane],o+16,size_t(counts[plane])*4);}
    }
    result.timings.merger_ms=elapsed(began);result.timings.total_ms=elapsed(start);result.used_rectangle=true;result.vertices=std::move(shaded.vertices);provider->failed=false;failed=false;std::printf("{\"ptl_rectangle_kernel_executed\":true,\"provider_calls\":%llu,\"execution_device\":\"NPU\"}\n",static_cast<unsigned long long>(result.provider_requests-2));std::fflush(stdout);return result;
 }
};
}
