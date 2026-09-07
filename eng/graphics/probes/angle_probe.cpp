// Diagnostic WebGL-compatible ES context probe, never a presentation implementation.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <GLES2/gl2.h>
#if defined(__linux__)
#include <vulkan/vulkan_core.h>
#endif

#include <array>
#include <cmath>
#include <iostream>
#include <string>
#include <string_view>

namespace {
std::string json(std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { result += '\\'; result += c; }
        else if (c < 32) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
        else result += c;
    }
    return result + '"';
}
std::string text(const char* value) { return value ? value : ""; }
std::string glText(GLenum name) { return text(reinterpret_cast<const char*>(glGetString(name))); }

int finish(std::string_view status, std::string_view reason, int code) {
    std::cout << "{\"schemaVersion\":1,\"probe\":\"angle\",\"status\":" << json(status)
              << ",\"reason\":" << json(reason) << ",\"eglError\":" << eglGetError() << "}\n";
    return code;
}

bool hasExtension(std::string_view extensions, std::string_view name) {
    const auto padded = " " + std::string(extensions) + " ";
    return padded.find(" " + std::string(name) + " ") != std::string::npos;
}

struct Context {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    GLuint texture = 0, framebuffer = 0;
    ~Context() {
        if (framebuffer) glDeleteFramebuffers(1, &framebuffer);
        if (texture) glDeleteTextures(1, &texture);
        if (display != EGL_NO_DISPLAY) {
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
            if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
            eglTerminate(display);
        }
    }
};
} // namespace

int main(int argc, char** argv) {
    if (argc != 3) return finish("failed", "Specify d3d11, metal, vulkan or gl, then ES major 2 or 3", 1);
    const std::string backend = argv[1];
    const std::string version = argv[2];
    if (version != "2" && version != "3") return finish("failed", "Invalid ES version", 1);
    const EGLint major = version == "2" ? 2 : 3;
    EGLint type = 0;
    if (backend == "d3d11") type = EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE;
    else if (backend == "metal") type = EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE;
    else if (backend == "vulkan") type = EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE;
    else if (backend == "gl") type = EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE;
    else return finish("failed", "Invalid backend", 1);
    auto platformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (!platformDisplay) return finish("failed", "ANGLE platform entry point missing", 1);
    const EGLint displayAttributes[] = {
        EGL_PLATFORM_ANGLE_TYPE_ANGLE, type,
        EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_DEVICE_TYPE_HARDWARE_ANGLE,
        EGL_NONE};
    Context state;
    state.display = platformDisplay(EGL_PLATFORM_ANGLE_ANGLE, nullptr, displayAttributes);
    EGLint eglMajor = 0, eglMinor = 0;
    if (state.display == EGL_NO_DISPLAY || !eglInitialize(state.display, &eglMajor, &eglMinor))
        return finish("unavailable", "Requested ANGLE hardware backend could not initialize", 77);
    const auto extensions = text(eglQueryString(state.display, EGL_EXTENSIONS));
    if (!hasExtension(extensions, "EGL_ANGLE_create_context_webgl_compatibility") ||
        !hasExtension(extensions, "EGL_ANGLE_robust_resource_initialization"))
        return finish("failed", "Required WebGL compatibility/robust initialization extensions missing", 1);
    if (!eglBindAPI(EGL_OPENGL_ES_API)) return finish("failed", "Cannot bind OpenGL ES", 1);
    const EGLint configAttributes[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, major == 2 ? EGL_OPENGL_ES2_BIT : EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE};
    EGLConfig config{};
    EGLint count = 0;
    if (!eglChooseConfig(state.display, configAttributes, &config, 1, &count) || count != 1)
        return finish("failed", "No RGBA8 pbuffer configuration for requested ES version", 1);
    constexpr EGLint width = 17, height = 4;
    const EGLint surfaceAttributes[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
    state.surface = eglCreatePbufferSurface(state.display, config, surfaceAttributes);
    const EGLint contextAttributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, major,
        EGL_CONTEXT_WEBGL_COMPATIBILITY_ANGLE, EGL_TRUE,
        EGL_ROBUST_RESOURCE_INITIALIZATION_ANGLE, EGL_TRUE, EGL_NONE};
    state.context = eglCreateContext(state.display, config, EGL_NO_CONTEXT, contextAttributes);
    if (state.surface == EGL_NO_SURFACE || state.context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(state.display, state.surface, state.surface, state.context))
        return finish("failed", "Cannot create/make current WebGL-compatible ES context", 1);

    const auto renderer = glText(GL_RENDERER);
    const auto vendor = glText(GL_VENDOR);
    const auto driver = glText(GL_VERSION);
    bool hardware = backend == "metal" || backend == "d3d11";
    std::string hardwareEvidence = hardware ? "Explicit ANGLE hardware device on native Metal/D3D11 backend" : "";
#if defined(__linux__)
    if (backend == "vulkan") {
        auto queryDisplay = reinterpret_cast<PFNEGLQUERYDISPLAYATTRIBEXTPROC>(eglGetProcAddress("eglQueryDisplayAttribEXT"));
        auto queryDevice = reinterpret_cast<PFNEGLQUERYDEVICEATTRIBEXTPROC>(eglGetProcAddress("eglQueryDeviceAttribEXT"));
        EGLAttrib device = 0, physical = 0, instance = 0, getProc = 0;
        if (queryDisplay && queryDevice && queryDisplay(state.display, EGL_DEVICE_EXT, &device) &&
            queryDevice(reinterpret_cast<EGLDeviceEXT>(device), EGL_VULKAN_PHYSICAL_DEVICE_ANGLE, &physical) &&
            queryDevice(reinterpret_cast<EGLDeviceEXT>(device), EGL_VULKAN_INSTANCE_ANGLE, &instance) &&
            queryDevice(reinterpret_cast<EGLDeviceEXT>(device), EGL_VULKAN_GET_INSTANCE_PROC_ADDR, &getProc)) {
            auto getInstanceProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(getProc);
            auto properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
                getInstanceProc(reinterpret_cast<VkInstance>(instance), "vkGetPhysicalDeviceProperties"));
            if (properties) {
                VkPhysicalDeviceProperties info{};
                properties(reinterpret_cast<VkPhysicalDevice>(physical), &info);
                hardware = info.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
                           info.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
                hardwareEvidence = std::string(info.deviceName) + "; Vulkan deviceType=" +
                    std::to_string(info.deviceType) + "; driverVersion=" + std::to_string(info.driverVersion);
            }
        }
    }
