#include "native_webgpu_surface.h"
#include "native_webgpu_device.h"
#include "native_webgpu_canvas_context.h"
#include "webgpu_iosurface_canvas_host.h"
#include <chrono>
#include <iostream>
#include <thread>
using namespace webscene::graphics;
static void check(bool value, const char* message) { if(!value) throw std::runtime_error(message); }
int main() {
    auto gpu = std::make_shared<native_webgpu_device>(native_webgpu_device::create(wgpu::BackendType::Metal,
        {wgpu::FeatureName::SharedTextureMemoryIOSurface, wgpu::FeatureName::SharedFenceMTLSharedEvent}));
    auto provider = std::make_shared<dawn_iosurface_canvas_host>(16 * 1024 * 1024);
    uint64_t serial = 0;
    native_webgpu_canvas_context canvas(64, 64, make_iosurface_webgpu_canvas_host(provider, [&] {
        image_metadata metadata;
        metadata.canvas=1; metadata.allocation_generation=1; metadata.content_serial=++serial;
        metadata.producer_timeline=1; metadata.producer_value=serial;
        return metadata;
    }, gpu));
    webgpu_canvas_configuration config;
    config.device=gpu->device; config.format=wgpu::TextureFormat::BGRA8Unorm;
    canvas.configure(config);
    for(unsigned frame=0;frame<8;++frame) {
        canvas.resize(64 + frame * 4, 64);
        auto texture=canvas.current_texture();
        check(bool(texture), "IOSurface acquisition failed");
        wgpu::RenderPassColorAttachment color{};
        color.view=texture.CreateView();color.loadOp=wgpu::LoadOp::Clear;color.storeOp=wgpu::StoreOp::Store;
        color.clearValue={0.2,0.4,0.6,1};
        wgpu::RenderPassDescriptor pass_descriptor{};
        pass_descriptor.colorAttachmentCount=1;pass_descriptor.colorAttachments=&color;
        auto encoder=gpu->device.CreateCommandEncoder();
        auto pass=encoder.BeginRenderPass(&pass_descriptor);pass.End();
        auto commands=encoder.Finish();gpu->device.GetQueue().Submit(1,&commands);
        canvas.end_frame();
        auto snapshot=provider->capture_latest_submission();
        check(bool(snapshot), "missing submitted image snapshot");
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(snapshot->state()==dawn_iosurface_submission::status::pending && std::chrono::steady_clock::now()<deadline) {
            gpu->instance.ProcessEvents();std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(snapshot->state()==dawn_iosurface_submission::status::ready,"GPU handoff did not complete");
        auto retained=snapshot->take_ready();check(bool(retained),"missing retained IOSurface");
        check(retained->describe().width==64+frame*4,"wrong image dimensions");
        auto consumer=retained->begin_consumer();check(bool(consumer),"consumer acquisition failed");
        check(consumer->provider()->kind()==image_provider_kind::iosurface,"wrong image provider");
        // No consumer GPU work in this contract test; actual Foco presentation
        // must signal completion only after its GPU sampling has completed.
        consumer->complete();
        auto ready=provider->take_ready();
    }
    canvas.unconfigure();
    check(provider->idle(),"provider did not retire submissions");
    check(!gpu->failed->load(),"WebGPU validation or device loss");
    native_webgpu_surface surface(42, 80, 60);
    auto surface_texture=surface.current_texture();
    check(bool(surface_texture), "native surface did not initialize");
    wgpu::RenderPassColorAttachment attachment{};
    attachment.view=surface_texture.CreateView();attachment.loadOp=wgpu::LoadOp::Clear;attachment.storeOp=wgpu::StoreOp::Store;
    attachment.clearValue={1,0,0,1};
    wgpu::RenderPassDescriptor desc{};desc.colorAttachmentCount=1;desc.colorAttachments=&attachment;
    auto encoder=surface.device().CreateCommandEncoder();auto pass=encoder.BeginRenderPass(&desc);pass.End();
    auto commands=encoder.Finish();surface.device().GetQueue().Submit(1,&commands);
    auto snapshot=surface.present();check(bool(snapshot), "native surface snapshot missing");
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    std::shared_ptr<const webscene_gpu_image_lease_v3> image;
    do {
        surface.process_events();image=snapshot->resolve();
        if(!image)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while(!image && std::chrono::steady_clock::now()<deadline);
    check(bool(image), "native surface snapshot resolution failed");
    check(snapshot->describe().canvas==42, "native surface identity");
    check(!surface.failed(), "native surface validation failure");
    std::cout << "Native IOSurface: 8 frames, resize, snapshots and retirement passed\n";
}
