#include "raster_executor.hpp"
#include <openvino/openvino.hpp>
#include <openvino/opsets/opset13.hpp>
#include <openvino/pass/serialize.hpp>
#include <cstring>
#include <sstream>
#include <stdexcept>
namespace nr::raster {
namespace {
namespace op = ov::opset13;
using Node = std::shared_ptr<ov::Node>;
void require(bool value) { if (!value) throw std::invalid_argument("unsupported coverage request"); }
Node integer(int32_t value) { return op::Constant::create(ov::element::i32, ov::Shape{}, {value}); }
Node lane(const Node &v, int64_t i) {
    return std::make_shared<op::Gather>(v, op::Constant::create(ov::element::i64, ov::Shape{1}, {i}), integer(1));
}
Node add(const Node &a, const Node &b) { return std::make_shared<op::Add>(a, b); }
Node sub(const Node &a, const Node &b) { return std::make_shared<op::Subtract>(a, b); }
Node mul(const Node &a, const Node &b) { return std::make_shared<op::Multiply>(a, b); }
Node both(const Node &a, const Node &b) { return std::make_shared<op::BitwiseAnd>(a, b); }
Node either(const Node &a, const Node &b) { return std::make_shared<op::BitwiseOr>(a, b); }
Node pred(const Node &a) { return std::make_shared<op::Convert>(a, ov::element::i32); }
Node positive(const Node &a) { return pred(std::make_shared<op::Greater>(a, integer(0))); }
Node negative(const Node &a) { return pred(std::make_shared<op::Less>(a, integer(0))); }
Node zero(const Node &a) { return pred(std::make_shared<op::Equal>(a, integer(0))); }
Node low(const Node &a) { return both(a, integer(1023)); }
Node high(const Node &a) {
    // Native i64 silently truncates; integer shifts fail NPU compilation. Mask
    // off 10 low bits first: the remaining signed i32 has <=22 significant bits,
    // so conversion to f32 and division by 1024 are exact, including negatives.
    auto integral = std::make_shared<op::Convert>(both(a, integer(-1024)), ov::element::f32);
    auto scale = op::Constant::create(ov::element::f32, ov::Shape{}, {1.0f / 1024.0f});
    return pred(mul(integral, scale));
}
std::array<Node, 3> product(const Node &a, const Node &b) {
    auto al = low(a), ah = high(a), bl = low(b), bh = high(b);
    return {mul(al, bl), add(mul(ah, bl), mul(al, bh)), mul(ah, bh)};
}
struct Sign { Node positive, negative, zero; };
Sign determinant(const Node &ax, const Node &by, const Node &ay, const Node &bx) {
    const auto p = product(ax, by), q = product(ay, bx);
    auto lo = sub(p[0], q[0]);
    auto mid = add(sub(p[1], q[1]), high(lo));
    auto hi = add(sub(p[2], q[2]), high(mid));
    lo = low(lo); mid = low(mid);
    // Integer masks avoid a separate NPU miscompile of compound boolean logic.
    return {either(positive(hi), both(zero(hi), either(positive(mid), positive(lo)))),
            negative(hi), both(zero(hi), both(zero(mid), zero(lo)))};
}
}
struct CoverageExecutor::State {
    uint32_t count;
    ov::Core core;
    ov::CompiledModel compiled;
    ov::InferRequest request;
    std::string execution_device;
    bool failed = false;
    State(uint32_t size, const std::string &backend) : count(size) {
        require(count && count <= 65536 && (backend == "NPU" || backend == "GPU" || backend == "CPU"));
        auto vertices = std::make_shared<op::Parameter>(ov::element::i32, ov::Shape{count, 6});
        auto points = std::make_shared<op::Parameter>(ov::element::i32, ov::Shape{count, 2});
        vertices->output(0).get_tensor().set_names({"vertices"});
        points->output(0).get_tensor().set_names({"points"});
        std::array<Node, 6> v;
        for (int64_t i = 0; i < 6; ++i) v[size_t(i)] = lane(vertices, i);
        auto px = lane(points, 0), py = lane(points, 1);
        const auto area = determinant(sub(v[2], v[0]), sub(v[5], v[1]), sub(v[3], v[1]), sub(v[4], v[0]));
        auto covered = either(area.positive, area.negative);
        for (size_t a = 0; a < 6; a += 2) {
            const auto b = (a + 2) % 6;
            auto dx = sub(v[b], v[a]), dy = sub(v[b + 1], v[a + 1]);
            const auto e = determinant(dx, sub(py, v[a + 1]), dy, sub(px, v[a]));
            auto top_left = either(negative(dy), both(zero(dy), positive(dx)));
            auto reverse_top_left = either(positive(dy), both(zero(dy), negative(dx)));
            auto inside = either(both(area.positive, either(e.positive, both(e.zero, top_left))),
                                 both(area.negative, either(e.negative, both(e.zero, reverse_top_left))));
            covered = both(covered, inside);
        }
        auto model = std::make_shared<ov::Model>(ov::OutputVector{covered}, ov::ParameterVector{vertices, points});
        ov::AnyMap properties{ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY)};
        if (backend == "NPU") {
            for (const auto &node : model->get_ops()) node->get_rt_info()["precise_0"] = std::string{};
            std::ostringstream xml, weights; ov::pass::Serialize serializer(xml, weights); serializer.run_on_model(model);
            const auto bytes = weights.str(); ov::Tensor data(ov::element::u8, ov::Shape{bytes.size()});
            if (!bytes.empty()) std::memcpy(data.data(), bytes.data(), bytes.size());
            model = core.read_model(xml.str(), data);
            for (const auto &node : model->get_ops())
                if (!node->get_rt_info().count("precise_0")) throw std::runtime_error("coverage precision marker lost");
            properties.emplace(ov::hint::execution_mode(ov::hint::ExecutionMode::ACCURACY));
        }
        compiled = core.compile_model(model, backend, properties);
        const auto devices = compiled.get_property(ov::execution_devices);
        require(devices.size() == 1 && (devices[0] == backend || devices[0].rfind(backend + ".", 0) == 0));
        execution_device = devices[0]; request = compiled.create_infer_request();
    }
};
CoverageExecutor::CoverageExecutor(uint32_t samples, const std::string &backend) : state(std::make_unique<State>(samples, backend)) {}
CoverageExecutor::~CoverageExecutor() = default;
CoverageExecutor::Result CoverageExecutor::execute(const std::vector<Sample> &samples) {
    auto &s = *state; require(!s.failed && samples.size() == s.count);
    for (const auto &sample : samples) {
        for (const auto &point : sample.triangle) for (const auto coordinate : point)
            require(coordinate >= -coordinate_limit && coordinate <= coordinate_limit);
        for (const auto coordinate : sample.point) require(coordinate >= -coordinate_limit && coordinate <= coordinate_limit);
    }
    s.failed = true;
    auto vertices = s.request.get_tensor("vertices"), points = s.request.get_tensor("points");
    for (size_t i = 0; i < samples.size(); ++i) {
        std::memcpy(vertices.data<int32_t>() + i * 6, samples[i].triangle.data(), 6 * sizeof(int32_t));
        std::memcpy(points.data<int32_t>() + i * 2, samples[i].point.data(), 2 * sizeof(int32_t));
    }
    s.request.infer(); const auto output = s.request.get_output_tensor();
    require(output.get_element_type() == ov::element::i32 && output.get_size() == s.count);
    Result result{{}, s.execution_device}; result.covered.reserve(s.count);
    for (size_t i = 0; i < s.count; ++i) {
        const auto value = output.data<const int32_t>()[i]; require(value == 0 || value == 1);
        result.covered.push_back(uint8_t(value));
    }
    s.failed = false; return result;
}
}
