#include "indexed_exchange_protocol.hpp"
#include "shader_contract.hpp"
#include <fstream>
#include <cstdio>
namespace ex = nr::registered_lab::indexed_exchange;
namespace {
void test(bool value) { if (!value) throw std::runtime_error("indexed exchange contract failed"); }
void rejects(const std::vector<uint8_t> &bytes) {
    bool rejected = false; try { (void)ex::decode(bytes); } catch (const std::invalid_argument &) { rejected = true; } test(rejected);
}
}
int main(int argc, char **argv) try {
    test(argc == 1 || argc == 3);
    const auto vw = nr::registered_lab::shader_tokens("float4 main(float4 v:POSITION):SV_Position{return v;}", "vs_4_0");
    const auto pw = nr::registered_lab::shader_tokens("float4 main():SV_Target{return float4(0,0,0,1);}", "ps_4_0");
    ex::Request r; r.header.nonce[0] = 0x12345678; r.header.nonce[1] = 0xabcdef; r.header.client_pid = 42;
    r.header.width = r.header.height = 64; r.header.vertices = 6; r.header.format = 87; r.header.flags = 0xf00; r.header.sequence = 1;
    r.vertex = nr::shader::parse_vertex(vw.data(), vw.size()); r.pixel = nr::shader::parse_pixel(pw.data(), pw.size());
    r.viewport = {0,0,64,64,0,1}; r.inputs.resize(6); r.background.resize(4096, {1,2,3,4});
    const float quad[6][4]{{-1,1,.5f,1},{1,1,.5f,1},{1,-1,.5f,1},{1,-1,.5f,1},{-1,-1,.5f,1},{-1,1,.5f,1}};
    for (size_t i = 0; i < 6; ++i) std::memcpy(r.inputs[i][0].data(), quad[i], 16);
    const auto bytes = ex::encode(r); const auto decoded = ex::decode(bytes);
    test(decoded.header.body_bytes + sizeof(ex::Header) == bytes.size() && decoded.inputs == r.inputs && decoded.background == r.background &&
         decoded.vertex.bytecode == vw && decoded.pixel.bytecode == pw && ex::same_request(decoded.header, r.header));
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
    result.provider_requests = 4; result.input_bytes = 16600; result.output_bytes = 20480; result.covered_samples = 4096; result.tile_executions = 1; result.used_rectangle = true;
    const auto reply = ex::response(r.header, result); const auto parsed = ex::decode_response(reply, r.header);
    test(parsed.pixels == result.pixels && parsed.provider_requests == 4 && parsed.covered_samples == 4096 && parsed.used_rectangle);
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
