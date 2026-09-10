#pragma once
#include "webscene_css_property_mask.h"
#include "webscene_shadow_value.h"

namespace webscene_native::css {
inline bool apply_box_shadow_value(
        node_style& style,
        const std::string& authored_value,
        bool& complete)
    {
        shadow_value_builder::clear(style);
        complete = true;

        const auto value = trim_value(authored_value);
        if (value.empty() || value == "none") return true;

        auto first_shadow_end = value.size();
        auto parenthesis_depth = 0;
        for (size_t index = 0; index < value.size(); ++index) {
            if (value[index] == '(') ++parenthesis_depth;
            else if (value[index] == ')' && parenthesis_depth > 0) --parenthesis_depth;
            else if (value[index] == ',' && parenthesis_depth == 0) {
                first_shadow_end = index;
                complete = false;
                break;
            }
        }
        const auto shadow = trim_value(std::string_view(value).substr(0, first_shadow_end));
        std::vector<std::string> tokens;
        size_t token_start = std::string::npos;
        parenthesis_depth = 0;
        for (size_t index = 0; index <= shadow.size(); ++index) {
            const auto at_end = index == shadow.size();
            const auto character = at_end ? ' ' : shadow[index];
            if (!at_end && character == '(') ++parenthesis_depth;
            else if (!at_end && character == ')' && parenthesis_depth > 0) --parenthesis_depth;
            const auto separator = parenthesis_depth == 0
                && std::isspace(static_cast<unsigned char>(character));
            if (!separator && token_start == std::string::npos) token_start = index;
            if (separator && token_start != std::string::npos) {
                tokens.emplace_back(shadow.substr(token_start, index - token_start));
                token_start = std::string::npos;
            }
        }

        shadow_value_builder builder;
        for (const auto& token : tokens) {
            const auto lower = ascii_lower(token);
            if (lower == "inset") { builder.inset(); continue; }
            if (lower == "currentcolor") { builder.color(std::nullopt); continue; }
            const auto starts_numeric = !token.empty()
                && (std::isdigit(static_cast<unsigned char>(token.front()))
                    || token.front() == '.' || token.front() == '-' || token.front() == '+');
            if (!starts_numeric) {
                builder.color(native_document::parse_color(token));
            } else {
                const auto length = native_document::parse_length(token);
                if (length.unit != length_unit::pixels) complete = false;
                builder.length(length);
            }
        }
        return builder.apply(style);
    }

inline std::optional<std::string> first_css_url(const std::string& value)
    {
        const auto lower = ascii_lower(value);
        const auto start = lower.find("url(");
        if (start == std::string::npos) return std::nullopt;
        const auto end = value.find(')', start + 4U);
        if (end == std::string::npos) return std::nullopt;
        auto url = trim_value(value.substr(start + 4U, end - start - 4U));
        if (url.size() >= 2U
            && ((url.front() == '\'' && url.back() == '\'')
                || (url.front() == '"' && url.back() == '"'))) {
            url = url.substr(1U, url.size() - 2U);
        }
        return url.empty() ? std::nullopt : std::optional<std::string>{std::move(url)};
    }

inline void apply_background_position(node_style& style, const std::string& value)
    {
        auto& background = style.mutable_background_image();
        std::istringstream stream(value);
        std::string first;
        std::string second;
        stream >> first >> second;
        if (first.empty()) return;
        const auto vertical = [](const std::string& token) {
            return token == "top" || token == "bottom";
        };
        const auto horizontal = [](const std::string& token) {
            return token == "left" || token == "right";
        };
        if (vertical(first)) {
            background.position_y = first;
            background.position_x = second.empty() ? "center" : second;
        } else {
            background.position_x = first;
            background.position_y = second.empty()
                ? (horizontal(first) ? "center" : "0%")
                : second;
        }
    }

// Resource loading remains with the host; all style mutation is native.
template<typename Decision,typename Protected,typename LoadSvg>
bool apply_paint_value(dom_node& node,const std::string& name,const std::string& value,
    Decision& decision,Protected&& is_inline,LoadSvg&& load_svg)
{
    if (name == "box-shadow" && !is_inline(inline_box_shadow)) {
            auto complete = true;
            if (!apply_box_shadow_value(node.style, value, complete)) {
                decision.classification = "unsupported";
                decision.semantic_slice = "single inset or outer shadow with two to four pixel lengths";
            } else if (!complete) {
                decision.classification = "partially-supported";
                decision.semantic_slice = "first shadow with pixel lengths";
            }
        } else if (name == "background-image"
            && !is_inline(inline_background_image)) {
            auto& background = node.style.mutable_background_image();
            background.image_value = value;
            background.image_markup.clear();
            background.image_view_box.clear();
            if (value == "none") return true;
            if (ascii_lower(value).starts_with("linear-gradient(") || ascii_lower(value).starts_with("radial-gradient(")) {
                decision.classification = "supported";
                decision.semantic_slice =
                    "linear-gradient color stops, percentages, angles, and side/corner directions";
                return true;
            }
            const auto url = first_css_url(value);
            if (!url.has_value()) {
                decision.classification = "unsupported";
                decision.semantic_slice = "single URL-backed SVG layer";
                return true;
            }
            std::string markup;
            std::string resolved_url;
            std::string view_box;
            if (!load_svg(*url,markup,resolved_url,view_box)) {
                decision.classification = "unsupported";
                decision.semantic_slice = "URL-backed SVG resource load failed";
                return true;
            }
            if (view_box.empty()) {
                decision.classification = "unsupported";
                decision.semantic_slice =
                    "SVG images with an explicit viewBox or numeric width and height";
                return true;
            }
            background.image_value = "url(\"" + resolved_url + "\")";
            background.image_markup = std::move(markup);
            background.image_view_box = view_box;
            decision.classification = "partially-supported";
            decision.semantic_slice =
                "first URL-backed SVG layer with explicit viewBox or numeric width and height";
        } else if (name == "background-repeat") {
            node.style.mutable_background_image().repeat = value;
            if (value != "no-repeat") {
                decision.classification = "partially-supported";
                decision.semantic_slice = "no-repeat";
            }
        } else if (name == "background-position") {
            node.style.mutable_background_image().position_value = value;
            apply_background_position(node.style, value);
            decision.classification = "partially-supported";
            decision.semantic_slice = "two-value keywords, percentages, and pixel lengths";
        } else if (name == "background-size") {
            auto& background = node.style.mutable_background_image();
            background.size_value = value;
            background.size_x.clear();
            background.size_y.clear();
            std::istringstream stream(value);
            stream >> background.size_x >> background.size_y;
            if (background.size_x.empty()) background.size_x = "auto";
            if (background.size_y.empty()) {
                background.size_y = background.size_x == "cover"
                    || background.size_x == "contain"
                    ? background.size_x : "auto";
            }
            decision.classification = "partially-supported";
            decision.semantic_slice = "cover, contain, auto, percentages, and pixel lengths";
        } else if (name == "background" || name == "background-color") {
            if (!is_inline(inline_background)) {
                node.style.background_current_color =
                    ascii_lower(value) == "currentcolor";
                const auto parsed = native_document::parse_color(value);
                if (name == "background" || is_explicit_color_token(value, parsed)) {
                    node.style.background_rgba = parsed;
                }
            }
            if (name == "background" && !is_inline(inline_background_image)) {
                const auto lower = ascii_lower(value);
                const auto gradient = std::min(lower.find("linear-gradient("), lower.find("radial-gradient("));
                if (gradient != std::string::npos) {
                    auto& background = node.style.mutable_background_image();
                    background.image_value = value.substr(gradient);
                    background.image_markup.clear();
                    background.image_view_box.clear();
                    decision.classification = "supported";
                    decision.semantic_slice =
                        "linear-gradient color stops, percentages, angles, and side/corner directions";
                } else {
                    node.style.clear_background_image();
                }
            }
        } else { return false; }
        return true;
}
} // namespace webscene_native::css
