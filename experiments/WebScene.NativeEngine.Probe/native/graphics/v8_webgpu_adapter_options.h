#pragma once
#include <v8.h>
#include <webgpu/webgpu_cpp.h>
#include <optional>
#include <string>

namespace webscene::graphics {
// GPURequestAdapterOptions from @webref/idl 3.82.1. Browser dictionary fields
// remain separate from Dawn's backend/private adapter selection extensions.
struct webgpu_adapter_options {
    std::u16string feature_level=u"core";
    std::optional<wgpu::PowerPreference> power_preference;
    bool force_fallback_adapter=false;
    bool xr_compatible=false;
};
inline bool read_webgpu_adapter_options(v8::Isolate* isolate,v8::Local<v8::Context> context,
    v8::Local<v8::Value> input,webgpu_adapter_options& output) {
    const auto string=[&](const char* value) { return v8::String::NewFromUtf8(isolate,value).ToLocalChecked(); };
    const auto fail=[&](const char* message) {
        isolate->ThrowException(v8::Exception::TypeError(string(message)));
        return false;
    };
    webgpu_adapter_options converted;
    if (input->IsNullOrUndefined()) { output=std::move(converted); return true; }
    if (!input->IsObject()) return fail("GPURequestAdapterOptions must be a dictionary.");
    auto dictionary=input.As<v8::Object>();
    const auto get=[&](const char* name,v8::Local<v8::Value>& value) {
        return dictionary->Get(context,string(name)).ToLocal(&value);
    };
    const auto text=[&](v8::Local<v8::Value> value,std::string& result) {
        v8::Local<v8::String> converted_string;
        if (!value->ToString(context).ToLocal(&converted_string)) return false;
        v8::String::Utf8Value bytes(isolate,converted_string);
        if (!*bytes) return false;
        result.assign(*bytes,bytes.length()); return true;
    };
    // WebIDL dictionary members are accessed lexicographically, not in IDL
    // declaration order. Preserve getter/coercion exceptions and stop at once.
    v8::Local<v8::Value> value;
    if (!get("featureLevel",value)) return false;
    if (!value->IsUndefined()) {
        v8::Local<v8::String> level;
        if (!value->ToString(context).ToLocal(&level)) return false;
        v8::String::Value units(isolate,level);
        if (!*units && units.length()) return false;
        if (units.length()) converted.feature_level.assign(reinterpret_cast<const char16_t*>(*units),units.length());
        else converted.feature_level.clear();
    }
    if (!get("forceFallbackAdapter",value)) return false;
    if (!value->IsUndefined()) converted.force_fallback_adapter=value->BooleanValue(isolate);
    if (!get("powerPreference",value)) return false;
    if (!value->IsUndefined()) {
        std::string preference;
        if (!text(value,preference)) return false;
        if (preference=="low-power") converted.power_preference=wgpu::PowerPreference::LowPower;
        else if (preference=="high-performance") converted.power_preference=wgpu::PowerPreference::HighPerformance;
        else return fail("Invalid GPUPowerPreference.");
    }
    if (!get("xrCompatible",value)) return false;
    if (!value->IsUndefined()) converted.xr_compatible=value->BooleanValue(isolate);
    output=std::move(converted); // No partially converted native request on failure.
    return true;
}
} // namespace webscene::graphics
