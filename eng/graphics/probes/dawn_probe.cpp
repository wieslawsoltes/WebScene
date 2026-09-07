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

    std::atomic<bool> error{false}; // Outlives the instance and every device callback.
    constexpr auto timedWait = wgpu::InstanceFeatureName::TimedWaitAny;
    wgpu::InstanceDescriptor instanceDescriptor{};
    instanceDescriptor.requiredFeatureCount = 1;
    instanceDescriptor.requiredFeatures = &timedWait;
    auto instance = wgpu::CreateInstance(&instanceDescriptor);
    if (!instance) return finish("failed", "Instance creation failed", 1);

    struct AdapterResult { wgpu::Adapter adapter; std::string message; };
    auto adapterResult = std::make_shared<AdapterResult>();
    auto adapterFuture = instance.RequestAdapter(&options, wgpu::CallbackMode::WaitAnyOnly,
        [adapterResult](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message) {
            if (status == wgpu::RequestAdapterStatus::Success) adapterResult->adapter = std::move(adapter);
            adapterResult->message = text(message);
        });
    if (!wait(instance, adapterFuture)) return finish("failed", "Adapter request timed out", 1);
    if (!adapterResult->adapter) return finish("unavailable", adapterResult->message, 77);
    const auto& adapter = adapterResult->adapter;
    wgpu::AdapterInfo info{};
    if (adapter.GetInfo(&info) != wgpu::Status::Success)
        return finish("failed", "Cannot inspect adapter", 1);
    if (info.backendType != options.backendType ||
        (info.adapterType != wgpu::AdapterType::DiscreteGPU &&
         info.adapterType != wgpu::AdapterType::IntegratedGPU))
        return finish("unavailable", "Selected adapter is not confirmed hardware on the requested backend", 77);

    struct DeviceResult { wgpu::Device device; std::string message; };
    auto deviceResult = std::make_shared<DeviceResult>();
    wgpu::DeviceDescriptor deviceDescriptor{};
    deviceDescriptor.SetUncapturedErrorCallback(
        [](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView, std::atomic<bool>* state) {
            state->store(true);
        }, &error);
    auto deviceFuture = adapter.RequestDevice(&deviceDescriptor, wgpu::CallbackMode::WaitAnyOnly,
        [deviceResult](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message) {
            if (status == wgpu::RequestDeviceStatus::Success) deviceResult->device = std::move(device);
            deviceResult->message = text(message);
        });
    if (!wait(instance, deviceFuture)) return finish("failed", "Device request timed out", 1);
    if (!deviceResult->device) return finish("failed", deviceResult->message, 1);
    const auto& device = deviceResult->device;

    // Non-row-aligned width exercises the texture-copy layout, not just the first pixel.
    constexpr uint32_t width = 17, height = 4, rowBytes = 256;
    constexpr size_t bufferSize = rowBytes * height;
    wgpu::TextureDescriptor textureDescriptor{};
    textureDescriptor.size = {width, height, 1};
    textureDescriptor.format = wgpu::TextureFormat::RGBA8Unorm;
    textureDescriptor.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
    auto texture = device.CreateTexture(&textureDescriptor);
    wgpu::BufferDescriptor bufferDescriptor{};
    bufferDescriptor.size = bufferSize;
    bufferDescriptor.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
    auto buffer = device.CreateBuffer(&bufferDescriptor);
    wgpu::RenderPassColorAttachment color{};
    color.view = texture.CreateView();
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
