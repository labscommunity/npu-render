// Finite physical-NPU worker. The controller transfers files through WinRM;
// this executable exposes no network endpoint and accepts no model or code.
#define NOMINMAX
#include "lab_exchange_io.hpp"
#include "blend_executor.hpp"
#include <cstdio>
#include <vector>
int wmain(int argc, wchar_t **argv) {
    if (argc != 3) return 2;
    try {
        nr::registered_lab::exchange::Header request{};
        std::vector<uint8_t> background, foreground;
        {
            nr::registered_lab::exchange::File file(argv[1], false);
            const auto size = file.size(); file.read(&request, sizeof(request));
            if (!nr::registered_lab::exchange::request_valid(request, size))
                throw std::runtime_error("invalid NPU worker request");
            background.resize(request.frame_bytes); foreground.resize(request.frame_bytes);
            file.read(background.data(), request.frame_bytes); file.read(foreground.data(), request.frame_bytes);
        }
        nr::registered_lab::BlendExecutor executor("NPU");
        const auto result = executor.compose(request.width, request.height, background, foreground);
        if ((result.execution_device != "NPU" && result.execution_device.rfind("NPU.", 0) != 0) ||
            result.pixels.size() != request.frame_bytes)
            throw std::runtime_error("NPU worker execution identity mismatch");
        auto response = request; response.kind = 2; response.device = 3;
        const std::wstring ready = argv[2], temporary = ready + L".tmp";
        {
            nr::registered_lab::exchange::File file(temporary, true);
            file.write(&response, sizeof(response)); file.write(result.pixels.data(), response.frame_bytes); file.flush();
        }
        nr::registered_lab::exchange::publish(temporary, ready);
        std::printf("{\"sequence\":%u,\"width\":%u,\"height\":%u,\"input_bytes\":%llu,"
                    "\"output_bytes\":%u,\"requested_backend\":\"NPU\",\"execution_device\":\"%s\",\"passed\":true}\n",
                    request.sequence, request.width, request.height, uint64_t(request.frame_bytes) * 2,
                    response.frame_bytes, result.execution_device.c_str());
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "NPU exchange failed: %s\n", error.what());
        return 1;
    }
}
