// Exercise real VidMm allocations without registering/substituting a driver.
// This is an allocation experiment, not a DWM graphics adapter.
#include "../windows/blend_model.hpp"
#include "../windows/d3d12_buffers.hpp"
#include <array>
#include <d3d12sdklayers.h>
#include <d3dkmthk.h>
#include <openvino/runtime/intel_npu/properties.hpp>
#include <winternl.h>

namespace {
std::string stage;
struct Shape {
    size_t width = 64, height = 64, frames = 32;
    void validate() const {
        if (!width || width > 4096 || width % 4 || !height || height > 4096 || !frames ||
            frames > 1000)
            throw std::invalid_argument(
                "width must be 4..4096 in groups of four; height 1..4096; frames 1..1000");
    }
};
void ntcheck(NTSTATUS status, const char *operation) {
    std::cout << "{\"operation\":" << quote(operation) << ",\"ntstatus\":" << uint32_t(status)
              << "}" << std::endl;
    if (status < 0) {
        std::ostringstream message;
        message << operation << " NTSTATUS 0x" << std::hex << uint32_t(status);
        throw std::runtime_error(message.str());
    }
}
struct Device {
    Device() = default;
    Device(const Device &) = delete;
    Device &operator=(const Device &) = delete;
    D3DKMT_HANDLE adapter = 0, device = 0;
    ~Device() {
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
    void open(LUID luid) {
        D3DKMT_OPENADAPTERFROMLUID a{};
        a.AdapterLuid = luid;
        ntcheck(D3DKMTOpenAdapterFromLuid(&a), "OpenAdapterFromLuid");
        adapter = a.hAdapter;
        D3DKMT_CREATEDEVICE d{};
        d.hAdapter = adapter;
        ntcheck(D3DKMTCreateDevice(&d), "CreateDevice");
        device = d.hDevice;
    }
};
struct Allocation {
    Allocation() = default;
    Allocation(const Allocation &) = delete;
    Allocation &operator=(const Allocation &) = delete;
    D3DKMT_HANDLE device = 0, resource = 0, allocation = 0;
    HANDLE shared = nullptr, section = nullptr;
    uint8_t *memory = nullptr;
    size_t bytes = 0;
    ~Allocation() {
        if (shared)
            CloseHandle(shared);
        if (resource) {
            D3DKMT_DESTROYALLOCATION2 a{};
            a.hDevice = device;
            a.hResource = resource;
            D3DKMTDestroyAllocation2(&a);
        }
        if (memory)
            UnmapViewOfFile(memory);
        if (section)
            CloseHandle(section);
    }
    void create(D3DKMT_HANDLE d, size_t size) {
        device = d;
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        const size_t alignment =
            std::max<size_t>(info.dwPageSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
        bytes = (size + alignment - 1) / alignment * alignment;
        section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, DWORD(bytes),
                                     nullptr);
        if (!section)
            throw std::runtime_error("CreateFileMapping failed");
        memory = static_cast<uint8_t *>(MapViewOfFile(section, FILE_MAP_ALL_ACCESS, 0, 0, bytes));
        if (!memory)
            throw std::runtime_error("MapViewOfFile failed");
        D3DDDI_ALLOCATIONINFO2 item{};
        item.hSection = section;
        D3DKMT_CREATESTANDARDALLOCATION standard{};
        standard.Type = D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP;
        standard.ExistingHeapData.Size = bytes;
        D3DKMT_CREATEALLOCATION a{};
        a.hDevice = device;
        a.NumAllocations = 1;
        a.pAllocationInfo2 = &item;
        a.pStandardAllocation = &standard;
        a.Flags.CreateResource = 1;
        a.Flags.CreateShared = 1;
        a.Flags.NtSecuritySharing = 1;
        a.Flags.CrossAdapter = 1;
        a.Flags.ExistingSection = 1;
        a.Flags.StandardAllocation = 1;
        ntcheck(D3DKMTCreateAllocation2(&a), "CreateAllocation2(standard existing section)");
        resource = a.hResource;
        allocation = item.hAllocation;
        OBJECT_ATTRIBUTES attributes{};
        attributes.Length = sizeof(attributes);
        ntcheck(D3DKMTShareObjects(1, &resource, &attributes, GENERIC_ALL, &shared),
                "ShareObjects");
    }
};
struct Imported {
    Imported() = default;
    Imported(const Imported &) = delete;
    Imported &operator=(const Imported &) = delete;
    D3DKMT_HANDLE device = 0, resource = 0, allocation = 0;
    ~Imported() {
        if (resource) {
            D3DKMT_DESTROYALLOCATION2 a{};
            a.hDevice = device;
            a.hResource = resource;
            D3DKMTDestroyAllocation2(&a);
        }
    }
    void open(D3DKMT_HANDLE d, HANDLE handle) {
        device = d;
        D3DKMT_QUERYRESOURCEINFOFROMNTHANDLE q{};
        q.hDevice = d;
        q.hNtHandle = handle;
        ntcheck(D3DKMTQueryResourceInfoFromNtHandle(&q), "QueryResourceInfoFromNtHandle");
        if (q.NumAllocations != 1 || q.PrivateRuntimeDataSize > 1048576 ||
            q.ResourcePrivateDriverDataSize > 1048576 || q.TotalPrivateDriverDataSize > 1048576)
            throw std::runtime_error("unexpected allocation metadata extent");
        std::vector<uint8_t> runtime(q.PrivateRuntimeDataSize),
            driver(q.ResourcePrivateDriverDataSize), allocations(q.TotalPrivateDriverDataSize);
        D3DDDI_OPENALLOCATIONINFO2 item{};
        D3DKMT_OPENRESOURCEFROMNTHANDLE a{};
        a.hDevice = d;
        a.hNtHandle = handle;
        a.NumAllocations = 1;
        a.pOpenAllocationInfo2 = &item;
        a.pPrivateRuntimeData = runtime.empty() ? nullptr : runtime.data();
        a.PrivateRuntimeDataSize = UINT(runtime.size());
        a.pResourcePrivateDriverData = driver.empty() ? nullptr : driver.data();
        a.ResourcePrivateDriverDataSize = UINT(driver.size());
        a.pTotalPrivateDriverDataBuffer = allocations.empty() ? nullptr : allocations.data();
        a.TotalPrivateDriverDataBufferSize = UINT(allocations.size());
        ntcheck(D3DKMTOpenResourceFromNtHandle(&a), "OpenResourceFromNtHandle");
        resource = a.hResource;
        allocation = item.hAllocation;
    }
};
LUID luid(ov::Core &core, const std::string &name) {
    const auto identity = core.get_property(name, ov::device::luid);
    LUID result{};
    static_assert(sizeof(result) == ov::device::LUID::MAX_LUID_SIZE);
    std::memcpy(&result, identity.luid.data(), sizeof(result));
    return result;
}
struct Worker {
    Handle process, thread, start, done, log_read, log_write;
    ~Worker() {
        if (process.value && WaitForSingleObject(process.value, 0) == WAIT_TIMEOUT) {
            TerminateProcess(process.value, 2);
            WaitForSingleObject(process.value, 5000);
        }
    }
    void launch(const std::array<HANDLE, 3> &resources, const Shape &shape) {
        start.value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        done.value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!start.value || !done.value)
            throw std::runtime_error("worker event creation failed");
        if (!CreatePipe(&log_read.value, &log_write.value, nullptr, 0))
            throw std::runtime_error("worker log pipe failed");
        std::array<HANDLE, 6> handles{resources[0], resources[1], resources[2],
                                      start.value,  done.value,   log_write.value};
        for (auto h : handles)
            if (!SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
                throw std::runtime_error("setting worker handle inheritance failed");
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        std::vector<uint8_t> storage(bytes);
        auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes))
            throw std::runtime_error("initializing worker attributes failed");
        struct Cleanup {
            LPPROC_THREAD_ATTRIBUTE_LIST p;
            ~Cleanup() { DeleteProcThreadAttributeList(p); }
        } cleanup{attributes};
        if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                       handles.data(), sizeof(handles), nullptr, nullptr))
            throw std::runtime_error("worker handle whitelist failed");
        wchar_t filename[32768]{};
        const auto count = GetModuleFileNameW(nullptr, filename, 32768);
        if (!count || count == 32768)
            throw std::runtime_error("worker executable path unavailable");
        std::wstring command = L"\"" + std::wstring(filename) + L"\" --worker";
        for (size_t i = 0; i < 5; ++i)
            command += L" " + std::to_wstring(reinterpret_cast<uintptr_t>(handles[i]));
        command += L" " + std::to_wstring(shape.width) + L" " + std::to_wstring(shape.height) +
                   L" " + std::to_wstring(shape.frames);
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = log_write.value;
        startup.lpAttributeList = attributes;
        PROCESS_INFORMATION child{};
        const BOOL created = CreateProcessW(filename, command.data(), nullptr, nullptr, TRUE,
                                            EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW,
                                            nullptr, nullptr, &startup.StartupInfo, &child);
        const DWORD error = GetLastError();
        for (auto h : handles)
            SetHandleInformation(h, HANDLE_FLAG_INHERIT, 0);
        if (!created)
            throw std::runtime_error("worker launch failed: " + std::to_string(error));
        process.value = child.hProcess;
        thread.value = child.hThread;
        wait(60000);
    }
    void wait(DWORD milliseconds) {
        HANDLE handles[]{done.value, process.value};
        if (WaitForMultipleObjects(2, handles, FALSE, milliseconds) != WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(process.value, &code);
            if (code != STILL_ACTIVE)
                drain_log();
            throw std::runtime_error("NPU worker failed or timed out, exit=" +
                                     std::to_string(code));
        }
    }
    void drain_log() {
        DWORD available = 0, read = 0;
        if (PeekNamedPipe(log_read.value, nullptr, 0, nullptr, &available, nullptr) && available) {
            std::vector<char> data(std::min<DWORD>(available, 16384));
            if (ReadFile(log_read.value, data.data(), DWORD(data.size()), &read, nullptr))
                std::cout.write(data.data(), read).flush();
        }
    }
    void infer() {
        if (!SetEvent(start.value))
            throw std::runtime_error("worker dispatch failed");
        wait(10000);
    }
    void finish() {
        DWORD code = 0;
        if (WaitForSingleObject(process.value, 10000) != WAIT_OBJECT_0 ||
            !GetExitCodeProcess(process.value, &code) || code != 0)
            throw std::runtime_error("NPU worker did not shut down successfully");
        drain_log();
    }
};

