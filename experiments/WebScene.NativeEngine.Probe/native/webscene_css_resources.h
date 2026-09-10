#pragma once
#include "webscene_css_matching.h"
#include "webscene_native_resource_url.h"

namespace webscene_native::css {
inline std::string resolve_resource_urls(
        std::string value,
        const std::string& stylesheet_address)
    {
        if (value.empty() || stylesheet_address.empty()) return value;

        auto lowercase = ascii_lower(value);
        size_t search = 0U;
        while ((search = lowercase.find("url(", search)) != std::string::npos) {
            const auto argument_begin = search + 4U;
            auto argument_end = argument_begin;
            char quote = 0;
            for (; argument_end < value.size(); ++argument_end) {
                const auto character = value[argument_end];
                if (quote != 0) {
                    if (character == quote
                        && (argument_end == argument_begin
                            || value[argument_end - 1U] != '\\')) {
                        quote = 0;
                    }
                    continue;
                }
                if (character == '\'' || character == '"') {
                    quote = character;
                } else if (character == ')') {
                    break;
                }
            }
            if (argument_end >= value.size()) break;

            auto address = trim_value(std::string_view(value).substr(
                argument_begin,
                argument_end - argument_begin));
            if (address.size() >= 2U
                && ((address.front() == '\'' && address.back() == '\'')
                    || (address.front() == '"' && address.back() == '"'))) {
                address = address.substr(1U, address.size() - 2U);
            }
            if (address.empty() || address.starts_with('#')) {
                search = argument_end + 1U;
                continue;
            }

            auto resolved = resources::resolve_url(address, stylesheet_address);
            std::string escaped;
            escaped.reserve(resolved.size());
            for (const auto character : resolved) {
                if (character == '\\' || character == '"') escaped.push_back('\\');
                escaped.push_back(character);
            }
            const auto replacement = "url(\"" + escaped + "\")";
            value.replace(search, argument_end - search + 1U, replacement);
            lowercase = ascii_lower(value);
            search += replacement.size();
        }
        return value;
    }

} // namespace webscene_native::css
