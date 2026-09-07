#include "graphics/dawn_event_service.h"
#include "graphics/engine_wake.h"
#include <iostream>
using namespace webscene::graphics;
int main() {
    auto wake=std::make_shared<engine_wake>();
    dawn_event_service service(4,wake);
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
    native_device->device.Destroy();
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
