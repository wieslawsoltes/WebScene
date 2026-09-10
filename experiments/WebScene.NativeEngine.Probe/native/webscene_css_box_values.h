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
} // namespace webscene_native::css
