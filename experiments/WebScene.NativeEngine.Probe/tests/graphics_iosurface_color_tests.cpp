#include "graphics/iosurface_color.h"
#include "graphics/iosurface_canvas_images.h"
#include <iostream>
int main() {
    using webscene::graphics::iosurface_color;
    if (iosurface_color::create_bgra8(0,4,1024*1024) ||
        iosurface_color::create_bgra8(17,0,1024*1024) ||
        iosurface_color::create_bgra8(UINT32_MAX,4,UINT64_MAX) ||
        iosurface_color::create_bgra8(17,4,17*4*4)) return 1;
    auto image=iosurface_color::create_bgra8(17,4,1024*1024);
    if (!image || image->allocation_bytes()>1024*1024 ||
        IOSurfaceGetWidth(image->borrowed_handle())!=17 ||
        IOSurfaceGetHeight(image->borrowed_handle())!=4) return 2;
    const auto bytes=image->allocation_bytes();
    if (iosurface_color::create_bgra8(17,4,bytes-1)) return 3;
    auto exact=iosurface_color::create_bgra8(17,4,bytes);
    if (!exact || exact->allocation_bytes()!=bytes) return 4;
    using namespace webscene::graphics;
    auto pool=std::make_unique<iosurface_canvas_images>(3*bytes);
    image_metadata first{700,0,1,1,701,1,17,4,image_format::bgra8_unorm};
    auto a=pool->acquire(first);
    if (!a) return 5;
    a->producer.begin();
    auto old=a->producer.publish();
    a->producer.complete(); a.reset();
    if (!old) return 6;
    auto reader=old->begin_consumer();
    first.width=9; first.allocation_generation=2; first.content_serial=2; first.producer_value=2;
    auto b=pool->acquire(first);
    if (!b) return 7;
    b->producer.begin();
    auto resized=b->producer.publish();
    b->producer.complete(); b.reset();
    auto newReader=resized->begin_consumer();
    old.reset(); resized.reset();
    pool.reset();
    bool retained=IOSurfaceGetWidth(iosurface_canvas_images::resolve(*reader).borrowed_handle())==17 &&
        IOSurfaceGetWidth(iosurface_canvas_images::resolve(*newReader).borrowed_handle())==9;
    reader->complete(); newReader->complete();
    if (!retained) return 8;
    iosurface_canvas_images bounded(3*bytes);
    auto one=bounded.acquire(first), two=bounded.acquire(first), three=bounded.acquire(first);
    if (!one || !two || !three || bounded.acquire(first)) return 9;
    one.reset(); two.reset(); three.reset();
    if (bounded.busy_images()!=0) return 10;
    std::cout << "IOSurface padded budget verified: " << bytes << " bytes\n";
    return 0;
}
