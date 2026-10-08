#pragma once
#include "../windows/blend_model.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <openvino/runtime/intel_npu/properties.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace nr {
struct BlendSpan {
    const uint8_t *source;
    uint8_t *destination;
    size_t pixels;
};
class PackedBlendProvider {
    ov::Core core;
    ov::CompiledModel compiled;
    ov::InferRequest request;
    static constexpr size_t width = 1024, height = 512, capacity = width * height;

  public:
    std::string execution_device, driver;
    uint64_t dispatches = 0, processed_pixels = 0, host_input_bytes = 0, host_output_bytes = 0;
    double inference_ms = 0;
    explicit PackedBlendProvider(const std::string &backend) {
        if (backend != "NPU" && backend != "GPU" && backend != "CPU")
            throw std::invalid_argument("explicit CPU, GPU or NPU backend required");
        compiled =
            core.compile_model(blend_model(width, height, true, 4, "convolution"), backend,
                               ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY));
        const auto devices = compiled.get_property(ov::execution_devices);
        if (devices.size() != 1 ||
            (devices[0] != backend && devices[0].rfind(backend + ".", 0) != 0))
            throw std::runtime_error("unexpected composition execution device");
        execution_device = devices[0];
        if (backend == "NPU")
            driver = std::to_string(core.get_property("NPU", ov::intel_npu::driver_version));
        request = compiled.create_infer_request();
        qualify();
    }
    void qualify() {
        auto bg = request.get_input_tensor(0), fg = request.get_input_tensor(1);
        auto b = bg.data<uint8_t>(), f = fg.data<uint8_t>();
        for (size_t i = 0; i < capacity; ++i) {
            const unsigned a = i % 256;
            for (size_t c = 0; c < 4; ++c) {
                b[i * 4 + c] = uint8_t(c == 3 ? 255 : (i * 13 + c * 41) % 256);
                f[i * 4 + c] = uint8_t(c == 3 ? a : (i * 7 + c * 19) % (a + 1));
            }
        }
        request.infer();
        auto output = request.get_output_tensor().data<uint8_t>();
        for (size_t i = 0; i < capacity; ++i)
            for (size_t c = 0; c < 4; ++c) {
                unsigned expected =
                    std::min(255u, unsigned(f[i * 4 + c]) +
                                       (unsigned(b[i * 4 + c]) * (255 - f[i * 4 + 3]) + 127) / 255);
                if (std::abs(int(output[i * 4 + c]) - int(expected)) > 1)
                    throw std::runtime_error("packed blend qualification failed");
            }
    }
    void compose(const uint8_t *source, size_t source_stride, uint8_t *destination,
                 size_t destination_stride, size_t region_width, size_t region_height) {
        if (!source || !destination || !region_width || !region_height || region_width > 16384 ||
            region_height > 16384 || source_stride < region_width * 4 ||
            destination_stride < region_width * 4)
            throw std::invalid_argument("invalid packed blend region");
        std::vector<BlendSpan> spans;
        for (size_t y = 0; y < region_height; ++y)
            spans.push_back(
                {source + y * source_stride, destination + y * destination_stride, region_width});
        compose_spans(spans);
    }
    void compose_spans(const std::vector<BlendSpan> &spans) {
        size_t count = 0;
        for (const auto &span : spans) {
            if (!span.source || !span.destination || !span.pixels ||
                span.pixels > 16384u * 16384u - count)
                throw std::invalid_argument("invalid packed blend span");
            count += span.pixels;
        }
        if (!count)
            return;
        std::vector<uint8_t> result(count * 4);
        auto bg = request.get_input_tensor(0), fg = request.get_input_tensor(1);
        auto b = bg.data<uint8_t>(), f = fg.data<uint8_t>();
        size_t span_index = 0, within = 0;
        for (size_t offset = 0; offset < count; offset += capacity) {
            const size_t pixels = std::min(capacity, count - offset);
            // Every model input is initialized, including a short final chunk.
            std::memset(b + pixels * 4, 0, (capacity - pixels) * 4);
            std::memset(f + pixels * 4, 0, (capacity - pixels) * 4);
            size_t at = 0;
            while (at < pixels) {
                const auto &span = spans.at(span_index);
                const size_t run = std::min(pixels - at, span.pixels - within);
                std::memcpy(b + at * 4, span.destination + within * 4, run * 4);
                std::memcpy(f + at * 4, span.source + within * 4, run * 4);
                at += run;
                within += run;
                if (within == span.pixels) {
                    ++span_index;
                    within = 0;
                }
            }
            const auto start = std::chrono::steady_clock::now();
            request.infer();
            inference_ms +=
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count();
            std::memcpy(result.data() + offset * 4, request.get_output_tensor().data(), pixels * 4);
            ++dispatches;
            processed_pixels += pixels;
            host_input_bytes += capacity * 8;
            host_output_bytes += pixels * 4;
        }
        // Stage every span before committing any destination bytes. A failed
        // inference cannot publish a partly updated layer.
        size_t offset = 0;
        for (const auto &span : spans) {
            std::memcpy(span.destination, result.data() + offset * 4, span.pixels * 4);
            offset += span.pixels;
        }
    }
};
} // namespace nr
