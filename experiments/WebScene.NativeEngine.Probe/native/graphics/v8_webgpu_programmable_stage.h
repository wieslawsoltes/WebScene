#pragma once
#include "v8_webgpu_shaders.h"
#include <cmath>
#include <optional>
namespace webscene::graphics {
struct webgpu_programmable_stage {
    wgpu::ShaderModule module;
    std::optional<std::string> entry_point;
    std::vector<std::pair<std::string,double>> constants;
    // The returned entries borrow key storage. Keep this descriptor alive and
    // unchanged until Dawn has consumed the native pipeline descriptor.
    std::vector<wgpu::ConstantEntry> native_constants() const & {
        std::vector<wgpu::ConstantEntry> result;result.reserve(constants.size());
        for(const auto& [key,value]:constants) {
            wgpu::ConstantEntry entry{};entry.key=wgpu::StringView(key.data(),key.size());entry.value=value;
            result.push_back(entry);
        }
        return result;
    }
    std::vector<wgpu::ConstantEntry> native_constants() const &&=delete;
};
// Converts inherited GPUProgrammableStage members in WebIDL name order. Derived
// vertex buffers / fragment targets must be converted after this function.
inline bool read_webgpu_programmable_stage(v8::Isolate* isolate,v8::Local<v8::Context> context,
    v8::Local<v8::Value> input,webgpu_programmable_stage& output) {
    const auto key=[&](const char* text){return v8::String::NewFromUtf8(isolate,text).ToLocalChecked();};
    const auto fail=[&](const char* text){isolate->ThrowException(v8::Exception::TypeError(key(text)));return false;};
    if(!input->IsNullOrUndefined() && !input->IsObject())return fail("Programmable stage must be a dictionary");
    const auto get=[&](const char* name,v8::Local<v8::Value>& value) {
        if(input->IsNullOrUndefined()){value=v8::Undefined(isolate);return true;}
        return input.As<v8::Object>()->Get(context,key(name)).ToLocal(&value);
    };
    const auto string=[&](v8::Local<v8::Value> value,std::string& result) {
        v8::Local<v8::String> text;if(!value->ToString(context).ToLocal(&text))return false;
        v8::String::Utf8Value bytes(isolate,text);if(!*bytes)return false;
        result.assign(*bytes,bytes.length());return true;
    };
    webgpu_programmable_stage converted;
    v8::Local<v8::Value> value;
    if(!get("constants",value))return false;
    if(!value->IsUndefined()) {
        if(!value->IsObject())return fail("Pipeline constants must be a record object");
        auto record=value.As<v8::Object>();v8::Local<v8::Array> keys;
        if(!record->GetOwnPropertyNames(context,v8::ALL_PROPERTIES,v8::KeyConversionMode::kConvertToString).ToLocal(&keys))return false;
        for(uint32_t i=0;i<keys->Length();++i) {
            v8::Local<v8::Value> property,descriptor,enumerable,constant;
            if(!keys->Get(context,i).ToLocal(&property))return false;
            if(!record->GetOwnPropertyDescriptor(context,property.As<v8::Name>()).ToLocal(&descriptor))return false;
            if(descriptor->IsUndefined())continue;
            if(!descriptor.As<v8::Object>()->Get(context,key("enumerable")).ToLocal(&enumerable))return false;
            if(!enumerable->BooleanValue(isolate))continue;
            std::string name;if(!string(property,name))return false;
            if(!record->Get(context,property).ToLocal(&constant))return false;
            v8::Local<v8::Number> number;if(!constant->ToNumber(context).ToLocal(&number))return false;
            if(!std::isfinite(number->Value()))return fail("Pipeline constants must be finite doubles");
            // USVString conversion can collapse distinct UTF-16 property names.
            auto existing=std::find_if(converted.constants.begin(),converted.constants.end(),[&](const auto& item){return item.first==name;});
            if(existing==converted.constants.end())converted.constants.emplace_back(std::move(name),number->Value());
            else existing->second=number->Value();
        }
    }
    if(!get("entryPoint",value))return false;
    if(!value->IsUndefined()) {
        std::string name;if(!string(value,name))return false;converted.entry_point=std::move(name);
    }
    if(!get("module",value))return false;
    try {converted.module=v8_webgpu_shaders::native_reference(value);}
    catch(const std::exception&) {return fail("Programmable stage requires a live GPUShaderModule");}
    output=std::move(converted);return true;
}
} // namespace webscene::graphics
