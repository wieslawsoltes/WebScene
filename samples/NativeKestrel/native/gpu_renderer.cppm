module;
#include <bit>
#include <cstdint>
#include <stdexcept>
#include <webgpu/webgpu_cpp.h>
export module kestrel.gpu_renderer;
import kestrel.gpu_pipelines;
import kestrel.render_data;
export namespace kestrel {
class gpu_renderer {
  wgpu::Device device;
  wgpu::TextureFormat format;
  gpu_pipelines pipelines;
  wgpu::Texture depth, multisample;
  struct vertex_buffer {
    wgpu::Buffer buffer;
    uint64_t capacity{};
  };
  vertex_buffer lines, triangles, grid_lines;
  uint32_t width{}, height{};
  void upload(vertex_buffer &entry, const void *data, uint64_t bytes) {
    if (!bytes)
      return;
    if (entry.capacity < bytes) {
      entry.capacity = std::bit_ceil(bytes < 256 ? uint64_t{256} : bytes);
      wgpu::BufferDescriptor descriptor;
      descriptor.size = entry.capacity;
      descriptor.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
      entry.buffer = device.CreateBuffer(&descriptor);
    }
    device.GetQueue().WriteBuffer(entry.buffer, 0, data, bytes);
  }

public:
  gpu_renderer(wgpu::Device device, wgpu::TextureFormat format)
      : device(device), format(format), pipelines(device, format) {}
  void resize(uint32_t w, uint32_t h) {
    if (!w || !h)
      throw std::invalid_argument("Viewport dimensions must be positive");
    if (w == width && h == height)
      return;
    wgpu::TextureDescriptor descriptor;
    descriptor.size = {w, h, 1};
    descriptor.sampleCount = 4;
    descriptor.usage = wgpu::TextureUsage::RenderAttachment;
    descriptor.format = format;
    multisample = device.CreateTexture(&descriptor);
    descriptor.format = wgpu::TextureFormat::Depth24Plus;
    depth = device.CreateTexture(&descriptor);
    width = w;
    height = h;
  }
  uint32_t render(const wgpu::TextureView &target, const render_data &data,
                  const camera_uniforms &uniforms, render_options options = {},
                  const render_data *grid = nullptr, bool upload_scene = true) {
    if (!width || !height)
      throw std::logic_error("Resize viewport before rendering");
    if (grid)
      upload(grid_lines, grid->lines.data(),
             grid->lines.size() * sizeof(line_instance));
    if (upload_scene) {
      upload(lines, data.lines.data(),
             data.lines.size() * sizeof(line_instance));
      upload(triangles, data.triangles.data(),
             data.triangles.size() * sizeof(triangle_vertex));
    }
    auto queue = device.GetQueue();
    queue.WriteBuffer(pipelines.uniform, 0, &uniforms, sizeof(uniforms));
    wgpu::RenderPassColorAttachment color;
    color.view = multisample.CreateView();
    color.resolveTarget = target;
    color.loadOp = wgpu::LoadOp::Clear;
    color.storeOp = wgpu::StoreOp::Store;
    auto background = render_color(options.light_theme ? "#edf2f6" : "#121c29");
    color.clearValue = {background[0], background[1], background[2], 1};
    wgpu::RenderPassDepthStencilAttachment depth_attachment;
    depth_attachment.view = depth.CreateView();
    depth_attachment.depthClearValue = 1;
    depth_attachment.depthLoadOp = wgpu::LoadOp::Clear;
    depth_attachment.depthStoreOp = wgpu::StoreOp::Store;
    wgpu::RenderPassDescriptor descriptor;
    descriptor.colorAttachmentCount = 1;
    descriptor.colorAttachments = &color;
    descriptor.depthStencilAttachment = &depth_attachment;
    auto encoder = device.CreateCommandEncoder();
    auto pass = encoder.BeginRenderPass(&descriptor);
    pass.SetBindGroup(0, pipelines.bind);
    uint32_t calls = 0;
    if (grid && !grid->lines.empty()) {
      pass.SetPipeline(pipelines.lines);
      pass.SetVertexBuffer(0, grid_lines.buffer);
      pass.Draw(6, static_cast<uint32_t>(grid->lines.size()));
      ++calls;
    }
    if (!data.triangles.empty()) {
      pass.SetPipeline(options.style == display_style::xray ? pipelines.xray
                                                            : pipelines.mesh);
      pass.SetVertexBuffer(0, triangles.buffer);
      pass.Draw(static_cast<uint32_t>(data.triangles.size()));
      ++calls;
    }
    if (!data.lines.empty()) {
      pass.SetPipeline(pipelines.lines);
      pass.SetVertexBuffer(0, lines.buffer);
      pass.Draw(6, static_cast<uint32_t>(data.lines.size()));
      ++calls;
    }
    pass.End();
    auto commands = encoder.Finish();
    queue.Submit(1, &commands);
    return calls;
  }
};
} // namespace kestrel
