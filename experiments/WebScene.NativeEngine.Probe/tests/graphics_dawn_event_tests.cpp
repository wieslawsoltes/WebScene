#include "graphics/graphics_service.h"
#include "graphics/engine_wake.h"
#include <iostream>
using namespace webscene::graphics;
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
    auto loss_ticket=mailbox->reserve(22,second_owner).value();
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
    if (!loss_delivered || !lost_rejected || mailbox->publish(loss_ticket,completion_status::success)) return 1;
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
