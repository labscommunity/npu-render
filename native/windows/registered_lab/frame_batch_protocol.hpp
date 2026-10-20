#pragma once
// Frame batch packets between the driver, the parent worker and the child
// provider. A batch replaces per-draw requests with one ordered command stream
// per flush, plus resource versions the worker lacks. The existing Header keeps
// its signature, nonce, client_pid and sequence; abi 24 marks a batch.
//
// Request body (after Header, little-endian, 4-aligned):
//   BatchHeader
//   shaders[shader_count]:     ShaderRecord then bytecode words
//   resources[resource_count]: ResourceRecord
//   uploads[upload_count]:     UploadRecord then w*h packed pixels
//   commands[command_count]:   CommandRecord then vertex words, constant words, texture bindings
// Response body:
//   ResponseHeader
//   targets[target_count]:     TargetRecord then rect pixels
// A response with missing_count > 0 executed nothing; the driver resends with
// uploads for the listed (resource, version) pairs.
#include "indexed_exchange_protocol.hpp"
#include <cstdint>
#include <vector>
namespace nr::registered_lab::frame_batch {
constexpr uint32_t abi = 24, batch_magic = 0x4e524231u, response_magic = 0x4e52424fu;
constexpr uint32_t max_shaders = 64, max_resources = 1024, max_uploads = 1024, max_commands = 1024, max_body = indexed_exchange::max_packet_bytes - 128;
enum Flags : uint32_t { volatile_resource = 1, upload_window = 2 };
struct BatchHeader {
    uint32_t magic = batch_magic, version = 1, shader_count = 0, resource_count = 0;
    uint32_t upload_count = 0, command_count = 0, flags = 0, reserved = 0;
    uint64_t batch_sequence = 0, expected_generation = 0;
};
static_assert(sizeof(BatchHeader) == 48);
struct ShaderRecord { uint32_t stage = 0, words = 0; uint8_t sha256[32]{}; };   // stage 1 vertex, 2 pixel
static_assert(sizeof(ShaderRecord) == 40);
struct ResourceRecord {
    uint64_t key = 0, version = 0;           // key: driver backing identity (process-unique)
    uint32_t width = 0, height = 0, format = 0, flags = 0;
};
static_assert(sizeof(ResourceRecord) == 32);
struct UploadRecord { uint32_t resource = 0, x = 0, y = 0, width = 0, height = 0, reserved = 0; }; // pixels follow
static_assert(sizeof(UploadRecord) == 24);
struct TextureBinding { uint32_t resource = 0xffffffffu, filter = 0, address_u = 0, address_v = 0; float border[4]{}; };
static_assert(sizeof(TextureBinding) == 32);
struct CommandRecord {
    uint32_t kind = 0;                        // 0 draw, 1 clear, 2 copy region (reserved)
    uint32_t target = 0;                      // resource index
    uint32_t vertex_shader = 0, pixel_shader = 0;   // shader indices (draw)
    uint32_t vertex_count = 0, vertex_words = 0;    // decoded VertexRegisters words (16 float4 per vertex)
    uint32_t vertex_constant_words = 0, pixel_constant_words = 0;
    uint32_t blend_mode = 0, write_mask = 15, cull = 0, front_ccw = 0;
    int32_t scissor[4]{}; uint32_t scissor_enabled = 0;
    float viewport[6]{}; float blend_constant[4]{}; float clear_color[4]{};
    uint32_t texture_count = 0, reserved[3]{};
};
static_assert(sizeof(CommandRecord) == 128);
struct ResponseHeader {
    uint32_t magic = response_magic, version = 1, target_count = 0, missing_count = 0;
    uint64_t generation = 0, batch_sequence = 0;
    uint64_t provider_requests = 0, covered_samples = 0, kernel_ms_x1000 = 0, host_ms_x1000 = 0;
    uint32_t commands_executed = 0, commands_fallback = 0, escapes = 0, reserved = 0;
};
static_assert(sizeof(ResponseHeader) == 80);
struct TargetRecord { uint64_t key = 0, version = 0; uint32_t x = 0, y = 0, width = 0, height = 0; }; // pixels follow
static_assert(sizeof(TargetRecord) == 32);
struct MissingRecord { uint64_t key = 0, version = 0; };
inline bool batch_header_valid(const indexed_exchange::Header &h) {
    return h.signature == indexed_exchange::magic && h.abi == abi && (h.nonce[0] || h.nonce[1]) && h.client_pid &&
           h.kind == 1 && !h.device && h.sequence && h.sequence <= indexed_exchange::max_requests &&
           h.body_bytes >= sizeof(BatchHeader) && h.body_bytes <= max_body && !h.vs_words && !h.ps_words && !h.vertices;
}
inline bool batch_response_header_valid(const indexed_exchange::Header &h, const indexed_exchange::Header &request) {
    return batch_header_valid(request) && h.signature == request.signature && h.abi == abi && h.nonce[0] == request.nonce[0] &&
           h.nonce[1] == request.nonce[1] && h.client_pid == request.client_pid && h.sequence == request.sequence &&
           h.kind == 2 && h.device == 3 && h.body_bytes >= sizeof(ResponseHeader) && h.body_bytes <= max_body &&
           h.width == request.width && h.height == request.height && h.flags == request.flags;
}
}
