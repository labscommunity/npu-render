#pragma once
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
namespace nr::provider {
inline bool compile_trace_enabled(){static const bool enabled=[](){
 char *text=nullptr;size_t length=0;
 if(_dupenv_s(&text,&length,"NR_RENDER_COMPILE_TRACE"))throw std::runtime_error("cannot read compile trace setting");
 const std::unique_ptr<char,decltype(&std::free)> owner(text,&std::free);
 if(!text)return false;const std::string value(text);
 if(value!="0" && value!="1")throw std::invalid_argument("invalid compile trace setting");
 return value=="1";}();return enabled;}
class CompileTrace {
 using Clock=std::chrono::steady_clock;
 bool enabled=false,passed=false;int cache_hit=-1;uint64_t id=0;
 Clock::time_point began{};
public:
 CompileTrace(bool active,const std::string &identity,size_t nodes,size_t inputs):enabled(active){
  if(!enabled)return;static std::atomic<uint64_t> next{0};id=next.fetch_add(1);began=Clock::now();
  const auto utc=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  std::printf("{\"compile_trace_begin\":true,\"id\":%llu,\"utc_us\":%lld,\"graph_sha256\":\"%s\",\"nodes\":%zu,\"inputs\":%zu,\"pixel_data_recorded\":false}\n",static_cast<unsigned long long>(id),static_cast<long long>(utc),identity.c_str(),nodes,inputs);std::fflush(stdout);
 }
 void success(int hit)noexcept{passed=true;cache_hit=hit;}
 ~CompileTrace(){if(!enabled)return;
  const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-began).count();
  std::printf("{\"compile_trace_end\":true,\"id\":%llu,\"elapsed_us\":%lld,\"loaded_from_cache\":%d,\"passed\":%s}\n",static_cast<unsigned long long>(id),static_cast<long long>(elapsed),cache_hit,passed?"true":"false");std::fflush(stdout);
 }
};
}
