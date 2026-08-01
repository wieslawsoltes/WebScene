#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {

struct webscene_css_byte_slice final {
    const uint8_t* data;
    size_t length;
};

using webscene_css_begin_rule_callback = uint8_t(*)(
    void*, uint32_t, uint8_t, size_t,
    webscene_css_byte_slice, webscene_css_byte_slice, size_t*);
using webscene_css_declaration_callback = uint8_t(*)(
    void*, webscene_css_byte_slice, webscene_css_byte_slice, uint8_t);
using webscene_css_end_rule_callback = uint8_t(*)(void*, size_t, size_t);

struct webscene_css_sink_vtable final {
    webscene_css_begin_rule_callback begin_rule;
    webscene_css_declaration_callback declaration;
    webscene_css_end_rule_callback end_rule;
};

struct webscene_css_stream_result final {
    uint32_t status;
    uint64_t parse_error_count;
    uint64_t rule_count;
    uint64_t declaration_count;
    uint64_t rust_allocation_count;
    uint64_t rust_peak_bytes;
    uint64_t rust_retained_bytes;
};

uint32_t webscene_css_stream_abi_version();
webscene_css_stream_result webscene_css_stream_stylesheet(
    webscene_css_byte_slice, const webscene_css_sink_vtable*, void*);
webscene_css_stream_result webscene_css_stream_declarations(
    webscene_css_byte_slice, const webscene_css_sink_vtable*, void*);

} // extern "C"
