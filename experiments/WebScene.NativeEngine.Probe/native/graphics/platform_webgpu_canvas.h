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
#endif
