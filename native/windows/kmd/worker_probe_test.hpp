// Failure/cleanup contracts for the actual lab worker source. Host thread and
// DPC calls are modeled; only the isolated guest can validate kernel execution.
int worker_probe_test(Adapter *adapter) {
    DXGK_START_INFO start{};
    DXGKRNL_INTERFACE kernel{};
    kernel.DeviceHandle = &registration;
    kernel.DxgkCbSynchronizeExecution = synchronize;
    kernel.DxgkCbQueueDpc = queue_dpc;
    dpc_owner = adapter;
    ULONG sources = 999, children = 999;
    for (unsigned mutation = 0; mutation < 12; ++mutation) {
        sync_mode = SyncMode::valid;
        const auto baseline_pool = outstanding;
        const auto baseline_mdls = mdl_outstanding;
        const auto baseline_flush = dpc_flushes;
        switch (mutation) {
        case 1: thread_fail = true; break;
        case 2: segment_fail = true; break;
        case 3: fail_after = 0; break;
        case 4: mdl_fail_after = 0; break;
        case 5: mdl_fail_after = 1; break;
        case 6: mdl_fail_after = 2; break;
        case 7: queue_accept = false; break;
        case 8: defer_dpc = true; break;
        case 9: sync_mode = SyncMode::failed; break;
        case 10: delivered_dpc_irql = KIRQL(DISPATCH_LEVEL + 1); break;
        case 11: thread_wait_failures = 1; break;
        }
        NR_CHECK(registration.DxgkDdiStartDevice(adapter, &start, &kernel, &sources, &children) ==
                 STATUS_NOT_SUPPORTED);
        const auto &probe = adapter->worker_probe;
        NR_CHECK((probe.status == STATUS_SUCCESS) == (mutation == 0 || mutation == 11));
        NR_CHECK(!worker_contract_error && !segment_contract_error && !mdl_contract_error);
        NR_CHECK(outstanding == baseline_pool && mdl_outstanding == baseline_mdls);
        NR_CHECK(!segment_memory && !pending_dpc && !probe.active && !probe.kernel.DeviceHandle);
        NR_CHECK(!sources && !children && !adapter->started && !adapter->scheduler.running());
        if (mutation == 1) {
            NR_CHECK(!probe.joined && dpc_flushes == baseline_flush && !thread_closed);
        } else {
            NR_CHECK(probe.joined && thread_joined && thread_closed &&
                     dpc_flushes == baseline_flush + 1);
        }
        if (mutation == 8) {
            // A callback drained after timeout is not a successful timed observation.
            NR_CHECK(probe.dpc_wait_status == STATUS_TIMEOUT && probe.dpc_calls == 1);
        }
        const auto observed = probe.dpc_calls;
        registration.DxgkDdiDpcRoutine(adapter); // Retired diagnostic cannot consume callbacks.
        NR_CHECK(probe.dpc_calls == observed);
        thread_fail = segment_fail = defer_dpc = false;
        fail_after = mdl_fail_after = -1;
        queue_accept = true;
        delivered_dpc_irql = DISPATCH_LEVEL;
    }
    NR_CHECK(retry_delays == 1 && !thread_wait_failures);
    dpc_owner = nullptr;
    return 0;
}
