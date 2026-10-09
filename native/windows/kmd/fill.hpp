// A single bounded software fill for the isolated Windows submission test.
// This is not a D3D shader, NPU command, or general render command processor.
#pragma once
namespace nr::kmd {
struct FillMarker {
    ULONG magic = 0x4e52464c, reserved = 0;
    ULONGLONG offset = 0;
};
struct FillRecord {
    ULONG magic = 0x4e52464c, version = 1;
    ULONGLONG offset = 0, bytes = 0;
    ULONG pattern = 0, patched = 0;
};
static_assert(sizeof(FillMarker) == 16 && sizeof(FillRecord) == 32);
class Fill {
public:
    static bool valid(const FillRecord &record) {
        return record.magic == FillRecord{}.magic && record.version == 1 &&
            record.bytes && record.bytes <= Segment::transfer_bytes && !(record.bytes % sizeof(ULONG)) &&
            record.patched <= 1 && (record.patched || !record.offset) &&
            record.offset <= Segment::address_bytes - record.bytes;
    }
    static NTSTATUS snapshot(const void *data, UINT bytes, kernel_contract::FillSurface &out) {
        if (!data || bytes != sizeof(out)) return STATUS_INVALID_USER_BUFFER;
        kernel_contract::FillSurface command{};
        // pCommand is an untrusted user-mode mapping. Snapshot once before
        // validation; an invalid or concurrently unmapped page must not crash.
        __try {
            ProbeForRead(const_cast<void *>(data), sizeof(command), 1);
            RtlCopyMemory(&command, data, sizeof(command));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return STATUS_INVALID_USER_BUFFER; }
        if (command.signature != kernel_contract::magic || command.abi != kernel_contract::version ||
            command.bytes != sizeof(command) || command.operation != 1 || command.reserved)
            return STATUS_ILLEGAL_INSTRUCTION;
        out = command;
        return STATUS_SUCCESS;
    }
    static NTSTATUS locate(const Segment &segment, const DXGK_ALLOCATIONLIST &allocation,
                           FillRecord &record, bool allow_unresident) {
        if (!allocation.WriteOperation || allocation.Reserved) return STATUS_INVALID_PARAMETER;
        if (!allocation.SegmentId && allow_unresident) {
            record.offset = record.patched = 0;
            return STATUS_SUCCESS;
        }
        if (allocation.SegmentId != 1 || allocation.PhysicalAddress.QuadPart < 0 ||
            !segment.contains(ULONGLONG(allocation.PhysicalAddress.QuadPart), SIZE_T(record.bytes)))
            return STATUS_INVALID_PARAMETER;
        record.offset = ULONGLONG(allocation.PhysicalAddress.QuadPart);
        record.patched = 1;
        return STATUS_SUCCESS;
    }
    // The caller has checked allocation ownership and obtained its actual size.
    static NTSTATUS build(const Segment &segment, SIZE_T bytes, DXGKARG_RENDER &args) {
        if (!segment.ready()) return STATUS_DEVICE_NOT_READY;
        if (args.MultipassOffset || args.DmaBufferSegmentId || args.PatchLocationListInSize ||
            !args.pAllocationList || args.AllocationListSize != 1) return STATUS_NOT_SUPPORTED;
        kernel_contract::FillSurface command{};
        auto status = snapshot(args.pCommand, args.CommandLength, command);
        if (status != STATUS_SUCCESS) return status;
        if (args.DmaSize < sizeof(FillMarker) || args.DmaBufferPrivateDataSize < sizeof(FillRecord) ||
            !args.PatchLocationListOutSize) return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
        if (!args.pDmaBuffer || !args.pDmaBufferPrivateData || !args.pPatchLocationListOut)
            return STATUS_INVALID_PARAMETER;
        FillRecord record{};
        record.bytes = bytes;
        record.pattern = command.pattern;
        if (!valid(record)) return STATUS_INVALID_PARAMETER;
        status = locate(segment, args.pAllocationList[0], record, true);
        if (status != STATUS_SUCCESS) return status;
        FillMarker marker{};
        marker.offset = record.offset;
        D3DDDI_PATCHLOCATIONLIST location{};
        location.PatchOffset = FIELD_OFFSET(FillMarker, offset);
        RtlCopyMemory(args.pDmaBuffer, &marker, sizeof(marker));
        RtlCopyMemory(args.pDmaBufferPrivateData, &record, sizeof(record));
        *args.pPatchLocationListOut++ = location;
        args.pDmaBuffer = static_cast<UCHAR *>(args.pDmaBuffer) + sizeof(marker);
        args.pDmaBufferPrivateData = static_cast<UCHAR *>(args.pDmaBufferPrivateData) + sizeof(record);
        return STATUS_SUCCESS;
    }
    static NTSTATUS patch(const Segment &segment, SIZE_T bytes, const DXGKARG_PATCH &args) {
        if (args.Flags.Value || args.EngineOrdinal || args.DmaBufferSegmentId ||
            !args.pAllocationList || args.AllocationListSize != 1 || !args.pPatchLocationList ||
            args.PatchLocationListSubmissionLength != 1 ||
            args.PatchLocationListSubmissionStart >= args.PatchLocationListSize)
            return STATUS_NOT_SUPPORTED;
        const auto dma_start = args.DmaBufferSubmissionStartOffset;
        const auto dma_end = args.DmaBufferSubmissionEndOffset;
        const auto private_start = args.DmaBufferPrivateDataSubmissionStartOffset;
        const auto private_end = args.DmaBufferPrivateDataSubmissionEndOffset;
        if (!args.pDmaBuffer || !args.pDmaBufferPrivateData || dma_start > dma_end ||
            dma_end > args.DmaBufferSize || dma_end - dma_start != sizeof(FillMarker) ||
            private_start > private_end || private_end > args.DmaBufferPrivateDataSize ||
            private_end - private_start != sizeof(FillRecord) ||
            dma_end > MAXULONG_PTR - ULONG_PTR(args.pDmaBuffer) ||
            private_end > MAXULONG_PTR - ULONG_PTR(args.pDmaBufferPrivateData))
            return STATUS_INVALID_PARAMETER;
        auto dma = static_cast<UCHAR *>(args.pDmaBuffer) + dma_start;
        auto data = static_cast<UCHAR *>(args.pDmaBufferPrivateData) + private_start;
        FillRecord record{};
        FillMarker marker{};
        RtlCopyMemory(&record, data, sizeof(record));
        RtlCopyMemory(&marker, dma, sizeof(marker));
        if (!valid(record) || record.bytes != bytes || marker.magic != FillMarker{}.magic ||
            marker.reserved || marker.offset != record.offset) return STATUS_INVALID_PARAMETER;
        const auto &location = args.pPatchLocationList[args.PatchLocationListSubmissionStart];
        if (location.AllocationIndex || location.Value || location.DriverId || location.AllocationOffset ||
            location.PatchOffset != dma_start + FIELD_OFFSET(FillMarker, offset) ||
            location.SplitOffset != dma_start) return STATUS_INVALID_PARAMETER;
        const auto status = locate(segment, args.pAllocationList[0], record, false);
        if (status != STATUS_SUCCESS) return status;
        marker.offset = record.offset;
        RtlCopyMemory(dma, &marker, sizeof(marker));
        RtlCopyMemory(data, &record, sizeof(record));
        return STATUS_SUCCESS;
    }
    static NTSTATUS execution(const void *data, UINT bytes, PagingCommand &out) {
        if (!data || bytes != sizeof(FillRecord)) return STATUS_INVALID_PARAMETER;
        FillRecord record{};
        RtlCopyMemory(&record, data, sizeof(record));
        if (!valid(record) || !record.patched) return STATUS_INVALID_PARAMETER;
        PagingCommand command{};
        command.operation = DXGK_OPERATION_FILL;
        command.bytes = SIZE_T(record.bytes);
        command.pattern = record.pattern;
        command.destination.offset = record.offset;
        out = command;
        return STATUS_SUCCESS;
    }
};
} // namespace nr::kmd
