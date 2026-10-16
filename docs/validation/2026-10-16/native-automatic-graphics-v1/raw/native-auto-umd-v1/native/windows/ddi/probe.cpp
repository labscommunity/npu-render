#define NOMINMAX
#include "contract.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
static void check(HRESULT hr, const char *where) {
    if (FAILED(hr)) {
        std::ostringstream s;
        s << where << " HRESULT=0x" << std::hex << uint32_t(hr);
        throw std::runtime_error(s.str());
    }
}
static std::string quote(const std::string &s) {
    std::string r = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            r += '\\';
            r += char(c);
        } else if (c == '\n')
            r += "\\n";
        else if (c == '\r')
            r += "\\r";
        else if (c >= 32)
            r += char(c);
    }
    return r + '"';
}
static ComPtr<ID3DBlob> compile(const char *source, const char *profile) {
    ComPtr<ID3DBlob> b, e;
    check(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "main", profile,
                     D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &b, &e),
          "D3DCompile");
    return b;
}
#ifdef NR_DDI_REGISTERED_PROBE
#include "registered_adapter.hpp"
#endif
int main(int argc, char **argv) {
#ifdef NR_DDI_REGISTERED_PROBE
    if (argc == 2 && std::string(argv[1]) == "--list") {
        try { list_registered_adapters(); return 0; }
        catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
    }
    constexpr int required_args = 5;
    constexpr const char *scope =
        "explicit registered DXGI adapter via normal D3D11 creation; configured UMD preload; "
        "restricted shader probe; no desktop routing";
#else
    constexpr int required_args = 4;
    constexpr const char *scope =
        "explicit process-local D3D11 software-driver DDI to selected compute device; "
        "no desktop routing";
#endif
    if (argc != required_args && argc != required_args + 1) {
#ifdef NR_DDI_REGISTERED_PROBE
        std::cerr << "usage: nr_registered_probe DLL CPU|GPU|NPU report.json HIGH:LOW "
                     "[reject-shader|reject-shared|reject-draw]\n";
#else
        std::cerr << "usage: nr_ddi_probe DLL CPU|GPU|NPU report.json "
                     "[reject-shader|reject-shared|reject-draw]\n";
#endif
        return 2;
    }
    const std::string rejection = argc > required_args ? argv[required_args] : "";
    HMODULE module = nullptr;
    NrDdiGetStats get = nullptr;
    NrDdiStats stats;
    std::string error;
    bool passed = false, rejection_confirmed = false;
    HRESULT rejected_hresult = S_OK;
    UINT tested = 0, max_error = 0;
    bool adapter_identity_confirmed = false, device_creation_attempted = false;
    HRESULT created = E_FAIL;
    D3D_FEATURE_LEVEL level{};
    try {
        module = LoadLibraryExA(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module)
            throw std::runtime_error("cannot load explicit renderer DLL");
        auto configure =
            reinterpret_cast<NrDdiConfigure>(GetProcAddress(module, "nr_ddi_configure"));
        get = reinterpret_cast<NrDdiGetStats>(GetProcAddress(module, "nr_ddi_get_stats"));
        if (!configure || !get || !configure(argv[2]))
            throw std::runtime_error("configuration failed");
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        const D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_10_0;
#ifdef NR_DDI_REGISTERED_PROBE
        auto adapter = registered_adapter(argv[4]);
        adapter_identity_confirmed = true;
        device_creation_attempted = true;
        created =
            D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                              D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                              &requested, 1, D3D11_SDK_VERSION, &device, &level, &context);
#else
        device_creation_attempted = true;
        created =
            D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_SOFTWARE, module,
                              D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                              &requested, 1, D3D11_SDK_VERSION, &device, &level, &context);
