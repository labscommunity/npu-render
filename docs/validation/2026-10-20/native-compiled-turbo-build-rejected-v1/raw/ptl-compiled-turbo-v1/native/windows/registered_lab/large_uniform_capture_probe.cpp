// Offline packet construction reuses the already qualified capture reader/fetch.
#define main original_draw_replay_main
#include "draw_replay_probe.cpp"
#undef main
#include "indexed_exchange_protocol.hpp"
#include <windows.h>
namespace ex=nr::registered_lab::indexed_exchange;
int main(int argc,char **argv)try{
    ex::Header h;h.abi=16;h.reserved=1;h.nonce[0]=1;h.client_pid=1;h.sequence=1;
    h.width=2880;h.height=1800;h.vertices=6;h.flags=0xf00;h.format=87;h.vs_words=h.ps_words=3;
    nr::capture::check(ex::shape_valid(h));unsigned negatives=0;
    for(unsigned i=0;i<8;++i){auto bad=h;
        switch(i){case 0:bad.abi=1;bad.reserved=0;break;case 1:bad.width=8192;bad.height=1024;break;
        case 2:bad.flags|=2;bad.reserved=2;break;case 3:bad.reserved=0;break;case 4:bad.reserved=16;break;
        case 5:bad.width=1024;bad.height=768;break;case 6:bad.flags=0x200000;bad.reserved=13;bad.vertices=bad.vs_words=bad.ps_words=0;break;
        case 7:bad.width=8193;bad.height=1023;break;}
        nr::capture::check(!ex::shape_valid(bad));++negatives;
    }
    auto maximum=h;maximum.width=8192;maximum.height=1023;nr::capture::check(ex::shape_valid(maximum));
    auto changed=h;changed.reserved=3;nr::capture::check(!ex::same_request(h,changed));++negatives;
    if(argc==2 && !std::strcmp(argv[1],"--contract")){
        std::printf("{\"large_uniform_contract\":true,\"negative_cases\":%u,\"legacy_large_rejected\":true,\"max_reply_packet_bytes\":%u}\n",negatives,ex::max_packet_bytes);return 0;
    }
    nr::capture::check(argc==3);
    const nr::capture::Snapshot s(argv[1]);const auto args=s.values<uint32_t>("arguments"),surface=s.values<uint32_t>("target-description");
    const auto view=s.values<float>("viewport");const auto blend=s.values<uint32_t>("blend"),raster=s.values<uint32_t>("raster");
    nr::capture::check(args.size()==9 && surface.size()==10 && view.size()==6 && blend.size()==17 && raster.size()==10);
    nr::capture::check(args[3]==4 && args[4]==1 && args[7]==UINT32_MAX && !blend[0] && blend[1] && blend[9]==2 && blend[10]==6 && blend[11]==1 && blend[12]==2 && blend[13]==6 && blend[14]==1 && (blend[15]&255)==15);
    nr::capture::check(raster[0]==3 && raster[1]==1 && !raster[2] && !raster[3] && !raster[4] && !raster[5] && !raster[8] && !raster[9]);
    nr::capture::check(s.values<uint32_t>("draw-indexed")==std::vector<uint32_t>{1} && s.values<uint32_t>("depth-target-bound")==std::vector<uint32_t>{0});
    ex::Request r;auto &header=r.header;header.nonce[0]=1;header.sequence=1;header.client_pid=GetCurrentProcessId();header.width=surface[3];header.height=surface[4];header.format=surface[5];header.flags=0xf01;
    const auto vs=s.values<uint32_t>("vertex-shader"),ps=s.values<uint32_t>("pixel-shader");r.vertex=nr::shader::parse_vertex(vs.data(),vs.size());r.pixel=nr::shader::parse_pixel(ps.data(),ps.size());
    for(bool textured:r.pixel.textures)nr::capture::check(!textured);
    int32_t base;std::memcpy(&base,&args[2],4);r.inputs=fetch(s,args[0],args[1],base,r.vertex,true,args[3]);header.vertices=uint32_t(r.inputs.size());
    r.viewport={view[0],view[1],view[2],view[3],view[4],view[5]};r.vertex_constants=constants(s,0,r.vertex);r.pixel_constants=constants(s,2,r.pixel);r.background=s.values<nr::raster::PackedColor>("target");
    if(raster[7]){const auto scissor=s.values<int32_t>("scissors");nr::capture::check(scissor.size()==4);const std::array<int32_t,4> clip{scissor[0],scissor[1],scissor[2],scissor[3]};
        if(clip!=std::array<int32_t,4>{0,0,int32_t(header.width),int32_t(header.height)}){r.scissor=clip;header.flags|=4;header.abi=3;}}
    const auto bytes=ex::encode(r);nr::capture::check(header.abi==16 && header.reserved==1 && ex::request_valid(header,bytes.size()));
    const auto decoded=ex::decode(bytes);nr::capture::check(decoded.background==r.background && decoded.inputs==r.inputs && decoded.vertex.bytecode==r.vertex.bytecode && decoded.pixel.bytecode==r.pixel.bytecode);
    std::ifstream existing(argv[2],std::ios::binary);nr::capture::check(!existing);existing.close();
    std::ofstream output(argv[2],std::ios::binary);nr::capture::check(bool(output));output.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));output.close();nr::capture::check(bool(output));
    std::printf("{\"large_uniform_capture_packet\":true,\"abi\":%u,\"legacy_abi\":%u,\"width\":%u,\"height\":%u,\"vertices\":%u,\"bytes\":%zu,\"negative_cases\":%u,\"rendering_performed\":false}\n",header.abi,header.reserved,header.width,header.height,header.vertices,bytes.size(),negatives);return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
