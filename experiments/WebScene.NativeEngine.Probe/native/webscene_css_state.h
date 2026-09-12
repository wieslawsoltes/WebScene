#pragma once
#include "webscene_native_dom.h"
#include "webscene_css_specified_ir.h"
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace webscene_native::css {
// Native stylesheet/cascade storage shared by runtime adapters and build tools.
// No JavaScript handles, V8 headers or parser ownership belong in this model.
    struct css_declaration final {
        std::string name;
        std::string value;
        bool important{false};
        css_property_id property{css_property_id::unknown};
        specified_css_value specified{};

        css_declaration() = default;
        css_declaration(std::string name_value, std::string value_value, bool important_value=false)
            : name(std::move(name_value)), value(std::move(value_value)), important(important_value)
        {
            property = property_id(name);
            specified = compile_specified_value(property, value);
        }

        bool has_typed_value() const noexcept { return specified.fully_typed(); }
    };

struct compiled_css_pseudo final {
    std::string name;
    std::string argument;
};

struct compiled_css_compound final {
    std::string tag;
    std::vector<std::pair<char, std::string>> identities;
    std::vector<std::string> attributes;
    std::vector<compiled_css_pseudo> pseudos;
    bool valid{false};
    bool pseudo_element{false};
};

struct transparent_string_hash final {
    using is_transparent = void;

    size_t operator()(std::string_view value) const noexcept
    {
        return std::hash<std::string_view>{}(value);
    }
};

struct transparent_string_equal final {
    using is_transparent = void;

    bool operator()(std::string_view left, std::string_view right) const noexcept
    {
        return left == right;
    }
};

#if defined(WEBSCENE_NATIVE_ENGINE_CSS_CLASS_LOOKUP_VIEW_EXPERIMENT)
template<typename Value>
using css_index_string_map = std::unordered_map<
    std::string, Value, transparent_string_hash, transparent_string_equal>;
using css_index_string_set = std::unordered_set<
    std::string, transparent_string_hash, transparent_string_equal>;
#else
template<typename Value>
using css_index_string_map = std::unordered_map<std::string, Value>;
using css_index_string_set = std::unordered_set<std::string>;
#endif

    struct compiled_css_selector final {
        std::vector<std::string> compounds;
        std::vector<char> combinators;
        uint32_t specificity{0};
        std::vector<compiled_css_compound> compiled_compounds;
    };

    struct compiled_css_selector_list final {
        std::vector<compiled_css_selector> selectors;
    };

    struct css_rule_payload final {
        std::string selector;
        compiled_css_selector compiled_selector;
        compiled_css_selector compiled_pseudo_origin;
        std::vector<css_declaration> declarations;
        std::vector<std::string> media_queries;
        uint32_t specificity{0};
    };

    struct css_rule final {
        std::shared_ptr<const css_rule_payload> payload;
        uint32_t stylesheet_owner_id{0};
        uint32_t shadow_scope_root_id{0};
        bool media_matches{true};

        const std::string& selector() const noexcept { return payload->selector; }
        const compiled_css_selector& compiled_selector() const noexcept
        {
            return payload->compiled_selector;
        }
        const std::vector<css_declaration>& declarations() const noexcept
        {
            return payload->declarations;
        }
        const std::vector<std::string>& media_queries() const noexcept
        {
            return payload->media_queries;
        }
        uint32_t specificity() const noexcept { return payload->specificity; }
    };

    struct css_opacity_keyframes final {
        std::vector<node_style::opacity_keyframe> opacity_stops;
        std::vector<node_style::rotation_keyframe> rotation_stops;
    };

    enum class hover_invalidation_scope : uint8_t
    {
        subject,
        subtree,
        siblings
    };

    struct hover_selector_dependency final
    {
        std::string trigger_compound;
        hover_invalidation_scope scope{hover_invalidation_scope::subject};
    };

    // A browser document owns an independent cascade. Keeping this movable
    // makes switching realms an O(1) exchange of vector/map storage, while the
    // active realm retains the existing direct-field hot path.
    struct css_cascade_state final {
        std::vector<css_rule> rules;
        uint32_t stylesheet_owner_id{0};
        uint32_t stylesheet_shadow_scope_root_id{0};
        std::unordered_map<std::string, css_opacity_keyframes> opacity_keyframes;
        css_index_string_map<std::vector<size_t>> rules_by_class;
        css_index_string_map<std::vector<size_t>> rules_by_id;
        css_index_string_map<std::vector<size_t>> rules_by_tag;
        css_index_string_map<std::vector<size_t>> rules_by_attribute;
        css_index_string_map<std::vector<size_t>> rules_by_variable_reference;
        std::vector<size_t> focus_rules;
        std::vector<size_t> unindexed_rules;
        css_index_string_set attribute_dependencies;
        css_index_string_set descendant_attribute_dependencies;
        std::vector<hover_selector_dependency> hover_dependencies;
        std::unordered_map<std::string, std::string> variables;
        std::unordered_set<std::string> important_variables;
    };


} // namespace webscene_native::css
