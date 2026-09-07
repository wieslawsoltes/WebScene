#include "graphics/iosurface_color.h"
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
    std::cout << "IOSurface padded budget verified: " << bytes << " bytes\n";
    return 0;
}
