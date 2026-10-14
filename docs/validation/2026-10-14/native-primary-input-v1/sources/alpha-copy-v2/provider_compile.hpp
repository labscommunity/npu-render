#pragma once
#include <openvino/openvino.hpp>
#include "provider_compile_statistics.hpp"
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
inline CompiledMemoryCache &compiled_memory_cache(){static CompiledMemoryCache cache(64);return cache;}
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
    auto compile=[&](){
        std::unique_lock<std::mutex> shared_lock(shared_npu_compile_mutex(),std::defer_lock);if(one_core)shared_lock.lock();
        if(!directory.empty()){core.set_property(ov::cache_dir(isolated_cache_directory(directory,model).string()));++cache_requests;}
        if(shared){
            const ov::AnyMap queue_properties{{"NPU_SHARED_COMMON_QUEUE",*shared}};core.set_property("NPU",queue_properties);
            if(core.get_property("NPU","NPU_SHARED_COMMON_QUEUE").as<bool>()!=*shared)throw std::runtime_error("shared queue property verification failed");
            static std::atomic<bool> logged{false};if(!logged.exchange(true)){std::printf("{\"kind\":\"private_shared_queue_experiment\",\"enabled\":%s,\"property_readback_verified\":true,\"hardware_queue_count_measured\":false}\n",*shared?"true":"false");std::fflush(stdout);}
        }
        ++backend_compile_calls;ov::CompiledModel compiled;{CompileTimer backend_timer(backend_compile_ns);compiled=core.compile_model(model,backend,configured);}
        if(!directory.empty()){try{if(compiled.get_property(ov::loaded_from_cache))++cache_hits;}catch(const ov::Exception &){++cache_status_unavailable;}}
        return compiled;
    };
    if(backend=="NPU" && memory_cache_experiment())return compiled_memory_cache().get(core,model,backend,configured,directory,compile);
    return compile();
}
}
