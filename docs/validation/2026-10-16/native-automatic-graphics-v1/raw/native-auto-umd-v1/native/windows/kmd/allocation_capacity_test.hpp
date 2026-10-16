// Exercise actual object admission, failure rollback and reuse above the old
// 128-object budget. Host shims allocate metadata, never physical display memory.
int allocation_capacity_test(Adapter *a) {
    const auto baseline = outstanding;
    NR_CHECK(!a->allocations && !a->resources);
    nr::kernel_contract::Surface surface{};
    surface.width = surface.height = 16; surface.format = 87;
    surface.row_pitch = 256; surface.allocation_bytes = 4096;
    DXGK_ALLOCATIONINFO info[2]{};
    for (auto &i : info) { i.pPrivateDriverData = &surface; i.PrivateDriverDataSize = sizeof(surface); }
    DXGKARG_CREATEALLOCATION create{};
    create.NumAllocations = 1; create.pAllocationInfo = info;
    create.Flags.Resource = 1;
    create.pPrivateDriverData = &surface; create.PrivateDriverDataSize = sizeof(surface);
    a->resources = Adapter::resource_limit;
    NR_CHECK(registration.DxgkDdiCreateAllocation(a, &create) == STATUS_INSUFFICIENT_RESOURCES);
    NR_CHECK(a->resources == Adapter::resource_limit && !a->allocations && outstanding == baseline);
    a->resources = 0;
    for (long failure = 0; failure < 2; ++failure) {
        fail_after = failure;
        NR_CHECK(registration.DxgkDdiCreateAllocation(a, &create) == STATUS_INSUFFICIENT_RESOURCES);
        NR_CHECK(!a->allocations && !a->resources && !info[0].hAllocation && !create.hResource && outstanding == baseline);
    }
    fail_after = -1;
    HANDLE allocations[Adapter::allocation_limit]{}, resources[Adapter::allocation_limit]{};
    for (LONG i = 0; i < Adapter::allocation_limit; ++i) {
        create.hResource = nullptr; info[0].hAllocation = nullptr;
        NR_CHECK(registration.DxgkDdiCreateAllocation(a, &create) == STATUS_SUCCESS);
        allocations[i] = info[0].hAllocation; resources[i] = create.hResource;
        NR_CHECK(allocations[i] && resources[i] && a->allocations == i + 1 && a->resources == i + 1);
    }
    NR_CHECK(outstanding == baseline + 2 * Adapter::allocation_limit);
    create.hResource = nullptr; info[0].hAllocation = nullptr;
    NR_CHECK(registration.DxgkDdiCreateAllocation(a, &create) == STATUS_INSUFFICIENT_RESOURCES);
    NR_CHECK(!create.hResource && !info[0].hAllocation && a->allocations == Adapter::allocation_limit);
    auto remove = [&](LONG i) {
        DXGKARG_DESTROYALLOCATION destroy{};
        destroy.NumAllocations = 1; destroy.pAllocationList = &allocations[i];
        destroy.Flags.DestroyResource = 1; destroy.hResource = resources[i];
        return registration.DxgkDdiDestroyAllocation(a, &destroy);
    };
    NR_CHECK(remove(0) == STATUS_SUCCESS);
    // A two-allocation request cannot consume the one free slot partially.
    auto batch = create; batch.Flags.Value = 0; batch.PrivateDriverDataSize = 0;
    batch.pPrivateDriverData = nullptr; batch.NumAllocations = 2;
    NR_CHECK(registration.DxgkDdiCreateAllocation(a, &batch) == STATUS_INSUFFICIENT_RESOURCES);
    NR_CHECK(!info[0].hAllocation && !info[1].hAllocation && a->allocations == Adapter::allocation_limit - 1);
    NR_CHECK(registration.DxgkDdiCreateAllocation(a, &create) == STATUS_SUCCESS);
    allocations[0] = info[0].hAllocation; resources[0] = create.hResource;
    for (LONG i = 0; i < Adapter::allocation_limit; ++i) NR_CHECK(remove(i) == STATUS_SUCCESS);
    NR_CHECK(!a->allocations && !a->resources && outstanding == baseline);
    return 0;
}
