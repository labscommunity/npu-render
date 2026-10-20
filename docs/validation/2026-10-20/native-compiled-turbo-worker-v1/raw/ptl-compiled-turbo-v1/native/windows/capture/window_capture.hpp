#pragma once
#define NOMINMAX
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>

namespace nr::capture {
namespace wgc = winrt::Windows::Graphics::Capture;
namespace direct = winrt::Windows::Graphics::DirectX;
struct Device {
    struct Retirement {
        std::future<void> completion;
        std::chrono::steady_clock::time_point started;
        std::shared_ptr<std::atomic<unsigned>> stage;
    };
    std::vector<Retirement> retirements;
    bool retirement_failed = false;
    uint64_t retired_sessions = 0;
    void reap() {
        if (retirement_failed)
            throw std::runtime_error("capture retirement could not be scheduled");
        for (auto it = retirements.begin(); it != retirements.end();) {
            if (it->completion.wait_for(std::chrono::milliseconds(0)) ==
                std::future_status::ready) {
                it->completion.get();
                ++retired_sessions;
                it = retirements.erase(it);
            } else {
                if (std::chrono::steady_clock::now() - it->started > std::chrono::seconds(2))
                    throw std::runtime_error("capture retirement timed out at stage " +
                                             std::to_string(it->stage->load()) +
                                             "; restore original desktop");
                ++it;
            }
        }
    }
    void retire(wgc::GraphicsCaptureSession session, wgc::Direct3D11CaptureFramePool pool,
                wgc::GraphicsCaptureItem item, HWND target,
                std::shared_ptr<std::atomic<bool>> closed_signal) {
        reap();
        if (retirements.size() >= 64)
            throw std::runtime_error("capture retirement queue exceeded resource bound");
        auto stage = std::make_shared<std::atomic<unsigned>>(0);
        std::packaged_task<void()> task([session = std::move(session), pool = std::move(pool),
                                         item = std::move(item), target, closed_signal,
                                         stage]() mutable {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            struct Apartment {
                ~Apartment() { winrt::uninit_apartment(); }
            } apartment;
            // Locals release the agile capture objects before apartment teardown.
            auto closing_session = std::move(session);
            auto closing_pool = std::move(pool);
            auto closing_item = std::move(item);
            // A destroyed source has already stopped its capture server. A second
            // synchronous Stop can race that teardown. Release its interfaces instead.
            // Give a hide/destroy transition time to deliver the item's Closed event.
            Sleep(100);
            const bool source_closed = closed_signal->load() || !IsWindow(target);
            if (!source_closed) {
                stage->store(1);
                if (closing_session)
                    closing_session.Close();
                stage->store(2);
                if (closing_pool)
                    closing_pool.Close();
            }
            stage->store(3);
            closing_session = nullptr;
            stage->store(4);
            closing_pool = nullptr;
            stage->store(5);
            closing_item = nullptr;
            stage->store(6);
        });
        auto completion = task.get_future();
        std::thread worker(std::move(task));
        worker.detach();
        retirements.push_back({std::move(completion), std::chrono::steady_clock::now(), stage});
    }
    void drain() {
        while (!retirements.empty()) {
            reap();
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            Sleep(1);
        }
    }
    ~Device() {
        try {
            drain();
        } catch (...) { /* Captures retain their own COM resources until exit. */
        }
    }

    winrt::com_ptr<ID3D11Device> device;
    winrt::com_ptr<ID3D11DeviceContext> context;
    direct::Direct3D11::IDirect3DDevice projected{nullptr};
    Device() {
        winrt::check_hresult(D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr,
            0, D3D11_SDK_VERSION, device.put(), nullptr, context.put()));
        auto dxgi = device.as<IDXGIDevice>();
        winrt::com_ptr<IInspectable> inspectable;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), inspectable.put()));
        projected = inspectable.as<direct::Direct3D11::IDirect3DDevice>();
    }
};
struct Image {
    UINT width = 0, height = 0;
    int64_t timestamp = 0;
    std::vector<uint8_t> bgra;
};
class Window {
    Device &device;
    HWND target = nullptr;
    std::shared_ptr<std::atomic<bool>> source_closed = std::make_shared<std::atomic<bool>>(false);
    wgc::GraphicsCaptureItem item{nullptr};
    wgc::Direct3D11CaptureFramePool pool{nullptr};
    wgc::GraphicsCaptureSession session{nullptr};
    winrt::com_ptr<ID3D11Texture2D> staging;
    UINT allocated_width = 0, allocated_height = 0;
    winrt::Windows::Graphics::SizeInt32 pool_size{};

