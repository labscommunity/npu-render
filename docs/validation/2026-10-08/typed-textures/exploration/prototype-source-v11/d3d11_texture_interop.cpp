// Graphics-owned, row-major texture -> NPU output -> native D3D11 consumer.
// This probes resource interoperability; it does not select a DWM adapter.
#include "../windows/blend_model.hpp"
#include "../windows/d3d12_buffers.hpp"
#include "../windows/ddi/contract.hpp"
#include <d3d11_1.h>
#include <d3d11on12.h>
#include <d3d11sdklayers.h>
#include <d3d12compatibility.h>
#include <d3d12sdklayers.h>
#include <d3dkmthk.h>
#include <dbghelp.h>
#include <winternl.h>

LONG WINAPI diagnostic_exception(EXCEPTION_POINTERS *exception) {
    if (exception->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    static LONG active = 0;
    if (InterlockedIncrement(&active) != 1)
        return EXCEPTION_CONTINUE_SEARCH;
    const auto process = GetCurrentProcess();
    CONTEXT context = *exception->ContextRecord;
    std::fprintf(stderr, "{\"access_violation\":true,\"null_instruction\":%s}\n",
                 context.Rip ? "false" : "true");
    if (!context.Rip) {
        SIZE_T read = 0;
        if (ReadProcessMemory(process, reinterpret_cast<void *>(context.Rsp), &context.Rip,
                              sizeof(context.Rip), &read) &&
            read == sizeof(context.Rip))
            context.Rsp += sizeof(context.Rip);
    }
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS);
    SymInitialize(process, nullptr, TRUE);
    STACKFRAME64 frame{};
    frame.AddrPC = {context.Rip, 0, AddrModeFlat};
    frame.AddrStack = {context.Rsp, 0, AddrModeFlat};
    frame.AddrFrame = {context.Rbp, 0, AddrModeFlat};
    for (unsigned i = 0; i < 16 && frame.AddrPC.Offset; ++i) {
        HMODULE module = nullptr;
        char path[MAX_PATH]{};
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(frame.AddrPC.Offset), &module);
        if (module)
            GetModuleFileNameA(module, path, MAX_PATH);
        const char *name = std::strrchr(path, '\\');
        std::fprintf(stderr, "{\"stack_module\":\"%s\",\"offset\":%llu}\n", name ? name + 1 : path,
                     frame.AddrPC.Offset - reinterpret_cast<uintptr_t>(module));
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &context,
                         nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
    }
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

