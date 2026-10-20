// Actual render/patch entry points with explicitly owned OS-handle shims.
#pragma once
int copy_test(Adapter *a) {
    using namespace nr::kmd;
    Device device{}; device.adapter = a;
    Context context{}; context.device = &device;
    nr::kernel_contract::Surface surface{};
    surface.width = 64; surface.height = 48; surface.format = 87;
    surface.row_pitch = 256; surface.allocation_bytes = 12288;
    Allocation allocations[2]{};
    OpenAllocation opens[2]{};
    DXGK_ALLOCATIONLIST list[2]{};
    for (UINT i = 0; i < 2; ++i) {
        allocations[i].adapter = a; allocations[i].surface = surface;
        opens[i].device = &device; opens[i].allocation = &allocations[i];
        list[i].hDeviceSpecificAllocation = &opens[i]; list[i].WriteOperation = i;
    }
    NR_CHECK(copy_allocations(a, list, 2, &device) == &allocations[1]);
    opens[0].read_only = true; // Read-only source is valid, read-only destination is not.
    NR_CHECK(copy_allocations(a, list, 2, &device) == &allocations[1]);
    opens[1].read_only = true; NR_CHECK(!copy_allocations(a, list, 2, &device)); opens[1].read_only = false;
    Device foreign{}; foreign.adapter = a;
    opens[0].device = &foreign; NR_CHECK(!copy_allocations(a, list, 2)); opens[0].device = &device;
    allocations[0].surface.format = 28; NR_CHECK(!copy_allocations(a, list, 2)); allocations[0].surface = surface;
    opens[0].allocation = &allocations[1]; NR_CHECK(!copy_allocations(a, list, 2)); opens[0].allocation = &allocations[0];
    nr::kernel_contract::CopySurface command{};
    CopyMarker marker{}; CopyRecord record{};
    D3DDDI_PATCHLOCATIONLIST locations[2]{};
    DXGKARG_RENDER render{};
    render.pCommand = &command; render.CommandLength = sizeof(command);
    render.pDmaBuffer = &marker; render.DmaSize = sizeof(marker);
    render.pDmaBufferPrivateData = &record; render.DmaBufferPrivateDataSize = sizeof(record);
    render.pAllocationList = list; render.AllocationListSize = 2;
    render.pPatchLocationListOut = locations; render.PatchLocationListOutSize = 2;
    const auto original = render;
    render.pCommand = reinterpret_cast<void *>(1);
    NR_CHECK(Copy::build(a->segment, SIZE_T(surface.allocation_bytes), render) == STATUS_INVALID_USER_BUFFER);
    render = original; command.reserved[1] = 1;
    NR_CHECK(Copy::build(a->segment, SIZE_T(surface.allocation_bytes), render) == STATUS_ILLEGAL_INSTRUCTION);
    command.reserved[1] = 0; render.PatchLocationListOutSize = 1;
    NR_CHECK(Copy::build(a->segment, SIZE_T(surface.allocation_bytes), render) == STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER);
    NR_CHECK(render.pDmaBuffer == &marker && !record.bytes);
    render = original;
    if (lab_fill_enabled) NR_CHECK(registration.DxgkDdiRender(&context, &render) == STATUS_SUCCESS);
    else {
        NR_CHECK(registration.DxgkDdiRender(&context, &render) == STATUS_NOT_SUPPORTED);
        NR_CHECK(Copy::build(a->segment, SIZE_T(surface.allocation_bytes), render) == STATUS_SUCCESS);
    }
    NR_CHECK(!record.patched && record.bytes == surface.allocation_bytes && render.pPatchLocationListOut == locations + 2);
    PagingCommand execution{};
    NR_CHECK(Copy::execution(&record, sizeof(record), execution) == STATUS_INVALID_PARAMETER);
    DXGKARG_PATCH patch{};
    patch.pDmaBuffer = &marker; patch.DmaBufferSize = patch.DmaBufferSubmissionEndOffset = sizeof(marker);
    patch.pDmaBufferPrivateData = &record;
    patch.DmaBufferPrivateDataSize = patch.DmaBufferPrivateDataSubmissionEndOffset = sizeof(record);
    patch.pAllocationList = list; patch.AllocationListSize = 2;
    patch.pPatchLocationList = locations; patch.PatchLocationListSize = patch.PatchLocationListSubmissionLength = 2;
    NR_CHECK(!NT_SUCCESS(registration.DxgkDdiPatch(a, &patch)));
    list[0].SegmentId = 1; list[0].PhysicalAddress.QuadPart = 4096;
    list[1].SegmentId = 1; list[1].PhysicalAddress.QuadPart = 65536;
    NR_CHECK(registration.DxgkDdiPatch(a, &patch) == STATUS_SUCCESS && record.patched);
    const auto good_record = record; const auto good_marker = marker; const auto good_patch = patch;
    const auto good_location = locations[1];
    for (unsigned mutation = 0; mutation < 13; ++mutation) {
        switch (mutation) {
        case 0: list[0].WriteOperation = 1; break;
        case 1: list[1].WriteOperation = 0; break;
        case 2: list[0].SegmentId = 2; break;
        case 3: list[1].PhysicalAddress.QuadPart = 8192; break; // Overlapping allocations.
        case 4: locations[1].AllocationIndex = 0; break;
        case 5: locations[1].PatchOffset++; break;
        case 6: locations[1].SplitOffset++; break;
        case 7: record.bytes++; break;
        case 8: record.reserved = 1; break;
        case 9: marker.source++; break;
        case 10: patch.PatchLocationListSubmissionStart = MAXULONG; break;
        case 11: patch.DmaBufferPrivateDataSubmissionEndOffset--; break;
        case 12: opens[1].read_only = true; break;
        }
        const auto before_record = record; const auto before_marker = marker;
        NR_CHECK(!NT_SUCCESS(registration.DxgkDdiPatch(a, &patch)));
        NR_CHECK(record.source == before_record.source && record.destination == before_record.destination &&
                 record.bytes == before_record.bytes && record.patched == before_record.patched && record.reserved == before_record.reserved);
        NR_CHECK(marker.source == before_marker.source && marker.destination == before_marker.destination);
        record = good_record; marker = good_marker; patch = good_patch; locations[1] = good_location;
        list[0].WriteOperation = 0; list[1].WriteOperation = 1; list[0].SegmentId = 1;
        list[1].PhysicalAddress.QuadPart = 65536; opens[1].read_only = false;
    }
    // Repatch after relocation and verify Build also handles already resident inputs.
    list[1].PhysicalAddress.QuadPart = 98304;
    NR_CHECK(registration.DxgkDdiPatch(a, &patch) == STATUS_SUCCESS && record.destination == 98304);
    render = original;
    NR_CHECK(Copy::build(a->segment, SIZE_T(surface.allocation_bytes), render) == STATUS_SUCCESS && record.patched);
    Scheduler queue; Submissions submissions;
    NR_CHECK(submissions.prepare(queue, 2) == STATUS_SUCCESS && queue.start() == QueueResult::ok);
    DXGKARG_SUBMITCOMMAND submit{}; submit.SubmissionFenceId = 21;
    submit.DmaBufferSize = submit.DmaBufferSubmissionEndOffset = sizeof(marker);
    submit.pDmaBufferPrivateData = &record;
    submit.DmaBufferPrivateDataSize = submit.DmaBufferPrivateDataSubmissionEndOffset = sizeof(record);
    auto source = static_cast<UCHAR *>(a->segment.range(4096, SIZE_T(surface.allocation_bytes)));
    auto destination = static_cast<UCHAR *>(a->segment.range(98304, SIZE_T(surface.allocation_bytes)));
    for (SIZE_T i = 0; i < surface.allocation_bytes; ++i) { source[i] = UCHAR(i * 37); destination[i] = 0; }
    Ticket ticket{}; NR_CHECK(submissions.capture(submit, ticket) == STATUS_SUCCESS);
    record = {}; // The captured packet must own its endpoints and extent.
    Work work{}; NR_CHECK(queue.take(work) == QueueResult::ok);
    const auto packet = submissions.packet(work.ticket);
    NR_CHECK(packet && packet->commands == 1 && packet->paging[0].operation == DXGK_OPERATION_TRANSFER);
    NR_CHECK(Paging::execute(a->segment, packet->paging[0]) == STATUS_SUCCESS);
    for (SIZE_T i = 0; i < surface.allocation_bytes; ++i) NR_CHECK(destination[i] == UCHAR(i * 37) && source[i] == destination[i]);
    NR_CHECK(!*static_cast<UCHAR *>(a->segment.range(98303, 1)) &&
             !*static_cast<UCHAR *>(a->segment.range(98304 + surface.allocation_bytes, 1)));
    NR_CHECK(queue.complete(work.ticket) == QueueResult::ok && queue.completed() == 21);
    NR_CHECK(queue.stop() == QueueResult::ok && submissions.release() == STATUS_SUCCESS);
    RtlZeroMemory(source, SIZE_T(surface.allocation_bytes)); RtlZeroMemory(destination, SIZE_T(surface.allocation_bytes));
    return 0;
}
