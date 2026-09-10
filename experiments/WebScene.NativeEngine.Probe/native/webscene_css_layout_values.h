#pragma once
#include "webscene_css_box_values.h"
#include <cstdlib>
#include <charconv>
#include "webscene_css_property_mask.h"

namespace webscene_native::css {
inline bool has_multiple_grid_columns(std::string_view value)
{
    auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return false;
    value.remove_prefix(first);
    if (value.starts_with("repeat(")) {
        const auto count_start = value.find_first_of("0123456789", 7U);
        if (count_start != std::string_view::npos) {
            const auto count_end = value.find_first_not_of("0123456789", count_start);
            const auto count_text = value.substr(count_start, count_end - count_start);
            int count = 0;
            const auto result = std::from_chars(
                count_text.data(), count_text.data() + count_text.size(), count);
            if (result.ec == std::errc{} && count > 1) return true;
        }
    }

    size_t track_count = 0;
    size_t token_start = std::string_view::npos;
    int parenthesis_depth = 0;
    int bracket_depth = 0;
    auto finish_token = [&](size_t end) {
        if (token_start == std::string_view::npos) return;
        const auto token = value.substr(token_start, end - token_start);
        if (!token.empty() && token.front() != '[') ++track_count;
        token_start = std::string_view::npos;
    };
    for (size_t index = 0; index <= value.size(); ++index) {
        const auto character = index < value.size() ? value[index] : ' ';
        if (character == '(') ++parenthesis_depth;
        else if (character == ')' && parenthesis_depth > 0) --parenthesis_depth;
        else if (character == '[') ++bracket_depth;
        else if (character == ']' && bracket_depth > 0) --bracket_depth;
        const auto separator = parenthesis_depth == 0 && bracket_depth == 0
            && std::isspace(static_cast<unsigned char>(character));
        if (!separator && token_start == std::string_view::npos) token_start = index;
        if (separator) finish_token(index);
    }
    return track_count > 1U;
}

inline std::vector<node_style::grid_data::track> parse_simple_grid_tracks(
    const std::string& value)
{
    using grid_track = node_style::grid_data::track;
    const auto trim = [](std::string_view input) {
        const auto first = input.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) return std::string_view{};
        return input.substr(first, input.find_last_not_of(" \t\r\n") - first + 1U);
    };
    const auto parse_fraction = [&](std::string_view token) {
        token = trim(token);
        if (!token.ends_with("fr")) return 0.0F;
        token.remove_suffix(2U);
        if (token.empty()) return 1.0F;
        char* end = nullptr;
        const auto text = std::string(token);
        const auto result = std::strtof(text.c_str(), &end);
        return end != text.c_str() && *end == '\0' && result > 0 ? result : 0.0F;
    };
    const auto parse_track = [&](std::string_view raw) -> std::optional<grid_track> {
        const auto token = trim(raw);
        if (token.empty()) return std::nullopt;
        if (token == "auto" || token == "max-content") {
            grid_track track;
            track.kind = grid_track::sizing::automatic;
            return track;
        }
        if (token == "min-content") {
            grid_track track;
            track.kind = grid_track::sizing::min_content;
            return track;
        }
        if (const auto fraction = parse_fraction(token); fraction > 0) {
            grid_track track;
            track.kind = grid_track::sizing::fractional;
            track.fraction = fraction;
            return track;
        }
        if (token.starts_with("minmax(") && token.ends_with(')')) {
            const auto arguments = token.substr(7U, token.size() - 8U);
            auto comma = std::string_view::npos;
            auto depth = 0;
            for (size_t index = 0; index < arguments.size(); ++index) {
                if (arguments[index] == '(') ++depth;
                else if (arguments[index] == ')') --depth;
                else if (arguments[index] == ',' && depth == 0) {
                    comma = index;
                    break;
                }
            }
            if (comma == std::string_view::npos) return std::nullopt;
            grid_track track;
            track.kind = grid_track::sizing::minmax;
            const auto minimum = trim(arguments.substr(0, comma));
            const auto maximum = trim(arguments.substr(comma + 1U));
            if (minimum != "auto" && minimum != "min-content"
                && minimum != "max-content") {
                track.minimum = native_document::parse_length(std::string(minimum));
            }
            track.fraction = parse_fraction(maximum);
            if (track.fraction <= 0 && maximum != "auto"
                && maximum != "min-content" && maximum != "max-content") {
                track.maximum = native_document::parse_length(std::string(maximum));
            }
            return track;
        }
        grid_track track;
        track.kind = grid_track::sizing::fixed;
        track.minimum = native_document::parse_length(std::string(token));
        track.maximum = track.minimum;
        return track;
    };

