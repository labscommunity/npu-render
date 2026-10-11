#include "vertex_executor.hpp"
#include "provider_compile_statistics.hpp"
#include "shader_reference.hpp"
#include "loop_fixture.hpp"
#include <cstdio>
using namespace nr::shader;
#define TEST(v) do{if(!(v))throw std::runtime_error(#v);}while(false)
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
   for(unsigned c=0;c<4;++c){const auto error=std::abs(as_float(result.vertices[n][0][c])-as_float(expected[0][c]));max_error=(std::max)(max_error,error);std::printf("sample=%u lane=%u actual=%.9g expected=%.9g error=%.9g\n",n,c,double(as_float(result.vertices[n][0][c])),double(as_float(expected[0][c])),double(error));}
  }
  std::printf("{\"captured_shader_words\":1850,\"scenario\":%u,\"taps\":%d,\"verified_components\":16,\"maximum_error\":%.9g,\"execution_device\":\"%s\"}\n",scenario,taps,double(max_error),backend.c_str());std::fflush(stdout);TEST(max_error<=1e-5f);
 }
}
int main(int argc,char **argv)try{TEST(argc==2);captured(argv[1]);return 0;}catch(const std::exception &e){std::fprintf(stderr,"loop diagnostic: %s\n",e.what());return 1;}
