#define NOMINMAX
#include "vertex_executor.hpp"
#include "shader_contract.hpp"
#include <cstdio>
using namespace nr::shader;
#define CHECK(v) do{if(!(v))throw std::runtime_error(#v);}while(false)
uint32_t bits(float value){uint32_t output;std::memcpy(&output,&value,4);return output;}
float value(uint32_t input){float output;std::memcpy(&output,&input,4);return output;}
int main(int argc,char **argv)try{
 CHECK(argc==2);const std::string backend=argv[1];
 const auto code=nr::registered_lab::shader_tokens(R"(Texture2D<float4> image:register(t0);SamplerState sampler0:register(s0);cbuffer Params:register(b0){float4 scale;}float4 main(float2 uv:TEXCOORD0):SV_Target{return image.Sample(sampler0,uv)*scale;})","ps_4_0");
 const auto program=parse_pixel(code.data(),code.size());TextureShapes shapes{};shapes[0]={4,4,15};TextureSamplers samplers{};samplers[0].filter=TextureFilter::point;
 RegisterExecutor executor(program,4,backend,shapes,samplers);TexturePixels textures{};textures[0].resize(16);uint64_t checked=0,uploaded=0,reused=0;unsigned rejections=0;
 for(unsigned generation=0;generation<6;++generation){
  for(unsigned n=0;n<16;++n)for(unsigned c=0;c<4;++c)textures[0][n][c]=float((n+c+generation)%16)/16;
  std::vector<VertexRegisters> inputs(4);VertexConstants constants{};constants[0].resize(1);
  auto prepare=[&](unsigned row,float scale){for(unsigned n=0;n<4;++n){inputs[n][0]={bits((n+.5f)/4),bits((row+.5f)/4),0,0};for(unsigned c=0;c<4;++c)constants[0][0][c]=bits(scale);}};
  auto verify=[&](const RegisterExecutor::Result &result,unsigned row,float scale){CHECK(result.execution_device.rfind(backend,0)==0);for(unsigned n=0;n<4;++n)for(unsigned c=0;c<4;++c){CHECK(value(result.vertices[n][0][c])==textures[0][row*4+n][c]*scale);++checked;}uploaded+=result.texture_upload_bytes;reused+=result.texture_reused_bytes;};
  {
   auto batch=executor.bind_textures(textures);prepare(0,1);
   bool rejected=false;try{auto nested=executor.bind_textures(textures);}catch(const std::invalid_argument &){rejected=true;}CHECK(rejected);++rejections;
   rejected=false;try{(void)executor.execute(inputs,constants,textures);}catch(const std::invalid_argument &){rejected=true;}CHECK(rejected);++rejections;
   const auto first=batch.execute(inputs,constants);verify(first,0,1);CHECK(first.texture_upload_bytes>0 && !first.texture_reused_bytes);
   for(unsigned row=1;row<4;++row){prepare(row,.5f);const auto next=batch.execute(inputs,constants);verify(next,row,.5f);CHECK(!next.texture_upload_bytes && next.texture_reused_bytes==first.texture_upload_bytes);}
  }
  prepare(0,1);const auto ordinary=executor.execute(inputs,constants,textures);verify(ordinary,0,1);CHECK(ordinary.texture_upload_bytes>0 && !ordinary.texture_reused_bytes);
 }
 std::printf("{\"passed\":true,\"execution_device\":\"%s\",\"checked_components\":%llu,\"host_uploaded_bytes\":%llu,\"host_recopy_bytes_avoided\":%llu,\"invalid_scope_uses_rejected\":%u,\"resource_generations\":6,\"desktop_offloaded\":false}\n",backend.c_str(),static_cast<unsigned long long>(checked),static_cast<unsigned long long>(uploaded),static_cast<unsigned long long>(reused),rejections);return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
