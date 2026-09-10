#pragma once
#include "webgpu_canvas_host.h"
#include <optional>
#include <thread>

namespace webscene::graphics {
// Native authoring counterpart of GPUCanvasContext. The caller owns the device
// service and pumps its completions. Host callbacks retain submitted images until
// GPU completion and compositor retirement; this object never reads them back.
class native_webgpu_canvas_context final {
    const std::thread::id thread_ = std::this_thread::get_id();
    webgpu_canvas_host host_;
    std::optional<webgpu_canvas_configuration> configuration_;
    wgpu::Texture current_;
    uint32_t width_, height_;
    void check_thread() const {
        if (std::this_thread::get_id() != thread_)
            throw std::logic_error("Canvas context requires its owning engine thread");
    }
public:
    native_webgpu_canvas_context(uint32_t width, uint32_t height, webgpu_canvas_host host)
        : host_(std::move(host)), width_(width), height_(height) {
        if (!host_.validate || !host_.acquire || !host_.retire)
            throw std::invalid_argument("Canvas requires validation, acquisition and retirement callbacks");
    }
    native_webgpu_canvas_context(const native_webgpu_canvas_context&) = delete;
    native_webgpu_canvas_context& operator=(const native_webgpu_canvas_context&) = delete;
    ~native_webgpu_canvas_context() {
        // Destruction on another thread or failed retirement is a violated
        // ownership contract; silently abandoning an imported image is unsafe.
        check_thread();
        end_frame(false);
    }
    void configure(webgpu_canvas_configuration configuration) {
        check_thread();
        if (!configuration.device)
            throw std::invalid_argument("Canvas requires a live device");
        host_.validate(configuration);
        validate_webgpu_canvas_format_usage(configuration);
        end_frame(false);
        if (host_.invalidate) host_.invalidate();
        configuration_ = std::move(configuration);
    }
    void unconfigure() {
        check_thread();
        end_frame(false);
        configuration_.reset();
        if (host_.invalidate) host_.invalidate();
    }
    void resize(uint32_t width, uint32_t height) {
        check_thread();
        if (width == width_ && height == height_) return;
        end_frame(false);
        width_ = width; height_ = height;
        if (host_.invalidate) host_.invalidate();
    }
    // An empty texture signals presenter backpressure; callers retry on the
    // next rendering opportunity rather than allocating unbounded images.
    wgpu::Texture current_texture() {
        check_thread();
        if (!configuration_) throw std::logic_error("Canvas is not configured");
        if (!current_)
            current_ = host_.acquire(*configuration_,
                webgpu_canvas_texture_descriptor(*configuration_, width_, height_));
        return current_;
    }
    void end_frame(bool present = true) {
        check_thread();
        if (!current_) return;
        host_.retire(current_, present);
        current_ = nullptr;
    }
};
} // namespace webscene::graphics
