#pragma once
#include "v8_webgpu_textures.h"
#include "v8_webgpu_object_descriptor.h"
namespace webscene::graphics {
struct webgpu_attachment_view {
    wgpu::Texture texture;wgpu::TextureView view;
    wgpu::TextureView native() const {return texture?texture.CreateView():view;}
};
inline bool read_webgpu_attachment_view(v8::Isolate* isolate,v8::Local<v8::Value> input,webgpu_attachment_view& output) {
    try {output.view=v8_webgpu_texture_views::native_reference(input);return true;}catch(const std::invalid_argument&){}
    try {output.texture=v8_webgpu_textures::native_reference(input);return true;}catch(const std::exception&){}
    isolate->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8Literal(isolate,"Attachment requires a GPUTexture or GPUTextureView")));return false;
}
inline bool read_webgpu_color(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Value> input,wgpu::Color& output,bool& valid_shape) {
    webgpu_state_reader reader(isolate,context,input);wgpu::Color converted{};v8::Local<v8::Value> iterator;
    const auto number=[&](v8::Local<v8::Value> value,double& output) {
        v8::Local<v8::Number> numeric;if(!value->ToNumber(context).ToLocal(&numeric))return false;
        if(!std::isfinite(numeric->Value()))return reader.fail("GPUColor components must be finite doubles");output=numeric->Value();return true;
    };
    if(input->IsObject()&&!input.As<v8::Object>()->Get(context,v8::Symbol::GetIterator(isolate)).ToLocal(&iterator))return false;
    if(!iterator.IsEmpty()&&!iterator->IsNullOrUndefined()) {
        size_t count=0;double* components[]={&converted.r,&converted.g,&converted.b,&converted.a};
        if(!read_webgpu_sequence(isolate,context,input,[&](auto value){double n;if(!number(value,n))return false;if(count<4)*components[count]=n;++count;return true;},iterator))return false;
        valid_shape=count==4;
    }else {
        v8::Local<v8::Value> value;
        for(auto [name,target]:{std::pair{"a",&converted.a},{"b",&converted.b},{"g",&converted.g},{"r",&converted.r}}) {
            if(!reader.get(name,value))return false;if(value->IsUndefined())return reader.fail("GPUColor components are required");if(!number(value,*target))return false;
        }
        valid_shape=true;
    }
    output=converted;return true;
}
struct webgpu_pass_color {
    wgpu::RenderPassColorAttachment state;
    webgpu_attachment_view view,resolve;
    bool valid_shape=true;
};
struct webgpu_pass_depth {
    wgpu::RenderPassDepthStencilAttachment state;
    webgpu_attachment_view view;
};
struct webgpu_render_pass_descriptor {
    std::string label;std::vector<std::optional<webgpu_pass_color>> colors;
    std::optional<webgpu_pass_depth> depth;uint64_t max_draw_count=50000000;
    bool valid_shapes()const{for(const auto& color:colors)if(color&&!color->valid_shape)return false;return true;}
    template<class Execute> void with_native(Execute execute) const & {
        if(!valid_shapes())throw std::invalid_argument("GPUColor sequence must have four elements");
        std::vector<wgpu::RenderPassColorAttachment> attachments;attachments.reserve(colors.size());
        for(const auto& color:colors) {
            wgpu::RenderPassColorAttachment attachment{};
            if(color){attachment=color->state;attachment.view=color->view.native();attachment.resolveTarget=color->resolve.native();}
            attachments.push_back(std::move(attachment));
        }
        wgpu::RenderPassDescriptor descriptor{};descriptor.label=wgpu::StringView(label.data(),label.size());
        descriptor.colorAttachmentCount=attachments.size();descriptor.colorAttachments=attachments.data();
        wgpu::RenderPassDepthStencilAttachment native_depth{};
        if(depth){native_depth=depth->state;native_depth.view=depth->view.native();descriptor.depthStencilAttachment=&native_depth;}
        wgpu::RenderPassMaxDrawCount count{};count.maxDrawCount=max_draw_count;descriptor.nextInChain=&count;
        execute(descriptor);
    }
};
inline bool read_webgpu_render_pass_descriptor(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Value> input,webgpu_render_pass_descriptor& output) {
    webgpu_render_pass_descriptor converted;webgpu_state_reader reader(isolate,context,input);v8::Local<v8::Value> value;
    if(!read_webgpu_object_label(isolate,context,input,converted.label)||!reader.get("colorAttachments",value))return false;
    if(!read_webgpu_sequence(isolate,context,value,[&](auto input) {
        if(input->IsNullOrUndefined()){converted.colors.emplace_back(std::nullopt);return true;}
        webgpu_state_reader color(isolate,context,input);webgpu_pass_color attachment;v8::Local<v8::Value> value;bool depth_slice_provided=false;
        if(!color.get("clearValue",value))return false;
        if(!value->IsUndefined()&&!read_webgpu_color(isolate,context,value,attachment.state.clearValue,attachment.valid_shape))return false;
        if(!color.uint32("depthSlice",attachment.state.depthSlice,false,&depth_slice_provided)||!color.enumeration("loadOp",attachment.state.loadOp,true)||!color.get("resolveTarget",value))return false;
        if(!value->IsUndefined()&&!read_webgpu_attachment_view(isolate,value,attachment.resolve))return false;
        if(!color.enumeration("storeOp",attachment.state.storeOp,true)||!color.get("view",value)||!read_webgpu_attachment_view(isolate,value,attachment.view))return false;
        // Explicit UINT_MAX must remain invalid rather than native omitted.
        if(depth_slice_provided&&attachment.state.depthSlice==wgpu::kDepthSliceUndefined)attachment.state.depthSlice=wgpu::kDepthSliceUndefined-1;
        converted.colors.emplace_back(std::move(attachment));return true;
    }))return false;
    if(!reader.get("depthStencilAttachment",value))return false;
    if(!value->IsUndefined()) {
        webgpu_state_reader depth(isolate,context,value);webgpu_pass_depth attachment;
        if(!depth.floating("depthClearValue",attachment.state.depthClearValue)||!depth.enumeration("depthLoadOp",attachment.state.depthLoadOp)
            ||!depth.boolean("depthReadOnly",attachment.state.depthReadOnly)||!depth.enumeration("depthStoreOp",attachment.state.depthStoreOp)
            ||!depth.uint32("stencilClearValue",attachment.state.stencilClearValue)||!depth.enumeration("stencilLoadOp",attachment.state.stencilLoadOp)
            ||!depth.boolean("stencilReadOnly",attachment.state.stencilReadOnly)||!depth.enumeration("stencilStoreOp",attachment.state.stencilStoreOp)
            ||!depth.get("view",value)||!read_webgpu_attachment_view(isolate,value,attachment.view))return false;
        converted.depth=std::move(attachment);
    }
    if(!reader.uint64("maxDrawCount",converted.max_draw_count)||!reader.get("occlusionQuerySet",value))return false;
    // Query-set wrappers are not exposed yet; do not accept forged interfaces.
    if(!value->IsUndefined())return reader.fail("Occlusion queries require a live GPUQuerySet");
    if(!reader.get("timestampWrites",value))return false;
    if(!value->IsUndefined()) {
        webgpu_state_reader timestamp(isolate,context,value);uint32_t begin=0,end=0;
        if(!timestamp.uint32("beginningOfPassWriteIndex",begin)||!timestamp.uint32("endOfPassWriteIndex",end)||!timestamp.get("querySet",value))return false;
        return reader.fail("Timestamp writes require a live GPUQuerySet");
    }
    output=std::move(converted);return true;
}
} // namespace webscene::graphics
