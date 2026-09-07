#define GL_SILENCE_DEPRECATION
#include <array>
#include <cstdlib>
// Test-only provider factory. Never linked into or installed with the runtime.
#include "graphics/image_lease_abi.h"
#include "graphics/iosurface_canvas_images.h"
#include "graphics_fixture_dawn_clear.h"
namespace {
std::weak_ptr<webscene::graphics::image_provider_lifetime> observed;
}
static uint8_t create_iosurface(webscene_gpu_image_lease_v3** result,bool paint) {
    if (!result) return 0;
    *result=nullptr;
    try {
        using namespace webscene::graphics;
        iosurface_canvas_images pool(1024*1024);
        auto frame=pool.acquire({700,0,1,1,701,1,17,4,image_format::bgra8_unorm});
        if (!frame || (paint && !fixture_dawn_clear(frame->color->borrowed_handle()))) return 0;
        frame->producer.begin();
        auto image=frame->producer.publish();
        frame->producer.complete(); frame.reset(); // Any diagnostic producer has already completed.
        if (!image) return 0;
        auto observer=image->begin_consumer();
        if (!observer) return 0;
        observed=observer->provider();
        observer->complete();
        auto lease=std::make_unique<webscene_gpu_image_lease_v3>(std::move(*image));
        *result=lease.release();
        return 1;
    } catch (...) { return 0; }
}
extern "C" __attribute__((visibility("default"))) uint8_t webscene_test_create_iosurface(webscene_gpu_image_lease_v3** result) {
    return create_iosurface(result,false);
}
extern "C" __attribute__((visibility("default"))) uint8_t webscene_test_create_dawn_iosurface(webscene_gpu_image_lease_v3** result) {
    return create_iosurface(result,true);
}
extern "C" __attribute__((visibility("default"))) uint8_t webscene_test_iosurface_alive() {
    return observed.expired() ? 0 : 1;
}

// CGL context is confined to the test's calling thread. The fixture links only
// Apple's OpenGL for GL symbols, avoiding the runtime's separate ANGLE entrypoints.
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
namespace {
thread_local CGLContextObj fixture_context=nullptr, previous_context=nullptr;
thread_local GLuint fixture_texture=0;
}
extern "C" __attribute__((visibility("default"))) void webscene_test_end_cgl() {
    if (!fixture_context) return;
    CGLSetCurrentContext(fixture_context);
    glFinish(); // Diagnostic failure cleanup only.
    if (fixture_texture) glDeleteTextures(1,&fixture_texture);
    CGLSetCurrentContext(previous_context);
    CGLReleaseContext(fixture_context);
    fixture_context=nullptr; previous_context=nullptr; fixture_texture=0;
}
extern "C" __attribute__((visibility("default"))) uint8_t webscene_test_begin_cgl() {
    if (fixture_context) return 0;
    const CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFAOpenGLProfile,
        static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_3_2_Core),
        static_cast<CGLPixelFormatAttribute>(0)};
    CGLPixelFormatObj format=nullptr; GLint count=0;
    if (CGLChoosePixelFormat(attributes,&format,&count)!=kCGLNoError || !format) return 0;
    previous_context=CGLGetCurrentContext();
    const auto status=CGLCreateContext(format,nullptr,&fixture_context);
    CGLReleasePixelFormat(format);
    if (status!=kCGLNoError || !fixture_context) return 0;
    if (CGLSetCurrentContext(fixture_context)!=kCGLNoError) { webscene_test_end_cgl(); return 0; }
    glGenTextures(1,&fixture_texture); glBindTexture(GL_TEXTURE_RECTANGLE,fixture_texture);
    if (!fixture_texture || glGetError()!=GL_NO_ERROR) { webscene_test_end_cgl(); return 0; }
    return 1;
}
extern "C" __attribute__((visibility("default"))) uint8_t webscene_test_cgl_image_bound() {
    if (!fixture_context || CGLGetCurrentContext()!=fixture_context) return 0;
    GLint width=0,height=0;
    glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE,0,GL_TEXTURE_WIDTH,&width);
    glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE,0,GL_TEXTURE_HEIGHT,&height);
    return width==17 && height==4 && glGetError()==GL_NO_ERROR;
}


// Diagnostic readback verifies Dawn output after the production-source CGL import.
extern "C" __attribute__((visibility("default"))) uint8_t webscene_test_cgl_pixels() {
    if (!fixture_context || CGLGetCurrentContext()!=fixture_context) return 0;
    GLuint framebuffer=0; glGenFramebuffers(1,&framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_RECTANGLE,fixture_texture,0);
    bool valid=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    std::array<unsigned char,17*4*4> pixels{};
    if (valid) glReadPixels(0,0,17,4,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    const int expected[]={51,102,153,255};
    for (size_t i=0;i<pixels.size();++i) valid &= std::abs(int(pixels[i])-expected[i%4])<=1;
    valid &= glGetError()==GL_NO_ERROR;
    glBindFramebuffer(GL_FRAMEBUFFER,0); glDeleteFramebuffers(1,&framebuffer);
    return valid ? 1 : 0;
}