void ntcheck(NTSTATUS status, const char *operation) {
    std::cout << "{\"operation\":" << quote(operation) << ",\"ntstatus\":" << uint32_t(status)
              << "}\n"
              << std::flush;
    if (status < 0)
        throw std::runtime_error(std::string(operation) + " failed");
}
struct TextureAlias {
    D3DKMT_HANDLE adapter = 0, device = 0, resource = 0, allocation = 0, alias = 0;
    HANDLE shared = nullptr;
    bool locked = false;
    ~TextureAlias() {
        if (shared)
            CloseHandle(shared);
        auto destroy = [&](D3DKMT_HANDLE r) {
            if (r) {
                D3DKMT_DESTROYALLOCATION2 a{};
                a.hDevice = device;
                a.hResource = r;
                D3DKMTDestroyAllocation2(&a);
            }
        };
        destroy(alias);
        if (locked) {
            D3DKMT_UNLOCK2 a{};
            a.hDevice = device;
            a.hAllocation = allocation;
            D3DKMTUnlock2(&a);
        }
        destroy(resource);
        if (device) {
            D3DKMT_DESTROYDEVICE a{};
            a.hDevice = device;
            D3DKMTDestroyDevice(&a);
        }
        if (adapter) {
            D3DKMT_CLOSEADAPTER a{};
            a.hAdapter = adapter;
            D3DKMTCloseAdapter(&a);
        }
    }
    void create(ov::Core &core, HANDLE texture, size_t bytes, bool gpu_map, bool legacy) {
        if (!bytes || bytes > 128 * 1024 * 1024 || bytes % 65536)
            throw std::runtime_error("unexpected texture allocation size");
        D3DKMT_OPENADAPTERFROMLUID ad{};
        const auto id = core.get_property(gpu_map ? "GPU" : "NPU", ov::device::luid);
        std::memcpy(&ad.AdapterLuid, id.luid.data(), sizeof(ad.AdapterLuid));
        ntcheck(D3DKMTOpenAdapterFromLuid(&ad),
                gpu_map ? "alias OpenAdapterFromLuid(GPU)" : "alias OpenAdapterFromLuid(NPU)");
        adapter = ad.hAdapter;
        D3DKMT_CREATEDEVICE dev{};
        dev.hAdapter = adapter;
        ntcheck(D3DKMTCreateDevice(&dev), "alias CreateDevice");
        device = dev.hDevice;
        D3DKMT_QUERYRESOURCEINFOFROMNTHANDLE query{};
        query.hDevice = device;
        query.hNtHandle = texture;
        if (legacy) {
            D3DKMT_QUERYRESOURCEINFO q{};
            q.hDevice = device;
            q.hGlobalShare = D3DKMT_HANDLE(uintptr_t(texture));
            ntcheck(D3DKMTQueryResourceInfo(&q), "alias QueryResourceInfo legacy");
            query.NumAllocations = q.NumAllocations;
            query.PrivateRuntimeDataSize = q.PrivateRuntimeDataSize;
            query.ResourcePrivateDriverDataSize = q.ResourcePrivateDriverDataSize;
            query.TotalPrivateDriverDataSize = q.TotalPrivateDriverDataSize;
        } else
            ntcheck(D3DKMTQueryResourceInfoFromNtHandle(&query),
                    "alias QueryResourceInfoFromNtHandle");
        if (query.NumAllocations != 1 || query.PrivateRuntimeDataSize > 1048576 ||
            query.ResourcePrivateDriverDataSize > 1048576 ||
            query.TotalPrivateDriverDataSize > 1048576)
            throw std::runtime_error("unexpected texture resource metadata extent");
        std::vector<uint8_t> rt(query.PrivateRuntimeDataSize),
            driver(query.ResourcePrivateDriverDataSize), items(query.TotalPrivateDriverDataSize);
        D3DDDI_OPENALLOCATIONINFO2 item{};
        D3DKMT_OPENRESOURCEFROMNTHANDLE open{};
        open.hDevice = device;
        open.hNtHandle = texture;
        open.NumAllocations = 1;
        open.pOpenAllocationInfo2 = &item;
        open.pPrivateRuntimeData = rt.empty() ? nullptr : rt.data();
        open.PrivateRuntimeDataSize = UINT(rt.size());
        open.pResourcePrivateDriverData = driver.empty() ? nullptr : driver.data();
        open.ResourcePrivateDriverDataSize = UINT(driver.size());
        open.pTotalPrivateDriverDataBuffer = items.empty() ? nullptr : items.data();
        open.TotalPrivateDriverDataBufferSize = UINT(items.size());
        if (legacy) {
            D3DKMT_OPENRESOURCE o{};
            o.hDevice = device;
            o.hGlobalShare = D3DKMT_HANDLE(uintptr_t(texture));
            o.NumAllocations = 1;
            o.pOpenAllocationInfo2 = &item;
            o.pPrivateRuntimeData = open.pPrivateRuntimeData;
            o.PrivateRuntimeDataSize = open.PrivateRuntimeDataSize;
            o.pResourcePrivateDriverData = open.pResourcePrivateDriverData;
            o.ResourcePrivateDriverDataSize = open.ResourcePrivateDriverDataSize;
            o.pTotalPrivateDriverDataBuffer = open.pTotalPrivateDriverDataBuffer;
            o.TotalPrivateDriverDataBufferSize = open.TotalPrivateDriverDataBufferSize;
            ntcheck(D3DKMTOpenResource2(&o), "alias OpenResource2 legacy");
            open.hResource = o.hResource;
        } else
            ntcheck(D3DKMTOpenResourceFromNtHandle(&open), "alias OpenResourceFromNtHandle");
        resource = open.hResource;
        allocation = item.hAllocation;
        D3DKMT_LOCK2 lock{};
        lock.hDevice = device;
        lock.hAllocation = allocation;
        ntcheck(D3DKMTLock2(&lock), "alias Lock2 texture");
        locked = true;
        MEMORY_BASIC_INFORMATION memory{};
        if (!lock.pData || !VirtualQuery(lock.pData, &memory, sizeof(memory)) ||
            memory.State != MEM_COMMIT || (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            throw std::runtime_error("texture mapping is not accessible");
        const size_t offset = uintptr_t(lock.pData) - uintptr_t(memory.BaseAddress);
        std::cout << "{\"texture_allocation_bytes\":" << bytes
                  << ",\"mapped_region_bytes\":" << memory.RegionSize
                  << ",\"mapped_offset\":" << offset << "}\n"
                  << std::flush;
        if (offset > memory.RegionSize || bytes > memory.RegionSize - offset)
            throw std::runtime_error("texture mapping is smaller than the allocation");
        D3DDDI_ALLOCATIONINFO2 info{};
        info.pSystemMem = lock.pData;
        D3DKMT_CREATESTANDARDALLOCATION standard{};
        standard.Type = D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP;
        standard.ExistingHeapData.Size = bytes;
        D3DKMT_CREATEALLOCATION create{};
        create.hDevice = device;
        create.NumAllocations = 1;
        create.pAllocationInfo2 = &info;
        create.pStandardAllocation = &standard;
        create.Flags.CreateResource = create.Flags.CreateShared = create.Flags.NtSecuritySharing =
            1;
        create.Flags.CrossAdapter = create.Flags.StandardAllocation = create.Flags.ExistingSysMem =
            1;
        ntcheck(D3DKMTCreateAllocation2(&create), "alias CreateAllocation2 existing texture pages");
        alias = create.hResource;
        OBJECT_ATTRIBUTES attributes{};
        attributes.Length = sizeof(attributes);
        ntcheck(D3DKMTShareObjects(1, &alias, &attributes, GENERIC_ALL, &shared),
                "alias ShareObjects");
    }
};

struct Diagnostics {
    ComPtr<ID3D11InfoQueue> eleven;
    ComPtr<ID3D12InfoQueue> twelve;
    void dump() {
        if (eleven) {
            const auto count = eleven->GetNumStoredMessagesAllowedByRetrievalFilter();
            for (UINT64 i = 0; i < std::min<UINT64>(count, 64); ++i) {
                SIZE_T bytes = 0;
                eleven->GetMessage(i, nullptr, &bytes);
                std::vector<uint8_t> storage(bytes);
                auto *m = reinterpret_cast<D3D11_MESSAGE *>(storage.data());
                if (SUCCEEDED(eleven->GetMessage(i, m, &bytes)))
                    std::cout << "{\"api\":\"D3D11\",\"severity\":" << m->Severity
                              << ",\"message\":" << quote(m->pDescription) << "}\n";
            }
            eleven->ClearStoredMessages();
        }
        if (twelve) {
            const auto count = twelve->GetNumStoredMessagesAllowedByRetrievalFilter();
            for (UINT64 i = 0; i < std::min<UINT64>(count, 64); ++i) {
                SIZE_T bytes = 0;
                twelve->GetMessage(i, nullptr, &bytes);
                std::vector<uint8_t> storage(bytes);
                auto *m = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
                if (SUCCEEDED(twelve->GetMessage(i, m, &bytes)))
                    std::cout << "{\"api\":\"D3D12\",\"severity\":" << m->Severity
                              << ",\"message\":" << quote(m->pDescription) << "}\n";
            }
            twelve->ClearStoredMessages();
        }
        std::cout.flush();
    }
};

void ddi_import(const std::string &dll, HANDLE texture) {
    struct Module {
        HMODULE value = nullptr;
        ~Module() {
            if (value)
                FreeLibrary(value);
        }
    } module;
    module.value = LoadLibraryExA(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module.value)
        throw std::runtime_error("DDI DLL load failed");
    auto configure =
        reinterpret_cast<NrDdiConfigure>(GetProcAddress(module.value, "nr_ddi_configure"));
    auto get = reinterpret_cast<NrDdiGetStats>(GetProcAddress(module.value, "nr_ddi_get_stats"));
    if (!configure || !get || !configure("NPU"))
        throw std::runtime_error("DDI configuration failed");
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_10_0;
    std::cout << "{\"ddi_stage\":\"create_device\"}\n" << std::flush;
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_SOFTWARE, module.value,
                            D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                            &level, 1, D3D11_SDK_VERSION, &device, nullptr, &context),
          "DDI device");
    ComPtr<ID3D11Device1> device1;
    std::cout << "{\"ddi_stage\":\"query_device1\"}\n" << std::flush;
    check(device.As(&device1), "DDI device1");
    ComPtr<ID3D11Texture2D> opened;
    std::cout << "{\"ddi_stage\":\"open_nt_texture\"}\n" << std::flush;
    const auto status = device1->OpenSharedResource1(texture, IID_PPV_ARGS(&opened));
    NrDdiStats stats;
    get(&stats);
    std::cout << "{\"ddi_nt_import_hresult\":" << uint32_t(status)
              << ",\"ddi_error\":" << quote(stats.last_error) << "}\n"
              << std::flush;
    check(status, "DDI OpenSharedResource1");
    opened.Reset();
    context->ClearState();
    context->Flush();
}

