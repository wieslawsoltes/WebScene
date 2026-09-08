#pragma once
#if defined(__APPLE__) && defined(__OBJC__)
#import <Metal/Metal.h>
#include <span>
#include <cstdint>

namespace webscene::graphics {
struct metal_producer_dependency {
    id<MTLSharedEvent> event;
    uint64_t value;
};
// Caller holds the host queue lease. All later Skia submissions must use this
// same queue; dependencies and image ownership remain retained by the scene.
// Returning a command buffer certifies submission, never GPU completion.
inline id<MTLCommandBuffer> submit_metal_producer_waits(
    id<MTLCommandQueue> queue, std::span<const metal_producer_dependency> dependencies) {
    if(!queue || dependencies.empty()) return nil;
    // Validate the entire list before encoding anything into the host queue.
    // Shared events can synchronize across devices; do not require device identity.
    for(const auto& dependency:dependencies)
        if(!dependency.event) return nil;
    id<MTLCommandBuffer> barrier=[queue commandBuffer];
    if(!barrier) return nil;
    barrier.label=@"WebScene producer dependencies";
    for(const auto& dependency:dependencies)
        [barrier encodeWaitForEvent:dependency.event value:dependency.value];
    [barrier commit];
    return barrier;
}
}
#endif
