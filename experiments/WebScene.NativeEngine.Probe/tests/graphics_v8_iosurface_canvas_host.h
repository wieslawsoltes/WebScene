#pragma once
#include "graphics/v8_webgpu_iosurface_canvas_host.h"
#include "graphics/v8_webgpu_realm.h"
#if defined(__APPLE__)
template<class Run>
void test_v8_iosurface_canvas_host(v8::Isolate* isolate,v8::Local<v8::Context> context,
    graphics_service& service,Run run) {
    auto exception=context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"DOMException")).ToLocalChecked().As<v8::Function>();
    auto realm=std::make_unique<v8_webgpu_realm>(isolate,context,service,exception,webgpu_canvas_interop::iosurface,wgpu::BackendType::Metal);
    require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"sharedCanvasGPU"),realm->object()).FromMaybe(false),"Shared discovery publication failed");
    require(run("globalThis.sharedCanvasAdapterPromise=sharedCanvasGPU.requestAdapter();"),"Shared canvas adapter request failed");
    auto adapter_promise=context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"sharedCanvasAdapterPromise")).ToLocalChecked().As<v8::Promise>();
    auto adapter_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(adapter_promise->State()==v8::Promise::kPending&&std::chrono::steady_clock::now()<adapter_deadline){
        service.pump([&](auto completion){require(realm->complete(completion),"Shared adapter completion not routed");});
        if(adapter_promise->State()==v8::Promise::kPending)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(adapter_promise->State()==v8::Promise::kFulfilled&&adapter_promise->Result()->IsObject(),"Shared canvas Metal adapter unavailable");
    require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"sharedCanvasAdapter"),adapter_promise->Result()).FromMaybe(false),"Shared adapter publication failed");
    require(run("globalThis.privateCanvasFeaturePromise=sharedCanvasAdapter.requestDevice({requiredFeatures:['shared-texture-memory-iosurface']});"),"Private feature rejection dispatch failed");
    auto rejected=context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"privateCanvasFeaturePromise")).ToLocalChecked().As<v8::Promise>();
    rejected->MarkAsHandled();
    require(rejected->State()==v8::Promise::kRejected&&rejected->Result().As<v8::Object>()->Get(context,v8::String::NewFromUtf8Literal(isolate,"name")).ToLocalChecked()->StrictEquals(v8::String::NewFromUtf8Literal(isolate,"TypeError")),"Host policy allowed JavaScript to request a native feature");
    require(run("globalThis.sharedCanvasDevicePromise=sharedCanvasAdapter.requestDevice();"),"Shared canvas requestDevice failed");
    auto promise=context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"sharedCanvasDevicePromise")).ToLocalChecked().As<v8::Promise>();
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(promise->State()==v8::Promise::kPending&&std::chrono::steady_clock::now()<deadline){
        service.pump([&](auto completion){require(realm->complete(completion),"Shared device completion not routed");});
        if(promise->State()==v8::Promise::kPending)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(promise->State()==v8::Promise::kFulfilled,"Shared canvas device promise failed");
    auto device=promise->Result().As<v8::Object>();
    auto native=v8_webgpu_devices::native_reference(device);
    for(auto feature:{wgpu::FeatureName::SharedTextureMemoryIOSurface,wgpu::FeatureName::SharedFenceMTLSharedEvent})require(native.HasFeature(feature),"Host sharing feature was not provisioned");
    require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"sharedCanvasDevice"),device).FromMaybe(false),"Shared device publication failed");
    auto provider=std::make_shared<dawn_iosurface_canvas_host>(1024*1024);
    uint64_t serial=0;
    auto host=make_iosurface_webgpu_canvas_host(provider,[&]{image_metadata metadata;metadata.canvas=123;metadata.allocation_generation=7;metadata.content_serial=++serial;metadata.producer_timeline=456;metadata.producer_value=serial;return metadata;});
    auto canvas=std::make_unique<v8_webgpu_canvas_context>(isolate,context,v8::Object::New(isolate),exception,4,2,std::move(host));
    require(context->Global()->Set(context,v8::String::NewFromUtf8Literal(isolate,"sharedCanvas"),canvas->object()).FromMaybe(false),"Shared canvas publication failed");
    require(run(R"JS(
        (()=>{
            if(sharedCanvasDevice.features.has('shared-texture-memory-iosurface')||sharedCanvasDevice.features.has('shared-fence-mtl-shared-event'))throw new Error('private feature exposed');
            sharedCanvas.configure({device:sharedCanvasDevice,format:sharedCanvasGPU.getPreferredCanvasFormat()});
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
    require(run("sharedCanvas.unconfigure();delete globalThis.sharedCanvas;delete globalThis.sharedCanvasDevice;delete globalThis.sharedCanvasAdapter;delete globalThis.sharedCanvasDevicePromise;delete globalThis.privateCanvasFeaturePromise;delete globalThis.sharedCanvasAdapterPromise;"),"Shared canvas cleanup failed");
    canvas.reset();require(provider->idle()&&provider->busy_images()==0,"Shared canvas provider retained completed frame");
    realm.reset();
    require(run("(()=>{let expired=false;try{sharedCanvasGPU.getPreferredCanvasFormat()}catch(e){expired=e instanceof TypeError}if(!expired)throw new Error('retired GPU realm callable');delete globalThis.sharedCanvasGPU;})();"),"GPU realm teardown left live discovery receiver");
}
#endif
