#pragma once
#include "webscene_css_box_values.h"
#include "webscene_css_selectors.h"

namespace webscene_native::css {
struct property_result {
    std::string classification = "supported";
    std::string semantic_slice;
};

inline std::string decode_content(std::string value)
    {
        if (value == "none" || value == "normal") return {};
        if (value.size() >= 2U
            && (value.front() == '\'' || value.front() == '"')
            && value.back() == value.front()) {
            value = value.substr(1U, value.size() - 2U);
        }
        std::string result;
        for (size_t index = 0; index < value.size(); ++index) {
            if (value[index] != '\\' || index + 1U >= value.size()) {
                result.push_back(value[index]);
                continue;
            }
            size_t end = index + 1U;
            while (end < value.size() && end - index <= 6U
                && std::isxdigit(static_cast<unsigned char>(value[end]))) ++end;
            if (end > index + 1U) {
                const auto codepoint = static_cast<uint32_t>(std::strtoul(
                    value.substr(index + 1U, end - index - 1U).c_str(), nullptr, 16));
                append_utf8_codepoint(result, codepoint);
                if (end < value.size() && std::isspace(static_cast<unsigned char>(value[end]))) ++end;
                index = end - 1U;
            } else {
                result.push_back(value[++index]);
            }
        }
        return result;
    }

inline property_result apply_pseudo_value(node_style::pseudo_element& pseudo,
    uint32_t parent_foreground, const std::string& name, const std::string& value)
{
    property_result decision;
        if (name == "content") {
            pseudo.generated = value != "none" && value != "normal";
            pseudo.content = decode_content(value);
        } else if (name == "display") {
            pseudo.display_none = value == "none";
            pseudo.display = value == "block" ? display_mode::block
                : value == "flex" ? display_mode::flex
                : value == "inline-flex" ? display_mode::inline_flex
                : value == "grid" ? display_mode::grid
                : value == "inline-grid" ? display_mode::inline_grid
                : value == "table" ? display_mode::table
                : value == "inline-table" ? display_mode::inline_table
                : value == "inline-block" ? display_mode::inline_block
                : display_mode::inline_flow;
        } else if (name == "align-self") {
            pseudo.align_self_specified = value != "auto";
            pseudo.align_self = value == "center" ? align_mode::center
                : value == "flex-start" || value == "start" ? align_mode::start
                : value == "flex-end" || value == "end" ? align_mode::end
                : align_mode::stretch;
        } else if (name == "visibility") {
            pseudo.visibility_hidden = value == "hidden" || value == "collapse";
        } else if (name == "position") {
            pseudo.position = value == "absolute" ? position_mode::absolute
                : value == "fixed" ? position_mode::fixed
                : value == "sticky" ? position_mode::sticky
                : value == "relative" ? position_mode::relative
                : position_mode::normal;
        } else if (name == "z-index") {
            pseudo.z_index = value == "auto" ? 0 : std::atoi(value.c_str());
        } else if (name == "inset") {
            std::vector<css_length> values;
            std::istringstream stream(value);
            for (std::string token; stream >> token;) {
                values.push_back(native_document::parse_length(token));
            }
            if (!values.empty()) {
                pseudo.top = values[0];
                pseudo.right = values.size() > 1 ? values[1] : values[0];
                pseudo.bottom = values.size() > 2 ? values[2] : values[0];
                pseudo.left = values.size() > 3 ? values[3]
                    : values.size() > 1 ? values[1] : values[0];
            }
        } else if (name == "width") pseudo.width = native_document::parse_length(value);
        else if (name == "height") pseudo.height = native_document::parse_length(value);
        else if (name == "left" || name == "inset-inline-start") {
            pseudo.left = native_document::parse_length(value);
        } else if (name == "right" || name == "inset-inline-end") {
            pseudo.right = native_document::parse_length(value);
        } else if (name == "top" || name == "inset-block-start") {
            pseudo.top = native_document::parse_length(value);
        } else if (name == "bottom" || name == "inset-block-end") {
            pseudo.bottom = native_document::parse_length(value);
        } else if (name == "margin") {
            std::vector<css_length> values;
            std::istringstream stream(value);
            for (std::string token; stream >> token;) {
                values.push_back(native_document::parse_length(token));
            }
            if (!values.empty()) {
                pseudo.margin_top = values[0];
                pseudo.margin_right = values.size() > 1 ? values[1] : values[0];
                pseudo.margin_bottom = values.size() > 2 ? values[2] : values[0];
                pseudo.margin_left = values.size() > 3 ? values[3]
                    : values.size() > 1 ? values[1] : values[0];
            }
        } else if (name == "margin-left" || name == "margin-inline-start") {
            pseudo.margin_left = native_document::parse_length(value);
        } else if (name == "margin-right" || name == "margin-inline-end") {
            pseudo.margin_right = native_document::parse_length(value);
        } else if (name == "margin-top" || name == "margin-block-start") {
            pseudo.margin_top = native_document::parse_length(value);
        } else if (name == "margin-bottom" || name == "margin-block-end") {
            pseudo.margin_bottom = native_document::parse_length(value);
        } else if (name == "padding") {
            std::vector<css_length> values;
            std::istringstream stream(value);
            for (std::string token; stream >> token;) {
                values.push_back(native_document::parse_length(token));
            }
            if (!values.empty()) {
                pseudo.padding_top = values[0];
                pseudo.padding_right = values.size() > 1 ? values[1] : values[0];
                pseudo.padding_bottom = values.size() > 2 ? values[2] : values[0];
                pseudo.padding_left = values.size() > 3 ? values[3]
                    : values.size() > 1 ? values[1] : values[0];
            }
        } else if (name == "padding-inline" || name == "padding-block") {
            const auto tokens = split_value_tokens(value);
            if (!tokens.empty() && tokens.size() <= 2U) {
                const auto& first = tokens[0];
                const auto& second = tokens.size() == 2U ? tokens[1] : tokens[0];
                const auto start = native_document::parse_length(first);
                const auto end = second.empty()
                    ? start : native_document::parse_length(second);
                if (name == "padding-inline") {
                    pseudo.padding_left = start;
                    pseudo.padding_right = end;
                } else {
                    pseudo.padding_top = start;
                    pseudo.padding_bottom = end;
                }
            }
        } else if (name == "padding-left" || name == "padding-inline-start") {
            pseudo.padding_left = native_document::parse_length(value);
        } else if (name == "padding-right" || name == "padding-inline-end") {
            pseudo.padding_right = native_document::parse_length(value);
        } else if (name == "padding-top" || name == "padding-block-start") {
            pseudo.padding_top = native_document::parse_length(value);
        } else if (name == "padding-bottom" || name == "padding-block-end") {
            pseudo.padding_bottom = native_document::parse_length(value);
        } else if (name == "box-sizing") {
            pseudo.border_box = value == "border-box";
        } else if (canonical_property_name(name).starts_with("border")
            && name.find("radius") == std::string::npos) {
            node_style border_style{};
            border_style.foreground_rgba = (pseudo.foreground_rgba & 0xFFU) != 0U
                ? pseudo.foreground_rgba : parent_foreground;
            border_style.border_left_width = pseudo.border_left_width;
            border_style.border_top_width = pseudo.border_top_width;
            border_style.border_right_width = pseudo.border_right_width;
            border_style.border_bottom_width = pseudo.border_bottom_width;
            border_style.border_left_rgba = pseudo.border_left_rgba;
            border_style.border_top_rgba = pseudo.border_top_rgba;
            border_style.border_right_rgba = pseudo.border_right_rgba;
            border_style.border_bottom_rgba = pseudo.border_bottom_rgba;
            border_style.border_left_current_color = pseudo.border_left_current_color;
            border_style.border_top_current_color = pseudo.border_top_current_color;
            border_style.border_right_current_color = pseudo.border_right_current_color;
            border_style.border_bottom_current_color = pseudo.border_bottom_current_color;
            if (apply_border_declaration(border_style, name, value)) {
                pseudo.border_left_width = border_style.border_left_width;
                pseudo.border_top_width = border_style.border_top_width;
                pseudo.border_right_width = border_style.border_right_width;
                pseudo.border_bottom_width = border_style.border_bottom_width;
                pseudo.border_left_rgba = border_style.border_left_rgba;
                pseudo.border_top_rgba = border_style.border_top_rgba;
                pseudo.border_right_rgba = border_style.border_right_rgba;
                pseudo.border_bottom_rgba = border_style.border_bottom_rgba;
                pseudo.border_left_current_color = border_style.border_left_current_color;
                pseudo.border_top_current_color = border_style.border_top_current_color;
                pseudo.border_right_current_color = border_style.border_right_current_color;
                pseudo.border_bottom_current_color = border_style.border_bottom_current_color;
            } else {
                decision.classification = "unsupported";
            }
        } else if (apply_corner_radius_declaration(
            name,
            value,
            pseudo)) {
        } else if (name == "outline") {
            pseudo.outline_width = {};
            pseudo.outline_rgba = 0;
            auto remaining = value;
            auto color_start = remaining.find("rgba(");
            if (color_start == std::string::npos) color_start = remaining.find("rgb(");
            if (color_start != std::string::npos) {
                const auto color_end = remaining.find(')', color_start);
                if (color_end != std::string::npos) {
                    pseudo.outline_rgba = native_document::parse_color(
                        remaining.substr(color_start, color_end - color_start + 1U));
                    remaining.erase(color_start, color_end - color_start + 1U);
                }
            }
            std::istringstream stream(remaining);
            for (std::string token; stream >> token;) {
                const auto lower = ascii_lower(token);
                if (lower == "none") {
                    pseudo.outline_width = {};
                    pseudo.outline_rgba = 0;
                } else if (lower == "thin") {
                    pseudo.outline_width = {1, length_unit::pixels};
                } else if (lower == "medium") {
                    pseudo.outline_width = {3, length_unit::pixels};
                } else if (lower == "thick") {
                    pseudo.outline_width = {5, length_unit::pixels};
                } else if (!token.empty()
                    && (std::isdigit(static_cast<unsigned char>(token.front()))
                        || token.front() == '.' || token.front() == '-')) {
                    pseudo.outline_width = native_document::parse_length(token);
                } else {
                    const auto color = native_document::parse_color(token);
                    if (color != 0U || lower == "transparent") {
                        pseudo.outline_rgba = color;
                    }
                }
            }
        } else if (name == "outline-width") {
            pseudo.outline_width = native_document::parse_length(value);
        } else if (name == "outline-color") {
            pseudo.outline_rgba = native_document::parse_color(value);
        } else if (name == "background-image") {
            pseudo.background_image.image_value = value;
            pseudo.background_image.image_markup.clear();
            pseudo.background_image.image_view_box.clear();
            if (value != "none" && !(ascii_lower(value).starts_with("linear-gradient(") || ascii_lower(value).starts_with("radial-gradient("))) {
                decision.classification = "unsupported";
                decision.semantic_slice = "single linear-gradient layer";
            }
        } else if (name == "background-repeat") {
            pseudo.background_image.repeat = value;
        } else if (name == "background-position") {
            pseudo.background_image.position_value = value;
        } else if (name == "background-size") {
            pseudo.background_image.size_value = value;
        } else if (name == "background" || name == "background-color") {
            pseudo.background_current_color = value == "currentcolor" || value == "currentColor";
            pseudo.background_rgba = pseudo.background_current_color
                ? 0U
                : native_document::parse_color(value);
            if (name == "background") {
                const auto lower = ascii_lower(value);
                const auto gradient_start = std::min(lower.find("linear-gradient("), lower.find("radial-gradient("));
                if (gradient_start == std::string::npos) {
                    pseudo.background_image.image_value = "none";
                } else {
                    auto depth = 0;
                    auto gradient_end = value.size();
                    for (auto index = gradient_start; index < value.size(); ++index) {
                        if (value[index] == '(') ++depth;
                        else if (value[index] == ')' && --depth == 0) {
                            gradient_end = index + 1U;
                            break;
                        }
                    }
                    pseudo.background_image.image_value = value.substr(
                        gradient_start, gradient_end - gradient_start);
                }
                pseudo.background_image.image_markup.clear();
                pseudo.background_image.image_view_box.clear();
            }
        } else if (name == "opacity") {
            pseudo.opacity = std::clamp(std::strtof(value.c_str(), nullptr), 0.0F, 1.0F);
        } else if (name == "color") {
            pseudo.foreground_rgba = native_document::parse_color(value);
        } else if (name == "font-size") {
            pseudo.font_size = std::max(0.0F, native_document::parse_length(value).value);
        } else if (name == "line-height") {
            if (value == "normal" || value == "initial" || value == "revert") {
                pseudo.line_height = -2.0F;
            } else if (value != "inherit" && value != "unset") {
                pseudo.line_height = std::max(0.0F, native_document::parse_length(value).value);
            } else {
                decision.classification = "partially-supported";
                decision.semantic_slice = "explicit numeric lengths";
            }
        } else {
            decision.classification = "unsupported";
        }
    return decision;
}
} // namespace webscene_native::css
