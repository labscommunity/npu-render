int standard_allocation_test() {
    using nr::kmd::StandardAllocation;
    using nr::kernel_contract::Surface;
    D3DKMDT_GDISURFACEDATA input{};
    input.Width = 65; input.Height = 37; input.Format = D3DDDIFMT_A8R8G8B8;
    input.Type = D3DKMDT_GDISURFACE_TEXTURE_CROSSADAPTER; input.Pitch = 0x1234;
    DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA query{};
    query.StandardAllocationType = D3DKMDT_STANDARDALLOCATION_GDISURFACE;
    query.pCreateGdiSurfaceData = &input;
    NR_CHECK(StandardAllocation::describe(query) == STATUS_SUCCESS && input.Pitch == 0x1234);
    NR_CHECK(query.AllocationPrivateDriverDataSize == sizeof(Surface) && query.ResourcePrivateDriverDataSize == sizeof(Surface));
    struct Guarded { ULONG before = 0x12345678; Surface surface{}; ULONG after = 0x87654321; } allocation, resource;
    query.pAllocationPrivateDriverData = &allocation.surface;
    NR_CHECK(StandardAllocation::describe(query) == STATUS_INVALID_PARAMETER && !allocation.surface.width);
    query.pResourcePrivateDriverData = &resource.surface;
    query.ResourcePrivateDriverDataSize = sizeof(Surface) - 1;
    NR_CHECK(StandardAllocation::describe(query) == STATUS_BUFFER_TOO_SMALL && !allocation.surface.width && !resource.surface.width && input.Pitch == 0x1234);
    NR_CHECK(StandardAllocation::describe(query) == STATUS_SUCCESS);
    NR_CHECK(input.Pitch == 384 && allocation.surface.row_pitch == 384 && resource.surface.row_pitch == 384);
    NR_CHECK(allocation.surface.allocation_bytes == 16384 && allocation.surface.flags == 1 && allocation.surface.format == 87);
    NR_CHECK(nr::kernel_contract::valid(allocation.surface));
    NR_CHECK(allocation.before == 0x12345678 && resource.before == 0x12345678 &&
             allocation.after == 0x87654321 && resource.after == 0x87654321);
    // Exact cross-adapter row/height alignment is independent of our ordinary layout.
    Surface bad = allocation.surface; bad.row_pitch = 512;
    NR_CHECK(!nr::kernel_contract::valid(bad));
    bad = allocation.surface; bad.height = 41; bad.allocation_bytes = 16384;
    NR_CHECK(!nr::kernel_contract::valid(bad)); // visible rows fit, aligned 44 rows do not.
    bad = allocation.surface; bad.flags = 2; NR_CHECK(!nr::kernel_contract::valid(bad));
    input.Type = D3DKMDT_GDISURFACE_TEXTURE_CPUVISIBLE; input.Format = D3DDDIFMT_A8B8G8R8;
    NR_CHECK(StandardAllocation::describe(query) == STATUS_SUCCESS && input.Pitch == 512 &&
             allocation.surface.allocation_bytes == 20480 && !allocation.surface.flags && allocation.surface.format == 28);
    input.Type = D3DKMDT_GDISURFACE_TEXTURE;
    NR_CHECK(StandardAllocation::describe(query) == STATUS_SUCCESS);
    input.Type = D3DKMDT_GDISURFACE_TEXTURE_CROSSADAPTER;
    input.Width = 8192; input.Height = 4096;
    NR_CHECK(StandardAllocation::describe(query) == STATUS_SUCCESS && allocation.surface.allocation_bytes == 128ull * 1024 * 1024);
    for (UINT invalid = 0; invalid < 8; ++invalid) {
        input.Width = 65; input.Height = 37; input.Flags.Value = 0; input.Format = D3DDDIFMT_A8R8G8B8;
        input.Type = D3DKMDT_GDISURFACE_TEXTURE_CROSSADAPTER;
        switch (invalid) {
        case 0: input.Width = 0; break;
        case 1: input.Height = 0; break;
        case 2: input.Width = MAXULONG; break;
        case 3: input.Height = 8193; break;
        case 4: input.Width = 8192; input.Height = 4097; break;
        case 5: input.Flags.Value = 1; break;
        case 6: input.Format = D3DDDIFMT_X8R8G8B8; break; // Opaque-alpha semantics are not implemented.
        case 7: input.Type = D3DKMDT_GDISURFACE_TEXTURE_CPUVISIBLE_CROSSADAPTER; break; // Reserved for OS.
        }
        input.Pitch = 0x1234; allocation.surface.width = resource.surface.width = 99;
        NR_CHECK(!NT_SUCCESS(StandardAllocation::describe(query)) && input.Pitch == 0x1234 &&
                 allocation.surface.width == 99 && resource.surface.width == 99);
    }
    query.StandardAllocationType = D3DKMDT_STANDARDALLOCATION_SHAREDPRIMARYSURFACE;
    NR_CHECK(StandardAllocation::describe(query) == STATUS_NOT_SUPPORTED);
    query.StandardAllocationType = D3DKMDT_STANDARDALLOCATION_GDISURFACE; query.pCreateGdiSurfaceData = nullptr;
    NR_CHECK(StandardAllocation::describe(query) == STATUS_INVALID_PARAMETER);
    D3DKMDT_STAGINGSURFACEDATA staging{};
    staging.Width = 65; staging.Height = 37; staging.Pitch = 0x1234;
    query = {}; query.StandardAllocationType = D3DKMDT_STANDARDALLOCATION_STAGINGSURFACE;
    query.pCreateStagingSurfaceData = &staging;
    NR_CHECK(StandardAllocation::describe(query) == STATUS_SUCCESS && staging.Pitch == 0x1234);
    NR_CHECK(query.AllocationPrivateDriverDataSize == sizeof(Surface) && !query.ResourcePrivateDriverDataSize);
    query.pAllocationPrivateDriverData = &allocation.surface;
    query.AllocationPrivateDriverDataSize = sizeof(Surface) - 1; allocation.surface.width = 99;
    NR_CHECK(StandardAllocation::describe(query) == STATUS_BUFFER_TOO_SMALL && staging.Pitch == 0x1234 && allocation.surface.width == 99);
    NR_CHECK(StandardAllocation::describe(query) == STATUS_SUCCESS && staging.Pitch == 512);
    NR_CHECK(allocation.surface.flags == nr::kernel_contract::presentation_staging_surface && allocation.surface.format == 88 &&
             allocation.surface.allocation_bytes == 20480 && nr::kernel_contract::valid(allocation.surface));
    bad = allocation.surface; bad.flags = 0; NR_CHECK(!nr::kernel_contract::valid(bad));
    bad = allocation.surface; bad.flags = 3; NR_CHECK(!nr::kernel_contract::valid(bad));
    bad = allocation.surface; bad.format = 87; NR_CHECK(!nr::kernel_contract::valid(bad));
    query.pResourcePrivateDriverData = &resource.surface;
    NR_CHECK(StandardAllocation::describe(query) == STATUS_INVALID_PARAMETER);
    query.pResourcePrivateDriverData = nullptr;
    for (UINT invalid = 0; invalid < 5; ++invalid) {
        staging.Width = 65; staging.Height = 37; staging.Pitch = 0x1234; allocation.surface.width = 99;
        switch (invalid) {
        case 0: staging.Width = 0; break;
        case 1: staging.Height = 0; break;
        case 2: staging.Width = MAXULONG; break;
        case 3: staging.Height = 8193; break;
        case 4: staging.Width = 8192; staging.Height = 4097; break;
        }
        NR_CHECK(StandardAllocation::describe(query) == STATUS_INVALID_PARAMETER && staging.Pitch == 0x1234 && allocation.surface.width == 99);
    }
    query.pCreateStagingSurfaceData = nullptr;
    NR_CHECK(StandardAllocation::describe(query) == STATUS_INVALID_PARAMETER);
    return 0;
}

