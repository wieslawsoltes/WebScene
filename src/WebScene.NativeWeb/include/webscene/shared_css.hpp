#pragma once
#include "native_web.hpp"
#include "webscene_css_stylesheet_data.h"

namespace webscene::native_web {
// Explicit opt-in adapter for the existing native CSS engine. Prepared records
// currently retain textual values and selectors; this is NOT the parser-free
// final compiler backend. Link webscene_native_web_shared_css to use it.
struct shared_css_report {
  std::vector<webscene_native::css::stylesheet_diagnostic> diagnostics;
};
std::unique_ptr<stylesheet_resolver> make_shared_stylesheet_resolver(
    std::vector<webscene_native::css::prepared_stylesheet>,
    std::shared_ptr<shared_css_report> report);
}
