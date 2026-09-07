#pragma once
#include "angle_context.h"
#include "dawn_event_service.h"
#include "dawn_device.h"
#include "command_channel.h"
#include "release_channel.h"
#include <chrono>
#include <algorithm>

namespace webscene::graphics {
// One instance per engine, created on its worker. This is native API state only;
// framework-owned Skia contexts and presenter textures never enter this service.
struct graphics_metrics {
    size_t live_devices{},live_contexts{};
    completion_metrics completions{};
    queue_metrics commands{};
    size_t release_registrations{};
    size_t live_adapters{};
};
class graphics_service {
    const std::thread::id thread_ = std::this_thread::get_id();
    const resource_owner owner_{new_owner_token(),new_owner_token(),0};
    std::shared_ptr<completion_wake> wake_;
    const size_t completion_capacity_;
    const bool measure_latency_;
    resource_table<angle_context> contexts_;
    resource_table<dawn_device> devices_;
    resource_table<wgpu::Adapter> adapters_;
    std::unique_ptr<dawn_event_service> dawn_;
    bool closed_{};
    std::chrono::steady_clock::time_point next_event_poll_{};
    size_t active_context_scopes_{};
    size_t active_device_scopes_{};
    size_t active_adapter_scopes_{};
    bool executing_commands_{};
    bool pumping_{};
    std::shared_ptr<command_channel> commands_;
    std::shared_ptr<release_channel> releases_;
    uint64_t executed_command_serial_{};
    void check_thread() const {
        if (std::this_thread::get_id()!=thread_)
            throw std::logic_error("graphics service requires its engine worker");
    }
    void check_open() const {
        check_thread();
        if (closed_) throw std::logic_error("graphics service is closed");
    }
public:
    graphics_service(std::shared_ptr<completion_wake> wake,size_t context_capacity=64,size_t completion_capacity=256,bool measure_latency=false)
        : wake_(std::move(wake)),completion_capacity_(completion_capacity),measure_latency_(measure_latency),contexts_(context_capacity,owner_),devices_(context_capacity,owner_),adapters_(context_capacity,owner_) {}
    graphics_service(const graphics_service&)=delete;
    graphics_service& operator=(const graphics_service&)=delete;
    ~graphics_service() {
        if (std::this_thread::get_id()!=thread_) std::terminate();
        close();
    }
    uint64_t engine_identity() const noexcept { return owner_.engine; }
    bool dawn_initialized() const { check_thread(); return dawn_!=nullptr; }
    dawn_event_service& dawn() {
        check_open();
        if (!dawn_) dawn_=std::make_unique<dawn_event_service>(completion_capacity_,wake_,measure_latency_);
        return *dawn_;
    }
    // Internal discovery completion hook. Only adapters discovered through this
    // engine's Dawn instance may be adopted. Async device requests must copy the
    // native reference during with_adapter, never retain its borrowed address.
    resource_handle<wgpu::Adapter> adopt_adapter(wgpu::Adapter adapter) {
        check_open();
        if (!adapter) throw std::invalid_argument("Cannot adopt a null adapter");
        return adapters_.insert(owner_,std::make_unique<wgpu::Adapter>(std::move(adapter)));
    }
    template<class Execute> void with_adapter(resource_handle<wgpu::Adapter> handle,Execute execute) {
        check_open();
        const auto& adapter=adapters_.get(handle,owner_);
        struct guard {
            size_t& count;
            explicit guard(size_t& value) : count(value) { ++count; }
            ~guard() { --count; }
        } scope(active_adapter_scopes_);
        execute(adapter);
    }
    void destroy_adapter(resource_handle<wgpu::Adapter> handle) {
        check_open();
        if (active_adapter_scopes_) throw std::logic_error("Cannot destroy adapters during execution");
        adapters_.destroy(handle,owner_);
    }
    size_t live_adapters() const { check_thread(); return adapters_.resident_count(); }
    // Internal request-device completion hook: pass a freshly created device
    // from this service's instance exactly once, with its originating adapter.
    resource_handle<dawn_device> adopt_device(wgpu::Adapter adapter,wgpu::Device device,std::shared_ptr<device_loss_signal> loss={},size_t buffer_capacity=1024,size_t shader_capacity=1024,size_t render_pipeline_capacity=1024,size_t texture_capacity=1024,size_t texture_view_capacity=4096,size_t command_capacity=1024) {
        check_open();
        return devices_.insert(owner_,std::make_unique<dawn_device>(
            owner_.engine,dawn().completions(),std::move(adapter),std::move(device),std::move(loss),buffer_capacity,shader_capacity,render_pipeline_capacity,texture_capacity,texture_view_capacity,command_capacity));
    }
    template<class Execute> void with_device(resource_handle<dawn_device> handle,Execute execute) {
        check_open();
        auto& device=devices_.get(handle,owner_);
        struct guard {
            size_t& count;
            explicit guard(size_t& value) : count(value) { ++count; }
            ~guard() { --count; }
        } scope(active_device_scopes_);
        execute(device);
    }
    void destroy_device(resource_handle<dawn_device> handle) {
        check_open();
        if (active_device_scopes_) throw std::logic_error("Cannot destroy devices during execution");
        devices_.destroy(handle,owner_);
    }
    size_t live_devices() const { check_thread(); return devices_.resident_count(); }
    resource_handle<angle_context> create_angle_context(EGLint backend,EGLint major) {
        check_open();
        auto display=angle_display::acquire(backend);
        const EGLint attributes[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE,major==2 ? EGL_OPENGL_ES2_BIT : EGL_OPENGL_ES3_BIT,EGL_NONE};
        EGLConfig config{}; EGLint count{};
        if (!eglChooseConfig(display->get(),attributes,&config,1,&count) || count!=1)
            throw std::runtime_error("ANGLE context configuration unavailable");
        return contexts_.insert(owner_,std::make_unique<angle_context>(display,config,major));
    }
    template<class Execute> void with_angle_context(resource_handle<angle_context> handle,Execute execute) {
        check_open();
        angle_context::scope scope(contexts_.get(handle,owner_));
        struct execution_guard {
            size_t& count;
            explicit execution_guard(size_t& value) : count(value) { ++count; }
            ~execution_guard() { --count; }
        } guard(active_context_scopes_);
        execute();
    }
    // Called by the execution thread after queued context operations have drained.
    void destroy_angle_context(resource_handle<angle_context> handle) {
        check_open();
        if (active_context_scopes_) throw std::logic_error("Cannot destroy ANGLE contexts during execution");
        contexts_.destroy(handle,owner_);
    }
    // Finalizers enqueue these value-only records through a retained endpoint.
    // A full queue requires retry/retention by the caller; it is not a release.
    static graphics_command deferred_buffer_release(resource_handle<dawn_device> device,resource_handle<wgpu::Buffer> buffer) noexcept {
        return {[](graphics_service& service,std::span<const std::byte>,const graphics_command::arguments& args) noexcept {
            try {
                service.with_device({args[0],args[1],static_cast<uint32_t>(args[2])},[&](auto& owner) {
                    owner.release_buffer({args[3],args[4],static_cast<uint32_t>(args[5])});
                });
            } catch (const std::invalid_argument&) { /* Device or wrapper already released. */ }
        },{device.table,device.generation,device.slot,buffer.table,buffer.generation,buffer.slot}};
    }
    static graphics_command deferred_shader_module_release(resource_handle<dawn_device> device,resource_handle<wgpu::ShaderModule> shader) noexcept {
        return {[](graphics_service& service,std::span<const std::byte>,const graphics_command::arguments& args) noexcept {
            try {
                service.with_device({args[0],args[1],static_cast<uint32_t>(args[2])},[&](auto& owner) {
                    owner.release_shader_module({args[3],args[4],static_cast<uint32_t>(args[5])});
                });
            } catch (const std::invalid_argument&) { /* Device or wrapper already released. */ }
        },{device.table,device.generation,device.slot,shader.table,shader.generation,shader.slot}};
    }
    static graphics_command deferred_render_pipeline_release(resource_handle<dawn_device> device,resource_handle<wgpu::RenderPipeline> pipeline) noexcept {
        return {[](graphics_service& service,std::span<const std::byte>,const graphics_command::arguments& args) noexcept {
            try {
                service.with_device({args[0],args[1],static_cast<uint32_t>(args[2])},[&](auto& owner) {
                    owner.release_render_pipeline({args[3],args[4],static_cast<uint32_t>(args[5])});
                });
            } catch (const std::invalid_argument&) { /* Device or wrapper already released. */ }
        },{device.table,device.generation,device.slot,pipeline.table,pipeline.generation,pipeline.slot}};
    }
    static graphics_command deferred_texture_release(resource_handle<dawn_device> device,resource_handle<wgpu::Texture> pipeline) noexcept {
        return {[](graphics_service& service,std::span<const std::byte>,const graphics_command::arguments& args) noexcept {
            try {
                service.with_device({args[0],args[1],static_cast<uint32_t>(args[2])},[&](auto& owner) {
                    owner.release_texture({args[3],args[4],static_cast<uint32_t>(args[5])});
                });
            } catch (const std::invalid_argument&) { /* Device or wrapper already released. */ }
        },{device.table,device.generation,device.slot,pipeline.table,pipeline.generation,pipeline.slot}};
    }
    static graphics_command deferred_texture_view_release(resource_handle<dawn_device> device,resource_handle<wgpu::TextureView> pipeline) noexcept {
        return {[](graphics_service& service,std::span<const std::byte>,const graphics_command::arguments& args) noexcept {
            try {
                service.with_device({args[0],args[1],static_cast<uint32_t>(args[2])},[&](auto& owner) {
                    owner.release_texture_view({args[3],args[4],static_cast<uint32_t>(args[5])});
                });
            } catch (const std::invalid_argument&) { /* Device or wrapper already released. */ }
        },{device.table,device.generation,device.slot,pipeline.table,pipeline.generation,pipeline.slot}};
    }
    static graphics_command deferred_command_encoder_release(resource_handle<dawn_device> device,resource_handle<wgpu::CommandEncoder> pipeline) noexcept {
        return {[](graphics_service& service,std::span<const std::byte>,const graphics_command::arguments& args) noexcept {
            try {
                service.with_device({args[0],args[1],static_cast<uint32_t>(args[2])},[&](auto& owner) {
                    owner.release_command_encoder({args[3],args[4],static_cast<uint32_t>(args[5])});
                });
            } catch (const std::invalid_argument&) { /* Device or wrapper already released. */ }
        },{device.table,device.generation,device.slot,pipeline.table,pipeline.generation,pipeline.slot}};
    }
    static graphics_command deferred_render_pass_release(resource_handle<dawn_device> device,resource_handle<wgpu::RenderPassEncoder> pipeline) noexcept {
        return {[](graphics_service& service,std::span<const std::byte>,const graphics_command::arguments& args) noexcept {
            try {
                service.with_device({args[0],args[1],static_cast<uint32_t>(args[2])},[&](auto& owner) {
                    owner.release_render_pass({args[3],args[4],static_cast<uint32_t>(args[5])});
                });
            } catch (const std::invalid_argument&) { /* Device or wrapper already released. */ }
        },{device.table,device.generation,device.slot,pipeline.table,pipeline.generation,pipeline.slot}};
    }
    static graphics_command deferred_command_buffer_release(resource_handle<dawn_device> device,resource_handle<wgpu::CommandBuffer> pipeline) noexcept {
        return {[](graphics_service& service,std::span<const std::byte>,const graphics_command::arguments& args) noexcept {
            try {
                service.with_device({args[0],args[1],static_cast<uint32_t>(args[2])},[&](auto& owner) {
                    owner.release_command_buffer({args[3],args[4],static_cast<uint32_t>(args[5])});
                });
            } catch (const std::invalid_argument&) { /* Device or wrapper already released. */ }
        },{device.table,device.generation,device.slot,pipeline.table,pipeline.generation,pipeline.slot}};
    }
    static graphics_command deferred_adapter_release(resource_handle<wgpu::Adapter> handle) noexcept {
        return {[](graphics_service& service,std::span<const std::byte>,const graphics_command::arguments& args) noexcept {
            try { service.destroy_adapter({args[0],args[1],static_cast<uint32_t>(args[2])}); }
            catch (const std::invalid_argument&) { /* Already explicitly destroyed. */ }
        },{handle.table,handle.generation,handle.slot}};
    }
    static graphics_command deferred_device_release(resource_handle<dawn_device> handle) noexcept {
        return {[](graphics_service& service,std::span<const std::byte>,const graphics_command::arguments& args) noexcept {
            try { service.destroy_device({args[0],args[1],static_cast<uint32_t>(args[2])}); }
            catch (const std::invalid_argument&) { /* Already explicitly destroyed. */ }
        },{handle.table,handle.generation,handle.slot}};
    }
    static graphics_command deferred_context_release(resource_handle<angle_context> handle) noexcept {
        return {[](graphics_service& service,std::span<const std::byte>,const graphics_command::arguments& args) noexcept {
            try { service.destroy_angle_context({args[0],args[1],static_cast<uint32_t>(args[2])}); }
            catch (const std::invalid_argument&) { /* Already explicitly destroyed. */ }
        },{handle.table,handle.generation,handle.slot}};
    }
    // Create lazily on the engine thread; other threads retain only the channel.
    std::shared_ptr<command_channel> command_endpoint(size_t capacity=256,size_t upload_limit=65536) {
        check_open();
        if (!commands_) commands_=std::make_shared<command_channel>(capacity,upload_limit,wake_);
        return commands_;
    }
    std::shared_ptr<release_channel> release_endpoint(size_t capacity=256) {
        check_open();
        if (!releases_) releases_=std::make_shared<release_channel>(capacity,command_endpoint(),wake_);
        return releases_;
    }
    size_t drain_commands(size_t budget=64) {
        check_thread();
        if (!commands_) return 0;
        if (executing_commands_) throw std::logic_error("graphics command dispatch is not reentrant");
        struct guard {
            bool& active;
            explicit guard(bool& value) : active(value) { active=true; }
            ~guard() { active=false; }
        } scope(executing_commands_);
        size_t count=0;
        while (count<budget && commands_->consume_one([&](const auto& command,auto upload,uint64_t serial) {
            command.execute(*this,upload,command.values);
            executed_command_serial_=serial;
        })) ++count;
        size_t released=0;
        while (releases_ && released<budget && releases_->consume_one(executed_command_serial_,[&](const auto& command) {
            command.execute(*this,{},command.values);
        })) ++released;
        return count;
    }
    template<class Deliver> size_t pump(Deliver deliver,size_t budget=64) {
        check_thread();
        if (pumping_) throw std::logic_error("graphics completion pumping is not reentrant");
        struct pump_guard {
            bool& active;
            explicit pump_guard(bool& value) : active(value) { active=true; }
            ~pump_guard() { active=false; }
        } scope(pumping_);
        devices_.visit_live([](auto& device) { device.process_loss(); });
        drain_commands(budget);
        if (!dawn_) return 0;
        next_event_poll_=std::chrono::steady_clock::now()+std::chrono::milliseconds(1);
        return dawn_->pump(deliver,budget);
    }
    bool has_ready_work() const {
        check_thread();
        if (devices_.any_live([](const auto& device) { return device.loss_pending(); })) return true;
        if (commands_ && commands_->metrics().depth) return true;
        if (releases_ && releases_->has_ready(executed_command_serial_)) return true;
        return dawn_ && (dawn_->completions()->has_ready()
            || (!closed_ && dawn_->completions()->has_pending()
                && std::chrono::steady_clock::now()>=next_event_poll_));
    }
    std::chrono::milliseconds recommended_idle_wait(std::chrono::milliseconds maximum) const {
        check_thread();
        if (devices_.any_live([](const auto& device) { return device.loss_pending(); })) return std::chrono::milliseconds::zero();
        if (commands_ && commands_->metrics().depth) return std::chrono::milliseconds::zero();
        if (releases_ && releases_->has_ready(executed_command_serial_)) return std::chrono::milliseconds::zero();
        if (!dawn_) return maximum;
        // Cancellation delivery remains runnable after admission closes.
        if (dawn_->completions()->has_ready()) return std::chrono::milliseconds::zero();
        // Only outstanding native operations require ProcessEvents polling.
        if (closed_ || !dawn_->completions()->has_pending()) return maximum;
        const auto now=std::chrono::steady_clock::now();
        if (now>=next_event_poll_)
            return std::chrono::milliseconds::zero();
        return std::min(maximum,std::chrono::ceil<std::chrono::milliseconds>(next_event_poll_-now));
    }
    graphics_metrics metrics() const {
        check_thread();
        return {devices_.resident_count(),contexts_.resident_count(),
            dawn_ ? dawn_->completions()->metrics() : completion_metrics{},
            commands_ ? commands_->metrics() : queue_metrics{},
            releases_ ? releases_->occupied() : 0,adapters_.resident_count()};
    }
    size_t live_contexts() const { check_thread(); return contexts_.resident_count(); }
    void close() {
        check_thread();
        if (closed_) return;
        if (active_context_scopes_ || active_device_scopes_ || active_adapter_scopes_ || executing_commands_ || pumping_)
            throw std::logic_error("Cannot close graphics service during native execution");
        if (releases_) releases_->close();
        if (commands_) {
            commands_->close();
            drain_commands(std::numeric_limits<size_t>::max());
        }
        closed_=true;
        devices_.destroy_owner(owner_);
        adapters_.destroy_owner(owner_);
        if (dawn_) dawn_->close();
        contexts_.destroy_owner(owner_);
    }
};
} // namespace webscene::graphics
