#pragma once
#include "angle_display.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <memory>
#include <string>
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
    bool lost_{};
    PFNGLGETGRAPHICSRESETSTATUSEXTPROC reset_status_{};
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
        const auto* extensions=eglQueryString(display_,EGL_EXTENSIONS);
        if(!extensions || (std::string(" ")+extensions+" ").find(" EGL_EXT_create_context_robustness ")==std::string::npos)
            throw std::runtime_error("ANGLE context reset notification is required");
        reset_status_=reinterpret_cast<PFNGLGETGRAPHICSRESETSTATUSEXTPROC>(eglGetProcAddress("glGetGraphicsResetStatusEXT"));
        if(!reset_status_) throw std::runtime_error("ANGLE reset status entry point missing");
        if (!eglBindAPI(EGL_OPENGL_ES_API)) throw std::runtime_error("Cannot bind ANGLE ES API");
        const EGLint surface_attributes[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
        surface_=eglCreatePbufferSurface(display_,config,surface_attributes);
        if (surface_==EGL_NO_SURFACE) throw std::runtime_error("Cannot create ANGLE backing surface");
        const EGLint attributes[]={EGL_CONTEXT_CLIENT_VERSION,major,
            EGL_CONTEXT_WEBGL_COMPATIBILITY_ANGLE,EGL_TRUE,
            EGL_ROBUST_RESOURCE_INITIALIZATION_ANGLE,EGL_TRUE,
            EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT,EGL_LOSE_CONTEXT_ON_RESET_EXT,EGL_NONE};
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
    bool is_lost() const { check_thread(); return lost_; }
    // Only query while this context is current. Loss is sticky across scopes.
    bool poll_loss() {
        check_thread();
        if(eglGetCurrentContext()!=context_) throw std::logic_error("ANGLE loss query requires current context");
        lost_=lost_ || reset_status_()!=GL_NO_ERROR;
        return lost_;
    }
    class scope {
        angle_context& owner_;
        EGLDisplay previous_display_;
        EGLContext previous_context_;
        EGLSurface previous_draw_,previous_read_;
        void restore() noexcept {
            const auto display=previous_display_==EGL_NO_DISPLAY ? owner_.display_ : previous_display_;
            if(previous_context_==owner_.context_ && owner_.lost_) {
                if(!eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT)) std::terminate();
                return;
            }
            if(!eglMakeCurrent(display,previous_draw_,previous_read_,previous_context_)) {
                // A device-wide reset can invalidate the previous context too.
                if(eglGetError()!=EGL_CONTEXT_LOST || !eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT))
                    std::terminate();
            }
        }
    public:
        explicit scope(angle_context& owner) : owner_(owner) {
            owner_.check_thread();
            if(owner_.lost_) throw std::runtime_error("ANGLE context is lost");
            previous_display_=eglGetCurrentDisplay();
            previous_context_=eglGetCurrentContext();
            previous_draw_=eglGetCurrentSurface(EGL_DRAW);
            previous_read_=eglGetCurrentSurface(EGL_READ);
            if (!eglMakeCurrent(owner_.display_,owner_.surface_,owner_.surface_,owner_.context_)) {
                if(eglGetError()==EGL_CONTEXT_LOST) owner_.lost_=true;
                throw std::runtime_error("Cannot activate ANGLE context");
            }
            if(owner_.poll_loss()) { restore(); throw std::runtime_error("ANGLE context is lost"); }
        }
        scope(const scope&)=delete;
        scope& operator=(const scope&)=delete;
        ~scope() {
            if (std::this_thread::get_id()!=owner_.thread_) std::terminate();
            if(eglGetCurrentContext()==owner_.context_) owner_.poll_loss();
            restore();
        }
    };
};
} // namespace webscene::graphics
