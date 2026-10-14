#pragma once
#include "provider_compile.hpp"
#include <openvino/openvino.hpp>
#include <openvino/opsets/opset13.hpp>
#include <map>
#include <optional>
#include <array>
#include <cmath>
#include <cstring>
namespace nr::raster {
// Host-side specialization proof over already device-transformed geometry.
// Pixel production, alpha replacement, channel order and merging run on backend.
class OpaqueAffineCrop {
    using Range=std::array<double,2>;
    using Key=std::array<uint32_t,6>; // output x/y/w/h, source x/y
    uint32_t sw,sh,w,h;bool swap,failed=false;std::string backend;ov::Core core;
    struct Entry {ov::CompiledModel model;ov::InferRequest request;std::string device;bool needs_background;};
    std::map<Key,Entry> entries;
    static double ulp(double x){return x==0?std::ldexp(1.,-149):std::ldexp(1.,(std::max)(-126,int(std::floor(std::log2(std::abs(x)))))-23);}
    static Range pad(double low,double high,double units){const auto e=(std::max)(ulp(low),ulp(high))*units+1e-12;return {low-e,high+e};}
    static Range mul(Range a,Range b){const double v[]{a[0]*b[0],a[0]*b[1],a[1]*b[0],a[1]*b[1]};return pad(*std::min_element(v,v+4),*std::max_element(v,v+4),.5);}
    static Range add(Range a,Range b){return pad(a[0]+b[0],a[1]+b[1],.5);}
    struct Axis {int32_t offset;double error;};
    static std::optional<Axis> axis(uint32_t extent,int32_t lo,int32_t hi,float u0,float u1,int32_t first,int32_t end){
        if(lo>=hi || first>=end || !std::isfinite(u0) || !std::isfinite(u1) || u0<0 || u1>1 || u0>=u1)return {};
        const auto low=double(lo)/256,span=double(hi-lo)/256;const auto delta=pad(double(u1)-u0,double(u1)-u0,.5);
        Axis result{};
        for(int32_t x=first;x<end;++x){
            // Snapped bounds, half-pixel centers and subtraction are exact in
            // FP32 at these dimensions. Division allows 1 ULP; arithmetic .5.
            const auto t=(x+.5-low)/span;
            auto mapped=mul(add({u0,u0},mul(pad(t,t,1),delta)),{double(extent),double(extent)});
            mapped[0]-=.5;mapped[1]-=.5;
            if(x==first)result.offset=int32_t(std::round((mapped[0]+mapped[1])*.5))-x;
            const auto expected=x+result.offset;
            if(expected<0 || expected>=int32_t(extent))return {};
            result.error=(std::max)({result.error,std::abs(mapped[0]-expected),std::abs(mapped[1]-expected)});
        }
        return result;
    }
    Entry create(const Key &key){
        namespace op=ov::opset13;using Node=std::shared_ptr<ov::Node>;
        const auto values=[](std::initializer_list<int64_t> a){return op::Constant::create(ov::element::i64,ov::Shape{a.size()},std::vector<int64_t>(a));};
        auto source=std::make_shared<op::Parameter>(ov::element::u8,ov::Shape{sh,sw,4});source->output(0).get_tensor().set_names({"source"});
        auto bg=std::make_shared<op::Parameter>(ov::element::u8,ov::Shape{h,w,4});bg->output(0).get_tensor().set_names({"background"});
        auto slice=[&](const Node &node,uint32_t y,uint32_t x,uint32_t rows,uint32_t cols,uint32_t channel,uint32_t channels)->Node{
            return std::make_shared<op::Slice>(node,values({y,x,channel}),values({y+rows,x+cols,channel+channels}),values({1,1,1}),values({0,1,2}));
        };
        const auto x=key[0],y=key[1],cw=key[2],ch=key[3],sx=key[4],sy=key[5];ov::OutputVector channels;
        for(uint32_t c=0;c<3;++c)channels.push_back(slice(source,sy,sx,ch,cw,swap?2-c:c,1));
        channels.push_back(op::Constant::create(ov::element::u8,ov::Shape{ch,cw,1},{uint8_t(255)}));
        Node center=std::make_shared<op::Concat>(channels,2);ov::OutputVector middle;
        if(x)middle.push_back(slice(bg,y,0,ch,x,0,4));middle.push_back(center);
        if(x+cw<w)middle.push_back(slice(bg,y,x+cw,ch,w-x-cw,0,4));
        Node row=middle.size()==1?middle[0].get_node_shared_ptr():Node(std::make_shared<op::Concat>(middle,1));ov::OutputVector output;
        if(y)output.push_back(slice(bg,0,0,y,w,0,4));output.push_back(row);
        if(y+ch<h)output.push_back(slice(bg,y+ch,0,h-y-ch,w,0,4));
        Node result=output.size()==1?output[0].get_node_shared_ptr():Node(std::make_shared<op::Concat>(output,0));
        const bool needs_background=x || y || cw!=w || ch!=h;
        ov::ParameterVector ports{source};if(needs_background)ports.push_back(bg);
        auto graph=std::make_shared<ov::Model>(ov::OutputVector{result},ports);
        const ov::AnyMap properties{ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY)};
        auto model=nr::provider::compile_model(core,graph,backend,properties);const auto devices=model.get_property(ov::execution_devices);
        if(devices.size()!=1 || !(devices[0]==backend || devices[0].rfind(backend+".",0)==0))throw std::runtime_error("crop device mismatch");
        return {model,model.create_infer_request(),devices[0],needs_background};
    }
public:
    OpaqueAffineCrop(uint32_t source_w,uint32_t source_h,uint32_t width,uint32_t height,bool exchange,std::string selected):sw(source_w),sh(source_h),w(width),h(height),swap(exchange),backend(std::move(selected)){
        if(!sw || sw>8192 || !sh || sh>8192 || uint64_t(sw)*sh>8388608 || !w || w>4096 || !h || h>8192 || uint64_t(w)*h>8388576 || (backend!="CPU" && backend!="NPU" && backend!="GPU"))throw std::invalid_argument("invalid crop dimensions/backend");
    }
    struct Result {std::vector<PackedColor> pixels;std::string device;uint64_t covered;double maximum_byte_error;Key mapping;};
    std::optional<Result> execute(const std::array<int32_t,4> &bounds,const std::array<int32_t,4> &clip,const std::array<float,4> &uv,const std::array<float,4> &color,const PackedTexture &source,const std::vector<PackedColor> &background,bool point){
        if(failed)throw std::runtime_error("crop context poisoned");
        if(point || color!=std::array<float,4>{1,1,1,1})return {};
        const auto first=[](int32_t bound){return int32_t(std::ceil((double(bound)-128)/256));};
        const int32_t x=(std::max)(clip[0],first(bounds[0])),y=(std::max)(clip[1],first(bounds[1]));
        const int32_t right=(std::min)(clip[2],first(bounds[2])),bottom=(std::min)(clip[3],first(bounds[3]));
        if(x<0 || y<0 || x>=right || y>=bottom || right>int32_t(w) || bottom>int32_t(h))return {};
        const auto ax=axis(sw,bounds[0],bounds[2],uv[0],uv[2],x,right),ay=axis(sh,bounds[1],bounds[3],uv[1],uv[3],y,bottom);if(!ax || !ay)return {};
        // For any u8 texture, bilinear deviation from this integer texel is
        // <=255*(ex+ey). Reserve .002 byte for normalized-value arithmetic.
        const auto error=255*(ax->error+ay->error)+.002;if(!(error<.5))return {};
        if(source.width!=sw || source.height!=sh || source.pixels.size()!=size_t(sw)*sh || background.size()!=size_t(w)*h)throw std::invalid_argument("crop storage mismatch");
        const Key key{uint32_t(x),uint32_t(y),uint32_t(right-x),uint32_t(bottom-y),uint32_t(x+ax->offset),uint32_t(y+ay->offset)};
        failed=true;
        if(!entries.count(key)){if(entries.size()>=8)entries.erase(entries.begin());entries.emplace(key,create(key));}
        auto &entry=entries.at(key);std::memcpy(entry.request.get_tensor("source").data<uint8_t>(),source.pixels.data(),source.pixels.size()*4);if(entry.needs_background)std::memcpy(entry.request.get_tensor("background").data<uint8_t>(),background.data(),background.size()*4);
        entry.request.infer();const auto tensor=entry.request.get_output_tensor();if(tensor.get_element_type()!=ov::element::u8 || tensor.get_byte_size()!=background.size()*4)throw std::runtime_error("crop output mismatch");
        Result result{std::vector<PackedColor>(background.size()),entry.device,uint64_t(right-x)*uint64_t(bottom-y),error,key};std::memcpy(result.pixels.data(),tensor.data<const uint8_t>(),tensor.get_byte_size());failed=false;return result;
    }
};
}
