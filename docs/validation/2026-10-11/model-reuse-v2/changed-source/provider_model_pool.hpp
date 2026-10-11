#pragma once
#include "provider_cache.hpp"
#include <mutex>
#include <atomic>
#include <map>
#include <memory>
#include <cstdlib>
namespace nr::provider {
using CompileOwner=std::shared_ptr<ov::CompiledModel>;
inline std::atomic<uint64_t> live_model_hits{0};
inline bool reuse_compiled_models(){
    char *value=nullptr;size_t length=0;
    if(_dupenv_s(&value,&length,"NR_RENDER_REUSE_COMPILED"))throw std::runtime_error("cannot read model reuse configuration");
    const std::unique_ptr<char,decltype(&std::free)> owned(value,&std::free);
    return value && std::string(value)=="1";
}
inline std::map<std::string,std::weak_ptr<ov::CompiledModel>> &compiled_models(){
    static std::map<std::string,std::weak_ptr<ov::CompiledModel>> pool;return pool;
}
inline std::mutex &compiled_models_mutex(){static std::mutex mutex;return mutex;}
inline std::string compiled_model_identity(const std::shared_ptr<ov::Model> &model,const std::string &backend,const ov::AnyMap &properties){
    CacheDigest digest;digest.field("npu-render-live-model-v1");digest.field(backend);digest.field(cache_model_identity(model));
    // Parameter binding names are observable. Preserve them even when the IR
    // friendly name matches, rather than coalescing different shader registers.
    for(const auto &parameter:model->get_parameters()){
        std::vector<std::string> names(parameter->output(0).get_names().begin(),parameter->output(0).get_names().end());
        std::sort(names.begin(),names.end());digest.field(std::to_string(names.size()));
        for(const auto &name:names)digest.field(name);
    }
    for(const auto &property:properties){digest.field(property.first);digest.field(property.second.as<std::string>());}
    return digest.finish();
}
}
