#include "provider_compile.hpp"
#include "solid_rectangle.hpp"
#include <openvino/openvino.hpp>
#include <openvino/opsets/opset13.hpp>
#include <openvino/pass/serialize.hpp>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
namespace nr::raster {
namespace {
namespace op = ov::opset13;
using Node = std::shared_ptr<ov::Node>;
void require(bool value) { if (!value) throw std::invalid_argument("unsupported solid rectangle request"); }
Node integer(int32_t x) { return op::Constant::create(ov::element::i32, ov::Shape{}, {x}); }
Node number(float x) { return op::Constant::create(ov::element::f32, ov::Shape{}, {x}); }
Node add(const Node &a, const Node &b) { return std::make_shared<op::Add>(a, b); }
Node sub(const Node &a, const Node &b) { return std::make_shared<op::Subtract>(a, b); }
Node mul(const Node &a, const Node &b) { return std::make_shared<op::Multiply>(a, b); }
Node both(const Node &a, const Node &b) { return std::make_shared<op::BitwiseAnd>(a, b); }
Node pred(const Node &a) { return std::make_shared<op::Convert>(a, ov::element::i32); }
Node lane(const Node &a, int64_t i, int32_t axis) {
    return std::make_shared<op::Gather>(a, op::Constant::create(ov::element::i64, ov::Shape{1}, {i}), integer(axis));
}
Node axes(std::initializer_list<int64_t> values) {
    return op::Constant::create(ov::element::i64, ov::Shape{values.size()}, std::vector<int64_t>(values));
}
Node floating(const Node &a) { return std::make_shared<op::Convert>(a, ov::element::f32); }
Node truncate_byte(const Node &a) { return floating(both(pred(a), integer(255))); }
// Exact byte source-over without full-image i32 multiplication or division.
// Each nibble product and sum stays <= 512, exactly representable even if the
// provider lowers floating arithmetic to FP16. For product p=256H+L,
// round(p/255) = H + [H+L>=128] + [H+L>=383]; ties cannot occur.
Node byte_source_over(const Node &source, const Node &background) {
    auto b = floating(background), d = sub(number(255), lane(source, 3, 3));
    auto bl = floating(both(pred(background), integer(15))), bh = mul(sub(b, bl), number(1.f/16));
    auto dl = floating(both(pred(d), integer(15))), dh = mul(sub(d, dl), number(1.f/16));
    auto mid = add(mul(bh, dl), mul(bl, dh)), mh = truncate_byte(mul(mid, number(1.f/16)));
    auto low = add(mul(sub(mid, mul(mh, number(16))), number(16)), mul(bl, dl));
    auto carry = truncate_byte(mul(low, number(1.f/256)));
    auto lo = sub(low, mul(carry, number(256))), hi = add(add(mul(bh, dh), mh), carry), sum = add(lo, hi);
    return add(source, add(hi, add(floating(std::make_shared<op::GreaterEqual>(sum, number(128))),
        floating(std::make_shared<op::GreaterEqual>(sum, number(383))))));
}
}
struct SolidRectangleExecutor::State {
    uint32_t width, height;
    ov::Core core; ov::CompiledModel compiled; ov::InferRequest request;
    std::string execution_device; bool failed = false, blending;
    State(uint32_t w, uint32_t h, const std::string &backend, bool bgra, bool blend) : width(w), height(h), blending(blend) {
        require(width && width <= 8192 && !(width % 4) && height && height <= 8192 && uint64_t(width) * height <= 4 * 1024 * 1024);
        require(backend == "CPU" || backend == "GPU" || backend == "NPU");
        auto rect = std::make_shared<op::Parameter>(ov::element::i32, ov::Shape{4});
        auto clip = std::make_shared<op::Parameter>(ov::element::i32, ov::Shape{4});
        clip->output(0).get_tensor().set_names({"scissor"});
        auto color = std::make_shared<op::Parameter>(ov::element::f32, ov::Shape{1, 1, 1, 4});
        auto bg = std::make_shared<op::Parameter>(ov::element::u8, ov::Shape{1, height, width, 4});
        rect->output(0).get_tensor().set_names({"rect"}); color->output(0).get_tensor().set_names({"color"}); bg->output(0).get_tensor().set_names({"background"});
        std::vector<int32_t> xx(width), yy(height);
        for (uint32_t i = 0; i < width; ++i) xx[i] = int32_t(i) * 256 + 128;
        for (uint32_t i = 0; i < height; ++i) yy[i] = int32_t(i) * 256 + 128;
        auto x = op::Constant::create(ov::element::i32, ov::Shape{1, 1, width, 1}, xx);
        auto y = op::Constant::create(ov::element::i32, ov::Shape{1, height, 1, 1}, yy);
        auto horizontal = both(pred(std::make_shared<op::GreaterEqual>(x, lane(rect, 0, 0))), pred(std::make_shared<op::Less>(x, lane(rect, 2, 0))));
        auto vertical = both(pred(std::make_shared<op::GreaterEqual>(y, lane(rect, 1, 0))), pred(std::make_shared<op::Less>(y, lane(rect, 3, 0))));
        auto clip_x = both(pred(std::make_shared<op::GreaterEqual>(x, lane(clip, 0, 0))), pred(std::make_shared<op::Less>(x, lane(clip, 2, 0))));
        auto clip_y = both(pred(std::make_shared<op::GreaterEqual>(y, lane(clip, 1, 0))), pred(std::make_shared<op::Less>(y, lane(clip, 3, 0))));
        auto covered = both(both(horizontal, vertical), both(clip_x, clip_y));
        ov::OutputVector channels;
        for (unsigned c = 0; c < 4; ++c) channels.push_back(lane(color, bgra && c < 3 ? 2 - c : c, 3));
        auto value = mul(std::make_shared<op::Concat>(channels, 3), number(255));
        auto base = both(pred(value), integer(255));
        auto fraction = sub(value, std::make_shared<op::Convert>(base, ov::element::f32));
        auto inc = std::make_shared<op::BitwiseOr>(pred(std::make_shared<op::Greater>(fraction, number(.5f))),
            both(pred(std::make_shared<op::Equal>(fraction, number(.5f))), both(base, integer(1))));
        Node quantized = add(base, inc);
        Node q = std::make_shared<op::Convert>(quantized, ov::element::f16);
        auto grouped = [&](const Node &input) -> Node {
            return std::make_shared<op::Transpose>(std::make_shared<op::Reshape>(input, axes({1, height, width / 4, 16}), false), axes({0, 3, 1, 2}));
        };
        auto mask = grouped(std::make_shared<op::Broadcast>(std::make_shared<op::Convert>(covered, ov::element::f16), axes({1, height, width, 4})));
        auto inverse = sub(op::Constant::create(ov::element::f16, ov::Shape{}, {1.f}), mask);
        if (blend) q = grouped(std::make_shared<op::Convert>(byte_source_over(floating(quantized), bg), ov::element::f16));
        else q = std::make_shared<op::Transpose>(std::make_shared<op::Concat>(ov::OutputVector{q, q, q, q}, 3), axes({0, 3, 1, 2}));
        auto value_image = add(mul(mask, q), mul(inverse, grouped(std::make_shared<op::Convert>(bg, ov::element::f16))));
        auto output = std::make_shared<op::Convert>(std::make_shared<op::Reshape>(
            std::make_shared<op::Transpose>(value_image, axes({0, 2, 3, 1})), axes({1, height, width, 4}), false), ov::element::u8);
        auto model = std::make_shared<ov::Model>(ov::OutputVector{output, std::make_shared<op::Convert>(covered, ov::element::u8)}, ov::ParameterVector{rect, color, bg, clip});
        ov::AnyMap properties{ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY)};
        if (backend == "NPU") {
            for (const auto &node : model->get_ops()) node->get_rt_info()["precise_0"] = std::string{};
            std::ostringstream xml, weights; ov::pass::Serialize serializer(xml, weights); serializer.run_on_model(model);
            const auto bytes = weights.str(); ov::Tensor data(ov::element::u8, ov::Shape{bytes.size()});
            if (!bytes.empty()) std::memcpy(data.data(), bytes.data(), bytes.size());
            model = core.read_model(xml.str(), data);
            for (const auto &node : model->get_ops()) require(nr::provider::precision_preserved(node));
            properties.emplace(ov::hint::execution_mode(ov::hint::ExecutionMode::ACCURACY));
        }
        compiled = nr::provider::compile_model(core, model, backend, properties);
        const auto devices = compiled.get_property(ov::execution_devices);
        require(devices.size() == 1 && (devices[0] == backend || devices[0].rfind(backend + ".", 0) == 0));
        execution_device = devices[0]; request = compiled.create_infer_request();
    }
};
SolidRectangleExecutor::SolidRectangleExecutor(uint32_t width, uint32_t height, const std::string &backend, bool bgra, bool blend)
    : state(std::make_unique<State>(width, height, backend, bgra, blend)) {}
