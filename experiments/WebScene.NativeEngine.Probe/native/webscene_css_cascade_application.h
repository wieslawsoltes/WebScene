#pragma once
#include "webscene_css_property_mask.h"
#include <array>
#include <span>

namespace webscene_native::css {
// Input rules are already matched and sorted in ascending cascade precedence.
// Preserve runtime ordering: custom properties, ordinary values, dependent inline
// values, then inline transitions. The callback applies one declaration and origin.
template<typename Apply>
void apply_matched_declarations(dom_node& node,std::span<const css_rule* const> matched_rules,
    Apply&& apply)
{
        // CSS custom properties are cascaded before dependent declarations are
        // computed. Applying each rule eagerly made a base button height resolve
        // with the default medium size before its later `.small-*` rule set the
        // size token.
        for (const auto* rule : matched_rules) {
            for (const auto& declaration : rule->declarations()) {
                if (declaration.name.starts_with("--")) {
                    apply(declaration,false);
                }
            }
        }
        for (const auto* rule : matched_rules) {
            for (const auto& declaration : rule->declarations()) {
                if (declaration.name.starts_with("--")) continue;
                apply(declaration,false);
            }
        }
        // An inline declaration can be authored before the stylesheet that
        // defines one of its var() references is connected. Its first
        // computed-value attempt is then invalid, but the authored tokens must
        // remain live and be recomputed when the variable becomes available.
        // Replay variable-dependent inline declarations and explicit dimension
        // inheritance, whose computed values can change with the parent.
        // Temporarily removing the inline guard lets a declaration update its own
        // computed value while the important-origin mask still prevents a
        // normal inline declaration from overriding an author !important rule.
        for (const auto& [name, value] : node.authored_style().declarations) {
            const auto inherited_dimension = (name == "width" || name == "height")
                && trim_css_view(value) == "inherit";
            if (name.starts_with("--")
                || (value.find("var(") == std::string::npos && !inherited_dimension)) continue;
            const auto property_mask = css::property_mask(name);
            const auto retained_inline_mask = node.style.inline_property_mask;
            node.style.inline_property_mask &= ~property_mask;
            apply(
                {name, value,
                    node.authored_style().important_declarations.contains(name)}, true);
            node.style.inline_property_mask = retained_inline_mask;
        }
        // Inline transition declarations are stored separately from the hot
        // style object. Recascade clears cold animation state before applying
        // stylesheet rules, so restore the inline origin afterward while
        // retaining per-longhand !important precedence.
        constexpr std::array transition_properties{
            "transition",
            "transition-property",
            "transition-duration",
            "transition-delay",
            "transition-timing-function"
        };
        for (const auto* property : transition_properties) {
            const auto authored = node.authored_style().declarations.find(property);
            if (authored == node.authored_style().declarations.end()) continue;
            const auto inline_important =
                node.authored_style().important_declarations.contains(property);
            const auto property_mask = css::property_mask(property);
            if (!inline_important
                && (node.style.important_property_mask & property_mask) != 0U) continue;
            apply({property, authored->second, inline_important},false);
        }
}
} // namespace webscene_native::css
