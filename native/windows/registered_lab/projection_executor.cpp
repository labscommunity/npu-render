#include "projection_executor.hpp"
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
void require(bool value) { if (!value) throw std::invalid_argument("unsupported projection request"); }
Node integer(int32_t value) { return op::Constant::create(ov::element::i32, ov::Shape{}, {value}); }
Node number(float value) { return op::Constant::create(ov::element::f32, ov::Shape{}, {value}); }
Node lane(const Node &v, int64_t i) {
    return std::make_shared<op::Gather>(v, op::Constant::create(ov::element::i64, ov::Shape{1}, {i}), integer(1));
}
Node add(const Node &a, const Node &b) { return std::make_shared<op::Add>(a, b); }
Node sub(const Node &a, const Node &b) { return std::make_shared<op::Subtract>(a, b); }
Node mul(const Node &a, const Node &b) { return std::make_shared<op::Multiply>(a, b); }
Node div(const Node &a, const Node &b) { return std::make_shared<op::Divide>(a, b); }
Node both(const Node &a, const Node &b) { return std::make_shared<op::BitwiseAnd>(a, b); }
Node pred(const Node &a) { return std::make_shared<op::Convert>(a, ov::element::i32); }
Node snap(const Node &screen) {
    // Native Round corrupts values on the tested NPU. A masked truncation
    // preserves the integer conversion, then explicit tie-to-even uses i32 masks.
    auto scaled = mul(screen, number(256));
    auto magnitude = std::make_shared<op::Abs>(scaled);
    auto base = both(pred(magnitude), integer(0x7fffffff));
    auto fraction = sub(magnitude, std::make_shared<op::Convert>(base, ov::element::f32));
    auto greater = pred(std::make_shared<op::Greater>(fraction, number(.5f)));
    auto equal = pred(std::make_shared<op::Equal>(fraction, number(.5f)));
    auto increment = std::make_shared<op::BitwiseOr>(greater, both(equal, both(base, integer(1))));
    auto sign = sub(integer(1), mul(pred(std::make_shared<op::Less>(scaled, number(0))), integer(2)));
    return mul(add(base, increment), sign);
}
}
struct ProjectionExecutor::State {
    uint32_t count;
    ov::Core core;
    ov::CompiledModel compiled;
    ov::InferRequest request;
    std::string execution_device;
    bool failed = false;
    State(uint32_t size, const std::string &backend) : count(size) {
        require(count && count <= 65536 && (backend == "NPU" || backend == "CPU" || backend == "GPU"));
        auto clip = std::make_shared<op::Parameter>(ov::element::f32, ov::Shape{count, 4});
        auto viewport = std::make_shared<op::Parameter>(ov::element::f32, ov::Shape{1, 6});
        clip->output(0).get_tensor().set_names({"clip"}); viewport->output(0).get_tensor().set_names({"viewport"});
        auto w = lane(clip, 3);
        auto x = add(mul(add(div(lane(clip, 0), w), number(1)), mul(lane(viewport, 2), number(.5f))), lane(viewport, 0));
        auto y = add(mul(sub(number(1), div(lane(clip, 1), w)), mul(lane(viewport, 3), number(.5f))), lane(viewport, 1));
        auto depth = add(mul(div(lane(clip, 2), w), sub(lane(viewport, 5), lane(viewport, 4))), lane(viewport, 4));
        auto floating = std::make_shared<op::Concat>(ov::OutputVector{x, y, depth, div(number(1), w)}, 1);
        auto snapped = snap(std::make_shared<op::Concat>(ov::OutputVector{x, y}, 1));
        auto model = std::make_shared<ov::Model>(ov::OutputVector{floating, snapped}, ov::ParameterVector{clip, viewport});
        ov::AnyMap properties{ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY)};
        if (backend == "NPU") {
            for (const auto &node : model->get_ops()) node->get_rt_info()["precise_0"] = std::string{};
            std::ostringstream xml, weights; ov::pass::Serialize serializer(xml, weights); serializer.run_on_model(model);
            const auto bytes = weights.str(); ov::Tensor data(ov::element::u8, ov::Shape{bytes.size()});
            if (!bytes.empty()) std::memcpy(data.data(), bytes.data(), bytes.size());
            model = core.read_model(xml.str(), data);
            for (const auto &node : model->get_ops())
                if (!node->get_rt_info().count("precise_0")) throw std::runtime_error("projection precision marker lost");
            properties.emplace(ov::hint::execution_mode(ov::hint::ExecutionMode::ACCURACY));
        }
        compiled = core.compile_model(model, backend, properties);
        const auto devices = compiled.get_property(ov::execution_devices);
        require(devices.size() == 1 && (devices[0] == backend || devices[0].rfind(backend + ".", 0) == 0));
        execution_device = devices[0]; request = compiled.create_infer_request();
    }
};
ProjectionExecutor::ProjectionExecutor(uint32_t vertices, const std::string &backend) : state(std::make_unique<State>(vertices, backend)) {}
ProjectionExecutor::~ProjectionExecutor() = default;
ProjectionExecutor::Result ProjectionExecutor::execute(const std::vector<ClipPosition> &vertices, const Viewport &viewport) {
    auto &s = *state; require(!s.failed && vertices.size() == s.count);
    const std::array<float, 6> v{viewport.x, viewport.y, viewport.width, viewport.height, viewport.min_depth, viewport.max_depth};
    for (const auto value : v) require(std::isfinite(value));
    require(std::abs(v[0]) <= 8192 && std::abs(v[1]) <= 8192 && v[2] > 0 && v[2] <= 8192 && v[3] > 0 && v[3] <= 8192 &&
            v[4] >= 0 && v[4] <= v[5] && v[5] <= 1);
    for (const auto &p : vertices) {
        for (const auto value : p) require(std::isfinite(value));
        require(p[3] >= 1.0f / 65536 && p[3] <= 65536 && p[2] >= 0 && p[2] <= p[3]);
        // Conservative admission bound, not a replacement for NPU projection.
        // Keep enough margin for FP32 rounding before conversion to signed i32.
        const double bound_x = (std::abs(double(p[0]) / p[3]) + 1) * v[2] * .5 + std::abs(v[0]);
        const double bound_y = (std::abs(double(p[1]) / p[3]) + 1) * v[3] * .5 + std::abs(v[1]);
        require(bound_x <= 16383 && bound_y <= 16383);
    }
    s.failed = true;
    auto clip = s.request.get_tensor("clip"), vp = s.request.get_tensor("viewport");
    std::memcpy(clip.data<float>(), vertices.data(), vertices.size() * sizeof(ClipPosition));
    std::memcpy(vp.data<float>(), v.data(), sizeof(v));
    s.request.infer();
    const auto floating = s.request.get_output_tensor(0), snapped = s.request.get_output_tensor(1);
    require(floating.get_element_type() == ov::element::f32 && floating.get_size() == size_t(s.count) * 4 &&
            snapped.get_element_type() == ov::element::i32 && snapped.get_size() == size_t(s.count) * 2);
    Result result{{}, s.execution_device}; result.vertices.resize(s.count);
    for (size_t i = 0; i < s.count; ++i) {
        const auto f = floating.data<const float>() + i * 4;
        const auto q = snapped.data<const int32_t>() + i * 2;
        for (unsigned c = 0; c < 4; ++c) require(std::isfinite(f[c]));
        for (unsigned c = 0; c < 2; ++c) require(q[c] >= -coordinate_limit && q[c] <= coordinate_limit);
        result.vertices[i] = {{q[0], q[1]}, {f[0], f[1]}, f[2], f[3]};
    }
    s.failed = false; return result;
}
}
