#define NOMINMAX
#include "response_envelope_v1.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
namespace ex=nr::registered_lab::indexed_exchange;
using Clock=std::chrono::steady_clock;
void test(bool x){if(!x)throw std::runtime_error("response envelope comparison failed");}
struct Outcome{unsigned category=0;ex::Receipt receipt{};};
Outcome outcome(const std::vector<uint8_t> &bytes,const ex::Header &request,bool compact){
 Outcome o;try{if(compact)o.receipt=ex::validate_response_envelope(bytes,request);else{auto r=ex::decode_response(bytes,request);o.receipt={r.input_bytes,r.output_bytes,r.provider_requests,r.covered_samples,r.tile_executions,r.used_rectangle?1u:0u,0};}}
 catch(const std::invalid_argument&){o.category=1;}catch(const std::runtime_error&){o.category=2;}return o;
}
void equivalent(const std::vector<uint8_t> &bytes,const ex::Header &request){auto a=outcome(bytes,request,false),b=outcome(bytes,request,true);test(a.category==b.category);if(!a.category)test(std::memcmp(&a.receipt,&b.receipt,sizeof(a.receipt))==0);}
std::pair<ex::Header,std::vector<uint8_t>> fixture(unsigned width,unsigned height,bool clear){
 ex::Header h;h.nonce[0]=7;h.client_pid=42;h.sequence=1;h.width=width;h.height=height;h.format=87;
 if(clear){h.abi=13;h.flags=0x200000;h.body_bytes=16;if(uint64_t(width)*height>4194304){h.abi=20;h.reserved=13;}}
 else{h.vertices=6;h.flags=0xf00;h.vs_words=h.ps_words=3;if(uint64_t(width)*height>4194304){h.abi=16;h.reserved=1;}}
 nr::raster::UniformDrawExecutor::Result result{};result.execution_device="NPU";result.pixels.assign(size_t(width)*height,{17,31,47,255});result.provider_requests=clear?1:2;result.input_bytes=clear?16:123;result.output_bytes=uint64_t(width)*height*4;result.tile_executions=1;result.covered_samples=uint64_t(width)*height;
 return {h,ex::response(h,result)};
}
__declspec(noinline) uint64_t old_validate(const std::vector<uint8_t> &bytes,const ex::Header &h){return ex::decode_response(bytes,h).provider_requests;}
__declspec(noinline) uint64_t new_validate(const std::vector<uint8_t> &bytes,const ex::Header &h){return ex::validate_response_envelope(bytes,h).provider_requests;}
int main()try{
 unsigned checks=0;uint64_t sink=0;
 for(bool clear:{false,true}){
  const auto data=fixture(32,8,clear);equivalent(data.second,data.first);++checks;
  for(size_t at=0;at<sizeof(ex::Header)+sizeof(ex::Receipt);++at)for(unsigned bit=0;bit<8;++bit){auto bad=data.second;bad[at]^=uint8_t(1u<<bit);equivalent(bad,data.first);++checks;}
  for(size_t at=0;at<sizeof(ex::Header);++at)for(unsigned bit=0;bit<8;++bit){auto h=data.first;reinterpret_cast<unsigned char*>(&h)[at]^=static_cast<unsigned char>(1u<<bit);equivalent(data.second,h);++checks;}
  for(size_t n=0;n<=sizeof(ex::Header)+sizeof(ex::Receipt);++n){auto short_packet=data.second;short_packet.resize(n);equivalent(short_packet,data.first);++checks;}
  auto bad=data.second;bad.pop_back();equivalent(bad,data.first);++checks;bad=data.second;bad.push_back(0);equivalent(bad,data.first);++checks;
  auto failure=data.first;failure.kind=3;failure.body_bytes=0;std::vector<uint8_t> rejected(sizeof(failure));std::memcpy(rejected.data(),&failure,sizeof(failure));equivalent(rejected,data.first);++checks;
 }
 for(const auto size:std::array<std::array<unsigned,2>,4>{{{640,480},{1920,1080},{2880,1800},{4096,2047}}}){
  const auto data=fixture(size[0],size[1],false);equivalent(data.second,data.first);++checks;double old_ms=0,new_ms=0;
  for(unsigned round=0;round<40;++round){double times[2]{};for(unsigned k=0;k<2;++k){unsigned method=(round+k)%2;const auto start=Clock::now();sink+=method?new_validate(data.second,data.first):old_validate(data.second,data.first);times[method]=std::chrono::duration<double,std::milli>(Clock::now()-start).count();}if(round){old_ms+=times[0];new_ms+=times[1];}}
  std::printf("{\"kind\":\"validation_only_timing\",\"width\":%u,\"height\":%u,\"warm_pairs\":39,\"old_ms\":%.6f,\"new_ms\":%.6f,\"npu_executed\":false,\"desktop_tested\":false}\n",size[0],size[1],old_ms,new_ms);
 }
 std::printf("{\"completed\":true,\"passed\":true,\"equivalence_checks\":%u,\"checksum\":%llu,\"npu_executed\":false,\"desktop_tested\":false}\n",checks,static_cast<unsigned long long>(sink));return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
