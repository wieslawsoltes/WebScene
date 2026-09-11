#pragma once
#include "webscene_css_box_values.h"
#include <cstdlib>
#include <charconv>
#include <cmath>

namespace webscene_native::css {
inline std::optional<float> css_time_ms(std::string_view text) {
    text=trim_css_view(text);
    float multiplier=1;
    if(text.ends_with("ms")) text.remove_suffix(2);
    else if(text.ends_with('s')) { text.remove_suffix(1); multiplier=1000; }
    else return std::nullopt;
    if(text.empty()) return std::nullopt;
    if(text.front()=='+') text.remove_prefix(1);
    if(text.empty() || (text.front()!='-' && text.front()!='.' &&
        (text.front()<'0' || text.front()>'9')) || text.back()<'0' || text.back()>'9') return std::nullopt;
    float value{};
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() ||
        !std::isfinite(value*multiplier)) return std::nullopt;
    return value*multiplier;
}
inline bool is_css_time(std::string_view value) { return css_time_ms(value).has_value(); }
inline float parse_css_time_ms(std::string value) { return css_time_ms(value).value_or(0); }

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

inline void apply_animation_shorthand(node_style& style, const std::string& value)
    {
        auto name = std::string("none");
        auto duration = std::string("0s");
        auto delay = std::string("0s");
        auto timing = std::string("ease");
        auto iterations = std::string("1");
        auto saw_time = false;
        const auto first = split_css_component_list(value, ',');
        for (const auto& token : split_value_tokens(
                 first.empty() ? std::string_view{} : std::string_view(first.front()))) {
            const auto lower = ascii_lower(token);
            if (is_css_time(lower)) {
                if (!saw_time) duration = lower;
                else delay = lower;
                saw_time = true;
            } else if (lower == "linear" || lower == "ease" || lower == "ease-in"
                || lower == "ease-out" || lower == "ease-in-out"
                || lower.starts_with("cubic-bezier(")) {
                timing = lower;
            } else if (lower == "infinite"
                || std::all_of(lower.begin(), lower.end(), [](unsigned char character) {
                    return std::isdigit(character) || character == '.';
                })) {
                iterations = lower;
            } else if (lower != "normal" && lower != "none"
                && lower != "forwards" && lower != "backwards" && lower != "both"
                && lower != "running" && lower != "paused"
                && lower != "alternate" && lower != "alternate-reverse"
                && lower != "reverse") {
                name = token;
            }
        }
        auto& animations = style.mutable_animations();
        animations.animation_name_value = name;
        animations.animation_duration_value = duration;
        animations.animation_delay_value = delay;
        animations.animation_timing_function_value = timing;
        animations.animation_iteration_count_value = iterations;
    }

inline void configure_keyframes(node_style& style,
    const std::unordered_map<std::string,css_opacity_keyframes>& definitions)
    {
        if (!style.has_animation_data()) return;
        auto& animations = style.mutable_animations();
        animations.opacity_keyframes.clear();
        animations.opacity_keyframe_animation_signature.clear();
        animations.rotation_keyframes.clear();
        animations.rotation_keyframe_animation_signature.clear();
        if (animations.animation_name_value == "none") return;
        const auto names = split_css_component_list(animations.animation_name_value, ',');
        if (names.empty()) return;
        const auto name = ascii_lower(trim_value(names.front()));
        const auto definition = definitions.find(name);
        if (name == "none" || definition == definitions.end()) return;
        const auto durations = split_css_component_list(animations.animation_duration_value, ',');
        const auto delays = split_css_component_list(animations.animation_delay_value, ',');
        const auto timings = split_css_component_list(
            animations.animation_timing_function_value, ',');
        const auto iteration_counts = split_css_component_list(
            animations.animation_iteration_count_value, ',');
        animations.opacity_keyframe_duration_ms = durations.empty()
            ? 0 : std::max(0.0F, parse_css_time_ms(durations.front()));
        animations.opacity_keyframe_delay_ms = delays.empty()
            ? 0 : parse_css_time_ms(delays.front());
        const auto iteration = iteration_counts.empty()
            ? std::string("1") : ascii_lower(trim_value(iteration_counts.front()));
        animations.opacity_keyframe_iterations = iteration == "infinite"
            ? std::numeric_limits<float>::infinity()
            : std::max(0.0F, std::strtof(iteration.c_str(), nullptr));
        node_style::transition_timing animation_timing;
        if (!timings.empty()) parse_transition_timing(timings.front(), animation_timing);
        animations.opacity_keyframe_x1 = animation_timing.x1;
        animations.opacity_keyframe_y1 = animation_timing.y1;
        animations.opacity_keyframe_x2 = animation_timing.x2;
        animations.opacity_keyframe_y2 = animation_timing.y2;
        animations.opacity_keyframes = definition->second.opacity_stops;
        animations.rotation_keyframes = definition->second.rotation_stops;
        if (animations.opacity_keyframe_duration_ms <= 0
            || animations.opacity_keyframe_iterations == 0
            || (animations.opacity_keyframes.size() < 2U
                && animations.rotation_keyframes.size() < 2U)) return;
        std::ostringstream signature;
        signature << name << '|' << animations.opacity_keyframe_duration_ms << '|'
            << animations.opacity_keyframe_delay_ms << '|'
            << animations.opacity_keyframe_iterations << '|'
            << animations.opacity_keyframe_x1 << ',' << animations.opacity_keyframe_y1 << ','
            << animations.opacity_keyframe_x2 << ',' << animations.opacity_keyframe_y2;
        const auto base_signature = signature.str();
        if (animations.opacity_keyframes.size() >= 2U) {
            signature.str(base_signature);
            signature.clear();
            for (const auto& stop : animations.opacity_keyframes) {
                signature << '|' << stop.offset << ':' << stop.opacity;
            }
            animations.opacity_keyframe_animation_signature = signature.str();
        }
        if (animations.rotation_keyframes.size() >= 2U) {
            signature.str(base_signature);
            signature.clear();
            for (const auto& stop : animations.rotation_keyframes) {
                signature << '|' << stop.offset << ':' << stop.degrees;
            }
            animations.rotation_keyframe_animation_signature = signature.str();
        }
    }

