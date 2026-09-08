#pragma once
#include "owned_image_pool.h"
#include "../webscene_native_engine.h"
// Backend-owned completion dependencies. Native pointers remain borrowed from
// this retained owner and must never be exposed to JavaScript.
struct webscene_gpu_producer_dependencies {
    virtual ~webscene_gpu_producer_dependencies()=default;
    virtual size_t count() const noexcept=0;
    virtual bool metal_event(size_t index,void*& event,uint64_t& value) const=0;
};
// Native-only implementation of the opaque C handles. Never expose to JS.
struct webscene_gpu_image_lease_v3 {
    webscene::graphics::owned_image_pool::retained value;
    std::shared_ptr<const webscene_gpu_producer_dependencies> dependencies;
    explicit webscene_gpu_image_lease_v3(webscene::graphics::owned_image_pool::retained image,
        std::shared_ptr<const webscene_gpu_producer_dependencies> producer={})
        : value(std::move(image)),dependencies(std::move(producer)) {}
};
struct webscene_gpu_image_consumer_v3 {
    webscene::graphics::owned_image_pool::consumer value;
    std::shared_ptr<const webscene_gpu_producer_dependencies> dependencies;
    explicit webscene_gpu_image_consumer_v3(webscene::graphics::owned_image_pool::consumer image,
        std::shared_ptr<const webscene_gpu_producer_dependencies> producer={})
        : value(std::move(image)),dependencies(std::move(producer)) {}
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

// A submitted opportunity whose exact output could not be retained must remain
// an explicit failed dependency. Absence would incorrectly reuse an older image.
struct webscene_failed_gpu_image_snapshot final : webscene_gpu_image_snapshot {
    const webscene::graphics::image_metadata metadata;
    explicit webscene_failed_gpu_image_snapshot(webscene::graphics::image_metadata value)
        : metadata(value) {}
    webscene::graphics::image_metadata describe() const override { return metadata; }
    status state() const override { return status::failed; }
    std::shared_ptr<const webscene_gpu_image_lease_v3> resolve() override { return {}; }
};
