#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <chrono>
#include <cstdio>
#include <thread>

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
        id<MTLCommandBuffer> read=[consumer commandBuffer];
        [read encodeWaitForEvent:ready value:7];
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
        if(completed_before_signal) return 3;
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
