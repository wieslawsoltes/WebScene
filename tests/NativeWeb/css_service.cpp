#include "webscene_css_declarations.h"
#include "webscene_css_selectors.h"
#include "webscene_css_matching.h"
#include "webscene_css_compound.h"
#include <iostream>
// Test host deliberately has no JavaScript context. Production hosts own their
// class/selector caches and project input/URL state through the same contract.
struct native_selector_test_host {
    webscene_native::native_document& document;
    const webscene_native::dom_node* hover_target{};
    const webscene_native::dom_node* active_element{};
    bool focus_visible{};
    std::optional<std::string> selector_target_hash() const { return "#destination"; }
    bool has_class(const webscene_native::dom_node& node,std::string_view wanted) const {
        std::istringstream words(node.class_name);
        for(std::string value;words>>value;) if(value==wanted) return true;
        return false;
    }
    bool is_text_control(const webscene_native::dom_node* node) const {
        return node && (node->tag=="input" || node->tag=="textarea");
    }
    bool css_selector_matches(const webscene_native::dom_node& node,std::string_view text,
        const webscene_native::dom_node* scope=nullptr) const {
        const auto list=webscene_native::css::compile_selector_list(text);
        for(const auto& selector:list.selectors) {
            if(!selector.compounds.empty() && webscene_native::css::selector_matches(document,node,
                selector,selector.compounds.size()-1,scope,[&](const auto& n,const auto& c,const auto* root) {
                    return webscene_native::css::compound_matches(*this,n,c,root);
                })) return true;
        }
        return false;
    }
    const webscene_native::dom_node* query_selector_node(webscene_native::dom_node& root,
        std::string_view text,bool include_root) const {
        if(include_root && css_selector_matches(root,text,&root)) return &root;
        for(auto* child:root.children) {
            if(!child) continue;
            if(css_selector_matches(*child,text,&root)) return child;
            if(auto* found=query_selector_node(*child,text,false)) return found;
        }
        return nullptr;
    }
};
int main() {
    using webscene_native::css::parse_declarations;
    const auto values=parse_declarations(R"CSS(
      COLOR: red !important; --Theme: blue; --theme: green;
      background:linear-gradient(125deg,var(--Theme),rgb(0,0,0));
      content:"a;b:c"; --empty: ; -unknown:ignored;
    )CSS",true);
    if(values.size()!=6 || values[0].name!="color" || !values[0].important ||
       values[1].name!="--Theme" || values[2].name!="--theme" ||
       values[3].value.find("linear-gradient")==std::string::npos ||
       values[4].value!=R"("a;b:c")" || values[5].value!=" ") return 1;
    const auto recovered=parse_declarations("bad; color:blue; width:12px");
    if(recovered.size()!=2 || recovered[0].value!="blue") return 2;
    const auto escaped=parse_declarations(R"(c\6flor: red; --\54heme: black)");
    if(escaped.size()!=2 || escaped[0].name!="color" || escaped[1].name!="--Theme") return 3;
    const auto selectors=webscene_native::css::compile_selector_list(
        R"(#toolbar > button.active:hover, .panel::before)");
    if(selectors.selectors.size()!=2 || selectors.selectors[0].compounds.size()!=2 ||
       selectors.selectors[0].combinators[0]!='>' ||
       !selectors.selectors[1].compiled_compounds[0].pseudo_element) return 4;
    const auto escaped_selector=webscene_native::css::compile_selector(R"(.a\:b)");
    if(escaped_selector.compiled_compounds.size()!=1 ||
       escaped_selector.compiled_compounds[0].identities[0].second!="a:b") return 5;
    if(!webscene_native::css::compile_selector_list("div > > span").selectors.empty()) return 6;
    webscene_native::dom_node node;
    node.attributes["data-state"]="active selected";
    node.attributes["lang"]="en-GB";
    if(!webscene_native::css::attribute_matches(node,R"(data-state~="selected")") ||
       !webscene_native::css::attribute_matches(node,R"(lang|="en")") ||
       webscene_native::css::attribute_matches(node,R"(data-state^="")")) return 7;
    node.attributes["data-state"]="idle";
    if(webscene_native::css::attribute_matches(node,R"(data-state~="selected")")) return 8;
    using webscene_native::css::nth_matches;
    if(!nth_matches("2n+1",3) || nth_matches("2n+1",2) ||
       !nth_matches("-n+3",2) || nth_matches("-n+3",4) ||
       !nth_matches("n-2147483648",1) || nth_matches("n+oops",1)) return 9;
    webscene_native::native_document document;
    auto& fieldset=document.create_element("fieldset");
    auto& legend=document.create_element("legend");
    auto& exempt=document.create_element("button");
    auto& blocked=document.create_element("input");
    document.append_child(document.body(),fieldset);
    document.append_child(fieldset,legend);
    document.append_child(legend,exempt);
    document.append_child(fieldset,blocked);
    fieldset.attributes["disabled"]="";
    using webscene_native::css::is_actually_disabled;
    if(!is_actually_disabled(document,blocked) || is_actually_disabled(document,exempt)) return 10;
    fieldset.attributes.erase("disabled");
    if(is_actually_disabled(document,blocked)) return 11;
    blocked.attributes["disabled"]="";
    if(!is_actually_disabled(document,blocked)) return 12;
    auto& select=document.create_element("select");
    auto& option=document.create_element("option");
    document.append_child(document.body(),select); document.append_child(select,option);
    select.attributes["disabled"]="";
    if(!is_actually_disabled(document,option)) return 13;
    webscene_native::css::interaction_state interaction{&exempt,&exempt,false};
    using webscene_native::css::interaction_matches;
    if(!interaction_matches(document,fieldset,"hover",interaction,false) ||
       !interaction_matches(document,fieldset,"focus-within",interaction,false) ||
       interaction_matches(document,fieldset,"focus",interaction,false) ||
       interaction_matches(document,exempt,"focus-visible",interaction,false)) return 14;
    interaction.focus_visible=true;
    if(!interaction_matches(document,exempt,"focus-visible",interaction,false)) return 15;
    interaction={nullptr,&blocked,false};
    if(interaction_matches(document,fieldset,"hover",interaction,false) ||
       interaction_matches(document,exempt,"focus",interaction,false) ||
       !interaction_matches(document,blocked,"focus-visible",interaction,true)) return 16;
    using webscene_native::css::language_matches;
    using webscene_native::css::direction_matches;
    fieldset.attributes["lang"]="en-GB";
    fieldset.attributes["dir"]="rtl";
    if(!language_matches(document,exempt,"EN") ||
       language_matches(document,exempt,"fr") ||
       !direction_matches(document,exempt,"rtl") || direction_matches(document,exempt,"ltr")) return 17;
    legend.attributes["lang"]="fr";
    legend.attributes["dir"]="ltr";
    if(language_matches(document,exempt,"en") || !language_matches(document,exempt,"fr") ||
       !direction_matches(document,exempt,"ltr")) return 18;
    legend.attributes["lang"]="";
    if(language_matches(document,exempt,"en")) return 19;
    if(!direction_matches(document,select,"ltr")) return 20;
    using webscene_native::css::checked_matches;
    auto& second_option=document.create_element("option");
    document.append_child(select,second_option);
    if(!checked_matches(option) || checked_matches(second_option)) return 21;
    second_option.attributes["selected"]="";
    if(checked_matches(option) || !checked_matches(second_option)) return 22;
    second_option.mutable_form_control().selectedness_initialized=true;
    second_option.mutable_form_control().selectedness=false;
    if(checked_matches(second_option)) return 23;
    blocked.attributes["type"]="checkbox";blocked.attributes["checked"]="";
    if(!checked_matches(blocked)) return 24;
    blocked.mutable_form_control().checkedness_initialized=true;
    blocked.mutable_form_control().checkedness=false;
    if(checked_matches(blocked)) return 25;
    blocked.id_attribute="destination";
    if(!webscene_native::css::target_matches(blocked,"#destination") ||
       webscene_native::css::target_matches(blocked,"#") ||
       webscene_native::css::target_matches(blocked,"#elsewhere")) return 26;
    const auto tag_match=[](const webscene_native::dom_node& n,
        const webscene_native::css::compiled_css_compound& c,const webscene_native::dom_node*) {
        return c.valid && c.tag==n.tag;
    };
    const auto matches=[&](const webscene_native::dom_node& n,std::string_view text) {
        const auto prepared=webscene_native::css::compile_selector(text);
        return !prepared.compounds.empty() && webscene_native::css::selector_matches(document,n,
            prepared,prepared.compounds.size()-1,nullptr,tag_match);
    };
    if(!matches(exempt,"fieldset button") || !matches(exempt,"legend > button") ||
       matches(exempt,"fieldset > button") || !matches(blocked,"legend + input") ||
       !matches(blocked,"legend ~ input") || matches(exempt,"input + button")) return 27;
    native_selector_test_host host{document,&exempt,&exempt,true};
    exempt.class_name="primary";
    if(!host.css_selector_matches(exempt,"fieldset > legend > button.primary:focus:hover") ||
       host.css_selector_matches(exempt,"button:disabled") ||
       !host.css_selector_matches(blocked,"input:disabled:target") ||
       !host.css_selector_matches(fieldset,"fieldset:has(> legend):focus-within") ||
       !host.css_selector_matches(exempt,"button:not(.secondary):is(.primary, .other)")) return 28;
    std::cout<<"V8-free shared CSS declaration service passed\n";
}
