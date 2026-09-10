#pragma once
#include "dawn_dxgi_submission.h"
#include "image_lease_abi.h"
#if defined(_WIN32)
namespace webscene::graphics {
class dawn_dxgi_scene_snapshot final : public webscene_gpu_image_snapshot {
    class dependencies final : public webscene_gpu_producer_dependencies {
        std::shared_ptr<dawn_dxgi_submission::snapshot> ticket_;
        std::vector<owned_dxgi_fence_wait> waits_;
    public:
        explicit dependencies(std::shared_ptr<dawn_dxgi_submission::snapshot> ticket):ticket_(std::move(ticket)) {
            if(export_dxgi_fences(ticket_->producer_handoff(),waits_)!=dxgi_fence_status::success)
                throw std::runtime_error("DXGI producer fence export failed");
        }
        size_t count() const noexcept override { return waits_.size(); }
        bool metal_event(size_t,void*& event,uint64_t& value) const override { event=nullptr;value=0;return false; }
        bool dxgi_fence(size_t index,void*& handle,uint64_t& value) const override {
            handle=nullptr;value=0;
            if(index>=waits_.size())return false;
            handle=waits_[index].handle.get();value=waits_[index].value;return true;
        }
    };
    std::shared_ptr<dawn_dxgi_submission::snapshot> ticket_;
    const image_metadata metadata_;
    std::optional<owned_image_pool::retained> ready_;
    std::shared_ptr<const webscene_gpu_image_lease_v3> resolved_;
    std::shared_ptr<const webscene_gpu_image_lease_v3> resolve_image(bool early) {
        if(resolved_)return resolved_;
        auto producer=std::make_shared<dependencies>(ticket_);
        if(!ready_) {
            auto image=early?ticket_->take_for_gpu_wait():ticket_->take_ready();
            if(!image)return {};
            ready_.emplace(std::move(*image));
        }
        resolved_=std::make_shared<webscene_gpu_image_lease_v3>(std::move(*ready_),std::move(producer),early);
        ready_.reset();return resolved_;
    }
public:
    explicit dawn_dxgi_scene_snapshot(std::unique_ptr<dawn_dxgi_submission::snapshot> ticket)
        :ticket_(std::move(ticket)),metadata_(ticket_->describe()) {}
    image_metadata describe() const override { return metadata_; }
    status state() const override {
        switch(ticket_->state()) {
            case dawn_dxgi_submission::status::pending:return status::pending;
            case dawn_dxgi_submission::status::ready:
            case dawn_dxgi_submission::status::consumed:return status::ready;
            default:return status::failed;
        }
    }
    std::shared_ptr<const webscene_gpu_image_lease_v3> resolve() override {
        return state()==status::ready?resolve_image(false):nullptr;
    }
    std::shared_ptr<const webscene_gpu_image_lease_v3> resolve_with_gpu_waits() override {
        if(auto image=resolve())return image;
        return ticket_->can_enqueue_gpu_wait()?resolve_image(true):nullptr;
    }
};
}
#endif
