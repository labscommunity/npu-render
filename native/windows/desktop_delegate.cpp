// A cooperating-window compositor handoff, not a replacement of dwm.exe.
#include "live_renderer.hpp"
#include "opaque_model.hpp"
#include <dwmapi.h>

struct DelegateOptions {
    UINT width = 1280, height = 720, fps = 60;
    double seconds = 30, switch_every = 3;
    std::string mode = "DWM", report = "desktop-delegate.json", ready, stop;
    std::string expected_driver, workload = "DEFAULT";
    std::string graph = "source-over", placement = "staged";
    int64_t tiles = 0;
    std::vector<std::string> sequence;
    bool validate_only = false, pinned = false;
};

static bool mode_valid(const std::string &mode) { return mode == "DWM" || mode == "NPU" || mode == "GPU"; }
static DelegateOptions options(int argc, char **argv) {
    DelegateOptions o;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--validate-only") { o.validate_only = true; continue; }
        if (++i == argc) throw std::invalid_argument("missing option value");
        const std::string value = argv[i];
        if (arg == "--mode") { o.mode = value; o.pinned = true; }
        else if (arg == "--width") o.width = std::stoul(value);
        else if (arg == "--height") o.height = std::stoul(value);
        else if (arg == "--fps") o.fps = std::stoul(value);
        else if (arg == "--seconds") o.seconds = std::stod(value);
        else if (arg == "--report") o.report = value;
        else if (arg == "--ready-file") o.ready = value;
        else if (arg == "--stop-file") o.stop = value;
        else if (arg == "--expected-npu-driver") o.expected_driver = value;
        else if (arg == "--npu-workload") o.workload = value;
        else if (arg == "--npu-graph") o.graph = value;
        else if (arg == "--placement") o.placement = value;
        else if (arg == "--npu-tiles") o.tiles = std::stoll(value);
        else if (arg == "--switch-every") o.switch_every = std::stod(value);
        else if (arg == "--sequence") {
            std::istringstream input(value);
            for (std::string mode; std::getline(input, mode, ',');) o.sequence.push_back(mode);
        } else throw std::invalid_argument("unknown option " + arg);
    }
    if (o.width < 640 || o.width > 1920 || o.width % 64 || o.height < 360 || o.height > 1080 ||
        !o.fps || o.fps > 120 || !std::isfinite(o.seconds) || o.seconds <= 0 || o.seconds > 1800 ||
        !std::isfinite(o.switch_every) || o.switch_every <= 0 || !mode_valid(o.mode) || o.tiles < 0 ||
        (o.workload != "DEFAULT" && o.workload != "EFFICIENT") ||
        (o.graph != "source-over" && o.graph != "opaque-select") ||
        (o.placement != "staged" && o.placement != "retained"))
        throw std::invalid_argument("invalid dimensions, timing, mode or NPU settings");
    return o;
}

