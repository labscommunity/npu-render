#include "vertex_executor.hpp"
#include "shader_reference.hpp"
#include <cstdio>
int nr_probe_stage=0;
using namespace nr::shader;
int main(){try{Program p;p.stage=Stage::pixel;p.inputs[0]=15;p.outputs[0]=15;Instruction i;i.opcode=Opcode::fraction;i.source_count=1;i.destination.kind=Register::output;i.destination.mask=15;i.sources[0].kind=Register::input;p.instructions.push_back(i);
 std::vector<VertexRegisters> input(64);const float values[]{3.125f,65535.5f,65536.5f,8388607.5f};for(auto &v:input)for(unsigned c=0;c<4;++c)v[0][c]=as_bits(values[c]);
 for(nr_probe_stage=1;nr_probe_stage<=10;++nr_probe_stage){PixelExecutor executor(p,64,"NPU");const auto result=executor.execute(input,{});std::printf("{\"stage\":%d,\"values\":[",nr_probe_stage);for(unsigned c=0;c<4;++c)std::printf("%s%.9g",c?",":"",double(as_float(result.vertices[0][0][c])));std::printf("]}\n");std::fflush(stdout);}return 0;
}catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
