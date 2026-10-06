#include "npu_render.h"

#include <openvino/openvino.hpp>
#include <openvino/opsets/opset13.hpp>
#include <openvino/runtime/properties.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

void copy_text(const std::string &text, char *into, size_t size) {
    if (into && size) {
        const size_t n = std::min(text.size(), size - 1);
        std::memcpy(into, text.data(), n);
        into[n] = '\0';
    }
}

std::string normalize(const char *value) {
    if (!value)
        throw std::invalid_argument("backend is required");
    std::string target(value);
    // Deliberately explicit; compound routing could hide fallback.
    if (target != "CPU" && target != "GPU" && target != "NPU")
        throw std::invalid_argument("native backend must be CPU, GPU or NPU");
    return target;
}

struct Program {
    std::string target;
    std::string actual;
    size_t capacity;
    ov::InferRequest request;
    ov::Tensor background;
    ov::Tensor foreground;
    std::mutex execution;
};

uint8_t byte_value(float value) {
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255));
}

std::shared_ptr<Program> prepare(ov::Core &core, uint32_t side, const std::string &target) {
    namespace op = ov::opset13;
    auto program = std::make_shared<Program>();
    program->target = target;
    program->capacity = size_t(side) * side;
    const ov::Shape shape{1, 4, side, side};
    auto background = std::make_shared<op::Parameter>(ov::element::f16, shape);
    auto foreground = std::make_shared<op::Parameter>(ov::element::f16, shape);
    auto alpha = std::make_shared<op::Gather>(
        foreground, op::Constant::create(ov::element::i64, {1}, {3}),
        op::Constant::create(ov::element::i64, {}, {1}));
    auto inverse = std::make_shared<op::Subtract>(
        op::Constant::create(ov::element::f16, {}, {1.0f}), alpha);
    auto result = std::make_shared<op::Add>(
        foreground, std::make_shared<op::Multiply>(inverse, background));
    auto model = std::make_shared<ov::Model>(ov::OutputVector{result},
                                           ov::ParameterVector{background, foreground});
    ov::AnyMap properties;
    if (target != "NPU")
        properties[ov::hint::inference_precision.name()] = ov::element::f16;
    auto compiled = core.compile_model(model, target, properties);
    const auto actual = compiled.get_property(ov::execution_devices);
    if (actual.empty())
        throw std::runtime_error("runtime returned no execution device");
    for (const auto &device : actual) {
        if (device != target && device.rfind(target + ".", 0) != 0)
            throw std::runtime_error("unexpected execution device: " + device);
        if (!program->actual.empty()) program->actual += ",";
        program->actual += device;
    }
    program->request = compiled.create_infer_request();
    program->background = program->request.get_input_tensor(0);
    program->foreground = program->request.get_input_tensor(1);
    auto *bg = program->background.data<ov::float16>();
    auto *fg = program->foreground.data<ov::float16>();
    const size_t n = program->capacity;
    // Deterministic byte-domain fixtures cover alpha endpoints and fractional
    // colors. Compare against independent integer source-over with <=1 LSB.
    for (size_t i = 0; i < n; ++i) {
        const uint32_t alpha = i % 256;
        for (size_t c = 0; c < 4; ++c) {
            const uint32_t b = c == 3 ? 255 : (i * (c * 2 + 3) + c * 19) % 256;
            const uint32_t f = c == 3 ? alpha : (i * (c + 5) + 17) % (alpha + 1);
            bg[c * n + i] = ov::float16(float(b) / 255);
            fg[c * n + i] = ov::float16(float(f) / 255);
        }
    }
    program->request.infer();
    const auto *out = program->request.get_output_tensor().data<const ov::float16>();
    for (size_t i = 0; i < n; ++i) {
        const uint32_t alpha = i % 256;
        for (size_t c = 0; c < 4; ++c) {
            const uint32_t b = c == 3 ? 255 : (i * (c * 2 + 3) + c * 19) % 256;
            const uint32_t f = c == 3 ? alpha : (i * (c + 5) + 17) % (alpha + 1);
            const int expected = int(f + (b * (255 - alpha) + 127) / 255);
            const float value = float(out[c * n + i]);
            if (!std::isfinite(value) || std::abs(int(byte_value(value)) - expected) > 1)
                throw std::runtime_error("native source-over qualification failed");
        }
    }
    return program;
}

