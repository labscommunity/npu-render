#pragma once
// Experimental backing for the explicitly loaded software DDI DLL. This does
// not register a graphics adapter or advertise graphics support for Intel's
// NPU.
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <d3dkmthk.h>
#include <d3dumddi.h>
#include <openvino/openvino.hpp>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>
#include <winternl.h>

namespace nr::ddi_shared {
inline constexpr UINT max_extent = 4096;
inline constexpr size_t max_region_bytes = 128 * 1024 * 1024;
inline std::atomic<bool> enabled{false};
inline std::atomic<D3DKMT_HANDLE> device{0};
inline std::atomic<uint64_t> created{0}, opened{0}, live_surfaces{0}, aliases{0}, live_aliases{0},
    cleanup_failures{0};
inline bool tracing() { return GetEnvironmentVariableW(L"NR_DDI_TRACE", nullptr, 0) > 0; }
template <class F> F real(const char *name) {
    static HMODULE module = LoadLibraryExW(L"gdi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto function = module ? GetProcAddress(module, name) : nullptr;
    if (!function)
        throw std::runtime_error("required system graphics thunk unavailable");
    return reinterpret_cast<F>(function);
}
template <class F> NTSTATUS guard(F f) noexcept {
    try {
        return f();
    } catch (...) {
        return NTSTATUS(0xc0000001);
    }
}
inline void require_nt(NTSTATUS s, const char *operation) {
    if (s < 0)
        throw std::runtime_error(std::string(operation) + " NTSTATUS " +
                                 std::to_string(uint32_t(s)));
}
inline NTSTATUS open_adapter(D3DKMT_HANDLE &handle, LUID *identity = nullptr) noexcept {
    return guard([&] {
        ov::Core core;
        const auto id = core.get_property(
#ifdef NR_DDI_NT_EXPERIMENT
            "GPU",
#else
            "NPU",
#endif
            ov::device::luid);
        D3DKMT_OPENADAPTERFROMLUID a{};
        static_assert(sizeof(a.AdapterLuid) == ov::device::LUID::MAX_LUID_SIZE);
        std::memcpy(&a.AdapterLuid, id.luid.data(), sizeof(a.AdapterLuid));
        const auto status =
            real<decltype(&D3DKMTOpenAdapterFromLuid)>("D3DKMTOpenAdapterFromLuid")(&a);
        if (status >= 0) {
            handle = a.hAdapter;
            if (identity)
                *identity = a.AdapterLuid;
        }
        return status;
    });
}
struct Request {
    uint64_t magic = 0x4e52534543544931ull;
    HANDLE section = nullptr;
    uint64_t bytes = 0;
};
inline NTSTATUS allocate(D3DKMT_CREATEALLOCATION *a, bool version2) noexcept {
    return guard([&]() -> NTSTATUS {
        if (!a || a->hDevice != device.load() || a->NumAllocations != 1 || a->hResource ||
            !a->Flags.CreateShared || a->Flags.NtSecuritySharing ||
            a->PrivateDriverDataSize != sizeof(Request) || !a->pPrivateDriverData)
            return NTSTATUS(0xc000000d);
        const auto request = *static_cast<const Request *>(a->pPrivateDriverData);
        if (request.magic != Request{}.magic || !request.section || !request.bytes ||
            request.bytes > max_region_bytes || request.bytes % 65536)
            return NTSTATUS(0xc000000d);
        D3DDDI_ALLOCATIONINFO2 item{};
        item.hSection = request.section;
        D3DKMT_CREATESTANDARDALLOCATION standard{};
        standard.Type = D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP;
        standard.ExistingHeapData.Size = request.bytes;
        D3DKMT_CREATEALLOCATION actual{};
        actual.hDevice = a->hDevice;
        actual.NumAllocations = 1;
        actual.pAllocationInfo2 = &item;
        actual.pStandardAllocation = &standard;
        actual.pPrivateRuntimeData = a->pPrivateRuntimeData;
        actual.PrivateRuntimeDataSize = a->PrivateRuntimeDataSize;
        actual.hPrivateRuntimeResourceHandle = a->hPrivateRuntimeResourceHandle;
        actual.Flags.CreateResource = 1;
        actual.Flags.CreateShared = 1;
        actual.Flags.CrossAdapter = 1;
        actual.Flags.StandardAllocation = 1;
        actual.Flags.ExistingSection = 1;
        const auto status =
            real<decltype(&D3DKMTCreateAllocation2)>("D3DKMTCreateAllocation2")(&actual);
        if (tracing())
            std::fprintf(stderr,
                         "create allocation: status=%08x requested_resource=%u "
                         "returned_resource=%s legacy_share=%s\n",
                         unsigned(status), unsigned(a->Flags.CreateResource),
                         actual.hResource ? "present" : "absent",
                         actual.hGlobalShare ? "present" : "absent");
        if (status >= 0) {
            a->hResource = actual.hResource;
            a->hGlobalShare = actual.hGlobalShare;
            if (version2)
                a->pAllocationInfo2[0].hAllocation = item.hAllocation;
            else
                a->pAllocationInfo[0].hAllocation = item.hAllocation;
        }
        return status;
    });
}
struct Metadata {
    uint64_t magic = 0x4e52544558545531ull;
    uint32_t version = 1, width = 0, height = 0, format = 0;
    uint64_t pixel_bytes = 0, region_bytes = 0;
    uint64_t reserved[3]{};
};
static_assert(sizeof(Metadata) == 64);
struct Surface {
    HANDLE section = nullptr, runtime_device = nullptr, runtime_resource = nullptr;
    D3DDDI_DEVICECALLBACKS callbacks{};
    D3DKMT_HANDLE kernel_device = 0, kernel_resource = 0, allocation = 0;
    D3DKMT_HANDLE alias_resource = 0;
    HANDLE alias_handle = nullptr;
    uint8_t *memory = nullptr;
    bool locked = false;
    size_t region_bytes = 0;
    Metadata metadata;
    Surface() { ++live_surfaces; }
    Surface(const Surface &) = delete;
    Surface &operator=(const Surface &) = delete;
    ~Surface() {
        if (alias_handle && !CloseHandle(alias_handle))
            ++cleanup_failures;
        if (alias_resource) {
            D3DKMT_DESTROYALLOCATION2 a{};
            a.hDevice = kernel_device;
            a.hResource = alias_resource;
            const auto status = guard([&] {
                return real<decltype(&D3DKMTDestroyAllocation2)>("D3DKMTDestroyAllocation2")(&a);
            });
            if (status < 0)
                ++cleanup_failures;
            --live_aliases;
        }
        if (locked) {
            D3DKMT_UNLOCK2 a{};
            a.hDevice = kernel_device;
            a.hAllocation = allocation;
            if (guard([&] { return real<decltype(&D3DKMTUnlock2)>("D3DKMTUnlock2")(&a); }) < 0)
                ++cleanup_failures;
        } else if (memory && !UnmapViewOfFile(memory))
            ++cleanup_failures;
        if (allocation && callbacks.pfnDeallocateCb) {
            D3DDDICB_DEALLOCATE a{};
            a.hResource = runtime_resource;
            if (FAILED(callbacks.pfnDeallocateCb(runtime_device, &a)))
                ++cleanup_failures;
        }
        if (section && !CloseHandle(section))
            ++cleanup_failures;
        --live_surfaces;
    }
    ov::Tensor input_tensor(ov::RemoteContext context) {
        if (!alias_handle) {
            if (alias_resource)
                throw std::runtime_error("previous NPU alias publication failed");
            // Legacy sharing serves D3D11. The NPU imports NT handles, so create
            // a process-local alias over these same locked/section-backed pages.
            // This alias must die before the original mapping is released.
            D3DDDI_ALLOCATIONINFO2 item{};
            item.pSystemMem = memory;
            D3DKMT_CREATESTANDARDALLOCATION standard{};
            standard.Type = D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP;
            standard.ExistingHeapData.Size = region_bytes;
            D3DKMT_CREATEALLOCATION a{};
            a.hDevice = kernel_device;
            a.NumAllocations = 1;
            a.pAllocationInfo2 = &item;
            a.pStandardAllocation = &standard;
            a.Flags.CreateResource = 1;
            a.Flags.CreateShared = 1;
            a.Flags.NtSecuritySharing = 1;
            a.Flags.CrossAdapter = 1;
            a.Flags.StandardAllocation = 1;
            a.Flags.ExistingSysMem = 1;
            require_nt(real<decltype(&D3DKMTCreateAllocation2)>("D3DKMTCreateAllocation2")(&a),
                       "create local NPU alias");
            alias_resource = a.hResource;
            ++aliases;
            ++live_aliases;
            OBJECT_ATTRIBUTES attributes{};
            attributes.Length = sizeof(attributes);
            require_nt(real<decltype(&D3DKMTShareObjects)>("D3DKMTShareObjects")(
                           1, &alias_resource, &attributes, GENERIC_ALL, &alias_handle),
                       "share local NPU alias");
        }
        return context.create_tensor(ov::element::u8, {1, metadata.height, metadata.width, 4},
                                     {{"MEM_TYPE", std::string("SHARED_BUF")},
                                      {"MEM_HANDLE", static_cast<void *>(alias_handle)},
                                      {"TENSOR_TYPE", std::string("BINDED")}});
    }
    void create(HANDLE rt_device, HANDLE rt_resource, const D3DDDI_DEVICECALLBACKS &cb, UINT width,
                UINT height, UINT format) {
        if (!enabled || !device || !rt_resource || !cb.pfnAllocateCb || !cb.pfnDeallocateCb ||
            !width || width > max_extent || width % 32 || !height || height > max_extent)
            throw std::runtime_error("shared surface is outside the explicit NPU contract");
        callbacks = cb;
        runtime_device = rt_device;
        runtime_resource = rt_resource;
        kernel_device = device;
        const size_t pixels = size_t(width) * height * 4;
        region_bytes = (pixels + sizeof(Metadata) + 65535) & ~size_t(65535);
        section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                     DWORD(region_bytes), nullptr);
        if (!section)
            throw std::runtime_error("shared section creation failed");
        memory =
            static_cast<uint8_t *>(MapViewOfFile(section, FILE_MAP_ALL_ACCESS, 0, 0, region_bytes));
        if (!memory)
            throw std::runtime_error("shared section mapping failed");
        metadata.width = width;
        metadata.height = height;
        metadata.format = format;
        metadata.pixel_bytes = pixels;
        metadata.region_bytes = region_bytes;
        std::memcpy(memory + region_bytes - sizeof(Metadata), &metadata, sizeof(metadata));
        Request request;
        request.section = section;
        request.bytes = region_bytes;
        D3DDDI_ALLOCATIONINFO item{};
        D3DDDICB_ALLOCATE a{};
        a.hResource = rt_resource;
        a.NumAllocations = 1;
        a.pAllocationInfo = &item;
        a.pPrivateDriverData = &request;
        a.PrivateDriverDataSize = sizeof(request);
        const HRESULT result = cb.pfnAllocateCb(rt_device, &a);
        if (tracing())
            std::fprintf(stderr, "allocate callback: hr=%08x resource=%s allocation=%s\n",
                         unsigned(result), a.hKMResource ? "present" : "absent",
                         item.hAllocation ? "present" : "absent");
        kernel_resource = a.hKMResource;
        allocation = item.hAllocation;
        if (FAILED(result))
            throw std::runtime_error("runtime shared allocation callback failed: " +
                                     std::to_string(uint32_t(result)));
        // The explicit software runtime on Windows 26200 retains hResource
        // internally but returns only hAllocation here. Deallocation still goes
        // through the original runtime resource identity, never a made-up handle.
        if (!allocation)
            throw std::runtime_error("runtime did not return shared allocation identity");
        ++created;
    }
    void open(HANDLE rt_device, HANDLE rt_resource, const D3DDDI_DEVICECALLBACKS &cb,
              D3DKMT_HANDLE resource, D3DKMT_HANDLE alloc) {
        callbacks = cb;
        runtime_device = rt_device;
        runtime_resource = rt_resource;
        kernel_device = device;
        kernel_resource = resource;
        allocation = alloc;
        if (!enabled || !kernel_device || !resource || !alloc || !cb.pfnDeallocateCb)
            throw std::runtime_error("shared resource has no valid kernel identity");
        D3DKMT_LOCK2 a{};
        a.hDevice = kernel_device;
        a.hAllocation = alloc;
        require_nt(real<decltype(&D3DKMTLock2)>("D3DKMTLock2")(&a), "lock imported surface");
        memory = static_cast<uint8_t *>(a.pData);
        locked = true;
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(memory, &info, sizeof(info)) || info.BaseAddress != memory ||
            info.RegionSize < 65536 || info.RegionSize > max_region_bytes ||
            info.RegionSize % 65536)
            throw std::runtime_error("shared resource mapping extent is invalid");
        region_bytes = info.RegionSize;
        std::memcpy(&metadata, memory + region_bytes - sizeof(metadata), sizeof(metadata));
        if (metadata.magic != Metadata{}.magic || metadata.version != 1 || !metadata.width ||
            metadata.width > max_extent || metadata.width % 32 || !metadata.height ||
            metadata.height > max_extent || metadata.region_bytes != region_bytes ||
            metadata.pixel_bytes != uint64_t(metadata.width) * metadata.height * 4 ||
            metadata.pixel_bytes > region_bytes - sizeof(metadata) || metadata.reserved[0] ||
            metadata.reserved[1] || metadata.reserved[2])
            throw std::runtime_error("shared texture metadata is incompatible");
        ++opened;
    }
};
} // namespace nr::ddi_shared
