#pragma once

#include "webscene_native_dom.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace webscene_native {

enum class html_quirks_mode : uint8_t {
    no_quirks,
    limited_quirks,
    quirks
};

struct html_parse_options final {
    bool scripting_enabled{true};
    bool iframe_srcdoc{false};
    bool exact_errors{false};
    bool drop_doctype{false};
    bool preserve_comments{true};
};

struct html_parse_metrics final {
    uint32_t status{0};
    html_quirks_mode quirks_mode{html_quirks_mode::no_quirks};
    uint64_t duration_ns{0};
    uint64_t parse_error_count{0};
    uint64_t callback_count{0};
    uint64_t element_count{0};
    uint64_t text_append_count{0};
    uint64_t comment_count{0};
    uint64_t doctype_count{0};
    uint64_t rust_allocation_count{0};
    uint64_t rust_peak_bytes{0};
    uint64_t rust_retained_bytes{0};
    std::string error;

    explicit operator bool() const noexcept { return status == 0; }
};

html_parse_metrics parse_html_document(
    native_document& document,
    dom_node& document_root,
    std::string_view input,
    const html_parse_options& options = {});

html_parse_metrics parse_html_fragment(
    native_document& document,
    dom_node& output_root,
    std::string_view input,
    std::string_view context_local_name,
    std::string_view context_namespace = "http://www.w3.org/1999/xhtml",
    const html_parse_options& options = {});

} // namespace webscene_native
