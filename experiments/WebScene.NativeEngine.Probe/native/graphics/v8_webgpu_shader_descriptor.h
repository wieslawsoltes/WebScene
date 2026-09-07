#pragma once
#include <webgpu/webgpu_cpp.h>
#include <v8.h>
#include <optional>
#include <string>
#include <vector>
namespace webscene::graphics {
struct webgpu_shader_hint {
    std::string entry_point;
    enum class layout_kind { omitted,automatic,explicit_layout } kind=layout_kind::omitted;
    wgpu::PipelineLayout layout;
};
struct webgpu_shader_descriptor {
    std::string label,code;
    std::vector<webgpu_shader_hint> hints;
};
// ResolveLayout recognizes genuine GPUPipelineLayout wrappers, returning an
// empty optional for other values. Unrecognized objects then undergo the string
// branch of the WebIDL union conversion. No user code should run in recognition.
template<class ResolveLayout>
inline bool read_webgpu_shader_descriptor(v8::Isolate* isolate,v8::Local<v8::Context> context,
    v8::Local<v8::Value> input,webgpu_shader_descriptor& output,ResolveLayout resolve_layout) {
    const auto key=[&](const char* text){return v8::String::NewFromUtf8(isolate,text).ToLocalChecked();};
    const auto fail=[&](const char* text){isolate->ThrowException(v8::Exception::TypeError(key(text)));return false;};
    const auto get=[&](v8::Local<v8::Value> dictionary,const char* name,v8::Local<v8::Value>& value) {
        if(dictionary->IsNullOrUndefined()){value=v8::Undefined(isolate);return true;}
        if(!dictionary->IsObject())return fail("Shader descriptor must be a dictionary");
        return dictionary.As<v8::Object>()->Get(context,key(name)).ToLocal(&value);
    };
    const auto string=[&](v8::Local<v8::Value> value,std::string& result) {
        v8::Local<v8::String> text;if(!value->ToString(context).ToLocal(&text))return false;
        v8::String::Utf8Value bytes(isolate,text);if(!*bytes)return false;
        result.assign(*bytes,bytes.length());return true;
    };
    webgpu_shader_descriptor converted;
    v8::Local<v8::Value> value;
    if(!get(input,"label",value))return false;
    if(!value->IsUndefined() && !string(value,converted.label))return false;
    if(!get(input,"code",value))return false;
    if(value->IsUndefined())return fail("Shader code is required");
    if(!string(value,converted.code))return false;
    if(!get(input,"compilationHints",value))return false;
    if(!value->IsUndefined()) {
        if(!value->IsObject())return fail("Compilation hints must be an iterable object");
        v8::Local<v8::Value> method,iterator,next;
        if(!value.As<v8::Object>()->Get(context,v8::Symbol::GetIterator(isolate)).ToLocal(&method))return false;
        if(!method->IsFunction())return fail("Compilation hints are not iterable");
        if(!method.As<v8::Function>()->Call(context,value,0,nullptr).ToLocal(&iterator))return false;
        if(!iterator->IsObject())return fail("Hint iterator must return an object");
        if(!iterator.As<v8::Object>()->Get(context,key("next")).ToLocal(&next))return false;
        if(!next->IsFunction())return fail("Hint iterator next must be callable");
        for(;;) {
            v8::Local<v8::Value> step,done,hint_value,member;
            if(!next.As<v8::Function>()->Call(context,iterator,0,nullptr).ToLocal(&step))return false;
            if(!step->IsObject())return fail("Hint iterator result must be an object");
            if(!step.As<v8::Object>()->Get(context,key("done")).ToLocal(&done))return false;
            if(done->BooleanValue(isolate))break;
            if(!step.As<v8::Object>()->Get(context,key("value")).ToLocal(&hint_value))return false;
            webgpu_shader_hint hint;
            if(!get(hint_value,"entryPoint",member))return false;
            if(member->IsUndefined())return fail("Hint entryPoint is required");
            if(!string(member,hint.entry_point))return false;
            if(!get(hint_value,"layout",member))return false;
            if(!member->IsUndefined()) {
                auto layout=member->IsObject()?resolve_layout(member):std::optional<wgpu::PipelineLayout>{};
                if(layout) {
                    if(!*layout)return fail("Pipeline layout native ownership is unavailable");
                    hint.kind=webgpu_shader_hint::layout_kind::explicit_layout;hint.layout=std::move(*layout);
                } else {
                    std::string automatic;if(!string(member,automatic))return false;
                    if(automatic!="auto")return fail("Invalid GPUAutoLayoutMode");
                    hint.kind=webgpu_shader_hint::layout_kind::automatic;
                }
            }
            converted.hints.push_back(std::move(hint));
        }
    }
    output=std::move(converted);return true;
}
} // namespace webscene::graphics