inline void append_keyframe(
        css_opacity_keyframes& definition,
        std::string selector,
        const std::vector<css_declaration>& declarations)
    {
        const auto opacity = std::find_if(
            declarations.begin(), declarations.end(), [](const auto& declaration) {
                return declaration.name == "opacity";
            });
        const auto transform = std::find_if(
            declarations.begin(), declarations.end(), [](const auto& declaration) {
                return declaration.name == "transform";
            });
        const auto rotation_degrees = [&]() -> std::optional<float> {
            if (transform == declarations.end()) return std::nullopt;
            auto value = ascii_lower(trim_value(transform->value));
            const auto rotate = value.find("rotate(");
            if (rotate == std::string::npos) return std::nullopt;
            const auto close = value.find(')', rotate + 7U);
            if (close == std::string::npos) return std::nullopt;
            auto angle = trim_value(value.substr(rotate + 7U, close - rotate - 7U));
            auto multiplier = 1.0F;
            if (angle.ends_with("turn")) {
                angle.resize(angle.size() - 4U);
                multiplier = 360.0F;
            } else if (angle.ends_with("deg")) {
                angle.resize(angle.size() - 3U);
            } else if (angle.ends_with("rad")) {
                angle.resize(angle.size() - 3U);
                multiplier = 57.29577951308232F;
            } else return std::nullopt;
            return std::strtof(angle.c_str(), nullptr) * multiplier;
        }();
        for (auto component : split_css_component_list(selector, ',')) {
            component = ascii_lower(trim_value(std::move(component)));
            float offset = -1;
            if (component == "from") offset = 0;
            else if (component == "to") offset = 1;
            else if (component.ends_with('%')) {
                component.pop_back();
                offset = std::strtof(component.c_str(), nullptr) / 100.0F;
            }
            if (offset < 0 || offset > 1) continue;
            if (opacity != declarations.end()) {
                definition.opacity_stops.push_back({
                    offset,
                    std::clamp(std::strtof(opacity->value.c_str(), nullptr), 0.0F, 1.0F)});
            }
            if (rotation_degrees.has_value()) {
                definition.rotation_stops.push_back({offset, *rotation_degrees});
            }
        }
    }

inline void finish_keyframes(
        std::unordered_map<std::string,css_opacity_keyframes>& definitions,
        std::string name,
        css_opacity_keyframes definition)
    {
        const auto normalize = [](auto& stops) {
            std::stable_sort(
                stops.begin(), stops.end(),
                [](const auto& left, const auto& right) { return left.offset < right.offset; });
            using stop_type = typename std::decay_t<decltype(stops)>::value_type;
            std::vector<stop_type> unique;
            for (const auto& stop : stops) {
                if (!unique.empty()
                    && std::abs(unique.back().offset - stop.offset) < 0.0001F) {
                    unique.back() = stop;
                } else {
                    unique.push_back(stop);
                }
            }
            stops = std::move(unique);
        };
        normalize(definition.opacity_stops);
        normalize(definition.rotation_stops);
        if (definition.rotation_stops.size() == 1U
            && definition.rotation_stops.front().offset > 0) {
            definition.rotation_stops.insert(definition.rotation_stops.begin(), {0, 0});
        }
        if (definition.opacity_stops.size() >= 2U
            || definition.rotation_stops.size() >= 2U) {
            definitions[ascii_lower(trim_value(std::move(name)))] =
                std::move(definition);
        }
    }

} // namespace webscene_native::css
