#pragma once
#if defined(_WIN32)
#include "image_lease_abi.h"
#include "d3d12_canvas_images.h"
#include <d3d11_4.h>

namespace webscene::graphics {
// Borrows the image consumer; its caller keeps that lease until poll reports
// completion. Owns every opened texture/fence and the actual host context.
// Ordinary use performs no CPU pixel transfer and no CPU GPU-completion wait.
class d3d11_scene_consumer {
    Microsoft::WRL::ComPtr<ID3D11Device5> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext4> context_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    std::vector<Microsoft::WRL::ComPtr<ID3D11Fence>> waits_;
    Microsoft::WRL::ComPtr<ID3D11Fence> completion_;
    bool sealed_=false;
public:
    ID3D11Texture2D* texture() const noexcept {return texture_.Get();}
    static HRESULT create(webscene_gpu_image_consumer_v3* image,ID3D11Device* host,
        std::unique_ptr<d3d11_scene_consumer>& output) {
        if(!image||!host||output)return E_INVALIDARG;
        adapter_luid adapter;
        auto status=query_adapter_luid(host,adapter);if(FAILED(status))return status;
        const auto& source=d3d12_canvas_images::resolve(image->value,adapter);
        auto candidate=std::make_unique<d3d11_scene_consumer>();
        status=host->QueryInterface(IID_PPV_ARGS(&candidate->device_));if(FAILED(status))return status;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediate;
        host->GetImmediateContext(&immediate);
        status=immediate.As(&candidate->context_);if(FAILED(status))return status;
        status=candidate->device_->OpenSharedResource1(source.borrowed_handle(),IID_PPV_ARGS(&candidate->texture_));
        if(FAILED(status))return status;
        D3D11_TEXTURE2D_DESC desc{};candidate->texture_->GetDesc(&desc);
        const auto metadata=image->value.describe();
        if(desc.Width!=metadata.width||desc.Height!=metadata.height||desc.SampleDesc.Count!=1
            ||desc.ArraySize!=1||desc.MipLevels!=1||desc.Format!=dxgi_color_format(metadata.format)
            ||!(desc.BindFlags&D3D11_BIND_SHADER_RESOURCE))return DXGI_ERROR_UNSUPPORTED;
        status=candidate->device_->CreateFence(0,D3D11_FENCE_FLAG_NONE,IID_PPV_ARGS(&candidate->completion_));
        if(FAILED(status))return status;
        std::vector<uint64_t> values;
        if(image->dependencies) {
            const auto count=image->dependencies->count();
            candidate->waits_.reserve(count);values.reserve(count);
            for(size_t index=0;index<count;++index) {
                void* handle=nullptr;uint64_t value=0;
                if(!image->dependencies->dxgi_fence(index,handle,value)||!win32_nt_handle_ops::valid(handle))return E_INVALIDARG;
                Microsoft::WRL::ComPtr<ID3D11Fence> fence;
                status=candidate->device_->OpenSharedFence(handle,IID_PPV_ARGS(&fence));if(FAILED(status))return status;
                candidate->waits_.push_back(std::move(fence));values.push_back(value);
            }
        }
        // Stage every import before changing queue state. A failed enqueue must
        // never be retried as if the earlier waits had not already happened.
        for(size_t index=0;index<values.size();++index) {
            status=candidate->context_->Wait(candidate->waits_[index].Get(),values[index]);
            if(FAILED(status))return status;
        }
        output=std::move(candidate);return S_OK;
    }
    HRESULT seal() {
        if(sealed_)return S_OK;
        const auto status=context_->Signal(completion_.Get(),1);
        if(FAILED(status))return status;
        context_->Flush();sealed_=true;return S_OK;
    }
    HRESULT poll() const {
        if(!sealed_)return E_PENDING;
        const auto status=device_->GetDeviceRemovedReason();if(FAILED(status))return status;
        const auto value=completion_->GetCompletedValue();
        if(value==UINT64_MAX)return DXGI_ERROR_DEVICE_REMOVED;
        return value>=1?S_OK:S_FALSE;
    }
};
}
#endif
