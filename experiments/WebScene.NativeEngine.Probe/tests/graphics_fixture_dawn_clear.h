#pragma once
#include "graphics/dawn_iosurface_submission.h"
#include <IOSurface/IOSurface.h>
#include <atomic>

// Synchronous diagnostic producer only; never part of ordinary presentation.
inline std::optional<webscene::graphics::owned_image_pool::retained> fixture_dawn_clear(
    webscene::graphics::iosurface_canvas_images::frame&& frame) {
    constexpr auto feature=wgpu::InstanceFeatureName::TimedWaitAny;
    wgpu::InstanceDescriptor instanceDescription{};
    instanceDescription.requiredFeatureCount=1;
    instanceDescription.requiredFeatures=&feature;
    auto instance=wgpu::CreateInstance(&instanceDescription);
    if (!instance) return {};
    auto wait=[&](wgpu::Future future) {
        return instance.WaitAny(future,30'000'000'000ULL)==wgpu::WaitStatus::Success;
    };
    auto adapter=std::make_shared<wgpu::Adapter>();
    wgpu::RequestAdapterOptions options{}; options.backendType=wgpu::BackendType::Metal;
    if (!wait(instance.RequestAdapter(&options,wgpu::CallbackMode::WaitAnyOnly,
        [adapter](wgpu::RequestAdapterStatus status,wgpu::Adapter value,wgpu::StringView) {
            if (status==wgpu::RequestAdapterStatus::Success) *adapter=std::move(value);
        })) || !*adapter) return {};
    wgpu::AdapterInfo info{};
    if (adapter->GetInfo(&info)!=wgpu::Status::Success ||
        (info.adapterType!=wgpu::AdapterType::IntegratedGPU && info.adapterType!=wgpu::AdapterType::DiscreteGPU))
        return {};
    const wgpu::FeatureName features[]={wgpu::FeatureName::SharedTextureMemoryIOSurface,
        wgpu::FeatureName::SharedFenceMTLSharedEvent};
    for (auto required:features) if (!adapter->HasFeature(required)) return {};
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
        })) || !*device) return {};
    // Recording rejection must release its slot without ever submitting GPU work.
    using namespace webscene::graphics;
    iosurface_canvas_images rejected(1024*1024);
    auto empty=rejected.acquire(frame.metadata);
    if (!empty || dawn_iosurface_submission::submit(std::move(*empty),*device,
        [](const wgpu::Texture&) { return wgpu::CommandBuffer{}; },storage)) return {};
    empty.reset();
    if (rejected.busy_images()!=0) return {};
    auto throwing=rejected.acquire(frame.metadata);
    if (!throwing) return {};
    bool caught=false;
    try {
        dawn_iosurface_submission::submit(std::move(*throwing),*device,
            [](const wgpu::Texture&) -> wgpu::CommandBuffer { throw std::runtime_error("recording rejected"); },storage);
    } catch (const std::runtime_error&) { caught=true; }
    throwing.reset();
    if (!caught || rejected.busy_images()!=0) return {};
    struct counted_wake final : completion_wake {
        std::atomic<unsigned> count{0};
        void signal() noexcept override { ++count; }
    };
    auto invalid_frame=rejected.acquire(frame.metadata);
    if (!invalid_frame) return {};
    auto invalid_wake=std::make_shared<counted_wake>();
    auto invalid=dawn_iosurface_submission::submit(std::move(*invalid_frame),*device,
        [&](const wgpu::Texture&) {
            wgpu::BufferDescriptor bad{}; bad.size=4; bad.usage=wgpu::BufferUsage::None;
            auto invalid_buffer=device->CreateBuffer(&bad);
            return device->CreateCommandEncoder().Finish();
        },storage,invalid_wake);
    invalid_frame.reset();
    if (!invalid || !wait(invalid->completion_future()) || !wait(invalid->validation_future()) ||
        invalid->state()!=dawn_iosurface_submission::status::failed || invalid->take_ready() ||
        invalid_wake->count.load()!=1 || rejected.busy_images()!=0) return {};
    auto ready_wake=std::make_shared<counted_wake>();
    auto submitted=webscene::graphics::dawn_iosurface_submission::submit(std::move(frame),*device,
        [&](const wgpu::Texture& texture) {
            auto encoder=device->CreateCommandEncoder();
            wgpu::RenderPassColorAttachment color{};
            color.view=texture.CreateView();
            color.loadOp=wgpu::LoadOp::Clear; color.storeOp=wgpu::StoreOp::Store;
            color.clearValue={0.2,0.4,0.6,1};
            wgpu::RenderPassDescriptor pass{}; pass.colorAttachmentCount=1; pass.colorAttachments=&color;
            auto recording=encoder.BeginRenderPass(&pass); recording.End();
            return encoder.Finish();
        },storage,ready_wake);
    if (!submitted || !wait(submitted->completion_future()) || !wait(submitted->validation_future()) ||
        ready_wake->count.load()!=1 || error->load()) return {};
    auto image=submitted->take_ready();
    if (submitted->take_ready()) return {}; // A publication transfers once.
    return image;
}
