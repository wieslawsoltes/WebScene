#pragma once
#include "native_webgpu_device.h"
#include "native_webgpu_canvas_context.h"
#include "platform_webgpu_canvas.h"
#if defined(__APPLE__) || defined(_WIN32)
namespace webscene::graphics {
// Native application surface using the same imported images and retained
// snapshots as the JS binding. Hosts consume snapshots through their compositor;
// a frame never needs to be copied into CPU Canvas commands.
class native_webgpu_surface final {
    std::shared_ptr<native_webgpu_device> gpu_;
    std::shared_ptr<platform_dawn_canvas_host> provider_;
    uint64_t canvas_id_, generation_=1, serial_=0;
    native_webgpu_canvas_context canvas_;
    static std::vector<wgpu::FeatureName> features() {
#if defined(__APPLE__)
        return {wgpu::FeatureName::SharedTextureMemoryIOSurface, wgpu::FeatureName::SharedFenceMTLSharedEvent};
#else
        return {wgpu::FeatureName::SharedTextureMemoryDXGISharedHandle, wgpu::FeatureName::SharedFenceDXGISharedHandle};
#endif
    }
    webgpu_canvas_host host() {
        auto metadata=[this] {
            image_metadata result;
            result.canvas=canvas_id_;result.allocation_generation=generation_;
            result.content_serial=++serial_;result.producer_timeline=canvas_id_;result.producer_value=serial_;
            return result;
        };
#if defined(__APPLE__)
        return make_iosurface_webgpu_canvas_host(provider_, std::move(metadata), gpu_);
#else
        return make_dxgi_webgpu_canvas_host(provider_, std::move(metadata), gpu_);
#endif
    }
public:
    native_webgpu_surface(uint64_t canvas_id, uint32_t width, uint32_t height,
                          uint64_t byte_budget=64*1024*1024)
        : gpu_(std::make_shared<native_webgpu_device>(native_webgpu_device::create(platform_canvas_backend,features()))),
          provider_(std::make_shared<platform_dawn_canvas_host>(byte_budget)),
          canvas_id_(canvas_id), canvas_(width,height,host()) {
        if(!canvas_id_)throw std::invalid_argument("Native GPU surface requires a nonzero canvas identity");
        webgpu_canvas_configuration config;
        config.device=gpu_->device;config.format=wgpu::TextureFormat::BGRA8Unorm;
        canvas_.configure(config);
    }
    const wgpu::Device& device() const { return gpu_->device; }
    wgpu::Texture current_texture() { return canvas_.current_texture(); }
    void resize(uint32_t width,uint32_t height) { canvas_.resize(width,height); }
    std::shared_ptr<webscene_gpu_image_snapshot> present() {
        canvas_.end_frame();
        auto ticket=provider_->capture_latest_submission();
        return ticket ? std::make_shared<platform_dawn_scene_snapshot>(std::move(ticket)) : nullptr;
    }
    void process_events() {
        gpu_->instance.ProcessEvents();
        // Snapshots retain exact outputs independently of the provider queue.
        while(provider_->take_ready()) {}
    }
    bool failed() const { return gpu_->failed->load(); }
};
} // namespace webscene::graphics
#endif
