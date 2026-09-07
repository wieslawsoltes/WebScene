#if defined(__APPLE__)
#include <IOSurface/IOSurface.h>
#include <CoreVideo/CoreVideo.h>
#endif
#include "../../../../experiments/WebScene.NativeEngine.Probe/native/graphics/dawn_canvas_images.h"
#include "include/gpu/graphite/dawn/DawnBackendContext.h"
#include "include/gpu/graphite/dawn/DawnGraphiteTypes.h"
#include "include/gpu/graphite/BackendTexture.h"
#include "include/gpu/graphite/Context.h"
#include "include/gpu/graphite/ContextOptions.h"
#include "include/gpu/graphite/Recorder.h"
#include "include/gpu/graphite/Recording.h"
#include "include/gpu/graphite/Surface.h"
#include "include/core/SkSurface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkImage.h"
#include "include/gpu/graphite/Image.h"
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
    std::cout << "{\"schemaVersion\":1,\"probe\":\"graphite-shared-device\",\"status\":" << json(status)
              << ",\"reason\":" << json(reason) << "}\n";
    return code;
}

bool wait(const wgpu::Instance& instance, wgpu::Future future) {
    return instance.WaitAny(future, 30'000'000'000ULL) == wgpu::WaitStatus::Success;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) return finish("failed", "Specify d3d12, metal or vulkan", 1);
    const std::string backend = argv[1];
    const bool sharedOutput=argc==3 && std::string(argv[2])=="iosurface";
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
    const std::array sharedFeatures{wgpu::FeatureName::SharedTextureMemoryIOSurface,
        wgpu::FeatureName::SharedFenceMTLSharedEvent};
    if (sharedOutput) {
        for (auto feature:sharedFeatures) if (!adapter.HasFeature(feature))
            return finish("unavailable","IOSurface sharing features unavailable",77);
        deviceDescriptor.requiredFeatureCount=sharedFeatures.size();
        deviceDescriptor.requiredFeatures=sharedFeatures.data();
    }
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
    textureDescriptor.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::TextureBinding;
    wgpu::SharedTextureMemory sharedMemory;
    wgpu::Texture texture;
    if (sharedOutput) {
#if defined(__APPLE__)
        auto dictionary=CFDictionaryCreateMutable(nullptr,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
        auto add=[&](CFStringRef key,int32_t value) {
            auto number=CFNumberCreate(nullptr,kCFNumberSInt32Type,&value);
            CFDictionarySetValue(dictionary,key,number); CFRelease(number);
        };
        add(kIOSurfaceWidth,width); add(kIOSurfaceHeight,height);
        add(kIOSurfaceBytesPerElement,4); add(kIOSurfacePixelFormat,kCVPixelFormatType_32RGBA);
        auto ioSurface=IOSurfaceCreate(dictionary); CFRelease(dictionary);
        if (!ioSurface) return finish("failed","IOSurface allocation failed",1);
        wgpu::SharedTextureMemoryIOSurfaceDescriptor io{}; io.ioSurface=ioSurface;
        wgpu::SharedTextureMemoryDescriptor descriptor{}; descriptor.nextInChain=&io;
        sharedMemory=device.ImportSharedTextureMemory(&descriptor); CFRelease(ioSurface);
        wgpu::SharedTextureMemoryProperties properties{};
        if (!sharedMemory || sharedMemory.GetProperties(&properties)!=wgpu::Status::Success)
            return finish("failed","IOSurface import failed",1);
        texture=sharedMemory.CreateTexture(&textureDescriptor);
        wgpu::SharedTextureMemoryBeginAccessDescriptor access{}; access.initialized=false;
        if (sharedMemory.BeginAccess(texture,&access)!=wgpu::Status::Success)
            return finish("failed","IOSurface BeginAccess failed",1);
#else
        return finish("unavailable","IOSurface requires macOS",77);
#endif
    } else texture=device.CreateTexture(&textureDescriptor);
    wgpu::BufferDescriptor bufferDescriptor{};
    bufferDescriptor.size = bufferSize;
    bufferDescriptor.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
    auto buffer = device.CreateBuffer(&bufferDescriptor);
    skgpu::graphite::DawnBackendContext backendContext;
    backendContext.fInstance=instance; backendContext.fDevice=device;
    backendContext.fQueue=device.GetQueue();
    auto graphite=skgpu::graphite::ContextFactory::MakeDawn(backendContext,{});
    if (!graphite) return finish("failed","Graphite context creation failed",1);
    // Native WebGPU producer writes a separate image, then Graphite samples it
    // on the same queue. No CPU wait or pixel upload sits between the submissions.
    using namespace webscene::graphics;
    auto canvasOwner=std::make_unique<dawn_canvas_images>(device,3*width*height*4);
    auto frame=canvasOwner->acquire(image_metadata{700,0,1,1,701,1,width,height});
    if (!frame) return finish("failed","Canvas allocation failed",1);
    auto produced=frame->texture;
    auto producerEncoder=device.CreateCommandEncoder();
    wgpu::RenderPassColorAttachment attachment{};
    attachment.view=produced.CreateView(); attachment.loadOp=wgpu::LoadOp::Clear;
    attachment.storeOp=wgpu::StoreOp::Store; attachment.clearValue={1,0,0,1};
    wgpu::RenderPassDescriptor producerPass{};
    producerPass.colorAttachmentCount=1; producerPass.colorAttachments=&attachment;
    auto render=producerEncoder.BeginRenderPass(&producerPass); render.End();
    auto producerCommands=producerEncoder.Finish();
    auto submitted=canvasOwner->submit(std::move(*frame),producerCommands); frame.reset();
    if (!submitted) return finish("failed","Canvas submission backpressure",1);
    auto consumer=submitted->image.begin_consumer();
    if (!consumer) return finish("failed","Canvas consumer backpressure",1);
    auto resized=canvasOwner->acquire(image_metadata{700,0,2,2,701,2,9,height});
    if (!resized || resized->metadata.allocation==consumer->describe().allocation
        || consumer->describe().width!=width)
        return finish("failed","Resize changed the retained allocation",1);
    auto resizeEncoder=device.CreateCommandEncoder();
    attachment.view=resized->texture.CreateView(); attachment.clearValue={0,1,0,1};
    auto resizePass=resizeEncoder.BeginRenderPass(&producerPass); resizePass.End();
    auto resizeCommands=resizeEncoder.Finish();
    auto replacement=canvasOwner->submit(std::move(*resized),resizeCommands); resized.reset();
    if (!replacement) return finish("failed","Replacement submission failed",1);
    auto replacementConsumer=replacement->image.begin_consumer();
    if (!replacementConsumer) return finish("failed","Replacement consumer failed",1);
    replacement.reset();
    submitted.reset(); canvasOwner.reset();
    produced=nullptr; attachment.view=nullptr;
    // Resolve through the real lease after canvas and scene ownership ends.
    auto retainedTexture=dawn_canvas_images::resolve(*consumer,device);
    auto replacementTexture=dawn_canvas_images::resolve(*replacementConsumer,device);
    auto recorder=graphite->makeRecorder();
    auto backendTexture=skgpu::graphite::BackendTextures::MakeDawn(texture.Get());
    auto surface=SkSurfaces::WrapBackendTexture(recorder.get(),backendTexture,nullptr,nullptr);
    if (!surface) return finish("failed","Graphite texture wrapping failed",1);
    surface->getCanvas()->clear(SkColorSetARGB(255,51,102,153));
    auto image=SkImages::WrapTexture(recorder.get(),
        skgpu::graphite::BackendTextures::MakeDawn(retainedTexture.Get()),kPremul_SkAlphaType,nullptr,
        skgpu::Origin::kTopLeft,SkImages::GenerateMipmapsFromBase::kNo);
    if (!image) return finish("failed","Graphite source image wrapping failed",1);
    auto canvas=surface->getCanvas();
    canvas->save(); canvas->clipRect(SkRect::MakeLTRB(2,1,8,3)); canvas->translate(1,0);
    SkPaint paint; paint.setAlphaf(0.5f);
    canvas->drawImage(image,0,0,SkSamplingOptions(),&paint); canvas->restore();
    auto replacementImage=SkImages::WrapTexture(recorder.get(),
        skgpu::graphite::BackendTextures::MakeDawn(replacementTexture.Get()),kPremul_SkAlphaType,nullptr,
        skgpu::Origin::kTopLeft,SkImages::GenerateMipmapsFromBase::kNo);
    if (!replacementImage) return finish("failed","Replacement image wrapping failed",1);
    canvas->save(); canvas->clipRect(SkRect::MakeLTRB(10,1,15,3));
    canvas->drawImage(replacementImage,9,0); canvas->restore();
    auto recording=recorder->snap();
    skgpu::graphite::InsertRecordingInfo insert; insert.fRecording=recording.get();
    if (!graphite->insertRecording(insert) || !graphite->submit())
        return finish("failed","Graphite submission failed",1);
    auto encoder=device.CreateCommandEncoder();
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

    wgpu::SharedTextureMemoryEndAccessState handoff;
    if (sharedOutput && sharedMemory.EndAccess(texture,&handoff)!=wgpu::Status::Success)
        return finish("failed","IOSurface EndAccess failed",1);
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
            for (uint32_t c = 0; c < 4; ++c) {
                constexpr std::array<int,4> blended{153,51,77,255};
                constexpr std::array<int,4> green{0,255,0,255};
                const auto& wanted=(x>=10 && x<15 && y>=1 && y<3) ? green
                    : (x>=2 && x<8 && y>=1 && y<3) ? blended : expected;
                valid &= std::abs(int(pixels[y * rowBytes + x * 4 + c]) - wanted[c]) <= 1;
            }
    buffer.Unmap();
    // The mapped readback follows Graphite submission on the same queue, proving
    // this consumer's GPU sampling is complete before its lease is retired.
    image.reset(); retainedTexture=nullptr;
    consumer->complete(); consumer.reset();
    replacementImage.reset(); replacementTexture=nullptr;
    replacementConsumer->complete(); replacementConsumer.reset();
    if (!valid) return finish("failed", "Readback pixels differ from clipped image composition", 1);
    std::cout << "{\"schemaVersion\":1,\"probe\":\"graphite-shared-device\",\"status\":\"passed\","
              << "\"hardwareAccelerated\":true,\"backend\":" << json(backend)
              << ",\"adapter\":" << json(text(info.device))
              << ",\"vendor\":" << json(text(info.vendor))
              << ",\"driver\":" << json(text(info.description))
              << ",\"vendorId\":" << info.vendorID << ",\"deviceId\":" << info.deviceID
              << ",\"iosurfaceOutput\":" << (sharedOutput ? "true" : "false")
              << ",\"verifiedPixels\":" << width * height
              << ",\"backgroundRGBA\":[51,102,153,255],\"compositedRGBA\":[153,51,77,255],\"tolerance\":1,\"diagnosticReadback\":true}\n";
    return 0;
}
