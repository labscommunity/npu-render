// Opt-in isolated-guest experiment. Exercises owned paging memory, a system
// thread and Dxgkrnl's DPC route without starting a graphics adapter or reporting
// any Windows DMA fence. All asynchronous users drain before StartDevice exits.
#pragma once

namespace nr::kmd {
struct WorkerProbe {
    NTSTATUS status = STATUS_NOT_SUPPORTED;
    NTSTATUS create_status = STATUS_NOT_SUPPORTED;
    NTSTATUS memory_status = STATUS_NOT_SUPPORTED;
    NTSTATUS sync_status = STATUS_NOT_SUPPORTED;
    NTSTATUS dpc_wait_status = STATUS_NOT_SUPPORTED;
    NTSTATUS close_status = STATUS_NOT_SUPPORTED;
    KIRQL worker_irql = 0xff, sync_irql = 0xff, dpc_irql = 0xff;
    ULONG sync_calls = 0;
    ULONG memory_phase = 0;
    ULONG backing_attempts = 0;
    NTSTATUS first_backing_status = STATUS_NOT_SUPPORTED;
    SIZE_T backing_bytes = 0;
    volatile LONG dpc_calls = 0, active = 0;
    BOOLEAN queued = FALSE, returned = FALSE, joined = FALSE;
    DXGKRNL_INTERFACE kernel{};
    KEVENT dpc_done{};

    void run(const DXGKRNL_INTERFACE &callbacks) {
        *this = WorkerProbe{};
        if (KeGetCurrentIrql() != PASSIVE_LEVEL || !callbacks.DeviceHandle ||
            !callbacks.DxgkCbSynchronizeExecution || !callbacks.DxgkCbQueueDpc) {
            status = STATUS_INVALID_PARAMETER;
            return;
        }
        kernel = callbacks;
        KeInitializeEvent(&dpc_done, NotificationEvent, FALSE);
        InterlockedExchange(&active, 1);
        OBJECT_ATTRIBUTES attributes{};
        InitializeObjectAttributes(&attributes, nullptr, OBJ_KERNEL_HANDLE, nullptr, nullptr);
        HANDLE thread = nullptr;
        create_status = PsCreateSystemThread(&thread, SYNCHRONIZE, &attributes,
                                            nullptr, nullptr, thread_main, this);
        if (create_status != STATUS_SUCCESS) {
            status = create_status;
            InterlockedExchange(&active, 0);
            RtlZeroMemory(&kernel, sizeof(kernel));
            return;
        }
        // This private, non-alertable kernel handle is kept until the thread
        // terminates. Never return an adapter context while a worker can use it.
        while (ZwWaitForSingleObject(thread, FALSE, nullptr) != STATUS_SUCCESS) {
            LARGE_INTEGER retry{};
            retry.QuadPart = -10000000;
            KeDelayExecutionThread(KernelMode, FALSE, &retry);
        }
        joined = TRUE;
        close_status = ZwClose(thread);
        // The worker is the sole producer and is now joined. This lab-only
        // teardown also drains a callback that arrived after the timed wait.
        KeFlushQueuedDpcs();
        InterlockedExchange(&active, 0);
        status = memory_status;
        if (status == STATUS_SUCCESS &&
            (worker_irql != PASSIVE_LEVEL || sync_status != STATUS_SUCCESS ||
             sync_calls != 1 || sync_irql <= DISPATCH_LEVEL || sync_irql >= PROFILE_LEVEL ||
             !returned || !queued || dpc_wait_status != STATUS_SUCCESS ||
             dpc_calls != 1 || dpc_irql != DISPATCH_LEVEL || close_status != STATUS_SUCCESS))
            status = STATUS_UNSUCCESSFUL;
        RtlZeroMemory(&kernel, sizeof(kernel));
    }

