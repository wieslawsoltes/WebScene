#pragma once
#include "webscene_css_box_application.h"
#include "webscene_css_layout_values.h"
#include "webscene_css_decoration_values.h"
#include "webscene_css_paint_values.h"
#include "webscene_css_visibility_values.h"
#include "webscene_css_text_values.h"
#include "webscene_css_reset.h"

namespace webscene_native::css {
// Apply an already-resolved declaration. The caller owns variable resolution,
// selector/cascade ordering, resource loading, and document invalidation.
template<typename Decision,typename LoadSvg>
void apply_resolved_declaration(native_document& document,dom_node& node,
    const css_declaration& declaration,const std::string& value,
    bool inline_origin,Decision& decision,LoadSvg&& load_svg)
{
    const auto& name=declaration.name;
        if (margin_sides(name)!=0U) {
            css::apply_margin(node,declaration,value,inline_origin);
            return;
        }
        const auto property_mask = css::property_mask(name);
        if (declaration.important && property_mask != 0U) {
            node.style.important_property_mask |= property_mask;
        }
        const auto is_inline = [&](uint64_t property) {
            return !declaration.important
                && ((node.style.inline_property_mask | node.style.important_property_mask)
                    & property) != 0U;
        };
        if (name == "all") {
            if (value != "unset" || declaration.important) {
                decision.classification = "unsupported";
                decision.semantic_slice =
                    "non-important unset across modeled properties, excluding custom properties";
                return;
            }

            css::apply_all_unset(node);
            decision.classification = "partially-supported";
            decision.semantic_slice =
                "non-important unset across modeled properties, excluding custom properties";
            return;
        }
        if (name == "color" && !is_inline(inline_color)
            && (value == "inherit" || value == "unset" || value == "initial")) {
            node.style.foreground_rgba = value == "initial" ? 0x000000FFU : 0U;
            return;
        }
        if ((name == "background" || name == "background-color")
            && !is_inline(inline_background)
            && (value == "unset" || value == "initial" || value == "inherit")) {
            const auto explicit_document_element = node.tag == "html"
                && node.parent == &document.body();
            node.style.background_rgba = value == "inherit" && node.parent != nullptr
                && !explicit_document_element
                ? node.parent->style.background_rgba
                : 0U;
            node.style.background_current_color = false;
            return;
        }
        if(css::apply_decoration_value(node,name,value,decision,is_inline)) return;
        if (css::apply_box_metrics(node,name,value,is_inline)) {
        } else if (css::apply_structure_value(document,node,name,value,decision,is_inline)) {
        } else if (css::apply_grid_value(node,name,value,decision,is_inline)) {
        } else if (css::apply_flex_value(node,name,value,is_inline)) {
        } else if (css::apply_paint_value(node,name,value,decision,is_inline,
            load_svg)) {
        } else if (css::apply_visibility_value(document,node,name,value,is_inline)) {
        } else if (css::apply_text_value(node,name,value,decision,is_inline)) {
        } else if (name == "border-style") {
            decision.classification = "partially-supported";
            decision.semantic_slice = "none";
        } else if (name == "vertical-align") {
            node.style.mutable_textual().vertical_align = value;
            decision.classification = "partially-supported";
            decision.semantic_slice = "middle on table rows and inline line boxes";
        } else if (property_mask == 0U) {
            decision.classification = "unsupported";
        }
}
} // namespace webscene_native::css
