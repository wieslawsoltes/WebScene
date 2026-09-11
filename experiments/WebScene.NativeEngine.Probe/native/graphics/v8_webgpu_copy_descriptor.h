#pragma once
#include "v8_webgpu_textures.h"
#include "v8_webgpu_buffers.h"
namespace webscene::graphics {
inline bool read_copy_coordinates(v8::Isolate* isolate,v8::Local<v8::Context> context,
    v8::Local<v8::Value> value,uint32_t (&coordinates)[3],bool extent){
    webgpu_state_reader reader(isolate,context,value);
    v8::Local<v8::Value> iterator;
    if(value->IsObject()&&!value.As<v8::Object>()->Get(context,v8::Symbol::GetIterator(isolate)).ToLocal(&iterator))return false;
    if(!iterator.IsEmpty()&&!iterator->IsNullOrUndefined()){
        size_t count=0;
        if(!read_webgpu_sequence(isolate,context,value,[&](auto item){
            if(count>=3)return reader.fail("Too many copy coordinates");
            v8::Local<v8::Number> number;if(!item->ToNumber(context).ToLocal(&number))return false;
            auto n=std::trunc(number->Value());if(!std::isfinite(n)||n<0||n>UINT32_MAX)return reader.fail("Invalid copy coordinate");
            coordinates[count++]=static_cast<uint32_t>(n);return true;
        },iterator))return false;
        return !extent||count>0||reader.fail("Copy extent requires width");
    }
    return reader.uint32(extent?"width":"x",coordinates[0],extent)
        &&reader.uint32(extent?"height":"y",coordinates[1])
        &&reader.uint32(extent?"depthOrArrayLayers":"z",coordinates[2]);
}
inline bool read_copy_extent(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Value> value,wgpu::Extent3D& extent){
    uint32_t coords[]={0,1,1};if(!read_copy_coordinates(isolate,context,value,coords,true))return false;
    extent={coords[0],coords[1],coords[2]};return true;
}
inline bool read_copy_texture(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Value> value,wgpu::TexelCopyTextureInfo& copy){
    webgpu_state_reader reader(isolate,context,value);v8::Local<v8::Value> texture,origin;
    if(!reader.enumeration("aspect",copy.aspect)||!reader.uint32("mipLevel",copy.mipLevel)||!reader.get("origin",origin))return false;
    uint32_t coords[]={0,0,0};if(!origin->IsUndefined()&&!read_copy_coordinates(isolate,context,origin,coords,false))return false;
    copy.origin={coords[0],coords[1],coords[2]};
    if(!reader.get("texture",texture))return false;
    if(!v8_webgpu_textures::is_instance(texture))return reader.fail("Copy requires GPUTexture");
    copy.texture=v8_webgpu_textures::native_reference(texture);return true;
}
inline bool read_copy_layout(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Value> value,wgpu::TexelCopyBufferLayout& layout){
    webgpu_state_reader reader(isolate,context,value);
    return reader.uint32("bytesPerRow",layout.bytesPerRow)&&reader.uint64("offset",layout.offset)&&reader.uint32("rowsPerImage",layout.rowsPerImage);
}
inline bool read_copy_buffer(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Value> value,wgpu::TexelCopyBufferInfo& copy){
    if(!read_copy_layout(isolate,context,value,copy.layout))return false;
    webgpu_state_reader reader(isolate,context,value);v8::Local<v8::Value> buffer;
    if(!reader.get("buffer",buffer))return false;
    if(!v8_webgpu_buffers::is_instance(buffer))return reader.fail("Copy requires GPUBuffer");
    copy.buffer=v8_webgpu_buffers::native_reference(buffer);return true;
}
}
