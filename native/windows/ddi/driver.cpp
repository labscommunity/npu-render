// Experimental D3D10 DDI command executor loaded only by an explicit test client.
// This is not a WDDM adapter registration or a replacement for the system driver.
#define NOMINMAX
#include "../blend_model.hpp"
#include "contract.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <d3d10_1.h>
#include <d3d10umddi.h>
#include <d3dcompiler.h>
#include <map>
#include <memory>
#include <mutex>
#include <openvino/runtime/intel_npu/properties.hpp>
#include <string>
#include <type_traits>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
namespace {
std::mutex stats_mutex;
NrDdiStats stats;
std::string selected = "NPU";
size_t active_devices = 0;
template <class F> void record(F action) {
    std::lock_guard<std::mutex> lock(stats_mutex);
    action(stats);
}
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Pixels {
    UINT width = 0, height = 0, usage = 0, cpu_access = 0;
    DXGI_FORMAT format{};
    bool mapped = false;
    std::vector<uint8_t> data;
};
struct Resource {
    std::shared_ptr<Pixels> pixels;
};
struct View {
    std::shared_ptr<Pixels> pixels;
};
struct Shader {
    int kind = 0;
};
struct Blend {
    ov::CompiledModel compiled;
    ov::InferRequest request;
    Blend(ov::Core &core, const std::string &device, UINT w, UINT h) {
        compiled =
            core.compile_model(blend_model(w, h, true, 4, "convolution"), device,
                               ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY));
        const auto executions = compiled.get_property(ov::execution_devices);
        require(executions.size() == 1 &&
                    (executions[0] == device || executions[0].rfind(device + ".", 0) == 0),
                "unexpected execution device");
        request = compiled.create_infer_request();
    }
};
struct Device {
    D3D10DDI_HRTCORELAYER core_layer{};
    D3D10DDI_CORELAYER_DEVICECALLBACKS callbacks{};
    D3D10DDI_DEVICEFUNCS functions{};
    ov::Core runtime;
    std::string backend;
    std::map<uint64_t, std::unique_ptr<Blend>> models;
    std::shared_ptr<Pixels> target, source;
    D3D10_DDI_BLEND_DESC blend{};
    D3D10_DDI_DEPTH_STENCIL_DESC depth{};
    D3D10_DDI_RASTERIZER_DESC raster{};
    D3D10_DDI_VIEWPORT viewport{};
    UINT viewport_count = 0, sample_mask = ~0u;
    D3D10_DDI_PRIMITIVE_TOPOLOGY topology{};
    int vs = 0, ps = 0;
    std::vector<UINT> vertex_code, pixel_code;
};
Device &device(D3D10DDI_HDEVICE h) { return *static_cast<Device *>(h.pDrvPrivate); }
void fail(D3D10DDI_HDEVICE h, const char *message, HRESULT status = E_INVALIDARG) noexcept {
    record([&](auto &s) {
        ++s.rejected;
        strncpy_s(s.last_error, message, _TRUNCATE);
    });
    auto &d = device(h);
    if (d.callbacks.pfnSetErrorCb)
        d.callbacks.pfnSetErrorCb(d.core_layer, status);
}
template <class F> void guard(D3D10DDI_HDEVICE h, F action) noexcept {
    try {
        action();
    } catch (const std::bad_alloc &) {
        fail(h, "allocation failed", E_OUTOFMEMORY);
    } catch (const std::exception &e) {
        fail(h, e.what());
    } catch (...) {
        fail(h, "unknown driver exception", E_FAIL);
    }
}
template <class T, size_t Offset> struct Unsupported;
template <class R, class... A, size_t Offset>
struct Unsupported<R(APIENTRY *)(D3D10DDI_HDEVICE, A...), Offset> {
    static R APIENTRY call(D3D10DDI_HDEVICE h, A...) {
        char message[80]{};
        sprintf_s(message, "unsupported DDI callback at table offset %zu", Offset);
        fail(h, message, DXGI_ERROR_UNSUPPORTED);
        if constexpr (!std::is_void_v<R>)
            return R{};
    }
};
template <class T> struct UnsupportedDxgi;
template <class R, class... A> struct UnsupportedDxgi<R(APIENTRY *)(A...)> {
    static R APIENTRY call(A...) {
        record([](auto &s) {
            ++s.rejected;
            strncpy_s(s.last_error, "DXGI presentation/resource operation unsupported", _TRUNCATE);
        });
        return DXGI_ERROR_UNSUPPORTED;
    }
};
bool format_ok(DXGI_FORMAT f) {
    return f == DXGI_FORMAT_R8G8B8A8_UNORM || f == DXGI_FORMAT_B8G8R8A8_UNORM;
}
std::shared_ptr<Pixels> pixels(D3D10DDI_HRESOURCE h) {
    require(h.pDrvPrivate != nullptr, "null resource");
    auto p = static_cast<Resource *>(h.pDrvPrivate)->pixels;
    require(bool(p), "uninitialized resource");
    return p;
}
std::vector<UINT> compile_tokens(const char *text, const char *profile) {
    ComPtr<ID3DBlob> code, errors;
    require(SUCCEEDED(D3DCompile(text, std::strlen(text), nullptr, nullptr, nullptr, "main",
                                 profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors)),
            "contract shader compilation failed");
    const auto *words = static_cast<const UINT *>(code->GetBufferPointer());
    const size_t count = code->GetBufferSize() / 4;
    require(count >= 8 && words[0] == 0x43425844 && words[7] <= count - 8,
            "invalid DXBC container");
    for (size_t i = 0; i < words[7]; ++i) {
        const size_t offset = words[8 + i] / 4;
        require(offset + 2 <= count && words[offset + 1] % 4 == 0, "invalid shader chunk");
        const size_t length = words[offset + 1] / 4;
        require(length <= count - offset - 2, "shader chunk outside container");
        if (words[offset] == 0x52444853 || words[offset] == 0x58454853)
            return {words + offset + 2, words + offset + 2 + length};
    }
    throw std::runtime_error("shader program missing");
}
SIZE_T APIENTRY resource_size(D3D10DDI_HDEVICE, const D3D10DDIARG_CREATERESOURCE *) {
    return sizeof(Resource);
}
void APIENTRY create_resource(D3D10DDI_HDEVICE h, const D3D10DDIARG_CREATERESOURCE *a,
                              D3D10DDI_HRESOURCE r, D3D10DDI_HRTRESOURCE) {
    new (r.pDrvPrivate) Resource();
    guard(h, [&] {
        require(a && a->pMipInfoList && a->ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D &&
                    a->MipLevels == 1 && a->ArraySize == 1,
                "only one-mip one-slice 2D textures are supported");
        require(format_ok(a->Format) && a->SampleDesc.Count == 1 && a->SampleDesc.Quality == 0 &&
                    !a->MiscFlags && !a->pPrimaryDesc,
                "format, multisampling, sharing or primary resource unsupported");
        const auto &m = a->pMipInfoList[0];
        require(m.TexelWidth && m.TexelWidth <= 2048 && m.TexelHeight && m.TexelHeight <= 2048 &&
                    m.TexelDepth == 1,
                "texture dimensions outside lab contract");
        auto p = std::make_shared<Pixels>();
        p->width = m.TexelWidth;
        p->height = m.TexelHeight;
        p->format = a->Format;
        p->usage = a->Usage;
        p->cpu_access = a->MapFlags;
        p->data.resize(size_t(p->width) * p->height * 4);
        if (a->pInitialDataUP) {
            require(a->pInitialDataUP->pSysMem && a->pInitialDataUP->SysMemPitch >= p->width * 4,
                    "invalid initial row pitch");
            for (UINT y = 0; y < p->height; ++y)
                std::memcpy(p->data.data() + size_t(y) * p->width * 4,
                            static_cast<const uint8_t *>(a->pInitialDataUP->pSysMem) +
                                size_t(y) * a->pInitialDataUP->SysMemPitch,
                            p->width * 4);
            record([&](auto &s) { s.host_upload_bytes += p->data.size(); });
        }
        static_cast<Resource *>(r.pDrvPrivate)->pixels = std::move(p);
    });
}
void APIENTRY destroy_resource(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE r) {
    static_cast<Resource *>(r.pDrvPrivate)->~Resource();
}
void APIENTRY copy_resource(D3D10DDI_HDEVICE h, D3D10DDI_HRESOURCE dst, D3D10DDI_HRESOURCE src) {
    guard(h, [&] {
        auto d = pixels(dst), s = pixels(src);
        require(d != s && !d->mapped && !s->mapped && d->width == s->width &&
                    d->height == s->height && d->format == s->format,
                "copy mismatch or mapped resource");
        std::memcpy(d->data.data(), s->data.data(), d->data.size());
        record([&](auto &v) { v.host_copy_bytes += d->data.size(); });
    });
}
void APIENTRY update_resource(D3D10DDI_HDEVICE h, D3D10DDI_HRESOURCE r, UINT sub,
                              const D3D10_DDI_BOX *box, const void *input, UINT pitch, UINT) {
    guard(h, [&] {
        auto p = pixels(r);
        require(sub == 0 && input && !p->mapped && p->usage == D3D10_DDI_USAGE_DEFAULT,
                "invalid texture update");
        const D3D10_DDI_BOX region =
            box ? *box : D3D10_DDI_BOX{0, 0, 0, LONG(p->width), LONG(p->height), 1};
        require(region.left >= 0 && region.top >= 0 && region.right >= region.left &&
                    region.bottom >= region.top && region.right <= LONG(p->width) &&
                    region.bottom <= LONG(p->height) && region.front == 0 && region.back == 1,
                "update box outside texture");
        const size_t row_bytes = size_t(region.right - region.left) * 4;
        require(pitch >= row_bytes, "update row pitch too small");
        for (LONG y = region.top; y < region.bottom; ++y)
            std::memcpy(p->data.data() + (size_t(y) * p->width + region.left) * 4,
                        static_cast<const uint8_t *>(input) + size_t(y - region.top) * pitch,
                        row_bytes);
        record([&](auto &v) { v.host_upload_bytes += row_bytes * (region.bottom - region.top); });
    });
}
void APIENTRY resource_map(D3D10DDI_HDEVICE h, D3D10DDI_HRESOURCE r, UINT sub, D3D10_DDI_MAP type,
                           UINT flags, D3D10DDI_MAPPED_SUBRESOURCE *mapped) {
    if (mapped)
        *mapped = {};
    guard(h, [&] {
        auto p = pixels(r);
        require(mapped && sub == 0 && flags == 0 && !p->mapped && type == D3D10_DDI_MAP_READ &&
                    p->usage == D3D10_DDI_USAGE_STAGING &&
                    (p->cpu_access & D3D10_DDI_CPU_ACCESS_READ),
                "only staging read mapping is supported");
        p->mapped = true;
        mapped->pData = p->data.data();
        mapped->RowPitch = p->width * 4;
        mapped->DepthPitch = UINT(p->data.size());
    });
}
void APIENTRY resource_unmap(D3D10DDI_HDEVICE h, D3D10DDI_HRESOURCE r, UINT sub) {
    guard(h, [&] {
        auto p = pixels(r);
        require(sub == 0 && p->mapped, "unmap without map");
        p->mapped = false;
    });
}
SIZE_T APIENTRY rtv_size(D3D10DDI_HDEVICE, const D3D10DDIARG_CREATERENDERTARGETVIEW *) {
    return sizeof(View);
}
SIZE_T APIENTRY srv_size(D3D10DDI_HDEVICE, const D3D10DDIARG_CREATESHADERRESOURCEVIEW *) {
    return sizeof(View);
}
void APIENTRY create_rtv(D3D10DDI_HDEVICE h, const D3D10DDIARG_CREATERENDERTARGETVIEW *a,
                         D3D10DDI_HRENDERTARGETVIEW v, D3D10DDI_HRTRENDERTARGETVIEW) {
    new (v.pDrvPrivate) View();
    guard(h, [&] {
        auto p = pixels(a->hDrvResource);
        require(a->ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D && a->Format == p->format &&
                    a->Tex2D.MipSlice == 0 && a->Tex2D.FirstArraySlice == 0 &&
                    a->Tex2D.ArraySize == 1,
                "unsupported render target view");
        static_cast<View *>(v.pDrvPrivate)->pixels = p;
    });
}
void APIENTRY create_srv(D3D10DDI_HDEVICE h, const D3D10DDIARG_CREATESHADERRESOURCEVIEW *a,
                         D3D10DDI_HSHADERRESOURCEVIEW v, D3D10DDI_HRTSHADERRESOURCEVIEW) {
    new (v.pDrvPrivate) View();
    guard(h, [&] {
        auto p = pixels(a->hDrvResource);
        require(a->ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D && a->Format == p->format &&
                    a->Tex2D.MostDetailedMip == 0 && a->Tex2D.MipLevels == 1 &&
                    a->Tex2D.FirstArraySlice == 0 && a->Tex2D.ArraySize == 1,
                "unsupported shader view");
        static_cast<View *>(v.pDrvPrivate)->pixels = p;
    });
}
void APIENTRY destroy_rtv(D3D10DDI_HDEVICE, D3D10DDI_HRENDERTARGETVIEW v) {
    static_cast<View *>(v.pDrvPrivate)->~View();
}
void APIENTRY destroy_srv(D3D10DDI_HDEVICE, D3D10DDI_HSHADERRESOURCEVIEW v) {
    static_cast<View *>(v.pDrvPrivate)->~View();
}
void APIENTRY targets(D3D10DDI_HDEVICE h, const D3D10DDI_HRENDERTARGETVIEW *views, UINT count, UINT,
                      D3D10DDI_HDEPTHSTENCILVIEW depth) {
    guard(h, [&] {
        require(count <= 1 && !depth.pDrvPrivate, "multiple targets or depth target unsupported");
        device(h).target = count && views[0].pDrvPrivate
                               ? static_cast<View *>(views[0].pDrvPrivate)->pixels
                               : nullptr;
    });
}
void APIENTRY sources(D3D10DDI_HDEVICE h, UINT start, UINT count,
                      const D3D10DDI_HSHADERRESOURCEVIEW *views) {
    guard(h, [&] {
        for (UINT i = 0; i < count; ++i) {
            require(start + i == 0 || !views[i].pDrvPrivate, "only texture slot zero supported");
            if (start + i == 0)
                device(h).source = views[i].pDrvPrivate
                                       ? static_cast<View *>(views[i].pDrvPrivate)->pixels
                                       : nullptr;
        }
    });
}
void APIENTRY clear(D3D10DDI_HDEVICE h, D3D10DDI_HRENDERTARGETVIEW v, FLOAT color[4]) {
    guard(h, [&] {
        auto p = static_cast<View *>(v.pDrvPrivate)->pixels;
        require(p && !p->mapped, "invalid clear target");
        std::array<uint8_t, 4> c{};
        for (size_t i = 0; i < 4; ++i) {
            require(std::isfinite(color[i]), "nonfinite clear color");
            c[i] = uint8_t(std::nearbyint(std::clamp(color[i], 0.f, 1.f) * 255));
        }
        if (p->format == DXGI_FORMAT_B8G8R8A8_UNORM)
            std::swap(c[0], c[2]);
        for (size_t i = 0; i < p->data.size(); i += 4)
            std::memcpy(p->data.data() + i, c.data(), 4);
        record([](auto &s) { ++s.cpu_clears; });
    });
}
#define NR_STATE(NAME, TYPE, HANDLE, RTHANDLE)                                                     \
    SIZE_T APIENTRY NAME##_size(D3D10DDI_HDEVICE, const TYPE *) { return sizeof(TYPE); }           \
    void APIENTRY create_##NAME(D3D10DDI_HDEVICE, const TYPE *desc, HANDLE handle, RTHANDLE) {     \
        *static_cast<TYPE *>(handle.pDrvPrivate) = *desc;                                          \
    }                                                                                              \
    void APIENTRY destroy_##NAME(D3D10DDI_HDEVICE, HANDLE) {}
