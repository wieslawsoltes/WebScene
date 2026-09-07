#pragma once
#if defined(__APPLE__)
#include <IOSurface/IOSurface.h>
#include <CoreVideo/CoreVideo.h>
#include <cstdint>
#include <limits>
#include <memory>

namespace webscene::graphics {
// Native allocation ownership only. Callers retain this owner through producer
// and consumer GPU completion; neither retain nor destruction synchronizes GPU work.
class iosurface_color final {
    IOSurfaceRef surface_=nullptr;
    explicit iosurface_color(IOSurfaceRef surface):surface_(surface) {}
public:
    ~iosurface_color() { if (surface_) CFRelease(surface_); }
    iosurface_color(const iosurface_color&)=delete;
    iosurface_color& operator=(const iosurface_color&)=delete;
    IOSurfaceRef borrowed_handle() const noexcept { return surface_; }
    size_t allocation_bytes() const noexcept { return IOSurfaceGetAllocSize(surface_); }

    // BGRA8 is the negotiated Dawn/Metal-to-CGL diagnostic format. Other formats
    // require explicit negotiation, not reinterpretation of these storage bytes.
    static std::shared_ptr<iosurface_color> create_bgra8(uint32_t width,uint32_t height) {
        if (!width || !height || width>uint32_t(std::numeric_limits<int32_t>::max()/4) ||
            height>uint32_t(std::numeric_limits<int32_t>::max())) return {};
        auto dictionary=CFDictionaryCreateMutable(nullptr,0,&kCFTypeDictionaryKeyCallBacks,
                                                  &kCFTypeDictionaryValueCallBacks);
        if (!dictionary) return {};
        bool valid=true;
        auto add=[&](CFStringRef key,int32_t value) {
            auto number=CFNumberCreate(nullptr,kCFNumberSInt32Type,&value);
            if (!number) { valid=false; return; }
            CFDictionarySetValue(dictionary,key,number);
            CFRelease(number);
        };
        add(kIOSurfaceWidth,static_cast<int32_t>(width));
        add(kIOSurfaceHeight,static_cast<int32_t>(height));
        add(kIOSurfaceBytesPerElement,4);
        add(kIOSurfacePixelFormat,kCVPixelFormatType_32BGRA);
        auto surface=valid ? IOSurfaceCreate(dictionary) : nullptr;
        CFRelease(dictionary);
        if (!surface) return {};
        // Take ownership before allocating the shared control block.
        std::unique_ptr<iosurface_color> owner;
        try { owner.reset(new iosurface_color(surface)); }
        catch (...) { CFRelease(surface); throw; }
        return std::shared_ptr<iosurface_color>(std::move(owner));
    }
};
} // namespace webscene::graphics
#endif
