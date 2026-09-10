#pragma once
#include "webscene_css_state.h"
#include "webscene_css_parser.h"

namespace webscene_native::css {
inline std::string ascii_lower(std::string_view value) {
    auto result=std::string(value);
    for (auto &c:result) if(c>='A' && c<='Z') c=static_cast<char>(c+('a'-'A'));
    return result;
}
inline bool valid_custom_property_name(std::string_view name) {
    if (!name.starts_with("--") || name.size()<=2) return false;
    return std::none_of(name.begin(),name.end(),[](unsigned char c) {
        return c<=0x20 || c==0x7f;
    });
}
inline void append_declaration(
        std::vector<css_declaration>& result,
        std::string_view raw_name,
        std::string_view raw_value,
        bool important,
        bool preserve_empty_custom_properties = false)
    {
        auto name = raw_name.starts_with("--")
            ? std::string(raw_name)
            : ascii_lower(raw_name);
        auto value = std::string(raw_value);
        const auto custom = name.starts_with("--");
        if (custom && !valid_custom_property_name(name)) return;
        if (!custom && name.starts_with("-")
            && name != "-moz-transform"
            && name != "-webkit-transform"
            && name != "-webkit-font-smoothing") return;
        if (custom && value.empty() && preserve_empty_custom_properties) value = " ";
        if (!name.empty() && !value.empty()) {
            result.push_back({std::move(name), std::move(value), important});
        }
    }

// Runtime-compatible declaration parsing with the shared Rust syntax bridge.
// This is a build/runtime CSS service, never a dependency of compiled-only apps.
inline std::vector<css_declaration> parse_declarations(std::string_view body,
    bool preserve_empty_custom_properties=false) {
    std::vector<css_declaration> result;

        class declaration_sink final : public css_syntax_sink {
        public:
            declaration_sink(
                std::vector<css_declaration>& declarations,
                bool preserve_empty_custom_properties)
                : declarations_(declarations),
                  preserve_empty_custom_properties_(preserve_empty_custom_properties)
            {
            }

            bool begin_rule(
                uint32_t,
                bool,
                size_t,
                std::string_view,
                std::string_view,
                size_t&) override
            {
                return false;
            }

            bool declaration(
                std::string_view name,
                std::string_view value,
                bool important) override
            {
                append_declaration(
                    declarations_, name, value, important,
                    preserve_empty_custom_properties_);
                return true;
            }

            bool end_rule(size_t, size_t) override { return false; }

        private:
            std::vector<css_declaration>& declarations_;
            bool preserve_empty_custom_properties_;
        };

        result.reserve(body.size() / 64U);
        declaration_sink sink(result, preserve_empty_custom_properties);
        const auto parsed = stream_css_syntax_declarations(body, sink);
        if (!parsed) result.clear();
    return result;
}
} // namespace webscene_native::css
