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
enum class Register : uint32_t { temporary = 0, input = 1, output = 2, indexable = 3, immediate = 4, sampler = 6, texture = 7, constant = 8 };
enum class Opcode : uint32_t {
    add = 0, divide = 14, dot2 = 15, dot3 = 16, dot4 = 17, otherwise = 18, end_if = 21,
    float_to_integer = 27, integer_add = 30, begin_if = 31, integer_mad = 35, shift_left = 41, shift_right = 42, integer_to_float = 43, load = 45, less = 49,
    mad = 50, minimum = 51, maximum = 52, move = 54, select = 55,
    multiply = 56, reciprocal_sqrt = 68, sample = 69
};
enum class Modifier : uint32_t { none = 0, negate = 1, absolute = 2, absolute_negate = 3 };
enum class Stage { vertex, pixel };
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
    bool test_nonzero = true;
};
struct Program {
    Stage stage = Stage::vertex;
    std::array<uint32_t, 16> inputs{}, outputs{};
    std::array<uint32_t, 16> interpolation{};
    std::array<bool, 16> textures{}, samplers{}, texture_uint{};
    std::array<uint32_t, 16> texture_dimensions{};
    std::array<uint32_t, 14> constants{};
    std::array<bool, 14> dynamic_constants{};
    uint32_t temporaries = 0, position_output = 16, position_input = 16;
    uint32_t vertex_id_input = 16;
    // Constant-initialized indexable temporaries are folded into immutable
    // tables. Runtime lookup and its bounds check still execute on the provider.
    std::array<std::vector<std::array<uint32_t,4>>,16> literal_tables;
    std::vector<Instruction> instructions;
    // Owned token stream for replay/transport; the caller may free its storage.
    std::vector<uint32_t> bytecode;
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
        const bool handle = value.kind == Register::sampler || value.kind == Register::texture;
        check(components == 1 || components == 2 || (handle && components == 0));
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
              value.kind == Register::output || value.kind == Register::constant || value.kind == Register::indexable || handle);
        check(dimensions == (value.kind == Register::constant || value.kind == Register::indexable ? 2u : 1u));
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
        if (!destination && !declaration) check(components == 1 || selection == 1 || selection == 2 || (handle && !components));
        return value;
    }
};
inline Program parse_program(const uint32_t *words, size_t count, Stage stage) {
    check(words && count >= 3 && count <= 16384 &&
          words[0] == (stage == Stage::vertex ? 0x00010040u : 0x00000040u) && words[1] == count);
    Program result; result.stage = stage; result.bytecode.assign(words, words + count);
    bool body = false, returned = false, temps_declared = false, flags_declared = false;
    std::array<uint32_t, 16> written_temps{}, written_outputs{};
    std::array<std::vector<uint32_t>,16> table_written;
    std::array<uint32_t,16> table_components{};
    uint32_t table_elements = 0;
    struct Branch {
        std::array<uint32_t, 16> before_temps, before_outputs, then_temps{}, then_outputs{};
        bool has_else = false;
    };
    std::vector<Branch> branches;
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
        } else if(source.kind == Register::indexable) {
            check(branches.empty() && !table_written[source.index].empty());
            if(source.relative.enabled) {
                initialized(source.relative.kind,source.relative.index,1u<<source.relative.component);
                for(const auto mask:table_written[source.index])check(!(lanes&~mask));
            } else {
                check(source.element<table_written[source.index].size() && !(lanes&~table_written[source.index][source.element]));
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
        } else if(opcode == 105) { // dcl_indexableTemp, bounded literal initialization below
            check(!body && !controls);
            const auto index=in.read(),elements=in.read(),components=in.read();
            check(index<16 && elements && elements<=4096 && components && components<=4 && result.literal_tables[index].empty());
            check(elements<=4096-table_elements);table_elements+=elements;
            result.literal_tables[index].resize(elements);table_written[index].resize(elements);
            table_components[index]=(1u<<components)-1;
        } else if (opcode == 89) { // dcl_constantbuffer
            check(!body && !(controls & ~0x800u)); const auto a = in.operand(false, true);
            check(a.kind == Register::constant && a.element && a.element <= 4096 && !result.constants[a.index]);
            result.constants[a.index] = a.element; result.dynamic_constants[a.index] = controls != 0;
        } else if (opcode == 88 || opcode == 90) {
            check(stage == Stage::pixel && !body && (opcode==88 ? (controls==0x1800u || controls==0x800u) : controls==0));
            const auto a = in.operand(false, true);
            if (opcode == 88) {
                check(a.kind == Register::texture && !result.textures[a.index]);const auto returns=in.read();
                check(returns==0x5555u || returns==0x4444u);
                result.textures[a.index] = true;result.texture_dimensions[a.index]=controls>>11;result.texture_uint[a.index]=returns==0x4444u;
            } else {
                check(a.kind == Register::sampler && !result.samplers[a.index]); result.samplers[a.index] = true;
            }
        } else if (opcode == 98 || opcode == 100) {
            check(stage == Stage::pixel && !body && !(controls & ~0x7800u));
            const auto interpolation = controls >> 11;
            check(interpolation >= 1 && interpolation <= 7);
            const auto a = in.operand(true, true);
            check(a.kind == Register::input && !(result.inputs[a.index] & a.mask));
            if (result.inputs[a.index]) check(result.interpolation[a.index] == interpolation);
            result.inputs[a.index] |= a.mask; result.interpolation[a.index] = interpolation;
            if (opcode == 100) {
                check(in.read() == 1 && result.position_input == 16);
                result.position_input = a.index;
            }
        } else if (opcode == 95 || opcode == 96 || opcode == 101 || opcode == 103) {
            check(!body && !controls); const auto a = in.operand(true, true);
            if (opcode == 95 || opcode == 96) {
                check(stage == Stage::vertex && a.kind == Register::input && !(result.inputs[a.index] & a.mask));
                result.inputs[a.index] |= a.mask;
                if(opcode==96){check(in.read()==6 && a.mask==1 && result.vertex_id_input==16);result.vertex_id_input=a.index;}
            } else {
                check(a.kind == Register::output && !(result.outputs[a.index] & a.mask));
                result.outputs[a.index] |= a.mask;
                if (stage == Stage::pixel) check(a.index < 8);
                if (opcode == 103) {
                    check(stage == Stage::vertex && in.read() == 1 && result.position_output == 16 && a.mask == 15);
                    result.position_output = a.index;
                }
            }
        } else if (opcode == 31) {
            check(!(controls & ~0x40000u) && branches.size() < 8 && result.instructions.size() < 1024);
            body = true; Instruction instruction; instruction.opcode = Opcode::begin_if;
            instruction.test_nonzero = controls != 0; instruction.source_count = 1;
            instruction.sources[0] = in.operand(false); source_valid(instruction.sources[0], 1);
            const auto &condition = instruction.sources[0];
            check(condition.modifier == Modifier::none && condition.lanes[0] == condition.lanes[1] &&
                  condition.lanes[0] == condition.lanes[2] && condition.lanes[0] == condition.lanes[3]);
            branches.push_back({written_temps, written_outputs}); result.instructions.push_back(instruction);
        } else if (opcode == 18 || opcode == 21) {
            check(!controls && !branches.empty() && result.instructions.size() < 1024);
            auto &branch = branches.back();
            if (opcode == 18) {
                check(!branch.has_else); branch.has_else = true;
                branch.then_temps = written_temps; branch.then_outputs = written_outputs;
                written_temps = branch.before_temps; written_outputs = branch.before_outputs;
            } else {
                const auto &other_temps = branch.has_else ? branch.then_temps : branch.before_temps;
                const auto &other_outputs = branch.has_else ? branch.then_outputs : branch.before_outputs;
                for (unsigned i = 0; i < 16; ++i) { written_temps[i] &= other_temps[i]; written_outputs[i] &= other_outputs[i]; }
                branches.pop_back();
            }
            Instruction instruction; instruction.opcode = Opcode(opcode); result.instructions.push_back(instruction);
        } else if (opcode == 62) {
            check(!controls && body && branches.empty()); returned = true;
        } else {
            check(opcode == 0 || opcode == 14 || opcode == 15 || opcode == 16 || opcode == 17 || opcode == 27 || opcode == 30 || opcode == 35 || opcode == 41 || opcode == 42 || opcode == 45 ||
                  opcode == 43 || opcode == 49 || opcode == 50 || opcode == 51 || opcode == 52 ||
                  opcode == 54 || opcode == 55 || opcode == 56 || opcode == 68 || opcode == 69);
            check(!(controls & ~0x2000u) && result.instructions.size() < 1024);
            body = true;
            Instruction instruction; instruction.opcode = Opcode(opcode); instruction.saturate = controls != 0;
            instruction.destination = in.operand(true);
            const auto &d = instruction.destination;
            if(d.kind == Register::indexable){
                // Only unconditional literal writes before executable work are
                // folded. Later writes, dynamic writes and partially initialized
                // reads cannot silently become immutable table accesses.
                check(opcode==54 && !controls && branches.empty() && result.instructions.empty() &&
                      !d.relative.enabled && d.element<result.literal_tables[d.index].size() && !(d.mask&~table_components[d.index]));
                const auto literal=in.operand(false);check(literal.kind==Register::immediate && literal.modifier==Modifier::none);
                for(unsigned c=0;c<4;++c)if(d.mask&(1u<<c))result.literal_tables[d.index][d.element][c]=literal.literal[literal.lanes[c]];
                table_written[d.index][d.element]|=d.mask;
                check(in.done());offset+=length;continue;
            }
            check((d.kind == Register::temporary && d.index < result.temporaries) ||
                  (d.kind == Register::output && !(d.mask & ~result.outputs[d.index])));
            instruction.source_count = (opcode == 27 || opcode == 43 || opcode == 54 || opcode == 68) ? 1u :
                                       ((opcode == 35 || opcode == 50 || opcode == 55 || opcode == 69) ? 3u : 2u);
            for (unsigned i = 0; i < instruction.source_count; ++i) {
                instruction.sources[i] = in.operand(false);
                if(opcode==45)continue; // Validate its dimension-dependent address after both operands.
                if (opcode == 69 && i) {
                    const auto &a = instruction.sources[i];
                    check(stage == Stage::pixel && !controls && a.modifier == Modifier::none &&
                          (i == 1 ? a.kind == Register::texture && result.textures[a.index] && result.texture_dimensions[a.index]==3 && !result.texture_uint[a.index] :
                                    a.kind == Register::sampler && result.samplers[a.index]));
                    continue;
                }
                if (opcode == 69) { check(stage == Stage::pixel && !controls); source_valid(instruction.sources[i], 3); continue; }
                source_valid(instruction.sources[i], opcode == 15 ? 3u : (opcode == 16 ? 7u : (opcode == 17 ? 15u : d.mask)));
                if (opcode == 30 || opcode==35) check(instruction.sources[i].modifier == Modifier::none ||
                                        instruction.sources[i].modifier == Modifier::negate);
                if (opcode == 43 || (opcode == 55 && !i)) check(instruction.sources[i].modifier == Modifier::none);
                if(opcode==41 || opcode==42)check(instruction.sources[i].modifier==Modifier::none);
            }
            if(opcode==45){
                const auto &address=instruction.sources[0],&resource=instruction.sources[1];
                check(stage==Stage::pixel && !controls && resource.kind==Register::texture && resource.modifier==Modifier::none && result.textures[resource.index]);
                check(address.modifier==Modifier::none && address.kind!=Register::immediate);
                source_valid(address,result.texture_dimensions[resource.index]==1?1u:11u);
            }
            if(opcode==41 || opcode==42){const auto &amount=instruction.sources[1];
                check((amount.kind==Register::immediate && amount.literal[0]==amount.literal[1] && amount.literal[0]==amount.literal[2] && amount.literal[0]==amount.literal[3]) ||
                      (amount.lanes[0]==amount.lanes[1] && amount.lanes[0]==amount.lanes[2] && amount.lanes[0]==amount.lanes[3]));}
            (d.kind == Register::temporary ? written_temps : written_outputs)[d.index] |= d.mask;
            result.instructions.push_back(instruction);
        }
        check(in.done()); offset += length;
    }
    check(returned && (stage == Stage::vertex ? result.position_output < 16 : result.outputs[0] != 0));
    check(table_elements+result.temporaries<=4096);
    for (unsigned i = 0; i < 16; ++i) check(!(result.outputs[i] & ~written_outputs[i]));
    return result;
}
inline Program parse_vertex(const uint32_t *words, size_t count) { return parse_program(words, count, Stage::vertex); }
inline Program parse_pixel(const uint32_t *words, size_t count) { return parse_program(words, count, Stage::pixel); }
} // namespace nr::shader