int standard_allocation_ddi_test(Adapter *a) {
    D3DKMDT_GDISURFACEDATA input{};
    input.Width = 65; input.Height = 37; input.Format = D3DDDIFMT_A8R8G8B8;
    input.Type = D3DKMDT_GDISURFACE_TEXTURE_CROSSADAPTER;
    nr::kernel_contract::Surface allocation{}, resource{};
    DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA query{};
    query.StandardAllocationType = D3DKMDT_STANDARDALLOCATION_GDISURFACE; query.pCreateGdiSurfaceData = &input;
    NR_CHECK(registration.DxgkDdiGetStandardAllocationDriverData(a, &query) == STATUS_SUCCESS);
    query.pAllocationPrivateDriverData = &allocation; query.pResourcePrivateDriverData = &resource;
    NR_CHECK(registration.DxgkDdiGetStandardAllocationDriverData(a, &query) == STATUS_SUCCESS);
    DXGK_ALLOCATIONINFO info{}; info.pPrivateDriverData = &allocation; info.PrivateDriverDataSize = sizeof(allocation);
    DXGKARG_CREATEALLOCATION create{}; create.NumAllocations = 1; create.pAllocationInfo = &info;
    NR_CHECK(registration.DxgkDdiCreateAllocation(a, &create) == STATUS_NOT_SUPPORTED && !info.hAllocation);
    create.Flags.Resource = 1; create.pPrivateDriverData = &resource; create.PrivateDriverDataSize = sizeof(resource);
    resource.flags = 0; resource.row_pitch = 512; resource.allocation_bytes = 20480;
    NR_CHECK(registration.DxgkDdiCreateAllocation(a, &create) == STATUS_INVALID_PARAMETER && !info.hAllocation);
    resource = allocation;
    NR_CHECK(registration.DxgkDdiCreateAllocation(a, &create) == STATUS_SUCCESS && create.hResource && info.hAllocation);
    NR_CHECK(info.Size == 16384 && info.Alignment == 4096 && info.SupportedReadSegmentSet == 1 && info.SupportedWriteSegmentSet == 1);
    NR_CHECK(info.Flags.Cached && info.Flags.CpuVisible);
    DXGKARG_DESTROYALLOCATION destroy{}; destroy.NumAllocations = 1; destroy.pAllocationList = &info.hAllocation;
    destroy.hResource = create.hResource; destroy.Flags.DestroyResource = 1;
    NR_CHECK(registration.DxgkDdiDestroyAllocation(a, &destroy) == STATUS_SUCCESS && !a->allocations && !a->resources);
    D3DKMDT_STAGINGSURFACEDATA staging{}; staging.Width = 320; staging.Height = 192;
    query = {}; query.StandardAllocationType = D3DKMDT_STANDARDALLOCATION_STAGINGSURFACE; query.pCreateStagingSurfaceData = &staging;
    NR_CHECK(registration.DxgkDdiGetStandardAllocationDriverData(a, &query) == STATUS_SUCCESS && !query.ResourcePrivateDriverDataSize);
    query.pAllocationPrivateDriverData = &allocation;
    NR_CHECK(registration.DxgkDdiGetStandardAllocationDriverData(a, &query) == STATUS_SUCCESS && staging.Pitch == 1280);
    info = {}; info.pPrivateDriverData = &allocation; info.PrivateDriverDataSize = sizeof(allocation);
    create = {}; create.NumAllocations = 1; create.pAllocationInfo = &info;
    NR_CHECK(registration.DxgkDdiCreateAllocation(a, &create) == STATUS_SUCCESS && info.hAllocation && !create.hResource);
    NR_CHECK(info.Size == 245760 && info.Flags.CpuVisible && info.Flags.Cached);
    destroy = {}; destroy.NumAllocations = 1; destroy.pAllocationList = &info.hAllocation;
    NR_CHECK(registration.DxgkDdiDestroyAllocation(a, &destroy) == STATUS_SUCCESS && !a->allocations && !a->resources);
    // Reproduce Windows' live staging request: Resource=1, no resource-private
    // metadata, one BGRX allocation. The returned resource owns its allocation.
    info.hAllocation = nullptr; create.hResource = nullptr; create.Flags.Resource = 1;
    for (UINT invalid = 0; invalid < 5; ++invalid) {
        const auto original = allocation;
        switch (invalid) {
        case 0: allocation.flags = 0; allocation.format = 87; break;
        case 1: allocation.flags = nr::kernel_contract::cross_adapter_surface; allocation.format = 87; break;
        case 2: create.NumAllocations = 2; break;
        case 3: create.pPrivateDriverData = &resource; break;
        case 4: create.PrivateDriverDataSize = 1; break;
        }
        NR_CHECK(registration.DxgkDdiCreateAllocation(a, &create) == STATUS_INVALID_PARAMETER &&
                 !info.hAllocation && !create.hResource && !a->allocations && !a->resources);
        allocation = original; create.NumAllocations = 1; create.pPrivateDriverData = nullptr; create.PrivateDriverDataSize = 0;
    }
    NR_CHECK(registration.DxgkDdiCreateAllocation(a, &create) == STATUS_SUCCESS && info.hAllocation && create.hResource);
    NR_CHECK(info.Size == 245760 && info.Flags.CpuVisible && info.Flags.Cached && a->allocations == 1 && a->resources == 1);
    DXGKARG_DESCRIBEALLOCATION description{}; description.hAllocation = info.hAllocation;
    NR_CHECK(registration.DxgkDdiDescribeAllocation(a, &description) == STATUS_SUCCESS &&
             description.Width == 320 && description.Height == 192 && description.Format == D3DDDIFMT_X8R8G8B8);
    destroy.hResource = create.hResource; destroy.Flags.DestroyResource = 1;
    NR_CHECK(registration.DxgkDdiDestroyAllocation(a, &destroy) == STATUS_SUCCESS && !a->allocations && !a->resources);
    return 0;
}
