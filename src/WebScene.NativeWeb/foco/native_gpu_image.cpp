#include "native_gpu_image.hpp"
#include "iosurface_canvas_images.h"
namespace webscene::foco_host {
std::shared_ptr<const foco::composition_resource_attachment> make_gpu_image(
    uint32_t node,uint64_t generation,std::shared_ptr<const webscene_gpu_image_lease_v3> image) {
    if(!node||!generation||!image)
        throw std::invalid_argument("Native frame requires a GPU image");
    if(image->requires_producer_wait && (!image->dependencies || !image->dependencies->count()))
        throw std::invalid_argument("Pending native frame requires GPU dependencies");
    if(image->dependencies) {
        if(image->dependencies->count()>4096) throw std::invalid_argument("Too many GPU dependencies");
        for(size_t i=0;i<image->dependencies->count();++i) {
            void* event=nullptr;uint64_t value=0;
            if(!image->dependencies->metal_event(i,event,value)||!event)
                throw std::invalid_argument("Native frame requires valid Metal dependencies");
        }
    }
    const auto m=image->value.describe();
    if(m.canvas!=node)throw std::invalid_argument("Canvas identity mismatch");
    auto frame=std::make_shared<foco::webscene_gpu_frame>();
    foco::webscene_gpu_image_info info;
    info.canvas=m.canvas;info.allocation=m.allocation;info.allocation_generation=m.allocation_generation;
    info.content_serial=m.content_serial;info.producer_timeline=m.producer_timeline;info.producer_value=m.producer_value;
    info.width=m.width;info.height=m.height;info.format=uint32_t(m.format);info.alpha=uint32_t(m.alpha);
    info.color_space=uint32_t(m.color_space);info.orientation=uint32_t(m.orientation);
    frame->images.push_back({const_cast<webscene_gpu_image_lease_v3*>(image.get()),info});
    frame->release=[image=std::move(image)] {};
    frame->begin_consumer=[](const void* lease,void** output)->uint32_t {
        if(!lease||!output)return 1;
        *output=nullptr;
        try {
            auto value=static_cast<const webscene_gpu_image_lease_v3*>(lease)->value.begin_consumer();
            if(!value)return 1;
            try { *output=new webscene_gpu_image_consumer_v3(std::move(*value),static_cast<const webscene_gpu_image_lease_v3*>(lease)->dependencies); }
            catch(...) { value->complete(); throw; }
            return 0;
        } catch(...) { return 1; }
    };
    frame->complete_consumer=[](void* value) {
        auto* consumer=static_cast<webscene_gpu_image_consumer_v3*>(value);
        if(consumer){consumer->value.complete();delete consumer;}
    };
    frame->dependency_count=[](const void* value,uint32_t* output)->uint8_t {
        if(!value||!output)return 0;
        const auto& dependencies=static_cast<const webscene_gpu_image_consumer_v3*>(value)->dependencies;
        const auto count=dependencies?dependencies->count():0;
        if(count>4096)return 0;
        *output=static_cast<uint32_t>(count);return 1;
    };
    frame->get_metal_event=[](const void* value,uint32_t index,foco::webscene_gpu_metal_event_view* output)->uint8_t {
        if(!value||!output)return 0;
        try {
            const auto& dependencies=static_cast<const webscene_gpu_image_consumer_v3*>(value)->dependencies;
            if(!dependencies||index>=dependencies->count())return 0;
            void* event=nullptr;uint64_t signal=0;
            if(!dependencies->metal_event(index,event,signal)||!event)return 0;
            output->event=event;output->value=signal;return 1;
        } catch(...) {return 0;}
    };
    frame->get_iosurface=[](const void* value,foco::webscene_gpu_iosurface_view* output)->uint8_t {
        if(!value||!output)return 0;
        try {
            const auto& consumer=static_cast<const webscene_gpu_image_consumer_v3*>(value)->value;
            const auto& surface=graphics::iosurface_canvas_images::resolve(consumer);
            output->surface=surface.borrowed_handle();output->allocation_bytes=surface.allocation_bytes();return 1;
        } catch(...) { return 0; }
    };
    return frame;
}
}
