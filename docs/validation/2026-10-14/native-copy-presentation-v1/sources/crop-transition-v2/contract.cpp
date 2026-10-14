#define NOMINMAX
#include "provider_compile.hpp"
#include "linear_texture_draw.hpp"
#include "point_texture_fixture.hpp"
#include "shader_contract.hpp"
#include <cstdio>
using namespace nr::raster;using namespace nr::shader;
#define CHECK(v) do{if(!(v))throw std::runtime_error(#v);}while(false)
uint32_t as_bits(float value){uint32_t result;std::memcpy(&result,&value,4);return result;}
int main(int argc,char **argv)try{
 CHECK(argc==2);const std::string backend=argv[1];uint64_t checked=0;
 const auto words=nr::registered_lab::shader_tokens(R"(struct O{float4 p:SV_Position;float4 c:COLOR0;float2 uv:TEXCOORD0;};O main(float4 p:POSITION,float4 c:COLOR0,float2 uv:TEXCOORD0){O o;o.p=p;o.c=c;o.uv=uv;return o;})","vs_4_0");
 const auto vs=parse_vertex(words.data(),words.size()),ps=parse_pixel(nr::registered_lab::test_fixture::point_texture_pixel_words,std::size(nr::registered_lab::test_fixture::point_texture_pixel_words));
 for(const uint32_t format:{28u,87u})for(const auto filter:{TextureFilter::linear,TextureFilter::point}){
  constexpr uint32_t w=64,h=48;LinearTextureDrawExecutor executor(vs,ps,w,h,format,w,h,format,backend,true,filter);
  PackedTexture source{w,h,format,std::vector<PackedColor>(w*h)};std::vector<PackedColor> background(w*h);const Viewport viewport{0,0,w,h,0,1};
  for(unsigned frame=0;frame<30;++frame){
   const bool crop=frame%3!=1;const float modulation=crop?1.f:.5f;
   for(size_t n=0;n<w*h;++n){for(unsigned c=0;c<3;++c){source.pixels[n][c]=uint8_t((n*31+frame*17+c*37)&254);background[n][c]=uint8_t((n*13+frame*23+c*41)&254);}source.pixels[n][3]=uint8_t((n+frame)&255);background[n][3]=255;}
   const unsigned order[]{0,1,2,2,3,0};std::vector<VertexRegisters> vertices(6);
   for(unsigned n=0;n<6;++n){const auto corner=order[n];const bool right=corner==1||corner==2,bottom=corner>=2;vertices[n][0]={as_bits(right?1.f:-1.f),as_bits(bottom?-1.f:1.f),as_bits(.5f),as_bits(1.f)};for(unsigned c=0;c<4;++c)vertices[n][1][c]=as_bits(modulation);vertices[n][2]={as_bits(right?1.f:0.f),as_bits(bottom?1.f:0.f),0,0};}
   const auto before=nr::provider::compile_statistics();const auto result=executor.execute(vertices,{},viewport,source,background);const auto after=nr::provider::compile_statistics();
   CHECK(result.execution_device.rfind(backend,0)==0 && result.covered_samples==w*h);
   CHECK(after.calls-before.calls==(frame<2?1u:0u));
   for(size_t n=0;n<w*h;++n)for(unsigned c=0;c<4;++c){const auto expected=c==3?255u:crop?unsigned(source.pixels[n][c]):(unsigned(source.pixels[n][c])+background[n][c])/2;CHECK(result.pixels[n][c]==expected);++checked;}
  }
 }
 std::printf("{\"passed\":true,\"execution_device\":\"%s\",\"checked_components\":%llu,\"draws\":120,\"general_model_prepared_only_on_demand\":true,\"repeated_crop_general_transitions\":true,\"desktop_offloaded\":false}\n",backend.c_str(),static_cast<unsigned long long>(checked));return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
