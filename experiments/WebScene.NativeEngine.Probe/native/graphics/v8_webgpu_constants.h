#pragma once
#include <v8.h>
#include <initializer_list>
#include <utility>
namespace webscene::graphics {
inline constexpr const char* webgpu_flag_namespaces[]{
    "GPUBufferUsage","GPUTextureUsage","GPUMapMode","GPUShaderStage","GPUColorWrite"
};
inline bool install_webgpu_flag_namespaces(v8::Isolate* isolate,v8::Local<v8::Context> context) {
    auto text=[&](const char* value){return v8::String::NewFromUtf8(isolate,value).ToLocalChecked();};
    auto define=[&](const char* name,std::initializer_list<std::pair<const char*,uint32_t>> constants) {
        auto object=v8::Object::New(isolate);
        for(auto [key,value]:constants)
            if(!object->DefineOwnProperty(context,text(key),v8::Integer::NewFromUnsigned(isolate,value),
                static_cast<v8::PropertyAttribute>(v8::ReadOnly|v8::DontDelete)).FromMaybe(false))return false;
        if(!object->DefineOwnProperty(context,v8::Symbol::GetToStringTag(isolate),text(name),
            static_cast<v8::PropertyAttribute>(v8::ReadOnly|v8::DontEnum)).FromMaybe(false))return false;
        return context->Global()->DefineOwnProperty(context,text(name),object,v8::DontEnum).FromMaybe(false);
    };
    return define("GPUBufferUsage",{{"MAP_READ",1},{"MAP_WRITE",2},{"COPY_SRC",4},{"COPY_DST",8},
            {"INDEX",16},{"VERTEX",32},{"UNIFORM",64},{"STORAGE",128},{"INDIRECT",256},{"QUERY_RESOLVE",512}})
        &&define("GPUTextureUsage",{{"COPY_SRC",1},{"COPY_DST",2},{"TEXTURE_BINDING",4},{"STORAGE_BINDING",8},{"RENDER_ATTACHMENT",16},{"TRANSIENT_ATTACHMENT",32}})
        &&define("GPUMapMode",{{"READ",1},{"WRITE",2}})
        &&define("GPUShaderStage",{{"VERTEX",1},{"FRAGMENT",2},{"COMPUTE",4}})
        &&define("GPUColorWrite",{{"RED",1},{"GREEN",2},{"BLUE",4},{"ALPHA",8},{"ALL",15}});
}
}
