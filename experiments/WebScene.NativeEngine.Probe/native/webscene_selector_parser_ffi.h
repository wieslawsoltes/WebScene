#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {

struct webscene_selector_byte_slice final {
    const uint8_t* data;
    size_t length;
};

struct webscene_selector_parse_result final {
    uint32_t status;
    uint64_t selector_count;
    uint64_t rust_allocation_count;
    uint64_t rust_peak_bytes;
    uint64_t rust_retained_bytes;
    void* handle;
};

struct webscene_selector_view final {
    webscene_selector_byte_slice serialized;
    uint32_t specificity;
    size_t compound_count;
    size_t combinator_count;
};

uint32_t webscene_selector_parser_abi_version();
webscene_selector_parse_result webscene_selector_parse(webscene_selector_byte_slice);
uint8_t webscene_selector_at(const void*, size_t, webscene_selector_view*);
uint8_t webscene_selector_compound_at(
    const void*, size_t, size_t, webscene_selector_byte_slice*);
uint8_t webscene_selector_combinator_at(
    const void*, size_t, size_t, uint8_t*);
void webscene_selector_free(void*);

} // extern "C"
