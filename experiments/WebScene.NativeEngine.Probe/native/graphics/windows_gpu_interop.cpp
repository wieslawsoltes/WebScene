#include "../webscene_native_engine.h"
#if defined(_WIN32) && defined(WEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS)
#include "d3d11_scene_consumer.h"
#include "windows_gpu_adapter.h"
#endif

int32_t webscene_gpu_d3d11_supported_v3(void* device) {
#if defined(_WIN32) && defined(WEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS)
    if(!device)return E_INVALIDARG;
    try {
        auto host=static_cast<ID3D11Device*>(device);
        webscene::graphics::adapter_luid actual;
        auto status=webscene::graphics::query_adapter_luid(host,actual);if(FAILED(status))return status;
        auto selected=webscene::graphics::windows_gpu_adapter_luid();
        if(actual.low!=selected.LowPart||actual.high!=selected.HighPart)return DXGI_ERROR_UNSUPPORTED;
        Microsoft::WRL::ComPtr<ID3D11Device5> device5;
        status=host->QueryInterface(IID_PPV_ARGS(&device5));if(FAILED(status))return status;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;host->GetImmediateContext(&context);
        Microsoft::WRL::ComPtr<ID3D11DeviceContext4> context4;
        status=context.As(&context4);if(FAILED(status))return status;
        Microsoft::WRL::ComPtr<ID3D11Fence> fence;
        return device5->CreateFence(0,D3D11_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence));
    }catch(...){return E_FAIL;}
#else
    return static_cast<int32_t>(0x80004001U);
#endif
}

int32_t webscene_gpu_d3d11_import_v3(webscene_gpu_image_consumer_v3* consumer,
    void* device,void** owner,void** texture) {
    if(owner)*owner=nullptr;if(texture)*texture=nullptr;
#if defined(_WIN32) && defined(WEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS)
    if(!owner||!texture)return E_INVALIDARG;
    try {
        std::unique_ptr<webscene::graphics::d3d11_scene_consumer> candidate;
        const auto status=webscene::graphics::d3d11_scene_consumer::create(consumer,
            static_cast<ID3D11Device*>(device),candidate);
        if(FAILED(status))return status;
        *texture=candidate->texture();*owner=candidate.release();return S_OK;
    }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
    catch(...){return E_INVALIDARG;}
#else
    return static_cast<int32_t>(0x80004001U); // E_NOTIMPL; no graphics dependency.
#endif
}
int32_t webscene_gpu_d3d11_seal_v3(void* owner) {
#if defined(_WIN32) && defined(WEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS)
    return owner?static_cast<webscene::graphics::d3d11_scene_consumer*>(owner)->seal():E_INVALIDARG;
#else
    return static_cast<int32_t>(0x80004001U);
#endif
}
int32_t webscene_gpu_d3d11_poll_v3(void* owner) {
#if defined(_WIN32) && defined(WEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS)
    return owner?static_cast<webscene::graphics::d3d11_scene_consumer*>(owner)->poll():E_INVALIDARG;
#else
    return static_cast<int32_t>(0x80004001U);
#endif
}
void webscene_gpu_d3d11_destroy_v3(void* owner) {
#if defined(_WIN32) && defined(WEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS)
    delete static_cast<webscene::graphics::d3d11_scene_consumer*>(owner);
#endif
}
