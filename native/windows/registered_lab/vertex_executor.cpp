#include "vertex_executor.hpp"
#include <openvino/openvino.hpp>
#include <openvino/opsets/opset13.hpp>
#include <openvino/pass/serialize.hpp>
#include <cstring>
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
Node lane(const Node &value, unsigned component) {
    return std::make_shared<op::Gather>(value, indices({int64_t(component)}), integer(1));
}
}
struct VertexExecutor::State {
    Program program;
    uint32_t count;
    struct Binding { std::shared_ptr<op::Parameter> parameter; bool constant; unsigned index; std::string name; };
    std::vector<Binding> bindings;
    std::vector<unsigned> outputs;
    std::array<uint32_t, 16> output_bit_masks{};
    ov::Core core;
    ov::CompiledModel compiled;
    ov::InferRequest request;
    std::string execution_device;
    bool failed = false;
    State(const Program &p, uint32_t vertices, const std::string &backend) : program(p), count(vertices) {
        check(count && count <= 65536 && (backend == "CPU" || backend == "GPU" || backend == "NPU"));
        VertexExecutor::validate(program);
        std::array<Lanes, 16> input{}, temporary{}, output{};
        std::array<Value, 14> constants{};
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
            } else if (a.kind == Register::constant) {
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
                    std::make_shared<op::Less>(index, integer(int32_t(program.constants[a.index]))));
                // Route out-of-range reads to an appended zero row. The Intel
                // NPU compiler miscompiled broadcast floating Select masks in
                // both default and precision-preserving graphs. Integer index
                // selection followed by Gather avoids that operation entirely.
                const auto safe = std::make_shared<op::Select>(valid, index, integer(int32_t(program.constants[a.index])));
                const auto &buffer = constants[a.index];
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
        for (const auto &instruction : program.instructions) {
            check(!instruction.saturate); // NaN/denormal saturation is not qualified on NPU yet.
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
                sources[i] = load(source, instruction.opcode == Opcode::integer_add);
            }
            Lanes result{};
            for (unsigned c = 0; c < 4; ++c) if (instruction.destination.mask & (1u << c)) {
                if (instruction.opcode == Opcode::move) result[c] = sources[0][c];
                else if (instruction.opcode == Opcode::integer_add) {
                    check(bool(sources[0][c].integer) && bool(sources[1][c].integer));
                    result[c].integer = std::make_shared<op::Add>(sources[0][c].integer, sources[1][c].integer);
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
                    if (instruction.opcode != Opcode::dot3 && instruction.opcode != Opcode::dot4)
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
                    } else if (instruction.opcode == Opcode::divide) value = std::make_shared<op::Divide>(a, b);
                    else if (instruction.opcode == Opcode::reciprocal_sqrt)
                        value = std::make_shared<op::Divide>(floating(0x3f800000), std::make_shared<op::Sqrt>(a));
                    else if (instruction.opcode == Opcode::less) {
                        result[c].integer = std::make_shared<op::Multiply>(
                            std::make_shared<op::Convert>(std::make_shared<op::Less>(a, b), ov::element::i32), integer(-1));
                    } else if (instruction.opcode == Opcode::minimum || instruction.opcode == Opcode::maximum) {
                        // Native NPU floating min/max returned zero in the
                        // precision-marked graph. Compare and gather instead.
                        const auto less = std::make_shared<op::Less>(a, b);
                        Node numeric = instruction.opcode == Opcode::minimum ? choose(less, a, b) : choose(less, b, a);
                        value = choose(std::make_shared<op::IsNaN>(a), b, choose(std::make_shared<op::IsNaN>(b), a, numeric));
                    } else if (instruction.opcode == Opcode::dot3 || instruction.opcode == Opcode::dot4) {
                        value = std::make_shared<op::Multiply>(sources[0][0].floating, sources[1][0].floating);
                        for (unsigned i = 1; i < (instruction.opcode == Opcode::dot3 ? 3u : 4u); ++i)
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
        compiled = core.compile_model(model, backend, properties);
        check(compiled.inputs().size() == bindings.size() && compiled.outputs().size() == results.size());
        const auto devices = compiled.get_property(ov::execution_devices);
        if (devices.size() != 1 || !(devices[0] == backend || devices[0].rfind(backend + ".", 0) == 0))
            throw std::runtime_error("unexpected vertex execution device");
        execution_device = devices[0]; request = compiled.create_infer_request();
    }
};
void VertexExecutor::validate(const Program &program) {
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
        } else if (instruction.opcode == Opcode::integer_add || instruction.opcode == Opcode::integer_to_float) {
            for (unsigned i = 0; i < instruction.source_count; ++i) check(!(mask & ~available(instruction.sources[i], true)));
            if (instruction.opcode == Opcode::integer_add) bit_views = mask; else floating_views = mask;
        } else {
            const auto used = instruction.opcode == Opcode::dot3 ? 7u : (instruction.opcode == Opcode::dot4 ? 15u : mask);
            for (unsigned i = 0; i < instruction.source_count; ++i) check(!(used & ~available(instruction.sources[i], false)));
            if (instruction.opcode == Opcode::less) bit_views = mask; else floating_views = mask;
        }
        check(!(mask & ~(floating_views | bit_views)));
        auto &floats = destination.kind == Register::temporary ? views.temporary_float : views.output_float;
        auto &bits = destination.kind == Register::temporary ? views.temporary_bits : views.output_bits;
        floats[destination.index] = (floats[destination.index] & ~mask) | (floating_views & mask);
        bits[destination.index] = (bits[destination.index] & ~mask) | (bit_views & mask);
    }
    for (unsigned i = 0; i < 16; ++i) check(!(program.outputs[i] & ~(views.output_float[i] | views.output_bits[i])));
}
VertexExecutor::VertexExecutor(const Program &p, uint32_t count, const std::string &backend)
    : state(std::make_unique<State>(p, count, backend)) {}
VertexExecutor::~VertexExecutor() = default;
VertexExecutor::Result VertexExecutor::execute(const std::vector<VertexRegisters> &vertices, const VertexConstants &constants) {
    check(!state->failed && vertices.size() == state->count);
    for (unsigned i = 0; i < 14; ++i) check(constants[i].size() >= state->program.constants[i]);
    try {
        for (size_t i = 0; i < state->bindings.size(); ++i) {
            const auto &binding = state->bindings[i]; auto tensor = state->request.get_tensor(binding.name);
            if (binding.constant) {
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
        Result result; result.vertices.resize(vertices.size()); result.execution_device = state->execution_device;
        for (size_t i = 0; i < state->outputs.size(); ++i) {
            const auto tensor = state->request.get_tensor("vs_output_" + std::to_string(state->outputs[i]));
            check(tensor.get_element_type() == ov::element::f32 && tensor.get_byte_size() == vertices.size() * sizeof(VertexBits));
            const auto *source = static_cast<const uint8_t *>(tensor.data());
            for (size_t v = 0; v < vertices.size(); ++v)
                std::memcpy(result.vertices[v][state->outputs[i]].data(), source + v * sizeof(VertexBits), sizeof(VertexBits));
            const auto mask = state->output_bit_masks[state->outputs[i]];
            if (mask) {
                const auto bits = state->request.get_tensor("vs_bits_" + std::to_string(state->outputs[i]));
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
