#pragma once
#include "webscene_css_state.h"

namespace webscene_native::css {
struct stylesheet_diagnostic {
    std::string feature;
    std::string classification;
    std::string detail;
};

// Prepared author data, independent of a document, viewport, resource loader or
// JavaScript runtime. Media conditions remain attached to rules for live matching.
struct prepared_stylesheet {
    std::string source_address;
    std::vector<std::shared_ptr<const css_rule_payload>> rules;
    std::unordered_map<std::string,css_opacity_keyframes> keyframes;
    std::vector<stylesheet_diagnostic> diagnostics;
};

} // namespace webscene_native::css