// All sources belong to this process and expose their own immutable GDI surface.
// No screen scraping, foreign HWND mutation or application input redirection.
struct SourceWindow {
    UINT width, height;
    HWND handle = nullptr;
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ old_bitmap = nullptr;
    uint8_t *pixels = nullptr;
    unsigned paints = 0;
    SourceWindow(UINT w, UINT h, unsigned index) : width(w), height(h) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = LONG(w); info.bmiHeader.biHeight = -LONG(h);
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        dc = CreateCompatibleDC(nullptr);
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, reinterpret_cast<void **>(&pixels), nullptr, 0);
        if (!dc || !bitmap) throw std::runtime_error("cannot allocate source surface");
        old_bitmap = SelectObject(dc, bitmap);
        for (UINT y = 0; y < h; ++y) for (UINT x = 0; x < w; ++x) {
            const size_t p = (size_t(y)*w+x)*4;
            pixels[p] = uint8_t(65 + index*40 + (x/16)%2*15);
            pixels[p+1] = uint8_t(45 + index*35 + (y/16)%2*15);
            pixels[p+2] = uint8_t(35 + index*45); pixels[p+3] = 255;
        }
        SetTextColor(dc, RGB(255,255,255)); SetBkMode(dc, TRANSPARENT);
        RECT rect{12,12,LONG(w)-12,LONG(h)-12};
        const auto label = L"Window " + std::to_wstring(index+1) + L"\nGDI surface\nSame pixels in DWM / NPU / GPU modes";
        DrawTextW(dc, label.c_str(), -1, &rect, DT_LEFT | DT_WORDBREAK);
        GdiFlush();
        for (size_t p = 3; p < size_t(w)*h*4; p += 4) pixels[p] = 255;
    }
    ~SourceWindow() {
        if (handle) DestroyWindow(handle);
        if (dc && old_bitmap) SelectObject(dc, old_bitmap);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
    }
    SourceWindow(const SourceWindow &) = delete;
    static LRESULT CALLBACK procedure(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto *self = reinterpret_cast<SourceWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            self = static_cast<SourceWindow *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self && msg == WM_PAINT) {
            PAINTSTRUCT paint{}; HDC target = BeginPaint(hwnd, &paint);
            BitBlt(target, 0, 0, self->width, self->height, self->dc, 0, 0, SRCCOPY);
            EndPaint(hwnd, &paint); ++self->paints; return 0;
        }
        if (msg == WM_CLOSE || (msg == WM_KEYDOWN && wp == VK_ESCAPE)) { PostQuitMessage(0); return 0; }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    void create(HWND owner) {
        WNDCLASSW klass{}; klass.lpfnWndProc = procedure; klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpszClassName = L"NpuRenderDelegatedSource";
        if (!RegisterClassW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("source class registration failed");
        handle = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, klass.lpszClassName,
            L"NPU Render cooperating source", WS_POPUP, 0,0,width,height,owner,nullptr,klass.hInstance,this);
        if (!handle) throw std::runtime_error("source window creation failed");
        const BOOL disable_transitions = TRUE;
        check(DwmSetWindowAttribute(handle,DWMWA_TRANSITIONS_FORCEDISABLED,&disable_transitions,
            sizeof(disable_transitions)),"source transition policy");
        const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_DONOTROUND;
        check(DwmSetWindowAttribute(handle,DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners)),
            "source rectangular surface policy");
    }
};

using Positions = std::array<POINT, 3>;
using Sources = std::array<std::unique_ptr<SourceWindow>, 3>;

static Positions positions(double t, UINT width, UINT height, UINT sw, UINT sh) {
    Positions result{};
    for (size_t i = 0; i < result.size(); ++i) {
        const double phase = t*1.7 + i*2.1;
        result[i] = {LONG((width-sw)*(0.5+0.45*std::sin(phase))),
                     LONG((height-sh)*(0.5+0.45*std::cos(phase*0.8)))};
    }
    return result;
}

static void place(std::vector<uint8_t> &dst, UINT width, UINT height, const SourceWindow &src, POINT point) {
    // Placement copies each source into its own transparent plane. It never
    // resolves overlap between sources; that occurs in the selected compositor.
    const LONG left = std::max(0L, point.x), top = std::max(0L, point.y);
    const LONG right = std::min(LONG(width), point.x+LONG(src.width));
    const LONG bottom = std::min(LONG(height), point.y+LONG(src.height));
    for (LONG y = top; y < bottom && left < right; ++y)
        std::memcpy(dst.data()+(size_t(y)*width+left)*4,
            src.pixels+(size_t(y-point.y)*src.width+(left-point.x))*4, size_t(right-left)*4);
}

