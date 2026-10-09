// Offscreen public-KMT presentation probe. This verifies copied pixels, not
// delivery to a monitor, DWM selection, cross-adapter sharing or NPU execution.
struct PresentTexture {
    D3DKMT_HANDLE device = 0, allocation = 0;
    bool locked = false;
    nr::kernel_contract::Surface surface{};
    explicit PresentTexture(D3DKMT_HANDLE owner) : device(owner) {}
    PresentTexture(const PresentTexture &) = delete;
    bool unlock() {
        if (!locked) return true;
        D3DKMT_UNLOCK a{}; a.hDevice = device; a.NumAllocations = 1; a.phAllocations = &allocation;
        if (!record("present_unlock", D3DKMTUnlock(&a))) return false;
        locked = false; return true;
    }
    bool close() {
        if (!unlock()) return false;
        if (!allocation) return true;
        D3DKMT_DESTROYALLOCATION a{}; a.hDevice = device; a.AllocationCount = 1; a.phAllocationList = &allocation;
        if (!record("present_destroy_texture", D3DKMTDestroyAllocation(&a))) return false;
        allocation = 0; return true;
    }
    ~PresentTexture() { close(); }
    bool create(UINT format, UINT pitch) {
        surface.width = 65; surface.height = 13; surface.format = format; surface.row_pitch = pitch;
        surface.allocation_bytes = (uint64_t(pitch) * surface.height + 4095) & ~4095ull;
        D3DDDI_ALLOCATIONINFO info{}; info.pPrivateDriverData = &surface; info.PrivateDriverDataSize = sizeof(surface);
        D3DKMT_CREATEALLOCATION a{}; a.hDevice = device; a.NumAllocations = 1; a.pAllocationInfo = &info;
        if (!record("present_create_texture", D3DKMTCreateAllocation(&a))) return false;
        allocation = info.hAllocation; return allocation != 0;
    }
    unsigned char *map(bool read) {
        D3DKMT_LOCK a{}; a.hDevice = device; a.hAllocation = allocation;
        a.Flags.LockEntire = a.Flags.DonotEvict = 1; a.Flags.ReadOnly = read; a.Flags.WriteOnly = !read;
        if (!record(read ? "present_lock_read" : "present_lock_write", D3DKMTLock(&a))) return nullptr;
        locked = true; return static_cast<unsigned char *>(a.pData);
    }
};
bool present_blits(D3DKMT_HANDLE adapter) {
    Allocation owner;
    if (!owner.open_device(adapter)) return false;
    D3DKMT_CREATECONTEXT context{};
    if (!owner.open_context(context)) return false;
    unsigned checks = 0;
    for (UINT format : {87u, 28u}) {
        PresentTexture source(owner.device), destination(owner.device);
        if (!source.create(format, 512) || !destination.create(format, 768)) return false;
        auto bytes = source.map(false); if (!bytes) return false;
        for (size_t i = 0; i < source.surface.allocation_bytes; ++i) bytes[i] = static_cast<unsigned char>(i * 17 + i / 53);
        if (!source.unlock()) return false;
        for (UINT pass = 0; pass < 2; ++pass) {
            bytes = destination.map(false); if (!bytes) return false;
            std::memset(bytes, 0xc3, size_t(destination.surface.allocation_bytes));
            if (!destination.unlock()) return false;
            D3DKMT_PRESENT present{};
            present.hContext = owner.context; present.hSource = source.allocation; present.hDestination = destination.allocation;
            present.Flags.Blt = present.Flags.SrcRectValid = present.Flags.DstRectValid = 1;
            present.FlipInterval = D3DDDI_FLIPINTERVAL_IMMEDIATE;
            present.SrcRect = {1, 1, 61, 12}; present.DstRect = {3, 2, 63, 13};
            // Public KMT accepts subrectangles in source coordinates; the KMD
            // receives destination coordinates after Windows translates them.
            const RECT rectangles[] = {{1, 1, 21, 6}, {28, 7, 61, 12}};
            if (pass) { present.SubRectCnt = 2; present.pSrcSubRects = rectangles; }
            if (!record("present_blit", D3DKMTPresent(&present)) || !owner.completion_event(pass)) return false;
            bytes = destination.map(true); if (!bytes) return false;
            for (size_t i = 0; i < destination.surface.allocation_bytes; ++i) {
                const auto y = i / 768, x = i % 768 / 4, channel = i % 4;
                const bool copied = pass ? ((x >= 3 && x < 23 && y >= 2 && y < 7) || (x >= 30 && x < 63 && y >= 8 && y < 13)) :
                                           x >= 3 && x < 63 && y >= 2 && y < 13;
                unsigned char expected = 0xc3;
                if (copied) { const auto index = (y - 1) * 512 + (x - 2) * 4 + channel; expected = static_cast<unsigned char>(index * 17 + index / 53); }
                if (bytes[i] != expected) {
                    std::printf("{\"present_mismatch_offset\":%zu,\"actual\":%u,\"expected\":%u}\n", i, UINT(bytes[i]), UINT(expected));
                    return false;
                }
            }
            if (!destination.unlock()) return false;
            ++checks;
            std::printf("{\"present_format\":%u,\"pass\":%u,\"verified_bytes\":%llu,\"source_pitch\":512,\"destination_pitch\":768}\n",
                        format, pass, destination.surface.allocation_bytes);
        }
        if (!destination.close() || !source.close()) return false;
    }
    return checks == 4 && owner.close();
}
