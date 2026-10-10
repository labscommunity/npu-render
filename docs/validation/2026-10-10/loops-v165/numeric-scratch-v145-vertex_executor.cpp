#include "provider_compile.hpp"
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
struct Value { Node floating, integer; };
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
struct RegisterExecutor::State {
    Program program;
    uint32_t count;
    TextureShapes texture_shapes;
    bool checks_sample_coordinates = false;
    struct Binding { std::shared_ptr<op::Parameter> parameter; bool constant; unsigned index; std::string name; bool texture = false, half_texel = false; };
    std::vector<Binding> bindings;
    std::vector<unsigned> outputs;
    std::array<uint32_t, 16> output_bit_masks{};
    ov::Core core;
    ov::CompiledModel compiled;
    ov::InferRequest request;
    std::string execution_device;
    bool failed = false;
    State(const Program &p, uint32_t vertices, const std::string &backend,
          const TextureShapes &shapes, const TextureSamplers &samplers) : program(lower_bounded_loops(p)), count(vertices), texture_shapes(shapes) {
        check(count && count <= 65536 && (backend == "CPU" || backend == "GPU" || backend == "NPU"));
        RegisterExecutor::validate(program);
        check(!quad_derivatives(program) || !(count%4));
        std::array<Lanes, 16> input{}, temporary{}, output{};
        std::array<Value, 14> constants{};
        std::array<Value,16> literal_tables{};
        std::array<Node, 16> textures{}, texture_bits{};
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
            const auto value = parameter_pair(false, i, count);
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
                for (unsigned c = 0; c < 4; ++c) raw[c] = {floating(a.literal[c]), integer(int32_t(a.literal[c]))};
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
                const auto valid = std::make_shared<op::LogicalAnd>(
                    std::make_shared<op::GreaterEqual>(index, integer(0)),
                    std::make_shared<op::Less>(index, integer(int32_t(size))));
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
            const auto filter = samplers[instruction.sources[2].index].filter;
            check(shape.width && shape.width <= 8192 && shape.height && shape.height <= 8192 &&
                  uint64_t(shape.width) * shape.height <= 4194304 &&
                  (filter == TextureFilter::point || filter == TextureFilter::linear));
            if (!textures[slot]) {
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
            auto coordinate = [&](unsigned axis, uint32_t extent) -> Node {
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
            const auto x = coordinate(0, shape.width), y = coordinate(1, shape.height);
            auto floor_index = [&](const Node &value) -> Node {
                // Coordinates are nonnegative and bounded. Native NPU Floor
                // miscomputes FP32; Convert gives truncation here. The AND is
                // an identity on this range but prevents the compiler from
                // incorrectly deleting the lossy float/int/float round trip.
                return std::make_shared<op::BitwiseAnd>(std::make_shared<op::Convert>(value, ov::element::i32), integer(0x7fffffff));
            };
            const auto ix = floor_index(x), iy = floor_index(y);
            auto fetch = [&](int dx, int dy) -> Node {
                const auto sx = std::make_shared<op::Minimum>(std::make_shared<op::Add>(ix, integer(dx)), integer(int32_t(shape.width - 1)));
                const auto sy = std::make_shared<op::Minimum>(std::make_shared<op::Add>(iy, integer(dy)), integer(int32_t(shape.height - 1)));
                const auto index = std::make_shared<op::Reshape>(std::make_shared<op::Add>(sx,
                    std::make_shared<op::Multiply>(sy, integer(int32_t(shape.width)))), indices({int64_t(count)}), false);
                return std::make_shared<op::Gather>(textures[slot], index, integer(0));
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
            check(!instruction.saturate); // NaN/denormal saturation is not qualified on NPU yet.
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
                        current = {join(current.floating, alternative.floating), join(current.integer, alternative.integer)};
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
                } else if(instruction.opcode==Opcode::integer_mad){
                    check(bool(sources[0][c].integer) && bool(sources[1][c].integer) && bool(sources[2][c].integer));
                    result[c].integer=std::make_shared<op::Add>(std::make_shared<op::Multiply>(sources[0][c].integer,sources[1][c].integer),sources[2][c].integer);
                } else if(instruction.opcode==Opcode::shift_right){
                    check(bool(sources[0][c].integer) && bool(sources[1][0].integer));
                    result[c].integer=shift_right(sources[0][c].integer,sources[1][0].integer);
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
                    else if(instruction.opcode==Opcode::fraction){
                        // Finite f32 values with magnitude >= 2^23 are integral.
                        // A guarded conversion avoids the NPU Floor issue and
                        // never converts a nonfinite or out-of-range value.
                        const auto magnitude=std::make_shared<op::Abs>(a);
                        const auto small=std::make_shared<op::Less>(magnitude,number(16777216.f));
                        const auto safe=choose(small,magnitude,number(0));
                        const auto whole=std::make_shared<op::BitwiseAnd>(std::make_shared<op::Convert>(safe,ov::element::i32),integer(0x7fffffff));
                        const auto fraction=std::make_shared<op::Subtract>(safe,std::make_shared<op::Convert>(whole,ov::element::f32));
                        const auto negative_fraction=choose(std::make_shared<op::Equal>(fraction,number(0)),number(0),std::make_shared<op::Subtract>(number(1),fraction));
                        Node finite=choose(std::make_shared<op::Less>(a,number(0)),negative_fraction,fraction);
                        finite=choose(std::make_shared<op::Less>(magnitude,number(1.1754943508222875e-38f)),number(0),finite);
                        value=choose(std::make_shared<op::LessEqual>(magnitude,number(3.4028234663852886e38f)),finite,std::make_shared<op::Subtract>(a,a));
                    } else if (instruction.opcode == Opcode::less || instruction.opcode==Opcode::greater_equal) {
                        auto flush=[&](const Node &v)->Node{return choose(std::make_shared<op::Less>(std::make_shared<op::Abs>(v),number(1.1754943508222875e-38f)),number(0),v);};
                        const Node comparison=instruction.opcode==Opcode::less?Node(std::make_shared<op::Less>(a,b)):Node(std::make_shared<op::GreaterEqual>(flush(a),flush(b)));
                        result[c].integer = std::make_shared<op::Multiply>(
                            std::make_shared<op::Convert>(comparison, ov::element::i32), integer(-1));
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
            auto result = std::make_shared<op::Concat>(components, 1);
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
                if (!node->get_rt_info().count("precise_0")) throw std::runtime_error("vertex precision marker was lost");
            properties.emplace(ov::hint::execution_mode(ov::hint::ExecutionMode::ACCURACY));
        }
        compiled = nr::provider::compile_model(core, model, backend, properties);
        check(compiled.inputs().size() == bindings.size() && compiled.outputs().size() == results.size());
        const auto devices = compiled.get_property(ov::execution_devices);
        if (devices.size() != 1 || !(devices[0] == backend || devices[0].rfind(backend + ".", 0) == 0))
            throw std::runtime_error("unexpected vertex execution device");
        execution_device = devices[0]; request = compiled.create_infer_request();
    }
};
void RegisterExecutor::validate(const Program &original) {
    const auto program=lower_bounded_loops(original);
    // Type views are properties of each SSA value, not permanent register
    // types. Branch joins retain only the views available on both paths.
    using Masks = std::array<uint32_t, 16>;
    struct Views { Masks temporary_float{}, temporary_bits{}, output_float{}, output_bits{}; };
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
    for (const auto &instruction : program.instructions) {
        check(!instruction.saturate);
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
            }
            branches.pop_back(); continue;
        }
        const auto &destination = instruction.destination;
        const auto mask = destination.mask;
        uint32_t floating_views = 0, bit_views = 0;
        if (instruction.opcode == Opcode::move) {
            floating_views = available(instruction.sources[0], false); bit_views = available(instruction.sources[0], true);
            if (instruction.sources[0].modifier != Modifier::none) check(!(mask & ~bit_views));
        } else if (instruction.opcode == Opcode::select) {
            check(!(mask & ~available(instruction.sources[0], true)));
            floating_views = available(instruction.sources[1], false) & available(instruction.sources[2], false);
            bit_views = available(instruction.sources[1], true) & available(instruction.sources[2], true);
        } else if (instruction.opcode == Opcode::integer_add || instruction.opcode == Opcode::integer_greater_equal || instruction.opcode == Opcode::integer_mad || instruction.opcode==Opcode::shift_right || instruction.opcode == Opcode::shift_left || instruction.opcode == Opcode::integer_to_float) {
            for (unsigned i = 0; i < instruction.source_count; ++i) check(!(mask & ~available(instruction.sources[i], true)));
            if (instruction.opcode != Opcode::integer_to_float) bit_views = mask; else floating_views = mask;
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
            if (instruction.opcode == Opcode::less || instruction.opcode==Opcode::greater_equal) bit_views = mask; else floating_views = mask;
        }
        check(!(mask & ~(floating_views | bit_views)));
        auto &floats = destination.kind == Register::temporary ? views.temporary_float : views.output_float;
        auto &bits = destination.kind == Register::temporary ? views.temporary_bits : views.output_bits;
        floats[destination.index] = (floats[destination.index] & ~mask) | (floating_views & mask);
        bits[destination.index] = (bits[destination.index] & ~mask) | (bit_views & mask);
    }
    for (unsigned i = 0; i < 16; ++i) check(!(program.outputs[i] & ~(views.output_float[i] | views.output_bits[i])));
}
RegisterExecutor::RegisterExecutor(const Program &p, uint32_t count, const std::string &backend,
                                   const TextureShapes &textures, const TextureSamplers &samplers)
    : state(std::make_unique<State>(p, count, backend, textures, samplers)) {}
RegisterExecutor::~RegisterExecutor() = default;
RegisterExecutor::Result RegisterExecutor::execute(const std::vector<VertexRegisters> &vertices, const VertexConstants &constants,
                                                   const TexturePixels &textures) {
    check(!state->failed && vertices.size() == state->count);
    for (unsigned i = 0; i < 14; ++i) check(constants[i].size() >= state->program.constants[i]);
    Result result;
    try {
        for (size_t i = 0; i < state->bindings.size(); ++i) {
            const auto &binding = state->bindings[i]; auto tensor = state->request.get_tensor(binding.name);
            result.input_bytes += tensor.get_byte_size();
            if (binding.half_texel) {
                check(tensor.get_size() == 1 && tensor.get_element_type() == ov::element::f32);
                *tensor.data<float>() = .5f;
            } else if (binding.texture) {
                const auto shape = state->texture_shapes[binding.index];
                check(textures[binding.index].size() == size_t(shape.width) * shape.height &&
                      tensor.get_byte_size() == textures[binding.index].size() * sizeof(std::array<float, 4>));
                std::memcpy(tensor.data(), textures[binding.index].data(), tensor.get_byte_size());
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
        state->request.infer();
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
