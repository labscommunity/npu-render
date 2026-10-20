// Isolated, start-disabled concurrency experiment. Windows supplies real
// synchronization/DPC routing. Completion notifications terminate at our own
// observers: invented test fences must NEVER reach the Windows GPU scheduler.
#pragma once
namespace nr::kmd {
struct EngineProbe {
    NTSTATUS status = STATUS_NOT_SUPPORTED, stop_status = STATUS_NOT_SUPPORTED;
    ULONG phase = 0, required_entries = 0;
    SIZE_T queue_entries = 0;
    volatile LONG notifications = 0, dpcs = 0, dpc_observed = 0;
    KIRQL notify_irql = 0xff, dpc_irql = 0xff, worker_irql = 0xff;
    DXGKARGCB_NOTIFY_INTERRUPT_DATA observed[3]{};
    DXGKRNL_INTERFACE original{};
    KEVENT changed{};

    void run(const DXGKRNL_INTERFACE &kernel, ULONG required, Engine &engine,
             Scheduler &queue, Submissions &submissions, Segment &segment) {
        *this = EngineProbe{};
        required_entries = required;
        if (KeGetCurrentIrql() != PASSIVE_LEVEL || !kernel.DeviceHandle ||
            !kernel.DxgkCbSynchronizeExecution || !kernel.DxgkCbQueueDpc ||
            !kernel.DxgkCbNotifyInterrupt || !kernel.DxgkCbNotifyDpc) {
            status = STATUS_INVALID_PARAMETER;
            return;
        }
        original = kernel;
        KeInitializeEvent(&changed, SynchronizationEvent, FALSE);
        status = exercise(engine, queue, submissions, segment);
        // Retain the adapter until every asynchronous user has drained. A
        // synchronization failure is not permission to free a live context.
        do {
            stop_status = engine.stop();
            if (stop_status != STATUS_SUCCESS) {
                LARGE_INTEGER retry{};
                retry.QuadPart = -10000000;
                KeDelayExecutionThread(KernelMode, FALSE, &retry);
            }
        } while (stop_status != STATUS_SUCCESS);
        worker_irql = engine.worker_irql_after_join();
        const auto released = submissions.release();
        if (released != STATUS_SUCCESS) status = released;
        if (released == STATUS_SUCCESS) segment.release();
        RtlZeroMemory(&original, sizeof(original));
    }
private:
    static NTSTATUS synchronize(HANDLE device, PKSYNCHRONIZE_ROUTINE routine,
                                 void *context, ULONG message, BOOLEAN *returned) {
        const auto self = static_cast<EngineProbe *>(device);
        return self->original.DxgkCbSynchronizeExecution(
            self->original.DeviceHandle, routine, context, message, returned);
    }
    static BOOLEAN queue_dpc(HANDLE device) {
        const auto self = static_cast<EngineProbe *>(device);
        return self->original.DxgkCbQueueDpc(self->original.DeviceHandle);
    }
    static void notify_interrupt(HANDLE device, DXGKARGCB_NOTIFY_INTERRUPT_DATA *data) {
        auto &self = *static_cast<EngineProbe *>(device);
        self.notify_irql = KeGetCurrentIrql();
        const auto index = InterlockedCompareExchange(&self.notifications, 0, 0);
        if (index < 3) self.observed[index] = *data;
        InterlockedIncrement(&self.notifications);
        // This observer deliberately does not call DxgkCbNotifyInterrupt.
    }
    static void notify_dpc(HANDLE device) {
        auto &self = *static_cast<EngineProbe *>(device);
        self.dpc_irql = KeGetCurrentIrql();
        InterlockedIncrement(&self.dpcs);
        InterlockedExchange(&self.dpc_observed, InterlockedCompareExchange(&self.notifications, 0, 0));
        if (self.dpc_irql == DISPATCH_LEVEL) KeSetEvent(&self.changed, IO_NO_INCREMENT, FALSE);
        // This observer deliberately does not call DxgkCbNotifyDpc.
    }
    DXGKRNL_INTERFACE callbacks() {
        auto result = original;
        result.DeviceHandle = this;
        result.DxgkCbSynchronizeExecution = synchronize;
        result.DxgkCbQueueDpc = queue_dpc;
        result.DxgkCbNotifyInterrupt = notify_interrupt;
        result.DxgkCbNotifyDpc = notify_dpc;
        return result;
    }
    NTSTATUS wait_for(LONG count) {
        LARGE_INTEGER timeout{};
        timeout.QuadPart = -50000000;
        while (InterlockedCompareExchange(&dpc_observed, 0, 0) < count) {
            const auto result = KeWaitForSingleObject(&changed, Executive, KernelMode, FALSE, &timeout);
            if (result != STATUS_SUCCESS) return result;
        }
        return STATUS_SUCCESS;
    }
    NTSTATUS exercise(Engine &engine, Scheduler &queue, Submissions &submissions, Segment &segment) {
        phase = 1;
        auto result = segment.prepare(16 * 1024 * 1024); // Explicit lab capacity, no hidden fallback.
        if (result != STATUS_SUCCESS) return result;
        phase = 2;
        result = submissions.prepare(queue, required_entries);
        if (result != STATUS_SUCCESS) return result;
        queue_entries = submissions.capacity();
        phase = 3;
        result = engine.start(callbacks(), queue, submissions, segment);
        if (result != STATUS_SUCCESS) return result;
        PagingCommand command{};
        PagingMarker marker{};
        DXGKARG_BUILDPAGINGBUFFER build{};
        build.pDmaBuffer = &marker;
        build.DmaSize = sizeof(marker);
        build.pDmaBufferPrivateData = &command;
        build.DmaBufferPrivateDataSize = sizeof(command);
        build.Operation = DXGK_OPERATION_FILL;
        build.Fill.FillSize = 1024;
        build.Fill.FillPattern = 0x04030201;
        build.Fill.Destination.SegmentId = 1;
        build.Fill.Destination.SegmentAddress.QuadPart = 4097;
        result = Paging::build(segment, build);
        if (result != STATUS_SUCCESS) return result;
        phase = 4;
        DXGKARG_SUBMITCOMMAND submission{};
        submission.Flags.Paging = 1;
        submission.SubmissionFenceId = 10;
        submission.DmaBufferSize = submission.DmaBufferSubmissionEndOffset = sizeof(marker);
        submission.pDmaBufferPrivateData = &command;
        submission.DmaBufferPrivateDataSize = submission.DmaBufferPrivateDataSubmissionEndOffset = sizeof(command);
        result = engine.submit(submission);
        if (result != STATUS_SUCCESS) return result;
        command.pattern = 0;
        phase = 5;
        result = wait_for(1);
        if (result != STATUS_SUCCESS) return result;
        const auto data = static_cast<const UCHAR *>(segment.range(4097, 1024));
        for (SIZE_T i = 0; i < 1024; ++i)
            if (data[i] != (i % 4) + 1) return STATUS_DATA_ERROR;
        if (observed[0].InterruptType != DXGK_INTERRUPT_DMA_COMPLETED ||
            observed[0].DmaCompleted.SubmissionFenceId != 10) return STATUS_DATA_ERROR;
        phase = 6;
        DXGKARG_PREEMPTCOMMAND preemption{};
        preemption.PreemptionFenceId = 7;
        result = engine.preempt(preemption);
        if (result != STATUS_SUCCESS) return result;
        result = wait_for(2);
        if (result != STATUS_SUCCESS) return result;
        if (observed[1].InterruptType != DXGK_INTERRUPT_DMA_PREEMPTED ||
            observed[1].DmaPreempted.PreemptionFenceId != 7 ||
            observed[1].DmaPreempted.LastCompletedFenceId != 10) return STATUS_DATA_ERROR;
        phase = 7;
        result = engine.stop();
        if (result != STATUS_SUCCESS) return result;
        if (engine.worker_irql_after_join() != PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
        result = engine.start(callbacks(), queue, submissions, segment);
        if (result != STATUS_SUCCESS) return result;
        phase = 8;
        submission = {};
        submission.Flags.ContextSwitch = 1;
        submission.SubmissionFenceId = 11;
        result = engine.submit(submission);
        if (result != STATUS_SUCCESS) return result;
        result = wait_for(3);
        if (result != STATUS_SUCCESS) return result;
        if (observed[2].InterruptType != DXGK_INTERRUPT_DMA_COMPLETED ||
            observed[2].DmaCompleted.SubmissionFenceId != 11 ||
            notify_irql <= DISPATCH_LEVEL || notify_irql >= PROFILE_LEVEL ||
            dpc_irql != DISPATCH_LEVEL || notifications != 3) return STATUS_DATA_ERROR;
        phase = 9;
        return STATUS_SUCCESS;
    }
};
} // namespace nr::kmd
