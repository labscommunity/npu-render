#pragma once
#include <openvino/openvino.hpp>
#include "provider_compile_statistics.hpp"
#include "provider_compile_trace.hpp"
#include "provider_precision.hpp"
#include "provider_cache.hpp"
#include "provider_memory_cache.hpp"
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <optional>
#include <atomic>
#include <cstdio>
#include <cstring>
namespace nr::provider {
// Opt-in graph-shape experiment, independent of shader/transport contracts.
inline uint32_t generic_raster_capacity(){
    char *text=nullptr;size_t length=0;if(_dupenv_s(&text,&length,"NR_RENDER_GENERIC_BATCH"))throw std::runtime_error("cannot read generic batch experiment");
    if(!text)return 4096;const std::string value(text);std::free(text);
    if(value=="4096")return 4096;if(value=="8192")return 8192;if(value=="16384")return 16384;
    throw std::invalid_argument("invalid generic batch experiment");
}
inline std::string model_cache_directory(){
#ifdef _MSC_VER
    char *value=nullptr;size_t length=0;
    if(_dupenv_s(&value,&length,"NR_RENDER_NPU_CACHE"))throw std::runtime_error("cannot read NPU cache configuration");
    const std::unique_ptr<char,decltype(&std::free)> owner(value,&std::free);
#else
    const char *value=std::getenv("NR_RENDER_NPU_CACHE");
#endif
    return value?std::string(value):std::string{};
}
// Isolated experiment only. Private API is restricted to the exact runtime
// whose primary source and installed default were inspected.
inline std::optional<bool> shared_queue_experiment(){
#ifdef _MSC_VER
    char *value=nullptr;size_t length=0;
    if(_dupenv_s(&value,&length,"NR_RENDER_SHARED_QUEUE"))throw std::runtime_error("cannot read shared queue experiment");
    const std::unique_ptr<char,decltype(&std::free)> owner(value,&std::free);
#else
    const char *value=std::getenv("NR_RENDER_SHARED_QUEUE");
#endif
    if(!value || !*value)return std::nullopt;
    if(std::strcmp(value,"0") && std::strcmp(value,"1"))throw std::invalid_argument("shared queue experiment requires 0 or 1");
    if(std::strcmp(ov::get_openvino_version().buildNumber,"2026.4.1-22982-e213a147257-releases/2026/4"))
        throw std::invalid_argument("shared queue experiment requires the pinned qualified runtime");
    return value[0]=='1';
}

inline bool memory_cache_experiment(){
#ifdef _MSC_VER
    char *value=nullptr;size_t length=0;if(_dupenv_s(&value,&length,"NR_RENDER_MEMORY_CACHE"))throw std::runtime_error("cannot read model memory cache configuration");const std::unique_ptr<char,decltype(&std::free)> owner(value,&std::free);
#else
    const char *value=std::getenv("NR_RENDER_MEMORY_CACHE");
#endif
    if(!value || !*value || !std::strcmp(value,"0"))return false;
    if(std::strcmp(value,"1"))throw std::invalid_argument("model memory cache requires 0 or 1");
    if(std::strcmp(ov::get_openvino_version().buildNumber,"2026.4.1-22982-e213a147257-releases/2026/4"))throw std::invalid_argument("model memory cache requires the pinned runtime");
    return true;
}
inline size_t memory_cache_capacity(){
    char *text=nullptr;size_t length=0;if(_dupenv_s(&text,&length,"NR_RENDER_MEMORY_CACHE_CAPACITY"))throw std::runtime_error("cannot read memory cache capacity");
    const std::unique_ptr<char,decltype(&std::free)> owner(text,&std::free);
    if(!text || !*text || !std::strcmp(text,"64"))return 64;
    if(!std::strcmp(text,"512"))return 512;
    throw std::invalid_argument("memory cache capacity requires 64 or 512");
}
inline CompiledMemoryCache &compiled_memory_cache(){static CompiledMemoryCache cache(memory_cache_capacity());return cache;}
inline void poison_compiled_memory_cache(){compiled_memory_cache().poison();}
inline bool shared_core_experiment(){
    char *text=nullptr;size_t length=0;if(_dupenv_s(&text,&length,"NR_RENDER_SHARED_CORE"))throw std::runtime_error("cannot read shared core experiment");
    if(!text)return false;const std::string value(text);std::free(text);
    if(value!="0" && value!="1")throw std::invalid_argument("invalid shared core experiment");return value=="1";
}
inline ov::Core &shared_npu_core(){static ov::Core value;return value;}
inline std::mutex &shared_npu_compile_mutex(){static std::mutex value;return value;}
inline ov::CompiledModel compile_model(ov::Core &caller,const std::shared_ptr<ov::Model> &model,
    const std::string &backend,const ov::AnyMap &properties){
    const bool one_core=backend=="NPU" && shared_core_experiment();auto &core=one_core?shared_npu_core():caller;
    if(one_core){static std::atomic<bool> logged{false};if(!logged.exchange(true)){std::printf("{\"shared_provider_core_experiment\":true,\"single_core_for_npu_compilation\":true,\"hardware_context_count_measured\":false}\n");std::fflush(stdout);}}
    ++compile_calls;CompileTimer setup_timer(provider_setup_ns);const auto directory=backend=="NPU"?model_cache_directory():std::string{};
    if(!directory.empty()){const std::filesystem::path path(directory);if(!path.is_absolute() || !std::filesystem::is_directory(path))throw std::invalid_argument("NPU cache must be an existing absolute directory");}
    auto configured=properties;const auto shared=backend=="NPU"?shared_queue_experiment():std::nullopt;
    if(shared)configured["NPU_SHARED_COMMON_QUEUE"]=*shared;
    if(backend=="NPU")configured["NPU_TURBO"]=true;
    auto compile=[&](){
        std::unique_lock<std::mutex> shared_lock(shared_npu_compile_mutex(),std::defer_lock);if(one_core)shared_lock.lock();
        const bool trace=compile_trace_enabled();std::string graph_identity;
        if(!directory.empty()){const auto path=isolated_cache_directory(directory,model);core.set_property(ov::cache_dir(path.string()));++cache_requests;if(trace)graph_identity=path.filename().string();}
        if(trace && graph_identity.empty())graph_identity=cache_model_identity(model);
        if(shared){
            const ov::AnyMap queue_properties{{"NPU_SHARED_COMMON_QUEUE",*shared}};core.set_property("NPU",queue_properties);
            if(core.get_property("NPU","NPU_SHARED_COMMON_QUEUE").as<bool>()!=*shared)throw std::runtime_error("shared queue property verification failed");
            static std::atomic<bool> logged{false};if(!logged.exchange(true)){std::printf("{\"kind\":\"private_shared_queue_experiment\",\"enabled\":%s,\"property_readback_verified\":true,\"hardware_queue_count_measured\":false}\n",*shared?"true":"false");std::fflush(stdout);}
        }
        CompileTrace compile_trace(trace,graph_identity,trace?model->get_ordered_ops().size():0,trace?model->inputs().size():0);
        ++backend_compile_calls;ov::CompiledModel compiled;{CompileTimer backend_timer(backend_compile_ns);compiled=core.compile_model(model,backend,configured);}
        if(backend=="NPU"){
            if(!compiled.get_property("NPU_TURBO").as<bool>())throw std::runtime_error("compiled NPU turbo readback failed");
            std::printf("{\"compiled_turbo_verified\":true,\"requested\":true,\"readback\":true,\"global_power_policy_changed\":false}\n");std::fflush(stdout);
        }
        int loaded=-1;if(!directory.empty()){try{loaded=compiled.get_property(ov::loaded_from_cache)?1:0;if(loaded)++cache_hits;}catch(const ov::Exception &){++cache_status_unavailable;}}
        compile_trace.success(loaded);
        return compiled;
    };
    if(backend=="NPU" && memory_cache_experiment())return compiled_memory_cache().get(core,model,backend,configured,directory,compile);
    return compile();
}
}
