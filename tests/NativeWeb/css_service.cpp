#include "webscene_css_declarations.h"
#include "webscene_css_selectors.h"
#include "webscene_css_matching.h"
#include "webscene_css_query.h"
#include "webscene_css_rule_operations.h"
#include "webscene_css_variables.h"
#include "webscene_css_box_values.h"
#include "webscene_css_transitions.h"
#include "webscene_css_layout_values.h"
#include "webscene_css_pseudo_values.h"
#include "webscene_css_stylesheet_sink.h"
#include "webscene_css_rule_payload.h"
#include "webscene_css_resources.h"
#include "webscene_css_rule_preparation.h"
#include <iostream>

struct stylesheet_test_host {
    struct rule {
        std::string selector;
        std::vector<webscene_native::css::css_declaration> declarations;
        std::vector<std::string> media;
        std::string address;
    };
    std::vector<rule> rules;
    std::vector<std::string> unsupported;
    bool inventory_media_query(const std::string&) { return true; }
    void record_feature(std::string_view, const std::string& name,
        std::string_view classification, const std::string&, std::string_view) {
        if(classification=="unsupported") unsupported.push_back(name);
    }
    void append_parsed_css_style_rule(std::string selector,
        std::vector<webscene_native::css::css_declaration> declarations,
        const std::vector<std::string>& media,const std::string& address) {
        webscene_native::css::prepare_style_rule(selector,std::move(declarations),media,address,
            [](const auto&) {},
            [&](const auto& prepared_selector,const auto& values,const auto& conditions) {
                rules.push_back({prepared_selector,values,conditions,address});
            });
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
    webscene_native::css::query_host host(document,1);
    host.set_interaction(&exempt,&exempt,true);
    host.set_target_hash("#destination");
    exempt.class_name="primary";
    if(!host.css_selector_matches(exempt,"fieldset > legend > button.primary:focus:hover") ||
       host.css_selector_matches(exempt,"button:disabled") ||
       !host.css_selector_matches(blocked,"input:disabled:target") ||
       !host.css_selector_matches(fieldset,"fieldset:has(> legend):focus-within") ||
       !host.css_selector_matches(exempt,"button:not(.secondary):is(.primary, .other)")) return 28;
    if(host.query_selector_node(fieldset,":scope > legend > button")!=&exempt) return 29;
    exempt.class_name="secondary";
    if(host.css_selector_matches(exempt,"button.primary") ||
       !host.css_selector_matches(exempt,"button.secondary")) return 30;
    host.set_interaction(nullptr,nullptr,false);
    if(host.css_selector_matches(exempt,"button:focus")) return 31;
    std::vector<webscene_native::css::css_rule> rules;
    const auto add_rule=[&](std::string selector,uint32_t specificity,std::string value,bool important) {
        auto payload=std::make_shared<webscene_native::css::css_rule_payload>();
        payload->selector=std::move(selector);payload->specificity=specificity;
        payload->declarations.push_back({"--theme",std::move(value),important});
        rules.push_back({payload});
    };
    add_rule(":root",10,"red",true);
    add_rule("html",1,"blue",false);
    add_rule(":root",10,"green",true);
    std::vector<size_t> candidates{2,0,1};
    webscene_native::css::sort_candidates(rules,candidates);
    if(candidates!=std::vector<size_t>{1,0,2}) return 32;
    std::unordered_map<std::string,std::string> variables;
    std::unordered_set<std::string> important;
    webscene_native::css::rebuild_root_variables(rules,variables,important);
    if(variables["--theme"]!="green" || !important.contains("--theme")) return 33;
    rules[2].media_matches=false;
    webscene_native::css::rebuild_root_variables(rules,variables,important);
    if(variables["--theme"]!="red") return 34;
    rules[0].shadow_scope_root_id=1;
    webscene_native::css::rebuild_root_variables(rules,variables,important);
    if(variables["--theme"]!="blue" || important.contains("--theme")) return 35;
    using webscene_native::css::resolve_value;
    variables["--size"]="12px";
    fieldset.style.mutable_custom_properties().values["--size"]="20px";
    if(resolve_value(exempt,"var(--size)",variables)!="20px" ||
       resolve_value(select,"var(--size)",variables)!="12px" ||
       resolve_value(exempt,"var(--absent, var(--size))",variables)!="20px") return 36;
    legend.style.mutable_custom_properties().values["--size"]="8px";
    if(resolve_value(exempt,"calc(var(--size) + var(--size))",variables)!="calc(8px + 8px)") return 37;
    legend.style.mutable_custom_properties().values["--size"]="9px";
    if(resolve_value(exempt,"var(--size)",variables)!="9px") return 38;
    using webscene_native::css::seed_inline_custom_properties;
    using webscene_native::css::apply_custom_property;
    exempt.mutable_authored_style().declarations["--size"]="11px";
    seed_inline_custom_properties(exempt);
    if(apply_custom_property(exempt,{"--size","20px",false}) ||
       resolve_value(exempt,"var(--size)",variables)!="11px") return 39;
    if(!apply_custom_property(exempt,{"--size","22px",true}) ||
       resolve_value(exempt,"var(--size)",variables)!="22px") return 40;
    exempt.mutable_authored_style().important_declarations.insert("--size");
    seed_inline_custom_properties(exempt);
    if(apply_custom_property(exempt,{"--size","30px",true}) ||
       resolve_value(exempt,"var(--size)",variables)!="11px") return 41;
    exempt.mutable_authored_style().declarations.erase("--size");
    exempt.mutable_authored_style().important_declarations.erase("--size");
    seed_inline_custom_properties(exempt);
    if(!apply_custom_property(exempt,{"--size","33px",false}) ||
       resolve_value(exempt,"var(--size)",variables)!="33px") return 42;
    webscene_native::node_style box;
    webscene_native::css::apply_margin_declaration(box,"margin","1px 2px 3px auto");
    if(box.margin_top.value!=1 || box.margin_right.value!=2 || box.margin_bottom.value!=3 || !box.margin_left_auto) return 43;
    webscene_native::css::apply_padding_declaration(box,"padding","2px 4px");
    if(box.padding_top.value!=2 || box.padding_left.value!=4 || box.padding_bottom.value!=2) return 44;
    exempt.mutable_authored_style().declarations["margin-left"]="7px";
    webscene_native::css::apply_margin_declaration(exempt.style,"margin-left","7px");
    webscene_native::css::apply_margin(exempt,{"margin","3px",false},"3px");
    if(exempt.style.margin_left.value!=7 || exempt.style.margin_right.value!=3) return 45;
    webscene_native::css::apply_margin(exempt,{"margin","9px",true},"9px");
    webscene_native::css::apply_margin(exempt,{"margin-right","1px",false},"1px");
    if(exempt.style.margin_left.value!=9 || exempt.style.margin_right.value!=9) return 46;
    webscene_native::css::apply_inset_declaration(box,"inset","1px 2px auto 4px");
    if(box.top.value!=1 || box.right.value!=2 || box.left.value!=4 ||
       box.bottom.unit!=webscene_native::length_unit::automatic) return 47;
    webscene_native::css::apply_border_declaration(box,"border","2px solid #123456");
    if(box.border_left_width.value!=2 || box.border_top_rgba!=0x123456ffu || box.border_top_current_color) return 48;
    webscene_native::css::apply_border_declaration(box,"border-left-color","currentColor");
    if(!box.border_left_current_color || box.border_top_current_color) return 49;
    webscene_native::css::apply_border_declaration(box,"border-width","1px 2px 3px 4px");
    if(box.border_top_width.value!=1 || box.border_left_width.value!=4) return 50;
    webscene_native::css::apply_border_declaration(box,"border","none");
    if(box.border_top_width.value!=0 || box.border_left_width.value!=0) return 51;
    using webscene_native::css::apply_corner_radius_declaration;
    apply_corner_radius_declaration("border-radius","1px 2px 3px 4px / 5px 6px 7px 8px",box);
    if(box.border_top_left_radius.value!=1 || box.border_bottom_left_radius.value!=4 ||
       box.border_top_left_radius_y().value!=5 || box.border_bottom_left_radius_y().value!=8) return 52;
    apply_corner_radius_declaration("border-top-left-radius","9px 10px",box);
    if(box.border_top_left_radius.value!=9 || box.border_top_left_radius_y().value!=10) return 53;
    webscene_native::node_style::pseudo_element pseudo;
    apply_corner_radius_declaration("border-radius","2px / 4px",pseudo);
    if(!pseudo.elliptical_border_radius || pseudo.border_top_right_radius_y.value!=4) return 54;
    apply_corner_radius_declaration("border-radius","3px",pseudo);
    if(pseudo.elliptical_border_radius || pseudo.border_top_left_radius.value!=3) return 55;
    using webscene_native::css::apply_transition_shorthand;
    apply_transition_shorthand(box,"opacity .2s ease-in 50ms, left 100ms linear -20ms");
    if(box.animations().opacity_transition.duration_ms!=200 ||
       box.animations().opacity_transition.delay_ms!=50 ||
       box.animations().left_transition.duration_ms!=100 ||
       box.animations().left_transition.delay_ms!=-20 ||
       box.animations().left_transition.x1!=0) return 56;
    apply_transition_shorthand(box,"none");
    if(box.animations().opacity_transition.duration_ms!=0 ||
       box.animations().left_transition.duration_ms!=0) return 57;
    using webscene_native::css::apply_grid_placement_declaration;
    apply_grid_placement_declaration(box,"grid-area","1 / 2 / 3 / 4");
    if(box.grid().row_start_value!="1" || box.grid().column_start_value!="2" ||
       box.grid().row_end_value!="3" || box.grid().column_end_value!="4") return 58;
    apply_grid_placement_declaration(box,"grid-column","2");
    if(box.grid().column_value!="2" || box.grid().column_end_value!="auto" || box.grid().span_all) return 59;
    webscene_native::css::apply_animation_shorthand(box,"progress 1.2s infinite ease-in-out");
    if(box.animations().animation_name_value!="progress" ||
       box.animations().animation_duration_value!="1.2s" ||
       box.animations().animation_iteration_count_value!="infinite") return 60;
    webscene_native::css::apply_animation_shorthand(box,"none");
    if(box.animations().animation_name_value!="none" ||
       box.animations().animation_duration_value!="0s") return 61;
    if(webscene_native::css::is_css_time("progress") || webscene_native::css::is_css_time("NaNs") ||
       webscene_native::css::is_css_time("1junkms") || !webscene_native::css::is_css_time("+.15s") ||
       webscene_native::css::parse_css_time_ms("1e-1s")!=100) return 62;
    webscene_native::native_document animated_document;
    auto& animated=animated_document.create_element("div");
    animated_document.append_child(animated_document.body(),animated);
    animated.style.width={20,webscene_native::length_unit::pixels};
    animated.style.height={20,webscene_native::length_unit::pixels};
    std::unordered_map<std::string,webscene_native::css::css_opacity_keyframes> definitions;
    definitions["progress"].opacity_stops={{0,0},{1,1}};
    webscene_native::css::apply_animation_shorthand(animated.style,"progress 1s linear infinite");
    webscene_native::css::configure_keyframes(animated.style,definitions);
    animated_document.layout(100,100);
    animated_document.signal_animation_frame(0);
    animated_document.update_style_animations(animated);
    animated_document.advance_animations();
    animated_document.signal_animation_frame(500);
    animated_document.advance_animations();
    if(std::abs(animated.painted_opacity_value()-.5f)>.02f) return 63;
    webscene_native::css::apply_animation_shorthand(animated.style,"none");
    webscene_native::css::configure_keyframes(animated.style,definitions);
    animated_document.update_style_animations(animated);
    if(animated.animation_runtime() && animated.animation_runtime()->opacity_keyframe_animation_active) return 64;
    webscene_native::node_style::pseudo_element generated;
    const auto apply_generated=[&](const std::string& name,const std::string& value) {
        return webscene_native::css::apply_pseudo_value(generated,0x123456FF,name,value);
    };
    apply_generated("content",R"("\2192 next")");
    apply_generated("display","inline-block");
    apply_generated("padding-inline","4px 8px");
    apply_generated("border","2px solid currentColor");
    apply_generated("border-radius","4px / 8px");
    if(!generated.generated || generated.content!="\u2192next" ||
       generated.display!=webscene_native::display_mode::inline_block ||
       generated.padding_left.value!=4 || generated.padding_right.value!=8 ||
       generated.border_left_width.value!=2 || !generated.border_left_current_color ||
       !generated.elliptical_border_radius || generated.border_top_left_radius.value!=4 ||
       generated.border_top_left_radius_y.value!=8) return 65;
    if(apply_generated("unknown-property","x").classification!="unsupported" ||
       apply_generated("line-height","inherit").classification!="partially-supported") return 66;
    apply_generated("content","none");
    apply_generated("border","none");
    if(generated.generated || !generated.content.empty() || generated.border_left_width.value!=0) return 67;
    stylesheet_test_host stylesheet_host;
    const std::string stylesheet_address="embedded:styles.css";
    webscene_native::css::stylesheet_sink stylesheet_sink(stylesheet_host,stylesheet_address,1024);
    const auto stylesheet_parsed=webscene_native::stream_css_syntax_stylesheet(R"CSS(
      .base { color: red; }
      @media (min-width: 600px) {
        @media (orientation: landscape) { .wide { color: blue !important; } }
      }
      @supports selector(:focus-visible) { .focus { outline: 2px solid; } }
      @container card (width > 10px) { .excluded { color: green; } }
      @keyframes pulse { from { opacity: 0; } to { opacity: 1; } }
      .last { content: "a;b"; }
    )CSS",stylesheet_sink);
    if(!stylesheet_parsed || !stylesheet_sink.complete() || stylesheet_host.rules.size()!=4 ||
       stylesheet_host.rules[1].media.size()!=2 || !stylesheet_host.rules[1].declarations[0].important ||
       stylesheet_host.rules[3].selector!=".last" || stylesheet_host.rules[3].address!=stylesheet_address ||
       stylesheet_sink.keyframes().size()!=1 || stylesheet_sink.keyframes()[0].second.opacity_stops.size()!=2 ||
       stylesheet_sink.keyframes()[0].first!="pulse" || stylesheet_host.unsupported.size()!=1) return 68;
    std::unordered_map<std::string,webscene_native::css::css_opacity_keyframes> parsed_keyframes;
    for(auto& [name,definition]:stylesheet_sink.keyframes()) {
        webscene_native::css::finish_keyframes(parsed_keyframes,name,std::move(definition));
    }
    webscene_native::css::apply_animation_shorthand(animated.style,"pulse 1s linear infinite");
    webscene_native::css::configure_keyframes(animated.style,parsed_keyframes);
    animated_document.signal_animation_frame(1000);
    animated_document.update_style_animations(animated);
    animated_document.advance_animations();
    animated_document.signal_animation_frame(1250);
    animated_document.advance_animations();
    if(std::abs(animated.painted_opacity_value()-.25f)>.02f) return 69;
    webscene_native::css::css_opacity_keyframes rotation_definition;
    webscene_native::css::append_keyframe(rotation_definition,"to",{{"transform","rotate(.5turn)",false}});
    webscene_native::css::finish_keyframes(parsed_keyframes,"spin",std::move(rotation_definition));
    if(parsed_keyframes["spin"].rotation_stops.size()!=2 ||
       parsed_keyframes["spin"].rotation_stops[0].degrees!=0 ||
       parsed_keyframes["spin"].rotation_stops[1].degrees!=180) return 70;
    webscene_native::css::rule_payload_cache payload_cache;
    std::mutex payload_mutex;
    const auto payload_for=[&](const std::string& selector,const std::string& color) {
        return webscene_native::css::intern_rule_payload(payload_mutex,payload_cache,
            webscene_native::css::compile_selector,selector,{{"color",color,false}},{});
    };
    auto red_payload=payload_for(".base","red");
    if(red_payload!=payload_for(".base","red") || red_payload==payload_for(".base","blue") ||
       red_payload->compiled_selector.compiled_compounds.size()!=1 || red_payload->specificity==0) return 71;
    std::weak_ptr<const webscene_native::css::css_rule_payload> released=red_payload;
    red_payload.reset();
    if(!released.expired()) return 72;
    red_payload=payload_for(".base","red");
    if(!red_payload || red_payload->declarations[0].value!="red") return 73;
    const std::string css_base="asset://kestrel/css/theme/main.css";
    const auto resolved_css=webscene_native::css::resolve_resource_urls(
        R"CSS(url('../../images/grid.png?v=2'), url("../fonts/ui.woff2"), url(#mask))CSS",css_base);
    if(resolved_css!=R"CSS(url("asset://kestrel/images/grid.png?v=2"), url("asset://kestrel/css/fonts/ui.woff2"), url(#mask))CSS") return 74;
    if(webscene_native::resources::resolve_url("data:image/png;base64,AA",css_base)!="data:image/png;base64,AA" ||
       webscene_native::resources::resolve_url("/icons/tool.svg",css_base)!="asset://kestrel/icons/tool.svg" ||
       webscene_native::resources::resolve_url("//cdn.test/a.png","https://example.test/css/main.css")!="https://cdn.test/a.png") return 75;
    if(webscene_native::css::resolve_resource_urls("url('unterminated",css_base)!="url('unterminated" ||
       webscene_native::css::resolve_resource_urls("url(icon.png)","")!="url(icon.png)") return 76;
    const auto old_rule_count=stylesheet_host.rules.size();
    stylesheet_host.append_parsed_css_style_rule(".one, :is(.two, .three)",
        {{"background-image","url(../../images/grid.png)",false}}, {},css_base);
    if(stylesheet_host.rules.size()!=old_rule_count+2 ||
       stylesheet_host.rules.back().declarations[0].value!="url(\"asset://kestrel/images/grid.png\")") return 77;
    stylesheet_host.append_parsed_css_style_rule("div > > span",{{"color","red",false}}, {},css_base);
    if(stylesheet_host.rules.size()!=old_rule_count+2) return 78;
    std::cout<<"V8-free shared CSS declaration service passed\n";
}
