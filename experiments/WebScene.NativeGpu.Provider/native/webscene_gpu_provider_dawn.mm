#define WEBSCENE_GPU_PROVIDER_BUILD 1
#include "webscene_gpu_provider.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreVideo/CoreVideo.h>
#include <IOSurface/IOSurface.h>
#include <dawn/native/DawnNative.h>
#include <dawn/native/MetalBackend.h>
#import <Metal/Metal.h>
#include <webgpu/webgpu_cpp.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t probe_texture_size = 4U;

struct cf_release final {
    void operator()(CFTypeRef value) const noexcept
    {
        if (value != nullptr) CFRelease(value);
    }
};

template<typename T>
using cf_unique = std::unique_ptr<std::remove_pointer_t<T>, cf_release>;

void add_int32(CFMutableDictionaryRef dictionary, CFStringRef key, int32_t value)
{
    cf_unique<CFNumberRef> number(
        CFNumberCreate(nullptr, kCFNumberSInt32Type, &value));
    if (number != nullptr) CFDictionarySetValue(dictionary, key, number.get());
}

cf_unique<IOSurfaceRef> create_io_surface(uint32_t width, uint32_t height)
{
    cf_unique<CFMutableDictionaryRef> properties(
        CFDictionaryCreateMutable(
            kCFAllocatorDefault,
            0,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks));
    if (properties == nullptr) return {};
    add_int32(properties.get(), kIOSurfaceWidth, static_cast<int32_t>(width));
    add_int32(properties.get(), kIOSurfaceHeight, static_cast<int32_t>(height));
    add_int32(properties.get(), kIOSurfacePixelFormat,
        static_cast<int32_t>(kCVPixelFormatType_32BGRA));
    add_int32(properties.get(), kIOSurfaceBytesPerElement, 4);
    return cf_unique<IOSurfaceRef>(IOSurfaceCreate(properties.get()));
}

struct probe_result final {
    bool available{false};
    std::string adapter_name;
};

probe_result probe_metal_zero_copy()
{
    probe_result result;
    dawn::native::Instance instance;
    wgpu::RequestAdapterOptions options{};
    options.backendType = wgpu::BackendType::Metal;
    options.featureLevel = wgpu::FeatureLevel::Core;
    auto adapters = instance.EnumerateAdapters(&options);
    if (adapters.empty()) return result;

    wgpu::Adapter adapter(adapters.front().Get());
    if (!adapter.HasFeature(wgpu::FeatureName::SharedTextureMemoryIOSurface)
        || !adapter.HasFeature(wgpu::FeatureName::SharedFenceMTLSharedEvent)) {
        return result;
    }

    const std::array required_features{
        wgpu::FeatureName::SharedTextureMemoryIOSurface,
        wgpu::FeatureName::SharedFenceMTLSharedEvent};
    wgpu::DeviceDescriptor device_descriptor{};
    device_descriptor.requiredFeatureCount = required_features.size();
    device_descriptor.requiredFeatures = required_features.data();
    wgpu::Device device(adapters.front().CreateDevice(&device_descriptor));
    if (device == nullptr) return result;

    auto surface = create_io_surface(probe_texture_size, probe_texture_size);
    if (surface == nullptr) return result;
    wgpu::SharedTextureMemoryIOSurfaceDescriptor io_surface_descriptor{};
    io_surface_descriptor.ioSurface = surface.get();
    io_surface_descriptor.allowStorageBinding = true;
    wgpu::SharedTextureMemoryDescriptor memory_descriptor{};
    memory_descriptor.nextInChain = &io_surface_descriptor;
    auto memory = device.ImportSharedTextureMemory(&memory_descriptor);
    if (memory == nullptr) return result;

    wgpu::SharedTextureMemoryProperties properties{};
    memory.GetProperties(&properties);
    if (properties.size.width != probe_texture_size
        || properties.size.height != probe_texture_size
        || properties.format != wgpu::TextureFormat::BGRA8Unorm
        || (properties.usage & wgpu::TextureUsage::RenderAttachment)
            != wgpu::TextureUsage::RenderAttachment) {
        return result;
    }

    wgpu::TextureDescriptor texture_descriptor{};
    texture_descriptor.size = properties.size;
    texture_descriptor.format = properties.format;
    texture_descriptor.usage = wgpu::TextureUsage::RenderAttachment;
    auto texture = memory.CreateTexture(&texture_descriptor);
    if (texture == nullptr) return result;

    wgpu::AdapterInfo adapter_info{};
    if (adapter.GetInfo(&adapter_info) == wgpu::Status::Success
        && adapter_info.device.data != nullptr) {
        result.adapter_name.assign(
            adapter_info.device.data,
            adapter_info.device.length == WGPU_STRLEN
                ? std::strlen(adapter_info.device.data)
                : adapter_info.device.length);
    }
    result.available = true;
    return result;
}

