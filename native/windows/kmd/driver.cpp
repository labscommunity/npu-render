// Original WDDM miniport bring-up. This target is deliberately not included in
// the desktop bundle. StartDevice remains closed until the memory manager,
// scheduler notifications, and worker teardown have passed isolated tests.
extern "C" {
#include <ntifs.h>
#include <dispmprt.h>
}
#if NR_KMD_LAB_PAGING_ADAPTER
#define NR_LAB_TRACE(...) DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL, __VA_ARGS__)
#else
#define NR_LAB_TRACE(...) do {} while (false)
#endif
inline void *operator new(size_t, void *p) noexcept { return p; }

#include "scheduler.hpp"
#include "contract.hpp"
#include "segment.hpp"
#include "paging.hpp"
#include "fill.hpp"
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
constexpr bool startup_enabled = runtime_ready || NR_KMD_LAB_PAGING_ADAPTER != 0;
constexpr bool lab_fill_enabled = NR_KMD_LAB_PAGING_ADAPTER != 0;
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
    LONG opens;
    volatile LONG power_state = PowerDeviceD0;
    bool started;
    bool resetting;
};
struct Allocation { Adapter *adapter; nr::kernel_contract::Surface surface; LONG opens; };
struct Device { Adapter *adapter; HANDLE runtime; LONG contexts; LONG opens; };
struct Context { Device *device; HANDLE runtime; };
struct OpenAllocation { Device *device; Allocation *allocation; bool read_only; };

