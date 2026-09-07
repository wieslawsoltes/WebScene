#if defined(__APPLE__)
#include <IOSurface/IOSurface.h>
#include <CoreVideo/CoreVideo.h>
#include "iosurface_gl_check.h"
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
#include <functional>
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

struct DawnRuntime {
    // Declare callback storage first so it outlives device destruction.
    std::shared_ptr<std::atomic<bool>> error=std::make_shared<std::atomic<bool>>(false);
    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;
    std::shared_ptr<skgpu::graphite::Context> graphite;
    std::shared_ptr<void> outputSurface;
    wgpu::SharedTextureMemory outputMemory;
    wgpu::Texture outputTexture;
    std::shared_ptr<webscene::graphics::dawn_canvas_images> canvasPool;
    unsigned outputAllocations=0;
    unsigned initializations=0;
    unsigned graphiteInitializations=0;
};
static thread_local DawnRuntime hostRuntime;
static thread_local unsigned hostDestinationTexture=0;
static thread_local unsigned hostFrameSerial=0;
static thread_local std::function<int(bool)> pendingProducer;
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

    DawnRuntime standaloneRuntime;
    auto& runtime=hostDestinationTexture ? hostRuntime : standaloneRuntime;
    auto error=runtime.error;
    if (error->load()) return finish("failed","Dawn runtime has an uncaptured error",1);
    auto& instance=runtime.instance;
    auto& adapter=runtime.adapter;
    auto& device=runtime.device;
    if (!instance) {
        constexpr auto timedWait = wgpu::InstanceFeatureName::TimedWaitAny;
        wgpu::InstanceDescriptor instanceDescriptor{};
        instanceDescriptor.requiredFeatureCount = 1;
        instanceDescriptor.requiredFeatures = &timedWait;
        instance = wgpu::CreateInstance(&instanceDescriptor);
        if (!instance) return finish("failed", "Instance creation failed", 1);
    }
    if (!adapter) {
        struct AdapterResult { wgpu::Adapter adapter; std::string message; };
        auto adapterResult = std::make_shared<AdapterResult>();
        auto adapterFuture = instance.RequestAdapter(&options, wgpu::CallbackMode::WaitAnyOnly,
            [adapterResult](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message) {
                if (status == wgpu::RequestAdapterStatus::Success) adapterResult->adapter = std::move(adapter);
                adapterResult->message = text(message);
            });
        if (!wait(instance, adapterFuture)) return finish("failed", "Adapter request timed out", 1);
        if (!adapterResult->adapter) return finish("unavailable", adapterResult->message, 77);
        adapter = adapterResult->adapter;
    }
    wgpu::AdapterInfo info{};
    if (adapter.GetInfo(&info) != wgpu::Status::Success)
        return finish("failed", "Cannot inspect adapter", 1);
    if (info.backendType != options.backendType ||
        (info.adapterType != wgpu::AdapterType::DiscreteGPU &&
         info.adapterType != wgpu::AdapterType::IntegratedGPU))
        return finish("unavailable", "Selected adapter is not confirmed hardware on the requested backend", 77);

    if (!device) {
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
            }, error.get());
        auto deviceFuture = adapter.RequestDevice(&deviceDescriptor, wgpu::CallbackMode::WaitAnyOnly,
            [deviceResult](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message) {
                if (status == wgpu::RequestDeviceStatus::Success) deviceResult->device = std::move(device);
                deviceResult->message = text(message);
            });
        if (!wait(instance, deviceFuture)) return finish("failed", "Device request timed out", 1);
        if (!deviceResult->device) return finish("failed", deviceResult->message, 1);
        device = deviceResult->device;
        ++runtime.initializations;
    }

    // Non-row-aligned width exercises the texture-copy layout, not just the first pixel.
    constexpr uint32_t width = 17, height = 4, rowBytes = 256;
    constexpr size_t bufferSize = rowBytes * height;
    wgpu::TextureDescriptor textureDescriptor{};
    textureDescriptor.size = {width, height, 1};
    textureDescriptor.format = sharedOutput ? wgpu::TextureFormat::BGRA8Unorm : wgpu::TextureFormat::RGBA8Unorm;
    textureDescriptor.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::TextureBinding;
    std::shared_ptr<void> sharedSurface;
    wgpu::SharedTextureMemory sharedMemory;
    wgpu::Texture texture;
    if (sharedOutput) {
#if defined(__APPLE__)
        if (runtime.outputTexture) {
            sharedSurface=runtime.outputSurface;
            sharedMemory=runtime.outputMemory;
            texture=runtime.outputTexture;
        } else {
            auto dictionary=CFDictionaryCreateMutable(nullptr,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
            auto add=[&](CFStringRef key,int32_t value) {
                auto number=CFNumberCreate(nullptr,kCFNumberSInt32Type,&value);
                CFDictionarySetValue(dictionary,key,number); CFRelease(number);
            };
            add(kIOSurfaceWidth,width); add(kIOSurfaceHeight,height);
            add(kIOSurfaceBytesPerElement,4); add(kIOSurfacePixelFormat,kCVPixelFormatType_32BGRA);
            auto ioSurface=IOSurfaceCreate(dictionary); CFRelease(dictionary);
            if (!ioSurface) return finish("failed","IOSurface allocation failed",1);
            wgpu::SharedTextureMemoryIOSurfaceDescriptor io{}; io.ioSurface=ioSurface;
            wgpu::SharedTextureMemoryDescriptor descriptor{}; descriptor.nextInChain=&io;
            sharedSurface=std::shared_ptr<void>(ioSurface,[](void* value) { CFRelease(value); });
            sharedMemory=device.ImportSharedTextureMemory(&descriptor);
            wgpu::SharedTextureMemoryProperties properties{};
            if (!sharedMemory || sharedMemory.GetProperties(&properties)!=wgpu::Status::Success)
                return finish("failed","IOSurface import failed",1);
            texture=sharedMemory.CreateTexture(&textureDescriptor);
            runtime.outputSurface=sharedSurface;
            runtime.outputMemory=sharedMemory;
            runtime.outputTexture=texture;
            ++runtime.outputAllocations;
        }
        // The host entry point rejects reuse until producer and CGL work retire.
        // Every submission fully clears the output, so prior contents are discarded.
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
    const bool verifyPixels=hostDestinationTexture==0;
    wgpu::Buffer buffer;
    if (verifyPixels) buffer=device.CreateBuffer(&bufferDescriptor);
    skgpu::graphite::DawnBackendContext backendContext;
    backendContext.fInstance=instance; backendContext.fDevice=device;
    backendContext.fQueue=device.GetQueue();
    if (!runtime.graphite) {
        runtime.graphite=skgpu::graphite::ContextFactory::MakeDawn(backendContext,{});
        if (!runtime.graphite) return finish("failed","Graphite context creation failed",1);
        ++runtime.graphiteInitializations;
    }
    auto graphite=runtime.graphite;
    // Native WebGPU producer writes a separate image, then Graphite samples it
    // on the same queue. No CPU wait or pixel upload sits between the submissions.
    using namespace webscene::graphics;
    if (hostDestinationTexture && !runtime.canvasPool)
        runtime.canvasPool=std::make_shared<dawn_canvas_images>(device,3*width*height*4);
    auto canvasOwner=hostDestinationTexture ? runtime.canvasPool :
        std::make_shared<dawn_canvas_images>(device,3*width*height*4);
    const uint64_t generation=hostDestinationTexture ? uint64_t(hostFrameSerial)*2+1 : 1;
    auto frame=canvasOwner->acquire(image_metadata{700,0,generation,generation,701,generation,width,height});
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
    auto resized=canvasOwner->acquire(image_metadata{700,0,generation+1,generation+1,701,generation+1,9,height});
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
    auto producerStatus=submitted->status, replacementStatus=replacement->status;
    replacement.reset();
    submitted.reset(); canvasOwner.reset();
    produced=nullptr; attachment.view=nullptr;
    // Resolve after scene ownership ends; standalone also destroys the canvas owner.
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
    if (hostDestinationTexture) {
        SkPaint marker;
        marker.setColor(SkColorSetARGB(255,hostFrameSerial & 255,(hostFrameSerial >> 8) & 255,0));
        canvas->drawRect(SkRect::MakeLTRB(16,0,17,1),marker);
    }
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
    if (verifyPixels) encoder.CopyTextureToBuffer(&source, &destination, &extent);
    auto commands = encoder.Finish();
    device.GetQueue().Submit(1, &commands);

    wgpu::SharedTextureMemoryEndAccessState handoff;
    if (sharedOutput && sharedMemory.EndAccess(texture,&handoff)!=wgpu::Status::Success)
        return finish("failed","IOSurface EndAccess failed",1);
    const uint8_t* pixels=nullptr;
    bool valid=true;
    if (!verifyPixels) {
#if defined(__APPLE__)
        auto completed=std::make_shared<std::atomic<int>>(0);
        auto future=device.GetQueue().OnSubmittedWorkDone(wgpu::CallbackMode::AllowSpontaneous,
            [completed](wgpu::QueueWorkDoneStatus status,wgpu::StringView) {
                completed->store(status==wgpu::QueueWorkDoneStatus::Success ? 1 : -1,std::memory_order_release);
            });
        // Keep every source lease and Graphite object alive while native GPU
        // work is outstanding. Delivery runs later on the host's CGL thread.
        auto deliver=[completed,future,instance,device,error,sharedSurface,sharedMemory,producerStatus,replacementStatus,
            target=hostDestinationTexture,context=CGLGetCurrentContext(),
            graphite=std::move(graphite),recorder=std::move(recorder),recording=std::move(recording),
            surface=std::move(surface),texture=std::move(texture),image=std::move(image),
            replacementImage=std::move(replacementImage),retainedTexture=std::move(retainedTexture),
            replacementTexture=std::move(replacementTexture),consumer=std::move(consumer),
            replacementConsumer=std::move(replacementConsumer)](bool drain) mutable -> int {
            if (CGLGetCurrentContext()!=context) return 0; // Never retire on a foreign context.
            if (drain && completed->load(std::memory_order_acquire)==0) wait(instance,future);
            const auto status=completed->load(std::memory_order_acquire);
            if (!status) return 0;
            if (producerStatus->load(std::memory_order_acquire)==dawn_canvas_images::submission_status::pending ||
                replacementStatus->load(std::memory_order_acquire)==dawn_canvas_images::submission_status::pending) return 0;
            // Service Graphite's completion queue on its owning thread so a
            // persistent context can release completed command buffers/resources.
            graphite->checkAsyncWorkCompletion();
            if (status==1 && graphite->hasUnfinishedGpuWork()) return 0;
            bool delivered=status==1 && !error->load() &&
                producerStatus->load()==dawn_canvas_images::submission_status::success &&
                replacementStatus->load()==dawn_canvas_images::submission_status::success;
            if (delivered) delivered=check_iosurface_gl(static_cast<IOSurfaceRef>(sharedSurface.get()),
                width,height,nullptr,rowBytes,target);
            consumer->complete(); consumer.reset();
            replacementConsumer->complete(); replacementConsumer.reset();
            return delivered ? 1 : -1;
        };
        auto owner=std::make_shared<decltype(deliver)>(std::move(deliver));
        pendingProducer=[owner](bool drain) { return (*owner)(drain); };
        return 0;
#else
        return finish("unavailable","Host delivery requires macOS",77);
#endif
    } else {
    auto mapped = std::make_shared<bool>(false);
    auto mapFuture = buffer.MapAsync(wgpu::MapMode::Read, 0, bufferSize,
        wgpu::CallbackMode::WaitAnyOnly,
        [mapped](wgpu::MapAsyncStatus status, wgpu::StringView) {
            *mapped = status == wgpu::MapAsyncStatus::Success;
        });
    if (!wait(instance, mapFuture) || !*mapped || error->load())
        return finish("failed", "GPU clear/copy/map failed or timed out", 1);
    pixels = static_cast<const uint8_t*>(buffer.GetConstMappedRange(0, bufferSize));
    if (!pixels) return finish("failed", "Mapped range is null", 1);
    constexpr std::array<int, 4> expected{51, 102, 153, 255};

    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
            for (uint32_t c = 0; c < 4; ++c) {
                constexpr std::array<int,4> blended{153,51,77,255};
                constexpr std::array<int,4> green{0,255,0,255};
                const auto& wanted=(x>=10 && x<15 && y>=1 && y<3) ? green
                    : (x>=2 && x<8 && y>=1 && y<3) ? blended : expected;
                valid &= std::abs(int(pixels[y * rowBytes + x * 4 + (sharedOutput && c<3 ? 2-c : c)]) - wanted[c]) <= 1;
            }
    }
#if defined(__APPLE__)
    if (sharedOutput) valid &= check_iosurface_gl(static_cast<IOSurfaceRef>(sharedSurface.get()),width,height,pixels,rowBytes,hostDestinationTexture);
#endif
    if (verifyPixels) buffer.Unmap();
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
              << ",\"verifiedPixels\":" << (verifyPixels ? width * height : 0)
              << ",\"backgroundRGBA\":[51,102,153,255],\"compositedRGBA\":[153,51,77,255],\"tolerance\":1,\"diagnosticReadback\":" << (verifyPixels ? "true" : "false") << "}\n";
    return 0;
}

#if defined(__APPLE__) && defined(WEBSCENE_GRAPHITE_HOST_PROBE)
extern "C" __attribute__((visibility("default"))) unsigned webscene_graphite_host_initializations() {
    return hostRuntime.initializations;
}
extern "C" __attribute__((visibility("default"))) unsigned webscene_graphite_host_context_initializations() {
    return hostRuntime.graphiteInitializations;
}
extern "C" __attribute__((visibility("default"))) unsigned webscene_graphite_host_output_allocations() {
    return hostRuntime.outputAllocations;
}
extern "C" __attribute__((visibility("default"))) unsigned webscene_graphite_host_canvas_allocations() {
    return hostRuntime.canvasPool ? static_cast<unsigned>(hostRuntime.canvasPool->created_images()) : 0;
}
extern "C" __attribute__((visibility("default"))) unsigned webscene_graphite_host_canvas_busy() {
    return hostRuntime.canvasPool ? static_cast<unsigned>(hostRuntime.canvasPool->busy_images()) : 0;
}
// Explicit diagnostic readback, never called by normal presentation.
extern "C" __attribute__((visibility("default"))) int webscene_graphite_host_verify_marker(unsigned texture,unsigned serial) {
    if (!CGLGetCurrentContext() || pendingProducer || host_blit.fence) return 1;
    GLint previous=0,packBuffer=0,pack[5]{};
    const GLenum names[]={GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_PIXELS,GL_PACK_SKIP_ROWS,GL_PACK_SWAP_BYTES};
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previous);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&packBuffer);
    for (int i=0;i<5;++i) glGetIntegerv(names[i],&pack[i]);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
    for (int i=0;i<5;++i) glPixelStorei(names[i],i==0 ? 1 : 0);
    GLuint framebuffer=0; glGenFramebuffers(1,&framebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,framebuffer);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    std::array<unsigned char,4> pixel{};
    bool valid=glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    if (valid) glReadPixels(16,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());
    valid=valid && glGetError()==GL_NO_ERROR && pixel[0]==(serial & 255) &&
        pixel[1]==((serial >> 8) & 255) && pixel[2]==0 && pixel[3]==255;
    glBindFramebuffer(GL_READ_FRAMEBUFFER,previous);
    glDeleteFramebuffers(1,&framebuffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,packBuffer);
    for (int i=0;i<5;++i) glPixelStorei(names[i],pack[i]);
    return valid ? 0 : 1;
}
// Diagnostic bridge only: caller supplies a current CGL context and a 17x4 2D
// texture. Defers producer delivery and GL retirement; not a production API.
extern "C" __attribute__((visibility("default"))) int webscene_graphite_host_poll(int drain) {
    if (pendingProducer) {
        const auto status=pendingProducer(drain!=0);
        if (!status) return 0;
        pendingProducer={};
        if (status<0) return -1;
    }
    return poll_host_blit(drain!=0);
}
extern "C" __attribute__((visibility("default"))) int webscene_graphite_host_probe(unsigned texture,unsigned serial) {
    if (!texture || hostDestinationTexture || pendingProducer || host_blit.fence || !CGLGetCurrentContext()) return 1;
    hostDestinationTexture=texture;
    hostFrameSerial=serial;
    char name[]="graphite-probe",backend[]="metal",mode[]="iosurface";
    char* args[]={name,backend,mode};
    int result=1;
    try { result=main(3,args); } catch (...) { result=1; }
    hostDestinationTexture=0;
    return result;
}
#endif