NR_STATE(blend, D3D10_DDI_BLEND_DESC, D3D10DDI_HBLENDSTATE, D3D10DDI_HRTBLENDSTATE)
NR_STATE(depth, D3D10_DDI_DEPTH_STENCIL_DESC, D3D10DDI_HDEPTHSTENCILSTATE,
         D3D10DDI_HRTDEPTHSTENCILSTATE)
NR_STATE(raster, D3D10_DDI_RASTERIZER_DESC, D3D10DDI_HRASTERIZERSTATE, D3D10DDI_HRTRASTERIZERSTATE)
NR_STATE(sampler, D3D10_DDI_SAMPLER_DESC, D3D10DDI_HSAMPLER, D3D10DDI_HRTSAMPLER)
#undef NR_STATE
void APIENTRY set_blend(D3D10DDI_HDEVICE h, D3D10DDI_HBLENDSTATE s, const FLOAT[4], UINT mask) {
    auto &d = device(h);
    d.blend = {};
    d.blend.RenderTargetWriteMask[0] = 15;
    if (s.pDrvPrivate)
        d.blend = *static_cast<D3D10_DDI_BLEND_DESC *>(s.pDrvPrivate);
    d.sample_mask = mask;
}
void APIENTRY set_depth(D3D10DDI_HDEVICE h, D3D10DDI_HDEPTHSTENCILSTATE s, UINT) {
    auto &d = device(h);
    d.depth = {};
    d.depth.DepthEnable = TRUE;
    if (s.pDrvPrivate)
        d.depth = *static_cast<D3D10_DDI_DEPTH_STENCIL_DESC *>(s.pDrvPrivate);
}
void APIENTRY set_raster(D3D10DDI_HDEVICE h, D3D10DDI_HRASTERIZERSTATE s) {
    auto &d = device(h);
    d.raster = {};
    d.raster.FillMode = D3D10_DDI_FILL_SOLID;
    d.raster.CullMode = D3D10_DDI_CULL_BACK;
    d.raster.DepthClipEnable = TRUE;
    if (s.pDrvPrivate)
        d.raster = *static_cast<D3D10_DDI_RASTERIZER_DESC *>(s.pDrvPrivate);
}
void APIENTRY viewports(D3D10DDI_HDEVICE h, UINT count, UINT, const D3D10_DDI_VIEWPORT *v) {
    device(h).viewport_count = count;
    if (count == 1)
        device(h).viewport = v[0];
}
void APIENTRY topology(D3D10DDI_HDEVICE h, D3D10_DDI_PRIMITIVE_TOPOLOGY t) {
    device(h).topology = t;
}
SIZE_T APIENTRY shader_size(D3D10DDI_HDEVICE, const UINT *,
                            const D3D10DDIARG_STAGE_IO_SIGNATURES *) {
    return sizeof(Shader);
}
void create_shader(D3D10DDI_HDEVICE h, const UINT *code, D3D10DDI_HSHADER s, int kind) {
    new (s.pDrvPrivate) Shader();
    guard(h, [&] {
        const auto &expected = kind == 1 ? device(h).vertex_code : device(h).pixel_code;
        require(code && code[1] == expected.size() &&
                    std::equal(expected.begin(), expected.end(), code),
                "shader outside exact admitted composition contract");
        static_cast<Shader *>(s.pDrvPrivate)->kind = kind;
    });
}
void APIENTRY create_vs(D3D10DDI_HDEVICE h, const UINT *c, D3D10DDI_HSHADER s, D3D10DDI_HRTSHADER,
                        const D3D10DDIARG_STAGE_IO_SIGNATURES *) {
    create_shader(h, c, s, 1);
}
void APIENTRY create_ps(D3D10DDI_HDEVICE h, const UINT *c, D3D10DDI_HSHADER s, D3D10DDI_HRTSHADER,
                        const D3D10DDIARG_STAGE_IO_SIGNATURES *) {
    create_shader(h, c, s, 2);
}
void APIENTRY destroy_shader(D3D10DDI_HDEVICE, D3D10DDI_HSHADER) {}
void APIENTRY set_vs(D3D10DDI_HDEVICE h, D3D10DDI_HSHADER s) {
    device(h).vs = s.pDrvPrivate ? static_cast<Shader *>(s.pDrvPrivate)->kind : 0;
}
void APIENTRY set_ps(D3D10DDI_HDEVICE h, D3D10DDI_HSHADER s) {
    device(h).ps = s.pDrvPrivate ? static_cast<Shader *>(s.pDrvPrivate)->kind : 0;
}
void APIENTRY draw(D3D10DDI_HDEVICE h, UINT count, UINT first) {
    guard(h, [&] {
        auto &d = device(h);
        auto p = d.target, s = d.source;
        require(count == 3 && first == 0 &&
                    d.topology == D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLELIST && d.vs == 1 &&
                    d.ps == 2,
                "unsupported draw geometry or shader");
        require(p && s && p != s && !p->mapped && !s->mapped && p->width == s->width &&
                    p->height == s->height && p->format == s->format,
                "draw resource mismatch or alias");
        require(d.viewport_count == 1 && d.viewport.TopLeftX == 0 && d.viewport.TopLeftY == 0 &&
                    d.viewport.Width == p->width && d.viewport.Height == p->height &&
                    d.viewport.MinDepth == 0 && d.viewport.MaxDepth == 1,
                "only full target viewport supported");
        require(!d.depth.DepthEnable && !d.depth.StencilEnable &&
                    d.raster.FillMode == D3D10_DDI_FILL_SOLID &&
                    d.raster.CullMode == D3D10_DDI_CULL_NONE && !d.raster.ScissorEnable &&
                    !d.raster.MultisampleEnable && !d.raster.AntialiasedLineEnable &&
                    d.raster.DepthBias == 0 && d.raster.SlopeScaledDepthBias == 0 &&
                    d.raster.DepthBiasClamp == 0,
                "unsupported depth or raster state");
        require(!d.blend.AlphaToCoverageEnable && d.blend.RenderTargetWriteMask[0] == 15 &&
                    d.sample_mask == ~0u,
                "unsupported coverage or write mask");
        if (d.blend.BlendEnable[0])
            require(d.blend.SrcBlend == D3D10_DDI_BLEND_ONE &&
                        d.blend.DestBlend == D3D10_DDI_BLEND_INV_SRC_ALPHA &&
                        d.blend.BlendOp == D3D10_DDI_BLEND_OP_ADD &&
                        d.blend.SrcBlendAlpha == D3D10_DDI_BLEND_ONE &&
                        d.blend.DestBlendAlpha == D3D10_DDI_BLEND_INV_SRC_ALPHA &&
                        d.blend.BlendOpAlpha == D3D10_DDI_BLEND_OP_ADD,
                    "only premultiplied source-over blend supported");
        const uint64_t key = (uint64_t(p->width) << 32) | p->height;
        if (!d.models.count(key)) {
            require(d.models.size() < 8, "compiled shape cache limit reached");
            d.models.emplace(key,
                             std::make_unique<Blend>(d.runtime, d.backend, p->width, p->height));
        }
        auto &b = *d.models.at(key);
        auto bg = b.request.get_input_tensor(0), fg = b.request.get_input_tensor(1);
        if (d.blend.BlendEnable[0])
            std::memcpy(bg.data(), p->data.data(), p->data.size());
        else
            std::memset(bg.data(), 0, p->data.size());
        std::memcpy(fg.data(), s->data.data(), s->data.size());
        b.request.infer();
        // Commit the output only after every state/resource check and successful inference.
        std::memcpy(p->data.data(), b.request.get_output_tensor().data(), p->data.size());
        record([&](auto &v) {
            ++v.draws;
            ++v.executions;
            v.inference_input_bytes += p->data.size() * 2;
            v.inference_output_bytes += p->data.size();
        });
    });
}
// The runtime initializes and clears unused pipeline stages with null bindings.
// Accept those state transitions; reject an actual unsupported binding.
void APIENTRY null_shader(D3D10DDI_HDEVICE h, D3D10DDI_HSHADER s) {
    if (s.pDrvPrivate)
        fail(h, "geometry shader unsupported");
}
void APIENTRY null_layout(D3D10DDI_HDEVICE h, D3D10DDI_HELEMENTLAYOUT s) {
    if (s.pDrvPrivate)
        fail(h, "vertex input layout unsupported");
}
void APIENTRY null_buffers(D3D10DDI_HDEVICE h, UINT, UINT n, const D3D10DDI_HRESOURCE *a) {
    for (UINT i = 0; i < n; ++i)
        if (a[i].pDrvPrivate) {
            fail(h, "constant buffers unsupported");
            return;
        }
}
void APIENTRY null_views(D3D10DDI_HDEVICE h, UINT, UINT n, const D3D10DDI_HSHADERRESOURCEVIEW *a) {
    for (UINT i = 0; i < n; ++i)
        if (a[i].pDrvPrivate) {
            fail(h, "texture binding in unsupported shader stage");
            return;
        }
}
void APIENTRY unused_samplers(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HSAMPLER *) {
} // Admitted shaders use Load, not Sample.

