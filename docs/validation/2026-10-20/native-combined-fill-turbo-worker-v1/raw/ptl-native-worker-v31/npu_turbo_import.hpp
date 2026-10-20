#pragma once
#include <openvino/openvino.hpp>
#include <cstdio>
#include <istream>
#include <stdexcept>
namespace nr::raster {
// Research candidate: this hint applies only to the imported PTL models.
// It is not a global Windows power policy or a demonstrated Tahoma benefit.
inline ov::CompiledModel import_ptl_turbo(ov::Core &core,std::istream &graph,const char *route){
 auto model=core.import_model(graph,"NPU",ov::AnyMap{{"NPU_TURBO",true}});
 if(!model.get_property("NPU_TURBO").as<bool>())throw std::runtime_error("PTL turbo import readback");
 std::printf("{\"ptl_turbo_import_verified\":true,\"route\":\"%s\",\"requested\":true,\"readback\":true,\"global_power_policy_changed\":false}\n",route);std::fflush(stdout);
 return model;
}
}
