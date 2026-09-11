#pragma once
#include "webscene_css_matching.h"

namespace webscene_native::css {
inline std::string canonical_property_name(std::string_view name)
{
    if (name.starts_with("--")) return std::string(name);
    if (name == "cssFloat") return "float";
    std::string result;
    result.reserve(name.size() + 4U);
    for (const auto character : name) {
        if (std::isupper(static_cast<unsigned char>(character))) {
            result.push_back('-');
            result.push_back(static_cast<char>(std::tolower(
                static_cast<unsigned char>(character))));
        } else {
            result.push_back(character);
        }
    }
    if (result == "grid-gap") return "gap";
    if (result == "grid-row-gap") return "row-gap";
    if (result == "grid-column-gap") return "column-gap";
    return result;
}

inline std::vector<std::string> split_value_tokens(std::string_view value)
    {
        std::vector<std::string> result;
        size_t start = std::string_view::npos;
        int depth = 0;
        for (size_t index = 0; index <= value.size(); ++index) {
            const auto character = index < value.size() ? value[index] : ' ';
            if (character == '(') ++depth;
            else if (character == ')' && depth > 0) --depth;
            if (std::isspace(static_cast<unsigned char>(character)) && depth == 0) {
                if (start != std::string_view::npos) {
                    result.emplace_back(value.substr(start, index - start));
                    start = std::string_view::npos;
                }
            } else if (start == std::string_view::npos) {
                start = index;
            }
        }
        return result;
    }

inline uint8_t margin_sides(std::string_view raw_name)
    {
        const auto name = canonical_property_name(raw_name);
        if (name == "margin") return 15U;
        if (name == "margin-inline") return 5U;
        if (name == "margin-block") return 10U;
        if (name == "margin-left" || name == "margin-inline-start") return 1U;
        if (name == "margin-top" || name == "margin-block-start") return 2U;
        if (name == "margin-right" || name == "margin-inline-end") return 4U;
        if (name == "margin-bottom" || name == "margin-block-end") return 8U;
        return 0U;
    }

inline bool apply_margin_declaration(
        node_style& style,
        std::string_view raw_name,
        const std::string& value,
        uint8_t allowed_sides = 15U)
    {
        const auto name = canonical_property_name(raw_name);
        const auto assign = [&](css_length& length, bool& automatic, const std::string& token) {
            const auto side = &length == &style.margin_left ? 1U
                : &length == &style.margin_top ? 2U
                : &length == &style.margin_right ? 4U : 8U;
            if ((allowed_sides & side) == 0U) return;
            length = native_document::parse_length(token);
            automatic = ascii_lower(trim_value(token)) == "auto";
        };
        if (name == "margin") {
            const auto tokens = split_value_tokens(value);
            if (tokens.empty() || tokens.size() > 4U) return true;
            const auto& top = tokens[0];
            const auto& right = tokens.size() > 1U ? tokens[1] : tokens[0];
            const auto& bottom = tokens.size() > 2U ? tokens[2] : tokens[0];
            const auto& left = tokens.size() > 3U ? tokens[3]
                : tokens.size() > 1U ? tokens[1] : tokens[0];
            assign(style.margin_top, style.margin_top_auto, top);
            assign(style.margin_right, style.margin_right_auto, right);
            assign(style.margin_bottom, style.margin_bottom_auto, bottom);
            assign(style.margin_left, style.margin_left_auto, left);
            return true;
        }
        if (name == "margin-block" || name == "margin-inline") {
            const auto tokens = split_value_tokens(value);
            if (tokens.empty() || tokens.size() > 2U) return true;
            const auto& start = tokens[0];
            const auto& end = tokens.size() > 1U ? tokens[1] : tokens[0];
            if (name == "margin-block") {
                assign(style.margin_top, style.margin_top_auto, start);
                assign(style.margin_bottom, style.margin_bottom_auto, end);
            } else {
                assign(style.margin_left, style.margin_left_auto, start);
                assign(style.margin_right, style.margin_right_auto, end);
            }
            return true;
        }
        if (name == "margin-left" || name == "margin-inline-start") {
            assign(style.margin_left, style.margin_left_auto, value);
            return true;
        }
        if (name == "margin-right" || name == "margin-inline-end") {
            assign(style.margin_right, style.margin_right_auto, value);
            return true;
        }
        if (name == "margin-top" || name == "margin-block-start") {
            assign(style.margin_top, style.margin_top_auto, value);
            return true;
        }
        if (name == "margin-bottom" || name == "margin-block-end") {
            assign(style.margin_bottom, style.margin_bottom_auto, value);
            return true;
        }
        return false;
    }

inline bool apply_padding_declaration(
        node_style& style,
        std::string_view raw_name,
        const std::string& value)
    {
        const auto name = canonical_property_name(raw_name);
        const auto assign = [](css_length& length, const std::string& token) {
            length = native_document::parse_length(token);
        };
        if (name == "padding") {
            const auto tokens = split_value_tokens(value);
            if (tokens.empty() || tokens.size() > 4U) return true;
            const auto& top = tokens[0];
            const auto& right = tokens.size() > 1U ? tokens[1] : tokens[0];
            const auto& bottom = tokens.size() > 2U ? tokens[2] : tokens[0];
            const auto& left = tokens.size() > 3U ? tokens[3]
                : tokens.size() > 1U ? tokens[1] : tokens[0];
            assign(style.padding_top, top);
            assign(style.padding_right, right);
            assign(style.padding_bottom, bottom);
            assign(style.padding_left, left);
            return true;
        }
        if (name == "padding-block" || name == "padding-inline") {
            const auto tokens = split_value_tokens(value);
            if (tokens.empty() || tokens.size() > 2U) return true;
            const auto& start = tokens[0];
            const auto& end = tokens.size() > 1U ? tokens[1] : tokens[0];
            if (name == "padding-block") {
                assign(style.padding_top, start);
                assign(style.padding_bottom, end);
            } else {
                assign(style.padding_left, start);
                assign(style.padding_right, end);
            }
            return true;
        }
        if (name == "padding-left" || name == "padding-inline-start") {
            assign(style.padding_left, value);
            return true;
        }
        if (name == "padding-right" || name == "padding-inline-end") {
            assign(style.padding_right, value);
            return true;
        }
        if (name == "padding-top" || name == "padding-block-start") {
            assign(style.padding_top, value);
            return true;
        }
        if (name == "padding-bottom" || name == "padding-block-end") {
            assign(style.padding_bottom, value);
            return true;
        }
        return false;
    }

inline bool apply_margin(dom_node& node,const css_declaration& declaration,
    const std::string& value,bool inline_origin=false) {
    const auto& name=declaration.name;
    if (const auto sides=margin_sides(name); sides!=0U) {
            // The coarse inline_margin bit is an invalidation category, not
            // a cascade priority shared by all four longhands.
            uint8_t protected_sides = declaration.important
                ? 0U : node.style.important_margin_sides;
            for (const auto& [authored_name, authored_value] : node.authored_style().declarations) {
                if (inline_origin) continue;
                if (!declaration.important
                    || node.authored_style().important_declarations.contains(authored_name)) {
                    protected_sides |= margin_sides(authored_name);
                }
            }
            const auto allowed = static_cast<uint8_t>(sides & ~protected_sides);
            apply_margin_declaration(node.style, name, value, allowed);
            if (declaration.important && !inline_origin) node.style.important_margin_sides |= allowed;
            return true;
        }
    return false;
}
inline bool is_explicit_color_token(
        std::string_view value,
        uint32_t parsed) noexcept
    {
        if (parsed != 0U) return true;
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) return false;
        const auto last = value.find_last_not_of(" \t\r\n");
        value = value.substr(first, last - first + 1U);
        const auto lower = ascii_lower(std::string(value));
        if (lower == "transparent" || lower == "currentcolor"
            || lower.starts_with("hsl(") || lower.starts_with("hsla(")) return true;
        if (value.front() != '#'
            || (value.size() != 4U && value.size() != 5U
                && value.size() != 7U && value.size() != 9U)) {
            return false;
        }
        return std::all_of(
            value.begin() + 1,
            value.end(),
            [](unsigned char character) { return std::isxdigit(character) != 0; });
    }

