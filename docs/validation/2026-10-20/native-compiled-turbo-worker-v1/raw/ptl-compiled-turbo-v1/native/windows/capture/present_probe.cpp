#include "desktop_sources.hpp"
#include "presenter.hpp"
#include <dxgi1_6.h>
#include <fstream>
#include <iostream>
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    if (!nr::capture::input_desktop_ready())
        return 3;
    nr::capture::Device device;
    auto dxgi = device.device.as<IDXGIDevice>();
    winrt::com_ptr<IDXGIAdapter> adapter;
    winrt::check_hresult(dxgi->GetAdapter(adapter.put()));
    winrt::com_ptr<IDXGIOutput> output;
    winrt::check_hresult(adapter->EnumOutputs(0, output.put()));
    auto output6 = output.as<IDXGIOutput6>();
    DXGI_OUTPUT_DESC1 desc{};
    winrt::check_hresult(output6->GetDesc1(&desc));
    UINT width = GetSystemMetrics(SM_CXSCREEN), height = GetSystemMetrics(SM_CYSCREEN);
    nr::capture::Presenter presenter(device, width, height);
    std::vector<uint8_t> pixels(size_t(width) * height * 4);
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x) {
            size_t i = (size_t(y) * width + x) * 4;
            pixels[i] = uint8_t((x / 128) * 23);
            pixels[i + 1] = uint8_t((y / 128) * 41);
            pixels[i + 2] = uint8_t(((x + y) / 256) * 31);
            pixels[i + 3] = 255;
        }
    presenter.calibrate_system_overlay();
    presenter.present(pixels);
    presenter.present(pixels);
    unsigned maximum = presenter.screen_error(pixels);
    std::ofstream out(argv[1]);
    out << "{\"screen_error\":" << maximum << ",\"raw_screen_error\":" << presenter.raw_screen_error
        << ",\"overlay_pixels\":" << presenter.overlay_pixels
        << ",\"color_space\":" << unsigned(desc.ColorSpace)
        << ",\"bits_per_color\":" << desc.BitsPerColor << "}\n";
    presenter.hide();
    std::cout << "screen error " << maximum << std::endl;
    return maximum <= 2 ? 0 : 1;
}