#endif
        check(created, "D3D11CreateDevice");
        if (!rejection.empty()) {
            if (rejection == "reject-shader") {
                auto code =
                    compile("float4 main(float4 p:SV_POSITION):SV_TARGET{return float4(1,0,0,1);}",
                            "ps_4_0");
                ComPtr<ID3D11PixelShader> shader;
                rejected_hresult = device->CreatePixelShader(
                    code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader);
            } else if (rejection == "reject-shared") {
                D3D11_TEXTURE2D_DESC desc{};
                desc.Width = 64;
                desc.Height = 48;
                desc.MipLevels = 1;
                desc.ArraySize = 1;
                desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
                ComPtr<ID3D11Texture2D> texture;
                rejected_hresult = device->CreateTexture2D(&desc, nullptr, &texture);
            } else if (rejection == "reject-draw") {
                context->Draw(3, 0);
                rejected_hresult = device->GetDeviceRemovedReason();
            } else
                throw std::runtime_error("unknown rejection case");
            get(&stats);
            if (SUCCEEDED(rejected_hresult) || stats.executions || !stats.rejected)
                throw std::runtime_error(
                    "unsupported command was not explicitly rejected before execution");
            rejection_confirmed = true;
        } else {
            auto vs_code = compile(nr_ddi_vertex_shader, "vs_4_0"),
                 ps_code = compile(nr_ddi_pixel_shader, "ps_4_0");
            ComPtr<ID3D11VertexShader> vs;
            ComPtr<ID3D11PixelShader> ps;
            check(device->CreateVertexShader(vs_code->GetBufferPointer(), vs_code->GetBufferSize(),
                                             nullptr, &vs),
                  "CreateVertexShader");
            check(device->CreatePixelShader(ps_code->GetBufferPointer(), ps_code->GetBufferSize(),
                                            nullptr, &ps),
                  "CreatePixelShader");
            D3D11_BLEND_DESC bd{};
            auto &rt = bd.RenderTarget[0];
            rt.BlendEnable = TRUE;
            rt.SrcBlend = D3D11_BLEND_ONE;
            rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            rt.BlendOp = D3D11_BLEND_OP_ADD;
            rt.SrcBlendAlpha = D3D11_BLEND_ONE;
            rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            rt.RenderTargetWriteMask = 15;
            ComPtr<ID3D11BlendState> blend;
            check(device->CreateBlendState(&bd, &blend), "CreateBlendState");
            D3D11_DEPTH_STENCIL_DESC dd{};
            dd.DepthEnable = FALSE;
            dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
            ComPtr<ID3D11DepthStencilState> depth;
            check(device->CreateDepthStencilState(&dd, &depth), "CreateDepthStencilState");
            D3D11_RASTERIZER_DESC rd{};
            rd.FillMode = D3D11_FILL_SOLID;
            rd.CullMode = D3D11_CULL_NONE;
            rd.DepthClipEnable = TRUE;
            ComPtr<ID3D11RasterizerState> raster;
            check(device->CreateRasterizerState(&rd, &raster), "CreateRasterizerState");
            for (const auto format : {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM})
                for (const UINT width : {64u, 132u}) {
                    const UINT height = 48;
                    std::vector<uint8_t> fg(size_t(width) * height * 4), bg(fg.size());
                    for (size_t i = 0; i < fg.size(); i += 4) {
                        const uint8_t alpha = uint8_t((i / 4 * 37) % 256);
                        for (size_t c = 0; c < 3; ++c) {
                            fg[i + c] = uint8_t((i / 4 + c * 23) % (unsigned(alpha) + 1));
                            bg[i + c] = uint8_t((i / 4 + c * 53) % 256);
                        }
                        fg[i + 3] = alpha;
                        bg[i + 3] = 255;
                    }
                    D3D11_TEXTURE2D_DESC desc{};
                    desc.Width = width;
                    desc.Height = height;
                    desc.MipLevels = 1;
                    desc.ArraySize = 1;
                    desc.Format = format;
                    desc.SampleDesc.Count = 1;
                    desc.Usage = D3D11_USAGE_DEFAULT;
                    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
                    D3D11_SUBRESOURCE_DATA initial{bg.data(), width * 4, UINT(bg.size())};
                    ComPtr<ID3D11Texture2D> target, source, staging;
                    check(device->CreateTexture2D(&desc, &initial, &target), "Create target");
                    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                    initial.pSysMem = fg.data();
                    check(device->CreateTexture2D(&desc, &initial, &source), "Create source");
                    desc.BindFlags = 0;
                    desc.Usage = D3D11_USAGE_STAGING;
                    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                    check(device->CreateTexture2D(&desc, nullptr, &staging), "Create staging");
                    ComPtr<ID3D11RenderTargetView> target_view;
                    ComPtr<ID3D11ShaderResourceView> source_view;
                    check(device->CreateRenderTargetView(target.Get(), nullptr, &target_view),
                          "Create RTV");
                    check(device->CreateShaderResourceView(source.Get(), nullptr, &source_view),
                          "Create SRV");
                    ID3D11RenderTargetView *tv = target_view.Get();
                    ID3D11ShaderResourceView *sv = source_view.Get();
                    context->OMSetRenderTargets(1, &tv, nullptr);
                    context->PSSetShaderResources(0, 1, &sv);
                    context->VSSetShader(vs.Get(), nullptr, 0);
                    context->PSSetShader(ps.Get(), nullptr, 0);
                    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    context->OMSetBlendState(blend.Get(), nullptr, ~0u);
                    context->OMSetDepthStencilState(depth.Get(), 0);
                    context->RSSetState(raster.Get());
                    D3D11_VIEWPORT viewport{0, 0, FLOAT(width), FLOAT(height), 0, 1};
                    context->RSSetViewports(1, &viewport);
                    for (unsigned pass = 0; pass < 2; ++pass) {
                        if (pass) {
                            const UINT left = width / 4, top = height / 4, right = left + width / 2,
                                       bottom = top + height / 2;
                            for (UINT y = top; y < bottom; ++y)
                                for (UINT x = left; x < right; ++x) {
                                    const size_t i = (size_t(y) * width + x) * 4;
                                    fg[i] = 23;
                                    fg[i + 1] = 42;
                                    fg[i + 2] = 17;
                                    fg[i + 3] = 128;
                                }
                            D3D11_BOX box{left, top, 0, right, bottom, 1};
                            context->UpdateSubresource(source.Get(), 0, &box,
                                                       fg.data() + (size_t(top) * width + left) * 4,
                                                       width * 4, UINT(fg.size()));
                            check(device->GetDeviceRemovedReason(), "UpdateSubresource");
                        }
                        context->Draw(3, 0);
                        check(device->GetDeviceRemovedReason(), "Draw");
                        context->CopyResource(staging.Get(), target.Get());
                        D3D11_MAPPED_SUBRESOURCE map{};
                        check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map),
                              "Map output");
                        unsigned difference = 0;
                        for (UINT y = 0; y < height; ++y)
                            for (UINT x = 0; x < width; ++x)
                                for (size_t c = 0; c < 4; ++c) {
                                    size_t i = (size_t(y) * width + x) * 4 + c;
                                    const double exact =
                                        fg[i] + bg[i] * (1.0 - fg[(i / 4) * 4 + 3] / 255.0);
                                    const int expected =
                                        int(std::nearbyint(std::clamp(exact, 0.0, 255.0)));
                                    int actual = static_cast<const uint8_t *>(
                                        map.pData)[size_t(y) * map.RowPitch + x * 4 + c];
                                    difference =
                                        std::max(difference, unsigned(std::abs(actual - expected)));
                                }
                        for (UINT y = 0; y < height; ++y)
                            std::memcpy(bg.data() + size_t(y) * width * 4,
                                        static_cast<const uint8_t *>(map.pData) +
                                            size_t(y) * map.RowPitch,
                                        width * 4);
                        context->Unmap(staging.Get(), 0);
                        max_error = std::max(max_error, difference);
                        if (difference > 1)
                            throw std::runtime_error("independent composition oracle failed");
                        ++tested;
                    }
                    context->OMSetRenderTargets(0, nullptr, nullptr);
                    ID3D11ShaderResourceView *empty = nullptr;
                    context->PSSetShaderResources(0, 1, &empty);
                }
            context->Flush();
            get(&stats);
            if (stats.executions != tested || stats.draws != tested || stats.rejected ||
                std::string(stats.device) != argv[2])
                throw std::runtime_error("execution attribution or command coverage failed");
        }
