// Public runtime callbacks for the isolated registered-adapter experiment.
// Pixel backing belongs to VidMm. No private heap substitutes for an allocation.
#pragma once
#pragma warning(push)
#pragma warning(disable: 4201) // Anonymous structures in Microsoft DDI headers.
#include <d3dumddi.h>
#pragma warning(pop)
#include <cstring>
#include <cstdint>
#include <memory>
#include "../kmd/contract.hpp"
namespace nr::registered_lab {
struct Error { HRESULT status; };
inline void check(HRESULT status) { if (FAILED(status)) throw Error{status}; }
inline void require(bool value) { if (!value) throw Error{E_INVALIDARG}; }
struct Counters {
    uint64_t allocations = 0, fills = 0, copies = 0, live_allocations = 0;
    uint64_t live_contexts = 0, cleanup_errors = 0, rejected = 0;
    uint64_t last_unsupported_offset = ~uint64_t(0);
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
        check(callbacks.pfnCreateContextCb(runtime, &args));
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
        D3DDDICB_RENDER args{};
        args.hContext = command.hContext;
        args.CommandLength = sizeof(data);
        args.NumAllocations = 1;
        // A failed callback must never leave old writable command pointers reusable.
        poisoned = true;
        check(callbacks.pfnRenderCb(runtime, &args));
        command.pCommandBuffer = args.pNewCommandBuffer;
        command.CommandBufferSize = args.NewCommandBufferSize;
        command.pAllocationList = args.pNewAllocationList;
        command.AllocationListSize = args.NewAllocationListSize;
        command.pPatchLocationList = args.pNewPatchLocationList;
        command.PatchLocationListSize = args.NewPatchLocationListSize;
        poisoned = false;
        ++stats.fills;
    }
};
struct Allocation {
    std::shared_ptr<Context> context;
    kernel_contract::Surface surface{};
    D3DKMT_HANDLE handle = 0;
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
        args.NumAllocations = 1; args.HandleList = &handle;
        if (FAILED(context->callbacks.pfnDeallocateCb(context->runtime, &args))) ++context->stats.cleanup_errors;
        --context->stats.live_allocations;
    }
    void create(UINT width, UINT height, UINT format) {
        require(!handle && context && context->command.hContext && width && width <= 1024 && height && height <= 1024);
        surface.width = width; surface.height = height; surface.format = format;
        surface.row_pitch = (width * 4 + 255) & ~255u;
        surface.allocation_bytes = (uint64_t(surface.row_pitch) * height + 4095) & ~uint64_t(4095);
        require(kernel_contract::valid(surface));
        D3DDDI_ALLOCATIONINFO item{};
        item.pPrivateDriverData = &surface; item.PrivateDriverDataSize = sizeof(surface);
        D3DDDICB_ALLOCATE args{};
        // Device-associated, unshared allocation. Shared resources need a
        // separately implemented resource identity/opening contract.
        args.NumAllocations = 1; args.pAllocationInfo = &item;
        check(context->callbacks.pfnAllocateCb(context->runtime, &args));
        require(item.hAllocation != 0);
        handle = item.hAllocation;
        ++context->stats.allocations; ++context->stats.live_allocations;
    }
    void *lock(bool read_only) {
        require(handle && !locked);
        D3DDDICB_LOCK args{};
        args.hAllocation = handle;
        args.Flags.LockEntire = args.Flags.DonotEvict = 1;
        args.Flags.ReadOnly = read_only;
        args.Flags.WriteOnly = !read_only;
        check(context->callbacks.pfnLockCb(context->runtime, &args));
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
