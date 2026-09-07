#pragma once
#include "graphics/v8_webgpu_iosurface_canvas_host.h"
#if defined(__APPLE__)
template<class Run>
void test_v8_iosurface_canvas_host(v8::Isolate* isolate,v8::Local<v8::Context> context,
    graphics_service& service,Run run) {
    struct adapter_state {wgpu::Adapter adapter;std::atomic<bool> ready=false;};
    auto discovered=std::make_shared<adapter_state>();
    wgpu::RequestAdapterOptions options{};options.backendType=wgpu::BackendType::Metal;
    service.dawn().instance().RequestAdapter(&options,wgpu::CallbackMode::AllowSpontaneous,
        [discovered](wgpu::RequestAdapterStatus status,wgpu::Adapter adapter,wgpu::StringView){
            if(status==wgpu::RequestAdapterStatus::Success)discovered->adapter=std::move(adapter);
            discovered->ready.store(true);
        });
    auto adapter_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!discovered->ready.load()&&std::chrono::steady_clock::now()<adapter_deadline){service.dawn().instance().ProcessEvents();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    require(discovered->ready.load()&&discovered->adapter,"Shared canvas Metal adapter unavailable");
    auto adapter=discovered->adapter;
    const wgpu::FeatureName private_features[]={wgpu::FeatureName::SharedTextureMemoryIOSurface,wgpu::FeatureName::SharedFenceMTLSharedEvent};
    for(auto feature:private_features)require(adapter.HasFeature(feature),"Metal shared canvas capability unavailable");
    struct request_state {wgpu::Device device;std::atomic<bool> ready=false;};
    auto requested=std::make_shared<request_state>();
    wgpu::DeviceDescriptor descriptor{};descriptor.requiredFeatureCount=2;descriptor.requiredFeatures=private_features;
    adapter.RequestDevice(&descriptor,wgpu::CallbackMode::AllowSpontaneous,
        [requested](wgpu::RequestDeviceStatus status,wgpu::Device device,wgpu::StringView){
            if(status==wgpu::RequestDeviceStatus::Success)requested->device=std::move(device);
            requested->ready.store(true);
        });
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!requested->ready.load()&&std::chrono::steady_clock::now()<deadline){service.dawn().instance().ProcessEvents();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    require(requested->ready.load()&&requested->device,"Shared canvas device request failed");
    auto native=requested->device;
    auto handle=service.adopt_device(adapter,native);
    auto exception=context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"DOMException")).ToLocalChecked().As<v8::Function>();
    v8_webgpu_devices devices(isolate,context,exception,1,2);
    auto device=devices.wrap(context,service,handle).ToLocalChecked();
    require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"sharedCanvasDevice"),device).FromMaybe(false),"Shared device publication failed");
    auto provider=std::make_shared<dawn_iosurface_canvas_host>(1024*1024);
    uint64_t serial=0;
    auto host=make_iosurface_webgpu_canvas_host(provider,[&]{image_metadata metadata;metadata.canvas=123;metadata.allocation_generation=7;metadata.content_serial=++serial;metadata.producer_timeline=456;metadata.producer_value=serial;return metadata;},requested);
    auto canvas=std::make_unique<v8_webgpu_canvas_context>(isolate,context,v8::Object::New(isolate),exception,4,2,std::move(host));
    require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"sharedCanvas"),canvas->object()).FromMaybe(false),"Shared canvas publication failed");
    require(run(R"JS(
        (()=>{
            if(sharedCanvasDevice.features.has('shared-texture-memory-iosurface')||sharedCanvasDevice.features.has('shared-fence-mtl-shared-event'))throw new Error('private feature exposed');
            sharedCanvas.configure({device:sharedCanvasDevice,format:'bgra8unorm'});
            const texture=sharedCanvas.getCurrentTexture();
            if(texture!==sharedCanvas.getCurrentTexture())throw new Error('shared texture identity');
            const encoder=sharedCanvasDevice.createCommandEncoder();
            const pass=encoder.beginRenderPass({colorAttachments:[{view:texture.createView(),loadOp:'clear',storeOp:'store',clearValue:[1,0,0,1]}]});
            pass.end();sharedCanvasDevice.queue.submit([encoder.finish()]);
        })();
    )JS"),"JavaScript shared canvas clear failed");
    canvas->end_frame(true);
    std::optional<owned_image_pool::retained> image;
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!image&&std::chrono::steady_clock::now()<deadline){
        auto ready=provider->take_ready();if(ready)image.emplace(std::move(*ready));
        else{service.dawn().instance().ProcessEvents();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    }
    require(image.has_value(),"JavaScript shared canvas image unavailable");
    auto metadata=image->describe();require(metadata.canvas==123&&metadata.allocation_generation==7&&metadata.content_serial==1&&metadata.width==4&&metadata.height==2,"Shared canvas frame identity changed");
    auto consumer=image->begin_consumer();require(consumer.has_value(),"Shared canvas consumer unavailable");
    auto surface=iosurface_canvas_images::resolve(*consumer).borrowed_handle();
    // Explicit diagnostic CPU inspection only, after the producer's GPU work.
    require(IOSurfaceLock(surface,kIOSurfaceLockReadOnly,nullptr)==kIOReturnSuccess,"Shared canvas diagnostic lock failed");
    auto pixels=static_cast<const uint8_t*>(IOSurfaceGetBaseAddress(surface));auto stride=IOSurfaceGetBytesPerRow(surface);
    bool correct=pixels!=nullptr;
    if(pixels)for(size_t y=0;y<2;++y)for(size_t x=0;x<4;++x){auto pixel=pixels+y*stride+x*4;correct&=pixel[0]==0&&pixel[1]==0&&pixel[2]==255&&pixel[3]==255;}
    auto unlocked=IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,nullptr);consumer->complete();image.reset();
    require(correct&&unlocked==kIOReturnSuccess,"JavaScript IOSurface canvas pixel mismatch");
    require(run("sharedCanvas.unconfigure();delete globalThis.sharedCanvas;delete globalThis.sharedCanvasDevice;"),"Shared canvas cleanup failed");
    canvas.reset();require(provider->idle()&&provider->busy_images()==0,"Shared canvas provider retained completed frame");
}
#endif
