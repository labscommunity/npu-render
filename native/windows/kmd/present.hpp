// Restricted blit presentation: kernel-owned rectangle records, later executed
// by the ordered CPU worker. No flip, display ownership or NPU work is implied.
#pragma once
namespace nr::kmd {
struct PresentMarker {
    ULONG magic = 0x4e525052, reserved = 0;
    ULONGLONG source = 0, destination = 0, reserved2 = 0;
};
struct PresentRecord {
    ULONG magic = 0x4e525052, version = 1;
    ULONGLONG source = 0, destination = 0, source_relative = 0, destination_relative = 0;
    RowCopy shape{};
    ULONG patched = 0, reserved = 0;
};
static_assert(sizeof(PresentMarker) == 32 && sizeof(PresentRecord) == 64);
class Present {
public:
    static constexpr UINT max_records = 512;
    static bool valid(const PresentRecord &r) {
        if (r.magic != PresentRecord{}.magic || r.version != 1 || r.reserved || r.patched > 1 ||
            !r.shape.valid() || r.shape.row_bytes % 4 || r.shape.source_pitch % 4 || r.shape.destination_pitch % 4 ||
            r.source_relative % 4 || r.destination_relative % 4 ||
            r.source_relative > Segment::transfer_bytes - r.shape.source_span() ||
            r.destination_relative > Segment::transfer_bytes - r.shape.destination_span()) return false;
        if (!r.patched) return !r.source && !r.destination;
        if (r.source > Segment::address_bytes - r.shape.source_span() ||
            r.destination > Segment::address_bytes - r.shape.destination_span()) return false;
        return r.source + r.shape.source_span() <= r.destination || r.destination + r.shape.destination_span() <= r.source;
    }
    static bool matches(const PresentRecord &r, const kernel_contract::Surface &s, const kernel_contract::Surface &d) {
        return valid(r) && kernel_contract::valid(s) && kernel_contract::valid(d) && s.format == d.format &&
               r.shape.source_pitch == s.row_pitch && r.shape.destination_pitch == d.row_pitch &&
               r.shape.source_span() <= s.allocation_bytes && r.shape.destination_span() <= d.allocation_bytes &&
               r.source_relative <= s.allocation_bytes - r.shape.source_span() &&
               r.destination_relative <= d.allocation_bytes - r.shape.destination_span();
    }
    static NTSTATUS locate(const Segment &segment, const DXGK_ALLOCATIONLIST *list, PresentRecord &r, bool unresident) {
        ULONGLONG address[2]{}; bool resident = true;
        for (UINT i = 0; i < 2; ++i) {
            const auto &allocation = list[i + 1];
            const auto relative = i ? r.destination_relative : r.source_relative;
            const auto span = i ? r.shape.destination_span() : r.shape.source_span();
            if (allocation.Reserved || allocation.WriteOperation != i) return STATUS_INVALID_PARAMETER;
            if (!allocation.SegmentId && unresident) { resident = false; continue; }
            if (allocation.SegmentId != 1 || allocation.PhysicalAddress.QuadPart < 0 ||
                ULONGLONG(allocation.PhysicalAddress.QuadPart) > Segment::address_bytes - relative)
                return STATUS_INVALID_PARAMETER;
            address[i] = ULONGLONG(allocation.PhysicalAddress.QuadPart) + relative;
            if (!segment.contains(address[i], SIZE_T(span))) return STATUS_INVALID_PARAMETER;
        }
        r.source = resident ? address[0] : 0; r.destination = resident ? address[1] : 0; r.patched = resident;
        return valid(r) ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    }
    static NTSTATUS build(const Segment &segment, const kernel_contract::Surface &s,
                          const kernel_contract::Surface &d, DXGKARG_PRESENT &a) {
        if (!segment.is_aperture()) return STATUS_NOT_SUPPORTED;
        if (a.Flags.Value != 1 || a.DmaBufferSegmentId || !a.pAllocationList ||
            !kernel_contract::valid(s) || !kernel_contract::valid(d) || s.format != d.format)
            return STATUS_NOT_SUPPORTED;
        const auto width = LONGLONG(a.SrcRect.right) - a.SrcRect.left;
        const auto height = LONGLONG(a.SrcRect.bottom) - a.SrcRect.top;
        if (width <= 0 || height <= 0 || a.SrcRect.left < 0 || a.SrcRect.top < 0 ||
            a.SrcRect.right > LONG(s.width) || a.SrcRect.bottom > LONG(s.height) ||
            LONGLONG(a.DstRect.right) - a.DstRect.left != width || LONGLONG(a.DstRect.bottom) - a.DstRect.top != height)
            return STATUS_NOT_SUPPORTED; // No stretching or color conversion.
        const UINT total = a.SubRectCnt ? a.SubRectCnt : 1;
        if (a.MultipassOffset >= total || (a.SubRectCnt && !a.pDstSubRects)) return STATUS_INVALID_PARAMETER;
        UINT count = a.DmaSize / UINT(sizeof(PresentMarker));
        const UINT private_count = a.DmaBufferPrivateDataSize / UINT(sizeof(PresentRecord)), patch_count = a.PatchLocationListOutSize / 2;
        if (count > private_count) count = private_count;
        if (count > patch_count) count = patch_count;
        if (count > max_records) count = max_records;
        if (count > total - a.MultipassOffset) count = total - a.MultipassOffset;
        if (!count) return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
        if (!a.pDmaBuffer || !a.pDmaBufferPrivateData || !a.pPatchLocationListOut) return STATUS_INVALID_PARAMETER;
        // Validate this entire batch before modifying output or progress. The
        // rectangle list is owned by Dxgkrnl and stays stable during this call.
        for (UINT pass = 0; pass < 2; ++pass) for (UINT i = 0; i < count; ++i) {
            const auto rect = a.SubRectCnt ? a.pDstSubRects[a.MultipassOffset + i] : a.DstRect;
            if (rect.left < 0 || rect.top < 0 || rect.right <= rect.left || rect.bottom <= rect.top ||
                rect.right > LONG(d.width) || rect.bottom > LONG(d.height) ||
                rect.left < a.DstRect.left || rect.top < a.DstRect.top || rect.right > a.DstRect.right || rect.bottom > a.DstRect.bottom)
                return STATUS_INVALID_PARAMETER;
            PresentRecord r{};
            r.shape = {ULONG(rect.right - rect.left) * 4, ULONG(rect.bottom - rect.top), s.row_pitch, d.row_pitch};
            const auto sx = LONGLONG(a.SrcRect.left) + rect.left - a.DstRect.left;
            const auto sy = LONGLONG(a.SrcRect.top) + rect.top - a.DstRect.top;
            r.source_relative = ULONGLONG(sy) * s.row_pitch + ULONGLONG(sx) * 4;
            r.destination_relative = ULONGLONG(rect.top) * d.row_pitch + ULONGLONG(rect.left) * 4;
            if (!matches(r, s, d)) return STATUS_INVALID_PARAMETER;
            const auto status = locate(segment, a.pAllocationList, r, true);
            if (status != STATUS_SUCCESS) return status;
            if (!pass) continue;
            PresentMarker marker{}; marker.source = r.source; marker.destination = r.destination;
            RtlCopyMemory(static_cast<UCHAR *>(a.pDmaBuffer) + SIZE_T(i) * sizeof(marker), &marker, sizeof(marker));
            RtlCopyMemory(static_cast<UCHAR *>(a.pDmaBufferPrivateData) + SIZE_T(i) * sizeof(r), &r, sizeof(r));
            D3DDDI_PATCHLOCATIONLIST locations[2]{};
            for (UINT j = 0; j < 2; ++j) {
                locations[j].AllocationIndex = j + 1;
                locations[j].PatchOffset = i * UINT(sizeof(marker)) + (j ? FIELD_OFFSET(PresentMarker, destination) : FIELD_OFFSET(PresentMarker, source));
                locations[j].AllocationOffset = UINT(j ? r.destination_relative : r.source_relative);
                locations[j].SplitOffset = i * UINT(sizeof(marker));
            }
            RtlCopyMemory(a.pPatchLocationListOut + i * 2, locations, sizeof(locations));
        }
        a.pDmaBuffer = static_cast<UCHAR *>(a.pDmaBuffer) + SIZE_T(count) * sizeof(PresentMarker);
        a.pDmaBufferPrivateData = static_cast<UCHAR *>(a.pDmaBufferPrivateData) + SIZE_T(count) * sizeof(PresentRecord);
        a.pPatchLocationListOut += count * 2; a.MultipassOffset += count;
        return a.MultipassOffset == total ? STATUS_SUCCESS : STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    }
    static NTSTATUS patch(const Segment &segment, const kernel_contract::Surface &s,
                          const kernel_contract::Surface &d, const DXGKARG_PATCH &a) {
        DXGK_PATCHFLAGS allowed{}; allowed.Present = allowed.RedirectedPresent = 1;
        if (!a.Flags.Present || (a.Flags.Value & ~allowed.Value) || a.EngineOrdinal || a.DmaBufferSegmentId ||
            !a.pAllocationList || a.AllocationListSize != 3) return STATUS_NOT_SUPPORTED;
        const auto ds = a.DmaBufferSubmissionStartOffset, de = a.DmaBufferSubmissionEndOffset;
        const auto ps = a.DmaBufferPrivateDataSubmissionStartOffset, pe = a.DmaBufferPrivateDataSubmissionEndOffset;
        if (!a.pDmaBuffer || !a.pDmaBufferPrivateData || !a.pPatchLocationList || ds >= de || de > a.DmaBufferSize ||
            ps >= pe || pe > a.DmaBufferPrivateDataSize || (de - ds) % sizeof(PresentMarker) ||
            (pe - ps) % sizeof(PresentRecord) || (de - ds) / sizeof(PresentMarker) != (pe - ps) / sizeof(PresentRecord) ||
            de > MAXULONG_PTR - ULONG_PTR(a.pDmaBuffer) || pe > MAXULONG_PTR - ULONG_PTR(a.pDmaBufferPrivateData))
            return STATUS_INVALID_PARAMETER;
        const UINT count = (de - ds) / UINT(sizeof(PresentMarker));
        if (count > max_records || a.PatchLocationListSubmissionLength != count * 2 ||
            a.PatchLocationListSubmissionStart > a.PatchLocationListSize ||
            a.PatchLocationListSize - a.PatchLocationListSubmissionStart < count * 2) return STATUS_INVALID_PARAMETER;
        for (UINT pass = 0; pass < 2; ++pass) for (UINT i = 0; i < count; ++i) {
            auto dma = static_cast<UCHAR *>(a.pDmaBuffer) + ds + SIZE_T(i) * sizeof(PresentMarker);
            auto data = static_cast<UCHAR *>(a.pDmaBufferPrivateData) + ps + SIZE_T(i) * sizeof(PresentRecord);
            PresentRecord r{}; PresentMarker marker{};
            RtlCopyMemory(&r, data, sizeof(r)); RtlCopyMemory(&marker, dma, sizeof(marker));
            if (!matches(r, s, d) || marker.magic != PresentMarker{}.magic || marker.reserved || marker.reserved2 ||
                marker.source != r.source || marker.destination != r.destination) return STATUS_INVALID_PARAMETER;
            for (UINT j = 0; j < 2; ++j) {
                const auto &p = a.pPatchLocationList[a.PatchLocationListSubmissionStart + i * 2 + j];
                if (p.AllocationIndex != j + 1 || p.Value || p.DriverId ||
                    p.AllocationOffset != (j ? r.destination_relative : r.source_relative) ||
                    p.PatchOffset != ds + i * UINT(sizeof(marker)) + (j ? FIELD_OFFSET(PresentMarker, destination) : FIELD_OFFSET(PresentMarker, source)) ||
                    p.SplitOffset != ds + i * UINT(sizeof(marker))) return STATUS_INVALID_PARAMETER;
            }
            const auto status = locate(segment, a.pAllocationList, r, false);
            if (status != STATUS_SUCCESS) return status;
            if (!pass) continue;
            marker.source = r.source; marker.destination = r.destination;
            RtlCopyMemory(dma, &marker, sizeof(marker)); RtlCopyMemory(data, &r, sizeof(r));
        }
        return STATUS_SUCCESS;
    }
    static NTSTATUS execute(Segment &segment, const PresentRecord &r) {
        if (!segment.is_aperture()) return STATUS_NOT_SUPPORTED;
        if (!valid(r) || !r.patched) return STATUS_INVALID_PARAMETER;
        return segment.aperture().copy_rows(r.source, r.destination, r.shape);
    }
};
}
