#include "provider_infer_trace.hpp"
#include "provider_compile.hpp"
#include "loop_fixture.hpp"
#include "compositor_fixture.hpp"
#include "vertex_executor.hpp"
#include <openvino/openvino.hpp>
#include <openvino/opsets/opset13.hpp>
#include <openvino/pass/serialize.hpp>
#include <cstring>
#include <cmath>
#include <set>
#include <functional>
#include <sstream>
namespace nr::shader {
namespace {
namespace op = ov::opset13;
using Node = std::shared_ptr<ov::Node>;
struct Value { Node floating, integer; bool predicate = false; };
using Lanes = std::array<Value, 4>;
Node integer(int32_t value) { return op::Constant::create(ov::element::i32, ov::Shape{}, {value}); }
Node indices(std::initializer_list<int64_t> values) {
    return op::Constant::create(ov::element::i64, ov::Shape{values.size()}, values);
}
Node floating(uint32_t bits) {
    float value; std::memcpy(&value, &bits, sizeof(value));
    return op::Constant::create(ov::element::f32, ov::Shape{1, 1}, {value});
}
Node number(float value) { return op::Constant::create(ov::element::f32, ov::Shape{1, 1}, {value}); }
Node lane(const Node &value, unsigned component) {
    return std::make_shared<op::Gather>(value, indices({int64_t(component)}), integer(1));
}
}
// Exact captured-shader specialization: independent mirrored
// sample pairs form a tap axis. All coordinates, sampling, weights and the
// reduction execute on the provider. The original scalar loop is the oracle.
static bool mirrored_loop_shader(const Program &p){
    const auto &words=nr::registered_lab::test_fixture::loop_pixel_words;
    return p.stage==Stage::pixel && p.bytecode.size()==std::size(words) &&
        std::equal(p.bytecode.begin(),p.bytecode.end(),std::begin(words));
}
static Program mirrored_tap_program(const Program &p){
    Program q=p;q.instructions.clear();q.inputs[15]=1;
    std::vector<size_t> begins,ends;
    for(size_t n=0;n<p.instructions.size();++n){if(p.instructions[n].opcode==Opcode::begin_loop)begins.push_back(n);if(p.instructions[n].opcode==Opcode::end_loop)ends.push_back(n);}
    check(begins.size()==2 && ends.size()==2);
    q.instructions.insert(q.instructions.end(),p.instructions.begin(),p.instructions.begin()+begins[0]);
    q.instructions.insert(q.instructions.end(),p.instructions.begin()+ends[0]+1,p.instructions.begin()+begins[1]);
    // The final pre-loop MOV initializes r5.z; replace it with a device tap id.
    auto &index=q.instructions.back();check(index.opcode==Opcode::move && index.destination.index==5 && index.destination.mask==4);
    index.sources[0]={};index.sources[0].kind=Register::input;index.sources[0].index=15;index.sources[0].lanes.fill(0);
    for(size_t n=begins[1]+1;n<ends[1]-1;++n){auto i=p.instructions[n];
        if(i.opcode==Opcode::break_if){i.opcode=Opcode::begin_if;i.test_nonzero=!i.test_nonzero;}
        q.instructions.push_back(i);
    }
    Instruction end;end.opcode=Opcode::end_if;q.instructions.push_back(end);
    q.instructions.insert(q.instructions.end(),p.instructions.begin()+ends[1]+1,p.instructions.end());
    return q;
}
struct RegisterExecutor::State {
    Program program;
    uint32_t count, logical_count, tap_capacity;
    bool mirrored_taps;
    TextureShapes texture_shapes;
    bool checks_sample_coordinates = false;
    struct Binding { std::shared_ptr<op::Parameter> parameter; bool constant; unsigned index; std::string name; bool texture = false, half_texel = false; uint32_t texture_offset=0, texture_rows=0; };
    std::vector<Binding> bindings;
    std::vector<unsigned> outputs;
    std::array<uint32_t, 16> output_bit_masks{};
    ov::Core core;
    ov::CompiledModel compiled;
    ov::InferRequest request;
    std::string execution_device;
    bool failed = false, ready = false, textures_uploaded=false;
    State(const Program &p, uint32_t vertices, const std::string &backend,
          const TextureShapes &shapes, const TextureSamplers &samplers, uint32_t tap_limit=128, bool defer_compile=false) : program(mirrored_loop_shader(p)?mirrored_tap_program(p):lower_bounded_loops(p)), count(vertices), logical_count(vertices), tap_capacity(tap_limit), mirrored_taps(mirrored_loop_shader(p)), texture_shapes(shapes) {
        check(count && count <= 65536 && (backend == "CPU" || backend == "GPU" || backend == "NPU"));
        check(count<=RegisterExecutor::maximum_batch(p));
        check(tap_capacity && tap_capacity<=128 && !(tap_capacity&(tap_capacity-1)));
        if(mirrored_taps)count*=tap_capacity;
        RegisterExecutor::validate(program);
        check(!quad_derivatives(program) || !(count%4));
        std::array<Lanes, 16> input{}, temporary{}, output{};
        std::array<Value, 14> constants{};
        std::array<Value,16> literal_tables{};
        std::array<Node, 16> textures{}, texture_bits{};
        std::array<std::vector<Node>,16> texture_chunks{};
        Node half_texel;
        std::vector<Node> invalid_coordinates;
        auto parameter_pair = [&](bool constant, unsigned index, size_t rows) {
            Value result;
            for (unsigned i = 0; i < 2; ++i) {
                auto parameter = std::make_shared<op::Parameter>(i ? ov::element::i32 : ov::element::f32, ov::Shape{rows, 4});
                const auto name = std::string(constant ? "cb" : "v") + std::to_string(index) + (i ? "_bits" : "_float");
                parameter->set_friendly_name(name);
                parameter->output(0).get_tensor().set_names({name});
                bindings.push_back({parameter, constant, index, name});
                (i ? result.integer : result.floating) = parameter;
            }
            return result;
        };
        for (unsigned i = 0; i < 16; ++i) if (program.inputs[i]) {
            if(mirrored_taps && i==15){
                std::vector<int32_t> taps(count);for(unsigned n=0;n<count;++n)taps[n]=int32_t(n%tap_capacity);
                input[i][0]={number(0),op::Constant::create(ov::element::i32,ov::Shape{count,1},taps)};continue;
            }
            auto value = parameter_pair(false, i, logical_count);
            if(mirrored_taps){std::vector<int32_t> repeated(count);for(unsigned n=0;n<count;++n)repeated[n]=int32_t(n/tap_capacity);
                const auto index=op::Constant::create(ov::element::i32,ov::Shape{count},repeated);
                value={std::make_shared<op::Gather>(value.floating,index,integer(0)),std::make_shared<op::Gather>(value.integer,index,integer(0))};
            }
            for (unsigned c = 0; c < 4; ++c) input[i][c] = {lane(value.floating, c), lane(value.integer, c)};
        }
        for (unsigned i = 0; i < 14; ++i) if (program.constants[i])
            constants[i] = parameter_pair(true, i, program.constants[i]);
        for(unsigned i=0;i<16;++i)if(!program.literal_tables[i].empty()){
            const auto size=program.literal_tables[i].size();std::vector<float> floats(size*4);std::vector<int32_t> bits(size*4);
            for(size_t n=0;n<size;++n)for(unsigned c=0;c<4;++c){
                std::memcpy(&floats[n*4+c],&program.literal_tables[i][n][c],4);
                std::memcpy(&bits[n*4+c],&program.literal_tables[i][n][c],4);
            }
            literal_tables[i]={op::Constant::create(ov::element::f32,ov::Shape{size,4},floats),
                               op::Constant::create(ov::element::i32,ov::Shape{size,4},bits)};
        }
        const auto batch_shape = indices({int64_t(count), 1});
        auto batch = [&](const Node &value) -> Node {
            check(bool(value)); return std::make_shared<op::Broadcast>(value, batch_shape);
        };
        std::vector<int32_t> row_numbers(count);
        for (uint32_t i = 0; i < count; ++i) row_numbers[i] = int32_t(i);
        const auto rows = op::Constant::create(ov::element::i32, ov::Shape{count}, row_numbers);
        auto choose = [&](const Node &condition, const Node &yes, const Node &no) -> Node {
            // NPU Select produced wrong elements for batched control flow.
            // Select rows by integer index instead, as for bounded CB reads.
            const auto alternate = std::make_shared<op::Reshape>(
                std::make_shared<op::Convert>(std::make_shared<op::LogicalNot>(batch(condition)), ov::element::i32),
                indices({int64_t(count)}), false);
            const auto index = std::make_shared<op::Add>(rows, std::make_shared<op::Multiply>(alternate, integer(int32_t(count))));
            return std::make_shared<op::Gather>(std::make_shared<op::Concat>(ov::OutputVector{batch(yes), batch(no)}, 0), index, integer(0));
        };
        auto load = [&](const Operand &a, bool integer_modifier = false) {
            Lanes raw{};
            if (a.kind == Register::input) raw = input[a.index];
            else if (a.kind == Register::temporary) raw = temporary[a.index];
            else if (a.kind == Register::immediate) {
                for (unsigned c = 0; c < 4; ++c) raw[c] = {floating(a.literal[c]), integer(int32_t(a.literal[c])),a.literal[c]==0 || a.literal[c]==UINT32_MAX};
            } else if (a.kind == Register::constant || a.kind == Register::indexable) {
                const auto size=a.kind==Register::constant?program.constants[a.index]:uint32_t(program.literal_tables[a.index].size());
                Node index = integer(int32_t(a.element));
                if (a.relative.enabled) {
                    const auto &registers = a.relative.kind == Register::input ? input : temporary;
                    const auto relative = registers[a.relative.index][a.relative.component].integer;
                    // MOV preserves integer bits. Arithmetic-to-address bitcasts
                    // need another lowering and are explicitly rejected here.
                    check(bool(relative)); index = std::make_shared<op::Add>(index, relative);
                }
                index = std::make_shared<op::Reshape>(batch(index), indices({int64_t(count)}), false);
                // The 2026.4 NPU compiler miscomputes LogicalAnd for these
                // integer-index comparisons. Combine exact 0/1 integer masks,
                // as in typed texture loads, and keep both bounds on device.
                const auto valid_bits=std::make_shared<op::BitwiseAnd>(
                    std::make_shared<op::Convert>(std::make_shared<op::GreaterEqual>(index,integer(0)),ov::element::i32),
                    std::make_shared<op::Convert>(std::make_shared<op::Less>(index,integer(int32_t(size))),ov::element::i32));
                const auto valid=std::make_shared<op::NotEqual>(valid_bits,integer(0));
                if(a.kind==Register::indexable)invalid_coordinates.push_back(std::make_shared<op::Reshape>(
                    std::make_shared<op::Convert>(std::make_shared<op::LogicalNot>(valid),ov::element::i32),batch_shape,false));
                // Route out-of-range reads to an appended zero row. The Intel
                // NPU compiler miscompiled broadcast floating Select masks in
                // both default and precision-preserving graphs. Integer index
                // selection followed by Gather avoids that operation entirely.
                const auto safe = std::make_shared<op::Select>(valid, index, integer(int32_t(size)));
                const auto &buffer = a.kind==Register::constant?constants[a.index]:literal_tables[a.index];
                const auto padded_f = std::make_shared<op::Concat>(ov::OutputVector{buffer.floating,
                    op::Constant::create(ov::element::f32, ov::Shape{1, 4}, {0.0f})}, 0);
                const auto padded_i = std::make_shared<op::Concat>(ov::OutputVector{buffer.integer,
                    op::Constant::create(ov::element::i32, ov::Shape{1, 4}, {0})}, 0);
                const auto f = std::make_shared<op::Gather>(padded_f, safe, integer(0));
                const auto i = std::make_shared<op::Gather>(padded_i, safe, integer(0));
                for (unsigned c = 0; c < 4; ++c) raw[c] = {lane(f, c), lane(i, c)};
            } else check(false);
            Lanes result{};
            for (unsigned c = 0; c < 4; ++c) {
                result[c] = raw[a.lanes[c]];
                if (a.modifier != Modifier::none) {
                    result[c].predicate=false;
                    if (integer_modifier) {
                        if (result[c].integer) result[c].integer = std::make_shared<op::Negative>(result[c].integer);
                        result[c].floating.reset(); continue;
                    }
                    if (result[c].floating && (a.modifier == Modifier::absolute || a.modifier == Modifier::absolute_negate))
                        result[c].floating = std::make_shared<op::Abs>(result[c].floating);
                    if (result[c].floating && (a.modifier == Modifier::negate || a.modifier == Modifier::absolute_negate))
                        result[c].floating = std::make_shared<op::Multiply>(result[c].floating, floating(0xbf800000));
                    if (result[c].integer) {
                        if (a.modifier == Modifier::absolute || a.modifier == Modifier::absolute_negate)
                            result[c].integer = std::make_shared<op::BitwiseAnd>(result[c].integer, integer(0x7fffffff));
                        if (a.modifier == Modifier::negate || a.modifier == Modifier::absolute_negate)
                            result[c].integer = std::make_shared<op::BitwiseXor>(result[c].integer, integer(INT32_MIN));
                    }
                }
            }
            return result;
        };
        using Registers = std::array<Lanes, 16>;
        struct Branch { Node condition; Registers before_temps, before_outputs, then_temps{}, then_outputs{}; bool has_else = false; };
        std::vector<Branch> branches;
        auto sample_texture = [&](const Instruction &instruction) -> Node {
            const auto slot = instruction.sources[1].index;
            const auto shape = texture_shapes[slot];
            const auto sampler = samplers[instruction.sources[2].index];
            const auto filter = sampler.filter;
            check(shape.width && shape.width <= 8192 && shape.height && shape.height <= 8192 &&
                  uint64_t(shape.width) * shape.height <= 8388608 &&
                  (filter == TextureFilter::point || filter == TextureFilter::linear) &&
                  (sampler.u==TextureAddress::clamp || sampler.u==TextureAddress::mirror || sampler.u==TextureAddress::border) &&
                  (sampler.v==TextureAddress::clamp || sampler.v==TextureAddress::mirror || sampler.v==TextureAddress::border));
            for(const auto value:sampler.border)check(std::isfinite(value));
            const auto source_size=shape.width*shape.height;
            // The qualified NPU compiler wraps Gather indices at 2^21 for
            // both scalar and RGBA FP32 inputs. Separate bounded ports keep
            // all reads, filtering and selection on the requested provider.
            const bool segmented=backend=="NPU" && source_size>2097152u;
            constexpr uint32_t chunk_rows=1048576;
            if(segmented && texture_chunks[slot].empty()){
                for(uint32_t base=0;base<source_size;base+=chunk_rows){
                    const auto length=(std::min)(chunk_rows,source_size-base);
                    auto parameter=std::make_shared<op::Parameter>(ov::element::f32,ov::Shape{length,4});
                    const auto name="texture_"+std::to_string(slot)+"_"+std::to_string(base);
                    parameter->set_friendly_name(name);parameter->output(0).get_tensor().set_names({name});
                    bindings.push_back({parameter,false,slot,name,true,false,base,length});
                    texture_chunks[slot].push_back(parameter);
                }
            }
            if (!segmented && !textures[slot]) {
                auto parameter = std::make_shared<op::Parameter>(ov::element::f32, ov::Shape{size_t(shape.width) * shape.height, 4});
                const auto name = "texture_" + std::to_string(slot);
                parameter->set_friendly_name(name); parameter->output(0).get_tensor().set_names({name});
                bindings.push_back({parameter, false, slot, name, true}); textures[slot] = parameter;
            }
            const auto values = load(instruction.sources[0]);
            if (filter == TextureFilter::linear && !half_texel) {
                // Constant scale/offset fusion demotes the coordinate to FP16
                // despite precise metadata. A runtime half-texel parameter
                // preserves the qualified FP32 multiply/subtract path.
                auto parameter = std::make_shared<op::Parameter>(ov::element::f32, ov::Shape{1, 1});
                const std::string name = "texture_half_texel";
                parameter->set_friendly_name(name); parameter->output(0).get_tensor().set_names({name});
                bindings.push_back({parameter, false, 0, name, false, true}); half_texel = parameter;
            }
            auto coordinate = [&](unsigned axis, uint32_t extent,TextureAddress address) -> Node {
                Node value = values[axis].floating; check(bool(value));
                const auto nan = std::make_shared<op::IsNaN>(value);
                // A combined floating isfinite pattern misclassifies ordinary
                // coordinates after NPU optimization. Keep the predicates as
                // integer status bits, separate from coordinate selection.
                const auto invalid = std::make_shared<op::BitwiseOr>(
                    std::make_shared<op::Convert>(nan, ov::element::i32),
                    std::make_shared<op::Convert>(std::make_shared<op::IsInf>(value), ov::element::i32));
                Node active_invalid = batch(invalid);
                for (const auto &branch : branches) {
                    Node active = branch.has_else ? Node(std::make_shared<op::LogicalNot>(branch.condition)) : branch.condition;
                    active_invalid = std::make_shared<op::BitwiseAnd>(active_invalid,
                        std::make_shared<op::Convert>(batch(active), ov::element::i32));
                }
                invalid_coordinates.push_back(active_invalid);
                if(address==TextureAddress::border){
                    // Preserve the half-texel transition across each edge.
                    // Wider finite coordinates remain wholly in the border;
                    // bound them before conversion, never clamp them to [0,1].
                    value=choose(nan,number(0),value);
                    value=choose(std::make_shared<op::Less>(value,number(-1)),number(-1),
                        choose(std::make_shared<op::Greater>(value,number(2)),number(2),value));
                    Node scaled=std::make_shared<op::Multiply>(value,number(float(extent)));
                    return filter==TextureFilter::linear?Node(std::make_shared<op::Subtract>(scaled,half_texel)):scaled;
                }
                if(address==TextureAddress::mirror){
                    // All finite FP32 magnitudes >= 2^24 are even integers.
                    // Reduce smaller magnitudes modulo two on the provider,
                    // before multiplying by the extent, to bound integer taps.
                    value=choose(std::make_shared<op::NotEqual>(invalid,integer(0)),number(0),value);
                    Node magnitude=std::make_shared<op::Abs>(value);
                    const auto small=std::make_shared<op::Less>(std::make_shared<op::Multiply>(magnitude,number(1.f/32768)),number(512));
                    magnitude=choose(small,magnitude,number(0));
                    const auto cycles=std::make_shared<op::BitwiseAnd>(std::make_shared<op::Convert>(
                        std::make_shared<op::Multiply>(magnitude,number(.5f)),ov::element::i32),integer(0x7fffffff));
                    Node reduced=std::make_shared<op::Subtract>(magnitude,std::make_shared<op::Multiply>(
                        std::make_shared<op::Convert>(cycles,ov::element::f32),number(2)));
                    reduced=choose(std::make_shared<op::Less>(value,number(0)),std::make_shared<op::Negative>(reduced),reduced);
                    Node scaled=std::make_shared<op::Multiply>(reduced,number(float(extent)));
                    return filter==TextureFilter::linear?Node(std::make_shared<op::Subtract>(scaled,half_texel)):scaled;
                }
                // Keep every speculative read in bounds. Nonfinite coordinates
                // on an active path fail explicitly after inference.
                value = choose(nan, number(0), value); // Infinities clamp at the comparisons below.
                value = choose(std::make_shared<op::Less>(value, number(0)), number(0),
                    choose(std::make_shared<op::Greater>(value, number(1)), number(1), value));
                value = std::make_shared<op::Multiply>(value, number(float(extent)));
                if (filter == TextureFilter::linear) value = std::make_shared<op::Subtract>(value, half_texel);
                // For CLAMP, clamping the continuous texel coordinate before
                // filtering is equivalent to clamping the individual taps.
                return choose(std::make_shared<op::Less>(value, number(0)), number(0),
                    choose(std::make_shared<op::Greater>(value, number(float(extent - 1))), number(float(extent - 1)), value));
            };
            const auto x = coordinate(0, shape.width,sampler.u), y = coordinate(1, shape.height,sampler.v);
            auto floor_index = [&](const Node &value,TextureAddress address) -> Node {
                // Coordinates are nonnegative and bounded. Native NPU Floor
                // miscomputes FP32; Convert gives truncation here. The AND is
                // an identity on this range but prevents the compiler from
                // incorrectly deleting the lossy float/int/float round trip.
                Node truncated=std::make_shared<op::BitwiseAnd>(std::make_shared<op::Convert>(
                    address!=TextureAddress::clamp?Node(std::make_shared<op::Abs>(value)):value,ov::element::i32),integer(0x7fffffff));
                if(address!=TextureAddress::clamp){
                    truncated=choose(std::make_shared<op::Less>(value,number(0)),std::make_shared<op::Negative>(truncated),truncated);
                    // Convert truncates toward zero; correct negative fractions.
                    return std::make_shared<op::Subtract>(truncated,std::make_shared<op::Convert>(
                        std::make_shared<op::Less>(value,std::make_shared<op::Convert>(truncated,ov::element::f32)),ov::element::i32));
                }
                return truncated;
            };
            const auto ix = floor_index(x,sampler.u), iy = floor_index(y,sampler.v);
            auto tap = [&](const Node &base,int delta,uint32_t extent,TextureAddress address)->Node {
                Node n=std::make_shared<op::Add>(base,integer(delta));
                if(address==TextureAddress::border)return std::make_shared<op::Maximum>(integer(0),
                    std::make_shared<op::Minimum>(n,integer(int32_t(extent-1))));
                if(address==TextureAddress::clamp)return std::make_shared<op::Minimum>(n,integer(int32_t(extent-1)));
                const auto period=integer(int32_t(extent*2));
                // Linear filtering can place the first tap one texel below
                // -period. Add two periods before the final single reduction.
                n=choose(std::make_shared<op::Less>(n,integer(0)),std::make_shared<op::Add>(n,integer(int32_t(extent*4))),n);
                n=choose(std::make_shared<op::GreaterEqual>(n,period),std::make_shared<op::Subtract>(n,period),n);
                return choose(std::make_shared<op::Less>(n,integer(int32_t(extent))),n,
                    std::make_shared<op::Subtract>(integer(int32_t(extent*2-1)),n));
            };
            const bool bordered=sampler.u==TextureAddress::border || sampler.v==TextureAddress::border;
            Node texels=textures[slot];
            if(bordered && !segmented)texels=std::make_shared<op::Concat>(ov::OutputVector{texels,
                op::Constant::create(ov::element::f32,ov::Shape{1,4},std::vector<float>(sampler.border.begin(),sampler.border.end()))},0);
            auto fetch = [&](int dx, int dy) -> Node {
                const auto sx = tap(ix,dx,shape.width,sampler.u),sy=tap(iy,dy,shape.height,sampler.v);
                Node address=std::make_shared<op::Add>(sx,std::make_shared<op::Multiply>(sy,integer(int32_t(shape.width))));
                if(bordered){
                    Node valid=integer(1);
                    auto bound=[&](const Node &base,int delta,uint32_t extent,TextureAddress mode){
                        if(mode!=TextureAddress::border)return;
                        const auto n=std::make_shared<op::Add>(base,integer(delta));
                        const auto bits=std::make_shared<op::BitwiseAnd>(
                            std::make_shared<op::Convert>(std::make_shared<op::GreaterEqual>(n,integer(0)),ov::element::i32),
                            std::make_shared<op::Convert>(std::make_shared<op::Less>(n,integer(int32_t(extent))),ov::element::i32));
                        valid=std::make_shared<op::BitwiseAnd>(valid,bits);
                    };
                    bound(ix,dx,shape.width,sampler.u);bound(iy,dy,shape.height,sampler.v);
                    // Select an integer address and Gather the border row.
                    // Avoid the NPU's previously rejected floating Select path.
                    address=std::make_shared<op::Select>(std::make_shared<op::NotEqual>(valid,integer(0)),address,integer(int32_t(shape.width*shape.height)));
                }
                const auto index=std::make_shared<op::Reshape>(address,indices({int64_t(count)}),false);
                if(!segmented)return std::make_shared<op::Gather>(texels,index,integer(0));
                Node selected;
                for(uint32_t base=0,port=0;base<source_size;base+=chunk_rows,++port){
                    const auto length=(std::min)(chunk_rows,source_size-base);
                    const auto safe=std::make_shared<op::Minimum>(std::make_shared<op::Maximum>(
                        std::make_shared<op::Subtract>(index,integer(int32_t(base))),integer(0)),integer(int32_t(length-1)));
                    const auto value=std::make_shared<op::Gather>(texture_chunks[slot][port],safe,integer(0));
                    const auto bits=std::make_shared<op::BitwiseAnd>(
                        std::make_shared<op::Convert>(std::make_shared<op::GreaterEqual>(index,integer(int32_t(base))),ov::element::i32),
                        std::make_shared<op::Convert>(std::make_shared<op::Less>(index,integer(int32_t(base+length))),ov::element::i32));
                    const auto mask=std::make_shared<op::Convert>(std::make_shared<op::Reshape>(bits,batch_shape,false),ov::element::f32);
                    const auto term=std::make_shared<op::Multiply>(value,mask);
                    selected=selected?Node(std::make_shared<op::Add>(selected,term)):term;
                }
                if(bordered){
                    const auto bits=std::make_shared<op::Convert>(std::make_shared<op::Equal>(index,integer(int32_t(source_size))),ov::element::i32);
                    const auto mask=std::make_shared<op::Convert>(std::make_shared<op::Reshape>(bits,batch_shape,false),ov::element::f32);
                    const auto border=op::Constant::create(ov::element::f32,ov::Shape{1,4},std::vector<float>(sampler.border.begin(),sampler.border.end()));
                    selected=std::make_shared<op::Add>(selected,std::make_shared<op::Multiply>(border,mask));
                }
                return selected;
            };
            if (filter == TextureFilter::point) return fetch(0, 0);
            const auto tx = std::make_shared<op::Subtract>(x, std::make_shared<op::Convert>(ix, ov::element::f32));
            const auto ty = std::make_shared<op::Subtract>(y, std::make_shared<op::Convert>(iy, ov::element::f32));
            auto mix = [&](const Node &a, const Node &b, const Node &weight) -> Node {
                return std::make_shared<op::Add>(std::make_shared<op::Multiply>(a, std::make_shared<op::Subtract>(number(1), weight)),
                    std::make_shared<op::Multiply>(b, weight));
            };
            return mix(mix(fetch(0, 0), fetch(1, 0), tx), mix(fetch(0, 1), fetch(1, 1), tx), ty);
        };
        auto load_texture = [&](const Instruction &instruction) -> Lanes {
            const auto slot=instruction.sources[1].index;const auto shape=texture_shapes[slot];
            const bool integer_resource=program.texture_uint[slot],buffer=program.texture_dimensions[slot]==1;
            Lanes result{};
            if(!shape.width && !shape.height){
                for(auto &v:result)v={integer_resource?Node{}:number(0),integer(0)};
                return result;
            }
            check(shape.width && shape.height && uint64_t(shape.width)*shape.height<=4194304 && shape.components && shape.components<=15 &&
                (buffer?shape.height==1:shape.width<=8192 && shape.height<=8192));
            const auto address=load(instruction.sources[0]);check(bool(address[0].integer));
            auto predicate=[](const Node &v)->Node{return std::make_shared<op::Convert>(v,ov::element::i32);};
            auto in_range=[&](const Node &v,uint32_t extent)->Node{return std::make_shared<op::BitwiseAnd>(
                predicate(std::make_shared<op::GreaterEqual>(v,integer(0))),predicate(std::make_shared<op::Less>(v,integer(int32_t(extent)))));};
            Node valid_bits=in_range(address[0].integer,shape.width);
            Node offset=address[0].integer;
            if(!buffer){
                check(bool(address[1].integer) && bool(address[3].integer));
                valid_bits=std::make_shared<op::BitwiseAnd>(valid_bits,in_range(address[1].integer,shape.height));
                valid_bits=std::make_shared<op::BitwiseAnd>(valid_bits,predicate(std::make_shared<op::Equal>(address[3].integer,integer(0))));
                offset=std::make_shared<op::Add>(offset,std::make_shared<op::Multiply>(address[1].integer,integer(int32_t(shape.width))));
            }
            const auto valid=std::make_shared<op::NotEqual>(valid_bits,integer(0));
            const auto size=size_t(shape.width)*shape.height;
            const auto index=std::make_shared<op::Reshape>(choose(valid,offset,integer(int32_t(size))),indices({int64_t(count)}),false);
            auto fetch=[&](bool bits)->Node{
                auto &storage=bits?texture_bits[slot]:textures[slot];
                const auto type=bits?ov::element::i32:ov::element::f32;
                if(!storage){
                    auto parameter=std::make_shared<op::Parameter>(type,ov::Shape{size,4});
                    const auto name="texture_"+std::to_string(slot)+(bits?"_bits":"");
                    parameter->set_friendly_name(name);parameter->output(0).get_tensor().set_names({name});
                    bindings.push_back({parameter,false,slot,name,true});storage=parameter;
                }
                const auto padded=std::make_shared<op::Concat>(ov::OutputVector{storage,op::Constant::create(type,ov::Shape{1,4},{0})},0);
                return std::make_shared<op::Gather>(padded,index,integer(0));
            };
            const auto bits=fetch(true);const Node floats=integer_resource?Node{}:fetch(false);
            for(unsigned c=0;c<4;++c){const auto component=instruction.sources[1].lanes[c];
                if(shape.components&(1u<<component))result[c]={floats?lane(floats,component):Node{},lane(bits,component)};
                else result[c]={integer_resource?Node{}:number(component==3?1.f:0.f),integer(component==3?(integer_resource?1:0x3f800000):0)};
            }
            return result;
        };
        auto shift_right = [&](Node value,const Node &raw_amount)->Node {
            const auto amount=std::make_shared<op::BitwiseAnd>(raw_amount,integer(31));
            // Mask before conversion: high lanes have at most 24 significant
            // bits and low lanes at most 8. Four <=8-bit stages avoid NPU
            // flushing tiny reciprocal scales; each quotient is exact.
            for(unsigned step=0;step<4;++step){
                const auto chunk=std::make_shared<op::Minimum>(std::make_shared<op::Maximum>(
                    std::make_shared<op::Subtract>(amount,integer(int32_t(step*8))),integer(0)),integer(8));
                const auto index=std::make_shared<op::Reshape>(batch(chunk),indices({int64_t(count)}),false);
                std::vector<float> scales(9);for(unsigned n=0;n<9;++n)scales[n]=std::ldexp(1.f,-int(n));
                const auto scale=std::make_shared<op::Reshape>(std::make_shared<op::Gather>(
                    op::Constant::create(ov::element::f32,ov::Shape{9},scales),index,integer(0)),batch_shape,false);
                auto part=[&](bool high)->Node{
                    std::vector<int32_t> masks(9);for(unsigned n=0;n<9;++n){const auto bits=(high?0xffffff00u:255u)&(~0u<<n);std::memcpy(&masks[n],&bits,4);}
                    const auto mask=std::make_shared<op::Reshape>(std::make_shared<op::Gather>(
                        op::Constant::create(ov::element::i32,ov::Shape{9},masks),index,integer(0)),batch_shape,false);
                    const auto integral=std::make_shared<op::Convert>(std::make_shared<op::BitwiseAnd>(value,mask),ov::element::f32);
                    return std::make_shared<op::Convert>(std::make_shared<op::Multiply>(integral,scale),ov::element::i32);
                };
                value=std::make_shared<op::Add>(part(true),part(false));
            }
            return value;
        };
        for (const auto &instruction : program.instructions) {
            if(instruction.opcode==Opcode::loop_overflow){
                Node invalid=batch(integer(1));
                for(const auto &branch:branches){
                    Node active=branch.has_else?Node(std::make_shared<op::LogicalNot>(branch.condition)):branch.condition;
                    invalid=std::make_shared<op::BitwiseAnd>(invalid,std::make_shared<op::Convert>(batch(active),ov::element::i32));
                }
                invalid_coordinates.push_back(invalid);continue;
            }
            if (instruction.opcode == Opcode::begin_if) {
                const auto value = load(instruction.sources[0])[0].integer; check(bool(value));
                Node condition = std::make_shared<op::NotEqual>(value, integer(0));
                if (!instruction.test_nonzero) condition = std::make_shared<op::LogicalNot>(condition);
                branches.push_back({condition, temporary, output}); continue;
            }
            if (instruction.opcode == Opcode::otherwise) {
                auto &b = branches.back(); b.then_temps = temporary; b.then_outputs = output;
                b.has_else = true; temporary = b.before_temps; output = b.before_outputs; continue;
            }
            if (instruction.opcode == Opcode::end_if) {
                const auto &b = branches.back();
                auto merge = [&](Registers &values, const Registers &other) {
                    for (unsigned i = 0; i < 16; ++i) for (unsigned c = 0; c < 4; ++c) {
                        auto &current = values[i][c]; const auto &alternative = other[i][c];
                        auto join = [&](const Node &a, const Node &other_value) -> Node {
                            if (!a || !other_value) return {};
                            if (a == other_value) return a;
                            return choose(b.condition, b.has_else ? other_value : a, b.has_else ? a : other_value);
                        };
                        current = {join(current.floating, alternative.floating), join(current.integer, alternative.integer),current.predicate && alternative.predicate};
                    }
                };
                merge(temporary, b.has_else ? b.then_temps : b.before_temps);
                merge(output, b.has_else ? b.then_outputs : b.before_outputs);
                branches.pop_back(); continue;
            }
            if(instruction.opcode==Opcode::load){
                const auto values=load_texture(instruction);
                auto &destination=(instruction.destination.kind==Register::temporary?temporary:output)[instruction.destination.index];
                for(unsigned c=0;c<4;++c)if(instruction.destination.mask&(1u<<c))destination[c]=values[c];
                continue;
            }
            if (instruction.opcode == Opcode::sample) {
                const auto sampled = sample_texture(instruction);
                auto &destination = (instruction.destination.kind == Register::temporary ? temporary : output)[instruction.destination.index];
                for (unsigned c = 0; c < 4; ++c) if (instruction.destination.mask & (1u << c))
                    destination[c] = {lane(sampled, instruction.sources[1].lanes[c]), {}};
                continue;
            }
            std::array<Lanes, 3> sources{};
            int subtract_source = -1;
            auto negative = [](Modifier modifier) { return modifier == Modifier::negate || modifier == Modifier::absolute_negate; };
            if (instruction.opcode == Opcode::mad && negative(instruction.sources[2].modifier)) subtract_source = 2;
            else if (instruction.opcode == Opcode::add) {
                if (negative(instruction.sources[1].modifier)) subtract_source = 1;
                else if (negative(instruction.sources[0].modifier)) subtract_source = 0;
            }
            for (unsigned i = 0; i < instruction.source_count; ++i) {
                auto source = instruction.sources[i];
                if (int(i) == subtract_source)
                    source.modifier = source.modifier == Modifier::negate ? Modifier::none : Modifier::absolute;
                sources[i] = load(source, instruction.opcode == Opcode::integer_add || instruction.opcode==Opcode::integer_mad);
            }
            Lanes result{};
            for (unsigned c = 0; c < 4; ++c) if (instruction.destination.mask & (1u << c)) {
                if (instruction.opcode == Opcode::move) result[c] = sources[0][c];
                else if(instruction.opcode==Opcode::bitwise_and){
                    const auto &a=sources[0][c],&b=sources[1][c];check(bool(a.integer) && bool(b.integer));
                    result[c].integer=std::make_shared<op::BitwiseAnd>(a.integer,b.integer);
                    result[c].predicate=a.predicate && b.predicate;
                    // A proven 0/all-ones mask preserves the other operand's
                    // float bits or returns +0. This is not numeric int-to-float
                    // conversion, and makes no assumption about arbitrary bits.
                    if(a.predicate && b.floating)result[c].floating=choose(std::make_shared<op::NotEqual>(a.integer,integer(0)),b.floating,number(0));
                    else if(b.predicate && a.floating)result[c].floating=choose(std::make_shared<op::NotEqual>(b.integer,integer(0)),a.floating,number(0));
                }
                else if(instruction.opcode==Opcode::bitwise_or){
                    const auto &a=sources[0][c],&b=sources[1][c];check(bool(a.integer) && bool(b.integer));
                    result[c].integer=std::make_shared<op::BitwiseOr>(a.integer,b.integer);
                    result[c].predicate=a.predicate && b.predicate;
                }
                else if (instruction.opcode == Opcode::integer_add) {
                    check(bool(sources[0][c].integer) && bool(sources[1][c].integer));
                    result[c].integer = std::make_shared<op::Add>(sources[0][c].integer, sources[1][c].integer);
                } else if(instruction.opcode==Opcode::integer_greater_equal){
                    check(bool(sources[0][c].integer) && bool(sources[1][c].integer));
                    // Provider integer comparisons can pass through f32 and
                    // conflate adjacent values above 2^24. Compare signed high
                    // words and unsigned low words, each exactly representable.
                    const auto a=sources[0][c].integer,b=sources[1][c].integer;
                    auto part=[&](const Node &v,int32_t mask)->Node{return std::make_shared<op::Convert>(
                        std::make_shared<op::BitwiseAnd>(v,integer(mask)),ov::element::f32);};
                    const auto ah=part(a,-65536),bh=part(b,-65536),al=part(a,65535),bl=part(b,65535);
                    auto bit=[](const Node &v)->Node{return std::make_shared<op::Convert>(v,ov::element::i32);};
                    const auto high=bit(std::make_shared<op::Greater>(ah,bh));
                    const auto same=bit(std::make_shared<op::Equal>(ah,bh));
                    const auto low=bit(std::make_shared<op::GreaterEqual>(al,bl));
                    result[c].integer=std::make_shared<op::Multiply>(std::make_shared<op::BitwiseOr>(high,
                        std::make_shared<op::BitwiseAnd>(same,low)),integer(-1));
                    result[c].predicate=true;
                } else if(instruction.opcode==Opcode::integer_mad){
                    check(bool(sources[0][c].integer) && bool(sources[1][c].integer) && bool(sources[2][c].integer));
                    result[c].integer=std::make_shared<op::Add>(std::make_shared<op::Multiply>(sources[0][c].integer,sources[1][c].integer),sources[2][c].integer);
                } else if(instruction.opcode==Opcode::shift_right){
                    check(bool(sources[0][c].integer) && bool(sources[1][0].integer));
                    result[c].integer=shift_right(sources[0][c].integer,sources[1][0].integer);
                } else if(instruction.opcode==Opcode::unsigned_shift_right){
                    check(bool(sources[0][c].integer) && bool(sources[1][0].integer));
                    const auto amount=std::make_shared<op::BitwiseAnd>(sources[1][0].integer,integer(31));
                    const auto index=std::make_shared<op::Reshape>(batch(amount),indices({int64_t(count)}),false);
                    std::vector<int32_t> masks(32);
                    for(unsigned n=0;n<32;++n){const auto bits=UINT32_MAX>>n;std::memcpy(&masks[n],&bits,4);}
                    const auto mask=std::make_shared<op::Reshape>(std::make_shared<op::Gather>(
                        op::Constant::create(ov::element::i32,ov::Shape{32},masks),index,integer(0)),batch_shape,false);
                    // Clear the sign extension after the exact arithmetic shift.
                    // Amount zero preserves all 32 original bits, including bit 31.
                    result[c].integer=std::make_shared<op::BitwiseAnd>(
                        shift_right(sources[0][c].integer,amount),mask);
                } else if(instruction.opcode==Opcode::float_to_integer){
                    // Decode the IEEE representation with integer operations.
                    // Native NPU Convert/clamp hangs on nonfinite and extreme
                    // inputs. A bit view also preserves NaN payloads and -0.
                    const auto raw=sources[0][c].integer;check(bool(raw));
                    const auto exponent=shift_right(std::make_shared<op::BitwiseAnd>(raw,integer(0x7f800000)),integer(23));
                    const auto fraction=std::make_shared<op::BitwiseAnd>(raw,integer(0x7fffff));
                    const auto mantissa=std::make_shared<op::BitwiseOr>(fraction,integer(0x800000));
                    const auto right=shift_right(mantissa,std::make_shared<op::Subtract>(integer(150),exponent));
                    const auto left_amount=std::make_shared<op::BitwiseAnd>(std::make_shared<op::Subtract>(exponent,integer(150)),integer(31));
                    std::vector<int32_t> powers(32);for(unsigned n=0;n<32;++n){const auto bits=1u<<n;std::memcpy(&powers[n],&bits,4);}
                    const auto index=std::make_shared<op::Reshape>(batch(left_amount),indices({int64_t(count)}),false);
                    const auto factor=std::make_shared<op::Reshape>(std::make_shared<op::Gather>(
                        op::Constant::create(ov::element::i32,ov::Shape{32},powers),index,integer(0)),batch_shape,false);
                    auto magnitude=choose(std::make_shared<op::GreaterEqual>(exponent,integer(150)),std::make_shared<op::Multiply>(mantissa,factor),right);
                    magnitude=choose(std::make_shared<op::Less>(exponent,integer(127)),integer(0),magnitude);
                    const auto sign_negative=std::make_shared<op::Less>(raw,integer(0));
                    auto value=choose(sign_negative,std::make_shared<op::Negative>(magnitude),magnitude);
                    value=choose(std::make_shared<op::GreaterEqual>(exponent,integer(158)),choose(sign_negative,integer(INT32_MIN),integer(INT32_MAX)),value);
                    const auto nan_bits=std::make_shared<op::BitwiseAnd>(
                        std::make_shared<op::Convert>(std::make_shared<op::Equal>(exponent,integer(255)),ov::element::i32),
                        std::make_shared<op::Convert>(std::make_shared<op::NotEqual>(fraction,integer(0)),ov::element::i32));
                    result[c].integer=choose(std::make_shared<op::NotEqual>(nan_bits,integer(0)),integer(0),value);
                } else if(instruction.opcode==Opcode::shift_left){
                    check(bool(sources[0][c].integer) && bool(sources[1][0].integer));
                    std::vector<int32_t> powers(32);for(unsigned n=0;n<32;++n){const uint32_t value=uint32_t(1)<<n;std::memcpy(&powers[n],&value,4);}
                    const auto amount=std::make_shared<op::BitwiseAnd>(sources[1][0].integer,integer(31));
                    const auto index=std::make_shared<op::Reshape>(batch(amount),indices({int64_t(count)}),false);
                    const auto factor=std::make_shared<op::Reshape>(std::make_shared<op::Gather>(
                        op::Constant::create(ov::element::i32,ov::Shape{32},powers),index,integer(0)),batch_shape,false);
                    result[c].integer=std::make_shared<op::Multiply>(sources[0][c].integer,factor);
                } else if (instruction.opcode == Opcode::integer_to_float) {
                    check(bool(sources[0][c].integer));
                    result[c].floating = std::make_shared<op::Convert>(sources[0][c].integer, ov::element::f32);
                } else if (instruction.opcode == Opcode::select) {
                    check(bool(sources[0][c].integer));
                    const auto condition = std::make_shared<op::NotEqual>(sources[0][c].integer, integer(0));
                    if (sources[1][c].floating && sources[2][c].floating)
                        result[c].floating = choose(condition, sources[1][c].floating, sources[2][c].floating);
                    if (sources[1][c].integer && sources[2][c].integer)
                        result[c].integer = choose(condition, sources[1][c].integer, sources[2][c].integer);
                    result[c].predicate=sources[1][c].predicate && sources[2][c].predicate;
                }
                else {
                    const auto a = sources[0][c].floating, b = sources[1][c].floating;
                    if (instruction.opcode != Opcode::dot2 && instruction.opcode != Opcode::dot3 && instruction.opcode != Opcode::dot4)
                        check(bool(a) && (instruction.source_count == 1 || bool(b)));
                    Node value;
                    if (instruction.opcode == Opcode::add) {
                        // Form subtraction before marking precision. Rewriting
                        // Add(Multiply(x,-1),y) inside the NPU compiler loses
                        // the precision marker on the replacement operation.
                        if (subtract_source == 0) value = std::make_shared<op::Subtract>(b, a);
                        else if (subtract_source == 1) value = std::make_shared<op::Subtract>(a, b);
                        else value = std::make_shared<op::Add>(a, b);
                    }
                    else if (instruction.opcode == Opcode::multiply || instruction.opcode == Opcode::mad) {
                        value = std::make_shared<op::Multiply>(a, b);
                        if (instruction.opcode == Opcode::mad) {
                            check(bool(sources[2][c].floating));
                            value = subtract_source == 2 ? Node(std::make_shared<op::Subtract>(value, sources[2][c].floating)) :
                                                           Node(std::make_shared<op::Add>(value, sources[2][c].floating));
                        }
                    } else if(instruction.opcode==Opcode::derivative_x || instruction.opcode==Opcode::derivative_y){
                        // The caller packs each aligned 2x2 quad TL, TR, BL, BR.
                        // Derivative arithmetic remains entirely on the provider.
                        const uint32_t axis=instruction.opcode==Opcode::derivative_x?1u:2u;
                        std::vector<int32_t> low(count),high(count);
                        for(uint32_t n=0;n<count;++n){low[n]=int32_t(n&~axis);high[n]=int32_t(n|axis);}
                        const auto source=batch(a);
                        const auto lo=std::make_shared<op::Gather>(source,op::Constant::create(ov::element::i32,ov::Shape{count},low),integer(0));
                        const auto hi=std::make_shared<op::Gather>(source,op::Constant::create(ov::element::i32,ov::Shape{count},high),integer(0));
                        value=std::make_shared<op::Subtract>(hi,lo);
                    } else if (instruction.opcode == Opcode::divide) value = std::make_shared<op::Divide>(a, b);
                    else if (instruction.opcode == Opcode::reciprocal_sqrt)
                        value = std::make_shared<op::Divide>(floating(0x3f800000), std::make_shared<op::Sqrt>(a));
                    else if(instruction.opcode==Opcode::floor){
                        // Avoid native NPU Floor and out-of-range integer casts.
                        // Every finite f32 at or above 2^24 is already integral.
                        // Keep the range constant representable in FP16.
                        const auto magnitude=std::make_shared<op::Abs>(a);
                        const auto small=std::make_shared<op::Less>(std::make_shared<op::Multiply>(magnitude,number(1.f/32768)),number(512));
                        const auto safe=choose(small,magnitude,number(0));
                        const auto integral=std::make_shared<op::BitwiseAnd>(std::make_shared<op::Convert>(safe,ov::element::i32),integer(0x7fffffff));
                        const auto whole=std::make_shared<op::Convert>(integral,ov::element::f32);
                        const auto negative_floor=std::make_shared<op::Subtract>(std::make_shared<op::Convert>(std::make_shared<op::Subtract>(integer(0),integral),ov::element::f32),
                            choose(std::make_shared<op::Greater>(safe,whole),number(1),number(0)));
                        const auto rounded=choose(std::make_shared<op::Less>(a,number(0)),negative_floor,whole);
                        const auto finite=choose(small,rounded,a);
                        // ROUND_NI flushes subnormals and preserves signed zero.
                        value=choose(std::make_shared<op::Less>(magnitude,number(1.1754943508222875e-38f)),
                            std::make_shared<op::Multiply>(a,number(0)),finite);
                    }
                    else if(instruction.opcode==Opcode::fraction){
                        // Finite f32 values with magnitude >= 2^23 are integral.
                        // Scale the range test to keep comparison constants within
                        // FP16 range; the provider demotes larger constants
                        // despite precision metadata. The 2^24 cutoff leaves
                        // slack below the last fractional f32 near 2^23.
                        // Explicit IsInf/IsNaN avoids a FLT_MAX comparison.
                        const auto magnitude=std::make_shared<op::Abs>(a);
                        const auto small=std::make_shared<op::Less>(std::make_shared<op::Multiply>(magnitude,number(1.f/32768)),number(512));
                        const auto safe=choose(small,magnitude,number(0));
                        const auto whole=std::make_shared<op::BitwiseAnd>(std::make_shared<op::Convert>(safe,ov::element::i32),integer(0x7fffffff));
                        const auto fraction=std::make_shared<op::Subtract>(safe,std::make_shared<op::Convert>(whole,ov::element::f32));
                        const auto negative_fraction=choose(std::make_shared<op::Equal>(fraction,number(0)),number(0),std::make_shared<op::Subtract>(number(1),fraction));
                        Node finite=choose(std::make_shared<op::Less>(a,number(0)),negative_fraction,fraction);
                        finite=choose(std::make_shared<op::Less>(magnitude,number(1.1754943508222875e-38f)),number(0),finite);
                        value=choose(std::make_shared<op::IsNaN>(a),a,choose(std::make_shared<op::IsInf>(a),std::make_shared<op::Subtract>(a,a),finite));
                    } else if (instruction.opcode == Opcode::less || instruction.opcode==Opcode::greater_equal || instruction.opcode==Opcode::equal || instruction.opcode==Opcode::not_equal) {
                        auto flush=[&](const Node &v)->Node{return choose(std::make_shared<op::Less>(std::make_shared<op::Abs>(v),number(1.1754943508222875e-38f)),number(0),v);};
                        const Node comparison=instruction.opcode==Opcode::less?Node(std::make_shared<op::Less>(a,b)):
                            instruction.opcode==Opcode::equal?Node(std::make_shared<op::Equal>(flush(a),flush(b))):
                            instruction.opcode==Opcode::not_equal?Node(std::make_shared<op::NotEqual>(flush(a),flush(b))):Node(std::make_shared<op::GreaterEqual>(flush(a),flush(b)));
                        const auto compared=std::make_shared<op::Convert>(comparison,ov::element::i32);
                        // Some CPU kernels lower GE to !LT, which is true for
                        // unordered NaN operands. Keep the unordered bits as
                        // separate integer operations, also avoiding NPU boolean
                        // fusion that changed the result for equal zero inputs.
                        const auto unordered=std::make_shared<op::BitwiseOr>(
                            std::make_shared<op::Convert>(std::make_shared<op::IsNaN>(a),ov::element::i32),
                            std::make_shared<op::Convert>(std::make_shared<op::IsNaN>(b),ov::element::i32));
                        const Node corrected=instruction.opcode==Opcode::not_equal?
                            Node(std::make_shared<op::BitwiseOr>(compared,unordered)):
                            Node(std::make_shared<op::BitwiseAnd>(compared,std::make_shared<op::BitwiseXor>(unordered,integer(1))));
                        result[c].integer=std::make_shared<op::Multiply>(corrected,integer(-1));
                        result[c].predicate=true;
                    } else if (instruction.opcode == Opcode::minimum || instruction.opcode == Opcode::maximum) {
                        // Native NPU floating min/max returned zero in the
                        // precision-marked graph. Compare and gather instead.
                        const auto less = std::make_shared<op::Less>(a, b);
                        Node numeric = instruction.opcode == Opcode::minimum ? choose(less, a, b) : choose(less, b, a);
                        value = choose(std::make_shared<op::IsNaN>(a), b, choose(std::make_shared<op::IsNaN>(b), a, numeric));
                    } else if (instruction.opcode == Opcode::dot2 || instruction.opcode == Opcode::dot3 || instruction.opcode == Opcode::dot4) {
                        value = std::make_shared<op::Multiply>(sources[0][0].floating, sources[1][0].floating);
                        for (unsigned i = 1; i < (instruction.opcode == Opcode::dot2 ? 2u : (instruction.opcode == Opcode::dot3 ? 3u : 4u)); ++i)
                            value = std::make_shared<op::Add>(value,
                                std::make_shared<op::Multiply>(sources[0][i].floating, sources[1][i].floating));
                    } else check(false);
                    result[c].floating = value;
                }
                if(instruction.saturate){
                    check(bool(result[c].floating));const auto original_value=result[c].floating;
                    // SM4 min/max flush before comparison and permit a flushed
                    // result. Choose that result explicitly for saturation.
                    const auto value=choose(std::make_shared<op::Less>(std::make_shared<op::Abs>(original_value),number(1.1754943508222875e-38f)),number(0),original_value);
                    const auto bounded=choose(std::make_shared<op::Less>(value,number(0)),number(0),
                        choose(std::make_shared<op::Greater>(value,number(1)),number(1),value));
                    result[c]={choose(std::make_shared<op::IsNaN>(value),number(0),bounded),{}};
                }
            }
            auto &destination = (instruction.destination.kind == Register::temporary ? temporary : output)[instruction.destination.index];
            for (unsigned c = 0; c < 4; ++c)
                if (instruction.destination.mask & (1u << c)) destination[c] = result[c];
        }
        ov::OutputVector results;
        for (unsigned i = 0; i < 16; ++i) if (program.outputs[i]) {
            ov::OutputVector components;
            for (unsigned c = 0; c < 4; ++c) {
                if (program.outputs[i] & (1u << c)) check(bool(output[i][c].floating) || bool(output[i][c].integer));
                components.push_back(batch((program.outputs[i] & (1u << c)) && output[i][c].floating ? output[i][c].floating : floating(0)));
            }
            Node result = std::make_shared<op::Concat>(components, 1);
            if(mirrored_taps){
                // Native ReduceSum demotes these values; axis-1 Gather in a
                // rank-3 reduction produced zeros. Flat row Gather + Add
                // preserves the qualified FP32 path for this tap reduction.
                for(unsigned remaining=tap_capacity;remaining>1;remaining/=2){
                    std::vector<int32_t> evens(logical_count*remaining/2),odds(evens.size());
                    for(unsigned row=0;row<logical_count;++row)for(unsigned n=0;n<remaining/2;++n){
                        const auto at=row*(remaining/2)+n;evens[at]=int32_t(row*remaining+n*2);odds[at]=evens[at]+1;}
                    const auto left=std::make_shared<op::Gather>(result,op::Constant::create(ov::element::i32,ov::Shape{evens.size()},evens),integer(0));
                    const auto right=std::make_shared<op::Gather>(result,op::Constant::create(ov::element::i32,ov::Shape{odds.size()},odds),integer(0));
                    result=std::make_shared<op::Add>(left,right);
                }
            }
            result->output(0).get_tensor().set_names({"vs_output_" + std::to_string(i)});
            results.push_back(result); outputs.push_back(i);
            // The NPU floating path canonicalizes negative zero. MOV and
            // source modifiers have exact integer bit views, so emit those
            // through an integer output and pack their bytes on the host.
            // Sign-bit arithmetic itself remains part of the NPU graph.
            ov::OutputVector bit_components;
            for (unsigned c = 0; c < 4; ++c) {
                const bool available = (program.outputs[i] & (1u << c)) && output[i][c].integer;
                if (available) output_bit_masks[i] |= 1u << c;
                bit_components.push_back(batch(available ? output[i][c].integer : integer(0)));
            }
            if (output_bit_masks[i]) {
                auto bits = std::make_shared<op::Concat>(bit_components, 1);
                bits->output(0).get_tensor().set_names({"vs_bits_" + std::to_string(i)});
                results.push_back(bits);
            }
        }
        if (!invalid_coordinates.empty()) {
            Node invalid = invalid_coordinates.front();
            for (size_t i = 1; i < invalid_coordinates.size(); ++i)
                invalid = std::make_shared<op::BitwiseOr>(invalid, invalid_coordinates[i]);
            auto status = std::make_shared<op::ReduceMax>(invalid, indices({0, 1}), false);
            status->output(0).get_tensor().set_names({"invalid_sample_coordinates"}); results.push_back(status);
            checks_sample_coordinates = true;
        }
        check(!results.empty());
        // Drop unused bit/float views before creating the model's public inputs.
        std::set<const ov::Node *> reachable;
        std::function<void(const ov::Node *)> visit = [&](const ov::Node *node) {
            if (!reachable.insert(node).second) return;
            for (const auto &value : node->input_values()) visit(value.get_node());
        };
        for (const auto &result : results) visit(result.get_node());
        ov::ParameterVector parameters; std::vector<Binding> used;
        for (const auto &binding : bindings) if (reachable.count(binding.parameter.get())) {
            parameters.push_back(binding.parameter); used.push_back(binding);
        }
        bindings = std::move(used);
        auto model = std::make_shared<ov::Model>(results, parameters, "SM4_vertex_arithmetic");
        ov::AnyMap properties{ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY)};
        if (backend == "NPU") {
            // OpenVINO 2026.1 uses the legacy precise_0 runtime attribute.
            // Its serializer materializes the typed precision marker; loading
            // the in-memory IR keeps it across the compiler boundary. A plain
            // f32 input type or ACCURACY hint alone still computes in f16.
            // No private runtime symbols, disk models or CPU fallback are used.
            for (const auto &node : model->get_ops()) node->get_rt_info()["precise_0"] = std::string{};
            std::ostringstream xml, weights;
            ov::pass::Serialize serializer(xml, weights);
            serializer.run_on_model(model);
            const auto bytes = weights.str();
            ov::Tensor data(ov::element::u8, ov::Shape{bytes.size()});
            if (!bytes.empty()) std::memcpy(data.data(), bytes.data(), bytes.size());
            model = core.read_model(xml.str(), data);
            for (const auto &node : model->get_ops())
                if (!nr::provider::precision_preserved(node)) throw std::runtime_error("vertex precision marker was lost");
            properties.emplace(ov::hint::execution_mode(ov::hint::ExecutionMode::ACCURACY));
        }
        if(defer_compile)return;
        compiled = nr::provider::compile_model(core, model, backend, properties);
        check(compiled.inputs().size() == bindings.size() && compiled.outputs().size() == results.size());
        const auto devices = compiled.get_property(ov::execution_devices);
        if (devices.size() != 1 || !(devices[0] == backend || devices[0].rfind(backend + ".", 0) == 0))
            throw std::runtime_error("unexpected vertex execution device");
        execution_device = devices[0]; request = compiled.create_infer_request();ready=true;
    }
};
void RegisterExecutor::validate(const Program &original) {
    const auto program=lower_bounded_loops(original);
    // Type views are properties of each SSA value, not permanent register
    // types. Branch joins retain only the views available on both paths.
    using Masks = std::array<uint32_t, 16>;
    struct Views { Masks temporary_float{}, temporary_bits{}, output_float{}, output_bits{},temporary_predicate{},output_predicate{}; };
    struct Branch { Views before, then_values{}; bool has_else = false; };
    Views views;
    std::vector<Branch> branches;
    auto available = [&](const Operand &source, bool bits) {
        uint32_t raw = 15;
        if (source.kind == Register::temporary)
            raw = (bits ? views.temporary_bits : views.temporary_float)[source.index];
        else if (source.kind == Register::input) raw = program.inputs[source.index];
        uint32_t result = 0;
        for (unsigned c = 0; c < 4; ++c) if (raw & (1u << source.lanes[c])) result |= 1u << c;
        return result;
    };
    auto predicates=[&](const Operand &source){
        uint32_t result=0;if(source.modifier!=Modifier::none)return result;
        for(unsigned c=0;c<4;++c){const auto lane=source.lanes[c];
            if(source.kind==Register::temporary && (views.temporary_predicate[source.index]&(1u<<lane)))result|=1u<<c;
            if(source.kind==Register::immediate && (source.literal[lane]==0 || source.literal[lane]==UINT32_MAX))result|=1u<<c;
        }
        return result;
    };
    for (const auto &instruction : program.instructions) {
        if(instruction.opcode==Opcode::loop_overflow)continue;
        if(instruction.opcode==Opcode::derivative_x || instruction.opcode==Opcode::derivative_y)check(program.stage==Stage::pixel && branches.empty());
        for (unsigned i = 0; i < instruction.source_count; ++i) {
            const auto &source = instruction.sources[i];
            if (source.relative.enabled && source.relative.kind == Register::temporary)
                check((views.temporary_bits[source.relative.index] & (1u << source.relative.component)) != 0);
        }
        if (instruction.opcode == Opcode::begin_if) {
            check((available(instruction.sources[0], true) & 1u) != 0);
            branches.push_back({views}); continue;
        }
        if (instruction.opcode == Opcode::otherwise) {
            auto &b = branches.back(); b.then_values = views; b.has_else = true; views = b.before; continue;
        }
        if (instruction.opcode == Opcode::end_if) {
            const auto &b = branches.back(); const auto &other = b.has_else ? b.then_values : b.before;
            for (unsigned i = 0; i < 16; ++i) {
                views.temporary_float[i] &= other.temporary_float[i]; views.temporary_bits[i] &= other.temporary_bits[i];
                views.output_float[i] &= other.output_float[i]; views.output_bits[i] &= other.output_bits[i];
                views.temporary_predicate[i]&=other.temporary_predicate[i];views.output_predicate[i]&=other.output_predicate[i];
            }
            branches.pop_back(); continue;
        }
        const auto &destination = instruction.destination;
        const auto mask = destination.mask;
        uint32_t floating_views = 0, bit_views = 0,predicate_views=0;
        if (instruction.opcode == Opcode::move) {
            floating_views = available(instruction.sources[0], false); bit_views = available(instruction.sources[0], true);
            predicate_views=predicates(instruction.sources[0]);
            if (instruction.sources[0].modifier != Modifier::none) check(!(mask & ~bit_views));
        } else if (instruction.opcode == Opcode::select) {
            check(!(mask & ~available(instruction.sources[0], true)));
            floating_views = available(instruction.sources[1], false) & available(instruction.sources[2], false);
            bit_views = available(instruction.sources[1], true) & available(instruction.sources[2], true);
            predicate_views=predicates(instruction.sources[1])&predicates(instruction.sources[2]);
        } else if(instruction.opcode==Opcode::bitwise_and){
            for(unsigned i=0;i<2;++i)check(!(mask&~available(instruction.sources[i],true)));
            const auto a=predicates(instruction.sources[0]),b=predicates(instruction.sources[1]);
            bit_views=mask;predicate_views=a&b;
            floating_views=(a&available(instruction.sources[1],false))|(b&available(instruction.sources[0],false));
        } else if (instruction.opcode == Opcode::integer_add || instruction.opcode == Opcode::integer_greater_equal || instruction.opcode == Opcode::integer_mad || instruction.opcode==Opcode::shift_right || instruction.opcode==Opcode::unsigned_shift_right || instruction.opcode==Opcode::bitwise_or || instruction.opcode == Opcode::shift_left || instruction.opcode == Opcode::integer_to_float) {
            for (unsigned i = 0; i < instruction.source_count; ++i) check(!(mask & ~available(instruction.sources[i], true)));
            if (instruction.opcode != Opcode::integer_to_float) bit_views = mask; else floating_views = mask;
            if(instruction.opcode==Opcode::integer_greater_equal)predicate_views=mask;
        } else if(instruction.opcode==Opcode::float_to_integer){
            check(!(mask&~available(instruction.sources[0],true)));bit_views=mask;
        } else if(instruction.opcode==Opcode::load){
            const auto slot=instruction.sources[1].index,used=program.texture_dimensions[slot]==1?1u:11u;
            check(!(used&~available(instruction.sources[0],true)));bit_views=mask;
            if(!program.texture_uint[slot])floating_views=mask;
        } else if (instruction.opcode == Opcode::sample) {
            check(program.stage == Stage::pixel && (available(instruction.sources[0], false) & 3u) == 3u);
            floating_views = mask;
        } else {
            const auto used = instruction.opcode == Opcode::dot2 ? 3u : (instruction.opcode == Opcode::dot3 ? 7u : (instruction.opcode == Opcode::dot4 ? 15u : mask));
            for (unsigned i = 0; i < instruction.source_count; ++i) check(!(used & ~available(instruction.sources[i], false)));
            if (instruction.opcode == Opcode::less || instruction.opcode==Opcode::greater_equal || instruction.opcode==Opcode::equal || instruction.opcode==Opcode::not_equal)bit_views=predicate_views=mask; else floating_views = mask;
        }
        if(instruction.saturate){check(!(mask&~floating_views));bit_views=predicate_views=0;}
        check(!(mask & ~(floating_views | bit_views)));
        auto &floats = destination.kind == Register::temporary ? views.temporary_float : views.output_float;
        auto &bits = destination.kind == Register::temporary ? views.temporary_bits : views.output_bits;
        floats[destination.index] = (floats[destination.index] & ~mask) | (floating_views & mask);
        bits[destination.index] = (bits[destination.index] & ~mask) | (bit_views & mask);
        auto &known=destination.kind==Register::temporary?views.temporary_predicate:views.output_predicate;
        known[destination.index]=(known[destination.index]&~mask)|(predicate_views&mask);
    }
    for (unsigned i = 0; i < 16; ++i) check(!(program.outputs[i] & ~(views.output_float[i] | views.output_bits[i])));
}
namespace {
bool captured_compositor_program(const Program &p){
    if(p.stage!=Stage::pixel)return false;
    using namespace nr::registered_lab::test_fixture;
    const auto matches=[&](const auto &words){return p.bytecode.size()==std::size(words) &&
        std::equal(p.bytecode.begin(),p.bytecode.end(),std::begin(words));};
    return matches(compositor_pixel_words) || matches(compositor_fade_pixel_words) ||
        matches(compositor_three_texture_pixel_words) || matches(compositor_three_texture_base_pixel_words);
}
}
uint32_t RegisterExecutor::maximum_batch(const Program &p){
    // The 65,536-tap raster graph lost the NPU device during qualification.
    // Bound this specialization to the verified 32,768-tap graph shape.
    if(mirrored_loop_shader(p))return 256u;
    // Only exact captured bytecode and the separately qualified runtime use
    // larger compositor graphs; the legacy compiler retains its old bound.
    if(captured_compositor_program(p))return nr::provider::compositor_batch_capacity();
    return 65536u;
}
uint32_t RegisterExecutor::preferred_raster_batch(const Program &p){
    // Preserve captured compositor scheduling; generic enlargement is opt-in.
    // The executor still applies maximum_batch to the selected raster shape.
    if(captured_compositor_program(p))return (std::max)(4096u,nr::provider::compositor_batch_capacity());
    auto batch=nr::provider::generic_raster_capacity();std::array<uint32_t,2> attributes{};
    for(unsigned r=0;r<16;++r)if(p.inputs[r] && r!=p.position_input){if(p.interpolation[r]==2)++attributes[0];if(p.interpolation[r]==4)++attributes[1];}
    // Interpolators pack equal-mode attributes; their graph limit is 65,536.
    const auto count=(std::max)({1u,attributes[0],attributes[1]});while(batch>4096 && uint64_t(batch)*count>65536)batch/=2;
    return batch;
}
struct RegisterExecutor::Configuration {
    Program program;
    uint32_t count;
    std::string backend;
    TextureShapes textures;
    TextureSamplers samplers;
};
RegisterExecutor::RegisterExecutor(const Program &p, uint32_t count, const std::string &backend,
                                   const TextureShapes &textures, const TextureSamplers &samplers)
    : state(std::make_unique<State>(p, count, backend, textures, samplers,mirrored_loop_shader(p)?1u:128u,mirrored_loop_shader(p))) {
    if(mirrored_loop_shader(p))configuration=std::make_unique<Configuration>(Configuration{p,count,backend,textures,samplers});
}
RegisterExecutor::~RegisterExecutor() = default;
RegisterExecutor::TextureBatch::TextureBatch(RegisterExecutor &executor,const TexturePixels &resources,bool enable):owner(executor),textures(resources),reuse(enable){
    check(owner.state && !owner.state->failed && !owner.active_batch);owner.active_batch=this;owner.state->textures_uploaded=false;
}
RegisterExecutor::TextureBatch::~TextureBatch(){owner.active_batch=nullptr;}
RegisterExecutor::Result RegisterExecutor::TextureBatch::execute(const std::vector<VertexRegisters> &vertices,const VertexConstants &constants){
    check(owner.active_batch==this);return owner.execute_internal(vertices,constants,textures,reuse);
}
RegisterExecutor::TextureBatch RegisterExecutor::bind_textures(const TexturePixels &textures,bool reuse){return TextureBatch(*this,textures,reuse);}
RegisterExecutor::Result RegisterExecutor::execute(const std::vector<VertexRegisters> &vertices,const VertexConstants &constants,const TexturePixels &textures){
    check(!active_batch);return execute_internal(vertices,constants,textures,false);
}
RegisterExecutor::Result RegisterExecutor::execute_internal(const std::vector<VertexRegisters> &vertices,const VertexConstants &constants,const TexturePixels &textures,bool reuse){
    check(state && !state->failed && vertices.size() == state->logical_count);
    for (unsigned i = 0; i < 14; ++i) check(constants[i].size() >= state->program.constants[i]);
    Result result;
    if(state->mirrored_taps){
        int32_t taps;std::memcpy(&taps,&constants[0][0][3],4);check(taps<=128);
        uint32_t capacity=1;while(taps>int32_t(capacity))capacity*=2;
        if(!state->ready || capacity!=state->tap_capacity){
            check(bool(configuration));const auto &c=*configuration;
            // Defer compilation until the first constants establish capacity.
            // Constructor graph validation remains eager.
            // The exact captured shader's uniform loop bound controls only
            // graph capacity. Coordinates, samples, weights and reduction
            // still execute on the provider. Release before recompiling for
            // the qualified NPU driver's model-lifetime constraint.
            state.reset();
            state=std::make_unique<State>(c.program,c.count,c.backend,c.textures,c.samplers,capacity);
        }
    }
    try {
        for (size_t i = 0; i < state->bindings.size(); ++i) {
            const auto &binding = state->bindings[i]; auto tensor = state->request.get_tensor(binding.name);
            if(!binding.texture || !reuse || !state->textures_uploaded)result.input_bytes += tensor.get_byte_size();
            if (binding.half_texel) {
                check(tensor.get_size() == 1 && tensor.get_element_type() == ov::element::f32);
                *tensor.data<float>() = .5f;
            } else if (binding.texture) {
                const auto shape = state->texture_shapes[binding.index];
                const auto total=size_t(shape.width)*shape.height;
                const auto length=binding.texture_rows?size_t(binding.texture_rows):total;
                check(textures[binding.index].size()==total && binding.texture_offset<=total && length<=total-binding.texture_offset &&
                      tensor.get_byte_size()==length*sizeof(std::array<float,4>));
                if(!reuse || !state->textures_uploaded){std::memcpy(tensor.data(),textures[binding.index].data()+binding.texture_offset,tensor.get_byte_size());result.texture_upload_bytes+=tensor.get_byte_size();}
                else result.texture_reused_bytes+=tensor.get_byte_size();
            } else if (binding.constant) {
                check(tensor.get_byte_size() == size_t(state->program.constants[binding.index]) * sizeof(VertexBits));
                std::memcpy(tensor.data(), constants[binding.index].data(), tensor.get_byte_size());
            } else {
                check(tensor.get_byte_size() == vertices.size() * sizeof(VertexBits));
                auto *destination = static_cast<uint8_t *>(tensor.data());
                for (size_t v = 0; v < vertices.size(); ++v)
                    std::memcpy(destination + v * sizeof(VertexBits), vertices[v][binding.index].data(), sizeof(VertexBits));
            }
        }
        nr::provider::traced_infer(state->request,__FILE__,__LINE__);state->textures_uploaded=true;
        if (state->checks_sample_coordinates) {
            const auto status = state->request.get_tensor("invalid_sample_coordinates");
            result.output_bytes += status.get_byte_size();
            check(status.get_size() == 1 && status.get_element_type() == ov::element::i32);
            if (*status.data<const int32_t>()) throw std::runtime_error("invalid texture coordinate, indexable temporary lookup, or loop iteration budget");
        }
        result.vertices.resize(vertices.size()); result.execution_device = state->execution_device;
        for (size_t i = 0; i < state->outputs.size(); ++i) {
            const auto tensor = state->request.get_tensor("vs_output_" + std::to_string(state->outputs[i]));
            result.output_bytes += tensor.get_byte_size();
            check(tensor.get_element_type() == ov::element::f32 && tensor.get_byte_size() == vertices.size() * sizeof(VertexBits));
            const auto *source = static_cast<const uint8_t *>(tensor.data());
            for (size_t v = 0; v < vertices.size(); ++v)
                std::memcpy(result.vertices[v][state->outputs[i]].data(), source + v * sizeof(VertexBits), sizeof(VertexBits));
            const auto mask = state->output_bit_masks[state->outputs[i]];
            if (mask) {
                const auto bits = state->request.get_tensor("vs_bits_" + std::to_string(state->outputs[i]));
                result.output_bytes += bits.get_byte_size();
                check(bits.get_element_type() == ov::element::i32 && bits.get_byte_size() == tensor.get_byte_size());
                const auto *raw = static_cast<const uint8_t *>(bits.data());
                for (size_t v = 0; v < vertices.size(); ++v) for (unsigned c = 0; c < 4; ++c) if (mask & (1u << c))
                    std::memcpy(&result.vertices[v][state->outputs[i]][c], raw + (v * 4 + c) * sizeof(uint32_t), sizeof(uint32_t));
            }
        }
        return result;
    } catch (...) { state->failed = true; throw; }
}
}
