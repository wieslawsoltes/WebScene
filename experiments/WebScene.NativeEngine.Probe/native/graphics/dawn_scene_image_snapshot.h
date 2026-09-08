#pragma once
#include "dawn_iosurface_submission.h"
#include "image_lease_abi.h"
#if defined(__APPLE__)
namespace webscene::graphics {
class dawn_scene_image_snapshot final : public webscene_gpu_image_snapshot {
    std::unique_ptr<dawn_iosurface_submission::snapshot> ticket_;
    const image_metadata metadata_;
    std::optional<owned_image_pool::retained> ready_;
    std::shared_ptr<const webscene_gpu_image_lease_v3> resolved_;
public:
    explicit dawn_scene_image_snapshot(std::unique_ptr<dawn_iosurface_submission::snapshot> ticket)
        :ticket_(std::move(ticket)),metadata_(ticket_->describe()) {}
    image_metadata describe() const override { return metadata_; }
    status state() const override {
        if(resolved_||ready_)return status::ready;
        switch(ticket_->state()) {
            case dawn_iosurface_submission::status::pending:return status::pending;
            case dawn_iosurface_submission::status::ready:
            case dawn_iosurface_submission::status::consumed:return status::ready;
            default:return status::failed;
        }
    }
    std::shared_ptr<const webscene_gpu_image_lease_v3> resolve() override {
        if(resolved_)return resolved_;
        if(state()!=status::ready)return {};
        if(!ready_) {
            auto image=ticket_->take_ready();
            if(!image)return {};
            ready_.emplace(std::move(*image));
        }
        // Keep ownership in ready_ if allocation throws; resolution is retryable.
        resolved_=std::make_shared<webscene_gpu_image_lease_v3>(std::move(*ready_));
        ready_.reset();
        return resolved_;
    }
};
}
#endif
