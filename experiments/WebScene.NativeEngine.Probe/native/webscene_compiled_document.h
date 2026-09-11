#pragma once
#include "webscene_css_stylesheet_data.h"
#include "webscene_compiled_template.h"
#include <functional>
#include <string>
#include <vector>

namespace webscene_native {
class native_document;
// Document construction and prepared author data have no dependency on V8.
// Hosts copy the package before queuing it; callbacks execute on the DOM owner.
struct compiled_document {
    std::string base_url;
    bool allow_runtime_html{true};
    std::function<void(native_document&)> construct;
    struct script { std::string url, source; bool defer{}, module{}; std::string name; };
    std::vector<script> scripts;
    std::vector<std::string> stylesheet_urls;
    std::vector<css::prepared_stylesheet> stylesheets;
    compiled_dom_factories templates;
};
}