const probe_result& cached_probe()
{
    static const probe_result value = probe_metal_zero_copy();
    return value;
}

dawn::native::Instance create_runtime_instance()
{
    static constexpr auto required_feature =
        wgpu::InstanceFeatureName::TimedWaitAny;
    wgpu::InstanceDescriptor descriptor{};
    descriptor.requiredFeatureCount = 1U;
    descriptor.requiredFeatures = &required_feature;
    return dawn::native::Instance(&descriptor);
}

void copy_string(char* destination, size_t capacity, const std::string& value)
{
    if (capacity == 0U) return;
    const auto length = std::min(capacity - 1U, value.size());
    std::memcpy(destination, value.data(), length);
    destination[length] = '\0';
}

} // namespace

struct webscene_gpu_canvas_slot final {
    cf_unique<IOSurfaceRef> surface;
    wgpu::SharedTextureMemory memory;
    wgpu::Texture texture;
    id<MTLTexture> metal_texture{nil};
    uint64_t generation{0U};
    bool dawn_access{false};
    bool initialized{false};

    ~webscene_gpu_canvas_slot()
    {
        [metal_texture release];
    }
};

struct webscene_gpu_provider final {
    dawn::native::Instance instance{create_runtime_instance()};
    std::mutex mutex;
    uint64_t next_generation{1U};

    struct retained_export final {
        std::shared_ptr<webscene_gpu_canvas_slot> slot;
        uint32_t retain_count{0U};
    };
    std::unordered_map<uint64_t, retained_export> exports;
};

struct webscene_gpu_canvas final {
    webscene_gpu_provider* owner{nullptr};
    wgpu::Device device;
    uint32_t width{0U};
    uint32_t height{0U};
    uint32_t pixel_format{0U};
    uint32_t alpha_mode{0U};
    uint64_t usage{0U};
    size_t next_slot{0U};
    std::shared_ptr<webscene_gpu_canvas_slot> acquired;
    std::vector<std::shared_ptr<webscene_gpu_canvas_slot>> slots;
    std::vector<std::shared_ptr<webscene_gpu_canvas_slot>> retired_slots;
};

namespace {

bool slot_is_exported(
    const webscene_gpu_provider& provider,
    const std::shared_ptr<webscene_gpu_canvas_slot>& slot)
{
    return slot->generation != 0U
        && provider.exports.contains(slot->generation);
}

std::shared_ptr<webscene_gpu_canvas_slot> create_canvas_slot(
    const wgpu::Device& device,
    uint32_t width,
    uint32_t height,
    uint64_t requested_usage)
{
    auto slot = std::make_shared<webscene_gpu_canvas_slot>();
    slot->surface = create_io_surface(width, height);
    if (slot->surface == nullptr) return {};

    wgpu::SharedTextureMemoryIOSurfaceDescriptor io_surface_descriptor{};
    io_surface_descriptor.ioSurface = slot->surface.get();
    io_surface_descriptor.allowStorageBinding = true;
    wgpu::SharedTextureMemoryDescriptor memory_descriptor{};
    memory_descriptor.nextInChain = &io_surface_descriptor;
    slot->memory = device.ImportSharedTextureMemory(&memory_descriptor);
    if (slot->memory == nullptr) return {};

    wgpu::SharedTextureMemoryProperties properties{};
    if (slot->memory.GetProperties(&properties) != wgpu::Status::Success
        || properties.size.width != width
        || properties.size.height != height
        || properties.format != wgpu::TextureFormat::BGRA8Unorm) {
        return {};
    }
    const auto usage = static_cast<wgpu::TextureUsage>(requested_usage)
        | wgpu::TextureUsage::RenderAttachment;
    if ((static_cast<uint64_t>(usage) & static_cast<uint64_t>(properties.usage))
        != static_cast<uint64_t>(usage)) {
        return {};
    }

    wgpu::TextureDescriptor texture_descriptor{};
    texture_descriptor.usage = usage;
    texture_descriptor.dimension = wgpu::TextureDimension::e2D;
    texture_descriptor.size = properties.size;
    texture_descriptor.format = properties.format;
    slot->texture = slot->memory.CreateTexture(&texture_descriptor);
    if (slot->texture == nullptr) return {};

    id<MTLDevice> metal_device =
        dawn::native::metal::GetMTLDevice(device.Get());
    if (metal_device == nil) return {};
    MTLTextureDescriptor* metal_descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
            width:width
            height:height
            mipmapped:NO];
    metal_descriptor.storageMode = MTLStorageModeShared;
    metal_descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
    slot->metal_texture = [metal_device newTextureWithDescriptor:metal_descriptor
        iosurface:slot->surface.get()
        plane:0U];
    return slot->metal_texture == nil ? nullptr : slot;
}

