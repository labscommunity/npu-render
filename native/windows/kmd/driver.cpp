// Original WDDM miniport bring-up. This target is deliberately not included in
// the desktop bundle. StartDevice remains closed until the memory manager,
// scheduler notifications, and worker teardown have passed isolated tests.
extern "C" {
#include <ntddk.h>
#include <dispmprt.h>
}
#include "scheduler.hpp"
#include "contract.hpp"

inline void *operator new(size_t, void *p) noexcept { return p; }

namespace {
constexpr ULONG pool_tag = 'rRpN';
constexpr bool runtime_ready = false;
struct Adapter {
    const DEVICE_OBJECT *physical;
    DXGKRNL_INTERFACE kernel;
    nr::kmd::Scheduler scheduler;
    LONG devices;
    LONG allocations;
    bool started;
};
struct Allocation { Adapter *adapter; nr::kernel_contract::Surface surface; };
struct Device { Adapter *adapter; HANDLE runtime; LONG contexts; };
struct Context { Device *device; HANDLE runtime; };

template <class T> T *allocate() {
    auto p = static_cast<T *>(ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(T), pool_tag));
    return p ? new (p) T{} : nullptr;
}
void release(void *p) { if (p) ExFreePoolWithTag(p, pool_tag); }
// Unsupported functions always return a failing NTSTATUS. In particular,
// present, render, paging, preemption and reset never report fake completion.
template <class T> struct Reject;
template <class... A> struct Reject<NTSTATUS(APIENTRY *)(A...)> {
    static NTSTATUS APIENTRY call(A...) { return STATUS_NOT_SUPPORTED; }
};