    std::vector<std::string_view> tokens;
    size_t token_start = std::string_view::npos;
    auto parenthesis_depth = 0;
    auto bracket_depth = 0;
    for (size_t index = 0; index <= value.size(); ++index) {
        const auto character = index < value.size() ? value[index] : ' ';
        if (character == '(') ++parenthesis_depth;
        else if (character == ')' && parenthesis_depth > 0) --parenthesis_depth;
        else if (character == '[') ++bracket_depth;
        else if (character == ']' && bracket_depth > 0) --bracket_depth;
        const auto separator = parenthesis_depth == 0 && bracket_depth == 0
            && std::isspace(static_cast<unsigned char>(character));
        if (!separator && token_start == std::string_view::npos) token_start = index;
        if (separator && token_start != std::string_view::npos) {
            tokens.push_back(std::string_view(value).substr(token_start, index - token_start));
            token_start = std::string_view::npos;
        }
    }

    std::vector<grid_track> tracks;
    for (auto token : tokens) {
        token = trim(token);
        if (token.empty() || token.front() == '[') continue;
        if (token == "none" || token.starts_with("repeat(")) return {};
        const auto parsed = parse_track(token);
        if (!parsed.has_value()) return {};
        tracks.push_back(*parsed);
    }
    return tracks;
}


inline bool apply_grid_placement_declaration(
        node_style& style,
        std::string_view raw_name,
        const std::string& value)
    {
        const auto name = canonical_property_name(raw_name);
        if (name != "grid-area" && name != "grid-row"
            && name != "grid-row-start" && name != "grid-row-end"
            && name != "grid-column" && name != "grid-column-start"
            && name != "grid-column-end") {
            return false;
        }
        auto& grid = style.mutable_grid();
        const auto update_column_layout = [&] {
            grid.column_value = grid.column_start_value == "auto"
                && grid.column_end_value == "auto"
                ? "auto"
                : grid.column_start_value + " / " + grid.column_end_value;
            grid.span_all = grid.column_end_value != "auto";
            const auto& start = grid.column_start_value;
            grid.column_start = !start.empty()
                && std::all_of(start.begin(), start.end(), [](unsigned char character) {
                    return std::isdigit(character) != 0;
                }) ? std::atoi(start.c_str()) : 0;
        };
        if (name == "grid-row-start") {
            grid.row_start_value = value;
            return true;
        }
        if (name == "grid-row-end") {
            grid.row_end_value = value;
            return true;
        }
        if (name == "grid-column-start") {
            grid.column_start_value = value;
            update_column_layout();
            return true;
        }
        if (name == "grid-column-end") {
            grid.column_end_value = value;
            update_column_layout();
            return true;
        }
        const auto components = split_css_component_list(value, '/');
        const auto maximum = name == "grid-area" ? 4U : 2U;
        if (components.empty() || components.size() > maximum) return true;
        const auto component = [&](size_t index) {
            return index < components.size() ? components[index] : std::string{"auto"};
        };
        if (name == "grid-area") {
            grid.area_value = value;
            grid.row_start_value = component(0);
            grid.column_start_value = component(1);
            grid.row_end_value = component(2);
            grid.column_end_value = component(3);
            grid.row_value =
                grid.row_start_value + " / " + grid.row_end_value;
            update_column_layout();
            return true;
        }
        if (name == "grid-row") {
            grid.row_value = value;
            grid.row_start_value = component(0);
            grid.row_end_value = component(1);
            return true;
        }

        grid.column_start_value = component(0);
        grid.column_end_value = component(1);
        update_column_layout();
        // Preserve the authored one-component shorthand serialization rather
        // than inflating `2` to `2 / auto`.
        grid.column_value = value;
        return true;
    }

