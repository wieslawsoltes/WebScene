#pragma once
#include "webscene_css_text_values.h"
#include "webscene_css_variables.h"
#include <array>
#include <span>

namespace webscene_native::css {
inline bool layout_length_equal(css_length left, css_length right) noexcept
    {
        return left.value == right.value
            && left.unit == right.unit
            && left.pixel_offset == right.pixel_offset;
    }

inline bool pseudo_layout_equal(
        const node_style::pseudo_element& left,
        const node_style::pseudo_element& right) noexcept
    {
        const std::array left_lengths{
            left.width, left.height, left.left, left.top, left.right, left.bottom,
            left.margin_left, left.margin_top, left.margin_right, left.margin_bottom,
            left.padding_left, left.padding_top, left.padding_right, left.padding_bottom,
            left.border_left_width, left.border_top_width,
            left.border_right_width, left.border_bottom_width};
        const std::array right_lengths{
            right.width, right.height, right.left, right.top, right.right, right.bottom,
            right.margin_left, right.margin_top, right.margin_right, right.margin_bottom,
            right.padding_left, right.padding_top, right.padding_right, right.padding_bottom,
            right.border_left_width, right.border_top_width,
            right.border_right_width, right.border_bottom_width};
        for (size_t index = 0; index < left_lengths.size(); ++index) {
            if (!layout_length_equal(left_lengths[index], right_lengths[index])) return false;
        }
        return left.display == right.display
            && left.position == right.position
            && left.align_self == right.align_self
            && left.font_size == right.font_size
            && left.line_height == right.line_height
            && left.content == right.content
            && left.generated == right.generated
            && left.display_none == right.display_none
            && left.align_self_specified == right.align_self_specified
            && left.border_box == right.border_box;
    }

inline bool grid_layout_equal(
        const node_style::grid_data& left,
        const node_style::grid_data& right) noexcept
    {
        const auto tracks_equal = [](const auto& left_tracks, const auto& right_tracks) {
            if (left_tracks.size() != right_tracks.size()) return false;
            for (size_t index = 0; index < left_tracks.size(); ++index) {
                const auto& left_track = left_tracks[index];
                const auto& right_track = right_tracks[index];
                if (!layout_length_equal(left_track.minimum, right_track.minimum)
                    || !layout_length_equal(left_track.maximum, right_track.maximum)
                    || left_track.fraction != right_track.fraction
                    || left_track.kind != right_track.kind) {
                    return false;
                }
            }
            return true;
        };
        return tracks_equal(left.template_columns, right.template_columns)
            && tracks_equal(left.template_rows, right.template_rows)
            && tracks_equal(left.auto_columns, right.auto_columns)
            && left.subgrid_columns == right.subgrid_columns
            && left.two_columns == right.two_columns
            && left.auto_flow_column == right.auto_flow_column
            && left.fractional_rows == right.fractional_rows
            && left.span_all == right.span_all
            && left.column_start == right.column_start
            && left.area_value == right.area_value
            && left.row_value == right.row_value
            && left.row_start_value == right.row_start_value
            && left.row_end_value == right.row_end_value
            && left.column_value == right.column_value
            && left.column_start_value == right.column_start_value
            && left.column_end_value == right.column_end_value;
    }

inline bool computed_layout_style_equal(
        const node_style& left,
        const node_style& right) noexcept
    {
        const std::array left_lengths{
            left.width, left.height, left.min_width, left.min_height,
            left.max_width, left.max_height, left.left, left.top, left.right, left.bottom,
            left.padding_left, left.padding_top, left.padding_right, left.padding_bottom,
            left.margin_left, left.margin_top, left.margin_right, left.margin_bottom,
            left.row_gap, left.column_gap,
            left.border_left_width, left.border_top_width,
            left.border_right_width, left.border_bottom_width,
            left.flex_basis};
        const std::array right_lengths{
            right.width, right.height, right.min_width, right.min_height,
            right.max_width, right.max_height, right.left, right.top, right.right, right.bottom,
            right.padding_left, right.padding_top, right.padding_right, right.padding_bottom,
            right.margin_left, right.margin_top, right.margin_right, right.margin_bottom,
            right.row_gap, right.column_gap,
            right.border_left_width, right.border_top_width,
            right.border_right_width, right.border_bottom_width,
            right.flex_basis};
        for (size_t index = 0; index < left_lengths.size(); ++index) {
            if (!layout_length_equal(left_lengths[index], right_lengths[index])) return false;
        }
        const auto& left_text = left.textual();
        const auto& right_text = right.textual();
        const auto& left_table = left.table();
        const auto& right_table = right.table();
        return left.display == right.display
            && left.position == right.position
            && left.floating == right.floating
            && left.direction == right.direction
            && left.align_items == right.align_items
            && left.align_self == right.align_self
            && left.justify_content == right.justify_content
            && left.overflow_x == right.overflow_x
            && left.overflow_y == right.overflow_y
            && left.flex_grow == right.flex_grow
            && left.flex_shrink == right.flex_shrink
            && left.font_size == right.font_size
            && left.line_height == right.line_height
            && left.font_weight == right.font_weight
            && left.letter_spacing == right.letter_spacing
            && left.word_spacing == right.word_spacing
            && left.letter_spacing_specified == right.letter_spacing_specified
            && left.word_spacing_specified == right.word_spacing_specified
            && left.clip == right.clip
            && left.scroll_x_enabled == right.scroll_x_enabled
            && left.scroll_y_enabled == right.scroll_y_enabled
            && left.scrollbar_hidden == right.scrollbar_hidden
            && left.flex_wrap == right.flex_wrap
            && left.flex_reverse == right.flex_reverse
            && left.align_self_specified == right.align_self_specified
            && left.border_box == right.border_box
            && left.margin_left_auto == right.margin_left_auto
            && left.margin_top_auto == right.margin_top_auto
            && left.margin_right_auto == right.margin_right_auto
            && left.margin_bottom_auto == right.margin_bottom_auto
            && left.table_layout_fixed == right.table_layout_fixed
            && left_text.font_family == right_text.font_family
            && left_text.text_align == right_text.text_align
            && left_text.vertical_align == right_text.vertical_align
            && left_text.text_transform == right_text.text_transform
            && left_text.white_space == right_text.white_space
            && left_text.list_style_position == right_text.list_style_position
            && left_text.list_style_type == right_text.list_style_type
            && layout_length_equal(
                left.transform_translate_x,
                right.transform_translate_x)
            && layout_length_equal(
                left.transform_translate_y,
                right.transform_translate_y)
            && layout_length_equal(
                left.transform_origin_x,
                right.transform_origin_x)
            && layout_length_equal(
                left.transform_origin_y,
                right.transform_origin_y)
            && left.transform_scale_x == right.transform_scale_x
            && left.transform_scale_y == right.transform_scale_y
            && left.transform_rotate_degrees == right.transform_rotate_degrees
            && layout_length_equal(
                left_table.border_spacing_horizontal,
                right_table.border_spacing_horizontal)
            && layout_length_equal(
                left_table.border_spacing_vertical,
                right_table.border_spacing_vertical)
            && left_table.border_collapsed == right_table.border_collapsed
            && grid_layout_equal(left.grid(), right.grid())
            && pseudo_layout_equal(left.before_pseudo(), right.before_pseudo())
            && pseudo_layout_equal(left.after_pseudo(), right.after_pseudo());
    }

inline void recompute_cascaded_line_height(dom_node& node, std::span<const css_rule* const> rules,
    const std::unordered_map<std::string,std::string>& variables)
    {
        // Relative lengths compute against the final font-size, irrespective
        // of declaration order. Unlike a number, an em/% value then inherits
        // as a length. Include font shorthands because they reset line-height.
        std::optional<std::string> winning_value;
        bool winning_important = false;
        const auto consider = [&](const css_declaration& declaration) {
            if (declaration.name != "line-height" && declaration.name != "font") return;
            if (winning_important && !declaration.important) return;
            auto value = resolve_value(node,declaration.value,variables);
            if (value.empty()) return;
            if (declaration.name == "font") {
                if (value == "inherit" || value == "unset") value = "inherit";
                else if (const auto font = parse_font_shorthand(node, value); font.has_value())
                    value = font->line_height_token;
                else return;
            }
            winning_value = std::move(value);
            winning_important = declaration.important;
        };
        for (const auto* rule : rules)
            for (const auto& declaration : rule->declarations()) consider(declaration);
        for (const auto& [name, value] : node.authored_style().declarations)
            consider({name, value, node.authored_style().important_declarations.contains(name)});
        if (winning_value.has_value()) {
            const auto font_size = node.style.font_size >= 0
                ? node.style.font_size : inherited_font_size(node);
            node.style.line_height = resolved_declared_line_height(node, *winning_value, font_size);
        }
    }

inline void recompute_inline_font_relative_metrics(dom_node& node)
    {
        const auto line_height = node.authored_style().declarations.find("line-height");
        if ((node.style.inline_property_mask & inline_line_height) != 0U
            && (node.style.important_property_mask & inline_line_height) == 0U
            && line_height != node.authored_style().declarations.end()) {
            const auto font_size = node.style.font_size >= 0
                ? node.style.font_size : inherited_font_size(node);
            node.style.line_height = resolved_declared_line_height(
                node,
                line_height->second,
                font_size);
        }
    }

} // namespace webscene_native::css