DXGKDDI_ADD_DEVICE add_device;
NTSTATUS add_device(DEVICE_OBJECT *physical, void **context) {
    if (!physical || !context) return STATUS_INVALID_PARAMETER;
    *context = nullptr;
    auto a = allocate<Adapter>();
    if (!a) return STATUS_INSUFFICIENT_RESOURCES;
    a->physical = physical;
    *context = a;
    return STATUS_SUCCESS;
}
DXGKDDI_START_DEVICE start_device;
NTSTATUS start_device(void *context, DXGK_START_INFO *start,
                      DXGKRNL_INTERFACE *kernel, ULONG *sources, ULONG *children) {
    if (!context || !start || !kernel || !sources || !children)
        return STATUS_INVALID_PARAMETER;
    *sources = 0;
    *children = 0;
    auto a = static_cast<Adapter *>(context);
    if (a->started) return STATUS_INVALID_DEVICE_STATE;
    // Do not let Dxgkrnl select an adapter with incomplete mandatory DDIs.
    if (!runtime_ready) return STATUS_NOT_SUPPORTED;
    a->kernel = *kernel;
    if (a->scheduler.start() != nr::kmd::QueueResult::ok)
        return STATUS_INVALID_DEVICE_STATE;
    a->started = true;
    return STATUS_SUCCESS;
}
DXGKDDI_STOP_DEVICE stop_device;
NTSTATUS stop_device(void *context) {
    if (!context) return STATUS_INVALID_PARAMETER;
    auto a = static_cast<Adapter *>(context);
    if (a->devices || a->allocations || a->scheduler.active()) return STATUS_DEVICE_BUSY;
    a->scheduler.stop();
    a->started = false;
    RtlZeroMemory(&a->kernel, sizeof(a->kernel));
    return STATUS_SUCCESS;
}
DXGKDDI_REMOVE_DEVICE remove_device;
NTSTATUS remove_device(void *context) {
    if (!context) return STATUS_INVALID_PARAMETER;
    auto a = static_cast<Adapter *>(context);
    if (a->started || a->devices || a->allocations || a->scheduler.active()) return STATUS_DEVICE_BUSY;
    release(a);
    return STATUS_SUCCESS;
}
DXGKDDI_QUERY_CHILD_RELATIONS child_relations;
NTSTATUS child_relations(void *, DXGK_CHILD_DESCRIPTOR *children, ULONG bytes) {
    if (!children || bytes < sizeof(*children)) return STATUS_BUFFER_TOO_SMALL;
    RtlZeroMemory(children, bytes);
    return STATUS_SUCCESS;
}
DXGKDDI_INTERRUPT_ROUTINE interrupt;
BOOLEAN interrupt(void *, ULONG) { return FALSE; }
DXGKDDI_DPC_ROUTINE dpc;
void dpc(void *) {} // No work can be submitted while runtime_ready is false.
DXGKDDI_RESET_DEVICE reset_device;
void reset_device(void *context) {
    if (context) {
        auto a = static_cast<Adapter *>(context);
        a->scheduler.stop();
    }
}
DXGKDDI_UNLOAD unload;
void unload() {}
DXGKDDI_CONTROL_ETW_LOGGING control_etw;
void control_etw(BOOLEAN, ULONG, UCHAR) {}
DXGKDDI_QUERYADAPTERINFO query_adapter;
NTSTATUS APIENTRY query_adapter(HANDLE adapter, const DXGKARG_QUERYADAPTERINFO *args) {
    if (!adapter || !args || !args->pOutputData) return STATUS_INVALID_PARAMETER;
    if (args->Type != DXGKQAITYPE_UMDRIVERPRIVATE) return STATUS_NOT_SUPPORTED;
    if (args->InputDataSize || args->OutputDataSize != sizeof(nr::kernel_contract::AdapterInfo))
        return STATUS_INVALID_PARAMETER;
    nr::kernel_contract::AdapterInfo info{};
    info.ready = runtime_ready ? 1 : 0;
    RtlCopyMemory(args->pOutputData, &info, sizeof(info));
    return STATUS_SUCCESS;
}
DXGKDDI_CREATEALLOCATION create_allocation;
NTSTATUS APIENTRY create_allocation(HANDLE adapter, DXGKARG_CREATEALLOCATION *args) {
    if (!adapter || !args || !args->pAllocationInfo || !args->NumAllocations ||
        args->NumAllocations > 16 || args->Flags.Value || args->hResource ||
        args->PrivateDriverDataSize) return STATUS_INVALID_PARAMETER;
    auto a = static_cast<Adapter *>(adapter);
    if (!a->started) return STATUS_DEVICE_NOT_READY;
    nr::kernel_contract::Surface descriptions[16]{};
    // Validate and snapshot every descriptor before publishing any handle.
    for (UINT i = 0; i < args->NumAllocations; ++i) {
        const auto &in = args->pAllocationInfo[i];
        if (!in.pPrivateDriverData || in.PrivateDriverDataSize != sizeof(descriptions[i]))
            return STATUS_INVALID_PARAMETER;
        RtlCopyMemory(&descriptions[i], in.pPrivateDriverData, sizeof(descriptions[i]));
        if (!nr::kernel_contract::valid(descriptions[i])) return STATUS_INVALID_PARAMETER;
    }
    LONG previous;
    do {
        previous = InterlockedCompareExchange(&a->allocations, 0, 0);
        if (previous < 0 || previous > 128 - LONG(args->NumAllocations))
            return STATUS_INSUFFICIENT_RESOURCES;
    } while (InterlockedCompareExchange(&a->allocations, previous + args->NumAllocations,
                                       previous) != previous);
    Allocation *created[16]{};
    for (UINT i = 0; i < args->NumAllocations; ++i) {
        created[i] = allocate<Allocation>();
        if (!created[i]) {
            for (UINT j = 0; j < i; ++j) release(created[j]);
            InterlockedExchangeAdd(&a->allocations, -LONG(args->NumAllocations));
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        created[i]->adapter = a;
        created[i]->surface = descriptions[i];
    }
    for (UINT i = 0; i < args->NumAllocations; ++i) {
        auto &out = args->pAllocationInfo[i];
        out.Alignment = 4096;
        out.Size = SIZE_T(descriptions[i].allocation_bytes);
        out.PitchAlignedSize = 0;
        out.HintedBank.Value = 0;
        out.PreferredSegment.Value = 0;
        out.PreferredSegment.SegmentId0 = 1;
        out.SupportedReadSegmentSet = 1;
        out.SupportedWriteSegmentSet = 1;
        out.EvictionSegmentSet = 0;
        out.MaximumRenamingListLength = 0;
        out.Flags.Value = 0;
        out.Flags.CpuVisible = 1;
        out.Flags.Cached = 1;
        out.pAllocationUsageHint = nullptr;
        out.AllocationPriority = D3DDDI_ALLOCATIONPRIORITY_NORMAL;
        out.hAllocation = created[i];
    }
    return STATUS_SUCCESS;
}
DXGKDDI_DESTROYALLOCATION destroy_allocation;
NTSTATUS APIENTRY destroy_allocation(HANDLE adapter, const DXGKARG_DESTROYALLOCATION *args) {
    if (!adapter || !args || !args->pAllocationList || !args->NumAllocations ||
        args->NumAllocations > 16 || args->Flags.Value || args->hResource)
        return STATUS_INVALID_PARAMETER;
    // These are Dxgkrnl-validated opaque KMD handles, never UMD pointers.
    for (UINT i = 0; i < args->NumAllocations; ++i) {
        auto allocation = static_cast<Allocation *>(args->pAllocationList[i]);
        if (!allocation || allocation->adapter != adapter) return STATUS_INVALID_PARAMETER;
        for (UINT j = 0; j < i; ++j)
            if (args->pAllocationList[j] == allocation) return STATUS_INVALID_PARAMETER;
    }
    for (UINT i = 0; i < args->NumAllocations; ++i) release(args->pAllocationList[i]);
    InterlockedExchangeAdd(&static_cast<Adapter *>(adapter)->allocations, -LONG(args->NumAllocations));
    return STATUS_SUCCESS;
}
DXGKDDI_CREATEDEVICE create_device;
NTSTATUS APIENTRY create_device(HANDLE adapter, DXGKARG_CREATEDEVICE *args) {
    if (!adapter || !args) return STATUS_INVALID_PARAMETER;
    auto a = static_cast<Adapter *>(adapter);
    if (!a->started) return STATUS_DEVICE_NOT_READY;
    auto d = allocate<Device>();
    if (!d) return STATUS_INSUFFICIENT_RESOURCES;
    d->adapter = a;
    d->runtime = args->hDevice;
    InterlockedIncrement(&a->devices);
    args->hDevice = d;
    return STATUS_SUCCESS;
}
DXGKDDI_DESTROYDEVICE destroy_device;
NTSTATUS APIENTRY destroy_device(HANDLE device) {
    if (!device) return STATUS_INVALID_PARAMETER;
    auto d = static_cast<Device *>(device);
    if (d->contexts) return STATUS_DEVICE_BUSY;
    InterlockedDecrement(&d->adapter->devices);
    release(d);
    return STATUS_SUCCESS;
}
DXGKDDI_CREATECONTEXT create_context;
NTSTATUS APIENTRY create_context(HANDLE device, DXGKARG_CREATECONTEXT *args) {
    if (!device || !args) return STATUS_INVALID_PARAMETER;
    auto d = static_cast<Device *>(device);
    if (!d->adapter->started) return STATUS_DEVICE_NOT_READY;
    if (args->NodeOrdinal || args->EngineAffinity != 1 || args->PrivateDriverDataSize)
        return STATUS_NOT_SUPPORTED;
    auto c = allocate<Context>();
    if (!c) return STATUS_INSUFFICIENT_RESOURCES;
    c->device = d;
    c->runtime = args->hContext;
    InterlockedIncrement(&d->contexts);
    RtlZeroMemory(&args->ContextInfo, sizeof(args->ContextInfo));
    args->ContextInfo.DmaBufferSize = nr::kmd::Scheduler::max_bytes;
    args->ContextInfo.AllocationListSize = 64;
    args->ContextInfo.PatchLocationListSize = 64;
    args->hContext = c;
    return STATUS_SUCCESS;
}
DXGKDDI_DESTROYCONTEXT destroy_context;
NTSTATUS APIENTRY destroy_context(HANDLE context) {
    if (!context) return STATUS_INVALID_PARAMETER;
    auto c = static_cast<Context *>(context);
    InterlockedDecrement(&c->device->contexts);
    release(c);
    return STATUS_SUCCESS;
}
} // namespace

