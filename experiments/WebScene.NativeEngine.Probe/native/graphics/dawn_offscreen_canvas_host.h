#pragma once
#include "native_webgpu_offscreen_surface.h"
#include "webgpu_canvas_host.h"
#include <algorithm>
#include <array>
#include <functional>

namespace webscene::graphics {
// GPUCanvasContext provider, not another GPUDevice. The JavaScript application's
// configured device and the engine's existing Dawn instance own every texture.
// Three pending publications and the shared image pool bound retained storage.
class dawn_offscreen_canvas_host final {
    struct publication {
        std::shared_ptr<offscreen_gpu_snapshot> snapshot;
        bool present{};
    };
    const std::thread::id thread_=std::this_thread::get_id();
    uint64_t budget_;
    std::shared_ptr<completion_wake> wake_;
    wgpu::Instance instance_;
    std::shared_ptr<offscreen_gpu_dependencies> dependencies_;
    std::unique_ptr<dawn_canvas_images> images_;
    std::optional<dawn_canvas_images::frame> current_;
    std::array<publication,3> pending_{};
    size_t current_slot_{};
    std::weak_ptr<offscreen_gpu_snapshot> latest_;
    void check_thread() const {
        if(thread_!=std::this_thread::get_id())
            throw std::logic_error("Runtime canvas requires its engine worker");
    }
    void clear_retired() {
        for(auto& item:pending_) if(item.snapshot) {
            const auto state=item.snapshot->state();
            if(state==webscene_gpu_image_snapshot::status::failed ||
               (!item.present && state!=webscene_gpu_image_snapshot::status::pending)) item={};
        }
    }
public:
    explicit dawn_offscreen_canvas_host(uint64_t budget,std::shared_ptr<completion_wake> wake={})
        :budget_(budget),wake_(std::move(wake)) {
        if(!budget_) throw std::invalid_argument("Offscreen canvas needs a byte budget");
    }
    dawn_offscreen_canvas_host(const dawn_offscreen_canvas_host&)=delete;
    dawn_offscreen_canvas_host& operator=(const dawn_offscreen_canvas_host&)=delete;
    ~dawn_offscreen_canvas_host() {
        // The context must retire submitted work before releasing its provider.
        if(current_) std::terminate();
    }
    void bind_instance(wgpu::Instance instance) {
        check_thread();
        if(!instance || instance_) throw std::logic_error("Bind the originating Dawn instance once");
        instance_=std::move(instance);
    }
    wgpu::Texture acquire(image_metadata metadata,const wgpu::Device& device,
                          const wgpu::TextureDescriptor& descriptor) {
        check_thread();
        if(current_) throw std::logic_error("Canvas already owns a current texture");
        if(!instance_ || !device || !device.HasFeature(wgpu::FeatureName::ImplicitDeviceSynchronization))
            throw std::invalid_argument("Offscreen device lacks its instance/synchronization contract");
        clear_retired();
        size_t slot=0;while(slot<pending_.size() && pending_[slot].snapshot) ++slot;
        if(slot==pending_.size()) return {};
        if(!dependencies_ || dependencies_->gpu->device.Get()!=device.Get()) {
            // Do not multiply the budget by reconfiguring devices while older
            // allocations are still retained by the compositor or GPU queue.
            if(images_ && images_->busy_images()) return {};
            auto gpu=std::make_shared<native_webgpu_device>();
            gpu->instance=instance_;gpu->device=device;
            auto dependencies=std::make_shared<offscreen_gpu_dependencies>(gpu);
            auto images=std::make_unique<dawn_canvas_images>(device,budget_,128,wake_);
            images_=std::move(images);dependencies_=std::move(dependencies);
        }
        auto frame=images_->acquire(metadata,&descriptor);
        if(!frame) return {};
        current_slot_=slot;current_.emplace(std::move(*frame));
        return current_->texture;
    }
    void retire(const wgpu::Texture& texture,bool present) {
        check_thread();
        if(!current_ || current_->texture.Get()!=texture.Get())
            throw std::invalid_argument("Retire the canvas's current texture exactly once");
        wgpu::Future completion;
        auto submitted=images_->retire_submitted(std::move(*current_),wake_,&completion);
        current_.reset();
        // Queue completion remains live while application frames are paused.
        dependencies_->completion.enqueue(completion);
        latest_.reset();
        if(submitted) {
            auto snapshot=std::make_shared<offscreen_gpu_snapshot>(std::move(*submitted),dependencies_);
            pending_[current_slot_]={snapshot,present};
            if(present) latest_=snapshot;
        }
    }
    std::shared_ptr<offscreen_gpu_snapshot> capture_latest_submission() {
        check_thread();
        if(current_) throw std::logic_error("Finish the rendering opportunity before snapshotting");
        return latest_.lock();
    }
    std::shared_ptr<const webscene_gpu_image_lease_v3> take_ready_image() {
        check_thread();clear_retired();
        for(auto& item:pending_) if(item.snapshot && item.present &&
            item.snapshot->state()==webscene_gpu_image_snapshot::status::ready) {
            auto image=item.snapshot->resolve();item={};return image;
        }
        return {};
    }
    bool has_completed_retirements() const {
        check_thread();
        return std::any_of(pending_.begin(),pending_.end(),[](const auto& item) {
            return item.snapshot && item.snapshot->state()!=webscene_gpu_image_snapshot::status::pending;
        });
    }
    bool can_acquire() {
        check_thread();clear_retired();
        return !current_ && (!images_ || images_->busy_images()<3) &&
            std::any_of(pending_.begin(),pending_.end(),[](const auto& item){return !item.snapshot;});
    }
    bool idle() {
        check_thread();clear_retired();
        return !current_ && std::none_of(pending_.begin(),pending_.end(),[](const auto& item){return bool(item.snapshot);});
    }
    size_t busy_images() const {check_thread();return images_?images_->busy_images():0;}
    image_lease_pool::occupancy inspect_occupancy() const {
        check_thread();return images_?images_->inspect_occupancy():image_lease_pool::occupancy{};
    }
};

class dawn_offscreen_scene_snapshot final : public webscene_gpu_image_snapshot {
    std::shared_ptr<offscreen_gpu_snapshot> snapshot_;
public:
    explicit dawn_offscreen_scene_snapshot(std::shared_ptr<offscreen_gpu_snapshot> snapshot)
        :snapshot_(std::move(snapshot)) {
        if(!snapshot_) throw std::invalid_argument("Missing offscreen submission");
    }
    image_metadata describe() const override {return snapshot_->describe();}
    status state() const override {return snapshot_->state();}
    std::shared_ptr<const webscene_gpu_image_lease_v3> resolve() override {return snapshot_->resolve();}
};

inline webgpu_canvas_host make_offscreen_webgpu_canvas_host(
    std::shared_ptr<dawn_offscreen_canvas_host> provider,std::function<image_metadata()> next_metadata) {
    if(!provider || !next_metadata) throw std::invalid_argument("Canvas provider and identities required");
    webgpu_canvas_host result;
    result.validate=[](const webgpu_canvas_configuration& config) {
        if((config.format!=wgpu::TextureFormat::BGRA8Unorm && config.format!=wgpu::TextureFormat::RGBA8Unorm) ||
           config.color_space!="srgb" || config.tone_mapping!="standard")
            throw std::invalid_argument("Offscreen capture profile requires BGRA8/RGBA8 sRGB standard tone mapping");
        if(!config.device.HasFeature(wgpu::FeatureName::ImplicitDeviceSynchronization))
            throw std::invalid_argument("Offscreen canvas requires implicit native device synchronization");
    };
    result.acquire=[provider,next_metadata=std::move(next_metadata)](
        const webgpu_canvas_configuration& config,const webgpu_texture_descriptor& descriptor) {
        auto metadata=next_metadata();metadata.width=descriptor.size.width;metadata.height=descriptor.size.height;
        metadata.format=config.format==wgpu::TextureFormat::BGRA8Unorm?image_format::bgra8_unorm:image_format::rgba8_unorm;
        metadata.color_space=image_color_space::srgb;
        metadata.alpha=config.alpha_mode=="opaque"?image_alpha::opaque:image_alpha::premultiplied;
        metadata.orientation=image_orientation::top_left;
        wgpu::Texture texture;
        descriptor.with_native([&](const auto& native){texture=provider->acquire(metadata,config.device,native);});
        return texture;
    };
    result.retire=[provider](const wgpu::Texture& texture,bool present){provider->retire(texture,present);};
    return result;
}
} // namespace webscene::graphics