struct LayerComposer {
    Renderer &r;
    std::array<std::unique_ptr<SharedBuffer>, 2> intermediate;
    std::array<std::unique_ptr<SharedBuffer>, 3> foregrounds, source_buffers;
    std::unique_ptr<SharedBuffer> transparent;
    bool retained, have_previous = false;
    Positions previous{};
    size_t preparation_upload_bytes = 0, frame_host_upload_bytes = 0, frame_device_copy_bytes = 0;
    ov::CompiledModel compiled;
    // Destroy requests/tensors before their imported heaps.
    std::array<std::array<ov::Tensor, 3>, 3> tensors;
    std::array<ov::InferRequest, 3> requests;
    unsigned oracle_max_error = 0;
    explicit LayerComposer(Renderer &renderer, const std::string &graph, bool retain, const Sources &sources)
        : r(renderer), retained(retain) {
        for (auto &buffer : intermediate) buffer = std::make_unique<SharedBuffer>(r.device.Get(), r.bytes);
        if (retained) {
            for (auto &buffer : foregrounds) buffer = std::make_unique<SharedBuffer>(r.device.Get(),r.bytes);
            transparent = std::make_unique<SharedBuffer>(r.device.Get(),r.bytes);
            std::array<ComPtr<ID3D12Resource>,4> uploads;
            auto &t = *r.transfers; t.begin();
            const auto copy = [&](size_t index, SharedBuffer &dst, const void *data, size_t bytes) {
                uploads[index] = staging(r.device.Get(),bytes,D3D12_HEAP_TYPE_UPLOAD);
                upload(uploads[index].Get(),data,bytes);
                t.barrier(dst.resource.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
                t.list->CopyBufferRegion(dst.resource.Get(),0,uploads[index].Get(),0,bytes);
                t.barrier(dst.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
                preparation_upload_bytes += bytes;
            };
            std::vector<uint8_t> zeros(r.bytes,0);
            copy(3,*transparent,zeros.data(),r.bytes);
            for (size_t i = 0; i < sources.size(); ++i) {
                const size_t bytes = size_t(sources[i]->width)*sources[i]->height*4;
                source_buffers[i] = std::make_unique<SharedBuffer>(r.device.Get(),bytes);
                copy(i,*source_buffers[i],sources[i]->pixels,bytes);
                t.barrier(foregrounds[i]->resource.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
                t.list->CopyBufferRegion(foregrounds[i]->resource.Get(),0,transparent->resource.Get(),0,r.bytes);
                t.barrier(foregrounds[i]->resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
            }
            t.finish(); // Upload resources remain alive until completion.
        }
        compiled = r.npu->compiled;
        if (graph == "opaque-select") {
            ov::AnyMap settings;
            if (r.npu->requested_tiles) settings.emplace(ov::intel_npu::tiles(r.npu->requested_tiles));
            if (r.npu->workload == "EFFICIENT") settings.emplace(ov::workload_type(ov::WorkloadType::EFFICIENT));
            compiled = r.npu->core.compile_model(opaque_model(r.width,r.height),"NPU",settings);
            const auto devices = compiled.get_property(ov::execution_devices);
            if (devices.empty() || std::any_of(devices.begin(),devices.end(),[](const auto &device) {
                return device != "NPU" && device.rfind("NPU.",0) != 0;
            })) throw std::runtime_error("opaque graph did not compile for NPU exclusively");
        }
        auto context = compiled.get_context();
        auto tensor = [&](SharedBuffer &buffer) {
            return context.create_tensor(ov::element::u8, {1,r.height,r.width,4},
                {{"MEM_TYPE",std::string("SHARED_BUF")},{"MEM_HANDLE",static_cast<void *>(buffer.shared.value)},
                 {"TENSOR_TYPE",std::string("BINDED")}});
        };
        for (size_t i = 0; i < 3; ++i) {
            tensors[i] = {tensor(input(i)),tensor(foreground(i)),tensor(output(i))};
            requests[i] = compiled.create_infer_request();
            requests[i].set_input_tensor(0,tensors[i][0]); requests[i].set_input_tensor(1,tensors[i][1]);
            requests[i].set_output_tensor(tensors[i][2]);
        }
    }
    SharedBuffer &foreground(size_t i) { return retained ? *foregrounds[i] : *r.fg; }
    SharedBuffer &input(size_t i) { return i ? *intermediate[i-1] : *r.bg; }
    SharedBuffer &output(size_t i) { return i == 2 ? *r.out : *intermediate[i]; }
    std::array<double, 3> compose(const std::string &mode, const Sources &sources, const Positions &p) {
        std::array<double, 3> times{};
        frame_host_upload_bytes = 0; frame_device_copy_bytes = 0;
        if (retained) {
            // Copy retained source rows into separate transparent planes. GPU
            // copies position each source; only the selected blend resolves overlap.
            const auto begin = Clock::now(); auto &t = *r.transfers; t.begin();
            for (size_t i = 0; i < sources.size(); ++i) {
                auto &fg = foreground(i); const auto &src = *sources[i];
                t.barrier(fg.resource.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
                const auto bounds = [&](POINT point) {
                    return RECT{std::max(0L,point.x),std::max(0L,point.y),
                        std::min(LONG(r.width),point.x+LONG(src.width)),
                        std::min(LONG(r.height),point.y+LONG(src.height))};
                };
                const auto clear = [&](RECT rect) {
                    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
                    for (LONG y = rect.top; y < rect.bottom; ++y) {
                        const size_t offset = (size_t(y)*r.width+rect.left)*4;
                        const size_t bytes = size_t(rect.right-rect.left)*4;
                        t.list->CopyBufferRegion(fg.resource.Get(),offset,transparent->resource.Get(),offset,bytes);
                        frame_device_copy_bytes += bytes;
                    }
                };
                if (have_previous) {
                    const RECT old = bounds(previous[i]), next = bounds(p[i]);
                    const RECT overlap{std::max(old.left,next.left),std::max(old.top,next.top),
                        std::min(old.right,next.right),std::min(old.bottom,next.bottom)};
                    if (overlap.right <= overlap.left || overlap.bottom <= overlap.top) clear(old);
                    else {
                        // Only vacated pixels need zeroing; the next source copy
                        // replaces the intersection without resolving other layers.
                        clear({old.left,old.top,old.right,overlap.top});
                        clear({old.left,overlap.bottom,old.right,old.bottom});
                        clear({old.left,overlap.top,overlap.left,overlap.bottom});
                        clear({overlap.right,overlap.top,old.right,overlap.bottom});
                    }
                }
                const LONG left = std::max(0L,p[i].x), top = std::max(0L,p[i].y);
                const LONG right = std::min(LONG(r.width),p[i].x+LONG(src.width));
                const LONG bottom = std::min(LONG(r.height),p[i].y+LONG(src.height));
                for (LONG y = top; y < bottom && left < right; ++y) {
                    const size_t bytes = size_t(right-left)*4;
                    t.list->CopyBufferRegion(fg.resource.Get(),(size_t(y)*r.width+left)*4,
                        source_buffers[i]->resource.Get(),(size_t(y-p[i].y)*src.width+left-p[i].x)*4,bytes);
                    frame_device_copy_bytes += bytes;
                }
                t.barrier(fg.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
            }
            const auto recorded = Clock::now(); t.finish();
            times[0] = milliseconds(begin,recorded); times[1] = milliseconds(recorded);
            previous = p; have_previous = true;
        }
        for (size_t i = 0; i < sources.size(); ++i) {
            const auto begin = Clock::now();
            if (!retained) {
                std::fill(r.f.begin(), r.f.end(), 0); place(r.f,r.width,r.height,*sources[i],p[i]);
                const auto placed = Clock::now();
                r.transfers->begin(); r.copy_input(r.staging_fg.Get(), *r.fg, r.f); r.transfers->finish();
                times[0] += milliseconds(begin,placed); times[1] += milliseconds(placed);
                frame_host_upload_bytes += r.bytes; frame_device_copy_bytes += r.bytes;
            }
            const auto uploaded = Clock::now();
            if (mode == "NPU") requests[i].infer();
            else if (mode == "GPU") r.gpu->dispatch(*r.transfers,input(i),foreground(i),output(i),r.pixels);
            else throw std::invalid_argument("not a delegated compositor mode");
            times[2] += milliseconds(uploaded);
        }
        return times;
    }
    std::vector<uint8_t> oracle(const Sources &sources, const Positions &p) {
        auto expected = r.b;
        // Independent opaque-window oracle: later source pixels replace earlier
        // ones directly, without using the blend graph or transparent planes.
        for (size_t i = 0; i < sources.size(); ++i) {
            const auto &source = *sources[i];
            for (UINT y = 0; y < source.height; ++y) for (UINT x = 0; x < source.width; ++x) {
                const LONG dx = p[i].x + LONG(x), dy = p[i].y + LONG(y);
                if (dx < 0 || dy < 0 || dx >= LONG(r.width) || dy >= LONG(r.height)) continue;
                for (size_t c = 0; c < 4; ++c)
                    expected[(size_t(dy)*r.width+dx)*4+c] = source.pixels[(size_t(y)*source.width+x)*4+c];
            }
        }
        return expected;
    }
    void qualify(const Sources &sources) {
        const std::array<Positions, 4> fixtures{{positions(0,r.width,r.height,sources[0]->width,sources[0]->height),
            Positions{{{0,0},{0,0},{0,0}}},
            Positions{{{-100,-80},{LONG(r.width)-50,LONG(r.height)-40},{200,150}}},
            Positions{{{LONG(r.width)+10,0},{0,LONG(r.height)+10},{-500,-500}}}}};
        for (const auto &p : fixtures) for (const std::string mode : {"GPU","NPU"}) {
            compose(mode,sources,p); const auto expected = oracle(sources,p);
            auto &t = *r.transfers; t.begin();
            t.barrier(r.out->resource.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
            t.list->CopyBufferRegion(r.readback.Get(),0,r.out->resource.Get(),0,r.bytes);
            t.barrier(r.out->resource.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON); t.finish();
            void *data = nullptr; D3D12_RANGE range{0,r.bytes};
            check(r.readback->Map(0,&range,&data),"oracle readback");
            for (size_t i = 0; i < r.bytes; ++i) oracle_max_error = std::max(oracle_max_error,
                unsigned(std::abs(int(static_cast<uint8_t *>(data)[i])-int(expected[i]))));
            D3D12_RANGE no_write{0,0}; r.readback->Unmap(0,&no_write);
            if (oracle_max_error) throw std::runtime_error(mode+" opaque-window oracle mismatch");
        }
    }
};

static unsigned visible_count(const Sources &sources) {
    unsigned count = 0;
    for (const auto &source : sources) if (IsWindowVisible(source->handle)) ++count;
    return count;
}

static void show_sources(const Sources &sources, HWND canvas, const Positions &p, bool show) {
    POINT origin{0,0}; if (!ClientToScreen(canvas,&origin)) throw std::runtime_error("canvas position unavailable");
    for (size_t i = 0; i < sources.size(); ++i) {
        if (show) {
            if (!SetWindowPos(sources[i]->handle,HWND_TOP,origin.x+p[i].x,origin.y+p[i].y,0,0,
                    SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW |
                    (IsWindowVisible(sources[i]->handle) ? SWP_NOZORDER : 0)))
                throw std::runtime_error("source handoff failed");
            UpdateWindow(sources[i]->handle);
        } else ShowWindow(sources[i]->handle,SW_HIDE);
    }
    if (visible_count(sources) != (show ? 3u : 0u)) throw std::runtime_error("source visibility mismatch");
}

static unsigned screen_error(HWND canvas, const Renderer &r, const std::vector<uint8_t> &expected) {
    // Read only this test canvas. Retain error counts, never desktop pixels.
    POINT origin{0,0}; RECT client{};
    if (!ClientToScreen(canvas,&origin) || !GetClientRect(canvas,&client) ||
        client.right != LONG(r.width) || client.bottom != LONG(r.height))
        throw std::runtime_error("screen check requires an unscaled test canvas");
    HDC screen = GetDC(nullptr), target = CreateCompatibleDC(screen);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = LONG(r.width); info.bmiHeader.biHeight = -LONG(r.height);
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void *pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(target,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if (!screen || !target || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (target) DeleteDC(target);
        if (screen) ReleaseDC(nullptr,screen);
        throw std::runtime_error("screen check allocation failed");
    }
    HGDIOBJ old = SelectObject(target,bitmap);
    const bool copied = BitBlt(target,0,0,r.width,r.height,screen,origin.x,origin.y,SRCCOPY) != FALSE;
    GdiFlush();
    unsigned maximum = 0;
    size_t mismatches = 0;
    if (copied) for (size_t i = 0; i < r.bytes; ++i) if (i%4 != 3)
    {
        const unsigned error = unsigned(std::abs(int(static_cast<uint8_t *>(pixels)[i])-int(expected[i])));
        maximum = std::max(maximum,error);
        if (error > 1) {
            if (mismatches < 12) std::cerr << "screen mismatch x=" << (i/4)%r.width << " y=" << i/(r.width*4)
                << " c=" << i%4 << " actual=" << unsigned(static_cast<uint8_t *>(pixels)[i])
                << " expected=" << unsigned(expected[i]) << "\n";
            ++mismatches;
        }
    }
    if (mismatches) std::cerr << "screen differing channels=" << mismatches << " origin=" << origin.x << "," << origin.y << "\n";
    SelectObject(target,old); DeleteObject(bitmap); DeleteDC(target); ReleaseDC(nullptr,screen);
    if (!copied) throw std::runtime_error("screen check copy failed");
    return maximum;
}

int main(int argc, char **argv) {
    std::string stage = "options";
    try {
        const auto o = options(argc,argv);
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        DWORD session = 0; ProcessIdToSessionId(GetCurrentProcessId(),&session);
        if (!o.validate_only && !session) throw std::runtime_error("interactive desktop required");
        if (!o.validate_only) {
            HDESK input = OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
            wchar_t name[256]{}; DWORD needed = 0;
            const bool active = input && GetUserObjectInformationW(input,UOI_NAME,name,sizeof(name),&needed) &&
                std::wstring(name) == L"Default";
            if (input) CloseDesktop(input);
            BOOL enabled = FALSE;
            if (!active || FAILED(DwmIsCompositionEnabled(&enabled)) || !enabled)
                throw std::runtime_error("requires an unlocked Default input desktop with DWM enabled");
        }
        stage = "preparation";
        Renderer r(o.width,o.height,o.expected_driver,"convolution",false,o.workload,o.tiles);
        r.qualify(); r.background(); std::fill(r.f.begin(),r.f.end(),0); r.inputs(true);
        Sources sources;
        for (unsigned i = 0; i < 3; ++i) sources[i] = std::make_unique<SourceWindow>(o.width/3,o.height/3,i);
        LayerComposer compositor(r,o.graph,o.placement == "retained",sources); compositor.qualify(sources);
        std::vector<std::string> frames, selections, screen_checks;
        size_t skipped = 0, misses = 0, slot = 0, selection = 0, moves = 0;
        double started_unix_s = 0, live_seconds = 0, cpu_seconds = 0;
        bool cancelled = false, recovered = false;
        std::string mode = "DWM", recovery_error;
        unsigned restored = 0, end_screen_max_error = 0;
        if (!o.validate_only) {
            stage = "handoff";
            Window window(r); window.allow_switch = !o.pinned;
            const BOOL disable_transitions = TRUE;
            check(DwmSetWindowAttribute(window.handle,DWMWA_TRANSITIONS_FORCEDISABLED,&disable_transitions,
                sizeof(disable_transitions)),"canvas transition policy");
            const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_DONOTROUND;
            check(DwmSetWindowAttribute(window.handle,DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners)),
                "canvas rectangular surface policy");
            window.key_modes = {"DWM","NPU","GPU"};
            RECT area{}, rect{}; GetWindowRect(window.handle,&rect);
            if (!SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0) || rect.right-rect.left > area.right-area.left ||
                rect.bottom-rect.top > area.bottom-area.top) throw std::runtime_error("canvas does not fit work area");
            SetWindowPos(window.handle,nullptr,area.left+(area.right-area.left-(rect.right-rect.left))/2,
                area.top+(area.bottom-area.top-(rect.bottom-rect.top))/2,0,0,SWP_NOSIZE | SWP_NOZORDER);
            for (auto &source : sources) source->create(window.handle);
            auto p = positions(0,o.width,o.height,sources[0]->width,sources[0]->height);
            auto background = [&] {
                std::fill(r.f.begin(),r.f.end(),0); r.compose("CPU");
                if (window.present(r) != S_OK) throw std::runtime_error("background presentation occluded");
            };
            background(); show_sources(sources,window.handle,p,true);
            window.messages(); check(DwmFlush(),"initial window flush");
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            auto activate = [&](const std::string &next, double elapsed) {
                if (!mode_valid(next)) {
                    selections.push_back("{\"elapsed_s\":"+std::to_string(elapsed)+",\"requested\":"+quote(next)+
                        ",\"accepted\":false,\"retained\":"+quote(mode)+"}");
                    return;
                }
                if (next == "DWM") { background(); show_sources(sources,window.handle,p,true); }
                else {
                    compositor.compose(next,sources,p);
                    if (window.present(r) != S_OK) throw std::runtime_error("replacement presentation occluded");
                    // Only retire original windows after the replacement is ready.
                    show_sources(sources,window.handle,p,false);
                }
                mode = next;
                SetWindowTextA(window.handle,("Controlled window delegation | "+mode+" | 1 DWM / 2 NPU / 3 GPU / Esc exit").c_str());
                selections.push_back("{\"elapsed_s\":"+std::to_string(elapsed)+",\"requested\":"+quote(next)+
                    ",\"accepted\":true,\"visible_source_windows\":"+std::to_string(visible_count(sources))+"}");
            };
            // Identical fixed arrangement in all modes, checked against a direct
            // opaque-window oracle before inference measurements start.
            const auto expected = compositor.oracle(sources,p);
            for (const std::string check_mode : {"DWM","NPU","GPU"}) {
                activate(check_mode,0); window.messages(); check(DwmFlush(),"screen qualification flush");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                const unsigned error = screen_error(window.handle,r,expected);
                screen_checks.push_back("{\"mode\":"+quote(check_mode)+",\"maximum_rgb_error\":"+std::to_string(error)+"}");
                if (error > 1) throw std::runtime_error(check_mode+" screen oracle mismatch: "+std::to_string(error));
            }
            selections.clear(); activate(o.mode,0); window.messages(); check(DwmFlush(),"initial desktop flush");
            FramePacer pacer;
            const auto start = Clock::now(); const double cpu_start = process_cpu_seconds();
            started_unix_s = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
            if (!o.ready.empty()) { std::ofstream file(o.ready); file << "ready\n"; if (!file) throw std::runtime_error("readiness write failed"); }
            stage = "live";
            try {
                while (!window.stopped) {
                    pacer.until(start+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(double(slot)/o.fps)));
                    window.messages(); const auto begin = Clock::now(); const double elapsed = milliseconds(start,begin)/1000;
                    if (elapsed >= o.seconds || window.stopped || (!o.stop.empty() && std::filesystem::exists(o.stop))) break;
                    p = positions(elapsed,o.width,o.height,sources[0]->width,sources[0]->height);
                    if (selection < o.sequence.size() && elapsed >= (selection+1)*o.switch_every)
                        window.requested = o.sequence[selection++];
                    if (!window.requested.empty()) { activate(window.requested,elapsed); window.requested.clear(); }
                    const auto current = size_t(elapsed*o.fps); if (current > slot) { skipped += current-slot; slot=current; }
                    std::array<double,3> phases{};
                    HRESULT status = S_OK; double present_ms = 0;
                    if (mode == "DWM") { show_sources(sources,window.handle,p,true); moves += 3; }
                    else {
                        phases = compositor.compose(mode,sources,p); const auto composed = Clock::now();
                        status = window.present(r); present_ms = milliseconds(composed);
                        if (status != S_OK) throw std::runtime_error("delegated frame occluded");
                        if (visible_count(sources)) throw std::runtime_error("original windows still visible during delegation");
                    }
                    const auto done = Clock::now();
                    if (milliseconds(start,done) > (slot+1)*1000.0/o.fps) ++misses;
                    std::ostringstream f; f << std::setprecision(12) << "{\"slot\":" << slot << ",\"elapsed_s\":" << elapsed
                        << ",\"mode\":" << quote(mode) << ",\"placement_ms\":" << phases[0] << ",\"upload_ms\":" << phases[1]
                        << ",\"compose_ms\":" << phases[2] << ",\"present_ms\":" << present_ms << ",\"work_ms\":" << milliseconds(begin,done)
                        << ",\"host_uploaded_bytes\":" << (mode == "DWM" ? 0 : compositor.frame_host_upload_bytes)
                        << ",\"device_placement_copy_bytes\":" << (mode == "DWM" ? 0 : compositor.frame_device_copy_bytes)
                        << ",\"visible_source_windows\":" << visible_count(sources) << ",\"npu_blends\":" << (mode == "NPU" ? 3 : 0)
                        << ",\"gpu_blends\":" << (mode == "GPU" ? 3 : 0) << ",\"present_hresult\":";
                    if (mode == "DWM") f << "null"; else f << status;
                    frames.push_back(f.str()+"}"); ++slot;
                }
            } catch (const std::exception &error) {
                recovery_error = error.what(); recovered = true;
            }
            live_seconds = milliseconds(start)/1000; cpu_seconds = process_cpu_seconds()-cpu_start;
            cancelled = window.stopped;
            if (!cancelled && !recovered) {
                window.messages(); check(DwmFlush(),"final screen qualification flush");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                end_screen_max_error = screen_error(window.handle,r,compositor.oracle(sources,p));
            }
            // Return owned surfaces to DWM before closing the controlled workspace.
            background(); show_sources(sources,window.handle,p,true); check(DwmFlush(),"restore desktop flush");
            restored = visible_count(sources);
        }
        stage = "report";
        std::ofstream report(o.report); report << std::setprecision(16);
        report << "{\"schema_version\":1,\"scope\":\"three cooperating opaque GDI windows; Windows DWM retains the outer desktop\","
            << "\"passed\":" << ((!cancelled && !recovered && end_screen_max_error <= 1) ? "true" : "false") << ",\"validation_only\":" << (o.validate_only ? "true" : "false")
            << ",\"pid\":" << GetCurrentProcessId() << ",\"session_id\":" << session << ",\"initial_mode\":" << quote(o.mode)
            << ",\"npu_execution_device\":\"NPU\",\"npu_driver_version\":" << quote(r.npu->driver_version)
            << ",\"npu_workload\":" << quote(r.npu->workload) << ",\"npu_requested_tiles\":" << r.npu->requested_tiles
            << ",\"npu_graph\":" << quote(o.graph) << ",\"placement_provider\":" << quote(o.placement)
            << ",\"preparation_upload_bytes\":" << compositor.preparation_upload_bytes
            << ",\"openvino_runtime\":" << quote(ov::get_openvino_version().buildNumber)
            << ",\"width\":" << o.width << ",\"height\":" << o.height << ",\"source_count\":3,\"source_width\":" << sources[0]->width
            << ",\"source_height\":" << sources[0]->height << ",\"opaque_oracle_max_error\":" << compositor.oracle_max_error
            << ",\"end_screen_max_error\":" << end_screen_max_error
            << ",\"pixel_qualification_cases\":8,\"started_unix_s\":" << started_unix_s << ",\"live_wall_seconds\":" << live_seconds
            << ",\"live_process_cpu_seconds\":" << cpu_seconds << ",\"fps\":" << o.fps << ",\"skipped_slots\":" << skipped
            << ",\"deadline_misses\":" << misses << ",\"move_calls\":" << moves << ",\"restored_source_windows\":" << restored
            << ",\"cancelled\":" << (cancelled ? "true" : "false") << ",\"recovered_to_dwm\":" << (recovered ? "true" : "false")
            << ",\"recovery_error\":" << quote(recovery_error) << ",\"scanout_measured\":false,\"whole_desktop_offloaded\":false,\"screen_checks\":[";
        for (size_t i = 0; i < screen_checks.size(); ++i) report << (i ? "," : "") << screen_checks[i];
        report << "],\"frames\":[";
        for (size_t i = 0; i < frames.size(); ++i) report << (i ? "," : "") << frames[i];
        report << "],\"selections\":[";
        for (size_t i = 0; i < selections.size(); ++i) report << (i ? "," : "") << selections[i];
        report << "]}\n";
        if (!report) throw std::runtime_error("cannot write report");
        std::cout << "completed " << frames.size() << " iterations, " << skipped << " skipped slots\n";
        return cancelled || recovered || end_screen_max_error > 1 ? 1 : 0;
    } catch (const std::exception &error) {
        std::cerr << "{\"passed\":false,\"stage\":" << quote(stage) << ",\"error\":" << quote(error.what()) << "}\n";
        return 1;
    }
}
