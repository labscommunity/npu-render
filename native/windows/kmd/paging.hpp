// WDDM 1.3 paging commands. Only kernel-private storage contains MDL pointers;
// the ordinary DMA buffer contains a pointer-free marker for each command.
// Building a command never modifies allocation contents. Execution belongs to
// a drained, serialized PASSIVE_LEVEL worker (not yet connected to submission).
#pragma once

namespace nr::kmd {
struct PagingEndpoint {
    PMDL mdl = nullptr; // Null means an offset in our owned memory segment.
    ULONGLONG offset = 0;
};
struct PagingCommand {
    ULONG magic = 0x4e525047;
    DXGK_BUILDPAGINGBUFFER_OPERATION operation{};
    SIZE_T bytes = 0;
    ULONG pattern = 0;
    ULONG reserved = 0;
    PagingEndpoint source{}, destination{};
};
struct PagingMarker {
    ULONG magic = 0x4e525047;
    ULONG reserved = 0;
};
static_assert(sizeof(PagingCommand) == 56 && sizeof(PagingMarker) == 8);

class Paging {
public:
    static constexpr UINT private_bytes = (PAGE_SIZE / sizeof(PagingMarker)) * sizeof(PagingCommand);

    static NTSTATUS build(const Segment &segment, DXGKARG_BUILDPAGINGBUFFER &args) {
        if (!segment.ready()) return STATUS_DEVICE_NOT_READY;
        if (args.MultipassOffset) return STATUS_INVALID_PARAMETER;
        // Every supported operation fits in one record. Exhaustion changes
        // neither cursor, remaining size, buffer contents nor destination data.
        if (args.DmaSize < sizeof(PagingMarker) ||
            args.DmaBufferPrivateDataSize < sizeof(PagingCommand))
            return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
        if (!args.pDmaBuffer || !args.pDmaBufferPrivateData) return STATUS_INVALID_PARAMETER;
        PagingCommand command{};
        command.operation = args.Operation;
        switch (args.Operation) {
        case DXGK_OPERATION_TRANSFER: {
            const auto &transfer = args.Transfer;
            command.bytes = transfer.TransferSize;
            if (!command.bytes || command.bytes > Segment::bytes) return STATUS_INVALID_PARAMETER;
            if (!endpoint(segment, transfer.Source.SegmentId, transfer.Source.SegmentAddress,
                          transfer.Source.pMdl, transfer.TransferOffset, transfer.MdlOffset,
                          command.bytes, command.source) ||
                !endpoint(segment, transfer.Destination.SegmentId, transfer.Destination.SegmentAddress,
                          transfer.Destination.pMdl, transfer.TransferOffset, transfer.MdlOffset,
                          command.bytes, command.destination)) return STATUS_INVALID_PARAMETER;
            break;
        }
        case DXGK_OPERATION_FILL:
            command.bytes = args.Fill.FillSize;
            command.pattern = args.Fill.FillPattern;
            if (!command.bytes || command.bytes % sizeof(ULONG) ||
                !endpoint(segment, args.Fill.Destination.SegmentId,
                          args.Fill.Destination.SegmentAddress, nullptr, 0, 0,
                          command.bytes, command.destination) || command.destination.mdl)
                return STATUS_INVALID_PARAMETER;
            break;
        case DXGK_OPERATION_DISCARD_CONTENT:
            // No residency metadata or swizzling state is maintained here.
            // Keep ordering with earlier work by emitting an explicit command.
            if (args.DiscardContent.SegmentId != 1 ||
                args.DiscardContent.SegmentAddress.QuadPart < 0 ||
                !segment.range(ULONGLONG(args.DiscardContent.SegmentAddress.QuadPart), 1))
                return STATUS_INVALID_PARAMETER;
            break;
        default:
            return STATUS_NOT_SUPPORTED;
        }
        const PagingMarker marker{};
        RtlCopyMemory(args.pDmaBuffer, &marker, sizeof(marker));
        RtlCopyMemory(args.pDmaBufferPrivateData, &command, sizeof(command));
        args.pDmaBuffer = static_cast<UCHAR *>(args.pDmaBuffer) + sizeof(marker);
        args.DmaSize -= sizeof(marker);
        args.pDmaBufferPrivateData = static_cast<UCHAR *>(args.pDmaBufferPrivateData) + sizeof(command);
        args.DmaBufferPrivateDataSize -= sizeof(command);
        return STATUS_SUCCESS;
    }

