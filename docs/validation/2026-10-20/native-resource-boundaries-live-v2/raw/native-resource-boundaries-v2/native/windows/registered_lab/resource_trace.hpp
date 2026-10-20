#pragma once
#include "kernel_memory.hpp"
#include "lab_trace.hpp"
#include <atomic>
#include <cstring>
namespace nr::registered_lab {
inline thread_local uint64_t diagnostic_call=0;
inline uint64_t diagnostic_qpc() noexcept {LARGE_INTEGER value{};return QueryPerformanceCounter(&value)?uint64_t(value.QuadPart):0;}
inline uint64_t diagnostic_epoch() noexcept {static const uint64_t value=diagnostic_qpc();return value;}
inline void resource_boundary(const char *operation,const Allocation *allocation=nullptr,uint64_t context=0,uint64_t argument=0) noexcept {
 static std::atomic<uint64_t> serial{0};const auto sequence=serial.fetch_add(1,std::memory_order_relaxed)+1;
 lab_trace("{\"event\":\"resource_boundary\",\"epoch\":%llu,\"serial\":%llu,\"call\":%llu,\"qpc\":%llu,\"operation\":\"%s\",\"context\":%llu,\"object\":%llu,\"backing\":%llu,\"version\":%llu,\"width\":%u,\"height\":%u,\"format\":%u,\"external_alias_possible\":%s,\"argument\":%llu,\"logical_version_only\":true}\n",
 static_cast<unsigned long long>(diagnostic_epoch()),static_cast<unsigned long long>(sequence),static_cast<unsigned long long>(diagnostic_call),static_cast<unsigned long long>(diagnostic_qpc()),operation,
 static_cast<unsigned long long>(allocation?allocation->context->diagnostic_id:context),static_cast<unsigned long long>(allocation?allocation->diagnostic_id:0),static_cast<unsigned long long>(allocation?allocation->diagnostic_backing:0),static_cast<unsigned long long>(allocation?allocation->diagnostic_version:0),
 allocation?allocation->surface.width:0,allocation?allocation->surface.height:0,allocation?allocation->surface.format:0,allocation&&allocation->runtime_resource?"true":"false",static_cast<unsigned long long>(argument));
}
inline void resource_written(const char *operation,Allocation &allocation) noexcept {++allocation.diagnostic_version;resource_boundary(operation,&allocation);}
struct ResourceBoundaryScope {
 uint64_t previous=diagnostic_call,context=0;bool enabled=false,complete=false;
 explicit ResourceBoundaryScope(const Context *c,const char *operation) noexcept:context(c?c->diagnostic_id:0){
  enabled=std::strstr(operation,"resource")||std::strstr(operation,"copy")||std::strstr(operation,"device")||std::strstr(operation,"draw")||std::strstr(operation,"clear")||std::strstr(operation,"map")||std::strstr(operation,"flush")||std::strstr(operation,"present")||std::strstr(operation,"rotate")||std::strstr(operation,"hazard");
  if(enabled){diagnostic_call=diagnostic_identity();resource_boundary(operation,nullptr,context);resource_boundary("call_begin",nullptr,context);}
 }
 void success() noexcept {complete=true;}
 ~ResourceBoundaryScope(){if(enabled)resource_boundary(complete?"call_success":"call_failed",nullptr,context);diagnostic_call=previous;}
};
}
