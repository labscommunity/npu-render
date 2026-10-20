// Bounded binary messages for the disposable-VM / physical-NPU laboratory.
// Transport is the existing authenticated management session, not a listener.
#pragma once
#include <cstdint>
namespace nr::registered_lab::exchange {
constexpr uint64_t magic = 0x4e525f4558434831ull;
constexpr uint32_t max_frame_bytes = 32u * 1024 * 1024;
struct Header {
    uint64_t signature = magic, nonce[2]{};
    uint32_t abi = 1, width = 0, height = 0, sequence = 0;
    uint32_t frame_bytes = 0, kind = 1, device = 0, reserved = 0;
    uint64_t reserved2 = 0;
};
static_assert(sizeof(Header) == 64);
inline bool shape_valid(const Header &h) {
    const uint64_t size = uint64_t(h.width) * h.height * 4;
    return h.signature == magic && h.abi == 1 && (h.nonce[0] || h.nonce[1]) &&
        h.width && h.width <= 8192 && h.height && h.height <= 8192 &&
        h.sequence && h.sequence <= 3 && size <= max_frame_bytes && h.frame_bytes == size &&
        !h.reserved && !h.reserved2;
}
inline bool request_valid(const Header &h, uint64_t file_bytes) {
    return shape_valid(h) && h.kind == 1 && !h.device && file_bytes == sizeof(Header) + uint64_t(h.frame_bytes) * 2;
}
inline bool response_valid(const Header &h, const Header &request, uint64_t file_bytes) {
    return request_valid(request, sizeof(Header) + uint64_t(request.frame_bytes) * 2) &&
        shape_valid(h) && h.kind == 2 && h.device == 3 &&
        h.nonce[0] == request.nonce[0] && h.nonce[1] == request.nonce[1] &&
        h.sequence == request.sequence && h.width == request.width && h.height == request.height &&
        h.frame_bytes == request.frame_bytes && file_bytes == sizeof(Header) + uint64_t(h.frame_bytes);
}
}
