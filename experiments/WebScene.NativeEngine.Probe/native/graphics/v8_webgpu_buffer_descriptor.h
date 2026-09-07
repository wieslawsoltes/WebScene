#pragma once
#include <v8.h>
#include <cmath>
#include <cstdint>
#include <string>

namespace webscene::graphics {
struct webgpu_buffer_descriptor {
    std::string label;
    uint64_t size{};
    uint32_t usage{};
    bool mapped_at_creation{};
};
// Dictionary conversion only. Usage combinations, alignment, device limits and
// allocation errors remain WebGPU/Dawn validation, not WebIDL TypeErrors.
inline bool read_webgpu_buffer_descriptor(v8::Isolate* isolate,v8::Local<v8::Context> context,
    v8::Local<v8::Value> input,webgpu_buffer_descriptor& output) {
    const auto string=[&](const char* value) { return v8::String::NewFromUtf8(isolate,value).ToLocalChecked(); };
    const auto fail=[&](const char* message) {
        isolate->ThrowException(v8::Exception::TypeError(string(message)));
        return false;
    };
    if (!input->IsNullOrUndefined() && !input->IsObject()) return fail("GPUBufferDescriptor must be a dictionary.");
    webgpu_buffer_descriptor converted;
    const auto get=[&](const char* name,v8::Local<v8::Value>& value) {
        if (input->IsNullOrUndefined()) { value=v8::Undefined(isolate); return true; }
        return input.As<v8::Object>()->Get(context,string(name)).ToLocal(&value);
    };
    const auto integer=[&](v8::Local<v8::Value> value,double maximum,uint64_t& result) {
        v8::Local<v8::Number> number;
        if (!value->ToNumber(context).ToLocal(&number)) return false;
        const double truncated=std::trunc(number->Value());
        if (!std::isfinite(truncated) || truncated<0 || truncated>maximum)
            return fail("GPU buffer integer is outside its WebIDL range.");
        result=static_cast<uint64_t>(truncated);
        return true;
    };
    v8::Local<v8::Value> value;
    // Inherited dictionary members first, then lexicographic own members.
    if (!get("label",value)) return false;
    if (!value->IsUndefined()) {
        v8::Local<v8::String> label;
        if (!value->ToString(context).ToLocal(&label)) return false;
        // V8 UTF-8 conversion replaces lone UTF-16 surrogates with U+FFFD,
        // implementing USVString while preserving embedded NUL via byte length.
        v8::String::Utf8Value bytes(isolate,label);
        if (!*bytes) return false;
        converted.label.assign(*bytes,bytes.length());
    }
    if (!get("mappedAtCreation",value)) return false;
    if (!value->IsUndefined()) converted.mapped_at_creation=value->BooleanValue(isolate);
    if (!get("size",value)) return false;
    if (value->IsUndefined()) return fail("GPUBufferDescriptor.size is required.");
    if (!integer(value,9007199254740991.0,converted.size)) return false;
    if (!get("usage",value)) return false;
    if (value->IsUndefined()) return fail("GPUBufferDescriptor.usage is required.");
    uint64_t usage{};
    if (!integer(value,4294967295.0,usage)) return false;
    converted.usage=static_cast<uint32_t>(usage);
    output=std::move(converted);
    return true;
}
} // namespace webscene::graphics
