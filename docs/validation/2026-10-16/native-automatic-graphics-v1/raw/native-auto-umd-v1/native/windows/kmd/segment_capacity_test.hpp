int segment_capacity_test(Adapter *adapter) {
    const SIZE_T invalid[] = {0, 1, PAGE_SIZE + 1, nr::kmd::Segment::bytes + PAGE_SIZE, ~SIZE_T(0)};
    const auto before = segment_allocations;
    for (const auto requested : invalid) {
        NR_CHECK(adapter->segment.prepare(requested) == STATUS_INVALID_PARAMETER);
        NR_CHECK(!adapter->segment.capacity() && segment_allocations == before);
    }
    const SIZE_T sizes[] = {PAGE_SIZE, 3 * PAGE_SIZE, 65536};
    for (const auto requested : sizes) {
        NR_CHECK(adapter->segment.prepare(requested) == STATUS_SUCCESS);
        NR_CHECK(adapter->segment.capacity() == requested);
        NR_CHECK(adapter->segment.range(0, requested) == segment_memory);
        NR_CHECK(!adapter->segment.range(requested, 1));
        NR_CHECK(!adapter->segment.range(1, requested));
        NR_CHECK(!adapter->segment.range(0, requested + 1));
        for (SIZE_T i = 0; i < requested; ++i) NR_CHECK(static_cast<UCHAR *>(segment_memory)[i] == 0);
        DXGK_SEGMENTDESCRIPTOR3 descriptor{};
        DXGK_QUERYSEGMENTOUT3 out{};
        out.NbSegment = 1;
        out.pSegmentDescriptor = &descriptor;
        NR_CHECK(adapter->segment.describe(out) == STATUS_SUCCESS);
        NR_CHECK(descriptor.Size == requested && descriptor.CommitLimit == requested);
        NR_CHECK(descriptor.CpuTranslatedAddress.QuadPart == segment_physical);
        adapter->segment.release();
        NR_CHECK(!segment_memory && !adapter->segment.capacity());
    }
    NR_CHECK(adapter->segment.prepare(65536) == STATUS_SUCCESS);
    adapter->started = true; // Test model only, never an OS startup override.
    nr::kernel_contract::Surface surface{};
    surface.width = 64; surface.height = 512; surface.format = 87;
    surface.row_pitch = 256; surface.allocation_bytes = 131072;
    DXGK_ALLOCATIONINFO allocation{};
    allocation.pPrivateDriverData = &surface;
    allocation.PrivateDriverDataSize = sizeof(surface);
    DXGKARG_CREATEALLOCATION args{};
    args.NumAllocations = 1;
    args.pAllocationInfo = &allocation;
    const auto baseline = outstanding;
    NR_CHECK(registration.DxgkDdiCreateAllocation(adapter, &args) == STATUS_INSUFFICIENT_RESOURCES);
    NR_CHECK(!allocation.hAllocation && !adapter->allocations && outstanding == baseline);
    NR_CHECK(registration.DxgkDdiStopDevice(adapter) == STATUS_SUCCESS);
    NR_CHECK(!segment_memory && !adapter->segment.capacity() && !adapter->started);
    return 0;
}
