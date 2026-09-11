#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <chrono>
#include <cstdio>
#include <thread>
#include "../native/graphics/dawn_metal_producer_wait.h"

// Hardware dependency test, not a presentation/FPS or Dawn/Skia interop test.
int main() {
    @autoreleasepool {
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();
        if(!device) { std::fprintf(stderr,"Metal hardware unavailable\n"); return 1; }
        id<MTLCommandQueue> producer=[device newCommandQueue];
        id<MTLCommandQueue> consumer=[device newCommandQueue];
        id<MTLSharedEvent> gate=[device newSharedEvent];
        id<MTLSharedEvent> ready=[device newSharedEvent];
        id<MTLBuffer> buffer=[device newBufferWithLength:4096 options:MTLResourceStorageModeShared];
        if(!producer || !consumer || !gate || !ready || !buffer) return 2;
        memset(buffer.contents,0,buffer.length);
        id<MTLCommandBuffer> write=[producer commandBuffer];
        [write encodeWaitForEvent:gate value:1];
        id<MTLBlitCommandEncoder> fill=[write blitCommandEncoder];
        [fill fillBuffer:buffer range:NSMakeRange(0,buffer.length) value:0x37];
        [fill endEncoding];
        [write encodeSignalEvent:ready value:7];
        [write commit];
        id<MTLSharedEvent> second=[device newSharedEvent];
        const webscene::graphics::metal_producer_dependency invalid[]={{ready,7},{nil,1}};
        if(webscene::graphics::submit_metal_producer_waits(consumer,invalid)) {
            gate.signaledValue=1; return 6;
        }
        const webscene::graphics::metal_producer_dependency dependencies[]={{ready,7},{second,11}};
        const wgpu::InstanceFeatureName timed=wgpu::InstanceFeatureName::TimedWaitAny;
        wgpu::InstanceDescriptor instance_desc{}; instance_desc.requiredFeatureCount=1; instance_desc.requiredFeatures=&timed;
        auto instance=wgpu::CreateInstance(&instance_desc);
        wgpu::Adapter adapter;
        wgpu::RequestAdapterOptions options{}; options.backendType=wgpu::BackendType::Metal;
        auto request=instance.RequestAdapter(&options,wgpu::CallbackMode::WaitAnyOnly,
            [&adapter](wgpu::RequestAdapterStatus status,wgpu::Adapter result,wgpu::StringView) {
                if(status==wgpu::RequestAdapterStatus::Success) adapter=std::move(result);
            });
        if(instance.WaitAny(request,5'000'000'000ULL)!=wgpu::WaitStatus::Success || !adapter) {
            gate.signaledValue=1; return 9;
        }
        const wgpu::FeatureName feature=wgpu::FeatureName::SharedFenceMTLSharedEvent;
        wgpu::DeviceDescriptor device_desc{}; device_desc.requiredFeatureCount=1; device_desc.requiredFeatures=&feature;
        wgpu::Device dawn;
        request=adapter.RequestDevice(&device_desc,wgpu::CallbackMode::WaitAnyOnly,
            [&dawn](wgpu::RequestDeviceStatus status,wgpu::Device result,wgpu::StringView) {
                if(status==wgpu::RequestDeviceStatus::Success) dawn=std::move(result);
            });
        if(instance.WaitAny(request,5'000'000'000ULL)!=wgpu::WaitStatus::Success || !dawn) {
            gate.signaledValue=1; return 10;
        }
        wgpu::SharedFence fences[2];
        uint64_t values[2]={7,11};
        for(size_t i=0;i<2;++i) {
            wgpu::SharedFenceMTLSharedEventDescriptor metal;
            metal.sharedEvent=(__bridge void*)dependencies[i].event;
            wgpu::SharedFenceDescriptor desc{};desc.nextInChain=&metal;
            fences[i]=dawn.ImportSharedFence(&desc);
        }
        if(webscene::graphics::submit_dawn_metal_producer_waits(consumer,fences,std::span(values,1))) {
            gate.signaledValue=1;second.signaledValue=11;return 11;
        }
        auto barrier=webscene::graphics::submit_dawn_metal_producer_waits(consumer,fences,values);
        if(!barrier) { gate.signaledValue=1; return 7; }
        id<MTLCommandBuffer> read=[consumer commandBuffer];
        id<MTLBuffer> result=[device newBufferWithLength:4096 options:MTLResourceStorageModeShared];
        id<MTLBlitCommandEncoder> copy=[read blitCommandEncoder];
        [copy copyFromBuffer:buffer sourceOffset:0 toBuffer:result destinationOffset:0 size:4096];
        [copy endEncoding];
        [read commit];
        // CPU submission must return while the producer is deliberately blocked.
        // The copy above is diagnostic verification only, never pixel transport.
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        const bool completed_before_signal=read.status==MTLCommandBufferStatusCompleted || ready.signaledValue>=7;
        gate.signaledValue=1;
        if(completed_before_signal) { second.signaledValue=11; return 3; }
        const auto producer_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(ready.signaledValue<7 && std::chrono::steady_clock::now()<producer_deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        // The first producer has finished, but the second dependency still holds
        // all subsequent work on the consumer queue.
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        const bool missed_second=read.status==MTLCommandBufferStatusCompleted;
        second.signaledValue=11;
        if(missed_second || ready.signaledValue<7) return 8;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(read.status!=MTLCommandBufferStatusCompleted && read.status!=MTLCommandBufferStatusError
            && std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if(read.status!=MTLCommandBufferStatusCompleted || write.status==MTLCommandBufferStatusError) return 4;
        const auto* bytes=static_cast<const unsigned char*>(result.contents);
        for(unsigned i=0;i<4096;++i) if(bytes[i]!=0x37) return 5;
        std::puts("Metal delayed producer dependency passed; CPU submission nonblocking; physical presentation unverified");
        return 0;
    }
}
