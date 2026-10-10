#include "vertex_executor.hpp"
#include "shader_reference.hpp"
#include <fstream>
#include <cstdio>
#include <limits>
using namespace nr::shader;
#define TEST(v) do{if(!(v))throw std::runtime_error(#v);}while(false)
namespace {
void samples(const std::string &backend){
 Program p;p.stage=Stage::pixel;p.inputs[0]=3;p.outputs[0]=15;p.textures[0]=p.samplers[0]=true;p.texture_dimensions[0]=3;
 Instruction instruction;instruction.opcode=Opcode::sample;instruction.destination.kind=Register::output;instruction.destination.index=0;instruction.destination.mask=15;instruction.source_count=3;
 instruction.sources[0].kind=Register::input;instruction.sources[1].kind=Register::texture;instruction.sources[2].kind=Register::sampler;p.instructions.push_back(instruction);
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
}
int main(int argc,char **argv)try{TEST(argc==2);samples(argv[1]);return 0;}catch(const std::exception &e){std::fprintf(stderr,"mirror scratch: %s\n",e.what());return 1;}
