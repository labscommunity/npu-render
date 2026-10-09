// Original WDDM miniport bring-up. This target is deliberately not included in
// the desktop bundle. StartDevice remains closed until the memory manager,
// scheduler notifications, and worker teardown have passed isolated tests.
extern "C" {
#include <ntifs.h>
#include <dispmprt.h>
}
inline void *operator new(size_t, void *p) noexcept { return p; }

#include "scheduler.hpp"
#include "contract.hpp"
#include "segment.hpp"
#include "paging.hpp"
#include "submission.hpp"
#include "engine.hpp"
#if NR_KMD_LAB_ENGINE_PROBE
#include "engine_probe.hpp"
#endif
#if NR_KMD_LAB_SYNC_PROBE
#include "synchronization_probe.hpp"
#endif
#if NR_KMD_LAB_WORKER_PROBE
#include "worker_probe.hpp"
#endif

namespace {
constexpr ULONG pool_tag = 'rRpN';
constexpr bool runtime_ready = false;
struct Adapter {
    const DEVICE_OBJECT *physical;
    DXGKRNL_INTERFACE kernel;
    nr::kmd::Scheduler scheduler;
    nr::kmd::Segment segment;
    nr::kmd::Submissions submissions;
    nr::kmd::Engine engine;
#if NR_KMD_LAB_ENGINE_PROBE
    nr::kmd::EngineProbe engine_probe;
#endif
#if NR_KMD_LAB_SYNC_PROBE
    nr::kmd::SynchronizationProbe synchronization;
#endif
#if NR_KMD_LAB_WORKER_PROBE
    nr::kmd::WorkerProbe worker_probe;
#endif
    LONG devices;
    LONG allocations;
    bool started;
    bool resetting;
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
// present and render never report fake completion.
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
#if NR_KMD_LAB_SYNC_PROBE
    a->synchronization.run(*kernel);
    // Error-level filtering makes this explicit lab diagnostic visible in a
    // default debugger configuration. This code is absent from default builds.
    const auto &probe = a->synchronization;
    DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
        "nr_render_lab: sync status=%08lx calls=%lu irql=%u returned=%u valid=%u; startup remains disabled\n",
        ULONG(probe.status), probe.calls, UINT(probe.callback_irql),
        UINT(probe.returned), UINT(probe.valid_observation()));
#endif
#if NR_KMD_LAB_WORKER_PROBE
    a->worker_probe.run(*kernel);
    const auto &worker = a->worker_probe;
    DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
        "nr_render_lab: worker status=%08lx create=%08lx memory=%08lx phase=%lu sync=%08lx wait=%08lx close=%08lx joined=%u irql=%u/%u/%u calls=%lu/%ld queued=%u returned=%u; startup remains disabled\n",
        ULONG(worker.status), ULONG(worker.create_status), ULONG(worker.memory_status), worker.memory_phase,
        ULONG(worker.sync_status), ULONG(worker.dpc_wait_status), ULONG(worker.close_status),
        UINT(worker.joined), UINT(worker.worker_irql), UINT(worker.sync_irql), UINT(worker.dpc_irql),
        worker.sync_calls, worker.dpc_calls, UINT(worker.queued), UINT(worker.returned));
    DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
        "nr_render_lab: backing attempts=%lu first_status=%08lx committed_bytes=%llu; probe capacity is not desktop capacity\n",
        worker.backing_attempts, ULONG(worker.first_backing_status), ULONGLONG(worker.backing_bytes));
#endif
#if NR_KMD_LAB_ENGINE_PROBE
    a->engine_probe.run(*kernel, start->RequiredDmaQueueEntry, a->engine,
                        a->scheduler, a->submissions, a->segment);
    const auto &engine_probe = a->engine_probe;
    DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
        "nr_render_lab: engine status=%08lx phase=%lu stop=%08lx required=%lu capacity=%llu notifications=%ld dpcs=%ld irql=%u/%u/%u; notifications terminate at lab observers, not Windows; startup remains disabled\n",
        ULONG(engine_probe.status), engine_probe.phase, ULONG(engine_probe.stop_status),
        engine_probe.required_entries, ULONGLONG(engine_probe.queue_entries), engine_probe.notifications,
        engine_probe.dpcs, UINT(engine_probe.worker_irql), UINT(engine_probe.notify_irql), UINT(engine_probe.dpc_irql));
