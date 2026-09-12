#include "native_webgpu_surface.h"
#include <chrono>
#include <iostream>
#include <thread>
#include <stdexcept>
using namespace webscene::graphics;
static void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
static auto paint(native_headless_webgpu_surface& surface, wgpu::Color color) {
    auto texture = surface.current_texture(); check(bool(texture), "Unexpected image backpressure");
    auto encoder = surface.device().CreateCommandEncoder();
    wgpu::RenderPassColorAttachment attachment{};
    attachment.view = texture.CreateView(); attachment.loadOp = wgpu::LoadOp::Clear;
    attachment.storeOp = wgpu::StoreOp::Store; attachment.clearValue = color;
    wgpu::RenderPassDescriptor pass{}; pass.colorAttachmentCount = 1; pass.colorAttachments = &attachment;
    auto render = encoder.BeginRenderPass(&pass); render.End();
    auto commands = encoder.Finish(); surface.device().GetQueue().Submit(1, &commands);
    return surface.present();
}
static auto wait(std::shared_ptr<webscene_gpu_image_snapshot> snapshot) {
    check(bool(snapshot), "No submitted snapshot");
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    // Deliberately no ProcessEvents/frame/presentation pumping: idle completion is required.
    while (snapshot->state() == webscene_gpu_image_snapshot::status::pending && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto result = snapshot->resolve(); check(bool(result), "Headless completion failed while idle"); return result;
}
static void red(const webscene_gpu_image_lease_v3& image, uint32_t width, uint32_t height) {
    auto capture = capture_native_image(image);
    check(capture.metadata.width == width && capture.metadata.height == height, "Wrong retained dimensions");
    check(capture.row_bytes == width * 4 && capture.pixels.size() == size_t(width) * height * 4, "Bad capture layout");
    for (size_t offset = 0; offset < capture.pixels.size(); offset += 4)
        check(capture.pixels[offset] == 0 && capture.pixels[offset + 1] == 0 && capture.pixels[offset + 2] == 255 && capture.pixels[offset + 3] == 255, "GPU pixels corrupted");
}
int main() {
    auto options = headless_webgpu_options::environment();
    native_headless_webgpu_surface surface(99, 65, 47, 1024 * 1024, options);
    const auto info = surface.adapter_info();
    std::cout << "backend=Vulkan software=" << info.software << " device=" << info.device << " driver=" << info.description << '\n';
    check(!options.force_software || info.software, "Software adapter request was misreported");
    auto first = wait(paint(surface, {1,0,0,1}));
    auto second = wait(paint(surface, {0,1,0,1}));
    auto third = wait(paint(surface, {0,0,1,1}));
    check(!surface.current_texture(), "Retained frame ring exceeded three images");
    check(surface.diagnostic_captures() == 0, "Ordinary rendering performed CPU readback");
    check(surface.occupancy().retained == 3 && surface.occupancy().producer_pending == 0, "Incorrect image ownership counters");
    red(*first, 65, 47);
    auto too_small = false;
    try { capture_native_image(*first, {30'000'000'000ULL, 4}); } catch (const std::length_error&) { too_small = true; }
    check(too_small, "Capture memory budget was ignored");
    check(surface.occupancy().consumer_pending == 0, "Rejected capture leaked a consumer");
    second.reset(); third.reset();
    surface.resize(83, 59);
    red(*first, 65, 47);
    auto resized = wait(paint(surface, {1,0,0,1})); red(*resized, 83, 59);
    check(resized->value.describe().allocation_generation != first->value.describe().allocation_generation, "Resize generation not changed");
    bool bad_size = false;
    try { surface.resize(0, 10); } catch (const std::invalid_argument&) { bad_size = true; }
    check(bad_size, "Zero-sized texture accepted");
    const auto captures = surface.diagnostic_captures();
    first.reset(); resized.reset();
    for (int i = 0; i < 200; ++i) { auto image = wait(paint(surface, {1,0,0,1})); }
    check(surface.diagnostic_captures() == captures, "Normal frames triggered hidden capture");
    check(surface.allocated_bytes() <= 1024 * 1024, "Image allocations exceed byte budget");
    auto retained = wait(paint(surface, {1,0,0,1}));
    surface.close(); red(*retained, 83, 59); retained.reset();
    check(surface.occupancy().busy == 0, "Shutdown leaked image leases");
    for (int i = 0; i < 8; ++i) {
        native_headless_webgpu_surface next(100+i, 16, 16, 65536, options);
        auto pending = paint(next, {1,0,0,1}); next.close(); auto image = wait(pending); red(*image,16,16);
    }
    std::cout << "headless GPU contracts passed\n";
}
