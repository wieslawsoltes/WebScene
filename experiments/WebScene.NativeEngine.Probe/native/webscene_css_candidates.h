#pragma once
#include "webscene_css_state.h"

namespace webscene_native::css {
// Collect candidate indices, not matches. The caller sorts by cascade precedence
// and removes duplicates before matching. AppendClass preserves host lookup policy.
template<class AppendClass>
inline std::vector<size_t> collect_candidates(const dom_node& node, bool focused,
    const css_index_string_map<std::vector<size_t>>& css_rules_by_tag,
    const css_index_string_map<std::vector<size_t>>& css_rules_by_id,
    const css_index_string_map<std::vector<size_t>>& css_rules_by_attribute,
    const std::vector<size_t>& css_focus_rules,
    const std::vector<size_t>& unindexed_css_rules, AppendClass&& append_class)
{
        std::vector<size_t> candidates = unindexed_css_rules;
        const auto append_candidates = [&](const auto& index, const auto& key) {
            const auto match = index.find(key);
            if (match != index.end()) {
                candidates.insert(candidates.end(), match->second.begin(), match->second.end());
            }
        };
        append_candidates(css_rules_by_tag, node.tag);
        const auto has_explicit_document_element = node.tag == "body"
            && std::any_of(node.children.begin(), node.children.end(), [](const auto* child) {
                return child != nullptr && child->tag == "html";
            });
        if (node.tag == "body"
            && (node.parent == nullptr || node.parent->tag == "iframe")
            && !has_explicit_document_element) {
            append_candidates(css_rules_by_tag, "html");
        }
        if (!node.id_attribute.empty()) append_candidates(css_rules_by_id, node.id_attribute);
        for (const auto& [name, value] : node.attributes) {
            static_cast<void>(value);
            append_candidates(css_rules_by_attribute, name);
        }
        if (focused) {
            candidates.insert(
                candidates.end(),
                css_focus_rules.begin(),
                css_focus_rules.end());
        }
        const std::string_view classes(node.class_name);
        size_t class_cursor = 0;
        while (class_cursor < classes.size()) {
            while (class_cursor < classes.size()
                && std::isspace(static_cast<unsigned char>(classes[class_cursor]))) ++class_cursor;
            const auto start = class_cursor;
            while (class_cursor < classes.size()
                && !std::isspace(static_cast<unsigned char>(classes[class_cursor]))) ++class_cursor;
            if (class_cursor > start) {
                append_class(candidates, classes.substr(start, class_cursor - start));
            }
        }
        return candidates;
}
} // namespace webscene_native::css
