#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {

struct webscene_css_byte_slice final {
    const uint8_t* data;
    size_t length;
};

struct webscene_css_parse_result final {
    uint32_t status;
    uint64_t parse_error_count;
    uint64_t rule_count;
    uint64_t declaration_count;
    uint64_t rust_allocation_count;
    uint64_t rust_peak_bytes;
    uint64_t rust_retained_bytes;
    void* handle;
};

struct webscene_css_rule_view final {
    uint32_t kind;
    uint8_t has_block;
    size_t parent_index;
    webscene_css_byte_slice name;
    webscene_css_byte_slice prelude;
    size_t first_declaration;
    size_t declaration_count;
};

struct webscene_css_declaration_view final {
    webscene_css_byte_slice name;
    webscene_css_byte_slice value;
    uint8_t important;
};

uint32_t webscene_css_parser_abi_version();
webscene_css_parse_result webscene_css_parse_stylesheet(webscene_css_byte_slice);
webscene_css_parse_result webscene_css_parse_declarations(webscene_css_byte_slice);
uint8_t webscene_css_rule_at(
    const void*, size_t, webscene_css_rule_view*);
uint8_t webscene_css_declaration_at(
    const void*, size_t, webscene_css_declaration_view*);
void webscene_css_free(void*);

} // extern "C"
