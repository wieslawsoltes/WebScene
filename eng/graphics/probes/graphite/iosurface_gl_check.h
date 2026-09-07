#pragma once
#if defined(__APPLE__)
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <OpenGL/CGLIOSurface.h>
#include <IOSurface/IOSurface.h>
#include <vector>
#include <cstdio>
// Diagnostic-only CPU readback. Caller must establish producer GPU completion.
inline bool check_iosurface_gl(IOSurfaceRef surface,unsigned width,unsigned height,
    const uint8_t* expected,unsigned expected_stride) {
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFAOpenGLProfile,
        static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_3_2_Core),static_cast<CGLPixelFormatAttribute>(0)};
    CGLPixelFormatObj format=nullptr; GLint count=0;
    if (CGLChoosePixelFormat(attributes,&format,&count)!=kCGLNoError || !format) return false;
    CGLContextObj context=nullptr;
    const auto created=CGLCreateContext(format,nullptr,&context); CGLDestroyPixelFormat(format);
    if (created!=kCGLNoError || !context) return false;
    auto previous=CGLGetCurrentContext();
    if (CGLSetCurrentContext(context)!=kCGLNoError) { CGLDestroyContext(context); return false; }
    GLuint texture=0,framebuffer=0;
    glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_RECTANGLE,texture);
    const auto imported=CGLTexImageIOSurface2D(context,GL_TEXTURE_RECTANGLE,GL_RGBA8,
        width,height,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,surface,0);
    bool valid=imported==kCGLNoError;
    if (!valid) std::fprintf(stderr,"IOSurface CGL import error %d\n",int(imported));
    if (valid) {
        glGenFramebuffers(1,&framebuffer); glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_RECTANGLE,texture,0);
        auto fb=glCheckFramebufferStatus(GL_FRAMEBUFFER);
        valid=fb==GL_FRAMEBUFFER_COMPLETE;
        if (!valid) std::fprintf(stderr,"CGL framebuffer status %u\n",fb);
        if (valid) {
            std::vector<uint8_t> pixels(width*height*4);
            glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            auto error=glGetError(); valid=error==GL_NO_ERROR;
            if (!valid) std::fprintf(stderr,"CGL read error %u\n",error);
            for (unsigned y=0;y<height;++y) for (unsigned x=0;x<width*4;++x)
                valid &= std::abs(int(pixels[y*width*4+x])-int(expected[y*expected_stride+(x/4)*4+(x%4<3 ? 2-x%4 : x%4)]))<=1;
        }
    }
    glDeleteFramebuffers(1,&framebuffer); glDeleteTextures(1,&texture);
    CGLSetCurrentContext(previous); CGLDestroyContext(context);
    return valid;
}
#endif
