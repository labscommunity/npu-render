#define NOMINMAX
#include "frame_readback.hpp"
#include "frame_file.hpp"
#include <cstdio>
using Microsoft::WRL::ComPtr;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "readback check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (false)
int main() {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));
    nr::indirect::FrameReadback reader; nr::indirect::FramePixels frame;
    uint64_t checked_bytes = 0;
    for (const auto format : {DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM})
        for (const auto width : {65u, 1024u}) {
            const UINT height = width == 65 ? 13 : 768;
            D3D11_TEXTURE2D_DESC desc{}; desc.Width = width; desc.Height = height;
            desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
            desc.Format = format; desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            ComPtr<ID3D11Texture2D> source;
            CHECK(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &source)));
            const UINT pitch = width * 4 + 28;
            std::vector<uint8_t> upload(size_t(pitch) * height, 0xc7);
            for (UINT pass = 0; pass < 3; ++pass) {
                for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width * 4; ++x)
                    upload[size_t(y) * pitch + x] = uint8_t(x * 17 + y * 31 + pass * 53);
                context->UpdateSubresource(source.Get(), 0, nullptr, upload.data(), pitch, 0);
                CHECK(SUCCEEDED(reader.read(device.Get(), context.Get(), source.Get(), frame)));
                CHECK(frame.width == width && frame.height == height && frame.format == format);
                CHECK(frame.bytes.size() == size_t(width) * height * 4);
                for (UINT y = 0; y < height; ++y)
                    CHECK(!std::memcmp(frame.bytes.data() + size_t(y) * width * 4, upload.data() + size_t(y) * pitch, width * 4));
                checked_bytes += frame.bytes.size();
            }
        }
    const auto before = frame;
    CHECK(reader.read(device.Get(), context.Get(), nullptr, frame) == E_INVALIDARG);
    D3D11_TEXTURE2D_DESC invalid{}; invalid.Width = invalid.Height = 8;
    invalid.MipLevels = invalid.ArraySize = invalid.SampleDesc.Count = 1;
    invalid.Format = DXGI_FORMAT_R32_FLOAT; invalid.Usage = D3D11_USAGE_DEFAULT;
    ComPtr<ID3D11Texture2D> unsupported;
    CHECK(SUCCEEDED(device->CreateTexture2D(&invalid, nullptr, &unsupported)));
    CHECK(reader.read(device.Get(), context.Get(), unsupported.Get(), frame) == E_INVALIDARG);
    CHECK(frame.width == before.width && frame.height == before.height && frame.format == before.format && frame.bytes == before.bytes);
    WCHAR root[MAX_PATH]{}, directory[MAX_PATH]{}, temporary[MAX_PATH]{}, destination[MAX_PATH]{};
    CHECK(GetTempPathW(MAX_PATH, root) && GetTempFileNameW(root, L"nrf", 0, directory));
    CHECK(DeleteFileW(directory) && CreateDirectoryW(directory, nullptr));
    swprintf_s(temporary, L"%s\\pending.bin", directory);
    swprintf_s(destination, L"%s\\latest.bin", directory);
    struct Cleanup {
        const WCHAR *directory, *temporary, *destination;
        ~Cleanup() { DeleteFileW(temporary); DeleteFileW(destination); RemoveDirectoryW(directory); }
    } cleanup{directory, temporary, destination};
    CHECK(SUCCEEDED(nr::indirect::write_frame_file(temporary, destination, frame, 1)));
    struct File {
        HANDLE handle;
        ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    } old{CreateFileW(destination, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr)};
    CHECK(old.handle != INVALID_HANDLE_VALUE);
    // An existing reader sees an intact old frame after the writer replaces it.
    frame.bytes[0] ^= 0xff;
    CHECK(SUCCEEDED(nr::indirect::write_frame_file(temporary, destination, frame, 2)));
    nr::indirect::FrameHeader header{}; DWORD read = 0;
    CHECK(ReadFile(old.handle, &header, sizeof(header), &read, nullptr) && read == sizeof(header) && header.sequence == 1);
    CHECK(header.magic == nr::indirect::FrameHeader{}.magic && header.version == 1 && header.header_bytes == 64 &&
          header.pixel_bytes == before.bytes.size() && header.width == before.width && header.height == before.height &&
          header.row_bytes == before.width * 4 && header.format == UINT(before.format));
    std::vector<uint8_t> received(before.bytes.size());
    CHECK(ReadFile(old.handle, received.data(), DWORD(received.size()), &read, nullptr) && read == received.size() && received == before.bytes);
    const auto byte = frame.bytes.back(); frame.bytes.pop_back();
    CHECK(nr::indirect::write_frame_file(temporary, destination, frame, 3) == E_INVALIDARG);
    frame.bytes.push_back(byte);
    File current{CreateFileW(destination, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr)};
    CHECK(current.handle != INVALID_HANDLE_VALUE);
    CHECK(ReadFile(current.handle, &header, sizeof(header), &read, nullptr) && read == sizeof(header) && header.sequence == 2);
    CHECK(ReadFile(current.handle, received.data(), DWORD(received.size()), &read, nullptr) && read == received.size() && received == frame.bytes);
    std::printf("{\"kind\":\"received-frame-readback-fixture\",\"device\":\"WARP\",\"frames\":12,\"verified_bytes\":%llu,\"npu_execution_tested\":false,\"display_delivery_tested\":false,\"passed\":true}\n", checked_bytes);
    return 0;
}
