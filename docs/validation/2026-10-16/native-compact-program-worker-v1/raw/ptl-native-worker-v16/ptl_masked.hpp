#pragma once
#include "projection_executor.hpp"
#include "vertex_executor.hpp"
#include "packed_resources.hpp"
#include "ptl_masked_shaders.hpp"
#include <openvino/openvino.hpp>
#include <windows.h>
#include <bcrypt.h>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <mutex>
#include <cstdio>
namespace nr::raster {
class PtlMasked {
 struct Provider {
  ov::Core core;ov::CompiledModel model;ov::InferRequest inference;std::string device;std::mutex mutex;bool failed=false;
  explicit Provider(const std::string &bytes){
   std::istringstream graph(bytes,std::ios::binary);model=core.import_model(graph,"NPU");auto devices=model.get_property(ov::execution_devices);require(devices.size()==1&&(devices[0]=="NPU"||devices[0]=="NPU.0"));device=devices[0];inference=model.create_infer_request();
   std::printf("{\"ptl_masked_provider_created\":true,\"single_import_shared\":true}\n");std::fflush(stdout);
   require(model.inputs().size()==1&&model.outputs().size()==1&&inference.get_input_tensor().get_element_type()==ov::element::f16&&inference.get_output_tensor().get_element_type()==ov::element::f16&&inference.get_input_tensor().get_shape()==ov::Shape({6,131072})&&inference.get_output_tensor().get_shape()==ov::Shape({6,131072}));
  }
 };
 static std::shared_ptr<Provider> acquire(const std::string &bytes){
  static std::mutex mutex;static std::weak_ptr<Provider> cached;std::lock_guard<std::mutex> lock(mutex);
  auto p=cached.lock();if(!p){p=std::make_shared<Provider>(bytes);cached=p;}return p;
 }
 std::shared_ptr<Provider> provider;
 PackedResources layout;unsigned cull=0,ccw=0,target=0,mask=0,blend=0;
 static void require(bool v){if(!v)throw std::invalid_argument("PTL masked contract");}
 static void put(unsigned char *p,unsigned v){std::memcpy(p,&v,4);}
 static unsigned get(const unsigned char *p){unsigned v;std::memcpy(&v,p,4);return v;}
public:
 struct Result {std::vector<PackedColor> pixels;uint64_t covered_samples=0;std::string execution_device;uint64_t provider_requests=0,input_bytes=0,output_bytes=0;};
 static bool eligible(const shader::Program &p,const PackedResources &resources,const shader::TextureSamplers &samplers){
  if(p.bytecode.size()!=std::size(ptl_masked_fixture::pixel)||!std::equal(p.bytecode.begin(),p.bytecode.end(),std::begin(ptl_masked_fixture::pixel)))return false;
  for(unsigned n=0;n<16;++n){const auto &r=resources[n];const auto &s=samplers[n];
   if(n<2){if(!r.width||r.width>8192||!r.height||r.height>8192||(r.format!=28&&r.format!=87)||uint64_t(r.width)*r.height>4194304||s.filter!=shader::TextureFilter::linear||s.u!=shader::TextureAddress::clamp||s.v!=shader::TextureAddress::clamp)return false;}
   else if(r.width||r.height||r.format||!r.pixels.empty())return false;
  }return true;
 }
 static std::string verified_blob(const std::filesystem::path &path){
  std::ifstream f(path,std::ios::binary|std::ios::ate);require(bool(f)&&f.tellg()==73857);std::string bytes(73857,'\0');f.seekg(0);f.read(bytes.data(),std::streamsize(bytes.size()));require(bool(f));
  BCRYPT_ALG_HANDLE algorithm=nullptr;require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0);std::array<unsigned char,32> digest{};
  auto status=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),digest.data(),ULONG(digest.size()));BCryptCloseAlgorithmProvider(algorithm,0);require(status>=0);
  constexpr unsigned char expected[]{0x81,0x10,0x4c,0x9e,0xa8,0x76,0x0b,0xbe,0xb7,0xcc,0x02,0x8e,0xa2,0xa9,0xc8,0x1e,0x86,0x05,0x48,0x2a,0xf2,0x19,0xf5,0x7c,0x08,0x2c,0x73,0x7b,0x96,0x62,0xd1,0xe0};
  require(std::equal(digest.begin(),digest.end(),std::begin(expected)));return bytes;
 }
 PtlMasked(const shader::Program &ps,const PackedResources &resources,const shader::TextureSamplers &samplers,CullMode c,bool front_ccw,unsigned format,BlendMode mode,unsigned write_mask,const std::string &backend):layout(resource_layout(resources)),cull(unsigned(c)),ccw(front_ccw?1u:0u),target(format),mask(write_mask),blend(unsigned(mode)){
  require(backend=="NPU"&&cull<=2&&(target==28||target==87)&&mask&&mask<=15&&blend<=1&&eligible(ps,resources,samplers));
  wchar_t *value=nullptr;size_t length=0;require(!_wdupenv_s(&value,&length,L"NR_RENDER_PTL_MASKED_BLOB"));const std::unique_ptr<wchar_t,decltype(&std::free)> owner(value,&std::free);require(value&&*value);const std::filesystem::path path(value);require(path.is_absolute());
  provider=acquire(verified_blob(path));
 }
 Result execute(const std::vector<ProjectedVertex> &vertices,const std::vector<shader::VertexRegisters> &shaded,const std::vector<Point> &points,const std::vector<size_t> &triangle_base,const std::array<int32_t,4> &clip,const PackedResources &resources,const std::vector<PackedColor> &background){
  std::lock_guard<std::mutex> lock(provider->mutex);auto &inference=provider->inference;const auto &device=provider->device;
  require(!provider->failed&&!vertices.empty()&&vertices.size()<=2046&&vertices.size()%3==0&&shaded.size()==vertices.size()&&!points.empty()&&points.size()<=18432&&points.size()==triangle_base.size()&&background.size()==points.size()&&resource_layout_equal(resources,layout));provider->failed=true;
  for(unsigned sample=0;sample<2;++sample)require(resources[sample].pixels.size()==size_t(layout[sample].width)*layout[sample].height);
  const unsigned total=unsigned(points.size());Result result;result.execution_device=device;result.pixels=background;
  auto *data=static_cast<unsigned char*>(inference.get_input_tensor().data());std::memset(data,0,1572864);unsigned counts[6]{};
  const auto initialize=[&](unsigned char *p,unsigned phase,unsigned count){put(p,0x4e524d31);put(p+4,1);put(p+8,phase);put(p+12,count);put(p+28,262144);put(p+48,target);put(p+52,mask);put(p+56,blend);for(unsigned sample=0;sample<2;++sample){put(p+64+sample*12,layout[sample].width);put(p+68+sample*12,layout[sample].height);put(p+72+sample*12,layout[sample].format);}};
  for(unsigned plane=0;plane<6;++plane){auto *p=data+plane*262144;const unsigned begin=plane*3072;counts[plane]=begin<total?(std::min)(3072u,total-begin):0;
   initialize(p,0,counts[plane]);put(p+16,unsigned(vertices.size()));put(p+20,cull);put(p+24,ccw);std::memcpy(p+32,clip.data(),16);
   for(unsigned n=0;n<vertices.size();++n){auto *v=p+256+n*96;std::memcpy(v,vertices[n].snapped.data(),8);std::memcpy(v+8,vertices[n].screen.data(),8);std::memcpy(v+16,&vertices[n].inverse_w,4);for(unsigned a=0;a<3;++a)std::memcpy(v+32+a*16,shaded[n][a+1].data(),16);}
   for(unsigned n=0;n<counts[plane];++n){const unsigned index=begin+n;require(triangle_base[index]<=vertices.size()-3&&triangle_base[index]%3==0);auto *task=p+196864+n*12;put(task,unsigned(triangle_base[index]));std::memcpy(task+4,points[index].data(),8);}
  }
  auto invoke=[&]{inference.start_async();if(!inference.wait_for(std::chrono::milliseconds(10000)))throw std::runtime_error("PTL masked timeout");++result.provider_requests;result.input_bytes+=1572864;result.output_bytes+=1572864;};
  invoke();const auto *plan=static_cast<const unsigned char*>(inference.get_output_tensor().data());
  // The NPU owns coverage, interpolation, filtering coordinates and color math.
  // The host validates addresses and transports only the addressed packed texels.
  std::memset(data,0,1572864);unsigned expected_hits[6]{};
  for(unsigned plane=0;plane<6;++plane){const auto *o=plan+plane*262144;auto *p=data+plane*262144;require(get(o)==0x4e524d4f&&get(o+4)==0&&get(o+8)==counts[plane]&&get(o+12)<=counts[plane]);initialize(p,1,counts[plane]);unsigned hits=0;
   for(unsigned n=0;n<counts[plane];++n){const auto *q=o+16+n*80;auto *record=p+256+n*80;const auto coverage=get(q);require(coverage<=1&&get(q+4)==0&&get(q+8)==0&&get(q+12)==0);put(record+68,coverage);hits+=coverage;std::memcpy(record+64,background[plane*3072+n].data(),4);if(!coverage)continue;
    std::memcpy(record,q+16,16);std::memcpy(record+16,q+64,16);
    for(unsigned sample=0;sample<2;++sample)for(unsigned tap=0;tap<4;++tap){const auto index=get(q+32+sample*16+tap*4);require(index<resources[sample].pixels.size());std::memcpy(record+32+sample*16+tap*4,resources[sample].pixels[index].data(),4);}
   }require(hits==get(o+12));expected_hits[plane]=hits;result.covered_samples+=hits;
  }
  if(result.covered_samples){invoke();const auto *pixels=static_cast<const unsigned char*>(inference.get_output_tensor().data());
   for(unsigned plane=0;plane<6;++plane){const auto *p=pixels+plane*262144;require(get(p)==0x4e524d4f&&get(p+4)==1&&get(p+8)==counts[plane]&&get(p+12)==expected_hits[plane]);for(unsigned n=0;n<counts[plane];++n)std::memcpy(result.pixels[plane*3072+n].data(),p+16+n*4,4);}
  }
  provider->failed=false;return result;
 }
};
}
