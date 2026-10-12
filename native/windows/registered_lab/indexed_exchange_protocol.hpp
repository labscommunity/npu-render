#pragma once
// Bounded, versioned laboratory draw packets. Shader tokens are parsed SM4 data,
// never native code or an imported OpenVINO model. Transport is defined separately.
#include "uniform_draw.hpp"
#include "point_texture_draw.hpp"
#include "linear_texture_draw.hpp"
#include "triangle_texture_draw.hpp"
#include <cstring>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace nr::registered_lab::indexed_exchange {
constexpr uint64_t magic = 0x4e525f4452415731ull;
constexpr uint32_t max_packet_bytes = 32u * 1024 * 1024, max_requests = 4096;
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
inline raster::BlendMode blend_mode(const Header &h) {
    if(h.flags & 0x10000)return raster::BlendMode((h.flags>>17)&15);
    return (h.flags & 0x8000) ? raster::BlendMode::destination_out : (h.flags & 0x4000) ? raster::BlendMode::destination_in : raster::source_blend((h.flags & 1) != 0);
}
inline bool color_clear(const Header &h){return h.abi==13 && h.flags==0x200000;}
inline bool shape_valid(const Header &h) {
    if(h.abi==13)return color_clear(h) && h.signature==magic && (h.nonce[0] || h.nonce[1]) && h.client_pid &&
        h.width && h.width<=8192 && h.height && h.height<=8192 && uint64_t(h.width)*h.height<=4194304 &&
        (h.format==28 || h.format==87) && !h.vertices && !h.vs_words && !h.ps_words &&
        h.sequence && h.sequence<=max_requests && h.body_bytes<=max_packet_bytes-sizeof(Header) && !h.reserved;

    // ABI 12 adds U/V address bits to each existing sampler word. Older
    // ABIs remain clamp-only; all shape/blend rules are inherited unchanged.
    // ABI 14 carries two-bit address modes and four finite border components.
    if(h.abi==12 || h.abi==14){
        if(!(h.flags&0x2000))return false;
        auto legacy=h;legacy.abi=(h.flags&0x10000)?11u:(h.flags&0x8000)?10u:(h.flags&0x4000)?9u:8u;
        return shape_valid(legacy);
    }
    if(h.flags & 0x1f0000){
        if(!(h.flags&0x10000) || h.abi!=11 || !raster::extended_blend(blend_mode(h)) ||
            !(h.flags&1) || (h.flags&0xc000) || ((h.flags&2) && !(h.flags&0x3000)))return false;
        auto legacy=h;legacy.flags&=~0x1f0000u;
        legacy.abi=(legacy.flags&0x2000)?8u:(legacy.flags&0x1000)?7u:(legacy.flags&0xe0)?6u:(legacy.flags&4)?3u:1u;
        return shape_valid(legacy);
    }
    if (h.flags & 0xc000) {
        const auto mode=h.flags & 0xc000;
        if (mode==0xc000 || h.abi != (mode==0x8000?10u:9u) || !(h.flags & 1) || ((h.flags & 2) && !(h.flags & 0x3000))) return false;
        auto legacy = h; legacy.flags &= ~0xc000u;
        legacy.abi = (legacy.flags & 0x2000) ? 8u : (legacy.flags & 0x1000) ? 7u :
            (legacy.flags & 0xe0) ? 6u : (legacy.flags & 4) ? 3u : 1u;
        return shape_valid(legacy);
    }
    const uint32_t abi=(h.flags&0x2000)?8u:(h.flags&0x1000)?7u:(h.flags&0xe0)?6u:(h.flags&16)?5u:(h.flags&8)?4u:(h.flags&4)?3u:(h.flags&2)?2u:1u;
    const bool valid = h.signature == magic && h.abi == abi && (h.nonce[0] || h.nonce[1]) && h.client_pid &&
        h.width && h.width <= 8192 && h.height && h.height <= 8192 && uint64_t(h.width) * h.height <= 4 * 1024 * 1024 &&
        h.vertices && h.vertices <= 4096 && !(h.vertices % 3) && (h.format == 28 || h.format == 87) &&
        !(h.flags & ~0x3fffu) && (h.flags & 0xf00u) && h.vs_words >= 3 && h.vs_words <= 16384 &&
        h.ps_words >= 3 && h.ps_words <= 16384 && h.sequence && h.sequence <= max_requests &&
        h.body_bytes <= max_packet_bytes - sizeof(Header) && !h.reserved;
    if (!valid) return false;
    if(h.flags & 0x2000) {
        if(!(h.flags&2) || (h.flags&0x1018) || (h.flags&96)==96)return false;
        // The payload's scissor determines work; validate it after decoding.
        return true;
    }
    if(h.flags & 0x1000) {
        if(!(h.flags & 2) || ((h.flags & 24)!=8 && (h.flags & 24)!=16) || (h.flags & 96)==96)return false;
        // The payload's scissor determines work; validate it after decoding.
        return true;
    }
    if((h.flags & 0xe0) && ((h.flags & 26) || !(h.flags & 96) || (h.flags & 96)==96))return false;
    if (h.flags & 16) return h.vertices == 6 && h.width <= 4096 && (h.flags & ~5u) == 0xf12;
    if (h.flags & 8) return h.vertices == 6 && h.width <= 4096 && (h.flags & ~5u) == 0xf0a;
    if (h.flags & 2) return h.vertices == 6 && h.flags == 0xf02;
    // Uniform draws also use device-computed bounds. A full-target estimate
    // rejects DWM's 66-vertex sparse draw before it can reach the provider.
    // The executor enforces the 4096-tile limit after projection and clipping.
    return true;
}
inline bool request_valid(const Header &h, uint64_t bytes) {
    return shape_valid(h) && h.kind == 1 && !h.device && (!color_clear(h) || h.body_bytes==16) && bytes == sizeof(Header) + uint64_t(h.body_bytes);
}
inline bool same_request(const Header &a, const Header &b) {
    return a.abi == b.abi && a.nonce[0] == b.nonce[0] && a.nonce[1] == b.nonce[1] && a.client_pid == b.client_pid && a.sequence == b.sequence &&
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
    raster::Color clear_color{};
    raster::Viewport viewport{};
    std::optional<std::array<int32_t,4>> scissor;
    shader::Program vertex, pixel;
    std::vector<shader::VertexRegisters> inputs;
    shader::VertexConstants vertex_constants, pixel_constants;
    std::vector<raster::PackedColor> background;
    raster::PackedTexture texture;
    raster::PackedResources resources{};
    shader::TextureSamplers samplers{};
};
inline bool scissor_valid(const std::array<int32_t,4> &s, const Header &h) {
    return s[0] >= 0 && s[1] >= 0 && s[0] <= s[2] && s[1] <= s[3] && s[2] <= int32_t(h.width) && s[3] <= int32_t(h.height);
}
struct TextureDescription { uint32_t width, height, format, reserved; };
static_assert(sizeof(TextureDescription) == 16);
inline bool texture_valid(const TextureDescription &t, const Header &h) {
    return t.width && t.width <= 8192 && t.height && t.height <= 8192 &&
        uint64_t(t.width)*t.height <= 4*1024*1024 && ((h.flags & 24) || (t.width >= h.width && t.height >= h.height)) &&
        (t.format == 28 || t.format == 87) && !t.reserved;
}
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
inline bool triangle_scissor_valid(const Request &r){
    const auto &h=r.header;if(!(h.flags&0x3000))return true;
    const auto clip=r.scissor.value_or(std::array<int32_t,4>{0,0,int32_t(h.width),int32_t(h.height)});
    if(!scissor_valid(clip,h))return false;
    // Shape limits bound vertex/shader input. The selected device computes
    // each triangle's bounds before the executor enforces its 4096-tile cap.
    // A full-scissor estimate would reject sparse instanced desktop geometry.
    return true;
}
inline uint32_t triangle_batch(const Request &r,uint32_t capacity=4096){
    // The renderer supplies its qualified capacity; transport has no provider dependency.
    require(capacity==4096 || capacity==8192);
    const auto &h=r.header;require(shape_valid(h) && (h.flags&0x3000));
    const auto clip=r.scissor.value_or(std::array<int32_t,4>{0,0,int32_t(h.width),int32_t(h.height)});
    require(scissor_valid(clip,h));
    if(shader::quad_derivatives(r.pixel)){
        const auto area=uint64_t(clip[2]-clip[0])*uint64_t(clip[3]-clip[1]);
        uint32_t batch=capacity;while(batch>4 && area<=batch/2)batch/=2;return batch;
    }
    // Packed regions pad their last batch; target area need not divide it.
    uint32_t batch=capacity;
    const auto area=uint64_t(clip[2]-clip[0])*uint64_t(clip[3]-clip[1]);
    // Shrink only while the complete region still fits in one batch. This
    // avoids padding work without increasing the validated tile budget.
    while(batch>256 && area<=batch/2)batch/=2;
    return batch;
}
inline std::vector<uint8_t> encode(Request &r) {
    auto &h = r.header;
    if(color_clear(h)){
        require(shape_valid(h) && h.kind==1 && !h.device && r.inputs.empty() && r.background.empty() &&
            r.vertex.bytecode.empty() && r.pixel.bytecode.empty() && !r.scissor);
        for(const auto value:r.clear_color)require(std::isfinite(value));
        h.body_bytes=sizeof(r.clear_color);Writer w;w.value(h);w.value(r.clear_color);return w.finish();
    }
    require(r.vertex.bytecode.size() <= 16384 && r.pixel.bytecode.size() <= 16384);
    h.vs_words = uint32_t(r.vertex.bytecode.size()); h.ps_words = uint32_t(r.pixel.bytecode.size());
    require(shape_valid(h) && h.kind == 1 && !h.device && r.inputs.size() == h.vertices && r.background.size() == size_t(h.width) * h.height);
    Writer w; w.value(h); w.value(r.viewport);
    if (h.flags & 4) { require(r.scissor.has_value() && scissor_valid(*r.scissor,h)); w.value(*r.scissor); }
    else require(!r.scissor.has_value());
    require(triangle_scissor_valid(r));
    w.append(r.vertex.bytecode.data(), size_t(h.vs_words) * 4); w.append(r.pixel.bytecode.data(), size_t(h.ps_words) * 4);
    w.append(r.inputs.data(), r.inputs.size() * sizeof(shader::VertexRegisters));
    auto constants = [&](const shader::Program &p, const shader::VertexConstants &c) {
        for (size_t i = 0; i < c.size(); ++i) { require(p.constants[i] <= c[i].size()); w.append(c[i].data(), size_t(p.constants[i]) * sizeof(shader::VertexBits)); }
    };
    constants(r.vertex, r.vertex_constants); constants(r.pixel, r.pixel_constants);
    w.append(r.background.data(), r.background.size() * sizeof(raster::PackedColor));
    if(h.flags & 0x2000){
        (void)raster::resource_shapes(r.pixel,r.resources);
        for(const auto &t:r.resources){const TextureDescription description{t.width,t.height,t.format,0};
            require(t.pixels.size()==size_t(t.width)*t.height);w.value(description);w.append(t.pixels.data(),t.pixels.size()*sizeof(raster::PackedColor));}
        for(const auto &sampler:r.samplers){
            const auto filter=uint32_t(sampler.filter),u=uint32_t(sampler.u),v=uint32_t(sampler.v);
            for(const auto value:sampler.border)require(std::isfinite(value));
            if(h.abi==14){require(filter<=1 && u<=2 && v<=2);w.value(filter|(u<<1)|(v<<3));w.value(sampler.border);}
            else {
                require(filter<=1 && u<=1 && v<=1 && (h.abi==12 || (!u && !v)) && sampler.border==std::array<float,4>{});
                w.value(filter|(u<<1)|(v<<2));
            }
        }
    } else if (h.flags & 2) {
        const auto &t = r.texture; const TextureDescription description{t.width,t.height,t.format,0};
        require(texture_valid(description,h) && t.pixels.size() == size_t(t.width)*t.height);
        w.value(description); w.append(t.pixels.data(),t.pixels.size()*sizeof(raster::PackedColor));
    } else require(!r.texture.width && !r.texture.height && !r.texture.format && r.texture.pixels.empty());
    auto bytes = w.finish(); h.body_bytes = uint32_t(bytes.size() - sizeof(Header));
    std::memcpy(bytes.data(), &h, sizeof(h)); require(request_valid(h, bytes.size())); return bytes;
}
inline Request decode(const std::vector<uint8_t> &bytes) {
    Reader reader(bytes); Request r; r.header = reader.value<Header>(); const auto &h = r.header;
    require(request_valid(h, bytes.size()));
    if(color_clear(h)){
        require(h.body_bytes==sizeof(r.clear_color));r.clear_color=reader.value<raster::Color>();
        for(const auto value:r.clear_color)require(std::isfinite(value));require(reader.done());return r;
    }
    r.viewport = reader.value<raster::Viewport>();
    if (h.flags & 4) { r.scissor = reader.value<std::array<int32_t,4>>(); require(scissor_valid(*r.scissor,h)); }
    require(triangle_scissor_valid(r));
    require(r.viewport.x == 0 && r.viewport.y == 0 && r.viewport.width == float(h.width) && r.viewport.height == float(h.height) &&
        r.viewport.min_depth >= 0 && r.viewport.min_depth <= r.viewport.max_depth && r.viewport.max_depth <= 1);
    const auto vs = reader.vector<uint32_t>(h.vs_words), ps = reader.vector<uint32_t>(h.ps_words);
    r.vertex = shader::parse_vertex(vs.data(), vs.size()); r.pixel = shader::parse_pixel(ps.data(), ps.size());
    r.inputs = reader.vector<shader::VertexRegisters>(h.vertices);
    for (size_t i = 0; i < r.vertex_constants.size(); ++i) r.vertex_constants[i] = reader.vector<shader::VertexBits>(r.vertex.constants[i]);
    for (size_t i = 0; i < r.pixel_constants.size(); ++i) r.pixel_constants[i] = reader.vector<shader::VertexBits>(r.pixel.constants[i]);
    r.background = reader.vector<raster::PackedColor>(size_t(h.width) * h.height);
    if(h.flags & 0x2000){
        for(auto &t:r.resources){const auto d=reader.value<TextureDescription>();require(!d.reserved);
            t={d.width,d.height,d.format,reader.vector<raster::PackedColor>(size_t(d.width)*d.height)};}
        (void)raster::resource_shapes(r.pixel,r.resources);
        for(auto &sampler:r.samplers){const auto encoded=reader.value<uint32_t>();
            sampler.filter=static_cast<shader::TextureFilter>(encoded&1);
            if(h.abi==14){
                require(!(encoded>>5) && ((encoded>>1)&3)<=2 && ((encoded>>3)&3)<=2);
                sampler.u=static_cast<shader::TextureAddress>((encoded>>1)&3);sampler.v=static_cast<shader::TextureAddress>((encoded>>3)&3);
                sampler.border=reader.value<std::array<float,4>>();for(const auto value:sampler.border)require(std::isfinite(value));
            }else {
                require(encoded<=(h.abi==12?7u:1u));
                sampler.u=static_cast<shader::TextureAddress>((encoded>>1)&1);sampler.v=static_cast<shader::TextureAddress>((encoded>>2)&1);
            }
        }
    } else if (h.flags & 2) {
        const auto t = reader.value<TextureDescription>(); require(texture_valid(t,h));
        r.texture = {t.width,t.height,t.format,reader.vector<raster::PackedColor>(size_t(t.width)*t.height)};
    }
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
        receipt.covered_samples <= uint64_t(h.width) * h.height * (color_clear(h)?1u:h.vertices/3));
    if(color_clear(h))require(receipt.input_bytes==16 && receipt.output_bytes==uint64_t(h.width)*h.height*4 &&
        receipt.provider_requests==1 && receipt.tile_executions==1 && receipt.covered_samples==uint64_t(h.width)*h.height && !receipt.used_rectangle);
    raster::UniformDrawExecutor::Result r{};
    r.pixels = reader.vector<raster::PackedColor>(size_t(h.width) * h.height); require(reader.done());
    r.execution_device = "NPU (remote lab indexed)"; r.input_bytes = receipt.input_bytes; r.output_bytes = receipt.output_bytes;
    r.provider_requests = receipt.provider_requests; r.covered_samples = receipt.covered_samples; r.tile_executions = receipt.tile_executions;
    r.used_rectangle = receipt.used_rectangle != 0; return r;
}
}
