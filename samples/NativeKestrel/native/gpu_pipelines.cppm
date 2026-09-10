module;
#include <array>
#include <webgpu/webgpu_cpp.h>
export module kestrel.gpu_pipelines;
import kestrel.shaders;
export namespace kestrel {
struct gpu_pipelines {
  wgpu::Buffer uniform;
  wgpu::BindGroup bind;
  wgpu::RenderPipeline lines, mesh, xray;
  gpu_pipelines(const wgpu::Device &device, wgpu::TextureFormat format) {
    wgpu::ShaderSourceWGSL source;
    source.code = wgpu::StringView(shader_source.data(), shader_source.size());
    wgpu::ShaderModuleDescriptor shader_descriptor;
    shader_descriptor.nextInChain = &source;
    auto shader = device.CreateShaderModule(&shader_descriptor);
    wgpu::BufferDescriptor buffer;
    buffer.size = 96;
    buffer.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    uniform = device.CreateBuffer(&buffer);
    wgpu::BindGroupLayoutEntry entry;
    entry.binding = 0;
    entry.visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    entry.buffer.type = wgpu::BufferBindingType::Uniform;
    wgpu::BindGroupLayoutDescriptor bind_layout_descriptor;
    bind_layout_descriptor.entryCount = 1;
    bind_layout_descriptor.entries = &entry;
    auto bind_layout = device.CreateBindGroupLayout(&bind_layout_descriptor);
    wgpu::BindGroupEntry bind_entry;
    bind_entry.binding = 0;
    bind_entry.buffer = uniform;
    bind_entry.size = 96;
    wgpu::BindGroupDescriptor bind_descriptor;
    bind_descriptor.layout = bind_layout;
    bind_descriptor.entryCount = 1;
    bind_descriptor.entries = &bind_entry;
    bind = device.CreateBindGroup(&bind_descriptor);
    wgpu::PipelineLayoutDescriptor layout_descriptor;
    layout_descriptor.bindGroupLayoutCount = 1;
    layout_descriptor.bindGroupLayouts = &bind_layout;
    auto layout = device.CreatePipelineLayout(&layout_descriptor);
    wgpu::BlendState blend;
    blend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
    blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    blend.alpha.srcFactor = wgpu::BlendFactor::One;
    blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    wgpu::ColorTargetState target;
    target.format = format;
    target.blend = &blend;
    wgpu::FragmentState fragment;
    fragment.module = shader;
    fragment.targetCount = 1;
    fragment.targets = &target;
    wgpu::DepthStencilState depth;
    depth.format = wgpu::TextureFormat::Depth24Plus;
    depth.depthWriteEnabled = false;
    depth.depthCompare = wgpu::CompareFunction::LessEqual;
    depth.depthBias = -2;
    std::array<wgpu::VertexAttribute, 4> attributes{};
    const std::array<wgpu::VertexFormat, 4> formats{
        wgpu::VertexFormat::Float32x3, wgpu::VertexFormat::Float32x3,
        wgpu::VertexFormat::Float32x4, wgpu::VertexFormat::Float32x2};
    const std::array<uint64_t, 4> offsets{0, 12, 24, 40};
    for (size_t i = 0; i < 4; ++i) {
      attributes[i].shaderLocation = i;
      attributes[i].format = formats[i];
      attributes[i].offset = offsets[i];
    }
    wgpu::VertexBufferLayout vertices;
    vertices.arrayStride = 48;
    vertices.stepMode = wgpu::VertexStepMode::Instance;
    vertices.attributeCount = 4;
    vertices.attributes = attributes.data();
    wgpu::RenderPipelineDescriptor descriptor;
    descriptor.layout = layout;
    descriptor.vertex.module = shader;
    descriptor.vertex.entryPoint = "lineVertex";
    descriptor.vertex.bufferCount = 1;
    descriptor.vertex.buffers = &vertices;
    fragment.entryPoint = "lineFragment";
    descriptor.fragment = &fragment;
    descriptor.depthStencil = &depth;
    descriptor.multisample.count = 4;
    descriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    lines = device.CreateRenderPipeline(&descriptor);
    vertices.arrayStride = 40;
    vertices.stepMode = wgpu::VertexStepMode::Vertex;
    vertices.attributeCount = 3;
    descriptor.vertex.entryPoint = "meshVertex";
    fragment.entryPoint = "meshFragment";
    depth.depthBias = 0;
    depth.depthWriteEnabled = true;
    depth.depthCompare = wgpu::CompareFunction::Less;
    mesh = device.CreateRenderPipeline(&descriptor);
    depth.depthWriteEnabled = false;
    depth.depthCompare = wgpu::CompareFunction::LessEqual;
    xray = device.CreateRenderPipeline(&descriptor);
  }
};
} // namespace kestrel
