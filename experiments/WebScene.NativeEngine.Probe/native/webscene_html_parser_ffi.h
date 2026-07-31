#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {

struct webscene_html_byte_slice final {
    const uint8_t* data;
    size_t length;
};

struct webscene_html_qualified_name final {
    webscene_html_byte_slice namespace_uri;
    webscene_html_byte_slice local_name;
    webscene_html_byte_slice prefix;
};

struct webscene_html_attribute final {
    webscene_html_qualified_name name;
    webscene_html_byte_slice value;
};

using webscene_html_node_handle = uintptr_t;

struct webscene_html_sink_vtable final {
    uint32_t abi_version;
    uint32_t struct_size;
    void* user_data;
    webscene_html_node_handle document;
    webscene_html_node_handle (*create_element)(
        void*,
        const webscene_html_qualified_name*,
        const webscene_html_attribute*,
        size_t);
    webscene_html_node_handle (*create_comment)(void*, webscene_html_byte_slice);
    webscene_html_node_handle (*create_processing_instruction)(
        void*, webscene_html_byte_slice, webscene_html_byte_slice);
    webscene_html_node_handle (*append_doctype)(
        void*, webscene_html_byte_slice, webscene_html_byte_slice,
        webscene_html_byte_slice);
    uint8_t (*append_node)(void*, webscene_html_node_handle, webscene_html_node_handle);
    uint8_t (*append_text)(void*, webscene_html_node_handle, webscene_html_byte_slice);
    uint8_t (*insert_node_before)(
        void*, webscene_html_node_handle, webscene_html_node_handle);
    uint8_t (*insert_text_before)(
        void*, webscene_html_node_handle, webscene_html_byte_slice);
    uint8_t (*append_node_based_on_parent)(
        void*, webscene_html_node_handle, webscene_html_node_handle,
        webscene_html_node_handle);
    uint8_t (*append_text_based_on_parent)(
        void*, webscene_html_node_handle, webscene_html_node_handle,
        webscene_html_byte_slice);
    uint8_t (*remove_from_parent)(void*, webscene_html_node_handle);
    uint8_t (*reparent_children)(
        void*, webscene_html_node_handle, webscene_html_node_handle);
    uint8_t (*add_attrs_if_missing)(
        void*, webscene_html_node_handle, const webscene_html_attribute*, size_t);
    webscene_html_node_handle (*get_template_contents)(
        void*, webscene_html_node_handle);
    void (*set_quirks_mode)(void*, uint32_t);
    void (*parse_error)(void*, webscene_html_byte_slice);
    uint8_t (*callback_failed)(void*);
};

struct webscene_html_parse_options final {
    uint32_t abi_version;
    uint32_t struct_size;
    uint8_t scripting_enabled;
    uint8_t iframe_srcdoc;
    uint8_t exact_errors;
    uint8_t drop_doctype;
    uint8_t preserve_comments;
    webscene_html_byte_slice context_namespace;
    webscene_html_byte_slice context_local_name;
};

struct webscene_html_parse_result final {
    uint32_t status;
    uint32_t quirks_mode;
    uint64_t parse_error_count;
    uint64_t callback_count;
    uint64_t element_count;
    uint64_t text_append_count;
    uint64_t comment_count;
    uint64_t doctype_count;
    uint64_t rust_allocation_count;
    uint64_t rust_peak_bytes;
    uint64_t rust_retained_bytes;
};

uint32_t webscene_html_parser_abi_version();
webscene_html_parse_result webscene_html_parse_document(
    webscene_html_byte_slice,
    const webscene_html_parse_options*,
    const webscene_html_sink_vtable*);
webscene_html_parse_result webscene_html_parse_fragment(
    webscene_html_byte_slice,
    const webscene_html_parse_options*,
    const webscene_html_sink_vtable*);

} // extern "C"
