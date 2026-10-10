#pragma once
// Test/reference evaluation only. Never a fallback for the NPU provider.
#include "shader_ir.hpp"
#include <cmath>
#include <cstring>
namespace nr::shader {
using Bits = std::array<uint32_t, 4>;
using Registers = std::array<Bits, 16>;
using Constants = std::array<std::vector<Bits>, 14>;
inline float as_float(uint32_t value) { float result; std::memcpy(&result, &value, 4); return result; }
inline uint32_t as_bits(float value) { uint32_t result; std::memcpy(&result, &value, 4); return result; }
inline Registers reference_vertex(const Program &program, const Registers &inputs, const Constants &constants) {
    Registers temporary{}, output{};
    for (unsigned i = 0; i < 14; ++i) check(constants[i].size() >= program.constants[i]);
    auto load = [&](const Operand &a, bool integer_modifier = false) {
        Bits raw{};
        if (a.kind == Register::temporary) raw = temporary[a.index];
        else if (a.kind == Register::input) raw = inputs[a.index];
        else if (a.kind == Register::immediate) raw = a.literal;
        else if (a.kind == Register::constant) {
            auto index = a.element;
            if (a.relative.enabled) {
                const auto &registers = a.relative.kind == Register::temporary ? temporary : inputs;
                index += registers[a.relative.index][a.relative.component];
            }
            // Unsigned 32-bit addressing and zero on an out-of-range CB read.
            if (index < program.constants[a.index]) raw = constants[a.index][index];
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
    bool active = true;
    for (const auto &instruction : program.instructions) {
        if (instruction.opcode == Opcode::begin_if) {
            const bool condition = active && ((load(instruction.sources[0])[0] != 0) == instruction.test_nonzero);
            branches.push_back({active, condition}); active = active && condition; continue;
        }
        if (instruction.opcode == Opcode::otherwise) {
            const auto &b = branches.back(); active = b.parent && !b.condition; continue;
        }
        if (instruction.opcode == Opcode::end_if) { active = branches.back().parent; branches.pop_back(); continue; }
        if (!active) continue;
        std::array<Bits, 3> values{};
        for (unsigned i = 0; i < instruction.source_count; ++i)
            values[i] = load(instruction.sources[i], instruction.opcode == Opcode::integer_add);
        Bits value{};
        for (unsigned i = 0; i < 4; ++i) {
            if (!(instruction.destination.mask & (1u << i))) continue;
            if (instruction.opcode == Opcode::move) value[i] = values[0][i];
            else if (instruction.opcode == Opcode::select) value[i] = values[0][i] ? values[1][i] : values[2][i];
            else if (instruction.opcode == Opcode::integer_add) value[i] = values[0][i] + values[1][i];
            else if (instruction.opcode == Opcode::less)
                value[i] = as_float(values[0][i]) < as_float(values[1][i]) ? 0xffffffffu : 0;
            else if (instruction.opcode == Opcode::integer_to_float) {
                int32_t source; std::memcpy(&source, &values[0][i], sizeof(source)); value[i] = as_bits(float(source));
            }
            else {
                const auto a = as_float(values[0][i]), b = as_float(values[1][i]);
                float v = 0;
                if (instruction.opcode == Opcode::add) v = a + b;
                else if (instruction.opcode == Opcode::multiply) v = a * b;
                else if (instruction.opcode == Opcode::mad) v = a * b + as_float(values[2][i]);
                else if (instruction.opcode == Opcode::divide) v = a / b;
                else if (instruction.opcode == Opcode::reciprocal_sqrt) v = 1.0f / std::sqrt(a);
                else if (instruction.opcode == Opcode::minimum) v = std::fmin(a, b);
                else if (instruction.opcode == Opcode::maximum) v = std::fmax(a, b);
                else if (instruction.opcode == Opcode::dot3 || instruction.opcode == Opcode::dot4) {
                    v = as_float(values[0][0]) * as_float(values[1][0]);
                    for (unsigned c = 1; c < (instruction.opcode == Opcode::dot3 ? 3u : 4u); ++c)
                        v += as_float(values[0][c]) * as_float(values[1][c]);
                }
                else check(false);
                value[i] = as_bits(v);
            }
            if (instruction.saturate) {
                const auto v = as_float(value[i]);
                value[i] = as_bits(std::isnan(v) || v < 0 ? 0.0f : (v > 1 ? 1.0f : v));
            }
        }
        auto &destination = (instruction.destination.kind == Register::temporary ? temporary : output)[instruction.destination.index];
        for (unsigned i = 0; i < 4; ++i)
            if (instruction.destination.mask & (1u << i)) destination[i] = value[i];
    }
    return output;
}
} // namespace nr::shader
