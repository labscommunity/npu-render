// Actual DDI/queue/worker path with host-only handle and page-number models.
int present_test(Adapter *a, Allocation *source, Allocation *destination, UCHAR *backing, const ULONGLONG *offsets, bool gdi) {
    using namespace nr::kmd;
    const auto source_before = source->surface, destination_before = destination->surface;
    source->surface.width = destination->surface.width = 65;
    source->surface.height = destination->surface.height = 13;
    if (gdi) source->surface.flags = nr::kernel_contract::shadow_surface;
    destination->surface.flags = nr::kernel_contract::cross_adapter_surface;
    destination->surface.row_pitch = 384;
    Device device{}; device.adapter = a;
    DXGKARG_CREATECONTEXT created{}; created.EngineAffinity = 1;
    created.Flags.GdiContext = gdi ? 1 : 0;
    const auto initial_fence = a->scheduler.completed();
    NR_CHECK(registration.DxgkDdiCreateContext(&device, &created) == STATUS_SUCCESS && device.contexts == 1);
    NR_CHECK(static_cast<Context *>(created.hContext)->gdi == gdi);
    if (gdi) {
        NR_CHECK(created.ContextInfo.AllocationListSize == DXGK_ALLOCATION_LIST_SIZE_GDICONTEXT);
        DXGKARG_RENDER user_command{};
        NR_CHECK(registration.DxgkDdiRender(created.hContext, &user_command) == STATUS_NOT_SUPPORTED);
        NR_CHECK(registration.DxgkDdiDestroyDevice(&device) == STATUS_DEVICE_BUSY);
    }
    OpenAllocation opens[2]{}; DXGK_ALLOCATIONLIST allocations[3]{};
    for (UINT i = 0; i < 2; ++i) {
        opens[i].device = &device; opens[i].allocation = i ? destination : source;
        allocations[i + 1].hDeviceSpecificAllocation = &opens[i]; allocations[i + 1].WriteOperation = i;
    }
    for (SIZE_T i = 0; i < 8192; ++i) { backing[i] = UCHAR(i * 17 + i / 53); backing[8192 + i] = 0xc3; }
    const RECT rectangles[] = {{3, 2, 23, 7}, {30, 8, 63, 13}};
    PresentMarker markers[2]{}; PresentRecord records[2]{}; D3DDDI_PATCHLOCATIONLIST patches[4]{};
    // Use the registered context's advertised budgets, not just a hand-built
    // Context. The v40 live run advertised 40 private bytes for 64-byte records.
    NR_CHECK(created.ContextInfo.DmaBufferSize >= sizeof(markers) &&
             created.ContextInfo.DmaBufferPrivateDataSize >= sizeof(records) &&
             created.ContextInfo.PatchLocationListSize >= 4 && created.ContextInfo.AllocationListSize >= 3);
    DXGKARG_PRESENT args{}; args.Flags.Blt = 1; args.pAllocationList = allocations;
    args.SrcRect = {1, 1, 61, 12}; args.DstRect = {3, 2, 63, 13};
    args.SubRectCnt = 2; args.pDstSubRects = rectangles;
    args.pDmaBuffer = markers; args.DmaSize = sizeof(markers);
    args.pDmaBufferPrivateData = records; args.DmaBufferPrivateDataSize = sizeof(records);
    args.pPatchLocationListOut = patches; args.PatchLocationListOutSize = 4;
    const auto original = args;
    for (UINT failure = 0; failure < 9; ++failure) {
        switch (failure) {
        case 0: args.Flags.Flip = 1; break;
        case 1: args.SrcRect.right++; break;
        case 2: args.pDstSubRects = nullptr; break;
        case 3: args.DstRect.left = -1; break;
        case 4: args.MultipassOffset = 2; break;
        case 5: opens[1].read_only = true; break;
        case 6: destination->surface.format = 28; break;
        case 7: args.PatchLocationListOutSize = 1; break;
        case 8: args.pDmaBufferPrivateData = nullptr; break;
        }
        NR_CHECK(!NT_SUCCESS(registration.DxgkDdiPresent(created.hContext, &args)));
        NR_CHECK(args.pDmaBuffer == markers && args.pPatchLocationListOut == patches && !records[0].shape.rows);
        args = original; opens[1].read_only = false; destination->surface.format = 87;
    }
    NR_CHECK(registration.DxgkDdiPresent(created.hContext, &args) == STATUS_SUCCESS && args.MultipassOffset == 2);
    NR_CHECK(args.pDmaBuffer == markers + 2 && args.pDmaBufferPrivateData == records + 2 && args.pPatchLocationListOut == patches + 4);
    NR_CHECK(!records[0].patched && !records[1].patched && records[0].shape.source_pitch == 512 && records[0].shape.destination_pitch == 384);
    DXGKARG_PATCH patch{}; patch.Flags.Present = patch.Flags.RedirectedPresent = 1;
    patch.pDmaBuffer = markers; patch.DmaBufferSize = patch.DmaBufferSubmissionEndOffset = sizeof(markers);
    patch.pDmaBufferPrivateData = records; patch.DmaBufferPrivateDataSize = patch.DmaBufferPrivateDataSubmissionEndOffset = sizeof(records);
    patch.pAllocationList = allocations; patch.AllocationListSize = 3;
    patch.pPatchLocationList = patches; patch.PatchLocationListSize = patch.PatchLocationListSubmissionLength = 4;
    NR_CHECK(!NT_SUCCESS(registration.DxgkDdiPatch(a, &patch)) && !records[0].patched);
    for (UINT i = 0; i < 2; ++i) { allocations[i + 1].SegmentId = 1; allocations[i + 1].PhysicalAddress.QuadPart = LONGLONG(offsets[i]); }
    patches[3].AllocationOffset++; // Reject a bad later rectangle before patching an earlier one.
    NR_CHECK(!NT_SUCCESS(registration.DxgkDdiPatch(a, &patch)) && !records[0].patched && !markers[0].source);
    patches[3].AllocationOffset--;
    NR_CHECK(registration.DxgkDdiPatch(a, &patch) == STATUS_SUCCESS && records[0].patched && records[1].patched);
    DXGKARG_SUBMITCOMMAND submit{}; submit.Flags.Present = submit.Flags.RedirectedPresent = 1;
    submit.SubmissionFenceId = initial_fence + 1; submit.DmaBufferSize = submit.DmaBufferSubmissionEndOffset = sizeof(markers);
    submit.pDmaBufferPrivateData = records;
    submit.DmaBufferPrivateDataSize = submit.DmaBufferPrivateDataSubmissionEndOffset = sizeof(records);
    records[1].shape.source_pitch = 1;
    NR_CHECK(registration.DxgkDdiSubmitCommand(a, &submit) == STATUS_INVALID_PARAMETER && !a->scheduler.pending());
    records[1].shape.source_pitch = 512;
    NR_CHECK(registration.DxgkDdiSubmitCommand(a, &submit) == STATUS_SUCCESS);
    RtlZeroMemory(records, sizeof(records));
    NR_CHECK(a->engine.process_one() == STATUS_SUCCESS && a->scheduler.completed() == initial_fence + 1);
    for (SIZE_T i = 0; i < 8192; ++i) {
        const auto y = i / 384, x = i % 384 / 4, channel = i % 4;
        const bool copied = (x >= 3 && x < 23 && y >= 2 && y < 7) || (x >= 30 && x < 63 && y >= 8 && y < 13);
        UCHAR expected = 0xc3;
        if (copied) expected = backing[(y - 1) * 512 + (x - 2) * 4 + channel];
        NR_CHECK(backing[8192 + i] == expected);
    }
    NR_CHECK(notifications[notification_count - 1].InterruptType == DXGK_INTERRUPT_DMA_COMPLETED &&
             notifications[notification_count - 1].DmaCompleted.SubmissionFenceId == initial_fence + 1);
    // Force two passes and confirm the second call resumes at the second rect.
    args = original; args.DmaSize = sizeof(PresentMarker);
    NR_CHECK(registration.DxgkDdiPresent(created.hContext, &args) == STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER && args.MultipassOffset == 1);
    NR_CHECK(records[0].source_relative == 512 + 4 && records[0].shape.rows == 5);
    args = original; args.MultipassOffset = 1;
    NR_CHECK(registration.DxgkDdiPresent(created.hContext, &args) == STATUS_SUCCESS && args.MultipassOffset == 2);
    NR_CHECK(records[0].source_relative == 7 * 512 + 28 * 4 && records[0].shape.row_bytes == 33 * 4);
    // The OS's presentation staging allocation is BGRX. A byte copy from BGRA
    // preserves all defined RGB channels without inventing an alpha conversion.
    destination->surface.format = 88; destination->surface.flags = nr::kernel_contract::presentation_staging_surface;
    destination->surface.row_pitch = 512;
    for (SIZE_T i = 0; i < 8192; ++i) backing[8192 + i] = 0xc3;
    args = original;
    NR_CHECK(registration.DxgkDdiPresent(created.hContext, &args) == STATUS_SUCCESS && args.MultipassOffset == 2);
    NR_CHECK(registration.DxgkDdiPatch(a, &patch) == STATUS_SUCCESS);
    submit.SubmissionFenceId = initial_fence + 2;
    NR_CHECK(registration.DxgkDdiSubmitCommand(a, &submit) == STATUS_SUCCESS);
    RtlZeroMemory(records, sizeof(records));
    NR_CHECK(a->engine.process_one() == STATUS_SUCCESS && a->scheduler.completed() == initial_fence + 2);
    for (SIZE_T i = 0; i < 8192; ++i) {
        const auto y = i / 512, x = i % 512 / 4, channel = i % 4;
        const bool copied = (x >= 3 && x < 23 && y >= 2 && y < 7) || (x >= 30 && x < 63 && y >= 8 && y < 13);
        UCHAR expected = 0xc3;
        if (copied) expected = backing[(y - 1) * 512 + (x - 2) * 4 + channel];
        NR_CHECK(backing[8192 + i] == expected);
    }
    source->surface.format = 88; source->surface.flags = nr::kernel_contract::presentation_staging_surface;
    destination->surface.format = 87; destination->surface.flags = 0;
    args = original;
    NR_CHECK(!NT_SUCCESS(registration.DxgkDdiPresent(created.hContext, &args)) && args.pDmaBuffer == markers);
    NR_CHECK(!nr::kernel_contract::copy_format_compatible(28, 88) && !nr::kernel_contract::copy_format_compatible(88, 87));
    NR_CHECK(registration.DxgkDdiDestroyContext(created.hContext) == STATUS_SUCCESS && !device.contexts);
    source->surface = source_before; destination->surface = destination_before;
    return 0;
}
