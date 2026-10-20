int aperture_test() {
    nr::kmd::Aperture aperture;
    UCHAR output[32]{};
    NR_CHECK(aperture.read(0, output, sizeof(output)) == STATUS_DEVICE_NOT_READY);
    const auto baseline = outstanding, baseline_mdls = mdl_outstanding;
    fail_after = 0;
    NR_CHECK(aperture.prepare() == STATUS_INSUFFICIENT_RESOURCES && !aperture.ready());
    fail_after = -1;
    NR_CHECK(aperture.prepare() == STATUS_SUCCESS && aperture.ready());
    NR_CHECK(aperture.prepare() == STATUS_INVALID_DEVICE_STATE);
    // Host-only PFNs point at aligned host memory; no real physical addresses.
    auto raw = static_cast<UCHAR *>(nr_host_allocate(8 * PAGE_SIZE));
    NR_CHECK(raw);
    auto data = reinterpret_cast<UCHAR *>((ULONG_PTR(raw) + PAGE_SIZE - 1) & ~ULONG_PTR(PAGE_SIZE - 1));
    for (SIZE_T i = 0; i < 6 * PAGE_SIZE; ++i) data[i] = UCHAR(i * 17 + i / PAGE_SIZE);
    auto source = mdl_allocate(data, 4 * PAGE_SIZE, FALSE, FALSE, nullptr);
    NR_CHECK(source);
    source->MdlFlags = MDL_PAGES_LOCKED;
    auto pfns = MmGetMdlPfnArray(source);
    for (SIZE_T i = 0; i < 4; ++i) pfns[i] = ULONG_PTR(data + i * PAGE_SIZE) / PAGE_SIZE;
    const HANDLE owner = reinterpret_cast<HANDLE>(ULONG_PTR(0x123)), other = reinterpret_cast<HANDLE>(ULONG_PTR(0x456));
    for (UINT failure = 0; failure < 8; ++failure) {
        SIZE_T first = 0, count = 3; ULONG offset = 1, flags = 1;
        PMDL input = source; HANDLE identity = owner;
        switch (failure) {
        case 0: first = nr::kmd::Aperture::pages; break;
        case 1: count = 0; break;
        case 2: count = ~SIZE_T(0); break;
        case 3: offset = 2; break;
        case 4: flags = 0; break;
        case 5: flags = 3; break;
        case 6: input = nullptr; break;
        case 7: identity = nullptr; break;
        }
        NR_CHECK(aperture.map(identity, first, count, input, offset, flags) == STATUS_INVALID_PARAMETER);
        NR_CHECK(!aperture.mapped_pages() && !aperture.dummy_pages());
    }
    const MDL original = *source;
    NR_CHECK(aperture.map(owner, 0, 3, source, 1, 1) == STATUS_SUCCESS);
    NR_CHECK(aperture.mapped_pages() == 3 && RtlCompareMemory(source, &original, sizeof(original)) == sizeof(original));
    NR_CHECK(aperture.map(other, 2, 1, source, 0, 1) == STATUS_INVALID_PARAMETER);
    NR_CHECK(aperture.read(PAGE_SIZE - 13, output, sizeof(output)) == STATUS_SUCCESS);
    NR_CHECK(RtlCompareMemory(output, data + 2 * PAGE_SIZE - 13, sizeof(output)) == sizeof(output));
    // The source descriptor can go away after its page identities are copied.
    source->MdlFlags = 0; mdl_free(source); source = nullptr;
    NR_CHECK(aperture.fill(7, 4 * sizeof(ULONG), 0x5a5a5a5a) == STATUS_SUCCESS);
    for (SIZE_T i = 0; i < 16; ++i) NR_CHECK(data[PAGE_SIZE + 7 + i] == 0x5a);
    RtlFillMemory(output, sizeof(output), 0x37);
    NR_CHECK(aperture.read(3 * PAGE_SIZE - 8, output, sizeof(output)) == STATUS_INVALID_ADDRESS);
    for (const auto value : output) NR_CHECK(value == 0x37);
    NR_CHECK(aperture.write(~ULONGLONG(0), output, sizeof(output)) == STATUS_INVALID_PARAMETER);
    NR_CHECK(aperture.fill(0, 3, 0) == STATUS_INVALID_PARAMETER);
    mdl_fail_after = 0;
    NR_CHECK(aperture.read(0, output, sizeof(output)) == STATUS_INSUFFICIENT_RESOURCES);
    mdl_fail_after = -1; aperture_map_fail = true;
    NR_CHECK(aperture.write(0, output, sizeof(output)) == STATUS_INSUFFICIENT_RESOURCES);
    aperture_map_fail = false;
    NR_CHECK(!aperture_mappings && mdl_outstanding == baseline_mdls);
    PHYSICAL_ADDRESS dummy{}; dummy.QuadPart = LONGLONG(ULONG_PTR(data + 5 * PAGE_SIZE));
    RtlFillMemory(data + 5 * PAGE_SIZE, PAGE_SIZE, 0x6c);
    NR_CHECK(aperture.unmap(other, 0, 3, dummy) == STATUS_INVALID_PARAMETER);
    NR_CHECK(aperture.unmap(owner, 1, 1, dummy) == STATUS_SUCCESS);
    NR_CHECK(aperture.mapped_pages() == 2 && aperture.dummy_pages() == 1);
    NR_CHECK(aperture.read(PAGE_SIZE, output, sizeof(output)) == STATUS_SUCCESS);
    for (const auto value : output) NR_CHECK(value == 0x6c);
    NR_CHECK(aperture.fill(PAGE_SIZE, sizeof(ULONG), 0x33333333) == STATUS_SUCCESS);
    for (SIZE_T i = 0; i < sizeof(ULONG); ++i) NR_CHECK(data[5 * PAGE_SIZE + i] == 0x33);
    source = mdl_allocate(data, PAGE_SIZE, FALSE, FALSE, nullptr); NR_CHECK(source);
    source->MdlFlags = MDL_PAGES_LOCKED; MmGetMdlPfnArray(source)[0] = ULONG_PTR(data) / PAGE_SIZE;
    NR_CHECK(aperture.map(other, 1, 1, source, 0, 1) == STATUS_SUCCESS);
    source->MdlFlags = 0; mdl_free(source);
    NR_CHECK(aperture.mapped_pages() == 3 && !aperture.dummy_pages());
    NR_CHECK(aperture.unmap(owner, 0, 3, dummy) == STATUS_INVALID_PARAMETER);
    NR_CHECK(aperture.mapped_pages() == 3 && !aperture.dummy_pages());
    NR_CHECK(aperture.unmap(owner, 0, 1, dummy) == STATUS_SUCCESS);
    NR_CHECK(aperture.unmap(other, 1, 1, dummy) == STATUS_SUCCESS);
    NR_CHECK(aperture.unmap(owner, 2, 1, dummy) == STATUS_SUCCESS);
    NR_CHECK(!aperture.mapped_pages() && aperture.dummy_pages() == 3);
    simulated_irql = DISPATCH_LEVEL;
    NR_CHECK(aperture.read(0, output, sizeof(output)) == STATUS_INVALID_DEVICE_STATE);
    simulated_irql = PASSIVE_LEVEL;
    aperture.release(); aperture.release(); nr_host_free(raw);
    NR_CHECK(!aperture.ready() && outstanding == baseline && mdl_outstanding == baseline_mdls);
    NR_CHECK(!aperture_contract_error && !aperture_mutex_held && !aperture_mappings);
    return 0;
}
