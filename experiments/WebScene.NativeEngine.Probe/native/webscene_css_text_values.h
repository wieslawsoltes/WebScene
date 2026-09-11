#pragma once
#include "webscene_css_property_mask.h"

namespace webscene_native::css {
inline float inherited_font_size(const dom_node& node)
{
    if (node.tag == "html") return 16.0F;
    for (auto* parent = node.parent; parent != nullptr; parent = parent->parent) {
        if (parent->style.font_size >= 0) return parent->style.font_size;
        if (parent->tag == "html") return 16.0F;
    }
    return 14.0F;
}

inline std::string resolved_font_family(const dom_node& node)
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        if (!current->style.textual().font_family.empty()) {
            return current->style.textual().font_family;
        }
    }
    return "sans-serif";
}

inline std::string resolved_font_smoothing(const dom_node& node)
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        const auto& value = current->style.textual().font_smoothing;
        if (!value.empty() && value != "inherit" && value != "unset") return value;
    }
    return "auto";
}

inline int32_t resolved_font_weight(const dom_node& node)
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        if (current->style.font_weight > 0) return current->style.font_weight;
    }
    return 400;
}

inline float resolved_letter_spacing(const dom_node& node)
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        if (current->style.letter_spacing_specified) return current->style.letter_spacing;
    }
    return 0;
}

inline float resolved_word_spacing(const dom_node& node)
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        if (current->style.word_spacing_specified) return current->style.word_spacing;
    }
    return 0;
}

inline float resolved_declared_font_size(const dom_node& node, const std::string& value)
{
    if (value == "inherit" || value == "unset") return -1.0F;
    if (value == "initial" || value == "revert") return 14.0F;
    const auto parsed = native_document::parse_length(value);
    if (parsed.unit == length_unit::em) {
        return std::max(0.0F, parsed.value * inherited_font_size(node));
    }
    if (parsed.unit == length_unit::rem) {
        const auto root_size = node.tag == "html" ? 16.0F : document_root_font_size(node);
        return std::max(0.0F, parsed.value * root_size);
    }
    if (parsed.unit == length_unit::percent) {
        return std::max(0.0F,
            parsed.value * inherited_font_size(node) / 100.0F + parsed.pixel_offset);
    }
    return std::max(0.0F, parsed.value);
}

inline float resolved_declared_line_height(
    const dom_node& node,
    const std::string& value,
    float font_size)
{
    // -1 means inherit/unspecified; -2 is the specified initial `normal`
    // value and must stop inheritance at this element.
    if (value == "inherit" || value == "unset") return -1.0F;
    if (value == "normal" || value == "initial" || value == "revert") return -2.0F;
    const auto parsed = native_document::parse_length(value);
    if (parsed.unit == length_unit::em) {
        return std::max(0.0F, parsed.value * font_size);
    }
    if (parsed.unit == length_unit::rem) {
        const auto root_size = document_root_font_size(node);
        return std::max(0.0F, parsed.value * root_size);
    }
    if (parsed.unit == length_unit::percent) {
        return std::max(0.0F,
            parsed.value * font_size / 100.0F + parsed.pixel_offset);
    }
    const auto parsed_value = std::max(0.0F, parsed.value);
    const auto has_explicit_unit = std::any_of(
        value.begin(),
        value.end(),
        [](unsigned char character) { return std::isalpha(character) || character == '%'; });
    // Preserve a <number> until used-value resolution. It inherits as a
    // multiplier, not as the declaring element's pixel line-height.
    return !has_explicit_unit
        ? -3.0F - parsed_value
        : parsed_value;
}

struct parsed_font_shorthand final {
    float font_size{14.0F};
    float line_height{-2.0F};
    std::string line_height_token{"normal"};
    int32_t font_weight{400};
    std::string font_family;
    bool complete{true};
};

