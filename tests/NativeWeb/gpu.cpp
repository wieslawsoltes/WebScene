#include "native_webgpu_device.h"
#include "native_webgpu_canvas_context.h"
// Diagnostic readback only. This executable is not a canvas presentation path.
#include <webgpu/webgpu_cpp.h>

#include <array>
#include <atomic>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace {
std::string text(wgpu::StringView value) {
    if (!value.data) return {};
    return value.length == WGPU_STRLEN ? std::string(value.data)
                                       : std::string(value.data, value.length);
}

std::string json(std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { result += '\\'; result += c; }
        else if (c < 32) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
        else result += c;
    }
    return result + '"';
}

int finish(std::string_view status, std::string_view reason, int code) {
    std::cout << "{\"schemaVersion\":1,\"probe\":\"dawn\",\"status\":" << json(status)
              << ",\"reason\":" << json(reason) << "}\n";
    return code;
}

bool wait(const wgpu::Instance& instance, wgpu::Future future) {
    return instance.WaitAny(future, 30'000'000'000ULL) == wgpu::WaitStatus::Success;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return finish("failed", "Specify d3d12, metal or vulkan", 1);
    const std::string backend = argv[1];
    wgpu::RequestAdapterOptions options{};
    if (backend == "d3d12") options.backendType = wgpu::BackendType::D3D12;
    else if (backend == "metal") options.backendType = wgpu::BackendType::Metal;
    else if (backend == "vulkan") options.backendType = wgpu::BackendType::Vulkan;
    else return finish("failed", "Unsupported backend", 1);
    options.forceFallbackAdapter = false;

    auto gpu = webscene::graphics::native_webgpu_device::create(options.backendType);
    const auto& instance = gpu.instance;
    const auto& device = gpu.device;
    auto& error = *gpu.failed;
    wgpu::AdapterInfo info{};
    if (gpu.adapter.GetInfo(&info) != wgpu::Status::Success) return 1;
    if (info.adapterType != wgpu::AdapterType::DiscreteGPU && info.adapterType != wgpu::AdapterType::IntegratedGPU) return 1;
    int retired = 0;
    webscene::graphics::webgpu_canvas_host host;
    host.validate = [](const auto&) {};
    host.acquire = [](const auto& config, const auto& descriptor) {
        wgpu::Texture texture;
        descriptor.with_native([&](const auto& native) { texture = config.device.CreateTexture(&native); });
        return texture;
    };
    host.retire = [&](const auto&, bool present) { if(present) ++retired; };
    webscene::graphics::native_webgpu_canvas_context canvas(17, 4, std::move(host));
    webscene::graphics::webgpu_canvas_configuration config;
    config.device = device;
    config.format = wgpu::TextureFormat::RGBA8Unorm;
    config.usage = (static_cast<uint32_t>(wgpu::TextureUsage::RenderAttachment) | static_cast<uint32_t>(wgpu::TextureUsage::CopySrc));
    canvas.configure(config);
    // Non-row-aligned width exercises the texture-copy layout, not just the first pixel.
    constexpr uint32_t width = 17, height = 4, rowBytes = 256;
    constexpr size_t bufferSize = rowBytes * height;
    auto texture = canvas.current_texture();
    if (canvas.current_texture().Get() != texture.Get()) return 1;
    wgpu::BufferDescriptor bufferDescriptor{};
    bufferDescriptor.size = bufferSize;
    bufferDescriptor.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
    auto buffer = device.CreateBuffer(&bufferDescriptor);
    wgpu::RenderPassColorAttachment color{};
    wgpu::TextureViewDescriptor view_descriptor{};
    view_descriptor.usage = wgpu::TextureUsage::RenderAttachment;
    color.view = texture.CreateView(&view_descriptor);
    color.loadOp = wgpu::LoadOp::Clear;
    color.storeOp = wgpu::StoreOp::Store;
    color.clearValue = {0.2, 0.4, 0.6, 1.0};
    wgpu::RenderPassDescriptor passDescriptor{};
    passDescriptor.colorAttachmentCount = 1;
    passDescriptor.colorAttachments = &color;
    auto encoder = device.CreateCommandEncoder();
    auto pass = encoder.BeginRenderPass(&passDescriptor);
    pass.End();
    wgpu::TexelCopyTextureInfo source{};
    source.texture = texture;
    wgpu::TexelCopyBufferInfo destination{};
    destination.buffer = buffer;
    destination.layout.bytesPerRow = rowBytes;
    destination.layout.rowsPerImage = height;
    const wgpu::Extent3D extent{width, height, 1};
    encoder.CopyTextureToBuffer(&source, &destination, &extent);
    auto commands = encoder.Finish();
    device.GetQueue().Submit(1, &commands);

    auto mapped = std::make_shared<bool>(false);
    auto mapFuture = buffer.MapAsync(wgpu::MapMode::Read, 0, bufferSize,
        wgpu::CallbackMode::WaitAnyOnly,
        [mapped](wgpu::MapAsyncStatus status, wgpu::StringView) {
            *mapped = status == wgpu::MapAsyncStatus::Success;
        });
    if (!wait(instance, mapFuture) || !*mapped || error.load())
        return finish("failed", "GPU clear/copy/map failed or timed out", 1);
    auto pixels = static_cast<const uint8_t*>(buffer.GetConstMappedRange(0, bufferSize));
    if (!pixels) return finish("failed", "Mapped range is null", 1);
    constexpr std::array<int, 4> expected{51, 102, 153, 255};
    bool valid = true;
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
            for (uint32_t c = 0; c < 4; ++c)
                valid &= std::abs(int(pixels[y * rowBytes + x * 4 + c]) - expected[c]) <= 1;
    buffer.Unmap();
    canvas.end_frame();
    if (retired != 1) return 1;
    canvas.resize(32, 16);
    if (canvas.current_texture().GetWidth() != 32) return 1;
    canvas.unconfigure();
    bool rejected = false;
    try { canvas.current_texture(); } catch (const std::logic_error&) { rejected = true; }
    if (!rejected) return 1;
    if (!valid) return finish("failed", "Readback pixels differ from the clear color", 1);
    std::cout << "{\"schemaVersion\":1,\"probe\":\"dawn\",\"status\":\"passed\","
              << "\"hardwareAccelerated\":true,\"backend\":" << json(backend)
              << ",\"adapter\":" << json(text(info.device))
              << ",\"vendor\":" << json(text(info.vendor))
              << ",\"driver\":" << json(text(info.description))
              << ",\"vendorId\":" << info.vendorID << ",\"deviceId\":" << info.deviceID
              << ",\"verifiedPixels\":" << width * height
              << ",\"expectedRGBA\":[51,102,153,255],\"tolerance\":1,\"diagnosticReadback\":true}\n";
    return 0;
}
