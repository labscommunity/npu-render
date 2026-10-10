#include "../../native/windows/registered_lab/vertex_executor.hpp"
#include "../../native/windows/registered_lab/capture_reader.hpp"
#include "../../native/windows/registered_lab/shader_reference.hpp"
#include <iostream>
#include <iomanip>
namespace {
const char *phase="arguments";
nr::shader::VertexConstants constants(const nr::capture::Snapshot &s, unsigned stage, const nr::shader::Program &p) {
    nr::shader::VertexConstants result;
    for (unsigned i = 0; i < result.size(); ++i) if (p.constants[i]) {
        result[i] = s.values<nr::shader::VertexBits>("constants-" + std::to_string(stage) + "-" + std::to_string(i));
        nr::capture::check(result[i].size() >= p.constants[i]); result[i].resize(p.constants[i]);
    }
    return result;
}
std::vector<nr::shader::VertexRegisters> fetch(const nr::capture::Snapshot &s, uint32_t count, uint32_t first, int32_t base,
                                                   const nr::shader::Program &vs, bool indexed, uint32_t topology) {
    const auto elements = s.has("layout") ? s.values<uint32_t>("layout") : std::vector<uint32_t>{};
    nr::capture::check(elements.size() % 6 == 0);
    std::vector<uint32_t> indices, ids;
    if (indexed) {
        const auto id = s.values<uint32_t>("indices-description");
        nr::capture::check(topology == 4 && id.size() == 4 && ((id[1] == 2 && id[3] == 57) || (id[1] == 4 && id[3] == 42)));
        const auto &ib = s.bytes("indices");
        nr::capture::check(id[0] == ib.size() && uint64_t(id[2]) + (uint64_t(first) + count) * id[1] <= ib.size());
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t index = 0; std::memcpy(&index, ib.data() + id[2] + size_t(first + i) * id[1], id[1]);
            const int64_t adjusted = int64_t(index) + base; nr::capture::check(adjusted >= 0 && adjusted <= UINT32_MAX);
            indices.push_back(uint32_t(adjusted)); ids.push_back(index);
        }
    } else {
        nr::capture::check(count >= 3 && uint64_t(first) + count - 1 <= UINT32_MAX);
        auto append = [&](uint32_t id) { indices.push_back(first + id); ids.push_back(id); };
        if (topology == 4) { nr::capture::check(!(count % 3)); for(uint32_t i=0;i<count;++i) append(i); }
        else {
            nr::capture::check(topology == 5 && uint64_t(count-2)*3 <= 65536);
            for(uint32_t i=0;i<count-2;++i) { append(i+(i&1));append(i+((i&1)?0:1));append(i+2); }
        }
    }
    const auto instance_args=s.has("instances")?s.values<uint32_t>("instances"):std::vector<uint32_t>{1,0};
    nr::capture::check(instance_args.size()==2 && instance_args[0] && uint64_t(indices.size())*instance_args[0]<=65536);
    const size_t per_instance=indices.size();const auto original_indices=indices,original_ids=ids;
    for(uint32_t n=1;n<instance_args[0];++n){indices.insert(indices.end(),original_indices.begin(),original_indices.end());ids.insert(ids.end(),original_ids.begin(),original_ids.end());}
    count=uint32_t(indices.size());
    std::vector<nr::shader::VertexRegisters> result(count);
    uint32_t registers = 0;
    for (size_t e = 0; e < elements.size(); e += 6) {
        const auto slot = elements[e], offset = elements[e + 1], format = elements[e + 2], reg = elements[e + 5];
        nr::capture::check(slot < 16 && reg < 16 && reg != vs.vertex_id_input && reg != vs.instance_id_input && elements[e+3]<=1 && (elements[e+3] || !elements[e+4]) && !(registers & (1u << reg)));
        registers |= 1u << reg;
        const auto name = "vertices-" + std::to_string(slot);
        const auto description = s.values<uint32_t>(name + "-description"); const auto &vb = s.bytes(name);
        nr::capture::check(description.size() == 4 && description[0] == vb.size() && description[1] <= 2048);
        // Captured desktop layout formats: float1/2/3/4 and RGBA8 UNORM.
        const auto lanes = format == 2 ? 4u : (format == 6 ? 3u : (format == 16 ? 2u : (format == 41 ? 1u : 0u)));
        uint32_t integer_lanes=0,component_bytes=0;bool signed_integer=false;
        switch(format) {
        case 62:case 64:integer_lanes=1;component_bytes=1;signed_integer=format==64;break;
        case 50:case 52:integer_lanes=2;component_bytes=1;signed_integer=format==52;break;
        case 30:case 32:integer_lanes=4;component_bytes=1;signed_integer=format==32;break;
        case 57:case 59:integer_lanes=1;component_bytes=2;signed_integer=format==59;break;
        case 38:case 40:integer_lanes=2;component_bytes=2;signed_integer=format==40;break;
        case 12:case 14:integer_lanes=4;component_bytes=2;signed_integer=format==14;break;
        case 42:case 43:integer_lanes=1;component_bytes=4;break;
        case 17:case 18:integer_lanes=2;component_bytes=4;break;
        case 7:case 8:integer_lanes=3;component_bytes=4;break;
        case 3:case 4:integer_lanes=4;component_bytes=4;break;
        }
        nr::capture::check(lanes || format == 28 || integer_lanes);
        const auto bytes = integer_lanes ? integer_lanes*component_bytes : format == 28 ? 4u : lanes * 4;
        for (uint32_t i = 0; i < count; ++i) {
            const uint64_t begin = uint64_t(description[2]) + offset + (elements[e+3]?uint64_t(instance_args[1])+(elements[e+4]?(i/per_instance)/elements[e+4]:0):indices[i]) * description[1];
            nr::capture::check(begin <= vb.size() && bytes <= vb.size() - begin);
            auto &v = result[i][reg]; v = {0, 0, 0, integer_lanes?1u:0x3f800000u};
            if (format == 28) for (unsigned c = 0; c < 4; ++c) v[c] = nr::shader::as_bits(float(vb[size_t(begin) + c]) / 255);
            else if(integer_lanes)for(unsigned c=0;c<integer_lanes;++c){
                uint32_t raw=0;std::memcpy(&raw,vb.data()+size_t(begin)+c*component_bytes,component_bytes);
                const auto sign=1u<<(component_bytes*8-1);if(signed_integer && (raw&sign))raw|=~((sign<<1)-1);v[c]=raw;
            }
            else std::memcpy(v.data(), vb.data() + size_t(begin), bytes);
        }
    }
    for(uint32_t reg=0;reg<16;++reg) nr::capture::check(!vs.inputs[reg] || reg==vs.vertex_id_input || reg==vs.instance_id_input || (registers & (1u<<reg)));
    if(vs.vertex_id_input<16) for(uint32_t n=0;n<count;++n) result[n][vs.vertex_id_input][0]=ids[n];
    if(vs.instance_id_input<16)for(uint32_t n=0;n<count;++n)result[n][vs.instance_id_input][0]=uint32_t(n/per_instance);
    return result;
}
}
int main(int argc,char **argv){if(argc!=2)return 2;std::cout<<std::setprecision(10);using namespace nr::shader;const nr::capture::Snapshot s(argv[1]);auto a=s.values<uint32_t>("arguments"),v=s.values<uint32_t>("vertex-shader");auto vs=parse_vertex(v.data(),v.size());auto cb=constants(s,0,vs);auto inputs=fetch(s,a[0],a[1],int32_t(a[2]),vs,true,a[3]);
for(auto &input:inputs){auto r=reference_vertex(vs,input,cb);for(unsigned n=0;n<16;++n)if(vs.outputs[n]){std::cout<<"r"<<n<<":";for(auto c:r[n])std::cout<<" "<<as_float(c);std::cout<<"; ";}std::cout<<"\n";}}
