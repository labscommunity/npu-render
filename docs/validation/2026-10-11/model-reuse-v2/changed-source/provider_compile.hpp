#pragma once
#include <openvino/openvino.hpp>
#include "provider_compile_statistics.hpp"
#include "provider_precision.hpp"
#include "provider_cache.hpp"
#include "provider_model_pool.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
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
inline ov::CompiledModel compile_model(ov::Core &core,const std::shared_ptr<ov::Model> &model,
    const std::string &backend,const ov::AnyMap &properties,CompileOwner &owner){
    const auto begin=std::chrono::steady_clock::now();
    const bool reuse=backend=="NPU" && reuse_compiled_models();
    const auto key=reuse?compiled_model_identity(model,backend,properties):std::string{};
    std::unique_lock<std::mutex> lock(compiled_models_mutex(),std::defer_lock);
    if(reuse){
        lock.lock();auto &pool=compiled_models();
        for(auto it=pool.begin();it!=pool.end();)if(it->second.expired())it=pool.erase(it);else ++it;
        const auto found=pool.find(key);
        if(found!=pool.end())if(auto existing=found->second.lock()){
            ++live_model_hits;owner=std::move(existing);return *owner;
        }
    }
    ++compile_calls;
    const auto directory=backend=="NPU"?model_cache_directory():std::string{};
    if(!directory.empty()){
        const std::filesystem::path path(directory);
        if(!path.is_absolute() || !std::filesystem::is_directory(path))throw std::invalid_argument("NPU cache must be an existing absolute directory");
        // Explicit controller-owned cache only. OpenVINO keys entries by graph
        // and compilation properties; frame pixels remain runtime inputs.
        core.set_property(ov::cache_dir(isolated_cache_directory(path,model).string()));++cache_requests;
    }
    auto compiled=core.compile_model(model,backend,properties);
    if(!directory.empty()){
        try{if(compiled.get_property(ov::loaded_from_cache))++cache_hits;}
        catch(const ov::Exception &){++cache_status_unavailable;}
    }
    if(reuse){owner=std::make_shared<ov::CompiledModel>(compiled);compiled_models()[key]=owner;}
    std::fprintf(stderr,"{\"kind\":\"provider_compile_duration\",\"reuse_enabled\":%s,\"milliseconds\":%.3f}\n",reuse?"true":"false",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
    return compiled;
}
}
