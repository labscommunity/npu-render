#pragma once
#include "provider_cache.hpp"
#include "provider_compile_statistics.hpp"
#include <list>
#include <mutex>
#include <vector>

namespace nr::provider {
inline std::string compiled_memory_identity(const std::shared_ptr<ov::Model> &model,
    const std::string &backend,const ov::AnyMap &properties,const std::string &directory){
    CacheDigest digest;digest.field("npu-render-compiled-memory-v1");
    digest.field(cache_model_identity(model));digest.field(backend);digest.field(directory);
    // Compiled models expose their original port aliases. Do not reuse one
    // with different aliases, even when the canonical graph is identical.
    for(const auto &ports:{model->inputs(),model->outputs()}){
        digest.field(std::to_string(ports.size()));
        for(const auto &port:ports){
            const auto &names=port.get_names();std::vector<std::string> sorted(names.begin(),names.end());
            std::sort(sorted.begin(),sorted.end());digest.field(std::to_string(sorted.size()));
            for(const auto &name:sorted)digest.field(name);
        }
    }
    digest.field(std::to_string(properties.size()));
    for(const auto &entry:properties){
        digest.field(entry.first);const auto &value=entry.second;
        if(value.is<bool>()){digest.field("bool");digest.field(value.as<bool>()?"1":"0");}
        else if(value.is<std::string>()){digest.field("string");digest.field(value.as<std::string>());}
        else if(value.is<ov::hint::PerformanceMode>()){digest.field("performance_mode");digest.field(std::to_string(static_cast<int>(value.as<ov::hint::PerformanceMode>())));}
        else if(value.is<ov::hint::ExecutionMode>()){digest.field("execution_mode");digest.field(std::to_string(static_cast<int>(value.as<ov::hint::ExecutionMode>())));}
        else throw std::invalid_argument("unsupported compiled memory cache property type");
    }
    return digest.finish();
}

// This experiment owns all caller Core configuration. The key includes every
// explicit compilation property; callers must not supply untracked Core-level
// configuration or extensions. Each consumer creates its own InferRequest.
class CompiledMemoryCache {
    struct Entry {std::string key;ov::Core owner;ov::CompiledModel model;};
    const size_t capacity;
    std::mutex mutex;
    std::list<Entry> entries;
    bool failed=false;
public:
    explicit CompiledMemoryCache(size_t count):capacity(count){
        if(!count || count>64)throw std::invalid_argument("compiled memory cache capacity out of range");
    }
    template<class Compile>
    ov::CompiledModel get(ov::Core &core,const std::shared_ptr<ov::Model> &model,
        const std::string &backend,const ov::AnyMap &properties,const std::string &directory,Compile compile){
        const auto key=compiled_memory_identity(model,backend,properties,directory);
        std::lock_guard<std::mutex> lock(mutex);
        if(failed)throw std::runtime_error("compiled memory cache is poisoned");
        ++memory_cache_requests;
        for(auto it=entries.begin();it!=entries.end();++it)if(it->key==key){
            ++memory_cache_hits;entries.splice(entries.begin(),entries,it);return entries.front().model;
        }
        try{
            // Complete retirement before compiling a successor. The pinned NPU
            // stack has previously failed when last references were released
            // after replacement model creation in the pipeline caches.
            if(entries.size()==capacity){entries.pop_back();++memory_cache_evictions;}
            auto compiled=compile();
            const auto devices=compiled.get_property(ov::execution_devices);
            if(devices.size()!=1 || !(devices[0]==backend || devices[0].rfind(backend+".",0)==0))
                throw std::runtime_error("compiled memory cache device mismatch");
            entries.push_front(Entry{key,core,compiled});
            return compiled;
        }catch(...){failed=true;entries.clear();throw;}
    }
    void poison(){std::lock_guard<std::mutex> lock(mutex);failed=true;entries.clear();}
    size_t size(){std::lock_guard<std::mutex> lock(mutex);return entries.size();}
};
}
