#pragma once
#include "webscene_css_state.h"
#include "webscene_selector_parser.h"
#include <cctype>

namespace webscene_native::css {
inline std::string_view trim_css_view(std::string_view value) {
    constexpr std::string_view whitespace=" \t\r\n\f";
    const auto start=value.find_first_not_of(whitespace);
    if(start==std::string_view::npos) return {};
    return value.substr(start,value.find_last_not_of(whitespace)-start+1);
}
inline void append_utf8_codepoint(std::string& result, uint32_t codepoint)
    {
        if (codepoint <= 0x7FU) {
            result.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FFU) {
            result.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
            result.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
        } else if (codepoint <= 0xFFFFU) {
            result.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
            result.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
        } else {
            result.push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
            result.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
        }
    }

inline bool is_css_hex_digit(unsigned char character)
    {
        return (character >= '0' && character <= '9')
            || (character >= 'a' && character <= 'f')
            || (character >= 'A' && character <= 'F');
    }

inline size_t skip_css_escape_sequence(std::string_view text, size_t slash)
    {
        auto cursor = slash + 1U;
        if (cursor >= text.size()) return cursor;
        if (text[cursor] == '\r') {
            ++cursor;
            if (cursor < text.size() && text[cursor] == '\n') ++cursor;
            return cursor;
        }
        if (text[cursor] == '\n' || text[cursor] == '\f') return cursor + 1U;
        const auto hex_start = cursor;
        while (cursor < text.size()
            && cursor - hex_start < 6U
            && is_css_hex_digit(static_cast<unsigned char>(text[cursor]))) ++cursor;
        if (cursor > hex_start) {
            if (cursor < text.size() && text[cursor] == '\r') {
                ++cursor;
                if (cursor < text.size() && text[cursor] == '\n') ++cursor;
            } else if (cursor < text.size()
                && std::isspace(static_cast<unsigned char>(text[cursor]))) ++cursor;
        } else {
            ++cursor;
        }
        return cursor;
    }

inline std::string read_css_identifier(std::string_view text, size_t& cursor)
    {
        std::string result;
        while (cursor < text.size()) {
            const auto character = static_cast<unsigned char>(text[cursor]);
            if (character == '\\') {
                ++cursor;
                if (cursor >= text.size()) {
                    append_utf8_codepoint(result, 0xfffdU);
                    break;
                }
                if (text[cursor] == '\r') {
                    ++cursor;
                    if (cursor < text.size() && text[cursor] == '\n') ++cursor;
                    continue;
                }
                if (text[cursor] == '\n' || text[cursor] == '\f') {
                    ++cursor;
                    continue;
                }
                const auto hex_start = cursor;
                uint32_t codepoint = 0U;
                while (cursor < text.size()
                    && cursor - hex_start < 6U
                    && is_css_hex_digit(static_cast<unsigned char>(text[cursor]))) {
                    const auto digit = static_cast<unsigned char>(text[cursor++]);
                    codepoint = codepoint * 16U
                        + (digit >= '0' && digit <= '9'
                            ? digit - '0'
                            : static_cast<unsigned char>(std::tolower(digit)) - 'a' + 10U);
                }
                if (cursor > hex_start) {
                    if (cursor < text.size() && text[cursor] == '\r') {
                        ++cursor;
                        if (cursor < text.size() && text[cursor] == '\n') ++cursor;
                    } else if (cursor < text.size()
                        && std::isspace(static_cast<unsigned char>(text[cursor]))) ++cursor;
                    append_utf8_codepoint(
                        result,
                        codepoint == 0U || codepoint > 0x10ffffU
                            || (codepoint >= 0xd800U && codepoint <= 0xdfffU)
                            ? 0xfffdU
                            : codepoint);
                } else {
                    result.push_back(text[cursor++]);
                }
                continue;
            }
            if (character == 0U) {
                append_utf8_codepoint(result, 0xfffdU);
                ++cursor;
                continue;
            }
            if (!std::isalnum(character) && character != '-' && character != '_'
                && character < 0x80U) break;
            result.push_back(text[cursor++]);
        }
        return result;
    }

inline size_t find_css_attribute_close(
        std::string_view selector,
        size_t cursor)
    {
        char quote = 0;
        for (; cursor < selector.size(); ++cursor) {
            const auto character = selector[cursor];
            if (character == '\\') {
                cursor = skip_css_escape_sequence(selector, cursor) - 1U;
                continue;
            }
            if (quote != 0) {
                if (character == quote) quote = 0;
                continue;
            }
            if (character == '\'' || character == '"') {
                quote = character;
            } else if (character == ']') {
                return cursor;
            }
        }
        return std::string_view::npos;
    }

inline compiled_css_compound compile_css_compound_selector(
        std::string_view selector)
    {
        compiled_css_compound result;
        selector = trim_css_view(selector);
        if (selector.empty()) return result;

        size_t cursor = 0U;
        if (selector.empty()) {
            // A bare structural pseudo-class has an implicit universal selector.
        } else if (selector[cursor] == '*') {
            ++cursor;
        } else if (std::isalpha(static_cast<unsigned char>(selector[cursor]))
            || selector[cursor] == '\\') {
            result.tag = read_css_identifier(selector, cursor);
        }
        while (cursor < selector.size()) {
            const auto marker = selector[cursor++];
            if (marker == '.' || marker == '#') {
                auto wanted = read_css_identifier(selector, cursor);
                if (wanted.empty()) return result;
                result.identities.emplace_back(marker, std::move(wanted));
            } else if (marker == '[') {
                const auto close = find_css_attribute_close(selector, cursor);
                if (close == std::string::npos) return result;
                result.attributes.emplace_back(selector.substr(cursor, close - cursor));
                cursor = close + 1U;
            } else if (marker == ':') {
                if (cursor < selector.size() && selector[cursor] == ':') {
                    result.pseudo_element = true;
                    ++cursor;
                }
                auto name = read_css_identifier(selector, cursor);
                if (name.empty()) return result;
                if (name == "before" || name == "after"
                    || name == "first-letter" || name == "first-line") {
                    result.pseudo_element = true;
                }
                auto argument = std::string{};
                if (cursor < selector.size() && selector[cursor] == '(') {
                    const auto argument_start = ++cursor;
                    auto depth = 1;
                    char quote = 0;
                    while (cursor < selector.size() && depth > 0) {
                        const auto value = selector[cursor];
                        if (quote != 0) {
                            if (value == quote
                                && (cursor == argument_start
                                    || selector[cursor - 1U] != '\\')) {
                                quote = 0;
                            }
                        } else if (value == '\'' || value == '"') {
                            quote = value;
                        } else if (value == '\\') {
                            cursor = skip_css_escape_sequence(selector, cursor) - 1U;
                        } else if (value == '(') {
                            ++depth;
                        } else if (value == ')') {
                            --depth;
                        }
                        if (depth > 0) ++cursor;
                    }
                    if (depth != 0) return result;
                    argument.assign(
                        selector.substr(argument_start, cursor - argument_start));
                    ++cursor;
                }
                result.pseudos.push_back(
                    compiled_css_pseudo{std::move(name), std::move(argument)});
            } else {
                return result;
            }
        }
        result.valid = true;
        return result;
    }


// Reuse Servo parsing/specificity and the runtime's native compound preparation.
inline compiled_css_selector_list compile_selector_list(std::string_view text) {
    compiled_css_selector_list result;
    const auto parsed=parse_selector_syntax(text);
    if(!parsed) return result;
    for(const auto& source:parsed.selectors) {
        compiled_css_selector selector{source.compounds,source.combinators,source.specificity,{}};
        for(const auto& part:selector.compounds)
            selector.compiled_compounds.push_back(compile_css_compound_selector(part));
        result.selectors.push_back(std::move(selector));
    }
    return result;
}
inline compiled_css_selector compile_selector(std::string_view text) {
    auto result=compile_selector_list(text);
    if(result.selectors.size()!=1) return {};
    return std::move(result.selectors.front());
}
} // namespace webscene_native::css