int worker_main(char **argv) {
    const Shape shape{std::stoull(argv[7]), std::stoull(argv[8]), std::stoull(argv[9])};
    shape.validate();
    std::array<HANDLE, 5> handles{};
    for (size_t i = 0; i < handles.size(); ++i)
        handles[i] = reinterpret_cast<HANDLE>(uintptr_t(std::stoull(argv[i + 2])));
    ov::Core core;
    Device owner;
    owner.open(luid(core, "NPU"));
    std::array<Imported, 3> resources;
    for (size_t i = 0; i < resources.size(); ++i) {
        resources[i].open(owner.device, handles[i]);
        D3DKMT_LOCK2 mapping{};
        mapping.hDevice = owner.device;
        mapping.hAllocation = resources[i].allocation;
        ntcheck(D3DKMTLock2(&mapping), "worker Lock2 imported pages");
        D3DKMT_UNLOCK2 release{};
        release.hDevice = owner.device;
        release.hAllocation = resources[i].allocation;
        ntcheck(D3DKMTUnlock2(&release), "worker Unlock2 imported pages");
    }
    auto compiled =
        core.compile_model(blend_model(shape.width, shape.height, true, 4, "convolution"), "NPU");
    const auto executions = compiled.get_property(ov::execution_devices);
    if (executions.size() != 1 || (executions[0] != "NPU" && executions[0].rfind("NPU.", 0) != 0))
        throw std::runtime_error("worker NPU execution identity mismatch");
    auto context = compiled.get_context();
    auto import = [&](HANDLE handle) {
        return context.create_tensor(ov::element::u8, {1, shape.height, shape.width, 4},
                                     {{"MEM_TYPE", std::string("SHARED_BUF")},
                                      {"MEM_HANDLE", static_cast<void *>(handle)},
                                      {"TENSOR_TYPE", std::string("BINDED")}});
    };
    auto b = import(handles[0]), f = import(handles[1]), o = import(handles[2]);
    auto request = compiled.create_infer_request();
    request.set_input_tensor(0, b);
    request.set_input_tensor(1, f);
    request.set_output_tensor(o);
    if (!SetEvent(handles[4]))
        throw std::runtime_error("worker readiness failed");
    for (unsigned frame = 0; frame < shape.frames; ++frame) {
        if (WaitForSingleObject(handles[3], 15000) != WAIT_OBJECT_0)
            throw std::runtime_error("worker producer deadline expired");
        request.infer();
        if (!SetEvent(handles[4]))
            throw std::runtime_error("worker completion failed");
    }
    std::cout << "{\"worker_execution_device\":" << quote(executions[0])
              << ",\"worker_frames\":" << shape.frames
              << ",\"npu_driver\":" << core.get_property("NPU", ov::intel_npu::driver_version)
              << "}" << std::endl;
    return 0;
}
} // namespace

