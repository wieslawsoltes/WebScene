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
    wgpu::Texture expired_texture;
    auto surface=frame.color->borrowed_handle();
    std::shared_ptr<void> owner(const_cast<void*>(CFRetain(surface)),[](void* value){CFRelease(value);});
    wgpu::SharedTextureMemoryIOSurfaceDescriptor io{};io.ioSurface=surface;
    wgpu::SharedTextureMemoryDescriptor import{};import.nextInChain=&io;
    wgpu::TextureDescriptor description{};description.dimension=wgpu::TextureDimension::e2D;
    description.size={frame.metadata.width,frame.metadata.height,1};description.format=wgpu::TextureFormat::BGRA8Unorm;description.usage=wgpu::TextureUsage::RenderAttachment;
    auto shared=dawn_shared_image::import(*device,import,description,std::move(owner));
    wgpu::SharedTextureMemoryBeginAccessDescriptor access{};access.initialized=false;
    if(!shared||!shared->begin(access))return {};
    expired_texture=shared->texture();
    auto encoder=device->CreateCommandEncoder();
    wgpu::RenderPassColorAttachment color{};color.view=shared->texture().CreateView();
    color.loadOp=wgpu::LoadOp::Clear;color.storeOp=wgpu::StoreOp::Store;color.clearValue={0.2,0.4,0.6,1};
    wgpu::RenderPassDescriptor pass{};pass.colorAttachmentCount=1;pass.colorAttachments=&color;
    auto recording=encoder.BeginRenderPass(&pass);recording.End();auto commands=encoder.Finish();
    // Model application-owned submission: handoff must not submit this again.
    auto foreign=rejected.acquire(frame.metadata);if(!foreign)return {};
    bool foreign_rejected=false;
    try{dawn_iosurface_submission::publish_submitted(std::move(*foreign),*device,shared,storage);}
    catch(const std::invalid_argument&){foreign_rejected=true;}
    if(!foreign_rejected||!foreign->color)return {};foreign.reset();
    device->GetQueue().Submit(1,&commands);
    auto submitted=dawn_iosurface_submission::publish_submitted(std::move(frame),*device,shared,storage,ready_wake);
    if (!submitted || !wait(submitted->completion_future()) || !wait(submitted->validation_future()) ||
        ready_wake->count.load()!=1 || error->load()) return {};
    if(shared->begin(access)||!shared->expire_texture())return {};
    device->PushErrorScope(wgpu::ErrorFilter::Validation);
    auto invalid_view=expired_texture.CreateView();
    auto expired_encoder=device->CreateCommandEncoder();
    wgpu::RenderPassColorAttachment expired_attachment{};expired_attachment.view=invalid_view;
    expired_attachment.loadOp=wgpu::LoadOp::Clear;expired_attachment.storeOp=wgpu::StoreOp::Store;
    expired_attachment.clearValue={1,0,1,1};
    wgpu::RenderPassDescriptor expired_pass{};expired_pass.colorAttachmentCount=1;expired_pass.colorAttachments=&expired_attachment;
    auto expired_recording=expired_encoder.BeginRenderPass(&expired_pass);expired_recording.End();
    auto expired_commands=expired_encoder.Finish();device->GetQueue().Submit(1,&expired_commands);
    auto expired_rejected=std::make_shared<std::atomic<bool>>(false);
    if(!wait(device->PopErrorScope(wgpu::CallbackMode::WaitAnyOnly,
        [expired_rejected](wgpu::PopErrorScopeStatus status,wgpu::ErrorType type,wgpu::StringView) {
            expired_rejected->store(status==wgpu::PopErrorScopeStatus::Success && type==wgpu::ErrorType::Validation);
        })) || !expired_rejected->load())return {};
    auto image=submitted->take_ready();
    if (submitted->take_ready()) return {}; // A publication transfers once.
    return image;
}
