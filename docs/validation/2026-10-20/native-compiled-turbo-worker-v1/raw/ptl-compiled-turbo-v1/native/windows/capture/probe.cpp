#include "window_capture.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

using Clock = std::chrono::steady_clock;
static unsigned ticks = 0, clicks = 0, keys = 0;
static HWND popup = nullptr;
static LRESULT CALLBACK popup_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) {
        PAINTSTRUCT ps{};
        auto dc = BeginPaint(h, &ps);
        RECT r{};
        GetClientRect(h, &r);
        auto brush = CreateSolidBrush(RGB(231, 111, 23));
        FillRect(dc, &r, brush);
        DeleteObject(brush);
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static LRESULT CALLBACK fixture_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TIMER) {
        ++ticks;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    if (m == WM_LBUTTONDOWN) {
        ++clicks;
        return 0;
    }
    if (m == WM_APP)
        return clicks;
    if (m == WM_CHAR) {
        ++keys;
        return 0;
    }
    if (m == WM_APP + 1) {
        KillTimer(h, 1);
        ticks = unsigned(w) % 200;
        InvalidateRect(h, nullptr, FALSE);
        UpdateWindow(h);
        return ticks;
    }
    if (m == WM_APP + 2) {
        if (popup && IsWindow(popup)) {
            DestroyWindow(popup);
            popup = nullptr;
        } else {
            WNDCLASSW cls{};
            cls.hInstance = GetModuleHandleW(nullptr);
            cls.lpszClassName = L"NrCapturePopup";
            cls.lpfnWndProc = popup_proc;
            RegisterClassW(&cls);
            popup =
                CreateWindowExW(0, cls.lpszClassName, L"capture test popup", WS_OVERLAPPEDWINDOW,
                                500, 400, 400, 300, h, nullptr, cls.hInstance, nullptr);
            ShowWindow(popup, SW_SHOWNOACTIVATE);
        }
        return reinterpret_cast<LRESULT>(popup);
    }
    if (m == WM_APP + 3) {
        SetTimer(h, 1, 30, nullptr);
        return 0;
    }
    if (m == WM_APP + 4)
        return keys;
    if (m == WM_APP + 6) {
        if (!popup || !IsWindow(popup))
            SendMessageW(h, WM_APP + 2, 0, 0);
        else
            ShowWindow(popup, IsWindowVisible(popup) ? SW_HIDE : SW_SHOWNOACTIVATE);
        return reinterpret_cast<LRESULT>(popup);
    }
    if (m == WM_PAINT) {
        PAINTSTRUCT ps{};
        auto dc = BeginPaint(h, &ps);
        RECT r{};
        GetClientRect(h, &r);
        auto b = CreateSolidBrush(RGB(22, ticks % 200 + 20, 78));
        FillRect(dc, &r, b);
        DeleteObject(b);
        EndPaint(h, &ps);
        return 0;
    }
    if (m == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static LRESULT CALLBACK cover_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCHITTEST)
        return HTTRANSPARENT;
    if (m == WM_MOUSEACTIVATE)
        return MA_NOACTIVATE;
    if (m == WM_PAINT) {
        PAINTSTRUCT ps{};
        auto dc = BeginPaint(h, &ps);
        RECT r{};
        GetClientRect(h, &r);
        auto b = CreateSolidBrush(RGB(220, 30, 220));
        FillRect(dc, &r, b);
        DeleteObject(b);
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static HWND make_window(const wchar_t *name, WNDPROC proc, DWORD style, DWORD ex, int x, int y,
                        int w, int h) {
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = name;
    cls.lpfnWndProc = proc;
    RegisterClassW(&cls);
    auto result = CreateWindowExW(ex, name, name, style, x, y, w, h, nullptr, nullptr,
                                  cls.hInstance, nullptr);
    if (!result)
        throw std::runtime_error("CreateWindow failed");
    return result;
}
static void pump() {
    MSG m{};
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}
struct Child {
    PROCESS_INFORMATION process{};
    ~Child() {
        if (process.hProcess) {
            TerminateProcess(process.hProcess, 0);
            WaitForSingleObject(process.hProcess, 5000);
            CloseHandle(process.hProcess);
            CloseHandle(process.hThread);
        }
    }
};
int main(int argc, char **argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc == 2 && std::string(argv[1]) == "--fixture") {
        auto h = make_window(L"NrCaptureFixture", fixture_proc, WS_OVERLAPPEDWINDOW, 0, 120, 120,
                             500, 350);
        ShowWindow(h, SW_SHOW);
        SetForegroundWindow(h);
        SetTimer(h, 1, 30, nullptr);
        auto end = Clock::now() + std::chrono::seconds(120);
        while (Clock::now() < end && IsWindow(h)) {
            pump();
            Sleep(5);
        }
        return 0;
    }
    if (argc != 2) {
        std::cerr << "usage: nr_capture_probe report.json\n";
        return 2;
    }
    bool passed = false;
    std::string error;
    unsigned frames[3]{}, changes[3]{}, bad[3]{}, click_result[3]{}, input_error[3]{};
    HWND cover = nullptr, fixture = nullptr;
    Child child;
    POINT cursor{};
    GetCursorPos(&cursor);
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        wchar_t file[32768]{};
        GetModuleFileNameW(nullptr, file, 32768);
        std::wstring command = L"\"" + std::wstring(file) + L"\" --fixture";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                            &startup, &child.process))
            throw std::runtime_error("fixture launch failed");
        auto deadline = Clock::now() + std::chrono::seconds(5);
        while (!fixture && Clock::now() < deadline) {
            fixture = FindWindowW(L"NrCaptureFixture", nullptr);
            Sleep(20);
        }
        DWORD pid = 0;
        GetWindowThreadProcessId(fixture, &pid);
        if (!fixture || pid != child.process.dwProcessId)
            throw std::runtime_error("owned fixture not found");
        nr::capture::Device device;
        nr::capture::Window capture(device, fixture);
        nr::capture::Image image;
        RECT rect{};
        GetWindowRect(fixture, &rect);
        cover =
            make_window(L"NrCaptureCover", cover_proc, WS_POPUP,
                        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, rect.left - 20,
                        rect.top - 20, rect.right - rect.left + 40, rect.bottom - rect.top + 40);
        for (unsigned phase = 0; phase < 3; ++phase) {
            if (phase == 1)
                ShowWindow(cover, SW_SHOWNOACTIVATE);
            if (phase == 2) {
                SetWindowLongPtrW(cover, GWL_EXSTYLE,
                                  GetWindowLongPtrW(cover, GWL_EXSTYLE) | WS_EX_LAYERED);
                SetLayeredWindowAttributes(cover, 0, 255, LWA_ALPHA);
                SetWindowPos(cover, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            }
            auto until = Clock::now() + std::chrono::seconds(3);
            int previous = -1;
            while (Clock::now() < until) {
                pump();
                if (capture.poll(image)) {
                    ++frames[phase];
                    auto i = (size_t(image.height / 2) * image.width + image.width / 2) * 4;
                    if (image.bgra[i] != 78 || image.bgra[i + 2] != 22 || image.bgra[i + 3] != 255)
                        ++bad[phase];
                    int green = image.bgra[i + 1];
                    if (previous >= 0 && green != previous)
                        ++changes[phase];
                    previous = green;
                }
                Sleep(5);
            }
            const auto before = SendMessageW(fixture, WM_APP, 0, 0);
            POINT point{200, 150};
            ClientToScreen(fixture, &point);
            SetCursorPos(point.x, point.y);
            INPUT input[2]{};
            input[0].type = input[1].type = INPUT_MOUSE;
            input[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            input[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
            if (SendInput(2, input, sizeof(INPUT)) != 2)
                input_error[phase] = GetLastError() ? GetLastError() : 1;
            Sleep(150);
            click_result[phase] = unsigned(SendMessageW(fixture, WM_APP, 0, 0) - before);
        }
        passed = true;
        for (unsigned p = 0; p < 3; ++p)
            if (frames[p] < 5 || changes[p] < 5 || bad[p] || input_error[p] ||
                click_result[p] != (p == 1 ? 0u : 1u))
                passed = false;
    } catch (const winrt::hresult_error &e) {
        error = winrt::to_string(e.message());
    } catch (const std::exception &e) {
        error = e.what();
    }
    if (cover)
        DestroyWindow(cover);
    SetCursorPos(cursor.x, cursor.y);
    if (fixture)
        PostMessageW(fixture, WM_CLOSE, 0, 0);
    std::ofstream out(argv[1]);
    out << "{\"passed\":" << (passed ? "true" : "false") << ",\"error\":\"";
    for (char c : error)
        if (c != '"' && c != '\\' && c >= 32)
            out << c;
    out << "\",\"phases\":[";
    for (unsigned p = 0; p < 3; ++p) {
        if (p)
            out << ',';
        out << "{\"phase\":" << p << ",\"frames\":" << frames[p] << ",\"changes\":" << changes[p]
            << ",\"bad_pixels\":" << bad[p] << ",\"clicks\":" << click_result[p]
            << ",\"input_error\":" << input_error[p] << '}';
    }
    out << "]}\n";
    out.close();
    std::cout << (passed ? "passed" : "failed") << " " << error << '\n';
    return passed && out ? 0 : 1;
}
