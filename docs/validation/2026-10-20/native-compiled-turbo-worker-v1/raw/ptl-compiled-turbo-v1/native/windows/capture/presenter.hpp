#pragma once
#include "window_capture.hpp"
#include <array>
#include <cmath>
#include <dwmapi.h>
#include <dxgi1_6.h>
#include <iostream>
namespace nr::capture {
class Presenter {
    Device &device;
    winrt::com_ptr<IDXGISwapChain1> swapchain;

  public:
    HWND hwnd = nullptr;
    bool stopped = false, visible = false, enabled = true;
    UINT width, height;
    uint64_t presentations = 0;
    static LRESULT CALLBACK procedure(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto self = reinterpret_cast<Presenter *>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            self = static_cast<Presenter *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (m == WM_NCHITTEST)
            return HTTRANSPARENT;
        if (m == WM_MOUSEACTIVATE)
            return MA_NOACTIVATE;
        if (self && m == WM_CLOSE) {
            self->stopped = true;
            self->hide();
            return 0;
        }
        if (self && m == WM_HOTKEY) {
            if (w == 1) {
                self->stopped = true;
                self->hide();
            } else if (w == 2) {
                self->enabled = !self->enabled;
                if (!self->enabled)
                    self->hide();
            }
            return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }
    Presenter(Device &d, UINT w, UINT h) : device(d), width(w), height(h) {
        WNDCLASSW cls{};
        cls.lpfnWndProc = procedure;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"NpuRenderDesktopHost";
        RegisterClassW(&cls);
        hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_LAYERED |
                                   WS_EX_TRANSPARENT,
                               cls.lpszClassName, L"NPU desktop composition", WS_POPUP, 0, 0, w, h,
                               nullptr, nullptr, cls.hInstance, this);
        if (!hwnd)
            throw std::runtime_error("desktop output window creation failed");
        try {
            if (!SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA))
                throw std::runtime_error("opaque click-through window setup failed");
            auto dxgi = d.device.as<IDXGIDevice>();
            winrt::com_ptr<IDXGIAdapter> adapter;
            winrt::check_hresult(dxgi->GetAdapter(adapter.put()));
            const auto monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
            bool supported_output = false;
            for (UINT index = 0;; ++index) {
                winrt::com_ptr<IDXGIOutput> output;
                const HRESULT result = adapter->EnumOutputs(index, output.put());
                if (result == DXGI_ERROR_NOT_FOUND)
                    break;
                winrt::check_hresult(result);
                DXGI_OUTPUT_DESC1 info{};
                winrt::check_hresult(output.as<IDXGIOutput6>()->GetDesc1(&info));
                if (info.Monitor == monitor && info.AttachedToDesktop) {
                    if (info.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 ||
                        info.BitsPerColor != 8)
                        throw std::runtime_error("desktop output currently requires 8-bit SDR");
                    supported_output = true;
                    break;
                }
            }
            if (!supported_output)
                throw std::runtime_error("capture adapter does not own the active display");
            winrt::com_ptr<IDXGIFactory2> factory;
            winrt::check_hresult(adapter->GetParent(__uuidof(IDXGIFactory2), factory.put_void()));
            DXGI_SWAP_CHAIN_DESC1 desc{};
            desc.Width = w;
            desc.Height = h;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 2;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
            winrt::check_hresult(factory->CreateSwapChainForHwnd(
                d.device.get(), hwnd, &desc, nullptr, nullptr, swapchain.put()));
            winrt::check_hresult(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));
            if (!RegisterHotKey(hwnd, 1, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, VK_F12))
                throw std::runtime_error("emergency exit hotkey unavailable");
            if (!RegisterHotKey(hwnd, 2, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, VK_F11))
                throw std::runtime_error("desktop toggle hotkey unavailable");
        } catch (...) {
            DestroyWindow(hwnd);
            hwnd = nullptr;
            throw;
        }
    }
    ~Presenter() {
        if (hwnd) {
            UnregisterHotKey(hwnd, 1);
            UnregisterHotKey(hwnd, 2);
            DestroyWindow(hwnd);
        }
    }
    void messages() {
        MSG m{};
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT)
                stopped = true;
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    void hide() {
        if (visible) {
            ShowWindow(hwnd, SW_HIDE);
            visible = false;
        }
    }
    HRESULT present(const std::vector<uint8_t> &pixels) {
        if (pixels.size() != size_t(width) * height * 4)
            throw std::runtime_error("presentation buffer dimensions mismatch");
        winrt::com_ptr<ID3D11Texture2D> back;
        winrt::check_hresult(swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), back.put_void()));
        device.context->UpdateSubresource(back.get(), 0, nullptr, pixels.data(), width * 4,
                                          UINT(pixels.size()));
        if (!visible) {
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            visible = true;
        }
        HRESULT hr = swapchain->Present(1, 0);
        winrt::check_hresult(hr);
        if (hr != S_OK)
            throw std::runtime_error("desktop output was not accepted as visible");
        ++presentations;
        return hr;
    }
    struct ScreenSample {
        std::vector<uint8_t> pixels;
        POINT cursor{};
    };
    struct OverlayPixel {
        size_t pixel;
        std::array<uint8_t, 3> low{}, high{};
    };
    std::vector<OverlayPixel> screen_model;
    unsigned overlay_pixels = 0, raw_screen_error = 0;
    uint64_t screen_compared_pixels = 0, screen_differing_pixels = 0;
    ScreenSample read_screen() {
        winrt::check_hresult(DwmFlush());
        Sleep(500);
        std::vector<uint8_t> pixels(size_t(width) * height * 4);
        auto screen = GetDC(nullptr);
        if (!screen)
            throw std::runtime_error("screen DC unavailable");
        auto memory = CreateCompatibleDC(screen);
        auto bitmap = CreateCompatibleBitmap(screen, width, height);
        HGDIOBJ old = nullptr;
        auto cleanup = [&] {
            if (old)
                SelectObject(memory, old);
            if (bitmap)
                DeleteObject(bitmap);
            if (memory)
                DeleteDC(memory);
            ReleaseDC(nullptr, screen);
        };
        try {
            if (!memory || !bitmap)
                throw std::runtime_error("screen staging allocation failed");
            old = SelectObject(memory, bitmap);
            if (!old || old == HGDI_ERROR)
                throw std::runtime_error("screen bitmap selection failed");
            if (!BitBlt(memory, 0, 0, width, height, screen, 0, 0, SRCCOPY | CAPTUREBLT))
                throw std::runtime_error("screen readback failed");
            SelectObject(memory, old);
            old = nullptr;
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = LONG(width);
            info.bmiHeader.biHeight = -LONG(height);
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            if (GetDIBits(memory, bitmap, 0, height, pixels.data(), &info, DIB_RGB_COLORS) !=
                height)
                throw std::runtime_error("screen readback extent mismatch");
        } catch (...) {
            cleanup();
            throw;
        }
        cleanup();
        POINT cursor{};
        GetCursorPos(&cursor);
        return {std::move(pixels), cursor};
    }
    // This models retained, stable system overlays for verification only. It does
    // not alter or hide them. The later desktop image is an independent holdout.
    // An overlay touching >1% of screen pixels is outside this bounded check.
    void calibrate_system_overlay() {
        std::vector<uint8_t> pixels(size_t(width) * height * 4, 64);
        for (size_t i = 3; i < pixels.size(); i += 4)
            pixels[i] = 255;
        present(pixels);
        present(pixels);
        auto low = read_screen();
        std::fill(pixels.begin(), pixels.end(), 192);
        for (size_t i = 3; i < pixels.size(); i += 4)
            pixels[i] = 255;
        present(pixels);
        auto high = read_screen();
        if (low.cursor.x != high.cursor.x || low.cursor.y != high.cursor.y)
            throw std::runtime_error("screen calibration cursor changed");
        for (size_t i = 0; i < size_t(width) * height; ++i) {
            bool overlay = false;
            OverlayPixel sample{i, {}, {}};
            for (size_t c = 0; c < 3; ++c) {
                sample.low[c] = low.pixels[i * 4 + c];
                sample.high[c] = high.pixels[i * 4 + c];
                int delta = int(sample.high[c]) - int(sample.low[c]);
                if (delta < 0 || delta > 130)
                    throw std::runtime_error("screen overlay is not a stable source-over layer");
                overlay |= std::abs(int(sample.low[c]) - 64) > 1 ||
                           std::abs(int(sample.high[c]) - 192) > 1;
            }
            if (overlay)
                screen_model.push_back(sample);
        }
        overlay_pixels = unsigned(screen_model.size());
        if (size_t(overlay_pixels) * 100 > size_t(width) * height)
            throw std::runtime_error("system overlay exceeds bounded screen qualification");
    }
    unsigned screen_error(const std::vector<uint8_t> &expected) {
        return compare_screen(expected, read_screen());
    }
    unsigned compare_screen(const std::vector<uint8_t> &expected, const ScreenSample &screen) {
        if (expected.size() != size_t(width) * height * 4 ||
            screen.pixels.size() != expected.size())
            throw std::runtime_error("screen oracle buffer dimensions mismatch");
        unsigned maximum = 0, logged = 0;
        raw_screen_error = 0;
        screen_compared_pixels = screen_differing_pixels = 0;
        auto model = screen_model.begin();
        for (size_t pixel = 0; pixel < size_t(width) * height; ++pixel) {
            while (model != screen_model.end() && model->pixel < pixel)
                ++model;
            const auto x = int(pixel % width), y = int(pixel / width);
            if (std::abs(x - screen.cursor.x) < 64 && std::abs(y - screen.cursor.y) < 64)
                continue;
            ++screen_compared_pixels;
            unsigned point_error = 0;
            for (size_t c = 0; c < 3; ++c) {
                const int value = expected[pixel * 4 + c], actual = screen.pixels[pixel * 4 + c];
                int predicted = value;
                if (model != screen_model.end() && model->pixel == pixel)
                    predicted = std::clamp(
                        int(std::lround(model->low[c] + (int(model->high[c]) - int(model->low[c])) *
                                                            (value - 64) / 128.0)),
                        0, 255);
                raw_screen_error = std::max(raw_screen_error, unsigned(std::abs(actual - value)));
                point_error = std::max(point_error, unsigned(std::abs(actual - predicted)));
            }
            maximum = std::max(maximum, point_error);
            if (point_error > 2) {
                ++screen_differing_pixels;
                if (logged++ < 12)
                    std::cout << "screen comparison difference " << x << ',' << y
                              << " error=" << point_error << std::endl;
            }
        }
        return maximum;
    }
};
} // namespace nr::capture