bool rebuild_canvas_ring(
    webscene_gpu_canvas& canvas,
    uint32_t width,
    uint32_t height,
    uint32_t buffer_count)
{
    std::vector<std::shared_ptr<webscene_gpu_canvas_slot>> slots;
    slots.reserve(buffer_count);
    for (uint32_t index = 0U; index < buffer_count; ++index) {
        auto slot = create_canvas_slot(canvas.device, width, height, canvas.usage);
        if (slot == nullptr) return false;
        slots.push_back(std::move(slot));
    }
    canvas.retired_slots.insert(
        canvas.retired_slots.end(),
        std::make_move_iterator(canvas.slots.begin()),
        std::make_move_iterator(canvas.slots.end()));
    canvas.slots = std::move(slots);
    canvas.width = width;
    canvas.height = height;
    canvas.next_slot = 0U;
    return true;
}

void collect_retired_slots(
    webscene_gpu_provider& provider,
    webscene_gpu_canvas& canvas)
{
    std::erase_if(
        canvas.retired_slots,
        [&provider](const auto& slot) {
            return !slot->dawn_access && !slot_is_exported(provider, slot);
        });
}

bool wait_for_submitted_work(
    webscene_gpu_provider& provider,
    const wgpu::Device& device)
{
    auto status = wgpu::QueueWorkDoneStatus::Error;
    const auto future = device.GetQueue().OnSubmittedWorkDone(
        wgpu::CallbackMode::WaitAnyOnly,
        [&status](wgpu::QueueWorkDoneStatus value, wgpu::StringView) {
            status = value;
        });
    const wgpu::Instance instance(provider.instance.Get());
    return instance.WaitAny(future, UINT64_MAX)
            == wgpu::WaitStatus::Success
        && status == wgpu::QueueWorkDoneStatus::Success;
}

} // namespace

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
    const auto& probe = cached_probe();
    *info = webscene_gpu_provider_info{};
    info->struct_size = sizeof(webscene_gpu_provider_info);
    info->abi_version = WEBSCENE_GPU_PROVIDER_ABI_VERSION;
    info->capabilities = probe.available ? WEBSCENE_GPU_CAPABILITY_WEBGPU : 0U;
    info->flags = WEBSCENE_GPU_PROVIDER_FLAG_ZERO_COPY
        | WEBSCENE_GPU_PROVIDER_FLAG_NO_SOFTWARE_FALLBACK
        | WEBSCENE_GPU_PROVIDER_FLAG_PRESENT_WAITS_FOR_GPU;
    copy_string(info->name, sizeof(info->name), "WebScene Dawn Metal provider");
    copy_string(info->adapter, sizeof(info->adapter), probe.adapter_name);
    return 1U;
}

webscene_gpu_provider* webscene_gpu_provider_create(
    const webscene_gpu_provider_options* options)
{
    if (options == nullptr
        || options->struct_size < sizeof(webscene_gpu_provider_options)
        || (options->required_capabilities & ~WEBSCENE_GPU_CAPABILITY_WEBGPU) != 0U
        || ((options->required_capabilities & WEBSCENE_GPU_CAPABILITY_WEBGPU) != 0U
            && !cached_probe().available)) {
        return nullptr;
    }
    return new (std::nothrow) webscene_gpu_provider();
}

void webscene_gpu_provider_destroy(webscene_gpu_provider* provider)
{
    delete provider;
}

void* webscene_gpu_provider_get_wgpu_instance(webscene_gpu_provider* provider)
{
    return provider == nullptr
        ? nullptr
        : reinterpret_cast<void*>(provider->instance.Get());
}

void* webscene_gpu_provider_get_wgpu_proc_address(
    webscene_gpu_provider*, const char* name)
{
    if (name == nullptr) return nullptr;
    const WGPUStringView view{name, WGPU_STRLEN};
    return reinterpret_cast<void*>(wgpuGetProcAddress(view));
}

