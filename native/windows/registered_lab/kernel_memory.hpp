// Public runtime callbacks for the isolated registered-adapter experiment.
// Pixel backing belongs to VidMm. No private heap substitutes for an allocation.
#pragma once
#pragma warning(push)
#pragma warning(disable: 4201) // Anonymous structures in Microsoft DDI headers.
#include <d3dumddi.h>
#pragma warning(pop)
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <memory>
#include "../kmd/contract.hpp"
namespace nr::registered_lab {
struct Error { HRESULT status; };
inline void check(HRESULT status, const char *operation = "runtime") {
    if (FAILED(status)) {
        std::fprintf(stderr, "nr_fill_umd: %s HRESULT=%08lx\n", operation, ULONG(status));
        throw Error{status};
    }
}
inline void require(bool value) { if (!value) throw Error{E_INVALIDARG}; }
struct Counters {
    uint64_t allocations = 0, fills = 0, copies = 0, live_allocations = 0;
    uint64_t live_contexts = 0, cleanup_errors = 0, rejected = 0;
    uint64_t last_unsupported_offset = ~uint64_t(0);
    uint64_t submitted_copies = 0;
    uint64_t upload_bytes = 0;
    uint64_t draws = 0, executions = 0, inference_input_bytes = 0, inference_output_bytes = 0;
    uint64_t cpu_executions = 0, gpu_executions = 0, npu_executions = 0;
    char last_execution_device[32]{};
    uint64_t present_requests = 0, accepted_present_callbacks = 0;
    uint64_t remote_npu_executions = 0;
};
struct Context {
    HANDLE runtime = nullptr;
    D3DDDI_DEVICECALLBACKS callbacks{};
    D3DDDICB_CREATECONTEXT command{};
    Counters &stats;
    bool poisoned = false;
    explicit Context(Counters &c) : stats(c) {}
    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;
    ~Context() {
        if (command.hContext) {
            D3DDDICB_DESTROYCONTEXT args{command.hContext};
            if (FAILED(callbacks.pfnDestroyContextCb(runtime, &args))) ++stats.cleanup_errors;
            --stats.live_contexts;
        }
    }
    void create(HANDLE rt, const D3DDDI_DEVICECALLBACKS &cb) {
        require(!command.hContext && rt && cb.pfnCreateContextCb && cb.pfnDestroyContextCb &&
                cb.pfnAllocateCb && cb.pfnDeallocateCb && cb.pfnLockCb && cb.pfnUnlockCb && cb.pfnRenderCb);
        runtime = rt; callbacks = cb;
        D3DDDICB_CREATECONTEXT args{};
        args.EngineAffinity = 1;
        check(callbacks.pfnCreateContextCb(runtime, &args), "CreateContextCb");
        require(args.hContext != nullptr);
        command = args;
        ++stats.live_contexts;
    }
    void fill(D3DKMT_HANDLE allocation, ULONG pattern) {
        require(!poisoned && command.hContext && allocation && command.pCommandBuffer &&
                command.CommandBufferSize >= sizeof(kernel_contract::FillSurface) &&
                command.pAllocationList && command.AllocationListSize);
        kernel_contract::FillSurface data{};
        data.pattern = pattern;
        std::memcpy(command.pCommandBuffer, &data, sizeof(data));
        command.pAllocationList[0] = {};
        command.pAllocationList[0].hAllocation = allocation;
        command.pAllocationList[0].WriteOperation = 1;
        submit(sizeof(data), 1);
        ++stats.fills;
    }
    void copy(D3DKMT_HANDLE source, D3DKMT_HANDLE destination) {
        require(!poisoned && command.hContext && source && destination && source != destination &&
                command.pCommandBuffer && command.CommandBufferSize >= sizeof(kernel_contract::CopySurface) &&
                command.pAllocationList && command.AllocationListSize >= 2);
        const kernel_contract::CopySurface data{};
        std::memcpy(command.pCommandBuffer, &data, sizeof(data));
        command.pAllocationList[0] = {}; command.pAllocationList[0].hAllocation = source;
        command.pAllocationList[1] = {}; command.pAllocationList[1].hAllocation = destination;
        command.pAllocationList[1].WriteOperation = 1;
        submit(sizeof(data), 2);
        ++stats.submitted_copies;
    }
    void submit(UINT bytes, UINT allocations) {
        D3DDDICB_RENDER args{};
        args.hContext = command.hContext;
        args.CommandLength = bytes;
        args.NumAllocations = allocations;
        // A failed callback must never leave old writable command pointers reusable.
        poisoned = true;
        check(callbacks.pfnRenderCb(runtime, &args), "RenderCb");
        command.pCommandBuffer = args.pNewCommandBuffer;
        command.CommandBufferSize = args.NewCommandBufferSize;
        command.pAllocationList = args.pNewAllocationList;
        command.AllocationListSize = args.NewAllocationListSize;
        command.pPatchLocationList = args.pNewPatchLocationList;
        command.PatchLocationListSize = args.NewPatchLocationListSize;
        poisoned = false;
    }
};
struct Allocation {
    std::shared_ptr<Context> context;
    kernel_contract::Surface surface{};
    D3DKMT_HANDLE handle = 0;
    HANDLE runtime_resource = nullptr;
    void *mapping = nullptr;
    bool locked = false;
    explicit Allocation(std::shared_ptr<Context> c) : context(std::move(c)) {}
    Allocation(const Allocation &) = delete;
    Allocation &operator=(const Allocation &) = delete;
    HRESULT unlock() noexcept {
        if (!locked) return S_OK;
        MemoryBarrier(); // Drain writes to write-combined VidMm mappings before release.
        D3DDDICB_UNLOCK args{1, &handle};
        const auto status = context->callbacks.pfnUnlockCb(context->runtime, &args);
        if (SUCCEEDED(status)) { locked = false; mapping = nullptr; }
        return status;
    }
    ~Allocation() {
        if (!handle) return;
        if (FAILED(unlock())) ++context->stats.cleanup_errors;
        D3DDDICB_DEALLOCATE args{};
        if (runtime_resource) args.hResource = runtime_resource;
        else { args.NumAllocations = 1; args.HandleList = &handle; }
        if (FAILED(context->callbacks.pfnDeallocateCb(context->runtime, &args))) ++context->stats.cleanup_errors;
        --context->stats.live_allocations;
    }
    void create(UINT width, UINT height, UINT format, HANDLE resource = nullptr) {
        require(!handle && context && context->command.hContext && width && width <= 8192 && height && height <= 8192);
        const bool alpha = format == 65; // A8_UNORM, packed bytes in private VidMm backing.
        require(!alpha || !resource);
        surface.width = width; surface.height = height; surface.format = format;
        surface.row_pitch = (width * (alpha ? 1 : 4) + 255) & ~255u;
        surface.allocation_bytes = (uint64_t(surface.row_pitch) * height + 4095) & ~uint64_t(4095);
        // The kernel only owns/copies bytes. Pack four alpha texels into one
        // private 32-bit storage unit, retaining the logical format in the UMD.
        // Such backing is never shared, presented or opened as a color surface.
        auto storage = surface;
        if (alpha) { storage.width = (width + 3) / 4; storage.format = 28; }
        require(kernel_contract::valid(storage));
        D3DDDI_ALLOCATIONINFO item{};
        item.pPrivateDriverData = &storage; item.PrivateDriverDataSize = sizeof(storage);
        D3DDDICB_ALLOCATE args{};
        args.hResource = resource;
        if (resource) { args.pPrivateDriverData = &surface; args.PrivateDriverDataSize = sizeof(surface); }
        args.NumAllocations = 1; args.pAllocationInfo = &item;
        check(context->callbacks.pfnAllocateCb(context->runtime, &args), "AllocateCb");
        require(item.hAllocation != 0);
        handle = item.hAllocation;
        runtime_resource = resource;
        ++context->stats.allocations; ++context->stats.live_allocations;
    }
    void open(HANDLE resource, D3DKMT_HANDLE allocation, const kernel_contract::Surface &description) {
        require(!handle && context && context->command.hContext && resource && allocation &&
                kernel_contract::valid(description) && !description.flags);
        surface = description; runtime_resource = resource; handle = allocation;
        ++context->stats.live_allocations;
    }
    void *lock(bool read_only) {
        require(handle && !locked);
        D3DDDICB_LOCK args{};
        args.hAllocation = handle;
        args.Flags.LockEntire = args.Flags.DonotEvict = 1;
        args.Flags.ReadOnly = read_only;
        args.Flags.WriteOnly = !read_only;
        check(context->callbacks.pfnLockCb(context->runtime, &args), "LockCb");
        locked = true; mapping = args.pData;
        require(mapping && args.hAllocation == handle);
        return mapping;
    }
};
struct Mapping {
    Allocation &allocation;
    void *data;
    Mapping(Allocation &a, bool read) : allocation(a), data(a.lock(read)) {}
    Mapping(const Mapping &) = delete;
    Mapping &operator=(const Mapping &) = delete;
    ~Mapping() { if (FAILED(allocation.unlock())) ++allocation.context->stats.cleanup_errors; }
    void finish() { check(allocation.unlock()); }
};
} // namespace nr::registered_lab