Allocation *writable_allocation(Adapter *adapter, const DXGK_ALLOCATIONLIST *list, UINT count,
                                Device *device = nullptr) {
    if (!list || count != 1 || !list[0].hDeviceSpecificAllocation ||
        !list[0].WriteOperation || list[0].Reserved) return nullptr;
    // The OS has converted its handle to our per-device open handle. No user
    // pointer is read here. The allocation remains referenced until completion.
    const auto opened = static_cast<OpenAllocation *>(list[0].hDeviceSpecificAllocation);
    if (opened->read_only || opened->device->adapter != adapter ||
        (device && opened->device != device) || opened->allocation->adapter != adapter) return nullptr;
    return opened->allocation;
}

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
    // Only the explicit disposable-VM build may start the paging-only engine.
    // Paired UMD readiness and display ownership remain disabled in that build.
    if (!startup_enabled) return STATUS_NOT_SUPPORTED;
    const auto memory_status = a->segment.prepare(NR_KMD_LAB_PAGING_ADAPTER ?
        16 * 1024 * 1024 : nr::kmd::Segment::bytes);
    NR_LAB_TRACE("nr_paging_lab: memory=%08lx required_queue=%lu\n", ULONG(memory_status), start->RequiredDmaQueueEntry);
    if (!NT_SUCCESS(memory_status)) return memory_status;
    const auto queue_status = a->submissions.prepare(a->scheduler, start->RequiredDmaQueueEntry);
    if (!NT_SUCCESS(queue_status)) {
        a->segment.release();
        return queue_status;
    }
    a->kernel = *kernel;
    const auto engine_status = a->engine.start(a->kernel, a->scheduler, a->submissions, a->segment);
    NR_LAB_TRACE("nr_paging_lab: engine_start=%08lx capacity=%llu; rendering_ready=0 sources=0 children=0\n",
        ULONG(engine_status), ULONGLONG(a->submissions.capacity()));
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
    if (a->devices || a->allocations || a->opens || (a->scheduler.active() && !a->engine.alive()))
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
    if (a->started || a->devices || a->allocations || a->opens || a->scheduler.active() ||
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
DXGKDDI_PATCH patch;
NTSTATUS APIENTRY patch(HANDLE adapter, const DXGKARG_PATCH *args) {
    if (!adapter || !args) return STATUS_INVALID_PARAMETER;
    auto a = static_cast<Adapter *>(adapter);
    if (!a->started || a->resetting) return STATUS_DEVICE_NOT_READY;
    NTSTATUS status;
    if (!args->Flags.Paging && args->DmaBufferSubmissionEndOffset != args->DmaBufferSubmissionStartOffset) {
        const auto allocation = writable_allocation(a, args->pAllocationList, args->AllocationListSize);
        status = allocation ? nr::kmd::Fill::patch(a->segment, SIZE_T(allocation->surface.allocation_bytes), *args) :
                              STATUS_INVALID_HANDLE;
    } else status = nr::kmd::Submissions::patch(*args);
    NR_LAB_TRACE("nr_paging_lab: patch=%08lx flags=%lx fence=%lu dma=%lu..%lu private=%lu..%lu\n",
        ULONG(status), args->Flags.Value, args->SubmissionFenceId,
        args->DmaBufferSubmissionStartOffset, args->DmaBufferSubmissionEndOffset,
        args->DmaBufferPrivateDataSubmissionStartOffset, args->DmaBufferPrivateDataSubmissionEndOffset);
    return status;
}
DXGKDDI_SUBMITCOMMAND submit;
NTSTATUS APIENTRY submit(HANDLE adapter, const DXGKARG_SUBMITCOMMAND *args) {
    if (!adapter || !args) return STATUS_INVALID_PARAMETER;
    auto a = static_cast<Adapter *>(adapter);
    if (!a->started || a->resetting) return STATUS_DEVICE_NOT_READY;
    const auto status = a->engine.submit(*args);
    NR_LAB_TRACE("nr_paging_lab: submit=%08lx flags=%lx fence=%lu\n", ULONG(status), args->Flags.Value, args->SubmissionFenceId);
    return status;
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
        // Reset joined the sole producer and established the recovery baseline.
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
    if (status == STATUS_SUCCESS) {
        if (a->scheduler.reset_adapter_fence() != nr::kmd::QueueResult::ok)
            return STATUS_INVALID_DEVICE_STATE;
        a->resetting = true;
    }
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
DXGKDDI_QUERYDEPENDENTENGINEGROUP dependent_engines;
NTSTATUS APIENTRY dependent_engines(HANDLE adapter, DXGKARG_QUERYDEPENDENTENGINEGROUP *args) {
    if (!adapter || !args) return STATUS_INVALID_PARAMETER;
    if (args->NodeOrdinal || args->EngineOrdinal) return STATUS_NOT_SUPPORTED;
    args->DependentNodeOrdinalMask = 1; // The single node depends only on itself.
    return STATUS_SUCCESS;
}
DXGKDDI_QUERYENGINESTATUS engine_status;
NTSTATUS APIENTRY engine_status(HANDLE adapter, DXGKARG_QUERYENGINESTATUS *args) {
    if (!adapter || !args) return STATUS_INVALID_PARAMETER;
    if (args->NodeOrdinal || args->EngineOrdinal) return STATUS_NOT_SUPPORTED;
    return static_cast<Adapter *>(adapter)->engine.health(args->EngineStatus);
}
DXGKDDI_RESETENGINE reset_engine;
NTSTATUS APIENTRY reset_engine(HANDLE adapter, DXGKARG_RESETENGINE *args) {
    if (!adapter || !args) return STATUS_INVALID_PARAMETER;
    if (args->NodeOrdinal || args->EngineOrdinal) return STATUS_NOT_SUPPORTED;
    auto a = static_cast<Adapter *>(adapter);
    if (!a->started || a->resetting) return STATUS_DEVICE_NOT_READY;
    auto status = a->engine.stop();
    if (status != STATUS_SUCCESS) return status;
    const auto aborted = a->engine.aborted_fence_after_join();
    if (a->scheduler.reset_engine_fence(aborted) != nr::kmd::QueueResult::ok)
        return STATUS_INVALID_DEVICE_STATE;
    // Per-engine reset returns only with an empty queue ready for new packets.
    status = a->engine.start(a->kernel, a->scheduler, a->submissions, a->segment);
    if (status == STATUS_SUCCESS) args->LastAbortedFenceId = aborted;
    return status;
}
DXGKDDI_COLLECTDBGINFO collect_debug;
NTSTATUS APIENTRY collect_debug(HANDLE adapter, const DXGKARG_COLLECTDBGINFO *args) {
    if (!adapter || !args || !args->pBuffer) return STATUS_INVALID_PARAMETER;
    if (args->BufferSize < sizeof(nr::kmd::Engine::DebugInfo)) return STATUS_BUFFER_TOO_SMALL;
    const auto data = static_cast<Adapter *>(adapter)->engine.debug_snapshot(args->Reason);
    RtlCopyMemory(args->pBuffer, &data, sizeof(data));
    return STATUS_SUCCESS;
}
DXGKDDI_SET_POWER_STATE set_power;
NTSTATUS set_power(void *context, ULONG device, DEVICE_POWER_STATE state, POWER_ACTION) {
    if (!context || state < PowerDeviceD0 || state > PowerDeviceD3)
        return STATUS_INVALID_PARAMETER;
    // This software paging engine has no bus resource, device registers or
    // volatile device memory. Its system thread and nonpaged RAM follow system
    // suspend/resume. Retain queue/fence/segment state; do not cancel packets or
    // allocate a replacement worker during D0. An NPU provider will need its own
    // device-power contract before this adapter can execute NPU work.
    if (device == DISPLAY_ADAPTER_HW_ID)
        InterlockedExchange(&static_cast<Adapter *>(context)->power_state, LONG(state));
    // There are no child power resources, including a stale child notification.
    return STATUS_SUCCESS;
}
DXGKDDI_GETNODEMETADATA node_metadata;
NTSTATUS APIENTRY node_metadata(HANDLE adapter, UINT node, DXGKARG_GETNODEMETADATA *args) {
    if (!adapter || !args) return STATUS_INVALID_PARAMETER;
    if (node) return STATUS_NOT_SUPPORTED;
    DXGKARG_GETNODEMETADATA info{};
    info.EngineType = DXGK_ENGINE_TYPE_COPY; // Current engine only performs paging.
    *args = info;
    return STATUS_SUCCESS;
}
DXGKDDI_UNLOAD unload;
void unload() {}
DXGKDDI_CONTROL_ETW_LOGGING control_etw;
void control_etw(BOOLEAN, ULONG, UCHAR) {}
DXGKDDI_QUERYADAPTERINFO query_adapter;
NTSTATUS APIENTRY query_adapter(HANDLE adapter, const DXGKARG_QUERYADAPTERINFO *args) {
    if (!adapter || !args || !args->pOutputData) return STATUS_INVALID_PARAMETER;
    NR_LAB_TRACE("nr_paging_lab: query type=%u input=%lu output=%lu\n", UINT(args->Type), args->InputDataSize, args->OutputDataSize);
    if (args->Type == DXGKQAITYPE_DRIVERCAPS) {
        if (args->InputDataSize) return STATUS_INVALID_PARAMETER;
        if (args->OutputDataSize < sizeof(DXGK_DRIVERCAPS)) return STATUS_BUFFER_TOO_SMALL;
        DXGK_DRIVERCAPS caps{};
        caps.HighestAcceptableAddress.QuadPart = MAXLONGLONG;
        caps.PresentationCaps.NoScreenToScreenBlt = 1;
        caps.PresentationCaps.NoOverlapScreenBlt = 1;
        caps.PresentationCaps.NoCacheCoherentApertureMemory = 1;
        caps.SchedulingCaps.MultiEngineAware = 1;
        caps.SchedulingCaps.PreemptionAware = 1;
        caps.GpuEngineTopology.NbAsymetricProcessingNodes = 1;
        caps.WDDMVersion = DXGKDDI_WDDMv1_3;
        caps.PreemptionCaps.GraphicsPreemptionGranularity = D3DKMDT_GRAPHICS_PREEMPTION_DMA_BUFFER_BOUNDARY;
        caps.PreemptionCaps.ComputePreemptionGranularity = D3DKMDT_COMPUTE_PREEMPTION_DMA_BUFFER_BOUNDARY;
        caps.SupportPerEngineTDR = TRUE;
        // No display flips, GDI aperture, cross-adapter resources, virtual GPU
        // addresses or runtime power components are implemented. Do not claim
        // them to satisfy a capability checklist. Match DriverEntry's WDDM 1.3
        // version as Microsoft's render-only sample does. The member reference
        // says zero for newer interfaces, but our zero-version live attempt
        // was rejected after this query; version consistency is under test.
        RtlCopyMemory(args->pOutputData, &caps, sizeof(caps));
        return STATUS_SUCCESS;
    }
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
    const auto status = nr::kmd::Paging::build(a->segment, *args);
    NR_LAB_TRACE("nr_paging_lab: build_paging operation=%u status=%08lx\n", UINT(args->Operation), ULONG(status));
    return status;
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
        // VidMm rejects cached allocations targeting a memory segment when
        // the adapter has no cache-coherent aperture. Segment backing uses
        // MmWriteCombined so CPU aliases use the same memory type.
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
        if (InterlockedCompareExchange(&allocation->opens, 0, 0)) return STATUS_DEVICE_BUSY;
        for (UINT j = 0; j < i; ++j)
            if (args->pAllocationList[j] == allocation) return STATUS_INVALID_PARAMETER;
    }
    for (UINT i = 0; i < args->NumAllocations; ++i) release(args->pAllocationList[i]);
    InterlockedExchangeAdd(&static_cast<Adapter *>(adapter)->allocations, -LONG(args->NumAllocations));
    return STATUS_SUCCESS;
}
DXGKDDI_DESCRIBEALLOCATION describe_allocation;
NTSTATUS APIENTRY describe_allocation(HANDLE adapter, DXGKARG_DESCRIBEALLOCATION *args) {
    if (!adapter || !args || !args->hAllocation) return STATUS_INVALID_PARAMETER;
    if (args->Flags.Value) return STATUS_NOT_SUPPORTED;
    const auto allocation = static_cast<Allocation *>(args->hAllocation);
    if (allocation->adapter != adapter) return STATUS_INVALID_HANDLE;
    const auto &surface = allocation->surface;
    args->Width = surface.width;
    args->Height = surface.height;
    args->Format = surface.format == 87 ? D3DDDIFMT_A8R8G8B8 : D3DDDIFMT_A8B8G8R8;
    args->MultisampleMethod = {};
    args->RefreshRate = {}; // No primary/scanout mode belongs to this allocation.
    args->PrivateDriverFormatAttribute = 0;
    args->Rotation = D3DDDI_ROTATION_IDENTITY;
    return STATUS_SUCCESS;
}
DXGKDDI_OPENALLOCATIONINFO open_allocation;
NTSTATUS APIENTRY open_allocation(HANDLE device, const DXGKARG_OPENALLOCATION *args) {
    if (!device || !args || !args->NumAllocations || args->NumAllocations > 16 ||
        !args->pOpenAllocation) return STATUS_INVALID_PARAMETER;
    DXGK_OPENALLOCATIONFLAGS allowed{};
    allowed.Create = allowed.ReadOnly = 1;
    if ((args->Flags.Value & ~allowed.Value) || args->PrivateDriverSize || args->SubresourceIndex)
        return STATUS_NOT_SUPPORTED;
    auto d = static_cast<Device *>(device);
    auto a = d->adapter;
    if (!a->started || !a->kernel.DxgkCbGetHandleData) return STATUS_DEVICE_NOT_READY;
    Allocation *allocations[16]{};
    for (UINT i = 0; i < args->NumAllocations; ++i) {
        const auto &in = args->pOpenAllocation[i];
        if (in.PrivateDriverDataSize &&
            (!in.pPrivateDriverData || in.PrivateDriverDataSize != sizeof(nr::kernel_contract::Surface)))
            return STATUS_INVALID_PARAMETER;
        DXGKARGCB_GETHANDLEDATA lookup{};
        lookup.hObject = in.hAllocation;
        lookup.Type = DXGK_HANDLE_ALLOCATION;
        // This 32-bit OS handle is never interpreted as a kernel pointer.
        allocations[i] = static_cast<Allocation *>(a->kernel.DxgkCbGetHandleData(&lookup));
        if (!allocations[i] || allocations[i]->adapter != a) return STATUS_INVALID_HANDLE;
    }
    // One adapter-wide bound also bounds every per-device/per-allocation count.
    LONG previous;
    do {
        previous = InterlockedCompareExchange(&a->opens, 0, 0);
        if (previous < 0 || previous > 4096 - LONG(args->NumAllocations))
            return STATUS_INSUFFICIENT_RESOURCES;
    } while (InterlockedCompareExchange(&a->opens, previous + LONG(args->NumAllocations), previous) != previous);
    OpenAllocation *created[16]{};
    for (UINT i = 0; i < args->NumAllocations; ++i) {
        created[i] = allocate<OpenAllocation>();
        if (!created[i]) {
            for (UINT j = 0; j < i; ++j) release(created[j]);
            InterlockedExchangeAdd(&a->opens, -LONG(args->NumAllocations));
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        created[i]->device = d;
        created[i]->allocation = allocations[i];
        created[i]->read_only = args->Flags.ReadOnly != 0;
    }
    for (UINT i = 0; i < args->NumAllocations; ++i) {
        InterlockedIncrement(&allocations[i]->opens);
        auto &out = args->pOpenAllocation[i];
        if (out.PrivateDriverDataSize)
            RtlCopyMemory(out.pPrivateDriverData, &allocations[i]->surface, sizeof(allocations[i]->surface));
        out.hDeviceSpecificAllocation = created[i];
    }
    InterlockedExchangeAdd(&d->opens, LONG(args->NumAllocations));
    return STATUS_SUCCESS;
}
DXGKDDI_CLOSEALLOCATION close_allocation;
NTSTATUS APIENTRY close_allocation(HANDLE device, const DXGKARG_CLOSEALLOCATION *args) {
    if (!device || !args || !args->NumAllocations || args->NumAllocations > 16 ||
        !args->pOpenHandleList) return STATUS_INVALID_PARAMETER;
    auto d = static_cast<Device *>(device);
    for (UINT i = 0; i < args->NumAllocations; ++i) {
        const auto opened = static_cast<OpenAllocation *>(args->pOpenHandleList[i]);
        if (!opened || opened->device != d) return STATUS_INVALID_HANDLE;
        for (UINT j = 0; j < i; ++j)
            if (args->pOpenHandleList[j] == opened) return STATUS_INVALID_PARAMETER;
    }
    for (UINT i = 0; i < args->NumAllocations; ++i) {
        auto opened = static_cast<OpenAllocation *>(args->pOpenHandleList[i]);
        InterlockedDecrement(&opened->allocation->opens);
        release(opened);
    }
    InterlockedExchangeAdd(&d->opens, -LONG(args->NumAllocations));
    InterlockedExchangeAdd(&d->adapter->opens, -LONG(args->NumAllocations));
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
    if (d->contexts || d->opens) return STATUS_DEVICE_BUSY;
    InterlockedDecrement(&d->adapter->devices);
    release(d);
    return STATUS_SUCCESS;
}
DXGKDDI_CREATECONTEXT create_context;
NTSTATUS APIENTRY create_context(HANDLE device, DXGKARG_CREATECONTEXT *args) {
    if (!device || !args) return STATUS_INVALID_PARAMETER;
    auto d = static_cast<Device *>(device);
    if (!d->adapter->started) return STATUS_DEVICE_NOT_READY;
    if (args->NodeOrdinal || args->EngineAffinity != 1 || args->PrivateDriverDataSize || args->Flags.Value)
        return STATUS_NOT_SUPPORTED;
    auto c = allocate<Context>();
    if (!c) return STATUS_INSUFFICIENT_RESOURCES;
    c->device = d;
    c->runtime = args->hContext;
    InterlockedIncrement(&d->contexts);
    RtlZeroMemory(&args->ContextInfo, sizeof(args->ContextInfo));
    args->ContextInfo.DmaBufferSize = nr::kmd::Scheduler::max_bytes;
    args->ContextInfo.DmaBufferPrivateDataSize = sizeof(nr::kmd::FillRecord);
    args->ContextInfo.AllocationListSize = 64;
    args->ContextInfo.PatchLocationListSize = 64;
    args->hContext = c;
    return STATUS_SUCCESS;
}
DXGKDDI_RENDER render;
NTSTATUS APIENTRY render(HANDLE context, DXGKARG_RENDER *args) {
    if (!lab_fill_enabled) return STATUS_NOT_SUPPORTED;
    if (!context || !args) return STATUS_INVALID_PARAMETER;
    const auto c = static_cast<Context *>(context);
    const auto a = c->device->adapter;
    if (!a->started || a->resetting) return STATUS_DEVICE_NOT_READY;
    const auto allocation = writable_allocation(a, args->pAllocationList, args->AllocationListSize, c->device);
    if (!allocation) return STATUS_INVALID_HANDLE;
    const auto status = nr::kmd::Fill::build(a->segment, SIZE_T(allocation->surface.allocation_bytes), *args);
    NR_LAB_TRACE("nr_paging_lab: render_fill=%08lx\n", ULONG(status));
    return status;
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
    data.DxgkDdiSetPowerState = set_power;
    NR_REJECT(DxgkDdiNotifyAcpiEvent);
    NR_REJECT(DxgkDdiQueryInterface);
    data.DxgkDdiQueryAdapterInfo = query_adapter;
    data.DxgkDdiCreateAllocation = create_allocation;
    data.DxgkDdiDestroyAllocation = destroy_allocation;
    data.DxgkDdiDescribeAllocation = describe_allocation;
    NR_REJECT(DxgkDdiGetStandardAllocationDriverData);
    data.DxgkDdiOpenAllocation = open_allocation;
    data.DxgkDdiCloseAllocation = close_allocation;
    data.DxgkDdiPatch = patch;
    data.DxgkDdiSubmitCommand = submit;
    data.DxgkDdiBuildPagingBuffer = build_paging;
    data.DxgkDdiPreemptCommand = preempt;
    data.DxgkDdiRender = render;
    NR_REJECT(DxgkDdiPresent);
    data.DxgkDdiResetFromTimeout = reset_from_timeout;
    data.DxgkDdiRestartFromTimeout = restart_from_timeout;
    NR_REJECT(DxgkDdiEscape);
    data.DxgkDdiCollectDbgInfo = collect_debug;
    data.DxgkDdiQueryCurrentFence = current_fence;
    NR_REJECT(DxgkDdiControlInterrupt);
    NR_REJECT(DxgkDdiRenderKm);
    data.DxgkDdiQueryDependentEngineGroup = dependent_engines;
    data.DxgkDdiQueryEngineStatus = engine_status;
    data.DxgkDdiResetEngine = reset_engine;
    NR_REJECT(DxgkDdiCancelCommand);
    data.DxgkDdiGetNodeMetadata = node_metadata;
#undef NR_REJECT
    // Every display-specific DDI stays null: this device must never own scanout.
    return DxgkInitialize(driver, registry, &data);
}
