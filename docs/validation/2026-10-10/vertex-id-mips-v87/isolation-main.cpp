#include "vertex_executor.hpp"
#include "vertex_id_fixture.hpp"
#include "shader_contract.hpp"
#include "projection_executor.hpp"
#include "raster_executor.hpp"
#include "output_merger.hpp"
#include "uniform_draw.hpp"
#include <cstdio>
#include <cstdlib>
int main(int argc,char **argv)try{
 std::setvbuf(stdout,nullptr,_IONBF,0);if(argc!=2)return 2;using namespace nr::shader;
 const unsigned n=unsigned(std::atoi(argv[1]));if(!n || n>64)return 2;
 const auto &words=nr::registered_lab::test_fixture::vertex_id_quad_words;
 auto p=parse_vertex(words,std::size(words));std::vector<VertexRegisters>input(n);
 for(unsigned i=0;i<n;++i)input[i][0][0]=i%4;
 std::printf("vertex compile %u\n",n);VertexExecutor vs(p,n,"NPU");std::puts("vertex infer");auto v=vs.execute(input,{});std::puts("vertex done");
 std::vector<nr::raster::ClipPosition> clips(n);for(unsigned i=0;i<n;++i)std::memcpy(clips[i].data(),v.vertices[i][p.position_output].data(),16);
 std::puts("projection compile");nr::raster::ProjectionExecutor proj(n,"NPU");std::puts("projection infer");auto pr=proj.execute(clips,{0,0,64,64,0,1});std::puts("projection done");
 const auto code=nr::registered_lab::shader_tokens("cbuffer C {float4 color;};float4 main():SV_Target{return color;}","ps_4_0");
 auto ps=parse_pixel(code.data(),code.size());VertexConstants cb;cb[0]={{{0x3e800000,0x3f000000,0x3f400000,0x3f800000}}};
 std::puts("pixel compile");PixelExecutor pixel(ps,1,"NPU");std::puts("pixel infer");auto result=pixel.execute(std::vector<VertexRegisters>(1),cb);std::puts("pixel done");
 for(auto x:result.vertices[0][0])std::printf("%08x ",x);std::puts("");
 std::puts("coverage compile 4096");nr::raster::CoverageExecutor coverage(4096,"NPU");
 std::vector<nr::raster::Sample> samples(4096);
 for(unsigned j=0;j<4096;++j){samples[j].triangle={pr.vertices[0].snapped,pr.vertices[1].snapped,pr.vertices[2].snapped};samples[j].point={int(j%64)*256+128,int(j/64)*256+128};}
 std::puts("coverage infer");auto covered=coverage.execute(samples);std::puts("coverage done");
 std::puts("merger compile");nr::raster::OutputMerger merger(4096,"NPU",false,true);
 std::vector<nr::raster::Color> colors(4096,{.25f,.5f,.75f,1});std::vector<nr::raster::PackedColor> bg(4096);
 std::puts("merger infer");auto merged=merger.execute(colors,bg,covered.covered);std::puts("merger done");
 std::vector<VertexRegisters> quad(6);const unsigned ids[6]{0,1,2,2,1,3};for(unsigned j=0;j<6;++j)quad[j][0][0]=ids[j];
 std::puts("full draw compile");nr::raster::UniformDrawExecutor draw(p,ps,6,64,64,"NPU",false,true,15,4096);
 for(unsigned pass=0;pass<3;++pass){std::printf("full draw infer %u\n",pass);auto dr=draw.execute(quad,{},cb,{0,0,64,64,0,1},bg);
  for(auto pixel:dr.pixels)if(pixel!=nr::raster::PackedColor{191,128,64,255})throw std::runtime_error("pixel mismatch");
  std::printf("full draw done rectangle=%u covered=%llu\n",unsigned(dr.used_rectangle),static_cast<unsigned long long>(dr.covered_samples));}
 return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
