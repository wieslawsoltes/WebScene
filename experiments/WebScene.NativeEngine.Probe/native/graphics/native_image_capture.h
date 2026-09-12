#pragma once
#include "image_lease_abi.h"
#include <memory>
#include <vector>

namespace webscene::graphics {
// Explicit diagnostics/export service. Normal publication never invokes it.
struct image_capture_options {
    uint64_t timeout_ns = 30'000'000'000ULL;
    uint64_t byte_budget = 128ULL * 1024 * 1024;
};
struct captured_native_image {
    image_metadata metadata;
    uint32_t row_bytes{};
    std::vector<uint8_t> pixels;
};
struct native_image_capture_provider : image_provider_lifetime {
    virtual captured_native_image capture(std::shared_ptr<owned_image_pool::consumer>,
                                           image_capture_options) = 0;
};
inline captured_native_image capture_native_image(const webscene_gpu_image_lease_v3& image,
                                                   image_capture_options options = {}) {
    if (!options.timeout_ns || options.timeout_ns > 30'000'000'000ULL || !options.byte_budget)
        throw std::invalid_argument("Invalid capture timeout or byte budget");
    if (image.requires_producer_wait)
        throw std::invalid_argument("Capture requires a completed producer image");
    auto ticket = image.value.begin_consumer();
    if (!ticket) throw std::runtime_error("Native image consumer capacity exhausted");
    owned_image_pool::consumer* raw;
    try { raw = new owned_image_pool::consumer(std::move(*ticket)); }
    catch (...) { ticket->complete(); throw; }
    auto consumer = std::shared_ptr<owned_image_pool::consumer>(raw, [](auto* value) {
        value->complete(); delete value;
    });
    auto provider = std::dynamic_pointer_cast<native_image_capture_provider>(consumer->provider());
    if (!provider) throw std::runtime_error("This image provider does not support diagnostic capture");
    return provider->capture(std::move(consumer), options);
}
} // namespace webscene::graphics
