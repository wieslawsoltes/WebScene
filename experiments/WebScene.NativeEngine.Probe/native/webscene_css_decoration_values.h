#pragma once
#include "webscene_css_property_mask.h"
#include "webscene_css_transitions.h"

namespace webscene_native::css {
template<typename Decision,typename Protected>
bool apply_decoration_value(dom_node& node,const std::string& name,const std::string& value,
    Decision& decision,Protected&& is_inline)
{
        const auto parse_declared_color = [&](const std::string& color_value) {
            return native_document::parse_color(color_value);
        };
        const auto parse_border = [&] (
            css_length& width,
            uint32_t& color,
            bool& current_color) {
            // A border shorthand resets its omitted color longhand to the
            // initial currentColor value. Keep that dependency deferred.
            color = 0;
            current_color = true;
            // Functional colors are single CSS components. Splitting their
            // arguments at spaces turns color-mix percentages into border widths.
            for (const auto& token : split_value_tokens(value)) {
                if (token == "none") {
                    width = {};
                    color = 0;
                } else if (token == "thin") {
                    width = {1, length_unit::pixels};
                } else if (token == "medium") {
                    width = {3, length_unit::pixels};
                } else if (token == "thick") {
                    width = {5, length_unit::pixels};
                } else if (std::isdigit(static_cast<unsigned char>(token.front()))
                    || token.front() == '.' || token.front() == '-') {
                    width = native_document::parse_length(token);
                } else {
                    const auto parsed = parse_declared_color(token);
                    if (is_explicit_color_token(token, parsed)) {
                        color = parsed;
                        current_color = ascii_lower(token) == "currentcolor";
                    }
                }
            }
        };
        const auto parse_four_border_values = [&](bool colors) {
            if (colors) {
                // Functional colors contain spaces after commas and therefore
                // form one CSS component value even though a whitespace stream
                // would split them into several apparent border sides.
                const auto single = parse_declared_color(value);
                if (single != 0U || value == "transparent"
                    || ascii_lower(value) == "currentcolor") {
                    node.style.border_top_rgba = single;
                    node.style.border_right_rgba = single;
                    node.style.border_bottom_rgba = single;
                    node.style.border_left_rgba = single;
                    const auto current = ascii_lower(value) == "currentcolor";
                    node.style.border_top_current_color = current;
                    node.style.border_right_current_color = current;
                    node.style.border_bottom_current_color = current;
                    node.style.border_left_current_color = current;
                    return;
                }
            }
            std::vector<std::string> values;
            std::istringstream stream(value);
            for (std::string token; stream >> token;) values.push_back(std::move(token));
            if (values.empty()) return;
            const auto top = values[0];
            const auto right = values.size() > 1 ? values[1] : values[0];
            const auto bottom = values.size() > 2 ? values[2] : values[0];
            const auto left = values.size() > 3 ? values[3]
                : values.size() > 1 ? values[1] : values[0];
            if (colors) {
                node.style.border_top_rgba = parse_declared_color(top);
                node.style.border_right_rgba = parse_declared_color(right);
                node.style.border_bottom_rgba = parse_declared_color(bottom);
                node.style.border_left_rgba = parse_declared_color(left);
                node.style.border_top_current_color =
                    ascii_lower(top) == "currentcolor";
                node.style.border_right_current_color =
                    ascii_lower(right) == "currentcolor";
                node.style.border_bottom_current_color =
                    ascii_lower(bottom) == "currentcolor";
                node.style.border_left_current_color =
                    ascii_lower(left) == "currentcolor";
            } else {
                node.style.border_top_width = native_document::parse_length(top);
                node.style.border_right_width = native_document::parse_length(right);
                node.style.border_bottom_width = native_document::parse_length(bottom);
                node.style.border_left_width = native_document::parse_length(left);
            }
        };
        if (name == "border-radius"
            || name == "border-top-left-radius"
            || name == "border-top-right-radius"
            || name == "border-bottom-right-radius"
            || name == "border-bottom-left-radius"
            || name == "border-start-start-radius"
            || name == "border-start-end-radius"
            || name == "border-end-end-radius"
            || name == "border-end-start-radius") {
            if (!is_inline(inline_border_radius)) {
                apply_corner_radius_declaration(
                    name,
                    value,
                    node.style);
            }
            return true;
        } else if (name == "transform") {
            if (!is_inline(inline_transform)) {
                native_document::parse_transform_translate(
                    value,
                    node.style.transform_translate_x,
                    node.style.transform_translate_y,
                    node.style.transform_scale_x,
                    node.style.transform_scale_y,
                    node.style.transform_rotate_degrees);
                node.style.transform_specified = true;
                node.style.transform_stacking_context =
                    ascii_lower(value) != "none";
            }
            return true;
        } else if (name == "transform-origin") {
            if (!is_inline(inline_transform_origin)) {
                native_document::parse_transform_origin(
                    value,
                    node.style.transform_origin_x,
                    node.style.transform_origin_y);
                node.style.transform_origin_specified = true;
            }
            return true;
        } else if (name == "transition") {
            apply_transition_shorthand(node.style, value);
            return true;
        } else if (name == "transition-property") {
            node.style.mutable_animations().transition_property_value = value;
            configure_style_transitions(node.style);
            return true;
        } else if (name == "transition-duration") {
            node.style.mutable_animations().transition_duration_value = value;
            configure_style_transitions(node.style);
            return true;
        } else if (name == "transition-delay") {
            node.style.mutable_animations().transition_delay_value = value;
            configure_style_transitions(node.style);
            return true;
        } else if (name == "transition-timing-function") {
            node.style.mutable_animations().transition_timing_function_value = value;
            configure_style_transitions(node.style);
            return true;
        } else if (name == "animation") {
            apply_animation_shorthand(node.style, value);
            decision.classification = "partially-supported";
            decision.semantic_slice = "first animation; opacity @keyframes";
            return true;
        } else if (name == "animation-name") {
            node.style.mutable_animations().animation_name_value = value;
            decision.classification = "partially-supported";
            decision.semantic_slice = "first animation; opacity @keyframes";
            return true;
        } else if (name == "animation-duration") {
            node.style.mutable_animations().animation_duration_value = value;
            return true;
        } else if (name == "animation-delay") {
            node.style.mutable_animations().animation_delay_value = value;
            return true;
        } else if (name == "animation-timing-function") {
            node.style.mutable_animations().animation_timing_function_value = value;
            return true;
        } else if (name == "animation-iteration-count") {
            node.style.mutable_animations().animation_iteration_count_value = value;
            return true;
        } else if (property_mask(name) == inline_border
            && is_inline(inline_border)) {
            return true;
        } else if (name == "border") {
            parse_border(node.style.border_top_width, node.style.border_top_rgba,
                node.style.border_top_current_color);
            parse_border(node.style.border_right_width, node.style.border_right_rgba,
                node.style.border_right_current_color);
            parse_border(node.style.border_bottom_width, node.style.border_bottom_rgba,
                node.style.border_bottom_current_color);
            parse_border(node.style.border_left_width, node.style.border_left_rgba,
                node.style.border_left_current_color);
            return true;
        } else if (name == "border-top" || name == "border-block-start") {
            parse_border(node.style.border_top_width, node.style.border_top_rgba,
                node.style.border_top_current_color);
            return true;
        } else if (name == "border-right" || name == "border-inline-end") {
            parse_border(node.style.border_right_width, node.style.border_right_rgba,
                node.style.border_right_current_color);
            return true;
        } else if (name == "border-bottom" || name == "border-block-end") {
            parse_border(node.style.border_bottom_width, node.style.border_bottom_rgba,
                node.style.border_bottom_current_color);
            return true;
        } else if (name == "border-left" || name == "border-inline-start") {
            parse_border(node.style.border_left_width, node.style.border_left_rgba,
                node.style.border_left_current_color);
            return true;
        } else if (name == "border-inline") {
            parse_border(node.style.border_left_width, node.style.border_left_rgba,
                node.style.border_left_current_color);
            parse_border(node.style.border_right_width, node.style.border_right_rgba,
                node.style.border_right_current_color);
            return true;
        } else if (name == "border-block") {
            parse_border(node.style.border_top_width, node.style.border_top_rgba,
                node.style.border_top_current_color);
            parse_border(node.style.border_bottom_width, node.style.border_bottom_rgba,
                node.style.border_bottom_current_color);
            return true;
        } else if (name == "border-color") {
            parse_four_border_values(true);
            return true;
        } else if (name == "border-width") {
            parse_four_border_values(false);
            return true;
        } else if (name == "outline") {
            node.style.outline_width = {};
            node.style.outline_rgba = 0;
            auto ignored_current_color = false;
            parse_border(
                node.style.outline_width,
                node.style.outline_rgba,
                ignored_current_color);
            return true;
        } else if (name == "outline-width") {
            node.style.outline_width = native_document::parse_length(value);
            return true;
        } else if (name == "outline-color") {
            node.style.outline_rgba = native_document::parse_color(value);
            return true;
        } else if (name == "border-style" && value == "none") {
            node.style.border_top_width = {};
            node.style.border_right_width = {};
            node.style.border_bottom_width = {};
            node.style.border_left_width = {};
            return true;
        } else if (name == "border-top-color"
            || name == "border-block-start-color") {
            node.style.border_top_rgba = parse_declared_color(value);
            node.style.border_top_current_color =
                ascii_lower(value) == "currentcolor";
            return true;
        } else if (name == "border-right-color"
            || name == "border-inline-end-color") {
            node.style.border_right_rgba = parse_declared_color(value);
            node.style.border_right_current_color =
                ascii_lower(value) == "currentcolor";
            return true;
        } else if (name == "border-bottom-color"
            || name == "border-block-end-color") {
            node.style.border_bottom_rgba = parse_declared_color(value);
            node.style.border_bottom_current_color =
                ascii_lower(value) == "currentcolor";
            return true;
        } else if (name == "border-left-color"
            || name == "border-inline-start-color") {
            node.style.border_left_rgba = parse_declared_color(value);
            node.style.border_left_current_color =
                ascii_lower(value) == "currentcolor";
            return true;
        } else if (name == "border-inline-color") {
            node.style.border_left_rgba = parse_declared_color(value);
            node.style.border_right_rgba = node.style.border_left_rgba;
            node.style.border_left_current_color =
                ascii_lower(value) == "currentcolor";
            node.style.border_right_current_color =
                node.style.border_left_current_color;
            return true;
        } else if (name == "border-block-color") {
            node.style.border_top_rgba = parse_declared_color(value);
            node.style.border_bottom_rgba = node.style.border_top_rgba;
            node.style.border_top_current_color =
                ascii_lower(value) == "currentcolor";
            node.style.border_bottom_current_color =
                node.style.border_top_current_color;
            return true;
        } else if (name == "border-top-width"
            || name == "border-block-start-width") {
            node.style.border_top_width = native_document::parse_length(value);
            return true;
        } else if (name == "border-right-width"
            || name == "border-inline-end-width") {
            node.style.border_right_width = native_document::parse_length(value);
            return true;
        } else if (name == "border-bottom-width"
            || name == "border-block-end-width") {
            node.style.border_bottom_width = native_document::parse_length(value);
            return true;
        } else if (name == "border-left-width"
            || name == "border-inline-start-width") {
            node.style.border_left_width = native_document::parse_length(value);
            return true;
        } else if (name == "border-inline-width") {
            node.style.border_left_width = native_document::parse_length(value);
            node.style.border_right_width = node.style.border_left_width;
            return true;
        } else if (name == "border-block-width") {
            node.style.border_top_width = native_document::parse_length(value);
            node.style.border_bottom_width = node.style.border_top_width;
            return true;
        }
    return false;
}
} // namespace webscene_native::css
