#pragma once
#include "dxgi_device_identity.h"
#include "nt_handle.h"
#include <memory>
#include <span>
#include <vector>
#if defined(_WIN32)
namespace webscene::graphics {
struct borrowed_dxgi_fence_wait { HANDLE handle{}; uint64_t value{}; };
// Open every fence before touching the command queue. Retain this owner until
// the consumer's GPU completion; releasing it is not a completion notification.
class d3d12_fence_waits {
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Fence>> fences_;
    std::vector<uint64_t> values_;
    bool attempted_{};
public:
    d3d12_fence_waits()=default;
    d3d12_fence_waits(const d3d12_fence_waits&)=delete;
    d3d12_fence_waits& operator=(const d3d12_fence_waits&)=delete;
    static HRESULT prepare(ID3D12CommandQueue* queue,adapter_luid expected_adapter,
        std::span<const borrowed_dxgi_fence_wait> waits,std::unique_ptr<d3d12_fence_waits>& result) {
        if (!queue || result || !expected_adapter.valid) return E_INVALIDARG;
        for (const auto& wait:waits) if (!win32_nt_handle_ops::valid(wait.handle)) return E_INVALIDARG;
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        auto status=queue->GetDevice(IID_PPV_ARGS(&device));
        if (FAILED(status)) return status;
        adapter_luid actual;
        status=query_adapter_luid(device.Get(),actual);
        if (FAILED(status)) return status;
        if (!(actual==expected_adapter)) return DXGI_ERROR_UNSUPPORTED;
        try {
            auto candidate=std::make_unique<d3d12_fence_waits>();
            candidate->queue_=queue;
            candidate->fences_.reserve(waits.size()); candidate->values_.reserve(waits.size());
            for (const auto& wait:waits) {
                Microsoft::WRL::ComPtr<ID3D12Fence> fence;
                status=device->OpenSharedHandle(wait.handle,IID_PPV_ARGS(&fence));
                if (FAILED(status)) return status;
                candidate->fences_.push_back(std::move(fence));
                candidate->values_.push_back(wait.value);
            }
            result=std::move(candidate); return S_OK;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    }
    // Enqueues GPU waits, never a CPU event wait or global device-idle wait.
    // Serialize with consumer submission on this queue. A failed enqueue may
    // leave earlier waits queued; do not submit sampling or retry this batch.
    HRESULT enqueue() noexcept {
        if (!queue_ || attempted_) return E_UNEXPECTED;
        attempted_=true;
        for (size_t i=0;i<fences_.size();++i) {
            const auto status=queue_->Wait(fences_[i].Get(),values_[i]);
            if (FAILED(status)) return status;
        }
        return S_OK;
    }
};
} // namespace webscene::graphics
#endif