SolidRectangleExecutor::~SolidRectangleExecutor() = default;
bool SolidRectangleExecutor::supports_blend_color(const Color &color) {
    for (unsigned i = 0; i < 4; ++i) {
        const float c = color[i];
        if (!std::isfinite(c) || c < 0 || c > 1 || c != std::round(c * 255) / 255 || (i < 3 && c > color[3])) return false;
    }
    return true;
}
SolidRectangleExecutor::Result SolidRectangleExecutor::execute(const std::array<int32_t, 4> &bounds, const Color &color,
    const std::vector<PackedColor> &background, const std::optional<std::array<int32_t, 4>> &scissor) {
    auto &s = *state; require(!s.failed && background.size() == size_t(s.width) * s.height && bounds[0] <= bounds[2] && bounds[1] <= bounds[3]);
    for (const auto c : bounds) require(c >= -coordinate_limit && c <= coordinate_limit);
    for (const auto c : color) require(std::isfinite(c) && c >= 0 && c <= 1);
    require(!s.blending || supports_blend_color(color));
    auto clip = scissor.value_or(std::array<int32_t,4>{0,0,int32_t(s.width),int32_t(s.height)});
    require(clip[0] >= 0 && clip[1] >= 0 && clip[0] <= clip[2] && clip[1] <= clip[3] && clip[2] <= int32_t(s.width) && clip[3] <= int32_t(s.height));
    for (auto &v : clip) v *= 256;
    s.failed = true;
    std::memcpy(s.request.get_tensor("scissor").data<int32_t>(), clip.data(), sizeof(clip));
    std::memcpy(s.request.get_tensor("rect").data<int32_t>(), bounds.data(), sizeof(bounds));
    std::memcpy(s.request.get_tensor("color").data<float>(), color.data(), sizeof(color));
    std::memcpy(s.request.get_tensor("background").data<uint8_t>(), background.data(), background.size() * sizeof(PackedColor));
    s.request.infer(); const auto output = s.request.get_output_tensor(0), count = s.request.get_output_tensor(1);
    require(output.get_element_type() == ov::element::u8 && output.get_byte_size() == background.size() * sizeof(PackedColor) &&
        count.get_element_type() == ov::element::u8 && count.get_size() == background.size());
    Result result{{}, 0, s.execution_device};
    for (size_t i = 0; i < count.get_size(); ++i) { const auto value = count.data<const uint8_t>()[i]; require(value <= 1); result.covered_samples += value; } result.pixels.resize(background.size());
    std::memcpy(result.pixels.data(), output.data<const uint8_t>(), output.get_byte_size()); s.failed = false; return result;
}
}
