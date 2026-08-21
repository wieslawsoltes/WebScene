#include "webscene_css_parser.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace webscene_native;

namespace {

void require(bool condition, std::string_view message)
{
    if (condition) return;
    std::cerr << "CSS parser test failed: " << message << '\n';
    std::exit(1);
}

void declaration_syntax()
{
    const auto parsed = parse_css_syntax_declarations(
        "COLOR: red; content: \"a;b:c\"; background: url(data:x;y); "
        "--Case: calc(1px + var(--x, 2px)); width: 10px ! IMPORTANT; "
        "-webkit-font-smoothing: antialiased; "
        "--vertical-tab:\vpreserved\v; broken");
    require(static_cast<bool>(parsed), parsed.error);
    if (parsed.declarations.size() != 6U) {
        std::cerr << "Recovered declarations: " << parsed.declarations.size() << '\n';
        for (const auto& declaration : parsed.declarations) {
            std::cerr << "  " << declaration.name << ": " << declaration.value
                << (declaration.important ? " !important" : "") << '\n';
        }
    }
    require(parsed.declarations.size() == 7U, "expected seven recovered declarations");
    require(parsed.declarations[0].name == "color", "ordinary property names are ASCII-lowercase");
    require(parsed.declarations[1].value == "\"a;b:c\"", "semicolon and colon inside strings survive");
    require(parsed.declarations[2].value == "url(data:x;y)", "semicolon inside url() survives");
    require(parsed.declarations[3].name == "--Case", "custom property names remain case-sensitive");
    if (!parsed.declarations[4].important) {
        std::cerr << "Important declaration value: " << parsed.declarations[4].name
            << ": " << parsed.declarations[4].value << '\n';
    }
    require(parsed.declarations[4].important, "case-insensitive !important is recognized");
    require(parsed.declarations[4].value == "10px", "!important is removed from the value");
    require(
        parsed.declarations[5].name == "-webkit-font-smoothing"
            && parsed.declarations[5].value == "antialiased",
        "supported vendor-prefixed font smoothing is retained");
    require(
        parsed.declarations[6].value == "\vpreserved\v",
        "vertical tab is token data rather than CSS whitespace");
    require(parsed.metrics.parse_error_count == 1U, "malformed tail is recovered and counted");
}

void stylesheet_structure()
{
    const auto parsed = parse_css_syntax_stylesheet(R"CSS(
        /* a comment with } and ; */
        .card, [data-label="}"] { content: "};"; color: red }
        @media (min-width: 600px) {
            .wide { width: calc(100% - var(--gap, 2px)); }
            @supports selector(:focus-visible) { button:focus-visible { outline: 2px solid } }
        }
        @font-face { font-family: "Probe"; src: url(probe.woff2) format("woff2"); }
        @keyframes fade { from { opacity: 0 } 50%, to { opacity: 1 } }
    )CSS");
    require(static_cast<bool>(parsed), parsed.error);
    if (parsed.metrics.parse_error_count != 0U) {
        std::cerr << "Valid stylesheet parse errors: "
            << parsed.metrics.parse_error_count << '\n';
    }
    require(parsed.metrics.parse_error_count == 0U, "valid stylesheet has no syntax errors");
    if (parsed.rules.size() != 9U) {
        std::cerr << "Stylesheet rules: " << parsed.rules.size() << '\n';
        for (size_t index = 0; index < parsed.rules.size(); ++index) {
            const auto& rule = parsed.rules[index];
            std::cerr << "  [" << index << "] kind=" << rule.kind
                << " name=" << rule.name << " prelude=" << rule.prelude
                << " parent=" << rule.parent_index
                << " declarations=" << rule.declaration_count << '\n';
        }
    }
    require(parsed.rules.size() == 9U, "flat rule tree contains nested at-rules and keyframes");
    require(parsed.rules[0].kind == css_syntax_style_rule, "first rule is a style rule");
    require(parsed.rules[1].name == "media", "media at-rule is normalized");
    require(parsed.rules[2].parent_index == 1U, "media child points to its parent");
    require(parsed.rules[3].name == "supports", "nested supports rule is retained");
    require(parsed.rules[4].parent_index == 3U, "supports style child points to supports");
    require(parsed.rules[5].name == "font-face", "font-face rule is retained");
    require(parsed.rules[5].declaration_count == 2U, "font-face descriptors are declarations");
    require(parsed.rules[6].name == "keyframes", "keyframes rule is retained");
    require(parsed.rules[7].parent_index == 6U, "keyframe child points to keyframes");
    require(parsed.declarations.size() == 8U, "all style and descriptor declarations are emitted");
}

void invalid_utf8_is_rejected()
{
    constexpr char input[]{static_cast<char>(0xff), 0};
    const auto parsed = parse_css_syntax_stylesheet(std::string_view(input, 1U));
    require(!parsed, "invalid UTF-8 should fail explicitly");
}

class direct_sink final : public css_syntax_sink {
public:
    bool begin_rule(
        uint32_t,
        bool,
        size_t parent_index,
        std::string_view,
        std::string_view,
        size_t& rule_index) override
    {
        const auto expected_parent = stack.empty()
            ? css_syntax_no_parent
            : stack.back();
        if (parent_index != expected_parent) return false;
        rule_index = 100U + rule_count++;
        stack.push_back(rule_index);
        return true;
    }

    bool declaration(
        std::string_view name,
        std::string_view value,
        bool important) override
    {
        if (name == "COLOR" && value == "red" && !important) saw_raw_color = true;
        ++declaration_count;
        return true;
    }

    bool end_rule(size_t rule_index, size_t) override
    {
        if (stack.empty() || stack.back() != rule_index) return false;
        stack.pop_back();
        return true;
    }

    std::vector<size_t> stack;
    size_t rule_count{0U};
    size_t declaration_count{0U};
    bool saw_raw_color{false};
};

void direct_streaming_sink()
{
    direct_sink sink;
    const auto parsed = stream_css_syntax_stylesheet(
        ".a { COLOR: red } @media (min-width: 1px) { .b { width: 2px } }",
        sink);
    require(static_cast<bool>(parsed), parsed.error);
    require(sink.stack.empty(), "direct callbacks preserve balanced rule nesting");
    require(sink.rule_count == 3U, "direct callback receives all nested rules");
    require(sink.declaration_count == 2U, "direct callback receives declarations once");
    require(sink.saw_raw_color, "direct callback receives borrowed unnormalized syntax");
    require(parsed.metrics.parser_allocation_count == 0U, "direct parser stream allocates no Rust output");
    require(parsed.metrics.parser_retained_bytes == 0U, "direct parser stream retains no Rust output");
}

} // namespace

int main()
{
    declaration_syntax();
    stylesheet_structure();
    invalid_utf8_is_rejected();
    direct_streaming_sink();
    std::cout << "CSS parser tests passed\n";
    return 0;
}
