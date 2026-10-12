#pragma once
#include <openvino/openvino.hpp>
#include <openvino/core/attribute_visitor.hpp>
#include <openvino/core/runtime_attribute.hpp>
#include <openvino/core/version.hpp>
#include <cstring>
namespace nr::provider {
inline bool precision_preserved(const std::shared_ptr<ov::Node> &node){
    const auto &attributes=node->get_rt_info();
    if(attributes.count("precise_0"))return true;
    const auto migrated=attributes.find("DisablePrecisionConversion_0");
    if(migrated==attributes.end())return false;
    struct Visitor final:ov::AttributeVisitor{
        unsigned values=0;bool valid=true;
        void on_adapter(const std::string &,ov::ValueAccessor<void> &)override{valid=false;}
        void on_adapter(const std::string &name,ov::ValueAccessor<std::string> &value)override{
            ++values;valid=valid && name=="value" && value.get()=="dynamic:f16";
        }
    } visitor;
    // IR normalization in 2026.4 migrates precise_0 to this typed map. Check
    // its actual prohibition, rather than accepting the new key by name alone.
    try{return migrated->second.as<ov::RuntimeAttribute>().visit_attributes(visitor) && visitor.valid && visitor.values==1;}
    catch(const ov::Exception &){return false;}
}
inline uint32_t compositor_batch_capacity(){
    // Only this isolated, qualified runtime enables the larger graph shape.
    // Other versions retain the older compiler's verified 512-lane limit.
    static const auto capacity=std::strcmp(ov::get_openvino_version().buildNumber,
        "2026.4.1-22982-e213a147257-releases/2026/4")==0?16384u:512u;
    return capacity;
}
}
