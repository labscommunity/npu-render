#pragma once
#include "ptl_infer_digest_v1.hpp"
#include "packed_resources.hpp"
#include "vertex_executor.hpp"
#include "ptl_complex_shaders.hpp"
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
namespace nr::shader {
// Experimental adapter: NPU calculates texel addresses; host transports exact
// addressed bytes; NPU filters and shades. No host shader or sampling arithmetic.
class PtlComplexShader {
 struct Provider {
  ov::Core core;ov::CompiledModel model;ov::InferRequest inference;std::string device;std::mutex mutex;bool failed=false;
  explicit Provider(const std::string &bytes){
  std::istringstream graph(bytes,std::ios::binary);model=core.import_model(graph,"NPU");auto devices=model.get_property(ov::execution_devices);require(devices.size()==1&&(devices[0]=="NPU"||devices[0]=="NPU.0"));device=devices[0];inference=model.create_infer_request();
  require(model.inputs().size()==1&&model.outputs().size()==1&&inference.get_input_tensor().get_element_type()==ov::element::f16&&inference.get_output_tensor().get_element_type()==ov::element::f16&&inference.get_input_tensor().get_shape()==ov::Shape({6,131072})&&inference.get_output_tensor().get_shape()==ov::Shape({6,131072}));
   std::printf("{\"ptl_complex_provider_created\":true,\"single_import_shared\":true}\n");std::fflush(stdout);
  }
 };
 static std::shared_ptr<Provider> acquire(const std::string &bytes){
  static std::mutex mutex;static std::weak_ptr<Provider> cached;std::lock_guard<std::mutex> lock(mutex);
  auto p=cached.lock();if(!p){p=std::make_shared<Provider>(bytes);cached=p;}return p;
 }
 std::shared_ptr<Provider> provider;
 raster::PackedResources layout;unsigned count=0,address_u[2]{},address_v[2]{},filters[2]{};
 static void require(bool v){if(!v)throw std::invalid_argument("PTL complex shader contract");}
 static void put(unsigned char *p,unsigned v){std::memcpy(p,&v,4);}
 static unsigned get(const unsigned char *p){unsigned v;std::memcpy(&v,p,4);return v;}
public:
 struct Result: RegisterExecutor::Result {uint64_t provider_requests=0;};
 static bool eligible(const Program &p,const raster::PackedResources &resources,const TextureSamplers &samplers){
  if(p.bytecode.size()!=ptl_complex_fixture::pixel.size() || !std::equal(p.bytecode.begin(),p.bytecode.end(),ptl_complex_fixture::pixel.begin()))return false;
  for(unsigned n=0;n<16;++n){const auto &r=resources[n];const auto &s=samplers[n];
   if(n<2){if(!r.width||r.width>8192||!r.height||r.height>8192||(r.format!=28&&r.format!=87)||uint64_t(r.width)*r.height>4194304||(s.filter!=TextureFilter::linear&&s.filter!=TextureFilter::point)||(s.u!=TextureAddress::clamp&&s.u!=TextureAddress::mirror)||(s.v!=TextureAddress::clamp&&s.v!=TextureAddress::mirror))return false;}
   else if(r.width||r.height||r.format||!r.pixels.empty())return false;
  }return true;
 }
 static std::string verified_blob(const std::filesystem::path &path){
  std::ifstream f(path,std::ios::binary|std::ios::ate);require(bool(f)&&f.tellg()==73857);std::string bytes(73857,'\0');f.seekg(0);f.read(bytes.data(),std::streamsize(bytes.size()));require(bool(f));
  BCRYPT_ALG_HANDLE algorithm=nullptr;require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0);std::array<unsigned char,32> digest{};
  auto status=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),digest.data(),ULONG(digest.size()));BCryptCloseAlgorithmProvider(algorithm,0);require(status>=0);
  constexpr unsigned char expected[]{0x47,0xf6,0x9b,0x58,0x61,0x73,0x2a,0x03,0x01,0x4b,0x58,0x16,0x01,0x16,0xeb,0xbb,0x92,0x8f,0xd6,0xe8,0x18,0xa1,0x1b,0x7f,0x55,0x3e,0x42,0x3a,0x4c,0xf3,0xc4,0x3a};
  require(std::equal(digest.begin(),digest.end(),std::begin(expected)));return bytes;
 }
 PtlComplexShader(const Program &p,unsigned batch,const std::string &backend,const raster::PackedResources &resources,const TextureSamplers &samplers){
  require(backend=="NPU"&&batch&&batch<=16384&&eligible(p,resources,samplers));count=batch;for(unsigned n=0;n<2;++n){address_u[n]=unsigned(samplers[n].u);address_v[n]=unsigned(samplers[n].v);filters[n]=samplers[n].filter==TextureFilter::point?1u:0u;}layout=raster::resource_layout(resources);
  wchar_t *value=nullptr;size_t length=0;require(!_wdupenv_s(&value,&length,L"NR_RENDER_PTL_COMPLEX_BLOB"));const std::unique_ptr<wchar_t,decltype(&std::free)> owner(value,&std::free);require(value&&*value);const std::filesystem::path path(value);require(path.is_absolute());
  provider=acquire(verified_blob(path));
 }
 Result execute(const std::vector<VertexRegisters> &vertices,const VertexConstants &constants,const raster::PackedResources &resources){
  std::lock_guard<std::mutex> lock(provider->mutex);auto &inference=provider->inference;const auto &device=provider->device;
  require(!provider->failed&&!vertices.empty()&&vertices.size()<=count&&constants[0].size()>=6&&raster::resource_layout_equal(layout,resources));provider->failed=true;
  for(unsigned n=0;n<2;++n)require(resources[n].pixels.size()==size_t(layout[n].width)*layout[n].height);
  const unsigned total=unsigned(vertices.size());Result result;result.execution_device=device;result.vertices.resize(total);
  auto *data=static_cast<unsigned char*>(inference.get_input_tensor().data());
  for(unsigned first=0;first<total;first+=6144){
   std::memset(data,0,1572864);unsigned counts[6]{};
   for(unsigned plane=0;plane<6;++plane){auto *p=data+plane*262144;const auto begin=first+plane*1024;counts[plane]=begin<total?(std::min)(1024u,total-begin):0;
    put(p,0x4e524331);put(p+4,1);put(p+8,0);put(p+12,counts[plane]);put(p+16,262144);for(unsigned n=0;n<2;++n){put(p+32+n*8,address_u[n]);put(p+36+n*8,address_v[n]);put(p+48+n*4,filters[n]);}std::memcpy(p+64,constants[0].data(),96);
    for(unsigned n=0;n<2;++n){put(p+256+n*12,layout[n].width);put(p+260+n*12,layout[n].height);put(p+264+n*12,layout[n].format);}
    for(unsigned n=0;n<counts[plane];++n)std::memcpy(p+512+n*192,vertices[begin+n].data(),64);
   }
   auto invoke=[&]{nr::provider::profile_tensor_request(inference,"complex");++result.provider_requests;result.input_bytes+=1572864;result.output_bytes+=1572864;};
   invoke();const auto *plan=static_cast<const unsigned char*>(inference.get_output_tensor().data());
   for(unsigned plane=0;plane<6;++plane){auto *p=data+plane*262144;const auto *o=plan+plane*262144;require(get(o)==0x4e52434f&&get(o+4)==0&&get(o+8)==counts[plane]&&get(o+12)==0);put(p+8,1);
    for(unsigned n=0;n<counts[plane];++n){const auto *q=o+16+n*128;auto *record=p+512+n*192;std::memcpy(record+64,q+80,40);
     for(unsigned sample=0;sample<5;++sample){const auto slot=sample?1:0;for(unsigned tap=0;tap<4;++tap){const unsigned index=get(q+sample*16+tap*4);require(index<resources[slot].pixels.size());std::memcpy(record+104+sample*16+tap*4,resources[slot].pixels[index].data(),4);}}
    }
   }
   invoke();const auto *pixels=static_cast<const unsigned char*>(inference.get_output_tensor().data());
   for(unsigned plane=0;plane<6;++plane){const auto *p=pixels+plane*262144;require(get(p)==0x4e52434f&&get(p+4)==1&&get(p+8)==counts[plane]&&get(p+12)==0);
    for(unsigned n=0;n<counts[plane];++n)std::memcpy(result.vertices[first+plane*1024+n][0].data(),p+16+n*16,16);
   }
  }
  provider->failed=false;return result;
 }
};
}
