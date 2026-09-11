#pragma once
#include "graphics/dawn_iosurface_submission.h"
#include "graphics/dawn_iosurface_canvas_host.h"
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
    wgpu::TextureDescriptor description{};description.dimension=wgpu::TextureDimension::e2D;
    description.size={frame.metadata.width,frame.metadata.height,1};description.format=wgpu::TextureFormat::BGRA8Unorm;description.usage=wgpu::TextureUsage::RenderAttachment;
    auto mismatch=description;mismatch.size.width++;
    bool mismatch_rejected=false;
    try{import_dawn_iosurface_canvas_texture(frame,*device,mismatch);}
    catch(const std::invalid_argument&){mismatch_rejected=true;}
    if(!mismatch_rejected)return {};
    auto shared=import_dawn_iosurface_canvas_texture(frame,*device,description);
    if(shared&&(shared->texture().GetUsage()!=description.usage||
        !shared->matches(*device,frame.color->borrowed_handle())))return {};
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
    auto captured=submitted ? submitted->capture_snapshot() : nullptr;
    if(!captured)return {};
    const auto captured_metadata=captured->describe();
    // EndAccess's local output has been destroyed. Captured ownership must retain
    // every exported Metal event/value, independently of callback completion.
    const auto& handoff=captured->producer_handoff();
    if(!handoff.initialized || !handoff.fenceCount ||
        handoff.fenceCount!=handoff.signaledValueCount) return {};
    for(size_t i=0;i<handoff.fenceCount;++i) {
        wgpu::SharedFenceMTLSharedEventExportInfo metal;
        wgpu::SharedFenceExportInfo info;info.nextInChain=&metal;
        handoff.fences[i].ExportInfo(&info);
        if(info.type!=wgpu::SharedFenceType::MTLSharedEvent || !metal.sharedEvent) return {};
    }

    if (!submitted || !wait(submitted->completion_future()) || !wait(submitted->validation_future()) ||
        (ready_wake->count.load()<1 || ready_wake->count.load()>2) || error->load()) return {};
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
    // Unconfigure/resize must retire submitted work without producing a scene
    // lease. Even an uninitialized current texture is safe to discard.
    for(bool submit_work:{false,true}) {
    auto discarded_frame=rejected.acquire(frame.metadata);
    if(!discarded_frame)return {};
    auto discarded_shared=import_dawn_iosurface_canvas_texture(*discarded_frame,*device,description);
    if(!discarded_shared||!discarded_shared->begin(access))return {};
    if(submit_work) {
        auto discard_encoder=device->CreateCommandEncoder();
        wgpu::RenderPassColorAttachment discard_color{};discard_color.view=discarded_shared->texture().CreateView();
        discard_color.loadOp=wgpu::LoadOp::Clear;discard_color.storeOp=wgpu::StoreOp::Store;discard_color.clearValue={1,0,1,1};
        wgpu::RenderPassDescriptor discard_pass{};discard_pass.colorAttachmentCount=1;discard_pass.colorAttachments=&discard_color;
        auto discard_recording=discard_encoder.BeginRenderPass(&discard_pass);discard_recording.End();
        auto discard_commands=discard_encoder.Finish();device->GetQueue().Submit(1,&discard_commands);
    }
    auto discarded_wake=std::make_shared<counted_wake>();
    auto discarded=dawn_iosurface_submission::publish_submitted(std::move(*discarded_frame),*device,discarded_shared,storage,discarded_wake,false);
    discarded_frame.reset();
    if(!discarded||!wait(discarded->completion_future())||!wait(discarded->validation_future())||
        discarded->state()!=dawn_iosurface_submission::status::discarded||discarded->take_ready()||
        discarded_wake->count.load()!=1||rejected.busy_images()!=0||error->load())return {};
    if(discarded_shared->begin(access)||!discarded_shared->expire_texture())return {};
    }
    dawn_iosurface_canvas_host canvas_provider(1024*1024);
    auto host_texture=canvas_provider.acquire(frame.metadata,*device,description,storage);
    if(!host_texture)return {};
    bool duplicate_acquire=false;
    try{canvas_provider.acquire(frame.metadata,*device,description,storage);}
    catch(const std::logic_error&){duplicate_acquire=true;}
    if(!duplicate_acquire)return {};
    bool foreign_retirement=false;
    try{canvas_provider.retire(expired_texture,false);}
    catch(const std::invalid_argument&){foreign_retirement=true;}
    if(!foreign_retirement)return {};
    canvas_provider.retire(host_texture,false);
    if(canvas_provider.capture_latest_submission())return {};
    auto host_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!canvas_provider.idle()&&std::chrono::steady_clock::now()<host_deadline) {
        instance.ProcessEvents();std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if(!canvas_provider.idle()||canvas_provider.take_ready()||canvas_provider.busy_images()!=0)return {};
    auto image=submitted->take_ready();
    if (submitted->take_ready()) return {}; // A publication transfers once.
    if(!image)return {};
    if(image->describe().allocation!=captured_metadata.allocation ||
        image->describe().content_serial!=captured_metadata.content_serial)return {};
    image.reset();
    // Draining the ordinary provider reference must not destroy a frozen
    // scene's exact output. Its completion gate still resolves independently.
    auto captured_image=captured->take_ready();
    if(!captured_image || captured->take_ready() ||
        captured_image->describe().allocation!=captured_metadata.allocation ||
        captured_image->describe().content_serial!=captured_metadata.content_serial)return {};
    captured_image.reset();captured.reset();
    host_texture=canvas_provider.acquire(frame.metadata,*device,description,storage);
    if(!host_texture)return {};
    auto host_encoder=device->CreateCommandEncoder();
    color.view=host_texture.CreateView();
    auto host_recording=host_encoder.BeginRenderPass(&pass);host_recording.End();
    auto host_commands=host_encoder.Finish();device->GetQueue().Submit(1,&host_commands);
    canvas_provider.retire(host_texture,true);
    auto provider_capture=canvas_provider.capture_latest_submission();
    if(!provider_capture)return {};
    const auto provider_metadata=provider_capture->describe();
    host_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    do {
        auto ready=canvas_provider.take_ready();
        if(ready) {
            auto captured_ready=provider_capture->take_ready();
            if(!captured_ready || provider_capture->take_ready() ||
                captured_ready->describe().allocation!=ready->describe().allocation ||
                captured_ready->describe().content_serial!=provider_metadata.content_serial)return {};
            ready.reset();
            return captured_ready;
        }
        instance.ProcessEvents();std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }while(std::chrono::steady_clock::now()<host_deadline);
    return {};
}
