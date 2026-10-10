#pragma once
// Bounded, versioned laboratory draw packets. Shader tokens are parsed SM4 data,
// never native code or an imported OpenVINO model. No network listener is used.
#include "uniform_draw.hpp"
#include <cstring>
#include <limits>
#include <stdexcept>
namespace nr::registered_lab::indexed_exchange {
constexpr uint64_t magic = 0x4e525f4452415731ull;
constexpr uint32_t max_packet_bytes = 32u * 1024 * 1024, max_requests = 128;
struct Header {
    uint64_t signature = magic, nonce[2]{};
    uint32_t abi = 1, width = 0, height = 0, vertices = 0, format = 0, flags = 0;
    uint32_t vs_words = 0, ps_words = 0, sequence = 0, body_bytes = 0, client_pid = 0;
    uint32_t kind = 1, device = 0, reserved = 0;
};
static_assert(sizeof(Header) == 80);
struct Receipt {
    uint64_t input_bytes = 0, output_bytes = 0, provider_requests = 0, covered_samples = 0, tile_executions = 0;
    uint32_t used_rectangle = 0, reserved = 0;
};
static_assert(sizeof(Receipt) == 48);
inline void require(bool value) { if (!value) throw std::invalid_argument("invalid indexed NPU exchange packet"); }
inline bool shape_valid(const Header &h) {
    const bool valid = h.signature == magic && h.abi == 1 && (h.nonce[0] || h.nonce[1]) && h.client_pid &&
        h.width && h.width <= 8192 && h.height && h.height <= 8192 && uint64_t(h.width) * h.height <= 4 * 1024 * 1024 &&
        h.vertices && h.vertices <= 4096 && !(h.vertices % 3) && (h.format == 28 || h.format == 87) &&
        !(h.flags & ~0xf01u) && (h.flags & 0xf00u) && h.vs_words >= 3 && h.vs_words <= 16384 &&
        h.ps_words >= 3 && h.ps_words <= 16384 && h.sequence && h.sequence <= max_requests &&
        h.body_bytes <= max_packet_bytes - sizeof(Header) && !h.reserved;
    if (!valid) return false;
    uint32_t batch = 4096; const uint64_t pixels = uint64_t(h.width) * h.height;
    while (pixels % batch) batch /= 2;
    return uint64_t(h.vertices / 3) * (pixels / batch) <= 4096;
}
inline bool request_valid(const Header &h, uint64_t bytes) {
    return shape_valid(h) && h.kind == 1 && !h.device && bytes == sizeof(Header) + uint64_t(h.body_bytes);
}
inline bool same_request(const Header &a, const Header &b) {
    return a.nonce[0] == b.nonce[0] && a.nonce[1] == b.nonce[1] && a.client_pid == b.client_pid && a.sequence == b.sequence &&
        a.width == b.width && a.height == b.height && a.vertices == b.vertices && a.format == b.format && a.flags == b.flags &&
        a.vs_words == b.vs_words && a.ps_words == b.ps_words;
}
inline bool response_valid(const Header &h, const Header &request, uint64_t bytes) {
    return request_valid(request, sizeof(Header) + uint64_t(request.body_bytes)) && shape_valid(h) && same_request(h, request) &&
        h.kind == 2 && h.device == 3 && bytes == sizeof(Header) + uint64_t(h.body_bytes) &&
        h.body_bytes == sizeof(Receipt) + uint64_t(h.width) * h.height * 4;
}
inline bool failure_valid(const Header &h, const Header &request, uint64_t bytes) {
    return request_valid(request, sizeof(Header) + uint64_t(request.body_bytes)) && shape_valid(h) && same_request(h, request) &&
        h.kind == 3 && !h.device && !h.body_bytes && bytes == sizeof(Header);
}
struct Request {
    Header header;
    raster::Viewport viewport{};
    shader::Program vertex, pixel;
    std::vector<shader::VertexRegisters> inputs;
    shader::VertexConstants vertex_constants, pixel_constants;
    std::vector<raster::PackedColor> background;
};
class Writer {
    std::vector<uint8_t> data;
public:
    void append(const void *p, size_t bytes) {
        require(bytes <= max_packet_bytes - data.size() && (!bytes || p));
        const auto old = data.size(); data.resize(old + bytes); if (bytes) std::memcpy(data.data() + old, p, bytes);
    }
    template<class T> void value(const T &v) { append(&v, sizeof(v)); }
    std::vector<uint8_t> finish() { return std::move(data); }
};
class Reader {
    const std::vector<uint8_t> &data; size_t cursor = 0;
public:
    explicit Reader(const std::vector<uint8_t> &bytes) : data(bytes) { require(data.size() <= max_packet_bytes); }
    void read(void *p, size_t bytes) {
        require(bytes <= data.size() - cursor && (!bytes || p));
        if (bytes) std::memcpy(p, data.data() + cursor, bytes); cursor += bytes;
    }
    template<class T> T value() { T v{}; read(&v, sizeof(v)); return v; }
    template<class T> std::vector<T> vector(size_t count) {
        require(count <= (data.size() - cursor) / sizeof(T));
        std::vector<T> result(count); read(result.data(), count * sizeof(T)); return result;
    }
    bool done() const { return cursor == data.size(); }
};
inline std::vector<uint8_t> encode(Request &r) {
    auto &h = r.header;
    require(r.vertex.bytecode.size() <= 16384 && r.pixel.bytecode.size() <= 16384);
    h.vs_words = uint32_t(r.vertex.bytecode.size()); h.ps_words = uint32_t(r.pixel.bytecode.size());
    require(shape_valid(h) && h.kind == 1 && !h.device && r.inputs.size() == h.vertices && r.background.size() == size_t(h.width) * h.height);
    Writer w; w.value(h); w.value(r.viewport);
    w.append(r.vertex.bytecode.data(), size_t(h.vs_words) * 4); w.append(r.pixel.bytecode.data(), size_t(h.ps_words) * 4);
    w.append(r.inputs.data(), r.inputs.size() * sizeof(shader::VertexRegisters));
    auto constants = [&](const shader::Program &p, const shader::VertexConstants &c) {
        for (size_t i = 0; i < c.size(); ++i) { require(p.constants[i] <= c[i].size()); w.append(c[i].data(), size_t(p.constants[i]) * sizeof(shader::VertexBits)); }
    };
    constants(r.vertex, r.vertex_constants); constants(r.pixel, r.pixel_constants);
    w.append(r.background.data(), r.background.size() * sizeof(raster::PackedColor));
    auto bytes = w.finish(); h.body_bytes = uint32_t(bytes.size() - sizeof(Header));
    std::memcpy(bytes.data(), &h, sizeof(h)); require(request_valid(h, bytes.size())); return bytes;
}
inline Request decode(const std::vector<uint8_t> &bytes) {
    Reader reader(bytes); Request r; r.header = reader.value<Header>(); const auto &h = r.header;
    require(request_valid(h, bytes.size())); r.viewport = reader.value<raster::Viewport>();
    require(r.viewport.x == 0 && r.viewport.y == 0 && r.viewport.width == float(h.width) && r.viewport.height == float(h.height) &&
        r.viewport.min_depth >= 0 && r.viewport.min_depth <= r.viewport.max_depth && r.viewport.max_depth <= 1);
    const auto vs = reader.vector<uint32_t>(h.vs_words), ps = reader.vector<uint32_t>(h.ps_words);
    r.vertex = shader::parse_vertex(vs.data(), vs.size()); r.pixel = shader::parse_pixel(ps.data(), ps.size());
    r.inputs = reader.vector<shader::VertexRegisters>(h.vertices);
    for (size_t i = 0; i < r.vertex_constants.size(); ++i) r.vertex_constants[i] = reader.vector<shader::VertexBits>(r.vertex.constants[i]);
    for (size_t i = 0; i < r.pixel_constants.size(); ++i) r.pixel_constants[i] = reader.vector<shader::VertexBits>(r.pixel.constants[i]);
    r.background = reader.vector<raster::PackedColor>(size_t(h.width) * h.height);
    require(reader.done()); return r;
}
inline std::vector<uint8_t> response(const Header &request, const raster::UniformDrawExecutor::Result &r) {
    require(request_valid(request, sizeof(Header) + uint64_t(request.body_bytes)) &&
        (r.execution_device == "NPU" || r.execution_device.rfind("NPU.", 0) == 0) && r.pixels.size() == size_t(request.width) * request.height);
    auto h = request; h.kind = 2; h.device = 3; h.body_bytes = uint32_t(sizeof(Receipt) + r.pixels.size() * sizeof(raster::PackedColor));
    Receipt receipt{r.input_bytes, r.output_bytes, r.provider_requests, r.covered_samples, r.tile_executions, r.used_rectangle ? 1u : 0u, 0};
    Writer w; w.value(h); w.value(receipt); w.append(r.pixels.data(), r.pixels.size() * sizeof(raster::PackedColor)); return w.finish();
}
inline raster::UniformDrawExecutor::Result decode_response(const std::vector<uint8_t> &bytes, const Header &request) {
    Reader reader(bytes); const auto h = reader.value<Header>();
    if (failure_valid(h, request, bytes.size())) throw std::runtime_error("physical NPU worker rejected indexed draw");
    require(response_valid(h, request, bytes.size())); const auto receipt = reader.value<Receipt>();
    require(!receipt.reserved && receipt.used_rectangle <= 1 && receipt.provider_requests && receipt.provider_requests <= 4 * 1024 * 1024 &&
        receipt.covered_samples <= uint64_t(h.width) * h.height * (h.vertices / 3));
    raster::UniformDrawExecutor::Result r{};
    r.pixels = reader.vector<raster::PackedColor>(size_t(h.width) * h.height); require(reader.done());
    r.execution_device = "NPU (remote lab indexed)"; r.input_bytes = receipt.input_bytes; r.output_bytes = receipt.output_bytes;
    r.provider_requests = receipt.provider_requests; r.covered_samples = receipt.covered_samples; r.tile_executions = receipt.tile_executions;
    r.used_rectangle = receipt.used_rectangle != 0; return r;
}
}
