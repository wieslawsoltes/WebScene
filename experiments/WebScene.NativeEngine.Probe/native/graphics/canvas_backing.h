#pragma once
#include "resource_table.h"

namespace webscene::graphics {
enum class canvas_context_mode : uint32_t { none, two_d, webgl1, webgl2, webgpu };
// Canvas identity is independent of DOM attachment, scene acknowledgement and
// native image allocation. This metadata owns no pixels or backend pointers.
class canvas_backing {
    const uint64_t identity_=new_owner_token();
    canvas_context_mode mode_{};
    uint32_t width_=300,height_=150;
    uint64_t allocation_generation_=1,content_serial_{},content_floor_{};
public:
    canvas_backing()=default;
    canvas_backing(const canvas_backing&)=delete;
    canvas_backing& operator=(const canvas_backing&)=delete;
    uint64_t identity() const noexcept { return identity_; }
    canvas_context_mode mode() const noexcept { return mode_; }
    uint32_t width() const noexcept { return width_; }
    uint32_t height() const noexcept { return height_; }
    uint64_t allocation_generation() const noexcept { return allocation_generation_; }
    uint64_t content_serial() const noexcept { return content_serial_; }
    bool accepts_completed_content(uint64_t serial) const noexcept {
        return serial>=content_floor_ && serial<=content_serial_;
    }
    bool claim_context(canvas_context_mode mode) noexcept {
        if (mode==canvas_context_mode::none || static_cast<uint32_t>(mode)>static_cast<uint32_t>(canvas_context_mode::webgpu)) return false;
        if (mode_!=canvas_context_mode::none && mode_!=mode) return false;
        mode_=mode;
        return true;
    }
    void publish_content() {
        if (content_serial_==UINT64_MAX) throw std::overflow_error("canvas content serial exhausted");
        ++content_serial_;
    }
    // Bitmap reset changes content even at the same dimensions. Only storage
    // dimension changes advance the allocation generation; CSS size is separate.
    void reset_bitmap(uint32_t width,uint32_t height) {
        const bool changed=width!=width_ || height!=height_;
        if (content_serial_==UINT64_MAX || (changed && allocation_generation_==UINT64_MAX))
            throw std::overflow_error("canvas backing version exhausted");
        width_=width; height_=height;
        if (changed) ++allocation_generation_;
        ++content_serial_;
        content_floor_=content_serial_;
    }
};
} // namespace webscene::graphics
