#include "webscene_gpu_provider_loader.h"

#include <cstring>
#include <stdexcept>
#include <utility>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace webscene_native {
namespace {

void* open_library(const char* path)
{
#if defined(_WIN32)
    return reinterpret_cast<void*>(LoadLibraryA(path));
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

void close_library(void* library)
{
    if (library == nullptr) return;
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(library));
#else
    dlclose(library);
#endif
}

void* find_symbol(void* library, const char* name)
{
#if defined(_WIN32)
    return reinterpret_cast<void*>(
        GetProcAddress(reinterpret_cast<HMODULE>(library), name));
#else
    return dlsym(library, name);
#endif
}

template<typename T>
T require_symbol(void* library, const char* name)
{
    auto* address = find_symbol(library, name);
    if (address == nullptr) {
        throw std::runtime_error(
            std::string("GPU provider export is missing: ") + name);
    }
    return reinterpret_cast<T>(address);
}

} // namespace

std::unique_ptr<gpu_provider_library> gpu_provider_library::load(
    const std::string& path,
    uint64_t required_capabilities)
{
    auto result = std::unique_ptr<gpu_provider_library>(
        new gpu_provider_library());
    result->library_ = open_library(path.c_str());
    if (result->library_ == nullptr) {
        throw std::runtime_error("The native GPU provider could not be loaded: " + path);
    }
    try {
        const auto get_abi_version = require_symbol<uint32_t (*)()>(
            result->library_,
            "webscene_gpu_provider_get_abi_version");
        if (get_abi_version() != WEBSCENE_GPU_PROVIDER_ABI_VERSION) {
            throw std::runtime_error("The native GPU provider ABI is incompatible");
        }
        const auto get_info = require_symbol<uint8_t (*)(webscene_gpu_provider_info*)>(
            result->library_,
            "webscene_gpu_provider_get_info");
        result->info_.struct_size = sizeof(result->info_);
        if (get_info(&result->info_) == 0U
            || result->info_.abi_version != WEBSCENE_GPU_PROVIDER_ABI_VERSION) {
            throw std::runtime_error("The native GPU provider returned invalid metadata");
        }
        if ((result->info_.capabilities & required_capabilities)
            != required_capabilities) {
            throw std::runtime_error(
                "The native GPU provider does not satisfy the required capabilities");
        }
        const auto create = require_symbol<webscene_gpu_provider* (*)(
            const webscene_gpu_provider_options*)>(
                result->library_,
                "webscene_gpu_provider_create");
        result->destroy_ = require_symbol<void (*)(webscene_gpu_provider*)>(
            result->library_,
            "webscene_gpu_provider_destroy");
        result->get_wgpu_instance_ = require_symbol<void* (*)(webscene_gpu_provider*)>(
            result->library_,
            "webscene_gpu_provider_get_wgpu_instance");
        result->get_wgpu_proc_address_ = require_symbol<void* (*)(
            webscene_gpu_provider*, const char*)>(
                result->library_,
                "webscene_gpu_provider_get_wgpu_proc_address");
        result->create_canvas_ = require_symbol<webscene_gpu_canvas* (*)(
            webscene_gpu_provider*,
            const webscene_gpu_canvas_configuration*,
            uint32_t,
            uint32_t)>(result->library_, "webscene_gpu_provider_create_canvas");
        result->resize_canvas_ = require_symbol<webscene_gpu_status (*)(
            webscene_gpu_provider*, webscene_gpu_canvas*, uint32_t, uint32_t)>(
                result->library_, "webscene_gpu_provider_resize_canvas");
        result->acquire_canvas_texture_ = require_symbol<webscene_gpu_status (*)(
            webscene_gpu_provider*, webscene_gpu_canvas*, uintptr_t*)>(
                result->library_, "webscene_gpu_provider_acquire_canvas_texture");
        result->present_canvas_ = require_symbol<webscene_gpu_status (*)(
            webscene_gpu_provider*,
            webscene_gpu_canvas*,
            webscene_gpu_external_texture*)>(
                result->library_, "webscene_gpu_provider_present_canvas");
        result->destroy_canvas_ = require_symbol<void (*)(
            webscene_gpu_provider*, webscene_gpu_canvas*)>(
                result->library_, "webscene_gpu_provider_destroy_canvas");
        result->retain_external_texture_ = require_symbol<webscene_gpu_status (*)(
            webscene_gpu_provider*, const webscene_gpu_external_texture*)>(
                result->library_, "webscene_gpu_provider_retain_external_texture");
        result->release_external_texture_ = require_symbol<void (*)(
            webscene_gpu_provider*, const webscene_gpu_external_texture*)>(
                result->library_, "webscene_gpu_provider_release_external_texture");
        const webscene_gpu_provider_options options{
            sizeof(webscene_gpu_provider_options),
            0U,
            required_capabilities};
        result->provider_ = create(&options);
        if (result->provider_ == nullptr) {
            throw std::runtime_error("The native GPU provider could not create a device service");
        }
        return result;
    } catch (...) {
        if (result->provider_ != nullptr && result->destroy_ != nullptr) {
            result->destroy_(result->provider_);
            result->provider_ = nullptr;
        }
        close_library(result->library_);
        result->library_ = nullptr;
        throw;
    }
}

