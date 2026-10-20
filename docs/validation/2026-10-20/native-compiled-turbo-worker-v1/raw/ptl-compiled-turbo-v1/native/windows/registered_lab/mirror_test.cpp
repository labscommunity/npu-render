#include "vertex_executor.hpp"
#include "shader_reference.hpp"
#include "shader_contract.hpp"
#include "indexed_exchange_protocol.hpp"
#include <fstream>
#include <cstdio>
#include <limits>
using namespace nr::shader;
#define TEST(v) do{if(!(v))throw std::runtime_error(#v);}while(false)
namespace {
void samples(const std::string &backend){
 const auto words=nr::registered_lab::shader_tokens("Texture2D image:register(t0);SamplerState filter:register(s0);float4 main(float2 uv:TEXCOORD):SV_Target{return image.Sample(filter,uv);}","ps_4_0");
 const auto p=parse_pixel(words.data(),words.size());
 const float limit=(std::numeric_limits<float>::max)();
 const float coordinates[]{-limit,-16777216,-8388607.5f,-1.99999988f,-3,-2.125f,-2,-1.875f,-1.25f,-1,-.875f,-.5f,-.25f,-.125f,-.00001f,-0.f,0,.00001f,.125f,.25f,.5f,.875f,1,1.25f,1.875f,2,2.125f,3,1.99999988f,8388607.5f,16777216,limit};
 std::vector<VertexRegisters> inputs(1024);
 for(unsigned n=0;n<inputs.size();++n)inputs[n][0]={as_bits(coordinates[n%32]),as_bits(coordinates[n/32]),0,0};
 for(const TextureShape shape:{TextureShape{4,8,15},TextureShape{17,13,15},TextureShape{1,1,15}})
 for(const auto filter:{TextureFilter::point,TextureFilter::linear})for(unsigned mode=0;mode<4;++mode){
  TextureShapes shapes{};shapes[0]=shape;TextureSamplers samplers{};samplers[0]={filter,mode&1?TextureAddress::mirror:TextureAddress::clamp,mode&2?TextureAddress::mirror:TextureAddress::clamp};
  TexturePixels textures{};textures[0].resize(size_t(shape.width)*shape.height);PixelExecutor executor(p,uint32_t(inputs.size()),backend,shapes,samplers);
  for(unsigned pass=0;pass<2;++pass){
   for(unsigned n=0;n<textures[0].size();++n)for(unsigned c=0;c<4;++c)textures[0][n][c]=float((n*37+c*19+pass*53)%256)/256;
   const auto result=executor.execute(inputs,{},textures);TEST(result.execution_device.rfind(backend,0)==0);float maximum=0;
   for(unsigned n=0;n<inputs.size();++n){const auto expected=reference_vertex(p,inputs[n],{},shapes,samplers,textures);for(unsigned c=0;c<4;++c){
    const auto actual=as_float(result.vertices[n][0][c]),wanted=as_float(expected[0][c]);const auto error=std::abs(actual-wanted);maximum=(std::max)(maximum,error);
    if(!std::isfinite(actual) || error>1e-6f || (filter==TextureFilter::point && actual!=wanted)){
     std::fprintf(stderr,"mirror filter=%u mode=%u pass=%u row=%u lane=%u actual=%.9g expected=%.9g\n",unsigned(filter),mode,pass,n,c,double(actual),double(wanted));TEST(false);
    }
   }}
   std::printf("{\"width\":%u,\"height\":%u,\"address_mode_bits\":%u,\"filter\":%u,\"pass\":%u,\"verified_components\":4096,\"maximum_error\":%.9g,\"execution_device\":\"%s\"}\n",shape.width,shape.height,mode,unsigned(filter),pass,double(maximum),result.execution_device.c_str());std::fflush(stdout);
  }
  auto invalid=inputs;invalid[3][0][0]=as_bits(std::numeric_limits<float>::quiet_NaN());
  invalid[7][0][1]=as_bits(std::numeric_limits<float>::infinity());bool rejected=false;
  try{(void)executor.execute(invalid,{},textures);}catch(const std::runtime_error &){rejected=true;}TEST(rejected);
  std::printf("{\"address_mode_bits\":%u,\"filter\":%u,\"active_nonfinite_rejected\":true,\"execution_device\":\"%s\"}\n",mode,unsigned(filter),backend.c_str());std::fflush(stdout);
 }
}
void raster(const std::string &backend,const std::string &packet_path){
 using namespace nr::raster;namespace ex=nr::registered_lab::indexed_exchange;
 const auto vw=nr::registered_lab::shader_tokens("struct O{float4 p:SV_Position;float2 uv:TEXCOORD;};O main(float4 p:POSITION,float2 uv:TEXCOORD){O o;o.p=p;o.uv=uv;return o;}","vs_4_0");
 const auto pw=nr::registered_lab::shader_tokens("Texture2D image:register(t0);SamplerState filter:register(s0);float4 main(float4 p:SV_Position,float2 uv:TEXCOORD):SV_Target{return image.Sample(filter,uv);}","ps_4_0");
 ex::Request r;r.header.abi=12;r.header.width=r.header.height=32;r.header.vertices=6;r.header.format=87;r.header.flags=0x2f06;r.header.sequence=1;r.header.client_pid=42;r.header.nonce[0]=0x12345678;r.header.nonce[1]=0xabcdef;
 r.vertex=parse_vertex(vw.data(),vw.size());r.pixel=parse_pixel(pw.data(),pw.size());r.viewport={0,0,32,32,0,1};r.scissor=std::array<int32_t,4>{3,5,29,27};r.inputs.resize(6);
 const float xy[6][2]{{-1,1},{1,1},{1,-1},{1,-1},{-1,-1},{-1,1}};
 for(unsigned n=0;n<6;++n){r.inputs[n][0]={as_bits(xy[n][0]),as_bits(xy[n][1]),as_bits(.5f),as_bits(1)};r.inputs[n][1]={as_bits(xy[n][0]*2),as_bits(-xy[n][1]*2),0,0};}
 const PackedColor old{7,11,13,17};r.resources[0]={4,4,87,{}};r.resources[0].pixels.resize(16);r.background.resize(1024,old);
 for(unsigned n=0;n<16;++n)r.resources[0].pixels[n]={uint8_t(n*16),uint8_t((15-n)*16),uint8_t((n%4)*64),255};
 for(const auto filter:{TextureFilter::linear,TextureFilter::point}){
  r.samplers[0]={filter,TextureAddress::mirror,TextureAddress::mirror};TriangleTextureDrawExecutor executor(r.vertex,r.pixel,6,32,32,87,r.resources,r.samplers,backend,false,15,CullMode::none,false,1024);
  const auto result=executor.execute(r.inputs,{}, {},r.viewport,r.resources,r.background,r.scissor);TEST(result.execution_device.rfind(backend,0)==0 && result.covered_samples==572);
  const auto shapes=resource_shapes(r.pixel,r.resources);const auto textures=decode_resources(r.pixel,r.resources);
  for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){PackedColor expected=old;
   if(x>=3 && x<29 && y>=5 && y<27){VertexRegisters fragment{};fragment[1]={as_bits(-2+(float(x)+.5f)/8),as_bits(-2+(float(y)+.5f)/8),0,0};
    // Linkage register is obtained from the compiled PS, not assumed from HLSL names.
    for(unsigned reg=0;reg<16;++reg)if(r.pixel.inputs[reg])fragment[reg]=fragment[1];
    const auto sample=reference_vertex(r.pixel,fragment,{},shapes,r.samplers,textures)[0];
    for(unsigned c=0;c<4;++c)expected[c]=uint8_t(std::nearbyint(as_float(sample[c<3?2-c:c])*255));
   }TEST(result.pixels[y*32+x]==expected);
  }
  if(filter==TextureFilter::point && !packet_path.empty()){
   const auto bytes=ex::encode(r);const auto decoded=ex::decode(bytes);TEST(sampler_layout_equal(decoded.samplers,r.samplers));
   std::ifstream prior(packet_path,std::ios::binary),prior_pixels(packet_path+".pixels",std::ios::binary);TEST(!prior && !prior_pixels);prior.close();prior_pixels.close();
   std::ofstream packet(packet_path,std::ios::binary),pixels(packet_path+".pixels",std::ios::binary);TEST(bool(packet) && bool(pixels));
   packet.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));pixels.write(reinterpret_cast<const char*>(result.pixels.data()),std::streamsize(result.pixels.size()*4));packet.close();pixels.close();TEST(bool(packet) && bool(pixels));
  }
  std::printf("{\"mirrored_raster_pixels\":1024,\"filter\":%u,\"covered_samples\":572,\"execution_device\":\"%s\",\"desktop_offload_tested\":false}\n",unsigned(filter),result.execution_device.c_str());std::fflush(stdout);
 }
}
}
int main(int argc,char **argv)try{TEST(argc<=3 || (argc==5 && !std::strcmp(argv[3],"--emit-packet")));const std::string backend=argc>=2?argv[1]:"CPU",section=argc>=3?argv[2]:"all";
 TEST(section=="all" || section=="samples" || section=="raster");if(section!="raster")samples(backend);if(section!="samples")raster(backend,argc==5?argv[4]:"");return 0;
}catch(const std::exception &e){std::fprintf(stderr,"mirror test: %s\n",e.what());return 1;}
