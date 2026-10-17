#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <stdexcept>
namespace nr::control {
struct Adapter {uint32_t vendor=0,device=0,flags=0,low=0;int32_t high=0;};
inline std::size_t choose(const std::vector<Adapter>& adapters,uint32_t vendor,uint32_t device){
    if(!vendor || !device || adapters.empty() || adapters.size()>64)throw std::invalid_argument("invalid GPU control selection");
    std::size_t selected=adapters.size();
    for(std::size_t i=0;i<adapters.size();++i){const auto &a=adapters[i];
        if(a.vendor!=vendor || a.device!=device || a.flags || (!a.low&&!a.high))continue;
        if(selected!=adapters.size())throw std::runtime_error("ambiguous hardware GPU control");selected=i;
    }
    if(selected==adapters.size())throw std::runtime_error("hardware GPU control unavailable");return selected;
}
}
