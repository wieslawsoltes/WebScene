#pragma once
#include "v8_webgpu_render_state.h"
#include <string>
namespace webscene::graphics {
inline bool read_webgpu_object_label(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Value> input,std::string& output) {
    webgpu_state_reader reader(isolate,context,input);v8::Local<v8::Value> value;
    if(!reader.get("label",value))return false;std::string converted;
    if(!value->IsUndefined()) {
        v8::Local<v8::String> text;if(!value->ToString(context).ToLocal(&text))return false;
        v8::String::Utf8Value bytes(isolate,text);if(!*bytes)return false;converted.assign(*bytes,bytes.length());
    }
    output=std::move(converted);return true;
}
} // namespace webscene::graphics
