#pragma once
// Bounded, device-independent SM4 vertex instruction representation. Numeric
// encodings follow the WDK d3d10TokenizedProgramFormat.hpp specification.
// Parsing is not execution or admission by the registered graphics driver.
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace nr::shader {
using std::size_t;
inline void check(bool value) {
    if (!value) throw std::invalid_argument("unsupported or malformed SM4 vertex program");
}
enum class Register : uint32_t { temporary = 0, input = 1, output = 2, immediate = 4, constant = 8 };
enum class Opcode : uint32_t { add = 0, mad = 50, move = 54, multiply = 56 };
enum class Modifier : uint32_t { none = 0, negate = 1, absolute = 2, absolute_negate = 3 };
struct RelativeIndex {
    bool enabled = false;
    Register kind = Register::temporary;
    uint32_t index = 0, component = 0;
};
struct Operand {
    Register kind{};
    Modifier modifier = Modifier::none;
    uint32_t index = 0, element = 0, mask = 0;
    std::array<uint32_t, 4> lanes{0, 1, 2, 3}, literal{};
    RelativeIndex relative{};
};
struct Instruction {
    Opcode opcode{};
    Operand destination{};
    std::array<Operand, 3> sources{};
    uint32_t source_count = 0;
    bool saturate = false;
};
struct Program {
    std::array<uint32_t, 16> inputs{}, outputs{};
    std::array<uint32_t, 14> constants{};
    std::array<bool, 14> dynamic_constants{};
    uint32_t temporaries = 0, position_output = 16;
    std::vector<Instruction> instructions;
};
class Reader {
    const uint32_t *words;
    size_t cursor, end;
public:
    Reader(const uint32_t *data, size_t first, size_t last) : words(data), cursor(first), end(last) {}
    uint32_t read() { check(cursor < end); return words[cursor++]; }
    bool done() const { return cursor == end; }
    Operand operand(bool destination, bool declaration = false, unsigned depth = 0) {
        check(depth <= 1);
        const auto token = read();
        Modifier modifier = Modifier::none;
        if (token & 0x80000000u) {
            check(!destination && !declaration && !depth);
            const auto extension = read();
            check((extension & 0x3fu) == 1 && !(extension & ~0xffu));
            modifier = Modifier((extension >> 6) & 3u);
        }
        const auto components = token & 3u, selection = (token >> 2) & 3u;
        const auto dimensions = (token >> 20) & 3u;
        Operand value; value.kind = Register((token >> 12) & 255u); value.modifier = modifier;
        check(components == 1 || components == 2);
        if (components == 1) { value.mask = 1; value.lanes.fill(0); }
        else if (!selection) { value.mask = (token >> 4) & 15u; }
        else if (selection == 1) {
            for (unsigned i = 0; i < 4; ++i) value.lanes[i] = (token >> (4 + 2 * i)) & 3u;
        } else if (selection == 2) value.lanes.fill((token >> 4) & 3u);
        else check(false);
        if (destination) check(components == 2 && !selection && value.mask);
        if (value.kind == Register::immediate) {
            check(!destination && !declaration && !dimensions && !(token & 0x7fc00000u));
            check(!selection);
            const auto count = components == 1 ? 1u : 4u;
            for (unsigned i = 0; i < count; ++i) value.literal[i] = read();
            if (count == 1) value.literal.fill(value.literal[0]);
            value.lanes = {0, 1, 2, 3};
            return value;
        }
        check(value.kind == Register::temporary || value.kind == Register::input ||
              value.kind == Register::output || value.kind == Register::constant);
        check(dimensions == (value.kind == Register::constant ? 2u : 1u));
        check(!((token >> 22) & 7u)); // First index is immediate32.
        value.index = read();
        check(value.index < (value.kind == Register::constant ? 14u : 16u));
        if (dimensions == 1) check(!(token & 0x7e000000u));
        else {
            check(!(token & 0x70000000u));
            const auto representation = (token >> 25) & 7u;
            check(representation == 0 || representation == 2 || representation == 3);
            if (representation != 2) value.element = read();
            if (representation) {
                check(!declaration && !depth);
                const auto index = operand(false, false, depth + 1);
                check((index.kind == Register::temporary || index.kind == Register::input) &&
                      index.lanes[0] == index.lanes[1] && index.lanes[0] == index.lanes[2] &&
                      index.lanes[0] == index.lanes[3]);
                value.relative = {true, index.kind, index.index, index.lanes[0]};
            }
        }
        if (!destination && !declaration) check(components == 1 || selection == 1 || selection == 2);
        return value;
    }
};
inline Program parse_vertex(const uint32_t *words, size_t count) {
    check(words && count >= 3 && count <= 16384 && words[0] == 0x00010040 && words[1] == count);
    Program result;
    bool body = false, returned = false, temps_declared = false, flags_declared = false;
    std::array<uint32_t, 16> written_temps{}, written_outputs{};
    auto source_valid = [&](const Operand &source, uint32_t used_mask) {
        auto initialized = [&](Register kind, uint32_t index, uint32_t lanes) {
            check(index < 16);
            if (kind == Register::temporary) check(index < result.temporaries && !(lanes & ~written_temps[index]));
            else if (kind == Register::input) check(!(lanes & ~result.inputs[index]));
            else check(false);
        };
        uint32_t lanes = 0;
        for (unsigned i = 0; i < 4; ++i) if (used_mask & (1u << i)) lanes |= 1u << source.lanes[i];
        if (source.kind == Register::temporary || source.kind == Register::input)
            initialized(source.kind, source.index, lanes);
        else if (source.kind == Register::constant) {
            check(result.constants[source.index] != 0);
            if (source.relative.enabled) {
                check(result.dynamic_constants[source.index]);
                initialized(source.relative.kind, source.relative.index, 1u << source.relative.component);
            }
        } else check(source.kind == Register::immediate);
    };
    for (size_t offset = 2; offset < count;) {
        check(!returned);
        const auto token = words[offset], opcode = token & 0x7ffu, length = (token >> 24) & 127u;
        check(!(token & 0x80000000u) && length && length <= count - offset);
        Reader in(words, offset + 1, offset + length);
        const auto controls = token & 0x00fff800u;
        if (opcode == 106) { // dcl_globalFlags
            check(!body && !flags_declared && !(controls & ~0x800u)); flags_declared = true;
        } else if (opcode == 104) { // dcl_temps
            check(!body && !temps_declared && !controls); result.temporaries = in.read();
            check(result.temporaries <= 16); temps_declared = true;
        } else if (opcode == 89) { // dcl_constantbuffer
            check(!body && !(controls & ~0x800u)); const auto a = in.operand(false, true);
            check(a.kind == Register::constant && a.element && a.element <= 4096 && !result.constants[a.index]);
            result.constants[a.index] = a.element; result.dynamic_constants[a.index] = controls != 0;
        } else if (opcode == 95 || opcode == 101 || opcode == 103) {
            check(!body && !controls); const auto a = in.operand(true, true);
            if (opcode == 95) {
                check(a.kind == Register::input && !(result.inputs[a.index] & a.mask));
                result.inputs[a.index] |= a.mask;
            } else {
                check(a.kind == Register::output && !(result.outputs[a.index] & a.mask));
                result.outputs[a.index] |= a.mask;
                if (opcode == 103) {
                    check(in.read() == 1 && result.position_output == 16 && a.mask == 15);
                    result.position_output = a.index;
                }
            }
        } else if (opcode == 62) {
            check(!controls && body); returned = true;
        } else {
            check(opcode == 0 || opcode == 50 || opcode == 54 || opcode == 56);
            check(!(controls & ~0x2000u) && result.instructions.size() < 1024);
            body = true;
            Instruction instruction; instruction.opcode = Opcode(opcode); instruction.saturate = controls != 0;
            instruction.destination = in.operand(true);
            const auto &d = instruction.destination;
            check((d.kind == Register::temporary && d.index < result.temporaries) ||
                  (d.kind == Register::output && !(d.mask & ~result.outputs[d.index])));
            instruction.source_count = opcode == 54 ? 1u : (opcode == 50 ? 3u : 2u);
            for (unsigned i = 0; i < instruction.source_count; ++i) {
                instruction.sources[i] = in.operand(false); source_valid(instruction.sources[i], d.mask);
            }
            (d.kind == Register::temporary ? written_temps : written_outputs)[d.index] |= d.mask;
            result.instructions.push_back(instruction);
        }
        check(in.done()); offset += length;
    }
    check(returned && result.position_output < 16);
    for (unsigned i = 0; i < 16; ++i) check(!(result.outputs[i] & ~written_outputs[i]));
    return result;
}
} // namespace nr::shader
