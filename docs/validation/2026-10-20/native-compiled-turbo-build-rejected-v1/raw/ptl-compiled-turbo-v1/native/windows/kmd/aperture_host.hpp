// Explicit host-only page/mapping model. Real PFN/cache behavior needs a guest.
namespace {
bool aperture_contract_error = false, aperture_map_fail = false, aperture_mutex_held = false;
LONG aperture_mappings = 0;
void aperture_mutex_init(PFAST_MUTEX mutex) { RtlZeroMemory(mutex, sizeof(*mutex)); }
void aperture_mutex_acquire(PFAST_MUTEX) {
    if (aperture_mutex_held || simulated_irql != PASSIVE_LEVEL) aperture_contract_error = true;
    aperture_mutex_held = true; simulated_irql = APC_LEVEL;
}
void aperture_mutex_release(PFAST_MUTEX) {
    if (!aperture_mutex_held || simulated_irql != APC_LEVEL) aperture_contract_error = true;
    aperture_mutex_held = false; simulated_irql = PASSIVE_LEVEL;
}
void *aperture_map_pages(PMDL mdl, KPROCESSOR_MODE mode, MEMORY_CACHING_TYPE cache,
                        void *requested, ULONG bugcheck, ULONG priority) {
    if (mode != KernelMode || cache != MmCached || requested || bugcheck ||
        priority != (HighPagePriority | MdlMappingNoExecute) || simulated_irql != APC_LEVEL ||
        mdl->ByteOffset || !(mdl->MdlFlags & MDL_PAGES_LOCKED) || mdl->MappedSystemVa)
        aperture_contract_error = true;
    if (aperture_map_fail) return nullptr;
    auto data = static_cast<UCHAR *>(nr_host_allocate(mdl->ByteCount));
    if (!data) return nullptr;
    const auto pfns = MmGetMdlPfnArray(mdl);
    for (ULONG i = 0; i < mdl->ByteCount / PAGE_SIZE; ++i)
        RtlCopyMemory(data + SIZE_T(i) * PAGE_SIZE, reinterpret_cast<void *>(pfns[i] * PAGE_SIZE), PAGE_SIZE);
    mdl->MappedSystemVa = data; mdl->MdlFlags |= MDL_MAPPED_TO_SYSTEM_VA;
    ++aperture_mappings;
    return data;
}
void aperture_unmap_pages(void *data, PMDL mdl) {
    if (mdl->MappedSystemVa != data || simulated_irql != APC_LEVEL || !aperture_mappings)
        aperture_contract_error = true;
    const auto pfns = MmGetMdlPfnArray(mdl);
    for (ULONG i = 0; i < mdl->ByteCount / PAGE_SIZE; ++i)
        RtlCopyMemory(reinterpret_cast<void *>(pfns[i] * PAGE_SIZE), static_cast<UCHAR *>(data) + SIZE_T(i) * PAGE_SIZE, PAGE_SIZE);
    mdl->MappedSystemVa = nullptr; mdl->MdlFlags &= ~MDL_MAPPED_TO_SYSTEM_VA;
    --aperture_mappings; nr_host_free(data);
}
}
#undef ExInitializeFastMutex
#define ExInitializeFastMutex aperture_mutex_init
#define ExAcquireFastMutex aperture_mutex_acquire
#define ExReleaseFastMutex aperture_mutex_release
#define MmMapLockedPagesSpecifyCache aperture_map_pages
#define MmUnmapLockedPages aperture_unmap_pages
#include "aperture.hpp"
