#pragma once
#include "webscene_css_state.h"
#include <span>

namespace webscene_native::css {
inline void sort_candidates(std::span<const css_rule> rules,std::vector<size_t>& candidates)
    {
        // Declarations are applied in ascending precedence; later declarations
        // therefore replace earlier ones while equal specificity retains source order.
        std::sort(
            candidates.begin(),
            candidates.end(),
            [&rules](size_t left, size_t right) {
                const auto left_specificity = rules[left].specificity();
                const auto right_specificity = rules[right].specificity();
                return left_specificity != right_specificity
                    ? left_specificity < right_specificity
                    : left < right;
            });
    }


// Preserve the ordinary runtime's root-variable policy; per-element custom
// property inheritance and substitution are handled by cascade application.
inline void rebuild_root_variables(std::span<const css_rule> rules,
    std::unordered_map<std::string,std::string>& variables,
    std::unordered_set<std::string>& important) {
        variables.clear();
        important.clear();
        for (const auto& rule : rules) {
            if (!rule.media_matches || rule.shadow_scope_root_id != 0U
                || (rule.selector() != ":root" && rule.selector() != "html")) continue;
            for (const auto& declaration : rule.declarations()) {
                if (!declaration.name.starts_with("--")) continue;
                if (!declaration.important
                    && important.contains(declaration.name)) continue;
                variables[declaration.name] = declaration.value;
                if (declaration.important) important.insert(declaration.name);
                else important.erase(declaration.name);
            }
        }
}
} // namespace webscene_native::css
