module;
#include "native_webgpu_surface.h"
#include <memory>
#include <stdexcept>
export module kestrel.viewport;
import kestrel.gpu_renderer;
export import kestrel.render_data;
export namespace kestrel {
// Host-independent viewport. Foco consumes the returned native image lease
// through its existing GPU image adapter; no compositor implementation lives
// here.
class viewport {
  webscene::graphics::native_webgpu_surface surface;
  gpu_renderer renderer;
  std::shared_ptr<webscene_gpu_image_snapshot> pending;
  uint32_t width, height;

public:
  camera camera;
  render_options options;
  bool grid_enabled = true;
  viewport(uint64_t canvas_id, uint32_t w, uint32_t h)
      : surface(canvas_id, w, h),
        renderer(surface.device(), wgpu::TextureFormat::BGRA8Unorm), width(w),
        height(h) {
    renderer.resize(w, h);
    camera.resize(w, h);
  }
  void resize(uint32_t w, uint32_t h) {
    if (!w || !h)
      throw std::invalid_argument("Viewport dimensions must be positive");
    if (w == width && h == height)
      return;
    // Discard an obsolete pending frame; retained compositor leases remain
    // valid.
    pending.reset();
    surface.resize(w, h);
    renderer.resize(w, h);
    camera.resize(w, h);
    width = w;
    height = h;
  }
  bool submit(const drawing &document) {
    if (pending)
      return false;
    auto texture = surface.current_texture();
    if (!texture)
      return false;
    auto scene = build_scene(document, camera.target, options);
    auto grid =
        build_grid(camera, camera.target, options.light_theme, grid_enabled);
    auto uniforms =
        make_camera_uniforms(camera, camera.target, width, height, options);
    renderer.render(texture.CreateView(), scene.buffers, uniforms, options,
                    &grid);
    pending = surface.present();
    if (!pending)
      throw std::runtime_error("Viewport submission produced no snapshot");
    return true;
  }
  std::shared_ptr<const webscene_gpu_image_lease_v3> poll() {
    surface.process_events();
    if (surface.failed())
      throw std::runtime_error("Viewport GPU failed");
    if (!pending)
      return {};
    auto image = pending->resolve();
    if (image)
      pending.reset();
    return image;
  }
};
} // namespace kestrel
