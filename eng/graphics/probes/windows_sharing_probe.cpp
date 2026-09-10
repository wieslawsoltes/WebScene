// Diagnostic readback belongs only to this hardware test, never presentation.
#include "../../../experiments/WebScene.NativeEngine.Probe/native/graphics/dawn_dxgi_canvas_host.h"
#include "../../../experiments/WebScene.NativeEngine.Probe/native/graphics/dawn_dxgi_scene_snapshot.h"
#include "../../../experiments/WebScene.NativeEngine.Probe/native/graphics/d3d11_scene_consumer.h"
#include <chrono>
#include <iostream>
#include <thread>
using namespace webscene::graphics;
using Microsoft::WRL::ComPtr;
static void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
int main() {
    try {
        auto instance=wgpu::CreateInstance();check(bool(instance),"Dawn instance unavailable");
        struct luid_options:wgpu::ChainedStruct{LUID adapterLUID; } luid;
        luid.sType=wgpu::SType::RequestAdapterOptionsLUID;luid.adapterLUID=windows_gpu_adapter_luid();
        wgpu::RequestAdapterOptions request;request.backendType=wgpu::BackendType::D3D12;request.nextInChain=&luid;
        wgpu::Adapter adapter;std::atomic<bool> discovered=false;
        instance.RequestAdapter(&request,wgpu::CallbackMode::AllowSpontaneous,
            [&](wgpu::RequestAdapterStatus,wgpu::Adapter value,wgpu::StringView){adapter=std::move(value);discovered=true;});
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        while(!discovered&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(discovered&&adapter,"Dawn D3D12 hardware adapter unavailable");
        const wgpu::FeatureName features[]={wgpu::FeatureName::SharedTextureMemoryDXGISharedHandle,wgpu::FeatureName::SharedFenceDXGISharedHandle};
        wgpu::DeviceDescriptor descriptor;descriptor.requiredFeatureCount=2;descriptor.requiredFeatures=features;
        descriptor.SetUncapturedErrorCallback([](const wgpu::Device&,wgpu::ErrorType,wgpu::StringView message){std::cerr<<std::string(message.data,message.length)<<'\n';});
        wgpu::Device device;std::atomic<bool> created=false;
        adapter.RequestDevice(&descriptor,wgpu::CallbackMode::AllowSpontaneous,
            [&](wgpu::RequestDeviceStatus,wgpu::Device value,wgpu::StringView message){device=std::move(value);if(!device)std::cerr<<std::string(message.data,message.length)<<'\n';created=true;});
        while(!created&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(created&&device,"Dawn shared-texture device unavailable");
        auto dxgi=windows_gpu_adapter();ComPtr<ID3D11Device> host;ComPtr<ID3D11DeviceContext> context;
        check(SUCCEEDED(D3D11CreateDevice(dxgi.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,0,D3D11_SDK_VERSION,&host,nullptr,&context)),"D3D11 hardware device unavailable");
        dawn_dxgi_canvas_host provider(64*1024*1024);
        image_metadata metadata;metadata.canvas=1;metadata.allocation_generation=1;metadata.content_serial=1;
        metadata.producer_timeline=1;metadata.producer_value=1;metadata.width=17;metadata.height=4;
        metadata.format=image_format::bgra8_unorm;metadata.alpha=image_alpha::opaque;metadata.color_space=image_color_space::srgb;
        wgpu::TextureDescriptor textureDesc;textureDesc.size={17,4,1};textureDesc.format=wgpu::TextureFormat::BGRA8Unorm;
        textureDesc.usage=wgpu::TextureUsage::RenderAttachment|wgpu::TextureUsage::TextureBinding;
        auto texture=provider.acquire(metadata,device,textureDesc);check(bool(texture),"Dawn shared allocation import failed");
        auto encoder=device.CreateCommandEncoder();
        wgpu::RenderPassColorAttachment color;color.view=texture.CreateView();color.loadOp=wgpu::LoadOp::Clear;
        color.storeOp=wgpu::StoreOp::Store;color.clearValue={64.0/255.0,128.0/255.0,192.0/255.0,1};
        wgpu::RenderPassDescriptor passDesc;passDesc.colorAttachmentCount=1;passDesc.colorAttachments=&color;
        auto pass=encoder.BeginRenderPass(&passDesc);pass.End();auto commands=encoder.Finish();device.GetQueue().Submit(1,&commands);
        provider.retire(texture,true);auto ticket=provider.capture_latest_submission();check(bool(ticket),"Submission capture failed");
        dawn_dxgi_scene_snapshot snapshot(std::move(ticket));std::shared_ptr<const webscene_gpu_image_lease_v3> image;
        while(!(image=snapshot.resolve_with_gpu_waits())&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(bool(image),"Producer handoff failed");
        auto consumed=image->value.begin_consumer();check(bool(consumed),"Image consumer admission failed");
        webscene_gpu_image_consumer_v3 consumer(std::move(*consumed),image->dependencies);
        std::unique_ptr<d3d11_scene_consumer> bridge;
        auto status=d3d11_scene_consumer::create(&consumer,host.Get(),bridge);
        if(FAILED(status)){consumer.value.complete();std::cerr<<"import HRESULT 0x"<<std::hex<<status<<'\n';throw std::runtime_error("D3D11 shared resource/fence import failed");}
        const bool unsealed_pending=bridge->poll()==E_PENDING;
        const bool consumer_retained=provider.inspect_occupancy().consumer_pending==1;
        adapter_luid wrong_adapter;check(SUCCEEDED(query_adapter_luid(host.Get(),wrong_adapter)),"Host LUID unavailable");
        wrong_adapter.low^=1;
        bool rejected_foreign_adapter=false;
        try {d3d12_canvas_images::resolve(consumer.value,wrong_adapter);}
        catch(const std::invalid_argument&) {rejected_foreign_adapter=true;}
        D3D11_TEXTURE2D_DESC stagingDesc;bridge->texture()->GetDesc(&stagingDesc);
        stagingDesc.Usage=D3D11_USAGE_STAGING;stagingDesc.BindFlags=0;stagingDesc.MiscFlags=0;stagingDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;check(SUCCEEDED(host->CreateTexture2D(&stagingDesc,nullptr,&staging)),"Diagnostic staging allocation failed");
        context->CopyResource(staging.Get(),bridge->texture());check(SUCCEEDED(bridge->seal()),"Consumer signal failed");
        while((status=bridge->poll())==S_FALSE&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(status==S_OK,"Consumer completion failed");
        D3D11_MAPPED_SUBRESOURCE pixels{};check(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&pixels)),"Diagnostic map failed");
        bool correct=true;
        {auto p=static_cast<unsigned char*>(pixels.pData);std::cerr<<"First BGRA pixel: "<<unsigned(p[0])<<','<<unsigned(p[1])<<','<<unsigned(p[2])<<','<<unsigned(p[3])<<"; pitch "<<pixels.RowPitch<<'\n';}
        for(unsigned y=0;y<4;++y)for(unsigned x=0;x<17;++x){auto p=static_cast<unsigned char*>(pixels.pData)+y*pixels.RowPitch+x*4;
            correct &= p[0]==192 && p[1]==128 && p[2]==64 && p[3]==255;}
        context->Unmap(staging.Get(),0);bridge.reset();consumer.value.complete();check(correct,"Shared pixels differ");
        check(unsealed_pending,"Unsealed reads must not report completion");
        check(consumer_retained&&provider.inspect_occupancy().consumer_pending==0,"Consumer ownership was not retained until completion");
        check(rejected_foreign_adapter,"Cross-adapter image was accepted");
        std::cout<<"{\"probe\":\"windows-dawn-d3d11-sharing\",\"status\":\"passed\",\"pixelsVerified\":68,\"producerBackend\":\"D3D12\",\"consumerBackend\":\"D3D11\",\"adapterLuidLow\":"<<luid.adapterLUID.LowPart
            <<",\"adapterLuidHigh\":"<<luid.adapterLUID.HighPart<<",\"presentationCpuCopyBytes\":0,\"diagnosticReadbackBytes\":272}\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
