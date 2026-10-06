#pragma once
#include <openvino/openvino.hpp>
#include <openvino/opsets/opset13.hpp>

inline size_t blend_pixel_group(size_t width, bool packed_u8, size_t requested = 4) {
    if (requested != 1 && requested != 4 && requested != 16 && requested != 32)
        throw std::invalid_argument("pixel group must be 1, 4, 16 or 32");
    return packed_u8 && width % requested == 0 ? requested : 1;
}

// Byte-unit arithmetic avoids a compiler-sensitive normalize/rescale pair.
inline std::shared_ptr<ov::Model> blend_model(size_t width, size_t height, bool packed_u8,
                                            size_t requested_group = 4) {
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
    std::vector<int64_t> alpha_indices(group == 1 ? 1 : group*4, 3);
    if (group > 1) for (size_t c = 0; c < group*4; ++c) alpha_indices[c] = int64_t((c/4)*4+3);
    std::shared_ptr<ov::Node> alpha = std::make_shared<op::Gather>(fg_value,
        op::Constant::create(ov::element::i64, {alpha_indices.size()}, alpha_indices),
        op::Constant::create(ov::element::i64, {}, {1}));
    if (packed_u8) alpha = std::make_shared<op::Multiply>(alpha,
        op::Constant::create(ov::element::f16, {}, {1.f / 255}));
    std::shared_ptr<ov::Node> result = std::make_shared<op::Add>(fg_value,
        std::make_shared<op::Multiply>(bg_value,
            std::make_shared<op::Subtract>(op::Constant::create(ov::element::f16, {}, {1.f}), alpha)));
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
