// Executes the actual DDI implementation with pool/registration shims. This
// tests validation and unwinding only; it is not a kernel/IRQL or Dxgkrnl test.
extern "C" {
#include <ntddk.h>
#include <dispmprt.h>
void *nr_host_allocate(size_t bytes);
void nr_host_free(void *pointer);
}
namespace {
DRIVER_INITIALIZATION_DATA registration{};
long fail_after = -1, outstanding = 0;
void *pool_allocate(POOL_FLAGS, SIZE_T bytes, ULONG) {
    if (fail_after == 0) return nullptr;
    if (fail_after > 0) --fail_after;
    auto p = nr_host_allocate(bytes);
    if (p) ++outstanding;
    return p;
}
void pool_free(void *p, ULONG) { if (p) { --outstanding; nr_host_free(p); } }
NTSTATUS initialize(DRIVER_OBJECT *, UNICODE_STRING *, DRIVER_INITIALIZATION_DATA *data) {
    registration = *data;
    return STATUS_SUCCESS;
}
}
#define ExAllocatePool2 pool_allocate
#define ExFreePoolWithTag pool_free
#define DxgkInitialize initialize
#define DriverEntry NrHostDriverEntry
#include "driver.cpp"
#undef DriverEntry
#define NR_CHECK(expression) do { if (!(expression)) return __LINE__; } while (false)
extern "C" int nr_kernel_host_test() {
    NR_CHECK(NT_SUCCESS(NrHostDriverEntry(nullptr, nullptr)));
    NR_CHECK(registration.Version == DXGKDDI_INTERFACE_VERSION_WDDM1_3);
    NR_CHECK(!registration.DxgkDdiSetVidPnSourceAddress && !registration.DxgkDdiCommitVidPn);
    NR_CHECK(registration.DxgkDdiSubmitCommand(nullptr, nullptr) == STATUS_NOT_SUPPORTED);
    NR_CHECK(registration.DxgkDdiPresent(nullptr, nullptr) == STATUS_NOT_SUPPORTED);
    NR_CHECK(registration.DxgkDdiPreemptCommand(nullptr, nullptr) == STATUS_NOT_SUPPORTED);
    DEVICE_OBJECT physical{};
    void *opaque = reinterpret_cast<void *>(1);
    fail_after = 0;
    NR_CHECK(registration.DxgkDdiAddDevice(&physical, &opaque) == STATUS_INSUFFICIENT_RESOURCES);
    NR_CHECK(!opaque && !outstanding);
    fail_after = -1;
    NR_CHECK(NT_SUCCESS(registration.DxgkDdiAddDevice(&physical, &opaque)));
    auto a = static_cast<Adapter *>(opaque);
    ULONG sources = 999, children = 999;
    DXGK_START_INFO start{};
    DXGKRNL_INTERFACE kernel{};
    NR_CHECK(registration.DxgkDdiStartDevice(a, &start, &kernel, &sources, &children) ==
             STATUS_NOT_SUPPORTED);
    NR_CHECK(!sources && !children && !a->started && !a->scheduler.running());
    DXGKARG_CREATEDEVICE device{};
    NR_CHECK(registration.DxgkDdiCreateDevice(a, &device) == STATUS_DEVICE_NOT_READY);
    nr::kernel_contract::AdapterInfo info{};
    info.ready = 99;
    DXGKARG_QUERYADAPTERINFO query{};
    query.Type = DXGKQAITYPE_UMDRIVERPRIVATE;
    query.pOutputData = &info;
    query.OutputDataSize = sizeof(info) - 1;
    NR_CHECK(registration.DxgkDdiQueryAdapterInfo(a, &query) == STATUS_INVALID_PARAMETER);
    NR_CHECK(info.ready == 99);
    query.OutputDataSize = sizeof(info);
    NR_CHECK(NT_SUCCESS(registration.DxgkDdiQueryAdapterInfo(a, &query)) && !info.ready);
    // Exercise the dormant allocation implementation without enabling StartDevice.
    // This is a local model state, never a live adapter registration.
    a->started = true;
    nr::kernel_contract::Surface surface{};
    surface.width = 64; surface.height = 48; surface.format = 87;
    surface.row_pitch = 256; surface.allocation_bytes = 12288;
    DXGK_ALLOCATIONINFO allocations[3]{};
    for (auto &allocation : allocations) {
        allocation.pPrivateDriverData = &surface;
        allocation.PrivateDriverDataSize = sizeof(surface);
    }
    DXGKARG_CREATEALLOCATION create{};
    create.NumAllocations = 3;
    create.pAllocationInfo = allocations;
    // Invalid tail descriptor must leave all output handles untouched.
    allocations[2].PrivateDriverDataSize--;
    NR_CHECK(registration.DxgkDdiCreateAllocation(a, &create) == STATUS_INVALID_PARAMETER);
    NR_CHECK(outstanding == 1 && !a->allocations && !allocations[0].hAllocation);
    allocations[2].PrivateDriverDataSize++;
    for (long failure = 0; failure < 3; ++failure) {
        fail_after = failure;
        NR_CHECK(registration.DxgkDdiCreateAllocation(a, &create) == STATUS_INSUFFICIENT_RESOURCES);
        NR_CHECK(outstanding == 1 && !a->allocations);
        for (auto &allocation : allocations) NR_CHECK(!allocation.hAllocation);
    }
    fail_after = -1;
    const auto valid_surface = surface;
    for (unsigned mutation = 0; mutation < 9; ++mutation) {
        surface = valid_surface;
        switch (mutation) {
        case 0: surface.width = 0; break;
        case 1: surface.width = 0xffffffff; break;
        case 2: surface.height = 0xffffffff; break;
        case 3: surface.row_pitch = 0xffffff00; break;
        case 4: surface.row_pitch = 255; break;
        case 5: surface.allocation_bytes = ~nr::kernel_contract::u64(0); break;
        case 6: surface.format = 0; break;
        case 7: surface.reserved = 1; break;
        case 8: surface.abi = 0; break;
        }
        NR_CHECK(registration.DxgkDdiCreateAllocation(a, &create) == STATUS_INVALID_PARAMETER);
        NR_CHECK(outstanding == 1 && !a->allocations && !allocations[0].hAllocation);
    }
    surface = valid_surface;
    NR_CHECK(NT_SUCCESS(registration.DxgkDdiCreateAllocation(a, &create)));
    NR_CHECK(outstanding == 4 && a->allocations == 3);
    HANDLE handles[3]{};
    for (unsigned i = 0; i < 3; ++i) {
        handles[i] = allocations[i].hAllocation;
        NR_CHECK(handles[i] && allocations[i].Size == 12288);
    }
    NR_CHECK(registration.DxgkDdiStopDevice(a) == STATUS_DEVICE_BUSY);
    DXGKARG_DESTROYALLOCATION destroy{};
    destroy.NumAllocations = 3; destroy.pAllocationList = handles;
    handles[2] = handles[1];
    NR_CHECK(registration.DxgkDdiDestroyAllocation(a, &destroy) == STATUS_INVALID_PARAMETER);
    NR_CHECK(outstanding == 4 && a->allocations == 3);
    handles[2] = allocations[2].hAllocation;
    NR_CHECK(NT_SUCCESS(registration.DxgkDdiDestroyAllocation(a, &destroy)));
    NR_CHECK(outstanding == 1 && !a->allocations);
    NR_CHECK(NT_SUCCESS(registration.DxgkDdiStopDevice(a)));
    NR_CHECK(NT_SUCCESS(registration.DxgkDdiRemoveDevice(a)) && !outstanding);
    return 0;
}
