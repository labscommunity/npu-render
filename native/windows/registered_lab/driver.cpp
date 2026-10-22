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
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include "contract.hpp"
#include "kernel_memory.hpp"
// Frame batch contracts and the shared planner live outside the anonymous
// namespace below; the batcher itself (frame_batcher.hpp) is included from
// draw.hpp once Device, upload and read_pixels exist.
#include "frame_batch_protocol.hpp"
#include "frame_batch_planner.hpp"
#include "frame_native_inventory.hpp"
#include "blend_executor.hpp"
#include "clear_executor.hpp"
#include "vertex_executor.hpp"
#include "uniform_draw.hpp"
#include "indexed_exchange.hpp"
#include "shader_contract.hpp"
#include "lab_trace.hpp"
#include "single_mip_filter.hpp"
#include "legacy_route.hpp"
namespace {
using namespace nr::registered_lab;
std::recursive_mutex mutex;
Counters counters;
std::string selected_backend = "NPU";
using InputElements = std::vector<D3D10DDIARG_INPUT_ELEMENT_DESC>;
struct ElementLayout { std::shared_ptr<const InputElements> elements; };
struct View {
    std::shared_ptr<Allocation> allocation;
    UINT format = 0, first_element = 0, elements = 0;
    bool buffer = false;
};
struct Device {
    D3D10DDI_HRTCORELAYER core{};
    D3D10DDI_CORELAYER_DEVICECALLBACKS callbacks{};
    D3D10DDI_DEVICEFUNCS functions{};
    const DXGI_DDI_BASE_CALLBACKS *dxgi_callbacks = nullptr;
    std::shared_ptr<Context> context;
    std::shared_ptr<Allocation> bound_target;
    std::shared_ptr<Allocation> bound_source;
    std::array<std::array<View, 128>, 3> shader_sources{};
#ifndef NR_NATIVE_PROBE
    std::unique_ptr<BlendExecutor> executor;
    std::unique_ptr<nr::raster::ClearExecutor> clear_executor;
    std::array<uint32_t,3> clear_key{};
    std::unique_ptr<nr::raster::UniformDrawExecutor> indexed_executor;
    std::unique_ptr<nr::raster::PointTextureDrawExecutor> point_texture_executor;
    std::shared_ptr<const nr::shader::Program> point_texture_vertex, point_texture_pixel;
    std::array<uint64_t, 6> point_texture_key{};
    std::unique_ptr<nr::raster::LinearTextureDrawExecutor> linear_texture_executor;
    std::shared_ptr<const nr::shader::Program> linear_texture_vertex, linear_texture_pixel;
    std::array<uint64_t, 8> linear_texture_key{};
    std::unique_ptr<nr::raster::TriangleTextureDrawExecutor> triangle_texture_executor;
    std::shared_ptr<const nr::shader::Program> triangle_texture_vertex, triangle_texture_pixel;
    std::array<uint64_t, 8> triangle_texture_key{};
    std::unique_ptr<nr::raster::TriangleTextureDrawExecutor> resources_executor;
    std::shared_ptr<const nr::shader::Program> resources_vertex,resources_pixel;
    std::array<uint64_t,5> resources_key{};
    nr::raster::PackedResources resources_layout{};
    nr::shader::TextureSamplers resources_samplers{};
#endif
    std::unique_ptr<IndexedNpuExchange> indexed_exchange;
    std::shared_ptr<struct FrameBatcher> batcher;   // frame batches on the indexed NPU path
    std::shared_ptr<const nr::shader::Program> indexed_vertex, indexed_pixel;
    std::array<uint64_t, 5> indexed_key{};
    std::shared_ptr<const nr::shader::Program> vertex_program;
    std::shared_ptr<const nr::shader::Program> pixel_program;
    std::shared_ptr<const InputElements> input_elements;
    std::string backend;
    std::vector<UINT> vertex_code, pixel_code;
    UINT vertex_shader = 0, pixel_shader = 0, viewport_count = 0;
    D3D10_DDI_VIEWPORT viewport{};
    std::vector<D3D10_DDI_RECT> scissors;
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
    std::array<std::array<std::shared_ptr<Allocation>, 14>, 3> constant_buffers{};
    std::array<std::array<UINT, 14>, 3> constant_bytes{};
    struct GeometryBinding {
        std::shared_ptr<Allocation> allocation;
        UINT bytes = 0, stride = 0, offset = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    };
    std::array<GeometryBinding, 16> vertex_buffers{};
    GeometryBinding index_buffer{};
};
struct Resource {
    std::shared_ptr<Allocation> allocation;
    std::vector<std::shared_ptr<Allocation>> additional_mips;
    UINT usage = 0, map_flags = 0;
    UINT buffer_bytes = 0, bind_flags = 0;
    bool mapped_write = false;
};
struct Shader { UINT kind = 0; std::shared_ptr<const nr::shader::Program> vertex_program, pixel_program; };
Device &device(D3D10DDI_HDEVICE h) { return *static_cast<Device *>(h.pDrvPrivate); }
// Frame batch hooks (frame_batcher.hpp): pending NPU results are written back
// before any read of a target and before any CPU write to a referenced resource.
void flush_frame_batches(Device &d, const char *reason);
void frame_batches_before_write(Allocation &a);
void frame_batches_before_read(Allocation &a);
void frame_batches_before_read(Allocation &a, const D3D10_DDI_BOX *box);
bool frame_batch_writeback_active();
void frame_batch_clear(Device &d, const std::shared_ptr<Allocation> &target, const nr::raster::Color &color);
bool frame_batch_copy(Device &d, const std::shared_ptr<Allocation> &source, const std::shared_ptr<Allocation> &destination, const D3D10_DDI_BOX &region, uint32_t x, uint32_t y);
extern std::vector<Device *> frame_devices;
void reject(D3D10DDI_HDEVICE h, HRESULT status, const char *operation) {
    lab_trace("{\"event\":\"reject\",\"operation\":\"%s\",\"status\":\"%08lx\"}\n", operation, ULONG(status));
    ++counters.rejected;
    auto &d = device(h);
    d.callbacks.pfnSetErrorCb(d.core, status);
}
template<class F> void guard(D3D10DDI_HDEVICE h, const char *operation, F f) noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    try { f(); }
    catch (const Error &e) { reject(h, e.status, operation); }
    catch (const std::bad_alloc &) { reject(h, E_OUTOFMEMORY, operation); }
    catch (const std::exception &e) {
        char detail[384]{};size_t n=0;for(const auto *c=e.what();*c && n+3<sizeof(detail);++c){if(*c=='"' || *c=='\\')detail[n++]='\\';detail[n++]=(*c>=32 && *c<127)?*c:' ';}
        lab_trace("{\"event\":\"operation_exception\",\"operation\":\"%s\",\"message\":\"%s\"}\n",operation,detail);
        std::fprintf(stderr, "nr_fill_umd: %s\n", e.what()); reject(h, E_FAIL, operation);
    }
    catch (...) { reject(h, E_FAIL, operation); }
}
template<class T, size_t Offset> struct Unsupported;
template<class R, class... A, size_t Offset> struct Unsupported<R(APIENTRY *)(D3D10DDI_HDEVICE, A...), Offset> {
    static R APIENTRY call(D3D10DDI_HDEVICE h, A...) {
        guard(h, __FUNCTION__, [] {
            counters.last_unsupported_offset = Offset;
            lab_trace("{\"event\":\"unsupported_ddi\",\"offset\":%zu}\n", Offset);
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
        lab_trace("{\"event\":\"unsupported_dxgi\",\"offset\":%zu}\n", Offset);
        std::fprintf(stderr, "nr_fill_umd: unsupported DXGI DDI offset=%zu\n", Offset);
        return DXGI_ERROR_UNSUPPORTED;
    }
};
bool format_ok(DXGI_FORMAT f) { return f == DXGI_FORMAT_R8G8B8A8_UNORM || f == DXGI_FORMAT_B8G8R8A8_UNORM; }
UINT texel_bytes(UINT format) { require(format == 28 || format == 87 || format == 65); return format == 65 ? 1 : 4; }
Resource &resource(D3D10DDI_HRESOURCE h) {
    require(h.pDrvPrivate != nullptr);
    auto &r = *static_cast<Resource *>(h.pDrvPrivate);
    require(bool(r.allocation));
    return r;
}
UINT mip_count(const Resource &r) { return 1u + UINT(r.additional_mips.size()); }
std::shared_ptr<Allocation> mip_allocation(const Resource &r, UINT sub) {
    require(sub < mip_count(r));
    return sub ? r.additional_mips[sub - 1] : r.allocation;
}
#include "buffer.hpp"
#include "input_assembler.hpp"
void upload(Allocation &target, const D3D10_DDI_BOX *box, const void *source, UINT pitch) {
    require(!target.locked && !target.context->poisoned);
    frame_batches_before_write(target);
    const auto &s = target.surface;
    const D3D10_DDI_BOX region = box ? *box : D3D10_DDI_BOX{0, 0, 0, LONG(s.width), LONG(s.height), 1};
    require(region.left >= 0 && region.left <= region.right && region.right <= LONG(s.width) &&
            region.top >= 0 && region.top <= region.bottom && region.bottom <= LONG(s.height) &&
            region.front >= 0 && region.front <= region.back && region.back <= 1);
    if (region.left == region.right || region.top == region.bottom || region.front == region.back) return;
    if (!frame_batch_writeback_active()) target.mark_dirty(region.left, region.top, region.right, region.bottom);
    const UINT bytes = texel_bytes(s.format);
    const UINT row_bytes = (region.right - region.left) * bytes, rows = region.bottom - region.top;
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
            auto destination = static_cast<unsigned char *>(write.data) + size_t(y + region.top) * s.row_pitch + region.left * bytes;
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
    if (!frame_batch_writeback_active()) target.content_version = next_content_version();
}
SIZE_T APIENTRY resource_size(D3D10DDI_HDEVICE, const D3D10DDIARG_CREATERESOURCE *) { return sizeof(Resource); }
void APIENTRY create_resource(D3D10DDI_HDEVICE h, const D3D10DDIARG_CREATERESOURCE *a,
                             D3D10DDI_HRESOURCE out, D3D10DDI_HRTRESOURCE rt) {
    new (out.pDrvPrivate) Resource();
    guard(h, __FUNCTION__, [&] {
        if (a) lab_trace("{\"event\":\"resource\",\"dimension\":%u,\"format\":%u,\"mips\":%u,\"array\":%u,"
            "\"usage\":%u,\"bind\":%u,\"map\":%u,\"misc\":%u,\"samples\":%u,\"quality\":%u,\"primary\":%s}\n",
            UINT(a->ResourceDimension), UINT(a->Format), a->MipLevels, a->ArraySize, UINT(a->Usage),
            a->BindFlags, a->MapFlags, a->MiscFlags, a->SampleDesc.Count, a->SampleDesc.Quality,
            a->pPrimaryDesc ? "true" : "false");
        if (a && a->pMipInfoList) lab_trace("{\"event\":\"resource_extent\",\"width\":%u,\"height\":%u,\"depth\":%u,\"initial_data\":%s}\n",
            a->pMipInfoList[0].TexelWidth, a->pMipInfoList[0].TexelHeight, a->pMipInfoList[0].TexelDepth,
            a->pInitialDataUP ? "true" : "false");
        if (a && a->ResourceDimension == D3D10DDIRESOURCE_BUFFER) {
            create_buffer(device(h), *a, *static_cast<Resource *>(out.pDrvPrivate)); return;
        }
        if (a && (a->MiscFlags || a->pPrimaryDesc || (a->BindFlags & D3D10_DDI_BIND_PRESENT))) {
            std::fprintf(stderr, "nr_fill_umd: resource dimension=%u mips=%u array=%u format=%u samples=%u/%u misc=%x usage=%u bind=%x map=%x primary=%u initial=%u runtime_resource=%u\n",
                UINT(a->ResourceDimension), a->MipLevels, a->ArraySize, UINT(a->Format), a->SampleDesc.Count,
                a->SampleDesc.Quality, a->MiscFlags, UINT(a->Usage), a->BindFlags, a->MapFlags,
                UINT(a->pPrimaryDesc != nullptr), UINT(a->pInitialDataUP != nullptr), UINT(rt.handle != nullptr));
            if (a->pMipInfoList) std::fprintf(stderr, "nr_fill_umd: resource extent=%ux%ux%u\n",
                a->pMipInfoList[0].TexelWidth, a->pMipInfoList[0].TexelHeight, a->pMipInfoList[0].TexelDepth);
        }
        constexpr UINT allowed_misc = D3D10_DDI_RESOURCE_MISC_SHARED | D3D10_DDI_RESOURCE_MISC_DISCARD_ON_PRESENT;
        require(a && a->ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D && a->MipLevels && a->MipLevels <= 14 &&
                a->ArraySize == 1 && a->pMipInfoList && (format_ok(a->Format) || a->Format == DXGI_FORMAT_A8_UNORM) && a->SampleDesc.Count == 1 &&
                !a->SampleDesc.Quality && !(a->MiscFlags & ~allowed_misc) && !a->pPrimaryDesc);
        const bool shared = (a->MiscFlags & D3D10_DDI_RESOURCE_MISC_SHARED) != 0;
        const bool presentation = (a->BindFlags & D3D10_DDI_BIND_PRESENT) != 0;
        require(a->Format != DXGI_FORMAT_A8_UNORM || (!shared && !presentation &&
                !(a->BindFlags & ~D3D10_DDI_BIND_SHADER_RESOURCE)));
        require(!(a->MiscFlags & D3D10_DDI_RESOURCE_MISC_DISCARD_ON_PRESENT) || presentation);
        require(!presentation || (a->BindFlags & D3D10_DDI_BIND_RENDER_TARGET));
        require(!shared || rt.handle);
        require(a->MipLevels == 1 || (!shared && !presentation && !a->MiscFlags));
        const bool staging = a->Usage == D3D10_DDI_USAGE_STAGING && !a->BindFlags && a->MapFlags && !(a->MapFlags & ~(D3D10_DDI_CPU_ACCESS_READ | D3D10_DDI_CPU_ACCESS_WRITE)) && !shared;
        constexpr UINT bindings = D3D10_DDI_BIND_RENDER_TARGET | D3D10_DDI_BIND_SHADER_RESOURCE | D3D10_DDI_BIND_PRESENT;
        const bool target = a->Usage == D3D10_DDI_USAGE_DEFAULT && a->BindFlags && !(a->BindFlags & ~bindings) && !a->MapFlags;
        const bool immutable=a->Usage==D3D10_DDI_USAGE_IMMUTABLE && a->BindFlags==D3D10_DDI_BIND_SHADER_RESOURCE && !a->MapFlags && !a->MiscFlags && a->pInitialDataUP;
        require((staging || target || immutable) && (!a->pInitialDataUP || target || immutable));
        const auto &base = a->pMipInfoList[0];
        require(base.TexelWidth && base.TexelWidth <= 8192 && base.TexelHeight && base.TexelHeight <= 8192);
        UINT max_levels = 1;
        for (UINT extent = (std::max)(base.TexelWidth, base.TexelHeight); extent > 1; extent >>= 1) ++max_levels;
        require(a->MipLevels <= max_levels);
        // Validate every level before creating any VidMm allocation. Each
        // unshared mip has independent storage and its own pitch/lock lifetime.
        for (UINT i = 0; i < a->MipLevels; ++i) {
            const auto &m = a->pMipInfoList[i];
            require(m.TexelDepth == 1 && m.TexelWidth == (std::max)(1u, base.TexelWidth >> i) &&
                    m.TexelHeight == (std::max)(1u, base.TexelHeight >> i));
            if(a->pInitialDataUP) require(a->pInitialDataUP[i].pSysMem && a->pInitialDataUP[i].SysMemPitch >= m.TexelWidth * texel_bytes(UINT(a->Format)));
        }
        std::vector<std::shared_ptr<Allocation>> levels;
        for (UINT i = 0; i < a->MipLevels; ++i) {
            const auto &m = a->pMipInfoList[i];
            auto allocation = std::make_shared<Allocation>(device(h).context);
            allocation->create(m.TexelWidth, m.TexelHeight, UINT(a->Format), shared ? rt.handle : nullptr);
            if(a->pInitialDataUP) upload(*allocation, nullptr, a->pInitialDataUP[i].pSysMem, a->pInitialDataUP[i].SysMemPitch);
            levels.push_back(std::move(allocation));
        }
        auto &r = *static_cast<Resource *>(out.pDrvPrivate);
        r.allocation = std::move(levels[0]);
        r.additional_mips.assign(std::make_move_iterator(levels.begin()+1), std::make_move_iterator(levels.end()));
        r.usage = a->Usage; r.map_flags = a->MapFlags;
        r.bind_flags = a->BindFlags;
    });
}
SIZE_T APIENTRY opened_resource_size(D3D10DDI_HDEVICE, const D3D10DDIARG_OPENRESOURCE *) { return sizeof(Resource); }
void APIENTRY open_resource(D3D10DDI_HDEVICE h, const D3D10DDIARG_OPENRESOURCE *a,
                           D3D10DDI_HRESOURCE out, D3D10DDI_HRTRESOURCE rt) {
    new (out.pDrvPrivate) Resource();
    guard(h, __FUNCTION__, [&] {
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
    guard(h, __FUNCTION__, [&] { static_cast<Resource *>(r.pDrvPrivate)->~Resource(); });
}
SIZE_T APIENTRY view_size(D3D10DDI_HDEVICE, const D3D10DDIARG_CREATERENDERTARGETVIEW *) { return sizeof(View); }
void APIENTRY create_view(D3D10DDI_HDEVICE h, const D3D10DDIARG_CREATERENDERTARGETVIEW *a,
                         D3D10DDI_HRENDERTARGETVIEW v, D3D10DDI_HRTRENDERTARGETVIEW) {
    new (v.pDrvPrivate) View();
    guard(h, __FUNCTION__, [&] {
        require(a != nullptr);
        auto &r = resource(a->hDrvResource);
        require(!r.buffer_bytes && format_ok(a->Format) && r.usage == D3D10_DDI_USAGE_DEFAULT && a->ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D &&
                UINT(a->Format) == r.allocation->surface.format && a->Tex2D.MipSlice < mip_count(r) &&
                !a->Tex2D.FirstArraySlice && a->Tex2D.ArraySize == 1);
        static_cast<View *>(v.pDrvPrivate)->allocation = mip_allocation(r, a->Tex2D.MipSlice);
    });
}
void APIENTRY destroy_view(D3D10DDI_HDEVICE h, D3D10DDI_HRENDERTARGETVIEW v) {
    guard(h, __FUNCTION__, [&] { static_cast<View *>(v.pDrvPrivate)->~View(); });
}
void APIENTRY clear(D3D10DDI_HDEVICE h, D3D10DDI_HRENDERTARGETVIEW v, FLOAT color[4]) {
    guard(h, __FUNCTION__, [&] {
        require(v.pDrvPrivate && color);
        auto a = static_cast<View *>(v.pDrvPrivate)->allocation;
        auto &d=device(h);require(a && !a->locked && a->context==d.context && !d.context->poisoned);
        for(unsigned n=0;n<4;++n)require(std::isfinite(color[n]));
        lab_trace("{\"event\":\"color_clear_requested\",\"width\":%u,\"height\":%u,\"format\":%u}\n",a->surface.width,a->surface.height,a->surface.format);
        // The historical blend-only file bridge retains its documented CPU
        // clears. Full desktop experiments use the indexed ABI 13 path.
        if(d.backend!="CPU" && d.backend!="NPU_LAB_FILE"){
            const auto &surface=a->surface;nr::raster::Color input{};std::copy_n(color,4,input.begin());
            std::vector<nr::raster::PackedColor> pixels;std::string execution_device;
            if(d.backend=="NPU_LAB_INDEXED"){
                // Clears join the open frame batch; the NPU fills the resident target and
                // the pixels reach VidMm at the next flush point.
                frame_batch_clear(d,a,input);
                ++counters.fills;++counters.executions;++counters.remote_npu_executions;
                lab_trace("{\"event\":\"color_clear_batched\",\"width\":%u,\"height\":%u}\n",surface.width,surface.height);
                return;
            }else{
                require(d.backend=="NPU" || d.backend=="GPU");
#ifdef NR_NATIVE_PROBE
                throw Error{DXGI_ERROR_UNSUPPORTED};
#else
                const std::array<uint32_t,3> key{surface.width,surface.height,surface.format};
                if(!d.clear_executor || d.clear_key!=key){
                    d.clear_executor.reset();d.clear_executor=std::make_unique<nr::raster::ClearExecutor>(key[0],key[1],key[2],d.backend);d.clear_key=key;
                }
                auto result=d.clear_executor->execute(input);pixels=std::move(result.pixels);execution_device=std::move(result.execution_device);
#endif
            }
            // VidMm upload/copy remains transport; every returned color byte was
            // generated and quantized on the explicitly selected provider.
            upload(*a,nullptr,pixels.data(),surface.width*4);++counters.fills;++counters.executions;
            if(d.backend=="NPU_LAB_INDEXED")++counters.remote_npu_executions;
            else if(d.backend=="NPU")++counters.npu_executions;else ++counters.gpu_executions;
            counters.inference_input_bytes+=16;counters.inference_output_bytes+=uint64_t(surface.width)*surface.height*4;
            strncpy_s(counters.last_execution_device,execution_device.c_str(),_TRUNCATE);
            lab_trace("{\"event\":\"color_clear_executed\",\"width\":%u,\"height\":%u,\"execution_device\":\"%s\"}\n",surface.width,surface.height,execution_device.c_str());
            return;
        }
        std::array<unsigned char, 4> bytes{};
        for (size_t i = 0; i < 4; ++i) {
            require(std::isfinite(color[i]));
            bytes[i] = static_cast<unsigned char>(std::nearbyint(std::clamp(color[i], 0.f, 1.f) * 255));
        }
        if (a->surface.format == UINT(DXGI_FORMAT_B8G8R8A8_UNORM)) std::swap(bytes[0], bytes[2]);
        ULONG pattern = 0;
        std::memcpy(&pattern, bytes.data(), sizeof(pattern));
        a->context->fill(a->handle, pattern);
        lab_trace("{\"event\":\"color_clear_executed\",\"width\":%u,\"height\":%u,\"execution_device\":\"CPU (kernel fill)\"}\n",a->surface.width,a->surface.height);
    });
}
void APIENTRY copy_resource(D3D10DDI_HDEVICE h, D3D10DDI_HRESOURCE dst, D3D10DDI_HRESOURCE src) {
    guard(h, __FUNCTION__, [&] {
        auto &d = resource(dst); auto &s = resource(src);
        require((d.usage == D3D10_DDI_USAGE_STAGING || d.usage == D3D10_DDI_USAGE_DEFAULT) && d.buffer_bytes == s.buffer_bytes &&
            (s.usage == D3D10_DDI_USAGE_DEFAULT || s.usage == D3D10_DDI_USAGE_STAGING || s.usage == D3D10_DDI_USAGE_IMMUTABLE || (s.buffer_bytes &&
             (s.usage == D3D10_DDI_USAGE_DYNAMIC || s.usage == D3D10_DDI_USAGE_IMMUTABLE))));
        require(mip_count(d) == mip_count(s));
        for (UINT sub = 0; sub < mip_count(d); ++sub) {
            const auto a = mip_allocation(d, sub), b = mip_allocation(s, sub);
            require(a != b && a->handle != b->handle && a->context == device(h).context && b->context == a->context &&
                a->surface.width == b->surface.width && a->surface.height == b->surface.height &&
                a->surface.format == b->surface.format && a->surface.row_pitch == b->surface.row_pitch &&
                a->surface.allocation_bytes == b->surface.allocation_bytes && !a->locked && !b->locked);
        }
        for (UINT sub = 0; sub < mip_count(d); ++sub) {
            const auto a = mip_allocation(d, sub), b = mip_allocation(s, sub);
            frame_batches_before_read(*b); frame_batches_before_write(*a);
            a->content_version = next_content_version(); a->mark_dirty_full();
            if (a->runtime_resource || b->runtime_resource) a->context->copy(b->handle, a->handle);
            else {
                // Explicit CPU staging, never counted as NPU rendering.
                Mapping read(*b, true), write(*a, false);
                std::memcpy(write.data, read.data, size_t(a->surface.allocation_bytes));
                write.finish(); read.finish();
            }
            counters.copies += a->surface.allocation_bytes;
        }
    });
}
void APIENTRY update_resource(D3D10DDI_HDEVICE h, D3D10DDI_HRESOURCE dst, UINT sub,
                             const D3D10_DDI_BOX *box, const void *source, UINT pitch, UINT) {
    guard(h, __FUNCTION__, [&] {
        auto &r = resource(dst);
        require(r.usage == D3D10_DDI_USAGE_DEFAULT && r.allocation->context == device(h).context);
        const auto allocation = mip_allocation(r, sub);
        if (r.buffer_bytes) {
            // A buffer may be a fetched texture of the open frame batch (WinUI glyph indices): settle it first.
            require(!box); frame_batches_before_write(*r.allocation); update_buffer(r, source);
            r.allocation->content_version = next_content_version(); r.allocation->mark_dirty_full(); return;
        }
        upload(*allocation, box, source, pitch); // CPU upload bytes are counted separately from submitted work.
    });
}
void APIENTRY map(D3D10DDI_HDEVICE h, D3D10DDI_HRESOURCE r, UINT sub, D3D10_DDI_MAP type,
                  UINT flags, D3D10DDI_MAPPED_SUBRESOURCE *out) {
    if (out) *out = {};
    guard(h, __FUNCTION__, [&] {
        auto &v = resource(r);
        lab_trace("{\"event\":\"resource_map\",\"subresource\":%u,\"type\":%u,\"flags\":%u,\"usage\":%u,\"cpu_access\":%u}\n",sub,UINT(type),flags,v.usage,v.map_flags);
        require(out && !(flags & ~D3D10_DDI_MAP_FLAG_DONOTWAIT) && v.allocation->context == device(h).context);
        const auto allocation = mip_allocation(v, sub);
        const bool wants_read=type==D3D10_DDI_MAP_READ || type==D3D10_DDI_MAP_READWRITE;
        const bool wants_write=type==D3D10_DDI_MAP_WRITE || type==D3D10_DDI_MAP_READWRITE;
        const bool staging=v.usage==D3D10_DDI_USAGE_STAGING && (wants_read || wants_write) &&
            (!wants_read || (v.map_flags&D3D10_DDI_CPU_ACCESS_READ)) &&
            (!wants_write || (v.map_flags&D3D10_DDI_CPU_ACCESS_WRITE));
        const bool geometry = (v.bind_flags & (D3D10_DDI_BIND_VERTEX_BUFFER | D3D10_DDI_BIND_INDEX_BUFFER)) != 0;
        const bool discard = v.buffer_bytes && (type == D3D10_DDI_MAP_WRITE_DISCARD ||
            (geometry && type == D3D10_DDI_MAP_WRITE_NOOVERWRITE)) &&
            v.usage == D3D10_DDI_USAGE_DYNAMIC && v.map_flags == D3D10_DDI_CPU_ACCESS_WRITE;
        const bool do_not_wait=(flags & D3D10_DDI_MAP_FLAG_DONOTWAIT)!=0;
        require(staging || (discard && !do_not_wait));
        // CPU access to a surface the open frame batch still writes or reads: settle the batch first.
        if (wants_read) frame_batches_before_read(*allocation); if (wants_write) frame_batches_before_write(*allocation);   // buffers too: a batch may fetch them
        try {
            out->pData = allocation->lock(staging && wants_read && !wants_write,staging && wants_read && wants_write,do_not_wait);
        } catch(const Error &e) {
            if(do_not_wait && (e.status==D3DDDIERR_WASSTILLDRAWING || e.status==DXGI_DDI_ERR_WASSTILLDRAWING || e.status==DXGI_ERROR_WAS_STILL_DRAWING)) {
                // Contention is retryable, not a device-fatal invalid argument.
                lab_trace("{\"event\":\"map_busy\",\"retryable\":true}\n");
                auto &d=device(h);d.callbacks.pfnSetErrorCb(d.core,DXGI_DDI_ERR_WASSTILLDRAWING);return;
            }
            throw;
        }
        v.mapped_write = discard || (staging && wants_write);
        out->RowPitch = allocation->surface.row_pitch;
        out->DepthPitch = UINT(allocation->surface.allocation_bytes);
    });
}
void APIENTRY unmap(D3D10DDI_HDEVICE h, D3D10DDI_HRESOURCE r, UINT sub) {
    guard(h, __FUNCTION__, [&] {
        auto &v = resource(r); const auto allocation = mip_allocation(v, sub); auto &a = *allocation;
        require(a.locked && a.context == device(h).context); check(a.unlock());
        if (v.mapped_write) counters.upload_bytes += v.buffer_bytes ? v.buffer_bytes :
            uint64_t(a.surface.width)*a.surface.height*texel_bytes(a.surface.format);
        if (v.mapped_write) { a.content_version = next_content_version(); a.mark_dirty_full(); }
        v.mapped_write = false;
    });
}
// The runtime constructs default pipeline state while initializing a device.
// Retain those descriptors even though ClearRenderTargetView ignores them.
// State is retained and validated against the admitted draw before execution.
#define NR_STATE(Name, Type, Handle, RtHandle) \
    SIZE_T APIENTRY Name##_size(D3D10DDI_HDEVICE, const Type *) { return sizeof(Type); } \
    void APIENTRY create_##Name(D3D10DDI_HDEVICE h, const Type *desc, Handle out, RtHandle) { \
        guard(h, __FUNCTION__, [&] { require(desc && out.pDrvPrivate); *static_cast<Type *>(out.pDrvPrivate) = *desc; }); \
    } \
    void APIENTRY destroy_##Name(D3D10DDI_HDEVICE, Handle) {}
NR_STATE(blend, D3D10_DDI_BLEND_DESC, D3D10DDI_HBLENDSTATE, D3D10DDI_HRTBLENDSTATE)
NR_STATE(depth, D3D10_DDI_DEPTH_STENCIL_DESC, D3D10DDI_HDEPTHSTENCILSTATE, D3D10DDI_HRTDEPTHSTENCILSTATE)
NR_STATE(raster, D3D10_DDI_RASTERIZER_DESC, D3D10DDI_HRASTERIZERSTATE, D3D10DDI_HRTRASTERIZERSTATE)
NR_STATE(sampler, D3D10_DDI_SAMPLER_DESC, D3D10DDI_HSAMPLER, D3D10DDI_HRTSAMPLER)
#undef NR_STATE
template<size_t Stage>
void APIENTRY set_samplers(D3D10DDI_HDEVICE h, UINT start, UINT count, const D3D10DDI_HSAMPLER *states) {
    guard(h, __FUNCTION__, [&] {
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
    guard(h, __FUNCTION__, [&] {
        auto &d = device(h);
        d.blend = state.pDrvPrivate ? *static_cast<D3D10_DDI_BLEND_DESC *>(state.pDrvPrivate) : D3D10_DDI_BLEND_DESC{};
        if (!state.pDrvPrivate) d.blend.RenderTargetWriteMask[0] = 15;
        for (UINT i = 0; i < 4; ++i) d.blend_factor[i] = factor ? factor[i] : 1.f;
        d.sample_mask = mask;
    });
}
void APIENTRY set_depth(D3D10DDI_HDEVICE h, D3D10DDI_HDEPTHSTENCILSTATE state, UINT ref) {
    guard(h, __FUNCTION__, [&] {
        auto &d = device(h);
        d.depth = state.pDrvPrivate ? *static_cast<D3D10_DDI_DEPTH_STENCIL_DESC *>(state.pDrvPrivate) : D3D10_DDI_DEPTH_STENCIL_DESC{};
        if (!state.pDrvPrivate) { d.depth.DepthEnable = TRUE; d.depth.DepthWriteMask = D3D10_DDI_DEPTH_WRITE_MASK_ALL; d.depth.DepthFunc = D3D10_DDI_COMPARISON_LESS; }
        d.stencil_ref = ref;
    });
}
void APIENTRY set_raster(D3D10DDI_HDEVICE h, D3D10DDI_HRASTERIZERSTATE state) {
    guard(h, __FUNCTION__, [&] {
        auto &d = device(h);
        d.raster = state.pDrvPrivate ? *static_cast<D3D10_DDI_RASTERIZER_DESC *>(state.pDrvPrivate) : D3D10_DDI_RASTERIZER_DESC{};
        if (!state.pDrvPrivate) { d.raster.FillMode = D3D10_DDI_FILL_SOLID; d.raster.CullMode = D3D10_DDI_CULL_BACK; d.raster.DepthClipEnable = TRUE; }
    });
}
void APIENTRY targets(D3D10DDI_HDEVICE h, const D3D10DDI_HRENDERTARGETVIEW *views, UINT count, UINT,
                      D3D10DDI_HDEPTHSTENCILVIEW depth) {
    guard(h, __FUNCTION__, [&] {
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
#include "copy_region.hpp"
void APIENTRY flush(D3D10DDI_HDEVICE h) { guard(h, __FUNCTION__, [&] { flush_frame_batches(device(h), "flush_ddi"); }); }
void APIENTRY hazard(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE) {} // Blocking locks order our staging copies.
void APIENTRY format_support(D3D10DDI_HDEVICE, DXGI_FORMAT f, UINT *out) {
    *out = format_ok(f) ? D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET | D3D10_DDI_FORMAT_SUPPORT_SHADER_SAMPLE |
                         D3D10_DDI_FORMAT_SUPPORT_BLENDABLE : (f == DXGI_FORMAT_A8_UNORM ? D3D10_DDI_FORMAT_SUPPORT_SHADER_SAMPLE : 0);
}
void APIENTRY multisample(D3D10DDI_HDEVICE, DXGI_FORMAT f, UINT samples, UINT *out) { *out = format_ok(f) && samples == 1 ? 1 : 0; }
void APIENTRY counter_info(D3D10DDI_HDEVICE, D3D10DDI_COUNTER_INFO *out) { *out = {}; }
void APIENTRY relocate(D3D10DDI_HDEVICE h, D3D10DDI_DEVICEFUNCS *out) { *out = device(h).functions; }
void APIENTRY destroy_device(D3D10DDI_HDEVICE h) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    frame_devices.erase(std::remove(frame_devices.begin(), frame_devices.end(), &device(h)), frame_devices.end());
    device(h).~Device();
}
// DXGI returns an HRESULT directly. A callback acceptance is not evidence of
// display delivery, and no local pixel copy substitutes for the callback.
HRESULT APIENTRY present(DXGI_DDI_ARG_PRESENT *a) {
    if (!a || !a->hDevice) return E_INVALIDARG;
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto &d = *reinterpret_cast<Device *>(a->hDevice);
    lab_trace("{\"event\":\"present_begin\",\"flags\":%u,\"source_subresource\":%u,\"destination_subresource\":%u,"
              "\"destination_bound\":%s,\"flip_interval\":%u,\"context_poisoned\":%s}\n",
              a->Flags.Value, a->SrcSubResourceIndex, a->DstSubResourceIndex,
              a->hDstResource ? "true" : "false", UINT(a->FlipInterval),
              d.context && d.context->poisoned ? "true" : "false");
    try {
        flush_frame_batches(d, "present");
        require((a->Flags.Value == 1 || a->Flags.Value == 2) && !a->SrcSubResourceIndex && !a->DstSubResourceIndex &&
                d.context && !d.context->poisoned && d.dxgi_callbacks &&
                d.dxgi_callbacks->pfnPresentCb && a->hSurfaceToPresent);
        D3D10DDI_HRESOURCE hs{}; hs.pDrvPrivate = reinterpret_cast<void *>(a->hSurfaceToPresent);
        const auto &source = resource(hs);
        require(source.usage == D3D10_DDI_USAGE_DEFAULT && source.allocation->context == d.context && !source.allocation->locked);
        if (a->Flags.Flip) {
            // DWM's indirect swapchain submits a shared PRESENT allocation.
            // DXGI retains flip/target metadata in its opaque callback context;
            // the runtime must perform the actual handoff. This does not add a
            // physical scanout engine or convert the request to a local blit.
            require(!a->hDstResource && !source.buffer_bytes && source.allocation->runtime_resource &&
                    (source.bind_flags & D3D10_DDI_BIND_PRESENT) && UINT(a->FlipInterval) <= 4);
        }
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
        lab_trace("{\"event\":\"present_callback_result\",\"status\":\"%08lx\",\"display_delivery_verified\":false}\n", ULONG(status));
        if (FAILED(status)) { d.context->poisoned = true; ++counters.rejected; }
        else ++counters.accepted_present_callbacks;
        return status;
    } catch (const Error &e) {
        lab_trace("{\"event\":\"present_rejected\",\"status\":\"%08lx\"}\n", ULONG(e.status));
        ++counters.rejected; return e.status;
    } catch (...) {
        lab_trace("{\"event\":\"present_rejected\",\"status\":\"%08lx\"}\n", ULONG(E_FAIL));
        ++counters.rejected; return E_FAIL;
    }
}
HRESULT APIENTRY rotate_resources(DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES *a) {
    if (!a || !a->hDevice) return E_INVALIDARG;
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto &d = *reinterpret_cast<Device *>(a->hDevice);
    try {
        require(d.context && !d.context->poisoned && a->pResources && a->Resources && a->Resources <= 16);
        std::array<Allocation *,16> allocations{};
        for (UINT i = 0; i < a->Resources; ++i) {
            D3D10DDI_HRESOURCE h{}; h.pDrvPrivate = reinterpret_cast<void *>(a->pResources[i]);
            const auto &r = resource(h); auto *allocation = r.allocation.get();
            require(!r.buffer_bytes && r.usage == D3D10_DDI_USAGE_DEFAULT && (r.bind_flags & D3D10_DDI_BIND_PRESENT) &&
                    !r.mapped_write && allocation->context == d.context && allocation->handle && !allocation->locked && !allocation->mapping);
            for (UINT j = 0; j < i; ++j) require(allocations[j] != allocation && allocations[j]->handle != allocation->handle);
            if (i) require(std::memcmp(&allocation->surface,&allocations[0]->surface,sizeof(allocation->surface)) == 0 &&
                           bool(allocation->runtime_resource) == bool(allocations[0]->runtime_resource));
            allocations[i] = allocation;
        }
        // Validate the complete list before changing any identity. Views and
        // current bindings retain these Allocation objects, so they observe the
        // new backing handle on their next use without stale embedded addresses.
        // DXGI rotates the runtime's backing association separately: do not
        // rotate runtime_resource, copy pixels, or replace the resource objects.
        for (UINT i = 0; i < a->Resources; ++i) frame_batches_before_read(*allocations[i]);
        // The frame batch identity, versions and dirty state describe the backing
        // pixels, so they rotate with the handle and the worker's resident copies stay valid.
        struct Sync { D3DKMT_HANDLE handle; uint64_t id, content_version, worker_version; int32_t dirty[4]; bool dirty_full; std::vector<uint8_t> shadow; };
        auto take = [](Allocation &x) { return Sync{x.handle, x.id, x.content_version, x.worker_version, {x.dirty_x0, x.dirty_y0, x.dirty_x1, x.dirty_y1}, x.dirty_full, std::move(x.shadow)}; };
        auto give = [](Allocation &x, Sync &&s) { x.handle = s.handle; x.id = s.id; x.content_version = s.content_version; x.worker_version = s.worker_version; x.dirty_x0 = s.dirty[0]; x.dirty_y0 = s.dirty[1]; x.dirty_x1 = s.dirty[2]; x.dirty_y1 = s.dirty[3]; x.dirty_full = s.dirty_full; x.shadow = std::move(s.shadow); };
        auto first = take(*allocations[0]);
        for (UINT i = 1; i < a->Resources; ++i) give(*allocations[i-1], take(*allocations[i]));
        give(*allocations[a->Resources-1], std::move(first));
        lab_trace("{\"event\":\"resource_identities_rotated\",\"resources\":%u}\n", a->Resources);
        return S_OK;
    } catch (const Error &e) {
        lab_trace("{\"event\":\"rotate_rejected\",\"status\":\"%08lx\"}\n",ULONG(e.status));
        ++counters.rejected; return e.status;
    } catch (...) { ++counters.rejected; return E_FAIL; }
}
SIZE_T APIENTRY device_size(D3D10DDI_HADAPTER, const D3D10DDIARG_CALCPRIVATEDEVICESIZE *) { return sizeof(Device); }
HRESULT APIENTRY create_device(D3D10DDI_HADAPTER, D3D10DDIARG_CREATEDEVICE *a) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (a) lab_trace("{\"event\":\"create_device\",\"interface\":%u,\"version\":%u}\n", a->Interface, a->Version);
    if (!a || !a->hDrvDevice.pDrvPrivate || !a->pUMCallbacks || !a->pUMCallbacks->pfnSetErrorCb ||
        !a->pKTCallbacks || !a->pDeviceFuncs || !a->DXGIBaseDDI.pDXGIDDIBaseFunctions) return E_INVALIDARG;
    if (a->Interface != D3D10_0_DDI_INTERFACE_VERSION && a->Interface != D3D10_0_x_DDI_INTERFACE_VERSION &&
        a->Interface != D3D10_0_7_DDI_INTERFACE_VERSION) return E_NOINTERFACE;
    auto d = new (a->hDrvDevice.pDrvPrivate) Device();
    frame_devices.push_back(d);
    HRESULT status = E_FAIL;
    try {
        d->core = a->hRTCoreLayer; d->callbacks = *a->pUMCallbacks;
        d->context = std::make_shared<Context>(counters);
        d->context->create(a->hRTDevice.handle, *a->pKTCallbacks);
        // The runtime may replace table entries between UMD calls. Retain the
        // table address, and read only the entry supported by this DDI version.
        d->dxgi_callbacks = a->DXGIBaseDDI.pDXGIBaseCallbacks;
#ifdef NR_NATIVE_PROBE
        if (!indexed_exchange_configured()) throw Error{DXGI_ERROR_UNSUPPORTED};
        d->backend = "NPU_LAB_INDEXED";
#else
        d->backend = selected_backend;
        if (d->backend == "NPU" && indexed_exchange_configured()) d->backend = "NPU_LAB_INDEXED";
#endif
        d->vertex_code = shader_tokens(nr_ddi_vertex_shader, "vs_4_0");
        d->pixel_code = shader_tokens(nr_ddi_pixel_shader, "ps_4_0");
        auto &f = d->functions;
#define NR_REJECT(field) f.field = &Unsupported<decltype(f.field), offsetof(D3D10DDI_DEVICEFUNCS, field)>::call
#include "../ddi/unsupported.inc"
#undef NR_REJECT
        f.pfnCalcPrivateResourceSize = resource_size; f.pfnCreateResource = create_resource;
        f.pfnCalcPrivateOpenedResourceSize = opened_resource_size; f.pfnOpenResource = open_resource;
        f.pfnDestroyResource = destroy_resource; f.pfnResourceCopy = copy_resource;
        f.pfnResourceCopyRegion = copy_region;
        f.pfnResourceUpdateSubresourceUP = update_resource;
        f.pfnDefaultConstantBufferUpdateSubresourceUP = update_resource;
        f.pfnResourceMap = f.pfnStagingResourceMap = map;
        f.pfnResourceUnmap = f.pfnStagingResourceUnmap = unmap;
        f.pfnDynamicConstantBufferMapDiscard = map;
        f.pfnDynamicConstantBufferUnmap = unmap;
        f.pfnDynamicIABufferMapDiscard = f.pfnDynamicIABufferMapNoOverwrite = map;
        f.pfnDynamicIABufferUnmap = unmap;
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
        dxgi.pfnRotateResourceIdentities = rotate_resources; NR_DXGI(pfnBlt);
#undef NR_DXGI
        *a->pDeviceFuncs = f;
        // This adapter has no shared D3D9 hardware implementation. The DXGI
        // contract requires CPU-accessible presentation instead of D3D9/DWM
        // resource redirection, even when the selected draw provider is NPU.
        lab_trace("{\"event\":\"device_created\",\"status\":\"%08lx\"}\n", ULONG(DXGI_STATUS_NO_REDIRECTION));
        return DXGI_STATUS_NO_REDIRECTION;
    } catch (const Error &e) { status = e.status; }
    catch (const std::bad_alloc &) { status = E_OUTOFMEMORY; }
    catch (...) {}
    d->~Device();
    lab_trace("{\"event\":\"device_failed\",\"status\":\"%08lx\"}\n", ULONG(status));
    return status;
}
HRESULT APIENTRY close_adapter(D3D10DDI_HADAPTER) { return S_OK; }
HRESULT APIENTRY versions(D3D10DDI_HADAPTER, UINT32 *count, UINT64 *out) {
    lab_trace("{\"event\":\"versions\",\"output_requested\":%s}\n", out ? "true" : "false");
    const UINT64 supported[] = {D3D10_0_DDI_SUPPORTED, D3D10_0_x_DDI_SUPPORTED, D3D10_0_7_DDI_SUPPORTED};
    if (!count) return E_INVALIDARG;
    if (out && *count < 3) return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    *count = 3;
    if (out) std::memcpy(out, supported, sizeof(supported));
    return S_OK;
}
HRESULT APIENTRY caps(D3D10DDI_HADAPTER, const D3D10_2DDIARG_GETCAPS *a) {
    if (!a || !a->pData) return E_INVALIDARG;
    lab_trace("{\"event\":\"caps\",\"type\":%u,\"bytes\":%u}\n", UINT(a->Type), a->DataSize);
    std::memset(a->pData, 0, a->DataSize);
    if (a->Type == D3D11DDICAPS_3DPIPELINESUPPORT && a->DataSize == sizeof(D3D11DDI_3DPIPELINESUPPORT_CAPS))
        static_cast<D3D11DDI_3DPIPELINESUPPORT_CAPS *>(a->pData)->Caps = D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3D11DDI_3DPIPELINELEVEL_10_0);
    return S_OK; // Negotiation for this restricted test is not feature-level conformance.
}
} // namespace
extern "C" __declspec(dllexport) HRESULT APIENTRY OpenAdapter10(D3D10DDIARG_OPENADAPTER *a) {
    if (!a || !a->pAdapterFuncs || !a->pAdapterCallbacks || !a->pAdapterCallbacks->pfnQueryAdapterInfoCb) return E_INVALIDARG;
#ifdef NR_NATIVE_PROBE
    if (!indexed_exchange_configured()) return DXGI_ERROR_UNSUPPORTED;
#else
    if (!nr_fill_guest()) return DXGI_ERROR_UNSUPPORTED;
#endif
    lab_trace("{\"event\":\"open_adapter\",\"interface\":%u,\"version\":%u}\n", a->Interface, a->Version);
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
#ifdef NR_NATIVE_PROBE
    if (!backend || counters.live_contexts || counters.live_allocations ||
        (std::strcmp(backend, "NPU") && std::strcmp(backend, "NPU_LAB_INDEXED")) || !indexed_exchange_configured()) return FALSE;
    try { selected_backend = "NPU_LAB_INDEXED"; return TRUE; } catch (...) { return FALSE; }
#else
    if (!backend || counters.live_contexts || counters.live_allocations ||
        (std::strcmp(backend, "CPU") && std::strcmp(backend, "GPU") && std::strcmp(backend, "NPU") &&
         ((std::strcmp(backend, "NPU_LAB_FILE") && std::strcmp(backend, "NPU_LAB_INDEXED")) || !nr_fill_guest()))) return FALSE;
    try { selected_backend = backend; return TRUE; } catch (...) { return FALSE; }
#endif
}