#ifdef NR_DDI_REGISTERED_PROBE
        if (stats.software_thunk_calls || stats.rejected_thunk_calls || !stats.adapters ||
            !stats.devices)
            throw std::runtime_error("normal UMD attribution failed");
#endif
        passed = true;
    } catch (const std::exception &e) {
        error = e.what();
    }
    if (get)
        get(&stats);
    std::ofstream out(argv[3]);
    out << "{\"scope\":" << quote(scope) << ",\"passed\":"
        << (passed ? "true" : "false") << ",\"requested_device\":" << quote(argv[2])
        << ",\"adapter_identity_confirmed\":" << (adapter_identity_confirmed ? "true" : "false")
        << ",\"device_creation_attempted\":" << (device_creation_attempted ? "true" : "false")
#ifdef NR_DDI_REGISTERED_PROBE
        << ",\"adapter_luid\":" << quote(argv[4])
#endif
        << ",\"device_creation_hresult\":" << uint32_t(created)
        << ",\"feature_level\":" << uint32_t(level) << ",\"cases\":" << tested
        << ",\"rejection_case\":" << quote(rejection)
        << ",\"rejection_confirmed\":" << (rejection_confirmed ? "true" : "false")
        << ",\"rejected_hresult\":" << uint32_t(rejected_hresult)
        << ",\"maximum_byte_error\":" << max_error << ",\"error\":" << quote(error)
        << ",\"stats\":{\"adapters\":" << stats.adapters << ",\"devices\":" << stats.devices
        << ",\"draws\":" << stats.draws << ",\"executions\":" << stats.executions
        << ",\"rejected\":" << stats.rejected
        << ",\"host_upload_bytes\":" << stats.host_upload_bytes
        << ",\"host_copy_bytes\":" << stats.host_copy_bytes
        << ",\"inference_input_bytes\":" << stats.inference_input_bytes
        << ",\"inference_output_bytes\":" << stats.inference_output_bytes
        << ",\"cpu_clears\":" << stats.cpu_clears << ",\"device\":" << quote(stats.device)
        << ",\"driver\":" << quote(stats.driver) << ",\"last_error\":" << quote(stats.last_error)
        << ",\"software_thunk_calls\":" << stats.software_thunk_calls
        << ",\"rejected_thunk_calls\":" << stats.rejected_thunk_calls
        << ",\"last_thunk\":" << quote(stats.last_thunk) << "}}\n";
    out.close();
    if (!out) {
        std::cerr << "could not write complete report\n";
        passed = false;
    }
    if (module)
        FreeLibrary(module);
    std::cout << (passed ? "passed" : "failed") << ": " << error << " " << stats.last_error << '\n';
    return passed ? 0 : 1;
}