inline std::optional<parsed_font_shorthand> parse_font_shorthand(
    const dom_node& node,
    const std::string& value)
{
    const auto lowercase = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return text;
    };
    std::istringstream stream(value);
    std::vector<std::string> tokens;
    for (std::string token; stream >> token;) tokens.push_back(std::move(token));
    if (tokens.empty()) return std::nullopt;

    const auto looks_like_size = [](std::string_view token) {
        if (token == "xx-small" || token == "x-small" || token == "small"
            || token == "medium" || token == "large" || token == "x-large"
            || token == "xx-large" || token == "smaller" || token == "larger") {
            return true;
        }
        const auto slash = token.find('/');
        if (slash != std::string_view::npos) token = token.substr(0, slash);
        return !token.empty()
            && (std::isdigit(static_cast<unsigned char>(token.front()))
                || token.front() == '.');
    };

    auto size_index = tokens.size();
    for (size_t index = 0; index < tokens.size(); ++index) {
        const auto& token=tokens[index];
        // Numeric weight precedes the mandatory font-size. Its digits do not
        // make it a unitless size; retain it for the weight pass below.
        if(token.size()==3U && token.front()>='1' && token.front()<='9'
            && token[1]=='0' && token[2]=='0') continue;
        if (looks_like_size(tokens[index])) {
            size_index = index;
            break;
        }
    }
    if (size_index == tokens.size()) return std::nullopt;

    parsed_font_shorthand result;
    for (size_t index = 0; index < size_index; ++index) {
        const auto token = lowercase(tokens[index]);
        if (token == "bold" || token == "bolder") result.font_weight = 700;
        else if (token == "normal") continue;
        else if (token.size() == 3U && token.front() >= '1' && token.front() <= '9'
            && token[1] == '0' && token[2] == '0') {
            result.font_weight = std::atoi(token.c_str());
        } else {
            // Style, stretch, and variant tokens are accepted so their size,
            // line-height, weight, and family companions still take effect,
            // but those additional font axes are not represented natively.
            result.complete = false;
        }
    }

    auto size_token = tokens[size_index];
    std::string line_height_token;
    if (const auto slash = size_token.find('/'); slash != std::string::npos) {
        line_height_token = size_token.substr(slash + 1U);
        size_token.resize(slash);
    }
    const auto size_keyword = lowercase(size_token);
    if (size_keyword == "medium") result.font_size = 14.0F;
    else if (size_keyword == "small") result.font_size = 12.0F;
    else if (size_keyword == "large") result.font_size = 18.0F;
    else if (size_keyword == "x-small") result.font_size = 10.0F;
    else if (size_keyword == "xx-small") result.font_size = 9.0F;
    else if (size_keyword == "x-large") result.font_size = 24.0F;
    else if (size_keyword == "xx-large") result.font_size = 32.0F;
    else if (size_keyword == "smaller") result.font_size = inherited_font_size(node) * 0.8F;
    else if (size_keyword == "larger") result.font_size = inherited_font_size(node) * 1.2F;
    else result.font_size = resolved_declared_font_size(node, size_token);

    auto family_index = size_index + 1U;
    if (line_height_token.empty() && family_index < tokens.size()
        && tokens[family_index] == "/") {
        ++family_index;
        if (family_index < tokens.size()) line_height_token = tokens[family_index++];
    }
    if (!line_height_token.empty()) {
        result.line_height_token = line_height_token;
        result.line_height = resolved_declared_line_height(
            node,
            line_height_token,
            result.font_size);
    }
    for (auto index = family_index; index < tokens.size(); ++index) {
        if (!result.font_family.empty()) result.font_family.push_back(' ');
        result.font_family += tokens[index];
    }
    if (result.font_family.empty()) return std::nullopt;
    return result;
}

