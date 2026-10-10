// Guest-only normal-loader D3D10 DDI bridge over real kernel allocations.
// Restricted composition shaders use an explicit provider. Blit presents forward
// to DXGI; callback acceptance alone does not establish display delivery.
#define NOMINMAX
#pragma warning(push)
#pragma warning(disable: 4201) // Anonymous structures in Microsoft DDI headers.
#include <windows.h>
#include <d3d10_1.h>
#include <d3d10umddi.h>
#pragma warning(pop)
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <new>
#include <type_traits>
#include "contract.hpp"
#include "kernel_memory.hpp"
#include "blend_executor.hpp"
#include "shader_contract.hpp"
namespace {
using namespace nr::registered_lab;
std::recursive_mutex mutex;
Counters counters;
std::string selected_backend = "NPU";
struct Device {
    D3D10DDI_HRTCORELAYER core{};
    D3D10DDI_CORELAYER_DEVICECALLBACKS callbacks{};
    D3D10DDI_DEVICEFUNCS functions{};
    const DXGI_DDI_BASE_CALLBACKS *dxgi_callbacks = nullptr;
    std::shared_ptr<Context> context;
    std::shared_ptr<Allocation> bound_target;
    std::shared_ptr<Allocation> bound_source;
    std::unique_ptr<BlendExecutor> executor;
    std::string backend;
    std::vector<UINT> vertex_code, pixel_code;
    UINT vertex_shader = 0, pixel_shader = 0, viewport_count = 0;
    D3D10_DDI_VIEWPORT viewport{};
    D3D10_DDI_PRIMITIVE_TOPOLOGY topology{};
    D3D10_DDI_BLEND_DESC blend{};
    D3D10_DDI_DEPTH_STENCIL_DESC depth{};
    D3D10_DDI_RASTERIZER_DESC raster{};
    std::array<FLOAT, 4> blend_factor{};
    UINT sample_mask = ~0u, stencil_ref = 0;
    struct SamplerBinding {
        bool bound = false;
        D3D10_DDI_SAMPLER_DESC desc{};
    };
    std::array<std::array<SamplerBinding, 16>, 3> samplers{};
};
struct Resource {
    std::shared_ptr<Allocation> allocation;
    UINT usage = 0, map_flags = 0;
};
struct View { std::shared_ptr<Allocation> allocation; };
struct Shader { UINT kind = 0; };
Device &device(D3D10DDI_HDEVICE h) { return *static_cast<Device *>(h.pDrvPrivate); }
void reject(D3D10DDI_HDEVICE h, HRESULT status) {
    ++counters.rejected;
    auto &d = device(h);
    d.callbacks.pfnSetErrorCb(d.core, status);
}
template<class F> void guard(D3D10DDI_HDEVICE h, F f) noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    try { f(); }
    catch (const Error &e) { reject(h, e.status); }
    catch (const std::bad_alloc &) { reject(h, E_OUTOFMEMORY); }
    catch (const std::exception &e) { std::fprintf(stderr, "nr_fill_umd: %s\n", e.what()); reject(h, E_FAIL); }
    catch (...) { reject(h, E_FAIL); }
}
template<class T, size_t Offset> struct Unsupported;
template<class R, class... A, size_t Offset> struct Unsupported<R(APIENTRY *)(D3D10DDI_HDEVICE, A...), Offset> {
    static R APIENTRY call(D3D10DDI_HDEVICE h, A...) {
        guard(h, [] {
            counters.last_unsupported_offset = Offset;
            std::fprintf(stderr, "nr_fill_umd: unsupported DDI offset=%zu\n", Offset);
            throw Error{DXGI_ERROR_UNSUPPORTED};
        });
        if constexpr (!std::is_void_v<R>) return R{};
    }
};
template<class T, size_t Offset> struct UnsupportedDxgi;
template<class R, class... A, size_t Offset> struct UnsupportedDxgi<R(APIENTRY *)(A...), Offset> {
    static R APIENTRY call(A...) {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        ++counters.rejected;
        std::fprintf(stderr, "nr_fill_umd: unsupported DXGI DDI offset=%zu\n", Offset);
        return DXGI_ERROR_UNSUPPORTED;
    }
};
bool format_ok(DXGI_FORMAT f) { return f == DXGI_FORMAT_R8G8B8A8_UNORM || f == DXGI_FORMAT_B8G8R8A8_UNORM; }
Resource &resource(D3D10DDI_HRESOURCE h) {
    require(h.pDrvPrivate != nullptr);
    auto &r = *static_cast<Resource *>(h.pDrvPrivate);
    require(bool(r.allocation));
    return r;
}
void upload(Allocation &target, const D3D10_DDI_BOX *box, const void *source, UINT pitch) {
    require(!target.locked && !target.context->poisoned);
    const auto &s = target.surface;
    const D3D10_DDI_BOX region = box ? *box : D3D10_DDI_BOX{0, 0, 0, LONG(s.width), LONG(s.height), 1};
    require(region.left >= 0 && region.left <= region.right && region.right <= LONG(s.width) &&
            region.top >= 0 && region.top <= region.bottom && region.bottom <= LONG(s.height) &&
            region.front >= 0 && region.front <= region.back && region.back <= 1);
    if (region.left == region.right || region.top == region.bottom || region.front == region.back) return;
    const UINT row_bytes = (region.right - region.left) * 4, rows = region.bottom - region.top;
    const uint64_t source_bytes = uint64_t(rows - 1) * pitch + row_bytes;
    require(source && pitch >= row_bytes && uintptr_t(source) <= UINTPTR_MAX - source_bytes);
    const bool entire = !region.left && !region.top && region.right == LONG(s.width) && region.bottom == LONG(s.height);
    // CPU-visible upload storage is separate from the shared memory-segment
    // allocation. A partial shared update first preserves the previous pixels.
    std::unique_ptr<Allocation> transfer;
    Allocation *writable = &target;
    if (target.runtime_resource) {
        transfer = std::make_unique<Allocation>(target.context);
        transfer->create(s.width, s.height, s.format);
        writable = transfer.get();
        if (!entire) {
            target.context->copy(target.handle, transfer->handle);
            counters.copies += s.allocation_bytes;
        }
    }
    {
        Mapping write(*writable, false);
        if (entire) std::memset(write.data, 0, size_t(s.allocation_bytes));
        for (UINT y = 0; y < rows; ++y) {
            auto destination = static_cast<unsigned char *>(write.data) + size_t(y + region.top) * s.row_pitch + region.left * 4;
            auto input = static_cast<const unsigned char *>(source) + size_t(y) * pitch;
            std::memcpy(destination, input, row_bytes);
        }
        write.finish();
    }
    if (transfer) {
        target.context->copy(transfer->handle, target.handle);
        counters.copies += s.allocation_bytes;
        // A blocking write lock waits for the submitted reader before the
        // temporary allocation is released. No cached pointer survives a lock.
        Mapping consumed(*transfer, false);
        consumed.finish();
    }
    counters.upload_bytes += uint64_t(row_bytes) * rows;
}
SIZE_T APIENTRY resource_size(D3D10DDI_HDEVICE, const D3D10DDIARG_CREATERESOURCE *) { return sizeof(Resource); }
void APIENTRY create_resource(D3D10DDI_HDEVICE h, const D3D10DDIARG_CREATERESOURCE *a,
                             D3D10DDI_HRESOURCE out, D3D10DDI_HRTRESOURCE rt) {
    new (out.pDrvPrivate) Resource();
    guard(h, [&] {
        if (a && (a->MiscFlags || a->pPrimaryDesc || (a->BindFlags & D3D10_DDI_BIND_PRESENT))) {
            std::fprintf(stderr, "nr_fill_umd: resource dimension=%u mips=%u array=%u format=%u samples=%u/%u misc=%x usage=%u bind=%x map=%x primary=%u initial=%u runtime_resource=%u\n",
                UINT(a->ResourceDimension), a->MipLevels, a->ArraySize, UINT(a->Format), a->SampleDesc.Count,
                a->SampleDesc.Quality, a->MiscFlags, UINT(a->Usage), a->BindFlags, a->MapFlags,
                UINT(a->pPrimaryDesc != nullptr), UINT(a->pInitialDataUP != nullptr), UINT(rt.handle != nullptr));
            if (a->pMipInfoList) std::fprintf(stderr, "nr_fill_umd: resource extent=%ux%ux%u\n",
                a->pMipInfoList[0].TexelWidth, a->pMipInfoList[0].TexelHeight, a->pMipInfoList[0].TexelDepth);
        }
        constexpr UINT allowed_misc = D3D10_DDI_RESOURCE_MISC_SHARED | D3D10_DDI_RESOURCE_MISC_DISCARD_ON_PRESENT;
        require(a && a->ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D && a->MipLevels == 1 &&
                a->ArraySize == 1 && a->pMipInfoList && format_ok(a->Format) && a->SampleDesc.Count == 1 &&
                !a->SampleDesc.Quality && !(a->MiscFlags & ~allowed_misc) && !a->pPrimaryDesc);
        const bool shared = (a->MiscFlags & D3D10_DDI_RESOURCE_MISC_SHARED) != 0;
        const bool presentation = (a->BindFlags & D3D10_DDI_BIND_PRESENT) != 0;
        require(!(a->MiscFlags & D3D10_DDI_RESOURCE_MISC_DISCARD_ON_PRESENT) || presentation);
        require(!presentation || (a->BindFlags & D3D10_DDI_BIND_RENDER_TARGET));
        require(!shared || rt.handle);
        const bool staging = a->Usage == D3D10_DDI_USAGE_STAGING && !a->BindFlags && a->MapFlags == D3D10_DDI_CPU_ACCESS_READ && !shared;
        constexpr UINT bindings = D3D10_DDI_BIND_RENDER_TARGET | D3D10_DDI_BIND_SHADER_RESOURCE | D3D10_DDI_BIND_PRESENT;
        const bool target = a->Usage == D3D10_DDI_USAGE_DEFAULT && a->BindFlags && !(a->BindFlags & ~bindings) && !a->MapFlags;
        require((staging || target) && (!a->pInitialDataUP || target));
        const auto &m = a->pMipInfoList[0];
        require(m.TexelDepth == 1);
        auto allocation = std::make_shared<Allocation>(device(h).context);
        allocation->create(m.TexelWidth, m.TexelHeight, UINT(a->Format), shared ? rt.handle : nullptr);
        if (a->pInitialDataUP) upload(*allocation, nullptr, a->pInitialDataUP->pSysMem, a->pInitialDataUP->SysMemPitch);
        auto &r = *static_cast<Resource *>(out.pDrvPrivate);
        r.allocation = std::move(allocation); r.usage = a->Usage; r.map_flags = a->MapFlags;
    });
}
SIZE_T APIENTRY opened_resource_size(D3D10DDI_HDEVICE, const D3D10DDIARG_OPENRESOURCE *) { return sizeof(Resource); }
void APIENTRY open_resource(D3D10DDI_HDEVICE h, const D3D10DDIARG_OPENRESOURCE *a,
                           D3D10DDI_HRESOURCE out, D3D10DDI_HRTRESOURCE rt) {
    new (out.pDrvPrivate) Resource();
    guard(h, [&] {
        require(a && rt.handle && a->hKMResource.handle && a->NumAllocations == 1 && a->pOpenAllocationInfo &&
                a->pPrivateDriverData && a->PrivateDriverDataSize == sizeof(nr::kernel_contract::Surface));
        const auto &item = a->pOpenAllocationInfo[0];
        require(item.hAllocation && item.pPrivateDriverData && item.PrivateDriverDataSize == sizeof(nr::kernel_contract::Surface));
        nr::kernel_contract::Surface description{}, per_allocation{};
        std::memcpy(&description, a->pPrivateDriverData, sizeof(description));
        std::memcpy(&per_allocation, item.pPrivateDriverData, sizeof(per_allocation));
        require(std::memcmp(&description, &per_allocation, sizeof(description)) == 0);
        auto allocation = std::make_shared<Allocation>(device(h).context);
        allocation->open(rt.handle, item.hAllocation, description);
        auto &r = *static_cast<Resource *>(out.pDrvPrivate);
        r.allocation = std::move(allocation); r.usage = D3D10_DDI_USAGE_DEFAULT;
    });
}
void APIENTRY destroy_resource(D3D10DDI_HDEVICE h, D3D10DDI_HRESOURCE r) {
    guard(h, [&] { static_cast<Resource *>(r.pDrvPrivate)->~Resource(); });
}
SIZE_T APIENTRY view_size(D3D10DDI_HDEVICE, const D3D10DDIARG_CREATERENDERTARGETVIEW *) { return sizeof(View); }
void APIENTRY create_view(D3D10DDI_HDEVICE h, const D3D10DDIARG_CREATERENDERTARGETVIEW *a,
                         D3D10DDI_HRENDERTARGETVIEW v, D3D10DDI_HRTRENDERTARGETVIEW) {
    new (v.pDrvPrivate) View();
    guard(h, [&] {
        require(a != nullptr);
        auto &r = resource(a->hDrvResource);
        require(r.usage == D3D10_DDI_USAGE_DEFAULT && a->ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D &&
                UINT(a->Format) == r.allocation->surface.format && !a->Tex2D.MipSlice &&
                !a->Tex2D.FirstArraySlice && a->Tex2D.ArraySize == 1);
        static_cast<View *>(v.pDrvPrivate)->allocation = r.allocation;
    });
}
void APIENTRY destroy_view(D3D10DDI_HDEVICE h, D3D10DDI_HRENDERTARGETVIEW v) {
    guard(h, [&] { static_cast<View *>(v.pDrvPrivate)->~View(); });
}
void APIENTRY clear(D3D10DDI_HDEVICE h, D3D10DDI_HRENDERTARGETVIEW v, FLOAT color[4]) {
    guard(h, [&] {
        require(v.pDrvPrivate && color);
        auto a = static_cast<View *>(v.pDrvPrivate)->allocation;
        require(a && !a->locked && a->context == device(h).context);
        std::array<unsigned char, 4> bytes{};
        for (size_t i = 0; i < 4; ++i) {
            require(std::isfinite(color[i]));
            bytes[i] = static_cast<unsigned char>(std::nearbyint(std::clamp(color[i], 0.f, 1.f) * 255));
        }
        if (a->surface.format == UINT(DXGI_FORMAT_B8G8R8A8_UNORM)) std::swap(bytes[0], bytes[2]);
        ULONG pattern = 0;
        std::memcpy(&pattern, bytes.data(), sizeof(pattern));
        a->context->fill(a->handle, pattern);
    });
}
void APIENTRY copy_resource(D3D10DDI_HDEVICE h, D3D10DDI_HRESOURCE dst, D3D10DDI_HRESOURCE src) {
    guard(h, [&] {
        auto &d = resource(dst); auto &s = resource(src);
        require(d.usage == D3D10_DDI_USAGE_STAGING && s.usage == D3D10_DDI_USAGE_DEFAULT);
        auto a = d.allocation, b = s.allocation;
        require(a != b && a->context == device(h).context && b->context == a->context &&
                a->surface.width == b->surface.width && a->surface.height == b->surface.height &&
                a->surface.format == b->surface.format && a->surface.row_pitch == b->surface.row_pitch &&
                a->surface.allocation_bytes == b->surface.allocation_bytes && !a->locked && !b->locked);
        if (b->runtime_resource) {
            // Shared memory-segment targets cannot be locked by the UMD. The
            // OS orders this submitted copy; staging Map waits on its write.
            a->context->copy(b->handle, a->handle);
            counters.copies += a->surface.allocation_bytes;
            return;
        }
        // This explicit CPU staging copy waits for submitted writes through the
        // blocking source lock. It is not a GPU or NPU copy implementation.
        Mapping read(*b, true), write(*a, false);
        std::memcpy(write.data, read.data, size_t(a->surface.allocation_bytes));
        write.finish(); read.finish();
        counters.copies += a->surface.allocation_bytes;
    });
}
void APIENTRY update_resource(D3D10DDI_HDEVICE h, D3D10DDI_HRESOURCE dst, UINT sub,
                             const D3D10_DDI_BOX *box, const void *source, UINT pitch, UINT) {
    guard(h, [&] {
        auto &r = resource(dst);
        require(!sub && r.usage == D3D10_DDI_USAGE_DEFAULT && r.allocation->context == device(h).context);
        upload(*r.allocation, box, source, pitch); // CPU upload bytes are counted separately from submitted work.
    });
}
void APIENTRY map(D3D10DDI_HDEVICE h, D3D10DDI_HRESOURCE r, UINT sub, D3D10_DDI_MAP type,
                  UINT flags, D3D10DDI_MAPPED_SUBRESOURCE *out) {
    if (out) *out = {};
    guard(h, [&] {
        auto &v = resource(r);
        require(out && !sub && !flags && type == D3D10_DDI_MAP_READ && v.usage == D3D10_DDI_USAGE_STAGING &&
                v.map_flags == D3D10_DDI_CPU_ACCESS_READ && v.allocation->context == device(h).context);
        out->pData = v.allocation->lock(true);
        out->RowPitch = v.allocation->surface.row_pitch;
        out->DepthPitch = UINT(v.allocation->surface.allocation_bytes);
    });
}
void APIENTRY unmap(D3D10DDI_HDEVICE h, D3D10DDI_HRESOURCE r, UINT sub) {
    guard(h, [&] { auto &a = *resource(r).allocation; require(!sub && a.locked); check(a.unlock()); });
}
// The runtime constructs default pipeline state while initializing a device.
// Retain those descriptors even though ClearRenderTargetView ignores them.
// State is retained and validated against the admitted draw before execution.
#define NR_STATE(Name, Type, Handle, RtHandle) \
    SIZE_T APIENTRY Name##_size(D3D10DDI_HDEVICE, const Type *) { return sizeof(Type); } \
    void APIENTRY create_##Name(D3D10DDI_HDEVICE h, const Type *desc, Handle out, RtHandle) { \
        guard(h, [&] { require(desc && out.pDrvPrivate); *static_cast<Type *>(out.pDrvPrivate) = *desc; }); \
    } \
    void APIENTRY destroy_##Name(D3D10DDI_HDEVICE, Handle) {}
