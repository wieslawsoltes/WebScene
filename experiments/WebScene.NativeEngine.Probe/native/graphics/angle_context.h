#pragma once
#include "angle_display.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <memory>
#include <stdexcept>
#include <thread>

namespace webscene::graphics {
// A context retains its display owner's lease. The display service, not each
// context, initializes/terminates EGL; destroying one context cannot terminate
// the display used by another engine/context.
class angle_context {
    const std::thread::id thread_ = std::this_thread::get_id();
    std::shared_ptr<angle_display> display_lease_;
    EGLDisplay display_;
    EGLContext context_{EGL_NO_CONTEXT};
    EGLSurface surface_{EGL_NO_SURFACE};
    void check_thread() const {
        if (std::this_thread::get_id()!=thread_)
            throw std::logic_error("ANGLE context requires its execution thread");
    }
public:
    angle_context(std::shared_ptr<angle_display> display_lease, EGLConfig config, EGLint major)
        : display_lease_(std::move(display_lease)),
          display_(display_lease_ ? display_lease_->get() : EGL_NO_DISPLAY) {
        if (!display_lease_ || display_==EGL_NO_DISPLAY || (major!=2 && major!=3))
            throw std::invalid_argument("ANGLE context requires a display lease and ES version");
        if (!eglBindAPI(EGL_OPENGL_ES_API)) throw std::runtime_error("Cannot bind ANGLE ES API");
        const EGLint surface_attributes[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
        surface_=eglCreatePbufferSurface(display_,config,surface_attributes);
        if (surface_==EGL_NO_SURFACE) throw std::runtime_error("Cannot create ANGLE backing surface");
        const EGLint attributes[]={EGL_CONTEXT_CLIENT_VERSION,major,
            EGL_CONTEXT_WEBGL_COMPATIBILITY_ANGLE,EGL_TRUE,
            EGL_ROBUST_RESOURCE_INITIALIZATION_ANGLE,EGL_TRUE,EGL_NONE};
        context_=eglCreateContext(display_,config,EGL_NO_CONTEXT,attributes);
        if (context_==EGL_NO_CONTEXT) {
            eglDestroySurface(display_,surface_);
            throw std::runtime_error("Cannot create isolated WebGL-compatible ANGLE context");
        }
    }
    angle_context(const angle_context&)=delete;
    angle_context& operator=(const angle_context&)=delete;
    ~angle_context() {
        if (std::this_thread::get_id()!=thread_) std::terminate();
        if (eglGetCurrentContext()==context_)
            if (!eglMakeCurrent(display_,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT)) std::terminate();
        eglDestroyContext(display_,context_);
        eglDestroySurface(display_,surface_);
    }
    class scope {
        angle_context& owner_;
        EGLDisplay previous_display_;
        EGLContext previous_context_;
        EGLSurface previous_draw_,previous_read_;
    public:
        explicit scope(angle_context& owner) : owner_(owner) {
            owner_.check_thread();
            previous_display_=eglGetCurrentDisplay();
            previous_context_=eglGetCurrentContext();
            previous_draw_=eglGetCurrentSurface(EGL_DRAW);
            previous_read_=eglGetCurrentSurface(EGL_READ);
            if (!eglMakeCurrent(owner_.display_,owner_.surface_,owner_.surface_,owner_.context_))
                throw std::runtime_error("Cannot activate ANGLE context");
        }
        scope(const scope&)=delete;
        scope& operator=(const scope&)=delete;
        ~scope() {
            if (std::this_thread::get_id()!=owner_.thread_) std::terminate();
            auto display=previous_display_==EGL_NO_DISPLAY ? owner_.display_ : previous_display_;
            if (!eglMakeCurrent(display,previous_draw_,previous_read_,previous_context_)) std::terminate();
        }
    };
};
} // namespace webscene::graphics
