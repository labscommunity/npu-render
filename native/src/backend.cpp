#include "npu_render.h"

#include <openvino/openvino.hpp>
#include <openvino/opsets/opset13.hpp>
#include <openvino/runtime/properties.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
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

struct SamplingProgram {
    size_t source_pixels, capacity;
    std::string actual;
    ov::InferRequest request;
    ov::Tensor image, indices, weights, background, output;
    std::mutex execution;
};

using ResizeKey = std::array<uint32_t, 4>;
struct ByteTables {
    std::array<ov::float16, 256> encode;
    std::array<uint8_t, 65536> decode;
    ByteTables() {
        for (size_t i = 0; i < encode.size(); ++i) encode[i] = ov::float16(float(i));
        for (size_t i = 0; i < decode.size(); ++i) {
            const float value = float(ov::float16::from_bits(uint16_t(i)));
            decode[i] = std::isfinite(value) ? uint8_t(std::lround(std::clamp(value,0.f,255.f))) : 0;
        }
    }
};
const ByteTables &byte_tables() { static const ByteTables tables; return tables; }

uint16_t half_bits(const ov::float16 &value) {
    static_assert(sizeof(value) == sizeof(uint16_t), "FP16 storage changed");
    uint16_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}
struct ResizeProgram {
    size_t source_pixels, output_pixels;
    std::string actual;
    ov::InferRequest request;
    ov::Tensor image, background, opacity, output;
    std::mutex execution;
};

struct Program {
    std::string target;
    std::string actual;
    size_t capacity;
    ov::InferRequest request;
    ov::Tensor background;
    ov::Tensor foreground;
    std::mutex execution;
    std::mutex sampling_mutex;
    std::map<std::pair<uint32_t, uint32_t>, std::shared_ptr<SamplingProgram>> sampling;
    std::map<ResizeKey, std::shared_ptr<ResizeProgram>> resizing;
};

// Logical tensor budget excludes compiler/driver workspace. Include all persistent
// graph inputs and outputs, counted in equivalent RGBA FP16 pixels.
void check_resource_budget(const Program &program, size_t additional) {
    size_t total = additional;
    for (const auto &entry : program.sampling)
        total += entry.second->source_pixels + 5 * entry.second->capacity;
    for (const auto &entry : program.resizing)
        total += entry.second->source_pixels + 2 * entry.second->output_pixels + 1;
    if (program.sampling.size() + program.resizing.size() >= 16 || total > 64u * 1024 * 1024)
        throw std::runtime_error("prepared graphics resource limit reached");
}

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

