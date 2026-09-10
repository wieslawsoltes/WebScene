#pragma once
#include "dawn_dxgi_submission.h"
#include <array>
#include "windows_gpu_adapter.h"
#if defined(_WIN32)
namespace webscene::graphics {
// Engine-thread canvas provider. The host drains completed publications into the
// scene and applies canvas generation/serial filtering there. Four pending
// retirements bound storage and callback ownership during presenter backpressure.
class dawn_dxgi_canvas_host final {
    struct active_frame {
        d3d12_canvas_images::frame frame;
        wgpu::Device device;
        std::shared_ptr<dawn_dxgi_image> shared;
        std::shared_ptr<void> device_lifetime;
    };
    const std::thread::id thread_=std::this_thread::get_id();
    Microsoft::WRL::ComPtr<ID3D12Device> allocator_=windows_canvas_device();
    d3d12_canvas_images images_;
    std::unique_ptr<active_frame> active_;
    std::array<std::shared_ptr<dawn_dxgi_submission>,4> pending_{};
    size_t active_slot_=0;
    std::weak_ptr<dawn_dxgi_submission> latest_submission_;
    std::shared_ptr<completion_wake> wake_;
    void check_thread()const {
        if(thread_!=std::this_thread::get_id())throw std::logic_error("Canvas provider requires its engine thread");
    }
    void clear_retired() {
        for(auto& item:pending_)if(item) {
            auto state=item->state();
            if(state==dawn_dxgi_submission::status::failed||state==dawn_dxgi_submission::status::discarded||
                state==dawn_dxgi_submission::status::consumed)item.reset();
        }
    }
public:
    explicit dawn_dxgi_canvas_host(uint64_t budget,std::shared_ptr<completion_wake> wake={})
        :images_(allocator_.Get(),budget,128,wake),wake_(std::move(wake)) {}
    dawn_dxgi_canvas_host(const dawn_dxgi_canvas_host&)=delete;
    dawn_dxgi_canvas_host& operator=(const dawn_dxgi_canvas_host&)=delete;
    ~dawn_dxgi_canvas_host() {
        // The GPUCanvasContext must retire before destroying its provider.
        // Dropping an active imported frame could recycle submitted storage.
        if(active_)std::terminate();
    }
    wgpu::Texture acquire(image_metadata metadata,const wgpu::Device& device,
        const wgpu::TextureDescriptor& descriptor,std::shared_ptr<void> device_lifetime={}) {
        check_thread();if(active_)throw std::logic_error("Canvas already owns a current frame");
        clear_retired();size_t slot=0;while(slot<pending_.size()&&pending_[slot])++slot;
        if(slot==pending_.size())return {};
        HRESULT status;auto frame=images_.acquire(metadata,status);if(!frame)return {};
        auto shared=import_dawn_dxgi_canvas_texture(*frame,device,descriptor,allocator_.Get());
        if(!shared)return {};
        // Allocate host state before BeginAccess; failure cannot abandon access.
        auto next=std::make_unique<active_frame>(active_frame{std::move(*frame),device,shared,std::move(device_lifetime)});
        if(shared->begin(false)!=dxgi_access_status::success)return {};
        active_=std::move(next);active_slot_=slot;
        return shared->texture();
    }
    void retire(const wgpu::Texture& texture,bool present) {
        check_thread();
        if(!active_||texture.Get()!=active_->shared->texture().Get())
            throw std::invalid_argument("Canvas retirement requires its current texture");
        pending_[active_slot_]=dawn_dxgi_submission::publish_submitted(std::move(active_->frame),
            active_->device,active_->shared,active_->device_lifetime,wake_,present);
        latest_submission_=present ? pending_[active_slot_] : std::weak_ptr<dawn_dxgi_submission>{};
        active_.reset();
    }
    // Capture at the rendering-opportunity boundary, before draining ready
    // outputs. The weak lookup adds no hidden image retention; the returned
    // ticket owns its exact allocation independently of later provider work.
    std::unique_ptr<dawn_dxgi_submission::snapshot> capture_latest_submission() {
        check_thread();
        if(active_)throw std::logic_error("End the current GPU opportunity before capturing its output");
        auto submission=latest_submission_.lock();
        return submission ? submission->capture_snapshot() : nullptr;
    }
    std::optional<owned_image_pool::retained> take_ready() {
        check_thread();clear_retired();
        for(auto& item:pending_)if(item&&item->state()==dawn_dxgi_submission::status::ready) {
            auto image=item->take_ready();item.reset();return image;
        }
        return {};
    }
    bool has_completed_retirements()const {
        check_thread();
        for(const auto& item:pending_)if(item&&item->state()!=dawn_dxgi_submission::status::pending)return true;
        return false;
    }
    bool idle() {
        check_thread();clear_retired();if(active_)return false;
        for(const auto& item:pending_)if(item)return false;
        return true;
    }
    bool can_acquire() {
        check_thread();clear_retired();
        if(active_||images_.busy_images()>=4)return false;
        return std::any_of(pending_.begin(),pending_.end(),[](const auto& item){return !item;});
    }
    image_lease_pool::occupancy inspect_occupancy() const { return images_.inspect_occupancy(); }
    size_t busy_images()const {check_thread();return images_.busy_images();}
};
} // namespace webscene::graphics
#endif
