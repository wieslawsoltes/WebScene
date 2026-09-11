// Compile-only compatibility check. This does not establish runtime ABI or GPU correctness.
#include "include/gpu/graphite/dawn/DawnBackendContext.h"
#include "include/gpu/graphite/dawn/DawnGraphiteTypes.h"
#include "include/gpu/graphite/BackendTexture.h"

void check_shared_device_headers(const wgpu::Instance& instance,const wgpu::Device& device,
    const wgpu::Texture& texture) {
    skgpu::graphite::DawnBackendContext context;
    context.fInstance=instance;
    context.fDevice=device;
    context.fQueue=device.GetQueue();
    auto backend=skgpu::graphite::BackendTextures::MakeDawn(texture.Get());
    (void)backend;
}
