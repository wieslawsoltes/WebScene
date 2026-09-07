#include "graphics/graphics_service.h"
#include "graphics/engine_wake.h"
#include "graphics/dawn_canvas_images.h"
#include <iostream>
using namespace webscene::graphics;
void test_canvas_consumer_pixels(dawn_event_service& service,const wgpu::Device& device) {
    auto pool=std::make_unique<dawn_canvas_images>(device,64*64*4);
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
    // Submit to the same queue before processing any completion. Queue order,
    // rather than a CPU wait or a pixel upload, makes producer writes visible.
    auto source_texture=dawn_canvas_images::resolve(*consumer,device);
    wgpu::BufferDescriptor buffer_descriptor{};
    buffer_descriptor.size=64*256;
    buffer_descriptor.usage=wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
    auto readback=device.CreateBuffer(&buffer_descriptor);
    auto copy=device.CreateCommandEncoder();
    wgpu::TexelCopyTextureInfo source{}; source.texture=source_texture;
    wgpu::TexelCopyBufferInfo destination{}; destination.buffer=readback;
    destination.layout.bytesPerRow=256; destination.layout.rowsPerImage=64;
    wgpu::Extent3D extent{64,64,1};
    copy.CopyTextureToBuffer(&source,&destination,&extent);
    auto consumer_commands=copy.Finish(); queue.Submit(1,&consumer_commands);
    submitted.reset(); pool.reset();
    bool completed=false,queue_success=false,mapped=false,map_success=false;
    queue.OnSubmittedWorkDone(wgpu::CallbackMode::AllowProcessEvents,
        [&](wgpu::QueueWorkDoneStatus status,wgpu::StringView) {
            consumer->complete(); consumer.reset();
            queue_success=status==wgpu::QueueWorkDoneStatus::Success; completed=true;
        });
    readback.MapAsync(wgpu::MapMode::Read,0,64*256,wgpu::CallbackMode::AllowProcessEvents,
        [&](wgpu::MapAsyncStatus status,wgpu::StringView) { map_success=status==wgpu::MapAsyncStatus::Success; mapped=true; });
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while ((!completed || !mapped || producer_status->load()==dawn_canvas_images::submission_status::pending) && std::chrono::steady_clock::now()<deadline) {
        service.instance().ProcessEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!completed || !mapped || !queue_success || !map_success
        || producer_status->load()!=dawn_canvas_images::submission_status::success)
        throw std::runtime_error("GPU image consumer completion failed");
    const auto* pixels=static_cast<const uint8_t*>(readback.GetConstMappedRange(0,64*256));
    if (!pixels) throw std::runtime_error("GPU image diagnostic mapping failed");
    for (size_t i=0;i<64*64;++i) {
        if (pixels[4*i]!=64 || pixels[4*i+1]!=128 || pixels[4*i+2]!=191 || pixels[4*i+3]!=255)
            throw std::runtime_error("retained GPU image pixels differ from producer clear");
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
    auto wake=std::make_shared<engine_wake>();
    graphics_service root(wake),other_root(wake);
    auto& service=root.dawn();
    auto mailbox=service.completions();
    resource_owner owner{new_owner_token(),new_owner_token(),0};
    auto ticket=mailbox->reserve(1,owner).value();
    struct result { wgpu::Adapter adapter; };
    auto state=std::make_shared<result>();
    wgpu::RequestAdapterOptions options{};
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
    struct device_result { wgpu::Device device; };
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
    auto owned_device=root.adopt_device(state->adapter,native_device->device);
    if (root.live_devices()!=1) return 1;
    root.with_device(owned_device,[&](auto& device) { owner=device.owner(); });
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
