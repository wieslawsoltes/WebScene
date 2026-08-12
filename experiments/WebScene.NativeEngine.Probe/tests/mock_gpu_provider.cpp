#define WEBSCENE_GPU_PROVIDER_BUILD 1
#include "webscene_gpu_provider.h"

#include <cstring>
#include <new>

struct webscene_gpu_provider final {
};

struct webscene_gpu_canvas final {
};

extern "C" {

uint32_t webscene_gpu_provider_get_abi_version(void)
{
    return WEBSCENE_GPU_PROVIDER_ABI_VERSION;
}

uint8_t webscene_gpu_provider_get_info(webscene_gpu_provider_info* info)
{
    if (info == nullptr || info->struct_size < sizeof(webscene_gpu_provider_info)) {
        return 0U;
    }
    *info = webscene_gpu_provider_info{};
    info->struct_size = sizeof(webscene_gpu_provider_info);
    info->abi_version = WEBSCENE_GPU_PROVIDER_ABI_VERSION;
    info->capabilities = WEBSCENE_GPU_CAPABILITY_WEBGPU;
    std::strncpy(info->name, "WebScene test GPU provider", sizeof(info->name) - 1U);
    std::strncpy(info->adapter, "zero-copy contract fixture", sizeof(info->adapter) - 1U);
    return 1U;
}

webscene_gpu_provider* webscene_gpu_provider_create(
    const webscene_gpu_provider_options* options)
{
    if (options == nullptr
        || options->struct_size < sizeof(webscene_gpu_provider_options)
        || (options->required_capabilities & ~WEBSCENE_GPU_CAPABILITY_WEBGPU) != 0U) {
        return nullptr;
    }
    return new (std::nothrow) webscene_gpu_provider();
}

void webscene_gpu_provider_destroy(webscene_gpu_provider* provider)
{
    delete provider;
}

void* webscene_gpu_provider_get_wgpu_instance(webscene_gpu_provider*)
{
    return reinterpret_cast<void*>(1U);
}

void* webscene_gpu_provider_get_wgpu_proc_address(
    webscene_gpu_provider*,
    const char*)
{
    return nullptr;
}

webscene_gpu_canvas* webscene_gpu_provider_create_canvas(
    webscene_gpu_provider*, const webscene_gpu_canvas_configuration*, uint32_t, uint32_t)
{
    return nullptr;
}

webscene_gpu_status webscene_gpu_provider_resize_canvas(
    webscene_gpu_provider*, webscene_gpu_canvas*, uint32_t, uint32_t)
{
    return WEBSCENE_GPU_STATUS_UNAVAILABLE;
}

webscene_gpu_status webscene_gpu_provider_acquire_canvas_texture(
    webscene_gpu_provider*, webscene_gpu_canvas*, uintptr_t*)
{
    return WEBSCENE_GPU_STATUS_UNAVAILABLE;
}

webscene_gpu_status webscene_gpu_provider_present_canvas(
    webscene_gpu_provider*, webscene_gpu_canvas*, webscene_gpu_external_texture*)
{
    return WEBSCENE_GPU_STATUS_UNAVAILABLE;
}

void webscene_gpu_provider_destroy_canvas(
    webscene_gpu_provider*, webscene_gpu_canvas*)
{
}

webscene_gpu_status webscene_gpu_provider_retain_external_texture(
    webscene_gpu_provider*, const webscene_gpu_external_texture*)
{
    return WEBSCENE_GPU_STATUS_UNAVAILABLE;
}

void webscene_gpu_provider_release_external_texture(
    webscene_gpu_provider*, const webscene_gpu_external_texture*)
{
}

} // extern "C"