gpu_provider_library::~gpu_provider_library()
{
    if (provider_ != nullptr && destroy_ != nullptr) destroy_(provider_);
    close_library(library_);
}

void* gpu_provider_library::wgpu_instance() const noexcept
{
    return provider_ != nullptr && get_wgpu_instance_ != nullptr
        ? get_wgpu_instance_(provider_)
        : nullptr;
}

void* gpu_provider_library::wgpu_proc_address(const char* name) const noexcept
{
    return provider_ != nullptr && get_wgpu_proc_address_ != nullptr && name != nullptr
        ? get_wgpu_proc_address_(provider_, name)
        : nullptr;
}

webscene_gpu_canvas* gpu_provider_library::create_canvas(
    const webscene_gpu_canvas_configuration& configuration,
    uint32_t width,
    uint32_t height) const noexcept
{
    return provider_ != nullptr && create_canvas_ != nullptr
        ? create_canvas_(provider_, &configuration, width, height)
        : nullptr;
}

webscene_gpu_status gpu_provider_library::resize_canvas(
    webscene_gpu_canvas* canvas, uint32_t width, uint32_t height) const noexcept
{
    return provider_ != nullptr && resize_canvas_ != nullptr
        ? resize_canvas_(provider_, canvas, width, height)
        : WEBSCENE_GPU_STATUS_UNAVAILABLE;
}

webscene_gpu_status gpu_provider_library::acquire_canvas_texture(
    webscene_gpu_canvas* canvas, uintptr_t* texture) const noexcept
{
    return provider_ != nullptr && acquire_canvas_texture_ != nullptr
        ? acquire_canvas_texture_(provider_, canvas, texture)
        : WEBSCENE_GPU_STATUS_UNAVAILABLE;
}

webscene_gpu_status gpu_provider_library::present_canvas(
    webscene_gpu_canvas* canvas,
    webscene_gpu_external_texture* texture) const noexcept
{
    return provider_ != nullptr && present_canvas_ != nullptr
        ? present_canvas_(provider_, canvas, texture)
        : WEBSCENE_GPU_STATUS_UNAVAILABLE;
}

void gpu_provider_library::destroy_canvas(webscene_gpu_canvas* canvas) const noexcept
{
    if (provider_ != nullptr && destroy_canvas_ != nullptr && canvas != nullptr) {
        destroy_canvas_(provider_, canvas);
    }
}

webscene_gpu_status gpu_provider_library::retain_external_texture(
    const webscene_gpu_external_texture& texture) const noexcept
{
    return provider_ != nullptr && retain_external_texture_ != nullptr
        ? retain_external_texture_(provider_, &texture)
        : WEBSCENE_GPU_STATUS_UNAVAILABLE;
}

void gpu_provider_library::release_external_texture(
    const webscene_gpu_external_texture& texture) const noexcept
{
    if (provider_ != nullptr && release_external_texture_ != nullptr) {
        release_external_texture_(provider_, &texture);
    }
}

} // namespace webscene_native