inline css_length parse_inset_length(const std::string& value)
    {
        const auto normalized = ascii_lower(value);
        if (normalized == "auto" || normalized == "initial"
            || normalized == "unset" || normalized == "revert"
            || normalized == "revert-layer") {
            return {};
        }
        return native_document::parse_length(value);
    }

inline bool apply_inset_declaration(
        node_style& style,
        std::string_view raw_name,
        const std::string& value)
    {
        if (canonical_property_name(raw_name) != "inset") return false;
        const auto tokens = split_value_tokens(value);
        if (tokens.empty() || tokens.size() > 4U) return true;
        const auto& top = tokens[0];
        const auto& right = tokens.size() > 1U ? tokens[1] : tokens[0];
        const auto& bottom = tokens.size() > 2U ? tokens[2] : tokens[0];
        const auto& left = tokens.size() > 3U ? tokens[3]
            : tokens.size() > 1U ? tokens[1] : tokens[0];
        style.top = parse_inset_length(top);
        style.right = parse_inset_length(right);
        style.bottom = parse_inset_length(bottom);
        style.left = parse_inset_length(left);
        return true;
    }

inline bool apply_border_declaration(
        node_style& style,
        std::string_view raw_name,
        const std::string& value)
    {
        auto name = canonical_property_name(raw_name);
        // The retained box model stores physical sides. Resolve the logical
        // side spellings accepted by the ordinary element cascade before
        // applying generated-element declarations as well. TradingView uses
        // border-inline-start on a zero-sized ::after box to construct its
        // diagonal split-colour swatch.
        if (name == "border-block-start") name = "border-top";
        else if (name == "border-block-end") name = "border-bottom";
        else if (name == "border-inline-start") name = "border-left";
        else if (name == "border-inline-end") name = "border-right";
        else if (name == "border-block-start-width") name = "border-top-width";
        else if (name == "border-block-end-width") name = "border-bottom-width";
        else if (name == "border-inline-start-width") name = "border-left-width";
        else if (name == "border-inline-end-width") name = "border-right-width";
        else if (name == "border-block-start-color") name = "border-top-color";
        else if (name == "border-block-end-color") name = "border-bottom-color";
        else if (name == "border-inline-start-color") name = "border-left-color";
        else if (name == "border-inline-end-color") name = "border-right-color";
        const auto parse_color = [&](const std::string& token) {
            return ascii_lower(token) == "currentcolor"
                ? style.foreground_rgba
                : native_document::parse_color(token);
        };
        const auto parse_width = [](const std::string& token, css_length& width) {
            if (token == "thin") {
                width = {1, length_unit::pixels};
                return true;
            }
            if (token == "medium") {
                width = {3, length_unit::pixels};
                return true;
            }
            if (token == "thick") {
                width = {5, length_unit::pixels};
                return true;
            }
            if (!token.empty()
                && (std::isdigit(static_cast<unsigned char>(token.front()))
                    || token.front() == '.' || token.front() == '-')) {
                width = native_document::parse_length(token);
                return true;
            }
            return false;
        };
        const auto parse_side = [&] (
            css_length& width,
            uint32_t& color,
            bool& current_color) {
            current_color = true;
            color = 0;
            if (value.empty() || value == "none") {
                width = {};
                return;
            }
            auto remaining = value;
            auto color_start = remaining.find("rgba(");
            if (color_start == std::string::npos) color_start = remaining.find("rgb(");
            if (color_start != std::string::npos) {
                const auto color_end = remaining.find(')', color_start);
                if (color_end != std::string::npos) {
                    color = native_document::parse_color(
                        remaining.substr(color_start, color_end - color_start + 1U));
                    current_color = false;
                    remaining.erase(color_start, color_end - color_start + 1U);
                }
            }
            std::istringstream stream(remaining);
            for (std::string token; stream >> token;) {
                if (token == "none") {
                    width = {};
                    color = 0;
                } else if (!parse_width(token, width)
                    && token != "solid" && token != "dashed" && token != "dotted"
                    && token != "double" && token != "hidden") {
                    const auto parsed = parse_color(token);
                    if (is_explicit_color_token(token, parsed)) {
                        color = parsed;
                        current_color = ascii_lower(token) == "currentcolor";
                    }
                }
            }
        };
        const auto assign_widths = [&](const std::vector<std::string>& tokens) {
            if (tokens.empty() || tokens.size() > 4U) return;
            const auto& top = tokens[0];
            const auto& right = tokens.size() > 1U ? tokens[1] : tokens[0];
            const auto& bottom = tokens.size() > 2U ? tokens[2] : tokens[0];
            const auto& left = tokens.size() > 3U ? tokens[3]
                : tokens.size() > 1U ? tokens[1] : tokens[0];
            parse_width(top, style.border_top_width);
            parse_width(right, style.border_right_width);
            parse_width(bottom, style.border_bottom_width);
            parse_width(left, style.border_left_width);
        };
        const auto assign_colors = [&](const std::vector<std::string>& tokens) {
            if (tokens.empty() || tokens.size() > 4U) return;
            const auto& top = tokens[0];
            const auto& right = tokens.size() > 1U ? tokens[1] : tokens[0];
            const auto& bottom = tokens.size() > 2U ? tokens[2] : tokens[0];
            const auto& left = tokens.size() > 3U ? tokens[3]
                : tokens.size() > 1U ? tokens[1] : tokens[0];
            style.border_top_rgba = parse_color(top);
            style.border_right_rgba = parse_color(right);
            style.border_bottom_rgba = parse_color(bottom);
            style.border_left_rgba = parse_color(left);
            style.border_top_current_color = ascii_lower(top) == "currentcolor";
            style.border_right_current_color = ascii_lower(right) == "currentcolor";
            style.border_bottom_current_color = ascii_lower(bottom) == "currentcolor";
            style.border_left_current_color = ascii_lower(left) == "currentcolor";
        };
        if (name == "border") {
            parse_side(style.border_top_width, style.border_top_rgba,
                style.border_top_current_color);
            parse_side(style.border_right_width, style.border_right_rgba,
                style.border_right_current_color);
            parse_side(style.border_bottom_width, style.border_bottom_rgba,
                style.border_bottom_current_color);
            parse_side(style.border_left_width, style.border_left_rgba,
                style.border_left_current_color);
            return true;
        }
        if (name == "border-top") {
            parse_side(style.border_top_width, style.border_top_rgba,
                style.border_top_current_color);
            return true;
        }
        if (name == "border-right") {
            parse_side(style.border_right_width, style.border_right_rgba,
                style.border_right_current_color);
            return true;
        }
        if (name == "border-bottom") {
            parse_side(style.border_bottom_width, style.border_bottom_rgba,
                style.border_bottom_current_color);
            return true;
        }
        if (name == "border-left") {
            parse_side(style.border_left_width, style.border_left_rgba,
                style.border_left_current_color);
            return true;
        }
        if (name == "border-width") {
            if (value.empty()) {
                style.border_top_width = {};
                style.border_right_width = {};
                style.border_bottom_width = {};
                style.border_left_width = {};
            } else {
                assign_widths(split_value_tokens(value));
            }
            return true;
        }
        if (name == "border-color") {
            if (value.empty()) {
                style.border_top_rgba = 0;
                style.border_right_rgba = 0;
                style.border_bottom_rgba = 0;
                style.border_left_rgba = 0;
                style.border_top_current_color = true;
                style.border_right_current_color = true;
                style.border_bottom_current_color = true;
                style.border_left_current_color = true;
            } else if (value.find("rgb(") != std::string::npos
                || value.find("rgba(") != std::string::npos) {
                const auto color = native_document::parse_color(value);
                style.border_top_rgba = color;
                style.border_right_rgba = color;
                style.border_bottom_rgba = color;
                style.border_left_rgba = color;
                style.border_top_current_color = false;
                style.border_right_current_color = false;
                style.border_bottom_current_color = false;
                style.border_left_current_color = false;
            } else {
                assign_colors(split_value_tokens(value));
            }
            return true;
        }
        if (name == "border-style") {
            if (value.empty() || value == "none") {
                style.border_top_width = {};
                style.border_right_width = {};
                style.border_bottom_width = {};
                style.border_left_width = {};
            }
            return true;
        }
        const auto set_width = [&](std::string_view candidate, css_length& width) {
            if (name != candidate) return false;
            if (value.empty()) width = {};
            else parse_width(value, width);
            return true;
        };
        if (set_width("border-top-width", style.border_top_width)
            || set_width("border-right-width", style.border_right_width)
            || set_width("border-bottom-width", style.border_bottom_width)
            || set_width("border-left-width", style.border_left_width)) {
            return true;
        }
        const auto set_color = [&] (
            std::string_view candidate,
            uint32_t& color,
            bool& current_color) {
            if (name != candidate) return false;
            color = value.empty() ? 0 : parse_color(value);
            current_color = value.empty()
                || ascii_lower(value) == "currentcolor";
            return true;
        };
        return set_color("border-top-color", style.border_top_rgba,
                style.border_top_current_color)
            || set_color("border-right-color", style.border_right_rgba,
                style.border_right_current_color)
            || set_color("border-bottom-color", style.border_bottom_rgba,
                style.border_bottom_current_color)
            || set_color("border-left-color", style.border_left_rgba,
                style.border_left_current_color);
    }

