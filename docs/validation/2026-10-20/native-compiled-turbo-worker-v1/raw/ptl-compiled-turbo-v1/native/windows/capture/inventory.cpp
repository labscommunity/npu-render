#include "window_capture.hpp"
#include <chrono>
#include <dwmapi.h>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

static std::string quoted(const std::string &s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\')
            o += '\\';
        if (c >= 32)
            o += char(c);
    }
    return o + '"';
}
struct Entry {
    HWND hwnd;
    DWORD pid;
    RECT rect, bounds;
    std::string name;
};
static BOOL CALLBACK enumerate(HWND h, LPARAM p) {
    if (!IsWindowVisible(h) || IsIconic(h))
        return TRUE;
    DWORD cloak = 0;
    DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloak, sizeof(cloak));
    if (cloak)
        return TRUE;
    RECT r{};
    if (!GetWindowRect(h, &r) || r.right <= r.left || r.bottom <= r.top)
        return TRUE;
    RECT screen{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)}, intersection{};
    if (!IntersectRect(&intersection, &r, &screen))
        return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId())
        return TRUE;
    char cls[256]{};
    GetClassNameA(h, cls, 256);
    RECT bounds = r;
    DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds));
    reinterpret_cast<std::vector<Entry> *>(p)->push_back({h, pid, r, bounds, cls});
    return TRUE;
}
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    std::ofstream out(argv[1]);
    try {
        nr::capture::Device device;
        std::vector<Entry> entries;
        EnumWindows(enumerate, reinterpret_cast<LPARAM>(&entries));
        out << "{\"screen\":[" << GetSystemMetrics(SM_CXSCREEN) << ','
            << GetSystemMetrics(SM_CYSCREEN) << "],\"windows\":[";
        bool first = true;
        for (const auto &e : entries) {
            if (!first)
                out << ',';
            first = false;
            out << "{\"class\":" << quoted(e.name) << ",\"pid\":" << e.pid
                << ",\"shell\":" << (e.hwnd == GetShellWindow() ? "true" : "false") << ",\"rect\":["
                << e.rect.left << ',' << e.rect.top << ',' << e.rect.right << ',' << e.rect.bottom
                << "],\"bounds\":[" << e.bounds.left << ',' << e.bounds.top << ',' << e.bounds.right
                << ',' << e.bounds.bottom << ']';
            try {
                nr::capture::Window capture(device, e.hwnd);
                nr::capture::Image image;
                bool got = false;
                const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                while (std::chrono::steady_clock::now() < until) {
                    if (capture.poll(image)) {
                        got = true;
                        break;
                    }
                    Sleep(10);
                }
                size_t opaque = 0, nonzero = 0;
                for (size_t i = 0; i < image.bgra.size(); i += 4) {
                    if (image.bgra[i + 3] == 255)
                        ++opaque;
                    if (image.bgra[i + 3])
                        ++nonzero;
                }
                out << ",\"captured\":" << (got ? "true" : "false") << ",\"size\":[" << image.width
                    << ',' << image.height << "],\"opaque_pixels\":" << opaque
                    << ",\"nonzero_alpha_pixels\":" << nonzero;
            } catch (const winrt::hresult_error &x) {
                out << ",\"error\":" << quoted(winrt::to_string(x.message()))
                    << ",\"hresult\":" << uint32_t(x.code().value);
            } catch (const std::exception &x) {
                out << ",\"error\":" << quoted(x.what());
            }
            out << '}';
            out.flush();
        }
        out << "]}\n";
        out.close();
        return out ? 0 : 1;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