extern "C" DRIVER_INITIALIZE DriverEntry;
extern "C" NTSTATUS DriverEntry(DRIVER_OBJECT *driver, UNICODE_STRING *registry) {
    DRIVER_INITIALIZATION_DATA data{};
    data.Version = DXGKDDI_INTERFACE_VERSION_WDDM1_3;
    data.DxgkDdiAddDevice = add_device;
    data.DxgkDdiStartDevice = start_device;
    data.DxgkDdiStopDevice = stop_device;
    data.DxgkDdiRemoveDevice = remove_device;
    data.DxgkDdiQueryChildRelations = child_relations;
    data.DxgkDdiInterruptRoutine = interrupt;
    data.DxgkDdiDpcRoutine = dpc;
    data.DxgkDdiResetDevice = reset_device;
    data.DxgkDdiUnload = unload;
    data.DxgkDdiControlEtwLogging = control_etw;
    data.DxgkDdiCreateDevice = create_device;
    data.DxgkDdiDestroyDevice = destroy_device;
    data.DxgkDdiCreateContext = create_context;
    data.DxgkDdiDestroyContext = destroy_context;
#define NR_REJECT(field) data.field = Reject<decltype(data.field)>::call
    NR_REJECT(DxgkDdiDispatchIoRequest);
    NR_REJECT(DxgkDdiQueryChildStatus);
    NR_REJECT(DxgkDdiQueryDeviceDescriptor);
    NR_REJECT(DxgkDdiSetPowerState);
    NR_REJECT(DxgkDdiNotifyAcpiEvent);
    NR_REJECT(DxgkDdiQueryInterface);
    data.DxgkDdiQueryAdapterInfo = query_adapter;
    data.DxgkDdiCreateAllocation = create_allocation;
    data.DxgkDdiDestroyAllocation = destroy_allocation;
    NR_REJECT(DxgkDdiDescribeAllocation);
    NR_REJECT(DxgkDdiGetStandardAllocationDriverData);
    NR_REJECT(DxgkDdiOpenAllocation);
    NR_REJECT(DxgkDdiCloseAllocation);
    NR_REJECT(DxgkDdiPatch);
    NR_REJECT(DxgkDdiSubmitCommand);
    NR_REJECT(DxgkDdiBuildPagingBuffer);
    NR_REJECT(DxgkDdiPreemptCommand);
    NR_REJECT(DxgkDdiRender);
    NR_REJECT(DxgkDdiPresent);
    NR_REJECT(DxgkDdiResetFromTimeout);
    NR_REJECT(DxgkDdiRestartFromTimeout);
    NR_REJECT(DxgkDdiEscape);
    NR_REJECT(DxgkDdiCollectDbgInfo);
    NR_REJECT(DxgkDdiQueryCurrentFence);
    NR_REJECT(DxgkDdiControlInterrupt);
    NR_REJECT(DxgkDdiRenderKm);
    NR_REJECT(DxgkDdiQueryDependentEngineGroup);
    NR_REJECT(DxgkDdiQueryEngineStatus);
    NR_REJECT(DxgkDdiResetEngine);
    NR_REJECT(DxgkDdiCancelCommand);
    NR_REJECT(DxgkDdiGetNodeMetadata);
#undef NR_REJECT
    // Every display-specific DDI stays null: this device must never own scanout.
    return DxgkInitialize(driver, registry, &data);
}
