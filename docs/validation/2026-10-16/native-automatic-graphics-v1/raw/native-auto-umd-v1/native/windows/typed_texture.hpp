#pragma once

#include "blend_model.hpp"
#include "d3d12_buffers.hpp"
#include "ddi/contract.hpp"
#include <d3d11_1.h>
#include <d3d11on12.h>
#include <d3d11sdklayers.h>
#include <d3d12compatibility.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <d3dkmthk.h>
#include <fcntl.h>
#include <io.h>
#include <openvino/runtime/intel_npu/properties.hpp>
#include <winternl.h>

inline void ntcheck(NTSTATUS status, const char *operation) {
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
    unsigned cleanup_failures = 0;
    TextureAlias() = default;
    TextureAlias(const TextureAlias &) = delete;
    TextureAlias &operator=(const TextureAlias &) = delete;
    ~TextureAlias() { close(); }
    void close() noexcept {
        const bool had_resources = adapter || device || resource || alias || shared || locked;
        if (shared && !CloseHandle(shared))
            ++cleanup_failures;
        shared = nullptr;
        auto destroy = [&](D3DKMT_HANDLE r) {
            if (r) {
                D3DKMT_DESTROYALLOCATION2 a{};
                a.hDevice = device;
                a.hResource = r;
                if (D3DKMTDestroyAllocation2(&a) < 0)
                    ++cleanup_failures;
            }
        };
        destroy(alias);
        alias = 0;
        if (locked) {
            D3DKMT_UNLOCK2 a{};
            a.hDevice = device;
            a.hAllocation = allocation;
            if (D3DKMTUnlock2(&a) < 0)
                ++cleanup_failures;
            locked = false;
        }
        destroy(resource);
        resource = allocation = 0;
        if (device) {
            D3DKMT_DESTROYDEVICE a{};
            a.hDevice = device;
            if (D3DKMTDestroyDevice(&a) < 0)
                ++cleanup_failures;
            device = 0;
        }
        if (adapter) {
            D3DKMT_CLOSEADAPTER a{};
            a.hAdapter = adapter;
            if (D3DKMTCloseAdapter(&a) < 0)
                ++cleanup_failures;
            adapter = 0;
        }
        if (had_resources) {
            std::printf("{\"alias_cleanup_failures\":%u}\n", cleanup_failures);
            std::fflush(stdout);
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
            memory.State != MEM_COMMIT || (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
            !(memory.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)))
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
    uint64_t errors = 0;
    void dump() {
        if (eleven) {
            const auto count = eleven->GetNumStoredMessagesAllowedByRetrievalFilter();
            if (count > 64)
                ++errors;
            for (UINT64 i = 0; i < std::min<UINT64>(count, 64); ++i) {
                SIZE_T bytes = 0;
                eleven->GetMessage(i, nullptr, &bytes);
                std::vector<uint8_t> storage(bytes);
                auto *m = reinterpret_cast<D3D11_MESSAGE *>(storage.data());
                if (SUCCEEDED(eleven->GetMessage(i, m, &bytes))) {
                    if (m->Severity <= D3D11_MESSAGE_SEVERITY_ERROR)
                        ++errors;
                    std::cout << "{\"api\":\"D3D11\",\"severity\":" << m->Severity
                              << ",\"message\":" << quote(m->pDescription) << "}\n";
                }
            }
            eleven->ClearStoredMessages();
        }
        if (twelve) {
            const auto count = twelve->GetNumStoredMessagesAllowedByRetrievalFilter();
            if (count > 64)
                ++errors;
            for (UINT64 i = 0; i < std::min<UINT64>(count, 64); ++i) {
                SIZE_T bytes = 0;
                twelve->GetMessage(i, nullptr, &bytes);
                std::vector<uint8_t> storage(bytes);
                auto *m = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
                if (SUCCEEDED(twelve->GetMessage(i, m, &bytes))) {
                    if (m->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
                        ++errors;
                    std::cout << "{\"api\":\"D3D12\",\"severity\":" << m->Severity
                              << ",\"message\":" << quote(m->pDescription) << "}\n";
                }
            }
            twelve->ClearStoredMessages();
        }
        std::cout.flush();
    }
};

// GPU sampling is an explicit graphics-consumer validation step, not NPU work.
struct ShaderConsumer {
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11RasterizerState> raster;
    UINT width = 0, height = 0;
    void create(ID3D11Device *device, ID3D11Texture2D *source) {
        D3D11_TEXTURE2D_DESC desc{};
        source->GetDesc(&desc);
        width = desc.Width;
        height = desc.Height;
        desc.MiscFlags = desc.CPUAccessFlags = 0;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        check(device->CreateTexture2D(&desc, nullptr, &target), "consumer render target");
        check(device->CreateRenderTargetView(target.Get(), nullptr, &rtv), "consumer RTV");
        check(device->CreateShaderResourceView(source, nullptr, &srv), "shared texture SRV");
        auto compile = [](const char *source, const char *profile) {
            ComPtr<ID3DBlob> code, errors;
            check(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "main",
                             profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors),
                  "consumer shader");
            return code;
        };
        auto vertex = compile(nr_ddi_vertex_shader, "vs_4_0");
        auto pixel = compile(nr_ddi_pixel_shader, "ps_4_0");
        check(device->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(),
                                         nullptr, &vs),
              "consumer VS");
        check(device->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(), nullptr,
                                        &ps),
              "consumer PS");
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        check(device->CreateRasterizerState(&rd, &raster), "consumer raster state");
    }
    void draw(ID3D11DeviceContext *context) {
        auto *target_view = rtv.Get();
        auto *source_view = srv.Get();
        context->OMSetRenderTargets(1, &target_view, nullptr);
        context->PSSetShaderResources(0, 1, &source_view);
        context->VSSetShader(vs.Get(), nullptr, 0);
        context->PSSetShader(ps.Get(), nullptr, 0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->RSSetState(raster.Get());
        D3D11_VIEWPORT viewport{0, 0, FLOAT(width), FLOAT(height), 0, 1};
        context->RSSetViewports(1, &viewport);
        context->Draw(3, 0);
        source_view = nullptr;
        context->PSSetShaderResources(0, 1, &source_view);
        context->OMSetRenderTargets(0, nullptr, nullptr);
    }
};
