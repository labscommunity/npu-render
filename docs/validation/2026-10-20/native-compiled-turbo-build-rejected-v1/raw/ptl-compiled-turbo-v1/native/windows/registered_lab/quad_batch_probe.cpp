#include "quad_batch.hpp"
#include <fstream>
#include <chrono>
#include <cstdio>
namespace ex=nr::registered_lab::indexed_exchange;
using Clock=std::chrono::steady_clock;
static double elapsed(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
int main(int argc,char **argv)try{
 ex::require(argc==3 || argc==5);
 const bool inspect=!std::strcmp(argv[1],"--inspect");ex::require(inspect || !std::strcmp(argv[1],"--render"));
 std::ifstream file(argv[2],std::ios::binary|std::ios::ate);ex::require(bool(file));const auto length=file.tellg();ex::require(length>=80 && length<=ex::max_packet_bytes);file.seekg(0);
 std::vector<uint8_t> bytes(size_t(length),0);file.read(reinterpret_cast<char*>(bytes.data()),length);ex::require(bool(file));const auto r=ex::decode(bytes);const auto &h=r.header;
 bool pattern=true;try{nr::raster::LinearTextureDrawExecutor::validate_shader(r.pixel);}catch(const std::invalid_argument &){pattern=false;}
 // First candidate: existing single-source point/linear packets, full write mask,
 // no cull, ordinary replace/source-over, groups of six vertices. Dynamic geometry
 // is still checked by the existing provider vertex/projection and quad validator.
 const bool eligible=pattern && (h.flags&~5u)==0x1f12 && h.vertices && !(h.vertices%6) && h.vertices<=4092 && h.width<=4096 && r.viewport.x==0 && r.viewport.y==0 && r.viewport.width==float(h.width) && r.viewport.height==float(h.height);
 std::printf("{\"quad_candidate\":true,\"shader_pattern\":%s,\"static_eligible\":%s,\"width\":%u,\"height\":%u,\"vertices\":%u,\"flags\":%u,\"source_width\":%u,\"source_height\":%u,\"geometry_qualified\":false,\"pixel_data_exported\":false}\n",pattern?"true":"false",eligible?"true":"false",h.width,h.height,h.vertices,h.flags,r.texture.width,r.texture.height);std::fflush(stdout);
 if(inspect){ex::require(argc==3);return 0;}ex::require(argc==5 && eligible);const std::string backend(argv[3]);ex::require(backend=="CPU" || backend=="NPU");
 const auto prepare=Clock::now();ex::QuadBatch executor(r,backend);const auto construction=elapsed(prepare);
 std::vector<nr::raster::PackedColor> final_pixels;
 for(unsigned repetition=0;repetition<2;++repetition){auto pixels=r.background;uint64_t calls=0,tiles=0;const auto start=Clock::now();
  auto attempt=executor.execute(r);ex::require(attempt.accepted && attempt.result.execution_device==backend);calls=attempt.result.provider_requests;tiles=attempt.result.tile_executions;pixels=std::move(attempt.result.pixels);
  if(repetition)ex::require(pixels==final_pixels);final_pixels=std::move(pixels);
  std::printf("{\"quad_rendered\":true,\"repetition\":%u,\"execution_device\":\"%s\",\"construction_ms\":%.3f,\"draw_ms\":%.3f,\"provider_requests\":%llu,\"tiles\":%llu,\"geometry_qualified\":true,\"desktop_changed\":false}\n",repetition,backend.c_str(),construction,elapsed(start),static_cast<unsigned long long>(calls),static_cast<unsigned long long>(tiles));std::fflush(stdout);
 }
 std::ifstream exists(argv[4],std::ios::binary);ex::require(!exists);exists.close();std::ofstream output(argv[4],std::ios::binary);ex::require(bool(output));output.write(reinterpret_cast<const char*>(final_pixels.data()),std::streamsize(final_pixels.size()*4));output.close();ex::require(bool(output));return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
