#pragma once

#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl.h>
#include <openvino/openvino.hpp>
#include <openvino/opsets/opset13.hpp>
#include <openvino/runtime/properties.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;

inline void check(HRESULT code, const char *operation) {
    if (FAILED(code)) {
        std::ostringstream error;
        error << operation << " failed with HRESULT 0x" << std::hex << uint32_t(code);
        throw std::runtime_error(error.str());
    }
}

inline std::string quote(const std::string &value) {
    std::ostringstream text;
    text << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') text << '\\' << c;
        else if (c < 32) text << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c);
        else text << c;
    }
    return text.str() + '"';
}

struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value) CloseHandle(value); }
    Handle() = default;
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
};

inline D3D12_RESOURCE_DESC buffer_description(size_t bytes, D3D12_RESOURCE_FLAGS flags) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    return desc;
}

struct SharedBuffer {
    ComPtr<ID3D12Heap> heap;
    ComPtr<ID3D12Resource> resource;
    Handle shared;

    SharedBuffer(ID3D12Device *device, size_t bytes) {
        const size_t alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        D3D12_HEAP_DESC desc{};
        desc.SizeInBytes = (bytes + alignment - 1) & ~(alignment - 1);
        desc.Alignment = alignment;
        desc.Properties.Type = D3D12_HEAP_TYPE_CUSTOM;
        desc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE;
        desc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
        desc.Properties.CreationNodeMask = desc.Properties.VisibleNodeMask = 1;
        desc.Flags = D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER;
        check(device->CreateHeap(&desc, IID_PPV_ARGS(&heap)), "CreateHeap(cross-adapter)");
        auto resource_desc = buffer_description(bytes, D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER |
                                                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        check(device->CreatePlacedResource(heap.Get(), 0, &resource_desc,
                                          D3D12_RESOURCE_STATE_COMMON, nullptr,
                                          IID_PPV_ARGS(&resource)), "CreatePlacedResource");
        check(device->CreateSharedHandle(heap.Get(), nullptr, GENERIC_ALL, nullptr, &shared.value),
              "CreateSharedHandle(heap)");
    }
};

inline ComPtr<ID3D12Resource> staging(ID3D12Device *device, size_t bytes, D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    auto desc = buffer_description(bytes, D3D12_RESOURCE_FLAG_NONE);
    ComPtr<ID3D12Resource> result;
    check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                          type == D3D12_HEAP_TYPE_UPLOAD ?
                                              D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COPY_DEST,
                                          nullptr, IID_PPV_ARGS(&result)), "CreateCommittedResource(staging)");
    return result;
}

struct Transfers {
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    Handle event;
    uint64_t serial = 0;

    explicit Transfers(ID3D12Device *device) {
        D3D12_COMMAND_QUEUE_DESC desc{};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        check(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)), "CreateCommandQueue");
        check(device->CreateCommandAllocator(desc.Type, IID_PPV_ARGS(&allocator)), "CreateCommandAllocator");
        check(device->CreateCommandList(0, desc.Type, allocator.Get(), nullptr,
                                        IID_PPV_ARGS(&list)), "CreateCommandList");
        check(list->Close(), "initial command-list close");
        check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence");
        event.value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event.value) throw std::runtime_error("CreateEvent failed");
    }

    void begin() {
        check(allocator->Reset(), "allocator reset");
        check(list->Reset(allocator.Get(), nullptr), "command-list reset");
    }

    void barrier(ID3D12Resource *resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER transition{};
        transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        transition.Transition.pResource = resource;
        transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        transition.Transition.StateBefore = before;
        transition.Transition.StateAfter = after;
        list->ResourceBarrier(1, &transition);
    }

    void finish() {
        check(list->Close(), "command-list close");
        ID3D12CommandList *commands[] = {list.Get()};
        queue->ExecuteCommandLists(1, commands);
        check(queue->Signal(fence.Get(), ++serial), "queue signal");
        check(fence->SetEventOnCompletion(serial, event.value), "fence event");
        if (WaitForSingleObject(event.value, 10000) != WAIT_OBJECT_0)
            throw std::runtime_error("GPU transfer fence did not complete within 10 seconds");
    }
};

inline void upload(ID3D12Resource *resource, const void *values, size_t bytes) {
    void *mapped = nullptr;
    D3D12_RANGE no_read{0, 0};
    check(resource->Map(0, &no_read, &mapped), "upload Map");
    std::memcpy(mapped, values, bytes);
    D3D12_RANGE written{0, bytes};
    resource->Unmap(0, &written);
}

