#include "graphics/graphics_service.h"
#include "graphics/webgpu_adapter_options.h"
#include "graphics/webgpu_feature_names.h"
#include "graphics/webgpu_buffer_descriptor.h"
#include "graphics/engine_wake.h"
#include "graphics/dawn_canvas_images.h"
#include "graphics/dawn_dxgi_image.h"
#include <iostream>
#include <set>
using namespace webscene::graphics;
// Synthetic exports exercise atomic handle ownership, not native fence signaling.
struct fence_test_ops {
    using handle_type=int;
    static inline std::set<int> live;
    static inline int next=100,duplicate_calls=0,fail_at=0;
    static int empty() { return 0; }
    static bool valid(int value) { return value>0; }
    static int duplicate(int) {
        if (++duplicate_calls==fail_at) throw std::system_error(std::make_error_code(std::errc::too_many_files_open));
        live.insert(next); return next++;
    }
    static void close(int value) noexcept { if (live.erase(value)!=1) std::terminate(); }
};
void test_dxgi_fence_ownership() {
    auto require=[](bool value) { if (!value) throw std::runtime_error("DXGI fence handoff ownership failed"); };
    std::array<wgpu::SharedFence,2> fences{};
    std::array<uint64_t,2> values{7,UINT64_MAX};
    std::vector<dxgi_fence_wait<fence_test_ops>> output;
    auto exported=[](const wgpu::SharedFence&,int& handle) { handle=42; return true; };
    require(duplicate_dxgi_fences<fence_test_ops>(fences,values,output,exported)==dxgi_fence_status::success);
    require(output.size()==2 && output[0].value==7 && output[1].value==UINT64_MAX
        && output[0].handle.get()!=output[1].handle.get() && fence_test_ops::live.size()==2);
    // A replacement failure must discard old output and roll back earlier duplicates.
    fence_test_ops::duplicate_calls=0; fence_test_ops::fail_at=2;
    require(duplicate_dxgi_fences<fence_test_ops>(fences,values,output,exported)==dxgi_fence_status::handle_failure);
    require(output.empty() && fence_test_ops::live.empty());
    fence_test_ops::fail_at=0;
    int calls=0;
    auto unsupported=[&](const wgpu::SharedFence&,int& handle) { handle=42; return ++calls!=2; };
    require(duplicate_dxgi_fences<fence_test_ops>(fences,values,output,unsupported)==dxgi_fence_status::unsupported_fence);
    require(output.empty() && fence_test_ops::live.empty());
    require(duplicate_dxgi_fences<fence_test_ops>(fences,{},output,exported)==dxgi_fence_status::invalid_argument);
    require(duplicate_dxgi_fences<fence_test_ops>({}, {},output,exported)==dxgi_fence_status::success);
    require(output.empty() && fence_test_ops::live.empty());
}
void test_canvas_consumer_pixels(dawn_event_service& service,const wgpu::Device& device) {
    auto pool=std::make_unique<dawn_canvas_images>(device,2*64*64*4);
    auto frame=pool->acquire(image_metadata{300,0,1,1,400,1,64,64});
    auto queue=device.GetQueue();
    auto encoder=device.CreateCommandEncoder();
    wgpu::RenderPassColorAttachment color{};
    color.view=frame->texture.CreateView(); color.loadOp=wgpu::LoadOp::Clear;
    color.storeOp=wgpu::StoreOp::Store; color.clearValue={0.25,0.5,0.75,1};
    wgpu::RenderPassDescriptor pass{}; pass.colorAttachmentCount=1; pass.colorAttachments=&color;
    auto render=encoder.BeginRenderPass(&pass); render.End();
    auto producer_commands=encoder.Finish();
    auto submitted=pool->submit(std::move(*frame),producer_commands); frame.reset();
    if (!submitted) throw std::runtime_error("canvas submission unexpectedly saturated");
    auto producer_status=submitted->status;
    auto consumer=submitted->image.begin_consumer();
    auto resized=pool->acquire(image_metadata{300,0,2,2,400,2,32,32});
    auto resized_encoder=device.CreateCommandEncoder();
    wgpu::RenderPassColorAttachment resized_color{};
    resized_color.view=resized->texture.CreateView(); resized_color.loadOp=wgpu::LoadOp::Clear;
    resized_color.storeOp=wgpu::StoreOp::Store; resized_color.clearValue={1,0,0,1};
    wgpu::RenderPassDescriptor resized_pass{}; resized_pass.colorAttachmentCount=1; resized_pass.colorAttachments=&resized_color;
    auto resized_render=resized_encoder.BeginRenderPass(&resized_pass); resized_render.End();
    auto resized_commands=resized_encoder.Finish();
    auto newer=pool->submit(std::move(*resized),resized_commands); resized.reset();
    if (!newer || newer->image.describe().allocation==submitted->image.describe().allocation
        || newer->image.describe().allocation_generation!=2 || consumer->describe().width!=64)
        throw std::runtime_error("resize reused a retained image allocation");
    auto resized_status=newer->status;
    auto resized_consumer=newer->image.begin_consumer();
    // Submit to the same queue before processing any completion. Queue order,
    // rather than a CPU wait or a pixel upload, makes producer writes visible.
    auto source_texture=dawn_canvas_images::resolve(*consumer,device);
    wgpu::BufferDescriptor buffer_descriptor{};
    buffer_descriptor.size=(64+32)*256;
    buffer_descriptor.usage=wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
    auto readback=device.CreateBuffer(&buffer_descriptor);
    auto copy=device.CreateCommandEncoder();
    wgpu::TexelCopyTextureInfo source{}; source.texture=source_texture;
    wgpu::TexelCopyBufferInfo destination{}; destination.buffer=readback;
    destination.layout.bytesPerRow=256; destination.layout.rowsPerImage=64;
    wgpu::Extent3D extent{64,64,1};
    copy.CopyTextureToBuffer(&source,&destination,&extent);
    source.texture=dawn_canvas_images::resolve(*resized_consumer,device);
    destination.layout.offset=64*256; destination.layout.rowsPerImage=32;
    extent={32,32,1}; copy.CopyTextureToBuffer(&source,&destination,&extent);
    auto consumer_commands=copy.Finish(); queue.Submit(1,&consumer_commands);
    submitted.reset(); newer.reset(); pool.reset();
    bool completed=false,queue_success=false,mapped=false,map_success=false;
    queue.OnSubmittedWorkDone(wgpu::CallbackMode::AllowProcessEvents,
        [&](wgpu::QueueWorkDoneStatus status,wgpu::StringView) {
            consumer->complete(); consumer.reset();
            resized_consumer->complete(); resized_consumer.reset();
            queue_success=status==wgpu::QueueWorkDoneStatus::Success; completed=true;
        });
    readback.MapAsync(wgpu::MapMode::Read,0,(64+32)*256,wgpu::CallbackMode::AllowProcessEvents,
        [&](wgpu::MapAsyncStatus status,wgpu::StringView) { map_success=status==wgpu::MapAsyncStatus::Success; mapped=true; });
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while ((!completed || !mapped || producer_status->load()==dawn_canvas_images::submission_status::pending
        || resized_status->load()==dawn_canvas_images::submission_status::pending) && std::chrono::steady_clock::now()<deadline) {
        service.instance().ProcessEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!completed || !mapped || !queue_success || !map_success
        || producer_status->load()!=dawn_canvas_images::submission_status::success
        || resized_status->load()!=dawn_canvas_images::submission_status::success)
        throw std::runtime_error("GPU image consumer completion failed");
    const auto* pixels=static_cast<const uint8_t*>(readback.GetConstMappedRange(0,(64+32)*256));
    if (!pixels) throw std::runtime_error("GPU image diagnostic mapping failed");
    for (size_t i=0;i<64*64;++i) {
        if (pixels[4*i]!=64 || pixels[4*i+1]!=128 || pixels[4*i+2]!=191 || pixels[4*i+3]!=255)
            throw std::runtime_error("retained GPU image pixels differ from producer clear");
    }
    for (size_t y=0;y<32;++y) for (size_t x=0;x<32;++x) {
        const auto* pixel=pixels+64*256+y*256+x*4;
        if (pixel[0]!=255 || pixel[1]!=0 || pixel[2]!=0 || pixel[3]!=255)
            throw std::runtime_error("resized GPU image pixels differ from new producer clear");
    }
    readback.Unmap();
}
void test_canvas_storage(dawn_event_service& service,const wgpu::Device& device,const wgpu::Device& foreign_device) {
    auto require=[](bool v) { if (!v) throw std::runtime_error("Dawn canvas storage requirement failed"); };
    dawn_canvas_images images(device,3*128*128*4);
    image_metadata m{100,0,1,1,200,1,64,64};
    std::vector<dawn_canvas_images::frame> frames;
    for (int i=0;i<3;++i) frames.push_back(std::move(images.acquire(m).value()));
    require(!images.acquire(m) && images.created_images()==3 && images.resident_bytes()==3*64*64*4);
    auto encoder=device.CreateCommandEncoder();
    std::vector<owned_image_pool::retained> retained;
    for (auto& frame:frames) {
        frame.producer.begin();
        wgpu::RenderPassColorAttachment attachment{};
        attachment.view=frame.texture.CreateView();
        attachment.loadOp=wgpu::LoadOp::Clear; attachment.storeOp=wgpu::StoreOp::Store;
        attachment.clearValue={0.25,0.5,0.75,1};
        wgpu::RenderPassDescriptor pass{}; pass.colorAttachmentCount=1; pass.colorAttachments=&attachment;
        auto render=encoder.BeginRenderPass(&pass); render.End();
        retained.push_back(std::move(frame.producer.publish().value()));
    }
    auto commands=encoder.Finish(); auto queue=device.GetQueue(); queue.Submit(1,&commands);
    bool complete=false,success=false;
    queue.OnSubmittedWorkDone(wgpu::CallbackMode::AllowProcessEvents,
        [&](wgpu::QueueWorkDoneStatus status,wgpu::StringView) {
            for (auto& frame:frames) frame.producer.complete();
            success=status==wgpu::QueueWorkDoneStatus::Success; complete=true;
        });
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while (!complete && std::chrono::steady_clock::now()<deadline) {
        service.instance().ProcessEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(complete && success && !images.acquire(m));
    frames.clear(); retained.clear(); require(images.busy_images()==0);
    for (int i=0;i<100;++i) {
        ++m.content_serial;
        auto frame=images.acquire(m);
        require(frame && frame->metadata.allocation!=0);
        // Metadata-only acquisition/cancellation performs no GPU submission.
    }
    require(images.created_images()==3 && images.resident_bytes()==3*64*64*4);
    m.width=128; m.height=128; ++m.allocation_generation;
    { auto resized=images.acquire(m); require(resized && images.created_images()==4); }
    require(images.resident_bytes()==128*128*4+2*64*64*4);
    m.width=512; m.height=512;
    require(!images.acquire(m) && images.created_images()==4);
    images.close(); require(!images.acquire(image_metadata{100,0,1,200,200,2,64,64}));
    // A retained image must block an over-budget resize, but idle cache entries
    // must not permanently strand it after that retained image is released.
    auto capacity_signal=std::make_shared<engine_wake>();
    dawn_canvas_images tight(device,3*64*64*4,128,capacity_signal);
    auto small=image_metadata{500,0,1,1,600,1,64,64};
    std::vector<dawn_canvas_images::frame> cached;
    for (int i=0;i<3;++i) cached.push_back(std::move(tight.acquire(small).value()));
    cached[1].producer.begin(); auto busy=cached[1].producer.publish();
    cached[1].producer.complete(); cached.clear();
    auto large=small; large.width=96; large.height=96; ++large.allocation_generation;
    capacity_signal->wait_for(std::chrono::milliseconds(0),[] { return false; });
    require(!tight.acquire(large) && tight.created_images()==3);
    require(!capacity_signal->wait_for(std::chrono::milliseconds(0),[] { return false; }));
    require(busy->describe().width==64 && tight.busy_images()==1);
    busy.reset();
    auto replacement=tight.acquire(large);
    require(replacement && tight.created_images()==4 && tight.resident_bytes()==96*96*4);
    replacement.reset(); require(tight.busy_images()==0);
    dawn_canvas_images admission(device,2*64*64*4,1,capacity_signal);
    auto first=admission.acquire(small);
    auto empty_commands=device.CreateCommandEncoder().Finish();
    auto accepted=admission.submit(std::move(*first),empty_commands); first.reset();
    require(accepted.has_value());
    auto refused=admission.acquire(small);
    require(refused.has_value());
    bool foreign_submission=false;
    try { images.submit(std::move(*refused),empty_commands); }
    catch (const std::invalid_argument&) { foreign_submission=true; }
    require(foreign_submission); // Rejection must preserve the caller's frame.
    capacity_signal->wait_for(std::chrono::milliseconds(0),[] { return false; });
    require(!admission.submit(std::move(*refused),empty_commands)); refused.reset();
    require(admission.busy_images()==1);
    require(!capacity_signal->wait_for(std::chrono::milliseconds(0),[] { return false; }));
    const auto accepted_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while (accepted->status->load()==dawn_canvas_images::submission_status::pending
        && std::chrono::steady_clock::now()<accepted_deadline) {
        service.instance().ProcessEvents(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(accepted->status->load()==dawn_canvas_images::submission_status::success);
    accepted.reset(); require(admission.busy_images()==0);
    auto detached=std::make_unique<dawn_canvas_images>(device,64*64*4);
    auto frame=detached->acquire(image_metadata{101,0,1,1,200,3,64,64});
    const auto native_identity=frame->texture.Get();
    frame->producer.begin(); auto scene=frame->producer.publish();
    frame->producer.complete(); frame.reset();
    auto consumer=scene->begin_consumer();
    auto anchor=std::weak_ptr<image_provider_lifetime>(consumer->provider());
    detached.reset(); scene.reset();
    require(!anchor.expired());
    bool rejected=false;
    try { dawn_canvas_images::resolve(*consumer,foreign_device); }
    catch (const std::invalid_argument&) { rejected=true; }
    require(rejected);
    std::thread presenter([&] {
        { auto texture=dawn_canvas_images::resolve(*consumer,device);
          require(texture.Get()==native_identity && texture.GetWidth()==64 && texture.CreateView()); }
        consumer->complete();
        bool stale=false;
        try { dawn_canvas_images::resolve(*consumer,device); }
        catch (const std::invalid_argument&) { stale=true; }
        require(stale);
    });
    presenter.join(); consumer.reset(); require(anchor.expired());
}
int main() {
    test_dxgi_fence_ownership();
    auto wake=std::make_shared<engine_wake>();
    graphics_service root(wake),other_root(wake);
    auto& service=root.dawn();
    auto mailbox=service.completions();
    resource_owner owner{new_owner_token(),new_owner_token(),0};
    auto ticket=mailbox->reserve(1,owner).value();
    struct result { wgpu::Adapter adapter; };
    auto state=std::make_shared<result>();
    webgpu_adapter_options browser_options;
    auto converted=make_dawn_adapter_options(browser_options);
    if (!converted || converted->featureLevel!=wgpu::FeatureLevel::Core ||
        converted->backendType!=wgpu::BackendType::Undefined || converted->nextInChain ||
        converted->powerPreference!=wgpu::PowerPreference::Undefined || converted->forceFallbackAdapter)
        throw std::runtime_error("Default browser adapter selection changed");
    browser_options.feature_level=u"compatibility";
    browser_options.power_preference=wgpu::PowerPreference::LowPower;
    browser_options.force_fallback_adapter=true;
    auto fallback=make_dawn_adapter_options(browser_options);
    if (!fallback || fallback->featureLevel!=wgpu::FeatureLevel::Compatibility ||
        !fallback->forceFallbackAdapter || fallback->powerPreference!=wgpu::PowerPreference::LowPower)
        throw std::runtime_error("Browser adapter preferences were lost");
    browser_options.feature_level=u"unknown";
    if (make_dawn_adapter_options(browser_options)) throw std::runtime_error("Unknown feature level accepted");
    browser_options.feature_level=u"core"; browser_options.xr_compatible=true;
    if (make_dawn_adapter_options(browser_options)) throw std::runtime_error("Unsupported XR adapter accepted");
    // Exercise the converted browser defaults through actual asynchronous Dawn discovery.
    auto options=*converted;
#if defined(__APPLE__)
    options.backendType=wgpu::BackendType::Metal;
#elif defined(_WIN32)
    options.backendType=wgpu::BackendType::D3D12;
#else
    options.backendType=wgpu::BackendType::Vulkan;
#endif
    service.instance().RequestAdapter(&options,wgpu::CallbackMode::AllowProcessEvents,
        [mailbox,ticket,state](wgpu::RequestAdapterStatus status,wgpu::Adapter adapter,wgpu::StringView) {
            state->adapter=std::move(adapter);
            mailbox->publish(ticket,status==wgpu::RequestAdapterStatus::Success
                ? completion_status::success : completion_status::failed);
        });
    bool done=false, success=false;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while (!done && std::chrono::steady_clock::now()<deadline) {
        service.pump([&](auto record) { done=true; success=record.status==completion_status::success; });
        if (!done) wake->wait_for(std::chrono::milliseconds(1),[] { return false; });
    }
    if (!done) { std::cerr << "Dawn headless completion timed out\n"; return 1; }
    if (!success) { std::cerr << "Hardware adapter unavailable\n"; return 77; }
    wgpu::AdapterInfo info{};
    if (state->adapter.GetInfo(&info)!=wgpu::Status::Success) return 1;
    if (info.adapterType!=wgpu::AdapterType::IntegratedGPU && info.adapterType!=wgpu::AdapterType::DiscreteGPU) return 77;
    if (webgpu_feature_from_name("SharedTextureMemoryIOSurface")
        || webgpu_feature_from_name("shared-texture-memory-iosurface")
        || webgpu_feature_from_name("SHADER-F16")
        || webgpu_feature_to_name(wgpu::FeatureName::SharedTextureMemoryIOSurface)
        || webgpu_feature_to_name(wgpu::FeatureName::SharedFenceMTLSharedEvent)
        || webgpu_feature_to_name(static_cast<wgpu::FeatureName>(0xffffffff))) return 1;
    bool null_features=false;
    try { webgpu_supported_feature_names(wgpu::Adapter{}); }
    catch (const std::invalid_argument&) { null_features=true; }
    if (!null_features) return 1;
    const auto adapter_features=webgpu_supported_feature_names(state->adapter);
    std::set<std::string_view> feature_names;
    std::set<wgpu::FeatureName> native_features;
    for (const auto& feature:webgpu_feature_names) {
        if (!feature_names.insert(feature.name).second || !native_features.insert(feature.native).second
            || webgpu_feature_from_name(feature.name)!=feature.native
            || webgpu_feature_to_name(feature.native)!=feature.name
            || (std::find(adapter_features.begin(),adapter_features.end(),feature.name)!=adapter_features.end())
                !=state->adapter.HasFeature(feature.native)) return 1;
    }
    auto adapter_handle=root.adopt_adapter(state->adapter);
    bool foreign_adapter=false,active_destroy=false,active_close=false,stale_adapter=false;
    try { other_root.with_adapter(adapter_handle,[](const auto&) {}); }
    catch (const std::invalid_argument&) { foreign_adapter=true; }
    wgpu::Adapter retained_adapter;
    root.with_adapter(adapter_handle,[&](const auto& adapter) {
        retained_adapter=adapter; // In-flight requests own a reference independent of the wrapper.
        try { root.destroy_adapter(adapter_handle); }
        catch (const std::logic_error&) { active_destroy=true; }
        try { root.close(); }
        catch (const std::logic_error&) { active_close=true; }
    });
    root.destroy_adapter(adapter_handle);
    auto replacement_adapter=root.adopt_adapter(retained_adapter);
    try { root.with_adapter(adapter_handle,[](const auto&) {}); }
    catch (const std::invalid_argument&) { stale_adapter=true; }
    if (!foreign_adapter || !active_destroy || !active_close || !stale_adapter
        || replacement_adapter.slot!=adapter_handle.slot
        || replacement_adapter.generation==adapter_handle.generation
        || root.metrics().live_adapters!=1) return 1;
    auto adapter_release=graphics_service::deferred_adapter_release(replacement_adapter);
    auto adapter_commands=root.command_endpoint(2,0);
    std::thread adapter_finalizer([&] {
        if (adapter_commands->enqueue(adapter_release)!=enqueue_result::accepted
            || adapter_commands->enqueue(adapter_release)!=enqueue_result::accepted) std::terminate();
    });
    adapter_finalizer.join();
    root.drain_commands();
    if (root.live_adapters()!=0 || retained_adapter.GetInfo(&info)!=wgpu::Status::Success) return 1;
    bool null_adapter=false,adapter_limit=false;
    try { root.adopt_adapter({}); }
    catch (const std::invalid_argument&) { null_adapter=true; }
    std::vector<resource_handle<wgpu::Adapter>> bounded_adapters;
    for (size_t i=0;i<64;++i) bounded_adapters.push_back(root.adopt_adapter(retained_adapter));
    try { root.adopt_adapter(retained_adapter); }
    catch (const std::length_error&) { adapter_limit=true; }
    if (!null_adapter || !adapter_limit || root.live_adapters()!=64) return 1;
    for (auto handle:bounded_adapters) root.destroy_adapter(handle);
    if (root.live_adapters()!=0) return 1;
    retained_adapter=nullptr;
    struct device_result { wgpu::Device device; };
#if !defined(_WIN32)
    dawn_dxgi_image unopened;
    wgpu::SharedTextureMemoryEndAccessState handoff;
    if (unopened.begin(false)!=dxgi_access_status::invalid_state
        || unopened.end(handoff)!=dxgi_access_status::invalid_state || unopened.abandon_lost_device()) return 1;
    bool unopened_rejected=false;
    try { unopened.texture(); } catch (const std::logic_error&) { unopened_rejected=true; }
    if (!unopened_rejected) return 1;
    std::unique_ptr<dawn_dxgi_image> unsupported;
    if (dawn_dxgi_image::import({},nullptr,{},{},{},wgpu::TextureUsage::TextureBinding,unsupported)
        !=dxgi_import_status::unsupported_platform || unsupported) return 1;
#endif
    auto native_device=std::make_shared<device_result>();
    auto device_ticket=mailbox->reserve(10,owner).value();
    wgpu::DeviceDescriptor device_descriptor{};
    state->adapter.RequestDevice(&device_descriptor,wgpu::CallbackMode::AllowProcessEvents,
        [mailbox,device_ticket,native_device](wgpu::RequestDeviceStatus status,wgpu::Device device,wgpu::StringView) {
            native_device->device=std::move(device);
            mailbox->publish(device_ticket,status==wgpu::RequestDeviceStatus::Success
                ? completion_status::success : completion_status::failed);
        });
    const auto device_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    bool device_done=false;
    while (!device_done && std::chrono::steady_clock::now()<device_deadline) {
        service.pump([&](auto record) {
            if (record.operation!=10 || record.status!=completion_status::success)
                throw std::runtime_error("native device request failed");
            device_done=true;
        });
        if (!device_done) wake->wait_for(std::chrono::milliseconds(1),[] { return false; });
    }
    if (!device_done || !native_device->device) return 1;
    for (uint32_t bit=0;bit<32;++bit) {
        const auto usage=webgpu_buffer_usage(1u<<bit);
        if (usage.has_value()!=(bit<10)) return 1;
    }
    if (webgpu_buffer_usage(0x408) || webgpu_buffer_usage(0xffffffff)
        || webgpu_buffer_usage(0)!=wgpu::BufferUsage::None) return 1;
    webgpu_buffer_descriptor browser_buffer;
    browser_buffer.label=std::string("browser\0buffer",14);
    browser_buffer.size=64;
    browser_buffer.usage=0x6; // MAP_WRITE | COPY_SRC
    browser_buffer.mapped_at_creation=true;
    auto translated_buffer=make_dawn_buffer_descriptor(browser_buffer);
    if (!translated_buffer || translated_buffer->nextInChain
        || translated_buffer->label.length!=14
        || translated_buffer->label.data!=browser_buffer.label.data()
        || translated_buffer->usage!=(wgpu::BufferUsage::MapWrite|wgpu::BufferUsage::CopySrc)) return 1;
    auto created_buffer=native_device->device.CreateBuffer(&*translated_buffer);
    if (!created_buffer || created_buffer.GetSize()!=64
        || created_buffer.GetUsage()!=translated_buffer->usage
        || created_buffer.GetMapState()!=wgpu::BufferMapState::Mapped
        || !created_buffer.GetMappedRange(0,64)) return 1;
    created_buffer.Unmap();
    created_buffer.Destroy();
    browser_buffer.usage=0x400;
    if (make_dawn_buffer_descriptor(browser_buffer)) return 1;
    const auto device_features=webgpu_supported_feature_names(native_device->device);
    for (const auto& feature:webgpu_feature_names) {
        const bool exposed=std::find(device_features.begin(),device_features.end(),feature.name)!=device_features.end();
        if (exposed!=native_device->device.HasFeature(feature.native)) return 1;
        // No optional features were requested on this device. Adapter support
        // must not silently become device enablement.
        if (exposed && feature.native!=wgpu::FeatureName::CoreFeaturesAndLimits) return 1;
    }
    imported_dxgi_fences imported;
    imported.values.push_back(99);
    if (import_dxgi_fences({}, {}, {}, imported)!=dxgi_fence_status::invalid_argument
        || !imported.values.empty() || !imported.fences.empty()) return 1;
    std::array<void*,1> invalid_handles{nullptr};
    std::array<uint64_t,1> wait_values{5};
    if (import_dxgi_fences(native_device->device,invalid_handles,wait_values,imported)
        !=dxgi_fence_status::invalid_argument) return 1;
    if (import_dxgi_fences(native_device->device,invalid_handles,{},imported)
        !=dxgi_fence_status::invalid_argument) return 1;
    // The test device has no optional DXGI fence feature enabled. Refuse before
    // calling ImportSharedFence, even if a caller supplies a non-null handle.
    invalid_handles[0]=reinterpret_cast<void*>(uintptr_t{1});
    if (import_dxgi_fences(native_device->device,invalid_handles,wait_values,imported)
        !=dxgi_fence_status::missing_device_feature || !imported.fences.empty()) return 1;
    auto owned_device=root.adopt_device(state->adapter,native_device->device,{},1,1,1,1,1,1);
    if (root.live_devices()!=1) return 1;
    root.with_device(owned_device,[&](auto& device) { owner=device.owner(); });
    root.with_device(owned_device,[&](auto& device) {
        wgpu::ShaderSourceWGSL source{};source.code="@compute @workgroup_size(1) fn main() {}";
        wgpu::ShaderModuleDescriptor descriptor{};descriptor.nextInChain=&source;
        auto shader=device.create_shader_module(descriptor);
        bool capacity=false;
        try{device.create_shader_module(descriptor);}catch(const std::length_error&){capacity=true;}
        if(!capacity || device.live_shader_modules()!=1)throw std::runtime_error("Shader capacity admission failed");
        device.with_shader_module(shader,[&](const auto& native) {
            if(!native)throw std::runtime_error("Owned shader is null");
            bool release_guard=false,close_guard=false;
            try{device.release_shader_module(shader);}catch(const std::logic_error&){release_guard=true;}
            try{device.close();}catch(const std::logic_error&){close_guard=true;}
            if(!release_guard || !close_guard)throw std::runtime_error("Shader borrowing did not guard lifetime");
        });
        device.release_shader_module(shader);
        bool stale=false;try{device.with_shader_module(shader,[](const auto&){});}catch(const std::invalid_argument&){stale=true;}
        auto replacement=device.create_shader_module(descriptor);
        if(!stale || replacement.generation==shader.generation)throw std::runtime_error("Shader generation identity reused");
        device.release_shader_module(replacement);
    });
    // An owned render pipeline must remain usable by an encoded command after
    // its table reference and source shader have been released.
    native_device->device.PushErrorScope(wgpu::ErrorFilter::Validation);
    root.with_device(owned_device,[&](auto& device) {
        wgpu::ShaderSourceWGSL source{};
        source.code=R"WGSL(
            @vertex fn vs(@builtin(vertex_index) i:u32)->@builtin(position) vec4f {
                let p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));
                return vec4f(p[i],0,1);
            }
            @fragment fn fs()->@location(0) vec4f {return vec4f(1,0,0,1);}
        )WGSL";
        wgpu::ShaderModuleDescriptor shader_desc{};shader_desc.nextInChain=&source;
        auto shader=device.create_shader_module(shader_desc);
        wgpu::RenderPipelineDescriptor descriptor{};
        wgpu::ColorTargetState target{};target.format=wgpu::TextureFormat::RGBA8Unorm;
        wgpu::FragmentState fragment{};fragment.entryPoint="fs";fragment.targetCount=1;fragment.targets=&target;
        descriptor.fragment=&fragment;descriptor.vertex.entryPoint="vs";
        resource_handle<wgpu::RenderPipeline> pipeline;
        device.with_shader_module(shader,[&](const auto& module) {
            descriptor.vertex.module=module;fragment.module=module;
            pipeline=device.create_render_pipeline(descriptor);
            bool full=false;try{device.create_render_pipeline(descriptor);}catch(const std::length_error&){full=true;}
            if(!full || device.live_render_pipelines()!=1)throw std::runtime_error("Render pipeline capacity failed");
        });
        device.release_shader_module(shader);
        wgpu::TextureDescriptor texture_desc{};texture_desc.size={4,4,1};texture_desc.format=target.format;
        texture_desc.usage=wgpu::TextureUsage::RenderAttachment;
        auto texture=device.create_texture(texture_desc);
        auto view_handle=device.create_texture_view(texture,{});wgpu::TextureView view;
        bool texture_full=false,view_full=false;
        try{device.create_texture(texture_desc);}catch(const std::length_error&){texture_full=true;}
        try{device.create_texture_view(texture,{});}catch(const std::length_error&){view_full=true;}
        if(!texture_full || !view_full)throw std::runtime_error("Texture capacity admission failed");
        device.with_texture_view(view_handle,[&](const auto& native) {
            view=native;bool release_guard=false,destroy_guard=false,close_guard=false;
            try{device.release_texture_view(view_handle);}catch(const std::logic_error&){release_guard=true;}
            try{device.destroy_texture(texture);}catch(const std::logic_error&){destroy_guard=true;}
            try{device.close();}catch(const std::logic_error&){close_guard=true;}
            if(!release_guard || !destroy_guard || !close_guard)throw std::runtime_error("Borrowed texture view lifetime unguarded");
        });
        device.release_texture(texture); // The view retains its source texture.
        device.release_texture_view(view_handle);
        bool stale_texture=false,stale_view=false;
        try{device.with_texture(texture,[](const auto&){});}catch(const std::invalid_argument&){stale_texture=true;}
        try{device.with_texture_view(view_handle,[](const auto&){});}catch(const std::invalid_argument&){stale_view=true;}
        if(!stale_texture || !stale_view || device.live_textures() || device.live_texture_views())throw std::runtime_error("Texture references did not retire");
        wgpu::RenderPassColorAttachment attachment{};attachment.view=view;
        attachment.loadOp=wgpu::LoadOp::Clear;attachment.storeOp=wgpu::StoreOp::Store;
        wgpu::RenderPassDescriptor pass_desc{};pass_desc.colorAttachmentCount=1;pass_desc.colorAttachments=&attachment;
        auto encoder=device.create_command_encoder({});auto pass=device.begin_render_pass(encoder,pass_desc);
        bool encoder_full=false,pass_full=false;
        try{device.create_command_encoder({});}catch(const std::length_error&){encoder_full=true;}
        try{device.begin_render_pass(encoder,pass_desc);}catch(const std::length_error&){pass_full=true;}
        if(!encoder_full || !pass_full)throw std::runtime_error("Command resource capacity failed");
        device.with_render_pipeline(pipeline,[&](const auto& native) {
            bool guarded=false;try{device.release_render_pipeline(pipeline);}catch(const std::logic_error&){guarded=true;}
            bool close_guarded=false;try{device.close();}catch(const std::logic_error&){close_guarded=true;}
            if(!guarded || !close_guarded)throw std::runtime_error("Borrowed render pipeline lifetime unguarded");
            device.with_render_pass(pass,[&](const auto& native_pass) {
                bool guarded=false;try{device.release_render_pass(pass);}catch(const std::logic_error&){guarded=true;}
                if(!guarded)throw std::runtime_error("Borrowed render pass release unguarded");
                native_pass.SetPipeline(native);native_pass.Draw(3);native_pass.End();
            });
        });
        device.release_render_pass(pass);
        auto command=device.finish_command_encoder(encoder,{});
        bool command_full=false;try{device.finish_command_encoder(encoder,{});}catch(const std::length_error&){command_full=true;}
        if(!command_full)throw std::runtime_error("Command buffer capacity failed");
        device.release_command_encoder(encoder);
        device.release_render_pipeline(pipeline);
        bool stale=false;try{device.with_render_pipeline(pipeline,[](const auto&){});}catch(const std::invalid_argument&){stale=true;}
        if(!stale || device.live_render_pipelines()!=0)throw std::runtime_error("Render pipeline stale handle accepted");
        device.with_command_buffer(command,[&](const auto& native_command) {
            bool close_guard=false;try{device.close();}catch(const std::logic_error&){close_guard=true;}
            if(!close_guard)throw std::runtime_error("Borrowed command buffer did not guard device close");
            device.native().GetQueue().Submit(1,&native_command);
        });
        device.release_command_buffer(command);
        bool stale_command=false;try{device.with_command_buffer(command,[](const auto&){});}catch(const std::invalid_argument&){stale_command=true;}
        if(!stale_command || device.live_command_buffers() || device.live_command_encoders() || device.live_render_passes())throw std::runtime_error("Command handles did not retire");
    });
    bool render_checked=false;
    native_device->device.PopErrorScope(wgpu::CallbackMode::AllowProcessEvents,
        [&](wgpu::PopErrorScopeStatus status,wgpu::ErrorType type,wgpu::StringView) {
            if(status!=wgpu::PopErrorScopeStatus::Success || type!=wgpu::ErrorType::NoError)
                throw std::runtime_error("Owned render pipeline draw failed validation");
            render_checked=true;
        });
    const auto render_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!render_checked && std::chrono::steady_clock::now()<render_deadline) {
        root.pump([](auto) {});
        if(!render_checked)wake->wait_for(std::chrono::milliseconds(1),[]{return false;});
    }
    if(!render_checked)throw std::runtime_error("Render pipeline validation did not complete");
    root.with_device(owned_device,[&](auto& device) {
        wgpu::TextureDescriptor descriptor{};descriptor.size={1,1,1};descriptor.format=wgpu::TextureFormat::RGBA8Unorm;descriptor.usage=wgpu::TextureUsage::RenderAttachment;
        auto imported=device.native().CreateTexture(&descriptor);
        bool missing_source=false;try{device.adopt_texture({},imported);}catch(const std::invalid_argument&){missing_source=true;}
        if(!missing_source)throw std::runtime_error("Imported texture accepted without its source device");
        auto texture=device.adopt_texture(device.native(),imported);
        device.with_texture(texture,[&](const auto& native){if(native.Get()!=imported.Get())throw std::runtime_error("Texture adoption changed native identity");});
        bool full=false;try{device.adopt_texture(device.native(),imported);}catch(const std::length_error&){full=true;}
        if(!full||device.live_textures()!=1)throw std::runtime_error("Imported texture capacity admission failed");
        auto view=device.create_texture_view(texture,{});
        device.destroy_texture(texture);device.destroy_texture(texture);
        if(device.live_textures()!=1 || device.live_texture_views()!=1)throw std::runtime_error("Texture destroy removed API handles");
        device.release_texture_view(view);device.release_texture(texture);
    });
    auto shader_releases=root.release_endpoint();
    resource_handle<wgpu::ShaderModule> retired_shader,reused_shader;
    release_ticket retired_ticket;
    root.with_device(owned_device,[&](auto& device) {
        wgpu::ShaderSourceWGSL source{};source.code="@compute @workgroup_size(1) fn main() {}";
        wgpu::ShaderModuleDescriptor descriptor{};descriptor.nextInChain=&source;
        retired_shader=device.create_shader_module(descriptor);
        retired_ticket=shader_releases->reserve(graphics_service::deferred_shader_module_release(owned_device,retired_shader)).value();
        device.release_shader_module(retired_shader);
        reused_shader=device.create_shader_module(descriptor);
    });
    std::thread stale_shader_finalizer([&] {if(!shader_releases->publish(retired_ticket))std::terminate();});
    stale_shader_finalizer.join();
    root.drain_commands();
    root.with_device(owned_device,[&](auto& device) {
        device.with_shader_module(reused_shader,[](const auto& native) {if(!native)throw std::runtime_error("Stale finalizer released replacement shader");});
        if(device.live_shader_modules()!=1)throw std::runtime_error("Stale shader release changed live count");
    });
    auto release_shader_ticket=shader_releases->reserve(graphics_service::deferred_shader_module_release(owned_device,reused_shader)).value();
    std::thread shader_finalizer([&] {if(!shader_releases->publish(release_shader_ticket))std::terminate();});shader_finalizer.join();
    root.with_device(owned_device,[&](auto& device) {if(device.live_shader_modules()!=1)throw std::runtime_error("Finalizer released native shader inline");});
    root.drain_commands();
    root.with_device(owned_device,[&](auto& device) {if(device.live_shader_modules()!=0)throw std::runtime_error("Shader finalizer did not retire handle");});
    if(shader_releases->occupied()!=0)return 1;
    auto second_adapter=std::make_shared<result>();
    auto adapter_ticket=mailbox->reserve(19,owner).value();
    service.instance().RequestAdapter(&options,wgpu::CallbackMode::AllowProcessEvents,
        [mailbox,adapter_ticket,second_adapter](wgpu::RequestAdapterStatus status,wgpu::Adapter adapter,wgpu::StringView) {
            second_adapter->adapter=std::move(adapter);
            mailbox->publish(adapter_ticket,status==wgpu::RequestAdapterStatus::Success
                ? completion_status::success : completion_status::failed);
        });
    bool adapter_done=false;
    const auto adapter_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while (!adapter_done && std::chrono::steady_clock::now()<adapter_deadline) {
        service.pump([&](auto record) {
            if (record.operation!=19 || record.status!=completion_status::success)
                throw std::runtime_error("second adapter request failed");
            adapter_done=true;
        });
        if (!adapter_done) wake->wait_for(std::chrono::milliseconds(1),[] { return false; });
    }
    if (!adapter_done || !second_adapter->adapter) return 1;
    auto second_native=std::make_shared<device_result>();
    auto second_ticket=mailbox->reserve(20,owner).value();
    wgpu::DeviceDescriptor second_descriptor{};
    auto second_loss=std::make_shared<device_loss_signal>(wake);
    device_loss_signal::configure(second_descriptor,second_loss);
    second_adapter->adapter.RequestDevice(&second_descriptor,wgpu::CallbackMode::AllowProcessEvents,
        [mailbox,second_ticket,second_native](wgpu::RequestDeviceStatus status,wgpu::Device device,wgpu::StringView) {
            second_native->device=std::move(device);
            mailbox->publish(second_ticket,status==wgpu::RequestDeviceStatus::Success
                ? completion_status::success : completion_status::failed);
        });
    const auto second_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    bool second_done=false;
    while (!second_done && std::chrono::steady_clock::now()<second_deadline) {
        service.pump([&](auto record) {
            if (record.operation!=20 || record.status!=completion_status::success)
                throw std::runtime_error("native device request failed");
            second_done=true;
        });
        if (!second_done) wake->wait_for(std::chrono::milliseconds(1),[] { return false; });
    }
    if (!second_done || !second_native->device) return 1;
    auto second_owned=root.adopt_device(second_adapter->adapter,second_native->device,second_loss);
    resource_owner second_owner{};
    root.with_device(second_owned,[&](auto& device) { second_owner=device.owner(); });
    if (root.live_devices()!=2 || owner==second_owner) return 1;
    resource_handle<wgpu::Buffer> managed_buffer;
    wgpu::Buffer borrowed_buffer_reference;
    root.with_device(owned_device,[&](auto& device) {
        browser_buffer.usage=0x6;
        auto descriptor=make_dawn_buffer_descriptor(browser_buffer);
        managed_buffer=device.create_buffer(*descriptor);
        bool capacity_rejected=false;
        try { device.create_buffer(*descriptor); }
        catch (const std::length_error&) { capacity_rejected=true; }
        if (!capacity_rejected || device.live_buffers()!=1) throw std::runtime_error("Buffer capacity was not enforced");
        device.with_buffer(managed_buffer,[&](const auto& buffer) {
            borrowed_buffer_reference=buffer;
            bool destroy_guard=false,release_guard=false,close_guard=false;
            try { device.destroy_buffer(managed_buffer); } catch (const std::logic_error&) { destroy_guard=true; }
            try { device.release_buffer(managed_buffer); } catch (const std::logic_error&) { release_guard=true; }
            try { device.close(); } catch (const std::logic_error&) { close_guard=true; }
            if (!destroy_guard || !release_guard || !close_guard) throw std::runtime_error("Buffer execution guards failed");
        });
    });
    bool foreign_buffer=false;
    root.with_device(second_owned,[&](auto& device) {
        try { device.with_buffer(managed_buffer,[](const auto&) {}); }
        catch (const std::invalid_argument&) { foreign_buffer=true; }
    });
    if (!foreign_buffer) return 1;
    auto buffer_release=root.release_endpoint();
    const auto buffer_release_ticket=buffer_release->reserve(graphics_service::deferred_buffer_release(owned_device,managed_buffer));
    if (!buffer_release_ticket) return 1;
    std::thread buffer_finalizer([&] {
        if (!buffer_release->publish(*buffer_release_ticket)) std::terminate();
    });
    buffer_finalizer.join();
    root.drain_commands();
    root.with_device(owned_device,[&](auto& device) {
        bool stale=false;
        try { device.with_buffer(managed_buffer,[](const auto&) {}); }
        catch (const std::invalid_argument&) { stale=true; }
        if (!stale || device.live_buffers()!=0 || borrowed_buffer_reference.GetMapState()!=wgpu::BufferMapState::Mapped
            || !borrowed_buffer_reference.GetMappedRange(0,64)) throw std::runtime_error("Wrapper release destroyed native buffer");
        auto descriptor=make_dawn_buffer_descriptor(browser_buffer);
        auto replacement=device.create_buffer(*descriptor);
        if (replacement.slot!=managed_buffer.slot || replacement.generation==managed_buffer.generation)
            throw std::runtime_error("Buffer slot generation was not advanced");
        device.destroy_buffer(replacement);
        device.destroy_buffer(replacement);
        device.with_buffer(replacement,[](const auto& buffer) {
            if (buffer.GetSize()!=64 || buffer.GetMapState()!=wgpu::BufferMapState::Unmapped)
                throw std::runtime_error("Destroyed buffer wrapper lost metadata");
        });
        device.release_buffer(replacement);
    });
    borrowed_buffer_reference.Unmap();
    borrowed_buffer_reference=nullptr;
    test_canvas_storage(service,native_device->device,second_native->device);
    test_canvas_consumer_pixels(service,native_device->device);
    bool foreign_rejected=false;
    try { other_root.with_device(owned_device,[](auto&) {}); }
    catch (const std::invalid_argument&) { foreign_rejected=true; }
    if (!foreign_rejected) return 1;
    // Resource-table destruction is logical until this device's GPU queue has
    // completed. Exercise a real command buffer, rather than a synthetic serial.
    resource_table<wgpu::Buffer> buffers(2,owner);
    wgpu::BufferDescriptor buffer_descriptor{};
    buffer_descriptor.size=4096;
    buffer_descriptor.usage=wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc;
    auto buffer=buffers.insert(owner,std::make_unique<wgpu::Buffer>(
        native_device->device.CreateBuffer(&buffer_descriptor)));
    auto encoder=native_device->device.CreateCommandEncoder();
    encoder.ClearBuffer(buffers.get(buffer,owner),0,4096);
    auto commands=encoder.Finish();
    auto submitted=mailbox->reserve(11,owner).value();
    buffers.mark_used(buffer,owner,1);
    auto queue=native_device->device.GetQueue();
    queue.Submit(1,&commands);
    queue.OnSubmittedWorkDone(wgpu::CallbackMode::AllowProcessEvents,
        [mailbox,submitted](wgpu::QueueWorkDoneStatus status,wgpu::StringView) {
            mailbox->publish(submitted,status==wgpu::QueueWorkDoneStatus::Success
                ? completion_status::success : completion_status::failed);
        });
    buffers.destroy(buffer,owner);
    if (buffers.resident_count()!=1 || buffers.deferred_count()!=1) return 1;
    bool submission_done=false;
    const auto submission_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while (!submission_done && std::chrono::steady_clock::now()<submission_deadline) {
        service.pump([&](auto record) {
            if (record.operation!=11 || record.status!=completion_status::success)
                throw std::runtime_error("native queue completion failed");
            buffers.complete(1);
            submission_done=true;
        });
        if (!submission_done) wake->wait_for(std::chrono::milliseconds(1),[] { return false; });
    }
    if (!submission_done || buffers.resident_count()!=0 || buffers.deferred_count()!=0) return 1;
    // Map completion must progress without presentation, including cancellation
    // caused by destroying a buffer before ProcessEvents delivers the callback.
    wgpu::BufferDescriptor map_descriptor{};
    map_descriptor.size=4096;
    map_descriptor.usage=wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
    for (const bool destroy_pending : {false,true}) {
        auto mapped=native_device->device.CreateBuffer(&map_descriptor);
        auto map_ticket=mailbox->reserve(destroy_pending ? 13 : 12,owner).value();
        auto map_status=std::make_shared<wgpu::MapAsyncStatus>();
        mapped.MapAsync(wgpu::MapMode::Read,0,4096,wgpu::CallbackMode::AllowProcessEvents,
            [mailbox,map_ticket,map_status](wgpu::MapAsyncStatus status,wgpu::StringView) {
                *map_status=status;
                mailbox->publish(map_ticket,status==wgpu::MapAsyncStatus::Success
                    ? completion_status::success : completion_status::cancelled);
            });
        if (destroy_pending) mapped.Destroy();
        bool map_done=false;
        const auto map_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        while (!map_done && std::chrono::steady_clock::now()<map_deadline) {
            service.pump([&](auto record) {
                if (record.operation!=(destroy_pending ? 13u : 12u))
                    throw std::runtime_error("unexpected map completion");
                map_done=true;
            });
            if (!map_done) wake->wait_for(std::chrono::milliseconds(1),[] { return false; });
        }
        if (!map_done || *map_status!=(destroy_pending
            ? wgpu::MapAsyncStatus::Aborted : wgpu::MapAsyncStatus::Success)) return 1;
        if (!destroy_pending) {
            const auto* bytes=static_cast<const unsigned char*>(mapped.GetConstMappedRange(0,4096));
            if (!bytes) return 1;
            for (size_t i=0;i<4096;++i) if (bytes[i]!=0) return 1;
            mapped.Unmap();
            mapped.Destroy();
        }
        if (mailbox->has_pending() || mailbox->has_ready()) return 1;
    }
    auto pending_device=mailbox->reserve(14,owner).value();
    auto pending_map=native_device->device.CreateBuffer(&map_descriptor);
    struct cancelled_map_result { bool called{},accepted{}; };
    auto cancelled_map=std::make_shared<cancelled_map_result>();
    pending_map.MapAsync(wgpu::MapMode::Read,0,4096,wgpu::CallbackMode::AllowProcessEvents,
        [mailbox,pending_device,cancelled_map](wgpu::MapAsyncStatus,wgpu::StringView) {
            cancelled_map->called=true;
            cancelled_map->accepted=mailbox->publish(pending_device,completion_status::success);
        });
    auto independent_owner=second_owner;
    auto independent=mailbox->reserve(15,independent_owner).value();
    root.destroy_device(owned_device);
    if (root.live_devices()!=1) return 1;
    bool stale_rejected=false;
    try { root.with_device(owned_device,[](auto&) {}); }
    catch (const std::invalid_argument&) { stale_rejected=true; }
    if (!stale_rejected || !mailbox->publish(independent,completion_status::success)) return 1;
    size_t device_records=0;
    service.pump([&](auto record) {
        if ((record.operation==14 && record.status==completion_status::cancelled)
            || (record.operation==15 && record.status==completion_status::success)) ++device_records;
        else throw std::runtime_error("device cancellation crossed ownership boundary");
    });
    const auto cancellation_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while (!cancelled_map->called && std::chrono::steady_clock::now()<cancellation_deadline) {
        if (root.has_ready_work())
            root.pump([](auto) { throw std::runtime_error("duplicate device cancellation"); });
        if (!cancelled_map->called) wake->wait_for(
            root.recommended_idle_wait(std::chrono::milliseconds(100)),[] { return false; });
    }
    if (device_records!=2 || !cancelled_map->called || cancelled_map->accepted) return 1;
    // The surviving native device must still execute an upload and map after
    // its sibling was destroyed, with no presentation or animation-frame pump.
    auto survivor_buffer=second_native->device.CreateBuffer(&map_descriptor);
    std::vector<uint32_t> upload(1024,0x13579bdfu);
    second_native->device.GetQueue().WriteBuffer(survivor_buffer,0,upload.data(),4096);
    std::fill(upload.begin(),upload.end(),0u);
    auto survivor_ticket=mailbox->reserve(21,second_owner).value();
    survivor_buffer.MapAsync(wgpu::MapMode::Read,0,4096,wgpu::CallbackMode::AllowProcessEvents,
        [mailbox,survivor_ticket](wgpu::MapAsyncStatus status,wgpu::StringView) {
            mailbox->publish(survivor_ticket,status==wgpu::MapAsyncStatus::Success
                ? completion_status::success : completion_status::failed);
        });
    bool survivor_done=false;
    const auto survivor_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while (!survivor_done && std::chrono::steady_clock::now()<survivor_deadline) {
        service.pump([&](auto record) {
            if (record.operation!=21 || record.status!=completion_status::success)
                throw std::runtime_error("surviving device failed after sibling destruction");
            survivor_done=true;
        });
        if (!survivor_done) wake->wait_for(std::chrono::milliseconds(1),[] { return false; });
    }
    if (!survivor_done) return 1;
    const auto* uploaded=static_cast<const uint32_t*>(survivor_buffer.GetConstMappedRange(0,4096));
    if (!uploaded) return 1;
    for (size_t i=0;i<1024;++i) if (uploaded[i]!=0x13579bdfu) return 1;
    survivor_buffer.Unmap();
    survivor_buffer.Destroy();
    wgpu::ShaderSourceWGSL wgsl{};
    wgsl.code="@compute @workgroup_size(1) fn main() {}";
    wgpu::ShaderModuleDescriptor shader_descriptor{};
    shader_descriptor.nextInChain=&wgsl;
    auto shader=second_native->device.CreateShaderModule(&shader_descriptor);
    wgpu::ComputePipelineDescriptor pipeline_descriptor{};
    pipeline_descriptor.compute.module=shader;
    pipeline_descriptor.compute.entryPoint="main";
    auto pipeline_ticket=mailbox->reserve(24,second_owner).value();
    auto pipeline=std::make_shared<wgpu::ComputePipeline>();
    second_native->device.CreateComputePipelineAsync(&pipeline_descriptor,wgpu::CallbackMode::AllowProcessEvents,
        [mailbox,pipeline_ticket,pipeline](wgpu::CreatePipelineAsyncStatus status,wgpu::ComputePipeline result,wgpu::StringView) {
            *pipeline=std::move(result);
            mailbox->publish(pipeline_ticket,status==wgpu::CreatePipelineAsyncStatus::Success
                ? completion_status::success : completion_status::failed);
        });
    bool pipeline_done=false;
    const auto pipeline_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while (!pipeline_done && std::chrono::steady_clock::now()<pipeline_deadline) {
        if (root.has_ready_work()) root.pump([&](auto record) {
            if (record.operation!=24 || record.status!=completion_status::success)
                throw std::runtime_error("asynchronous native compute pipeline failed");
            pipeline_done=true;
        });
        if (!pipeline_done) wake->wait_for(root.recommended_idle_wait(std::chrono::milliseconds(100)),[] { return false; });
    }
    if (!pipeline_done || !*pipeline) return 1;
    // Captured validation errors and empty scopes both complete independently
    // of animation frames. A captured error must not poison the next scope.
    for (const bool invalid : {true,false}) {
        second_native->device.PushErrorScope(wgpu::ErrorFilter::Validation);
        wgpu::BufferDescriptor scoped_descriptor{};
        scoped_descriptor.size=4;
        scoped_descriptor.usage=invalid ? wgpu::BufferUsage::None : wgpu::BufferUsage::CopyDst;
        auto scoped_buffer=second_native->device.CreateBuffer(&scoped_descriptor);
        auto error_ticket=mailbox->reserve(invalid ? 25 : 26,second_owner).value();
        second_native->device.PopErrorScope(wgpu::CallbackMode::AllowProcessEvents,
            [mailbox,error_ticket,invalid](wgpu::PopErrorScopeStatus status,wgpu::ErrorType type,wgpu::StringView message) {
                const bool expected=status==wgpu::PopErrorScopeStatus::Success
                    && type==(invalid ? wgpu::ErrorType::Validation : wgpu::ErrorType::NoError)
                    && (!invalid || (message.data && message.length));
                mailbox->publish(error_ticket,expected ? completion_status::success : completion_status::failed);
            });
        bool error_done=false;
        const auto error_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while (!error_done && std::chrono::steady_clock::now()<error_deadline) {
            if (root.has_ready_work()) root.pump([&](auto record) {
                if (record.operation!=(invalid ? 25u : 26u) || record.status!=completion_status::success)
                    throw std::runtime_error("native error scope completion was incorrect");
                error_done=true;
            });
            if (!error_done) wake->wait_for(root.recommended_idle_wait(std::chrono::milliseconds(100)),[] { return false; });
        }
        if (!error_done || mailbox->metrics().occupied!=0) return 1;
    }
    for(const bool invalid:{false,true}) {
        second_native->device.PushErrorScope(wgpu::ErrorFilter::Validation);
        root.with_device(second_owned,[&](auto& device) {
            wgpu::ShaderSourceWGSL source{};
            source.code=invalid?"@compute fn broken( {":"@compute @workgroup_size(1) fn main() {}";
            wgpu::ShaderModuleDescriptor descriptor{};descriptor.nextInChain=&source;
            auto shader=device.create_shader_module(descriptor);
            // Invalid shader modules remain valid API objects; validation is
            // delivered through the native scope rather than a null wrapper.
            device.with_shader_module(shader,[&](const auto& native) {if(!native)throw std::runtime_error("Shader module wrapper missing");});
            device.release_shader_module(shader);
        });
        auto shader_ticket=mailbox->reserve(30,second_owner).value();
        second_native->device.PopErrorScope(wgpu::CallbackMode::AllowProcessEvents,
            [mailbox,shader_ticket,invalid](wgpu::PopErrorScopeStatus status,wgpu::ErrorType type,wgpu::StringView) {
                mailbox->publish(shader_ticket,status==wgpu::PopErrorScopeStatus::Success
                    && type==(invalid?wgpu::ErrorType::Validation:wgpu::ErrorType::NoError)?completion_status::success:completion_status::failed);
            });
        bool compiled=false;auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!compiled && std::chrono::steady_clock::now()<deadline) {
            root.pump([&](auto record) {if(record.operation!=30 || record.status!=completion_status::success)throw std::runtime_error("Owned WGSL validation result incorrect");compiled=true;});
            if(!compiled)wake->wait_for(std::chrono::milliseconds(1),[]{return false;});
        }
        if(!compiled)return 1;
    }
    auto loss_ticket=mailbox->reserve(22,second_owner).value();
    auto loss_buffer=second_native->device.CreateBuffer(&map_descriptor);
    struct loss_map_result { bool called{},accepted{}; };
    auto loss_map=std::make_shared<loss_map_result>();
    loss_buffer.MapAsync(wgpu::MapMode::Read,0,4096,wgpu::CallbackMode::AllowProcessEvents,
        [mailbox,loss_ticket,loss_map](wgpu::MapAsyncStatus status,wgpu::StringView) {
            loss_map->called=true;
            loss_map->accepted=mailbox->publish(loss_ticket,status==wgpu::MapAsyncStatus::Success
                ? completion_status::success : completion_status::failed);
        });
    second_native->device.ForceLoss(wgpu::DeviceLostReason::Unknown,"G02 loss test");
    const auto loss_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while (!second_loss->lost.load(std::memory_order_acquire) && std::chrono::steady_clock::now()<loss_deadline)
        wake->wait_for(std::chrono::milliseconds(1),[] { return false; });
    if (!second_loss->lost.load(std::memory_order_acquire) || !root.has_ready_work()) return 1;
    bool loss_delivered=false;
    root.pump([&](auto record) {
        if (record.operation!=22 || record.status!=completion_status::device_lost)
            throw std::runtime_error("device loss did not terminate its pending record");
        loss_delivered=true;
    });
    bool lost_rejected=false;
    root.with_device(second_owned,[&](auto& device) {
        try { device.native(); } catch (const std::logic_error&) { lost_rejected=true; }
    });
    const auto map_retirement_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while (!loss_map->called && std::chrono::steady_clock::now()<map_retirement_deadline) {
        if (root.has_ready_work()) root.pump([](auto) {
            throw std::runtime_error("lost mapping delivered twice");
        });
        if (!loss_map->called) wake->wait_for(root.recommended_idle_wait(std::chrono::milliseconds(100)),[] { return false; });
    }
    if (!loss_delivered || !lost_rejected || !loss_map->called || loss_map->accepted
        || mailbox->metrics().occupied!=0 || mailbox->metrics().native_pending!=0) return 1;
    loss_buffer.Destroy();
    auto releases=root.command_endpoint(2,0);
    auto release=graphics_service::deferred_device_release(second_owned);
    std::thread finalizer([&] {
        if (releases->enqueue(release)!=enqueue_result::accepted
            || releases->enqueue(release)!=enqueue_result::accepted) std::terminate();
    });
    finalizer.join();
    if (root.live_devices()!=1) return 1;
    root.pump([](auto) { throw std::runtime_error("unexpected release completion"); });
    if (root.live_devices()!=0 || mailbox->has_pending() || mailbox->has_ready()) return 1;
    service.close();
    bool rejected=false;
    try { service.instance(); } catch (const std::logic_error&) { rejected=true; }
    if (!rejected) return 1;
    // Close before processing the next native request. The promise-side record
    // must terminate once; the backend callback may arrive only after teardown.
    auto cancelled=std::make_unique<dawn_event_service>(1,wake);
    auto retained=cancelled->completions();
    auto pending=retained->reserve(2,owner).value();
    auto late_accepted=std::make_shared<bool>(false);
    cancelled->instance().RequestAdapter(&options,wgpu::CallbackMode::AllowProcessEvents,
        [retained,pending,late_accepted](wgpu::RequestAdapterStatus,wgpu::Adapter,wgpu::StringView) {
            *late_accepted=retained->publish(pending,completion_status::success);
        });
    cancelled->close();
    size_t terminated=0;
    cancelled->pump([&](auto record) {
        if (record.operation!=2 || record.status!=completion_status::cancelled)
            throw std::runtime_error("pending operation not cancelled");
        ++terminated;
    });
    cancelled.reset();
    if (terminated!=1 || *late_accepted || retained->has_ready()
        || retained->publish(pending,completion_status::success)) return 1;
    std::cout << "Native Dawn adapter/device/submission completion and deferred buffer release passed without RAF/UI\n";
}
