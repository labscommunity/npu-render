#pragma once
#include "provider_profile.hpp"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
namespace nr::provider {
namespace infer_trace {
using Clock=std::chrono::steady_clock;
inline bool enabled(){static const bool result=[](){char *text=nullptr;size_t length=0;
 if(_dupenv_s(&text,&length,"NR_RENDER_INFER_TRACE"))throw std::runtime_error("cannot read inference trace setting");
 if(!text)return false;const std::string value(text);std::free(text);if(value!="0" && value!="1")throw std::invalid_argument("invalid inference trace setting");return value=="1";}();return result;}
struct Event{uint64_t sequence=0,thread=0,request_address=0,utc_us=0,duration_us=0,gap_us=0;const char *site="";unsigned line=0;bool failed=false;};
struct History{std::mutex mutex;std::array<Event,512> events{};uint64_t issued=0,maximum_gap_us=0;Clock::time_point last_finished{};};
inline History &history(){static History value;return value;}
inline void save(Event event,Clock::time_point began){auto &h=history();std::lock_guard<std::mutex> lock(h.mutex);
 const auto ended=Clock::now();event.duration_us=std::chrono::duration_cast<std::chrono::microseconds>(ended-began).count();
 event.gap_us=h.issued && began>h.last_finished?uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(began-h.last_finished).count()):0;
 if(event.gap_us>h.maximum_gap_us)h.maximum_gap_us=event.gap_us;event.sequence=h.issued++;h.events[event.sequence%h.events.size()]=event;h.last_finished=ended;
 if(!event.failed)return;
 std::printf("{\"inference_failure_history\":true,\"total_inferences\":%llu,\"maximum_inter_inference_gap_us\":%llu,\"ring_capacity\":512,\"pixel_data_recorded\":false}\n",static_cast<unsigned long long>(h.issued),static_cast<unsigned long long>(h.maximum_gap_us));
 const auto first=h.issued>h.events.size()?h.issued-h.events.size():0;
 for(uint64_t n=first;n<h.issued;++n){const auto &e=h.events[n%h.events.size()];
  std::printf("{\"inference_history\":true,\"index\":%llu,\"thread_id_hash\":%llu,\"request_address\":%llu,\"utc_us\":%llu,\"duration_us\":%llu,\"gap_us\":%llu,\"site\":\"%s\",\"line\":%u,\"failed\":%s}\n",static_cast<unsigned long long>(e.sequence),static_cast<unsigned long long>(e.thread),static_cast<unsigned long long>(e.request_address),static_cast<unsigned long long>(e.utc_us),static_cast<unsigned long long>(e.duration_us),static_cast<unsigned long long>(e.gap_us),e.site,e.line,e.failed?"true":"false");
 }std::fflush(stdout);
}
}
template<class Request> void traced_infer_impl(Request &request,const char *file,unsigned line){
 if(!infer_trace::enabled()){request.infer();return;}
 const auto began=infer_trace::Clock::now();const char *site=std::strrchr(file,'\\');if(!site)site=std::strrchr(file,'/');site=site?site+1:file;
 infer_trace::Event event{};event.thread=std::hash<std::thread::id>{}(std::this_thread::get_id());event.request_address=uint64_t(reinterpret_cast<uintptr_t>(&request));event.utc_us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();event.site=site;event.line=line;
 try{request.infer();infer_trace::save(event,began);}catch(...){event.failed=true;infer_trace::save(event,began);throw;}
}
template<class Request> void traced_infer(Request &request,const char *file,unsigned line){
 if(!profile::enabled()){traced_infer_impl(request,file,line);return;}
 const auto began=std::chrono::steady_clock::now();
 try{traced_infer_impl(request,file,line);profile::record(file,began,false);}
 catch(...){profile::record(file,began,true);throw;}
}
}