void validate(const nr_surface &surface) {
    if (!surface.data || !surface.width || !surface.height ||
        size_t(surface.width) > std::numeric_limits<size_t>::max() / 4 ||
        surface.stride < size_t(surface.width) * 4 ||
        size_t(surface.height) > std::numeric_limits<size_t>::max() / surface.stride ||
        surface.length < surface.stride * surface.height)
        throw std::invalid_argument("invalid surface extent, stride or storage length");
}
} // namespace

struct nr_context {
    ov::Core core;
    uint32_t side;
    std::mutex switch_mutex;
    std::mutex active_mutex;
    std::shared_ptr<Program> active;
};

extern "C" NR_API nr_context *nr_create(const char *backend, uint32_t tile_side,
                                         char *error, size_t error_size) {
    try {
        if (!tile_side || tile_side > 1024)
            throw std::invalid_argument("tile_side must be in 1..1024");
        auto context = std::make_unique<nr_context>();
        context->side = tile_side;
        context->active = prepare(context->core, tile_side, normalize(backend));
        copy_text("", error, error_size);
        return context.release();
    } catch (const std::exception &e) {
        copy_text(e.what(), error, error_size);
        return nullptr;
    } catch (...) {
        copy_text("unknown native preparation failure", error, error_size);
        return nullptr;
    }
}

extern "C" NR_API void nr_destroy(nr_context *context) { delete context; }

extern "C" NR_API int nr_switch(nr_context *context, const char *backend,
                                 char *error, size_t error_size) {
    try {
        if (!context) throw std::invalid_argument("context is required");
        const auto target = normalize(backend);
        std::lock_guard<std::mutex> changing(context->switch_mutex);
        {
            std::lock_guard<std::mutex> lock(context->active_mutex);
            if (context->active->target == target) {
                copy_text("", error, error_size);
                return 0;
            }
        }
        auto candidate = prepare(context->core, context->side, target);
        {
            std::lock_guard<std::mutex> lock(context->active_mutex);
            context->active = std::move(candidate);
        }
        copy_text("", error, error_size);
        return 0;
    } catch (const std::exception &e) {
        copy_text(e.what(), error, error_size);
        return -1;
    } catch (...) {
        copy_text("unknown native switch failure", error, error_size);
        return -1;
    }
}

