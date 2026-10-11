#pragma once
#include <openvino/openvino.hpp>
#include "provider_compile_statistics.hpp"
#include "provider_precision.hpp"
#include "provider_cache.hpp"
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
    const std::string &backend,const ov::AnyMap &properties){
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
    return compiled;
}
}
