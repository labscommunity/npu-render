// Native window for the measured shared-buffer provider. The OS compositor and
// display engine still present this application's swapchain.
#include "live_renderer.hpp"
#include "selection.hpp"

struct Options {
    UINT width = 1280, height = 720, fps = 60;
    double seconds = 30, switch_every = 10;
    std::string backend = "GPU", report = "native-live.json", ready_file, stop_file;
    std::vector<std::string> sequence;
    bool validate_only = false;
    bool pinned = false;
    std::string config;
    std::string expected_npu_driver;
    std::string npu_alpha = "gather";
    bool profile_npu = false;
    std::string diagnostic = "none";
    double diagnostic_wait_ms = 0;
    std::string npu_workload = "DEFAULT";
    int64_t npu_tiles = 0;
};

bool valid_backend(const std::string &name) {
    return name == "GPU" || name == "NPU" || name == "CPU" || name == "IDLE";
}

Options options(int argc, char **argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--validate-only") { o.validate_only = true; continue; }
        if (arg == "--profile-npu") { o.profile_npu = true; continue; }
        if (i + 1 == argc) throw std::invalid_argument("missing option value: " + arg);
        const std::string value = argv[++i];
        if (arg == "--width") o.width = std::stoul(value);
        else if (arg == "--height") o.height = std::stoul(value);
        else if (arg == "--fps") o.fps = std::stoul(value);
        else if (arg == "--seconds") o.seconds = std::stod(value);
        else if (arg == "--switch-every") o.switch_every = std::stod(value);
        else if (arg == "--backend") { o.backend = value; o.pinned = true; }
        else if (arg == "--config") o.config = value;
        else if (arg == "--report") o.report = value;
        else if (arg == "--expected-npu-driver") o.expected_npu_driver = value;
        else if (arg == "--npu-alpha") o.npu_alpha = value;
        else if (arg == "--npu-workload") o.npu_workload = value;
        else if (arg == "--npu-tiles") o.npu_tiles = std::stoll(value);
        else if (arg == "--diagnostic") o.diagnostic = value;
        else if (arg == "--diagnostic-wait-ms") o.diagnostic_wait_ms = std::stod(value);
        else if (arg == "--ready-file") o.ready_file = value;
        else if (arg == "--stop-file") o.stop_file = value;
        else if (arg == "--sequence") {
            std::istringstream items(value);
            for (std::string item; std::getline(items, item, ','); ) o.sequence.push_back(item);
        } else throw std::invalid_argument("unknown option: " + arg);
    }
    if (o.config.empty()) {
        if (const char *custom = std::getenv("NPU_RENDER_CONFIG")) o.config = custom;
        else if (const char *local = std::getenv("LOCALAPPDATA"))
            o.config = (std::filesystem::path(local) / "npu-render" / "config.json").string();
    }
    if (!o.pinned) {
        if (const char *backend = std::getenv("NPU_RENDER_BACKEND")) {
            o.backend = backend; o.pinned = true;
        } else if (!o.config.empty()) {
            nr::SelectionFile selection(o.config);
            if (auto selected = selection.changed()) o.backend = *selected;
        }
    }
    if (o.width < 64 || o.width > 4096 || o.width % 64 || !o.height || o.height > 4096 ||
        uint64_t(o.width)*o.height > 65535u*256u ||
        !o.fps || o.fps > 240 || !std::isfinite(o.seconds) || o.seconds <= 0 || o.seconds > 3600 ||
        !std::isfinite(o.switch_every) || o.switch_every <= 0 || !valid_backend(o.backend) ||
        (o.npu_alpha != "gather" && o.npu_alpha != "convolution") ||
        (o.npu_workload != "DEFAULT" && o.npu_workload != "EFFICIENT") || o.npu_tiles < 0 ||
        std::any_of(o.sequence.begin(), o.sequence.end(), [](const auto &s) { return !valid_backend(s); }))
        throw std::invalid_argument("invalid options; width must be a multiple of 64, backend GPU/NPU/CPU/IDLE");
    if ((o.diagnostic != "none" && o.diagnostic != "compute-only" && o.diagnostic != "transfer-present") ||
        !std::isfinite(o.diagnostic_wait_ms) || o.diagnostic_wait_ms < 0 || o.diagnostic_wait_ms > 10)
        throw std::invalid_argument("invalid diagnostic mode or wait (0..10 ms)");
    if (o.diagnostic != "none" || o.diagnostic_wait_ms > 0) {
        if (!o.pinned || !o.sequence.empty() || (o.backend != "GPU" && o.backend != "NPU"))
            throw std::invalid_argument("diagnostics require a fixed GPU/NPU backend without a switch sequence");
        if ((o.diagnostic == "transfer-present" || o.diagnostic_wait_ms > 0) && o.backend != "GPU")
            throw std::invalid_argument("transfer/presentation and wait controls require GPU backend");
        if (o.diagnostic != "none" && o.diagnostic_wait_ms > 0)
            throw std::invalid_argument("wait control cannot be combined with stage omission");
    }
    return o;
}

