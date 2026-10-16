#include "../windows/opaque_model.hpp"
#include <iostream>
#include <vector>

int main() {
    try {
        ov::Core core;
        for (const size_t width : {64,71}) {
            constexpr size_t height = 17;
            auto compiled = core.compile_model(opaque_model(width,height),"CPU");
            auto request = compiled.create_infer_request();
            auto bg = request.get_input_tensor(0), fg = request.get_input_tensor(1);
            for (unsigned frame = 0; frame < 3; ++frame) {
                for (size_t i = 0; i < width*height; ++i) for (size_t c = 0; c < 4; ++c) {
                    bg.data<uint8_t>()[i*4+c] = uint8_t((i*37+c*17+frame*11)%256);
                    fg.data<uint8_t>()[i*4+c] = c == 3 ? ((i+frame)%3 ? 255 : 0) : uint8_t((i*19+c*23+frame*31)%256);
                }
                request.infer(); const auto output = request.get_output_tensor();
                for (size_t i = 0; i < width*height; ++i) for (size_t c = 0; c < 4; ++c) {
                    const auto expected = fg.data<uint8_t>()[i*4+3] == 255 ? fg.data<uint8_t>()[i*4+c] : bg.data<uint8_t>()[i*4+c];
                    if (output.data<uint8_t>()[i*4+c] != expected) throw std::runtime_error("opaque byte-copy mismatch");
                }
            }
        }
        std::cout << "passed six opaque byte-copy cases (CPU graph semantics only)\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