webscene_gpu_canvas* webscene_gpu_provider_create_canvas(
    webscene_gpu_provider* provider,
    const webscene_gpu_canvas_configuration* configuration,
    uint32_t width,
    uint32_t height)
{
    if (provider == nullptr
        || configuration == nullptr
        || configuration->struct_size < sizeof(webscene_gpu_canvas_configuration)
        || configuration->device == 0U
        || width == 0U
        || height == 0U
        || configuration->pixel_format != WEBSCENE_GPU_PIXEL_FORMAT_BGRA8_UNORM
        || (configuration->alpha_mode != WEBSCENE_GPU_ALPHA_MODE_OPAQUE
            && configuration->alpha_mode
                != WEBSCENE_GPU_ALPHA_MODE_PREMULTIPLIED)) {
        return nullptr;
    }
    const auto buffer_count = std::clamp(configuration->buffer_count, 2U, 4U);
    auto canvas = std::unique_ptr<webscene_gpu_canvas>(
        new (std::nothrow) webscene_gpu_canvas());
    if (canvas == nullptr) return nullptr;
    canvas->owner = provider;
    canvas->device = wgpu::Device(
        reinterpret_cast<WGPUDevice>(configuration->device));
    canvas->pixel_format = configuration->pixel_format;
    canvas->alpha_mode = configuration->alpha_mode;
    canvas->usage = configuration->usage;
    std::lock_guard lock(provider->mutex);
    if (!rebuild_canvas_ring(*canvas, width, height, buffer_count)) return nullptr;
    return canvas.release();
}

webscene_gpu_status webscene_gpu_provider_resize_canvas(
    webscene_gpu_provider* provider,
    webscene_gpu_canvas* canvas,
    uint32_t width,
    uint32_t height)
{
    if (provider == nullptr || canvas == nullptr || canvas->owner != provider
        || width == 0U || height == 0U) {
        return WEBSCENE_GPU_STATUS_INVALID_ARGUMENT;
    }
    std::lock_guard lock(provider->mutex);
    if (canvas->acquired != nullptr) return WEBSCENE_GPU_STATUS_BUSY;
    if (canvas->width == width && canvas->height == height) {
        return WEBSCENE_GPU_STATUS_SUCCESS;
    }
    const auto buffer_count = static_cast<uint32_t>(canvas->slots.size());
    if (!rebuild_canvas_ring(*canvas, width, height, buffer_count)) {
        return WEBSCENE_GPU_STATUS_INTERNAL_ERROR;
    }
    collect_retired_slots(*provider, *canvas);
    return WEBSCENE_GPU_STATUS_SUCCESS;
}

webscene_gpu_status webscene_gpu_provider_acquire_canvas_texture(
    webscene_gpu_provider* provider,
    webscene_gpu_canvas* canvas,
    uintptr_t* texture)
{
    if (provider == nullptr || canvas == nullptr || canvas->owner != provider
        || texture == nullptr) {
        return WEBSCENE_GPU_STATUS_INVALID_ARGUMENT;
    }
    *texture = 0U;
    std::lock_guard lock(provider->mutex);
    if (canvas->acquired != nullptr) return WEBSCENE_GPU_STATUS_BUSY;
    collect_retired_slots(*provider, *canvas);
    for (size_t offset = 0U; offset < canvas->slots.size(); ++offset) {
        const auto index = (canvas->next_slot + offset) % canvas->slots.size();
        const auto& slot = canvas->slots[index];
        if (slot->dawn_access || slot_is_exported(*provider, slot)) continue;
        wgpu::SharedTextureMemoryBeginAccessDescriptor begin{};
        begin.concurrentRead = false;
        begin.initialized = slot->initialized;
        begin.fenceCount = 0U;
        begin.signaledValueCount = 0U;
        if (slot->memory.BeginAccess(slot->texture, &begin)
            != wgpu::Status::Success) {
            return slot->memory.IsDeviceLost()
                ? WEBSCENE_GPU_STATUS_DEVICE_LOST
                : WEBSCENE_GPU_STATUS_INTERNAL_ERROR;
        }
        slot->dawn_access = true;
        canvas->acquired = slot;
        canvas->next_slot = (index + 1U) % canvas->slots.size();
        wgpuTextureAddRef(slot->texture.Get());
        *texture = reinterpret_cast<uintptr_t>(slot->texture.Get());
        return WEBSCENE_GPU_STATUS_SUCCESS;
    }
    return WEBSCENE_GPU_STATUS_BUSY;
}

