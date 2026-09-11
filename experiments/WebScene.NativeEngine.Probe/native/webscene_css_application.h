#pragma once
#include "webscene_css_box_application.h"
#include "webscene_css_layout_values.h"
#include "webscene_css_decoration_values.h"
#include "webscene_css_paint_values.h"
#include "webscene_css_visibility_values.h"
#include "webscene_css_text_values.h"
#include "webscene_css_reset.h"
#include "webscene_css_variables.h"
#include "webscene_css_scrollbar_values.h"

namespace webscene_native::css {
// Apply an already-resolved declaration. The caller owns variable resolution,
// selector/cascade ordering, resource loading, and document invalidation.
template<typename Decision,typename LoadSvg>
void apply_resolved_declaration(native_document& document,dom_node& node,
    const css_declaration& declaration,const std::string& value,
    bool inline_origin,Decision& decision,LoadSvg&& load_svg)
{
    const auto& name=declaration.name;
    if(name=="stroke-width") {
        apply_text_value(node,name,value,decision,[&](uint64_t mask) {
            return !declaration.important && ((node.style.inline_property_mask|node.style.important_property_mask)&mask)!=0;
        });
        if(declaration.important && decision.classification=="supported")
            node.style.important_property_mask|=inline_svg_stroke_width;
        return;
    }
    if(name=="scrollbar-width" || name=="scrollbar-color") {
        apply_scrollbar_value(node,declaration,value,decision);return;
    }
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
// Complete property application entry point; rule ordering and invalidation
// remain with the document cascade owner. Custom values stay live on the node.
template<typename Decision,typename LoadSvg,typename Resolved>
void apply_declaration(native_document& document,dom_node& node,
    const css_declaration& authored,const std::unordered_map<std::string,std::string>& variables,
    bool inline_origin,Decision& decision,LoadSvg&& load_svg,Resolved&& on_resolved)
{
    std::optional<css_declaration> normalized;
    if(authored.name=="-moz-transform" || authored.name=="-webkit-transform") {
        normalized=authored;normalized->name="transform";
    } else if(authored.name=="grid-gap" || authored.name=="grid-row-gap" || authored.name=="grid-column-gap") {
        normalized=authored;normalized->name=canonical_property_name(authored.name);
    }
    const auto& declaration=normalized?*normalized:authored;
    if(declaration.name.starts_with("--")) {
        apply_custom_property(node,declaration);
        return;
    }
    const auto contains_variable=declaration.value.find("var(")!=std::string::npos;
    auto resolved=contains_variable?resolve_value(node,declaration.value,variables):std::string{};
    const auto& value=contains_variable?resolved:declaration.value;
    if(contains_variable && value.empty()) {
        decision.classification="invalid-authoring";
        decision.semantic_slice="unresolved custom property at computed-value time";
        return;
    }
    on_resolved(contains_variable);
    apply_resolved_declaration(document,node,declaration,value,inline_origin,decision,load_svg);
}
} // namespace webscene_native::css
