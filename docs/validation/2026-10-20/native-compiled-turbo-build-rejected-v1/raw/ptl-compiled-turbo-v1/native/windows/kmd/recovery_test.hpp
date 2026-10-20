// Per-engine recovery model with real engine code and host callback shims.
#pragma once
int recovery_test(Adapter *a) {
    using namespace nr::kmd;
    const auto baseline = outstanding;
    const auto reported = notification_count;
    engine_mode = true;
    sync_mode = SyncMode::valid;
    dpc_owner = a;
    NR_CHECK(a->segment.prepare(PAGE_SIZE) == STATUS_SUCCESS);
    NR_CHECK(a->submissions.prepare(a->scheduler, 4) == STATUS_SUCCESS);
    DXGKRNL_INTERFACE kernel{};
    kernel.DeviceHandle = &registration;
    kernel.DxgkCbSynchronizeExecution = synchronize;
    kernel.DxgkCbNotifyInterrupt = notify_interrupt;
    kernel.DxgkCbQueueDpc = queue_dpc;
    kernel.DxgkCbNotifyDpc = notify_dpc;
    a->kernel = kernel;
    NR_CHECK(a->engine.start(kernel, a->scheduler, a->submissions, a->segment) == STATUS_SUCCESS);
    a->started = true;
    DXGKARG_QUERYENGINESTATUS health{};
    health.EngineStatus.Value = MAXULONG;
    NR_CHECK(registration.DxgkDdiQueryEngineStatus(a, &health) == STATUS_SUCCESS);
    NR_CHECK(health.EngineStatus.Value == 1);
    sync_mode = SyncMode::failed;
    NR_CHECK(registration.DxgkDdiQueryEngineStatus(a, &health) == STATUS_SUCCESS);
    NR_CHECK(!health.EngineStatus.Value);
    sync_mode = SyncMode::valid;
    DXGKARG_QUERYDEPENDENTENGINEGROUP group{};
    NR_CHECK(registration.DxgkDdiQueryDependentEngineGroup(a, &group) == STATUS_SUCCESS);
    NR_CHECK(group.DependentNodeOrdinalMask == 1);
    group.NodeOrdinal = 1;
    group.DependentNodeOrdinalMask = 99;
    NR_CHECK(registration.DxgkDdiQueryDependentEngineGroup(a, &group) == STATUS_NOT_SUPPORTED);
    NR_CHECK(group.DependentNodeOrdinalMask == 99);

    struct DebugBuffer { UCHAR before[8]; Engine::DebugInfo info; UCHAR after[8]; } debug;
    RtlFillMemory(&debug, sizeof(debug), 0xa5);
    DXGKARG_COLLECTDBGINFO collect{};
    collect.Reason = 0x119;
    collect.pBuffer = &debug.info;
    collect.BufferSize = sizeof(debug.info) - 1;
    NR_CHECK(registration.DxgkDdiCollectDbgInfo(a, &collect) == STATUS_BUFFER_TOO_SMALL);
    NR_CHECK(debug.info.magic == 0xa5a5a5a5);
    collect.BufferSize++;
    const auto calls = sync_calls;
    simulated_irql = PROFILE_LEVEL; // Non-TDR diagnostics cannot assume PASSIVE_LEVEL.
    NR_CHECK(registration.DxgkDdiCollectDbgInfo(a, &collect) == STATUS_SUCCESS);
    simulated_irql = PASSIVE_LEVEL;
    NR_CHECK(sync_calls == calls && debug.info.magic == Engine::DebugInfo{}.magic);
    NR_CHECK(debug.info.alive == 1 && !debug.info.quit && debug.info.reason == collect.Reason);
    for (unsigned i = 0; i < 8; ++i) NR_CHECK(debug.before[i] == 0xa5 && debug.after[i] == 0xa5);

    PagingCommand record{};
    record.operation = DXGK_OPERATION_FILL;
    record.bytes = 4;
    record.pattern = 0x11111111;
    DXGKARG_SUBMITCOMMAND args{};
    args.Flags.Paging = 1;
    args.SubmissionFenceId = 101;
    args.DmaBufferSize = args.DmaBufferSubmissionEndOffset = sizeof(PagingMarker);
    args.pDmaBufferPrivateData = &record;
    args.DmaBufferPrivateDataSize = args.DmaBufferPrivateDataSubmissionEndOffset = sizeof(record);
    NR_CHECK(registration.DxgkDdiSubmitCommand(a, &args) == STATUS_SUCCESS);
    NR_CHECK(a->engine.process_one() == STATUS_SUCCESS);
    args.SubmissionFenceId = 102;
    NR_CHECK(registration.DxgkDdiSubmitCommand(a, &args) == STATUS_SUCCESS);
    NR_CHECK(registration.DxgkDdiQueryEngineStatus(a, &health) == STATUS_SUCCESS && health.EngineStatus.Responsive);
    simulated_time += 10000000;
    NR_CHECK(registration.DxgkDdiQueryEngineStatus(a, &health) == STATUS_SUCCESS && !health.EngineStatus.Responsive);
    DXGKARG_RESETENGINE reset{};
    reset.LastAbortedFenceId = 999;
    NR_CHECK(registration.DxgkDdiResetEngine(a, &reset) == STATUS_SUCCESS);
    NR_CHECK(reset.LastAbortedFenceId == 101 && a->scheduler.completed() == 101);
    NR_CHECK(a->engine.alive() && !a->scheduler.pending() && !a->scheduler.active());
    NR_CHECK(notification_count == reported + 1);

    // Replay unaffected queued work, then lose synchronization for an active
    // packet. A later pending packet must not be reported as the aborted one.
    NR_CHECK(registration.DxgkDdiSubmitCommand(a, &args) == STATUS_SUCCESS);
    NR_CHECK(a->engine.process_one() == STATUS_SUCCESS);
    args.SubmissionFenceId = 103;
    NR_CHECK(registration.DxgkDdiSubmitCommand(a, &args) == STATUS_SUCCESS);
    sync_fail_call = sync_calls + 2;
    NR_CHECK(a->engine.process_one() == STATUS_UNSUCCESSFUL && a->scheduler.active());
    sync_fail_call = 0;
    args.SubmissionFenceId = 104;
    NR_CHECK(registration.DxgkDdiSubmitCommand(a, &args) == STATUS_SUCCESS);
    NR_CHECK(registration.DxgkDdiResetEngine(a, &reset) == STATUS_SUCCESS);
    NR_CHECK(reset.LastAbortedFenceId == 103 && a->scheduler.completed() == 103);
    NR_CHECK(notification_count == reported + 2);
    NR_CHECK(registration.DxgkDdiSubmitCommand(a, &args) == STATUS_SUCCESS);
    NR_CHECK(a->engine.process_one() == STATUS_SUCCESS);

    record.destination.offset = PAGE_SIZE;
    args.SubmissionFenceId = 105;
    NR_CHECK(registration.DxgkDdiSubmitCommand(a, &args) == STATUS_SUCCESS);
    NR_CHECK(a->engine.process_one() == STATUS_INVALID_PARAMETER);
    NR_CHECK(registration.DxgkDdiQueryEngineStatus(a, &health) == STATUS_SUCCESS && !health.EngineStatus.Responsive);
    NR_CHECK(registration.DxgkDdiResetEngine(a, &reset) == STATUS_SUCCESS);
    NR_CHECK(reset.LastAbortedFenceId == 105 && a->scheduler.completed() == 105);
    NR_CHECK(notification_count == reported + 4);

    // A restart allocation failure is an unsuccessful node reset. Preserve
    // the output sentinel and allow adapter-wide recovery to establish its
    // different (last-submitted) baseline before trying a new worker.
    record.destination.offset = 0;
    args.SubmissionFenceId = 106;
    NR_CHECK(registration.DxgkDdiSubmitCommand(a, &args) == STATUS_SUCCESS);
    thread_fail = true;
    reset.LastAbortedFenceId = 999;
    NR_CHECK(registration.DxgkDdiResetEngine(a, &reset) == STATUS_INSUFFICIENT_RESOURCES);
    NR_CHECK(reset.LastAbortedFenceId == 999 && !a->engine.alive());
    thread_fail = false;
    NR_CHECK(registration.DxgkDdiResetFromTimeout(a) == STATUS_SUCCESS && a->scheduler.completed() == 106);
    NR_CHECK(registration.DxgkDdiRestartFromTimeout(a) == STATUS_SUCCESS);
    NR_CHECK(registration.DxgkDdiStopDevice(a) == STATUS_SUCCESS);
    NR_CHECK(outstanding == baseline && !a->segment.ready() && !a->submissions.ready());
    NR_CHECK(notification_count == reported + 4 && !worker_contract_error && !sync_contract_error);
    engine_mode = false;
    return 0;
}
