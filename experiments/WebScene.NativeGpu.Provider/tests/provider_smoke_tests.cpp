#include "webscene_gpu_provider.h"

#include <webgpu/webgpu.h>

#include <array>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <mutex>

// The smoke test intentionally exercises calls inside its checks. Standard
// assert() removes those calls from Release builds, so keep checks active in
// every configuration used by the packaging script.
#ifdef NDEBUG
#undef assert
#define assert(condition)                                                        \
    do {                                                                         \
        if (!(condition)) {                                                      \
            std::fprintf(stderr, "check failed: %s (%s:%d)\n",                  \
                #condition, __FILE__, __LINE__);                                 \
            std::abort();                                                        \
        }                                                                        \
    } while (false)
#endif

namespace {

struct adapter_result final {
    std::mutex mutex;
    std::condition_variable ready;
    WGPUAdapter adapter{nullptr};
    bool completed{false};
};

struct device_result final {
    std::mutex mutex;
    std::condition_variable ready;
    WGPUDevice device{nullptr};
    bool completed{false};
};

template<typename T>
T proc(webscene_gpu_provider* provider, const char* name)
{
    const auto address = webscene_gpu_provider_get_wgpu_proc_address(provider, name);
    assert(address != nullptr);
    return reinterpret_cast<T>(address);
}

WGPUDevice create_metal_device(webscene_gpu_provider* provider)
{
    adapter_result adapter_state;
    WGPURequestAdapterOptions adapter_options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter_options.backendType = WGPUBackendType_Metal;
    adapter_options.featureLevel = WGPUFeatureLevel_Core;
    WGPURequestAdapterCallbackInfo adapter_callback =
        WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    adapter_callback.mode = WGPUCallbackMode_AllowSpontaneous;
    adapter_callback.userdata1 = &adapter_state;
    adapter_callback.callback = [](
        WGPURequestAdapterStatus status,
        WGPUAdapter adapter,
        WGPUStringView,
        void* userdata,
        void*) {
        auto* state = static_cast<adapter_result*>(userdata);
        std::lock_guard lock(state->mutex);
        state->adapter = status == WGPURequestAdapterStatus_Success
            ? adapter
            : nullptr;
        state->completed = true;
        state->ready.notify_one();
    };
    proc<WGPUProcInstanceRequestAdapter>(provider, "wgpuInstanceRequestAdapter")(
        reinterpret_cast<WGPUInstance>(
            webscene_gpu_provider_get_wgpu_instance(provider)),
        &adapter_options,
        adapter_callback);
    {
        std::unique_lock lock(adapter_state.mutex);
        assert(adapter_state.ready.wait_for(
            lock,
            std::chrono::seconds(10),
            [&adapter_state] { return adapter_state.completed; }));
    }
    assert(adapter_state.adapter != nullptr);

    device_result device_state;
    constexpr std::array features{
        WGPUFeatureName_SharedTextureMemoryIOSurface,
        WGPUFeatureName_SharedFenceMTLSharedEvent};
    WGPUDeviceDescriptor device_descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    device_descriptor.requiredFeatureCount = features.size();
    device_descriptor.requiredFeatures = features.data();
    WGPURequestDeviceCallbackInfo device_callback =
        WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    device_callback.mode = WGPUCallbackMode_AllowSpontaneous;
    device_callback.userdata1 = &device_state;
    device_callback.callback = [](
        WGPURequestDeviceStatus status,
        WGPUDevice device,
        WGPUStringView,
        void* userdata,
        void*) {
        auto* state = static_cast<device_result*>(userdata);
        std::lock_guard lock(state->mutex);
        state->device = status == WGPURequestDeviceStatus_Success
            ? device
            : nullptr;
        state->completed = true;
        state->ready.notify_one();
    };
    proc<WGPUProcAdapterRequestDevice>(provider, "wgpuAdapterRequestDevice")(
        adapter_state.adapter,
        &device_descriptor,
        device_callback);
    {
        std::unique_lock lock(device_state.mutex);
        assert(device_state.ready.wait_for(
            lock,
            std::chrono::seconds(10),
            [&device_state] { return device_state.completed; }));
    }
    proc<WGPUProcAdapterRelease>(provider, "wgpuAdapterRelease")(
        adapter_state.adapter);
    assert(device_state.device != nullptr);
    return device_state.device;
}

} // namespace

int main()
{
    assert(webscene_gpu_provider_get_abi_version()
        == WEBSCENE_GPU_PROVIDER_ABI_VERSION);
    webscene_gpu_provider_info info{};
    info.struct_size = sizeof(info);
    assert(webscene_gpu_provider_get_info(&info) != 0U);
    assert(info.abi_version == WEBSCENE_GPU_PROVIDER_ABI_VERSION);
    assert((info.capabilities & WEBSCENE_GPU_CAPABILITY_WEBGPU) != 0U);

    webscene_gpu_provider_options options{};
    options.struct_size = sizeof(options);
    options.required_capabilities = WEBSCENE_GPU_CAPABILITY_WEBGPU;
    auto* provider = webscene_gpu_provider_create(&options);
    assert(provider != nullptr);
    assert(webscene_gpu_provider_get_wgpu_instance(provider) != nullptr);
    assert(webscene_gpu_provider_get_wgpu_proc_address(
        provider, "wgpuInstanceRequestAdapter") != nullptr);

    auto device = create_metal_device(provider);
    webscene_gpu_canvas_configuration configuration{};
    configuration.struct_size = sizeof(configuration);
    configuration.device = reinterpret_cast<uintptr_t>(device);
    configuration.usage = WGPUTextureUsage_RenderAttachment
        | WGPUTextureUsage_CopySrc;
    configuration.pixel_format = WEBSCENE_GPU_PIXEL_FORMAT_BGRA8_UNORM;
    configuration.alpha_mode = WEBSCENE_GPU_ALPHA_MODE_PREMULTIPLIED;
    configuration.buffer_count = 3U;
    auto* canvas = webscene_gpu_provider_create_canvas(
        provider,
        &configuration,
        32U,
        24U);
    assert(canvas != nullptr);

    std::array<webscene_gpu_external_texture, 3> frames{};
    for (auto& frame : frames) {
        uintptr_t texture = 0U;
        assert(webscene_gpu_provider_acquire_canvas_texture(
            provider, canvas, &texture) == WEBSCENE_GPU_STATUS_SUCCESS);
        assert(texture != 0U);
        proc<WGPUProcTextureRelease>(provider, "wgpuTextureRelease")(
            reinterpret_cast<WGPUTexture>(texture));
        frame.struct_size = sizeof(frame);
        assert(webscene_gpu_provider_present_canvas(provider, canvas, &frame)
            == WEBSCENE_GPU_STATUS_SUCCESS);
        assert(frame.handle_kind == WEBSCENE_GPU_HANDLE_IOSURFACE);
        assert(frame.shared_handle != 0U);
        assert(frame.texture_handle != 0U);
        assert((frame.flags & WEBSCENE_GPU_EXTERNAL_TEXTURE_GPU_COMPLETE) != 0U);
    }
    uintptr_t blocked_texture = 0U;
    assert(webscene_gpu_provider_acquire_canvas_texture(
        provider, canvas, &blocked_texture) == WEBSCENE_GPU_STATUS_BUSY);

    assert(webscene_gpu_provider_retain_external_texture(provider, &frames[0])
        == WEBSCENE_GPU_STATUS_SUCCESS);
    webscene_gpu_provider_release_external_texture(provider, &frames[0]);
    assert(webscene_gpu_provider_acquire_canvas_texture(
        provider, canvas, &blocked_texture) == WEBSCENE_GPU_STATUS_BUSY);
    webscene_gpu_provider_release_external_texture(provider, &frames[0]);
    assert(webscene_gpu_provider_acquire_canvas_texture(
        provider, canvas, &blocked_texture) == WEBSCENE_GPU_STATUS_SUCCESS);
    proc<WGPUProcTextureRelease>(provider, "wgpuTextureRelease")(
        reinterpret_cast<WGPUTexture>(blocked_texture));
    assert(webscene_gpu_provider_resize_canvas(provider, canvas, 64U, 48U)
        == WEBSCENE_GPU_STATUS_BUSY);
    webscene_gpu_external_texture resized_frame{};
    resized_frame.struct_size = sizeof(resized_frame);
    assert(webscene_gpu_provider_present_canvas(provider, canvas, &resized_frame)
        == WEBSCENE_GPU_STATUS_SUCCESS);
    webscene_gpu_provider_release_external_texture(provider, &frames[1]);
    webscene_gpu_provider_release_external_texture(provider, &frames[2]);
    webscene_gpu_provider_release_external_texture(provider, &resized_frame);
    assert(webscene_gpu_provider_resize_canvas(provider, canvas, 64U, 48U)
        == WEBSCENE_GPU_STATUS_SUCCESS);

    uintptr_t final_texture = 0U;
    assert(webscene_gpu_provider_acquire_canvas_texture(
        provider, canvas, &final_texture) == WEBSCENE_GPU_STATUS_SUCCESS);
    proc<WGPUProcTextureRelease>(provider, "wgpuTextureRelease")(
        reinterpret_cast<WGPUTexture>(final_texture));
    webscene_gpu_external_texture final_frame{};
    final_frame.struct_size = sizeof(final_frame);
    assert(webscene_gpu_provider_present_canvas(provider, canvas, &final_frame)
        == WEBSCENE_GPU_STATUS_SUCCESS);
    assert(final_frame.width == 64U && final_frame.height == 48U);
    webscene_gpu_provider_destroy_canvas(provider, canvas);
    webscene_gpu_provider_release_external_texture(provider, &final_frame);

    proc<WGPUProcDeviceRelease>(provider, "wgpuDeviceRelease")(device);
    webscene_gpu_provider_destroy(provider);
    return 0;
}
