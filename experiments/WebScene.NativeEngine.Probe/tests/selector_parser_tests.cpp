#include "webscene_selector_parser.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using webscene_native::parse_selector_syntax;

[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "selector parser test failed: " << message << '\n';
    std::exit(1);
}

void require(bool condition, std::string_view message)
{
    if (!condition) fail(message);
}

void require_selector_shape()
{
    const auto parsed = parse_selector_syntax(
        R"CSS(main > .card[data-label="a,b"] + a:hover::before, #escaped\+id ~ section:nth-child(2n + 1))CSS");
    require(static_cast<bool>(parsed), parsed.error);
    require(parsed.selectors.size() == 2U, "selector-list cardinality");

    const auto& first = parsed.selectors[0];
    require(first.compounds.size() == 3U, "first selector compound count");
    require(first.combinators.size() == 2U, "first selector combinator count");
    require(first.combinators[0] == '>' && first.combinators[1] == '+',
        "first selector combinator values");
    require(first.compounds[0] == "main", "type compound");
    require(first.compounds[1].find(".card") != std::string::npos,
        "class compound serialization");
    require(first.compounds[1].find("a,b") != std::string::npos,
        "attribute comma remains inside compound");
    require(first.compounds[2].find("a:hover::before") != std::string::npos,
        "pseudo-element remains on originating compound");

    const auto& second = parsed.selectors[1];
    require(second.compounds.size() == 2U, "second selector compound count");
    require(second.combinators.size() == 1U && second.combinators[0] == '~',
        "second selector sibling combinator");
}

void require_specificity()
{
    const auto parsed = parse_selector_syntax(
        ".card:where(#ignored), article:is(.note, #winner), div:not(.a, #b)");
    require(static_cast<bool>(parsed), parsed.error);
    require(parsed.selectors.size() == 3U, "specificity selector count");
    require(parsed.selectors[0].specificity == 0x000100U,
        ":where contributes zero specificity");
    require(parsed.selectors[1].specificity == 0x010001U,
        ":is uses its most specific argument");
    require(parsed.selectors[2].specificity == 0x010001U,
        ":not uses its most specific argument");
}

void require_validation()
{
    require(!parse_selector_syntax("").operator bool(), "empty selector rejected");
    require(!parse_selector_syntax(".a,").operator bool(), "empty list item rejected");
    require(!parse_selector_syntax("div >").operator bool(), "dangling combinator rejected");
    require(!parse_selector_syntax("div:unknown-state").operator bool(),
        "unknown pseudo-class rejected");
    require(!parse_selector_syntax("[data-value=]").operator bool(),
        "missing attribute value rejected");
    require(static_cast<bool>(parse_selector_syntax("div:is(.valid, :unknown-state)")),
        "forgiving :is list retains valid selector");
}

void require_wtf8_domstring_round_trip()
{
    auto selector = std::string("#");
    selector.append("\xF3\xB0\x80\x80", 4U);
    selector.append("\xED\xA0\xBD", 3U);
    selector += "surrogateFirst";
    const auto parsed = parse_selector_syntax(selector);
    require(static_cast<bool>(parsed), parsed.error);
    require(parsed.selectors.size() == 1U, "WTF-8 selector count");
    require(parsed.selectors[0].serialized == selector,
        "lone surrogate and private-use sentinel round-trip through Servo");
    require(parsed.selectors[0].compounds.size() == 1U
            && parsed.selectors[0].compounds[0] == selector,
        "compiled compound restores the original WTF-8 bytes");

    constexpr char invalid[]{static_cast<char>(0xFF)};
    require(!parse_selector_syntax(std::string_view(invalid, 1U)),
        "non-WTF-8 invalid input remains rejected");
}

} // namespace

int main()
{
    require_selector_shape();
    require_specificity();
    require_validation();
    require_wtf8_domstring_round_trip();
    std::cout << "selector parser tests passed\n";
    return 0;
}
