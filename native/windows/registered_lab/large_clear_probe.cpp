#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "indexed_exchange_protocol.hpp"
#include "clear_executor.hpp"
#include <windows.h>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdio>
namespace ex=nr::registered_lab::indexed_exchange;
using namespace nr::raster;
void check(bool v){if(!v)throw std::runtime_error("large clear contract failed");}
ex::Request request(){ex::Request r;auto &h=r.header;h.abi=13;h.flags=0x200000;h.width=2880;h.height=1800;h.format=87;h.sequence=1;h.client_pid=GetCurrentProcessId();h.nonce[0]=1;r.clear_color={-.2f,.25f,.75f,1};return r;}
unsigned contract(){
 auto r=request();const auto packet=ex::encode(r);check(r.header.abi==20&&r.header.reserved==13&&packet.size()==96&&ex::request_valid(r.header,packet.size()));const auto decoded=ex::decode(packet);check(decoded.clear_color==r.clear_color);unsigned rejected=0;
 for(unsigned n=0;n<11;++n){auto h=r.header;switch(n){case 0:h.abi=13;h.reserved=0;break;case 1:h.width=4096;h.height=2048;break;case 2:h.width=0;break;case 3:h.width=8193;break;case 4:h.reserved=12;break;case 5:h.flags|=1;break;case 6:h.vertices=6;break;case 7:h.body_bytes=20;break;case 8:h.width=1024;h.height=768;break;case 9:h.abi=16;break;case 10:h.sequence=0;break;}check(!ex::request_valid(h,80+uint64_t(h.body_bytes)));++rejected;}
 auto truncated=packet;truncated.pop_back();bool failed=false;try{(void)ex::decode(truncated);}catch(const std::invalid_argument&){failed=true;}check(failed);++rejected;
 auto bad=request();bad.clear_color[0]=std::numeric_limits<float>::quiet_NaN();failed=false;try{(void)ex::encode(bad);}catch(const std::invalid_argument&){failed=true;}check(failed);++rejected;
 auto legacy_request=request();legacy_request.header.width=1024;legacy_request.header.height=768;const auto small_packet=ex::encode(legacy_request);check(legacy_request.header.abi==13&&!legacy_request.header.reserved&&ex::decode(small_packet).clear_color==legacy_request.clear_color);
 return rejected;
}
int main(int argc,char **argv)try{
 const auto negative=contract();
 if(argc==2&&!std::strcmp(argv[1],"--contract")){std::printf("{\"large_clear_contract\":true,\"negative_cases\":%u,\"legacy_small_clear_preserved\":true}\n",negative);return 0;}
 if(argc==3&&!std::strcmp(argv[1],"--emit")){auto r=request();const auto packet=ex::encode(r);std::ofstream f(argv[2],std::ios::binary);f.write(reinterpret_cast<const char *>(packet.data()),std::streamsize(packet.size()));check(bool(f));std::printf("{\"large_clear_request_emitted\":true,\"abi\":20,\"legacy_abi\":13,\"negative_cases\":%u}\n",negative);return 0;}
 check(argc==2);const std::string backend=argv[1];uint64_t components=0;unsigned cases=0;
 const std::array<Color,4> colors{{{-2,0,1,3},{.5f,.25f,.75f,1},{126.5f/255,127.5f/255,std::nextafter(128.5f/255,0.f),1},{1.f/255,127.f/255,128.f/255,254.f/255}}};
 for(const auto shape:{std::array<uint32_t,2>{2880,1800},{4096,2047},{8191,1024}})for(const auto format:{28u,87u}){
  ClearExecutor executor(shape[0],shape[1],format,backend);
  for(const auto &color:colors){const auto result=executor.execute(color);check(result.execution_device==backend||result.execution_device.rfind(backend+".",0)==0);check(result.pixels.size()==uint64_t(shape[0])*shape[1]);PackedColor expected{};
   for(unsigned c=0;c<4;++c){const auto lane=format==87&&c<3?2-c:c;expected[c]=uint8_t(std::nearbyint(std::clamp(color[lane],0.f,1.f)*255));}
   for(const auto &pixel:result.pixels)check(pixel==expected);components+=uint64_t(result.pixels.size())*4;++cases;
   std::printf("{\"large_clear_case\":true,\"width\":%u,\"height\":%u,\"format\":%u,\"execution_device\":\"%s\",\"draw_ms\":%.3f,\"pixels_exact\":true}\n",shape[0],shape[1],format,result.execution_device.c_str(),result.milliseconds);std::fflush(stdout);
  }
 }
 bool failed=false;try{ClearExecutor bad(4096,2048,87,backend);}catch(const std::invalid_argument&){failed=true;}check(failed);
 std::printf("{\"completed\":true,\"passed\":true,\"large_clear_cases\":%u,\"verified_components\":%llu,\"negative_packet_cases\":%u,\"oversized_executor_rejected\":true,\"execution_device\":\"%s\"}\n",cases,static_cast<unsigned long long>(components),negative,backend.c_str());return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
