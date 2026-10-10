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
    for(unsigned n=0;n<6;++n){auto bad=triangle_bytes;auto h=triangles.header;
        if(n==0)h.abi=6;if(n==1)h.flags&=~2u;if(n==2)h.flags&=~24u;if(n==3)h.flags|=24u;if(n==4)h.flags|=96u;if(n==5)h.vertices=4095;
        std::memcpy(bad.data(),&h,sizeof(h));rejects(bad);}
    auto resources=r;resources.header.abi=8;resources.header.flags=0x2f42;
    const auto &lookup=nr::registered_lab::test_fixture::typed_lookup_pixel_words;
    resources.pixel=nr::shader::parse_pixel(lookup,std::size(lookup));
    resources.resources[0]={4,1,42,{{0,0,0,0},{1,0,0,0},{2,0,0,0},{3,0,0,0}}};
    resources.resources[1]={2,2,28,{{1,2,3,4},{5,6,7,8},{9,10,11,12},{13,14,15,16}}};
    const auto resource_packet=ex::encode(resources);const auto resource_decoded=ex::decode(resource_packet);
    test(resource_decoded.resources[0].pixels==resources.resources[0].pixels && resource_decoded.resources[1].pixels==resources.resources[1].pixels &&
        nr::raster::resource_layout_equal(resources.resources,resource_decoded.resources));
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
        case 6: h.flags = 0x10000; break; case 7: h.vs_words = 16385; break; case 8: h.sequence = 129; break;
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
        if (!std::strcmp(argv[1], "--emit-request")) {
            std::ifstream old(argv[2], std::ios::binary); test(!old); old.close();
            std::ofstream out(argv[2], std::ios::binary); test(bool(out));
            out.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size())); out.close(); test(bool(out));
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
        } else if (!std::strcmp(argv[1], "--verify-reply")) {
            std::ifstream in(argv[2], std::ios::binary | std::ios::ate); test(bool(in)); const auto size = in.tellg();
            test(size >= std::streamoff(sizeof(ex::Header)) && size <= ex::max_packet_bytes); in.seekg(0);
            std::vector<uint8_t> contents(size_t(size), 0); in.read(reinterpret_cast<char *>(contents.data()), std::streamsize(contents.size())); test(bool(in));
            const auto output = ex::decode_response(contents, r.header); test(output.covered_samples == 4096);
            for (const auto &p : output.pixels) test(p == nr::raster::PackedColor{0,0,0,255});
            std::printf("{\"indexed_npu_packet\":true,\"verified_components\":16384,\"execution_device\":\"%s\",\"displayed_frames\":0}\n", output.execution_device.c_str());
        } else test(false);
    }
    std::puts("indexed exchange preserves draw state and rejects malformed, stale, failed and non-NPU replies"); return 0;
} catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
