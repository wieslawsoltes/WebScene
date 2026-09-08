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

// Engine-thread-only dependency for an immutable scene capture. Backends own
// completion synchronization; neither this interface nor metadata expose a
// consumer handle while producer work is pending. This is not a C ABI change.
struct webscene_gpu_image_snapshot {
    enum class status { pending, ready, failed };
    virtual ~webscene_gpu_image_snapshot()=default;
    virtual webscene::graphics::image_metadata describe() const=0;
    virtual status state() const=0;
    virtual std::shared_ptr<const webscene_gpu_image_lease_v3> resolve()=0;
};
