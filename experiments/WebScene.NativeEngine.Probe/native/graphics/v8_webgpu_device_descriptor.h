#pragma once
#include "webgpu_device_descriptor.h"
#include <v8.h>
#include <cmath>
#include <string>

namespace webscene::graphics {
// WebIDL conversion only: capability/limit checks precede native RequestDevice.
inline bool read_webgpu_device_descriptor(v8::Isolate* isolate,v8::Local<v8::Context> context,
    v8::Local<v8::Value> input,webgpu_device_descriptor& output) {
    const auto key=[&](const char* name) { return v8::String::NewFromUtf8(isolate,name).ToLocalChecked(); };
    const auto fail=[&](const char* message) {
        isolate->ThrowException(v8::Exception::TypeError(key(message))); return false;
    };
    const auto get=[&](v8::Local<v8::Value> object,const char* name,v8::Local<v8::Value>& value) {
        if (object->IsNullOrUndefined()) { value=v8::Undefined(isolate); return true; }
        if (!object->IsObject()) return fail("GPU descriptor must be a dictionary");
        return object.As<v8::Object>()->Get(context,key(name)).ToLocal(&value);
    };
    const auto label=[&](v8::Local<v8::Value> dictionary,std::string& result) {
        v8::Local<v8::Value> value;
        if (!get(dictionary,"label",value)) return false;
        if (value->IsUndefined()) return true;
        v8::Local<v8::String> text;
        if (!value->ToString(context).ToLocal(&text)) return false;
        v8::String::Utf8Value bytes(isolate,text);
        if (!*bytes) return false;
        result.assign(*bytes,bytes.length()); return true;
    };
    webgpu_device_descriptor converted;
    // Inherited label first; then dictionary members in lexicographic order.
    if (!label(input,converted.label)) return false;
    v8::Local<v8::Value> value;
    if (!get(input,"defaultQueue",value) || !label(value,converted.queue_label)) return false;
    if (!get(input,"requiredFeatures",value)) return false;
    if (!value->IsUndefined()) {
        if (!value->IsObject()) return fail("requiredFeatures must be an iterable object");
        v8::Local<v8::Value> method,iterator_value,next;
        if (!value.As<v8::Object>()->Get(context,v8::Symbol::GetIterator(isolate)).ToLocal(&method)) return false;
        if (!method->IsFunction()) return fail("requiredFeatures is not iterable");
        if (!method.As<v8::Function>()->Call(context,value,0,nullptr).ToLocal(&iterator_value)) return false;
        if (!iterator_value->IsObject()) return fail("Feature iterator must return an object");
        auto iterator=iterator_value.As<v8::Object>();
        if (!iterator->Get(context,key("next")).ToLocal(&next)) return false;
        if (!next->IsFunction()) return fail("Feature iterator next is not callable");
        for (;;) {
            v8::Local<v8::Value> step,done,feature;
            if (!next.As<v8::Function>()->Call(context,iterator,0,nullptr).ToLocal(&step)) return false;
            if (!step->IsObject()) return fail("Feature iterator result must be an object");
            if (!step.As<v8::Object>()->Get(context,key("done")).ToLocal(&done)) return false;
            if (done->BooleanValue(isolate)) break;
            if (!step.As<v8::Object>()->Get(context,key("value")).ToLocal(&feature)) return false;
            v8::Local<v8::String> text;
            if (!feature->ToString(context).ToLocal(&text)) return false;
            v8::String::Utf8Value bytes(isolate,text);
            if (!*bytes) return false;
            auto native=webgpu_feature_from_name(std::string_view(*bytes,bytes.length()));
            if (!native) return fail("Unknown GPUFeatureName");
            converted.required_features.push_back(*native);
        }
    }
    if (!get(input,"requiredLimits",value)) return false;
    if (!value->IsUndefined()) {
        if (!value->IsObject()) return fail("requiredLimits must be a record object");
        auto record=value.As<v8::Object>();
        v8::Local<v8::Array> keys;
        // Snapshot all own keys first. Check each descriptor immediately before
        // conversion/get: an earlier getter may change a later property's flags.
        if (!record->GetOwnPropertyNames(context,v8::ALL_PROPERTIES,v8::KeyConversionMode::kConvertToString).ToLocal(&keys)) return false;
        for (uint32_t i=0;i<keys->Length();++i) {
            v8::Local<v8::Value> property,descriptor,enumerable,limit;
            if (!keys->Get(context,i).ToLocal(&property)) return false;
            if (!record->GetOwnPropertyDescriptor(context,property.As<v8::Name>()).ToLocal(&descriptor)) return false;
            if (descriptor->IsUndefined()) continue;
            if (!descriptor.As<v8::Object>()->Get(context,key("enumerable")).ToLocal(&enumerable)) return false;
            if (!enumerable->BooleanValue(isolate)) continue;
            v8::Local<v8::String> name;
            if (!property->ToString(context).ToLocal(&name)) return false;
            v8::String::Value units(isolate,name);
            if (!*units) return false;
            std::u16string limit_name(reinterpret_cast<const char16_t*>(*units),units.length());
            if (!record->Get(context,property).ToLocal(&limit)) return false;
            std::optional<uint64_t> number;
            if (!limit->IsUndefined()) {
                v8::Local<v8::Number> numeric;
                if (!limit->ToNumber(context).ToLocal(&numeric)) return false;
                const double truncated=std::trunc(numeric->Value());
                if (!std::isfinite(truncated) || truncated<0 || truncated>9007199254740991.0)
                    return fail("Required limit is outside GPUSize64 range");
                number=static_cast<uint64_t>(truncated);
            }
            converted.required_limits.emplace_back(std::move(limit_name),number);
        }
    }
    output=std::move(converted); return true;
}
} // namespace webscene::graphics