webscene_gpu_status webscene_gpu_provider_present_canvas(
    webscene_gpu_provider* provider,
    webscene_gpu_canvas* canvas,
    webscene_gpu_external_texture* texture)
{
    if (provider == nullptr || canvas == nullptr || canvas->owner != provider
        || texture == nullptr
        || texture->struct_size < sizeof(webscene_gpu_external_texture)) {
        return WEBSCENE_GPU_STATUS_INVALID_ARGUMENT;
    }
    std::lock_guard lock(provider->mutex);
    if (canvas->acquired == nullptr) return WEBSCENE_GPU_STATUS_INVALID_ARGUMENT;
    auto slot = std::move(canvas->acquired);
    wgpu::SharedTextureMemoryMetalEndAccessState metal_end{};
    wgpu::SharedTextureMemoryEndAccessState end{};
    end.nextInChain = &metal_end;
    if (slot->memory.EndAccess(slot->texture, &end) != wgpu::Status::Success) {
        slot->dawn_access = false;
        return slot->memory.IsDeviceLost()
            ? WEBSCENE_GPU_STATUS_DEVICE_LOST
            : WEBSCENE_GPU_STATUS_INTERNAL_ERROR;
    }
    slot->dawn_access = false;
    slot->initialized = end.initialized;
    if (!wait_for_submitted_work(*provider, canvas->device)) {
        return WEBSCENE_GPU_STATUS_DEVICE_LOST;
    }
    slot->generation = provider->next_generation++;
    provider->exports.emplace(
        slot->generation,
        webscene_gpu_provider::retained_export{slot, 1U});

    *texture = webscene_gpu_external_texture{};
    texture->struct_size = sizeof(webscene_gpu_external_texture);
    texture->handle_kind = WEBSCENE_GPU_HANDLE_IOSURFACE;
    texture->pixel_format = canvas->pixel_format;
    texture->flags = WEBSCENE_GPU_EXTERNAL_TEXTURE_GPU_COMPLETE
        | (canvas->alpha_mode == WEBSCENE_GPU_ALPHA_MODE_PREMULTIPLIED
            ? WEBSCENE_GPU_EXTERNAL_TEXTURE_PREMULTIPLIED_ALPHA
            : 0U);
    texture->width = canvas->width;
    texture->height = canvas->height;
    texture->generation = slot->generation;
    texture->shared_handle = reinterpret_cast<uintptr_t>(slot->surface.get());
    texture->texture_handle = reinterpret_cast<uintptr_t>(slot->metal_texture);
    return WEBSCENE_GPU_STATUS_SUCCESS;
}

void webscene_gpu_provider_destroy_canvas(
    webscene_gpu_provider* provider, webscene_gpu_canvas* canvas)
{
    if (provider == nullptr || canvas == nullptr || canvas->owner != provider) return;
    std::lock_guard lock(provider->mutex);
    if (canvas->acquired != nullptr && canvas->acquired->dawn_access) {
        wgpu::SharedTextureMemoryEndAccessState end{};
        canvas->acquired->memory.EndAccess(canvas->acquired->texture, &end);
        canvas->acquired->dawn_access = false;
    }
    delete canvas;
}

webscene_gpu_status webscene_gpu_provider_retain_external_texture(
    webscene_gpu_provider* provider, const webscene_gpu_external_texture* texture)
{
    if (provider == nullptr || texture == nullptr
        || texture->struct_size < sizeof(webscene_gpu_external_texture)) {
        return WEBSCENE_GPU_STATUS_INVALID_ARGUMENT;
    }
    std::lock_guard lock(provider->mutex);
    const auto found = provider->exports.find(texture->generation);
    if (found == provider->exports.end()) return WEBSCENE_GPU_STATUS_INVALID_ARGUMENT;
    ++found->second.retain_count;
    return WEBSCENE_GPU_STATUS_SUCCESS;
}

void webscene_gpu_provider_release_external_texture(
    webscene_gpu_provider* provider, const webscene_gpu_external_texture* texture)
{
    if (provider == nullptr || texture == nullptr) return;
    std::lock_guard lock(provider->mutex);
    const auto found = provider->exports.find(texture->generation);
    if (found == provider->exports.end()) return;
    if (found->second.retain_count > 1U) {
        --found->second.retain_count;
    } else {
        provider->exports.erase(found);
    }
}

} // extern "C"
