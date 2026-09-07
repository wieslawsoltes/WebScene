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
    service.close();
    std::cout << "Native Dawn hardware adapter completion delivered without RAF/UI\n";
}