struct Frame {
    size_t slot;
    std::string backend;
    double elapsed, scene, upload, compose, wait, present, work;
    bool presented, composed;
    HRESULT presentation_status;
};

int main(int argc, char **argv) {
    std::string stage = "options";
    std::vector<Frame> frames;
    std::vector<std::string> selections;
    try {
        const auto o = options(argc, argv);
        stage = "device_and_providers";
        Renderer renderer(o.width, o.height, o.expected_npu_driver, o.npu_alpha, o.profile_npu,
                          o.npu_workload, o.npu_tiles);
        stage = "qualification"; renderer.qualify();
        size_t skipped = 0, misses = 0;
        double started_unix_s = 0;
        double live_cpu_seconds = 0;
        double live_wall_seconds = 0;
        HRESULT initial_present_status = S_OK;
        if (!o.validate_only) {
            stage = "window"; Window window(renderer);
            window.allow_switch = o.diagnostic == "none" && o.diagnostic_wait_ms == 0;
            FramePacer pacer;
            renderer.background(); renderer.scene(0); renderer.inputs(true);
            renderer.compose(o.backend); window.messages();
            initial_present_status = window.present(renderer); window.title(o.backend);
            // All providers already compiled and qualified. Activation is at a
            // frame boundary; this harness measures no background compilation.
            std::string backend = o.backend;
            nr::SelectionFile saved(o.config);
            size_t selection = 0, slot = 0;
            double next_config_check = 0;
            const auto started = Clock::now();
            const double cpu_start = process_cpu_seconds();
            started_unix_s = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
            if (!o.ready_file.empty()) {
                std::ofstream ready(o.ready_file); ready << "ready\n";
                if (!ready) throw std::runtime_error("cannot create readiness file");
            }
            stage = "live";
            while (!window.stopped) {
                auto target = started + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(double(slot)/o.fps));
                pacer.until(target);
                window.messages();
                const auto frame_start = Clock::now();
                const double elapsed = milliseconds(started, frame_start)/1000;
                if (elapsed >= o.seconds || window.stopped) break;
                if (!o.stop_file.empty() && GetFileAttributesA(o.stop_file.c_str()) != INVALID_FILE_ATTRIBUTES) break;
                if (!o.pinned && !o.config.empty() && elapsed >= next_config_check) {
                    next_config_check = elapsed + 0.25;
                    try {
                        if (auto selected = saved.changed()) window.requested = *selected;
                    } catch (const std::exception &error) {
                        selections.push_back("{\"elapsed_s\":" + std::to_string(elapsed) +
                            ",\"accepted\":false,\"error\":" + quote(error.what()) + "}");
                    }
                }
                if (selection < o.sequence.size() && elapsed >= (selection+1)*o.switch_every)
                    window.requested = o.sequence[selection++];
                bool switched = false;
                if (!window.requested.empty()) {
                    backend = window.requested; window.requested.clear(); window.title(backend); switched = true;
                    selections.push_back("{\"elapsed_s\":" + std::to_string(elapsed) +
                        ",\"accepted\":true,\"backend\":" + quote(backend) + "}");
                }
                const size_t current = size_t(elapsed*o.fps);
                if (current > slot) { skipped += current-slot; slot = current; }
                if (backend == "IDLE" && !switched) { ++slot; continue; }
                if (o.diagnostic != "compute-only") renderer.scene(elapsed);
                const auto after_scene = Clock::now();
                // CPU mode avoids unnecessary GPU input uploads.
                if (o.diagnostic != "compute-only" && (backend == "GPU" || backend == "NPU"))
                    renderer.inputs(false);
                const auto after_upload = Clock::now();
                const bool composed = o.diagnostic != "transfer-present";
                if (composed) renderer.compose(backend);
                const auto after_compose = Clock::now();
                if (o.diagnostic_wait_ms > 0) pacer.until(after_compose +
                    std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(o.diagnostic_wait_ms)));
                const auto after_wait = Clock::now();
                const bool presented = o.diagnostic != "compute-only";
                const HRESULT status = presented ? window.present(renderer) : S_OK;
                const auto done = Clock::now();
                const double work = milliseconds(frame_start, done);
                if (milliseconds(started, done) > (slot+1)*1000.0/o.fps) ++misses;
                frames.push_back({slot, backend, elapsed, milliseconds(frame_start, after_scene),
                    milliseconds(after_scene, after_upload), milliseconds(after_upload, after_compose),
                    milliseconds(after_compose, after_wait), milliseconds(after_wait, done), work,
                    presented, composed, status});
                ++slot;
            }
            live_cpu_seconds = process_cpu_seconds() - cpu_start;
            live_wall_seconds = milliseconds(started)/1000;
        }
        stage = "report";
        std::ostringstream report;
        DWORD session = 0; ProcessIdToSessionId(GetCurrentProcessId(), &session);
        report << std::setprecision(12) << "{\"passed\":true,\"adapter\":" << quote(renderer.adapter)
               << ",\"openvino_runtime\":" << quote(ov::get_openvino_version().buildNumber)
               << ",\"npu_driver_version\":" << quote(renderer.npu->driver_version)
               << ",\"npu_driver_query_error\":" << quote(renderer.npu->driver_error)
               << ",\"npu_execution_device\":\"NPU\",\"gpu_provider\":\"D3D12 compute shader\",\"session_id\":" << session
               << ",\"npu_pixel_group\":" << blend_pixel_group(o.width, true)
               << ",\"npu_alpha\":" << quote(o.npu_alpha)
               << ",\"npu_workload\":" << quote(renderer.npu->workload)
               << ",\"npu_requested_tiles\":" << renderer.npu->requested_tiles
               << ",\"npu_max_tiles\":" << renderer.npu->max_tiles
               << ",\"diagnostic\":" << quote(o.diagnostic)
               << ",\"diagnostic_wait_ms\":" << o.diagnostic_wait_ms
               << ",\"full_graphics_trace\":" << (o.diagnostic == "none" ? "true" : "false")
               << ",\"live_process_cpu_seconds\":" << live_cpu_seconds
               << ",\"live_wall_seconds\":" << live_wall_seconds
               << ",\"validation_only\":" << (o.validate_only ? "true" : "false")
               << ",\"width\":" << o.width << ",\"height\":" << o.height << ",\"target_fps\":" << o.fps
               << ",\"requested_seconds\":" << o.seconds << ",\"max_byte_error\":" << renderer.max_error
               << ",\"started_unix_s\":" << started_unix_s
               << ",\"initial_present_hresult\":" << initial_present_status
               << ",\"shared_npu_inputs_and_output\":true,\"host_waited_fences\":true,\"scanout_measured\":false"
               << ",\"skipped_slots\":" << skipped << ",\"deadline_misses\":" << misses << ",\"frames\":[";
        for (size_t i = 0; i < frames.size(); ++i) {
            const auto &f = frames[i];
            report << (i ? "," : "") << "{\"slot\":" << f.slot << ",\"backend\":" << quote(f.backend)
                   << ",\"elapsed_s\":" << f.elapsed << ",\"scene_ms\":" << f.scene << ",\"upload_ms\":" << f.upload
                   << ",\"compose_ms\":" << f.compose << ",\"wait_ms\":" << f.wait
                   << ",\"present_ms\":" << f.present << ",\"work_ms\":" << f.work
                   << ",\"composed\":" << (f.composed ? "true" : "false")
                   << ",\"presented\":" << (f.presented ? "true" : "false")
                   << ",\"present_hresult\":";
            if (f.presented) report << f.presentation_status; else report << "null";
            report << "}";
        }
        report << "],\"selections\":[";
        for (size_t i = 0; i < selections.size(); ++i) report << (i ? "," : "") << selections[i];
        report << "],\"npu_profile\":[";
        if (o.profile_npu) {
            const auto profile = renderer.npu->request.get_profiling_info();
            for (size_t i = 0; i < profile.size(); ++i) {
                const auto &p = profile[i];
                report << (i ? "," : "") << "{\"name\":" << quote(p.node_name)
                    << ",\"type\":" << quote(p.node_type) << ",\"exec_type\":" << quote(p.exec_type)
                    << ",\"real_ms\":" << double(p.real_time.count())/1000
                    << ",\"cpu_ms\":" << double(p.cpu_time.count())/1000 << '}';
            }
        }
        report << "]}\n";
        std::ofstream destination(o.report, std::ios::binary); destination << report.str();
        if (!destination) throw std::runtime_error("cannot write report " + o.report);
        std::cout << "passed: " << frames.size() << " iterations; " << skipped << " skipped slots; report=" << o.report << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "{\"passed\":false,\"stage\":" << quote(stage) << ",\"error\":" << quote(error.what())
                  << ",\"completed_frames\":" << frames.size() << "}\n";
        return 1;
    }
}
