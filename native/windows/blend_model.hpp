#pragma once
#include <openvino/openvino.hpp>
#include <openvino/opsets/opset13.hpp>

// Packed byte-color contracts require the CPU's FP32 arithmetic path. Default
// CPU precision can select BF16/AMX convolution on Xeon Windows hosts, where
// the qualified runtime has produced illegal-instruction exceptions.
inline ov::AnyMap blend_compile_properties(const std::string &backend) {
    ov::AnyMap properties{ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY)};
    if (backend == "CPU") properties.emplace(ov::hint::inference_precision(ov::element::f32));
    return properties;
}

inline size_t blend_pixel_group(size_t width, bool packed_u8, size_t requested = 4) {
    if (requested != 1 && requested != 4 && requested != 16 && requested != 32)
        throw std::invalid_argument("pixel group must be 1, 4, 16 or 32");
    return packed_u8 && width % requested == 0 ? requested : 1;
}

// Byte-unit arithmetic avoids a compiler-sensitive normalize/rescale pair.
inline std::shared_ptr<ov::Model> blend_model(size_t width, size_t height, bool packed_u8,
                                            size_t requested_group = 4,
                                            const std::string &alpha_mode = "gather") {
    namespace op = ov::opset13;
    const size_t group = blend_pixel_group(width, packed_u8, requested_group);
    const ov::Shape shape = packed_u8 ? ov::Shape{1, height, width, 4} : ov::Shape{1, 4, height, width};
    const auto element_type = packed_u8 ? ov::element::u8 : ov::element::f16;
    auto bg = std::make_shared<op::Parameter>(element_type, shape);
    auto fg = std::make_shared<op::Parameter>(element_type, shape);
    auto planar = [&](const std::shared_ptr<op::Parameter> &parameter) -> std::shared_ptr<ov::Node> {
        if (!packed_u8) return parameter;
        std::shared_ptr<ov::Node> converted = std::make_shared<op::Convert>(parameter, ov::element::f16);
        // Consecutive pixels become channel groups. External BGRA byte order
        // stays intact; a four-pixel group gives the NPU 16 arithmetic channels.
        if (group > 1) converted = std::make_shared<op::Reshape>(converted,
            op::Constant::create(ov::element::i64, {4},
                std::vector<int64_t>{1, int64_t(height), int64_t(width/group), int64_t(group*4)}), false);
        return std::make_shared<op::Transpose>(converted,
            op::Constant::create(ov::element::i64, {4}, {0, 3, 1, 2}));
    };
    auto bg_value = planar(bg), fg_value = planar(fg);
    std::shared_ptr<ov::Node> inverse;
    auto one = op::Constant::create(ov::element::f16, {}, {1.f});
    if (alpha_mode == "convolution") {
        // A 1x1 channel transform extracts, broadcasts and negates alpha in
        // one DPU convolution. It preserves the original rounding contract.
        const size_t channels = group*4;
        std::vector<float> weights(channels*channels, 0.f);
        for (size_t c = 0; c < channels; ++c)
            weights[c*channels + (c/4)*4+3] = packed_u8 ? -1.f/255 : -1.f;
        auto transform = std::make_shared<op::Convolution>(fg_value,
            op::Constant::create(ov::element::f16, {channels, channels, 1, 1}, weights),
            ov::Strides{1,1}, ov::CoordinateDiff{0,0}, ov::CoordinateDiff{0,0}, ov::Strides{1,1});
        inverse = std::make_shared<op::Add>(transform, one);
    } else if (alpha_mode == "gather") {
        std::vector<int64_t> alpha_indices(group == 1 ? 1 : group*4, 3);
        if (group > 1) for (size_t c = 0; c < group*4; ++c) alpha_indices[c] = int64_t((c/4)*4+3);
        std::shared_ptr<ov::Node> alpha = std::make_shared<op::Gather>(fg_value,
            op::Constant::create(ov::element::i64, {alpha_indices.size()}, alpha_indices),
            op::Constant::create(ov::element::i64, {}, {1}));
        if (packed_u8) alpha = std::make_shared<op::Multiply>(alpha,
            op::Constant::create(ov::element::f16, {}, {1.f / 255}));
        inverse = std::make_shared<op::Subtract>(one, alpha);
    } else throw std::invalid_argument("alpha mode must be gather or convolution");
    std::shared_ptr<ov::Node> result = std::make_shared<op::Add>(fg_value,
        std::make_shared<op::Multiply>(bg_value, inverse));
    if (packed_u8) {
        auto clamped = std::make_shared<op::Clamp>(result, 0.0, 255.0);
        auto rounded = std::make_shared<op::Round>(clamped, op::Round::RoundMode::HALF_TO_EVEN);
        auto converted = std::make_shared<op::Convert>(rounded, ov::element::u8);
        result = std::make_shared<op::Transpose>(converted,
            op::Constant::create(ov::element::i64, {4}, {0, 2, 3, 1}));
        if (group > 1) result = std::make_shared<op::Reshape>(result,
            op::Constant::create(ov::element::i64, {4},
                std::vector<int64_t>{1, int64_t(height), int64_t(width), 4}), false);
    }
    return std::make_shared<ov::Model>(ov::OutputVector{result}, ov::ParameterVector{bg, fg});
}
