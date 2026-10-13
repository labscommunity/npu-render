#include "vertex_executor.hpp"
#include "shader_reference.hpp"
#include "shader_contract.hpp"
#include <cstdio>
#include <cmath>
using namespace nr::shader;
#define TEST(v) do{if(!(v))throw std::runtime_error(#v);}while(false)
int main(int argc,char **argv)try{
 TEST(argc==2);const std::string backend=argv[1];TEST(backend=="CPU" || backend=="NPU");
 const auto words=nr::registered_lab::shader_tokens("Texture2D image:register(t0);SamplerState filter:register(s0);float4 main(float2 uv:TEXCOORD):SV_Target{return image.Sample(filter,uv);}","ps_4_0");
 const auto p=parse_pixel(words.data(),words.size());TEST(p.inputs[0]==3);
 constexpr uint32_t width=2048,height=4096,count=32;
 TextureShapes shapes{};shapes[0]={width,height,15};TexturePixels textures{};textures[0].resize(size_t(width)*height);
 std::vector<VertexRegisters> inputs(count);
 // Exact binary coordinates include both sides of chunk/wrap boundaries,
 // the last texel, and outside each border. Every pixel has a distinct
 // non-power-of-two period so wrapped addresses cannot hide in the fixture.
 const float x[8]{-.25f/width,0,.5f/width,1.5f/width,.5f,1-.25f/width,1+.25f/width,1.25f};
 const float y[8]{.5f/height,1024.f/height,1024.5f/height,2048.f/height,2048.5f/height,1-.25f/height,1+.25f/height,-.25f/height};
 for(unsigned n=0;n<count;++n)inputs[n][0]={as_bits(x[n%8]),as_bits(y[(n/8+n)%8]),0,0};
 for(const auto filter:{TextureFilter::point,TextureFilter::linear})for(const auto mode:{TextureAddress::clamp,TextureAddress::mirror,TextureAddress::border}){
  TextureSamplers samplers{};samplers[0]={filter,mode,mode,{.25f,.5f,.75f,1.f}};
  PixelExecutor executor(p,count,backend,shapes,samplers);
  for(unsigned pass=0;pass<2;++pass){
   for(unsigned n=0;n<textures[0].size();++n)for(unsigned c=0;c<4;++c)textures[0][n][c]=float((n*17+c*29+pass*53)%251)/256;
   const auto result=executor.execute(inputs,{},textures);TEST(result.execution_device.rfind(backend,0)==0);float maximum=0;
   for(unsigned n=0;n<count;++n){const auto expected=reference_vertex(p,inputs[n],{},shapes,samplers,textures);for(unsigned c=0;c<4;++c){
    const auto actual=as_float(result.vertices[n][0][c]),wanted=as_float(expected[0][c]);const auto error=std::abs(actual-wanted);maximum=(std::max)(maximum,error);
    if(!std::isfinite(actual) || error>1e-6f || (filter==TextureFilter::point && actual!=wanted)){
     std::fprintf(stderr,"filter=%u mode=%u pass=%u row=%u lane=%u actual=%.9g expected=%.9g\n",unsigned(filter),unsigned(mode),pass,n,c,double(actual),double(wanted));TEST(false);
    }
   }}
   std::printf("{\"source_pixels\":%u,\"filter\":%u,\"mode\":%u,\"pass\":%u,\"verified_components\":%u,\"maximum_error\":%.9g,\"input_bytes\":%llu,\"execution_device\":\"%s\"}\n",width*height,unsigned(filter),unsigned(mode),pass,count*4,double(maximum),static_cast<unsigned long long>(result.input_bytes),result.execution_device.c_str());std::fflush(stdout);
  }
 }
 return 0;
}catch(const std::exception &e){std::fprintf(stderr,"large sampler: %s\n",e.what());return 1;}
