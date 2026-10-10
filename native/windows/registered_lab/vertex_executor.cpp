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
        auto load = [&](const Operand &a) {
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
                if (a.modifier != Modifier::none && result[c].floating) {
                    if (a.modifier == Modifier::absolute || a.modifier == Modifier::absolute_negate)
                        result[c].floating = std::make_shared<op::Abs>(result[c].floating);
                    if (a.modifier == Modifier::negate || a.modifier == Modifier::absolute_negate)
                        result[c].floating = std::make_shared<op::Negative>(result[c].floating);
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
        for (const auto &instruction : program.instructions) {
            check(!instruction.saturate); // NaN/denormal saturation is not qualified on NPU yet.
            std::array<Lanes, 3> sources{};
            for (unsigned i = 0; i < instruction.source_count; ++i) sources[i] = load(instruction.sources[i]);
            Lanes result{};
            for (unsigned c = 0; c < 4; ++c) if (instruction.destination.mask & (1u << c)) {
                if (instruction.opcode == Opcode::move) result[c] = sources[0][c];
                else {
                    const auto a = sources[0][c].floating, b = sources[1][c].floating;
                    check(bool(a) && bool(b)); Node value;
                    if (instruction.opcode == Opcode::add) value = std::make_shared<op::Add>(a, b);
                    else if (instruction.opcode == Opcode::multiply || instruction.opcode == Opcode::mad) {
                        value = std::make_shared<op::Multiply>(a, b);
                        if (instruction.opcode == Opcode::mad) {
                            check(bool(sources[2][c].floating));
                            value = std::make_shared<op::Add>(value, sources[2][c].floating);
                        }
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
            for (unsigned c = 0; c < 4; ++c)
                components.push_back(batch((program.outputs[i] & (1u << c)) ? output[i][c].floating : floating(0)));
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
    // parse_vertex has already checked declarations, operand bounds and lane
    // initialization. Track the additional bit-view requirement of this graph
    // lowering, using the old register state for simultaneous masked writes.
    std::array<uint32_t, 16> integer_lanes{};
    for (const auto &instruction : program.instructions) {
        check(!instruction.saturate);
        for (unsigned i = 0; i < instruction.source_count; ++i) {
            const auto &source = instruction.sources[i];
            if (source.relative.enabled && source.relative.kind == Register::temporary)
                check((integer_lanes[source.relative.index] & (1u << source.relative.component)) != 0);
            if (instruction.opcode == Opcode::move && source.modifier != Modifier::none &&
                source.kind == Register::temporary) {
                for (unsigned c = 0; c < 4; ++c) if (instruction.destination.mask & (1u << c))
                    check((integer_lanes[source.index] & (1u << source.lanes[c])) != 0);
            }
        }
        const auto &destination = instruction.destination;
        if (destination.kind != Register::temporary) continue;
        uint32_t bits = 0;
        if (instruction.opcode == Opcode::move) {
            const auto &source = instruction.sources[0];
            const auto available = source.kind == Register::temporary ? integer_lanes[source.index] : 15u;
            for (unsigned c = 0; c < 4; ++c)
                if (available & (1u << source.lanes[c])) bits |= 1u << c;
        }
        integer_lanes[destination.index] = (integer_lanes[destination.index] & ~destination.mask) |
                                          (bits & destination.mask);
    }
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
