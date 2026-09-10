#pragma once
#include "webscene_css_box_values.h"
#include <cstdlib>

namespace webscene_native::css {
inline bool is_css_time(std::string_view value)
    {
        return value.ends_with("ms") || value.ends_with('s');
    }

inline float parse_css_time_ms(std::string value)
    {
        value = trim_value(std::move(value));
        auto multiplier = 1.0F;
        if (value.ends_with("ms")) value.resize(value.size() - 2U);
        else if (value.ends_with('s')) {
            value.pop_back();
            multiplier = 1000.0F;
        } else return 0;
        return std::strtof(value.c_str(), nullptr) * multiplier;
    }

inline void parse_transition_timing(
        const std::string& value,
        node_style::transition_timing& timing)
    {
        const auto lower = ascii_lower(value);
        if (lower == "linear") {
            timing.x1 = 0; timing.y1 = 0; timing.x2 = 1; timing.y2 = 1;
        } else if (lower == "ease-in") {
            timing.x1 = 0.42F; timing.y1 = 0; timing.x2 = 1; timing.y2 = 1;
        } else if (lower == "ease-out") {
            timing.x1 = 0; timing.y1 = 0; timing.x2 = 0.58F; timing.y2 = 1;
        } else if (lower == "ease-in-out") {
            timing.x1 = 0.42F; timing.y1 = 0; timing.x2 = 0.58F; timing.y2 = 1;
        } else if (lower.starts_with("cubic-bezier(") && lower.ends_with(')')) {
            auto points = lower.substr(13U, lower.size() - 14U);
            std::replace(points.begin(), points.end(), ',', ' ');
            std::istringstream stream(points);
            float x1 = 0.25F, y1 = 0.1F, x2 = 0.25F, y2 = 1;
            if (stream >> x1 >> y1 >> x2 >> y2) {
                timing.x1 = std::clamp(x1, 0.0F, 1.0F);
                timing.y1 = y1;
                timing.x2 = std::clamp(x2, 0.0F, 1.0F);
                timing.y2 = y2;
            }
        }
    }

inline void configure_style_transitions(node_style& style)
    {
        if (!style.has_animation_data()) return;
        auto& animations = style.mutable_animations();
        const auto properties = split_css_component_list(animations.transition_property_value, ',');
        const auto durations = split_css_component_list(animations.transition_duration_value, ',');
        const auto delays = split_css_component_list(animations.transition_delay_value, ',');
        const auto timings = split_css_component_list(
            animations.transition_timing_function_value, ',');
        const auto resolve = [&](std::string_view property) {
            node_style::transition_timing result;
            for (size_t index = 0; index < properties.size(); ++index) {
                auto candidate = ascii_lower(trim_value(properties[index]));
                // Transition matching uses the canonical physical property.
                // A logical inset declaration and its physical alias address
                // the same computed value (CSS Logical Properties §4.1).
                if (candidate == "inset-inline-start") candidate = "left";
                else if (candidate == "inset-block-start") candidate = "top";
                if (candidate != property && candidate != "all") continue;
                if (!durations.empty()) {
                    result.duration_ms = std::max(
                        0.0F, parse_css_time_ms(durations[index % durations.size()]));
                }
                if (!delays.empty()) {
                    result.delay_ms = parse_css_time_ms(delays[index % delays.size()]);
                }
                if (!timings.empty()) {
                    parse_transition_timing(timings[index % timings.size()], result);
                }
                return result;
            }
            return result;
        };
        animations.transform_transition = resolve("transform");
        animations.left_transition = resolve("left");
        animations.top_transition = resolve("top");
        animations.opacity_transition = resolve("opacity");
        animations.color_transition = resolve("color");
    }

inline void apply_transition_shorthand(node_style& style, const std::string& value)
    {
        std::vector<std::string> properties;
        std::vector<std::string> durations;
        std::vector<std::string> delays;
        std::vector<std::string> timings;
        for (const auto& item : split_css_component_list(value, ',')) {
            auto property = std::string("all");
            auto duration = std::string("0s");
            auto delay = std::string("0s");
            auto timing = std::string("ease");
            auto saw_time = false;
            for (const auto& token : split_value_tokens(item)) {
                const auto lower = ascii_lower(token);
                if (is_css_time(lower)) {
                    if (!saw_time) duration = lower;
                    else delay = lower;
                    saw_time = true;
                } else if (lower == "linear" || lower == "ease" || lower == "ease-in"
                    || lower == "ease-out" || lower == "ease-in-out"
                    || lower.starts_with("cubic-bezier(")) {
                    timing = lower;
                } else if (lower != "normal") {
                    property = lower;
                }
            }
            properties.push_back(std::move(property));
            durations.push_back(std::move(duration));
            delays.push_back(std::move(delay));
            timings.push_back(std::move(timing));
        }
        const auto join = [](const std::vector<std::string>& values) {
            std::string result;
            for (const auto& value : values) {
                if (!result.empty()) result += ", ";
                result += value;
            }
            return result;
        };
        auto& animations = style.mutable_animations();
        animations.transition_property_value = join(properties);
        animations.transition_duration_value = join(durations);
        animations.transition_delay_value = join(delays);
        animations.transition_timing_function_value = join(timings);
        configure_style_transitions(style);
    }

} // namespace webscene_native::css
