#pragma once
#include "webscene_css_box_values.h"
#include <cstdlib>

namespace webscene_native::css {
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

} // namespace webscene_native::css
