#pragma once
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace webscene::graphics {
// EGL initialization is display-wide, not reference-counted per engine. Serialize
// acquisition and final termination, including the interval after the last lease
// expires, so another engine cannot initialize a display just before it is killed.
class angle_display {
    struct registry {
        std::mutex mutex;
        std::unordered_map<EGLDisplay,size_t> references;
    };
    std::shared_ptr<registry> registry_;
    EGLDisplay display_;
    bool registered_{};
    angle_display(std::shared_ptr<registry> state,EGLDisplay display)
        : registry_(std::move(state)),display_(display) {}
public:
    angle_display(const angle_display&)=delete;
    angle_display& operator=(const angle_display&)=delete;
    ~angle_display() {
        if (!registered_) return;
        std::lock_guard lock(registry_->mutex);
        auto item=registry_->references.find(display_);
        if (item==registry_->references.end()) std::terminate();
        if (--item->second==0) {
            eglTerminate(display_);
            registry_->references.erase(item);
        }
    }
    static std::shared_ptr<angle_display> acquire(EGLint backend) {
        static auto state=std::make_shared<registry>();
        auto get_display=reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
        if (!get_display) throw std::runtime_error("ANGLE platform display entry point missing");
        const EGLint attributes[]={EGL_PLATFORM_ANGLE_TYPE_ANGLE,backend,
            EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE,EGL_PLATFORM_ANGLE_DEVICE_TYPE_HARDWARE_ANGLE,EGL_NONE};
        std::lock_guard lock(state->mutex);
        auto display=get_display(EGL_PLATFORM_ANGLE_ANGLE,nullptr,attributes);
        if (display==EGL_NO_DISPLAY) throw std::runtime_error("ANGLE hardware display unavailable");
        auto lease=std::shared_ptr<angle_display>(new angle_display(state,display));
        auto [item,inserted]=state->references.try_emplace(display,0);
        if (inserted && !eglInitialize(display,nullptr,nullptr)) {
            state->references.erase(item);
            throw std::runtime_error("ANGLE hardware display initialization failed");
        }
        ++item->second;
        lease->registered_=true;
        return lease;
    }
    EGLDisplay get() const noexcept { return display_; }
};
} // namespace webscene::graphics
