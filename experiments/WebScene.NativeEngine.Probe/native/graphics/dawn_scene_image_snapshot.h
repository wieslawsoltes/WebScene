#pragma once
#include "dawn_iosurface_submission.h"
#include "image_lease_abi.h"
#if defined(__APPLE__)
namespace webscene::graphics {
class dawn_scene_image_snapshot final : public webscene_gpu_image_snapshot {
    class dependencies final : public webscene_gpu_producer_dependencies {
        std::shared_ptr<dawn_iosurface_submission::snapshot> ticket_;
    public:
        explicit dependencies(std::shared_ptr<dawn_iosurface_submission::snapshot> ticket):ticket_(std::move(ticket)) {}
        size_t count() const noexcept override { return ticket_->producer_handoff().fenceCount; }
        bool metal_event(size_t index,void*& event,uint64_t& value) const override {
            event=nullptr; value=0;
            const auto& handoff=ticket_->producer_handoff();
            if(index>=handoff.fenceCount || handoff.fenceCount!=handoff.signaledValueCount) return false;
            wgpu::SharedFenceExportInfo type; handoff.fences[index].ExportInfo(&type);
            if(type.type!=wgpu::SharedFenceType::MTLSharedEvent) return false;
            wgpu::SharedFenceMTLSharedEventExportInfo metal;
            wgpu::SharedFenceExportInfo info;info.nextInChain=&metal;
            handoff.fences[index].ExportInfo(&info);
            event=metal.sharedEvent;value=handoff.signaledValues[index];return event!=nullptr;
        }
    };
    std::shared_ptr<dawn_iosurface_submission::snapshot> ticket_;
    const image_metadata metadata_;
    std::optional<owned_image_pool::retained> ready_;
    std::shared_ptr<const webscene_gpu_image_lease_v3> resolved_;
public:
    explicit dawn_scene_image_snapshot(std::unique_ptr<dawn_iosurface_submission::snapshot> ticket)
        :ticket_(std::move(ticket)),metadata_(ticket_->describe()) {}
    image_metadata describe() const override { return metadata_; }
    status state() const override {
        switch(ticket_->state()) {
            case dawn_iosurface_submission::status::pending:return status::pending;
            case dawn_iosurface_submission::status::ready:
            case dawn_iosurface_submission::status::consumed:return status::ready;
            default:return status::failed;
        }
    }
    std::shared_ptr<const webscene_gpu_image_lease_v3> resolve() override {
        if(state()!=status::ready)return {};
        if(resolved_)return resolved_;
        if(!ready_) {
            auto image=ticket_->take_ready();
            if(!image)return {};
            ready_.emplace(std::move(*image));
        }
        // Keep ownership in ready_ if allocation throws; resolution is retryable.
        auto producer=std::make_shared<dependencies>(ticket_);
        resolved_=std::make_shared<webscene_gpu_image_lease_v3>(std::move(*ready_),std::move(producer));
        ready_.reset();
        return resolved_;
    }
    std::shared_ptr<const webscene_gpu_image_lease_v3> resolve_with_gpu_waits() override {
        if(auto completed=resolve())return completed;
        if(!ticket_->can_enqueue_gpu_wait())return {};
        if(resolved_)return resolved_;
        auto producer=std::make_shared<dependencies>(ticket_);
        for(size_t i=0;i<producer->count();++i) {
            void* event=nullptr;uint64_t value=0;
            if(!producer->metal_event(i,event,value))return {};
        }
        if(!ready_) {
            auto image=ticket_->take_for_gpu_wait();
            if(!image)return {};
            ready_.emplace(std::move(*image));
        }
        resolved_=std::make_shared<webscene_gpu_image_lease_v3>(std::move(*ready_),std::move(producer),true);
        ready_.reset();return resolved_;
    }
};
}
#endif