void APIENTRY null_vertices(D3D10DDI_HDEVICE h, UINT, UINT n, const D3D10DDI_HRESOURCE *a,
                            const UINT *, const UINT *) {
    for (UINT i = 0; i < n; ++i)
        if (a[i].pDrvPrivate) {
            fail(h, "vertex buffers unsupported");
            return;
        }
}
void APIENTRY null_index(D3D10DDI_HDEVICE h, D3D10DDI_HRESOURCE r, DXGI_FORMAT, UINT) {
    if (r.pDrvPrivate)
        fail(h, "index buffers unsupported");
}
void APIENTRY null_stream(D3D10DDI_HDEVICE h, UINT n, UINT, const D3D10DDI_HRESOURCE *a,
                          const UINT *) {
    for (UINT i = 0; i < n; ++i)
        if (a[i].pDrvPrivate) {
            fail(h, "stream output unsupported");
            return;
        }
}
void APIENTRY null_predication(D3D10DDI_HDEVICE h, D3D10DDI_HQUERY q, BOOL) {
    if (q.pDrvPrivate)
        fail(h, "predication unsupported");
}
void APIENTRY scissors(D3D10DDI_HDEVICE, UINT, UINT, const D3D10_DDI_RECT *) {
} // Draw rejects enabled scissoring.
void APIENTRY flush(D3D10DDI_HDEVICE) {} // Commands complete synchronously.
void APIENTRY hazard(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE) {}
void APIENTRY view_hazard(D3D10DDI_HDEVICE, D3D10DDI_HSHADERRESOURCEVIEW, D3D10DDI_HRESOURCE) {}
BOOL APIENTRY busy(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE) { return FALSE; }
void APIENTRY format_support(D3D10DDI_HDEVICE, DXGI_FORMAT f, UINT *caps) {
    *caps = format_ok(f)
                ? D3D10_DDI_FORMAT_SUPPORT_SHADER_SAMPLE | D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET |
                      D3D10_DDI_FORMAT_SUPPORT_BLENDABLE
                : 0;
}
void APIENTRY multisample(D3D10DDI_HDEVICE, DXGI_FORMAT f, UINT samples, UINT *levels) {
    *levels = format_ok(f) && samples == 1 ? 1 : 0;
}
void APIENTRY counter_info(D3D10DDI_HDEVICE, D3D10DDI_COUNTER_INFO *i) { *i = {}; }
void APIENTRY relocate(D3D10DDI_HDEVICE h, D3D10DDI_DEVICEFUNCS *f) { *f = device(h).functions; }
void APIENTRY destroy_device(D3D10DDI_HDEVICE h) {
    device(h).~Device();
    std::lock_guard<std::mutex> lock(stats_mutex);
    --active_devices;
}
SIZE_T APIENTRY device_size(D3D10DDI_HADAPTER, const D3D10DDIARG_CALCPRIVATEDEVICESIZE *) {
    return sizeof(Device);
}
HRESULT APIENTRY create_device(D3D10DDI_HADAPTER, D3D10DDIARG_CREATEDEVICE *a) {
    if (!a || !a->hDrvDevice.pDrvPrivate || !a->pUMCallbacks || !a->pDeviceFuncs ||
        !a->DXGIBaseDDI.pDXGIDDIBaseFunctions)
        return E_INVALIDARG;
    if (a->Interface != D3D10_0_DDI_INTERFACE_VERSION &&
        a->Interface != D3D10_0_x_DDI_INTERFACE_VERSION &&
        a->Interface != D3D10_0_7_DDI_INTERFACE_VERSION)
        return E_NOINTERFACE;
    Device *d = nullptr;
    // Reserve configuration before constructing OpenVINO state, which may take time.
    // All devices in a process must retain the same explicitly selected backend.
    bool reserved = false;
    try {
        std::string backend;
        {
            std::lock_guard<std::mutex> lock(stats_mutex);
            backend = selected;
            ++active_devices;
            reserved = true;
        }
        d = new (a->hDrvDevice.pDrvPrivate) Device();
        d->core_layer = a->hRTCoreLayer;
        d->callbacks = *a->pUMCallbacks;
        d->backend = backend;
        const auto available = d->runtime.get_available_devices();
        require(std::find(available.begin(), available.end(), d->backend) != available.end(),
                "requested device unavailable");
        d->vertex_code = compile_tokens(nr_ddi_vertex_shader, "vs_4_0");
        d->pixel_code = compile_tokens(nr_ddi_pixel_shader, "ps_4_0");
        auto &f = d->functions;
#define NR_REJECT(field)                                                                           \
    f.field = &Unsupported<decltype(f.field), offsetof(D3D10DDI_DEVICEFUNCS, field)>::call
#include "unsupported.inc"
#undef NR_REJECT
        f.pfnCalcPrivateResourceSize = resource_size;
        f.pfnCreateResource = create_resource;
        f.pfnDestroyResource = destroy_resource;
        f.pfnResourceCopy = copy_resource;
        f.pfnResourceUpdateSubresourceUP = update_resource;
        f.pfnResourceMap = resource_map;
        f.pfnResourceUnmap = resource_unmap;
        f.pfnStagingResourceMap = resource_map;
        f.pfnStagingResourceUnmap = resource_unmap;
        f.pfnResourceIsStagingBusy = busy;
        f.pfnCalcPrivateRenderTargetViewSize = rtv_size;
        f.pfnCreateRenderTargetView = create_rtv;
        f.pfnDestroyRenderTargetView = destroy_rtv;
        f.pfnCalcPrivateShaderResourceViewSize = srv_size;
        f.pfnCreateShaderResourceView = create_srv;
        f.pfnDestroyShaderResourceView = destroy_srv;
        f.pfnSetRenderTargets = targets;
        f.pfnPsSetShaderResources = sources;
        f.pfnClearRenderTargetView = clear;
#define NR_BIND_STATE(Name, name)                                                                  \
    f.pfnCalcPrivate##Name##Size = name##_size;                                                    \
    f.pfnCreate##Name = create_##name;                                                             \
    f.pfnDestroy##Name = destroy_##name
        NR_BIND_STATE(BlendState, blend);
        NR_BIND_STATE(DepthStencilState, depth);
        NR_BIND_STATE(RasterizerState, raster);
        NR_BIND_STATE(Sampler, sampler);
#undef NR_BIND_STATE
        f.pfnSetBlendState = set_blend;
        f.pfnSetDepthStencilState = set_depth;
        f.pfnSetRasterizerState = set_raster;
        f.pfnSetViewports = viewports;
        f.pfnIaSetTopology = topology;
        f.pfnCalcPrivateShaderSize = shader_size;
        f.pfnCreateVertexShader = create_vs;
        f.pfnCreatePixelShader = create_ps;
        f.pfnDestroyShader = destroy_shader;
        f.pfnVsSetShader = set_vs;
        f.pfnPsSetShader = set_ps;
        f.pfnDraw = draw;
        f.pfnGsSetShader = null_shader;
        f.pfnIaSetInputLayout = null_layout;
        f.pfnVsSetConstantBuffers = null_buffers;
        f.pfnPsSetConstantBuffers = null_buffers;
        f.pfnGsSetConstantBuffers = null_buffers;
        f.pfnVsSetShaderResources = null_views;
        f.pfnGsSetShaderResources = null_views;
        f.pfnVsSetSamplers = unused_samplers;
        f.pfnPsSetSamplers = unused_samplers;
        f.pfnGsSetSamplers = unused_samplers;
        f.pfnIaSetVertexBuffers = null_vertices;
        f.pfnIaSetIndexBuffer = null_index;
        f.pfnSoSetTargets = null_stream;
        f.pfnSetPredication = null_predication;
        f.pfnSetScissorRects = scissors;
        f.pfnFlush = flush;
        f.pfnResourceReadAfterWriteHazard = hazard;
        f.pfnShaderResourceViewReadAfterWriteHazard = view_hazard;
        f.pfnCheckFormatSupport = format_support;
        f.pfnCheckMultisampleQualityLevels = multisample;
        f.pfnCheckCounterInfo = counter_info;
        f.pfnRelocateDeviceFuncs = relocate;
        f.pfnDestroyDevice = destroy_device;
        *a->pDeviceFuncs = f;
        // Presentation and shared KMD allocations are deliberately unsupported.
        auto &dxgi = *a->DXGIBaseDDI.pDXGIDDIBaseFunctions;
        dxgi = {};
#define NR_DXGI(field) dxgi.field = &UnsupportedDxgi<decltype(dxgi.field)>::call
        NR_DXGI(pfnPresent);
        NR_DXGI(pfnGetGammaCaps);
        NR_DXGI(pfnSetDisplayMode);
        NR_DXGI(pfnSetResourcePriority);
        NR_DXGI(pfnQueryResourceResidency);
        NR_DXGI(pfnRotateResourceIdentities);
        NR_DXGI(pfnBlt);
#undef NR_DXGI
        const auto driver =
            d->backend == "NPU"
                ? std::to_string(d->runtime.get_property("NPU", ov::intel_npu::driver_version))
                : std::string();
        record([&](auto &v) {
            ++v.devices;
            strncpy_s(v.device, d->backend.c_str(), _TRUNCATE);
            strncpy_s(v.driver, driver.c_str(), _TRUNCATE);
        });
        return S_OK;
    } catch (const std::exception &e) {
        record([&](auto &v) { strncpy_s(v.last_error, e.what(), _TRUNCATE); });
        if (d)
            d->~Device();
        std::lock_guard<std::mutex> lock(stats_mutex);
        if (reserved)
            --active_devices;
        return E_FAIL;
    }
}
HRESULT APIENTRY close_adapter(D3D10DDI_HADAPTER) { return S_OK; }
HRESULT APIENTRY versions(D3D10DDI_HADAPTER, UINT32 *count, UINT64 *out) {
    static const UINT64 supported[] = {D3D10_0_DDI_SUPPORTED, D3D10_0_x_DDI_SUPPORTED,
                                       D3D10_0_7_DDI_SUPPORTED};
    if (!count)
        return E_INVALIDARG;
    if (out && *count < 3)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    *count = 3;
    if (out)
        std::memcpy(out, supported, sizeof(supported));
    return S_OK;
}
HRESULT APIENTRY caps(D3D10DDI_HADAPTER, const D3D10_2DDIARG_GETCAPS *a) {
    if (!a || !a->pData)
        return E_INVALIDARG;
    std::memset(a->pData, 0, a->DataSize);
    if (a->Type == D3D11DDICAPS_3DPIPELINESUPPORT &&
        a->DataSize == sizeof(D3D11DDI_3DPIPELINESUPPORT_CAPS))
        static_cast<D3D11DDI_3DPIPELINESUPPORT_CAPS *>(a->pData)->Caps =
            D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3D11DDI_3DPIPELINELEVEL_10_0);
    return S_OK;
}
} // namespace
extern "C" __declspec(dllexport) int nr_ddi_configure(const char *name) {
    std::lock_guard<std::mutex> lock(stats_mutex);
    if (active_devices || !name ||
        (std::strcmp(name, "NPU") && std::strcmp(name, "GPU") && std::strcmp(name, "CPU")))
        return 0;
    selected = name;
    stats = {};
    return 1;
}
extern "C" __declspec(dllexport) int nr_ddi_get_stats(NrDdiStats *out) {
    if (!out || out->size != sizeof(NrDdiStats) || out->version != 1)
        return 0;
    std::lock_guard<std::mutex> lock(stats_mutex);
    *out = stats;
    return 1;
}
extern "C" __declspec(dllexport) HRESULT APIENTRY OpenAdapter10(D3D10DDIARG_OPENADAPTER *a) {
    if (!a || !a->pAdapterFuncs)
        return E_INVALIDARG;
    a->hAdapter.pDrvPrivate = reinterpret_cast<void *>(1);
    a->pAdapterFuncs->pfnCalcPrivateDeviceSize = device_size;
    a->pAdapterFuncs->pfnCreateDevice = create_device;
    a->pAdapterFuncs->pfnCloseAdapter = close_adapter;
    record([](auto &s) { ++s.adapters; });
    return S_OK;
}
extern "C" __declspec(dllexport) HRESULT APIENTRY OpenAdapter10_2(D3D10DDIARG_OPENADAPTER *a) {
    auto r = OpenAdapter10(a);
    if (SUCCEEDED(r)) {
        a->pAdapterFuncs_2->pfnGetSupportedVersions = versions;
        a->pAdapterFuncs_2->pfnGetCaps = caps;
    }
    return r;
}

void nr_ddi_thunk(const char *name, bool rejected) {
    record([&](auto &s) {
        ++s.software_thunk_calls;
        if (rejected)
            ++s.rejected_thunk_calls;
        strncpy_s(s.last_thunk, name, _TRUNCATE);
    });
}
