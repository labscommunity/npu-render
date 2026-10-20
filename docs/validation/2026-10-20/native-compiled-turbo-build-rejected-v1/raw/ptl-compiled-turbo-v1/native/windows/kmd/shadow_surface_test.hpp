// Shadow memory is CPU-lockable backing, never a scanout/primary allocation.
int shadow_surface_test(Adapter *a) {
    using namespace nr::kernel_contract;
    const D3DDDIFORMAT formats[] = {D3DDDIFMT_A8R8G8B8, D3DDDIFMT_A8B8G8R8, D3DDDIFMT_X8R8G8B8};
    const UINT expected_formats[] = {87, 28, 88};
    for (UINT index = 0; index < 3; ++index) {
        D3DKMDT_SHADOWSURFACEDATA input{};
        input.Width = 65; input.Height = 37; input.Format = formats[index]; input.Pitch = 0x1234;
        DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA query{};
        query.StandardAllocationType = D3DKMDT_STANDARDALLOCATION_SHADOWSURFACE;
        query.pCreateShadowSurfaceData = &input;
        NR_CHECK(registration.DxgkDdiGetStandardAllocationDriverData(a, &query) == STATUS_SUCCESS);
        NR_CHECK(input.Pitch == 0x1234 && query.AllocationPrivateDriverDataSize == sizeof(Surface) &&
                 !query.ResourcePrivateDriverDataSize);
        struct Guarded { UINT before = 0x12345678; Surface value{}; UINT after = 0x87654321; } data;
        data.value.width = 99;
        query.pAllocationPrivateDriverData = &data.value;
        query.AllocationPrivateDriverDataSize = sizeof(Surface) - 1;
        NR_CHECK(registration.DxgkDdiGetStandardAllocationDriverData(a, &query) == STATUS_BUFFER_TOO_SMALL);
        NR_CHECK(input.Pitch == 0x1234 && data.value.width == 99);
        NR_CHECK(registration.DxgkDdiGetStandardAllocationDriverData(a, &query) == STATUS_SUCCESS);
        NR_CHECK(input.Pitch == 512 && data.value.row_pitch == 512 && data.value.allocation_bytes == 20480 &&
                 data.value.flags == shadow_surface && data.value.format == expected_formats[index] && valid(data.value));
        NR_CHECK(data.before == 0x12345678 && data.after == 0x87654321);
        const auto expected = data.value;
        for (UINT invalid = 0; invalid < 8; ++invalid) {
            input.Width = 65; input.Height = 37; input.Format = formats[index]; input.Pitch = 0x1234;
            switch (invalid) {
            case 0: input.Width = 0; break;
            case 1: input.Height = 0; break;
            case 2: input.Width = MAXULONG; break;
            case 3: input.Height = 8193; break;
            case 4: input.Width = 8192; input.Height = 4097; break;
            case 5: input.Format = D3DDDIFMT_R5G6B5; break;
            case 6: query.pCreateShadowSurfaceData = nullptr; break;
            case 7: query.pResourcePrivateDriverData = &data.value; break;
            }
            NR_CHECK(!NT_SUCCESS(registration.DxgkDdiGetStandardAllocationDriverData(a, &query)));
            NR_CHECK(input.Pitch == 0x1234 && same_surface(data.value, expected));
            query.pCreateShadowSurfaceData = &input; query.pResourcePrivateDriverData = nullptr;
        }
        for (UINT resource = 0; resource < 2; ++resource) {
            const auto baseline = outstanding;
            DXGK_ALLOCATIONINFO info{}; info.pPrivateDriverData = &data.value; info.PrivateDriverDataSize = sizeof(Surface);
            DXGKARG_CREATEALLOCATION create{}; create.NumAllocations = 1; create.pAllocationInfo = &info;
            create.Flags.Resource = resource;
            fail_after = 0;
            NR_CHECK(registration.DxgkDdiCreateAllocation(a, &create) == STATUS_INSUFFICIENT_RESOURCES);
            NR_CHECK(!info.hAllocation && !create.hResource && outstanding == baseline);
            fail_after = -1;
            NR_CHECK(registration.DxgkDdiCreateAllocation(a, &create) == STATUS_SUCCESS);
            NR_CHECK(info.Flags.CpuVisible && info.Flags.Cached && info.Size == 20480 && info.Alignment == 4096);
            DXGKARG_DESCRIBEALLOCATION description{}; description.hAllocation = info.hAllocation;
            NR_CHECK(registration.DxgkDdiDescribeAllocation(a, &description) == STATUS_SUCCESS &&
                     description.Format == formats[index] && description.Width == 65 && description.Height == 37);
            static Allocation *lookup_allocation = nullptr;
            lookup_allocation = static_cast<Allocation *>(info.hAllocation);
            const auto old_lookup = a->kernel.DxgkCbGetHandleData;
            a->kernel.DxgkCbGetHandleData = [](const DXGKARGCB_GETHANDLEDATA *lookup) -> void * {
                return lookup && lookup->hObject == 902 && lookup->Type == DXGK_HANDLE_ALLOCATION &&
                       !lookup->Flags.Value ? lookup_allocation : nullptr;
            };
            DXGKARG_CREATEDEVICE device{}; device.Flags.GdiDevice = 1;
            NR_CHECK(registration.DxgkDdiCreateDevice(a, &device) == STATUS_SUCCESS);
            DXGK_OPENALLOCATIONINFO opened{}; opened.hAllocation = 902;
            DXGKARG_OPENALLOCATION open{}; open.NumAllocations = 1; open.pOpenAllocation = &opened; open.Flags.Create = 1;
            NR_CHECK(registration.DxgkDdiOpenAllocation(device.hDevice, &open) == STATUS_SUCCESS);
            DXGKARG_DESTROYALLOCATION destroy{}; destroy.NumAllocations = 1; destroy.pAllocationList = &info.hAllocation;
            destroy.hResource = create.hResource; destroy.Flags.DestroyResource = resource;
            NR_CHECK(registration.DxgkDdiDestroyAllocation(a, &destroy) == STATUS_DEVICE_BUSY);
            DXGKARG_CLOSEALLOCATION close{}; close.NumAllocations = 1; close.pOpenHandleList = &opened.hDeviceSpecificAllocation;
            NR_CHECK(registration.DxgkDdiCloseAllocation(device.hDevice, &close) == STATUS_SUCCESS);
            NR_CHECK(registration.DxgkDdiDestroyDevice(device.hDevice) == STATUS_SUCCESS);
            a->kernel.DxgkCbGetHandleData = old_lookup; lookup_allocation = nullptr;
            NR_CHECK(registration.DxgkDdiDestroyAllocation(a, &destroy) == STATUS_SUCCESS && outstanding == baseline);
        }
    }
    return 0;
}
