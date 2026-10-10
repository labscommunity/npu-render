// Disposable-guest IddCx admission laboratory. Never an NPU/offload success claim.
// Public lifecycle reference: Microsoft Windows-driver-samples, IndirectDisplay.
#define NOMINMAX
#include <windows.h>
#include <bugcodes.h>
#include <wudfwdm.h>
#pragma warning(push)
#pragma warning(disable:4324) // UMDF headers intentionally align request unions.
#include <wdf.h>
#pragma warning(pop)
#include <iddcx.h>
#include <d3dkmthk.h>
#include <dxgi1_5.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <mutex>
#include <memory>
#include <vector>
#include "../registered_lab/contract.hpp"
#include "../kmd/contract.hpp"
using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT width = 1024, height = 768;
std::mutex log_mutex;
void trace(const char *format, ...) {
    std::lock_guard<std::mutex> lock(log_mutex);
    WCHAR path[MAX_PATH]{};
    swprintf_s(path, L"C:\\npu-lab\\idd-trace\\process-%lu.jsonl", GetCurrentProcessId());
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA | FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
                             nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) && size.QuadPart < 4 * 1024 * 1024) {
        char line[1536]{};
        va_list args; va_start(args, format);
        const int length = _vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
        va_end(args);
        DWORD written = 0;
        if (length > 0) WriteFile(file, line, DWORD(length), &written, nullptr);
    }
    CloseHandle(file);
}
[[maybe_unused]] HRESULT paired_adapter(LUID &luid) {
    D3DKMT_ENUMADAPTERS2 list{};
    auto status = D3DKMTEnumAdapters2(&list);
    trace("{\"event\":\"adapter_capacity\",\"status\":\"%08lx\",\"capacity\":%u}\n", ULONG(status), list.NumAdapters);
    if (status) return HRESULT_FROM_NT(status);
    if (!list.NumAdapters || list.NumAdapters > 64) return E_UNEXPECTED;
    std::vector<D3DKMT_ADAPTERINFO> adapters(list.NumAdapters);
    list.pAdapters = adapters.data();
    status = D3DKMTEnumAdapters2(&list);
    trace("{\"event\":\"adapter_enumeration\",\"status\":\"%08lx\",\"returned\":%u}\n", ULONG(status), list.NumAdapters);
    if (status) return HRESULT_FROM_NT(status);
    adapters.resize(list.NumAdapters);
    UINT matches = 0;
    for (const auto &adapter : adapters) {
        nr::kernel_contract::AdapterInfo info{};
        std::memset(&info, 0, sizeof(info));
        D3DKMT_QUERYADAPTERINFO query{};
        query.hAdapter = adapter.hAdapter; query.Type = KMTQAITYPE_UMDRIVERPRIVATE;
        query.pPrivateDriverData = &info; query.PrivateDriverDataSize = sizeof(info);
        const nr::kernel_contract::AdapterInfo expected{};
        if (!D3DKMTQueryAdapterInfo(&query) && info.signature == expected.signature &&
            info.abi == expected.abi && info.bytes == expected.bytes && !info.ready) {
            ++matches; luid = adapter.AdapterLuid;
        }
        D3DKMT_CLOSEADAPTER close{adapter.hAdapter}; D3DKMTCloseAdapter(&close);
    }
    return matches == 1 ? S_OK : DXGI_ERROR_NOT_FOUND;
}
void signal(DISPLAYCONFIG_VIDEO_SIGNAL_INFO &mode, bool monitor) {
    mode.totalSize.cx = mode.activeSize.cx = width;
    mode.totalSize.cy = mode.activeSize.cy = height;
    mode.AdditionalSignalInfo.vSyncFreqDivider = monitor ? 0 : 1;
    mode.AdditionalSignalInfo.videoStandard = 255;
    mode.vSyncFreq = {60, 1}; mode.hSyncFreq = {60 * height, 1};
    mode.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
    mode.pixelRate = uint64_t(60) * width * height;
}
struct DeviceState { IDDCX_ADAPTER adapter = nullptr; LUID preferred{}; };
WDF_DECLARE_CONTEXT_TYPE(DeviceState);
class Consumer {
    IDDCX_SWAPCHAIN chain;
    HANDLE available, stop = nullptr, thread = nullptr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    static DWORD WINAPI run_thread(void *self) { static_cast<Consumer *>(self)->run(); return 0; }
    void run() {
        ComPtr<IDXGIDevice> dxgi;
        HRESULT status = device.As(&dxgi);
        if (SUCCEEDED(status)) {
            IDARG_IN_SWAPCHAINSETDEVICE args{}; args.pDevice = dxgi.Get();
            status = IddCxSwapChainSetDevice(chain, &args);
        }
        trace("{\"event\":\"set_device\",\"status\":\"%08lx\"}\n", ULONG(status));
        UINT frames = 0;
        while (SUCCEEDED(status) && WaitForSingleObject(stop, 0) == WAIT_TIMEOUT) {
            IDARG_OUT_RELEASEANDACQUIREBUFFER out{};
            status = IddCxSwapChainReleaseAndAcquireBuffer(chain, &out);
            if (status == E_PENDING) {
                HANDLE handles[]{stop, available};
                const auto wait = WaitForMultipleObjects(2, handles, FALSE, 100);
                if (wait == WAIT_OBJECT_0) break;
                if (wait != WAIT_OBJECT_0 + 1 && wait != WAIT_TIMEOUT) break;
                status = S_OK; continue;
            }
            if (FAILED(status)) break;
            ComPtr<IDXGIResource> resource; resource.Attach(out.MetaData.pSurface);
            ComPtr<ID3D11Texture2D> texture;
            status = resource.As(&texture);
            if (SUCCEEDED(status)) {
                D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
                if (frames < 8) trace("{\"event\":\"frame\",\"index\":%u,\"width\":%u,\"height\":%u,\"format\":%u}\n",
                                     frames, desc.Width, desc.Height, UINT(desc.Format));
                ++frames;
            }
            resource.Reset(); texture.Reset();
            if (FAILED(status)) break;
            status = IddCxSwapChainFinishedProcessingFrame(chain);
        }
        trace("{\"event\":\"consumer_exit\",\"status\":\"%08lx\",\"received_frames\":%u,\"displayed_frames\":0}\n", ULONG(status), frames);
        WdfObjectDelete(chain);
    }
public:
    Consumer(IDDCX_SWAPCHAIN value, HANDLE event) : chain(value), available(event) {}
    HRESULT start(LUID luid) {
        ComPtr<IDXGIFactory4> factory; ComPtr<IDXGIAdapter1> adapter;
        auto status = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if (SUCCEEDED(status)) status = factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter));
        D3D_FEATURE_LEVEL level{};
        if (SUCCEEDED(status)) status = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device, &level, &context);
        trace("{\"event\":\"consumer_device\",\"status\":\"%08lx\",\"feature_level\":%u}\n", ULONG(status), UINT(level));
        if (FAILED(status)) return status;
        stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stop) return HRESULT_FROM_WIN32(GetLastError());
        thread = CreateThread(nullptr, 0, run_thread, this, 0, nullptr);
        return thread ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }
    ~Consumer() {
        if (stop) SetEvent(stop);
        if (thread) { WaitForSingleObject(thread, INFINITE); CloseHandle(thread); }
        if (stop) CloseHandle(stop);
    }
};
struct MonitorState { Consumer *consumer = nullptr; LUID preferred{}; };
WDF_DECLARE_CONTEXT_TYPE(MonitorState);
EVT_IDD_CX_MONITOR_GET_DEFAULT_DESCRIPTION_MODES modes;
NTSTATUS modes(IDDCX_MONITOR, const IDARG_IN_GETDEFAULTDESCRIPTIONMODES *in, IDARG_OUT_GETDEFAULTDESCRIPTIONMODES *out) {
    out->DefaultMonitorModeBufferOutputCount = 1; out->PreferredMonitorModeIdx = 0;
    if (!in->DefaultMonitorModeBufferInputCount) return STATUS_SUCCESS;
    if (!in->pDefaultMonitorModes) return STATUS_INVALID_PARAMETER;
    IDDCX_MONITOR_MODE mode{}; mode.Size = sizeof(mode); mode.Origin = IDDCX_MONITOR_MODE_ORIGIN_DRIVER;
    signal(mode.MonitorVideoSignalInfo, true); in->pDefaultMonitorModes[0] = mode;
    return STATUS_SUCCESS;
}
EVT_IDD_CX_MONITOR_QUERY_TARGET_MODES targets;
NTSTATUS targets(IDDCX_MONITOR, const IDARG_IN_QUERYTARGETMODES *in, IDARG_OUT_QUERYTARGETMODES *out) {
    out->TargetModeBufferOutputCount = 1;
    if (!in->TargetModeBufferInputCount) return STATUS_SUCCESS;
    if (!in->pTargetModes) return STATUS_INVALID_PARAMETER;
    IDDCX_TARGET_MODE mode{}; mode.Size = sizeof(mode); signal(mode.TargetVideoSignalInfo.targetVideoSignalInfo, false);
    in->pTargetModes[0] = mode; return STATUS_SUCCESS;
}
EVT_IDD_CX_ADAPTER_COMMIT_MODES commit_modes;
NTSTATUS commit_modes(IDDCX_ADAPTER, const IDARG_IN_COMMITMODES *in) {
    // A headless diagnostic sink has no physical scanout mode to program.
    trace("{\"event\":\"commit_modes\",\"paths\":%u}\n", in->PathCount);
    return in->PathCount <= 1 ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}
