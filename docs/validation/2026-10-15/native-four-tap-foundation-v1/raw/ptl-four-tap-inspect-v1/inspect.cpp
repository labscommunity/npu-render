#define NOMINMAX
#include "indexed_exchange_protocol.hpp"
#include "shader_reference.hpp"
#include <cstdio>
#include <fstream>
int main(int argc,char **argv)try{
    using namespace nr::shader;using namespace nr::registered_lab::indexed_exchange;
    require(argc==2);std::ifstream f(argv[1],std::ios::binary|std::ios::ate);require(bool(f));const auto size=f.tellg();require(size>0 && size<=64*1024*1024);std::vector<uint8_t> bytes(static_cast<size_t>(size));f.seekg(0);f.read(reinterpret_cast<char *>(bytes.data()),size);require(bool(f));
    const auto r=decode(bytes);require(r.inputs.size()==6 && r.vertex.position_output<16);
    std::printf("{\"kind\":\"geometry\",\"cpu_reference_only\":true,\"pixel_data_exported\":false,\"width\":%u,\"height\":%u,\"format\":%u,\"flags\":%u,\"blend_mode\":%u,\"pixel_instructions\":%u,\"vertices\":[",r.header.width,r.header.height,r.header.format,r.header.flags,unsigned(blend_mode(r.header)),unsigned(r.pixel.instructions.size()));
    for(size_t n=0;n<r.inputs.size();++n){const auto v=reference_vertex(r.vertex,r.inputs[n],r.vertex_constants);const auto &pos=v[r.vertex.position_output];const float w=as_float(pos[3]);require(std::isfinite(w) && w!=0);const float x=(as_float(pos[0])/w+1)*r.viewport.width*.5f,y=(1-as_float(pos[1])/w)*r.viewport.height*.5f;
        if(n)std::printf(",");std::printf("{\"screen\":[%.9g,%.9g],\"w\":%.9g,\"color\":[",double(x),double(y),double(w));for(unsigned c=0;c<4;++c)std::printf("%s%.9g",c?",":"",double(as_float(v[1][c])));std::printf("],\"uv\":[");for(unsigned c=0;c<4;++c)std::printf("%s%.9g",c?",":"",double(as_float(v[2][c])));std::printf("]}");
    }
    std::printf("],\"resources\":[");bool first=true;for(unsigned slot=0;slot<16;++slot)if(r.resources[slot].width){const auto &t=r.resources[slot];const auto &s=r.samplers[slot];std::printf("%s{\"slot\":%u,\"width\":%u,\"height\":%u,\"format\":%u,\"filter\":%u,\"u\":%u,\"v\":%u}",first?"":",",slot,t.width,t.height,t.format,unsigned(s.filter),unsigned(s.u),unsigned(s.v));first=false;}
    const auto clip=r.scissor.value_or(std::array<int32_t,4>{0,0,int32_t(r.header.width),int32_t(r.header.height)});
    std::printf("],\"scissor\":[%d,%d,%d,%d]}\n",clip[0],clip[1],clip[2],clip[3]);return 0;
}catch(const std::exception &e){std::fprintf(stderr,"geometry inspection: %s\n",e.what());return 1;}
