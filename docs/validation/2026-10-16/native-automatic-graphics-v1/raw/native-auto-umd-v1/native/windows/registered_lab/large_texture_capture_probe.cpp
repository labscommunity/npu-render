#define main original_draw_replay_main
#include "draw_replay_probe.cpp"
#undef main
#include "indexed_exchange_protocol.hpp"
#include <windows.h>
namespace ex=nr::registered_lab::indexed_exchange;
int main(int argc,char **argv)try{
    ex::Header h;h.abi=17;h.reserved=4;h.nonce[0]=1;h.client_pid=1;h.sequence=1;
    h.width=91;h.height=64;h.vertices=6;h.flags=0xf0a;h.format=87;h.vs_words=h.ps_words=3;
    nr::capture::check(ex::shape_valid(h));unsigned negatives=0;
    ex::TextureDescription t{2912,1824,87,0};nr::capture::check(ex::texture_valid(t,h));
    auto legacy=h;legacy.abi=4;legacy.reserved=0;nr::capture::check(!ex::texture_valid(t,legacy));++negatives;
    for(unsigned i=0;i<8;++i){auto bad=h;
        switch(i){case 0:bad.reserved=1;break;case 1:bad.reserved=17;break;case 2:bad.flags=0xf02;bad.reserved=2;break;
        case 3:bad.flags|=0x1000;break;case 4:bad.vertices=9;break;case 5:bad.width=4097;break;
        case 6:bad.width=4096;bad.height=2048;break;case 7:bad.flags=0xf00;break;}
        nr::capture::check(!ex::shape_valid(bad));++negatives;
    }
    auto point=h;point.reserved=5;point.flags=0xf12;nr::capture::check(ex::shape_valid(point));
    auto too_big=t;too_big.width=8192;too_big.height=1025;nr::capture::check(!ex::texture_valid(too_big,h));++negatives;
    auto changed=h;changed.reserved=5;nr::capture::check(!ex::same_request(h,changed));++negatives;
    if(argc==2 && !std::strcmp(argv[1],"--contract")){
        std::printf("{\"large_texture_contract\":true,\"negative_cases\":%u,\"legacy_large_source_rejected\":true,\"max_packet_bytes\":%u}\n",negatives,ex::max_packet_bytes);return 0;
    }
    nr::capture::check(argc==3);const nr::capture::Snapshot s(argv[1]);
    const auto args=s.values<uint32_t>("arguments"),surface=s.values<uint32_t>("target-description"),source=s.values<uint32_t>("source-description"),sampler=s.values<uint32_t>("sampler-2-0");
    const auto view=s.values<float>("viewport");const auto blend=s.values<uint32_t>("blend"),raster=s.values<uint32_t>("raster");
    nr::capture::check(args.size()==9 && surface.size()==10 && source.size()==10 && sampler.size()==13 && view.size()==6 && blend.size()==17 && raster.size()==10);
    nr::capture::check(args[0]==6 && args[3]==4 && args[4]==1 && args[7]==UINT32_MAX && !blend[0] && !blend[1] && (blend[15]&255)==15);
    nr::capture::check(raster[0]==3 && raster[1]==1 && !raster[2] && !raster[3] && !raster[4] && !raster[5] && !raster[8] && !raster[9]);
    nr::capture::check(s.values<uint32_t>("draw-indexed")==std::vector<uint32_t>{1} && s.values<uint32_t>("depth-target-bound")==std::vector<uint32_t>{0});
    nr::capture::check(nr::registered_lab::single_mip_linear_filter(sampler[0]) && sampler[1]==3 && sampler[2]==3 && sampler[3]==3 && !sampler[4] && !sampler[11]);
    ex::Request r;auto &header=r.header;header.abi=4;header.nonce[0]=1;header.sequence=1;header.client_pid=GetCurrentProcessId();header.width=surface[3];header.height=surface[4];header.format=surface[5];header.flags=0xf0a;
    const auto vs=s.values<uint32_t>("vertex-shader"),ps=s.values<uint32_t>("pixel-shader");r.vertex=nr::shader::parse_vertex(vs.data(),vs.size());r.pixel=nr::shader::parse_pixel(ps.data(),ps.size());
    nr::raster::LinearTextureDrawExecutor::validate_shader(r.pixel);
    int32_t base;std::memcpy(&base,&args[2],4);r.inputs=fetch(s,args[0],args[1],base,r.vertex,true,args[3]);header.vertices=uint32_t(r.inputs.size());
    r.viewport={view[0],view[1],view[2],view[3],view[4],view[5]};r.vertex_constants=constants(s,0,r.vertex);r.pixel_constants=constants(s,2,r.pixel);r.background=s.values<nr::raster::PackedColor>("target");
    r.texture={source[3],source[4],source[5],s.values<nr::raster::PackedColor>("source")};
    if(raster[7]){const auto scissor=s.values<int32_t>("scissors");nr::capture::check(scissor.size()==4);const std::array<int32_t,4> clip{scissor[0],scissor[1],scissor[2],scissor[3]};
        if(clip!=std::array<int32_t,4>{0,0,int32_t(header.width),int32_t(header.height)}){r.scissor=clip;header.flags|=4;}}
    const auto bytes=ex::encode(r);nr::capture::check(header.abi==17 && header.reserved==4 && ex::request_valid(header,bytes.size()));
    const auto decoded=ex::decode(bytes);nr::capture::check(decoded.background==r.background && decoded.texture.pixels==r.texture.pixels && decoded.inputs==r.inputs && decoded.pixel.bytecode==r.pixel.bytecode);
    auto corrupt=bytes;auto malformed=header;malformed.abi=4;malformed.reserved=0;std::memcpy(corrupt.data(),&malformed,sizeof(malformed));bool rejected=false;
    try{(void)ex::decode(corrupt);}catch(const std::invalid_argument&){rejected=true;}nr::capture::check(rejected);++negatives;
    std::ifstream existing(argv[2],std::ios::binary);nr::capture::check(!existing);existing.close();std::ofstream output(argv[2],std::ios::binary);nr::capture::check(bool(output));output.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));output.close();nr::capture::check(bool(output));
    std::printf("{\"large_texture_capture_packet\":true,\"abi\":%u,\"legacy_abi\":%u,\"width\":%u,\"height\":%u,\"source_width\":%u,\"source_height\":%u,\"bytes\":%zu,\"negative_cases\":%u,\"rendering_performed\":false}\n",header.abi,header.reserved,header.width,header.height,r.texture.width,r.texture.height,bytes.size(),negatives);return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
