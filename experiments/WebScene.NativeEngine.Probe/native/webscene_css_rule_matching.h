#pragma once
#include "webscene_css_pseudo_application.h"
#include <span>

namespace webscene_native::css {
struct rule_matches {
    std::vector<const css_rule*> ordinary;
    std::vector<std::pair<int,const css_rule*>> pseudo;
};
// Candidates are valid indices in ascending precedence order. Results borrow the
// rule vector and must be consumed before its owner mutates/replaces that storage.
template<typename MatchSelector,typename MatchRule>
rule_matches match_candidates(native_document& document,const dom_node& node,
    std::span<const css_rule> rules,std::span<const size_t> candidates,
    MatchSelector&& match_selector,MatchRule&& match_rule)
{
        const auto* node_shadow_root = document.containing_shadow_root(node);
        const auto rule_is_in_scope = [&](const css_rule& rule) {
            if (rule.shadow_scope_root_id == 0U) return node_shadow_root == nullptr;
            auto* scope_root = document.find_by_native_id(rule.shadow_scope_root_id);
            if (scope_root == nullptr) return false;
            const auto* scope = document.shadow_dom(*scope_root);
            const auto host_selector = trim_css_view(rule.selector()) == ":host";
            if (host_selector) return scope != nullptr && scope->host == &node;
            if (scope != nullptr && scope->host == &node) return false;
            return node_shadow_root == scope_root;
        };
        rule_matches result;
        for (const auto index : candidates) {
            const auto& rule = rules[index];
            if (!rule.media_matches) continue;
            if (!rule_is_in_scope(rule)) continue;
            std::string pseudo_origin;
            const auto pseudo_kind = split_pseudo_element_selector(rule.selector(), pseudo_origin);
            if (pseudo_kind != 0) {
                if (!pseudo_origin.empty() && match_selector(node,pseudo_origin)) {
                    result.pseudo.emplace_back(pseudo_kind, &rule);
                }
                continue;
            }
            if (trim_css_view(rule.selector()) != ":host"
                && !match_rule(node,rule)) continue;
            result.ordinary.push_back(&rule);
        }
    return result;
}
} // namespace webscene_native::css
