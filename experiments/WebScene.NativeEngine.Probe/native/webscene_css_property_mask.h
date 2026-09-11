#pragma once
#include "webscene_css_box_values.h"

namespace webscene_native::css {
// Existing native style storage groups used for inline/important precedence.
// These masks describe modeled fields, not a full CSS property registry.
enum inline_style_property : uint64_t {
    inline_width = 1ULL << 0U,
    inline_height = 1U << 1U,
    inline_left = 1U << 2U,
    inline_top = 1U << 3U,
    inline_right = 1U << 4U,
    inline_bottom = 1U << 5U,
    inline_display = 1U << 6U,
    inline_position = 1U << 7U,
    inline_flex_direction = 1U << 8U,
    inline_flex_grow = 1U << 9U,
    inline_background = 1U << 10U,
    inline_overflow = 1U << 11U,
    inline_color = 1U << 12U,
    inline_font_size = 1U << 13U,
    inline_font_family = 1U << 14U,
    inline_font_weight = 1U << 15U,
    inline_line_height = 1U << 16U,
    inline_text_align = 1U << 17U,
    inline_visibility = 1U << 18U,
    inline_pointer_events = 1U << 19U,
    inline_padding = 1U << 20U,
    inline_margin = 1U << 21U,
    inline_align_items = 1U << 22U,
    inline_opacity = 1U << 23U,
    inline_flex_wrap = 1U << 24U,
    inline_flex_shrink = 1U << 25U,
    inline_align_self = 1U << 26U,
    inline_min_width = 1U << 27U,
    inline_justify_content = 1U << 28U,
    inline_box_sizing = 1U << 29U,
    inline_border_radius = 1U << 30U,
    inline_transform = 1ULL << 31U,
    inline_white_space = 1ULL << 32U,
    inline_min_height = 1ULL << 33U,
    inline_max_width = 1ULL << 34U,
    inline_max_height = 1ULL << 35U,
    inline_gap = 1ULL << 36U,
    inline_box_shadow = 1ULL << 37U,
    inline_transform_origin = 1ULL << 38U,
    inline_svg_fill = 1ULL << 39U,
    inline_svg_stroke = 1ULL << 40U,
    inline_cursor = 1ULL << 41U,
    inline_letter_spacing = 1ULL << 42U,
    inline_word_spacing = 1ULL << 43U,
    inline_z_index = 1ULL << 44U,
    inline_float = 1ULL << 45U,
    inline_border = 1ULL << 46U,
    inline_flex_basis = 1ULL << 47U,
    inline_grid = 1ULL << 48U,
    inline_transition_property = 1ULL << 49U,
    inline_transition_duration = 1ULL << 50U,
    inline_transition_delay = 1ULL << 51U,
    inline_transition_timing = 1ULL << 52U,
    inline_table_border_model = 1ULL << 53U,
    inline_font_smoothing = 1ULL << 54U,
    inline_background_image = 1ULL << 55U,
    inline_contain = 1ULL << 56U,
    inline_svg_text_anchor = 1ULL << 57U,
    inline_scrollbar_width = 1ULL << 58U,
    inline_scrollbar_color = 1ULL << 59U,
    inline_transition = inline_transition_property | inline_transition_duration
        | inline_transition_delay | inline_transition_timing
};
inline uint64_t property_mask(std::string_view name)
    {
        if (name == "scrollbar-width") return inline_scrollbar_width;
        if (name == "scrollbar-color") return inline_scrollbar_color;
        if (name == "width") return inline_width;
        if (name == "height") return inline_height;
        if (name == "min-width") return inline_min_width;
        if (name == "min-height") return inline_min_height;
        if (name == "max-width") return inline_max_width;
        if (name == "max-height") return inline_max_height;
        if (name == "left" || name == "inset-inline-start") return inline_left;
        if (name == "top" || name == "inset-block-start") return inline_top;
        if (name == "right" || name == "inset-inline-end") return inline_right;
        if (name == "bottom" || name == "inset-block-end") return inline_bottom;
        if (name == "inset") {
            return inline_left | inline_top | inline_right | inline_bottom;
        }
        if (name == "display") return inline_display;
        if (name == "position") return inline_position;
        if (name == "contain") return inline_contain;
        if (name == "float" || name == "cssFloat") return inline_float;
        if (name == "z-index" || name == "zIndex") return inline_z_index;
        if (name == "flex-direction" || name == "flexDirection") {
            return inline_flex_direction;
        }
        if (name == "flex-flow" || name == "flexFlow") {
            return inline_flex_direction | inline_flex_wrap;
        }
        if (name == "flex") {
            return inline_flex_grow | inline_flex_shrink | inline_flex_basis;
        }
        if (name == "flex-grow") return inline_flex_grow;
        if (name == "flex-shrink") return inline_flex_shrink;
        if (name == "flex-basis") return inline_flex_basis;
        if (name == "flex-wrap" || name == "flexWrap") return inline_flex_wrap;
        if (name == "grid-template-columns" || name == "grid-template-rows"
            || name == "grid-area" || name == "grid-row"
            || name == "grid-row-start" || name == "grid-row-end"
            || name == "grid-column" || name == "grid-column-start"
            || name == "grid-column-end") {
            return inline_grid;
        }
        if (name == "transition") return inline_transition;
        if (name == "transition-property") return inline_transition_property;
        if (name == "transition-duration") return inline_transition_duration;
        if (name == "transition-delay") return inline_transition_delay;
        if (name == "transition-timing-function") return inline_transition_timing;
        if (name == "background") return inline_background | inline_background_image;
        if (name == "background-color" || name == "backgroundColor") {
            return inline_background;
        }
        if (name == "background-image" || name == "backgroundImage") {
            return inline_background_image;
        }
        if (name == "overflow" || name == "overflow-x" || name == "overflow-y") return inline_overflow;
        if (name == "visibility") return inline_visibility;
        if (name == "pointer-events") return inline_pointer_events;
        if (name == "color") return inline_color;
        if (name == "fill") return inline_svg_fill;
        if (name == "stroke") return inline_svg_stroke;
        if (name == "text-anchor") return inline_svg_text_anchor;
        if (name == "cursor") return inline_cursor;
        if (name == "font-size") return inline_font_size;
        if (name == "font-family") return inline_font_family;
        if (name == "-webkit-font-smoothing" || name == "webkit-font-smoothing"
            || name == "webkitFontSmoothing") {
            return inline_font_smoothing;
        }
        if (name == "font-weight") return inline_font_weight;
        if (name == "line-height") return inline_line_height;
        if (name == "letter-spacing") return inline_letter_spacing;
        if (name == "word-spacing") return inline_word_spacing;
        if (name == "text-align") return inline_text_align;
        if (name == "white-space") return inline_white_space;
        if (name == "padding" || name.starts_with("padding-")) return inline_padding;
        const auto canonical_name = canonical_property_name(name);
        if (canonical_name == "margin" || canonical_name.starts_with("margin-")) {
            return inline_margin;
        }
        if (name == "align-items") return inline_align_items;
        if (name == "align-self") return inline_align_self;
        if (name == "justify-content") return inline_justify_content;
        if (name == "gap" || name == "row-gap" || name == "column-gap"
            || name == "rowGap" || name == "columnGap") return inline_gap;
        if (name == "border-spacing" || name == "borderSpacing"
            || name == "border-collapse" || name == "borderCollapse") {
            return inline_table_border_model;
        }
        if (name == "box-sizing") return inline_box_sizing;
        if (name == "box-shadow" || name == "boxShadow") return inline_box_shadow;
        if (canonical_name == "border"
            || canonical_name == "border-width"
            || canonical_name == "border-style"
            || canonical_name == "border-color"
            || (canonical_name.starts_with("border-")
                && (canonical_name.ends_with("-width")
                    || canonical_name.ends_with("-style")
                    || canonical_name.ends_with("-color")))
            || canonical_name == "border-top"
            || canonical_name == "border-right"
            || canonical_name == "border-bottom"
            || canonical_name == "border-left") {
            return inline_border;
        }
        if (name == "border-radius"
            || name == "border-top-left-radius" || name == "border-top-right-radius"
            || name == "border-bottom-right-radius" || name == "border-bottom-left-radius"
            || name == "border-start-start-radius" || name == "border-start-end-radius"
            || name == "border-end-end-radius" || name == "border-end-start-radius") {
            return inline_border_radius;
        }
        if (name == "transform") return inline_transform;
        if (name == "transform-origin" || name == "transformOrigin") {
            return inline_transform_origin;
        }
        if (name == "opacity") return inline_opacity;
        return 0U;
    }

} // namespace webscene_native::css
