#pragma once
#include "webscene_css_resources.h"
#include "webscene_css_selectors.h"

namespace webscene_native::css {
// Preparing syntax is independent of rule ownership and resource loading. Hosts
// observe resolved declarations and consume one serialized selector at a time.
template<typename Observe, typename Append>
void prepare_style_rule(std::string_view prelude,
    std::vector<css_declaration> declarations,
    const std::vector<std::string>& media, const std::string& address,
    Observe&& observe, Append&& append)
{
    for(auto& declaration:declarations) {
        declaration.value=resolve_resource_urls(std::move(declaration.value),address);
        declaration.property=property_id(declaration.name);
        declaration.specified=compile_specified_value(declaration.property,declaration.value);
        observe(declaration);
    }
    // Exact :host is scoped by the document owner, outside ordinary matching.
    if(trim_css_view(prelude)==":host") {
        append(std::string(":host"),declarations,media);
        return;
    }
    const auto selectors=parse_selector_syntax(prelude);
    if(!selectors) return;
    for(const auto& selector:selectors.selectors)
        append(selector.serialized,declarations,media);
}
} // namespace webscene_native::css