#endif
    // Do not let Dxgkrnl select an adapter with incomplete mandatory DDIs.
    if (!runtime_ready) return STATUS_NOT_SUPPORTED;
    const auto memory_status = a->segment.prepare();
    if (!NT_SUCCESS(memory_status)) return memory_status;
    const auto queue_status = a->submissions.prepare(a->scheduler, start->RequiredDmaQueueEntry);
    if (!NT_SUCCESS(queue_status)) {
        a->segment.release();
        return queue_status;
    }
    a->kernel = *kernel;
    const auto engine_status = a->engine.start(a->kernel, a->scheduler, a->submissions, a->segment);
    if (engine_status != STATUS_SUCCESS) {
        a->submissions.release();
        a->segment.release();
        RtlZeroMemory(&a->kernel, sizeof(a->kernel));
        return engine_status;
    }
    a->started = true;
    return STATUS_SUCCESS;
}
DXGKDDI_STOP_DEVICE stop_device;
NTSTATUS stop_device(void *context) {
    if (!context) return STATUS_INVALID_PARAMETER;
    auto a = static_cast<Adapter *>(context);
    if (a->devices || a->allocations || (a->scheduler.active() && !a->engine.alive()))
        return STATUS_DEVICE_BUSY;
    const auto engine_status = a->engine.stop();
    if (engine_status != STATUS_SUCCESS) return engine_status;
    a->scheduler.stop();
    const auto queue_status = a->submissions.release();
    if (!NT_SUCCESS(queue_status)) return queue_status;
    a->started = false;
    a->resetting = false;
    a->segment.release();
    RtlZeroMemory(&a->kernel, sizeof(a->kernel));
    return STATUS_SUCCESS;
}
DXGKDDI_REMOVE_DEVICE remove_device;
NTSTATUS remove_device(void *context) {
    if (!context) return STATUS_INVALID_PARAMETER;
    auto a = static_cast<Adapter *>(context);
    if (a->started || a->devices || a->allocations || a->scheduler.active() ||
        a->segment.ready() || a->submissions.ready() || a->engine.alive()) return STATUS_DEVICE_BUSY;
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
void dpc(void *context) {
    if (context) static_cast<Adapter *>(context)->engine.observe_dpc();
#if NR_KMD_LAB_WORKER_PROBE
    if (context) static_cast<Adapter *>(context)->worker_probe.observe_dpc();
#else
    UNREFERENCED_PARAMETER(context);
#endif
    // The engine stays inactive while runtime_ready is false.
}
DXGKDDI_RESET_DEVICE reset_device;
void reset_device(void *context) {
    if (context) {
        auto a = static_cast<Adapter *>(context);
        a->engine.request_shutdown();
    }
}
DXGKDDI_SUBMITCOMMAND submit;
NTSTATUS APIENTRY submit(HANDLE adapter, const DXGKARG_SUBMITCOMMAND *args) {
    if (!adapter || !args) return STATUS_INVALID_PARAMETER;
    auto a = static_cast<Adapter *>(adapter);
    if (!a->started || a->resetting) return STATUS_DEVICE_NOT_READY;
    return a->engine.submit(*args);
}
DXGKDDI_PREEMPTCOMMAND preempt;
NTSTATUS APIENTRY preempt(HANDLE adapter, const DXGKARG_PREEMPTCOMMAND *args) {
    if (!adapter || !args) return STATUS_INVALID_PARAMETER;
    auto a = static_cast<Adapter *>(adapter);
    if (!a->started || a->resetting) return STATUS_DEVICE_NOT_READY;
    return a->engine.preempt(*args);
}
DXGKDDI_QUERYCURRENTFENCE current_fence;
NTSTATUS APIENTRY current_fence(HANDLE adapter, DXGKARG_QUERYCURRENTFENCE *args) {
    if (!adapter || !args) return STATUS_INVALID_PARAMETER;
    if (args->NodeOrdinal || args->EngineOrdinal) return STATUS_NOT_SUPPORTED;
    auto a = static_cast<Adapter *>(adapter);
    if (!a->started) return STATUS_DEVICE_NOT_READY;
    if (a->resetting) {
        // Reset joined the sole producer and retains the last reported fence.
        args->CurrentFence = a->scheduler.completed();
        return STATUS_SUCCESS;
    }
    return a->engine.query(args->CurrentFence);
}
DXGKDDI_RESETFROMTIMEOUT reset_from_timeout;
NTSTATUS APIENTRY reset_from_timeout(HANDLE adapter) {
    if (!adapter) return STATUS_INVALID_PARAMETER;
    auto a = static_cast<Adapter *>(adapter);
    if (!a->started) return STATUS_DEVICE_NOT_READY;
    const auto status = a->engine.stop();
    if (status == STATUS_SUCCESS) a->resetting = true;
    return status;
}
DXGKDDI_RESTARTFROMTIMEOUT restart_from_timeout;
NTSTATUS APIENTRY restart_from_timeout(HANDLE adapter) {
    if (!adapter) return STATUS_INVALID_PARAMETER;
    auto a = static_cast<Adapter *>(adapter);
    if (!a->started || !a->resetting) return STATUS_INVALID_DEVICE_STATE;
    const auto status = a->engine.start(a->kernel, a->scheduler, a->submissions, a->segment);
    if (status == STATUS_SUCCESS) a->resetting = false;
    return status;
}
DXGKDDI_UNLOAD unload;
void unload() {}
DXGKDDI_CONTROL_ETW_LOGGING control_etw;
void control_etw(BOOLEAN, ULONG, UCHAR) {}
DXGKDDI_QUERYADAPTERINFO query_adapter;
NTSTATUS APIENTRY query_adapter(HANDLE adapter, const DXGKARG_QUERYADAPTERINFO *args) {
    if (!adapter || !args || !args->pOutputData) return STATUS_INVALID_PARAMETER;
    if (args->Type == DXGKQAITYPE_QUERYSEGMENT3) {
        if (!args->pInputData || args->InputDataSize != sizeof(DXGK_QUERYSEGMENTIN))
            return STATUS_INVALID_PARAMETER;
        if (args->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT3))
            return STATUS_BUFFER_TOO_SMALL;
        auto &out = *static_cast<DXGK_QUERYSEGMENTOUT3 *>(args->pOutputData);
        const auto status = static_cast<Adapter *>(adapter)->segment.describe(out);
        if (NT_SUCCESS(status) && out.pSegmentDescriptor)
            out.PagingBufferPrivateDataSize = nr::kmd::Paging::private_bytes;
        return status;
    }
    if (args->Type != DXGKQAITYPE_UMDRIVERPRIVATE) return STATUS_NOT_SUPPORTED;
    if (args->InputDataSize || args->OutputDataSize != sizeof(nr::kernel_contract::AdapterInfo))
        return STATUS_INVALID_PARAMETER;
    nr::kernel_contract::AdapterInfo info{};
    info.ready = runtime_ready ? 1 : 0;
    RtlCopyMemory(args->pOutputData, &info, sizeof(info));
    return STATUS_SUCCESS;
}
DXGKDDI_BUILDPAGINGBUFFER build_paging;
NTSTATUS APIENTRY build_paging(HANDLE adapter, DXGKARG_BUILDPAGINGBUFFER *args) {
    if (!adapter || !args) return STATUS_INVALID_PARAMETER;
    auto a = static_cast<Adapter *>(adapter);
    if (!a->started) return STATUS_DEVICE_NOT_READY;
    // During TDR cleanup VidMm reports an eviction with a zero-byte transfer.
    // No data is moved and no DMA command is emitted for this notification.
    if (a->resetting && args->Operation == DXGK_OPERATION_TRANSFER && !args->Transfer.TransferSize)
        return STATUS_SUCCESS;
    return nr::kmd::Paging::build(a->segment, *args);
}
DXGKDDI_CREATEALLOCATION create_allocation;
NTSTATUS APIENTRY create_allocation(HANDLE adapter, DXGKARG_CREATEALLOCATION *args) {
    if (!adapter || !args || !args->pAllocationInfo || !args->NumAllocations ||
        args->NumAllocations > 16 || args->Flags.Value || args->hResource ||
        args->PrivateDriverDataSize) return STATUS_INVALID_PARAMETER;
    auto a = static_cast<Adapter *>(adapter);
    if (!a->started || !a->segment.ready()) return STATUS_DEVICE_NOT_READY;
    nr::kernel_contract::Surface descriptions[16]{};
    // Validate and snapshot every descriptor before publishing any handle.
    for (UINT i = 0; i < args->NumAllocations; ++i) {
        const auto &in = args->pAllocationInfo[i];
        if (!in.pPrivateDriverData || in.PrivateDriverDataSize != sizeof(descriptions[i]))
            return STATUS_INVALID_PARAMETER;
        RtlCopyMemory(&descriptions[i], in.pPrivateDriverData, sizeof(descriptions[i]));
        if (!nr::kernel_contract::valid(descriptions[i])) return STATUS_INVALID_PARAMETER;
        if (descriptions[i].allocation_bytes > a->segment.capacity())
            return STATUS_INSUFFICIENT_RESOURCES;
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
    data.DxgkDdiSubmitCommand = submit;
    data.DxgkDdiBuildPagingBuffer = build_paging;
    data.DxgkDdiPreemptCommand = preempt;
    NR_REJECT(DxgkDdiRender);
    NR_REJECT(DxgkDdiPresent);
    data.DxgkDdiResetFromTimeout = reset_from_timeout;
    data.DxgkDdiRestartFromTimeout = restart_from_timeout;
    NR_REJECT(DxgkDdiEscape);
    NR_REJECT(DxgkDdiCollectDbgInfo);
    data.DxgkDdiQueryCurrentFence = current_fence;
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
