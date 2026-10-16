// Exercise Windows' existing compositor using only this process's static windows.
// This is a repeatable DWM workload, not an NPU renderer or a desktop capture.
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

static unsigned paints = 0;
static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT rect{};
        GetClientRect(window, &rect);
        const int index = int(GetWindowLongPtrW(window, GWLP_USERDATA));
        HBRUSH brush = CreateSolidBrush(RGB(45 + index * 45, 65 + index * 20, 115));
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        const wchar_t *label = L"DWM workload: static content / close any window to stop";
        DrawTextW(dc, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(window, &paint);
        ++paints;
        return 0;
    }
    if (message == WM_CLOSE || (message == WM_KEYDOWN && wparam == VK_ESCAPE)) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int main(int argc, char **argv) {
    std::vector<HWND> windows;
    std::string report;
    try {
        int seconds = 20;
        std::string mode = "move";
        std::string ready_file, stop_file;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (i + 1 >= argc) throw std::invalid_argument("missing argument value");
            if (argument == "--seconds") seconds = std::stoi(argv[++i]);
            else if (argument == "--mode") mode = argv[++i];
            else if (argument == "--report") report = argv[++i];
            else if (argument == "--ready-file") ready_file = argv[++i];
            else if (argument == "--stop-file") stop_file = argv[++i];
            else throw std::invalid_argument("unknown argument");
        }
        if (seconds < 1 || seconds > 300 || (mode != "move" && mode != "static") || report.empty())
            throw std::invalid_argument("use --mode move|static --seconds 1..300 --report path");
        DWORD session = 0;
        if (!ProcessIdToSessionId(GetCurrentProcessId(), &session) || !session)
            throw std::runtime_error("requires an interactive user session");
        HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
        wchar_t desktop_name[256]{};
        DWORD needed = 0;
        const bool input_default = desktop && GetUserObjectInformationW(desktop, UOI_NAME,
            desktop_name, sizeof(desktop_name), &needed) && std::wstring(desktop_name) == L"Default";
        if (desktop) CloseDesktop(desktop);
        if (!input_default) throw std::runtime_error("requires the unlocked Default input desktop");
        BOOL composition = FALSE;
        if (FAILED(DwmIsCompositionEnabled(&composition)) || !composition)
            throw std::runtime_error("DWM composition is unavailable");
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        RECT area{};
        if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0))
            throw std::runtime_error("cannot query work area");
        const int available_w = area.right - area.left, available_h = area.bottom - area.top;
        const int width = std::min(900, available_w * 2 / 3);
        const int height = std::min(650, available_h * 2 / 3);
        WNDCLASSW type{};
        type.lpfnWndProc = window_proc;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"NpuRenderDesktopWorkload";
        type.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        if (!RegisterClassW(&type)) throw std::runtime_error("cannot register window class");
        for (int i = 0; i < 3; ++i) {
            HWND window = CreateWindowExW(WS_EX_NOACTIVATE, type.lpszClassName,
                L"NPU Render: Windows compositor measurement", WS_OVERLAPPEDWINDOW,
                area.left + 40 * i, area.top + 35 * i, width, height,
                nullptr, nullptr, type.hInstance, nullptr);
            if (!window) throw std::runtime_error("cannot create workload window");
            windows.push_back(window);
            SetWindowLongPtrW(window, GWLP_USERDATA, i);
            ShowWindow(window, SW_SHOWNOACTIVATE);
            UpdateWindow(window);
        }
        SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
        using clock = std::chrono::steady_clock;
        const auto start = clock::now();
        const double started_unix_s = std::chrono::duration<double>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (!ready_file.empty()) {
            std::ofstream ready(ready_file);
            ready << "ready\n";
            if (!ready) throw std::runtime_error("cannot write readiness file");
        }
        unsigned ticks = 0, move_calls = 0;
        bool cancelled = false;
        while (std::chrono::duration<double>(clock::now() - start).count() < seconds) {
            if (!stop_file.empty() && std::filesystem::exists(stop_file)) break;
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) cancelled = true;
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (cancelled) break;
            const double elapsed = std::chrono::duration<double>(clock::now() - start).count();
            if (mode == "move") {
                for (size_t i = 0; i < windows.size(); ++i) {
                    const double phase = elapsed * 1.7 + double(i) * 2.1;
                    const int x = area.left + int((available_w - width) * (0.5 + 0.45 * std::sin(phase)));
                    const int y = area.top + int((available_h - height) * (0.5 + 0.45 * std::cos(phase * 0.8)));
                    if (!SetWindowPos(windows[i], nullptr, x, y, 0, 0,
                        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE))
                        throw std::runtime_error("window movement failed");
                    ++move_calls;
                }
            }
            ++ticks;
            std::this_thread::sleep_until(start + std::chrono::microseconds(int64_t(ticks) * 1000000 / 60));
        }
        const double elapsed = std::chrono::duration<double>(clock::now() - start).count();
        for (HWND window : windows) DestroyWindow(window);
        windows.clear();
        SetThreadExecutionState(ES_CONTINUOUS);
        std::ofstream output(report);
        if (!output) throw std::runtime_error("cannot open report");
        output.precision(16);
        output << "{\"schema_version\":1,\"scope\":\"controlled window-motion workload rendered by Windows DWM; no NPU offload\","
               << "\"mode\":\"" << mode << "\",\"pid\":" << GetCurrentProcessId()
               << ",\"session_id\":" << session << ",\"dwm_composition_enabled\":true,\"started_unix_s\":" << started_unix_s
               << ",\"requested_seconds\":" << seconds << ",\"elapsed_seconds\":" << elapsed
               << ",\"window_count\":3,\"window_width\":" << width << ",\"window_height\":" << height
               << ",\"ticks\":" << ticks << ",\"move_calls\":" << move_calls << ",\"paint_calls\":" << paints
               << ",\"cancelled\":" << (cancelled ? "true" : "false")
               << ",\"scanout_measured\":false}\n";
        if (!output) throw std::runtime_error("cannot write report");
        return cancelled ? 2 : 0;
    } catch (const std::exception &error) {
        for (HWND window : windows) DestroyWindow(window);
        SetThreadExecutionState(ES_CONTINUOUS);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
