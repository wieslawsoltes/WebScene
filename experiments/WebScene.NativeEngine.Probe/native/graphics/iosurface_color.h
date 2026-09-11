#pragma once
#if defined(__APPLE__)
#include <IOSurface/IOSurface.h>
#include <CoreVideo/CoreVideo.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <unistd.h>

namespace webscene::graphics {
// Native allocation ownership only. Callers retain this owner through producer
// and consumer GPU completion; neither retain nor destruction synchronizes GPU work.
class iosurface_color final {
    IOSurfaceRef surface_=nullptr;
    std::shared_ptr<void> external_owner_;
    explicit iosurface_color(IOSurfaceRef surface):surface_(surface) {}
public:
    ~iosurface_color() {
        if (surface_) {
            if (std::getenv("WEBSCENE_GRAPHICS_MEMORY_TRACE"))
                std::fprintf(stderr, "iosurface-release id=%u bytes=%zu\n", IOSurfaceGetID(surface_), allocation_bytes());
            CFRelease(surface_);
        }
    }
    iosurface_color(const iosurface_color&)=delete;
    iosurface_color& operator=(const iosurface_color&)=delete;
    IOSurfaceRef borrowed_handle() const noexcept { return surface_; }
    size_t allocation_bytes() const noexcept { return IOSurfaceGetAllocSize(surface_); }

    // Keep the decoder's CVPixelBuffer lease, not merely its IOSurface. The
    // decoder pool may recycle pixels as soon as the pixel buffer is released.
    static std::shared_ptr<iosurface_color> adopt_bgra8(CVPixelBufferRef pixel, std::shared_ptr<void> owner) {
        if (!pixel || !owner || CVPixelBufferGetPixelFormatType(pixel)!=kCVPixelFormatType_32BGRA) return {};
        auto surface=CVPixelBufferGetIOSurface(pixel);
        if (!surface) return {};
        auto result=std::shared_ptr<iosurface_color>(new iosurface_color(nullptr));
        CFRetain(surface); result->surface_=surface; result->external_owner_=std::move(owner);
        return result;
    }

    // BGRA8 is the negotiated Dawn/Metal-to-CGL diagnostic format. Other formats
    // require explicit negotiation, not reinterpretation of these storage bytes.
    static std::shared_ptr<iosurface_color> create_bgra8(uint32_t width,uint32_t height,uint64_t available_bytes) {
        if (!width || !height || width>uint32_t(std::numeric_limits<int32_t>::max()/4) ||
            height>uint32_t(std::numeric_limits<int32_t>::max())) return {};
        const uint64_t row=IOSurfaceAlignProperty(kIOSurfaceBytesPerRow,uint64_t(width)*4);
        const auto page=static_cast<uint64_t>(getpagesize());
        if (!row || !page || row>uint64_t(INT64_MAX)/height) return {};
        const uint64_t raw=row*height;
        if (raw>uint64_t(INT64_MAX)-page+1) return {};
        const uint64_t allocation=((raw+page-1)/page)*page;
        if (allocation>available_bytes) return {};
        auto dictionary=CFDictionaryCreateMutable(nullptr,0,&kCFTypeDictionaryKeyCallBacks,
                                                  &kCFTypeDictionaryValueCallBacks);
        if (!dictionary) return {};
        bool valid=true;
        auto add=[&](CFStringRef key,int64_t value) {
            auto number=CFNumberCreate(nullptr,kCFNumberSInt64Type,&value);
            if (!number) { valid=false; return; }
            CFDictionarySetValue(dictionary,key,number);
            CFRelease(number);
        };
        add(kIOSurfaceWidth,static_cast<int32_t>(width));
        add(kIOSurfaceHeight,static_cast<int32_t>(height));
        add(kIOSurfaceBytesPerElement,4);
        add(kIOSurfaceBytesPerRow,static_cast<int64_t>(row));
        add(kIOSurfaceAllocSize,static_cast<int64_t>(allocation));
        add(kIOSurfacePixelFormat,kCVPixelFormatType_32BGRA);
        auto surface=valid ? IOSurfaceCreate(dictionary) : nullptr;
        CFRelease(dictionary);
        if (!surface) return {};
        if (IOSurfaceGetAllocSize(surface)>available_bytes) {
            CFRelease(surface);
            return {};
        }
        if (std::getenv("WEBSCENE_GRAPHICS_MEMORY_TRACE"))
            std::fprintf(stderr, "iosurface-create id=%u width=%u height=%u bytes=%zu\n",
                IOSurfaceGetID(surface), width, height, IOSurfaceGetAllocSize(surface));
        // Take ownership before allocating the shared control block.
        std::unique_ptr<iosurface_color> owner;
        try { owner.reset(new iosurface_color(surface)); }
        catch (...) { CFRelease(surface); throw; }
        return std::shared_ptr<iosurface_color>(std::move(owner));
    }
};
} // namespace webscene::graphics
#endif