template<typename Decision,typename Protected>
bool apply_grid_value(dom_node& node,const std::string& name,const std::string& value,
    Decision& decision,Protected&& is_inline)
{
    if (name == "grid-template-columns" && !is_inline(inline_grid)) {
            auto& grid = node.style.mutable_grid();
            const auto first = value.find_first_not_of(" \t\r\n");
            const auto last = value.find_last_not_of(" \t\r\n");
            const auto trimmed = first == std::string::npos
                ? std::string_view{}
                : std::string_view(value).substr(first, last - first + 1U);
            grid.subgrid_columns = trimmed == "subgrid"
                || trimmed.starts_with("subgrid ");
            grid.template_columns = grid.subgrid_columns
                ? std::vector<node_style::grid_data::track>{}
                : parse_simple_grid_tracks(value);
            grid.two_columns = grid.subgrid_columns
                || grid.template_columns.size() > 1U
                || (grid.template_columns.empty() && has_multiple_grid_columns(value));
            decision.classification = "partially-supported";
            decision.semantic_slice =
                "component grids with fixed, auto, fractional, minmax, and column subgrid tracks";
        } else if (name == "grid-template-rows" && !is_inline(inline_grid)) {
            auto& grid = node.style.mutable_grid();
            grid.template_rows = parse_simple_grid_tracks(value);
            grid.fractional_rows = std::any_of(
                grid.template_rows.begin(),
                grid.template_rows.end(),
                [](const auto& track) { return track.fraction > 0; });
            decision.classification = "partially-supported";
            decision.semantic_slice = "fractional component rows and explicit fixed track lengths";
        } else if (name == "grid-auto-columns" && !is_inline(inline_grid)) {
            auto& grid = node.style.mutable_grid();
            grid.auto_columns = parse_simple_grid_tracks(value);
            decision.classification = "partially-supported";
            decision.semantic_slice =
                "fixed, auto, fractional, and minmax implicit column track lengths";
        } else if (name == "grid-auto-flow" && !is_inline(inline_grid)) {
            auto& grid = node.style.mutable_grid();
            grid.auto_flow_column = value == "column" || value == "column dense";
            decision.classification = "partially-supported";
            decision.semantic_slice = "row and column auto-flow without dense backfill";
        } else if (!is_inline(inline_grid)
            && apply_grid_placement_declaration(node.style, name, value)) {
            decision.classification = "supported";
            decision.semantic_slice =
                "grid-area, grid-row, grid-column, and placement longhand CSSOM values";
        } else { return false; }
        return true;
}
struct parsed_flex_flow
{
    flex_direction direction{flex_direction::row};
    bool reverse{false};
    bool wrap{false};
};