std::shared_ptr<SamplingProgram> prepare_sampling(ov::Core &core, uint32_t width, uint32_t height,
                                                 size_t capacity, const std::string &target) {
    namespace op = ov::opset13;
    (void)byte_tables();
    // Driver 32.0.100.4724 fails segmented Gather tiling for larger output batches
    // with common texture extents. Retile on the same device, without changing
    // the image, operation, or hidden CPU/GPU routing.
    if (target == "NPU") capacity = std::min(capacity, size_t(128)*128);
    auto program = std::make_shared<SamplingProgram>();
    program->source_pixels = size_t(width) * height;
    program->capacity = capacity;
    const size_t source_pixels = program->source_pixels;
    const size_t side = size_t(std::sqrt(double(capacity)));
    if (side*side != capacity) throw std::logic_error("sampling capacity must be square");
    auto image = std::make_shared<op::Parameter>(ov::element::f16, ov::Shape{4, source_pixels});
    auto indices = std::make_shared<op::Parameter>(ov::element::i32, ov::Shape{4, side, side});
    auto weights = std::make_shared<op::Parameter>(ov::element::f16, ov::Shape{1, 4, side, side});
    auto background = std::make_shared<op::Parameter>(ov::element::f16, ov::Shape{4, side, side});
    auto fetched = std::make_shared<op::Gather>(image, indices,
        op::Constant::create(ov::element::i64, {}, {1}));
    auto sampled = std::make_shared<op::ReduceSum>(std::make_shared<op::Multiply>(fetched, weights),
        op::Constant::create(ov::element::i64, {1}, {1}), false);
    auto alpha = std::make_shared<op::Gather>(sampled,
        op::Constant::create(ov::element::i64, {1}, {3}), op::Constant::create(ov::element::i64, {}, {0}));
    auto inverse = std::make_shared<op::Subtract>(op::Constant::create(ov::element::f16, {}, {1.f}),
        std::make_shared<op::Multiply>(alpha, op::Constant::create(ov::element::f16, {}, {1.f / 255})));
    auto result = std::make_shared<op::Add>(sampled, std::make_shared<op::Multiply>(background, inverse));
    auto model = std::make_shared<ov::Model>(ov::OutputVector{result},
        ov::ParameterVector{image, indices, weights, background});
    ov::AnyMap properties;
    if (target != "NPU") properties[ov::hint::inference_precision.name()] = ov::element::f16;
    auto compiled = core.compile_model(model, target, properties);
    auto actual = compiled.get_property(ov::execution_devices);
    if (actual.empty() || std::any_of(actual.begin(), actual.end(), [&](const auto &device) {
        return device != target && device.rfind(target + ".", 0) != 0;
    })) throw std::runtime_error("unexpected sampling execution device");
    for (const auto &device : actual) {
        if (!program->actual.empty()) program->actual += ",";
        program->actual += device;
    }
    program->request = compiled.create_infer_request();
    program->image = ov::Tensor(ov::element::f16, ov::Shape{4, source_pixels});
    program->indices = ov::Tensor(ov::element::i32, ov::Shape{4, side, side});
    program->weights = ov::Tensor(ov::element::f16, ov::Shape{1, 4, side, side});
    program->background = ov::Tensor(ov::element::f16, ov::Shape{4, side, side});
    program->output = ov::Tensor(ov::element::f16, ov::Shape{4, side, side});
    program->request.set_input_tensor(0, program->image);
    program->request.set_input_tensor(1, program->indices);
    program->request.set_input_tensor(2, program->weights);
    program->request.set_input_tensor(3, program->background);
    program->request.set_output_tensor(program->output);
    auto *texels = program->image.data<ov::float16>();
    auto *address = program->indices.data<int32_t>();
    auto *weight = program->weights.data<ov::float16>();
    auto *bg = program->background.data<ov::float16>();
    for (size_t i = 0; i < source_pixels; ++i) {
        const unsigned alpha_value = unsigned(i % 256);
        for (size_t c = 0; c < 4; ++c)
            texels[c * source_pixels + i] = ov::float16(float(c == 3 ? alpha_value :
                (i * (c + 3) + c * 17) % (alpha_value + 1)));
    }
    // Two changing address/weight fixtures catch stale binding and end-of-image
    // addressing, and cover unit, fractional and zero-total interpolation.
    for (size_t frame = 0; frame < 2; ++frame) {
        for (size_t i = 0; i < capacity; ++i) {
            for (size_t k = 0; k < 4; ++k) {
                address[k * capacity + i] = int32_t((i * 31 + k * (source_pixels - 1) + frame * 19) % source_pixels);
                float value = i % 3 == 0 ? (k == 0 ? 1.f : 0.f) : i % 3 == 1 ? 0.25f : 0.f;
                weight[k * capacity + i] = ov::float16(value);
                bg[k * capacity + i] = ov::float16(float(k == 3 ? 255 : (i * 13 + k * 31) % 256));
            }
        }
        program->request.infer();
        const auto *out = program->request.get_output_tensor().data<const ov::float16>();
        for (size_t i = 0; i < capacity; ++i) {
            double sample[4]{};
            for (size_t c = 0; c < 4; ++c) for (size_t k = 0; k < 4; ++k)
                sample[c] += float(texels[c * source_pixels + address[k * capacity + i]]) *
                             double(float(weight[k * capacity + i]));
            for (size_t c = 0; c < 4; ++c) {
                const double expected = sample[c] + float(bg[c * capacity + i]) * (1 - sample[3] / 255);
                const double observed = float(out[c * capacity + i]);
                if (!std::isfinite(observed) || std::abs(std::lround(observed) - std::lround(expected)) > 2)
                    throw std::runtime_error("indexed texture sampling qualification failed");
            }
        }
    }
    return program;
}

