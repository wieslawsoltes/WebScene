#pragma once
#include "webscene_css_pseudo_values.h"
#include "webscene_css_variables.h"

namespace webscene_native::css {
inline int split_pseudo_element_selector(const std::string& selector, std::string& origin)
    {
        const auto split_suffix = [&](std::string_view suffix, int kind) {
            if (!selector.ends_with(suffix)) return 0;
            origin = trim_value(std::string_view(selector).substr(0, selector.size() - suffix.size()));
            return kind;
        };
        if (const auto kind = split_suffix("::before", 1); kind != 0) return kind;
        if (const auto kind = split_suffix("::after", 2); kind != 0) return kind;
        if (const auto kind = split_suffix("::backdrop", 7); kind != 0) return kind;
        if (const auto kind = split_suffix("::-webkit-scrollbar-thumb", 4); kind != 0) return kind;
        if (const auto kind = split_suffix("::-webkit-scrollbar-track", 5); kind != 0) return kind;
        if (const auto kind = split_suffix("::-webkit-scrollbar-corner", 6); kind != 0) return kind;
        if (const auto kind = split_suffix("::-webkit-scrollbar", 3); kind != 0) return kind;
        if (const auto kind = split_suffix(":before", 1); kind != 0) return kind;
        return split_suffix(":after", 2);
    }
inline void apply_scrollbar_declaration(
        dom_node& node,
        int pseudo_kind,
        const css_declaration& declaration,
        const std::unordered_map<std::string,std::string>& variables)
    {
        auto& style = node.style;
        const auto contains_variable = declaration.value.find("var(") != std::string::npos;
        auto resolved_value = std::string{};
        const auto& value = contains_variable
            ? (resolved_value = resolve_value(node,declaration.value,variables))
            : declaration.value;
        if (value.empty() && contains_variable) return;
        const auto lower = ascii_lower(trim_value(value));
        auto& scrollbar = style.mutable_scrollbar();
        if (pseudo_kind == 3) {
            if (declaration.name == "display") {
                if (style.scrollbar_visibility_important && !declaration.important) return;
                style.scrollbar_hidden = lower == "none";
                style.scrollbar_visibility_important = declaration.important;
            } else if (declaration.name == "width") {
                const auto length = native_document::parse_length(value);
                if (length.unit == length_unit::pixels) {
                    scrollbar.width = std::max(0.0F, length.value);
                    scrollbar.overlay_inset = 0;
                }
            } else if (declaration.name == "height") {
                const auto length = native_document::parse_length(value);
                if (length.unit == length_unit::pixels) {
                    scrollbar.height = std::max(0.0F, length.value);
                    scrollbar.overlay_inset = 0;
                }
            }
            return;
        }
        if (pseudo_kind == 4) {
            if (declaration.name == "background" || declaration.name == "background-color") {
                scrollbar.thumb_rgba = lower == "initial" ? 0U
                    : native_document::parse_color(value);
            } else if (declaration.name == "border-radius") {
                scrollbar.thumb_radius = std::max(
                    0.0F, native_document::parse_length(value).value);
            } else if (declaration.name == "border" || declaration.name == "border-width") {
                std::istringstream stream(value);
                for (std::string token; stream >> token;) {
                    if (!token.empty() && (std::isdigit(static_cast<unsigned char>(token.front()))
                            || token.front() == '.')) {
                        scrollbar.thumb_border_width = std::max(
                            0.0F, native_document::parse_length(token).value);
                        break;
                    }
                }
            }
            return;
        }
        if (pseudo_kind == 5) {
            if (declaration.name == "background" || declaration.name == "background-color") {
                scrollbar.track_rgba = lower == "initial" ? 0U
                    : native_document::parse_color(value);
            } else if (declaration.name == "border-radius") {
                scrollbar.track_radius = std::max(
                    0.0F, native_document::parse_length(value).value);
            }
        }
    }

template<typename Decision,typename Resolved>
void apply_pseudo_declaration(dom_node& node,node_style::pseudo_element& pseudo,
    const css_declaration& declaration,const std::unordered_map<std::string,std::string>& variables,
    Decision& decision,Resolved&& on_resolved)
{
        const auto contains_variable = declaration.value.find("var(") != std::string::npos;
        auto resolved_value = std::string{};
        const auto& value = contains_variable
            ? (resolved_value = resolve_value(node,declaration.value,variables))
            : declaration.value;
        if (value.empty() && contains_variable) {
            decision.classification = "invalid-authoring";
            decision.semantic_slice = "unresolved custom property at computed-value time";
            return;
        }
        const auto& name = declaration.name;
        on_resolved(contains_variable);
        const auto result = css::apply_pseudo_value(
            pseudo, node.style.foreground_rgba, name, value);
        decision.classification = result.classification;
        decision.semantic_slice = result.semantic_slice;
}
} // namespace webscene_native::css