inline bool apply_corner_radius_values(
        std::string_view name,
        const std::string& value,
        css_length& top_left,
        css_length& top_right,
        css_length& bottom_right,
        css_length& bottom_left,
        css_length& top_left_y,
        css_length& top_right_y,
        css_length& bottom_right_y,
        css_length& bottom_left_y)
    {
        std::vector<css_length> horizontal;
        std::vector<css_length> vertical;
        auto* target = &horizontal;
        size_t token_start = std::string::npos;
        int parenthesis_depth = 0;
        size_t slash_count = 0U;
        for (size_t index = 0; index <= value.size(); ++index) {
            const auto character = index < value.size() ? value[index] : ' ';
            if (character == '(') ++parenthesis_depth;
            else if (character == ')' && parenthesis_depth > 0) --parenthesis_depth;
            const auto slash = parenthesis_depth == 0 && character == '/';
            const auto separator = parenthesis_depth == 0
                && (std::isspace(static_cast<unsigned char>(character)) || slash);
            if (!separator && token_start == std::string::npos) token_start = index;
            if (separator && token_start != std::string::npos) {
                target->push_back(native_document::parse_length(
                    value.substr(token_start, index - token_start)));
                token_start = std::string::npos;
            }
            if (slash) {
                ++slash_count;
                target = &vertical;
            }
        }

        const auto assign = [&](css_length& horizontal_radius,
                                css_length& vertical_radius) {
            if (horizontal.empty() || horizontal.size() > 2U
                || slash_count != 0U || !vertical.empty()) return;
            horizontal_radius = horizontal[0];
            vertical_radius = horizontal.size() > 1U ? horizontal[1] : horizontal[0];
        };
        if (name == "border-top-left-radius" || name == "border-start-start-radius") {
            assign(top_left, top_left_y);
            return true;
        }
        if (name == "border-top-right-radius" || name == "border-start-end-radius") {
            assign(top_right, top_right_y);
            return true;
        }
        if (name == "border-bottom-right-radius" || name == "border-end-end-radius") {
            assign(bottom_right, bottom_right_y);
            return true;
        }
        if (name == "border-bottom-left-radius" || name == "border-end-start-radius") {
            assign(bottom_left, bottom_left_y);
            return true;
        }
        if (name != "border-radius") return false;
        if (horizontal.empty() || horizontal.size() > 4U || vertical.size() > 4U
            || slash_count > 1U || (slash_count == 1U && vertical.empty())) return true;
        const auto expand = [](const std::vector<css_length>& values,
                               css_length& first,
                               css_length& second,
                               css_length& third,
                               css_length& fourth) {
            first = values[0];
            second = values.size() > 1U ? values[1] : values[0];
            third = values.size() > 2U ? values[2] : values[0];
            fourth = values.size() > 3U ? values[3]
                : values.size() > 1U ? values[1] : values[0];
        };
        expand(horizontal, top_left, top_right, bottom_right, bottom_left);
        if (vertical.empty()) {
            top_left_y = top_left;
            top_right_y = top_right;
            bottom_right_y = bottom_right;
            bottom_left_y = bottom_left;
        } else {
            expand(vertical, top_left_y, top_right_y, bottom_right_y, bottom_left_y);
        }
        return true;
    }

