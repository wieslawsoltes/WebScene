#pragma once
#include "webscene_native_dom.h"
#include <cmath>
#include <optional>

namespace webscene_native {
// Shared property semantics after tokenization/variable evaluation. This builder
// consumes typed components only and has no dependency on CSS parsing or V8.
class shadow_value_builder final {
    float lengths_[4]{};
    size_t count_{};
    bool valid_{true}, inset_{false}, colored_{false}, closed_{false};
    std::optional<uint32_t> color_;
public:
    void inset() {
        if (inset_) valid_=false;
        inset_=true;
        if (count_) closed_=true;
    }
    void color(std::optional<uint32_t> rgba) {
        if (colored_) valid_=false;
        colored_=true; color_=rgba;
        if (count_) closed_=true;
    }
    void length(css_length value) {
        if (closed_ || count_==4 || value.unit!=length_unit::pixels ||
            !std::isfinite(value.value) || !std::isfinite(value.pixel_offset)) {
            valid_=false; return;
        }
        lengths_[count_++]=value.value+value.pixel_offset;
    }
    void invalidate() { valid_=false; }
    static void clear(node_style &style) {
        style.box_shadow_present=false;
        style.box_shadow_inset=false;
        style.box_shadow_current_color=false;
        style.box_shadow_offset_x=style.box_shadow_offset_y=0;
        style.box_shadow_blur_radius=style.box_shadow_spread_radius=0;
        style.box_shadow_rgba=0;
    }
    bool apply(node_style &style) const {
        clear(style);
        if (!valid_ || count_<2 || lengths_[2]<0) return false;
        style.box_shadow_offset_x=lengths_[0];
        style.box_shadow_offset_y=lengths_[1];
        style.box_shadow_blur_radius=lengths_[2];
        style.box_shadow_spread_radius=lengths_[3];
        style.box_shadow_rgba=color_.value_or(0);
        style.box_shadow_current_color=!color_;
        style.box_shadow_present=true;
        style.box_shadow_inset=inset_;
        return true;
    }
};
}
