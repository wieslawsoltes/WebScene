#include "rendering/webscene_graphite.hpp"
#include <foco/platform.hpp>
#include <foco/composition.hpp>
#include <foco/webscene_packet.hpp>
#include "include/core/SkSurface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkBitmap.h"
#include <cstring>
#include <stdexcept>
#include <vector>
struct image_attachment final : foco::composition_resource_attachment {};
struct image_control final : foco::control {
    std::vector<std::byte> packet;
    std::shared_ptr<const foco::composition_resource_attachment> owner;
    uint64_t generation=1;
    std::optional<foco::composition_command_stream_view> composition_command_stream() const noexcept override {
        return foco::composition_command_stream_view{packet,generation,
            static_cast<uint32_t>(foco::command_stream_format::webscene_scene_v1),16,16,owner};
    }
};
int main() {
    namespace p=foco::webscene_packet;
    p::header header{};
    header.magic_value=p::magic;header.version_value=p::version;
    header.viewport_width=16;header.viewport_height=16;header.revision=1;
    header.layer_count=1;header.layer_offset=sizeof(header);
    header.byte_count=sizeof(header)+sizeof(p::layer);
    p::layer layer{};
    layer.node_id=17;layer.generation=9;layer.flags=p::layer_replace|p::layer_external_image;
    layer.x=4;layer.y=4;layer.width=8;layer.height=8;layer.bitmap_width=4;layer.bitmap_height=4;
    std::vector<std::byte> bytes(header.byte_count);
    std::memcpy(bytes.data(),&header,sizeof(header));std::memcpy(bytes.data()+sizeof(header),&layer,sizeof(layer));
    auto control=foco::make_ref<image_control>();
    control->packet=bytes;control->owner=std::make_shared<image_attachment>();
    std::weak_ptr<const foco::composition_resource_attachment> weak=control->owner;
    control->measure({16,16});control->arrange({0,0,16,16});
    foco::scene_publisher publisher;foco::headless_renderer renderer;
    foco::compositor compositor(*publisher.mailbox(),renderer);
    publisher.commit(*control,foco::theme_variant::dark);
    control->owner.reset();
    if(weak.expired())throw std::runtime_error("publication dropped image owner");
    compositor.tick();
    bool retained=false;
    for(const auto& [id, resource]:compositor.scene().resources()) retained|=bool(resource.attachment);
    if(!retained)throw std::runtime_error("retained scene lost image attachment");
    ++control->generation;control->invalidate_render();
    publisher.commit(*control,foco::theme_variant::dark);compositor.tick();
    if(!weak.expired())throw std::runtime_error("replaced image attachment leaked");
    auto source=SkSurfaces::Raster(SkImageInfo::MakeN32Premul(4,4));
    source->getCanvas()->clear(SK_ColorRED);
    if(foco::rendering::compile_webscene_picture(bytes))
        throw std::runtime_error("unresolved image must reject picture");
    int calls=0;
    auto picture=foco::rendering::compile_webscene_picture(bytes,[&](auto node,auto generation){
        if(node!=17||generation!=9)throw std::runtime_error("image identity mismatch");
        ++calls;return source->makeImageSnapshot();
    });
    if(!picture||calls!=1)throw std::runtime_error("image resolver not used");
    auto target=SkSurfaces::Raster(SkImageInfo::MakeN32Premul(16,16));
    target->getCanvas()->clear(SK_ColorWHITE);target->getCanvas()->drawPicture(picture->picture);
    SkBitmap pixels;pixels.allocN32Pixels(16,16);
    if(!target->readPixels(pixels.pixmap(),0,0))throw std::runtime_error("readPixels failed");
    if(pixels.getColor(5,5)!=SK_ColorRED||pixels.getColor(2,2)!=SK_ColorWHITE)
        throw std::runtime_error("external image placement/scaling failed");
}