std::shared_ptr<ResizeProgram> prepare_resize(ov::Core &core, const ResizeKey &extent,
                                             const std::string &target) {
    namespace op = ov::opset13;
    (void)byte_tables(); // Initialize conversion tables on the control thread.
    const auto sw = extent[0], sh = extent[1], rw = extent[2], rh = extent[3];
    auto program = std::make_shared<ResizeProgram>();
    program->source_pixels = size_t(sw) * sh;
    program->output_pixels = size_t(rw) * rh;
    auto image = std::make_shared<op::Parameter>(ov::element::f16, ov::Shape{1, 4, sh, sw});
    auto background = std::make_shared<op::Parameter>(ov::element::f16, ov::Shape{1, 4, rh, rw});
    auto opacity = std::make_shared<op::Parameter>(ov::element::f16, ov::Shape{1});
    op::Interpolate::InterpolateAttrs attributes;
    attributes.mode = op::Interpolate::InterpolateMode::LINEAR_ONNX;
    attributes.shape_calculation_mode = op::Interpolate::ShapeCalcMode::SIZES;
    attributes.coordinate_transformation_mode = op::Interpolate::CoordinateTransformMode::HALF_PIXEL;
    std::shared_ptr<ov::Node> resized;
    if (sw == 2*rw && sh == 2*rh) {
        // Half-pixel coordinates are (2*x+.5, 2*y+.5): exactly four
        // neighboring texels with weight 1/4, with no border extrapolation.
        // This is not a new box-filter contract for arbitrary reduction ratios.
        resized = std::make_shared<op::AvgPool>(image, ov::Strides{2,2},
            ov::Shape{0,0}, ov::Shape{0,0}, ov::Shape{2,2}, true);
    } else {
        resized = std::make_shared<op::Interpolate>(image,
            op::Constant::create(ov::element::i64, {2}, {rh, rw}),
            op::Constant::create(ov::element::i64, {2}, {2, 3}), attributes);
    }
    auto foreground = std::make_shared<op::Multiply>(resized, opacity);
    auto alpha = std::make_shared<op::Gather>(foreground,
        op::Constant::create(ov::element::i64, {1}, {3}), op::Constant::create(ov::element::i64, {}, {1}));
    auto inverse = std::make_shared<op::Subtract>(op::Constant::create(ov::element::f16, {}, {1.f}),
        std::make_shared<op::Multiply>(alpha, op::Constant::create(ov::element::f16, {}, {1.f / 255})));
    auto result = std::make_shared<op::Add>(foreground, std::make_shared<op::Multiply>(background, inverse));
    auto model = std::make_shared<ov::Model>(ov::OutputVector{result},
        ov::ParameterVector{image, background, opacity});
    ov::AnyMap properties;
    if (target != "NPU") properties[ov::hint::inference_precision.name()] = ov::element::f16;
    auto compiled = core.compile_model(model, target, properties);
    const auto actual = compiled.get_property(ov::execution_devices);
    if (actual.empty() || std::any_of(actual.begin(), actual.end(), [&](const auto &device) {
        return device != target && device.rfind(target + ".", 0) != 0;
    })) throw std::runtime_error("unexpected resize execution device");
    for (const auto &device : actual) {
        if (!program->actual.empty()) program->actual += ",";
        program->actual += device;
    }
    program->request = compiled.create_infer_request();
    // This API consumes ordinary CPU memory. Use owned host tensors so the CPU
    // packing/oracle loops never scatter across a driver's mapped device allocation.
    // The runtime performs any required upload/download; this is not a zero-copy path.
    program->image = ov::Tensor(ov::element::f16, ov::Shape{1, 4, sh, sw});
    program->background = ov::Tensor(ov::element::f16, ov::Shape{1, 4, rh, rw});
    program->opacity = ov::Tensor(ov::element::f16, ov::Shape{1});
    program->output = ov::Tensor(ov::element::f16, ov::Shape{1, 4, rh, rw});
    program->request.set_input_tensor(0, program->image);
    program->request.set_input_tensor(1, program->background);
    program->request.set_input_tensor(2, program->opacity);
    program->request.set_output_tensor(program->output);
    auto *texels = program->image.data<ov::float16>();
    auto *bg = program->background.data<ov::float16>();
    const size_t sn = program->source_pixels, dn = program->output_pixels;
    // Qualify this exact shape against double precision, including border clamp,
    // changing colors/alpha, and opacity endpoints. No graph becomes active first.
    const float opacity_cases[] = {1.f, .5f, 0.f};
    for (size_t frame = 0; frame < 3; ++frame) {
        for (size_t i = 0; i < sn; ++i) {
            const unsigned a = unsigned((i * 7 + frame * 31) % 256);
            for (size_t c = 0; c < 4; ++c)
                texels[c*sn+i] = ov::float16(float(c == 3 ? a : (i*(c+3)+frame*19)%(a+1)));
        }
        for (size_t i = 0; i < dn; ++i) for (size_t c = 0; c < 4; ++c)
            bg[c*dn+i] = ov::float16(float(c == 3 ? 255 : (i*13+c*31)%256));
        program->opacity.data<ov::float16>()[0] = ov::float16(opacity_cases[frame]);
        program->request.infer();
        const auto *out = program->request.get_output_tensor().data<const ov::float16>();
        for (uint32_t y = 0; y < rh; ++y) for (uint32_t x = 0; x < rw; ++x) {
            const double sx = (x+.5)*sw/rw-.5, sy = (y+.5)*sh/rh-.5;
            const int ix = int(std::floor(sx)), iy = int(std::floor(sy));
            const double fx = sx-ix, fy = sy-iy;
            double sample[4]{};
            for (int k = 0; k < 4; ++k) {
                const size_t tx = std::clamp(ix+(k&1), 0, int(sw)-1);
                const size_t ty = std::clamp(iy+(k>>1), 0, int(sh)-1);
                const double weight = ((k&1)?fx:1-fx)*((k>>1)?fy:1-fy)*opacity_cases[frame];
                for (size_t c = 0; c < 4; ++c) sample[c] += float(texels[c*sn+ty*sw+tx])*weight;
            }
            for (size_t c = 0; c < 4; ++c) {
                const size_t index = c*dn+size_t(y)*rw+x;
                const double expected = sample[c]+float(bg[index])*(1-sample[3]/255);
                const double observed = float(out[index]);
                if (!std::isfinite(observed) || std::abs(std::lround(observed)-std::lround(expected)) > 2)
                    throw std::runtime_error("bilinear resize qualification failed");
            }
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
        std::vector<std::pair<uint32_t, uint32_t>> extents;
        std::vector<ResizeKey> resize_extents;
        {
            std::lock_guard<std::mutex> lock(context->active->sampling_mutex);
            for (const auto &entry : context->active->sampling) extents.push_back(entry.first);
            for (const auto &entry : context->active->resizing) resize_extents.push_back(entry.first);
        }
        for (const auto &extent : extents) {
            const size_t capacity = target == "NPU" ? std::min(candidate->capacity, size_t(128)*128) : candidate->capacity;
            check_resource_budget(*candidate, size_t(extent.first)*extent.second + 5*capacity);
            candidate->sampling[extent] = prepare_sampling(context->core, extent.first, extent.second,
                                                          candidate->capacity, target);
        }
        for (const auto &extent : resize_extents) {
            check_resource_budget(*candidate, size_t(extent[0])*extent[1] + 2*size_t(extent[2])*extent[3] + 1);
            candidate->resizing[extent] = prepare_resize(context->core, extent, target);
        }
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

extern "C" NR_API int nr_prepare_sampling(nr_context *context, uint32_t source_width,
                                            uint32_t source_height, char *error, size_t error_size) {
    try {
        if (!context || !source_width || !source_height || source_width > 4096 || source_height > 4096)
            throw std::invalid_argument("sampling source extent must be in 1..4096");
        std::lock_guard<std::mutex> changing(context->switch_mutex);
        std::shared_ptr<Program> program;
        {
            std::lock_guard<std::mutex> lock(context->active_mutex);
            program = context->active;
        }
        const auto key = std::make_pair(source_width, source_height);
        {
            std::lock_guard<std::mutex> lock(program->sampling_mutex);
            if (program->sampling.count(key)) { copy_text("", error, error_size); return 0; }
            const size_t capacity = program->target == "NPU" ? std::min(program->capacity, size_t(128)*128) : program->capacity;
            check_resource_budget(*program, size_t(source_width)*source_height + 5*capacity);
        }
        auto sampler = prepare_sampling(context->core, source_width, source_height, program->capacity, program->target);
        {
            std::lock_guard<std::mutex> lock(program->sampling_mutex);
            program->sampling.emplace(key, std::move(sampler));
        }
        copy_text("", error, error_size);
        return 0;
    } catch (const std::exception &e) { copy_text(e.what(), error, error_size); return -1; }
      catch (...) { copy_text("unknown sampling preparation failure", error, error_size); return -1; }
}

extern "C" NR_API int nr_prepare_resize(nr_context *context, uint32_t source_width,
                                        uint32_t source_height, uint32_t resized_width,
                                        uint32_t resized_height, char *error, size_t error_size) {
    try {
        const ResizeKey extent{source_width, source_height, resized_width, resized_height};
        if (!context || std::any_of(extent.begin(), extent.end(), [](uint32_t n) { return n < 1 || n > 4096; }))
            throw std::invalid_argument("resize extents must be in 1..4096");
        std::lock_guard<std::mutex> changing(context->switch_mutex);
        std::shared_ptr<Program> program;
        {
            std::lock_guard<std::mutex> lock(context->active_mutex);
            program = context->active;
        }
        {
            std::lock_guard<std::mutex> lock(program->sampling_mutex);
            if (program->resizing.count(extent)) { copy_text("", error, error_size); return 0; }
            check_resource_budget(*program, size_t(source_width)*source_height + 2*size_t(resized_width)*resized_height + 1);
        }
        auto resizer = prepare_resize(context->core, extent, program->target);
        {
            std::lock_guard<std::mutex> lock(program->sampling_mutex);
            program->resizing.emplace(extent, std::move(resizer));
        }
        copy_text("", error, error_size); return 0;
    } catch (const std::exception &e) { copy_text(e.what(), error, error_size); return -1; }
      catch (...) { copy_text("unknown resize preparation failure", error, error_size); return -1; }
}

extern "C" NR_API int nr_resize_regions(nr_context *context, const nr_surface *source,
                                        nr_surface *destination, uint32_t resized_width,
                                        uint32_t resized_height, int32_t origin_x, int32_t origin_y,
                                        const nr_damage_rect *regions, size_t region_count,
                                        nr_operator operation, float opacity, nr_frame_stats *stats,
                                        char *error, size_t error_size) {
    if (stats) std::memset(stats, 0, sizeof(*stats));
    try {
        const auto started = Clock::now();
        if (!context || !source || !destination || !regions || !region_count || region_count > 4096 ||
            !std::isfinite(opacity) || opacity < 0 || opacity > 1 ||
            (operation != NR_SOURCE && operation != NR_SOURCE_OVER))
            throw std::invalid_argument("invalid resize arguments");
        validate(*source); validate(*destination);
        std::shared_ptr<Program> program;
        {
            std::lock_guard<std::mutex> lock(context->active_mutex);
            program = context->active;
        }
        std::shared_ptr<ResizeProgram> resizer;
        {
            std::lock_guard<std::mutex> lock(program->sampling_mutex);
            auto found = program->resizing.find({source->width, source->height, resized_width, resized_height});
            if (found == program->resizing.end()) throw std::runtime_error("extent has no prepared resize graph");
            resizer = found->second;
        }
        std::vector<size_t> offsets{0};
        for (size_t i = 0; i < region_count; ++i) {
            const auto &r = regions[i];
            const int64_t local_x = int64_t(r.x)-origin_x, local_y = int64_t(r.y)-origin_y;
            if (!r.width || !r.height || r.x > destination->width || r.width > destination->width-r.x ||
                r.y > destination->height || r.height > destination->height-r.y ||
                local_x < 0 || local_y < 0 || local_x+r.width > resized_width || local_y+r.height > resized_height)
                throw std::invalid_argument("resize damage must fit destination and virtual image");
            const size_t count = size_t(r.width)*r.height;
            if (count > std::numeric_limits<size_t>::max()/4-offsets.back())
                throw std::invalid_argument("resize damage overflows storage");
            offsets.push_back(offsets.back()+count);
            for (size_t j = 0; j < i; ++j) {
                const auto &q = regions[j];
                if (r.x < q.x+q.width && q.x < r.x+r.width && r.y < q.y+q.height && q.y < r.y+r.height)
                    throw std::invalid_argument("resize damage must be disjoint");
            }
        }
        std::lock_guard<std::mutex> lock(resizer->execution);
        const auto pack_start = Clock::now();
        auto *image = resizer->image.data<ov::float16>();
        auto *bg = resizer->background.data<ov::float16>();
        const auto &tables = byte_tables();
        const size_t sn = resizer->source_pixels, dn = resizer->output_pixels;
        for (size_t y = 0; y < source->height; ++y) for (size_t x = 0; x < source->width; ++x)
            for (size_t c = 0; c < 4; ++c)
                image[c*sn+y*source->width+x] = tables.encode[c == 3 && source->opaque ? 255 :
                    source->data[y*source->stride+x*4+c]];
        std::fill_n(bg, dn*4, ov::float16(0.f));
        if (operation == NR_SOURCE_OVER) for (size_t i = 0; i < region_count; ++i) {
            const auto &r = regions[i];
            for (size_t y = r.y; y < size_t(r.y)+r.height; ++y)
                for (size_t x = r.x; x < size_t(r.x)+r.width; ++x) {
                    const auto *pixel = destination->data+y*destination->stride+x*4;
                    const size_t index = size_t(int64_t(y)-origin_y)*resized_width+size_t(int64_t(x)-origin_x);
                    for (size_t c = 0; c < 4; ++c)
                        bg[c*dn+index] = tables.encode[c == 3 && destination->opaque ? 255 : pixel[c]];
                }
        }
        resizer->opacity.data<ov::float16>()[0] = ov::float16(opacity);
        const auto infer_start = Clock::now();
        resizer->request.infer();
        const auto stage_start = Clock::now();
        const auto *out = resizer->request.get_output_tensor().data<const ov::float16>();
        std::vector<uint8_t> staged(offsets.back()*4);
        for (size_t i = 0; i < region_count; ++i) {
            const auto &r = regions[i];
            for (size_t y = 0; y < r.height; ++y) for (size_t x = 0; x < r.width; ++x) {
                const size_t index = size_t(int64_t(r.y)+y-origin_y)*resized_width+size_t(int64_t(r.x)+x-origin_x);
                for (size_t c = 0; c < 4; ++c) {
                    const uint16_t bits = half_bits(out[c*dn+index]);
                    if ((bits & 0x7c00) == 0x7c00) throw std::runtime_error("non-finite resize output");
                    staged[4*(offsets[i]+y*r.width+x)+c] = c == 3 && destination->opaque ? 255 :
                        tables.decode[bits];
                }
            }
        }
        // Aliased inputs are already packed; any failure above leaves all destination bytes intact.
        for (size_t i = 0; i < region_count; ++i) {
            const auto &r = regions[i];
            for (size_t y = 0; y < r.height; ++y)
                std::memcpy(destination->data+(r.y+y)*destination->stride+size_t(r.x)*4,
                            staged.data()+4*(offsets[i]+y*r.width), size_t(r.width)*4);
        }
        const auto finished = Clock::now();
        const char *trace = std::getenv("NPU_RENDER_TRACE_RESIZE");
        if (trace && std::strcmp(trace, "1") == 0) {
            const auto ms = [](auto a, auto b) { return std::chrono::duration<double,std::milli>(b-a).count(); };
            std::fprintf(stderr, "npu-render-resize-stages: {\"execution_devices\":\"%s\","
                "\"validate_wait_ms\":%.4f,\"pack_ms\":%.4f,\"infer_ms\":%.4f,\"stage_commit_ms\":%.4f}\n",
                resizer->actual.c_str(), ms(started,pack_start), ms(pack_start,infer_start),
                ms(infer_start,stage_start), ms(stage_start,finished));
        }
        if (stats) {
            stats->pixels = offsets.back(); stats->dispatches = 1;
            stats->elapsed_ms = std::chrono::duration<double, std::milli>(finished-started).count();
            copy_text(resizer->actual, stats->execution_devices, sizeof(stats->execution_devices));
        }
        copy_text("", error, error_size); return 0;
    } catch (const std::exception &e) { copy_text(e.what(), error, error_size); return -1; }
      catch (...) { copy_text("unknown resize failure", error, error_size); return -1; }
}

extern "C" NR_API int nr_sample_regions(nr_context *context, const nr_surface *source,
                                         nr_surface *destination, const nr_damage_rect *regions,
                                         size_t region_count, const nr_sample_point *points,
                                         size_t point_count, nr_operator operation, float opacity,
                                         nr_frame_stats *stats, char *error, size_t error_size) {
    if (stats) std::memset(stats, 0, sizeof(*stats));
    try {
        const auto started = Clock::now();
        if (!context || !source || !destination || !regions || !points || !region_count || region_count > 4096 ||
            !std::isfinite(opacity) || opacity < 0 || opacity > 1 ||
            (operation != NR_SOURCE && operation != NR_SOURCE_OVER))
            throw std::invalid_argument("invalid sampling arguments");
        validate(*source); validate(*destination);
        std::shared_ptr<Program> program;
        {
            std::lock_guard<std::mutex> lock(context->active_mutex);
            program = context->active;
        }
        std::shared_ptr<SamplingProgram> sampler;
        {
            std::lock_guard<std::mutex> lock(program->sampling_mutex);
            auto found = program->sampling.find({source->width, source->height});
            if (found == program->sampling.end()) throw std::runtime_error("source extent has no prepared sampling graph");
            sampler = found->second;
        }
        std::vector<size_t> offsets{0};
        for (size_t i = 0; i < region_count; ++i) {
            const auto &r = regions[i];
            if (!r.width || !r.height || r.x > destination->width || r.width > destination->width-r.x ||
                r.y > destination->height || r.height > destination->height-r.y ||
                size_t(r.width) > std::numeric_limits<size_t>::max()/r.height)
                throw std::invalid_argument("invalid sampling damage rectangle");
            const size_t count = size_t(r.width)*r.height;
            if (count > std::numeric_limits<size_t>::max()/4-offsets.back())
                throw std::invalid_argument("sampling damage overflows storage");
            offsets.push_back(offsets.back()+count);
            for (size_t j = 0; j < i; ++j) {
                const auto &q = regions[j];
                if (r.x < q.x+q.width && q.x < r.x+r.width && r.y < q.y+q.height && q.y < r.y+r.height)
                    throw std::invalid_argument("sampling damage must be disjoint");
            }
        }
        if (point_count != offsets.back()) throw std::invalid_argument("sample count does not match damage pixels");
        for (size_t i = 0; i < point_count; ++i) {
            double sum = 0;
            for (size_t k = 0; k < 4; ++k) {
                const auto &p = points[i];
                if (p.index[k] >= sampler->source_pixels || !std::isfinite(p.weight[k]) || p.weight[k] < 0 || p.weight[k] > 1)
                    throw std::invalid_argument("invalid sampling address or weight");
                sum += p.weight[k];
            }
            if (sum > 1.000001) throw std::invalid_argument("sampling weights exceed one");
        }
        std::lock_guard<std::mutex> lock(sampler->execution);
        const auto pack_start = Clock::now();
        const auto &tables = byte_tables();
        auto *image = sampler->image.data<ov::float16>();
        for (size_t y = 0; y < source->height; ++y) for (size_t x = 0; x < source->width; ++x)
            for (size_t c = 0; c < 4; ++c)
                image[c * sampler->source_pixels + y * source->width + x] = tables.encode[
                    c == 3 && source->opaque ? 255 : source->data[y * source->stride + x * 4 + c]];
        const auto batches_start = Clock::now();
        double infer_ms = 0, batch_pack_ms = 0, stage_ms = 0;
        const size_t capacity = sampler->capacity;
        auto *indices = sampler->indices.data<int32_t>();
        auto *weights = sampler->weights.data<ov::float16>();
        auto *bg = sampler->background.data<ov::float16>();
        std::vector<uint8_t> staged(point_count*4);
        size_t rectangle = 0;
        uint64_t dispatches = 0;
        for (size_t offset = 0; offset < point_count; offset += capacity) {
            const auto batch_start = Clock::now();
            std::fill_n(indices, capacity*4, int32_t(0));
            std::fill_n(weights, capacity*4, ov::float16(0.f));
            std::fill_n(bg, capacity*4, ov::float16(0.f));
            const size_t count = std::min(capacity, point_count-offset);
            for (size_t i = 0; i < count; ++i) {
                while (offset+i >= offsets[rectangle+1]) ++rectangle;
                const auto &r = regions[rectangle];
                const size_t within = offset+i-offsets[rectangle];
                const auto *pixel = destination->data+(r.y+within/r.width)*destination->stride+4*(r.x+within%r.width);
                for (size_t c = 0; c < 4; ++c) {
                    indices[c*capacity+i] = int32_t(points[offset+i].index[c]);
                    weights[c*capacity+i] = ov::float16(points[offset+i].weight[c]*opacity);
                    bg[c*capacity+i] = tables.encode[operation == NR_SOURCE ? 0 : c == 3 && destination->opaque ? 255 : pixel[c]];
                }
            }
            const auto before_infer = Clock::now();
            sampler->request.infer(); ++dispatches;
            const auto before_stage = Clock::now();
            batch_pack_ms += std::chrono::duration<double,std::milli>(before_infer-batch_start).count();
            infer_ms += std::chrono::duration<double,std::milli>(before_stage-before_infer).count();
            const auto *out = sampler->request.get_output_tensor().data<const ov::float16>();
            for (size_t i = 0; i < count; ++i) for (size_t c = 0; c < 4; ++c) {
                const uint16_t bits = half_bits(out[c*capacity+i]);
                if ((bits & 0x7c00) == 0x7c00) throw std::runtime_error("non-finite sampling output");
                staged[4*(offset+i)+c] = c == 3 && destination->opaque ? 255 :
                    tables.decode[bits];
            }
            stage_ms += std::chrono::duration<double,std::milli>(Clock::now()-before_stage).count();
        }
        for (size_t i = 0; i < region_count; ++i) {
            const auto &r = regions[i];
            for (size_t y = 0; y < r.height; ++y)
                std::memcpy(destination->data+(r.y+y)*destination->stride+size_t(r.x)*4,
                            staged.data()+4*(offsets[i]+y*r.width), size_t(r.width)*4);
        }
        const auto finished = Clock::now();
        const char *trace = std::getenv("NPU_RENDER_TRACE_SAMPLING");
        if (trace && std::strcmp(trace, "1") == 0) {
            const auto ms = [](auto a, auto b) { return std::chrono::duration<double,std::milli>(b-a).count(); };
            std::fprintf(stderr, "npu-render-sampling-stages: {\"execution_devices\":\"%s\","
                "\"validate_wait_ms\":%.4f,\"source_pack_ms\":%.4f,\"batch_pack_ms\":%.4f,"
                "\"infer_ms\":%.4f,\"stage_ms\":%.4f,\"total_ms\":%.4f}\n",
                sampler->actual.c_str(), ms(started,pack_start), ms(pack_start,batches_start),
                batch_pack_ms,infer_ms,stage_ms,ms(started,finished));
        }
        if (stats) {
            stats->pixels = point_count; stats->dispatches = dispatches;
            stats->elapsed_ms = std::chrono::duration<double, std::milli>(finished-started).count();
            copy_text(sampler->actual, stats->execution_devices, sizeof(stats->execution_devices));
        }
        copy_text("", error, error_size); return 0;
    } catch (const std::exception &e) { copy_text(e.what(), error, error_size); return -1; }
      catch (...) { copy_text("unknown sampling failure", error, error_size); return -1; }
}
