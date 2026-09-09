#pragma once
#include "dxgi_bridge_contract.h"
#include "nt_handle.h"
#include "dawn_dxgi_fences.h"
#include <memory>
#include <span>
#include <thread>
#include <webgpu/webgpu_cpp.h>

namespace webscene::graphics {
enum class dxgi_import_status {
    success,unsupported_platform,invalid_argument,unsupported_pairing,
    missing_device_feature,handle_failure,import_failure,property_mismatch,out_of_memory
};
enum class dxgi_access_status { success,invalid_state,invalid_fences,native_failure,device_lost,fence_export_failure,fence_import_failure };
// Thread-confined import/access owner. EndAccess is a fence handoff, not CPU/GPU
// completion; the external consumer must wait on every returned fence/value.
class dawn_dxgi_image {
    struct handle_anchor { virtual ~handle_anchor()=default; };
#if defined(_WIN32)
    struct windows_handle final : handle_anchor {
        owned_nt_handle value;
        explicit windows_handle(owned_nt_handle owned):value(std::move(owned)) {}
    };
#endif
    std::shared_ptr<handle_anchor> handle_; // Last destroyed, after Dawn objects.
    wgpu::Device device_;
    wgpu::SharedTextureMemory memory_;
    wgpu::Texture texture_;
    wgpu::SharedTextureMemoryProperties properties_{};
    const std::thread::id thread_=std::this_thread::get_id();
    bool active_{},failed_{};
    void* source_handle_{}; // Identity only; the owned duplicate anchors access.
    void check_thread() const {
        if (thread_!=std::this_thread::get_id()) throw std::logic_error("DXGI access requires its owner thread");
    }
    static wgpu::TextureFormat native_format(image_format format) {
        switch (format) {
            case image_format::rgba8_unorm: return wgpu::TextureFormat::RGBA8Unorm;
            case image_format::bgra8_unorm: return wgpu::TextureFormat::BGRA8Unorm;
            case image_format::rgba16_float: return wgpu::TextureFormat::RGBA16Float;
            case image_format::rgba8_srgb: return wgpu::TextureFormat::RGBA8UnormSrgb;
            case image_format::bgra8_srgb: return wgpu::TextureFormat::BGRA8UnormSrgb;
        }
        return wgpu::TextureFormat::Undefined;
    }
    dxgi_import_status initialize(const wgpu::Device& device,void* handle,const image_metadata& image,
        dxgi_sync synchronization,wgpu::TextureUsage usage) {
        device_=device; source_handle_=handle;
        wgpu::SharedTextureMemoryDXGISharedHandleDescriptor dxgi{};
        dxgi.handle=handle; dxgi.useKeyedMutex=synchronization==dxgi_sync::keyed_mutex;
        wgpu::SharedTextureMemoryDescriptor descriptor{}; descriptor.nextInChain=&dxgi;
        memory_=device.ImportSharedTextureMemory(&descriptor);
        if (!memory_ || memory_.GetProperties(&properties_)!=wgpu::Status::Success || memory_.IsDeviceLost())
            return dxgi_import_status::import_failure;
        if (properties_.size.width!=image.width || properties_.size.height!=image.height
            || properties_.size.depthOrArrayLayers!=1 || properties_.format!=native_format(image.format)
            || (properties_.usage & usage)!=usage)
            return dxgi_import_status::property_mismatch;
        wgpu::TextureDescriptor texture{};
        texture.size=properties_.size; texture.format=properties_.format; texture.usage=usage;
        texture_=memory_.CreateTexture(&texture);
        return texture_ ? dxgi_import_status::success : dxgi_import_status::import_failure;
    }
public:
    bool matches(const wgpu::Device& device,void* handle) const noexcept {
        return device_.Get()==device.Get() && source_handle_==handle;
    }
    bool expire_texture() {
        check_thread();
        if(active_ || failed_ || !texture_)return false;
        texture_.Destroy();return true;
    }
    dawn_dxgi_image()=default;
    dawn_dxgi_image(const dawn_dxgi_image&)=delete;
    dawn_dxgi_image& operator=(const dawn_dxgi_image&)=delete;
    ~dawn_dxgi_image() { if (active_) std::terminate(); }
    const wgpu::SharedTextureMemoryProperties& properties() const noexcept { return properties_; }
    dxgi_access_status begin(bool initialized,std::span<const wgpu::SharedFence> fences={},
        std::span<const uint64_t> values={}) {
        check_thread();
        if (!memory_ || !texture_ || active_ || failed_) return dxgi_access_status::invalid_state;
        if (fences.size()!=values.size()) return dxgi_access_status::invalid_fences;
        for (const auto& fence:fences) if (!fence) return dxgi_access_status::invalid_fences;
        if (memory_.IsDeviceLost()) return dxgi_access_status::device_lost;
        wgpu::SharedTextureMemoryBeginAccessDescriptor descriptor{};
        descriptor.initialized=initialized;
        descriptor.fenceCount=fences.size(); descriptor.fences=fences.data();
        descriptor.signaledValueCount=values.size(); descriptor.signaledValues=values.data();
        if (memory_.BeginAccess(texture_,&descriptor)!=wgpu::Status::Success) {
            // Native access state can change before backend setup fails. Reject
            // reuse of an ambiguous allocation rather than retrying its access.
            failed_=true; return dxgi_access_status::native_failure;
        }
        active_=true; return dxgi_access_status::success;
    }
    dxgi_access_status begin_shared_fences(bool initialized,std::span<void* const> handles,
        std::span<const uint64_t> values) {
        check_thread();
        if (!memory_ || !texture_ || active_ || failed_) return dxgi_access_status::invalid_state;
        if (handles.size()!=values.size()) return dxgi_access_status::invalid_fences;
        if (handles.empty()) return begin(initialized);
        imported_dxgi_fences waits;
        if (import_dxgi_fences(device_,handles,values,waits)!=dxgi_fence_status::success)
            return dxgi_access_status::fence_import_failure;
        // BeginAccess retains the imported fences for the native texture access.
        // The temporary vectors do not need to survive the interval.
        return begin(initialized,waits.fences,waits.values);
    }
    const wgpu::Texture& texture() const {
        check_thread();
        if (!active_) throw std::logic_error("DXGI texture access has not begun");
        return texture_;
    }
    dxgi_access_status end(wgpu::SharedTextureMemoryEndAccessState& handoff) {
        check_thread();
        if (!memory_ || !active_) return dxgi_access_status::invalid_state;
        wgpu::SharedTextureMemoryEndAccessState result;
        const auto status=memory_.EndAccess(texture_,&result);
        // Dawn may end access before failing to export a fence. Never retry an
        // ambiguous handoff or expose this allocation for further sampling.
        active_=false;
        if (status!=wgpu::Status::Success) {
            failed_=true; handoff=wgpu::SharedTextureMemoryEndAccessState{};
            return memory_.IsDeviceLost() ? dxgi_access_status::device_lost : dxgi_access_status::native_failure;
        }
        handoff=std::move(result); return dxgi_access_status::success;
    }
#if defined(_WIN32)
    // Own all outgoing handles before Dawn's temporary end state is freed. A
    // failed export invalidates this allocation: presenting without its waits
    // would race producer work. The caller must discard the external image.
    dxgi_access_status end_owned(std::vector<owned_dxgi_fence_wait>& waits,bool& initialized) {
        check_thread();
        waits.clear(); initialized=false;
        wgpu::SharedTextureMemoryEndAccessState state;
        const auto status=end(state);
        if (status!=dxgi_access_status::success) return status;
        if (export_dxgi_fences(state,waits)!=dxgi_fence_status::success) {
            failed_=true; return dxgi_access_status::fence_export_failure;
        }
        initialized=state.initialized;
        return dxgi_access_status::success;
    }
#endif
    // Device removal cannot yield a valid fence handoff. Permit cleanup only
    // after Dawn confirms loss; callers must invalidate the external image.
    bool abandon_lost_device() {
        check_thread();
        if (!memory_ || !memory_.IsDeviceLost()) return false;
        active_=false; failed_=true; return true;
    }
    static dxgi_import_status import(const wgpu::Device& device,void* borrowed_nt_handle,
        const dxgi_endpoint& producer,const dxgi_endpoint& consumer,const image_metadata& image,
        wgpu::TextureUsage usage,std::unique_ptr<dawn_dxgi_image>& result) {
        result.reset();
#if !defined(_WIN32)
        return dxgi_import_status::unsupported_platform;
#else
        if (!device || !win32_nt_handle_ops::valid(borrowed_nt_handle) || usage==wgpu::TextureUsage::None)
            return dxgi_import_status::invalid_argument;
        const auto choice=choose_dxgi_bridge(producer,consumer,image);
        if (choice.status!=dxgi_bridge_status::supported) return dxgi_import_status::unsupported_pairing;
        if (!device.HasFeature(wgpu::FeatureName::SharedTextureMemoryDXGISharedHandle)
            || (choice.synchronization==dxgi_sync::shared_fence
                && !device.HasFeature(wgpu::FeatureName::SharedFenceDXGISharedHandle)))
            return dxgi_import_status::missing_device_feature;
        try {
            auto owned=std::make_unique<dawn_dxgi_image>();
            auto handle=std::make_shared<windows_handle>(owned_nt_handle::duplicate(borrowed_nt_handle));
            owned->handle_=handle;
            const auto status=owned->initialize(device,handle->value.get(),image,choice.synchronization,usage);
            if (status!=dxgi_import_status::success) return status;
            owned->source_handle_=borrowed_nt_handle;
            result=std::move(owned); return dxgi_import_status::success;
        } catch (const std::bad_alloc&) { return dxgi_import_status::out_of_memory; }
          catch (const std::system_error&) { return dxgi_import_status::handle_failure; }
#endif
    }
};
} // namespace webscene::graphics
