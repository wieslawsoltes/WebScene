#pragma once
#include "webscene_css_stylesheet_owner.h"
#include "webscene_css_query.h"
#include "webscene_css_application.h"
#include "webscene_css_cascade_reset.h"
#include "webscene_css_cascade_application.h"
#include "webscene_css_cascade_finalization.h"
#include "webscene_css_rule_matching.h"
#include "webscene_css_declarations.h"

namespace webscene_native::css {
// Apply one node in parent-before-child order. The host supplies resource loading,
// diagnostics and scheduling; it must not mutate sheets from either callback.
template<class LoadSvg,class Observe>
bool apply_native_cascade(native_document& document,dom_node& node,
    const stylesheet_owner& sheets,query_host& query,
    const std::unordered_map<std::string,std::string>& variables,
    bool focused,LoadSvg&& load_svg,Observe&& observe,bool inline_attributes=false)
{
    if(node.tag=="input" || node.tag=="textarea") forms::ensure_text_value(node);
    const auto previous=node.style;
    if(node.dialog_state)node.dialog_state->backdrop_rgba=0;
    if(node.kind!=dom_node_kind::element) {
        node.style.display=node.kind==dom_node_kind::text?display_mode::inline_flow:display_mode::none;
    } else {
        std::vector<css_declaration> inline_values;
        if(inline_attributes) {
            node.style.inline_property_mask=0;
            node.clear_authored_style();
            const auto attribute=node.attributes.find("style");
            if(attribute!=node.attributes.end()) inline_values=parse_declarations(attribute->second);
            // Seed inline custom properties before matching dependent values.
            // Preserve priority when the attribute declares the same name twice.
            for(const auto& declaration:inline_values) {
                auto& authored=node.mutable_authored_style();
                if(!declaration.important && authored.important_declarations.contains(declaration.name)) continue;
                authored.declarations[declaration.name]=declaration.value;
                if(declaration.important) authored.important_declarations.insert(declaration.name);
            }
        }
        reset_cascaded_style(node,variables);
        auto indices=sheets.candidates(node,focused);
        auto matched=match_candidates(document,node,sheets.state().rules,indices,
            [&](const auto& subject,const auto& rule,const auto&) { return query.matches_prepared(subject,rule.payload->compiled_pseudo_origin); },
            [&](const auto& subject,const auto& rule) { return query.matches_prepared(subject,rule.compiled_selector()); });
        apply_matched_declarations(node,matched.ordinary,[&](const css_declaration& declaration,bool inline_origin) {
            property_result result;
            apply_declaration(document,node,declaration,variables,inline_origin,result,load_svg,[](bool) {});
            observe(declaration,result);
        });
        if(inline_attributes) {
            for(bool important:{false,true}) for(const auto& declaration:inline_values) {
                if(declaration.important!=important || declaration.name.starts_with("--")) continue;
                property_result result;
                apply_declaration(document,node,declaration,variables,true,result,load_svg,[](bool) {});
                observe(declaration,result);
            }
        }
        recompute_cascaded_line_height(node,matched.ordinary,variables);
        recompute_inline_font_relative_metrics(node);
        bool backdrop_important=false;
        for(const auto& [kind,rule]:matched.pseudo) {
            for(const auto& declaration:rule->declarations()) {
                if(kind==7) {
                    const auto result=apply_backdrop_declaration(node,declaration,variables,backdrop_important);
                    observe(declaration,result);
                }
                else if(kind>=3) apply_scrollbar_declaration(node,kind,declaration,variables);
                else {
                    auto& pseudo=kind==1?node.style.mutable_before_pseudo():node.style.mutable_after_pseudo();
                    property_result result;
                    apply_pseudo_declaration(node,pseudo,declaration,variables,result,[](bool) {});
                    observe(declaration,result);
                }
            }
        }
        if(node.style.before_pseudo().generated && previous.before_pseudo().generated)
            node.style.mutable_before_pseudo().layout=previous.before_pseudo().layout;
        if(node.style.after_pseudo().generated && previous.after_pseudo().generated)
            node.style.mutable_after_pseudo().layout=previous.after_pseudo().layout;
        configure_keyframes(node.style,sheets.state().opacity_keyframes);
        document.update_style_animations(node);
    }
    const bool layout_changed=!computed_layout_style_equal(previous,node.style);
    if(layout_changed) document.mark_dirty();
    else document.mark_scene_changed();
    return layout_changed;
}
// Explicit full-document refresh for stylesheet, viewport and DOM changes.
// This correctness path is not a per-frame operation. Hosts may later schedule
// smaller invalidated subtrees; callbacks must not mutate the tree during traversal.
template<class LoadSvg,class Observe>
bool apply_native_document_cascade(native_document& document,
    const stylesheet_owner& sheets,query_host& query,LoadSvg&& load_svg,Observe&& observe,bool inline_attributes=false)
{
    std::unordered_map<std::string,std::string> variables;
    std::unordered_set<std::string> important;
    rebuild_root_variables(sheets.state().rules,variables,important);
    const auto* focus=query.selector_interaction_state().focused;
    std::vector<dom_node*> pending{&document.body()};
    bool layout_changed=false;
    while(!pending.empty()) {
        auto* node=pending.back();
        pending.pop_back();
        layout_changed|=apply_native_cascade(document,*node,sheets,query,variables,
            node==focus,load_svg,observe,inline_attributes);
        for(auto child=node->children.rbegin();child!=node->children.rend();++child)
            if(*child) pending.push_back(*child);
    }
    return layout_changed;
}
} // namespace webscene_native::css
