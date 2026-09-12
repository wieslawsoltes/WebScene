#pragma once
#include "webgpu_canvas_interop.h"
#if defined(__APPLE__)
#include "dawn_scene_image_snapshot.h"
#include "webgpu_iosurface_canvas_host.h"
namespace webscene::graphics {
using platform_dawn_canvas_host=dawn_iosurface_canvas_host;
using platform_dawn_scene_snapshot=dawn_scene_image_snapshot;
inline constexpr auto platform_canvas_interop=webgpu_canvas_interop::iosurface;
inline constexpr auto platform_canvas_backend=wgpu::BackendType::Metal;
inline auto make_platform_webgpu_canvas_host(std::shared_ptr<platform_dawn_canvas_host> provider,
    std::function<image_metadata()> metadata) {return make_iosurface_webgpu_canvas_host(std::move(provider),std::move(metadata));}
}
#elif defined(_WIN32)
#include "dawn_dxgi_scene_snapshot.h"
#include "webgpu_dxgi_canvas_host.h"
namespace webscene::graphics {
using platform_dawn_canvas_host=dawn_dxgi_canvas_host;
using platform_dawn_scene_snapshot=dawn_dxgi_scene_snapshot;
inline constexpr auto platform_canvas_interop=webgpu_canvas_interop::dxgi;
inline constexpr auto platform_canvas_backend=wgpu::BackendType::D3D12;
inline auto make_platform_webgpu_canvas_host(std::shared_ptr<platform_dawn_canvas_host> provider,
    std::function<image_metadata()> metadata) {return make_dxgi_webgpu_canvas_host(std::move(provider),std::move(metadata));}
}
#elif defined(__linux__)
#include "dawn_offscreen_canvas_host.h"
namespace webscene::graphics {
using platform_dawn_canvas_host=dawn_offscreen_canvas_host;
using platform_dawn_scene_snapshot=dawn_offscreen_scene_snapshot;
inline constexpr auto platform_canvas_interop=webgpu_canvas_interop::offscreen;
inline constexpr auto platform_canvas_backend=wgpu::BackendType::Vulkan;
inline auto make_platform_webgpu_canvas_host(std::shared_ptr<platform_dawn_canvas_host> provider,
    std::function<image_metadata()> metadata) {return make_offscreen_webgpu_canvas_host(std::move(provider),std::move(metadata));}
}
#endif
#if defined(__APPLE__) || defined(_WIN32) || defined(__linux__)
namespace webscene::graphics {
// The offscreen image carries its originating Dawn device/completion lifetime.
// Do not strip that dependency when promoting a completed JS canvas into a scene.
inline std::shared_ptr<const webscene_gpu_image_lease_v3> take_platform_ready_gpu_image(
    const std::shared_ptr<platform_dawn_canvas_host>& provider) {
#if defined(__linux__)
    return provider->take_ready_image();
#else
    auto image=provider->take_ready();
    return image?std::make_shared<webscene_gpu_image_lease_v3>(std::move(*image)):nullptr;
#endif
}
}
#endif
