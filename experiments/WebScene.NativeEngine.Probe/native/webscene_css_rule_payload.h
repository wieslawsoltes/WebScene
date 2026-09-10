#pragma once
#include "webscene_css_state.h"
#include <mutex>

namespace webscene_native::css {
using rule_payload_cache = std::unordered_map<uint64_t,
    std::vector<std::weak_ptr<const css_rule_payload>>>;
inline uint64_t rule_payload_hash(
        std::string_view selector,
        const std::vector<css_declaration>& declarations,
        const std::vector<std::string>& media_queries)
    {
        auto hash = uint64_t{1469598103934665603ULL};
        const auto append = [&](std::string_view value) {
            for (const auto character : value) {
                hash ^= static_cast<unsigned char>(character);
                hash *= 1099511628211ULL;
            }
            hash ^= 0xffU;
            hash *= 1099511628211ULL;
        };
        append(selector);
        for (const auto& declaration : declarations) {
            append(declaration.name);
            append(declaration.value);
            hash ^= declaration.important ? 1U : 0U;
            hash *= 1099511628211ULL;
        }
        for (const auto& query : media_queries) append(query);
        return hash;
    }

inline bool rule_payload_matches(
        const css_rule_payload& payload,
        std::string_view selector,
        const std::vector<css_declaration>& declarations,
        const std::vector<std::string>& media_queries)
    {
        if (payload.selector != selector
            || payload.declarations.size() != declarations.size()
            || payload.media_queries != media_queries) {
            return false;
        }
        for (size_t index = 0; index < declarations.size(); ++index) {
            const auto& left = payload.declarations[index];
            const auto& right = declarations[index];
            if (left.name != right.name || left.value != right.value
                || left.important != right.important) {
                return false;
            }
        }
        return true;
    }

template<typename Compile>
std::shared_ptr<const css_rule_payload> intern_rule_payload(
    std::mutex& mutex, rule_payload_cache& payloads, Compile&& compile,
    std::string selector, const std::vector<css_declaration>& declarations,
    const std::vector<std::string>& media_queries)
{
        const auto hash = rule_payload_hash(
            selector,
            declarations,
            media_queries);
        std::lock_guard lock(mutex);
        auto& candidates = payloads[hash];
        for (auto iterator = candidates.begin(); iterator != candidates.end();) {
            auto candidate = iterator->lock();
            if (candidate == nullptr) {
                iterator = candidates.erase(iterator);
                continue;
            }
            if (rule_payload_matches(
                    *candidate,
                    selector,
                    declarations,
                    media_queries)) {
                return candidate;
            }
            ++iterator;
        }
        auto payload = std::make_shared<css_rule_payload>();
        payload->selector = std::move(selector);
        payload->compiled_selector = compile(payload->selector);
        payload->specificity = payload->compiled_selector.specificity;
        payload->declarations = declarations;
        payload->media_queries = media_queries;
        candidates.emplace_back(payload);
        return payload;
}
} // namespace webscene_native::css
