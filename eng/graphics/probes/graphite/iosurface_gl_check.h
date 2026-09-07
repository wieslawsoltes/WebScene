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
    const uint8_t* expected,unsigned expected_stride,GLuint hostTexture=0) {
    auto previous=CGLGetCurrentContext();
    CGLContextObj context=previous;
    if (!hostTexture) {
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFAOpenGLProfile,
        static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_3_2_Core),static_cast<CGLPixelFormatAttribute>(0)};
    CGLPixelFormatObj format=nullptr; GLint count=0;
    if (CGLChoosePixelFormat(attributes,&format,&count)!=kCGLNoError || !format) return false;
    context=nullptr;
    const auto created=CGLCreateContext(format,nullptr,&context); CGLDestroyPixelFormat(format);
    if (created!=kCGLNoError || !context) return false;
    }
    if (!context) return false;
    if (CGLSetCurrentContext(context)!=kCGLNoError) { if (!hostTexture) CGLDestroyContext(context); return false; }
    GLuint texture=0,framebuffer=0,destination=0,destinationFramebuffer=0;
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
            // Avalonia's composition texture is GL_TEXTURE_2D. This explicit
            // GPU-local copy avoids uploading pixels to adapt the texture target.
            destination=hostTexture;
            if (!destination) {
                glGenTextures(1,&destination); glBindTexture(GL_TEXTURE_2D,destination);
                glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
            }
            glGenFramebuffers(1,&destinationFramebuffer);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,destinationFramebuffer);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,destination,0);
            valid=glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
            if (valid) {
                glBindFramebuffer(GL_READ_FRAMEBUFFER,framebuffer);
                glBlitFramebuffer(0,0,width,height,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
                valid=glGetError()==GL_NO_ERROR;
            }
            glBindFramebuffer(GL_READ_FRAMEBUFFER,destinationFramebuffer);
            std::vector<uint8_t> pixels(width*height*4);
            glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            auto error=glGetError(); valid &= error==GL_NO_ERROR;
            if (!valid) std::fprintf(stderr,"CGL read error %u\n",error);
            for (unsigned y=0;y<height;++y) for (unsigned x=0;x<width*4;++x)
                valid &= std::abs(int(pixels[y*width*4+x])-int(expected[y*expected_stride+(x/4)*4+(x%4<3 ? 2-x%4 : x%4)]))<=1;
        }
    }
    glDeleteFramebuffers(1,&destinationFramebuffer); if (!hostTexture) glDeleteTextures(1,&destination);
    glDeleteFramebuffers(1,&framebuffer); glDeleteTextures(1,&texture);
    CGLSetCurrentContext(previous); if (!hostTexture) CGLDestroyContext(context);
    return valid;
}
#endif
