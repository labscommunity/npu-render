// Submitted segment-to-segment copy for non-lockable shared targets. Execution
// uses the laboratory CPU worker; no user address or private copy buffer exists.
#pragma once
namespace nr::kmd {
struct CopyMarker {
    ULONG magic = 0x4e524350, reserved = 0;
    ULONGLONG source = 0, destination = 0;
};
struct CopyRecord {
    ULONG magic = 0x4e524350, version = 1;
    ULONGLONG source = 0, destination = 0, bytes = 0;
    ULONG patched = 0, reserved = 0;
};
static_assert(sizeof(CopyMarker) == 24 && sizeof(CopyRecord) == 40);
class Copy {
public:
    static bool valid(const CopyRecord &r) {
        if (r.magic != CopyRecord{}.magic || r.version != 1 || r.reserved ||
            !r.bytes || r.bytes > Segment::transfer_bytes || r.patched > 1 ||
            r.source > Segment::address_bytes - r.bytes || r.destination > Segment::address_bytes - r.bytes)
            return false;
        if (!r.patched) return !r.source && !r.destination;
        return r.source + r.bytes <= r.destination || r.destination + r.bytes <= r.source;
    }
    static NTSTATUS locate(const Segment &segment, const DXGK_ALLOCATIONLIST *list,
                           CopyRecord &r, bool allow_unresident) {
        if (list[0].WriteOperation || !list[1].WriteOperation || list[0].Reserved || list[1].Reserved)
            return STATUS_INVALID_PARAMETER;
        ULONGLONG offsets[2]{};
        bool resident = true;
        for (UINT i = 0; i < 2; ++i) {
            if (!list[i].SegmentId && allow_unresident) { resident = false; continue; }
            if (list[i].SegmentId != 1 || list[i].PhysicalAddress.QuadPart < 0 ||
                !segment.contains(ULONGLONG(list[i].PhysicalAddress.QuadPart), SIZE_T(r.bytes)))
                return STATUS_INVALID_PARAMETER;
            offsets[i] = ULONGLONG(list[i].PhysicalAddress.QuadPart);
        }
        r.source = resident ? offsets[0] : 0;
        r.destination = resident ? offsets[1] : 0;
        r.patched = resident ? 1 : 0;
        return valid(r) ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    }
    static NTSTATUS build(const Segment &segment, SIZE_T bytes, DXGKARG_RENDER &a) {
        if (!segment.ready()) return STATUS_DEVICE_NOT_READY;
        if (a.MultipassOffset || a.DmaBufferSegmentId || a.PatchLocationListInSize ||
            !a.pAllocationList || a.AllocationListSize != 2) return STATUS_NOT_SUPPORTED;
        kernel_contract::CopySurface command{};
        if (!a.pCommand || a.CommandLength != sizeof(command)) return STATUS_INVALID_USER_BUFFER;
        __try {
            ProbeForRead(const_cast<void *>(a.pCommand), sizeof(command), 1);
            RtlCopyMemory(&command, a.pCommand, sizeof(command));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return STATUS_INVALID_USER_BUFFER; }
        if (command.signature != kernel_contract::magic || command.abi != kernel_contract::version ||
            command.bytes != sizeof(command) || command.operation != 2 || command.reserved[0] || command.reserved[1])
            return STATUS_ILLEGAL_INSTRUCTION;
        if (a.DmaSize < sizeof(CopyMarker) || a.DmaBufferPrivateDataSize < sizeof(CopyRecord) ||
            a.PatchLocationListOutSize < 2) return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
        if (!a.pDmaBuffer || !a.pDmaBufferPrivateData || !a.pPatchLocationListOut)
            return STATUS_INVALID_PARAMETER;
        CopyRecord record{}; record.bytes = bytes;
        const auto status = locate(segment, a.pAllocationList, record, true);
        if (status != STATUS_SUCCESS) return status;
        CopyMarker marker{}; marker.source = record.source; marker.destination = record.destination;
        D3DDDI_PATCHLOCATIONLIST locations[2]{};
        locations[0].PatchOffset = FIELD_OFFSET(CopyMarker, source);
        locations[1].AllocationIndex = 1;
        locations[1].PatchOffset = FIELD_OFFSET(CopyMarker, destination);
        RtlCopyMemory(a.pDmaBuffer, &marker, sizeof(marker));
        RtlCopyMemory(a.pDmaBufferPrivateData, &record, sizeof(record));
        RtlCopyMemory(a.pPatchLocationListOut, locations, sizeof(locations));
        a.pDmaBuffer = static_cast<UCHAR *>(a.pDmaBuffer) + sizeof(marker);
        a.pDmaBufferPrivateData = static_cast<UCHAR *>(a.pDmaBufferPrivateData) + sizeof(record);
        a.pPatchLocationListOut += 2;
        return STATUS_SUCCESS;
    }
    static NTSTATUS patch(const Segment &segment, SIZE_T bytes, const DXGKARG_PATCH &a) {
        if (a.Flags.Value || a.EngineOrdinal || a.DmaBufferSegmentId || !a.pAllocationList ||
            a.AllocationListSize != 2 || !a.pPatchLocationList || a.PatchLocationListSubmissionLength != 2 ||
            a.PatchLocationListSubmissionStart > a.PatchLocationListSize ||
            a.PatchLocationListSize - a.PatchLocationListSubmissionStart < 2) return STATUS_NOT_SUPPORTED;
        const auto ds = a.DmaBufferSubmissionStartOffset, de = a.DmaBufferSubmissionEndOffset;
        const auto ps = a.DmaBufferPrivateDataSubmissionStartOffset, pe = a.DmaBufferPrivateDataSubmissionEndOffset;
        if (!a.pDmaBuffer || !a.pDmaBufferPrivateData || ds > de || de > a.DmaBufferSize ||
            de - ds != sizeof(CopyMarker) || ps > pe || pe > a.DmaBufferPrivateDataSize ||
            pe - ps != sizeof(CopyRecord) || de > MAXULONG_PTR - ULONG_PTR(a.pDmaBuffer) ||
            pe > MAXULONG_PTR - ULONG_PTR(a.pDmaBufferPrivateData)) return STATUS_INVALID_PARAMETER;
        auto dma = static_cast<UCHAR *>(a.pDmaBuffer) + ds;
        auto data = static_cast<UCHAR *>(a.pDmaBufferPrivateData) + ps;
        CopyRecord record{}; CopyMarker marker{};
        RtlCopyMemory(&record, data, sizeof(record)); RtlCopyMemory(&marker, dma, sizeof(marker));
        if (!valid(record) || record.bytes != bytes || marker.magic != CopyMarker{}.magic || marker.reserved ||
            marker.source != record.source || marker.destination != record.destination) return STATUS_INVALID_PARAMETER;
        for (UINT i = 0; i < 2; ++i) {
            const auto &p = a.pPatchLocationList[a.PatchLocationListSubmissionStart + i];
            const UINT offset = i ? FIELD_OFFSET(CopyMarker, destination) : FIELD_OFFSET(CopyMarker, source);
            if (p.AllocationIndex != i || p.Value || p.DriverId || p.AllocationOffset ||
                p.PatchOffset != ds + offset || p.SplitOffset != ds) return STATUS_INVALID_PARAMETER;
        }
        const auto status = locate(segment, a.pAllocationList, record, false);
        if (status != STATUS_SUCCESS) return status;
        marker.source = record.source; marker.destination = record.destination;
        RtlCopyMemory(dma, &marker, sizeof(marker)); RtlCopyMemory(data, &record, sizeof(record));
        return STATUS_SUCCESS;
    }
    static NTSTATUS execution(const void *data, UINT bytes, PagingCommand &out) {
        if (!data || bytes != sizeof(CopyRecord)) return STATUS_INVALID_PARAMETER;
        CopyRecord record{}; RtlCopyMemory(&record, data, sizeof(record));
        if (!valid(record) || !record.patched) return STATUS_INVALID_PARAMETER;
        PagingCommand command{};
        command.operation = DXGK_OPERATION_TRANSFER; command.bytes = SIZE_T(record.bytes);
        command.source.offset = record.source; command.destination.offset = record.destination;
        out = command;
        return STATUS_SUCCESS;
    }
};
} // namespace nr::kmd