NR_STATE(blend, D3D10_DDI_BLEND_DESC, D3D10DDI_HBLENDSTATE, D3D10DDI_HRTBLENDSTATE)
NR_STATE(depth, D3D10_DDI_DEPTH_STENCIL_DESC, D3D10DDI_HDEPTHSTENCILSTATE, D3D10DDI_HRTDEPTHSTENCILSTATE)
NR_STATE(raster, D3D10_DDI_RASTERIZER_DESC, D3D10DDI_HRASTERIZERSTATE, D3D10DDI_HRTRASTERIZERSTATE)
NR_STATE(sampler, D3D10_DDI_SAMPLER_DESC, D3D10DDI_HSAMPLER, D3D10DDI_HRTSAMPLER)
#undef NR_STATE
template<size_t Stage>
void APIENTRY set_samplers(D3D10DDI_HDEVICE h, UINT start, UINT count, const D3D10DDI_HSAMPLER *states) {
    guard(h, [&] {
        auto &slots = device(h).samplers[Stage];
        require(start < slots.size() && count <= slots.size() - start && (!count || states));
        for (UINT i = 0; i < count; ++i) {
            auto &binding = slots[start + i];
            binding.bound = states[i].pDrvPrivate != nullptr;
            binding.desc = binding.bound ? *static_cast<const D3D10_DDI_SAMPLER_DESC *>(states[i].pDrvPrivate)
                                         : D3D10_DDI_SAMPLER_DESC{};
        }
    });
}
void APIENTRY set_blend(D3D10DDI_HDEVICE h, D3D10DDI_HBLENDSTATE state, const FLOAT factor[4], UINT mask) {
    guard(h, [&] {
        auto &d = device(h);
        d.blend = state.pDrvPrivate ? *static_cast<D3D10_DDI_BLEND_DESC *>(state.pDrvPrivate) : D3D10_DDI_BLEND_DESC{};
        if (!state.pDrvPrivate) d.blend.RenderTargetWriteMask[0] = 15;
        for (UINT i = 0; i < 4; ++i) d.blend_factor[i] = factor ? factor[i] : 1.f;
        d.sample_mask = mask;
    });
}
void APIENTRY set_depth(D3D10DDI_HDEVICE h, D3D10DDI_HDEPTHSTENCILSTATE state, UINT ref) {
    guard(h, [&] {
        auto &d = device(h);
        d.depth = state.pDrvPrivate ? *static_cast<D3D10_DDI_DEPTH_STENCIL_DESC *>(state.pDrvPrivate) : D3D10_DDI_DEPTH_STENCIL_DESC{};
        if (!state.pDrvPrivate) { d.depth.DepthEnable = TRUE; d.depth.DepthWriteMask = D3D10_DDI_DEPTH_WRITE_MASK_ALL; d.depth.DepthFunc = D3D10_DDI_COMPARISON_LESS; }
        d.stencil_ref = ref;
    });
}
void APIENTRY set_raster(D3D10DDI_HDEVICE h, D3D10DDI_HRASTERIZERSTATE state) {
    guard(h, [&] {
        auto &d = device(h);
        d.raster = state.pDrvPrivate ? *static_cast<D3D10_DDI_RASTERIZER_DESC *>(state.pDrvPrivate) : D3D10_DDI_RASTERIZER_DESC{};
        if (!state.pDrvPrivate) { d.raster.FillMode = D3D10_DDI_FILL_SOLID; d.raster.CullMode = D3D10_DDI_CULL_BACK; d.raster.DepthClipEnable = TRUE; }
    });
}
void APIENTRY targets(D3D10DDI_HDEVICE h, const D3D10DDI_HRENDERTARGETVIEW *views, UINT count, UINT,
                      D3D10DDI_HDEPTHSTENCILVIEW depth) {
    guard(h, [&] {
        require(count <= 8 && (!count || views) && !depth.pDrvPrivate);
        std::shared_ptr<Allocation> selected;
        for (UINT i = 0; i < count; ++i) {
            if (!views[i].pDrvPrivate) continue;
            require(i == 0);
            selected = static_cast<View *>(views[i].pDrvPrivate)->allocation;
            require(selected && selected->context == device(h).context);
        }
        device(h).bound_target = std::move(selected);
    });
}
#include "draw.hpp"
void APIENTRY flush(D3D10DDI_HDEVICE) {} // Clears submit immediately; draws complete synchronously.
void APIENTRY hazard(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE) {} // Blocking locks order our staging copies.
void APIENTRY format_support(D3D10DDI_HDEVICE, DXGI_FORMAT f, UINT *out) {
    *out = format_ok(f) ? D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET | D3D10_DDI_FORMAT_SUPPORT_SHADER_SAMPLE |
                         D3D10_DDI_FORMAT_SUPPORT_BLENDABLE : 0;
}
void APIENTRY multisample(D3D10DDI_HDEVICE, DXGI_FORMAT f, UINT samples, UINT *out) { *out = format_ok(f) && samples == 1 ? 1 : 0; }
void APIENTRY counter_info(D3D10DDI_HDEVICE, D3D10DDI_COUNTER_INFO *out) { *out = {}; }
void APIENTRY relocate(D3D10DDI_HDEVICE h, D3D10DDI_DEVICEFUNCS *out) { *out = device(h).functions; }
void APIENTRY destroy_device(D3D10DDI_HDEVICE h) { std::lock_guard<std::recursive_mutex> lock(mutex); device(h).~Device(); }
// DXGI returns an HRESULT directly. A callback acceptance is not evidence of
// display delivery, and no local pixel copy substitutes for the callback.
HRESULT APIENTRY present(DXGI_DDI_ARG_PRESENT *a) {
    if (!a || !a->hDevice) return E_INVALIDARG;
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto &d = *reinterpret_cast<Device *>(a->hDevice);
    try {
        require(a->Flags.Value == 1 && !a->SrcSubResourceIndex && !a->DstSubResourceIndex &&
                d.context && !d.context->poisoned && d.dxgi_callbacks &&
                d.dxgi_callbacks->pfnPresentCb && a->hSurfaceToPresent);
        D3D10DDI_HRESOURCE hs{}; hs.pDrvPrivate = reinterpret_cast<void *>(a->hSurfaceToPresent);
        const auto &source = resource(hs);
        require(source.usage == D3D10_DDI_USAGE_DEFAULT && source.allocation->context == d.context && !source.allocation->locked);
        DXGIDDICB_PRESENT callback{};
        callback.hSrcAllocation = source.allocation->handle;
        callback.hContext = d.context->command.hContext;
        callback.pDXGIContext = a->pDXGIContext;
        if (a->hDstResource) {
            D3D10DDI_HRESOURCE hd{}; hd.pDrvPrivate = reinterpret_cast<void *>(a->hDstResource);
            const auto &destination = resource(hd);
            require(destination.usage == D3D10_DDI_USAGE_DEFAULT && destination.allocation->context == d.context &&
                    !destination.allocation->locked && destination.allocation != source.allocation &&
                    destination.allocation->surface.format == source.allocation->surface.format);
            callback.hDstAllocation = destination.allocation->handle;
        }
        ++counters.present_requests;
        const auto status = d.dxgi_callbacks->pfnPresentCb(d.context->runtime, &callback);
        if (FAILED(status)) { d.context->poisoned = true; ++counters.rejected; }
        else ++counters.accepted_present_callbacks;
        return status;
    } catch (const Error &e) { ++counters.rejected; return e.status; }
    catch (...) { ++counters.rejected; return E_FAIL; }
}
SIZE_T APIENTRY device_size(D3D10DDI_HADAPTER, const D3D10DDIARG_CALCPRIVATEDEVICESIZE *) { return sizeof(Device); }
HRESULT APIENTRY create_device(D3D10DDI_HADAPTER, D3D10DDIARG_CREATEDEVICE *a) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!a || !a->hDrvDevice.pDrvPrivate || !a->pUMCallbacks || !a->pUMCallbacks->pfnSetErrorCb ||
        !a->pKTCallbacks || !a->pDeviceFuncs || !a->DXGIBaseDDI.pDXGIDDIBaseFunctions) return E_INVALIDARG;
    if (a->Interface != D3D10_0_DDI_INTERFACE_VERSION && a->Interface != D3D10_0_x_DDI_INTERFACE_VERSION &&
        a->Interface != D3D10_0_7_DDI_INTERFACE_VERSION) return E_NOINTERFACE;
    auto d = new (a->hDrvDevice.pDrvPrivate) Device();
    HRESULT status = E_FAIL;
    try {
        d->core = a->hRTCoreLayer; d->callbacks = *a->pUMCallbacks;
        d->context = std::make_shared<Context>(counters);
        d->context->create(a->hRTDevice.handle, *a->pKTCallbacks);
        // The runtime may replace table entries between UMD calls. Retain the
        // table address, and read only the entry supported by this DDI version.
        d->dxgi_callbacks = a->DXGIBaseDDI.pDXGIBaseCallbacks;
        d->backend = selected_backend;
        d->vertex_code = shader_tokens(nr_ddi_vertex_shader, "vs_4_0");
        d->pixel_code = shader_tokens(nr_ddi_pixel_shader, "ps_4_0");
        auto &f = d->functions;
#define NR_REJECT(field) f.field = &Unsupported<decltype(f.field), offsetof(D3D10DDI_DEVICEFUNCS, field)>::call
#include "../ddi/unsupported.inc"
#undef NR_REJECT
        f.pfnCalcPrivateResourceSize = resource_size; f.pfnCreateResource = create_resource;
        f.pfnCalcPrivateOpenedResourceSize = opened_resource_size; f.pfnOpenResource = open_resource;
        f.pfnDestroyResource = destroy_resource; f.pfnResourceCopy = copy_resource;
        f.pfnResourceUpdateSubresourceUP = update_resource;
        f.pfnResourceMap = f.pfnStagingResourceMap = map;
        f.pfnResourceUnmap = f.pfnStagingResourceUnmap = unmap;
        f.pfnCalcPrivateRenderTargetViewSize = view_size; f.pfnCreateRenderTargetView = create_view;
        f.pfnDestroyRenderTargetView = destroy_view; f.pfnClearRenderTargetView = clear;
        f.pfnSetRenderTargets = targets;
#define NR_BIND(Name, name) \
        f.pfnCalcPrivate##Name##Size = name##_size; \
        f.pfnCreate##Name = create_##name; f.pfnDestroy##Name = destroy_##name
        NR_BIND(BlendState, blend); NR_BIND(DepthStencilState, depth);
        NR_BIND(RasterizerState, raster); NR_BIND(Sampler, sampler);
#undef NR_BIND
        f.pfnSetBlendState = set_blend; f.pfnSetDepthStencilState = set_depth; f.pfnSetRasterizerState = set_raster;
        f.pfnVsSetSamplers = set_samplers<0>; f.pfnGsSetSamplers = set_samplers<1>; f.pfnPsSetSamplers = set_samplers<2>;
        bind_draw_functions(f);
        f.pfnFlush = flush; f.pfnResourceReadAfterWriteHazard = hazard;
        f.pfnCheckFormatSupport = format_support; f.pfnCheckMultisampleQualityLevels = multisample;
        f.pfnCheckCounterInfo = counter_info; f.pfnRelocateDeviceFuncs = relocate; f.pfnDestroyDevice = destroy_device;
        auto &dxgi = *a->DXGIBaseDDI.pDXGIDDIBaseFunctions;
        dxgi = {};
#define NR_DXGI(field) dxgi.field = &UnsupportedDxgi<decltype(dxgi.field), offsetof(DXGI_DDI_BASE_FUNCTIONS, field)>::call
        dxgi.pfnPresent = present; NR_DXGI(pfnGetGammaCaps); NR_DXGI(pfnSetDisplayMode);
        NR_DXGI(pfnSetResourcePriority); NR_DXGI(pfnQueryResourceResidency);
        NR_DXGI(pfnRotateResourceIdentities); NR_DXGI(pfnBlt);
#undef NR_DXGI
        *a->pDeviceFuncs = f;
        // This adapter has no shared D3D9 hardware implementation. The DXGI
        // contract requires CPU-accessible presentation instead of D3D9/DWM
        // resource redirection, even when the selected draw provider is NPU.
        return DXGI_STATUS_NO_REDIRECTION;
    } catch (const Error &e) { status = e.status; }
    catch (const std::bad_alloc &) { status = E_OUTOFMEMORY; }
    catch (...) {}
    d->~Device();
    return status;
}
HRESULT APIENTRY close_adapter(D3D10DDI_HADAPTER) { return S_OK; }
HRESULT APIENTRY versions(D3D10DDI_HADAPTER, UINT32 *count, UINT64 *out) {
    const UINT64 supported[] = {D3D10_0_DDI_SUPPORTED, D3D10_0_x_DDI_SUPPORTED, D3D10_0_7_DDI_SUPPORTED};
    if (!count) return E_INVALIDARG;
    if (out && *count < 3) return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    *count = 3;
    if (out) std::memcpy(out, supported, sizeof(supported));
    return S_OK;
}
HRESULT APIENTRY caps(D3D10DDI_HADAPTER, const D3D10_2DDIARG_GETCAPS *a) {
    if (!a || !a->pData) return E_INVALIDARG;
    std::memset(a->pData, 0, a->DataSize);
    if (a->Type == D3D11DDICAPS_3DPIPELINESUPPORT && a->DataSize == sizeof(D3D11DDI_3DPIPELINESUPPORT_CAPS))
        static_cast<D3D11DDI_3DPIPELINESUPPORT_CAPS *>(a->pData)->Caps = D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3D11DDI_3DPIPELINELEVEL_10_0);
    return S_OK; // Negotiation for this restricted test is not feature-level conformance.
}
} // namespace
extern "C" __declspec(dllexport) HRESULT APIENTRY OpenAdapter10(D3D10DDIARG_OPENADAPTER *a) {
    if (!a || !a->pAdapterFuncs || !a->pAdapterCallbacks || !a->pAdapterCallbacks->pfnQueryAdapterInfoCb) return E_INVALIDARG;
    if (!nr_fill_guest()) return DXGI_ERROR_UNSUPPORTED;
    nr::kernel_contract::AdapterInfo info{};
    std::memset(&info, 0, sizeof(info));
    D3DDDICB_QUERYADAPTERINFO query{};
    query.pPrivateDriverData = &info; query.PrivateDriverDataSize = sizeof(info);
    const auto status = a->pAdapterCallbacks->pfnQueryAdapterInfoCb(a->hRTAdapter.handle, &query);
    if (FAILED(status)) return status;
    const nr::kernel_contract::AdapterInfo expected{};
    if (info.signature != expected.signature || info.abi != expected.abi || info.bytes != expected.bytes || info.ready != 0)
        return DXGI_ERROR_UNSUPPORTED;
    a->hAdapter.pDrvPrivate = reinterpret_cast<void *>(1);
    a->pAdapterFuncs->pfnCalcPrivateDeviceSize = device_size;
    a->pAdapterFuncs->pfnCreateDevice = create_device;
    a->pAdapterFuncs->pfnCloseAdapter = close_adapter;
    return S_OK;
}
extern "C" __declspec(dllexport) HRESULT APIENTRY OpenAdapter10_2(D3D10DDIARG_OPENADAPTER *a) {
    const auto status = OpenAdapter10(a);
    if (SUCCEEDED(status)) { a->pAdapterFuncs_2->pfnGetSupportedVersions = versions; a->pAdapterFuncs_2->pfnGetCaps = caps; }
    return status;
}
extern "C" __declspec(dllexport) BOOL nr_fill_stats(NrFillStats *out) {
    if (!out || out->bytes != sizeof(*out) || out->version != 6) return FALSE;
    std::lock_guard<std::recursive_mutex> lock(mutex);
    out->allocations = counters.allocations; out->fills = counters.fills; out->staging_copy_bytes = counters.copies;
    out->live_allocations = counters.live_allocations; out->live_contexts = counters.live_contexts;
    out->cleanup_errors = counters.cleanup_errors; out->rejected = counters.rejected;
    out->last_unsupported_offset = counters.last_unsupported_offset;
    out->submitted_copies = counters.submitted_copies;
    out->upload_bytes = counters.upload_bytes;
    out->draws = counters.draws; out->executions = counters.executions;
    out->inference_input_bytes = counters.inference_input_bytes; out->inference_output_bytes = counters.inference_output_bytes;
    out->cpu_executions = counters.cpu_executions; out->gpu_executions = counters.gpu_executions;
    out->npu_executions = counters.npu_executions;
    out->present_requests = counters.present_requests; out->accepted_present_callbacks = counters.accepted_present_callbacks;
    out->remote_npu_executions = counters.remote_npu_executions;
    std::memcpy(out->last_execution_device, counters.last_execution_device, sizeof(out->last_execution_device));
    return TRUE;
}
extern "C" __declspec(dllexport) BOOL nr_fill_configure(const char *backend) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!backend || counters.live_contexts || counters.live_allocations ||
        (std::strcmp(backend, "CPU") && std::strcmp(backend, "GPU") && std::strcmp(backend, "NPU") &&
         (std::strcmp(backend, "NPU_LAB_FILE") || !nr_fill_guest()))) return FALSE;
    try { selected_backend = backend; return TRUE; } catch (...) { return FALSE; }
}
