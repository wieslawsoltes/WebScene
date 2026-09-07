#include "graphics/angle_context.h"
#include "graphics/angle_display.h"
#include <GLES2/gl2.h>
#include <iostream>
#include <string_view>
using namespace webscene::graphics;
void require(bool value) { if (!value) throw std::runtime_error("requirement failed"); }
int main(int argc, char** argv) {
    const EGLint major=argc==2 && std::string_view(argv[1])=="3" ? 3 : 2;
#if defined(__APPLE__)
    constexpr EGLint backend=EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE;
#elif defined(_WIN32)
    constexpr EGLint backend=EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE;
#else
    constexpr EGLint backend=EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE;
#endif
    auto lease=angle_display::acquire(backend);
    const auto display=lease->get();
    auto other_engine=angle_display::acquire(backend);
    require(other_engine->get()==display);
    other_engine.reset();
    require(eglQueryString(display,EGL_VERSION)!=nullptr);
    const EGLint config_attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,major==2 ? EGL_OPENGL_ES2_BIT : EGL_OPENGL_ES3_BIT,EGL_NONE};
    EGLConfig config{}; EGLint count{};
    require(eglChooseConfig(display,config_attrs,&config,1,&count) && count==1);
    angle_context first(lease,config,major);
    {
        angle_context second(lease,config,major);
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
