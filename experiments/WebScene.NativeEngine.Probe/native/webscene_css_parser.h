#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace webscene_native {

inline constexpr uint32_t css_syntax_style_rule = 0;
inline constexpr uint32_t css_syntax_at_rule = 1;
inline constexpr size_t css_syntax_no_parent = static_cast<size_t>(-1);

struct css_syntax_declaration final {
    std::string name;
    std::string value;
    bool important{false};
};

struct css_syntax_rule final {
    uint32_t kind{css_syntax_style_rule};
    bool has_block{true};
    size_t parent_index{css_syntax_no_parent};
    std::string name;
    std::string prelude;
    size_t first_declaration{0};
    size_t declaration_count{0};
};

struct css_syntax_metrics final {
    uint64_t duration_ns{0};
    uint64_t parse_error_count{0};
    uint64_t parser_allocation_count{0};
    uint64_t parser_peak_bytes{0};
    uint64_t parser_retained_bytes{0};
};

struct css_syntax_output final {
    std::vector<css_syntax_rule> rules;
    std::vector<css_syntax_declaration> declarations;
    css_syntax_metrics metrics;
    std::string error;

    explicit operator bool() const noexcept { return error.empty(); }
};

css_syntax_output parse_css_syntax_stylesheet(std::string_view input);
css_syntax_output parse_css_syntax_declarations(std::string_view input);

} // namespace webscene_native
