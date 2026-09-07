#pragma once
#include "dxgi_device_identity.h"
#include "nt_handle.h"
#include <memory>
#if defined(_WIN32)
namespace webscene::graphics {
// Allocation owner only. The lease provider must retain this object through all
// producer/consumer GPU completion; destruction does not wait for a queue.
class d3d12_shared_color {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    owned_nt_handle handle_;
    adapter_luid adapter_;
    uint64_t allocation_bytes_{};
public:
    d3d12_shared_color()=default;
    d3d12_shared_color(const d3d12_shared_color&)=delete;
    d3d12_shared_color& operator=(const d3d12_shared_color&)=delete;
    ID3D12Resource* resource() const noexcept { return resource_.Get(); }
    HANDLE borrowed_handle() const noexcept { return handle_.get(); }
    adapter_luid adapter() const noexcept { return adapter_; }
    uint64_t allocation_bytes() const noexcept { return allocation_bytes_; }
    static HRESULT create(ID3D12Device* device,const image_metadata& image,uint64_t available_bytes,
        std::unique_ptr<d3d12_shared_color>& result) {
        // Never implicitly discard a previous allocation which may be in flight.
        if (result || !device || !image.width || !image.height) return E_INVALIDARG;
        const auto format=dxgi_color_format(image.format);
        if (format==DXGI_FORMAT_UNKNOWN) return E_INVALIDARG;
        dxgi_endpoint endpoint;
        auto status=identify_dxgi_endpoint(device,endpoint);
        if (FAILED(status)) return status;
        if (!(endpoint.color_formats & (1U<<(static_cast<uint32_t>(image.format)-1))))
            return DXGI_ERROR_UNSUPPORTED;
        D3D12_RESOURCE_DESC description{};
        description.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width=image.width; description.Height=image.height;
        description.DepthOrArraySize=1; description.MipLevels=1;
        description.Format=format; description.SampleDesc.Count=1;
        description.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
        description.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        const auto allocation=device->GetResourceAllocationInfo(0,1,&description);
        if (allocation.SizeInBytes==UINT64_MAX || !allocation.SizeInBytes) return E_INVALIDARG;
        if (allocation.SizeInBytes>available_bytes) return E_OUTOFMEMORY;
        try {
            auto candidate=std::make_unique<d3d12_shared_color>();
            D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            heap.CreationNodeMask=1; heap.VisibleNodeMask=1;
            status=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&description,
                D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&candidate->resource_));
            if (FAILED(status)) return status;
            HANDLE handle=nullptr;
            status=device->CreateSharedHandle(candidate->resource_.Get(),nullptr,GENERIC_ALL,nullptr,&handle);
            if (FAILED(status)) return status;
            if (!win32_nt_handle_ops::valid(handle)) return E_FAIL;
            candidate->handle_=owned_nt_handle::adopt(handle);
            candidate->adapter_=endpoint.adapter;
            candidate->allocation_bytes_=allocation.SizeInBytes;
            result=std::move(candidate);
            return S_OK;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    }
};
} // namespace webscene::graphics
#endif
