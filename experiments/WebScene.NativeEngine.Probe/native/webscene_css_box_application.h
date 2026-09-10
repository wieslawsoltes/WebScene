#pragma once
#include "webscene_css_property_mask.h"

namespace webscene_native::css {
// Apply resolved box values after custom-property substitution. The owner supplies
// the existing inline/important precedence test; unhandled values return false.
template<typename Protected>
bool apply_box_metrics(dom_node& node,const std::string& name,
    const std::string& value,Protected&& is_inline)
{
    if (name == "width" && !is_inline(inline_width)) {
        node.style.width = value == "inherit" && node.parent != nullptr
            ? node.parent->style.width
            : value == "initial" || value == "unset" || value == "revert"
                ? css_length{}
                : native_document::parse_length(value);
    } else if (name == "height" && !is_inline(inline_height)) {
        node.style.height = value == "inherit" && node.parent != nullptr
            ? node.parent->style.height
            : value == "initial" || value == "unset" || value == "revert"
                ? css_length{}
                : native_document::parse_length(value);
    } else if (name == "min-width" && !is_inline(inline_min_width)) {
        node.style.min_width = native_document::parse_length(value);
    } else if (name == "min-height" && !is_inline(inline_min_height)) {
        node.style.min_height = native_document::parse_length(value);
    } else if (name == "max-width" && !is_inline(inline_max_width)) {
        node.style.max_width = value == "none" || value == "fit-content"
            || value == "max-content" || value == "min-content"
            ? css_length{} : native_document::parse_length(value);
    } else if (name == "max-height" && !is_inline(inline_max_height)) {
        node.style.max_height = value == "none" || value == "fit-content"
            || value == "max-content" || value == "min-content"
            ? css_length{} : native_document::parse_length(value);
    } else if ((name == "left" || name == "inset-inline-start") && !is_inline(inline_left)) {
        node.style.left = parse_inset_length(value);
    } else if ((name == "top" || name == "inset-block-start") && !is_inline(inline_top)) {
        node.style.top = parse_inset_length(value);
    } else if ((name == "right" || name == "inset-inline-end") && !is_inline(inline_right)) {
        node.style.right = parse_inset_length(value);
    } else if ((name == "bottom" || name == "inset-block-end") && !is_inline(inline_bottom)) {
        node.style.bottom = parse_inset_length(value);
    }
    else if (name == "inset") {
        std::vector<css_length> values;
        std::istringstream stream(value);
        for (std::string token; stream >> token;) {
            values.push_back(native_document::parse_length(token));
        }
        if (!values.empty()) {
            const auto top = values[0];
            const auto right = values.size() > 1 ? values[1] : values[0];
            const auto bottom = values.size() > 2 ? values[2] : values[0];
            const auto left = values.size() > 3 ? values[3]
                : values.size() > 1 ? values[1] : values[0];
            if (!is_inline(inline_left)) node.style.left = left;
            if (!is_inline(inline_top)) node.style.top = top;
            if (!is_inline(inline_right)) node.style.right = right;
            if (!is_inline(inline_bottom)) node.style.bottom = bottom;
        }
    } else if (name == "padding" && !is_inline(inline_padding)) {
        std::vector<css_length> values;
        size_t cursor = 0;
        while (cursor < value.size()) {
            while (cursor < value.size()
                && std::isspace(static_cast<unsigned char>(value[cursor]))) ++cursor;
            if (cursor >= value.size()) break;
            auto end = value.find(' ', cursor);
            if (end == std::string::npos) end = value.size();
            values.push_back(native_document::parse_length(
                value.substr(cursor, end - cursor)));
            cursor = end + 1U;
        }
        if (!values.empty()) {
            node.style.padding_top = values[0];
            node.style.padding_right = values.size() > 1 ? values[1] : values[0];
            node.style.padding_bottom = values.size() > 2 ? values[2] : values[0];
            node.style.padding_left = values.size() > 3 ? values[3]
                : values.size() > 1 ? values[1] : values[0];
        }
    } else if ((name == "padding-block" || name == "padding-inline")
        && !is_inline(inline_padding)) {
        apply_padding_declaration(node.style, name, value);
    } else if ((name == "padding-left" || name == "padding-inline-start")
        && !is_inline(inline_padding)) {
        node.style.padding_left = native_document::parse_length(value);
    } else if ((name == "padding-right" || name == "padding-inline-end")
        && !is_inline(inline_padding)) {
        node.style.padding_right = native_document::parse_length(value);
    } else if ((name == "padding-top" || name == "padding-block-start")
        && !is_inline(inline_padding)) {
        node.style.padding_top = native_document::parse_length(value);
    } else if ((name == "padding-bottom" || name == "padding-block-end")
        && !is_inline(inline_padding)) {
        node.style.padding_bottom = native_document::parse_length(value);
    } else if (name.starts_with("margin") && !is_inline(inline_margin)
        && apply_margin_declaration(node.style, name, value)) {
        // Share the function-aware component parser with inline CSSOM
        // updates. Whitespace inside calc()/min()/max() is not a side
        // separator in a margin shorthand.
    } else if (name == "gap" && !is_inline(inline_gap)) {
        std::istringstream stream(value);
        std::string row;
        std::string column;
        stream >> row >> column;
        node.style.row_gap = row == "normal" ? css_length{}
            : native_document::parse_length(row);
        node.style.column_gap = column.empty() ? node.style.row_gap
            : column == "normal" ? css_length{}
            : native_document::parse_length(column);
    } else if (name == "row-gap" && !is_inline(inline_gap)) {
        node.style.row_gap = value == "normal" ? css_length{}
            : native_document::parse_length(value);
    } else if (name == "column-gap" && !is_inline(inline_gap)) {
        node.style.column_gap = value == "normal" ? css_length{}
            : native_document::parse_length(value);
    } else { return false; }
    return true;
}
} // namespace webscene_native::css