inline bool apply_corner_radius_declaration(
        std::string_view name,
        const std::string& value,
        node_style& style)
    {
        auto top_left_y = style.border_top_left_radius_y();
        auto top_right_y = style.border_top_right_radius_y();
        auto bottom_right_y = style.border_bottom_right_radius_y();
        auto bottom_left_y = style.border_bottom_left_radius_y();
        if (!apply_corner_radius_values(
                name,
                value,
                style.border_top_left_radius,
                style.border_top_right_radius,
                style.border_bottom_right_radius,
                style.border_bottom_left_radius,
                top_left_y,
                top_right_y,
                bottom_right_y,
                bottom_left_y)) {
            return false;
        }
        style.set_vertical_corner_radii(
            top_left_y,
            top_right_y,
            bottom_right_y,
            bottom_left_y);
        return true;
    }

inline bool apply_corner_radius_declaration(
        std::string_view name,
        const std::string& value,
        node_style::pseudo_element& pseudo)
    {
        auto top_left_y = pseudo.elliptical_border_radius
            ? pseudo.border_top_left_radius_y : pseudo.border_top_left_radius;
        auto top_right_y = pseudo.elliptical_border_radius
            ? pseudo.border_top_right_radius_y : pseudo.border_top_right_radius;
        auto bottom_right_y = pseudo.elliptical_border_radius
            ? pseudo.border_bottom_right_radius_y : pseudo.border_bottom_right_radius;
        auto bottom_left_y = pseudo.elliptical_border_radius
            ? pseudo.border_bottom_left_radius_y : pseudo.border_bottom_left_radius;
        if (!apply_corner_radius_values(
                name,
                value,
                pseudo.border_top_left_radius,
                pseudo.border_top_right_radius,
                pseudo.border_bottom_right_radius,
                pseudo.border_bottom_left_radius,
                top_left_y,
                top_right_y,
                bottom_right_y,
                bottom_left_y)) {
            return false;
        }
        pseudo.border_top_left_radius_y = top_left_y;
        pseudo.border_top_right_radius_y = top_right_y;
        pseudo.border_bottom_right_radius_y = bottom_right_y;
        pseudo.border_bottom_left_radius_y = bottom_left_y;
        const auto differs = [](css_length first, css_length second) {
            return first.value != second.value
                || first.unit != second.unit
                || first.pixel_offset != second.pixel_offset;
        };
        pseudo.elliptical_border_radius =
            differs(top_left_y, pseudo.border_top_left_radius)
            || differs(top_right_y, pseudo.border_top_right_radius)
            || differs(bottom_right_y, pseudo.border_bottom_right_radius)
            || differs(bottom_left_y, pseudo.border_bottom_left_radius);
        return true;
    }

inline std::vector<std::string> split_css_component_list(
        std::string_view value,
        char separator)
    {
        std::vector<std::string> result;
        size_t start = 0;
        int depth = 0;
        char quote = 0;
        for (size_t index = 0; index <= value.size(); ++index) {
            const auto character = index < value.size() ? value[index] : separator;
            if (quote != 0) {
                if (character == quote && (index == 0 || value[index - 1] != '\\')) quote = 0;
                continue;
            }
            if (character == '\'' || character == '"') {
                quote = character;
                continue;
            }
            if (character == '(') ++depth;
            else if (character == ')' && depth > 0) --depth;
            if (character == separator && depth == 0) {
                auto component = trim_value(std::string(value.substr(start, index - start)));
                if (!component.empty()) result.push_back(std::move(component));
                start = index + 1U;
            }
        }
        return result;
    }

} // namespace webscene_native::css
