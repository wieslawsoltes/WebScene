#include "webscene_css_declarations.h"
#include "webscene_css_selectors.h"
#include "webscene_css_matching.h"
#include <iostream>
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
    std::cout<<"V8-free shared CSS declaration service passed\n";
}
