// Host-only notification and lifetime model. The actual engine source is
// stepped manually; the deferred thread shim is joined during shutdown.
#pragma once
int engine_test(Adapter *a) {
    using namespace nr::kmd;
    const auto baseline = outstanding;
    engine_mode = true;
    sync_mode = SyncMode::valid;
    dpc_owner = a;
    NR_CHECK(a->segment.prepare(PAGE_SIZE) == STATUS_SUCCESS);
    NR_CHECK(a->submissions.prepare(a->scheduler, 4) == STATUS_SUCCESS);
    DXGKRNL_INTERFACE kernel{};
    NR_CHECK(a->engine.start(kernel, a->scheduler, a->submissions, a->segment) == STATUS_INVALID_PARAMETER);
    kernel.DeviceHandle = &registration;
    kernel.DxgkCbSynchronizeExecution = synchronize;
    kernel.DxgkCbNotifyInterrupt = notify_interrupt;
    kernel.DxgkCbQueueDpc = queue_dpc;
    kernel.DxgkCbNotifyDpc = notify_dpc;
    a->kernel = kernel;
    thread_fail = true;
    NR_CHECK(a->engine.start(kernel, a->scheduler, a->submissions, a->segment) ==
             STATUS_INSUFFICIENT_RESOURCES);
    NR_CHECK(!a->engine.alive() && !a->scheduler.running());
    thread_fail = false;
    NR_CHECK(a->engine.start(kernel, a->scheduler, a->submissions, a->segment) == STATUS_SUCCESS);
    a->started = true; // Host model only. StartDevice's live gate stays closed.
    NR_CHECK(a->engine.alive() && !notification_count && !notified_dpcs);

    PagingCommand record{};
    record.operation = DXGK_OPERATION_FILL;
    record.bytes = 16;
    record.pattern = 0x04030201;
    record.destination.offset = 32;
    DXGKARG_SUBMITCOMMAND args{};
    args.Flags.Paging = 1;
    args.SubmissionFenceId = 10;
    args.DmaBufferSize = args.DmaBufferSubmissionEndOffset = sizeof(PagingMarker);
    args.pDmaBufferPrivateData = &record;
    args.DmaBufferPrivateDataSize = args.DmaBufferPrivateDataSubmissionEndOffset = sizeof(record);
    const auto paging_args = args;
    auto target = static_cast<const UCHAR *>(a->segment.range(32, 16));
    simulated_irql = DISPATCH_LEVEL;
    NR_CHECK(registration.DxgkDdiSubmitCommand(a, &args) == STATUS_SUCCESS);
    NR_CHECK(simulated_irql == DISPATCH_LEVEL && !notification_count && !notified_dpcs);
    simulated_irql = PASSIVE_LEVEL;
    record.pattern = 0; // The worker must execute the captured original bytes.
    for (UINT i = 0; i < 16; ++i) NR_CHECK(target[i] == 0);
    NR_CHECK(a->engine.process_one() == STATUS_SUCCESS);
    for (UINT i = 0; i < 16; ++i) NR_CHECK(target[i] == (i % 4) + 1);
    NR_CHECK(notification_count == 1 && notified_dpcs == 1);
    NR_CHECK(notifications[0].InterruptType == DXGK_INTERRUPT_DMA_COMPLETED &&
             notifications[0].DmaCompleted.SubmissionFenceId == 10);
    DXGKARG_QUERYCURRENTFENCE query{};
    NR_CHECK(registration.DxgkDdiQueryCurrentFence(a, &query) == STATUS_SUCCESS && query.CurrentFence == 10);

    args = {};
    args.Flags.ContextSwitch = 1;
    args.SubmissionFenceId = 20;
    NR_CHECK(registration.DxgkDdiSubmitCommand(a, &args) == STATUS_SUCCESS);
    args.SubmissionFenceId = 21;
    NR_CHECK(registration.DxgkDdiSubmitCommand(a, &args) == STATUS_SUCCESS);
    DXGKARG_PREEMPTCOMMAND preempt_args{}; // A zero preemption fence is still a request.
    NR_CHECK(registration.DxgkDdiPreemptCommand(a, &preempt_args) == STATUS_SUCCESS);
    args.SubmissionFenceId = 22;
    NR_CHECK(registration.DxgkDdiSubmitCommand(a, &args) == STATUS_SUCCESS);
    NR_CHECK(a->engine.process_one() == STATUS_SUCCESS && notification_count == 2);
    NR_CHECK(notifications[1].InterruptType == DXGK_INTERRUPT_DMA_PREEMPTED &&
             !notifications[1].DmaPreempted.PreemptionFenceId &&
             notifications[1].DmaPreempted.LastCompletedFenceId == 10);
    NR_CHECK(a->engine.process_one() == STATUS_SUCCESS && notification_count == 3);
    NR_CHECK(notifications[2].InterruptType == DXGK_INTERRUPT_DMA_COMPLETED &&
             notifications[2].DmaCompleted.SubmissionFenceId == 22);
    NR_CHECK(a->engine.process_one() == STATUS_NO_MORE_ENTRIES && notification_count == 3);

    // A deferred execution failure must report a fault, never a successful fence.
    args = paging_args;
    args.SubmissionFenceId = 23;
    record.destination.offset = PAGE_SIZE;
    NR_CHECK(registration.DxgkDdiSubmitCommand(a, &args) == STATUS_SUCCESS);
    NR_CHECK(a->engine.process_one() == STATUS_INVALID_PARAMETER);
    NR_CHECK(notification_count == 4 && notifications[3].InterruptType == DXGK_INTERRUPT_DMA_FAULTED);
    NR_CHECK(notifications[3].DmaFaulted.FaultedFenceId == 23 &&
             notifications[3].DmaFaulted.Status == STATUS_INVALID_PARAMETER);
    NR_CHECK(registration.DxgkDdiQueryCurrentFence(a, &query) == STATUS_SUCCESS && query.CurrentFence == 22);
    NR_CHECK(!a->scheduler.running() && !a->scheduler.active());
    NR_CHECK(registration.DxgkDdiResetFromTimeout(a) == STATUS_SUCCESS);
    NR_CHECK(a->resetting && !a->engine.alive() && thread_joined && thread_closed);
    NR_CHECK(a->segment.ready() && a->submissions.ready() && outstanding == baseline + 2);
    query.CurrentFence = 999;
    NR_CHECK(registration.DxgkDdiQueryCurrentFence(a, &query) == STATUS_SUCCESS && query.CurrentFence == 23);
    NR_CHECK(notification_count == 4); // Recovery baseline is not a completion.
    DXGKARG_BUILDPAGINGBUFFER eviction{};
    eviction.Operation = DXGK_OPERATION_TRANSFER;
    const auto before = eviction;
    NR_CHECK(registration.DxgkDdiBuildPagingBuffer(a, &eviction) == STATUS_SUCCESS);
    NR_CHECK(RtlCompareMemory(&before, &eviction, sizeof(before)) == sizeof(before));
    NR_CHECK(registration.DxgkDdiSubmitCommand(a, &args) == STATUS_DEVICE_NOT_READY);
    NR_CHECK(registration.DxgkDdiRestartFromTimeout(a) == STATUS_SUCCESS);
    NR_CHECK(!a->resetting && a->engine.alive());

    // If synchronization fails after execution, retain the claimed slot until
    // reset joins the worker. No completion is invented for the failed callback.
    args = paging_args;
    args.SubmissionFenceId = 30;
    record.destination.offset = 32;
    NR_CHECK(registration.DxgkDdiSubmitCommand(a, &args) == STATUS_SUCCESS);
    sync_fail_call = sync_calls + 2;
    NR_CHECK(a->engine.process_one() == STATUS_UNSUCCESSFUL);
    NR_CHECK(notification_count == 4 && a->scheduler.active());
    NR_CHECK(a->submissions.release() == STATUS_DEVICE_BUSY);
    sync_fail_call = 0;
    NR_CHECK(registration.DxgkDdiResetFromTimeout(a) == STATUS_SUCCESS);
    NR_CHECK(!a->scheduler.active() && !a->engine.alive() && notification_count == 4);
    NR_CHECK(registration.DxgkDdiQueryCurrentFence(a, &query) == STATUS_SUCCESS && query.CurrentFence == 30);
    NR_CHECK(registration.DxgkDdiRestartFromTimeout(a) == STATUS_SUCCESS);

    // Multiple interrupts can share one queued DPC. Stop must drain it while
    // the callback context is still alive, after joining the sole producer.
    defer_dpc = true;
    args = {};
    args.Flags.ContextSwitch = 1;
    args.SubmissionFenceId = 22; // Repeated OS fences are allowed after restart.
    NR_CHECK(registration.DxgkDdiSubmitCommand(a, &args) == STATUS_SUCCESS);
    NR_CHECK(a->engine.process_one() == STATUS_SUCCESS && pending_dpc);
    args.SubmissionFenceId = 24;
    NR_CHECK(registration.DxgkDdiSubmitCommand(a, &args) == STATUS_SUCCESS);
    NR_CHECK(a->engine.process_one() == STATUS_SUCCESS && pending_dpc);
    NR_CHECK(notification_count == 6 && notified_dpcs == 4);
    args.SubmissionFenceId = 25;
    NR_CHECK(registration.DxgkDdiSubmitCommand(a, &args) == STATUS_SUCCESS);
    NR_CHECK(registration.DxgkDdiStopDevice(a) == STATUS_SUCCESS);
    NR_CHECK(!pending_dpc && notified_dpcs == 5 && notification_count == 6);
    NR_CHECK(!a->started && !a->resetting && !a->engine.alive());
    NR_CHECK(!a->segment.ready() && !a->submissions.ready() && outstanding == baseline);
    NR_CHECK(thread_joined && thread_closed && !worker_contract_error && !sync_contract_error);
    engine_mode = defer_dpc = false;
    return 0;
}
