// An ordinary HWND swap chain on the paired adapter. The screen oracle reads
// only this owned window; it is measurement, not the production rendering path.
#include <dwmapi.h>
struct PresentWindow {
    static constexpr UINT width = 320, height = 192;
    HWND window = nullptr;
    ATOM type = 0;
    EXECUTION_STATE prior_execution_state = 0;
    PresentWindow() = default;
    PresentWindow(const PresentWindow &) = delete;
    ~PresentWindow() {
        if (window) DestroyWindow(window);
        if (type) UnregisterClassW(MAKEINTRESOURCEW(type), GetModuleHandleW(nullptr));
        if (prior_execution_state) SetThreadExecutionState(prior_execution_state);
    }
    void open() {
        USEROBJECTFLAGS flags{}; DWORD bytes = 0, session = 0;
        require(ProcessIdToSessionId(GetCurrentProcessId(), &session) && session &&
                GetUserObjectInformationW(GetProcessWindowStation(), UOI_FLAGS, &flags, sizeof(flags), &bytes) &&
                (flags.dwFlags & WSF_VISIBLE), "interactive guest desktop required");
        struct InputDesktop {
            HDESK handle = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
            ~InputDesktop() { if (handle) CloseDesktop(handle); }
        } input;
        WCHAR input_name[256]{}, thread_name[256]{};
        require(input.handle &&
                GetUserObjectInformationW(input.handle, UOI_NAME, input_name, sizeof(input_name), &bytes) &&
                GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME,
                                          thread_name, sizeof(thread_name), &bytes) &&
                !std::wcscmp(input_name, thread_name), "presentation desktop is not the input desktop");
        prior_execution_state = SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
        require(prior_execution_state != 0, "keep presentation display awake");
        std::printf("{\"presentation_session\":%lu,\"screen_oracle_width\":%u,\"screen_oracle_height\":%u}\n", session, width, height);
        WNDCLASSW cls{}; cls.lpfnWndProc = DefWindowProcW; cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"NpuRenderRegisteredPresentOracle";
        type = RegisterClassW(&cls); require(type != 0, "register presentation window");
        window = CreateWindowExW(WS_EX_TOPMOST, MAKEINTRESOURCEW(type), L"NPU Render VM presentation test",
                                 WS_POPUP | WS_VISIBLE, 64, 64, width, height, nullptr, nullptr, cls.hInstance, nullptr);
        require(window != nullptr, "create presentation window");
        // STARTUPINFO can override the first ShowWindow call, including when
        // a launcher hides the console. Explicitly show this owned test HWND.
        ShowWindow(window, SW_SHOWNORMAL);
        require(SetWindowPos(window, HWND_TOPMOST, 64, 64, width, height, SWP_SHOWWINDOW) != FALSE,
                "show presentation window");
        UpdateWindow(window); pump();
        check(DwmFlush(), "presentation_window_dwm_flush");
        DWORD cloaked = 0;
        check(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)), "presentation_window_cloaked");
        RECT client{}; require(GetClientRect(window, &client) != FALSE, "presentation client extent");
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); GetStartupInfoW(&startup);
        std::printf("{\"window_visible\":%s,\"window_minimized\":%s,\"window_cloaked\":%lu,"
                    "\"startup_show_specified\":%s,\"startup_show\":%u,\"display_required\":true}\n",
                    IsWindowVisible(window) ? "true" : "false", IsIconic(window) ? "true" : "false", cloaked,
                    startup.dwFlags & STARTF_USESHOWWINDOW ? "true" : "false", UINT(startup.wShowWindow));
        std::fflush(stdout);
        require(IsWindowVisible(window) && !IsIconic(window) && !cloaked &&
                client.right - client.left == LONG(width) && client.bottom - client.top == LONG(height),
                "presentation window is not visible at its required extent");
    }
    void pump() {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    bool pixels(UINT channel) {
        POINT origin{}; require(ClientToScreen(window, &origin), "presentation client position");
        struct Capture {
            HDC screen = GetDC(nullptr), memory = nullptr;
            HBITMAP bitmap = nullptr; HGDIOBJ previous = nullptr;
            ~Capture() {
                if (previous) SelectObject(memory, previous);
                if (bitmap) DeleteObject(bitmap);
                if (memory) DeleteDC(memory);
                if (screen) ReleaseDC(nullptr, screen);
            }
        } capture;
        require(capture.screen != nullptr, "screen oracle DC");
        capture.memory = CreateCompatibleDC(capture.screen); require(capture.memory != nullptr, "screen oracle memory DC");
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -LONG(height);
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        void *data = nullptr;
        capture.bitmap = CreateDIBSection(capture.screen, &info, DIB_RGB_COLORS, &data, nullptr, 0);
        require(capture.bitmap && data, "screen oracle bitmap");
        capture.previous = SelectObject(capture.memory, capture.bitmap);
        require(capture.previous && capture.previous != HGDI_ERROR, "screen oracle bitmap selection");
        require(BitBlt(capture.memory, 0, 0, width, height, capture.screen, origin.x, origin.y, SRCCOPY | CAPTUREBLT) && GdiFlush(), "screen oracle capture");
        const auto pixels = static_cast<const unsigned char *>(data);
        for (size_t i = 0; i < size_t(width) * height; ++i)
            for (UINT c = 0; c < 3; ++c) if (pixels[i * 4 + c] != (c == 2 - channel ? 255 : 0)) return false;
        return true;
    }
};
void exercise_present(IDXGIFactory *factory, ID3D11Device *device, ID3D11DeviceContext *context) {
    PresentWindow window; window.open();
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = PresentWindow::width; desc.BufferDesc.Height = PresentWindow::height;
    desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 1;
    desc.OutputWindow = window.window; desc.Windowed = TRUE; desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    ComPtr<IDXGISwapChain> swap;
    check(factory->CreateSwapChain(device, &desc, &swap), "create_registered_hwnd_swapchain");
    ComPtr<ID3D11Texture2D> target;
    check(swap->GetBuffer(0, IID_PPV_ARGS(&target)), "get_registered_backbuffer");
    ComPtr<ID3D11RenderTargetView> view;
    check(device->CreateRenderTargetView(target.Get(), nullptr, &view), "create_registered_backbuffer_view");
    for (UINT pass = 0; pass < 3; ++pass) {
        FLOAT color[4]{}; color[pass] = color[3] = 1.f;
        context->ClearRenderTargetView(view.Get(), color);
        check(device->GetDeviceRemovedReason(), "present_clear_status");
        const auto status = swap->Present(0, 0);
        check(status, "registered_hwnd_present");
        require(status == S_OK, "presentation was occluded or incomplete");
        bool matched = false;
        const auto started = GetTickCount64();
        do {
            window.pump(); check(DwmFlush(), "screen_oracle_dwm_flush");
            matched = window.pixels(pass);
            if (!matched) Sleep(20);
        } while (!matched && GetTickCount64() - started < 2000);
        require(matched, "presented window pixels did not match");
        std::printf("{\"presented_pass\":%u,\"verified_rgb_bytes\":%u,\"screen_delivery_verified\":true,\"dwm_migrated\":false}\n",
                    pass, PresentWindow::width * PresentWindow::height * 3);
    }
    context->Flush();
}
