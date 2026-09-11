#pragma once
#include "webscene_css_selectors.h"
#include "webscene_native_form_state.h"
#include "webscene_css_declarations.h"
#include <charconv>
#include <sstream>

namespace webscene_native::css {
inline std::string trim_value(std::string_view text) { return std::string(trim_css_view(text)); }
// Native matching primitives shared with the ordinary WebScene CSS runtime.
inline bool attribute_matches(
        const dom_node& node,
        std::string_view condition)
    {
        const auto equal = condition.find('=');
        auto name_end = equal;
        auto attribute_operator = std::string_view{};
        if (equal != std::string::npos) {
            if (equal > 0U && (condition[equal - 1U] == '~'
                || condition[equal - 1U] == '|'
                || condition[equal - 1U] == '^'
                || condition[equal - 1U] == '$'
                || condition[equal - 1U] == '*')) {
                name_end = equal - 1U;
                attribute_operator = condition.substr(equal - 1U, 2U);
            } else {
                attribute_operator = condition.substr(equal, 1U);
            }
        }
        const auto raw_name = trim_value(std::string_view(condition).substr(0, name_end));
        size_t name_cursor = 0U;
        auto name = read_css_identifier(raw_name, name_cursor);
        if (name.empty() || name_cursor != raw_name.size()) return false;
        if (!node.xml_mode) name = ascii_lower(name);
        const auto attribute = node.attributes.find(name);
        if (attribute == node.attributes.end()) return false;
        if (equal == std::string::npos) return true;

        auto wanted = trim_value(std::string_view(condition).substr(equal + 1U));
        if (wanted.size() >= 2U && (wanted.front() == '\'' || wanted.front() == '"')) {
            wanted = wanted.substr(1U, wanted.size() - 2U);
        }
        const auto& actual = attribute->second;
        if (attribute_operator == "=") return actual == wanted;
        if (wanted.empty() && (attribute_operator == "^="
            || attribute_operator == "$=" || attribute_operator == "*=")) {
            return false;
        }
        if (attribute_operator == "^=") return actual.starts_with(wanted);
        if (attribute_operator == "$=") return actual.ends_with(wanted);
        if (attribute_operator == "*=") return actual.find(wanted) != std::string::npos;
        if (attribute_operator == "|=") {
            return actual == wanted || actual.starts_with(wanted + "-");
        }
        if (attribute_operator == "~=") {
            std::istringstream words(actual);
            for (std::string word; words >> word;) {
                if (word == wanted) return true;
            }
            return false;
        }
        return true;
    }

inline bool nth_matches(
        std::string_view argument,
        int one_based_position)
    {
        auto expression = ascii_lower(trim_css_view(argument));
        std::erase_if(expression, [](unsigned char character) {
            return std::isspace(character);
        });
        auto coefficient = 0;
        auto offset = 0;
        const auto parse_integer = [](std::string_view value, int& result) {
            if (value.empty()) return false;
            if (value.front() == '+') value.remove_prefix(1U);
            if (value.empty()) return false;
            const auto parsed = std::from_chars(
                value.data(), value.data() + value.size(), result);
            return parsed.ec == std::errc{}
                && parsed.ptr == value.data() + value.size();
        };
        if (expression == "odd") {
            coefficient = 2;
            offset = 1;
        } else if (expression == "even") {
            coefficient = 2;
        } else if (const auto n = expression.find('n'); n != std::string::npos) {
            const auto coefficient_text = std::string_view(expression).substr(0, n);
            if (coefficient_text.empty() || coefficient_text == "+") {
                coefficient = 1;
            } else if (coefficient_text == "-") {
                coefficient = -1;
            } else if (!parse_integer(coefficient_text, coefficient)) {
                return false;
            }
            const auto offset_text = std::string_view(expression).substr(n + 1U);
            if (!offset_text.empty() && !parse_integer(offset_text, offset)) return false;
        } else if (!parse_integer(expression, offset)) {
            return false;
        }
        if (coefficient == 0) return one_based_position == offset;
        const auto difference = static_cast<int64_t>(one_based_position) - offset;
        return !((coefficient > 0 && difference < 0)
            || (coefficient < 0 && difference > 0))
            && difference % coefficient == 0;
    }


inline bool is_actually_disabled(const native_document& document,const dom_node& node)
    {
        if (node.attributes.contains("disabled")) return true;
        if (node.tag == "option") {
            for (auto* ancestor = document.dom_parent(node); ancestor != nullptr;
                ancestor = document.dom_parent(*ancestor)) {
                if ((ancestor->tag == "optgroup" || ancestor->tag == "select")
                    && ancestor->attributes.contains("disabled")) return true;
                if (ancestor->tag == "select") break;
            }
        }
        if (node.tag != "button" && node.tag != "input"
            && node.tag != "select" && node.tag != "textarea") return false;
        for (auto* fieldset = document.dom_parent(node); fieldset != nullptr;
            fieldset = document.dom_parent(*fieldset)) {
            if (fieldset->tag != "fieldset"
                || !fieldset->attributes.contains("disabled")) continue;
            const dom_node* first_legend = nullptr;
            for (const auto* child : fieldset->children) {
                if (child != nullptr && child->tag == "legend") {
                    first_legend = child;
                    break;
                }
            }
            auto inside_first_legend = false;
            for (auto* ancestor = &node; ancestor != fieldset;
                ancestor = document.dom_parent(*ancestor)) {
                if (ancestor == first_legend) {
                    inside_first_legend = true;
                    break;
                }
            }
            if (!inside_first_legend) return true;
        }
        return false;
    }

// Host-independent interaction state. The host supplies focus modality; the
// document defines ancestry (including event ancestry for focus-within).
struct interaction_state final {
    const dom_node* hovered{};
    const dom_node* focused{};
    bool focus_visible{};
};
inline bool interaction_matches(const native_document& document,const dom_node& node,
    std::string_view pseudo,const interaction_state& state,bool text_control) {
    if(pseudo=="hover") {
        for(auto* current=state.hovered;current;current=current->parent)
            if(current==&node) return true;
        return false;
    }
    if(pseudo=="focus") return state.focused==&node;
    if(pseudo=="focus-visible") return state.focused==&node && (state.focus_visible || text_control);
    if(pseudo=="focus-within") {
        for(auto* current=state.focused;current;current=document.event_parent(*current))
            if(current==&node) return true;
    }
    return false;
}
inline bool language_matches(const native_document& document,const dom_node& node,std::string_view argument) {
                auto wanted = ascii_lower(trim_value(argument));
                if (wanted.size() >= 2U
                    && (wanted.front() == '\'' || wanted.front() == '"')
                    && wanted.back() == wanted.front()) {
                    wanted = wanted.substr(1U, wanted.size() - 2U);
                } else {
                    size_t language_cursor = 0U;
                    auto decoded = read_css_identifier(wanted, language_cursor);
                    if (decoded.empty() || language_cursor != wanted.size()) return false;
                    wanted = ascii_lower(decoded);
                }
                auto matched = false;
                for (auto* language_node = &node; language_node != nullptr;
                    language_node = document.dom_parent(*language_node)) {
                    auto language = language_node->attributes.find("lang");
                    if (language == language_node->attributes.end()) {
                        language = language_node->attributes.find("xml:lang");
                    }
                    if (language == language_node->attributes.end()) continue;
                    const auto actual = ascii_lower(language->second);
                    matched = actual == wanted || actual.starts_with(wanted + "-");
                    break;
                }
                return matched;
}
inline bool direction_matches(const native_document& document,const dom_node& node,std::string_view argument) {
                auto wanted = ascii_lower(trim_value(argument));
                if (wanted.size() >= 2U
                    && (wanted.front() == '\'' || wanted.front() == '"')
                    && wanted.back() == wanted.front()) {
                    wanted = wanted.substr(1U, wanted.size() - 2U);
                }
                auto actual = std::string("ltr");
                for (auto* direction_node = &node; direction_node != nullptr;
                    direction_node = document.dom_parent(*direction_node)) {
                    const auto authored = direction_node->attributes.find("dir");
                    if (authored == direction_node->attributes.end()) continue;
                    const auto candidate = ascii_lower(trim_value(authored->second));
                    if (candidate == "ltr" || candidate == "rtl") {
                        actual = candidate;
                        break;
                    }
                }
                if ((wanted != "ltr" && wanted != "rtl") || wanted != actual) {
                    return false;
                }
                return true;
}
inline bool checked_matches(const dom_node& node) {
    const auto type=node.attributes.find("type");
    const bool checkable=node.tag=="input" && type!=node.attributes.end() &&
        (type->second=="checkbox" || type->second=="radio");
    if(checkable) return node.form_control().checkedness_initialized ?
        node.form_control().checkedness : node.attributes.contains("checked");
    return node.tag=="option" && forms::option_is_selected(const_cast<dom_node&>(node));
}
inline bool target_matches(const dom_node& node,std::string_view hash) {
    if(hash.starts_with('#')) hash.remove_prefix(1);
    return !hash.empty() && node.id_attribute==hash;
}
inline const dom_node* previous_element_sibling(const dom_node& node)
    {
        if (node.parent == nullptr) return nullptr;
        const auto position = std::find(
            node.parent->children.begin(),
            node.parent->children.end(),
            &node);
        if (position == node.parent->children.begin()
            || position == node.parent->children.end()) {
            return nullptr;
        }
        auto sibling = position;
        while (sibling != node.parent->children.begin()) {
            --sibling;
            if (*sibling != nullptr && !(*sibling)->tag.starts_with('#')) {
                return *sibling;
            }
        }
        return nullptr;
    }

template<typename CompoundMatcher>
inline bool selector_matches(const native_document& document,const dom_node& node,
    const compiled_css_selector& selector,size_t component,const dom_node* scope_root,
    const CompoundMatcher& match_compound)
    {
        if (selector.compounds.empty()
            || selector.compiled_compounds.size() != selector.compounds.size()
            || component >= selector.compiled_compounds.size()
            || !match_compound(
                node,
                selector.compiled_compounds[component],
                scope_root)) {
            return false;
        }
        if (component == 0U) return true;
        if (selector.combinators.size() < component) return false;

        const auto combinator = selector.combinators[component - 1U];
        if (combinator == '>') {
            const auto* parent = document.dom_parent(node);
            return parent != nullptr
                && selector_matches(document,
                    *parent,
                    selector,
                    component - 1U,
                    scope_root,match_compound);
        }
        if (combinator == '+') {
            const auto* sibling = previous_element_sibling(node);
            return sibling != nullptr
                && selector_matches(document,
                    *sibling,
                    selector,
                    component - 1U,
                    scope_root,match_compound);
        }
        if (combinator == '~') {
            for (auto* sibling = previous_element_sibling(node); sibling != nullptr;
                sibling = previous_element_sibling(*sibling)) {
                if (selector_matches(document,
                        *sibling,
                        selector,
                        component - 1U,
                        scope_root,match_compound)) {
                    return true;
                }
            }
            return false;
        }
        for (auto* ancestor = document.dom_parent(node); ancestor != nullptr;
            ancestor = document.dom_parent(*ancestor)) {
            if (selector_matches(document,
                    *ancestor,
                    selector,
                    component - 1U,
                    scope_root,match_compound)) {
                return true;
            }
        }
        return false;
    }

} // namespace webscene_native::css
