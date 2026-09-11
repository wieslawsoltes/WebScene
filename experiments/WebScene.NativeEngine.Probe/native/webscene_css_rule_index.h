#pragma once
#include "webscene_css_selectors.h"

namespace webscene_native::css {
// Index a selector by a necessary subject key. Matching still verifies the full
// selector. The caller owns storage and must rebuild it when rules are removed.
inline void index_selector(size_t index, const std::string& selector,
    css_index_string_map<std::vector<size_t>>& css_rules_by_id,
    css_index_string_map<std::vector<size_t>>& css_rules_by_class,
    css_index_string_map<std::vector<size_t>>& css_rules_by_tag,
    css_index_string_map<std::vector<size_t>>& css_rules_by_attribute,
    std::vector<size_t>& css_focus_rules,
    std::vector<size_t>& unindexed_css_rules,
    css_index_string_set& css_descendant_attribute_dependencies)
    {
        size_t compound_begin = 0;
        int bracket_depth = 0;
        int parenthesis_depth = 0;
        for (size_t offset = selector.size(); offset > 0; --offset) {
            const auto position = offset - 1U;
            const auto character = selector[position];
            if (character == ']') ++bracket_depth;
            else if (character == '[') --bracket_depth;
            else if (character == ')') ++parenthesis_depth;
            else if (character == '(') --parenthesis_depth;
            else if (bracket_depth == 0 && parenthesis_depth == 0
                && (std::isspace(static_cast<unsigned char>(character))
                    || character == '>' || character == '+' || character == '~')) {
                compound_begin = position + 1U;
                break;
            }
        }
        // An attribute in an ancestor compound can change which descendants
        // match. Attribute selectors in the rightmost compound only require
        // recascading the mutated subject.
        for (size_t open = 0;
             (open = selector.find('[', open)) != std::string_view::npos;) {
            auto cursor = open + 1U;
            while (cursor < selector.size()
                && std::isspace(static_cast<unsigned char>(selector[cursor]))) {
                ++cursor;
            }
            const auto start = cursor;
            while (cursor < selector.size()
                && (std::isalnum(static_cast<unsigned char>(selector[cursor]))
                    || selector[cursor] == '-' || selector[cursor] == '_')) {
                ++cursor;
            }
            if (cursor > start
                && (open < compound_begin
                    || selector.find(":has(") != std::string_view::npos)) {
                css_descendant_attribute_dependencies.emplace(
                    selector.substr(start, cursor - start));
            }
            open = cursor;
        }
        auto compound = std::string_view(selector).substr(compound_begin);
        while (!compound.empty()
            && std::isspace(static_cast<unsigned char>(compound.front()))) {
            compound.remove_prefix(1U);
        }

        size_t pseudo = compound.size();
        bracket_depth = 0;
        for (size_t position = 0; position < compound.size(); ++position) {
            if (compound[position] == '[') ++bracket_depth;
            else if (compound[position] == ']') --bracket_depth;
            else if (compound[position] == ':' && bracket_depth == 0) {
                pseudo = position;
                break;
            }
        }
        compound = compound.substr(0, pseudo);

        const auto token_end = [&](size_t start) {
            auto end = start;
            while (end < compound.size()
                && compound[end] != '.' && compound[end] != '#'
                && compound[end] != '[' && compound[end] != ':') ++end;
            return end;
        };
        bracket_depth = 0;
        for (size_t position = 0; position < compound.size(); ++position) {
            if (compound[position] == '[') ++bracket_depth;
            else if (compound[position] == ']') --bracket_depth;
            else if (bracket_depth == 0
                && (compound[position] == '#' || compound[position] == '.')) {
                const auto end = token_end(position + 1U);
                const auto key = std::string(compound.substr(position + 1U, end - position - 1U));
                if (!key.empty()) {
                    auto& target = compound[position] == '#'
                        ? css_rules_by_id[key]
                        : css_rules_by_class[key];
                    target.push_back(index);
                    return;
                }
            }
        }
        if (!compound.empty() && std::isalpha(static_cast<unsigned char>(compound.front()))) {
            size_t end = 1U;
            while (end < compound.size()
                && (std::isalnum(static_cast<unsigned char>(compound[end]))
                    || compound[end] == '-')) ++end;
            css_rules_by_tag[std::string(compound.substr(0, end))].push_back(index);
            return;
        }
        if (const auto open = compound.find('['); open != std::string_view::npos) {
            auto cursor = open + 1U;
            while (cursor < compound.size()
                && std::isspace(static_cast<unsigned char>(compound[cursor]))) {
                ++cursor;
            }
            const auto start = cursor;
            while (cursor < compound.size()
                && (std::isalnum(static_cast<unsigned char>(compound[cursor]))
                    || compound[cursor] == '-' || compound[cursor] == '_')) {
                ++cursor;
            }
            if (cursor > start) {
                css_rules_by_attribute[
                    std::string(compound.substr(start, cursor - start))].push_back(index);
                return;
            }
        }
        if (trim_css_view(selector) == ":root") {
            css_rules_by_tag["html"].push_back(index);
            return;
        }
        if (trim_css_view(selector) == ":focus") {
            css_focus_rules.push_back(index);
            return;
        }
        unindexed_css_rules.push_back(index);
    }
} // namespace webscene_native::css
