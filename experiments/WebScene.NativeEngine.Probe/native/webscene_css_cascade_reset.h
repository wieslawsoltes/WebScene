#pragma once
#include "webscene_css_property_mask.h"
#include "webscene_css_variables.h"
#include "webscene_css_visibility_values.h"
#include "webscene_native_style_defaults.h"
#include "webscene_shadow_value.h"

namespace webscene_native::css {
// Clear previously cascaded fields before matching a new rule set. Authored
// inline metadata remains owned by the node and is preserved/reseeded here.
inline void reset_cascaded_style(dom_node& node,
    const std::unordered_map<std::string,std::string>& variables)
{
        node.style.important_property_mask = 0;
        node.style.important_margin_sides = 0;
        node.style.scrollbar_hidden = false;
        node.style.scrollbar_visibility_important = false;
        node.style.reset_scrollbar_style();
        if(node.parent) {
            const auto& inherited=node.parent->style.scrollbar();
            if(inherited.thumb_rgba!=node.style.scrollbar().thumb_rgba ||
               inherited.track_rgba!=node.style.scrollbar().track_rgba) {
                auto& bar=node.style.mutable_scrollbar();
                bar.thumb_rgba=inherited.thumb_rgba;bar.track_rgba=inherited.track_rgba;
            }
        }
        if ((node.style.inline_property_mask & inline_position) == 0U) {
            node.style.position = position_mode::normal;
        }
        if ((node.style.inline_property_mask & inline_float) == 0U) {
            node.style.floating = float_mode::none;
        }
        if ((node.style.inline_property_mask & inline_z_index) == 0U) {
            node.style.z_index = 0;
            node.style.z_index_auto = true;
        }
        if ((node.style.inline_property_mask & inline_grid) == 0U) {
            node.style.clear_grid();
        }
        if ((node.style.inline_property_mask & inline_width) == 0U) node.style.width = {};
        if ((node.style.inline_property_mask & inline_height) == 0U) node.style.height = {};
        if ((node.style.inline_property_mask & inline_min_width) == 0U) node.style.min_width = {};
        if ((node.style.inline_property_mask & inline_min_height) == 0U) node.style.min_height = {};
        if ((node.style.inline_property_mask & inline_max_width) == 0U) node.style.max_width = {};
        if ((node.style.inline_property_mask & inline_max_height) == 0U) node.style.max_height = {};
        if ((node.style.inline_property_mask & inline_left) == 0U) node.style.left = {};
        if ((node.style.inline_property_mask & inline_top) == 0U) node.style.top = {};
        if ((node.style.inline_property_mask & inline_right) == 0U) node.style.right = {};
        if ((node.style.inline_property_mask & inline_bottom) == 0U) node.style.bottom = {};
        if ((node.style.inline_property_mask & inline_display) == 0U) {
            node.style.display = native_default_display_for_node(node);
        }
        if ((node.style.inline_property_mask & inline_padding) == 0U) {
            node.style.padding_left = {};
            node.style.padding_top = {};
            node.style.padding_right = {};
            node.style.padding_bottom = {};
        }
        {
            node.style.margin_left = {};
            node.style.margin_top = {};
            node.style.margin_right = {};
            node.style.margin_bottom = {};
            node.style.margin_left_auto = false;
            node.style.margin_top_auto = false;
            node.style.margin_right_auto = false;
            node.style.margin_bottom_auto = false;
            // Rebuild from authored inline values, not a previous computed
            // value that may have been replaced by an important stylesheet.
            for (const auto important : {false, true}) {
                for (const auto& [name, value] : node.authored_style().declarations) {
                    if (margin_sides(name) != 0U
                        && node.authored_style().important_declarations.contains(name) == important) {
                        apply_margin_declaration(node.style, name, resolve_value(node,value,variables));
                    }
                }
            }
        }
        if ((node.style.inline_property_mask & inline_border) == 0U) {
            node.style.border_left_width = {};
            node.style.border_top_width = {};
            node.style.border_right_width = {};
            node.style.border_bottom_width = {};
            node.style.border_left_rgba = 0;
            node.style.border_top_rgba = 0;
            node.style.border_right_rgba = 0;
            node.style.border_bottom_rgba = 0;
            node.style.border_left_current_color = true;
            node.style.border_top_current_color = true;
            node.style.border_right_current_color = true;
            node.style.border_bottom_current_color = true;
        }
        node.style.outline_width = {};
        node.style.outline_rgba = 0;
        if ((node.style.inline_property_mask & inline_border_radius) == 0U) {
            node.style.border_top_left_radius = {};
            node.style.border_top_right_radius = {};
            node.style.border_bottom_right_radius = {};
            node.style.border_bottom_left_radius = {};
            node.style.clear_vertical_corner_radii();
        }
        if ((node.style.inline_property_mask & inline_transform) == 0U) {
            node.style.transform_translate_x = {};
            node.style.transform_translate_y = {};
            node.style.transform_scale_x = 1;
            node.style.transform_scale_y = 1;
            node.style.transform_rotate_degrees = 0;
            node.style.transform_specified = false;
            node.style.transform_stacking_context = false;
        }
        if ((node.style.inline_property_mask & inline_transform_origin) == 0U) {
            node.style.transform_origin_x = {50, length_unit::percent};
            node.style.transform_origin_y = {50, length_unit::percent};
            node.style.transform_origin_specified = false;
        }
        if ((node.style.inline_property_mask & inline_contain) == 0U) {
            node.style.contain_stacking_context = false;
            node.style.mutable_textual().contain_value = "none";
        }
        node.style.clear_animations();
        if ((node.style.inline_property_mask & inline_flex_direction) == 0U) {
            node.style.direction = flex_direction::row;
            node.style.flex_reverse = false;
        }
        if ((node.style.inline_property_mask & inline_align_items) == 0U) {
            node.style.align_items = align_mode::stretch;
        }
        if ((node.style.inline_property_mask & inline_align_self) == 0U) {
            node.style.align_self = align_mode::stretch;
            node.style.align_self_specified = false;
        }
        if ((node.style.inline_property_mask & inline_justify_content) == 0U) {
            node.style.justify_content = justify_mode::start;
        }
        if ((node.style.inline_property_mask & inline_gap) == 0U) {
            node.style.row_gap = {};
            node.style.column_gap = {};
        }
        if ((node.style.inline_property_mask & inline_table_border_model) == 0U) {
            node.style.clear_table();
        }
        if ((node.style.inline_property_mask & inline_flex_wrap) == 0U) node.style.flex_wrap = false;
        if ((node.style.inline_property_mask & inline_flex_grow) == 0U) node.style.flex_grow = 0;
        if ((node.style.inline_property_mask & inline_flex_shrink) == 0U) node.style.flex_shrink = 1;
        if ((node.style.inline_property_mask & inline_flex_basis) == 0U) {
            node.style.flex_basis = {};
        }
        if ((node.style.inline_property_mask & inline_box_sizing) == 0U) node.style.border_box = false;
        if ((node.style.inline_property_mask & inline_box_shadow) == 0U) {
            shadow_value_builder::clear(node.style);
        }
        if ((node.style.inline_property_mask & inline_background) == 0U) {
            node.style.background_rgba = 0;
            node.style.background_current_color = false;
        }
        if ((node.style.inline_property_mask & inline_background_image) == 0U) {
            node.style.clear_background_image();
        }
        if ((node.style.inline_property_mask & inline_overflow) == 0U) {
            node.style.overflow_x = overflow_mode::visible;
            node.style.overflow_y = overflow_mode::visible;
            refresh_overflow_state(node.style);
        }
        if ((node.style.inline_property_mask & inline_pointer_events) == 0U) {
            node.style.pointer_events_none = false;
            node.style.pointer_events_specified = false;
        }
        if ((node.style.inline_property_mask & inline_opacity) == 0U) node.style.opacity = 1;
        if ((node.style.inline_property_mask & inline_color) == 0U) node.style.foreground_rgba = 0;
        if ((node.style.inline_property_mask & inline_font_size) == 0U) node.style.font_size = -1;
        if (auto* textual = node.style.mutable_textual_if_present();
            textual != nullptr) {
            if ((node.style.inline_property_mask & inline_font_family) == 0U) {
                textual->font_family.clear();
            }
            if ((node.style.inline_property_mask & inline_font_smoothing) == 0U) {
                textual->font_smoothing.clear();
            }
            if ((node.style.inline_property_mask & inline_svg_fill) == 0U)
                textual->svg_fill.clear();
            if ((node.style.inline_property_mask & inline_svg_stroke) == 0U)
                textual->svg_stroke.clear();
            if ((node.style.inline_property_mask & inline_svg_stroke_width) == 0U)
                textual->svg_stroke_width.clear();
            textual->list_style_position.clear();
            textual->list_style_type.clear();
            if ((node.style.inline_property_mask & inline_text_align) == 0U) {
                textual->text_align.clear();
            }
            if ((node.style.inline_property_mask & inline_white_space) == 0U) {
                textual->white_space.clear();
            }
        }
        if ((node.style.inline_property_mask & inline_font_weight) == 0U) node.style.font_weight = 0;
        if ((node.style.inline_property_mask & inline_line_height) == 0U) node.style.line_height = -1;
        if ((node.style.inline_property_mask & inline_letter_spacing) == 0U) {
            node.style.letter_spacing = 0;
            node.style.letter_spacing_specified = false;
        }
        if ((node.style.inline_property_mask & inline_word_spacing) == 0U) {
            node.style.word_spacing = 0;
            node.style.word_spacing_specified = false;
        }
        css::seed_inline_custom_properties(node);
        node.style.clear_pseudo_elements();
        // Recompute stylesheet visibility from the current class/selector set.
        // React reuses toolbar nodes while replacing their responsive classes;
        // retaining an earlier `visibility:hidden` makes the new variant stale.
        if ((node.style.inline_property_mask & inline_visibility) == 0U) {
            node.style.visibility_hidden = false;
            node.style.visibility_specified = false;
        }
}
} // namespace webscene_native::css