template<typename Decision,typename Protected>
bool apply_text_value(dom_node& node,const std::string& name,const std::string& value,
    Decision& decision,Protected&& is_inline)
{
    if (name == "color" && !is_inline(inline_color)) {
            const auto parsed = native_document::parse_color(value);
            if (is_explicit_color_token(value, parsed)) {
                node.style.foreground_rgba = parsed;
            }
        } else if (name == "fill" && !is_inline(inline_svg_fill)) {
            node.style.mutable_textual().svg_fill =
                value == "inherit" || value == "unset"
                ? std::string{} : value;
            decision.classification = "partially-supported";
            decision.semantic_slice = "solid SVG paint, none, and currentColor";
        } else if (name == "stroke" && !is_inline(inline_svg_stroke)) {
            node.style.mutable_textual().svg_stroke =
                value == "inherit" || value == "unset"
                ? std::string{} : value;
            decision.classification = "partially-supported";
            decision.semantic_slice = "solid SVG paint, none, and currentColor";
        } else if (name == "stroke-width" && !is_inline(inline_svg_stroke_width)) {
            std::string resolved=value;
            if(value=="initial") resolved="1";
            else if(value=="inherit" || value=="unset") {
                resolved="1";
                for(auto* parent=node.parent;parent;parent=parent->parent) {
                    if(!parent->style.textual().svg_stroke_width.empty()) {
                        resolved=parent->style.textual().svg_stroke_width;break;
                    }
                    const auto attribute=parent->attributes.find("stroke-width");
                    if(attribute!=parent->attributes.end()) {resolved=attribute->second;break;}
                }
            }
            char* end=nullptr;
            const auto numeric=std::strtof(resolved.c_str(),&end);
            const std::string_view unit(end);
            if(end==resolved.c_str() || !std::isfinite(numeric) || numeric<0 ||
               (!unit.empty() && unit!="px" && unit!="%")) {
                decision.classification="unsupported";
                decision.semantic_slice="nonnegative SVG numbers, px, percentages and inheritance";
            } else node.style.mutable_textual().svg_stroke_width=resolved;
        } else if (name == "text-anchor" && !is_inline(inline_svg_text_anchor)) {
            if (value == "start" || value == "middle" || value == "end") {
                node.style.mutable_textual().svg_text_anchor = value;
            } else if (value == "initial") {
                node.style.mutable_textual().svg_text_anchor = "start";
            } else if (value == "inherit" || value == "unset") {
                node.style.mutable_textual().svg_text_anchor.clear();
            }
        } else if (name == "cursor" && !is_inline(inline_cursor)) {
            node.style.mutable_textual().cursor =
                value == "initial" ? "auto" : value;
            decision.classification = "partially-supported";
            decision.semantic_slice = "host cursor projection for common keyword cursors";
        } else if (name == "font") {
            if (value == "inherit" || value == "unset") {
                node.style.font_size = -1;
                node.style.line_height = -1;
                node.style.font_weight = 0;
                node.style.mutable_textual().font_family.clear();
                decision.classification = "partially-supported";
                decision.semantic_slice =
                    "inherit/unset for size, line-height, weight, and family";
            } else if (const auto font = parse_font_shorthand(node, value); font.has_value()) {
                if (!is_inline(inline_font_size)) node.style.font_size = font->font_size;
                if (!is_inline(inline_line_height)) node.style.line_height = font->line_height;
                if (!is_inline(inline_font_weight)) node.style.font_weight = font->font_weight;
                if (!is_inline(inline_font_family)) {
                    node.style.mutable_textual().font_family =
                        font->font_family;
                }
                decision.classification = "partially-supported";
                decision.semantic_slice = font->complete
                    ? "font-size, line-height, weight, and family"
                    : "font-size, line-height, weight, and family; style/variant/stretch retained without native shaping";
            } else {
                decision.classification = "unsupported";
                decision.semantic_slice = "unparsed font shorthand";
            }
        } else if (name == "font-size" && !is_inline(inline_font_size)) {
            node.style.font_size = resolved_declared_font_size(node, value);
        } else if (name == "font-family" && !is_inline(inline_font_family)) {
            node.style.mutable_textual().font_family =
                value == "inherit" || value == "unset"
                ? std::string{} : value;
        } else if ((name == "-webkit-font-smoothing" || name == "webkit-font-smoothing")
            && !is_inline(inline_font_smoothing)) {
            node.style.mutable_textual().font_smoothing =
                value == "inherit" || value == "unset"
                ? std::string{} : value;
        } else if (name == "font-weight" && !is_inline(inline_font_weight)) {
            node.style.font_weight = value == "inherit" || value == "unset" ? 0
                : value == "bold" ? 700
                : value == "normal" ? 400
                : std::max(1, static_cast<int>(std::lround(
                    native_document::parse_length(value).value)));
        } else if (name == "letter-spacing" && !is_inline(inline_letter_spacing)) {
            node.style.letter_spacing_specified = value != "inherit" && value != "unset";
            const auto length = native_document::parse_length(value);
            const auto font_size = node.style.font_size >= 0
                ? node.style.font_size : inherited_font_size(node);
            node.style.letter_spacing = value == "normal" ? 0
                : length.unit == length_unit::em ? length.value * font_size
                : length.unit == length_unit::rem ? length.value * 14.0F
                : length.value;
        } else if (name == "word-spacing" && !is_inline(inline_word_spacing)) {
            node.style.word_spacing_specified = value != "inherit" && value != "unset";
            const auto length = native_document::parse_length(value);
            const auto font_size = node.style.font_size >= 0
                ? node.style.font_size : inherited_font_size(node);
            node.style.word_spacing = value == "normal" ? 0
                : length.unit == length_unit::em ? length.value * font_size
                : length.unit == length_unit::rem ? length.value * 14.0F
                : length.value;
        } else if (name == "line-height" && !is_inline(inline_line_height)) {
            const auto font_size = node.style.font_size >= 0
                ? node.style.font_size : inherited_font_size(node);
            node.style.line_height = resolved_declared_line_height(node, value, font_size);
        } else if (name == "text-align" && !is_inline(inline_text_align)) {
            node.style.mutable_textual().text_align = value;
        } else if (name == "text-transform") {
            node.style.mutable_textual().text_transform =
                value == "inherit" || value == "unset"
                ? std::string{} : value;
            if (value != "none" && value != "uppercase" && value != "lowercase"
                && value != "capitalize" && value != "inherit" && value != "unset") {
                decision.classification = "unsupported";
                decision.semantic_slice = "ASCII none, uppercase, lowercase, and capitalize";
            } else {
                decision.classification = "partially-supported";
                decision.semantic_slice = "ASCII none, uppercase, lowercase, and capitalize";
            }
        } else if (name == "white-space" && !is_inline(inline_white_space)) {
            node.style.mutable_textual().white_space =
                value == "inherit" || value == "unset"
                ? std::string{} : value;
        } else if (name == "list-style-position") {
            node.style.mutable_textual().list_style_position = value;
        } else if (name == "list-style-type") {
            node.style.mutable_textual().list_style_type = value;
        } else if (name == "list-style") {
            std::istringstream stream(value);
            std::string token;
            while (stream >> token) {
                if (token == "inside" || token == "outside") {
                    node.style.mutable_textual().list_style_position = token;
                } else if (token == "none" || token == "decimal"
                    || token == "decimal-leading-zero" || token == "disc"
                    || token == "circle" || token == "square") {
                    node.style.mutable_textual().list_style_type = token;
                }
            }
        } else { return false; }
        return true;
}
} // namespace webscene_native::css
