#pragma once
// Copy a received surface to tightly packed host bytes. This performs no
// rendering and makes no claim about the producer or screen delivery.
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstring>
#include <new>
#include <utility>
#include <vector>

namespace nr::indirect {
struct FramePixels {
    UINT width = 0, height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    std::vector<uint8_t> bytes;
};
class FrameReadback {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    D3D11_TEXTURE2D_DESC previous{};
public:
    // The caller serializes every use of this immediate context, including
    // other readback instances associated with overlapping swapchain lifetimes.
    HRESULT read(ID3D11Device *device, ID3D11DeviceContext *context,
                 ID3D11Texture2D *source, FramePixels &output) noexcept {
        if (!device || !context || !source || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
            return E_INVALIDARG;
        Microsoft::WRL::ComPtr<ID3D11Device> owner, context_owner;
        source->GetDevice(&owner); context->GetDevice(&context_owner);
        if (owner.Get() != device || context_owner.Get() != device) return E_INVALIDARG;
        D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
        if (!desc.Width || !desc.Height || desc.Width > 8192 || desc.Height > 8192 ||
            uint64_t(desc.Width) * desc.Height > 4 * 1024 * 1024 || desc.MipLevels != 1 ||
            desc.ArraySize != 1 || desc.SampleDesc.Count != 1 || desc.SampleDesc.Quality ||
            (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM && desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM))
            return E_INVALIDARG;
        try {
            FramePixels next{desc.Width, desc.Height, desc.Format, {}};
            const auto row_bytes = size_t(desc.Width) * 4;
            next.bytes.resize(row_bytes * desc.Height);
            Microsoft::WRL::ComPtr<ID3D11Device> staging_owner;
            if (staging) staging->GetDevice(&staging_owner);
            if (!staging || staging_owner.Get() != device || previous.Width != desc.Width ||
                previous.Height != desc.Height || previous.Format != desc.Format) {
                auto copy = desc; copy.Usage = D3D11_USAGE_STAGING;
                copy.BindFlags = copy.MiscFlags = 0; copy.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                Microsoft::WRL::ComPtr<ID3D11Texture2D> replacement;
                const auto status = device->CreateTexture2D(&copy, nullptr, &replacement);
                if (FAILED(status)) return status;
                staging = std::move(replacement); previous = desc;
            }
            context->CopyResource(staging.Get(), source);
            auto status = device->GetDeviceRemovedReason();
            if (FAILED(status)) return status;
            D3D11_MAPPED_SUBRESOURCE mapped{};
            status = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
            if (FAILED(status)) return status;
            // No allocation or throwing operation is permitted before Unmap.
            if (!mapped.pData || mapped.RowPitch < row_bytes) status = E_UNEXPECTED;
            else for (UINT y = 0; y < desc.Height; ++y)
                std::memcpy(next.bytes.data() + size_t(y) * row_bytes,
                            static_cast<const uint8_t *>(mapped.pData) + size_t(y) * mapped.RowPitch, row_bytes);
            context->Unmap(staging.Get(), 0);
            if (FAILED(status)) return status;
            output = std::move(next);
            return S_OK;
        } catch (const std::bad_alloc &) { return E_OUTOFMEMORY; }
        catch (...) { return E_FAIL; }
    }
};
}
