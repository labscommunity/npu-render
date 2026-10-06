#pragma once
#include <openvino/openvino.hpp>
#include <openvino/opsets/opset13.hpp>

// Byte-unit arithmetic avoids a compiler-sensitive normalize/rescale pair.
inline std::shared_ptr<ov::Model> blend_model(size_t width, size_t height, bool packed_u8) {
    namespace op = ov::opset13;
    const ov::Shape shape = packed_u8 ? ov::Shape{1, height, width, 4} : ov::Shape{1, 4, height, width};
    const auto element_type = packed_u8 ? ov::element::u8 : ov::element::f16;
    auto bg = std::make_shared<op::Parameter>(element_type, shape);
    auto fg = std::make_shared<op::Parameter>(element_type, shape);
    auto planar = [&](const std::shared_ptr<op::Parameter> &parameter) -> std::shared_ptr<ov::Node> {
        if (!packed_u8) return parameter;
        auto converted = std::make_shared<op::Convert>(parameter, ov::element::f16);
        return std::make_shared<op::Transpose>(converted,
            op::Constant::create(ov::element::i64, {4}, {0, 3, 1, 2}));
    };
    auto bg_value = planar(bg), fg_value = planar(fg);
    std::shared_ptr<ov::Node> alpha = std::make_shared<op::Gather>(fg_value,
        op::Constant::create(ov::element::i64, {1}, {3}), op::Constant::create(ov::element::i64, {}, {1}));
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
    }
    return std::make_shared<ov::Model>(ov::OutputVector{result}, ov::ParameterVector{bg, fg});
}