EVT_IDD_CX_MONITOR_ASSIGN_SWAPCHAIN assign;
NTSTATUS assign(IDDCX_MONITOR monitor, const IDARG_IN_SETSWAPCHAIN *in) {
    auto state = WdfObjectGet_MonitorState(monitor);
    delete state->consumer; state->consumer = nullptr;
    const bool paired = (state->preferred.LowPart || state->preferred.HighPart) &&
                        in->RenderAdapterLuid.LowPart == state->preferred.LowPart &&
                        in->RenderAdapterLuid.HighPart == state->preferred.HighPart;
    trace("{\"event\":\"assigned\",\"actual_low\":%lu,\"actual_high\":%ld,\"preferred_selected\":%s}\n",
          in->RenderAdapterLuid.LowPart, in->RenderAdapterLuid.HighPart, paired ? "true" : "false");
    try {
        auto consumer = std::make_unique<Consumer>(in->hSwapChain, in->hNextSurfaceAvailable);
        const auto status = consumer->start(in->RenderAdapterLuid);
        if (FAILED(status)) { WdfObjectDelete(in->hSwapChain); return STATUS_SUCCESS; }
        state->consumer = consumer.release();
        return STATUS_SUCCESS;
    } catch (...) { WdfObjectDelete(in->hSwapChain); return STATUS_SUCCESS; }
    // IddCx ownership is explicitly released on failure, following its retry contract.
}
EVT_IDD_CX_MONITOR_UNASSIGN_SWAPCHAIN unassign;
NTSTATUS unassign(IDDCX_MONITOR monitor) {
    auto state = WdfObjectGet_MonitorState(monitor);
    delete state->consumer; state->consumer = nullptr;
    trace("{\"event\":\"unassigned\"}\n"); return STATUS_SUCCESS;
}
EVT_IDD_CX_ADAPTER_INIT_FINISHED initialized;
NTSTATUS initialized(IDDCX_ADAPTER adapter, const IDARG_IN_ADAPTER_INIT_FINISHED *in) {
    trace("{\"event\":\"adapter_initialized\",\"status\":\"%08lx\"}\n", ULONG(in->AdapterInitStatus));
    if (!NT_SUCCESS(in->AdapterInitStatus)) return STATUS_SUCCESS;
    auto state = WdfObjectGet_DeviceState(adapter);
#ifndef NR_IDD_SYSTEM_RENDERER_CONTROL
    IDARG_IN_ADAPTERSETRENDERADAPTER preference{}; preference.PreferredRenderAdapter = state->preferred;
    IddCxAdapterSetRenderAdapter(adapter, &preference);
    trace("{\"event\":\"preferred\",\"low\":%lu,\"high\":%ld}\n", state->preferred.LowPart, state->preferred.HighPart);
#else
    trace("{\"event\":\"system_renderer_control\",\"experimental_adapter_requested\":false}\n");
#endif
    WDF_OBJECT_ATTRIBUTES attributes; WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, MonitorState);
    attributes.EvtCleanupCallback = [](WDFOBJECT object) {
        auto monitor = WdfObjectGet_MonitorState(object); delete monitor->consumer; monitor->consumer = nullptr;
    };
    IDDCX_MONITOR_INFO info{}; info.Size = sizeof(info); info.MonitorType = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED;
    info.MonitorDescription.Size = sizeof(info.MonitorDescription); info.MonitorDescription.Type = IDDCX_MONITOR_DESCRIPTION_TYPE_EDID;
    info.MonitorContainerId = {0x60f0a813,0xc8ad,0x4421,{0x91,0x38,0x2b,0x18,0x4a,0x5e,0x33,0x90}};
    IDARG_IN_MONITORCREATE create{}; create.ObjectAttributes = &attributes; create.pMonitorInfo = &info;
    IDARG_OUT_MONITORCREATE out{};
    auto status = IddCxMonitorCreate(adapter, &create, &out);
    if (NT_SUCCESS(status)) {
        WdfObjectGet_MonitorState(out.MonitorObject)->preferred = state->preferred;
        IDARG_OUT_MONITORARRIVAL arrival{}; status = IddCxMonitorArrival(out.MonitorObject, &arrival);
    }
    trace("{\"event\":\"monitor_arrival\",\"status\":\"%08lx\"}\n", ULONG(status));
    return status;
}
EVT_WDF_DEVICE_D0_ENTRY power_on;
NTSTATUS power_on(WDFDEVICE device, WDF_POWER_DEVICE_STATE) {
    auto state = WdfObjectGet_DeviceState(device);
    if (state->adapter) return STATUS_SUCCESS;
#ifndef NR_IDD_SYSTEM_RENDERER_CONTROL
    const auto found = paired_adapter(state->preferred);
    trace("{\"event\":\"paired_adapter\",\"status\":\"%08lx\"}\n", ULONG(found));
    if (FAILED(found)) return STATUS_DEVICE_NOT_READY;
#endif
    IDDCX_ADAPTER_CAPS caps{}; caps.Size = sizeof(caps); caps.MaxMonitorsSupported = 1;
    caps.EndPointDiagnostics.Size = sizeof(caps.EndPointDiagnostics);
    caps.EndPointDiagnostics.GammaSupport = IDDCX_FEATURE_IMPLEMENTATION_NONE;
    caps.EndPointDiagnostics.TransmissionType = IDDCX_TRANSMISSION_TYPE_WIRED_OTHER;
    caps.EndPointDiagnostics.pEndPointFriendlyName = L"NPU Render Admission Lab";
    caps.EndPointDiagnostics.pEndPointManufacturerName = L"t8";
    caps.EndPointDiagnostics.pEndPointModelName = L"Disposable VM diagnostic sink";
    IDDCX_ENDPOINT_VERSION version{}; version.Size = sizeof(version); version.MajorVer = 1;
    caps.EndPointDiagnostics.pFirmwareVersion = caps.EndPointDiagnostics.pHardwareVersion = &version;
    WDF_OBJECT_ATTRIBUTES attributes; WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DeviceState);
    IDARG_IN_ADAPTER_INIT init{}; init.WdfDevice = device; init.pCaps = &caps; init.ObjectAttributes = &attributes;
    IDARG_OUT_ADAPTER_INIT out{}; const auto status = IddCxAdapterInitAsync(&init, &out);
    if (NT_SUCCESS(status)) {
        state->adapter = out.AdapterObject;
        WdfObjectGet_DeviceState(out.AdapterObject)->preferred = state->preferred;
    }
    return status;
}
EVT_WDF_DRIVER_DEVICE_ADD add_device;
NTSTATUS add_device(WDFDRIVER, PWDFDEVICE_INIT init) {
    if (!nr_fill_guest()) return STATUS_NOT_SUPPORTED;
    WDF_PNPPOWER_EVENT_CALLBACKS power; WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&power);
    power.EvtDeviceD0Entry = power_on; WdfDeviceInitSetPnpPowerEventCallbacks(init, &power);
    IDD_CX_CLIENT_CONFIG config; IDD_CX_CLIENT_CONFIG_INIT(&config);
    config.EvtIddCxAdapterInitFinished = initialized; config.EvtIddCxAdapterCommitModes = commit_modes;
    config.EvtIddCxParseMonitorDescription = [](const IDARG_IN_PARSEMONITORDESCRIPTION *, IDARG_OUT_PARSEMONITORDESCRIPTION *) -> NTSTATUS {
        // This diagnostic advertises no EDID; only the default mode contract applies.
        return STATUS_NOT_SUPPORTED;
    };
    config.EvtIddCxMonitorGetDefaultDescriptionModes = modes; config.EvtIddCxMonitorQueryTargetModes = targets;
    config.EvtIddCxMonitorAssignSwapChain = assign; config.EvtIddCxMonitorUnassignSwapChain = unassign;
    auto status = IddCxDeviceInitConfig(init, &config);
    trace("{\"event\":\"client_config\",\"status\":\"%08lx\"}\n", ULONG(status));
    if (!NT_SUCCESS(status)) return status;
    WDF_OBJECT_ATTRIBUTES attributes; WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DeviceState);
    WDFDEVICE device = nullptr; status = WdfDeviceCreate(&init, &attributes, &device);
    trace("{\"event\":\"framework_device\",\"status\":\"%08lx\"}\n", ULONG(status));
    if (NT_SUCCESS(status)) status = IddCxDeviceInitialize(device);
    trace("{\"event\":\"device_added\",\"status\":\"%08lx\"}\n", ULONG(status));
    return status;
}
}
extern "C" DRIVER_INITIALIZE DriverEntry;
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT object, PUNICODE_STRING path) {
    if (!nr_fill_guest()) return STATUS_NOT_SUPPORTED;
    WDF_DRIVER_CONFIG config; WDF_DRIVER_CONFIG_INIT(&config, add_device);
    WDF_OBJECT_ATTRIBUTES attributes; WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    trace("{\"event\":\"driver_entry\"}\n");
    const auto status = WdfDriverCreate(object, path, &attributes, &config, WDF_NO_HANDLE);
    trace("{\"event\":\"framework_driver\",\"status\":\"%08lx\"}\n", ULONG(status));
    return status;
}
