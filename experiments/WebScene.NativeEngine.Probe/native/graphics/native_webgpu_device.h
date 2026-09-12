#pragma once
#include <webgpu/webgpu_cpp.h>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace webscene::graphics {
// Explicit startup helper for native applications. Blocking discovery is limited
// to initialization; rendering uses the device queue and event service normally.
struct native_webgpu_device {
    std::shared_ptr<std::atomic<bool>> failed = std::make_shared<std::atomic<bool>>(false);
    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;
    static native_webgpu_device create(wgpu::BackendType backend,
                                       std::vector<wgpu::FeatureName> features = {},
                                       bool force_fallback_adapter = false) {
        native_webgpu_device result;
        constexpr auto timed_wait = wgpu::InstanceFeatureName::TimedWaitAny;
        wgpu::InstanceDescriptor descriptor{};
        descriptor.requiredFeatureCount = 1;
        descriptor.requiredFeatures = &timed_wait;
        result.instance = wgpu::CreateInstance(&descriptor);
        if (!result.instance) throw std::runtime_error("WebGPU instance creation failed");
        struct discovery { wgpu::Adapter adapter; wgpu::Device device; std::string error; };
        auto state = std::make_shared<discovery>();
        auto message = [](wgpu::StringView text) {
            return !text.data ? std::string{} : text.length == WGPU_STRLEN
                ? std::string(text.data) : std::string(text.data, text.length);
        };
        wgpu::RequestAdapterOptions options{};
        options.backendType = backend;
        options.forceFallbackAdapter = force_fallback_adapter;
        auto future = result.instance.RequestAdapter(&options, wgpu::CallbackMode::WaitAnyOnly,
            [state, message](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView error) {
                if (status == wgpu::RequestAdapterStatus::Success) state->adapter = std::move(adapter);
                state->error = message(error);
            });
        if (result.instance.WaitAny(future, 30'000'000'000ULL) != wgpu::WaitStatus::Success)
            throw std::runtime_error("WebGPU adapter discovery timed out");
        if (!state->adapter) throw std::runtime_error("WebGPU adapter discovery failed: " + state->error);
        result.adapter = state->adapter;
        wgpu::DeviceDescriptor device_descriptor{};
        device_descriptor.requiredFeatureCount = features.size();
        device_descriptor.requiredFeatures = features.data();
        device_descriptor.SetUncapturedErrorCallback(
            [](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView error, std::atomic<bool>* failed) { failed->store(true); if (error.data) std::fprintf(stderr, "WebGPU: %.*s\n", int(std::min<size_t>(4096, error.length == WGPU_STRLEN ? std::char_traits<char>::length(error.data) : error.length)), error.data); }, result.failed.get());
        device_descriptor.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous,
            [failed=result.failed](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView) {
                if (reason != wgpu::DeviceLostReason::Destroyed && reason != wgpu::DeviceLostReason::CallbackCancelled)
                    failed->store(true);
            });
        future = result.adapter.RequestDevice(&device_descriptor, wgpu::CallbackMode::WaitAnyOnly,
            [state, message](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView error) {
                if (status == wgpu::RequestDeviceStatus::Success) state->device = std::move(device);
                state->error = message(error);
            });
        if (result.instance.WaitAny(future, 30'000'000'000ULL) != wgpu::WaitStatus::Success)
            throw std::runtime_error("WebGPU device creation timed out");
        if (!state->device) throw std::runtime_error("WebGPU device creation failed: " + state->error);
        result.device = state->device;
        return result;
    }
};
} // namespace webscene::graphics
