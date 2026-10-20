#pragma once
#include "npu_turbo_import.hpp"
#include "packed_resources.hpp"
#include "vertex_executor.hpp"
#include "ptl_texture_shaders.hpp"
#include <openvino/openvino.hpp>
#include <windows.h>
#include <bcrypt.h>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
namespace nr::shader {
// Experimental adapter: NPU calculates texel addresses; host transports exact
// addressed bytes; NPU filters and shades. No host shader or sampling arithmetic.
class PtlTextureShader {
 ov::Core core;ov::CompiledModel model;ov::InferRequest inference;
 raster::PackedResources layout;std::string device;unsigned count=0;bool failed=false;
 static void require(bool v){if(!v)throw std::invalid_argument("PTL texture shader contract");}
 static void put(unsigned char *p,unsigned v){std::memcpy(p,&v,4);}
 static unsigned get(const unsigned char *p){unsigned v;std::memcpy(&v,p,4);return v;}
public:
 struct Result: RegisterExecutor::Result {uint64_t provider_requests=0;};
 static bool eligible(const Program &p,const raster::PackedResources &resources,const TextureSamplers &samplers){
  if(p.bytecode.size()!=ptl_texture_fixture::pixel.size() || !std::equal(p.bytecode.begin(),p.bytecode.end(),ptl_texture_fixture::pixel.begin()))return false;
  for(unsigned n=0;n<16;++n){const auto &r=resources[n];const auto &s=samplers[n];
   if(n<3){if(!r.width||r.width>8192||!r.height||r.height>8192||(r.format!=28&&r.format!=87)||uint64_t(r.width)*r.height>4194304||s.filter!=TextureFilter::linear||s.u!=TextureAddress::clamp||s.v!=TextureAddress::clamp)return false;}
   else if(r.width||r.height||r.format||!r.pixels.empty())return false;
  }return true;
 }
 static std::string verified_blob(const std::filesystem::path &path){
  std::ifstream f(path,std::ios::binary|std::ios::ate);require(bool(f)&&f.tellg()==73857);std::string bytes(73857,'\0');f.seekg(0);f.read(bytes.data(),std::streamsize(bytes.size()));require(bool(f));
  BCRYPT_ALG_HANDLE algorithm=nullptr;require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0);std::array<unsigned char,32> digest{};
  auto status=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),digest.data(),ULONG(digest.size()));BCryptCloseAlgorithmProvider(algorithm,0);require(status>=0);
  constexpr unsigned char expected[]{0x48,0x17,0x65,0xdd,0x51,0xd8,0xd9,0xb0,0x51,0xb8,0x7d,0xb8,0xbe,0x7e,0x62,0xae,0x79,0x31,0x24,0x5f,0xe0,0x51,0xb3,0x3b,0x64,0x57,0xbe,0x77,0x81,0x6c,0x2e,0x87};
  require(std::equal(digest.begin(),digest.end(),std::begin(expected)));return bytes;
 }
 PtlTextureShader(const Program &p,unsigned batch,const std::string &backend,const raster::PackedResources &resources,const TextureSamplers &samplers){
  require(backend=="NPU"&&batch&&batch<=16384&&eligible(p,resources,samplers));count=batch;layout=raster::resource_layout(resources);
  wchar_t *value=nullptr;size_t length=0;require(!_wdupenv_s(&value,&length,L"NR_RENDER_PTL_TEXTURE_BLOB"));const std::unique_ptr<wchar_t,decltype(&std::free)> owner(value,&std::free);require(value&&*value);const std::filesystem::path path(value);require(path.is_absolute());
  const auto bytes=verified_blob(path);std::istringstream graph(bytes,std::ios::binary);model=nr::raster::import_ptl_turbo(core,graph,"texture_shader");auto devices=model.get_property(ov::execution_devices);require(devices.size()==1&&(devices[0]=="NPU"||devices[0]=="NPU.0"));device=devices[0];inference=model.create_infer_request();
  require(model.inputs().size()==1&&model.outputs().size()==1&&inference.get_input_tensor().get_element_type()==ov::element::f16&&inference.get_output_tensor().get_element_type()==ov::element::f16&&inference.get_input_tensor().get_shape()==ov::Shape({6,131072})&&inference.get_output_tensor().get_shape()==ov::Shape({6,131072}));
 }
 Result execute(const std::vector<VertexRegisters> &vertices,const VertexConstants &constants,const raster::PackedResources &resources){
  require(!failed&&!vertices.empty()&&vertices.size()<=count&&constants[0].size()>=6&&raster::resource_layout_equal(layout,resources));failed=true;
  for(unsigned n=0;n<3;++n)require(resources[n].pixels.size()==size_t(layout[n].width)*layout[n].height);
  const unsigned total=unsigned(vertices.size());Result result;result.execution_device=device;result.vertices.resize(total);
  auto *data=static_cast<unsigned char*>(inference.get_input_tensor().data());
  for(unsigned first=0;first<total;first+=6144){
   std::memset(data,0,1572864);unsigned counts[6]{};
   for(unsigned plane=0;plane<6;++plane){auto *p=data+plane*262144;const auto begin=first+plane*1024;counts[plane]=begin<total?(std::min)(1024u,total-begin):0;
    put(p,0x4e525433);put(p+4,1);put(p+8,0);put(p+12,counts[plane]);put(p+16,262144);std::memcpy(p+64,constants[0].data(),96);
    for(unsigned n=0;n<3;++n){put(p+160+n*12,layout[n].width);put(p+164+n*12,layout[n].height);put(p+168+n*12,layout[n].format);}
    for(unsigned n=0;n<counts[plane];++n)std::memcpy(p+256+n*224,vertices[begin+n].data(),80);
   }
   auto invoke=[&]{inference.start_async();if(!inference.wait_for(std::chrono::milliseconds(10000)))throw std::runtime_error("PTL texture shader timeout");++result.provider_requests;result.input_bytes+=1572864;result.output_bytes+=1572864;};
   invoke();const auto *plan=static_cast<const unsigned char*>(inference.get_output_tensor().data());
   for(unsigned plane=0;plane<6;++plane){auto *p=data+plane*262144;const auto *o=plan+plane*262144;require(get(o)==0x4e52544f&&get(o+4)==0&&get(o+8)==counts[plane]&&get(o+12)==0);put(p+8,1);
    for(unsigned n=0;n<counts[plane];++n){const auto *q=o+16+n*144;auto *record=p+256+n*224;std::memcpy(record+80,q+96,48);
     for(unsigned sample=0;sample<6;++sample){const auto slot=sample==0?0:sample==5?2:1;for(unsigned tap=0;tap<4;++tap){const unsigned index=get(q+sample*16+tap*4);require(index<resources[slot].pixels.size());std::memcpy(record+128+sample*16+tap*4,resources[slot].pixels[index].data(),4);}}
    }
   }
   invoke();const auto *pixels=static_cast<const unsigned char*>(inference.get_output_tensor().data());
   for(unsigned plane=0;plane<6;++plane){const auto *p=pixels+plane*262144;require(get(p)==0x4e52544f&&get(p+4)==1&&get(p+8)==counts[plane]&&get(p+12)==0);
    for(unsigned n=0;n<counts[plane];++n)std::memcpy(result.vertices[first+plane*1024+n][0].data(),p+16+n*16,16);
   }
  }
  failed=false;return result;
 }
};
}
