#pragma once
#include "graphics/dawn_shared_image.h"
#include <IOSurface/IOSurface.h>
#include <atomic>

// Synchronous diagnostic producer only; never part of ordinary presentation.
inline bool fixture_dawn_clear(IOSurfaceRef surface) {
    constexpr auto feature=wgpu::InstanceFeatureName::TimedWaitAny;
    wgpu::InstanceDescriptor instanceDescription{};
    instanceDescription.requiredFeatureCount=1;
    instanceDescription.requiredFeatures=&feature;
    auto instance=wgpu::CreateInstance(&instanceDescription);
    if (!instance) return false;
    auto wait=[&](wgpu::Future future) {
        return instance.WaitAny(future,30'000'000'000ULL)==wgpu::WaitStatus::Success;
    };
    auto adapter=std::make_shared<wgpu::Adapter>();
    wgpu::RequestAdapterOptions options{}; options.backendType=wgpu::BackendType::Metal;
    if (!wait(instance.RequestAdapter(&options,wgpu::CallbackMode::WaitAnyOnly,
        [adapter](wgpu::RequestAdapterStatus status,wgpu::Adapter value,wgpu::StringView) {
            if (status==wgpu::RequestAdapterStatus::Success) *adapter=std::move(value);
        })) || !*adapter) return false;
    wgpu::AdapterInfo info{};
    if (adapter->GetInfo(&info)!=wgpu::Status::Success ||
        (info.adapterType!=wgpu::AdapterType::IntegratedGPU && info.adapterType!=wgpu::AdapterType::DiscreteGPU))
        return false;
    const wgpu::FeatureName features[]={wgpu::FeatureName::SharedTextureMemoryIOSurface,
        wgpu::FeatureName::SharedFenceMTLSharedEvent};
    for (auto required:features) if (!adapter->HasFeature(required)) return false;
    auto error=std::make_shared<std::atomic<bool>>(false);
    wgpu::DeviceDescriptor deviceDescription{};
    deviceDescription.requiredFeatureCount=2; deviceDescription.requiredFeatures=features;
    deviceDescription.SetUncapturedErrorCallback(
        [](const wgpu::Device&,wgpu::ErrorType,wgpu::StringView,std::atomic<bool>* state) { state->store(true); },
        error.get());
    struct device_storage { std::shared_ptr<std::atomic<bool>> error; wgpu::Device device; };
    auto storage=std::make_shared<device_storage>(device_storage{error,{}});
    auto device=std::shared_ptr<wgpu::Device>(storage,&storage->device);
    if (!wait(adapter->RequestDevice(&deviceDescription,wgpu::CallbackMode::WaitAnyOnly,
        [device](wgpu::RequestDeviceStatus status,wgpu::Device value,wgpu::StringView) {
            if (status==wgpu::RequestDeviceStatus::Success) *device=std::move(value);
        })) || !*device) return false;
    std::shared_ptr<void> owner(const_cast<void*>(CFRetain(surface)),[](void* p) { CFRelease(p); });
    wgpu::SharedTextureMemoryIOSurfaceDescriptor io{}; io.ioSurface=surface;
    wgpu::SharedTextureMemoryDescriptor import{}; import.nextInChain=&io;
    wgpu::TextureDescriptor texture{};
    texture.dimension=wgpu::TextureDimension::e2D; texture.size={17,4,1};
    texture.format=wgpu::TextureFormat::BGRA8Unorm;
    texture.usage=wgpu::TextureUsage::RenderAttachment;
    auto shared=webscene::graphics::dawn_shared_image::import(*device,import,texture,owner);
    if (!shared) return false;
    wgpu::SharedTextureMemoryBeginAccessDescriptor access{}; access.initialized=false;
    if (!shared->begin(access)) return false;
    auto encoder=device->CreateCommandEncoder();
    wgpu::RenderPassColorAttachment color{};
    color.view=shared->texture().CreateView();
    color.loadOp=wgpu::LoadOp::Clear; color.storeOp=wgpu::StoreOp::Store;
    color.clearValue={0.2,0.4,0.6,1};
    wgpu::RenderPassDescriptor pass{}; pass.colorAttachmentCount=1; pass.colorAttachments=&color;
    auto recording=encoder.BeginRenderPass(&pass); recording.End();
    auto command=encoder.Finish();
    auto queue=device->GetQueue(); queue.Submit(1,&command);
    wgpu::SharedTextureMemoryEndAccessState handoff;
    const bool ended=shared->end(handoff);
    auto done=std::make_shared<std::atomic<int>>(0);
    // Keep native import/device ownership alive even if the diagnostic wait times out.
    auto future=queue.OnSubmittedWorkDone(wgpu::CallbackMode::AllowSpontaneous,
        [done,shared,device,error](wgpu::QueueWorkDoneStatus status,wgpu::StringView) {
            done->store(status==wgpu::QueueWorkDoneStatus::Success && !error->load() ? 1 : -1);
        });
    return wait(future) && ended && done->load()==1;
}
