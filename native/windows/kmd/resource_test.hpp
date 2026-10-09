// Resource ownership is separate from allocation and per-device open lifetime.
#pragma once
int resource_test(Adapter *a) {
    const auto baseline = outstanding;
    nr::kernel_contract::Surface surface{};
    surface.width = 64; surface.height = 48; surface.format = 87;
    surface.row_pitch = 256; surface.allocation_bytes = 12288;
    DXGK_ALLOCATIONINFO info{};
    info.pPrivateDriverData = &surface; info.PrivateDriverDataSize = sizeof(surface);
    DXGKARG_CREATEALLOCATION create{};
    create.Flags.Resource = 1; create.NumAllocations = 1; create.pAllocationInfo = &info;
    create.pPrivateDriverData = &surface; create.PrivateDriverDataSize = sizeof(surface);
    auto other = surface; other.width = 32;
    create.pPrivateDriverData = &other;
    NR_CHECK(registration.DxgkDdiCreateAllocation(a, &create) == STATUS_INVALID_PARAMETER);
    create.pPrivateDriverData = &surface;
    for (long failure = 0; failure < 2; ++failure) {
        fail_after = failure;
        NR_CHECK(registration.DxgkDdiCreateAllocation(a, &create) == STATUS_INSUFFICIENT_RESOURCES);
        NR_CHECK(outstanding == baseline && !a->allocations && !a->resources && !create.hResource && !info.hAllocation);
    }
    fail_after = -1;
    a->resources = 128;
    NR_CHECK(registration.DxgkDdiCreateAllocation(a, &create) == STATUS_INSUFFICIENT_RESOURCES);
    NR_CHECK(a->resources == 128 && !a->allocations && outstanding == baseline);
    a->resources = 0;
    for (unsigned separate = 0; separate < 2; ++separate) {
        NR_CHECK(registration.DxgkDdiCreateAllocation(a, &create) == STATUS_SUCCESS);
        NR_CHECK(create.hResource && info.hAllocation && a->resources == 1 && a->allocations == 1);
        NR_CHECK(!info.Flags.CpuVisible && info.SupportedReadSegmentSet == 1 && info.SupportedWriteSegmentSet == 1);
        auto allocation = static_cast<Allocation *>(info.hAllocation);
        NR_CHECK(allocation->resource == create.hResource);
        DXGKARG_CREATEDEVICE device_args{};
        NR_CHECK(registration.DxgkDdiCreateDevice(a, &device_args) == STATUS_SUCCESS);
        a->kernel.DxgkCbGetHandleData = allocation_lookup; lookup_allocations[0] = allocation;
        DXGK_OPENALLOCATIONINFO opened{}; opened.hAllocation = 101;
        DXGKARG_OPENALLOCATION open{};
        open.NumAllocations = 1; open.pOpenAllocation = &opened;
        NR_CHECK(registration.DxgkDdiOpenAllocation(device_args.hDevice, &open) == STATUS_INVALID_PARAMETER);
        open.PrivateDriverSize = sizeof(surface); open.pPrivateDriverData = &other;
        NR_CHECK(registration.DxgkDdiOpenAllocation(device_args.hDevice, &open) == STATUS_INVALID_PARAMETER);
        open.pPrivateDriverData = &surface;
        NR_CHECK(registration.DxgkDdiOpenAllocation(device_args.hDevice, &open) == STATUS_SUCCESS);
        const HANDLE opened_handle = opened.hDeviceSpecificAllocation;
        NR_CHECK(registration.DxgkDdiCreateAllocation(a, &create) == STATUS_INVALID_PARAMETER); // No append contract.
        DXGKARG_DESTROYALLOCATION destroy{};
        destroy.NumAllocations = 1; destroy.pAllocationList = &info.hAllocation;
        NR_CHECK(registration.DxgkDdiDestroyAllocation(a, &destroy) == STATUS_INVALID_PARAMETER); // Wrong owner.
        destroy.hResource = create.hResource; destroy.Flags.DestroyResource = separate ? 0 : 1;
        NR_CHECK(registration.DxgkDdiDestroyAllocation(a, &destroy) == STATUS_DEVICE_BUSY);
        NR_CHECK(outstanding == baseline + 4 && a->resources == 1 && a->allocations == 1);
        DXGKARG_CLOSEALLOCATION close{}; close.NumAllocations = 1; close.pOpenHandleList = &opened_handle;
        NR_CHECK(registration.DxgkDdiCloseAllocation(device_args.hDevice, &close) == STATUS_SUCCESS);
        NR_CHECK(registration.DxgkDdiDestroyDevice(device_args.hDevice) == STATUS_SUCCESS);
        a->kernel.DxgkCbGetHandleData = nullptr; lookup_allocations[0] = nullptr;
        NR_CHECK(registration.DxgkDdiDestroyAllocation(a, &destroy) == STATUS_SUCCESS);
        NR_CHECK(!a->allocations);
        if (separate) {
            NR_CHECK(a->resources == 1 && outstanding == baseline + 1);
            NR_CHECK(registration.DxgkDdiStopDevice(a) == STATUS_DEVICE_BUSY);
            destroy.NumAllocations = 0; destroy.pAllocationList = nullptr; destroy.Flags.DestroyResource = 1;
            NR_CHECK(registration.DxgkDdiDestroyAllocation(a, &destroy) == STATUS_SUCCESS);
        }
        NR_CHECK(outstanding == baseline && !a->resources);
        create.hResource = nullptr; info.hAllocation = nullptr;
    }
    return 0;
}