extern "C" NR_API int nr_compose_regions(nr_context *context, const nr_surface *source,
                                        nr_surface *destination, const nr_region *regions,
                                        size_t region_count, nr_operator operation, float opacity, nr_frame_stats *stats,
                                        char *error, size_t error_size) {
    if (stats) std::memset(stats, 0, sizeof(*stats));
    try {
        if (!context || !source || !destination || !regions || !region_count || region_count > 4096)
            throw std::invalid_argument("context, surfaces and 1..4096 regions are required");
        if (operation != NR_SOURCE_OVER && operation != NR_SOURCE)
            throw std::invalid_argument("unsupported composition operator");
        validate(*source);
        validate(*destination);
        std::vector<size_t> offsets{0};
        for (size_t index = 0; index < region_count; ++index) {
            const auto &r = regions[index];
            if (!r.width || !r.height || !std::isfinite(opacity) || opacity < 0 || opacity > 1 ||
                r.source_x > source->width || r.width > source->width - r.source_x ||
                r.source_y > source->height || r.height > source->height - r.source_y ||
                r.destination_x > destination->width ||
                r.width > destination->width - r.destination_x ||
                r.destination_y > destination->height ||
                r.height > destination->height - r.destination_y)
                throw std::invalid_argument("invalid blend rectangle or opacity");
            if (size_t(r.width) > std::numeric_limits<size_t>::max() / r.height)
                throw std::invalid_argument("blend rectangle overflows storage");
            const size_t pixels = size_t(r.width) * r.height;
            if (pixels > std::numeric_limits<size_t>::max() / 4 - offsets.back())
                throw std::invalid_argument("blend regions overflow storage");
            offsets.push_back(offsets.back() + pixels);
            for (size_t previous = 0; previous < index; ++previous) {
                const auto &q = regions[previous];
                if (r.destination_x < q.destination_x + q.width &&
                    q.destination_x < r.destination_x + r.width &&
                    r.destination_y < q.destination_y + q.height &&
                    q.destination_y < r.destination_y + r.height)
                    throw std::invalid_argument("destination regions must be disjoint");
            }
        }
        std::shared_ptr<Program> program;
        {
            std::lock_guard<std::mutex> lock(context->active_mutex);
            program = context->active;
        }
        const auto started = Clock::now();
        std::lock_guard<std::mutex> lock(program->execution);
        const size_t count = offsets.back();
        std::vector<uint8_t> staged(count * 4);
        const size_t capacity = program->capacity;
        auto *bg = program->background.data<ov::float16>();
        auto *fg = program->foreground.data<ov::float16>();
        uint64_t dispatches = 0;
        size_t region_index = 0;
        for (size_t offset = 0; offset < count; offset += capacity) {
            const size_t pixels = std::min(capacity, count - offset);
            std::fill_n(bg, capacity * 4, ov::float16(0.0f));
            std::fill_n(fg, capacity * 4, ov::float16(0.0f));
            for (size_t i = 0; i < pixels; ++i) {
                while (offset + i >= offsets[region_index + 1]) ++region_index;
                const auto &r = regions[region_index];
                const size_t within = offset + i - offsets[region_index];
                const size_t row = within / r.width;
                const size_t column = within % r.width;
                const auto *s = source->data + (r.source_y + row) * source->stride +
                                4 * (r.source_x + column);
                const auto *d = destination->data + (r.destination_y + row) * destination->stride +
                                4 * (r.destination_x + column);
                for (size_t c = 0; c < 4; ++c) {
                    const uint8_t f = c == 3 && source->opaque ? 255 : s[c];
                    const uint8_t b = c == 3 && destination->opaque ? 255 : d[c];
                    fg[c * capacity + i] = ov::float16(float(f) * opacity / 255);
                    bg[c * capacity + i] = ov::float16(operation == NR_SOURCE ? 0.0f : float(b) / 255);
                }
            }
            program->request.infer();
            ++dispatches;
            const auto *out = program->request.get_output_tensor().data<const ov::float16>();
            for (size_t i = 0; i < pixels; ++i) {
                for (size_t c = 0; c < 4; ++c) {
                    const float value = float(out[c * capacity + i]);
                    if (!std::isfinite(value))
                        throw std::runtime_error("non-finite native blend output");
                    staged[(offset + i) * 4 + c] =
                        c == 3 && destination->opaque ? 255 : byte_value(value);
                }
            }
        }
        // Commit after every inference succeeds; callers can retry safely.
        for (size_t index = 0; index < region_count; ++index) {
            const auto &r = regions[index];
            for (size_t row = 0; row < r.height; ++row)
                std::memcpy(destination->data + (r.destination_y + row) * destination->stride +
                            size_t(r.destination_x) * 4,
                            staged.data() + (offsets[index] + row * r.width) * 4, size_t(r.width) * 4);
        }
        if (stats) {
            stats->pixels = count;
            stats->dispatches = dispatches;
            stats->elapsed_ms = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
            copy_text(program->actual, stats->execution_devices, sizeof(stats->execution_devices));
        }
        copy_text("", error, error_size);
        return 0;
    } catch (const std::exception &e) {
        copy_text(e.what(), error, error_size);
        return -1;
    } catch (...) {
        copy_text("unknown native blend failure", error, error_size);
        return -1;
    }
}

extern "C" NR_API int nr_blend(nr_context *context, const nr_surface *source,
                                nr_surface *destination, const nr_region *region,
                                float opacity, nr_frame_stats *stats,
                                char *error, size_t error_size) {
    return nr_compose_regions(context, source, destination, region, 1, NR_SOURCE_OVER,
                              opacity, stats, error, error_size);
}

extern "C" NR_API int nr_blend_regions(nr_context *context, const nr_surface *source,
                                        nr_surface *destination, const nr_region *regions,
                                        size_t region_count, float opacity, nr_frame_stats *stats,
                                        char *error, size_t error_size) {
    return nr_compose_regions(context, source, destination, regions, region_count,
                              NR_SOURCE_OVER, opacity, stats, error, error_size);
}
