// Test-only provider factory. Never linked into or installed with the runtime.
#include "graphics/image_lease_abi.h"
#include "graphics/iosurface_canvas_images.h"
namespace {
std::weak_ptr<webscene::graphics::image_provider_lifetime> observed;
}
extern "C" __attribute__((visibility("default"))) uint8_t webscene_test_create_iosurface(
    webscene_gpu_image_lease_v3** result) {
    if (!result) return 0;
    *result=nullptr;
    try {
        using namespace webscene::graphics;
        iosurface_canvas_images pool(1024*1024);
        auto frame=pool.acquire({700,0,1,1,701,1,17,4,image_format::bgra8_unorm});
        if (!frame) return 0;
        frame->producer.begin();
        auto image=frame->producer.publish();
        frame->producer.complete(); frame.reset(); // No GPU commands in this lifetime fixture.
        if (!image) return 0;
        auto observer=image->begin_consumer();
        if (!observer) return 0;
        observed=observer->provider();
        observer->complete();
        auto lease=std::make_unique<webscene_gpu_image_lease_v3>(std::move(*image));
        *result=lease.release();
        return 1;
    } catch (...) { return 0; }
}
extern "C" __attribute__((visibility("default"))) uint8_t webscene_test_iosurface_alive() {
    return observed.expired() ? 0 : 1;
}
