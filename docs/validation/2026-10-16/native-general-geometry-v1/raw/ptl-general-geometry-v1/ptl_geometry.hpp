#pragma once
#include "projection_executor.hpp"
#include "vertex_executor.hpp"
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
class PtlGeometry {
 struct Provider {
  ov::Core core;ov::CompiledModel model;ov::InferRequest inference;std::string device;std::mutex mutex;bool failed=false;
  explicit Provider(const std::string &bytes){
   std::istringstream graph(bytes,std::ios::binary);model=core.import_model(graph,"NPU");auto devices=model.get_property(ov::execution_devices);require(devices.size()==1&&(devices[0]=="NPU"||devices[0]=="NPU.0"));device=devices[0];inference=model.create_infer_request();
   std::printf("{\"ptl_geometry_provider_created\":true,\"single_import_shared\":true}\n");std::fflush(stdout);
   require(model.inputs().size()==1&&model.outputs().size()==1&&inference.get_input_tensor().get_element_type()==ov::element::f16&&inference.get_output_tensor().get_element_type()==ov::element::f16&&inference.get_input_tensor().get_shape()==ov::Shape({6,131072})&&inference.get_output_tensor().get_shape()==ov::Shape({6,131072}));
  }
 };
 static std::shared_ptr<Provider> acquire(const std::string &bytes){
  static std::mutex mutex;static std::weak_ptr<Provider> cached;std::lock_guard<std::mutex> lock(mutex);
  auto p=cached.lock();if(!p){p=std::make_shared<Provider>(bytes);cached=p;}return p;
 }
 std::shared_ptr<Provider> provider;
 std::array<unsigned,4> registers{},modes{};unsigned cull=0,ccw=0;
 static void require(bool v){if(!v)throw std::invalid_argument("PTL geometry contract");}
 static void put(unsigned char *p,unsigned v){std::memcpy(p,&v,4);}
 static unsigned get(const unsigned char *p){unsigned v;std::memcpy(&v,p,4);return v;}
public:
 struct Result {std::vector<shader::VertexRegisters> fragments;std::vector<uint8_t> covered;std::string execution_device;uint64_t provider_requests=0,input_bytes=0,output_bytes=0;};
 static std::string verified_blob(const std::filesystem::path &path){
  std::ifstream f(path,std::ios::binary|std::ios::ate);require(bool(f)&&f.tellg()==73857);std::string bytes(73857,'\0');f.seekg(0);f.read(bytes.data(),std::streamsize(bytes.size()));require(bool(f));
  BCRYPT_ALG_HANDLE algorithm=nullptr;require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0);std::array<unsigned char,32> digest{};
  auto status=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),digest.data(),ULONG(digest.size()));BCryptCloseAlgorithmProvider(algorithm,0);require(status>=0);
  constexpr unsigned char expected[]{0xb8,0x48,0xe8,0x4c,0xd4,0xfa,0xf2,0x7a,0x2e,0xc0,0x8f,0x5d,0xc6,0xcc,0x7b,0xc6,0xf6,0x09,0x9a,0xb5,0x12,0x81,0x52,0x46,0x23,0xdc,0xb4,0xc8,0xa3,0xad,0xe4,0xc2};
  require(std::equal(digest.begin(),digest.end(),std::begin(expected)));return bytes;
 }
 PtlGeometry(const shader::Program &ps,CullMode c,bool front_ccw,const std::string &backend):cull(unsigned(c)),ccw(front_ccw?1u:0u){
  require(backend=="NPU"&&cull<=2);unsigned attributes=0;
  for(unsigned r=0;r<16;++r)if(ps.inputs[r]&&r!=ps.position_input){require(attributes<4&&(ps.interpolation[r]==1||ps.interpolation[r]==2||ps.interpolation[r]==4));registers[attributes]=r;modes[attributes++]=ps.interpolation[r];}
  wchar_t *value=nullptr;size_t length=0;require(!_wdupenv_s(&value,&length,L"NR_RENDER_PTL_GEOMETRY_BLOB"));const std::unique_ptr<wchar_t,decltype(&std::free)> owner(value,&std::free);require(value&&*value);const std::filesystem::path path(value);require(path.is_absolute());
  provider=acquire(verified_blob(path));
 }
 Result execute(const std::vector<ProjectedVertex> &vertices,const std::vector<shader::VertexRegisters> &shaded,const std::vector<Point> &points,const std::vector<size_t> &triangle_base,const std::array<int32_t,4> &clip){
  std::lock_guard<std::mutex> lock(provider->mutex);auto &inference=provider->inference;const auto &device=provider->device;
  require(!provider->failed&&!vertices.empty()&&vertices.size()<=252&&vertices.size()%3==0&&shaded.size()==vertices.size()&&!points.empty()&&points.size()<=18432&&points.size()==triangle_base.size());provider->failed=true;
  const unsigned total=unsigned(points.size());Result result;result.execution_device=device;result.fragments.resize(total);result.covered.resize(total);
  auto *data=static_cast<unsigned char*>(inference.get_input_tensor().data());std::memset(data,0,1572864);unsigned counts[6]{};
  for(unsigned plane=0;plane<6;++plane){auto *p=data+plane*262144;const unsigned begin=plane*3072;counts[plane]=begin<total?(std::min)(3072u,total-begin):0;
   put(p,0x4e524731);put(p+4,1);put(p+8,counts[plane]);put(p+12,unsigned(vertices.size()));put(p+16,cull);put(p+20,ccw);put(p+24,262144);std::memcpy(p+32,clip.data(),16);std::memcpy(p+48,modes.data(),16);
   for(unsigned n=0;n<vertices.size();++n){auto *v=p+256+n*96;std::memcpy(v,vertices[n].snapped.data(),8);std::memcpy(v+8,vertices[n].screen.data(),8);std::memcpy(v+16,&vertices[n].inverse_w,4);
    for(unsigned a=0;a<4;++a)if(modes[a])std::memcpy(v+32+a*16,shaded[n][registers[a]].data(),16);
   }
   for(unsigned n=0;n<counts[plane];++n){const unsigned index=begin+n;require(triangle_base[index]<=vertices.size()-3&&triangle_base[index]%3==0);auto *task=p+24576+n*12;put(task,unsigned(triangle_base[index]));std::memcpy(task+4,points[index].data(),8);}
  }
  inference.start_async();if(!inference.wait_for(std::chrono::milliseconds(10000)))throw std::runtime_error("PTL geometry timeout");result.provider_requests=1;result.input_bytes=1572864;result.output_bytes=1572864;
  const auto *pixels=static_cast<const unsigned char*>(inference.get_output_tensor().data());
  for(unsigned plane=0;plane<6;++plane){const auto *p=pixels+plane*262144;require(get(p)==0x4e52474f&&get(p+4)==counts[plane]&&get(p+8)<=counts[plane]&&get(p+12)==0);unsigned hits=0;
   for(unsigned n=0;n<counts[plane];++n){const auto *record=p+16+n*80;unsigned coverage=get(record);require(coverage<=1&&get(record+4)==0&&get(record+8)==0&&get(record+12)==0);result.covered[plane*3072+n]=uint8_t(coverage);hits+=coverage;
    for(unsigned a=0;a<4;++a)if(modes[a])std::memcpy(result.fragments[plane*3072+n][registers[a]].data(),record+16+a*16,16);
   }require(hits==get(p+8));
  }
  provider->failed=false;return result;
 }
};
}