int main(int argc, char **argv) {
    AddVectoredExceptionHandler(0, diagnostic_exception);
    struct Stage : std::string {
        using std::string::string;
        Stage &operator=(const char *value) {
            std::string::operator=(value);
            std::cout << "{\"stage\":" << quote(*this) << "}\n" << std::flush;
            return *this;
        }
    } stage = "arguments";
    Diagnostics diagnostics;
    try {
        bool debug = false, buffer_control = false, writable_texture = false, mapped_alias = false,
             aligned_texture = false, gpu_map = false, legacy = false, render_target = false;
        UINT width = 64, height = 48;
        std::string ddi;
        for (int i = 1; i < argc; ++i) {
            const std::string argument(argv[i]);
            if (argument == "--debug")
                debug = true;
            else if (argument == "--buffer-control")
                buffer_control = true;
            else if (argument == "--writable-texture")
                writable_texture = true;
            else if (argument == "--mapped-alias")
                mapped_alias = true;
            else if (argument == "--aligned-texture")
                aligned_texture = true;
            else if (argument == "--gpu-map")
                gpu_map = true;
            else if (argument == "--legacy")
                legacy = true;
            else if (argument == "--render-target")
                render_target = true;
            else if (argument == "--ddi-import" && i + 1 < argc)
                ddi = argv[++i];
            else if ((argument == "--width" || argument == "--height") && i + 1 < argc) {
                const std::string value(argv[++i]);
                if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
                    throw std::runtime_error("invalid dimension");
                const auto n = std::stoull(value);
                if (!n || n > 4096)
                    throw std::runtime_error("dimension outside 1..4096");
                if (argument == "--width")
                    width = UINT(n);
                else
                    height = UINT(n);
            } else
                throw std::runtime_error(
                    "use [--debug] [--buffer-control | --writable-texture] [--mapped-alias]");
        }
        if (buffer_control && (writable_texture || mapped_alias))
            throw std::runtime_error("texture variant and buffer control are exclusive");
        if (gpu_map && !mapped_alias)
            throw std::runtime_error("--gpu-map requires --mapped-alias");
        if (aligned_texture)
            height = 256;
        if (width % 64)
            throw std::runtime_error("texture width must align to 256-byte rows");
        const UINT frames = 16;
        const size_t bytes = size_t(width) * height * 4;
        stage = "exact_gpu_device";
        ov::Core core;
        const auto id = core.get_property("GPU", ov::device::luid);
        LUID luid{};
        static_assert(sizeof(luid) == ov::device::LUID::MAX_LUID_SIZE);
        std::memcpy(&luid, id.luid.data(), sizeof(luid));
        ComPtr<IDXGIFactory4> factory;
        ComPtr<IDXGIAdapter1> adapter;
        check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
        check(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter)), "exact GPU adapter");
        if (debug) {
            ComPtr<ID3D12Debug> layer;
            check(D3D12GetDebugInterface(IID_PPV_ARGS(&layer)), "D3D12 debug layer");
            layer->EnableDebugLayer();
        }
        ComPtr<ID3D12Device> gpu;
        check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&gpu)),
              "D3D12 device");
        gpu.As(&diagnostics.twelve);
        ComPtr<ID3D11Device> eleven;
        ComPtr<ID3D11DeviceContext> context;
        const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        check(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                debug ? D3D11_CREATE_DEVICE_DEBUG : 0, &level, 1, D3D11_SDK_VERSION,
                                &eleven, nullptr, &context),
              "D3D11 device");
        eleven.As(&diagnostics.eleven);
        ComPtr<ID3D12CompatibilityDevice> compatibility;
        check(gpu.As(&compatibility), "D3D12 compatibility interface");
        stage = "create_typed_cross_adapter_texture";
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
        desc.Format = writable_texture ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags =
            D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
        D3D11_RESOURCE_FLAGS flags{};
        flags.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (writable_texture) {
            desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            flags.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
        }
        if (render_target) {
            desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            flags.BindFlags |= D3D11_BIND_RENDER_TARGET;
        }
        flags.MiscFlags =
            D3D11_RESOURCE_MISC_SHARED | (legacy ? 0 : D3D11_RESOURCE_MISC_SHARED_NTHANDLE);
        ComPtr<ID3D12Resource> texture;
        Handle shared;
        std::unique_ptr<SharedBuffer> buffer;
        ComPtr<ID3D12Resource> buffer_readback;
        std::unique_ptr<Transfers> transfers;
        ComPtr<ID3D11Texture2D> imported, readback;
        HANDLE output_handle = nullptr;
        if (buffer_control) {
            buffer = std::make_unique<SharedBuffer>(gpu.Get(), bytes);
            output_handle = buffer->shared.value;
            buffer_readback = staging(gpu.Get(), bytes, D3D12_HEAP_TYPE_READBACK);
            transfers = std::make_unique<Transfers>(gpu.Get());
        } else {
            check(compatibility->CreateSharedResource(
                      &heap, D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER, &desc,
                      D3D12_RESOURCE_STATE_COMMON, nullptr, &flags,
                      legacy ? D3D12_COMPATIBILITY_SHARED_FLAG_NON_NT_HANDLE
                             : D3D12_COMPATIBILITY_SHARED_FLAG_NONE,
                      nullptr, nullptr, IID_PPV_ARGS(&texture)),
                  "CreateSharedResource(row-major texture)");
            if (legacy) {
                check(compatibility->ReflectSharedProperties(
                          texture.Get(), D3D12_REFLECT_SHARED_PROPERTY_NON_NT_SHARED_HANDLE,
                          &output_handle, sizeof(output_handle)),
                      "legacy texture handle");
            } else {
                check(gpu->CreateSharedHandle(texture.Get(), nullptr, GENERIC_ALL, nullptr,
                                              &shared.value),
                      "CreateSharedHandle(texture)");
                output_handle = shared.value;
            }
            stage = "native_d3d11_open";
            ComPtr<ID3D11Device1> eleven1;
            check(eleven.As(&eleven1), "D3D11.1 device");
            check(legacy ? eleven->OpenSharedResource(output_handle, IID_PPV_ARGS(&imported))
                         : eleven1->OpenSharedResource1(output_handle, IID_PPV_ARGS(&imported)),
                  "OpenSharedResource1(typed texture)");
            D3D11_TEXTURE2D_DESC actual{};
            imported->GetDesc(&actual);
            if (actual.Width != width || actual.Height != height || actual.Format != desc.Format ||
                actual.MipLevels != 1 || actual.ArraySize != 1 || actual.SampleDesc.Count != 1)
                throw std::runtime_error("D3D11 resource description mismatch");
            actual.Usage = D3D11_USAGE_STAGING;
            actual.BindFlags = actual.MiscFlags = 0;
            actual.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            check(eleven->CreateTexture2D(&actual, nullptr, &readback), "D3D11 readback");
        }
        diagnostics.dump();
        if (!ddi.empty()) {
            if (legacy || buffer_control)
                throw std::runtime_error("DDI NT import needs an NT texture");
            stage = "ddi_nt_texture_import";
            ddi_import(ddi, output_handle);
        }
        TextureAlias alias;
        if (mapped_alias) {
            stage = "npu_texture_page_alias";
            const auto allocation = gpu->GetResourceAllocationInfo(0, 1, &desc);
            alias.create(core, output_handle, allocation.SizeInBytes, gpu_map, legacy);
            output_handle = alias.shared;
        }
        stage = "npu_import_and_compile";
        const auto compile_started = Clock::now();
        auto compiled =
            core.compile_model(blend_model(width, height, true, 4, "convolution"), "NPU",
                               ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY));
        std::cout
            << "{\"compile_ms\":"
            << std::chrono::duration<double, std::milli>(Clock::now() - compile_started).count()
            << "}\n"
            << std::flush;
        const auto devices = compiled.get_property(ov::execution_devices);
        if (devices.empty() || std::any_of(devices.begin(), devices.end(), [](const auto &d) {
                return d != "NPU" && d.rfind("NPU.", 0) != 0;
            }))
            throw std::runtime_error("NPU execution identity mismatch");
        auto output = compiled.get_context().create_tensor(
            ov::element::u8, {1, height, width, 4},
            {{"MEM_TYPE", std::string("SHARED_BUF")},
             {"MEM_HANDLE", static_cast<void *>(output_handle)},
             {"TENSOR_TYPE", std::string("BINDED")}});
        auto request = compiled.create_infer_request();
        request.set_output_tensor(output);
        auto background = request.get_input_tensor(0), foreground = request.get_input_tensor(1);
        // Keep the oracle in ordinary CPU memory. Device input allocations can
        // be write-combined; reading them byte by byte is a different workload.
        std::vector<uint8_t> background_fixture(bytes), foreground_fixture(bytes);
        auto *bg = background_fixture.data();
        auto *fg = foreground_fixture.data();
        MEMORY_BASIC_INFORMATION input_memory{};
        VirtualQuery(background.data(), &input_memory, sizeof(input_memory));
        std::cout << "{\"input_memory_protection\":" << input_memory.Protect << "}\n" << std::flush;
        unsigned maximum_error = 0;
        for (UINT frame = 0; frame < frames; ++frame) {
            for (size_t i = 0; i < bytes / 4; ++i) {
                const auto a = uint8_t((i * 19 + frame * 31) % 256);
                for (size_t c = 0; c < 4; ++c) {
                    bg[i * 4 + c] = c == 3 ? 255 : uint8_t((i + c * 37 + frame * 17) % 256);
                    fg[i * 4 + c] =
                        c == 3 ? a : uint8_t(((i + c * 23 + frame * 7) % 256) * a / 255);
                }
            }
            stage = "npu_write_texture";
            std::memcpy(background.data(), bg, bytes);
            std::memcpy(foreground.data(), fg, bytes);
            const auto infer_started = Clock::now();
            request.infer(); // Complete the NPU write before native GPU consumption.
            const auto infer_finished = Clock::now();
            stage = "native_d3d11_readback";
            D3D11_MAPPED_SUBRESOURCE map{};
            if (buffer_control) {
                stage = "native_d3d12_buffer_readback";
                transfers->begin();
                transfers->barrier(buffer->resource.Get(), D3D12_RESOURCE_STATE_COMMON,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE);
                transfers->list->CopyBufferRegion(buffer_readback.Get(), 0, buffer->resource.Get(),
                                                  0, bytes);
                transfers->barrier(buffer->resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                                   D3D12_RESOURCE_STATE_COMMON);
                transfers->finish();
                D3D12_RANGE range{0, bytes};
                check(buffer_readback->Map(0, &range, &map.pData), "D3D12 buffer Map");
                map.RowPitch = width * 4;
            } else {
                context->CopyResource(readback.Get(), imported.Get());
                check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &map), "D3D11 Map");
            }
            unsigned error = 0;
            const auto map_finished = Clock::now();
            for (UINT y = 0; y < height; ++y)
                for (UINT x = 0; x < width * 4; ++x) {
                    const size_t i = size_t(y) * width * 4 + x;
                    // The denominator is odd, so integer source-over has no half ties.
                    const int expected = std::min(
                        255, int(fg[i]) + (int(bg[i]) * (255 - fg[(i / 4) * 4 + 3]) + 127) / 255);
                    const int observed =
                        static_cast<const uint8_t *>(map.pData)[size_t(y) * map.RowPitch + x];
                    error = std::max(error, unsigned(std::abs(expected - observed)));
                }
            if (buffer_control) {
                D3D12_RANGE no_write{0, 0};
                buffer_readback->Unmap(0, &no_write);
            } else
                context->Unmap(readback.Get(), 0);
            diagnostics.dump();
            maximum_error = std::max(maximum_error, error);
            std::cout
                << "{\"frame\":" << frame << ",\"max_byte_error\":" << error << ",\"npu_infer_ms\":"
                << std::chrono::duration<double, std::milli>(infer_finished - infer_started).count()
                << ",\"gpu_map_ms\":"
                << std::chrono::duration<double, std::milli>(map_finished - infer_finished).count()
                << ",\"oracle_ms\":"
                << std::chrono::duration<double, std::milli>(Clock::now() - map_finished).count()
                << "}\n"
                << std::flush;
            if (error > 1)
                throw std::runtime_error("native graphics pixels differ from independent oracle");
        }
        std::cout << "{\"passed\":true,\"full_desktop_offload\":false,\"npu_output_shared\":true,"
                     "\"host_inputs\":true,\"host_synchronized\":true,\"frames\":"
                  << frames << ",\"consumer_api\":"
                  << quote(buffer_control ? "D3D12_BUFFER" : "D3D11_TEXTURE")
                  << ",\"mapped_alias\":" << (mapped_alias ? "true" : "false")
                  << ",\"mapping_owner\":" << quote(gpu_map ? "GPU" : "NPU")
                  << ",\"width\":" << width << ",\"height\":" << height
                  << ",\"max_byte_error\":" << maximum_error << "}\n";
        return 0;
    } catch (const std::exception &error) {
        diagnostics.dump();
        std::cout << "{\"passed\":false,\"full_desktop_offload\":false,\"stage\":" << quote(stage)
                  << ",\"error\":" << quote(error.what()) << "}\n";
        return 1;
    }
}
