#include "graphics/angle_context.h"
#include "graphics/angle_display.h"
#include <GLES2/gl2.h>
#include <iostream>
#include <array>
#include <string_view>
using namespace webscene::graphics;
void require(bool value) { if (!value) throw std::runtime_error("requirement failed"); }
// Diagnostic readback proves storage isolation; it is not presentation transport.
GLuint make_texture(const std::array<GLubyte,4>& pixel) {
    GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());
    require(texture!=0 && glGetError()==GL_NO_ERROR);return texture;
}
void verify_texture(GLuint texture,const std::array<GLubyte,4>& expected) {
    GLuint framebuffer=0;glGenFramebuffers(1,&framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    std::array<GLubyte,4> pixels{};
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    require(glGetError()==GL_NO_ERROR && pixels==expected);
    glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&framebuffer);
}
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
    GLuint retained_texture=0;
    const std::array<GLubyte,4> red{255,0,0,255},green{0,255,0,255};
    {
        angle_context second(lease,config,major);
        angle_context::scope active(first);
        const auto first_context=eglGetCurrentContext();
        glClearColor(1,0,0,1);
        retained_texture=make_texture(red);
        verify_texture(retained_texture,red);
        {
            angle_context::scope other(second);
            require(eglGetCurrentContext()!=first_context);
            glClearColor(0,1,0,1);
            require(!glIsTexture(retained_texture));
            const auto second_texture=make_texture(green);
            verify_texture(second_texture,green);
            glDeleteTextures(1,&second_texture);
        }
        require(eglGetCurrentContext()==first_context);
        GLfloat color[4]{}; glGetFloatv(GL_COLOR_CLEAR_VALUE,color);
        require(color[0]==1 && color[1]==0);
        bool rejected=false;
        std::thread wrong([&] { try { angle_context::scope invalid(first); } catch(const std::logic_error&) { rejected=true; } });
        wrong.join(); require(rejected);
    }
    {
        angle_context::scope still_valid(first);
        require(glGetError()==GL_NO_ERROR && glIsTexture(retained_texture));
        verify_texture(retained_texture,red);
        glDeleteTextures(1,&retained_texture);
    }
    require(eglGetCurrentContext()==EGL_NO_CONTEXT);
    std::cout << "ANGLE isolated contexts, nested restoration and execution-thread checks passed\n";
}
