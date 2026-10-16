#include "../../include/visible_spans.hpp"
#include "../../providers/packed_blend.hpp"
#include "desktop_sources.hpp"
#include "presenter.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>

using Clock = std::chrono::steady_clock;
static std::string quote(const std::string &s) {
    std::string r = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\')
            r += '\\';
        if (c >= 32)
            r += char(c);
    }
    return r + '"';
}
struct Source {
    nr::capture::Entry entry;
    std::unique_ptr<nr::capture::Window> capture;
    nr::capture::Image image;
    bool reported_size_wait = false;
    std::vector<uint8_t> coverage_alpha;
    std::vector<nr::VisibleSpan> spans;
    Source(nr::capture::Device &device, const nr::capture::Entry &e)
        : entry(e), capture(std::make_unique<nr::capture::Window>(device, e.hwnd)) {}
};
static RECT clipped(const RECT &r, UINT width, UINT height) {
    RECT screen{0, 0, LONG(width), LONG(height)}, result{};
    IntersectRect(&result, &r, &screen);
    return result;
}
static void cpu_over(const Source &s, std::vector<uint8_t> &canvas, UINT width, UINT height) {
    const RECT bounds = nr::capture::source_bounds(s.entry, s.image),
               r = clipped(bounds, width, height);
    for (LONG y = r.top; y < r.bottom; ++y)
        for (LONG x = r.left; x < r.right; ++x) {
            const auto *f = s.image.bgra.data() +
                            (size_t(y - bounds.top) * s.image.width + x - bounds.left) * 4;
            auto *b = canvas.data() + (size_t(y) * width + x) * 4;
            for (size_t c = 0; c < 4; ++c)
                b[c] = uint8_t(
                    std::min(255u, unsigned(f[c]) + (unsigned(b[c]) * (255 - f[3]) + 127) / 255));
        }
}
static void blend(nr::PackedBlendProvider &provider, const Source &s, std::vector<uint8_t> &canvas,
                  UINT width, UINT height) {
    const RECT bounds = nr::capture::source_bounds(s.entry, s.image),
               r = clipped(bounds, width, height);
    if (r.right <= r.left || r.bottom <= r.top)
        return;
    provider.compose(s.image.bgra.data() +
                         (size_t(r.top - bounds.top) * s.image.width + r.left - bounds.left) * 4,
                     size_t(s.image.width) * 4,
                     canvas.data() + (size_t(r.top) * width + r.left) * 4, size_t(width) * 4,
                     r.right - r.left, r.bottom - r.top);
}
int main(int argc, char **argv) {
    std::string backend = "NPU", report = "desktop-offload.json", stop, ready, mode = "compose";
    double seconds = 30;
    std::string state_file;
    bool calibrate_overlay = false, compare_original = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (i + 1 == argc)
            return 2;
        std::string value = argv[++i];
        if (a == "--backend")
            backend = value;
        else if (a == "--report")
            report = value;
        else if (a == "--seconds")
            seconds = std::stod(value);
        else if (a == "--state-file")
            state_file = value;
        else if (a == "--stop-file")
            stop = value;
        else if (a == "--ready-file")
            ready = value;
        else if (a == "--compare-original")
            compare_original = value == "true";
        else if (a == "--calibrate-system-overlay")
            calibrate_overlay = value == "true";
        else if (a == "--mode")
            mode = value;
        else
            return 2;
    }
    if (seconds <= 0 || seconds > 3600 || (mode != "compose" && mode != "capture"))
        return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    bool passed = false;
    std::string error;
    UINT width = 0, height = 0;
    uint64_t frames = 0, updates = 0, capture_bytes = 0, present_bytes = 0, culled_pixels = 0,
             retired_sessions = 0;
    unsigned oracle_error = 0, screen_error = 0, raw_screen_error = 0, overlay_pixels = 0,
             original_error = 0;
    size_t source_count = 0;
    double elapsed = 0, composition_ms = 0;
    const auto launched = Clock::now();
    uint64_t original_different_pixels = 0, compared_pixels = 0;
    std::vector<std::string> classes;
    std::unique_ptr<nr::PackedBlendProvider> provider;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        if (!nr::capture::input_desktop_ready())
            throw std::runtime_error("interactive Default desktop required");
        if (GetSystemMetrics(SM_CMONITORS) != 1)
            throw std::runtime_error("current desktop host requires one monitor");
        width = GetSystemMetrics(SM_CXSCREEN);
        height = GetSystemMetrics(SM_CYSCREEN);
        if (!width || !height || width > 8192 || height > 8192)
            throw std::runtime_error("desktop dimensions unsupported");
        nr::capture::Device device;
        provider = std::make_unique<nr::PackedBlendProvider>(backend);
        nr::capture::Presenter presenter(device, width, height);
        const auto original_samples =
            compare_original ? presenter.read_screen() : nr::capture::Presenter::ScreenSample{};
        std::map<HWND, std::unique_ptr<Source>> sources;
        std::vector<HWND> last_order;
        std::vector<uint8_t> base(size_t(width) * height * 4), canvas(base.size());
        for (size_t i = 3; i < base.size(); i += 4)
            base[i] = 255;
        bool base_ready = false, need_frame = true, first = true, layout_dirty = true;
        const auto started = Clock::now();
        auto last_ready = started;
        auto next_state = started;
        while (!presenter.stopped &&
               std::chrono::duration<double>(Clock::now() - started).count() < seconds) {
            const bool was_enabled = presenter.enabled;
            presenter.messages();
            device.reap();
            if (!state_file.empty() && Clock::now() >= next_state) {
                const std::filesystem::path target(state_file), temporary(state_file + ".tmp");
                std::ofstream state(temporary);
                state << "{\"pid\":" << GetCurrentProcessId()
                      << ",\"enabled\":" << (presenter.enabled ? "true" : "false")
                      << ",\"visible\":" << (presenter.visible ? "true" : "false")
                      << ",\"frames\":" << frames << ",\"dispatches\":" << provider->dispatches
                      << ",\"sources\":" << source_count
                      << ",\"pending_retirements\":" << device.retirements.size()
                      << ",\"tick_ms\":" << GetTickCount64() << "}\n";
                state.close();
                if (!state ||
                    !MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING))
                    throw std::runtime_error("desktop state publication failed");
                next_state = Clock::now() + std::chrono::milliseconds(250);
            }
            if (presenter.enabled != was_enabled) {
                need_frame = true;
                last_ready = Clock::now();
            }
            if (!stop.empty() && std::filesystem::exists(stop))
                break;
            if (!nr::capture::input_desktop_ready())
                throw std::runtime_error("input desktop changed; original desktop restored");
            if (UINT(GetSystemMetrics(SM_CXSCREEN)) != width ||
                UINT(GetSystemMetrics(SM_CYSCREEN)) != height)
                throw std::runtime_error("display dimensions changed; original desktop restored");
            if (!presenter.enabled) {
                presenter.hide();
                sources.clear();
                last_order.clear();
                source_count = 0;
                base_ready = false;
                need_frame = layout_dirty = true;
                Sleep(10);
                continue;
            }
            auto entries = nr::capture::enumerate_sources();
            std::vector<HWND> order;
            for (const auto &e : entries)
                order.push_back(e.hwnd);
            if (order != last_order)
                need_frame = layout_dirty = true;
            last_order = order;
            std::set<HWND> alive(order.begin(), order.end());
            for (auto it = sources.begin(); it != sources.end();)
                if (!alive.count(it->first)) {
                    it = sources.erase(it);
                    need_frame = true;
                } else
                    ++it;
            bool all_ready = true;
            Source *shell = nullptr;
            for (const auto &e : entries) {
                auto previous = sources.find(e.hwnd);
                if (previous != sources.end() && (previous->second->entry.pid != e.pid ||
                                                  previous->second->entry.name != e.name)) {
                    sources.erase(previous);
                    need_frame = layout_dirty = true;
                }
                if (!sources.count(e.hwnd)) {
                    sources.emplace(e.hwnd, std::make_unique<Source>(device, e));
                    need_frame = layout_dirty = true;
                }
                auto &s = *sources.at(e.hwnd);
                if (std::memcmp(&s.entry.bounds, &e.bounds, sizeof(RECT)) ||
                    std::memcmp(&s.entry.rect, &e.rect, sizeof(RECT)))
                    need_frame = layout_dirty = true;
                s.entry = e;
                nr::capture::Image updated;
                if (s.capture->poll(updated)) {
                    ++updates;
                    capture_bytes += updated.bgra.size();
                    if (updated.width != s.image.width || updated.height != s.image.height ||
                        updated.bgra != s.image.bgra) {
                        need_frame = true;
                        if (e.hwnd == GetShellWindow())
                            base_ready = false;
                    }
                    const size_t pixels = size_t(updated.width) * updated.height;
                    if (s.coverage_alpha.size() != pixels) {
                        s.coverage_alpha.assign(pixels, 0);
                        layout_dirty = true;
                    }
                    for (size_t i = 0; i < pixels; ++i) {
                        auto a = updated.bgra[i * 4 + 3];
                        // Keep nonzero RGB even when capture reports zero alpha;
                        // this preserves the established source-over byte contract.
                        if (!a && (updated.bgra[i * 4] || updated.bgra[i * 4 + 1] ||
                                   updated.bgra[i * 4 + 2]))
                            a = 1;
                        if (s.coverage_alpha[i] != a) {
                            s.coverage_alpha[i] = a;
                            layout_dirty = true;
                        }
                    }
                    s.image = std::move(updated);
                }
                if (s.image.bgra.empty())
                    all_ready = false;
                else {
                    try {
                        nr::capture::source_bounds(e, s.image);
                        s.reported_size_wait = false;
                    } catch (...) {
                        if (!s.reported_size_wait)
                            std::cout << "pending source dimensions: " << e.name
                                      << " capture=" << s.image.width << 'x' << s.image.height
                                      << " bounds=" << e.bounds.right - e.bounds.left << 'x'
                                      << e.bounds.bottom - e.bounds.top
                                      << " rect=" << e.rect.right - e.rect.left << 'x'
                                      << e.rect.bottom - e.rect.top << std::endl;
                        s.reported_size_wait = true;
                        all_ready = false;
                    }
                }
                if (e.hwnd == GetShellWindow())
                    shell = &s;
            }
            source_count = sources.size();
            if (!all_ready || !shell) {
                presenter.hide();
                if (std::chrono::duration<double>(Clock::now() - last_ready).count() > 5)
                    throw std::runtime_error("complete desktop source coverage unavailable");
                Sleep(5);
                continue;
            }
            last_ready = Clock::now();
            if (!need_frame) {
                Sleep(5);
                continue;
            }
            if (shell->image.width != width || shell->image.height != height)
                throw std::runtime_error("desktop background extent mismatch");
            const auto compose_start = Clock::now();
            if (layout_dirty) {
                std::vector<uint8_t> covered(size_t(width) * height);
                for (const auto &e : entries) {
                    if (e.hwnd == shell->entry.hwnd)
                        continue;
                    auto &s = *sources.at(e.hwnd);
                    const auto bounds = nr::capture::source_bounds(e, s.image);
                    s.spans =
                        nr::visible_spans(s.coverage_alpha.data(), s.image.width, s.image.height,
                                          bounds.left, bounds.top, width, height, covered);
                }
                layout_dirty = false;
            }
            if (!base_ready) {
                std::fill(base.begin(), base.end(), 0);
                for (size_t i = 3; i < base.size(); i += 4)
                    base[i] = 255;
                blend(*provider, *shell, base, width, height);
                base_ready = true;
            }
            canvas = base;
            for (auto it = entries.rbegin(); it != entries.rend(); ++it)
                if (it->hwnd != shell->entry.hwnd) {
                    const auto &s = *sources.at(it->hwnd);
                    std::vector<nr::BlendSpan> spans;
                    size_t visible = 0;
                    for (const auto &span : s.spans) {
                        spans.push_back({s.image.bgra.data() + span.source_pixel * 4,
                                         canvas.data() + span.destination_pixel * 4, span.pixels});
                        visible += span.pixels;
                    }
                    const auto r =
                        clipped(nr::capture::source_bounds(s.entry, s.image), width, height);
                    culled_pixels += size_t(r.right - r.left) * size_t(r.bottom - r.top) - visible;
                    provider->compose_spans(spans);
                }
            composition_ms +=
                std::chrono::duration<double, std::milli>(Clock::now() - compose_start).count();
            if (first) {
                auto expected = shell->image.bgra;
                for (auto it = entries.rbegin(); it != entries.rend(); ++it)
                    if (it->hwnd != shell->entry.hwnd)
                        cpu_over(*sources.at(it->hwnd), expected, width, height);
                for (size_t i = 0; i < canvas.size(); ++i)
                    oracle_error = std::max(oracle_error,
                                            unsigned(std::abs(int(canvas[i]) - int(expected[i]))));
                if (oracle_error > sources.size())
                    throw std::runtime_error("desktop source-over oracle failed");
                for (const auto &e : entries)
                    classes.push_back(e.name);
            }
            if (mode == "compose" && presenter.enabled) {
                if (first && calibrate_overlay) {
                    presenter.calibrate_system_overlay();
                    overlay_pixels = presenter.overlay_pixels;
                }
                presenter.present(canvas);
                present_bytes += canvas.size();
                if (first) {
                    presenter.present(canvas);
                    screen_error = presenter.screen_error(canvas);
                    raw_screen_error = presenter.raw_screen_error;
                    compared_pixels = presenter.screen_compared_pixels;
                    if (compare_original) {
                        original_error = presenter.compare_screen(canvas, original_samples);
                        original_different_pixels = presenter.screen_differing_pixels;
                    }
                    if (screen_error > 2)
                        throw std::runtime_error("desktop output screen oracle failed");
                }
            } else
                presenter.hide();
            ++frames;
            need_frame = false;
            if (first) {
                if (!ready.empty()) {
                    std::ofstream f(ready);
                    f << "ready\n";
                    if (!f)
                        throw std::runtime_error("readiness write failed");
                }
                std::cout << "ready: " << width << 'x' << height << " sources=" << sources.size()
                          << " device=" << provider->execution_device << std::endl;
                first = false;
            }
            Sleep(5);
        }
        presenter.hide();
        sources.clear();
        device.drain();
        retired_sessions = device.retired_sessions;
        elapsed = std::chrono::duration<double>(Clock::now() - started).count();
        if (first || !frames)
            throw std::runtime_error("no complete desktop frame produced");
        passed = true;
    } catch (const winrt::hresult_error &e) {
        error = winrt::to_string(e.message());
    } catch (const std::exception &e) {
        error = e.what();
    }
    const double total_elapsed = std::chrono::duration<double>(Clock::now() - launched).count();
    std::ofstream out(report);
    out << "{\"scope\":\"per-window Windows capture to selected composition backend and opaque "
           "desktop output; client rendering and capture remain\",\"passed\":"
        << (passed ? "true" : "false") << ",\"error\":" << quote(error)
        << ",\"mode\":" << quote(mode) << ",\"backend\":" << quote(backend)
        << ",\"width\":" << width << ",\"height\":" << height << ",\"frames\":" << frames
        << ",\"source_updates\":" << updates << ",\"last_source_count\":" << source_count
        << ",\"retired_capture_sessions\":" << retired_sessions
        << ",\"culled_layer_pixels\":" << culled_pixels
        << ",\"capture_readback_bytes\":" << capture_bytes
        << ",\"presentation_upload_bytes\":" << present_bytes
        << ",\"maximum_composition_byte_error\":" << oracle_error
        << ",\"full_desktop_offload\":false"
        << ",\"compared_original_desktop\":" << (compare_original ? "true" : "false")
        << ",\"original_desktop_error\":" << original_error
        << ",\"original_desktop_differing_pixels\":" << original_different_pixels
        << ",\"screen_compared_pixels\":" << compared_pixels
        << ",\"raw_screen_byte_error\":" << raw_screen_error
        << ",\"retained_system_overlay_pixels\":" << overlay_pixels
        << ",\"maximum_screen_byte_error\":" << screen_error << ",\"elapsed_seconds\":" << elapsed
        << ",\"total_elapsed_seconds\":" << total_elapsed
        << ",\"composition_ms\":" << composition_ms << ",\"classes\":[";
    for (size_t i = 0; i < classes.size(); ++i) {
        if (i)
            out << ',';
        out << quote(classes[i]);
    }
    out << ']';
    if (provider)
        out << ",\"provider\":{\"execution_device\":" << quote(provider->execution_device)
            << ",\"driver\":" << quote(provider->driver)
            << ",\"dispatches\":" << provider->dispatches
            << ",\"processed_pixels\":" << provider->processed_pixels
            << ",\"inference_ms\":" << provider->inference_ms
            << ",\"host_input_bytes\":" << provider->host_input_bytes
            << ",\"host_output_bytes\":" << provider->host_output_bytes << '}';
    out << "}\n";
    out.close();
    std::cout << (passed ? "passed: " : "failed: ") << error << std::endl;
    return passed && out ? 0 : 1;
}
