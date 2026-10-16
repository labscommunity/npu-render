#include "shader_ir.hpp"
#include "captured.hpp"
#include <cstdio>
#include <iterator>
using namespace nr::shader;
void operand(const Operand &o){
 std::printf("{\"kind\":%u,\"modifier\":%u,\"index\":%u,\"element\":%u,\"mask\":%u,\"lanes\":[%u,%u,%u,%u],\"literal\":[%u,%u,%u,%u],\"relative\":%s}",unsigned(o.kind),unsigned(o.modifier),o.index,o.element,o.mask,o.lanes[0],o.lanes[1],o.lanes[2],o.lanes[3],o.literal[0],o.literal[1],o.literal[2],o.literal[3],o.relative.enabled?"true":"false");
}
void program(const Program &p){
 std::printf("{\"temporaries\":%u,\"inputs\":[",p.temporaries);for(unsigned n=0;n<16;++n)std::printf("%s%u",n?",":"",p.inputs[n]);std::printf("],\"interpolation\":[");for(unsigned n=0;n<16;++n)std::printf("%s%u",n?",":"",p.interpolation[n]);std::printf("],\"instructions\":[");
 for(size_t n=0;n<p.instructions.size();++n){const auto &i=p.instructions[n];std::printf("%s{\"opcode\":%u,\"saturate\":%s,\"destination\":",n?",":"",unsigned(i.opcode),i.saturate?"true":"false");operand(i.destination);std::printf(",\"sources\":[");for(unsigned k=0;k<i.source_count;++k){if(k)std::printf(",");operand(i.sources[k]);}std::printf("]}");}std::printf("]}");
}
int main(){std::printf("{\"vertex\":");program(parse_program(vertex,std::size(vertex),Stage::vertex));std::printf(",\"pixel\":");program(parse_program(pixel,std::size(pixel),Stage::pixel));std::printf("}\n");}
