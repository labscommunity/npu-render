// Lab-only startup model. Actual Windows scheduler acceptance requires a VM run.
#pragma once
int lab_start_test() {
    DEVICE_OBJECT physical{};
    void *opaque = nullptr;
    NR_CHECK(registration.DxgkDdiAddDevice(&physical, &opaque) == STATUS_SUCCESS);
    auto a = static_cast<Adapter *>(opaque);
    dpc_owner = a;
    engine_mode = true;
    sync_mode = SyncMode::valid;
    DXGK_START_INFO start{};
    start.RequiredDmaQueueEntry = 32;
    DXGKRNL_INTERFACE kernel{};
    kernel.DeviceHandle = &registration;
    kernel.DxgkCbSynchronizeExecution = synchronize;
    kernel.DxgkCbNotifyInterrupt = notify_interrupt;
    kernel.DxgkCbQueueDpc = queue_dpc;
    kernel.DxgkCbNotifyDpc = notify_dpc;
    ULONG sources = 99, children = 99;
    segment_fail = true;
    NR_CHECK(registration.DxgkDdiStartDevice(a, &start, &kernel, &sources, &children) == STATUS_INSUFFICIENT_RESOURCES);
    segment_fail = false;
    start.RequiredDmaQueueEntry = 0;
    NR_CHECK(registration.DxgkDdiStartDevice(a, &start, &kernel, &sources, &children) == STATUS_INVALID_PARAMETER);
    start.RequiredDmaQueueEntry = 32;
    for (long failure = 0; failure < 2; ++failure) {
        fail_after = failure;
        NR_CHECK(registration.DxgkDdiStartDevice(a, &start, &kernel, &sources, &children) == STATUS_INSUFFICIENT_RESOURCES);
        NR_CHECK(!a->started && !a->segment.ready() && !a->submissions.ready() && outstanding == 1);
    }
    fail_after = -1;
    thread_fail = true;
    NR_CHECK(registration.DxgkDdiStartDevice(a, &start, &kernel, &sources, &children) == STATUS_INSUFFICIENT_RESOURCES);
    NR_CHECK(!a->started && !a->segment.ready() && !a->submissions.ready() && outstanding == 1);
    thread_fail = false;
    NR_CHECK(registration.DxgkDdiStartDevice(a, &start, &kernel, &sources, &children) == STATUS_SUCCESS);
    NR_CHECK(a->started && a->engine.alive() && a->scheduler.running());
    NR_CHECK(!sources && !children && a->segment.capacity() == 16 * 1024 * 1024 &&
             a->submissions.capacity() == 33 && !notification_count);
    nr::kernel_contract::AdapterInfo info{};
    DXGKARG_QUERYADAPTERINFO query{};
    query.Type = DXGKQAITYPE_UMDRIVERPRIVATE;
    query.pOutputData = &info;
    query.OutputDataSize = sizeof(info);
    NR_CHECK(registration.DxgkDdiQueryAdapterInfo(a, &query) == STATUS_SUCCESS && !info.ready);
    NR_CHECK(registration.DxgkDdiStartDevice(a, &start, &kernel, &sources, &children) == STATUS_INVALID_DEVICE_STATE);
    const auto resource_result = resource_test(a);
    if (resource_result) return resource_result;
    const auto fill_result = fill_test(a);
    if (fill_result) return fill_result;
    const auto copy_result = copy_test(a);
    if (copy_result) return copy_result;
    NR_CHECK(registration.DxgkDdiStopDevice(a) == STATUS_SUCCESS);
    NR_CHECK(!a->started && !a->engine.alive() && !a->segment.ready() && !a->submissions.ready());
    NR_CHECK(registration.DxgkDdiRemoveDevice(a) == STATUS_SUCCESS && !outstanding);
    NR_CHECK(!notification_count && !worker_contract_error && !segment_contract_error && !sync_contract_error);
    return 0;
}
