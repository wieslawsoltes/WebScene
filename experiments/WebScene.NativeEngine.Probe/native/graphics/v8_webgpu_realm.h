#pragma once
#include "v8_webgpu_discovery.h"
namespace webscene::graphics {
// One ownership/dispatch unit per document realm. The caller decides secure
// context exposure and negotiated interop before installing object() on Navigator.
// Canvas controllers must be destroyed first, and the service must outlive this.
class v8_webgpu_realm final {
    v8_webgpu_devices devices_;
    v8_webgpu_adapters adapters_;
    v8_webgpu_discovery discovery_;
public:
    v8_webgpu_realm(v8::Isolate* isolate,v8::Local<v8::Context> context,
        graphics_service& service,v8::Local<v8::Function> dom_exception,
        webgpu_canvas_interop interop=webgpu_canvas_interop::none,
        wgpu::BackendType backend=wgpu::BackendType::Undefined,
        wgpu::TextureFormat preferred_format=wgpu::TextureFormat::BGRA8Unorm)
        :devices_(isolate,context,dom_exception),
         adapters_(isolate,context,devices_,dom_exception,64,interop),
         discovery_(isolate,context,service,adapters_,backend,preferred_format) {}
    v8_webgpu_realm(const v8_webgpu_realm&)=delete;
    v8_webgpu_realm& operator=(const v8_webgpu_realm&)=delete;
    v8::Local<v8::Object> object()const {return discovery_.object();}
    bool complete(completion_record record) {
        return discovery_.complete(record)||adapters_.complete(record)||devices_.complete(record);
    }
};
} // namespace webscene::graphics
