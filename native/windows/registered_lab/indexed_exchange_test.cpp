#include "indexed_pipe.hpp"
#include "shader_contract.hpp"
#include "shader_reference.hpp"
#include "point_texture_fixture.hpp"
#include "linear_texture_fixture.hpp"
#include "typed_load_fixture.hpp"
#include <fstream>
#include <cstdio>
#include <thread>
#include <exception>
namespace ex = nr::registered_lab::indexed_exchange;
namespace stream = nr::registered_lab::indexed_stream;
namespace {
void test(bool value) { if (!value) throw std::runtime_error("indexed exchange contract failed"); }
void rejects(const std::vector<uint8_t> &bytes) {
    bool rejected = false; try { (void)ex::decode(bytes); } catch (const std::invalid_argument &) { rejected = true; } test(rejected);
}
// Real local pipe I/O with a synthetic protocol peer; this is never NPU evidence.
void pipe_contract(const std::vector<uint8_t> &request, const std::vector<uint8_t> &reply, bool stall) {
    wchar_t suffix[40]{}; swprintf_s(suffix, L"%016llx%016llx", static_cast<unsigned long long>(GetCurrentProcessId()), stall ? 2ull : 1ull);
    const auto name = std::wstring(L"\\\\.\\pipe\\npu-render-indexed-") + suffix;
    stream::Handle pipe(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 512, 512, 1000, nullptr));
    std::exception_ptr server_error;
    std::thread server([&] {
        try {
            const auto deadline = GetTickCount64() + 10000;
            stream::Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr)); OVERLAPPED op{}; op.hEvent = event.value;
            const auto ready = ConnectNamedPipe(pipe.value, &op); const auto error = ready ? ERROR_SUCCESS : GetLastError();
            if (error != ERROR_PIPE_CONNECTED) (void)stream::completed(pipe.value, op, ready, error, deadline);
            test(stream::read_packet(pipe.value, deadline) == request);
            if (stall) { Sleep(200); return; }
            stream::write_packet(pipe.value, reply, deadline); uint32_t ack = 0;
            stream::transfer(pipe.value, &ack, 4, false, deadline); test(ack == ex::Reader(request).value<ex::Header>().sequence);
        } catch (...) { server_error = std::current_exception(); }
    });
    std::exception_ptr client_error;
    try {
        if (!stall) test(stream::exchange_packet(name, request) == reply);
        else {
            stream::Handle client(CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
            stream::write_packet(client.value, request, GetTickCount64() + 10000);
            const auto started = GetTickCount64(); bool rejected = false;
            try { (void)stream::read_packet(client.value, started + 20); } catch (const std::runtime_error &) { rejected = true; }
            test(rejected && GetTickCount64() - started < 2000);
        }
    } catch (...) { client_error = std::current_exception(); }
    server.join(); if (server_error) std::rethrow_exception(server_error); if (client_error) std::rethrow_exception(client_error);
}
}
int main(int argc, char **argv) try {
    test(argc == 1 || argc == 3);
    ex::Request clear;clear.header.abi=13;clear.header.flags=0x200000;clear.header.nonce[0]=7;
    clear.header.client_pid=42;clear.header.sequence=1;clear.header.width=65;clear.header.height=7;clear.header.format=87;
    clear.clear_color={.5f,.25f,.75f,1};const auto clear_packet=ex::encode(clear);const auto clear_decoded=ex::decode(clear_packet);
    test(clear_packet.size()==96 && clear_decoded.clear_color==clear.clear_color && clear_decoded.inputs.empty());
    for(unsigned n=0;n<10;++n){auto bad=clear_packet;auto h=clear.header;
        if(n==0)h.flags|=1;if(n==1)h.vertices=3;if(n==2)h.vs_words=3;if(n==3)h.ps_words=3;
        if(n==4)h.abi=12;if(n==5)h.reserved=1;if(n==6)h.body_bytes=12;if(n==7)h.device=3;
        if(n==8)h.width=0;if(n==9)h.nonce[0]=0;std::memcpy(bad.data(),&h,sizeof(h));rejects(bad);
    }
    for(const auto value:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}){
        auto bad=clear_packet;std::memcpy(bad.data()+sizeof(ex::Header),&value,4);rejects(bad);
        auto candidate=clear;candidate.clear_color[0]=value;bool failed=false;try{ex::encode(candidate);}catch(const std::invalid_argument&){failed=true;}test(failed);
    }
    auto short_clear=clear_packet;short_clear.pop_back();rejects(short_clear);auto long_clear=clear_packet;long_clear.push_back(0);rejects(long_clear);
    nr::raster::UniformDrawExecutor::Result clear_result{};clear_result.pixels.assign(455,{191,64,128,255});
    clear_result.execution_device="NPU";clear_result.input_bytes=16;clear_result.output_bytes=1820;
    clear_result.covered_samples=455;clear_result.provider_requests=clear_result.tile_executions=1;
    const auto clear_reply=ex::response(clear.header,clear_result);test(ex::decode_response(clear_reply,clear.header).pixels==clear_result.pixels);
    for(unsigned n=0;n<5;++n){auto bad=clear_reply;ex::Receipt receipt{};std::memcpy(&receipt,bad.data()+sizeof(ex::Header),sizeof(receipt));
        if(n==0)receipt.covered_samples=0;if(n==1)receipt.input_bytes=0;if(n==2)receipt.output_bytes=0;
        if(n==3)receipt.provider_requests=2;if(n==4)receipt.tile_executions=0;
        std::memcpy(bad.data()+sizeof(ex::Header),&receipt,sizeof(receipt));bool failed=false;
        try{ex::decode_response(bad,clear.header);}catch(const std::invalid_argument&){failed=true;}test(failed);
    }
    const auto vw = nr::registered_lab::shader_tokens("float4 main(float4 v:POSITION):SV_Position{return v;}", "vs_4_0");
    const auto pw = nr::registered_lab::shader_tokens("float4 main():SV_Target{return float4(0,0,0,1);}", "ps_4_0");
    ex::Request r; r.header.nonce[0] = 0x12345678; r.header.nonce[1] = 0xabcdef; r.header.client_pid = 42;
    const bool scissor_packet = argc == 3 && !std::strcmp(argv[1], "--pipe-scissor");
    const bool texture_packet = argc == 3 && !std::strcmp(argv[1], "--pipe-texture");
    const bool desktop_packet = argc == 3 && (!std::strcmp(argv[1], "--pipe-desktop") || texture_packet || scissor_packet);
    r.header.width = desktop_packet ? 1024u : 64u; r.header.height = desktop_packet ? 768u : 64u;
    const auto pixel_count = size_t(r.header.width) * r.header.height; r.header.vertices = 6; r.header.format = 87; r.header.flags = 0xf00; r.header.sequence = 1;
    r.vertex = nr::shader::parse_vertex(vw.data(), vw.size()); r.pixel = nr::shader::parse_pixel(pw.data(), pw.size());
    r.viewport = {0,0,float(r.header.width),float(r.header.height),0,1}; r.inputs.resize(6); r.background.resize(pixel_count, {1,2,3,4});
    const float quad[6][4]{{-1,1,.5f,1},{1,1,.5f,1},{1,-1,.5f,1},{1,-1,.5f,1},{-1,-1,.5f,1},{-1,1,.5f,1}};
    for (size_t i = 0; i < 6; ++i) std::memcpy(r.inputs[i][0].data(), quad[i], 16);
    const auto bytes = ex::encode(r); const auto decoded = ex::decode(bytes);
    test(decoded.header.body_bytes + sizeof(ex::Header) == bytes.size() && decoded.inputs == r.inputs && decoded.background == r.background &&
         decoded.vertex.bytecode == vw && decoded.pixel.bytecode == pw && ex::same_request(decoded.header, r.header));
    auto destination_in=r;destination_in.header.abi=9;destination_in.header.flags|=0x4001;
    const auto mask_words=nr::registered_lab::shader_tokens("float4 main():SV_Target{return float4(1,.75,.25,.5);}","ps_4_0");
    destination_in.pixel=nr::shader::parse_pixel(mask_words.data(),mask_words.size());
    const auto destination_packet=ex::encode(destination_in);const auto destination_decoded=ex::decode(destination_packet);
    test(destination_decoded.header.abi==9 && ex::blend_mode(destination_decoded.header)==nr::raster::BlendMode::destination_in &&
        destination_decoded.inputs==r.inputs && destination_decoded.background==r.background);
    for(unsigned n=0;n<4;++n){auto bad=destination_packet;auto h=destination_decoded.header;
        if(n==0)h.abi=1;if(n==1)h.flags&=~1u;if(n==2)h.flags|=0x8000;if(n==3)h.flags&=~0x4000u;
        std::memcpy(bad.data(),&h,sizeof(h));rejects(bad);}
    auto destination_out=r;destination_out.header.abi=10;destination_out.header.flags|=0x8001;
    const auto erase_words=nr::registered_lab::shader_tokens("float4 main():SV_Target{return float4(1,.75,.5,.25);}","ps_4_0");
    destination_out.pixel=nr::shader::parse_pixel(erase_words.data(),erase_words.size());
    const auto erase_packet=ex::encode(destination_out);const auto erase_decoded=ex::decode(erase_packet);
    test(erase_decoded.header.abi==10 && ex::blend_mode(erase_decoded.header)==nr::raster::BlendMode::destination_out &&
        erase_decoded.inputs==r.inputs && erase_decoded.background==r.background);
    for(unsigned n=0;n<5;++n){auto bad=erase_packet;auto h=erase_decoded.header;
        if(n==0)h.abi=9;if(n==1)h.flags&=~1u;if(n==2)h.flags|=0x4000;if(n==3)h.flags&=~0x8000u;if(n==4)h.flags|=0x10000;
        std::memcpy(bad.data(),&h,sizeof(h));rejects(bad);}
    auto destination_over=r;destination_over.header.abi=11;destination_over.header.flags|=0x90001;
    const auto behind_words=nr::registered_lab::shader_tokens("float4 main():SV_Target{return float4(.25,.5,.75,.5);}","ps_4_0");
    destination_over.pixel=nr::shader::parse_pixel(behind_words.data(),behind_words.size());
    destination_over.background.assign(pixel_count,{1,2,3,127});
    const auto behind_packet=ex::encode(destination_over);
    for(uint32_t mode=4;mode<13;++mode){auto candidate=destination_over;candidate.header.flags=0xf01|0x10000|(mode<<17);
        const auto packet=ex::encode(candidate);const auto roundtrip=ex::decode(packet);
        test(roundtrip.header.abi==11 && ex::blend_mode(roundtrip.header)==nr::raster::BlendMode(mode) && roundtrip.background==candidate.background);
        for(unsigned n=0;n<8;++n){auto bad=packet;auto h=roundtrip.header;
            if(n==0)h.abi=10;if(n==1)h.flags&=~1u;if(n==2)h.flags|=0x4000;if(n==3)h.flags|=0x8000;
            if(n==4)h.flags&=~0x10000u;if(n==5)h.flags=(h.flags&~0x1e0000u)|(15u<<17);
            if(n==6)h.flags&=~0x1e0000u;if(n==7)h.flags|=0x200000;
            std::memcpy(bad.data(),&h,sizeof(h));rejects(bad);
        }
    }
    auto loop=r;
    const auto loop_words=nr::registered_lab::shader_tokens("cbuffer C {int count;};float4 main():SV_Target{float3 sum=0;[loop]for(int i=0;i<count;++i)sum+=float3(.03125,.015625,.0625);return float4(sum,1);}","ps_4_0");
    loop.pixel=nr::shader::parse_pixel(loop_words.data(),loop_words.size());loop.pixel_constants[0].push_back({8,0,0,0});
    const auto loop_packet=ex::encode(loop);const auto loop_roundtrip=ex::decode(loop_packet);
    test(loop_roundtrip.pixel.bytecode==loop_words && loop_roundtrip.pixel_constants==loop.pixel_constants);
    test(nr::shader::lower_bounded_loops(loop_roundtrip.pixel).instructions.size()>loop_roundtrip.pixel.instructions.size());
    auto derivative=r;derivative.header.abi=8;derivative.header.flags=0x2f02;
    const auto derivative_vs=nr::registered_lab::shader_tokens("struct O{float4 p:SV_Position;noperspective float2 uv:TEXCOORD;};O main(float4 p:POSITION,float2 uv:TEXCOORD){O o;o.p=p;o.uv=uv;return o;}","vs_4_0");
    const auto derivative_ps=nr::registered_lab::shader_tokens("float4 main(float4 p:SV_Position,noperspective float2 uv:TEXCOORD):SV_Target{return float4(ddx(uv.x)*64,ddy(uv.y)*64,.25,1);}","ps_4_0");
    derivative.vertex=nr::shader::parse_vertex(derivative_vs.data(),derivative_vs.size());derivative.pixel=nr::shader::parse_pixel(derivative_ps.data(),derivative_ps.size());
    for(size_t i=0;i<6;++i)derivative.inputs[i][1]={nr::shader::as_bits((quad[i][0]+1)*.5f),nr::shader::as_bits((1-quad[i][1])*.5f),0,0};
    const auto derivative_packet=ex::encode(derivative);const auto derivative_roundtrip=ex::decode(derivative_packet);
    test(nr::shader::quad_derivatives(derivative_roundtrip.pixel) && derivative_roundtrip.inputs==derivative.inputs && ex::triangle_batch(derivative_roundtrip)%4==0);
    {auto tiny=derivative;tiny.header.width=tiny.header.height=1;tiny.viewport={0,0,1,1,0,1};tiny.background.resize(1);
     test(ex::triangle_batch(ex::decode(ex::encode(tiny)))==4);}
    // DWM's sparse 66-vertex solid draw exceeds the old full-target estimate
    // (22*192 tiles). Admission must defer the real work bound to projection.
    {auto sparse=r;sparse.header.width=1024;sparse.header.height=768;sparse.header.vertices=66;
     sparse.viewport={0,0,1024,768,0,1};sparse.inputs.resize(66);sparse.background.resize(1024*768);
     const auto packet=ex::encode(sparse);const auto decoded_sparse=ex::decode(packet);
     test(decoded_sparse.inputs.size()==66 && decoded_sparse.background.size()==1024*768);}
    auto textured = r; textured.header.abi = 2; textured.header.flags = 0xf02;
    const auto textured_vs = nr::registered_lab::shader_tokens(R"(
struct O {float4 p:SV_Position;float4 c:COLOR0;float2 uv:TEXCOORD0;};
O main(float4 p:POSITION,float4 c:COLOR0,float2 uv:TEXCOORD0){O o;o.p=p;o.c=c;o.uv=uv;return o;})","vs_4_0");
    const auto &textured_ps = nr::registered_lab::test_fixture::point_texture_pixel_words;
    textured.vertex = nr::shader::parse_vertex(textured_vs.data(),textured_vs.size());
    textured.pixel = nr::shader::parse_pixel(textured_ps,std::size(textured_ps));
    textured.texture = {r.header.width+32,r.header.height+16,87,{}};
    textured.texture.pixels.resize(size_t(textured.texture.width)*textured.texture.height);
    for (size_t i=0;i<textured.texture.pixels.size();++i) for(uint32_t c=0;c<4;++c) textured.texture.pixels[i][c]=uint8_t((i*17+c*71)&255);
    const uint32_t corners[6][2]{{0,0},{r.header.width,0},{r.header.width,r.header.height},{r.header.width,r.header.height},{0,r.header.height},{0,0}};
    for(size_t i=0;i<6;++i) {
        textured.inputs[i][1] = {0x3f800000,0x3f800000,0x3f800000,0x3f800000};
        const float uv[]{float(32+corners[i][0])/textured.texture.width,float(16+corners[i][1])/textured.texture.height};
        std::memcpy(textured.inputs[i][2].data(),uv,8);
    }
    const auto texture_bytes = ex::encode(textured); const auto texture_decoded = ex::decode(texture_bytes);
    test(texture_decoded.texture.pixels == textured.texture.pixels && texture_decoded.texture.width == textured.texture.width &&
         texture_decoded.texture.height == textured.texture.height && texture_decoded.texture.format == 87 && texture_decoded.inputs == textured.inputs);
    auto linear=textured;linear.header.abi=4;linear.header.flags=0xf0f;linear.scissor=std::array<int32_t,4>{3,5,int32_t(r.header.width)-7,int32_t(r.header.height)-9};
    linear.pixel=nr::shader::parse_pixel(nr::registered_lab::test_fixture::linear_texture_pixel_words,std::size(nr::registered_lab::test_fixture::linear_texture_pixel_words));
    linear.texture={2,2,28,{{1,2,3,4},{5,6,7,8},{9,10,11,12},{13,14,15,16}}};
    const auto linear_bytes=ex::encode(linear);const auto linear_decoded=ex::decode(linear_bytes);
    test(linear_decoded.texture.pixels==linear.texture.pixels && linear_decoded.scissor==linear.scissor && linear_decoded.header.flags==0xf0f);
    for(unsigned n=0;n<5;++n){auto bad=linear_bytes;auto h=linear.header;
        if(n==0)h.abi=3;if(n==1)h.flags&=~2u;if(n==2)h.flags&=~8u;if(n==3)h.vertices=3;if(n==4)h.width=4097;
        std::memcpy(bad.data(),&h,sizeof(h));rejects(bad);}
    {auto invalid=linear;invalid.header.abi=9;invalid.header.flags|=0x4001;
     test(!ex::shape_valid(invalid.header));}
    auto point=linear;point.header.abi=5;point.header.flags=0xf17;
    const auto point_bytes=ex::encode(point);const auto point_decoded=ex::decode(point_bytes);
    test(point_decoded.texture.pixels==point.texture.pixels && point_decoded.header.abi==5 && point_decoded.scissor==point.scissor);
    for(unsigned n=0;n<5;++n){auto bad=point_bytes;auto h=point.header;
        if(n==0)h.abi=4;if(n==1)h.flags|=8;if(n==2)h.flags&=~2u;if(n==3)h.flags&=~16u;if(n==4)h.vertices=3;
        std::memcpy(bad.data(),&h,sizeof(h));rejects(bad);}
    auto triangles=linear;triangles.header.abi=7;triangles.header.flags=0x1f4f;
    triangles.header.vertices=30;triangles.inputs.resize(30);
    const auto triangle_bytes=ex::encode(triangles);const auto triangle_decoded=ex::decode(triangle_bytes);
    test(triangle_decoded.inputs==triangles.inputs && triangle_decoded.texture.pixels==triangles.texture.pixels && triangle_decoded.scissor==triangles.scissor);
    {auto masked=triangles;masked.header.abi=9;masked.header.flags|=0x4001;
     const auto decoded_mask=ex::decode(ex::encode(masked));
     test(ex::blend_mode(decoded_mask.header)==nr::raster::BlendMode::destination_in && decoded_mask.texture.pixels==triangles.texture.pixels);}
    {auto masked=triangles;masked.header.abi=10;masked.header.flags|=0x8001;
     const auto decoded_mask=ex::decode(ex::encode(masked));
     test(ex::blend_mode(decoded_mask.header)==nr::raster::BlendMode::destination_out && decoded_mask.texture.pixels==triangles.texture.pixels);}
    {auto invalid=linear;invalid.header.abi=10;invalid.header.flags|=0x8001;
     bool rejected=false;try{(void)ex::encode(invalid);}catch(const std::invalid_argument&){rejected=true;}test(rejected);}
    for(unsigned n=0;n<6;++n){auto bad=triangle_bytes;auto h=triangles.header;
        if(n==0)h.abi=6;if(n==1)h.flags&=~2u;if(n==2)h.flags&=~24u;if(n==3)h.flags|=24u;if(n==4)h.flags|=96u;if(n==5)h.vertices=4095;
        std::memcpy(bad.data(),&h,sizeof(h));rejects(bad);}
    auto desktop_clipped=triangles;desktop_clipped.header.width=1024;desktop_clipped.header.height=768;desktop_clipped.header.vertices=81;
    desktop_clipped.viewport={0,0,1024,768,0,1};desktop_clipped.inputs.resize(81);desktop_clipped.background.resize(1024*768);
    desktop_clipped.scissor=std::array<int32_t,4>{429,732,453,756};
    test(ex::triangle_batch(desktop_clipped)==1024);
    const auto desktop_clipped_packet=ex::encode(desktop_clipped);test(ex::decode(desktop_clipped_packet).inputs.size()==81);
    auto excessive=desktop_clipped_packet;const std::array<int32_t,4> full_clip{0,0,1024,768};
    std::memcpy(excessive.data()+sizeof(ex::Header)+sizeof(nr::raster::Viewport),full_clip.data(),sizeof(full_clip));
    test(ex::decode(excessive).inputs.size()==81); // Raster budget follows device-computed bounds.
    desktop_clipped.scissor=full_clip;test(ex::decode(ex::encode(desktop_clipped)).inputs.size()==81);
    // A thin, full-height scissor is one packed batch, not 192 full rows.
    desktop_clipped.header.vertices=4095;desktop_clipped.inputs.resize(4095);
    for(const std::array<int32_t,4> clip: {std::array<int32_t,4>{429,0,430,768},{429,0,429,768},{0,768,1024,768}}){
        desktop_clipped.scissor=clip;const auto packed=ex::encode(desktop_clipped);
        test(ex::decode(packed).inputs.size()==4095);
        test(ex::triangle_batch(desktop_clipped)==(clip[0]!=clip[2] && clip[1]!=clip[3]?1024u:256u));
    }
    desktop_clipped.scissor=std::array<int32_t,4>{0,0,1024,5};test(ex::triangle_batch(desktop_clipped)==4096);
    // A window's odd extent must not reduce packed batches to one/eight pixels.
    for(const auto extent:{std::array<uint32_t,3>{642,660,4096},{31,29,1024},{1,1,256}}){
        auto window=desktop_clipped;window.header.width=extent[0];window.header.height=extent[1];
        window.viewport={0,0,float(extent[0]),float(extent[1]),0,1};
        window.background.resize(size_t(extent[0])*extent[1]);
        window.scissor=std::array<int32_t,4>{0,0,int32_t(extent[0]),int32_t(extent[1])};
        const auto decoded_window=ex::decode(ex::encode(window));test(ex::triangle_batch(decoded_window)==extent[2]);
    }
    // Scheduling accepts the renderer's bounded preference without importing
    // provider APIs into the standalone packet helper.
    for(const auto capacity:{4096u,8192u}){
        auto sized=desktop_clipped;sized.scissor=full_clip;
        test(ex::triangle_batch(sized,capacity)==capacity);
        sized.scissor=std::array<int32_t,4>{429,0,430,768};
        test(ex::triangle_batch(sized,capacity)==1024);
    }
    for(const auto invalid:{0u,512u,8193u,16384u}){
        bool rejected=false;try{(void)ex::triangle_batch(desktop_clipped,invalid);}catch(const std::invalid_argument &){rejected=true;}test(rejected);
    }
    auto resources=r;resources.header.abi=8;resources.header.flags=0x2f42;
    const auto &lookup=nr::registered_lab::test_fixture::typed_lookup_pixel_words;
    resources.pixel=nr::shader::parse_pixel(lookup,std::size(lookup));
    resources.resources[0]={4,1,42,{{0,0,0,0},{1,0,0,0},{2,0,0,0},{3,0,0,0}}};
    resources.resources[1]={2,2,28,{{1,2,3,4},{5,6,7,8},{9,10,11,12},{13,14,15,16}}};
    const auto resource_packet=ex::encode(resources);const auto resource_decoded=ex::decode(resource_packet);
    test(resource_decoded.resources[0].pixels==resources.resources[0].pixels && resource_decoded.resources[1].pixels==resources.resources[1].pixels &&
        nr::raster::resource_layout_equal(resources.resources,resource_decoded.resources));
    for(unsigned mode=0;mode<4;++mode){auto mirrored=resources;mirrored.header.abi=12;
        mirrored.samplers[0].u=mode&1?nr::shader::TextureAddress::mirror:nr::shader::TextureAddress::clamp;
        mirrored.samplers[0].v=mode&2?nr::shader::TextureAddress::mirror:nr::shader::TextureAddress::clamp;
        for(const uint32_t flags:{0u,0x4001u,0x8001u,0x90001u}){
            mirrored.header.flags=resources.header.flags|flags;const auto packet=ex::encode(mirrored);const auto roundtrip=ex::decode(packet);
            test(nr::raster::sampler_layout_equal(mirrored.samplers,roundtrip.samplers) && ex::blend_mode(mirrored.header)==ex::blend_mode(roundtrip.header));
            auto corrupt=packet;const uint32_t invalid=8;std::memcpy(corrupt.data()+corrupt.size()-16*4,&invalid,4);rejects(corrupt);
            if(mode){auto legacy=mirrored;legacy.header.abi=flags==0?8:flags==0x4001?9:flags==0x8001?10:11;
                bool rejected=false;try{(void)ex::encode(legacy);}catch(const std::invalid_argument &){rejected=true;}test(rejected);
                auto old_packet=packet;auto h=legacy.header;std::memcpy(old_packet.data(),&h,sizeof(h));rejects(old_packet);
            }
        }
        if(mode)test(!nr::raster::sampler_layout_equal(mirrored.samplers,resources.samplers));
    }
    {auto bad=r;bad.header.abi=12;test(!ex::shape_valid(bad.header));}
    for(uint32_t mode=4;mode<13;++mode){
        for(const auto &base : {triangles,resources}){auto candidate=base;candidate.header.abi=11;candidate.header.flags|=0x10001|(mode<<17);
            const auto roundtrip=ex::decode(ex::encode(candidate));
            test(ex::blend_mode(roundtrip.header)==nr::raster::BlendMode(mode) && roundtrip.inputs==candidate.inputs &&
                roundtrip.texture.pixels==candidate.texture.pixels && nr::raster::resource_layout_equal(roundtrip.resources,candidate.resources));
        }
        for(const auto &base : {textured,linear,point}){auto candidate=base;candidate.header.abi=11;candidate.header.flags|=0x10001|(mode<<17);test(!ex::shape_valid(candidate.header));}
    }
    for(unsigned n=0;n<5;++n){auto bad=resource_packet;auto h=resources.header;
        if(n==0)h.abi=7;if(n==1)h.flags|=8;if(n==2)h.flags|=16;if(n==3)h.flags|=0x1000;if(n==4)h.flags&=~2u;
        std::memcpy(bad.data(),&h,sizeof(h));rejects(bad);}
    // The first resource descriptor follows the fixed shader/input/target data.
    const auto resource_offset=sizeof(ex::Header)+sizeof(nr::raster::Viewport)+resources.vertex.bytecode.size()*4+resources.pixel.bytecode.size()*4+
        resources.inputs.size()*sizeof(nr::shader::VertexRegisters)+resources.background.size()*4;
    for(unsigned n=0;n<4;++n){auto bad=resource_packet;ex::TextureDescription d{};std::memcpy(&d,bad.data()+resource_offset,sizeof(d));
        if(n==0)d.width=UINT32_MAX;if(n==1)d.height=2;if(n==2)d.format=87;if(n==3)d.reserved=1;
        std::memcpy(bad.data()+resource_offset,&d,sizeof(d));rejects(bad);}
    const size_t description_offset = texture_bytes.size()-textured.texture.pixels.size()*4-sizeof(ex::TextureDescription);
    for(uint32_t bad=0;bad<5;++bad) {
        auto packet = texture_bytes; ex::TextureDescription desc{}; std::memcpy(&desc,packet.data()+description_offset,sizeof(desc));
        switch(bad){case 0:desc.width=UINT32_MAX;break;case 1:desc.height=8193;break;case 2:desc.format=1;break;case 3:desc.reserved=1;break;case 4:desc.width++;break;}
        std::memcpy(packet.data()+description_offset,&desc,sizeof(desc)); rejects(packet);
    }
    auto truncated_texture=texture_bytes;truncated_texture.pop_back();auto th=textured.header;--th.body_bytes;
    std::memcpy(truncated_texture.data(),&th,sizeof(th));rejects(truncated_texture);
    auto clipped=r;clipped.header.abi=3;clipped.header.flags|=4;
    clipped.scissor=std::array<int32_t,4>{0,7,int32_t(r.header.width),int32_t(r.header.height)};
    const auto clip_packet=ex::encode(clipped);test(ex::decode(clip_packet).scissor==clipped.scissor);
    for(uint32_t cull=1;cull<=2;++cull)for(uint32_t ccw=0;ccw<2;++ccw){
        auto faces=clipped;faces.header.abi=6;faces.header.flags|=(cull<<5)|(ccw<<7);
        const auto packet=ex::encode(faces);const auto decoded_faces=ex::decode(packet);
        test(decoded_faces.header.flags==faces.header.flags && decoded_faces.scissor==faces.scissor);
        for(unsigned failure=0;failure<5;++failure){auto bad=packet;auto h=faces.header;
            if(failure==0)h.abi=3;if(failure==1)h.flags|=96;if(failure==2)h.flags|=2;if(failure==3)h.flags|=16;if(failure==4)h.flags=(h.flags&~96u)|128u;
            std::memcpy(bad.data(),&h,sizeof(h));rejects(bad);}
    }
    for(unsigned i=0;i<5;++i) {
        auto bad=clip_packet;
        auto bounds=*clipped.scissor;
        if(i==0) bounds[0]=-1;
        if(i==1) bounds[2]=int32_t(r.header.width)+1;
        if(i==2) bounds[3]=6;
        if(i<3) std::memcpy(bad.data()+sizeof(ex::Header)+sizeof(nr::raster::Viewport),bounds.data(),sizeof(bounds));
        else {auto h=clipped.header;if(i==3)h.abi=1;else h.flags|=2;std::memcpy(bad.data(),&h,sizeof(h));}
        rejects(bad);
    }
    for (const auto size : {size_t(0), size_t(79), size_t(80), size_t(103), bytes.size() - 1}) rejects({bytes.begin(), bytes.begin() + size});
    for (unsigned i = 0; i < 12; ++i) {
        auto h = r.header; switch (i) {
        case 0: h.signature ^= 1; break; case 1: h.abi = 2; break; case 2: h.nonce[0] = h.nonce[1] = 0; break;
        case 3: h.client_pid = 0; break; case 4: h.width = UINT32_MAX; break; case 5: h.vertices = 4098; break;
        case 6: h.flags = 0x10000; break; case 7: h.vs_words = 16385; break; case 8: h.sequence = ex::max_requests + 1; break;
        case 9: h.kind = 2; break; case 10: h.device = 1; break; case 11: h.body_bytes++; break;
        }
        auto bad = bytes; std::memcpy(bad.data(), &h, sizeof(h)); rejects(bad);
    }
    auto extra = bytes; extra.push_back(0); auto h = r.header; ++h.body_bytes; std::memcpy(extra.data(), &h, sizeof(h)); rejects(extra);
    auto invalid_view = bytes; const float nan = std::numeric_limits<float>::quiet_NaN(); std::memcpy(invalid_view.data() + sizeof(h), &nan, 4); rejects(invalid_view);
    nr::raster::UniformDrawExecutor::Result result{}; result.pixels = r.background; result.execution_device = "NPU";
    result.provider_requests = 4; result.input_bytes = 16600; result.output_bytes = 20480; result.covered_samples = pixel_count; result.tile_executions = 1; result.used_rectangle = true;
    const auto reply = ex::response(r.header, result); const auto parsed = ex::decode_response(reply, r.header);
    test(parsed.pixels == result.pixels && parsed.provider_requests == 4 && parsed.covered_samples == pixel_count && parsed.used_rectangle);
    if (argc == 1) { pipe_contract(bytes, reply, false); pipe_contract(bytes, reply, true); }
    for (unsigned i = 0; i < 5; ++i) {
        auto bad = reply; auto header = ex::Reader(bad).value<ex::Header>();
        switch (i) { case 0: header.nonce[0]++; break; case 1: header.device = 1; break; case 2: header.client_pid++; break;
            case 3: header.sequence++; break; case 4: header.body_bytes--; break; }
        std::memcpy(bad.data(), &header, sizeof(header)); bool rejected = false;
        try { (void)ex::decode_response(bad, r.header); } catch (const std::invalid_argument &) { rejected = true; } test(rejected);
    }
    auto failed = r.header; failed.kind = 3; failed.device = 0; failed.body_bytes = 0;
    ex::Writer failure; failure.value(failed); const auto failure_bytes = failure.finish();
    test(ex::failure_valid(failed, r.header, failure_bytes.size())); bool rejected = false;
    try { (void)ex::decode_response(failure_bytes, r.header); } catch (const std::runtime_error &) { rejected = true; } test(rejected);
    result.execution_device = "CPU"; rejected = false;
    try { (void)ex::response(r.header, result); } catch (const std::invalid_argument &) { rejected = true; } test(rejected);
    if (argc == 3) {
        if (!std::strcmp(argv[1], "--emit-point-texture") || !std::strcmp(argv[1], "--emit-clear") || !std::strcmp(argv[1], "--emit-request") || !std::strcmp(argv[1], "--emit-destination-in") || !std::strcmp(argv[1], "--emit-destination-out") || !std::strcmp(argv[1], "--emit-destination-over") || !std::strcmp(argv[1], "--emit-derivatives") || !std::strcmp(argv[1], "--emit-loops")) {
            const auto &emitted = !std::strcmp(argv[1], "--emit-point-texture") ? texture_bytes : !std::strcmp(argv[1], "--emit-clear") ? clear_packet : !std::strcmp(argv[1], "--emit-loops") ? loop_packet : !std::strcmp(argv[1], "--emit-derivatives") ? derivative_packet : !std::strcmp(argv[1], "--emit-destination-over") ? behind_packet : !std::strcmp(argv[1], "--emit-destination-out") ? erase_packet : !std::strcmp(argv[1], "--emit-destination-in") ? destination_packet : bytes;
            std::ifstream old(argv[2], std::ios::binary); test(!old); old.close();
            std::ofstream out(argv[2], std::ios::binary); test(bool(out));
            out.write(reinterpret_cast<const char *>(emitted.data()), std::streamsize(emitted.size())); out.close(); test(bool(out));
        } else if (scissor_packet) {
            const auto blend_vs=nr::registered_lab::shader_tokens("struct O{float4 p:SV_Position;float4 c:COLOR;};O main(float4 p:POSITION,float4 c:COLOR){O o;o.p=p;o.c=c;return o;}","vs_4_0");
            const auto blend_ps=nr::registered_lab::shader_tokens("float4 main(float4 p:SV_Position,float4 c:COLOR):SV_Target{return c;}","ps_4_0");
            clipped.vertex=nr::shader::parse_vertex(blend_vs.data(),blend_vs.size());clipped.pixel=nr::shader::parse_pixel(blend_ps.data(),blend_ps.size());
            clipped.header.flags=0xf05;clipped.scissor=std::array<int32_t,4>{5,7,int32_t(r.header.width)-3,int32_t(r.header.height)-5};
            const auto original=clipped.inputs;clipped.inputs[3]=original[0];clipped.inputs[4]=original[2];clipped.inputs[5]=original[4];
            const std::string argument=argv[2];const std::wstring name(argument.begin(),argument.end());
            const unsigned alphas[]{0,128,255};
            for(uint32_t frame=0;frame<3;++frame) {
                const auto alpha=alphas[frame];clipped.header.sequence=frame+1;clipped.header.nonce[0]=0xabce0000+frame;
                const std::array<unsigned,4> tint{(std::min)(17u,alpha),(std::min)(73u,alpha),(std::min)(123u,alpha),alpha};
                for(auto &v:clipped.inputs)for(unsigned c=0;c<4;++c)v[1][c]=nr::shader::as_bits(float(tint[c])/255);
                for(size_t i=0;i<clipped.background.size();++i)for(unsigned c=0;c<4;++c)clipped.background[i][c]=uint8_t(i+c*31+frame*17);
                const auto packet=ex::encode(clipped);const auto started=GetTickCount64();
                const auto output=ex::decode_response(stream::exchange_packet(name,packet),clipped.header);
                for(uint32_t y=0;y<r.header.height;++y)for(uint32_t x=0;x<r.header.width;++x)for(unsigned c=0;c<4;++c){
                    const bool inside=x>=5 && x<r.header.width-3 && y>=7 && y<r.header.height-5;
                    const auto old=clipped.background[size_t(y)*r.header.width+x][c];const auto lane=c<3?2-c:c;
                    const auto expected=inside?uint8_t((tint[lane]*255+unsigned(old)*(255-alpha)+127)/255):old;
                    test(output.pixels[size_t(y)*r.header.width+x][c]==expected);
                }
                test(output.used_rectangle && output.covered_samples==uint64_t(r.header.width-8)*(r.header.height-12));
                std::printf("{\"indexed_pipe_npu_scissor\":true,\"sequence\":%u,\"alpha\":%u,\"verified_components\":%zu,\"roundtrip_ms\":%llu,\"displayed_frames\":0}\n",frame+1,alpha,pixel_count*4,static_cast<unsigned long long>(GetTickCount64()-started));
            }
        } else if (!std::strcmp(argv[1], "--pipe-texture")) {
            const std::string argument=argv[2];const std::wstring name(argument.begin(),argument.end());
            for(uint32_t frame=1;frame<=3;++frame) {
                textured.header.sequence=frame;textured.header.nonce[0]=0xabcd0000+frame;
                for(auto &p:textured.texture.pixels) for(auto &c:p) c=uint8_t(c+17);
                const auto packet=ex::encode(textured);const auto started=GetTickCount64();
                const auto output=ex::decode_response(stream::exchange_packet(name,packet),textured.header);
                for(uint32_t y=0;y<r.header.height;++y) for(uint32_t x=0;x<r.header.width;++x) {
                    auto expected=textured.texture.pixels[size_t(y+16)*textured.texture.width+x+32];expected[3]=255;
                    test(output.pixels[size_t(y)*r.header.width+x]==expected);
                }
                std::printf("{\"indexed_pipe_npu_texture\":true,\"sequence\":%u,\"verified_components\":%zu,\"roundtrip_ms\":%llu,\"displayed_frames\":0}\n",frame,pixel_count*4,static_cast<unsigned long long>(GetTickCount64()-started));
            }
        } else if (!std::strcmp(argv[1], "--pipe") || desktop_packet) {
            const std::string argument = argv[2]; const std::wstring name(argument.begin(), argument.end());
            for (uint32_t i = 1; i <= 3; ++i) {
                r.header.sequence = i; r.header.nonce[0] = 0x12345678 + i; const auto request = ex::encode(r);
                const auto started = GetTickCount64();
                const auto output = ex::decode_response(nr::registered_lab::indexed_stream::exchange_packet(name, request), r.header);
                test(output.covered_samples == pixel_count); for (const auto &p : output.pixels) test(p == nr::raster::PackedColor{0,0,0,255});
                std::printf("{\"indexed_pipe_npu_packet\":true,\"sequence\":%u,\"verified_components\":%zu,\"roundtrip_ms\":%llu,\"displayed_frames\":0}\n", i, pixel_count * 4, static_cast<unsigned long long>(GetTickCount64() - started));
            }
        } else if (!std::strcmp(argv[1], "--verify-clear") || !std::strcmp(argv[1], "--verify-reply")) {
            std::ifstream in(argv[2], std::ios::binary | std::ios::ate); test(bool(in)); const auto size = in.tellg();
            test(size >= std::streamoff(sizeof(ex::Header)) && size <= ex::max_packet_bytes); in.seekg(0);
            std::vector<uint8_t> contents(size_t(size), 0); in.read(reinterpret_cast<char *>(contents.data()), std::streamsize(contents.size())); test(bool(in));
            if(!std::strcmp(argv[1], "--verify-clear")){
                const auto output=ex::decode_response(contents,clear.header);test(output.pixels==clear_result.pixels && output.covered_samples==455);
                std::puts("{\"clear_exchange\":true,\"verified_components\":1820,\"execution_device\":\"NPU (remote lab indexed)\",\"displayed_frames\":0}");return 0;
            }
            const auto output = ex::decode_response(contents, r.header); test(output.covered_samples == 4096);
            for (const auto &p : output.pixels) test(p == nr::raster::PackedColor{0,0,0,255});
            std::printf("{\"indexed_npu_packet\":true,\"verified_components\":16384,\"execution_device\":\"%s\",\"displayed_frames\":0}\n", output.execution_device.c_str());
        } else test(false);
    }
    std::puts("indexed exchange preserves draw state and rejects malformed, stale, failed and non-NPU replies"); return 0;
} catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
