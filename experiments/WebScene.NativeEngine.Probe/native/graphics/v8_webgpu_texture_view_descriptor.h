#pragma once
#include "v8_webgpu_render_state.h"
#include <string>
namespace webscene::graphics {
struct webgpu_texture_view_descriptor {
    std::string label;
    std::u16string swizzle=u"rgba";
    wgpu::TextureFormat format=wgpu::TextureFormat::Undefined;
    wgpu::TextureViewDimension dimension=wgpu::TextureViewDimension::Undefined;
    wgpu::TextureAspect aspect=wgpu::TextureAspect::All;
    uint32_t base_mip=0,base_layer=0,usage=0;
    std::optional<uint32_t> mip_count,layer_count;
    template<class Execute> void with_native(Execute execute) const & {
        wgpu::TextureViewDescriptor result{};result.label=wgpu::StringView(label.data(),label.size());
        result.format=format;result.dimension=dimension;result.aspect=aspect;result.baseMipLevel=base_mip;result.baseArrayLayer=base_layer;
        result.mipLevelCount=mip_count.value_or(wgpu::kMipLevelCountUndefined);result.arrayLayerCount=layer_count.value_or(wgpu::kArrayLayerCountUndefined);
        result.usage=static_cast<wgpu::TextureUsage>(usage&0x3fu);
        // Explicit UINT_MAX counts must not become native "unspecified".
        // Unknown browser usage bits must not enable native-only capabilities.
        // Both are WebGPU validation failures, represented by an invalid native
        // enum so Dawn returns an error view through its usual error machinery.
        if((mip_count&&*mip_count==wgpu::kMipLevelCountUndefined)||(layer_count&&*layer_count==wgpu::kArrayLayerCountUndefined)||(usage&~0x3fu))
            result.dimension=static_cast<wgpu::TextureViewDimension>(0xffffffffu);
        wgpu::TextureComponentSwizzleDescriptor components{};
        if(swizzle!=u"rgba") {
            const auto component=[](char16_t c) {
                switch(c) {case u'r':return wgpu::ComponentSwizzle::R;case u'g':return wgpu::ComponentSwizzle::G;
                    case u'b':return wgpu::ComponentSwizzle::B;case u'a':return wgpu::ComponentSwizzle::A;
                    case u'0':return wgpu::ComponentSwizzle::Zero;case u'1':return wgpu::ComponentSwizzle::One;
                    default:return static_cast<wgpu::ComponentSwizzle>(0xffffffffu);}
            };
            if(swizzle.size()==4)components.swizzle={component(swizzle[0]),component(swizzle[1]),component(swizzle[2]),component(swizzle[3])};
            else components.swizzle.r=static_cast<wgpu::ComponentSwizzle>(0xffffffffu);
            result.nextInChain=&components;
        }
        execute(result);
    }
};
inline bool read_webgpu_texture_view_descriptor(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Value> input,webgpu_texture_view_descriptor& output) {
    webgpu_state_reader reader(isolate,context,input);webgpu_texture_view_descriptor converted;v8::Local<v8::Value> value;
    const auto optional_count=[&](const char* name,std::optional<uint32_t>& output) {
        if(!reader.get(name,value))return false;if(value->IsUndefined())return true;
        v8::Local<v8::Number> number;if(!value->ToNumber(context).ToLocal(&number))return false;
        const double n=std::trunc(number->Value());if(!std::isfinite(n)||n<0||n>4294967295.0)return reader.fail("Texture view count out of range");
        output=static_cast<uint32_t>(n);return true;
    };
    if(!reader.get("label",value))return false;
    if(!value->IsUndefined()) {
        v8::Local<v8::String> text;if(!value->ToString(context).ToLocal(&text))return false;
        v8::String::Utf8Value bytes(isolate,text);if(!*bytes)return false;converted.label.assign(*bytes,bytes.length());
    }
    if(!optional_count("arrayLayerCount",converted.layer_count)||!reader.enumeration("aspect",converted.aspect)
        ||!reader.uint32("baseArrayLayer",converted.base_layer)||!reader.uint32("baseMipLevel",converted.base_mip)
        ||!reader.enumeration("dimension",converted.dimension)||!reader.enumeration("format",converted.format)
        ||!optional_count("mipLevelCount",converted.mip_count)||!reader.get("swizzle",value))return false;
    if(!value->IsUndefined()) {
        v8::Local<v8::String> text;if(!value->ToString(context).ToLocal(&text))return false;
        v8::String::Value units(isolate,text);if(!*units)return false;converted.swizzle.assign(reinterpret_cast<const char16_t*>(*units),units.length());
    }
    if(!reader.uint32("usage",converted.usage))return false;
    output=std::move(converted);return true;
}
} // namespace webscene::graphics
