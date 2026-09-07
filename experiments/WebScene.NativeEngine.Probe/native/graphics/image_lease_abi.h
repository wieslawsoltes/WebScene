#pragma once
#include "owned_image_pool.h"
#include "../webscene_native_engine.h"
// Native-only implementation of the opaque C handles. Never expose to JS.
struct webscene_gpu_image_lease_v3 {
    webscene::graphics::owned_image_pool::retained value;
    explicit webscene_gpu_image_lease_v3(webscene::graphics::owned_image_pool::retained image)
        : value(std::move(image)) {}
};
struct webscene_gpu_image_consumer_v3 {
    webscene::graphics::owned_image_pool::consumer value;
    explicit webscene_gpu_image_consumer_v3(webscene::graphics::owned_image_pool::consumer image)
        : value(std::move(image)) {}
};
