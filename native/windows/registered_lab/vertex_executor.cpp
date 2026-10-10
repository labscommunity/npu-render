#include "vertex_executor.hpp"
#include <openvino/openvino.hpp>
#include <openvino/opsets/opset13.hpp>
#include <cstring>
#include <set>
#include <functional>
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
    struct Binding { std::shared_ptr<op::Parameter> parameter; bool constant; unsigned index; };
    std::vector<Binding> bindings;
    std::vector<unsigned> outputs;
    ov::Core core;
    ov::CompiledModel compiled;
    ov::InferRequest request;
    std::string execution_device;
    bool failed = false;
    State(const Program &p, uint32_t vertices, const std::string &backend) : program(p), count(vertices) {
        check(count && count <= 65536 && (backend == "CPU" || backend == "GPU" || backend == "NPU"));
        std::array<Lanes, 16> input{}, temporary{}, output{};
        std::array<Value, 14> constants{};
        auto parameter_pair = [&](bool constant, unsigned index, size_t rows) {
            Value result;
            for (unsigned i = 0; i < 2; ++i) {
                auto parameter = std::make_shared<op::Parameter>(i ? ov::element::i32 : ov::element::f32, ov::Shape{rows, 4});
                parameter->set_friendly_name(std::string(constant ? "cb" : "v") + std::to_string(index) + (i ? "_bits" : "_float"));
                bindings.push_back({parameter, constant, index});
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
                const auto safe = std::make_shared<op::Select>(valid, index, integer(0));
                // Intel NPU 5540/OpenVINO 2026.1 miscompiles the broadcast
                // Select when this boolean comes directly from i32 comparisons.
                // Rebuild only the 0/1 mask through f32; keep addresses and bounds
                // comparisons integer so large valid indices retain precision.
                const auto mask = std::make_shared<op::Greater>(
                    std::make_shared<op::Convert>(
                        std::make_shared<op::Reshape>(valid, batch_shape, false), ov::element::f32),
                    op::Constant::create(ov::element::f32, ov::Shape{}, {0.5f}));
                const auto &buffer = constants[a.index];
                const auto f = std::make_shared<op::Select>(mask,
                    std::make_shared<op::Gather>(buffer.floating, safe, integer(0)), floating(0));
                const auto i = std::make_shared<op::Select>(mask,
                    std::make_shared<op::Gather>(buffer.integer, safe, integer(0)), integer(0));
                for (unsigned c = 0; c < 4; ++c) raw[c] = {lane(f, c), lane(i, c)};
            } else check(false);
            Lanes result{};
            for (unsigned c = 0; c < 4; ++c) result[c] = raw[a.lanes[c]];
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
            results.push_back(std::make_shared<op::Concat>(components, 1)); outputs.push_back(i);
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
        compiled = core.compile_model(model, backend, ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY));
        const auto devices = compiled.get_property(ov::execution_devices);
        if (devices.size() != 1 || !(devices[0] == backend || devices[0].rfind(backend + ".", 0) == 0))
            throw std::runtime_error("unexpected vertex execution device");
        execution_device = devices[0]; request = compiled.create_infer_request();
    }
};
VertexExecutor::VertexExecutor(const Program &p, uint32_t count, const std::string &backend)
    : state(std::make_unique<State>(p, count, backend)) {}
VertexExecutor::~VertexExecutor() = default;
VertexExecutor::Result VertexExecutor::execute(const std::vector<VertexRegisters> &vertices, const VertexConstants &constants) {
    check(!state->failed && vertices.size() == state->count);
    for (unsigned i = 0; i < 14; ++i) check(constants[i].size() >= state->program.constants[i]);
    try {
        for (size_t i = 0; i < state->bindings.size(); ++i) {
            const auto &binding = state->bindings[i]; auto tensor = state->request.get_input_tensor(i);
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
            const auto tensor = state->request.get_output_tensor(i);
            check(tensor.get_element_type() == ov::element::f32 && tensor.get_byte_size() == vertices.size() * sizeof(VertexBits));
            const auto *source = static_cast<const uint8_t *>(tensor.data());
            for (size_t v = 0; v < vertices.size(); ++v)
                std::memcpy(result.vertices[v][state->outputs[i]].data(), source + v * sizeof(VertexBits), sizeof(VertexBits));
        }
        return result;
    } catch (...) { state->failed = true; throw; }
}
}
