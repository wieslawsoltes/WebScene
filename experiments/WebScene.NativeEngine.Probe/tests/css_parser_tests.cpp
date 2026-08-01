#include "webscene_css_parser.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

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
        "--vertical-tab:\vpreserved\v; broken");
    require(static_cast<bool>(parsed), parsed.error);
    require(parsed.declarations.size() == 6U, "expected six recovered declarations");
    require(parsed.declarations[0].name == "color", "ordinary property names are ASCII-lowercase");
    require(parsed.declarations[1].value == "\"a;b:c\"", "semicolon and colon inside strings survive");
    require(parsed.declarations[2].value == "url(data:x;y)", "semicolon inside url() survives");
    require(parsed.declarations[3].name == "--Case", "custom property names remain case-sensitive");
    require(parsed.declarations[4].important, "case-insensitive !important is recognized");
    require(parsed.declarations[4].value == "10px", "!important is removed from the value");
    require(
        parsed.declarations[5].value == "\vpreserved\v",
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
    require(parsed.metrics.parse_error_count == 0U, "valid stylesheet has no syntax errors");
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

} // namespace

int main()
{
    declaration_syntax();
    stylesheet_structure();
    invalid_utf8_is_rejected();
    std::cout << "CSS parser tests passed\n";
    return 0;
}
