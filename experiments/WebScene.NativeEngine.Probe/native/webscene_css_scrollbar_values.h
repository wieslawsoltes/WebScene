#pragma once
#include "webscene_css_property_mask.h"

namespace webscene_native::css {
// Standard scrollbar properties use the same overlay geometry as typed styles.
// This helper validates before updating priority or computed values.
template<class Decision>
void apply_scrollbar_value(dom_node& node,const css_declaration& declaration,
                           const std::string& value,Decision& decision) {
    const bool width=declaration.name=="scrollbar-width";
    const auto mask=width?inline_scrollbar_width:inline_scrollbar_color;
    if(!declaration.important && ((node.style.inline_property_mask|node.style.important_property_mask)&mask)) return;
    float thickness=6;
    bool hidden=false;
    uint32_t thumb=0xA0A0A0D0U,track=0x7F7F7F40U;
    bool valid=true;
    if(width) {
        if(value=="inherit" && node.parent) {
            thickness=node.parent->style.scrollbar().width;
            hidden=node.parent->style.scrollbar_hidden;
        } else if(value=="none") hidden=true;
        else if(value=="thin") thickness=4;
        else if(value!="auto" && value!="initial" && value!="unset" && value!="inherit") valid=false;
    } else if(value=="inherit" || value=="unset") {
        if(node.parent) {thumb=node.parent->style.scrollbar().thumb_rgba;track=node.parent->style.scrollbar().track_rgba;}
    } else if(value!="auto" && value!="initial") {
        const auto tokens=split_value_tokens(value);
        valid=tokens.size()==2;
        if(valid) {
            thumb=native_document::parse_color(tokens[0]);track=native_document::parse_color(tokens[1]);
            valid=is_explicit_color_token(tokens[0],thumb) && is_explicit_color_token(tokens[1],track);
        }
    }
    if(!valid) {
        decision.classification="unsupported";
        decision.semantic_slice=width?"auto, thin, none and inheritance":"two explicit colors, auto and inheritance";
        return;
    }
    if(declaration.important) node.style.important_property_mask|=mask;
    auto& bar=node.style.mutable_scrollbar();
    if(width) {bar.width=bar.height=thickness;node.style.scrollbar_hidden=hidden;}
    else {bar.thumb_rgba=thumb;bar.track_rgba=track;}
}
}
