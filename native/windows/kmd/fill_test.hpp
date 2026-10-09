// Owned-allocation translation, patching and deferred execution in host shims.
#pragma once
int fill_test(Adapter *a) {
    using namespace nr::kmd;
    const auto baseline = outstanding;
    DXGKARG_CREATEDEVICE device{};
    NR_CHECK(registration.DxgkDdiCreateDevice(a, &device) == STATUS_SUCCESS);
    auto d = static_cast<Device *>(device.hDevice);
    DXGKARG_CREATECONTEXT context{};
    context.EngineAffinity = 1;
    for (unsigned bit = 1; bit < 32; ++bit) {
        context.Flags.Value = 1u << bit;
        NR_CHECK(registration.DxgkDdiCreateContext(d, &context) == STATUS_NOT_SUPPORTED);
        context.Flags.SystemContext = 1;
        NR_CHECK(registration.DxgkDdiCreateContext(d, &context) == STATUS_NOT_SUPPORTED);
        NR_CHECK(!d->contexts && !context.hContext);
    }
    context.Flags.Value = 0;
    context.Flags.SystemContext = 1;
    NR_CHECK(registration.DxgkDdiCreateContext(d, &context) == STATUS_SUCCESS);
    NR_CHECK(d->contexts == 1 && static_cast<Context *>(context.hContext)->system);
    DXGKARG_RENDER system_render{};
    NR_CHECK(registration.DxgkDdiRender(context.hContext, &system_render) == STATUS_NOT_SUPPORTED);
    NR_CHECK(registration.DxgkDdiDestroyContext(context.hContext) == STATUS_SUCCESS);
    context = {};
    context.EngineAffinity = 1;
    context.Flags.Value = 0;
    fail_after = 0;
    NR_CHECK(registration.DxgkDdiCreateContext(d, &context) == STATUS_INSUFFICIENT_RESOURCES);
    fail_after = -1;
    NR_CHECK(!d->contexts && registration.DxgkDdiCreateContext(d, &context) == STATUS_SUCCESS);
    NR_CHECK(context.ContextInfo.DmaBufferPrivateDataSize >= sizeof(FillRecord));
    nr::kernel_contract::Surface surface{};
    surface.width = 64; surface.height = 48; surface.format = 87;
    surface.row_pitch = 256; surface.allocation_bytes = 12288;
    DXGK_ALLOCATIONINFO info{};
    info.pPrivateDriverData = &surface; info.PrivateDriverDataSize = sizeof(surface);
    DXGKARG_CREATEALLOCATION create{};
    create.NumAllocations = 1; create.pAllocationInfo = &info;
    NR_CHECK(registration.DxgkDdiCreateAllocation(a, &create) == STATUS_SUCCESS);
    const auto previous_lookup = a->kernel.DxgkCbGetHandleData;
    a->kernel.DxgkCbGetHandleData = allocation_lookup;
    lookup_allocations[0] = static_cast<Allocation *>(info.hAllocation);
    DXGK_OPENALLOCATIONINFO opened{};
    opened.hAllocation = 101;
    DXGKARG_OPENALLOCATION open{};
    open.NumAllocations = 1; open.pOpenAllocation = &opened; open.Flags.Create = 1;
    NR_CHECK(registration.DxgkDdiOpenAllocation(d, &open) == STATUS_SUCCESS);
    DXGK_ALLOCATIONLIST list{};
    list.hDeviceSpecificAllocation = opened.hDeviceSpecificAllocation;
    list.WriteOperation = 1;
    auto owned_open = static_cast<OpenAllocation *>(opened.hDeviceSpecificAllocation);
    NR_CHECK(writable_allocation(a, &list, 1, d) == info.hAllocation);
    owned_open->read_only = true;
    NR_CHECK(!writable_allocation(a, &list, 1, d));
    owned_open->read_only = false;
    Device foreign{};
    NR_CHECK(!writable_allocation(a, &list, 1, &foreign));
    list.WriteOperation = 0;
    NR_CHECK(!writable_allocation(a, &list, 1, d));
    list.WriteOperation = 1;

    nr::kernel_contract::FillSurface command{};
    command.pattern = 0x04030201;
    nr::kernel_contract::FillSurface snapshot{};
    NR_CHECK(Fill::snapshot(nullptr, sizeof(command), snapshot) == STATUS_INVALID_USER_BUFFER);
    NR_CHECK(Fill::snapshot(reinterpret_cast<void *>(1), sizeof(command), snapshot) == STATUS_INVALID_USER_BUFFER);
    NR_CHECK(Fill::snapshot(reinterpret_cast<void *>(MAXULONG_PTR), sizeof(command), snapshot) == STATUS_INVALID_USER_BUFFER);
    NR_CHECK(Fill::snapshot(&command, sizeof(command) - 1, snapshot) == STATUS_INVALID_USER_BUFFER);
    command.reserved = 1;
    NR_CHECK(Fill::snapshot(&command, sizeof(command), snapshot) == STATUS_ILLEGAL_INSTRUCTION);
    command.reserved = 0;
    FillMarker marker{};
    FillRecord record{};
    D3DDDI_PATCHLOCATIONLIST location{};
    DXGKARG_RENDER render_args{};
    render_args.pCommand = &command; render_args.CommandLength = sizeof(command);
    render_args.pDmaBuffer = &marker; render_args.DmaSize = sizeof(marker);
    render_args.pDmaBufferPrivateData = &record; render_args.DmaBufferPrivateDataSize = sizeof(record);
    render_args.pAllocationList = &list; render_args.AllocationListSize = 1;
    render_args.pPatchLocationListOut = &location; render_args.PatchLocationListOutSize = 1;
    const auto original = render_args;
    render_args.DmaSize--;
    NR_CHECK(Fill::build(a->segment, SIZE_T(surface.allocation_bytes), render_args) == STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER);
    NR_CHECK(render_args.pDmaBuffer == &marker && !record.bytes);
    render_args = original;
    if (lab_fill_enabled) NR_CHECK(registration.DxgkDdiRender(context.hContext, &render_args) == STATUS_SUCCESS);
    else {
        NR_CHECK(registration.DxgkDdiRender(context.hContext, &render_args) == STATUS_NOT_SUPPORTED);
        NR_CHECK(Fill::build(a->segment, SIZE_T(surface.allocation_bytes), render_args) == STATUS_SUCCESS);
    }
    NR_CHECK(render_args.pDmaBuffer == reinterpret_cast<UCHAR *>(&marker) + sizeof(marker));
    NR_CHECK(!record.patched && record.bytes == surface.allocation_bytes);
    PagingCommand execution{};
    NR_CHECK(Fill::execution(&record, sizeof(record), execution) == STATUS_INVALID_PARAMETER);

    DXGKARG_PATCH patch{};
    patch.pDmaBuffer = &marker; patch.DmaBufferSize = patch.DmaBufferSubmissionEndOffset = sizeof(marker);
    patch.pDmaBufferPrivateData = &record;
    patch.DmaBufferPrivateDataSize = patch.DmaBufferPrivateDataSubmissionEndOffset = sizeof(record);
    patch.pAllocationList = &list; patch.AllocationListSize = 1;
    patch.pPatchLocationList = &location;
    patch.PatchLocationListSize = patch.PatchLocationListSubmissionLength = 1;
    NR_CHECK(!NT_SUCCESS(registration.DxgkDdiPatch(a, &patch))); // Not resident yet.
    list.SegmentId = 1;
    const LONGLONG offsets[] = {4096, 8192, 4096};
    for (const LONGLONG offset : offsets) {
        list.PhysicalAddress.QuadPart = offset;
        NR_CHECK(registration.DxgkDdiPatch(a, &patch) == STATUS_SUCCESS);
        NR_CHECK(record.patched && record.offset == ULONGLONG(offset) && marker.offset == record.offset);
        NR_CHECK(*static_cast<ULONG *>(a->segment.range(4096, 4)) == 0);
    }
    const auto good_record = record;
    const auto good_marker = marker;
    const auto good_patch = patch;
    for (unsigned mutation = 0; mutation < 12; ++mutation) {
        switch (mutation) {
        case 0: patch.Flags.Paging = 1; break;
        case 1: patch.EngineOrdinal = 1; break;
        case 2: patch.DmaBufferSubmissionEndOffset--; break;
        case 3: patch.DmaBufferPrivateDataSubmissionEndOffset--; break;
        case 4: location.PatchOffset++; break;
        case 5: location.AllocationIndex = 1; break;
        case 6: record.bytes += PAGE_SIZE; break;
        case 7: record.magic = 0; break;
        case 8: marker.offset++; break;
        case 9: list.PhysicalAddress.QuadPart = LONGLONG(a->segment.capacity()) - 4; break;
        case 10: list.WriteOperation = 0; break;
        case 11: owned_open->read_only = true; break;
        }
        const auto before_record = record;
        const auto before_marker = marker;
        NR_CHECK(!NT_SUCCESS(registration.DxgkDdiPatch(a, &patch)));
        NR_CHECK(record.magic == before_record.magic && record.version == before_record.version &&
                 record.offset == before_record.offset && record.bytes == before_record.bytes &&
                 record.pattern == before_record.pattern && record.patched == before_record.patched);
        NR_CHECK(marker.magic == before_marker.magic && marker.reserved == before_marker.reserved &&
                 marker.offset == before_marker.offset);
        record = good_record; marker = good_marker; patch = good_patch;
        location = {}; location.PatchOffset = FIELD_OFFSET(FillMarker, offset);
        list.PhysicalAddress.QuadPart = 4096; list.WriteOperation = 1; owned_open->read_only = false;
    }
    // A resident allocation must be fully prepatched even if Windows skips Patch.
    render_args = original;
    NR_CHECK(Fill::build(a->segment, SIZE_T(surface.allocation_bytes), render_args) == STATUS_SUCCESS);
    NR_CHECK(record.patched && record.offset == 4096);
    Scheduler queue;
    Submissions submissions;
    NR_CHECK(submissions.prepare(queue, 2) == STATUS_SUCCESS && queue.start() == QueueResult::ok);
    DXGKARG_SUBMITCOMMAND submit_args{};
    submit_args.SubmissionFenceId = 17;
    submit_args.DmaBufferSize = submit_args.DmaBufferSubmissionEndOffset = sizeof(marker);
    submit_args.pDmaBufferPrivateData = &record;
    submit_args.DmaBufferPrivateDataSize = submit_args.DmaBufferPrivateDataSubmissionEndOffset = sizeof(record);
    Ticket ticket{};
    NR_CHECK(submissions.capture(submit_args, ticket) == STATUS_SUCCESS);
    record.pattern = 0; record.offset = 0; // Captured work must own its original values.
    auto target = static_cast<ULONG *>(a->segment.range(4096, SIZE_T(surface.allocation_bytes)));
    for (SIZE_T i = 0; i < surface.allocation_bytes / sizeof(ULONG); ++i) NR_CHECK(!target[i]);
    Work work{};
    NR_CHECK(queue.take(work) == QueueResult::ok);
    const auto packet = submissions.packet(work.ticket);
    NR_CHECK(packet && packet->commands == 1);
    NR_CHECK(Paging::execute(a->segment, packet->paging[0]) == STATUS_SUCCESS);
    for (SIZE_T i = 0; i < surface.allocation_bytes / sizeof(ULONG); ++i) NR_CHECK(target[i] == 0x04030201);
    NR_CHECK(!*static_cast<ULONG *>(a->segment.range(4092, 4)) &&
             !*static_cast<ULONG *>(a->segment.range(4096 + surface.allocation_bytes, 4)));
    NR_CHECK(queue.complete(work.ticket) == QueueResult::ok && queue.completed() == 17);
    NR_CHECK(queue.stop() == QueueResult::ok && submissions.release() == STATUS_SUCCESS);
    RtlZeroMemory(target, SIZE_T(surface.allocation_bytes));
    DXGKARG_CLOSEALLOCATION close{};
    close.NumAllocations = 1; close.pOpenHandleList = &opened.hDeviceSpecificAllocation;
    NR_CHECK(registration.DxgkDdiCloseAllocation(d, &close) == STATUS_SUCCESS);
    DXGKARG_DESTROYALLOCATION destroy{};
    destroy.NumAllocations = 1; destroy.pAllocationList = &info.hAllocation;
    NR_CHECK(registration.DxgkDdiDestroyAllocation(a, &destroy) == STATUS_SUCCESS);
    NR_CHECK(registration.DxgkDdiDestroyContext(context.hContext) == STATUS_SUCCESS);
    NR_CHECK(registration.DxgkDdiDestroyDevice(d) == STATUS_SUCCESS);
    a->kernel.DxgkCbGetHandleData = previous_lookup;
    lookup_allocations[0] = nullptr;
    NR_CHECK(outstanding == baseline && !a->devices && !a->allocations && !a->opens);
    return 0;
}
