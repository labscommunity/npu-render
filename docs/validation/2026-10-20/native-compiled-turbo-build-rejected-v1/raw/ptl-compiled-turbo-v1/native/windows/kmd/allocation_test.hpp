// OS handle resolution and per-device ownership; user-mode shims only.
#pragma once
namespace {
Allocation *lookup_allocations[3]{};
bool lookup_error = false;
void *allocation_lookup(const DXGKARGCB_GETHANDLEDATA *args) {
    if (!args || args->Type != DXGK_HANDLE_ALLOCATION || args->Flags.Value) {
        lookup_error = true;
        return nullptr;
    }
    return args->hObject >= 101 && args->hObject <= 103 ?
        lookup_allocations[args->hObject - 101] : nullptr;
}
}
int allocation_test(Adapter *a, HANDLE *handles) {
    const auto baseline = outstanding;
    DXGKARG_CREATEDEVICE device_args[2]{};
    Device *devices[2]{};
    for (unsigned i = 0; i < 2; ++i) {
        NR_CHECK(registration.DxgkDdiCreateDevice(a, &device_args[i]) == STATUS_SUCCESS);
        devices[i] = static_cast<Device *>(device_args[i].hDevice);
    }
    for (unsigned i = 0; i < 3; ++i) {
        lookup_allocations[i] = static_cast<Allocation *>(handles[i]);
        DXGKARG_DESCRIBEALLOCATION description{};
        description.hAllocation = handles[i];
        NR_CHECK(registration.DxgkDdiDescribeAllocation(a, &description) == STATUS_SUCCESS);
        NR_CHECK(description.Width == 64 && description.Height == 48 &&
                 description.Format == D3DDDIFMT_A8R8G8B8 &&
                 description.Rotation == D3DDDI_ROTATION_IDENTITY);
        NR_CHECK(!description.RefreshRate.Numerator && !description.RefreshRate.Denominator);
    }
    DXGK_OPENALLOCATIONINFO infos[3]{};
    nr::kernel_contract::Surface metadata[3]{};
    for (UINT i = 0; i < 3; ++i) {
        infos[i].hAllocation = 101 + i;
        infos[i].pPrivateDriverData = &metadata[i];
        infos[i].PrivateDriverDataSize = sizeof(metadata[i]);
    }
    DXGKARG_OPENALLOCATION open{};
    open.NumAllocations = 3;
    open.pOpenAllocation = infos;
    open.Flags.Create = 1;
    NR_CHECK(registration.DxgkDdiOpenAllocation(devices[0], &open) == STATUS_DEVICE_NOT_READY);
    a->kernel.DxgkCbGetHandleData = allocation_lookup;
    infos[2].hAllocation = 999;
    NR_CHECK(registration.DxgkDdiOpenAllocation(devices[0], &open) == STATUS_INVALID_HANDLE);
    infos[2].hAllocation = 103;
    lookup_allocations[2]->adapter = nullptr;
    NR_CHECK(registration.DxgkDdiOpenAllocation(devices[0], &open) == STATUS_INVALID_HANDLE);
    lookup_allocations[2]->adapter = a;
    infos[2].PrivateDriverDataSize--;
    NR_CHECK(registration.DxgkDdiOpenAllocation(devices[0], &open) == STATUS_INVALID_PARAMETER);
    infos[2].PrivateDriverDataSize++;
    open.PrivateDriverSize = 1;
    NR_CHECK(registration.DxgkDdiOpenAllocation(devices[0], &open) == STATUS_NOT_SUPPORTED);
    open.PrivateDriverSize = 0;
    open.SubresourceIndex = 1;
    NR_CHECK(registration.DxgkDdiOpenAllocation(devices[0], &open) == STATUS_NOT_SUPPORTED);
    open.SubresourceIndex = 0;
    open.Flags.Value = 4;
    NR_CHECK(registration.DxgkDdiOpenAllocation(devices[0], &open) == STATUS_NOT_SUPPORTED);
    open.Flags.Value = 1;
    a->opens = 4094;
    NR_CHECK(registration.DxgkDdiOpenAllocation(devices[0], &open) == STATUS_INSUFFICIENT_RESOURCES);
    NR_CHECK(a->opens == 4094);
    a->opens = 0;
    for (long failure = 0; failure < 3; ++failure) {
        fail_after = failure;
        NR_CHECK(registration.DxgkDdiOpenAllocation(devices[0], &open) == STATUS_INSUFFICIENT_RESOURCES);
        NR_CHECK(outstanding == baseline + 2 && !a->opens && !devices[0]->opens);
        for (unsigned i = 0; i < 3; ++i)
            NR_CHECK(!infos[i].hDeviceSpecificAllocation && !lookup_allocations[i]->opens &&
                     !metadata[i].width);
    }
    fail_after = -1;
    NR_CHECK(registration.DxgkDdiOpenAllocation(devices[0], &open) == STATUS_SUCCESS);
    HANDLE opened[3]{};
    for (unsigned i = 0; i < 3; ++i) {
        opened[i] = infos[i].hDeviceSpecificAllocation;
        NR_CHECK(opened[i] && metadata[i].width == 64 && metadata[i].height == 48 &&
                 metadata[i].allocation_bytes == 12288 && lookup_allocations[i]->opens == 1);
        NR_CHECK(!static_cast<OpenAllocation *>(opened[i])->read_only);
    }
    // The same allocation can be opened by another device without transferring
    // ownership or treating the OS's 32-bit handle as a kernel pointer.
    open.NumAllocations = 1;
    open.Flags.Value = 2;
    infos[0].PrivateDriverDataSize = 0;
    infos[0].pPrivateDriverData = nullptr;
    NR_CHECK(registration.DxgkDdiOpenAllocation(devices[1], &open) == STATUS_SUCCESS);
    const HANDLE readonly = infos[0].hDeviceSpecificAllocation;
    NR_CHECK(readonly != opened[0] && static_cast<OpenAllocation *>(readonly)->read_only);
    NR_CHECK(a->opens == 4 && devices[0]->opens == 3 && devices[1]->opens == 1 &&
             lookup_allocations[0]->opens == 2);
    DXGKARG_DESTROYALLOCATION destroy{};
    destroy.NumAllocations = 3;
    destroy.pAllocationList = handles;
    NR_CHECK(registration.DxgkDdiDestroyAllocation(a, &destroy) == STATUS_DEVICE_BUSY);
    NR_CHECK(registration.DxgkDdiDestroyDevice(devices[0]) == STATUS_DEVICE_BUSY);
    NR_CHECK(registration.DxgkDdiDestroyDevice(devices[1]) == STATUS_DEVICE_BUSY);
    DXGKARG_CLOSEALLOCATION close{};
    close.NumAllocations = 3;
    close.pOpenHandleList = opened;
    NR_CHECK(registration.DxgkDdiCloseAllocation(devices[1], &close) == STATUS_INVALID_HANDLE);
    const auto last = opened[2];
    opened[2] = opened[1];
    NR_CHECK(registration.DxgkDdiCloseAllocation(devices[0], &close) == STATUS_INVALID_PARAMETER);
    NR_CHECK(a->opens == 4 && outstanding == baseline + 6);
    opened[2] = last;
    NR_CHECK(registration.DxgkDdiCloseAllocation(devices[0], &close) == STATUS_SUCCESS);
    NR_CHECK(a->opens == 1 && !devices[0]->opens && lookup_allocations[0]->opens == 1);
    NR_CHECK(registration.DxgkDdiDestroyDevice(devices[0]) == STATUS_SUCCESS);
    close.NumAllocations = 1;
    close.pOpenHandleList = &readonly;
    NR_CHECK(registration.DxgkDdiCloseAllocation(devices[1], &close) == STATUS_SUCCESS);
    NR_CHECK(registration.DxgkDdiDestroyDevice(devices[1]) == STATUS_SUCCESS);
    NR_CHECK(!a->opens && !a->devices && outstanding == baseline && !lookup_error);
    for (auto allocation : lookup_allocations) NR_CHECK(!allocation->opens);
    a->kernel.DxgkCbGetHandleData = nullptr;
    return 0;
}
