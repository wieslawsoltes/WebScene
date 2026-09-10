#include "webscene_css_declarations.h"
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
    std::cout<<"V8-free shared CSS declaration service passed\n";
}
