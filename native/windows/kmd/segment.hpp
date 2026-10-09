// Driver-owned backing for the first WDDM 1.3 laboratory memory segment.
// The caller serializes lifecycle changes and drains all users before release.
// This does not implement paging, residency, or NPU mapping.
#pragma once
#include "aperture.hpp"

namespace nr::kmd {
class Segment {
public:
    static constexpr SIZE_T bytes = 64 * 1024 * 1024;
    static constexpr SIZE_T address_bytes = Aperture::bytes;
    static constexpr SIZE_T transfer_bytes = 128 * 1024 * 1024;
    Segment() = default;
    Segment(const Segment &) = delete;
    Segment &operator=(const Segment &) = delete;

    NTSTATUS prepare(SIZE_T requested = bytes) {
        if (ready()) return STATUS_INVALID_DEVICE_STATE;
        if (!requested || requested > bytes || requested % PAGE_SIZE)
            return STATUS_INVALID_PARAMETER;
        PHYSICAL_ADDRESS low{}, high{}, boundary{};
        high.QuadPart = MAXLONGLONG;
        // This is a memory segment, not a cache-coherent aperture. Match the
        // write-combined mappings VidMm uses for our uncached allocations.
        auto memory = MmAllocateContiguousMemorySpecifyCache(requested, low, high, boundary, MmWriteCombined);
        if (!memory) return STATUS_INSUFFICIENT_RESOURCES;
        const auto physical = MmGetPhysicalAddress(memory);
        if (physical.QuadPart < 0 || (physical.QuadPart & (PAGE_SIZE - 1)) ||
            physical.QuadPart > MAXLONGLONG - LONGLONG(requested)) {
            MmFreeContiguousMemory(memory);
            return STATUS_INVALID_ADDRESS;
        }
        // VidMm may expose this memory to user mode. Never expose stale pages.
        RtlZeroMemory(memory, requested);
        KeMemoryBarrier(); // Publish initialized bytes before exposing the segment.
        physical_ = physical;
        memory_ = memory;
        committed_ = requested;
        return STATUS_SUCCESS;
    }
    void release() {
        aperture_.release();
        if (memory_) MmFreeContiguousMemory(memory_);
        memory_ = nullptr;
        physical_.QuadPart = 0;
        committed_ = 0;
    }
    NTSTATUS prepare_aperture() {
        return ready() ? STATUS_INVALID_DEVICE_STATE : aperture_.prepare();
    }
    bool is_aperture() const { return aperture_.ready(); }
    Aperture &aperture() { return aperture_; }
    bool ready() const { return memory_ != nullptr || is_aperture(); }
    SIZE_T capacity() const { return is_aperture() ? Aperture::bytes : committed_; }
    bool contains(ULONGLONG offset, SIZE_T length) const {
        return ready() && length && offset < capacity() && length <= capacity() - offset;
    }
    void *range(ULONGLONG offset, SIZE_T length) const {
        if (!memory_ || !length || offset >= committed_ || length > committed_ - offset) return nullptr;
        return static_cast<UCHAR *>(memory_) + SIZE_T(offset);
    }
    NTSTATUS describe(DXGK_QUERYSEGMENTOUT3 &out) const {
        if (!ready()) return STATUS_DEVICE_NOT_READY;
        if (!out.pSegmentDescriptor) {
            // The first query owns only NbSegment. Leave all other fields alone.
            out.NbSegment = 1;
            return STATUS_SUCCESS;
        }
        if (out.NbSegment != 1) return STATUS_INVALID_PARAMETER;
        DXGK_SEGMENTDESCRIPTOR3 descriptor{};
        if (is_aperture()) {
            // CPU accesses use separate mappings of actual VidMm pages. There
            // is no CPU-visible bus range and neither address is fabricated.
            descriptor.Flags.Aperture = descriptor.Flags.CacheCoherent = 1;
            descriptor.Size = descriptor.CommitLimit = Aperture::bytes;
            *out.pSegmentDescriptor = descriptor;
            out.PagingBufferSegmentId = 0;
            out.PagingBufferSize = PAGE_SIZE;
            out.PagingBufferPrivateDataSize = 0;
            return STATUS_SUCCESS;
        }
        descriptor.Flags.CpuVisible = 1;
        descriptor.Flags.PopulatedFromSystemMemory = 1;
        // CacheCoherent is defined only for aperture segments; keep it clear.
        // Our software engine uses offsets. CPU access uses the actual reserved
        // physical pages; there is no fabricated aperture/MMIO address.
        descriptor.BaseAddress.QuadPart = 0;
        descriptor.CpuTranslatedAddress = physical_;
        descriptor.Size = committed_;
        descriptor.CommitLimit = committed_;
        *out.pSegmentDescriptor = descriptor;
        out.PagingBufferSegmentId = 0; // VidMm-owned contiguous system memory.
        out.PagingBufferSize = PAGE_SIZE;
        out.PagingBufferPrivateDataSize = 0;
        return STATUS_SUCCESS;
    }
private:
    Aperture aperture_;
    void *memory_ = nullptr;
    PHYSICAL_ADDRESS physical_{};
    SIZE_T committed_ = 0;
};
} // namespace nr::kmd
