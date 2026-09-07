#include "graphics/angle_context.h"
#include <GLES2/gl2.h>
#include <iostream>
using namespace webscene::graphics;
void require(bool value) { if (!value) throw std::runtime_error("requirement failed"); }
struct display_owner {
    EGLDisplay display;
    ~display_owner() { eglTerminate(display); }
};
int main() {
    auto get_display=reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (!get_display) return 1;
#if defined(__APPLE__)
    constexpr EGLint backend=EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE;
#elif defined(_WIN32)
    constexpr EGLint backend=EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE;
#else
    constexpr EGLint backend=EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE;
#endif
    const EGLint attrs[]={EGL_PLATFORM_ANGLE_TYPE_ANGLE,backend,
        EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE,EGL_PLATFORM_ANGLE_DEVICE_TYPE_HARDWARE_ANGLE,EGL_NONE};
    auto display=get_display(EGL_PLATFORM_ANGLE_ANGLE,nullptr,attrs);
    if (display==EGL_NO_DISPLAY || !eglInitialize(display,nullptr,nullptr)) return 77;
    auto lease=std::make_shared<display_owner>(); lease->display=display;
    const EGLint config_attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_NONE};
    EGLConfig config{}; EGLint count{};
    require(eglChooseConfig(display,config_attrs,&config,1,&count) && count==1);
    angle_context first(display,lease,config,2);
    {
        angle_context second(display,lease,config,2);
        angle_context::scope active(first);
        const auto first_context=eglGetCurrentContext();
        glClearColor(1,0,0,1);
        {
            angle_context::scope other(second);
            require(eglGetCurrentContext()!=first_context);
            glClearColor(0,1,0,1);
        }
        require(eglGetCurrentContext()==first_context);
        GLfloat color[4]{}; glGetFloatv(GL_COLOR_CLEAR_VALUE,color);
        require(color[0]==1 && color[1]==0);
        bool rejected=false;
        std::thread wrong([&] { try { angle_context::scope invalid(first); } catch(const std::logic_error&) { rejected=true; } });
        wrong.join(); require(rejected);
    }
    { angle_context::scope still_valid(first); require(glGetError()==GL_NO_ERROR); }
    require(eglGetCurrentContext()==EGL_NO_CONTEXT);
    std::cout << "ANGLE isolated contexts, nested restoration and execution-thread checks passed\n";
}
