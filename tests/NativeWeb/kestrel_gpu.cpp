#include "native_webgpu_device.h"
#include <stdexcept>
import kestrel.gpu_pipelines;
import kestrel.gpu_renderer;
import kestrel.render_data;
int main() {
  auto gpu = webscene::graphics::native_webgpu_device::create(
      wgpu::BackendType::Metal);
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
}
