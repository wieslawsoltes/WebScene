#pragma once

#include "webscene_gpu_provider.h"

#include <cstdint>
#include <memory>
#include <string>

namespace webscene_native {

class gpu_provider_library final {
public:
    static std::unique_ptr<gpu_provider_library> load(
        const std::string& path,
        uint64_t required_capabilities);

    ~gpu_provider_library();
    gpu_provider_library(const gpu_provider_library&) = delete;
    gpu_provider_library& operator=(const gpu_provider_library&) = delete;

    uint64_t capabilities() const noexcept { return info_.capabilities; }
    const webscene_gpu_provider_info& info() const noexcept { return info_; }
    webscene_gpu_provider* provider() const noexcept { return provider_; }
    void* wgpu_instance() const noexcept;
    void* wgpu_proc_address(const char* name) const noexcept;
    webscene_gpu_canvas* create_canvas(
        const webscene_gpu_canvas_configuration& configuration,
        uint32_t width,
        uint32_t height) const noexcept;
    webscene_gpu_status resize_canvas(
        webscene_gpu_canvas* canvas,
        uint32_t width,
        uint32_t height) const noexcept;
    webscene_gpu_status acquire_canvas_texture(
        webscene_gpu_canvas* canvas,
        uintptr_t* texture) const noexcept;
    webscene_gpu_status present_canvas(
        webscene_gpu_canvas* canvas,
        webscene_gpu_external_texture* texture) const noexcept;
    void destroy_canvas(webscene_gpu_canvas* canvas) const noexcept;
    webscene_gpu_status retain_external_texture(
        const webscene_gpu_external_texture& texture) const noexcept;
    void release_external_texture(
        const webscene_gpu_external_texture& texture) const noexcept;

private:
    gpu_provider_library() = default;

    void* library_{nullptr};
    webscene_gpu_provider* provider_{nullptr};
    webscene_gpu_provider_info info_{};
    void (*destroy_)(webscene_gpu_provider*){nullptr};
    void* (*get_wgpu_instance_)(webscene_gpu_provider*){nullptr};
    void* (*get_wgpu_proc_address_)(webscene_gpu_provider*, const char*){nullptr};
    webscene_gpu_canvas* (*create_canvas_)(
        webscene_gpu_provider*,
        const webscene_gpu_canvas_configuration*,
        uint32_t,
        uint32_t){nullptr};
    webscene_gpu_status (*resize_canvas_)(
        webscene_gpu_provider*, webscene_gpu_canvas*, uint32_t, uint32_t){nullptr};
    webscene_gpu_status (*acquire_canvas_texture_)(
        webscene_gpu_provider*, webscene_gpu_canvas*, uintptr_t*){nullptr};
    webscene_gpu_status (*present_canvas_)(
        webscene_gpu_provider*,
        webscene_gpu_canvas*,
        webscene_gpu_external_texture*){nullptr};
    void (*destroy_canvas_)(webscene_gpu_provider*, webscene_gpu_canvas*){nullptr};
    webscene_gpu_status (*retain_external_texture_)(
        webscene_gpu_provider*, const webscene_gpu_external_texture*){nullptr};
    void (*release_external_texture_)(
        webscene_gpu_provider*, const webscene_gpu_external_texture*){nullptr};
};

} // namespace webscene_native
