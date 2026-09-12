#include "../../samples/NativeKestrel/third_party/nlohmann/json.hpp"
#include "native_webgpu_device.h"
#include "native_webgpu_surface.h"
#include <chrono>
#include <algorithm>
#include <iostream>
#include <vector>
#include <string_view>
#include <stdexcept>
#include <thread>
import kestrel.gpu_pipelines;
import kestrel.viewport;
import kestrel.gpu_renderer;
import kestrel.render_data;
int main(int argc, char **argv) {
#if defined(__linux__)
  const auto headless_options = webscene::graphics::headless_webgpu_options::environment();
  auto gpu = webscene::graphics::native_webgpu_device::create(
      wgpu::BackendType::Vulkan, {}, headless_options.force_software);
  wgpu::AdapterInfo adapter_info{};
  if (gpu.adapter.GetInfo(&adapter_info) != wgpu::Status::Success ||
      (adapter_info.adapterType == wgpu::AdapterType::CPU && !headless_options.allow_software))
    throw std::runtime_error("Linux Kestrel test requires an identified, explicitly authorized adapter");
#else
  auto gpu = webscene::graphics::native_webgpu_device::create(
      wgpu::BackendType::Metal);
#endif
  kestrel::gpu_pipelines pipelines(gpu.device, wgpu::TextureFormat::BGRA8Unorm);
  gpu.instance.ProcessEvents();
  if (*gpu.failed || !pipelines.lines || !pipelines.mesh || !pipelines.xray ||
      !pipelines.bind)
    throw std::runtime_error("Native Kestrel pipeline validation failed");

  kestrel::gpu_renderer renderer(gpu.device, wgpu::TextureFormat::BGRA8Unorm);
  renderer.resize(64, 64);
  wgpu::TextureDescriptor target_descriptor;
  target_descriptor.size = {64, 64, 1};
  target_descriptor.format = wgpu::TextureFormat::BGRA8Unorm;
  target_descriptor.usage =
      wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
  auto target = gpu.device.CreateTexture(&target_descriptor);
  kestrel::render_data data;
  data.add_triangle({{kestrel::vec3{-.8, -.8, .5}, kestrel::vec3{.8, -.8, .5},
                      kestrel::vec3{0, .8, .5}},
                     {0, 0, 1}},
                    {1, 0, 0, 1});
  data.add_line({kestrel::vec3{-.8, 0, .4}, kestrel::vec3{.8, 0, .4}},
                {0, 1, 0, 1}, 3, 0);
  kestrel::camera_uniforms uniforms{};
  uniforms.mvp = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  uniforms.eye = {0, 0, 2, 1};
  uniforms.viewport = {64, 64, 0, 0};
  if (renderer.render(target.CreateView(), data, uniforms) != 2)
    throw std::runtime_error("Expected mesh and line draw calls");
  wgpu::BufferDescriptor readback_descriptor;
  readback_descriptor.size = 64 * 256;
  readback_descriptor.usage =
      wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
  auto readback = gpu.device.CreateBuffer(&readback_descriptor);
  auto encoder = gpu.device.CreateCommandEncoder();
  wgpu::TexelCopyTextureInfo source;
  source.texture = target;
  wgpu::TexelCopyBufferInfo destination;
  destination.buffer = readback;
  destination.layout.bytesPerRow = 256;
  destination.layout.rowsPerImage = 64;
  wgpu::Extent3D extent{64, 64, 1};
  encoder.CopyTextureToBuffer(&source, &destination, &extent);
  auto commands = encoder.Finish();
  gpu.device.GetQueue().Submit(1, &commands);
  bool mapped = false;
  auto future = readback.MapAsync(
      wgpu::MapMode::Read, 0, 64 * 256, wgpu::CallbackMode::WaitAnyOnly,
      [&](wgpu::MapAsyncStatus status, wgpu::StringView) {
        mapped = status == wgpu::MapAsyncStatus::Success;
      });
  if (gpu.instance.WaitAny(future, 30'000'000'000ULL) !=
          wgpu::WaitStatus::Success ||
      !mapped || *gpu.failed)
    throw std::runtime_error("Frame readback failed");
  auto pixels =
      static_cast<const unsigned char *>(readback.GetConstMappedRange());
  size_t red = 0, green = 0, background = 0;
  for (size_t i = 0; i < 64 * 64; ++i) {
    auto p = pixels + i * 4;
    if (p[2] > 200 && p[1] < 30)
      ++red;
    if (p[1] > 200 && p[2] < 30)
      ++green;
    if (p[0] > 35 && p[0] < 46 && p[1] > 20 && p[1] < 33 && p[2] < 25)
      ++background;
  }
  readback.Unmap();
  if (red < 500 || green < 80 || background < 1000)
    throw std::runtime_error(
        "Rendered frame missing triangle, line or background");
  kestrel::viewport viewport(77, 160, 120);
  kestrel::drawing drawing;
  drawing.add("MESH", kestrel::geo::box({-10, -10, 0}, 20, 20, 20));
  viewport.options.style = kestrel::display_style::shaded_edges;
  for (int frame = 0; frame < 5; ++frame) {
    viewport.resize(160 + frame * 16, 120 + frame * 8);
    viewport.camera.pan(10, 5);
    if (frame == 3)
      viewport.options.style = kestrel::display_style::wireframe;
    if (frame == 4)
      viewport.invalidate_scene();
    if (!viewport.submit(drawing) || viewport.submit(drawing))
      throw std::runtime_error("Viewport pending-frame contract failed");
    if (viewport.scene_build_count() != (frame < 3 ? 1u : unsigned(frame - 1)))
      throw std::runtime_error(
          "Camera-only frame rebuilt scene or invalidation was lost");
    std::shared_ptr<const webscene_gpu_image_lease_v3> image;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do {
      image = viewport.poll();
      if (!image)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (!image && std::chrono::steady_clock::now() < deadline);
    if (!image)
      throw std::runtime_error("Viewport shared image timed out");
    auto metadata = image->value.describe();
    if (metadata.width != unsigned(160 + frame * 16) ||
        metadata.height != unsigned(120 + frame * 8))
      throw std::runtime_error("Viewport published stale resize dimensions");
  }
  // Resize while a frame is still pending, then ensure it cannot escape as
  // content for the new allocation. This models consecutive live-resize ticks.
  if (!viewport.submit(drawing)) throw std::runtime_error("Resize probe submission failed");
  viewport.resize(333, 217);
  if (viewport.poll()) throw std::runtime_error("Obsolete pending frame survived resize");
  if (!viewport.submit(drawing)) throw std::runtime_error("Resized submission rejected");
  std::shared_ptr<const webscene_gpu_image_lease_v3> resized;
  auto resize_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!(resized = viewport.poll())) {
    if (std::chrono::steady_clock::now() >= resize_deadline)
      throw std::runtime_error("Resized image timed out");
    std::this_thread::yield();
  }
  auto resized_metadata = resized->value.describe();
  if (resized_metadata.width != 333 || resized_metadata.height != 217)
    throw std::runtime_error("Resized image allocation dimensions mismatch");
  if (argc == 2 && std::string_view(argv[1]) == "--benchmark") {
    for (bool resizing : {false, true}) {
      std::vector<double> timings;
      viewport.resize(1280, 720);
      for (int frame = 0; frame < 140; ++frame) {
        auto started = std::chrono::steady_clock::now();
        if (resizing) viewport.resize(1280 + (frame % 40) * 4, 720 + (frame % 40) * 2);
        viewport.camera.pan(3, 1);
        if (!viewport.submit(drawing)) throw std::runtime_error("Benchmark submission rejected");
        auto deadline = started + std::chrono::seconds(10);
        while (!viewport.poll()) {
          if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("Benchmark GPU completion timed out");
          std::this_thread::yield();
        }
        if (frame >= 20) timings.push_back(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count());
      }
      std::sort(timings.begin(), timings.end());
      std::cout << (resizing ? "resize+pan" : "pan")
                << " submit-to-image ms: median=" << timings[timings.size()/2]
                << " p95=" << timings[timings.size()*95/100]
                << " max=" << timings.back()
                << " over16.67=" << std::count_if(timings.begin(), timings.end(),
                    [](double ms) { return ms > 1000.0/60; })
                << "/" << timings.size() << '\n';
    }
  }

}