#endif
    glGenTextures(1, &state.texture);
    glBindTexture(GL_TEXTURE_2D, state.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &state.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, state.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, state.texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return finish("failed", "Texture framebuffer incomplete", 1);
    glDisable(GL_DITHER);
    glClearColor(0.2f, 0.4f, 0.6f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    std::array<GLubyte, width * height * 4> pixels{};
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    if (glGetError() != GL_NO_ERROR) return finish("failed", "Resource, clear or readback generated GL error", 1);
    constexpr std::array<int, 4> expected{51, 102, 153, 255};
    for (size_t i = 0; i < pixels.size(); ++i)
        if (std::abs(int(pixels[i]) - expected[i % 4]) > 1)
            return finish("failed", "Readback differs from expected clear color", 1);
    // GL needs independent native adapter classification before it can qualify hardware.
    std::cout << "{\"schemaVersion\":1,\"probe\":\"angle\",\"status\":" << json(hardware ? "passed" : "unavailable")
              << ",\"hardwareAccelerated\":" << (hardware ? "true" : "false")
              << ",\"backend\":" << json(backend) << ",\"esMajor\":" << major
              << ",\"adapter\":" << json(renderer) << ",\"vendor\":" << json(vendor)
              << ",\"driver\":" << json(driver) << ",\"hardwareEvidence\":" << json(hardwareEvidence)
              << ",\"verifiedPixels\":" << width * height
              << ",\"webglCompatibleContext\":true,\"robustResourceInitialization\":true"
              << ",\"diagnosticReadback\":true,\"expectedRGBA\":[51,102,153,255],\"tolerance\":1}\n";
    return hardware ? 0 : 77;
}
