#include "vertex_executor.hpp"
#include "provider_compile_statistics.hpp"
#include "shader_reference.hpp"
#include "loop_fixture.hpp"
#include "triangle_texture_draw.hpp"
#include "shader_contract.hpp"
#include "indexed_exchange_protocol.hpp"
#include <fstream>
#include <cstdio>
#include <limits>
using namespace nr::shader;
#define TEST(v) do{if(!(v))throw std::runtime_error(#v);}while(false)
namespace {
Operand reg(Register kind,unsigned index){Operand a;a.kind=kind;a.index=index;a.mask=15;return a;}
Operand scalar(Register kind,unsigned index,unsigned lane=0){auto a=reg(kind,index);a.lanes.fill(lane);return a;}
Operand literal(uint32_t value){auto a=reg(Register::immediate,0);a.literal.fill(value);return a;}
Instruction operation(Opcode op,Operand destination,std::initializer_list<Operand> sources){Instruction i;i.opcode=op;i.destination=destination;i.source_count=unsigned(sources.size());unsigned n=0;for(const auto &s:sources)i.sources[n++]=s;return i;}
Program loop_program(){
 Program p;p.stage=Stage::pixel;p.inputs[0]=1;p.inputs[1]=15;p.outputs[0]=15;p.temporaries=3;
 auto counter=reg(Register::temporary,0);counter.mask=1;auto condition=reg(Register::temporary,2);condition.mask=1;
 p.instructions={operation(Opcode::move,counter,{literal(0)}),operation(Opcode::move,reg(Register::temporary,1),{literal(0)}),
 operation(Opcode::begin_loop,{},{}),operation(Opcode::integer_greater_equal,condition,{scalar(Register::temporary,0),scalar(Register::input,0)}),
 operation(Opcode::break_if,{}, {scalar(Register::temporary,2)}),operation(Opcode::add,reg(Register::temporary,1),{reg(Register::temporary,1),reg(Register::input,1)}),
 operation(Opcode::integer_add,counter,{scalar(Register::temporary,0),literal(1)}),operation(Opcode::end_loop,{},{}),operation(Opcode::move,reg(Register::output,0),{reg(Register::temporary,1)})};
 return p;
}
void loops(const std::string &backend){
 const auto p=loop_program();std::vector<VertexRegisters> input(32);
 for(unsigned i=0;i<input.size();++i){input[i][0][0]=i%17;input[i][1]={as_bits(.125f),as_bits(.25f),as_bits(.5f),as_bits(1.f)};}
 PixelExecutor executor(p,uint32_t(input.size()),backend);const auto result=executor.execute(input,{});TEST(result.execution_device.rfind(backend,0)==0);
 for(unsigned i=0;i<input.size();++i){TEST(result.vertices[i][0]==reference_vertex(p,input[i],{})[0]);for(unsigned c=0;c<4;++c)TEST(as_float(result.vertices[i][0][c])==float(i%17)*as_float(input[i][1][c]));}
 input[3][0][0]=17;bool rejected=false;try{(void)executor.execute(input,{});}catch(const std::runtime_error &){rejected=true;}TEST(rejected);
 rejected=false;try{(void)reference_vertex(p,input[3],{});}catch(const std::runtime_error &){rejected=true;}TEST(rejected);
 std::printf("{\"bounded_loop_components\":128,\"divergent_counts_zero_through_sixteen\":true,\"overflow_rejected\":true,\"execution_device\":\"%s\"}\n",backend.c_str());std::fflush(stdout);
}
void arithmetic(const std::string &backend){
 for(const auto code:{Opcode::fraction,Opcode::greater_equal,Opcode::integer_greater_equal}){
  Program p;p.stage=Stage::pixel;p.inputs[0]=15;p.inputs[1]=15;p.outputs[0]=15;
  auto i=operation(code,reg(Register::output,0),{reg(Register::input,0),reg(Register::input,1)});if(code==Opcode::fraction)i.source_count=1;p.instructions.push_back(i);
  std::vector<VertexRegisters> input(64);
  const float values[]{-17.75f,-3.f,-.125f,-0.f,0.f,.125f,.75f,1.f,3.125f,8388607.5f,8388608.f,3.4028234663852886e38f,std::numeric_limits<float>::denorm_min(),-std::numeric_limits<float>::denorm_min(),std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()};
  const uint32_t integers[]{0,1,0x7fffffffu,0x80000000u,0xffffffffu,0x00ffffffu,0x01000000u,0x01000001u};
  for(unsigned n=0;n<input.size();++n)for(unsigned c=0;c<4;++c){input[n][0][c]=code==Opcode::integer_greater_equal?integers[(n+c)%8]:as_bits(values[(n+c)%16]);input[n][1][c]=code==Opcode::integer_greater_equal?integers[(n/8+c)%8]:as_bits(values[(n/4+c)%16]);}
  PixelExecutor executor(p,uint32_t(input.size()),backend);const auto result=executor.execute(input,{});TEST(result.execution_device.rfind(backend,0)==0);
  for(unsigned n=0;n<input.size();++n){const auto expected=reference_vertex(p,input[n],{});
   for(unsigned c=0;c<4;++c){const auto actual=result.vertices[n][0][c],want=expected[0][c];const bool equal=actual==want || (code==Opcode::fraction && std::isnan(as_float(actual)) && std::isnan(as_float(want)));
    if(!equal)std::fprintf(stderr,"arithmetic opcode=%u sample=%u component=%u actual=%08x expected=%08x\n",unsigned(code),n,c,actual,want);TEST(equal);}
  }
  std::printf("{\"opcode\":%u,\"exact_components\":256,\"execution_device\":\"%s\"}\n",unsigned(code),backend.c_str());std::fflush(stdout);
 }
}
void captured(const std::string &backend){
 const auto &words=nr::registered_lab::test_fixture::loop_pixel_words;const auto p=parse_pixel(words,std::size(words));
 TEST(p.constants[0]==130 && lower_bounded_loops(p).instructions.size()==3395);
 TextureShapes shapes{};shapes[0]={4,4,15};TextureSamplers samplers{};samplers[0].filter=TextureFilter::linear;TexturePixels textures{};textures[0].resize(16);
 for(unsigned n=0;n<16;++n)textures[0][n]={float(n%4)/4,float(n/4)/4,float(n)/16,1};
 std::vector<VertexRegisters> input(4);for(unsigned n=0;n<4;++n){input[n][1]={as_bits(1),as_bits(1),as_bits(1),as_bits(1)};input[n][2]={as_bits((float(n)+.5f)/4),as_bits(.375f),as_bits(1),as_bits(1)};}
 const auto before=nr::provider::compile_statistics();
 PixelExecutor executor(p,uint32_t(input.size()),backend,shapes,samplers);
 TEST(nr::provider::compile_statistics().calls==before.calls);bool first_result=true;
 for(unsigned scenario=0;scenario<3;++scenario)for(int32_t taps:{9,-1,0,1,2,3,4,5,7,8,9,16,17,31,32,33,64,65,127,128,9,1,128,0}){
  VertexConstants constants;constants[0].resize(130);
  for(unsigned n=0;n<128;++n)constants[0][n]={as_bits(float(n)/16),as_bits(float(n)/32),as_bits(taps>0?1.f/(2*taps):0.f),0};
  if(scenario){
   for(unsigned n=0;n<128;++n)constants[0][n]={as_bits(float(int(n%13)-6)*.37f),as_bits(float(int(n%17)-8)*.21f),as_bits(float(int(n%7)-3)*.0078125f),0};
   for(unsigned n=0;n<input.size();++n)input[n][2]={as_bits((float(n)-2.25f)*17),as_bits((float(n)-1.5f)*9),as_bits(scenario==1?1.f:.375f),as_bits(1)};
  }else for(unsigned n=0;n<input.size();++n)input[n][2]={as_bits((float(n)+.5f)/4),as_bits(.375f),as_bits(1),as_bits(1)};
  constants[0][0][3]=taps;constants[0][128]={as_bits(0),as_bits(0),as_bits(1),as_bits(1)};constants[0][129]={0,0,as_bits(.25f),as_bits(.25f)};
  if(taps==9){
   constants[0][0][3]=129;bool rejected=false;
   try{(void)executor.execute(input,constants,textures);}catch(const std::invalid_argument &){rejected=true;}
   TEST(rejected);constants[0][0][3]=uint32_t(taps);
  }
  const auto result=executor.execute(input,constants,textures);
  if(first_result){TEST(nr::provider::compile_statistics().calls==before.calls+1);first_result=false;}
  TEST(result.execution_device.rfind(backend,0)==0);float max_error=0;
  for(unsigned n=0;n<input.size();++n){const auto expected=reference_vertex(p,input[n],constants,shapes,samplers,textures);
   for(unsigned c=0;c<4;++c){const auto error=std::abs(as_float(result.vertices[n][0][c])-as_float(expected[0][c]));max_error=(std::max)(max_error,error);TEST(error<=1e-5f);}
  }
  std::printf("{\"captured_shader_words\":1850,\"scenario\":%u,\"taps\":%d,\"verified_components\":16,\"maximum_error\":%.9g,\"execution_device\":\"%s\"}\n",scenario,taps,double(max_error),backend.c_str());std::fflush(stdout);
 }
}
void raster(const std::string &backend,const std::string &packet_path){
 using namespace nr::raster;namespace ex=nr::registered_lab::indexed_exchange;
 const auto vw=nr::registered_lab::shader_tokens("struct O{float4 p:SV_Position;float4 c:COLOR;float4 uv:TEXCOORD;};O main(float4 p:POSITION){O o;o.p=p;o.c=1;o.uv=float4(.5,.5,1,1);return o;}","vs_4_0");
 const auto vs=parse_vertex(vw.data(),vw.size());const auto &words=nr::registered_lab::test_fixture::loop_pixel_words;const auto ps=parse_pixel(words,std::size(words));
 TEST(RegisterExecutor::maximum_batch(ps)==256);
 ex::Request request;request.header.abi=8;request.header.width=request.header.height=32;request.header.vertices=6;request.header.format=87;
 request.header.flags=0x2f06;request.header.sequence=1;request.header.client_pid=42;request.header.nonce[0]=0x12345678;request.header.nonce[1]=0xabcdef;
 request.vertex=vs;request.pixel=ps;request.viewport={0,0,32,32,0,1};request.scissor=std::array<int32_t,4>{3,5,29,27};request.inputs.resize(6);
 const float xy[6][2]{{-1,1},{1,1},{1,-1},{1,-1},{-1,-1},{-1,1}};
 for(unsigned n=0;n<6;++n)request.inputs[n][0]={as_bits(xy[n][0]),as_bits(xy[n][1]),as_bits(.5f),as_bits(1)};
 const PackedColor color{37,91,153,255},old{7,11,13,17};request.resources[0]={1,1,87,{color}};request.samplers[0].filter=TextureFilter::linear;request.background.resize(1024,old);
 request.pixel_constants[0].resize(130);for(unsigned n=0;n<128;++n)request.pixel_constants[0][n]={as_bits(float(n)),as_bits(-float(n)),as_bits(1.f/18),0};
 request.pixel_constants[0][0][3]=9;request.pixel_constants[0][128]={0,0,as_bits(1),as_bits(1)};request.pixel_constants[0][129]={0,0,as_bits(.25f),as_bits(.25f)};
 TriangleTextureDrawExecutor executor(vs,ps,6,32,32,87,request.resources,request.samplers,backend,false,15,CullMode::none,false,1024);
 const auto result=executor.execute(request.inputs,{},request.pixel_constants,request.viewport,request.resources,request.background,request.scissor);
 TEST(result.execution_device.rfind(backend,0)==0 && result.covered_samples==26*22);
 for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x)TEST(result.pixels[y*32+x]==(x>=3 && x<29 && y>=5 && y<27?color:old));
 if(!packet_path.empty()){
  const auto bytes=ex::encode(request);const auto decoded=ex::decode(bytes);TEST(decoded.pixel.bytecode==ps.bytecode && decoded.pixel_constants==request.pixel_constants);
  std::ifstream prior(packet_path,std::ios::binary),prior_pixels(packet_path+".pixels",std::ios::binary);TEST(!prior && !prior_pixels);prior.close();prior_pixels.close();
  std::ofstream packet(packet_path,std::ios::binary),pixels(packet_path+".pixels",std::ios::binary);TEST(bool(packet) && bool(pixels));
  packet.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));pixels.write(reinterpret_cast<const char*>(result.pixels.data()),std::streamsize(result.pixels.size()*4));
  packet.close();pixels.close();TEST(bool(packet) && bool(pixels));
 }
 std::printf("{\"captured_loop_raster_pixels\":1024,\"covered_samples\":572,\"requested_batch\":1024,\"bounded_batch\":256,\"execution_device\":\"%s\",\"desktop_offload_tested\":false}\n",backend.c_str());std::fflush(stdout);
}
}
int main(int argc,char **argv)try{
 if(!(argc<=3 || (argc==5 && !std::strcmp(argv[3],"--emit-packet"))))throw std::invalid_argument("invalid loop test arguments");
 const std::string backend=argc>=2?argv[1]:"CPU";const std::string section=argc>=3?argv[2]:"all";
 if(backend!="CPU" && backend!="GPU" && backend!="NPU")throw std::invalid_argument("invalid loop test backend");
 if(section!="all" && section!="loops" && section!="arithmetic" && section!="captured" && section!="raster")throw std::invalid_argument("invalid loop test section");
 if(argc==5 && ((section!="all" && section!="raster") || !argv[4][0]))throw std::invalid_argument("invalid loop test packet output");
 if(section=="all" || section=="loops")loops(backend);if(section=="all" || section=="arithmetic")arithmetic(backend);if(section=="all" || section=="captured")captured(backend);if(section=="all" || section=="raster")raster(backend,argc==5?argv[4]:"");return 0;
}catch(const std::exception&e){std::fprintf(stderr,"loop test: %s\n",e.what());return 1;}
