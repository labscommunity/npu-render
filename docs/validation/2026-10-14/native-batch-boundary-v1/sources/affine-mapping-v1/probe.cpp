#define main original_draw_replay_main
#include "draw_replay_probe.cpp"
#undef main
int main(int argc,char **argv)try{
 nr::capture::check(argc==2);const nr::capture::Snapshot s(argv[1]);
 const auto args=s.values<uint32_t>("arguments"),surface=s.values<uint32_t>("target-description"),source=s.values<uint32_t>("source-description"),vw=s.values<uint32_t>("vertex-shader"),pw=s.values<uint32_t>("pixel-shader");
 const auto vs=nr::shader::parse_vertex(vw.data(),vw.size()),ps=nr::shader::parse_pixel(pw.data(),pw.size());
 nr::raster::LinearTextureDrawExecutor::validate_shader(ps);
 int32_t base=0;std::memcpy(&base,&args[2],4);const auto inputs=fetch(s,args[0],args[1],base,vs,true,args[3]);const auto cb=constants(s,0,vs);
 const auto cr=ps.instructions.back().sources[1].index,ur=ps.instructions[0].sources[0].index;
 std::printf("{\"target_width\":%u,\"target_height\":%u,\"source_width\":%u,\"source_height\":%u,\"vertices\":%zu,\"opaque_shader\":%s,\"rendering_performed\":false}\n",surface[3],surface[4],source[3],source[4],inputs.size(),ps.instructions.size()==3?"true":"false");
 for(size_t n=0;n<inputs.size();++n){const auto result=nr::shader::reference_vertex(vs,inputs[n],cb);
  const auto &p=result[vs.position_output],&uv=result[ur],&color=result[cr];
  std::printf("{\"vertex\":%zu,\"position\":[%.9g,%.9g,%.9g,%.9g],\"uv\":[%.9g,%.9g],\"color\":[%.9g,%.9g,%.9g,%.9g]}\n",n,nr::shader::as_float(p[0]),nr::shader::as_float(p[1]),nr::shader::as_float(p[2]),nr::shader::as_float(p[3]),nr::shader::as_float(uv[0]),nr::shader::as_float(uv[1]),nr::shader::as_float(color[0]),nr::shader::as_float(color[1]),nr::shader::as_float(color[2]),nr::shader::as_float(color[3]));
 }
 return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
