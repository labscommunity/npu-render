// Executes the actual DDI implementation with memory/registration shims. This
// tests validation and unwinding only; it is not a kernel/IRQL or Dxgkrnl test.
extern "C" {
#include <ntifs.h>
#include <dispmprt.h>
void *nr_host_allocate(size_t bytes);
void nr_host_free(void *pointer);
void nr_host_probe(void *data, size_t bytes, unsigned long alignment);
}
namespace {
DRIVER_INITIALIZATION_DATA registration{};
enum class SyncMode {
    failed, missing_callback, valid, repeated, dispatch, profile, false_return, pending_return
};
SyncMode sync_mode = SyncMode::failed;
KIRQL simulated_irql = PASSIVE_LEVEL;
ULONGLONG simulated_time = 100000000;
ULONGLONG interrupt_time() { return simulated_time; }
ULONG sync_calls = 0;
ULONG sync_fail_call = 0;
bool sync_contract_error = false;
KIRQL current_irql() { return simulated_irql; }
#if NR_KMD_LAB_SYNC_PROBE || NR_KMD_LAB_WORKER_PROBE || NR_KMD_LAB_ENGINE_PROBE || NR_KMD_LAB_PAGING_ADAPTER
ULONG lab_print(ULONG, ULONG, PCSTR, ...) { return 0; }
#endif
NTSTATUS synchronize(HANDLE device, PKSYNCHRONIZE_ROUTINE routine, PVOID context,
                     ULONG message, PBOOLEAN result) {
    ++sync_calls;
    if (sync_calls == sync_fail_call) return STATUS_UNSUCCESSFUL;
    if (device != &registration || !routine || !context || message || !result) {
        sync_contract_error = true;
        return STATUS_INVALID_PARAMETER;
    }
    if (sync_mode == SyncMode::failed) return STATUS_UNSUCCESSFUL;
    if (sync_mode == SyncMode::missing_callback) return STATUS_SUCCESS;
    const auto previous = simulated_irql;
    simulated_irql = KIRQL(sync_mode == SyncMode::dispatch ? DISPATCH_LEVEL :
                          sync_mode == SyncMode::profile ? PROFILE_LEVEL : DISPATCH_LEVEL + 1);
    *result = routine(context);
    if (sync_mode == SyncMode::repeated) routine(context);
    if (sync_mode == SyncMode::false_return) *result = FALSE;
    simulated_irql = previous;
    return sync_mode == SyncMode::pending_return ? STATUS_PENDING : STATUS_SUCCESS;
}
long fail_after = -1, outstanding = 0;
void *segment_memory = nullptr;
bool segment_fail = false, segment_contract_error = false;
SIZE_T segment_fail_above = ~SIZE_T(0);
LONGLONG segment_physical = 0x20000000; // Host-only physical-address model.
unsigned segment_allocations = 0, segment_frees = 0;
long mdl_fail_after = -1, mdl_outstanding = 0;
bool mdl_map_fail = false, mdl_contract_error = false;
PMDL mdl_allocate(void *start, ULONG bytes, BOOLEAN secondary, BOOLEAN quota, PIRP irp) {
    if (secondary || quota || irp) mdl_contract_error = true;
    if (mdl_fail_after == 0) return nullptr;
    if (mdl_fail_after > 0) --mdl_fail_after;
    auto mdl = static_cast<PMDL>(nr_host_allocate(sizeof(MDL)));
    if (!mdl) return nullptr;
    RtlZeroMemory(mdl, sizeof(MDL));
    mdl->StartVa = reinterpret_cast<void *>(ULONG_PTR(start) & ~ULONG_PTR(PAGE_SIZE - 1));
    mdl->ByteOffset = ULONG(ULONG_PTR(start) & (PAGE_SIZE - 1));
    mdl->ByteCount = bytes;
    ++mdl_outstanding;
    return mdl;
}
void mdl_partial(PMDL source, PMDL target, void *start, ULONG bytes) {
    const auto offset = ULONG_PTR(start) - ULONG_PTR(MmGetMdlVirtualAddress(source));
    if (offset > source->ByteCount || bytes > source->ByteCount - offset ||
        target->ByteCount != bytes || target->Next) mdl_contract_error = true;
    target->MappedSystemVa = static_cast<UCHAR *>(source->MappedSystemVa) + offset;
    target->MdlFlags = MDL_PARTIAL;
}
void *mdl_map(PMDL mdl, ULONG priority) {
    if (priority != (HighPagePriority | MdlMappingNoExecute) || !(mdl->MdlFlags & MDL_PARTIAL))
        mdl_contract_error = true;
    return mdl_map_fail ? nullptr : mdl->MappedSystemVa;
}
void mdl_free(PMDL mdl) { --mdl_outstanding; nr_host_free(mdl); }
void *segment_allocate(SIZE_T bytes, PHYSICAL_ADDRESS low, PHYSICAL_ADDRESS high,
                       PHYSICAL_ADDRESS boundary, MEMORY_CACHING_TYPE cache) {
    ++segment_allocations;
    if (segment_memory || !bytes || bytes > 64 * 1024 * 1024 || bytes % PAGE_SIZE || low.QuadPart ||
        high.QuadPart != MAXLONGLONG || boundary.QuadPart || cache != MmWriteCombined)
        segment_contract_error = true;
    if (segment_fail || bytes > segment_fail_above) return nullptr;
    segment_memory = nr_host_allocate(bytes);
    if (segment_memory) RtlFillMemory(segment_memory, bytes, 0xA5);
    return segment_memory;
}
PHYSICAL_ADDRESS segment_address(void *p) {
    if (!p || p != segment_memory) segment_contract_error = true;
    PHYSICAL_ADDRESS result{};
    result.QuadPart = segment_physical;
    return result;
}
void segment_free(void *p) {
    if (!p || p != segment_memory) segment_contract_error = true;
    ++segment_frees;
    nr_host_free(p);
    segment_memory = nullptr;
}
void *pool_allocate(POOL_FLAGS, SIZE_T bytes, ULONG) {
    if (fail_after == 0) return nullptr;
    if (fail_after > 0) --fail_after;
    auto p = nr_host_allocate(bytes);
    if (p) ++outstanding;
    return p;
}
void pool_free(void *p, ULONG) { if (p) { --outstanding; nr_host_free(p); } }
void *dpc_owner = nullptr;
bool engine_mode = false;
PKSTART_ROUTINE deferred_thread = nullptr;
void *deferred_thread_context = nullptr;
bool worker_contract_error = false, thread_fail = false, thread_joined = false;
bool thread_closed = false, thread_terminated = false;
bool queue_accept = true, defer_dpc = false, pending_dpc = false;
unsigned thread_wait_failures = 0, retry_delays = 0, dpc_flushes = 0;
KIRQL delivered_dpc_irql = DISPATCH_LEVEL;
#if NR_KMD_LAB_WORKER_PROBE
void mdl_nonpaged(PMDL mdl) {
    mdl->MappedSystemVa = MmGetMdlVirtualAddress(mdl);
    mdl->MdlFlags = MDL_SOURCE_IS_NONPAGED_POOL;
}
#endif
void event_initialize(PRKEVENT event, EVENT_TYPE type, BOOLEAN state) {
    if (type != (engine_mode ? SynchronizationEvent : NotificationEvent) || state)
        worker_contract_error = true;
    RtlZeroMemory(event, sizeof(*event));
}
LONG event_set(PRKEVENT event, KPRIORITY increment, BOOLEAN wait) {
    if (increment || wait || simulated_irql > DISPATCH_LEVEL) worker_contract_error = true;
    return InterlockedExchange(&event->Header.SignalState, 1);
}
NTSTATUS event_wait(void *object, KWAIT_REASON reason, KPROCESSOR_MODE mode,
                    BOOLEAN alertable, LARGE_INTEGER *timeout) {
    if (!object || reason != Executive || mode != KernelMode || alertable ||
        !timeout || timeout->QuadPart != -50000000 || simulated_irql != PASSIVE_LEVEL)
        worker_contract_error = true;
    return static_cast<PRKEVENT>(object)->Header.SignalState ? STATUS_SUCCESS : STATUS_TIMEOUT;
}
NTSTATUS thread_create(PHANDLE handle, ULONG access, POBJECT_ATTRIBUTES attributes,
                       HANDLE process, PCLIENT_ID client, PKSTART_ROUTINE start, void *context) {
    if (!handle || access != SYNCHRONIZE || !attributes ||
        attributes->Attributes != OBJ_KERNEL_HANDLE || process || client || !start || !context ||
        simulated_irql != PASSIVE_LEVEL) worker_contract_error = true;
    thread_joined = thread_closed = thread_terminated = false;
    if (thread_fail) return STATUS_INSUFFICIENT_RESOURCES;
    *handle = &thread_joined;
    if (engine_mode) { deferred_thread = start; deferred_thread_context = context; }
    else start(context); // Host model only; the guest exercises real concurrency.
    return STATUS_SUCCESS;
}
NTSTATUS thread_terminate(NTSTATUS status) {
    if (status != STATUS_SUCCESS) worker_contract_error = true;
    thread_terminated = true;
    return STATUS_SUCCESS;
}
NTSTATUS thread_wait(HANDLE handle, BOOLEAN alertable, LARGE_INTEGER *timeout) {
    if (engine_mode && !thread_terminated && deferred_thread) deferred_thread(deferred_thread_context);
    if (handle != &thread_joined || alertable || timeout || !thread_terminated)
        worker_contract_error = true;
    if (thread_wait_failures) { --thread_wait_failures; return STATUS_UNSUCCESSFUL; }
    thread_joined = true;
    return STATUS_SUCCESS;
}
NTSTATUS thread_close(HANDLE handle) {
    if (handle != &thread_joined || !thread_joined || thread_closed) worker_contract_error = true;
    thread_closed = true;
    return STATUS_SUCCESS;
}
NTSTATUS retry_delay(KPROCESSOR_MODE mode, BOOLEAN alertable, LARGE_INTEGER *interval) {
    if (mode != KernelMode || alertable || !interval || interval->QuadPart != -10000000)
        worker_contract_error = true;
    ++retry_delays;
    return STATUS_SUCCESS;
}
void deliver_dpc() {
    const auto previous = simulated_irql;
    simulated_irql = delivered_dpc_irql;
    registration.DxgkDdiDpcRoutine(dpc_owner);
    simulated_irql = previous;
}
BOOLEAN queue_dpc(HANDLE device) {
    if (device != &registration || !dpc_owner || simulated_irql <= DISPATCH_LEVEL ||
        simulated_irql >= PROFILE_LEVEL) worker_contract_error = true;
    if (pending_dpc) return FALSE;
    if (!queue_accept) return FALSE;
    if (defer_dpc) pending_dpc = true;
    else deliver_dpc();
    return TRUE;
}
void flush_dpcs() {
    if (!thread_joined || !thread_closed || simulated_irql != PASSIVE_LEVEL)
        worker_contract_error = true;
    ++dpc_flushes;
    if (pending_dpc) { pending_dpc = false; deliver_dpc(); }
}
DXGKARGCB_NOTIFY_INTERRUPT_DATA notifications[64]{};
UINT notification_count = 0, notified_dpcs = 0;
void notify_interrupt(HANDLE device, DXGKARGCB_NOTIFY_INTERRUPT_DATA *data) {
    if (device != &registration || !data || simulated_irql <= DISPATCH_LEVEL ||
        simulated_irql >= PROFILE_LEVEL || notification_count == 64) {
        worker_contract_error = true;
        return;
    }
    notifications[notification_count++] = *data;
}
void notify_dpc(HANDLE device) {
    if (device != &registration || simulated_irql != DISPATCH_LEVEL || !notification_count)
        worker_contract_error = true;
    ++notified_dpcs;
}
NTSTATUS initialize(DRIVER_OBJECT *, UNICODE_STRING *, DRIVER_INITIALIZATION_DATA *data) {
    registration = *data;
    return STATUS_SUCCESS;
}
}
#define ExAllocatePool2 pool_allocate
#define ProbeForRead nr_host_probe
#define ExFreePoolWithTag pool_free
#define DxgkInitialize initialize
#define MmAllocateContiguousMemorySpecifyCache segment_allocate
#define MmGetPhysicalAddress segment_address
#define MmFreeContiguousMemory segment_free
#define IoAllocateMdl mdl_allocate
#define IoBuildPartialMdl mdl_partial
#define IoFreeMdl mdl_free
#undef MmGetSystemAddressForMdlSafe
#define MmGetSystemAddressForMdlSafe mdl_map
#define DriverEntry NrHostDriverEntry
#define KeGetCurrentIrql current_irql
#undef KeQueryInterruptTime
#define KeQueryInterruptTime interrupt_time
#if NR_KMD_LAB_SYNC_PROBE || NR_KMD_LAB_WORKER_PROBE || NR_KMD_LAB_ENGINE_PROBE || NR_KMD_LAB_PAGING_ADAPTER
#define DbgPrintEx lab_print
#endif
#if NR_KMD_LAB_WORKER_PROBE
#define MmBuildMdlForNonPagedPool mdl_nonpaged
#endif
#define KeInitializeEvent event_initialize
#define KeSetEvent event_set
#define KeWaitForSingleObject event_wait
#define PsCreateSystemThread thread_create
#define PsTerminateSystemThread thread_terminate
#define ZwWaitForSingleObject thread_wait
#define ZwClose thread_close
#define KeDelayExecutionThread retry_delay
#define KeFlushQueuedDpcs flush_dpcs
#include "driver.cpp"
#undef DriverEntry
#define NR_CHECK(expression) do { if (!(expression)) return __LINE__; } while (false)
#include "paging_test.hpp"
#include "segment_capacity_test.hpp"
#include "submission_test.hpp"
#include "engine_test.hpp"
#include "recovery_test.hpp"
#include "allocation_test.hpp"
#include "fill_test.hpp"
#if NR_KMD_LAB_PAGING_ADAPTER
#include "lab_start_test.hpp"
#endif
#if NR_KMD_LAB_WORKER_PROBE
#include "worker_probe_test.hpp"
#endif
extern "C" int nr_kernel_host_test() {
    NR_CHECK(NT_SUCCESS(NrHostDriverEntry(nullptr, nullptr)));
#if NR_KMD_LAB_PAGING_ADAPTER
    return lab_start_test();
#else
    NR_CHECK(registration.Version == DXGKDDI_INTERFACE_VERSION_WDDM1_3);
    NR_CHECK(!registration.DxgkDdiSetVidPnSourceAddress && !registration.DxgkDdiCommitVidPn);
    NR_CHECK(registration.DxgkDdiSubmitCommand(nullptr, nullptr) == STATUS_INVALID_PARAMETER);
    NR_CHECK(registration.DxgkDdiPresent(nullptr, nullptr) == STATUS_NOT_SUPPORTED);
    NR_CHECK(registration.DxgkDdiPreemptCommand(nullptr, nullptr) == STATUS_INVALID_PARAMETER);
    DEVICE_OBJECT physical{};
    void *opaque = reinterpret_cast<void *>(1);
    fail_after = 0;
    NR_CHECK(registration.DxgkDdiAddDevice(&physical, &opaque) == STATUS_INSUFFICIENT_RESOURCES);
    NR_CHECK(!opaque && !outstanding);
    fail_after = -1;
    NR_CHECK(NT_SUCCESS(registration.DxgkDdiAddDevice(&physical, &opaque)));
    auto a = static_cast<Adapter *>(opaque);
    // Capability output must not imply the unimplemented rendering/display
    // contracts. Too-small output leaves both data and guard bytes untouched.
    struct CapsBuffer { DXGK_DRIVERCAPS caps; ULONG guard; } cap_buffer{};
    cap_buffer.caps.HighestAcceptableAddress.QuadPart = 123;
    cap_buffer.guard = 0xa5a5a5a5;
    DXGKARG_QUERYADAPTERINFO cap_query{};
    cap_query.Type = DXGKQAITYPE_DRIVERCAPS;
    cap_query.pOutputData = &cap_buffer.caps;
    cap_query.OutputDataSize = sizeof(cap_buffer.caps) - 1;
    NR_CHECK(registration.DxgkDdiQueryAdapterInfo(a, &cap_query) == STATUS_BUFFER_TOO_SMALL);
    NR_CHECK(cap_buffer.caps.HighestAcceptableAddress.QuadPart == 123);
    cap_query.OutputDataSize++;
    NR_CHECK(registration.DxgkDdiQueryAdapterInfo(a, &cap_query) == STATUS_SUCCESS);
    const auto &caps = cap_buffer.caps;
    NR_CHECK(cap_buffer.guard == 0xa5a5a5a5 && caps.WDDMVersion == DXGKDDI_WDDMv1_3);
    NR_CHECK(caps.GpuEngineTopology.NbAsymetricProcessingNodes == 1 && caps.SupportPerEngineTDR);
    NR_CHECK(caps.SchedulingCaps.MultiEngineAware && caps.SchedulingCaps.PreemptionAware);
    NR_CHECK(!caps.SchedulingCaps.CancelCommandAware && !caps.SchedulingCaps.NoDmaPatching);
    NR_CHECK(!caps.MemoryManagementCaps.Value && !caps.FlipCaps.Value && !caps.HybridDiscrete &&
             !caps.SupportDirectFlip && !caps.SupportRuntimePowerManagement);
    NR_CHECK(caps.PresentationCaps.NoCacheCoherentApertureMemory &&
             !caps.PresentationCaps.SupportKernelModeCommandBuffer &&
             !caps.PresentationCaps.SupportSoftwareDeviceBitmaps);
    DXGKARG_GETNODEMETADATA metadata{};
    NR_CHECK(registration.DxgkDdiGetNodeMetadata(a, 1, &metadata) == STATUS_NOT_SUPPORTED);
    NR_CHECK(registration.DxgkDdiGetNodeMetadata(a, 0, &metadata) == STATUS_SUCCESS);
    NR_CHECK(metadata.EngineType == DXGK_ENGINE_TYPE_COPY && !metadata.FriendlyName[0]);
    const auto power_outstanding = outstanding;
    fail_after = 0;
    NR_CHECK(registration.DxgkDdiSetPowerState(a, DISPLAY_ADAPTER_HW_ID, PowerDeviceD3,
                                             PowerActionSleep) == STATUS_SUCCESS);
    NR_CHECK(a->power_state == PowerDeviceD3);
    NR_CHECK(registration.DxgkDdiSetPowerState(a, 99, PowerDeviceD0, PowerActionNone) == STATUS_SUCCESS);
    NR_CHECK(a->power_state == PowerDeviceD3); // Stale child notification changes nothing.
    NR_CHECK(registration.DxgkDdiSetPowerState(a, DISPLAY_ADAPTER_HW_ID, PowerDeviceD0,
                                             PowerActionNone) == STATUS_SUCCESS);
    NR_CHECK(a->power_state == PowerDeviceD0 && outstanding == power_outstanding);
    fail_after = -1;
    ULONG sources = 999, children = 999;
    DXGK_START_INFO start{};
    DXGKRNL_INTERFACE kernel{};
    NR_CHECK(registration.DxgkDdiStartDevice(a, &start, &kernel, &sources, &children) ==
             STATUS_NOT_SUPPORTED);
    NR_CHECK(!sources && !children && !a->started && !a->scheduler.running());
#if NR_KMD_LAB_SYNC_PROBE
    NR_CHECK(a->synchronization.status == STATUS_INVALID_PARAMETER && sync_calls == 0);
    kernel.DxgkCbSynchronizeExecution = synchronize;
    NR_CHECK(registration.DxgkDdiStartDevice(a, &start, &kernel, &sources, &children) ==
             STATUS_NOT_SUPPORTED);
    NR_CHECK(!a->synchronization.valid_observation() && sync_calls == 0);
    kernel.DeviceHandle = &registration;
    const SyncMode modes[] = {SyncMode::failed, SyncMode::missing_callback, SyncMode::valid,
        SyncMode::repeated, SyncMode::dispatch, SyncMode::profile, SyncMode::false_return,
        SyncMode::pending_return};
    for (const auto mode : modes) {
        sync_mode = mode;
        const auto before = sync_calls;
        sources = children = 999;
        NR_CHECK(registration.DxgkDdiStartDevice(a, &start, &kernel, &sources, &children) ==
                 STATUS_NOT_SUPPORTED);
        NR_CHECK(sync_calls == before + 1 && !sync_contract_error);
        NR_CHECK(a->synchronization.valid_observation() == (mode == SyncMode::valid));
        NR_CHECK(!sources && !children && !a->started && !a->scheduler.running());
        NR_CHECK(!a->segment.ready() && segment_allocations == 0 && simulated_irql == PASSIVE_LEVEL);
    }
    // A later failed probe must not retain an earlier observation.
    kernel = {};
    NR_CHECK(registration.DxgkDdiStartDevice(a, &start, &kernel, &sources, &children) ==
             STATUS_NOT_SUPPORTED);
    NR_CHECK(a->synchronization.status == STATUS_INVALID_PARAMETER &&
             !a->synchronization.calls && a->synchronization.callback_irql == 0xff &&
             !a->synchronization.returned);
#endif
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
    DXGK_QUERYSEGMENTIN segment_in{};
    DXGK_QUERYSEGMENTOUT3 segment_out{};
    segment_out.NbSegment = 99;
    segment_out.PagingBufferSize = 123;
    query.Type = DXGKQAITYPE_QUERYSEGMENT3;
    query.pInputData = &segment_in;
    query.InputDataSize = sizeof(segment_in);
    query.pOutputData = &segment_out;
    query.OutputDataSize = sizeof(segment_out);
    NR_CHECK(registration.DxgkDdiQueryAdapterInfo(a, &query) == STATUS_DEVICE_NOT_READY);
    NR_CHECK(segment_out.NbSegment == 99);
    // Exercise dormant segment code through OS allocation/address test shims.
    // No real physical mapping, residency or kernel execution is established.
    segment_fail = true;
    NR_CHECK(a->segment.prepare() == STATUS_INSUFFICIENT_RESOURCES);
    NR_CHECK(!a->segment.ready() && !segment_memory && segment_frees == 0);
    segment_fail = false;
    const LONGLONG invalid_addresses[] = {-1, 1, MAXLONGLONG - 4095};
    for (const auto invalid_address : invalid_addresses) {
        segment_physical = invalid_address;
        NR_CHECK(a->segment.prepare() == STATUS_INVALID_ADDRESS);
        NR_CHECK(!a->segment.ready() && !segment_memory);
    }
    NR_CHECK(segment_frees == 3);
    segment_physical = 0x20000000;
    NR_CHECK(NT_SUCCESS(a->segment.prepare()));
    NR_CHECK(a->segment.prepare() == STATUS_INVALID_DEVICE_STATE);
    NR_CHECK(segment_allocations == 5 && segment_frees == 3);
    NR_CHECK(registration.DxgkDdiRemoveDevice(a) == STATUS_DEVICE_BUSY);
    NR_CHECK(a->segment.range(0, nr::kmd::Segment::bytes) == segment_memory);
    for (SIZE_T i = 0; i < nr::kmd::Segment::bytes; ++i)
        NR_CHECK(static_cast<UCHAR *>(segment_memory)[i] == 0);
    NR_CHECK(!a->segment.range(0, 0));
    NR_CHECK(!a->segment.range(~ULONGLONG(0), 1));
    NR_CHECK(!a->segment.range(nr::kmd::Segment::bytes - 1, 2));
    NR_CHECK(!a->segment.range(1, ~SIZE_T(0)));
    NR_CHECK(a->segment.range(nr::kmd::Segment::bytes - 1, 1));
    query.InputDataSize--;
    NR_CHECK(registration.DxgkDdiQueryAdapterInfo(a, &query) == STATUS_INVALID_PARAMETER);
    query.InputDataSize++;
    query.OutputDataSize--;
    NR_CHECK(registration.DxgkDdiQueryAdapterInfo(a, &query) == STATUS_BUFFER_TOO_SMALL);
    NR_CHECK(segment_out.NbSegment == 99);
    query.OutputDataSize++;
    NR_CHECK(NT_SUCCESS(registration.DxgkDdiQueryAdapterInfo(a, &query)));
    NR_CHECK(segment_out.NbSegment == 1 && segment_out.PagingBufferSize == 123);
    DXGK_SEGMENTDESCRIPTOR3 descriptor{};
    descriptor.Reserved = 999;
    segment_out.pSegmentDescriptor = &descriptor;
    segment_out.NbSegment = 0;
    NR_CHECK(registration.DxgkDdiQueryAdapterInfo(a, &query) == STATUS_INVALID_PARAMETER);
    NR_CHECK(descriptor.Reserved == 999 && segment_out.PagingBufferSize == 123);
    segment_out.NbSegment = 1;
    NR_CHECK(NT_SUCCESS(registration.DxgkDdiQueryAdapterInfo(a, &query)));
    NR_CHECK(descriptor.CpuTranslatedAddress.QuadPart == segment_physical);
    NR_CHECK(!descriptor.BaseAddress.QuadPart && !descriptor.Reserved && !descriptor.Flags.Aperture);
    NR_CHECK(descriptor.Flags.CpuVisible && descriptor.Flags.PopulatedFromSystemMemory);
    NR_CHECK(!descriptor.Flags.CacheCoherent);
    NR_CHECK(descriptor.Size == nr::kmd::Segment::bytes && descriptor.CommitLimit == descriptor.Size);
    NR_CHECK(!segment_out.PagingBufferSegmentId && segment_out.PagingBufferSize == PAGE_SIZE);
    NR_CHECK(segment_out.PagingBufferPrivateDataSize == nr::kmd::Paging::private_bytes);
    // Exercise the dormant allocation implementation without enabling StartDevice.
    // This is a local model state, never a live adapter registration.
    a->started = true;
    const auto fill_result = fill_test(a);
    if (fill_result) return fill_result;
    const auto paging_result = paging_test(a);
    if (paging_result) return paging_result;
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
    surface.allocation_bytes = nr::kmd::Segment::bytes + 4096;
    NR_CHECK(registration.DxgkDdiCreateAllocation(a, &create) == STATUS_INSUFFICIENT_RESOURCES);
    NR_CHECK(outstanding == 1 && !a->allocations && !allocations[0].hAllocation);
    surface = valid_surface;
    NR_CHECK(NT_SUCCESS(registration.DxgkDdiCreateAllocation(a, &create)));
    NR_CHECK(outstanding == 4 && a->allocations == 3);
    HANDLE handles[3]{};
    for (unsigned i = 0; i < 3; ++i) {
        handles[i] = allocations[i].hAllocation;
        NR_CHECK(handles[i] && allocations[i].Size == 12288);
        NR_CHECK(allocations[i].Flags.CpuVisible && !allocations[i].Flags.Cached);
    }
    NR_CHECK(registration.DxgkDdiStopDevice(a) == STATUS_DEVICE_BUSY);
    const auto allocation_result = allocation_test(a, handles);
    if (allocation_result) return allocation_result;
    DXGKARG_DESTROYALLOCATION destroy{};
    destroy.NumAllocations = 3; destroy.pAllocationList = handles;
    handles[2] = handles[1];
    NR_CHECK(registration.DxgkDdiDestroyAllocation(a, &destroy) == STATUS_INVALID_PARAMETER);
    NR_CHECK(outstanding == 4 && a->allocations == 3);
    handles[2] = allocations[2].hAllocation;
    NR_CHECK(NT_SUCCESS(registration.DxgkDdiDestroyAllocation(a, &destroy)));
    NR_CHECK(outstanding == 1 && !a->allocations);
    NR_CHECK(NT_SUCCESS(registration.DxgkDdiStopDevice(a)));
    NR_CHECK(!segment_memory && !a->segment.ready() && segment_frees == 4);
    NR_CHECK(!a->segment.range(0, 1));
    NR_CHECK(registration.DxgkDdiQueryAdapterInfo(a, &query) == STATUS_DEVICE_NOT_READY);
    NR_CHECK(NT_SUCCESS(registration.DxgkDdiStopDevice(a)) && segment_frees == 4);
    NR_CHECK(!segment_contract_error);
#if NR_KMD_LAB_WORKER_PROBE
    const auto worker_result = worker_probe_test(a);
    if (worker_result) return worker_result;
#endif
    const auto capacity_result = segment_capacity_test(a);
    if (capacity_result) return capacity_result;
    const auto submission_result = submission_test(a);
    if (submission_result) return submission_result;
    const auto engine_result = engine_test(a);
    if (engine_result) return engine_result;
    const auto recovery_result = recovery_test(a);
    if (recovery_result) return recovery_result;
    NR_CHECK(NT_SUCCESS(registration.DxgkDdiRemoveDevice(a)) && !outstanding);
    return 0;
#endif
}
