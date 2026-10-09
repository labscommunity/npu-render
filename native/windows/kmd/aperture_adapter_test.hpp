// Actual DDI map/render/submit calls with explicit host-only OS handle/PFN shims.
int aperture_adapter_test() {
    using namespace nr::kmd;
    DEVICE_OBJECT physical{}; void *opaque = nullptr;
    NR_CHECK(registration.DxgkDdiAddDevice(&physical, &opaque) == STATUS_SUCCESS);
    auto a = static_cast<Adapter *>(opaque);
    dpc_owner = a; engine_mode = true; sync_mode = SyncMode::valid;
    DXGK_START_INFO start{}; start.RequiredDmaQueueEntry = 32;
    DXGKRNL_INTERFACE kernel{}; kernel.DeviceHandle = &registration;
    kernel.DxgkCbSynchronizeExecution = synchronize; kernel.DxgkCbNotifyInterrupt = notify_interrupt;
    kernel.DxgkCbQueueDpc = queue_dpc; kernel.DxgkCbNotifyDpc = notify_dpc;
    ULONG sources = 99, children = 99;
    for (long failure = 0; failure < 3; ++failure) {
        fail_after = failure;
        NR_CHECK(registration.DxgkDdiStartDevice(a, &start, &kernel, &sources, &children) == STATUS_INSUFFICIENT_RESOURCES);
        NR_CHECK(!a->started && !a->segment.ready() && !a->submissions.ready() && outstanding == 1);
    }
    fail_after = -1; thread_fail = true;
    NR_CHECK(registration.DxgkDdiStartDevice(a, &start, &kernel, &sources, &children) == STATUS_INSUFFICIENT_RESOURCES);
    NR_CHECK(!a->segment.ready() && outstanding == 1); thread_fail = false;
    NR_CHECK(registration.DxgkDdiStartDevice(a, &start, &kernel, &sources, &children) == STATUS_SUCCESS);
    NR_CHECK(!sources && !children && a->segment.is_aperture() && a->segment.capacity() == Aperture::bytes);
    DXGK_SEGMENTDESCRIPTOR3 descriptor{}; DXGK_QUERYSEGMENTOUT3 segment{};
    segment.NbSegment = 1; segment.pSegmentDescriptor = &descriptor;
    NR_CHECK(a->segment.describe(segment) == STATUS_SUCCESS);
    NR_CHECK(descriptor.Flags.Aperture && descriptor.Flags.CacheCoherent && !descriptor.Flags.CpuVisible &&
             !descriptor.CpuTranslatedAddress.QuadPart && !descriptor.BaseAddress.QuadPart && !segment.PagingBufferSegmentId);
    nr::kernel_contract::Surface surface{};
    surface.width = 128; surface.height = 16; surface.row_pitch = 512;
    surface.format = 87; surface.allocation_bytes = 8192;
    DXGK_ALLOCATIONINFO info[2]{};
    for (auto &i : info) { i.pPrivateDriverData = &surface; i.PrivateDriverDataSize = sizeof(surface); }
    DXGKARG_CREATEALLOCATION create{}; create.NumAllocations = 2; create.pAllocationInfo = info;
    NR_CHECK(registration.DxgkDdiCreateAllocation(a, &create) == STATUS_SUCCESS);
    for (const auto &i : info) NR_CHECK(i.Flags.CpuVisible && i.Flags.Cached && i.SupportedReadSegmentSet == 1);
    auto raw = static_cast<UCHAR *>(nr_host_allocate(6 * PAGE_SIZE)); NR_CHECK(raw);
    auto backing = reinterpret_cast<UCHAR *>((ULONG_PTR(raw) + PAGE_SIZE - 1) & ~ULONG_PTR(PAGE_SIZE - 1));
    RtlFillMemory(backing, 5 * PAGE_SIZE, 0x39);
    PMDL mdl = mdl_allocate(backing, 4 * PAGE_SIZE, FALSE, FALSE, nullptr); NR_CHECK(mdl);
    mdl->MdlFlags = MDL_PAGES_LOCKED;
    for (SIZE_T i = 0; i < 4; ++i) MmGetMdlPfnArray(mdl)[i] = ULONG_PTR(backing + i * PAGE_SIZE) / PAGE_SIZE;
    const ULONGLONG offsets[] = {100 * 1024 * 1024, 200 * 1024 * 1024};
    for (UINT i = 0; i < 2; ++i) {
        DXGKARG_BUILDPAGINGBUFFER map{}; map.Operation = DXGK_OPERATION_MAP_APERTURE_SEGMENT;
        auto &m = map.MapApertureSegment;
        m.hAllocation = info[i].hAllocation; m.SegmentId = 1; m.OffsetInPages = SIZE_T(offsets[i] / PAGE_SIZE);
        m.NumberOfPages = 2; m.pMdl = mdl; m.MdlOffset = i * 2; m.Flags.CacheCoherent = 1;
        NR_CHECK(registration.DxgkDdiBuildPagingBuffer(a, &map) == STATUS_SUCCESS);
        NR_CHECK(registration.DxgkDdiBuildPagingBuffer(a, &map) == STATUS_INVALID_PARAMETER);
        NR_CHECK(!map.pDmaBuffer && !map.pDmaBufferPrivateData); // Synchronous mapping, no fake packet.
    }
    mdl->MdlFlags = 0; mdl_free(mdl);
    NR_CHECK(a->segment.aperture().mapped_pages() == 4);
    NR_CHECK(registration.DxgkDdiStopDevice(a) == STATUS_DEVICE_BUSY);
    PagingCommand fill{}; fill.operation = DXGK_OPERATION_FILL; fill.bytes = 8192;
    fill.pattern = 0xa4a4a4a4; fill.destination.offset = offsets[0];
    NR_CHECK(Paging::execute(a->segment, fill) == STATUS_SUCCESS);
    for (SIZE_T i = 0; i < 8192; ++i) NR_CHECK(backing[i] == 0xa4 && backing[8192 + i] == 0x39);
    Device device{}; device.adapter = a; Context context{}; context.device = &device;
    OpenAllocation opened[2]{}; DXGK_ALLOCATIONLIST list[2]{};
    for (UINT i = 0; i < 2; ++i) {
        opened[i].device = &device; opened[i].allocation = static_cast<Allocation *>(info[i].hAllocation);
        list[i].hDeviceSpecificAllocation = &opened[i]; list[i].WriteOperation = i;
        list[i].SegmentId = 1; list[i].PhysicalAddress.QuadPart = LONGLONG(offsets[i]);
    }
    nr::kernel_contract::CopySurface command{};
    CopyMarker marker{}; CopyRecord record{}; D3DDDI_PATCHLOCATIONLIST locations[2]{};
    DXGKARG_RENDER render{};
    render.pCommand = &command; render.CommandLength = sizeof(command);
    render.pDmaBuffer = &marker; render.DmaSize = sizeof(marker);
    render.pDmaBufferPrivateData = &record; render.DmaBufferPrivateDataSize = sizeof(record);
    render.pAllocationList = list; render.AllocationListSize = 2;
    render.pPatchLocationListOut = locations; render.PatchLocationListOutSize = 2;
    NR_CHECK(registration.DxgkDdiRender(&context, &render) == STATUS_SUCCESS);
    NR_CHECK(record.patched && record.source == offsets[0] && record.destination == offsets[1]);
    DXGKARG_SUBMITCOMMAND submit{}; submit.SubmissionFenceId = 1;
    submit.DmaBufferSize = submit.DmaBufferSubmissionEndOffset = sizeof(marker);
    submit.pDmaBufferPrivateData = &record;
    submit.DmaBufferPrivateDataSize = submit.DmaBufferPrivateDataSubmissionEndOffset = sizeof(record);
    NR_CHECK(registration.DxgkDdiSubmitCommand(a, &submit) == STATUS_SUCCESS);
    record = {}; // Queue storage, not the caller's record, must own the copy.
    NR_CHECK(a->engine.process_one() == STATUS_SUCCESS && notification_count == 1);
    NR_CHECK(notifications[0].InterruptType == DXGK_INTERRUPT_DMA_COMPLETED &&
             notifications[0].DmaCompleted.SubmissionFenceId == 1);
    for (SIZE_T i = 0; i < 8192; ++i) NR_CHECK(backing[i] == 0xa4 && backing[8192 + i] == 0xa4);
    for (UINT i = 0; i < 2; ++i) {
        DXGKARG_BUILDPAGINGBUFFER unmap{}; unmap.Operation = DXGK_OPERATION_UNMAP_APERTURE_SEGMENT;
        auto &u = unmap.UnmapApertureSegment;
        u.hAllocation = info[i].hAllocation; u.SegmentId = 1; u.OffsetInPages = SIZE_T(offsets[i] / PAGE_SIZE);
        u.NumberOfPages = 2; u.DummyPage.QuadPart = LONGLONG(ULONG_PTR(backing + 4 * PAGE_SIZE));
        NR_CHECK(registration.DxgkDdiBuildPagingBuffer(a, &unmap) == STATUS_SUCCESS);
    }
    NR_CHECK(!a->segment.aperture().mapped_pages() && a->segment.aperture().dummy_pages() == 4);
    HANDLE allocations[] = {info[0].hAllocation, info[1].hAllocation};
    DXGKARG_DESTROYALLOCATION destroy{}; destroy.NumAllocations = 2; destroy.pAllocationList = allocations;
    NR_CHECK(registration.DxgkDdiDestroyAllocation(a, &destroy) == STATUS_SUCCESS);
    NR_CHECK(registration.DxgkDdiStopDevice(a) == STATUS_SUCCESS);
    nr_host_free(raw);
    NR_CHECK(registration.DxgkDdiRemoveDevice(a) == STATUS_SUCCESS && !outstanding && !mdl_outstanding);
    NR_CHECK(!aperture_contract_error && !aperture_mappings && !worker_contract_error && !sync_contract_error);
    return 0;
}
