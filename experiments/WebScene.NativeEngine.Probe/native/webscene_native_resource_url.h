#pragma once
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace webscene_native::resources {
inline std::string resolve_url(std::string value, const std::string& base)
    {
        if (value.empty()) return {};
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
            value.erase(value.begin());
        }
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
            value.pop_back();
        }
        const auto scheme = value.find(':');
        if (scheme != std::string::npos
            && scheme > 0U
            && std::all_of(value.begin(), value.begin() + static_cast<std::ptrdiff_t>(scheme),
                [](unsigned char character) {
                    return std::isalnum(character) || character == '+' || character == '-' || character == '.';
                })) {
            return value;
        }
        const auto base_scheme = base.find("://");
        if (value.starts_with("//")) {
            return base_scheme == std::string::npos
                ? value
                : base.substr(0U, base_scheme) + ":" + value;
        }
        if (base_scheme == std::string::npos) return value;
        const auto authority_end = base.find('/', base_scheme + 3U);
        const auto origin = authority_end == std::string::npos ? base : base.substr(0U, authority_end);
        if (value.starts_with('/')) return origin + value;

        const auto value_suffix_offset = value.find_first_of("?#");
        const auto value_suffix = value_suffix_offset == std::string::npos
            ? std::string{}
            : value.substr(value_suffix_offset);
        if (value_suffix_offset != std::string::npos) value.resize(value_suffix_offset);
        const auto preserve_trailing_slash = value.ends_with('/');
        auto clean_base = base.substr(0U, base.find_first_of("?#"));
        const auto slash = clean_base.rfind('/');
        auto combined = (slash == std::string::npos ? clean_base + '/' : clean_base.substr(0U, slash + 1U)) + value;
        const auto combined_authority_end = combined.find('/', base_scheme + 3U);
        if (combined_authority_end == std::string::npos) return combined;
        const auto prefix = combined.substr(0U, combined_authority_end);
        const auto suffix = combined.substr(combined_authority_end + 1U);
        std::vector<std::string> segments;
        size_t cursor = 0U;
        while (cursor <= suffix.size()) {
            auto end = suffix.find('/', cursor);
            if (end == std::string::npos) end = suffix.size();
            const auto segment = suffix.substr(cursor, end - cursor);
            if (segment == "..") {
                if (!segments.empty()) segments.pop_back();
            } else if (!segment.empty() && segment != ".") {
                segments.push_back(segment);
            }
            if (end == suffix.size()) break;
            cursor = end + 1U;
        }
        std::ostringstream result;
        result << prefix << '/';
        for (size_t index = 0U; index < segments.size(); ++index) {
            if (index != 0U) result << '/';
            result << segments[index];
        }
        if (preserve_trailing_slash && !segments.empty()) result << '/';
        result << value_suffix;
        return result.str();
    }

} // namespace webscene_native::resources
