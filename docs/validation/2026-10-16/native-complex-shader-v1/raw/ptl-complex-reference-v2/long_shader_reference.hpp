#pragma once
#include <algorithm>
// Test/reference evaluation only. Never a fallback for the NPU provider.
#include "shader_ir.hpp"
#include "shader_texture.hpp"
#include <cmath>
#include <cstring>
namespace nr::shader {
using Bits = std::array<uint32_t, 4>;
using Registers = std::array<Bits, 16>;
using Constants = std::array<std::vector<Bits>, 14>;
inline float as_float(uint32_t value) { float result; std::memcpy(&result, &value, 4); return result; }
inline uint32_t as_bits(float value) { uint32_t result; std::memcpy(&result, &value, 4); return result; }
inline Registers reference_vertex(const Program &program, const Registers &inputs, const Constants &constants,
                                 const TextureShapes &shapes = {}, const TextureSamplers &samplers = {},
                                 const TexturePixels &textures = {}, unsigned fused_arithmetic = 0,
                                 const std::vector<Bits> *derivatives = nullptr, bool reciprocal_division = false) {
    Registers temporary{}, output{};
    for (unsigned i = 0; i < 14; ++i) check(constants[i].size() >= program.constants[i]);
    auto load = [&](const Operand &a, bool integer_modifier = false) {
        Bits raw{};
        if (a.kind == Register::temporary) raw = temporary[a.index];
        else if (a.kind == Register::input) raw = inputs[a.index];
        else if (a.kind == Register::immediate) raw = a.literal;
        else if (a.kind == Register::constant || a.kind == Register::indexable) {
            auto index = a.element;
            if (a.relative.enabled) {
                const auto &registers = a.relative.kind == Register::temporary ? temporary : inputs;
                index += registers[a.relative.index][a.relative.component];
            }
            // Unsigned 32-bit addressing and zero on an out-of-range CB read.
            if(a.kind==Register::indexable){check(index<program.literal_tables[a.index].size());raw=program.literal_tables[a.index][index];}
            else if (index < program.constants[a.index]) raw = constants[a.index][index];
        } else check(false);
        Bits result{};
        for (unsigned i = 0; i < 4; ++i) {
            auto value = raw[a.lanes[i]];
            if (a.modifier == Modifier::absolute || a.modifier == Modifier::absolute_negate) value &= 0x7fffffffu;
            if (a.modifier == Modifier::negate || a.modifier == Modifier::absolute_negate)
                value = integer_modifier ? 0u - value : value ^ 0x80000000u;
            result[i] = value;
        }
        return result;
    };
    struct Branch { bool parent, condition; };
    std::vector<Branch> branches;
    struct Loop {size_t begin,end;unsigned iterations=0;};
    std::vector<Loop> loops;
    bool active = true;
    for (size_t instruction_index=0;instruction_index<program.instructions.size();++instruction_index) {
        const auto &instruction=program.instructions[instruction_index];
        if(instruction.opcode==Opcode::begin_loop){
            check(active && loops.empty());size_t end=instruction_index+1;
            while(end<program.instructions.size() && program.instructions[end].opcode!=Opcode::end_loop)++end;
            check(end<program.instructions.size());loops.push_back({instruction_index,end});continue;
        }
        if(instruction.opcode==Opcode::break_if){
            check(active && !loops.empty());
            if((load(instruction.sources[0])[0]!=0)==instruction.test_nonzero){instruction_index=loops.back().end;loops.pop_back();}
            else if(++loops.back().iterations>maximum_loop_iterations)throw std::runtime_error("shader loop iteration budget exceeded");
            continue;
        }
        if(instruction.opcode==Opcode::end_loop){check(active && !loops.empty());instruction_index=loops.back().begin;continue;}
        if (instruction.opcode == Opcode::begin_if) {
            const bool condition = active && ((load(instruction.sources[0])[0] != 0) == instruction.test_nonzero);
            branches.push_back({active, condition}); active = active && condition; continue;
        }
        if (instruction.opcode == Opcode::otherwise) {
            const auto &b = branches.back(); active = b.parent && !b.condition; continue;
        }
        if (instruction.opcode == Opcode::end_if) { active = branches.back().parent; branches.pop_back(); continue; }
        if (!active) continue;
        if(instruction.opcode==Opcode::loop_overflow)throw std::runtime_error("shader loop iteration budget exceeded");
        std::array<Bits, 3> values{};
        for (unsigned i = 0; i < ((instruction.opcode == Opcode::sample || instruction.opcode==Opcode::load) ? 1u : instruction.source_count); ++i)
            values[i] = load(instruction.sources[i], instruction.opcode == Opcode::integer_add || instruction.opcode==Opcode::integer_mad);
        Bits value{};
        for (unsigned i = 0; i < 4; ++i) {
            if (!(instruction.destination.mask & (1u << i))) continue;
            if(instruction.opcode==Opcode::derivative_x || instruction.opcode==Opcode::derivative_y){
                check(derivatives && instruction_index<derivatives->size());value[i]=(*derivatives)[instruction_index][i];
            }
            else if (instruction.opcode == Opcode::move) value[i] = values[0][i];
            else if (instruction.opcode == Opcode::select) value[i] = values[0][i] ? values[1][i] : values[2][i];
            else if (instruction.opcode == Opcode::integer_add) value[i] = values[0][i] + values[1][i];
            else if(instruction.opcode==Opcode::bitwise_and)value[i]=values[0][i]&values[1][i];
            else if(instruction.opcode==Opcode::bitwise_or)value[i]=values[0][i]|values[1][i];
            else if(instruction.opcode==Opcode::unsigned_shift_right)value[i]=values[0][i]>>(values[1][0]&31);
            else if(instruction.opcode==Opcode::integer_mad)value[i]=values[0][i]*values[1][i]+values[2][i];
            else if(instruction.opcode==Opcode::shift_right){
                const auto amount=values[1][0]&31,raw=values[0][i];
                value[i]=(raw>>amount)|((raw&0x80000000u) && amount ? (~0u<<(32-amount)):0u);
            }
            else if(instruction.opcode==Opcode::float_to_integer){
                const auto v=as_float(values[0][i]);int32_t converted=std::isnan(v)?0:v>=2147483648.f?INT32_MAX:v<=-2147483648.f?INT32_MIN:int32_t(v);
                std::memcpy(&value[i],&converted,4);
            }
            else if(instruction.opcode==Opcode::load){
                const auto slot=instruction.sources[1].index,component=instruction.sources[1].lanes[i];const auto shape=shapes[slot];
                if(!shape.width && !shape.height)value[i]=0;
                else if(!(shape.components&(1u<<component)))value[i]=component==3?(program.texture_uint[slot]?1u:as_bits(1.f)):0;
                else {
                    const auto x=values[0][0],y=program.texture_dimensions[slot]==1?0u:values[0][1];
                    const bool valid=x<shape.width && y<shape.height && (program.texture_dimensions[slot]==1 || values[0][3]==0);
                    check(textures[slot].size()==size_t(shape.width)*shape.height);
                    value[i]=valid?as_bits(textures[slot][size_t(y)*shape.width+x][component]):0;
                }
            }
            else if (instruction.opcode == Opcode::shift_left) value[i] = values[0][i] << (values[1][0]&31);
            else if (instruction.opcode == Opcode::less)
                value[i] = as_float(values[0][i]) < as_float(values[1][i]) ? 0xffffffffu : 0;
            else if(instruction.opcode==Opcode::greater_equal){auto flush=[](float v){return std::fpclassify(v)==FP_SUBNORMAL?0.f:v;};value[i]=flush(as_float(values[0][i]))>=flush(as_float(values[1][i]))?0xffffffffu:0;}
            else if(instruction.opcode==Opcode::equal){auto flush=[](float v){return std::fpclassify(v)==FP_SUBNORMAL?0.f:v;};value[i]=flush(as_float(values[0][i]))==flush(as_float(values[1][i]))?0xffffffffu:0;}
            else if(instruction.opcode==Opcode::not_equal){auto flush=[](float v){return std::fpclassify(v)==FP_SUBNORMAL?0.f:v;};value[i]=flush(as_float(values[0][i]))!=flush(as_float(values[1][i]))?0xffffffffu:0;}
            else if(instruction.opcode==Opcode::integer_greater_equal){int32_t a,b;std::memcpy(&a,&values[0][i],4);std::memcpy(&b,&values[1][i],4);value[i]=a>=b?0xffffffffu:0;}
            else if (instruction.opcode == Opcode::integer_to_float) {
                int32_t source; std::memcpy(&source, &values[0][i], sizeof(source)); value[i] = as_bits(float(source));
            }
            else if (instruction.opcode == Opcode::sample) {
                const auto slot = instruction.sources[1].index;
                const auto shape = shapes[slot];
                const auto sampler = samplers[instruction.sources[2].index];
                const auto filter = sampler.filter;
                check(shape.width && shape.height && textures[slot].size() == size_t(shape.width) * shape.height);
                const auto u = as_float(values[0][0]), v = as_float(values[0][1]);
                check(std::isfinite(u) && std::isfinite(v));
                // Independent scalar oracle: reduce with double fmod, then
                // address individual integer taps, including mirror seams.
                const auto coordinate = [](float x, TextureAddress address) {
                    if(address==TextureAddress::mirror)return std::fmod(double(x),2.0);
                    if(address==TextureAddress::border)return std::clamp(double(x),-2.,3.);
                    check(address==TextureAddress::clamp);return std::clamp(double(x),0.,1.);
                };
                const float scaled_x = float(coordinate(u,sampler.u)) * float(shape.width);
                const float x = scaled_x - (filter == TextureFilter::linear ? .5f : 0.f);
                const float scaled_y = float(coordinate(v,sampler.v)) * float(shape.height);
                const float y = scaled_y - (filter == TextureFilter::linear ? .5f : 0.f);
                const auto ix = int(std::floor(x)), iy = int(std::floor(y));
                const auto component = instruction.sources[1].lanes[i];
                auto tap = [](int n,uint32_t extent,TextureAddress address) {
                    if(address==TextureAddress::clamp)return uint32_t(std::clamp(n,0,int(extent)-1));
                    const int period=int(extent)*2;int wrapped=n%period;if(wrapped<0)wrapped+=period;
                    return uint32_t(wrapped<int(extent)?wrapped:period-1-wrapped);
                };
                auto fetch = [&](int dx, int dy) {
                    if((sampler.u==TextureAddress::border && (ix+dx<0 || ix+dx>=int(shape.width))) ||
                       (sampler.v==TextureAddress::border && (iy+dy<0 || iy+dy>=int(shape.height))))
                        return double(sampler.border[component]);
                    const auto sx = tap(ix+dx,shape.width,sampler.u);
                    const auto sy = tap(iy+dy,shape.height,sampler.v);
                    return double(textures[slot][size_t(sy) * shape.width + sx][component]);
                };
                double sampled = fetch(0, 0);
                if (filter == TextureFilter::linear) {
                    const float tx = x - float(ix), ty = y - float(iy);
                    // Explicit independent FP32 operations; /fp:strict prevents contraction.
                    const auto interpolate = [](float a,float b,float t) { volatile float left=a*(1.f-t),right=b*t; return left+right; };
                    sampled = interpolate(interpolate(float(fetch(0,0)),float(fetch(1,0)),tx),interpolate(float(fetch(0,1)),float(fetch(1,1)),tx),ty);
                } else check(filter == TextureFilter::point);
                value[i] = as_bits(float(sampled));
            } else {
                const auto a = as_float(values[0][i]), b = as_float(values[1][i]);
                float v = 0;
                if (instruction.opcode == Opcode::add) v = a + b;
                else if (instruction.opcode == Opcode::multiply) v = a * b;
                else if (instruction.opcode == Opcode::mad) {
                    // D3D11.3 section 3.1.3 permits fused MAD precision. Replay
                    // can test that exact alternative without an arbitrary ULP
                    // tolerance or relaxing integer/MOV bit comparisons.
                    if(fused_arithmetic)v=std::fma(a,b,as_float(values[2][i]));
                    else {volatile float product=a*b;v=product+as_float(values[2][i]);}
                }
                else if (instruction.opcode == Opcode::divide) {
                    // D3D11.3 section 3.1.3 also permits x*(1/y), with
                    // independently rounded reciprocal and multiplication.
                    // This exact alternative is opt-in for replay validation;
                    // it does not introduce a tolerance for MOV/integer bits.
                    if(reciprocal_division){volatile float reciprocal=1.f/b;v=a*reciprocal;}
                    else v=a/b;
                }
                else if(instruction.opcode==Opcode::floor)v=std::fpclassify(a)==FP_SUBNORMAL?std::copysign(0.f,a):std::floor(a);
                else if(instruction.opcode==Opcode::fraction)v=std::fpclassify(a)==FP_SUBNORMAL?0.f:a-std::floor(a);
                else if (instruction.opcode == Opcode::reciprocal_sqrt) v = 1.0f / std::sqrt(a);
                else if (instruction.opcode == Opcode::minimum) v = std::fmin(a, b);
                else if (instruction.opcode == Opcode::maximum) v = std::fmax(a, b);
                else if (instruction.opcode == Opcode::dot2 || instruction.opcode == Opcode::dot3 || instruction.opcode == Opcode::dot4) {
                    const unsigned count=instruction.opcode==Opcode::dot2?2u:instruction.opcode==Opcode::dot3?3u:4u;
                    const unsigned first=fused_arithmetic==2?count-1:0;
                    v=as_float(values[0][first])*as_float(values[1][first]);
                    for(unsigned n=1;n<count;++n){const unsigned c=fused_arithmetic==2?count-1-n:n;
                        if(fused_arithmetic)v=std::fma(as_float(values[0][c]),as_float(values[1][c]),v);
                        else {volatile float product=as_float(values[0][c])*as_float(values[1][c]);v+=product;}
                    }
                }
                else check(false);
                value[i] = as_bits(v);
            }
            if (instruction.saturate) {
                const auto v = as_float(value[i]);
                value[i] = as_bits(std::isnan(v) || std::fpclassify(v)==FP_SUBNORMAL || v < 0 ? 0.0f : (v > 1 ? 1.0f : v));
            }
        }
        auto &destination = (instruction.destination.kind == Register::temporary ? temporary : output)[instruction.destination.index];
        for (unsigned i = 0; i < 4; ++i)
            if (instruction.destination.mask & (1u << i)) destination[i] = value[i];
    }
    return output;
}
// Independent quad oracle. Evaluate each derivative's source through the scalar
// interpreter, including earlier derivative values, before subtracting neighbors.
inline std::array<Registers,4> reference_pixel_quad(const Program &program,const std::array<Registers,4> &inputs,
    const Constants &constants,const TextureShapes &shapes={},const TextureSamplers &samplers={},const TexturePixels &textures={}){
    check(program.stage==Stage::pixel);
    std::array<std::vector<Bits>,4> derivatives;
    for(auto &v:derivatives)v.resize(program.instructions.size());
    for(size_t i=0;i<program.instructions.size();++i){const auto &instruction=program.instructions[i];
        if(instruction.opcode!=Opcode::derivative_x && instruction.opcode!=Opcode::derivative_y)continue;
        Program prefix=program;prefix.instructions.resize(i);
        Instruction read{};read.opcode=Opcode::move;read.destination.kind=Register::output;read.destination.index=0;read.destination.mask=15;
        read.source_count=1;read.sources[0]=instruction.sources[0];prefix.instructions.push_back(read);
        std::array<Bits,4> values;
        for(unsigned lane=0;lane<4;++lane)values[lane]=reference_vertex(prefix,inputs[lane],constants,shapes,samplers,textures,0,&derivatives[lane])[0];
        const unsigned stride=instruction.opcode==Opcode::derivative_x?1u:2u;
        for(unsigned lane=0;lane<4;++lane)for(unsigned c=0;c<4;++c)
            derivatives[lane][i][c]=as_bits(as_float(values[lane|stride][c])-as_float(values[lane&~stride][c]));
    }
    std::array<Registers,4> result;
    for(unsigned lane=0;lane<4;++lane)result[lane]=reference_vertex(program,inputs[lane],constants,shapes,samplers,textures,0,&derivatives[lane]);
    return result;
}
} // namespace nr::shader
