// Included only by the Windows host DDI test after the actual driver source.
int paging_test(Adapter *adapter) {
    using nr::kmd::Paging;
    using nr::kmd::PagingCommand;
    using nr::kmd::PagingMarker;
    UCHAR dma[sizeof(PagingMarker) * 4]{};
    UCHAR private_data[sizeof(PagingCommand) * 4]{};
    DXGKARG_BUILDPAGINGBUFFER args{};
    args.pDmaBuffer = dma;
    args.DmaSize = sizeof(dma);
    args.pDmaBufferPrivateData = private_data;
    args.DmaBufferPrivateDataSize = sizeof(private_data);
    args.Operation = DXGK_OPERATION_FILL;
    args.Fill.Destination.SegmentId = 1;
    args.Fill.Destination.SegmentAddress.QuadPart = 8193; // Unaligned is intentional.
    args.Fill.FillSize = 16;
    args.Fill.FillPattern = 0x04030201;
    auto memory = static_cast<UCHAR *>(segment_memory);
    RtlFillMemory(memory, 20000, 0xA5);
    const auto original = args;
    for (unsigned i = 0; i < 2; ++i) {
        args = original;
        if (i == 0) args.DmaSize = sizeof(PagingMarker) - 1;
        else args.DmaBufferPrivateDataSize = sizeof(PagingCommand) - 1;
        NR_CHECK(registration.DxgkDdiBuildPagingBuffer(adapter, &args) ==
                 STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER);
        NR_CHECK(args.pDmaBuffer == dma && args.pDmaBufferPrivateData == private_data);
        for (auto byte : dma) NR_CHECK(byte == 0);
        for (auto byte : private_data) NR_CHECK(byte == 0);
    }
    args = original;
    NR_CHECK(NT_SUCCESS(registration.DxgkDdiBuildPagingBuffer(adapter, &args)));
    NR_CHECK(args.pDmaBuffer == dma + sizeof(PagingMarker));
    NR_CHECK(args.pDmaBufferPrivateData == private_data + sizeof(PagingCommand));
    NR_CHECK(args.DmaSize == sizeof(dma) - sizeof(PagingMarker));
    NR_CHECK(args.DmaBufferPrivateDataSize == sizeof(private_data) - sizeof(PagingCommand));
    NR_CHECK(memory[8193] == 0xA5); // Build did not execute the fill.
    PagingCommand fill{};
    RtlCopyMemory(&fill, private_data, sizeof(fill));
    NR_CHECK(NT_SUCCESS(Paging::execute(adapter->segment, fill)));
    for (SIZE_T i = 0; i < 16; ++i) NR_CHECK(memory[8193 + i] == (i % 4) + 1);
    NR_CHECK(memory[8192] == 0xA5 && memory[8209] == 0xA5);
    for (unsigned i = 0; i < 4; ++i) {
        args = original;
        switch (i) {
        case 0: args.Fill.FillSize = 0; break;
        case 1: args.Fill.FillSize = 15; break;
        case 2: args.Fill.Destination.SegmentAddress.QuadPart = nr::kmd::Segment::bytes - 8; break;
        case 3: args.Fill.Destination.SegmentId = 0; break;
        }
        NR_CHECK(registration.DxgkDdiBuildPagingBuffer(adapter, &args) == STATUS_INVALID_PARAMETER);
        NR_CHECK(args.pDmaBuffer == dma && args.pDmaBufferPrivateData == private_data);
    }

    UCHAR system_memory[12288]{};
    for (SIZE_T i = 0; i < sizeof(system_memory); ++i) system_memory[i] = UCHAR(i % 251);
    MDL mdl{};
    mdl.StartVa = reinterpret_cast<void *>(0x100000);
    mdl.ByteOffset = 37;
    mdl.ByteCount = sizeof(system_memory);
    mdl.MappedSystemVa = system_memory;
    mdl.MdlFlags = MDL_SOURCE_IS_NONPAGED_POOL;
    const MDL borrowed = mdl;
    args = original;
    args.Operation = DXGK_OPERATION_TRANSFER;
    RtlZeroMemory(&args.Transfer, sizeof(args.Transfer));
    args.Transfer.TransferSize = 128;
    args.Transfer.TransferOffset = 19;
    args.Transfer.MdlOffset = 1;
    args.Transfer.Source.pMdl = &mdl;
    args.Transfer.Destination.SegmentId = 1;
    args.Transfer.Destination.SegmentAddress.QuadPart = 9000;
    NR_CHECK(NT_SUCCESS(registration.DxgkDdiBuildPagingBuffer(adapter, &args)));
    PagingCommand transfer{};
    RtlCopyMemory(&transfer, private_data, sizeof(transfer));
    NR_CHECK(transfer.source.offset == 4096 && transfer.destination.offset == 9019);
    NR_CHECK(memory[9019] == 0xA5);
    // Allocation and mapping failures preserve the destination and borrowed MDL.
    mdl_fail_after = 0;
    NR_CHECK(Paging::execute(adapter->segment, transfer) == STATUS_INSUFFICIENT_RESOURCES);
    NR_CHECK(!mdl_outstanding && memory[9019] == 0xA5);
    mdl_fail_after = -1;
    mdl_map_fail = true;
    NR_CHECK(Paging::execute(adapter->segment, transfer) == STATUS_INSUFFICIENT_RESOURCES);
    NR_CHECK(!mdl_outstanding && memory[9019] == 0xA5);
    mdl_map_fail = false;
    NR_CHECK(NT_SUCCESS(Paging::execute(adapter->segment, transfer)));
    for (SIZE_T i = 0; i < 128; ++i) NR_CHECK(memory[9019 + i] == system_memory[4096 + i]);
    NR_CHECK(memory[9018] == 0xA5 && memory[9147] == 0xA5);
    // Reverse direction: segment offset applies to source; MDL page offset to destination.
    args = original;
    args.Operation = DXGK_OPERATION_TRANSFER;
    RtlZeroMemory(&args.Transfer, sizeof(args.Transfer));
    args.Transfer.TransferSize = 128;
    args.Transfer.TransferOffset = 19;
    args.Transfer.MdlOffset = 2;
    args.Transfer.Source.SegmentId = 1;
    args.Transfer.Source.SegmentAddress.QuadPart = 9000;
    args.Transfer.Destination.pMdl = &mdl;
    NR_CHECK(NT_SUCCESS(registration.DxgkDdiBuildPagingBuffer(adapter, &args)));
    RtlCopyMemory(&transfer, private_data, sizeof(transfer));
    NR_CHECK(NT_SUCCESS(Paging::execute(adapter->segment, transfer)));
    for (SIZE_T i = 0; i < 128; ++i) NR_CHECK(system_memory[8192 + i] == system_memory[4096 + i]);
    NR_CHECK(RtlCompareMemory(&mdl, &borrowed, sizeof(mdl)) == sizeof(mdl));
    NR_CHECK(!mdl_outstanding && !mdl_contract_error);
    // Two MDL endpoints: failure on the second mapping allocation must release
    // the first partial MDL without modifying either borrowed buffer.
    auto two_mdls = transfer;
    two_mdls.source = {&mdl, 4096};
    two_mdls.destination = {&mdl, 64};
    UCHAR untouched[128]{};
    RtlCopyMemory(untouched, system_memory + 64, sizeof(untouched));
    mdl_fail_after = 1;
    NR_CHECK(Paging::execute(adapter->segment, two_mdls) == STATUS_INSUFFICIENT_RESOURCES);
    mdl_fail_after = -1;
    NR_CHECK(!mdl_outstanding && !mdl_contract_error);
    NR_CHECK(RtlCompareMemory(untouched, system_memory + 64, sizeof(untouched)) == sizeof(untouched));
    // End-of-MDL, end-of-segment and huge offsets must be rejected before publication.
    const auto valid_transfer = args;
    for (unsigned i = 0; i < 5; ++i) {
        args = valid_transfer;
        switch (i) {
        case 0: args.Transfer.MdlOffset = MAXULONG; break;
        case 1: args.Transfer.TransferSize = sizeof(system_memory); break;
        case 2: args.Transfer.Source.SegmentAddress.QuadPart = MAXLONGLONG; break;
        case 3: args.Transfer.Source.SegmentAddress.QuadPart = -1; break;
        case 4: args.Transfer.Source.SegmentId = 2; break;
        }
        auto dma_cursor = args.pDmaBuffer;
        auto private_cursor = args.pDmaBufferPrivateData;
        NR_CHECK(registration.DxgkDdiBuildPagingBuffer(adapter, &args) == STATUS_INVALID_PARAMETER);
        NR_CHECK(args.pDmaBuffer == dma_cursor && args.pDmaBufferPrivateData == private_cursor);
    }
    // Overlapping movement within the owned segment follows memmove semantics.
    transfer = {};
    transfer.operation = DXGK_OPERATION_TRANSFER;
    transfer.bytes = 100;
    transfer.source.offset = 9019;
    transfer.destination.offset = 9023;
    UCHAR expected[100]{};
    RtlCopyMemory(expected, memory + 9019, sizeof(expected));
    NR_CHECK(NT_SUCCESS(Paging::execute(adapter->segment, transfer)));
    NR_CHECK(RtlCompareMemory(expected, memory + 9023, sizeof(expected)) == sizeof(expected));
    args = original;
    args.Operation = DXGK_OPERATION_DISCARD_CONTENT;
    RtlZeroMemory(&args.DiscardContent, sizeof(args.DiscardContent));
    args.DiscardContent.SegmentId = 1;
    args.DiscardContent.SegmentAddress.QuadPart = 9019;
    NR_CHECK(NT_SUCCESS(registration.DxgkDdiBuildPagingBuffer(adapter, &args)));
    PagingCommand discard{};
    RtlCopyMemory(&discard, private_data, sizeof(discard));
    NR_CHECK(NT_SUCCESS(Paging::execute(adapter->segment, discard)));
    NR_CHECK(RtlCompareMemory(expected, memory + 9023, sizeof(expected)) == sizeof(expected));
    discard.bytes = 1;
    NR_CHECK(Paging::execute(adapter->segment, discard) == STATUS_INVALID_PARAMETER);
    return 0;
}
