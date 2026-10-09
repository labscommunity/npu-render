// Preallocated ownership of WDDM paging submissions. The caller serializes all
// queue/pool accesses and keeps borrowed VidMm MDLs alive through completion.
// This object does not execute commands or notify Windows of a fence.
#pragma once

namespace nr::kmd {
class Submissions {
public:
    static constexpr SIZE_T max_commands = PAGE_SIZE / sizeof(PagingMarker);
    static constexpr SIZE_T memory_budget = 64 * 1024 * 1024;
    struct Packet {
        Ticket ticket{};
        UINT commands = 0;
        PagingCommand paging[max_commands]{};
    };
    Submissions() = default;
    Submissions(const Submissions &) = delete;
    Submissions &operator=(const Submissions &) = delete;
    NTSTATUS prepare(Scheduler &scheduler, ULONG required) {
        if (ready() || scheduler.running() || scheduler.active() || scheduler.pending())
            return STATUS_INVALID_DEVICE_STATE;
        if (!required) return STATUS_INVALID_PARAMETER;
        // Reserve an additional slot for the active worker. Refuse excessive
        // requirements before startup; never silently cap Windows' requirement.
        const SIZE_T count = SIZE_T(required) + 1;
        if (count > memory_budget / (sizeof(Packet) + sizeof(Work)))
            return STATUS_INSUFFICIENT_RESOURCES;
        auto work = static_cast<Work *>(ExAllocatePool2(POOL_FLAG_NON_PAGED, count * sizeof(Work), tag));
        if (!work) return STATUS_INSUFFICIENT_RESOURCES;
        auto packets = static_cast<Packet *>(ExAllocatePool2(
            POOL_FLAG_NON_PAGED, count * sizeof(Packet), tag));
        if (!packets) {
            ExFreePoolWithTag(work, tag);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        for (SIZE_T i = 0; i < count; ++i) {
            new (work + i) Work{};
            new (packets + i) Packet{};
        }
        if (scheduler.set_storage(work, count) != QueueResult::ok) {
            ExFreePoolWithTag(packets, tag);
            ExFreePoolWithTag(work, tag);
            return STATUS_INVALID_DEVICE_STATE;
        }
        scheduler_ = &scheduler;
        work_ = work;
        packets_ = packets;
        count_ = count;
        return STATUS_SUCCESS;
    }
    NTSTATUS release() {
        if (!ready()) return STATUS_SUCCESS;
        if (scheduler_->set_storage(nullptr, 0) != QueueResult::ok) return STATUS_DEVICE_BUSY;
        ExFreePoolWithTag(packets_, tag);
        ExFreePoolWithTag(work_, tag);
        scheduler_ = nullptr;
        work_ = nullptr;
        packets_ = nullptr;
        count_ = 0;
        return STATUS_SUCCESS;
    }
    bool ready() const { return packets_ != nullptr; }
    SIZE_T capacity() const { return count_; }

    struct View {
        const UCHAR *data = nullptr;
        UINT dma_bytes = 0;
        UINT private_bytes = 0;
        UINT commands = 0;
    };
    // Shared inspection for patch and capture. This never allocates, queues,
    // executes or changes the caller's output on failure.
    static NTSTATUS inspect(const DXGKARG_SUBMITCOMMAND &args, View &view) {
        DXGK_SUBMITCOMMANDFLAGS allowed{};
        allowed.Paging = 1;
        allowed.ContextSwitch = 1;
        if ((args.Flags.Value & ~allowed.Value) || args.NodeOrdinal || args.EngineOrdinal ||
            args.DmaBufferSegmentId)
            return STATUS_NOT_SUPPORTED;
        // WDDM leaves private-data fields unspecified when its size is zero.
        const UINT private_start = args.DmaBufferPrivateDataSize ?
            args.DmaBufferPrivateDataSubmissionStartOffset : 0;
        const UINT private_end = args.DmaBufferPrivateDataSize ?
            args.DmaBufferPrivateDataSubmissionEndOffset : 0;
        if (args.DmaBufferSubmissionStartOffset > args.DmaBufferSubmissionEndOffset ||
            args.DmaBufferSubmissionEndOffset > args.DmaBufferSize ||
            private_start > private_end || private_end > args.DmaBufferPrivateDataSize)
            return STATUS_INVALID_PARAMETER;
        const UINT dma_bytes = args.DmaBufferSubmissionEndOffset - args.DmaBufferSubmissionStartOffset;
        const UINT private_bytes = private_end - private_start;
        UINT commands = 0;
        const UCHAR *data = nullptr;
        if (dma_bytes || private_bytes) {
            if (!args.Flags.Paging || args.Flags.ContextSwitch) return STATUS_NOT_SUPPORTED;
            if (!args.pDmaBufferPrivateData || args.DmaBufferSize > PAGE_SIZE ||
                args.DmaBufferSubmissionStartOffset % sizeof(PagingMarker) ||
                dma_bytes % sizeof(PagingMarker) ||
                private_start % sizeof(PagingCommand) ||
                private_bytes % sizeof(PagingCommand) ||
                private_bytes > Paging::private_bytes ||
                dma_bytes / sizeof(PagingMarker) != private_bytes / sizeof(PagingCommand))
                return STATUS_INVALID_PARAMETER;
            const auto base = ULONG_PTR(args.pDmaBufferPrivateData);
            if (private_end > MAXULONG_PTR - base)
                return STATUS_INVALID_PARAMETER;
            data = static_cast<const UCHAR *>(args.pDmaBufferPrivateData) +
                   private_start;
            commands = UINT(private_bytes / sizeof(PagingCommand));
            // The input is kernel-private paging data, not a user DMA stream.
            // Validate every record before publishing a ticket. Endpoint range
            // and MDL mapping checks remain in the deferred executor.
            for (UINT i = 0; i < commands; ++i) {
                PagingCommand command{};
                RtlCopyMemory(&command, data + SIZE_T(i) * sizeof(command), sizeof(command));
                if (command.magic != PagingCommand{}.magic || command.reserved)
                    return STATUS_INVALID_PARAMETER;
                switch (command.operation) {
                case DXGK_OPERATION_TRANSFER:
                    if (!command.bytes || command.bytes > Segment::bytes) return STATUS_INVALID_PARAMETER;
                    break;
                case DXGK_OPERATION_FILL:
                    if (!command.bytes || command.bytes > Segment::bytes ||
                        command.bytes % sizeof(ULONG) || command.destination.mdl)
                        return STATUS_INVALID_PARAMETER;
                    break;
                case DXGK_OPERATION_DISCARD_CONTENT:
                    if (command.bytes) return STATUS_INVALID_PARAMETER;
                    break;
                default:
                    return STATUS_NOT_SUPPORTED;
                }
            }
        }
        view = {data, dma_bytes, private_bytes, commands};
        return STATUS_SUCCESS;
    }
    static NTSTATUS patch(const DXGKARG_PATCH &args) {
        DXGK_PATCHFLAGS allowed{};
        allowed.Paging = 1;
        if ((args.Flags.Value & ~allowed.Value) || args.EngineOrdinal || args.DmaBufferSegmentId ||
            args.pAllocationList || args.AllocationListSize || args.pPatchLocationList ||
            args.PatchLocationListSize || args.PatchLocationListSubmissionStart ||
            args.PatchLocationListSubmissionLength) return STATUS_NOT_SUPPORTED;
        DXGKARG_SUBMITCOMMAND input{};
        input.Flags.Paging = args.Flags.Paging;
        input.DmaBufferSize = args.DmaBufferSize;
        input.DmaBufferSubmissionStartOffset = args.DmaBufferSubmissionStartOffset;
        input.DmaBufferSubmissionEndOffset = args.DmaBufferSubmissionEndOffset;
        input.pDmaBufferPrivateData = args.pDmaBufferPrivateData;
        input.DmaBufferPrivateDataSize = args.DmaBufferPrivateDataSize;
        input.DmaBufferPrivateDataSubmissionStartOffset = args.DmaBufferPrivateDataSubmissionStartOffset;
        input.DmaBufferPrivateDataSubmissionEndOffset = args.DmaBufferPrivateDataSubmissionEndOffset;
        View view{};
        const auto status = inspect(input, view);
        if (status != STATUS_SUCCESS) return status;
        if (view.dma_bytes) {
            const auto base = ULONG_PTR(args.pDmaBuffer);
            if (!base || args.DmaBufferSubmissionEndOffset > MAXULONG_PTR - base)
                return STATUS_INVALID_PARAMETER;
            for (UINT i = 0; i < view.commands; ++i) {
                PagingMarker marker{};
                RtlCopyMemory(&marker, static_cast<const UCHAR *>(args.pDmaBuffer) +
                    args.DmaBufferSubmissionStartOffset + SIZE_T(i) * sizeof(marker), sizeof(marker));
                if (marker.magic != PagingMarker{}.magic || marker.reserved)
                    return STATUS_INVALID_PARAMETER;
            }
        }
        // BuildPagingBuffer already resolved segment offsets and borrowed MDLs.
        // The pointer-free markers have no addresses or fence words to patch;
        // execution/notification happen later, using SubmitCommand's fence.
        return STATUS_SUCCESS;
    }
    NTSTATUS capture(const DXGKARG_SUBMITCOMMAND &args, Ticket &ticket) {
        if (!ready() || !scheduler_->running()) return STATUS_DEVICE_NOT_READY;
        View view{};
        const auto status = inspect(args, view);
        if (status != STATUS_SUCCESS) return status;
        Ticket accepted{};
        switch (scheduler_->submit(args.SubmissionFenceId, view.dma_bytes, accepted)) {
        case QueueResult::ok: break;
        case QueueResult::full: return STATUS_INSUFFICIENT_RESOURCES;
        case QueueResult::stopped: return STATUS_DEVICE_NOT_READY;
        default: return STATUS_INVALID_PARAMETER;
        }
        auto &packet = packets_[accepted.serial % count_];
        packet.ticket = accepted;
        packet.commands = view.commands;
        if (view.private_bytes) RtlCopyMemory(packet.paging, view.data, view.private_bytes);
        ticket = accepted;
        return STATUS_SUCCESS;
    }
    // Access is valid only while the caller owns the matching queue ticket.
    // A worker must finish all accesses before publishing completion or drain.
    const Packet *packet(Ticket ticket) const {
        if (!ready() || !ticket.serial) return nullptr;
        const auto &result = packets_[ticket.serial % count_];
        return result.ticket.serial == ticket.serial && result.ticket.epoch == ticket.epoch ?
            &result : nullptr;
    }
private:
    static constexpr ULONG tag = 'sRpN';
    Scheduler *scheduler_ = nullptr;
    Work *work_ = nullptr;
    Packet *packets_ = nullptr;
    SIZE_T count_ = 0;
};
} // namespace nr::kmd
