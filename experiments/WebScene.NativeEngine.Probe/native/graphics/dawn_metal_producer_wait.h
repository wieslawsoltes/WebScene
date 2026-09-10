#pragma once
#if defined(__APPLE__) && defined(__OBJC__)
#include "metal_producer_wait.h"
#include <webgpu/webgpu_cpp.h>
#include <vector>

namespace webscene::graphics {
// Export every Dawn dependency before touching the consumer queue. Keep the
// originating handoff/snapshot alive through submission and image retirement.
inline id<MTLCommandBuffer> submit_dawn_metal_producer_waits(
    id<MTLCommandQueue> queue, std::span<const wgpu::SharedFence> fences,
    std::span<const uint64_t> values) {
    if(!queue || fences.empty() || fences.size()!=values.size()) return nil;
    std::vector<metal_producer_dependency> dependencies;
    dependencies.reserve(fences.size());
    for(size_t i=0;i<fences.size();++i) {
        if(!fences[i]) return nil;
        wgpu::SharedFenceExportInfo type;
        fences[i].ExportInfo(&type);
        if(type.type!=wgpu::SharedFenceType::MTLSharedEvent) return nil;
        wgpu::SharedFenceMTLSharedEventExportInfo metal;
        wgpu::SharedFenceExportInfo info; info.nextInChain=&metal;
        fences[i].ExportInfo(&info);
        if(!metal.sharedEvent) return nil;
        dependencies.push_back({(__bridge id<MTLSharedEvent>)metal.sharedEvent,values[i]});
    }
    return submit_metal_producer_waits(queue,dependencies);
}
}
#endif
