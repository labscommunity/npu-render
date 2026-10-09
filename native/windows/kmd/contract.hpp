#pragma once
namespace nr::kernel_contract {
using u32 = unsigned int;
using u64 = unsigned long long;
static_assert(sizeof(u32) == 4 && sizeof(u64) == 8);
constexpr u32 magic = 0x4e52574b; // NRWK
constexpr u32 version = 1;
constexpr u64 max_surface_bytes = 128ull * 1024 * 1024;
constexpr u32 cross_adapter_surface = 1;
constexpr u32 presentation_staging_surface = 2;
struct AdapterInfo {
    u32 signature = magic;
    u32 abi = version;
    u32 bytes = sizeof(AdapterInfo);
    u32 ready = 0;
};
struct Surface {
    u32 signature = magic;
    u32 abi = version;
    u32 bytes = sizeof(Surface);
    u32 width = 0;
    u32 height = 0;
    u32 format = 0;
    u32 row_pitch = 0;
    u32 flags = 0;
    u64 allocation_bytes = 0;
};
// Private laboratory command: fill the entire single writable allocation in
// the OS-validated allocation list. No addresses or kernel handles are accepted.
struct FillSurface {
    u32 signature = magic;
    u32 abi = version;
    u32 bytes = sizeof(FillSurface);
    u32 operation = 1;
    u32 pattern = 0;
    u32 reserved = 0;
};
static_assert(sizeof(FillSurface) == 24);
// Copy between two OS-validated allocations (source first, destination second).
// Extent is derived from matching allocation descriptors, never user addresses.
struct CopySurface {
    u32 signature = magic;
    u32 abi = version;
    u32 bytes = sizeof(CopySurface);
    u32 operation = 2;
    u32 reserved[2]{};
};
static_assert(sizeof(CopySurface) == sizeof(FillSurface));
static_assert(sizeof(AdapterInfo) == 16 && sizeof(Surface) == 40);
constexpr bool valid(const Surface &s) {
    const bool cross = s.flags == cross_adapter_surface;
    const bool staging = s.flags == presentation_staging_surface;
    const u64 rows = cross ? (u64(s.height) + 3) & ~u64(3) : s.height;
    return s.signature == magic && s.abi == version && s.bytes == sizeof(Surface) &&
           (s.flags == 0 || cross || staging) && s.width && s.width <= 8192 && s.height && s.height <= 8192 &&
           (staging ? s.format == 88 : (s.format == 28 || s.format == 87)) && // BGRX8 / RGBA8 / BGRA8
           (cross ? s.row_pitch == ((u64(s.width) * 4 + 127) & ~u64(127)) :
                    s.row_pitch >= u64(s.width) * 4 && !(s.row_pitch % 256)) &&
           s.allocation_bytes && s.allocation_bytes <= max_surface_bytes &&
           !(s.allocation_bytes % 4096) &&
           u64(s.row_pitch) * rows <= s.allocation_bytes;
}
// BGRX ignores its fourth byte. Copying BGRA into BGRX preserves every defined
// channel; the reverse direction would need to synthesize alpha and is rejected.
constexpr bool copy_format_compatible(u32 source, u32 destination) {
    return source == destination || (source == 87 && destination == 88);
}
} // namespace nr::kernel_contract
