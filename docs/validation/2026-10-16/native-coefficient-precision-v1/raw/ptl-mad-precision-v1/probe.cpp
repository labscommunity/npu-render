#define NOMINMAX
#include "vertex_executor.hpp"
#include "shader_reference.hpp"
#include <cstdio>
#include <chrono>
using namespace nr::shader;
int main()try{
 constexpr unsigned count=4096;std::vector<VertexRegisters> inputs(count);uint32_t state=0x15c31;
 for(unsigned n=0;n<count;++n)for(unsigned c=0;c<4;++c){state=1664525*state+1013904223;float v=float(state>>8)/16777216.0f;if(n<5)v=std::array<float,5>{0,1,.5f,.25f,.75f}[n];inputs[n][0][c]=as_bits(v);}
 Program base{};base.stage=Stage::pixel;base.inputs[0]=15;base.outputs[0]=15;base.temporaries=1;
 Instruction mad{};mad.opcode=Opcode::mad;mad.source_count=3;mad.destination.kind=Register::output;mad.destination.mask=15;
 mad.sources[0].kind=Register::input;mad.sources[0].index=0;mad.sources[0].modifier=Modifier::negate;
 mad.sources[1].kind=Register::immediate;mad.sources[1].literal.fill(as_bits(.02f));mad.sources[2].kind=Register::immediate;mad.sources[2].literal.fill(as_bits(1.0f));base.instructions.push_back(mad);
 for(unsigned variant=0;variant<4;++variant){auto p=base;
  if(variant==1){p.instructions[0].sources[0].modifier=Modifier::none;p.instructions[0].sources[1].literal.fill(as_bits(-.02f));}
  if(variant==2){p.instructions.clear();auto multiply=mad;multiply.opcode=Opcode::multiply;multiply.source_count=2;multiply.destination.kind=Register::temporary;multiply.sources[0].modifier=Modifier::none;p.instructions.push_back(multiply);
   Instruction subtract{};subtract.opcode=Opcode::add;subtract.source_count=2;subtract.destination=mad.destination;subtract.sources[0]=mad.sources[2];subtract.sources[1].kind=Register::temporary;subtract.sources[1].modifier=Modifier::negate;p.instructions.push_back(subtract);
  }
  if(variant==3){p.instructions[0].sources[0].modifier=Modifier::none;}
  const auto start=std::chrono::steady_clock::now();RegisterExecutor executor(p,count,"NPU");auto actual=executor.execute(inputs,{});check(actual.execution_device=="NPU"||actual.execution_device=="NPU.0");uint64_t differences=0,half_matches=0,oracle_equivalence=0;
  for(unsigned n=0;n<count;++n){auto reference=reference_vertex(p,inputs[n],{});auto original=reference_vertex(base,inputs[n],{});
   for(unsigned c=0;c<4;++c){if(variant!=3 && reference[0][c]!=original[0][c])++oracle_equivalence;
    const auto observed=actual.vertices[n][0][c];if(observed!=reference[0][c]){++differences;if(differences<=8)std::printf("{\"kind\":\"difference\",\"variant\":%u,\"input\":%u,\"actual\":%u,\"reference\":%u}\n",variant,inputs[n][0][c],observed,reference[0][c]);}
    volatile float product=as_float(inputs[n][0][c])*(variant==3?.0200042724609375f:-.0200042724609375f);if(observed==as_bits(product+1.0f))++half_matches;
   }
  }
  std::printf("{\"kind\":\"variant\",\"variant\":%u,\"execution_device\":\"%s\",\"components\":%u,\"different_components\":%llu,\"half_coefficient_matches\":%llu,\"cpu_rewrite_differences\":%llu,\"elapsed_ms\":%.3f}\n",variant,actual.execution_device.c_str(),count*4,(unsigned long long)differences,(unsigned long long)half_matches,(unsigned long long)oracle_equivalence,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());std::fflush(stdout);
 }
 return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
