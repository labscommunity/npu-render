#pragma once
#include <openvino/openvino.hpp>
#include <openvino/pass/serialize.hpp>
#include <algorithm>
#include <array>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace nr::provider {
class CacheDigest {
    void *algorithm=nullptr;
    void *hash=nullptr;
public:
    CacheDigest();
    ~CacheDigest();
    CacheDigest(const CacheDigest &)=delete;
    CacheDigest &operator=(const CacheDigest &)=delete;
    void bytes(const void *input,size_t size);
    void field(const std::string &value);
    std::string finish();
};
inline std::string cache_model_identity(const std::shared_ptr<ov::Model> &model){
    // Clone only for hashing; the graph submitted to the provider is untouched.
    // Canonical generated names let the same graph reuse a cache entry when
    // unrelated graphs were constructed earlier in the process.
    auto copy=model->clone();copy->set_friendly_name("npu_render_cache_model");
    const auto nodes=copy->get_ordered_ops();std::unordered_map<std::string,std::string> names;
    for(size_t i=0;i<nodes.size();++i)names.emplace(nodes[i]->get_friendly_name(),"node_"+std::to_string(i));
    for(size_t i=0;i<nodes.size();++i){
        auto &node=nodes[i];node->set_friendly_name("node_"+std::to_string(i));
        for(auto output:node->outputs()){
            std::unordered_set<std::string> normalized;
            for(const auto &name:output.get_names()){
                const auto found=names.find(name);normalized.insert(found==names.end()?name:found->second);
            }
            output.get_tensor().set_names(normalized);
        }
    }
    std::ostringstream xml,weights;ov::pass::Serialize serializer(xml,weights);serializer.run_on_model(copy);
    CacheDigest digest;digest.field("npu-render-graph-cache-v1");
    digest.field(ov::get_openvino_version().buildNumber);digest.field(xml.str());digest.field(weights.str());
    return digest.finish();
}
inline std::filesystem::path isolated_cache_directory(const std::filesystem::path &root,const std::shared_ptr<ov::Model> &model){
    const auto path=root/"graph-sha256-v1"/cache_model_identity(model);
    std::filesystem::create_directories(path);return path;
}
}
