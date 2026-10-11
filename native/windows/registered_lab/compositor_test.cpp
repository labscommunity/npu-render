#include "vertex_executor.hpp"
#include "shader_reference.hpp"
#include "compositor_fixture.hpp"
#include <fstream>
#include <cstdio>
#include <limits>
using namespace nr::shader;
#define TEST(v) do{if(!(v))throw std::runtime_error(#v);}while(false)
namespace {
Operand reg(Register kind,unsigned index){Operand a;a.kind=kind;a.index=index;a.mask=15;return a;}
Instruction operation(Opcode op,Operand destination,std::initializer_list<Operand> sources){Instruction i;i.opcode=op;i.destination=destination;i.source_count=unsigned(sources.size());unsigned n=0;for(const auto &s:sources)i.sources[n++]=s;return i;}
void arithmetic(const std::string &backend){
 const uint32_t values[]{0,0x80000000u,1,0x80000001u,0x007fffffu,0x807fffffu,0x00800000u,0x80800000u,
  0x3f800000u,0xbf800000u,0x3f800001u,0xbf800001u,0x7f7fffffu,0xff7fffffu,0x7f800000u,0xff800000u,
  0x7fc00000u,0xffffffffu,as_bits(-17.75f),as_bits(-.125f),as_bits(.125f),as_bits(.75f),as_bits(3.125f),as_bits(-3.125f),
  as_bits(8388607.5f),as_bits(-8388607.5f),as_bits(8388608.f),as_bits(-8388608.f),as_bits(16777215.f),as_bits(-16777215.f),as_bits(16777216.f),as_bits(-16777216.f)};
 std::vector<VertexRegisters> inputs(256);
 for(unsigned n=0;n<inputs.size();++n)for(unsigned c=0;c<4;++c){inputs[n][0][c]=values[(n*4+c)%32];inputs[n][1][c]=values[(n*4+c)/32];}
 for(const auto code:{Opcode::equal,Opcode::not_equal,Opcode::greater_equal,Opcode::floor})for(const auto modifier:{Modifier::none,Modifier::negate,Modifier::absolute,Modifier::absolute_negate}){
  Program p;p.stage=Stage::pixel;p.inputs[0]=p.inputs[1]=p.outputs[0]=15;
  auto a=reg(Register::input,0);a.modifier=modifier;
  p.instructions={code==Opcode::floor?operation(code,reg(Register::output,0),{a}):operation(code,reg(Register::output,0),{a,reg(Register::input,1)})};
  PixelExecutor executor(p,uint32_t(inputs.size()),backend);const auto result=executor.execute(inputs,{});TEST(result.execution_device.rfind(backend,0)==0);
  for(unsigned n=0;n<inputs.size();++n){const auto expected=reference_vertex(p,inputs[n],{});for(unsigned c=0;c<4;++c){
   const auto actual=result.vertices[n][0][c],want=expected[0][c];const bool equal=actual==want || (code==Opcode::floor && std::isnan(as_float(actual)) && std::isnan(as_float(want)));
   if(!equal)std::fprintf(stderr,"compositor arithmetic opcode=%u modifier=%u row=%u lane=%u actual=%08x expected=%08x\n",unsigned(code),unsigned(modifier),n,c,actual,want);TEST(equal);}}
  std::printf("{\"opcode\":%u,\"modifier\":%u,\"verified_components\":1024,\"signed_zero_checked\":true,\"execution_device\":\"%s\"}\n",unsigned(code),unsigned(modifier),backend.c_str());std::fflush(stdout);
 }
 // Saturation must clamp infinities and map NaN to zero without preserving
 // a stale integer view from MOV. Check the exact shader arithmetic path too.
 for(const auto code:{Opcode::move,Opcode::add,Opcode::mad}){
  Program p;p.stage=Stage::pixel;p.inputs[0]=p.inputs[1]=p.outputs[0]=15;
  auto zero=reg(Register::immediate,0);zero.literal.fill(as_bits(0));
  auto one=reg(Register::immediate,0);one.literal.fill(as_bits(1));
  auto i=code==Opcode::move?operation(code,reg(Register::output,0),{reg(Register::input,0)}):
   code==Opcode::add?operation(code,reg(Register::output,0),{reg(Register::input,0),zero}):
   operation(code,reg(Register::output,0),{reg(Register::input,0),one,zero});i.saturate=true;p.instructions={i};
  PixelExecutor executor(p,uint32_t(inputs.size()),backend);const auto result=executor.execute(inputs,{});
  TEST(result.execution_device.rfind(backend,0)==0);
  for(unsigned n=0;n<inputs.size();++n){const auto expected=reference_vertex(p,inputs[n],{});for(unsigned c=0;c<4;++c){
   const auto a=as_float(result.vertices[n][0][c]),b=as_float(expected[0][c]);
   if(!(std::isfinite(a) && a==b))std::fprintf(stderr,"saturation opcode=%u row=%u lane=%u actual=%08x expected=%08x\n",unsigned(code),n,c,result.vertices[n][0][c],expected[0][c]);
   TEST(std::isfinite(a) && a==b);}}
  std::printf("{\"saturated_opcode\":%u,\"verified_components\":1024,\"execution_device\":\"%s\"}\n",unsigned(code),backend.c_str());std::fflush(stdout);
 }
}
VertexConstants constants(unsigned scenario,bool fade=false){
 VertexConstants c;c[0].resize(6);
 auto f=[](float a,float b,float c,float d){return std::array<uint32_t,4>{as_bits(a),as_bits(b),as_bits(c),as_bits(d)};};
 c[0][0]=f(.125f,.25f,.375f,.5f);c[0][1]=f(.25f,.125f,.375f,.75f);c[0][2]=c[0][4]=f(0,0,1,1);c[0][5]=f(4,4,.25f,.25f);
 if(scenario==1)c[0][0]=f(0,0,0,0);
 if(scenario==2)c[0][1]=f(0,0,0,0);
 if(scenario==3)c[0][0]=c[0][1]=f(0,0,0,0);
 if(scenario==4)c[0][0]=c[0][1]=f(0,0,0,1);
 if(scenario==5)c[0][0]=c[0][1]=f(1,1,1,1);
 if(fade){const auto old=c[0];c[0].resize(8);c[0][1]=old[0];c[0][2]=old[1];
  for(unsigned n=2;n<6;++n)c[0][n+2]=old[n];
  c[0][0]=f(.0625f,.125f,.25f,.5f);c[0][3]=f(float(scenario)/5,0,0,0);}
 return c;
}
void captured(const std::string &backend,bool desktop_shapes=false,bool fade=false){
 using namespace nr::registered_lab::test_fixture;
 const auto p=fade?parse_pixel(compositor_fade_pixel_words,std::size(compositor_fade_pixel_words)):
  parse_pixel(compositor_pixel_words,std::size(compositor_pixel_words));
 TEST(p.constants[0]==(fade?8u:6u) && p.instructions.size()==(fade?113u:111u));
 TEST(RegisterExecutor::maximum_batch(p)==512);
 bool rejected=false;try{PixelExecutor oversized(p,1024,backend);}catch(const std::invalid_argument &){rejected=true;}TEST(rejected);
 TextureShapes shapes{};TextureSamplers samplers{};TexturePixels textures{};
 for(unsigned slot=0;slot<2;++slot){shapes[slot]=desktop_shapes?(slot?TextureShape{fade?768u:512u,fade?576u:384u,15}:TextureShape{fade?192u:128u,64,15}):TextureShape{4,4,15};
  textures[slot].resize(size_t(shapes[slot].width)*shapes[slot].height);for(size_t n=0;n<textures[slot].size();++n)
   textures[slot][n]={float(n%4)/8,float((n/4)%4)/8,float(n%16)/32,float((n+slot*3)%16)/16};}
 const float coordinates[]{-2.f,-.125f,0.f,.125f,.375f,.75f,1.f,2.f};std::vector<VertexRegisters> inputs(desktop_shapes?512:64);
 for(unsigned n=0;n<inputs.size();++n){inputs[n][1]={as_bits(1),as_bits(1),as_bits(1),as_bits(float(n%4)/4)};
  inputs[n][2]={as_bits(coordinates[n%8]*.5f),as_bits(coordinates[(n/8)%8]*.5f),as_bits(.5f),as_bits(1)};
  inputs[n][3]={as_bits(coordinates[7-n%8]*2),as_bits(coordinates[7-(n/8)%8]*2),as_bits(2),as_bits(1)};}
 for(const auto filter:{TextureFilter::point,TextureFilter::linear}){
  samplers[0].filter=samplers[1].filter=filter;PixelExecutor executor(p,uint32_t(inputs.size()),backend,shapes,samplers);
  for(unsigned scenario=0;scenario<6;++scenario){const auto cb=constants(scenario,fade);const auto result=executor.execute(inputs,cb,textures);TEST(result.execution_device.rfind(backend,0)==0);float error=0;
   for(unsigned n=0;n<inputs.size();++n){const auto expected=reference_vertex(p,inputs[n],cb,shapes,samplers,textures);for(unsigned c=0;c<4;++c){
    const auto a=as_float(result.vertices[n][0][c]),b=as_float(expected[0][c]);const auto d=std::abs(a-b);error=(std::max)(error,d);
    if(!(std::isfinite(a) && std::isfinite(b) && d<=1e-5f))std::fprintf(stderr,"captured filter=%u scenario=%u row=%u lane=%u actual=%.9g expected=%.9g\n",unsigned(filter),scenario,n,c,double(a),double(b));
    TEST(std::isfinite(a) && std::isfinite(b) && d<=1e-5f);}}
   std::printf("{\"captured_shader_words\":%u,\"desktop_texture_shapes\":%s,\"batch\":%u,\"filter\":%u,\"scenario\":%u,\"verified_components\":%u,\"maximum_error\":%.9g,\"execution_device\":\"%s\"}\n",unsigned(p.bytecode.size()),desktop_shapes?"true":"false",unsigned(inputs.size()),unsigned(filter),scenario,unsigned(inputs.size()*4),double(error),backend.c_str());std::fflush(stdout);
  }
 }
}
void three_textures(const std::string &backend){
 const auto &words=nr::registered_lab::test_fixture::compositor_three_texture_pixel_words;
 const auto program=parse_pixel(words,std::size(words));
 TEST(program.constants[0]==8 && program.inputs[4]==15 && program.textures[2] && RegisterExecutor::maximum_batch(program)==512);
 bool rejected=false;try{PixelExecutor oversized(program,1024,backend);}catch(const std::invalid_argument &){rejected=true;}TEST(rejected);
 TextureShapes shapes{};TextureSamplers samplers{};TexturePixels textures{};
 for(unsigned slot=0;slot<3;++slot){
  shapes[slot]=slot?TextureShape{768,576,15}:TextureShape{128,128,15};
  textures[slot].resize(size_t(shapes[slot].width)*shapes[slot].height);
  for(size_t n=0;n<textures[slot].size();++n)textures[slot][n]={float((n+slot)%7)/8,float((n/7+slot)%7)/8,float(n%11)/16,float((n+slot*3)%16)/16};
 }
 const float coordinates[]{-2.f,-.125f,0.f,.125f,.375f,.75f,1.f,2.f};
 std::vector<VertexRegisters> inputs(512);
 for(unsigned n=0;n<inputs.size();++n){
  inputs[n][1]={as_bits(1),as_bits(1),as_bits(1),as_bits(float(n%4)/4)};
  for(unsigned reg_index=2;reg_index<=4;++reg_index)
   inputs[n][reg_index]={as_bits(coordinates[(n+reg_index)%8]),as_bits(coordinates[(n/8+reg_index)%8]),as_bits(float(reg_index)*.5f),as_bits(1)};
 }
 const uint32_t captured_constants[8][4]{
  {1064234735,1064234735,1064234735,1065353216},{1062996279,1062996279,1062996279,1063708391},
  {0,0,0,0},{1065353216,0,0,0},{998244352,998244352,1057947648,1058209792},
  {1124073472,1124073472,1006632960,1006632960},{984263339,996380217,1051415894,1055216981},
  {1145044992,1141899264,984263339,987991609}};
 for(const auto filter:{TextureFilter::point,TextureFilter::linear}){
  for(unsigned slot=0;slot<3;++slot)samplers[slot].filter=filter;
  PixelExecutor executor(program,uint32_t(inputs.size()),backend,shapes,samplers);
  for(unsigned scenario=0;scenario<6;++scenario){
   VertexConstants cb;cb[0].resize(8);std::memcpy(cb[0].data(),captured_constants,sizeof(captured_constants));
   if(scenario)for(unsigned c=0;c<4;++c){cb[0][0][c]=as_bits(float(scenario-1)/4);cb[0][1][c]=as_bits(float(5-scenario)/4);}
   const auto result=executor.execute(inputs,cb,textures);TEST(result.execution_device.rfind(backend,0)==0);float maximum=0;
   for(unsigned n=0;n<inputs.size();++n){const auto expected=reference_vertex(program,inputs[n],cb,shapes,samplers,textures);
    for(unsigned c=0;c<4;++c){const auto actual=as_float(result.vertices[n][0][c]),want=as_float(expected[0][c]);
     maximum=(std::max)(maximum,std::abs(actual-want));TEST(std::isfinite(actual) && std::isfinite(want) && std::abs(actual-want)<=1e-5f);}}
   std::printf("{\"captured_shader_words\":1061,\"textures\":3,\"batch\":512,\"filter\":%u,\"scenario\":%u,\"verified_components\":2048,\"maximum_error\":%.9g,\"execution_device\":\"%s\"}\n",
    unsigned(filter),scenario,double(maximum),backend.c_str());std::fflush(stdout);
  }
 }
}
}
int main(int argc,char **argv)try{TEST(argc<=3);const std::string backend=argc>=2?argv[1]:"CPU";const std::string section=argc>=3?argv[2]:"all";
 if(section=="all" || section=="arithmetic")arithmetic(backend);if(section=="all" || section=="captured")captured(backend);
 if(section=="all" || section=="desktop")captured(backend,true);
 if(section=="all" || section=="fade")captured(backend,true,true);
 if(section=="all" || section=="three")three_textures(backend);return 0;
}catch(const std::exception&e){std::fprintf(stderr,"compositor test: %s\n",e.what());return 1;}