int main(int argc, char **argv) {
    if (argc == 10 && std::string(argv[1]) == "--worker") {
        try {
            return worker_main(argv);
        } catch (const std::exception &e) {
            std::cout << "{\"worker_error\":" << quote(e.what()) << "}" << std::endl;
            return 1;
        }
    }
    if ((argc != 2 && argc != 5) ||
        (std::string(argv[1]) != "GPU" && std::string(argv[1]) != "NPU"))
        return 2;
    const std::string owner = argv[1];
    try {
        Shape shape;
        if (argc == 5)
            shape = {std::stoull(argv[2]), std::stoull(argv[3]), std::stoull(argv[4])};
        shape.validate();
        ov::Core core;
        stage = "open_owner_device";
        Device producer;
        producer.open(luid(core, owner));
        stage = "create_shared_allocations";
        const size_t width = shape.width, height = shape.height, bytes = width * height * 4;
        Allocation background, foreground, output;
        background.create(producer.device, bytes);
        foreground.create(producer.device, bytes);
        output.create(producer.device, bytes);
        stage = "open_other_adapter";
        Device consumer;
        consumer.open(luid(core, owner == "GPU" ? "NPU" : "GPU"));
        Imported imported;
        imported.open(consumer.device, output.shared);
        D3DKMT_LOCK2 lock{};
        lock.hDevice = consumer.device;
        lock.hAllocation = imported.allocation;
        const NTSTATUS lock_status = D3DKMTLock2(&lock);
        std::cout << "{\"operation\":\"Lock2 imported allocation\",\"ntstatus\":"
                  << uint32_t(lock_status) << "}" << std::endl;
        ntcheck(lock_status, "Lock2 required for interop check");
        if (lock_status >= 0) {
            D3DKMT_UNLOCK2 unlock{};
            unlock.hDevice = consumer.device;
            unlock.hAllocation = imported.allocation;
            ntcheck(D3DKMTUnlock2(&unlock), "Unlock2 imported allocation");
        }
        stage = "gpu_open_existing_pages";
        ComPtr<IDXGIFactory4> factory;
        check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "DXGI factory");
        ComPtr<IDXGIAdapter1> adapter;
        check(factory->EnumAdapterByLuid(luid(core, "GPU"), IID_PPV_ARGS(&adapter)),
              "exact GPU adapter");
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
            debug->EnableDebugLayer();
        ComPtr<ID3D12Device3> gpu;
        check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&gpu)),
              "GPU D3D12 device");
        ComPtr<ID3D12Heap> heap;
        check(gpu->OpenExistingHeapFromFileMapping(output.section, IID_PPV_ARGS(&heap)),
              "OpenExistingHeapFromFileMapping");
        auto hd = heap->GetDesc();
        std::cout << "{\"diagnostic_heap\":{\"flags\":" << unsigned(hd.Flags)
                  << ",\"type\":" << unsigned(hd.Properties.Type)
                  << ",\"cpu_page_property\":" << unsigned(hd.Properties.CPUPageProperty)
                  << ",\"bytes\":" << hd.SizeInBytes << "}}" << std::endl;
        auto desc = buffer_description(bytes, D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER);
        ComPtr<ID3D12Resource> resource;
        const auto placed_status = gpu->CreatePlacedResource(
            heap.Get(), 0, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource));
        if (FAILED(placed_status)) {
            ComPtr<ID3D12InfoQueue> messages;
            if (SUCCEEDED(gpu.As(&messages))) {
                for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i) {
                    SIZE_T n = 0;
                    messages->GetMessage(i, nullptr, &n);
                    std::vector<uint8_t> data(n);
                    auto m = reinterpret_cast<D3D12_MESSAGE *>(data.data());
                    if (SUCCEEDED(messages->GetMessage(i, m, &n)))
                        std::cout << "{\"d3d12_debug_message\":" << quote(m->pDescription) << "}"
                                  << std::endl;
                }
            }
        }
        check(placed_status, "GPU resource over existing pages");
        auto readback = staging(gpu.Get(), bytes, D3D12_HEAP_TYPE_READBACK);
        Transfers transfers(gpu.Get());
        stage = "npu_worker_compile_and_import";
        Worker worker;
        worker.launch({background.shared, foreground.shared, output.shared}, shape);
        unsigned maximum = 0;
        stage = "cross_process_npu_and_gpu_readback";
        for (unsigned frame = 0; frame < shape.frames; ++frame) {
            for (size_t p = 0; p < width * height; ++p) {
                const unsigned alpha = (p * 13 + frame * 7) % 256;
                for (unsigned c = 0; c < 4; ++c) {
                    background.memory[p * 4 + c] = c == 3 ? 255 : uint8_t(p * 19 + c * 37 + frame);
                    foreground.memory[p * 4 + c] =
                        c == 3 ? uint8_t(alpha)
                               : uint8_t(((p * 31 + c * 17 + frame * 3) % 256) * alpha / 255);
                    output.memory[p * 4 + c] = 0xa5;
                }
            }
            MemoryBarrier();
            worker.infer(); // Worker signals only after synchronous NPU completion.
            MemoryBarrier();
            transfers.begin();
            transfers.barrier(resource.Get(), D3D12_RESOURCE_STATE_COMMON,
                              D3D12_RESOURCE_STATE_COPY_SOURCE);
            transfers.list->CopyBufferRegion(readback.Get(), 0, resource.Get(), 0, bytes);
            transfers.barrier(resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                              D3D12_RESOURCE_STATE_COMMON);
            transfers.finish();
            void *mapped = nullptr;
            D3D12_RANGE read{0, bytes};
            check(readback->Map(0, &read, &mapped), "GPU output readback map");
            for (size_t i = 0; i < bytes; ++i) {
                const unsigned alpha = foreground.memory[(i / 4) * 4 + 3];
                const unsigned expected =
                    foreground.memory[i] +
                    (unsigned(background.memory[i]) * (255 - alpha) + 127) / 255;
                maximum =
                    std::max(maximum, unsigned(std::abs(int(output.memory[i]) - int(expected))));
                maximum = std::max(maximum,
                                   unsigned(std::abs(int(static_cast<const uint8_t *>(mapped)[i]) -
                                                     int(expected))));
            }
            D3D12_RANGE no_write{0, 0};
            readback->Unmap(0, &no_write);
            if (maximum > 1)
                throw std::runtime_error("shared-memory output differs from independent oracle");
        }
        worker.finish();
        std::cout
            << "{\"passed\":true,\"allocation_owner\":" << quote(owner)
            << ",\"execution_device\":\"NPU\",\"npu_worker_process\":true,\"gpu_readback_"
               "checked\":true"
            << ",\"frames\":" << shape.frames << ",\"width\":" << width << ",\"height\":" << height
            << ",\"maximum_byte_error\":" << maximum
            << ",\"host_backed_standard_allocations\":true,\"other_adapter_opened\":true,"
               "\"gpu_alias_api\":\"OpenExistingHeapFromFileMapping\",\"host_synchronized\":true,"
               "\"dwm_adapter\":false,\"presentation\":false}"
            << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cout << "{\"passed\":false,\"allocation_owner\":" << quote(owner)
                  << ",\"stage\":" << quote(stage) << ",\"error\":" << quote(e.what()) << "}"
                  << std::endl;
        return 1;
    }
}
