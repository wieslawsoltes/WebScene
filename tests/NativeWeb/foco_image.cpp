#include "rendering/webscene_graphite.hpp"
#include <foco/webscene_packet.hpp>
#include "include/core/SkSurface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkBitmap.h"
#include <cstring>
#include <stdexcept>
#include <vector>
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
