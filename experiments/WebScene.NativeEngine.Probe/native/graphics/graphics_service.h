#pragma once
#include "angle_context.h"
#include "dawn_event_service.h"

namespace webscene::graphics {
// One instance per engine, created on its worker. This is native API state only;
// framework-owned Skia contexts and presenter textures never enter this service.
class graphics_service {
    const std::thread::id thread_ = std::this_thread::get_id();
    const resource_owner owner_{new_owner_token(),new_owner_token(),0};
    std::shared_ptr<completion_wake> wake_;
    const size_t completion_capacity_;
    resource_table<angle_context> contexts_;
    std::unique_ptr<dawn_event_service> dawn_;
    bool closed_{};
    size_t active_context_scopes_{};
    void check_thread() const {
        if (std::this_thread::get_id()!=thread_)
            throw std::logic_error("graphics service requires its engine worker");
    }
    void check_open() const {
        check_thread();
        if (closed_) throw std::logic_error("graphics service is closed");
    }
public:
    graphics_service(std::shared_ptr<completion_wake> wake,size_t context_capacity=64,size_t completion_capacity=256)
        : wake_(std::move(wake)),completion_capacity_(completion_capacity),contexts_(context_capacity,owner_) {}
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
        if (!dawn_) dawn_=std::make_unique<dawn_event_service>(completion_capacity_,wake_);
        return *dawn_;
    }
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
    template<class Deliver> size_t pump(Deliver deliver,size_t budget=64) {
        check_thread();
        return dawn_ ? dawn_->pump(deliver,budget) : 0;
    }
    size_t live_contexts() const { check_thread(); return contexts_.resident_count(); }
    void close() {
        check_thread();
        if (closed_) return;
        if (active_context_scopes_) throw std::logic_error("Cannot close graphics service during ANGLE execution");
        closed_=true;
        if (dawn_) dawn_->close();
        contexts_.destroy_owner(owner_);
    }
};
} // namespace webscene::graphics
