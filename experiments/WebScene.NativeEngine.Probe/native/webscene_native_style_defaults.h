#pragma once
#include "webscene_native_dom.h"
#include <cctype>

namespace webscene_native {
inline display_mode native_default_display_for_tag(std::string_view tag)
{
    constexpr std::array<std::string_view, 8> non_rendered_tags{
        "base", "head", "link", "meta", "script", "style", "template", "title"};
    if (std::find(non_rendered_tags.begin(), non_rendered_tags.end(), tag)
        != non_rendered_tags.end()) {
        return display_mode::none;
    }
    if (tag == "table") return display_mode::table;
    if (tag == "thead") return display_mode::table_header_group;
    if (tag == "tbody") return display_mode::table_row_group;
    if (tag == "tfoot") return display_mode::table_footer_group;
    if (tag == "tr") return display_mode::table_row;
    if (tag == "td" || tag == "th") return display_mode::table_cell;
    if (tag == "colgroup") return display_mode::table_column_group;
    if (tag == "col") return display_mode::table_column;
    if (tag == "caption") return display_mode::table_caption;
    if (tag == "li" || tag == "summary") return display_mode::list_item;
    // CSS starts elements at `display:inline`; the HTML UA stylesheet then
    // promotes only known structural elements. In particular autonomous
    // custom elements and HTMLUnknownElement remain inline. Treating every
    // unlisted tag as a block made consumer helpers such as <time-format>
    // split a single metadata line into several overlapping flex rows.
    constexpr std::array<std::string_view, 38> block_tags{
        "address", "article", "aside", "blockquote", "body", "center",
        "dd", "details", "dialog", "dir", "div", "dl", "dt", "fieldset",
        "figcaption", "figure", "footer", "form", "h1", "h2", "h3", "h4",
        "h5", "h6", "header", "hgroup", "html", "hr", "main", "menu",
        "nav", "ol", "p", "pre", "search", "section", "ul", "legend"};
    if (std::find(block_tags.begin(), block_tags.end(), tag) != block_tags.end()) {
        return display_mode::block;
    }
    constexpr std::array<std::string_view, 9> inline_block_tags{
        "button", "img", "input", "meter", "progress", "rect", "select",
        "textarea", "object"};
    if (std::find(inline_block_tags.begin(), inline_block_tags.end(), tag)
        != inline_block_tags.end()) {
        return display_mode::inline_block;
    }
    return display_mode::inline_flow;
}

inline display_mode native_default_display_for_node(const dom_node& node)
{
    if (node.kind != dom_node_kind::element) {
        return node.kind == dom_node_kind::text
            ? display_mode::inline_flow
            : display_mode::none;
    }
    if (node.attributes.contains("hidden")) return display_mode::none;
    if (node.tag == "dialog" && !node.attributes.contains("open")) return display_mode::none;
    if (node.tag == "input") {
        const auto type = node.attributes.find("type");
        if (type != node.attributes.end()) {
            auto value = type->second;
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            if (value == "hidden") return display_mode::none;
        }
    }
    return native_default_display_for_tag(node.tag);
}

}
