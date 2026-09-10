#pragma once
#include "webscene_css_matching.h"

namespace webscene_native::css {
// Existing parsed-CSS substitution semantics, shared with the runtime adapter.
// Compiled-only style expressions continue to use their typed evaluator.
inline std::string resolve_value(const dom_node& node,std::string value,
    const std::unordered_map<std::string,std::string>& root_variables)
    {
        // Component-library dimensions frequently expand through several
        // custom-property layers and then repeat those variables across five
        // size terms. A fixed substitution limit truncated large component stylesheets'
        // button height before it reached a numeric calc() expression.
        for (int depth = 0; depth < 128; ++depth) {
            const auto start = value.find("var(");
            if (start == std::string::npos) break;
            size_t close = std::string::npos;
            int parenthesis_depth = 1;
            for (size_t index = start + 4U; index < value.size(); ++index) {
                if (value[index] == '(') ++parenthesis_depth;
                else if (value[index] == ')' && --parenthesis_depth == 0) {
                    close = index;
                    break;
                }
            }
            if (close == std::string::npos) break;
            const auto content = value.substr(start + 4U, close - start - 4U);
            size_t comma = std::string::npos;
            parenthesis_depth = 0;
            for (size_t index = 0; index < content.size(); ++index) {
                if (content[index] == '(') ++parenthesis_depth;
                else if (content[index] == ')') --parenthesis_depth;
                else if (content[index] == ',' && parenthesis_depth == 0) {
                    comma = index;
                    break;
                }
            }
            const auto name = trim_value(std::string_view(content).substr(0, comma));
            const std::string* known_value = nullptr;
            for (auto* current = &node; current != nullptr; current = current->parent) {
                const auto& custom = current->style.custom_properties().values;
                const auto known = custom.find(name);
                if (known != custom.end()) {
                    known_value = &known->second;
                    break;
                }
            }
            if (known_value == nullptr) {
                const auto known = root_variables.find(name);
                if (known != root_variables.end()) known_value = &known->second;
            }
            const auto replacement = known_value != nullptr
                ? *known_value
                : comma == std::string::npos ? std::string{} : trim_value(std::string_view(content).substr(comma + 1U));
            value.replace(start, close - start + 1U, replacement);
        }
        return trim_value(value);
    }

} // namespace webscene_native::css
