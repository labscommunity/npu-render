#include "npu_render.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

int main(int argc, char **argv) {
    const char *backend = argc > 1 ? argv[1] : "CPU";
    char error[2048];
    nr_context *context = nullptr;
    try {
        context = nr_create(backend, 32, error, sizeof(error));
        require(context != nullptr, error);
        const uint32_t width = 71, height = 39;
        const size_t stride = width * 4 + 20;
        std::vector<uint8_t> source(stride * height, 0xAB), dest(stride * height, 0xCD);
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                const uint8_t alpha = (x + y * width) % 256;
                for (size_t c = 0; c < 4; ++c) {
                    source[y * stride + x * 4 + c] = c == 3 ? alpha : (x * 17 + c * 31) % (alpha + 1);
                    dest[y * stride + x * 4 + c] = c == 3 ? 255 : (y * 19 + c * 7) % 256;
                }
            }
        }
        auto original = dest;
        nr_surface s{source.data(), source.size(), stride, width, height, 0};
        nr_surface d{dest.data(), dest.size(), stride, width, height, 0};
        nr_region r{3, 2, 7, 4, 60, 32};
        nr_frame_stats stats;
        require(nr_blend(context, &s, &d, &r, 1.0f, &stats, error, sizeof(error)) == 0, error);
        int maximum_error = 0;
        for (size_t y = 0; y < height; ++y) {
            for (size_t byte = 0; byte < stride; ++byte) {
                const size_t x = byte / 4, c = byte % 4;
                int expected = original[y * stride + byte];
                if (y >= r.destination_y && y < r.destination_y + r.height &&
                    x >= r.destination_x && x < r.destination_x + r.width) {
                    const auto *pixel = source.data() + (y - r.destination_y + r.source_y) * stride +
                                        (x - r.destination_x + r.source_x) * 4;
                    expected = pixel[c] + (expected * (255 - pixel[3]) + 127) / 255;
                }
                const int difference = std::abs(expected - int(dest[y * stride + byte]));
                const bool touched = y >= r.destination_y && y < r.destination_y + r.height &&
                                     x >= r.destination_x && x < r.destination_x + r.width;
                require(difference <= (touched ? 1 : 0), "pixel or untouched padding mismatch");
                maximum_error = std::max(maximum_error, difference);
            }
        }
        require(stats.dispatches == 2, "expected multiple fixed-shape chunks");
        const auto valid = dest;
        nr_region invalid = r;
        invalid.destination_x = width;
        require(nr_blend(context, &s, &d, &invalid, 1, &stats, error, sizeof(error)) != 0,
                "bad bounds accepted");
        require(dest == valid, "invalid call mutated destination");
        require(nr_switch(context, "AUTO", error, sizeof(error)) != 0, "AUTO accepted");
        require(nr_blend(context, &s, &d, &r, 0, &stats, error, sizeof(error)) == 0, error);
        for (size_t i = 0; i < dest.size(); ++i)
            require(std::abs(int(dest[i]) - int(valid[i])) <= 1, "failed switch damaged context");
        auto before_batch = dest;
        nr_region batch[] = {{0, 0, 0, 0, 10, 5}, {0, 0, 20, 20, 12, 6}};
        require(nr_blend_regions(context, &s, &d, batch, 2, 1, &stats, error, sizeof(error)) == 0,
                error);
        require(stats.pixels == 122 && stats.dispatches == 1, "damage not batched into one dispatch");
        for (size_t y = 0; y < height; ++y) {
            for (size_t byte = 0; byte < stride; ++byte) {
                const size_t x = byte / 4, c = byte % 4;
                int expected = before_batch[y * stride + byte];
                bool touched = false;
                for (const auto &rect : batch) {
                    if (x >= rect.destination_x && x < rect.destination_x + rect.width &&
                        y >= rect.destination_y && y < rect.destination_y + rect.height) {
                        const auto *p = source.data() + (y - rect.destination_y) * stride +
                                        (x - rect.destination_x) * 4;
                        expected = p[c] + (expected * (255 - p[3]) + 127) / 255;
                        touched = true;
                    }
                }
                require(std::abs(expected - int(dest[y * stride + byte])) <= (touched ? 1 : 0),
                        "batched damage result mismatch");
            }
        }
        const auto batch_result = dest;
        batch[1].destination_x = 5;
        batch[1].destination_y = 2;
        require(nr_blend_regions(context, &s, &d, batch, 2, 1, &stats, error, sizeof(error)) != 0,
                "overlapping damage accepted");
        require(dest == batch_result, "invalid batch partially committed");
        require(nr_blend(context, &s, &d, &r, 0.5f, &stats, error, sizeof(error)) == 0, error);
        require(nr_compose_regions(context, &s, &d, &r, 1, NR_SOURCE, 1.0f,
                                    &stats, error, sizeof(error)) == 0, error);
        for (size_t y = 0; y < r.height; ++y) {
            for (size_t x = 0; x < r.width; ++x) {
                for (size_t c = 0; c < 4; ++c) {
                    require(dest[(y + r.destination_y) * stride + (x + r.destination_x) * 4 + c] ==
                            source[(y + r.source_y) * stride + (x + r.source_x) * 4 + c],
                            "source copy changed a byte");
                }
            }
        }
        std::cout << "{\"backend\":\"" << backend << "\",\"execution_devices\":\""
                  << stats.execution_devices << "\",\"max_byte_error\":" << maximum_error
                  << ",\"pixels\":" << stats.pixels << ",\"dispatches\":" << stats.dispatches
                  << ",\"elapsed_ms\":" << stats.elapsed_ms << ",\"passed\":true}\n";
        nr_destroy(context);
        return 0;
    } catch (const std::exception &e) {
        nr_destroy(context);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
