#pragma once
#include "blend_model.hpp"

// Restricted cooperating-window contract: foreground alpha is exactly 0 or 255.
// Transparent pixels preserve the background; opaque pixels replace it byte for
// byte. This is not a general source-over graph for translucent surfaces.
inline std::shared_ptr<ov::Model> opaque_model(size_t width, size_t height) {
    namespace op = ov::opset13;
    const size_t group = blend_pixel_group(width,true);
    const ov::Shape shape{1,height,width,4};
    auto bg = std::make_shared<op::Parameter>(ov::element::u8,shape);
    auto fg = std::make_shared<op::Parameter>(ov::element::u8,shape);
    auto planar = [&](const std::shared_ptr<op::Parameter> &input) {
        auto grouped = std::make_shared<op::Reshape>(input,
            op::Constant::create(ov::element::i64,{4},
                std::vector<int64_t>{1,int64_t(height),int64_t(width/group),int64_t(group*4)}),false);
        return std::make_shared<op::Transpose>(grouped,op::Constant::create(ov::element::i64,{4},{0,3,1,2}));
    };
    auto b = std::make_shared<op::Convert>(planar(bg),ov::element::f16);
    auto f = std::make_shared<op::Convert>(planar(fg),ov::element::f16);
    std::vector<int64_t> indices(group*4);
    for (size_t c = 0; c < indices.size(); ++c) indices[c] = int64_t((c/4)*4+3);
    auto alpha = std::make_shared<op::Gather>(f,op::Constant::create(ov::element::i64,{indices.size()},indices),
        op::Constant::create(ov::element::i64,{}, {1}));
    // The NPU compiler rejects Equal and Select on u8. Byte values convert
    // exactly to f16; Select performs no rounding or color arithmetic.
    auto mask = std::make_shared<op::Equal>(alpha,
        op::Constant::create(ov::element::f16,{}, {255.f}));
    auto selected = std::make_shared<op::Convert>(std::make_shared<op::Select>(mask,f,b),ov::element::u8);
    auto interleaved = std::make_shared<op::Transpose>(selected,op::Constant::create(ov::element::i64,{4},{0,2,3,1}));
    auto output = std::make_shared<op::Reshape>(interleaved,op::Constant::create(ov::element::i64,{4},
        std::vector<int64_t>{1,int64_t(height),int64_t(width),4}),false);
    return std::make_shared<ov::Model>(ov::OutputVector{output},ov::ParameterVector{bg,fg});
}
