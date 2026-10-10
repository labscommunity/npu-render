#include "../windows/blend_model.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

int main() {
    try {
        ov::Core core;
        int maximum = 0, cases = 0;
        for (const std::string alpha_mode : {"gather", "convolution"})
        for (size_t width : {64u,71u}) for (size_t group : {1u,4u,16u,32u}) {
            const size_t height = 39, count = width*height;
            auto compiled = core.compile_model(blend_model(width,height,true,group,alpha_mode), "CPU", blend_compile_properties("CPU"));
            if (compiled.get_property(ov::hint::inference_precision) != ov::element::f32)
                throw std::runtime_error("CPU packed blend did not retain FP32 precision");
            const auto actual = compiled.get_property(ov::execution_devices);
            if (actual.empty() || std::any_of(actual.begin(),actual.end(),[](const auto &d) {
                return d != "CPU" && d.rfind("CPU.",0) != 0;
            })) throw std::runtime_error("packed blend used another device");
            auto request = compiled.create_infer_request();
            auto background = request.get_input_tensor(0), foreground = request.get_input_tensor(1);
            auto *bg = background.data<uint8_t>(), *fg = foreground.data<uint8_t>();
            for (size_t frame = 0; frame < 3; ++frame) {
                for (size_t i = 0; i < count; ++i) {
                    const unsigned alpha = unsigned((i*19+frame*31)%256);
                    for (size_t c = 0; c < 4; ++c) {
                        bg[i*4+c] = c == 3 ? 255 : uint8_t((i*7+c*17+frame*19)%256);
                        fg[i*4+c] = c == 3 ? uint8_t(alpha) : uint8_t((i*13+c*41+frame*23)%(alpha+1));
                    }
                }
                request.infer();
                const auto *out = request.get_output_tensor().data<const uint8_t>();
                for (size_t i = 0; i < count; ++i) for (size_t c = 0; c < 4; ++c) {
                    const int expected = fg[i*4+c]+(bg[i*4+c]*(255-fg[i*4+3])+127)/255;
                    const int difference = std::abs(int(out[i*4+c])-expected);
                    if (difference > 1) throw std::runtime_error("packed blend differs from integer pixel oracle");
                    maximum = std::max(maximum,difference);
                }
                ++cases;
            }
            if (blend_pixel_group(width,true,group) != (width%group ? 1 : group))
                throw std::runtime_error("unexpected width specialization");
        }
        std::cout << "{\"packed_blend_cpu\":true,\"cases\":" << cases << ",\"max_byte_error\":" << maximum << "}\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
