#include "native_gpu_image.hpp"
#include "iosurface_canvas_images.h"
namespace webscene::foco_host {
std::shared_ptr<const foco::composition_resource_attachment> make_gpu_image(
    uint32_t node,uint64_t generation,std::shared_ptr<const webscene_gpu_image_lease_v3> image) {
    if(!node||!generation||!image||image->requires_producer_wait)
        throw std::invalid_argument("Native frame requires a completed GPU image");
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
            try { *output=new webscene_gpu_image_consumer_v3(std::move(*value)); }
            catch(...) { value->complete(); throw; }
            return 0;
        } catch(...) { return 1; }
    };
    frame->complete_consumer=[](void* value) {
        auto* consumer=static_cast<webscene_gpu_image_consumer_v3*>(value);
        if(consumer){consumer->value.complete();delete consumer;}
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