    // Only accept commands copied from VidMm's kernel-private paging data.
    // Never dispatch this method using a user-provided byte stream/MDL pointer.
    static NTSTATUS execute(const Segment &segment, const PagingCommand &command) {
        if (!segment.ready()) return STATUS_DEVICE_NOT_READY;
        if (command.magic != PagingCommand{}.magic || command.reserved)
            return STATUS_INVALID_PARAMETER;
        if (command.operation == DXGK_OPERATION_DISCARD_CONTENT)
            return command.bytes ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
        if (command.operation != DXGK_OPERATION_TRANSFER && command.operation != DXGK_OPERATION_FILL)
            return STATUS_NOT_SUPPORTED;
        if (!command.bytes || command.bytes > Segment::bytes) return STATUS_INVALID_PARAMETER;
        if (command.operation == DXGK_OPERATION_FILL &&
            (command.bytes % sizeof(ULONG) || command.destination.mdl)) return STATUS_INVALID_PARAMETER;
        Mapping destination;
        auto status = destination.prepare(segment, command.destination, command.bytes);
        if (!NT_SUCCESS(status)) return status;
        if (command.operation == DXGK_OPERATION_FILL) {
            // Byte copies also handle unaligned segment addresses safely.
            for (SIZE_T i = 0; i < command.bytes; i += sizeof(ULONG))
                RtlCopyMemory(static_cast<UCHAR *>(destination.address) + i,
                              &command.pattern, sizeof(command.pattern));
        } else {
            Mapping source;
            status = source.prepare(segment, command.source, command.bytes);
            if (!NT_SUCCESS(status)) return status;
            RtlMoveMemory(destination.address, source.address, command.bytes);
        }
        return STATUS_SUCCESS;
    }

private:
    static bool mdl_range(PMDL mdl, ULONGLONG offset, SIZE_T bytes) {
        if (!mdl || !bytes || offset > MmGetMdlByteCount(mdl)) return false;
        return bytes <= MmGetMdlByteCount(mdl) - offset;
    }
    static bool endpoint(const Segment &segment, UINT id, LARGE_INTEGER address, PMDL mdl,
                         UINT transfer_offset, UINT mdl_pages, SIZE_T bytes, PagingEndpoint &out) {
        if (id == 0) {
            // TransferOffset belongs only to segment locations. MdlOffset is
            // separately measured in pages, not bytes, from the MDL's range.
            const auto offset = ULONGLONG(mdl_pages) * PAGE_SIZE;
            if (!mdl_range(mdl, offset, bytes)) return false;
            out = {mdl, offset};
            return true;
        }
        if (id != 1 || address.QuadPart < 0) return false;
        const auto offset = ULONGLONG(address.QuadPart) + transfer_offset;
        if (!segment.range(offset, bytes)) return false;
        out = {nullptr, offset};
        return true;
    }
    struct Mapping {
        void *address = nullptr;
        PMDL partial = nullptr;
        Mapping() = default;
        Mapping(const Mapping &) = delete;
        Mapping &operator=(const Mapping &) = delete;
        ~Mapping() { if (partial) IoFreeMdl(partial); }
        NTSTATUS prepare(const Segment &segment, PagingEndpoint endpoint, SIZE_T bytes) {
            if (!endpoint.mdl) {
                address = segment.range(endpoint.offset, bytes);
                return address ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
            }
            if (!mdl_range(endpoint.mdl, endpoint.offset, bytes) || bytes > MAXULONG)
                return STATUS_INVALID_PARAMETER;
            // Preserve the borrowed MDL and its original mapping. A partial
            // MDL owns any new mapping and IoFreeMdl releases that mapping.
            const auto base = ULONG_PTR(MmGetMdlVirtualAddress(endpoint.mdl));
            if (endpoint.offset > MAXULONG_PTR - base ||
                bytes - 1 > MAXULONG_PTR - (base + SIZE_T(endpoint.offset)))
                return STATUS_INVALID_PARAMETER;
            auto start = reinterpret_cast<void *>(base + SIZE_T(endpoint.offset));
            partial = IoAllocateMdl(start, ULONG(bytes), FALSE, FALSE, nullptr);
            if (!partial) return STATUS_INSUFFICIENT_RESOURCES;
            IoBuildPartialMdl(endpoint.mdl, partial, start, ULONG(bytes));
            address = MmGetSystemAddressForMdlSafe(partial, HighPagePriority | MdlMappingNoExecute);
            return address ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
        }
    };
};
} // namespace nr::kmd