inline parsed_flex_flow parse_flex_flow(std::string_view value)
{
    parsed_flex_flow result{};
    std::istringstream stream{std::string(value)};
    std::string token;
    while (stream >> token) {
        std::transform(
            token.begin(),
            token.end(),
            token.begin(),
            [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
        if (token == "row" || token == "row-reverse"
            || token == "column" || token == "column-reverse") {
            result.direction = token == "row" || token == "row-reverse"
                ? flex_direction::row : flex_direction::column;
            result.reverse = token == "row-reverse" || token == "column-reverse";
        } else if (token == "wrap" || token == "wrap-reverse") {
            result.wrap = true;
        } else if (token == "nowrap") {
            result.wrap = false;
        }
    }
    return result;
}

template<typename Protected>
bool apply_flex_value(dom_node& node,const std::string& name,const std::string& value,
    Protected&& is_inline)
{
    if (name == "flex-direction" && !is_inline(inline_flex_direction)) {
            node.style.direction = value == "row" || value == "row-reverse"
                ? flex_direction::row : flex_direction::column;
            node.style.flex_reverse = value == "row-reverse" || value == "column-reverse";
        } else if (name == "flex-flow"
            && (!is_inline(inline_flex_direction) || !is_inline(inline_flex_wrap))) {
            const auto flow = parse_flex_flow(value);
            if (!is_inline(inline_flex_direction)) {
                node.style.direction = flow.direction;
                node.style.flex_reverse = flow.reverse;
            }
            if (!is_inline(inline_flex_wrap)) node.style.flex_wrap = flow.wrap;
        } else if (name == "flex-wrap" && !is_inline(inline_flex_wrap)) {
            node.style.flex_wrap = value == "wrap" || value == "wrap-reverse";
        } else if (name == "align-items" && !is_inline(inline_align_items)) {
            node.style.align_items = value == "center" ? align_mode::center
                : value == "flex-start" || value == "start" ? align_mode::start
                : value == "flex-end" || value == "end" ? align_mode::end
                : value == "baseline" || value == "first baseline" ? align_mode::baseline
                : align_mode::stretch;
        } else if (name == "vertical-align" && node.tag == "tr" && value == "middle"
            && !is_inline(inline_align_items)) {
            // A flex-backed row uses cross-axis alignment to preserve the table-row
            // meaning of vertical-align: middle for its cells.
            node.style.mutable_textual().vertical_align = value;
            node.style.align_items = align_mode::center;
        } else if (name == "align-self" && !is_inline(inline_align_self)) {
            node.style.align_self_specified = value != "auto";
            node.style.align_self = value == "center" ? align_mode::center
                : value == "flex-start" || value == "start" ? align_mode::start
                : value == "flex-end" || value == "end" ? align_mode::end
                : value == "baseline" || value == "first baseline" ? align_mode::baseline
                : align_mode::stretch;
        } else if (name == "justify-content" && !is_inline(inline_justify_content)) {
            node.style.justify_content = value == "center" ? justify_mode::center
                : value == "flex-end" || value == "end" ? justify_mode::end
                : value == "space-between" ? justify_mode::space_between
                : value == "space-around" ? justify_mode::space_around
                : value == "space-evenly" ? justify_mode::space_evenly
                : justify_mode::start;
        } else if (name == "flex-grow" && !is_inline(inline_flex_grow)) {
            node.style.flex_grow = std::strtof(value.c_str(), nullptr);
        } else if (name == "flex-shrink" && !is_inline(inline_flex_shrink)) {
            node.style.flex_shrink = std::max(0.0F, std::strtof(value.c_str(), nullptr));
        } else if (name == "flex-basis" && !is_inline(inline_flex_basis)) {
            node.style.flex_basis = native_document::parse_length(value);
        } else if (name == "flex"
            && (!is_inline(inline_flex_grow) || !is_inline(inline_flex_shrink)
                || !is_inline(inline_flex_basis))) {
            if (value == "none") {
                if (!is_inline(inline_flex_grow)) node.style.flex_grow = 0;
                if (!is_inline(inline_flex_shrink)) node.style.flex_shrink = 0;
                if (!is_inline(inline_flex_basis)) node.style.flex_basis = {};
            } else {
                std::istringstream stream(value);
                std::string grow_value;
                std::string shrink_value;
                std::string basis_value;
                stream >> grow_value >> shrink_value >> basis_value;
                if (!is_inline(inline_flex_grow) && !grow_value.empty()) {
                    node.style.flex_grow = grow_value == "auto" ? 1.0F
                        : std::max(0.0F, std::strtof(grow_value.c_str(), nullptr));
                }
                if (!is_inline(inline_flex_shrink)) {
                    node.style.flex_shrink = shrink_value.empty()
                        ? 1.0F : std::max(0.0F, std::strtof(shrink_value.c_str(), nullptr));
                }
                if (!is_inline(inline_flex_basis) && !basis_value.empty()) {
                    node.style.flex_basis = native_document::parse_length(basis_value);
                }
            }
        } else if (name == "box-sizing" && !is_inline(inline_box_sizing)) {
            node.style.border_box = value == "border-box";
        } else { return false; }
        return true;
}
} // namespace webscene_native::css
