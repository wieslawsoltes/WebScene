#include "native_webgpu_device.h"
#include <stdexcept>
import kestrel.gpu_pipelines;
int main() {
  auto gpu=webscene::graphics::native_webgpu_device::create(wgpu::BackendType::Metal);
  kestrel::gpu_pipelines pipelines(gpu.device,wgpu::TextureFormat::BGRA8Unorm);
  gpu.instance.ProcessEvents();
  if(*gpu.failed||!pipelines.lines||!pipelines.mesh||!pipelines.xray||!pipelines.bind)
    throw std::runtime_error("Native Kestrel pipeline validation failed");
}
