#pragma once
#include "webscene_css_declarations.h"
#include "webscene_css_matching.h"

namespace webscene_native::css {
// Host supplies rule/keyframe storage, media capability inventory and diagnostics.
// This adapter preserves the existing runtime at-rule policy; it is not a full
// implementation of supports, layers, containers or CSS nesting.
template<typename Host>
class stylesheet_sink final : public css_syntax_sink {
public:
    stylesheet_sink(
        Host& owner,
        const std::string& stylesheet_address,
        size_t input_size)
        : owner_(owner), stylesheet_address_(stylesheet_address)
    {
        stack_.reserve(8U);
        completed_keyframes_.reserve(input_size / 4096U);
    }

    bool begin_rule(
        uint32_t kind,
        bool,
        size_t parent_index,
        std::string_view raw_name,
        std::string_view prelude,
        size_t& rule_index) override
    {
        const auto expected_parent = stack_.empty()
            ? css_syntax_no_parent
            : stack_.back().rule_index;
        if (parent_index != expected_parent) return false;

        rule_index = next_rule_index_++;
        frame current;
        current.rule_index = rule_index;
        current.kind = kind;
        current.active = stack_.empty() || stack_.back().children_active;
        current.children_active = current.active;

        if (!stack_.empty() && stack_.back().keyframes
            && kind != css_syntax_style_rule) {
            current.active = false;
            current.children_active = false;
        }

        if (kind == css_syntax_style_rule) {
            current.prelude = std::string(prelude);
            current.children_active = false;
            // Two declarations is the common generated-rule shape and
            // avoids the former IR's exact-size allocation without
            // over-reserving every small rule.
            current.declarations.reserve(2U);
            stack_.push_back(std::move(current));
            return true;
        }

        if (!current.active) {
            current.children_active = false;
            stack_.push_back(std::move(current));
            return true;
        }

        const auto name = ascii_lower(raw_name);
        if (name == "keyframes" || name == "-webkit-keyframes") {
            current.keyframes = true;
            current.prelude = std::string(prelude);
        } else if (name == "font-face") {
            current.children_active = false;
            owner_.record_feature(
                "css", "at-rule:@font-face", "supported",
                "font-family and first src url are registered by the native host",
                "stylesheet-parser");
        } else if (name == "media") {
            current.media_query = trim_value(prelude);
            const auto supported = owner_.inventory_media_query(current.media_query);
            owner_.record_feature(
                "css", "at-rule:@media",
                supported ? "supported" : "unsupported",
                supported ? std::string{} : "unsupported media type or condition",
                "stylesheet-parser");
        } else if (name == "supports") {
            auto condition = ascii_lower(trim_value(prelude));
            auto negated = false;
            if (condition.starts_with("not ")) {
                negated = true;
                condition = trim_value(std::string_view(condition).substr(4U));
            }
            const auto supported = condition == "selector(:focus-visible)";
            owner_.record_feature(
                "css", "at-rule:@supports",
                supported ? "supported" : "unsupported",
                supported ? "selector(:focus-visible)" : "condition is not evaluated",
                "stylesheet-parser");
            current.children_active = supported != negated;
        } else if (name == "layer") {
            owner_.record_feature(
                "css", "at-rule:@layer", "partially-supported",
                "nested rules are parsed without cascade-layer ordering",
                "stylesheet-parser");
        } else if (name == "container") {
            owner_.record_feature(
                "css", "at-rule:@container", "unsupported",
                "container conditions are not evaluated",
                "stylesheet-parser");
            current.children_active = false;
        } else {
            owner_.record_feature(
                "css", "at-rule:@" + name, "unsupported", {},
                "stylesheet-parser");
            current.children_active = false;
        }
        stack_.push_back(std::move(current));
        return true;
    }

    bool declaration(
        std::string_view name,
        std::string_view value,
        bool important) override
    {
        if (stack_.empty()) return false;
        auto& current = stack_.back();
        ++current.observed_declarations;
        if (current.active && current.kind == css_syntax_style_rule) {
            append_declaration(
                current.declarations, name, value, important);
        }
        return true;
    }

    bool end_rule(size_t rule_index, size_t declaration_count) override
    {
        if (stack_.empty() || stack_.back().rule_index != rule_index
            || stack_.back().observed_declarations != declaration_count) {
            return false;
        }
        auto current = std::move(stack_.back());
        stack_.pop_back();
        if (!current.active) return true;

        if (current.kind == css_syntax_style_rule) {
            if (!stack_.empty() && stack_.back().keyframes) {
                owner_.append_css_syntax_keyframe(
                    stack_.back().keyframe_definition,
                    std::move(current.prelude),
                    current.declarations);
                return true;
            }
            std::vector<std::string> inherited_media;
            inherited_media.reserve(stack_.size());
            for (const auto& ancestor : stack_) {
                if (!ancestor.media_query.empty()) {
                    inherited_media.push_back(ancestor.media_query);
                }
            }
            owner_.append_parsed_css_style_rule(
                std::move(current.prelude),
                std::move(current.declarations),
                inherited_media,
                stylesheet_address_);
        } else if (current.keyframes) {
            completed_keyframes_.emplace_back(
                std::move(current.prelude),
                std::move(current.keyframe_definition));
        }
        return true;
    }

    bool complete() const noexcept { return stack_.empty(); }

    std::vector<std::pair<std::string, css_opacity_keyframes>>& keyframes()
    {
        return completed_keyframes_;
    }

private:
    struct frame final {
        size_t rule_index{css_syntax_no_parent};
        uint32_t kind{css_syntax_style_rule};
        std::string prelude;
        std::string media_query;
        std::vector<css_declaration> declarations;
        css_opacity_keyframes keyframe_definition;
        size_t observed_declarations{0U};
        bool active{false};
        bool children_active{false};
        bool keyframes{false};
    };

    Host& owner_;
    const std::string& stylesheet_address_;
    std::vector<frame> stack_;
    std::vector<std::pair<std::string, css_opacity_keyframes>> completed_keyframes_;
    size_t next_rule_index_{0U};
};
} // namespace webscene_native::css