    void observe_dpc() {
        if (!InterlockedCompareExchange(&active, 0, 0)) return;
        dpc_irql = KeGetCurrentIrql();
        InterlockedIncrement(&dpc_calls);
        if (dpc_irql == DISPATCH_LEVEL) KeSetEvent(&dpc_done, IO_NO_INCREMENT, FALSE);
    }

private:
    static BOOLEAN synchronize(void *context) {
        auto &probe = *static_cast<WorkerProbe *>(context);
        ++probe.sync_calls;
        probe.sync_irql = KeGetCurrentIrql();
        if (probe.sync_calls != 1 || probe.sync_irql <= DISPATCH_LEVEL ||
            probe.sync_irql >= PROFILE_LEVEL) return FALSE;
        probe.queued = probe.kernel.DxgkCbQueueDpc(probe.kernel.DeviceHandle);
        return probe.queued;
    }
    static void thread_main(void *context) {
        auto &probe = *static_cast<WorkerProbe *>(context);
        probe.worker_irql = KeGetCurrentIrql();
        probe.memory_status = probe.worker_irql == PASSIVE_LEVEL ?
            paging_round_trip(probe) : STATUS_INVALID_DEVICE_STATE;
        if (probe.memory_status == STATUS_SUCCESS) {
            probe.sync_status = probe.kernel.DxgkCbSynchronizeExecution(
                probe.kernel.DeviceHandle, synchronize, &probe, 0, &probe.returned);
            if (probe.sync_status == STATUS_SUCCESS && probe.returned && probe.queued) {
                LARGE_INTEGER timeout{};
                timeout.QuadPart = -50000000;
                probe.dpc_wait_status = KeWaitForSingleObject(
                    &probe.dpc_done, Executive, KernelMode, FALSE, &timeout);
            }
        }
        PsTerminateSystemThread(STATUS_SUCCESS);
    }
    struct Memory {
        static constexpr ULONG tag = 'wRpN';
        Segment segment;
        UCHAR *buffer = nullptr;
        PMDL mdl = nullptr;
        ~Memory() {
            if (mdl) IoFreeMdl(mdl);
            if (buffer) ExFreePoolWithTag(buffer, tag);
            segment.release();
        }
    };
    static NTSTATUS build(const Segment &segment, DXGKARG_BUILDPAGINGBUFFER &args,
                          PagingCommand &command) {
        PagingMarker marker{};
        args.pDmaBuffer = &marker;
        args.DmaSize = sizeof(marker);
        args.pDmaBufferPrivateData = &command;
        args.DmaBufferPrivateDataSize = sizeof(command);
        return Paging::build(segment, args);
    }
    static NTSTATUS paging_round_trip(WorkerProbe &probe) {
        Memory memory;
        auto &phase = probe.memory_phase;
        phase = 1;
        NTSTATUS result = STATUS_INSUFFICIENT_RESOURCES;
        // Explicit lab sizing experiment, not a silent runtime fallback.
        // The normal adapter still requests the original 64 MiB budget.
        const SIZE_T sizes[] = {Segment::bytes, 16 * 1024 * 1024, 1024 * 1024, 65536};
        for (const auto requested : sizes) {
            ++probe.backing_attempts;
            result = memory.segment.prepare(requested);
            if (probe.backing_attempts == 1) probe.first_backing_status = result;
            if (result != STATUS_INSUFFICIENT_RESOURCES) break;
        }
        if (result != STATUS_SUCCESS) return result;
        probe.backing_bytes = memory.segment.capacity();
        phase = 2;
        memory.buffer = static_cast<UCHAR *>(ExAllocatePool2(
            POOL_FLAG_NON_PAGED, 4 * PAGE_SIZE + 37, Memory::tag));
        if (!memory.buffer) return STATUS_INSUFFICIENT_RESOURCES;
        RtlFillMemory(memory.buffer, 4 * PAGE_SIZE + 37, 0xa5);
        auto data = memory.buffer + 37;
        phase = 3;
        memory.mdl = IoAllocateMdl(data, 4 * PAGE_SIZE, FALSE, FALSE, nullptr);
        if (!memory.mdl) return STATUS_INSUFFICIENT_RESOURCES;
        MmBuildMdlForNonPagedPool(memory.mdl);
        const MDL original = *memory.mdl;
        constexpr SIZE_T length = 1024;
        auto destination = static_cast<UCHAR *>(memory.segment.range(9019, length));
        for (SIZE_T i = 0; i < length; ++i) data[PAGE_SIZE + i] = UCHAR((i % 251) ^ 0x5a);

        DXGKARG_BUILDPAGINGBUFFER args{};
        args.Operation = DXGK_OPERATION_FILL;
        args.Fill.Destination.SegmentId = 1;
        args.Fill.Destination.SegmentAddress.QuadPart = 9019;
        args.Fill.FillSize = length;
        args.Fill.FillPattern = 0x04030201;
        PagingCommand command{};
        phase = 4;
        result = build(memory.segment, args, command);
        if (result != STATUS_SUCCESS) return result;
        for (SIZE_T i = 0; i < length; ++i)
            if (destination[i] != 0) return STATUS_DATA_ERROR;
        phase = 5;
        result = Paging::execute(memory.segment, command);
        if (result != STATUS_SUCCESS) return result;
        for (SIZE_T i = 0; i < length; ++i)
            if (destination[i] != (i % 4) + 1) return STATUS_DATA_ERROR;

        args = {};
        args.Operation = DXGK_OPERATION_TRANSFER;
        args.Transfer.TransferSize = length;
        args.Transfer.TransferOffset = 19;
        args.Transfer.MdlOffset = 1;
        args.Transfer.Source.pMdl = memory.mdl;
        args.Transfer.Destination.SegmentId = 1;
        args.Transfer.Destination.SegmentAddress.QuadPart = 9000;
        phase = 6;
        result = build(memory.segment, args, command);
        if (result != STATUS_SUCCESS) return result;
        for (SIZE_T i = 0; i < length; ++i)
            if (destination[i] != (i % 4) + 1) return STATUS_DATA_ERROR;
        phase = 7;
        result = Paging::execute(memory.segment, command);
        if (result != STATUS_SUCCESS) return result;
        for (SIZE_T i = 0; i < length; ++i)
            if (destination[i] != UCHAR((i % 251) ^ 0x5a)) return STATUS_DATA_ERROR;

        args = {};
        args.Operation = DXGK_OPERATION_TRANSFER;
        args.Transfer.TransferSize = length;
        args.Transfer.TransferOffset = 19;
        args.Transfer.MdlOffset = 2;
        args.Transfer.Source.SegmentId = 1;
        args.Transfer.Source.SegmentAddress.QuadPart = 9000;
        args.Transfer.Destination.pMdl = memory.mdl;
        phase = 8;
        result = build(memory.segment, args, command);
        if (result != STATUS_SUCCESS) return result;
        for (SIZE_T i = 0; i < length; ++i)
            if (data[2 * PAGE_SIZE + i] != 0xa5) return STATUS_DATA_ERROR;
        phase = 9;
        result = Paging::execute(memory.segment, command);
        if (result != STATUS_SUCCESS) return result;
        for (SIZE_T i = 0; i < length; ++i)
            if (data[2 * PAGE_SIZE + i] != UCHAR((i % 251) ^ 0x5a)) return STATUS_DATA_ERROR;
        if (data[2 * PAGE_SIZE - 1] != 0xa5 || data[2 * PAGE_SIZE + length] != 0xa5 ||
            RtlCompareMemory(memory.mdl, &original, sizeof(original)) != sizeof(original))
            return STATUS_DATA_ERROR;
        phase = 10;
        return STATUS_SUCCESS;
    }
};
} // namespace nr::kmd
