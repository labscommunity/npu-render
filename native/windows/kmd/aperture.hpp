// Software-engine address translation over pages supplied by VidMm. This is
// not a hardware/MMIO aperture and does not give the NPU access to these pages.
// The caller must expose this only after live map/unmap/submission validation.
#pragma once
namespace nr::kmd {
class Aperture {
public:
    static constexpr SIZE_T bytes = 256 * 1024 * 1024;
    static constexpr SIZE_T pages = bytes / PAGE_SIZE;
    Aperture() = default;
    Aperture(const Aperture &) = delete;
    Aperture &operator=(const Aperture &) = delete;
    NTSTATUS prepare() {
        if (entries_) return STATUS_INVALID_DEVICE_STATE;
        if (KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
        auto entries = static_cast<Entry *>(ExAllocatePool2(POOL_FLAG_NON_PAGED, pages * sizeof(Entry), tag));
        if (!entries) return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(entries, pages * sizeof(Entry));
        ExInitializeFastMutex(&mutex_);
        entries_ = entries;
        return STATUS_SUCCESS;
    }
    // Adapter teardown must first join its engine and exclude paging callbacks.
    void release() {
        if (entries_) ExFreePoolWithTag(entries_, tag);
        entries_ = nullptr;
        mapped_pages_ = dummy_pages_ = 0;
    }
    bool ready() const { return entries_ != nullptr; }
    SIZE_T mapped_pages() const { return mapped_pages_; }
    SIZE_T dummy_pages() const { return dummy_pages_; }
    NTSTATUS map(HANDLE allocation, SIZE_T first, SIZE_T count, PMDL source, ULONG source_page, ULONG flags) {
        if (!ready()) return STATUS_DEVICE_NOT_READY;
        if (KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
        // Only cacheable, coherent system pages are admitted by this prototype.
        if (!allocation || !page_range(first, count) || !source || flags != 1 ||
            MmGetMdlByteOffset(source) || (MmGetMdlByteCount(source) % PAGE_SIZE) ||
            !(source->MdlFlags & (MDL_PAGES_LOCKED | MDL_SOURCE_IS_NONPAGED_POOL)) ||
            source_page > MmGetMdlByteCount(source) / PAGE_SIZE ||
            count > MmGetMdlByteCount(source) / PAGE_SIZE - source_page)
            return STATUS_INVALID_PARAMETER;
        Lock lock(mutex_);
        const auto pfns = MmGetMdlPfnArray(source) + source_page;
        for (SIZE_T i = 0; i < count; ++i)
            if (entries_[first + i].owner || !valid_pfn(pfns[i])) return STATUS_INVALID_PARAMETER;
        // Copy PFN values while the OS-owned descriptor is valid. Neither the
        // original MDL pointer nor its optional system mapping is retained.
        for (SIZE_T i = 0; i < count; ++i) {
            auto &entry = entries_[first + i];
            if (entry.valid) --dummy_pages_;
            entry = {pfns[i], allocation, true};
        }
        mapped_pages_ += count;
        KeMemoryBarrier();
        return STATUS_SUCCESS;
    }
    NTSTATUS unmap(HANDLE allocation, SIZE_T first, SIZE_T count, PHYSICAL_ADDRESS dummy) {
        if (!ready()) return STATUS_DEVICE_NOT_READY;
        if (KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
        if (!allocation || !page_range(first, count) || dummy.QuadPart < 0 ||
            (dummy.QuadPart % PAGE_SIZE) || !valid_pfn(PFN_NUMBER(dummy.QuadPart / PAGE_SIZE)))
            return STATUS_INVALID_PARAMETER;
        Lock lock(mutex_);
        for (SIZE_T i = 0; i < count; ++i)
            if (entries_[first + i].owner != allocation) return STATUS_INVALID_PARAMETER;
        // The OS-provided dummy page remains accessible at every released
        // address. It is never replaced with an invented address or zero data.
        for (SIZE_T i = 0; i < count; ++i)
            entries_[first + i] = {PFN_NUMBER(dummy.QuadPart / PAGE_SIZE), nullptr, true};
        mapped_pages_ -= count; dummy_pages_ += count;
        KeMemoryBarrier();
        return STATUS_SUCCESS;
    }
    NTSTATUS read(ULONGLONG offset, void *destination, SIZE_T length) {
        return transfer(offset, destination, length, false);
    }
    NTSTATUS write(ULONGLONG offset, const void *source, SIZE_T length) {
        return transfer(offset, const_cast<void *>(source), length, true);
    }
    NTSTATUS fill(ULONGLONG offset, SIZE_T length, ULONG pattern) {
        if (!ready()) return STATUS_DEVICE_NOT_READY;
        if (KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
        if (!byte_range(offset, length) || length % sizeof(pattern)) return STATUS_INVALID_PARAMETER;
        Lock lock(mutex_);
        Mapping target;
        const auto status = target.prepare(entries_, offset, length);
        if (status != STATUS_SUCCESS) return status;
        for (SIZE_T i = 0; i < length; i += sizeof(pattern))
            RtlCopyMemory(target.data + i, &pattern, sizeof(pattern));
        KeMemoryBarrier();
        return STATUS_SUCCESS;
    }
private:
    static constexpr ULONG tag = 'aRpN';
    struct Entry { PFN_NUMBER pfn; HANDLE owner; bool valid; };
    static bool valid_pfn(PFN_NUMBER pfn) { return pfn && pfn <= PFN_NUMBER(MAXLONGLONG / PAGE_SIZE); }
    static bool page_range(SIZE_T first, SIZE_T count) { return count && first < pages && count <= pages - first; }
    static bool byte_range(ULONGLONG offset, SIZE_T length) { return length && offset < bytes && length <= bytes - offset; }
    struct Lock {
        FAST_MUTEX &mutex;
        explicit Lock(FAST_MUTEX &m) : mutex(m) { ExAcquireFastMutex(&mutex); }
        ~Lock() { ExReleaseFastMutex(&mutex); }
        Lock(const Lock &) = delete;
    };
    struct Mapping {
        PMDL descriptor = nullptr;
        UCHAR *base = nullptr, *data = nullptr;
        Mapping() = default;
        Mapping(const Mapping &) = delete;
        ~Mapping() {
            if (base) MmUnmapLockedPages(base, descriptor);
            if (descriptor) {
                // VidMm owns the pins. We own only this descriptor and mapping;
                // calling MmUnlockPages would release somebody else's pins.
                descriptor->MdlFlags &= ~MDL_PAGES_LOCKED;
                IoFreeMdl(descriptor);
            }
        }
        NTSTATUS prepare(const Entry *entries, ULONGLONG offset, SIZE_T length) {
            const auto first = SIZE_T(offset / PAGE_SIZE), within = SIZE_T(offset % PAGE_SIZE);
            const auto count = (within + length + PAGE_SIZE - 1) / PAGE_SIZE;
            for (SIZE_T i = 0; i < count; ++i) if (!entries[first + i].valid) return STATUS_INVALID_ADDRESS;
            descriptor = IoAllocateMdl(nullptr, ULONG(count * PAGE_SIZE), FALSE, FALSE, nullptr);
            if (!descriptor) return STATUS_INSUFFICIENT_RESOURCES;
            auto pfns = MmGetMdlPfnArray(descriptor);
            for (SIZE_T i = 0; i < count; ++i) pfns[i] = entries[first + i].pfn;
            // These page numbers come only from VidMm's locked MDL or its
            // documented dummy page. No user command can provide a page number.
            descriptor->MdlFlags |= MDL_PAGES_LOCKED;
            base = static_cast<UCHAR *>(MmMapLockedPagesSpecifyCache(descriptor, KernelMode,
                MmCached, nullptr, FALSE, HighPagePriority | MdlMappingNoExecute));
            if (!base) return STATUS_INSUFFICIENT_RESOURCES;
            data = base + within;
            return STATUS_SUCCESS;
        }
    };
    NTSTATUS transfer(ULONGLONG offset, void *host, SIZE_T length, bool writing) {
        if (!ready()) return STATUS_DEVICE_NOT_READY;
        if (KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
        if (!host || !byte_range(offset, length)) return STATUS_INVALID_PARAMETER;
        Lock lock(mutex_);
        Mapping mapping;
        const auto status = mapping.prepare(entries_, offset, length);
        if (status != STATUS_SUCCESS) return status;
        if (writing) RtlCopyMemory(mapping.data, host, length);
        else RtlCopyMemory(host, mapping.data, length);
        KeMemoryBarrier();
        return STATUS_SUCCESS;
    }
    Entry *entries_ = nullptr;
    FAST_MUTEX mutex_{};
    SIZE_T mapped_pages_ = 0, dummy_pages_ = 0;
};
} // namespace nr::kmd