  public:
    static void validate_size(winrt::Windows::Graphics::SizeInt32 size) {
        if (size.Width <= 0 || size.Height <= 0 || size.Width > 8192 || size.Height > 8192 ||
            uint64_t(size.Width) * size.Height > 16ull * 1024 * 1024)
            throw std::runtime_error("capture item exceeds dimension bound");
    }
    explicit Window(Device &d, HWND hwnd) : device(d), target(hwnd) {
        if (!IsWindow(hwnd))
            throw std::runtime_error("capture window missing");
        DWORD affinity = 0;
        if (GetWindowDisplayAffinity(hwnd, &affinity) && affinity != WDA_NONE)
            throw std::runtime_error("window excludes capture");
        auto factory =
            winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        winrt::check_hresult(factory->CreateForWindow(
            hwnd, winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi(item)));
        item.Closed([closed = source_closed](const auto &, const auto &) { closed->store(true); });
        pool_size = item.Size();
        validate_size(pool_size);
        pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
            d.projected, direct::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, pool_size);
        session = pool.CreateCaptureSession(item);
        session.IsCursorCaptureEnabled(false);
        session.StartCapture();
    }
    ~Window() {
        // A synchronous Close can wait for UI work while this thread owns the
        // output HWND. Retire on an initialized MTA and keep the UI pumping.
        try {
            staging = nullptr;
            device.context->Flush();
            device.retire(std::move(session), std::move(pool), std::move(item), target,
                          source_closed);
        } catch (...) {
            device.retirement_failed = true;
        }
    }
    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;
    bool poll(Image &image) {
        auto frame = pool.TryGetNextFrame();
        if (!frame)
            return false;
        auto size = frame.ContentSize();
        if (size.Width <= 0 || size.Height <= 0) {
            frame.Close();
            return false;
        }
        validate_size(size);
        if (size.Width != pool_size.Width || size.Height != pool_size.Height) {
            frame.Close();
            pool_size = size;
            pool.Recreate(device.projected, direct::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
                          size);
            return false;
        }
        auto access =
            frame.Surface()
                .as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        winrt::com_ptr<ID3D11Texture2D> texture;
        winrt::check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D), texture.put_void()));
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
            throw std::runtime_error("unexpected capture format");
        // During growth, an already queued frame can still use the old pool's
        // smaller texture. It cannot provide the complete new window surface.
        if (desc.Width < UINT(size.Width) || desc.Height < UINT(size.Height)) {
            frame.Close();
            return false;
        }
        if (!staging || allocated_width != desc.Width || allocated_height != desc.Height) {
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags = 0;
            staging = nullptr;
            winrt::check_hresult(device.device->CreateTexture2D(&desc, nullptr, staging.put()));
            allocated_width = desc.Width;
            allocated_height = desc.Height;
        }
        device.context->CopyResource(staging.get(), texture.get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        winrt::check_hresult(device.context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped));
        image.width = size.Width;
        image.height = size.Height;
        image.timestamp = frame.SystemRelativeTime().count();
        try {
            image.bgra.resize(size_t(image.width) * image.height * 4);
            for (UINT y = 0; y < image.height; ++y)
                std::memcpy(image.bgra.data() + size_t(y) * image.width * 4,
                            static_cast<const uint8_t *>(mapped.pData) +
                                size_t(y) * mapped.RowPitch,
                            size_t(image.width) * 4);
        } catch (...) {
            device.context->Unmap(staging.get(), 0);
            throw;
        }
        device.context->Unmap(staging.get(), 0);
        frame.Close();
        return true;
    }
};
} // namespace nr::capture
