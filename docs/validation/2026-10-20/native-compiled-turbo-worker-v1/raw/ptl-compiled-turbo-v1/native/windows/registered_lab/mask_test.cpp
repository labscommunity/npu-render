#include "vertex_executor.hpp"
#include "shader_reference.hpp"
#include "mask_fixture.hpp"
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
Operand literal(uint32_t value){auto a=reg(Register::immediate,0);a.literal.fill(value);return a;}
Instruction operation(Opcode op,Operand destination,std::initializer_list<Operand> sources){Instruction i;i.opcode=op;i.destination=destination;i.source_count=unsigned(sources.size());unsigned n=0;for(const auto &s:sources)i.sources[n++]=s;return i;}
void arithmetic(const std::string &backend){
 const uint32_t values[]{0,0x80000000u,1,0x80000001u,0x007fffffu,0x00800000u,0x3f800000u,0xbf800000u,0x3f800001u,0x7f7fffffu,0xff7fffffu,0x7f800000u,0xff800000u,0x7fc00000u,0xffffffffu,0x55555555u};
 std::vector<VertexRegisters> inputs(64);
 for(unsigned n=0;n<inputs.size();++n)for(unsigned c=0;c<4;++c){inputs[n][0][c]=values[(n+c)%16];inputs[n][1][c]=values[(n/4+c)%16];}
 for(const auto code:{Opcode::bitwise_and,Opcode::not_equal}){
  Program p;p.stage=Stage::pixel;p.inputs[0]=p.inputs[1]=p.outputs[0]=15;
  p.instructions={operation(code,reg(Register::output,0),{reg(Register::input,0),reg(Register::input,1)})};
  PixelExecutor executor(p,uint32_t(inputs.size()),backend);const auto result=executor.execute(inputs,{});TEST(result.execution_device.rfind(backend,0)==0);
  for(unsigned n=0;n<inputs.size();++n){const auto expected=reference_vertex(p,inputs[n],{});for(unsigned c=0;c<4;++c){
   if(result.vertices[n][0][c]!=expected[0][c])std::fprintf(stderr,"mask arithmetic opcode=%u row=%u lane=%u actual=%08x expected=%08x\n",unsigned(code),n,c,result.vertices[n][0][c],expected[0][c]);
   TEST(result.vertices[n][0][c]==expected[0][c]);}}
  std::printf("{\"opcode\":%u,\"exact_components\":256,\"execution_device\":\"%s\"}\n",unsigned(code),backend.c_str());std::fflush(stdout);
 }
 // Prove mask metadata survives MOV/swizzles, SELECT and branch joins, then
 // verify that an arbitrary bitwise result cannot masquerade as a float.
 Program p;p.stage=Stage::pixel;p.inputs[0]=p.inputs[1]=15;p.inputs[2]=1;p.outputs[0]=15;p.temporaries=3;
 auto condition=reg(Register::input,2);condition.lanes.fill(0);
 auto swizzled=reg(Register::temporary,0);swizzled.lanes={3,2,1,0};
 p.instructions={operation(Opcode::not_equal,reg(Register::temporary,0),{reg(Register::input,0),reg(Register::input,1)}),
  operation(Opcode::begin_if,{}, {condition}),operation(Opcode::move,reg(Register::temporary,1),{swizzled}),operation(Opcode::otherwise,{},{}),
  operation(Opcode::select,reg(Register::temporary,1),{condition,swizzled,reg(Register::temporary,0)}),operation(Opcode::end_if,{},{}),
  operation(Opcode::bitwise_and,reg(Register::temporary,2),{literal(as_bits(1.f)),reg(Register::temporary,1)}),
  operation(Opcode::multiply,reg(Register::output,0),{reg(Register::temporary,2),literal(as_bits(.25f))})};
 for(unsigned n=0;n<inputs.size();++n)inputs[n][2][0]=n%2;
 PixelExecutor masks(p,uint32_t(inputs.size()),backend);const auto result=masks.execute(inputs,{});TEST(result.execution_device.rfind(backend,0)==0);
 for(unsigned n=0;n<inputs.size();++n)TEST(result.vertices[n][0]==reference_vertex(p,inputs[n],{})[0]);
 auto invalid=p;invalid.instructions[1].sources[0]=condition;
 invalid.instructions[2].sources[0].modifier=Modifier::negate;
 bool rejected=false;try{RegisterExecutor::validate(invalid);}catch(const std::invalid_argument &){rejected=true;}TEST(rejected);
 invalid=p;invalid.instructions[0].opcode=Opcode::integer_add;
 rejected=false;try{RegisterExecutor::validate(invalid);}catch(const std::invalid_argument &){rejected=true;}TEST(rejected);
 std::printf("{\"predicate_flow_components\":256,\"unproven_float_views_rejected\":true,\"execution_device\":\"%s\"}\n",backend.c_str());std::fflush(stdout);
}
void captured(const std::string &backend){
 const auto &words=nr::registered_lab::test_fixture::mask_pixel_words;const auto p=parse_pixel(words,std::size(words));TEST(p.constants[0]==3);
 TextureShapes shapes{};TextureSamplers samplers{};TexturePixels textures{};
 for(unsigned slot=0;slot<2;++slot){shapes[slot]={4,4,15};textures[slot].resize(16);for(unsigned n=0;n<16;++n)textures[slot][n]={float(n%4)/4,float(n/4)/4,float(n)/16,float((n+slot*3)%16)/16};}
 const float coordinates[]{-1.f,0.f,.125f,.25f,.5f,.75f,1.f,2.f};std::vector<VertexRegisters> inputs(64);
 for(unsigned n=0;n<inputs.size();++n){inputs[n][1]={as_bits(1),as_bits(1),as_bits(1),as_bits(float(n%4)/4)};
  inputs[n][2]={as_bits(coordinates[n%8]*.5f),as_bits(coordinates[n/8]*.5f),as_bits(.5f),as_bits(1)};
  inputs[n][3]={as_bits(coordinates[7-n%8]*2),as_bits(coordinates[7-n/8]*2),as_bits(2),as_bits(1)};}
 VertexConstants constants;constants[0].resize(3);constants[0][0]={as_bits(0),as_bits(0),as_bits(1),as_bits(1)};constants[0][2]={as_bits(.125f),as_bits(.25f),as_bits(.75f),as_bits(1)};
 for(const auto filter:{TextureFilter::point,TextureFilter::linear}){
  samplers[0].filter=samplers[1].filter=filter;PixelExecutor executor(p,uint32_t(inputs.size()),backend,shapes,samplers);const auto result=executor.execute(inputs,constants,textures);TEST(result.execution_device.rfind(backend,0)==0);float error=0;
  for(unsigned n=0;n<inputs.size();++n){const auto expected=reference_vertex(p,inputs[n],constants,shapes,samplers,textures);for(unsigned c=0;c<4;++c){const auto d=std::abs(as_float(result.vertices[n][0][c])-as_float(expected[0][c]));error=(std::max)(error,d);TEST(d<=1e-6f);}}
  std::printf("{\"captured_shader_words\":223,\"filter\":%u,\"verified_components\":256,\"maximum_error\":%.9g,\"execution_device\":\"%s\"}\n",unsigned(filter),double(error),backend.c_str());std::fflush(stdout);
 }
}
void raster(const std::string &backend,const std::string &packet_path){
 using namespace nr::raster;namespace ex=nr::registered_lab::indexed_exchange;
 const auto vw=nr::registered_lab::shader_tokens("struct O{float4 p:SV_Position;float4 c:COLOR;float4 uv:TEXCOORD0;float4 mask:TEXCOORD1;};O main(float4 p:POSITION){O o;o.p=p;o.c=1;o.uv=float4(.5,.5,1,1);o.mask=o.uv;return o;}","vs_4_0");
 const auto vs=parse_vertex(vw.data(),vw.size());const auto &words=nr::registered_lab::test_fixture::mask_pixel_words;const auto ps=parse_pixel(words,std::size(words));
 ex::Request request;request.header.abi=8;request.header.width=request.header.height=32;request.header.vertices=6;request.header.format=87;
 request.header.flags=0x2f06;request.header.sequence=1;request.header.client_pid=42;request.header.nonce[0]=0x12345678;request.header.nonce[1]=0xabcdef;
 request.vertex=vs;request.pixel=ps;request.viewport={0,0,32,32,0,1};request.scissor=std::array<int32_t,4>{3,5,29,27};request.inputs.resize(6);
 const float xy[6][2]{{-1,1},{1,1},{1,-1},{1,-1},{-1,-1},{-1,1}};
 for(unsigned n=0;n<6;++n)request.inputs[n][0]={as_bits(xy[n][0]),as_bits(xy[n][1]),as_bits(.5f),as_bits(1)};
 const PackedColor color{64,128,192,255},mask{0,0,0,128},old{7,11,13,17},expected{32,64,96,127};request.resources[0]={1,1,87,{color}};request.resources[1]={1,1,87,{mask}};
 request.samplers[0].filter=request.samplers[1].filter=TextureFilter::linear;request.background.resize(1024,old);
 request.pixel_constants[0].resize(3);request.pixel_constants[0][0]=request.pixel_constants[0][2]={0,0,as_bits(1),as_bits(1)};
 TriangleTextureDrawExecutor executor(vs,ps,6,32,32,87,request.resources,request.samplers,backend,false,15,CullMode::none,false,1024);
 const auto result=executor.execute(request.inputs,{},request.pixel_constants,request.viewport,request.resources,request.background,request.scissor);
 TEST(result.execution_device.rfind(backend,0)==0 && result.covered_samples==26*22);
 for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x)TEST(result.pixels[y*32+x]==(x>=3 && x<29 && y>=5 && y<27?expected:old));
 if(!packet_path.empty()){
  const auto bytes=ex::encode(request);const auto decoded=ex::decode(bytes);TEST(decoded.pixel.bytecode==ps.bytecode && decoded.pixel_constants==request.pixel_constants);
  std::ifstream prior(packet_path,std::ios::binary),prior_pixels(packet_path+".pixels",std::ios::binary);TEST(!prior && !prior_pixels);prior.close();prior_pixels.close();
  std::ofstream packet(packet_path,std::ios::binary),pixels(packet_path+".pixels",std::ios::binary);TEST(bool(packet) && bool(pixels));
  packet.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));pixels.write(reinterpret_cast<const char*>(result.pixels.data()),std::streamsize(result.pixels.size()*4));
  packet.close();pixels.close();TEST(bool(packet) && bool(pixels));
 }
 std::printf("{\"captured_mask_raster_pixels\":1024,\"covered_samples\":572,\"execution_device\":\"%s\",\"desktop_offload_tested\":false}\n",backend.c_str());std::fflush(stdout);
}
}
int main(int argc,char **argv)try{TEST(argc<=3 || (argc==5 && !std::strcmp(argv[3],"--emit-packet")));const std::string backend=argc>=2?argv[1]:"CPU";const std::string section=argc>=3?argv[2]:"all";
 if(section=="all" || section=="arithmetic")arithmetic(backend);if(section=="all" || section=="captured")captured(backend);if(section=="all" || section=="raster")raster(backend,argc==5?argv[4]:"");return 0;
}catch(const std::exception&e){std::fprintf(stderr,"mask test: %s\n",e.what());return 1;}
