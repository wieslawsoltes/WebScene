#include "webscene_html_parser.h"

#include "webscene_html_parser_ffi.h"

#include <algorithm>
#include <chrono>
#include <exception>

namespace webscene_native {
namespace {

constexpr uint32_t parser_abi_version = 1;

std::string copy_slice(webscene_html_byte_slice value)
{
    if (value.data == nullptr || value.length == 0) return {};
    return {reinterpret_cast<const char*>(value.data), value.length};
}

webscene_html_byte_slice borrow(std::string_view value) noexcept
{
    return {
        reinterpret_cast<const uint8_t*>(value.data()),
        value.size()};
}

dom_node* node(webscene_html_node_handle handle) noexcept
{
    return reinterpret_cast<dom_node*>(handle);
}

webscene_html_node_handle handle(dom_node& value) noexcept
{
    return reinterpret_cast<webscene_html_node_handle>(&value);
}

struct sink_context final {
    native_document& document;
    dom_node& root;
    bool failed{false};
    uint32_t quirks_mode{0};
    std::string error;
    bool fragment_mode{false};
    dom_node* fragment_root{nullptr};
};

template<typename Result, typename Callback>
Result guard(sink_context& context, Result fallback, Callback&& callback) noexcept
{
    if (context.failed) return fallback;
    try {
        return callback();
    } catch (const std::exception& exception) {
        context.failed = true;
        context.error = exception.what();
    } catch (...) {
        context.failed = true;
        context.error = "unknown native HTML tree-sink failure";
    }
    return fallback;
}

void apply_attribute(dom_node& target, const webscene_html_attribute& attribute, bool missing_only)
{
    auto name = copy_slice(attribute.name.local_name);
    if (name.empty()) return;
    if (missing_only && target.attributes.contains(name)) return;
    auto value = copy_slice(attribute.value);
    if (name == "id") target.id_attribute = value;
    if (name == "class") target.class_name = value;
    target.attributes[std::move(name)] = std::move(value);
}

webscene_html_node_handle create_element(
    void* opaque,
    const webscene_html_qualified_name* name,
    const webscene_html_attribute* attributes,
    size_t attribute_count) noexcept
{
    auto& context = *static_cast<sink_context*>(opaque);
    return guard<webscene_html_node_handle>(context, 0, [&] {
        if (name == nullptr) return webscene_html_node_handle{};
        auto& result = context.document.create_node(
            dom_node_kind::element,
            copy_slice(name->local_name));
        result.set_namespace(
            copy_slice(name->namespace_uri),
            copy_slice(name->prefix));
        if (context.fragment_mode
            && context.fragment_root == nullptr
            && result.tag == "html"
            && result.namespace_uri() == dom_node::html_namespace_uri) {
            context.fragment_root = &result;
        }
        for (size_t index = 0; index < attribute_count; ++index) {
            apply_attribute(result, attributes[index], false);
        }
        return handle(result);
    });
}

webscene_html_node_handle create_comment(
    void* opaque,
    webscene_html_byte_slice text) noexcept
{
    auto& context = *static_cast<sink_context*>(opaque);
    return guard<webscene_html_node_handle>(context, 0, [&] {
        auto& result = context.document.create_node(dom_node_kind::comment, "#comment");
        result.text_content = copy_slice(text);
        return handle(result);
    });
}

webscene_html_node_handle create_processing_instruction(
    void* opaque,
    webscene_html_byte_slice target,
    webscene_html_byte_slice data) noexcept
{
    auto& context = *static_cast<sink_context*>(opaque);
    return guard<webscene_html_node_handle>(context, 0, [&] {
        auto& result = context.document.create_node(
            dom_node_kind::processing_instruction,
            "#processing-instruction");
        result.set_namespace({}, copy_slice(target));
        result.text_content = copy_slice(data);
        return handle(result);
    });
}

webscene_html_node_handle append_doctype(
    void* opaque,
    webscene_html_byte_slice name,
    webscene_html_byte_slice public_id,
    webscene_html_byte_slice system_id) noexcept
{
    auto& context = *static_cast<sink_context*>(opaque);
    return guard<webscene_html_node_handle>(context, 0, [&] {
        auto& result = context.document.create_node(dom_node_kind::document_type, "#doctype");
        result.text_content = copy_slice(name);
        result.attributes["publicId"] = copy_slice(public_id);
        result.attributes["systemId"] = copy_slice(system_id);
        if (!context.document.parser_append_child(context.root, result)) {
            throw std::runtime_error("could not append document type");
        }
        return handle(result);
    });
}

uint8_t append_node(
    void* opaque,
    webscene_html_node_handle parent,
    webscene_html_node_handle child) noexcept
{
    auto& context = *static_cast<sink_context*>(opaque);
    return guard<uint8_t>(context, 0, [&] {
        auto* parent_node = node(parent);
        auto* child_node = node(child);
        if (parent_node == nullptr || child_node == nullptr
            || !context.document.parser_append_child(*parent_node, *child_node)) {
            throw std::runtime_error("could not append an HTML parser node");
        }
        return uint8_t{1};
    });
}

dom_node& make_text(sink_context& context, webscene_html_byte_slice value)
{
    auto& result = context.document.create_node(dom_node_kind::text, "#text");
    result.text_content = copy_slice(value);
    return result;
}

uint8_t append_text(
    void* opaque,
    webscene_html_node_handle parent,
    webscene_html_byte_slice text) noexcept
{
    auto& context = *static_cast<sink_context*>(opaque);
    return guard<uint8_t>(context, 0, [&] {
        auto* parent_node = node(parent);
        if (parent_node == nullptr) {
            throw std::runtime_error("HTML parser text parent was null");
        }
        if (!parent_node->children.empty()
            && parent_node->children.back()->kind == dom_node_kind::text) {
            parent_node->children.back()->text_content += copy_slice(text);
            return uint8_t{1};
        }
        auto& text_node = make_text(context, text);
        if (!context.document.parser_append_child(*parent_node, text_node)) {
            throw std::runtime_error("could not append HTML parser text");
        }
        return uint8_t{1};
    });
}

uint8_t insert_node_before(
    void* opaque,
    webscene_html_node_handle sibling,
    webscene_html_node_handle child) noexcept
{
    auto& context = *static_cast<sink_context*>(opaque);
    return guard<uint8_t>(context, 0, [&] {
        auto* sibling_node = node(sibling);
        auto* child_node = node(child);
        if (sibling_node == nullptr || child_node == nullptr
            || !context.document.parser_insert_before(*sibling_node, *child_node)) {
            throw std::runtime_error("could not insert an HTML parser node");
        }
        return uint8_t{1};
    });
}

uint8_t insert_text_before(
    void* opaque,
    webscene_html_node_handle sibling,
    webscene_html_byte_slice text) noexcept
{
    auto& context = *static_cast<sink_context*>(opaque);
    return guard<uint8_t>(context, 0, [&] {
        auto* sibling_node = node(sibling);
        if (sibling_node == nullptr || sibling_node->parent == nullptr) {
            throw std::runtime_error("HTML parser text sibling was detached");
        }
        const auto position = std::find(
            sibling_node->parent->children.begin(),
            sibling_node->parent->children.end(),
            sibling_node);
        if (position != sibling_node->parent->children.begin()) {
            auto* previous = *(position - 1);
            if (previous != nullptr && previous->kind == dom_node_kind::text) {
                previous->text_content += copy_slice(text);
                return uint8_t{1};
            }
        }
        auto& text_node = make_text(context, text);
        if (!context.document.parser_insert_before(*sibling_node, text_node)) {
            throw std::runtime_error("could not insert HTML parser text");
        }
        return uint8_t{1};
    });
}

uint8_t append_node_based_on_parent(
    void* opaque,
    webscene_html_node_handle element,
    webscene_html_node_handle previous_element,
    webscene_html_node_handle child) noexcept
{
    auto* element_node = node(element);
    return element_node != nullptr && element_node->parent != nullptr
        ? insert_node_before(opaque, element, child)
        : append_node(opaque, previous_element, child);
}

uint8_t append_text_based_on_parent(
    void* opaque,
    webscene_html_node_handle element,
    webscene_html_node_handle previous_element,
    webscene_html_byte_slice text) noexcept
{
    auto* element_node = node(element);
    return element_node != nullptr && element_node->parent != nullptr
        ? insert_text_before(opaque, element, text)
        : append_text(opaque, previous_element, text);
}

uint8_t remove_from_parent(void* opaque, webscene_html_node_handle target) noexcept
{
    auto& context = *static_cast<sink_context*>(opaque);
    return guard<uint8_t>(context, 0, [&] {
        auto* target_node = node(target);
        if (target_node == nullptr
            || !context.document.parser_remove_from_parent(*target_node)) {
            throw std::runtime_error("could not detach an HTML parser node");
        }
        return uint8_t{1};
    });
}

uint8_t reparent_children(
    void* opaque,
    webscene_html_node_handle source,
    webscene_html_node_handle destination) noexcept
{
    auto& context = *static_cast<sink_context*>(opaque);
    return guard<uint8_t>(context, 0, [&] {
        auto* source_node = node(source);
        auto* destination_node = node(destination);
        if (source_node == nullptr || destination_node == nullptr
            || !context.document.parser_reparent_children(
                *source_node, *destination_node)) {
            throw std::runtime_error("could not reparent HTML parser children");
        }
        return uint8_t{1};
    });
}

uint8_t add_attrs_if_missing(
    void* opaque,
    webscene_html_node_handle target,
    const webscene_html_attribute* attributes,
    size_t attribute_count) noexcept
{
    auto& context = *static_cast<sink_context*>(opaque);
    return guard<uint8_t>(context, 0, [&] {
        auto* target_node = node(target);
        if (target_node == nullptr) {
            throw std::runtime_error("HTML parser attribute target was null");
        }
        for (size_t index = 0; index < attribute_count; ++index) {
            apply_attribute(*target_node, attributes[index], true);
        }
        return uint8_t{1};
    });
}

webscene_html_node_handle get_template_contents(
    void* opaque,
    webscene_html_node_handle target) noexcept
{
    auto& context = *static_cast<sink_context*>(opaque);
    return guard<webscene_html_node_handle>(context, 0, [&] {
        auto* target_node = node(target);
        if (target_node == nullptr) {
            throw std::runtime_error("HTML parser template target was null");
        }
        return handle(context.document.parser_template_contents(*target_node));
    });
}

void set_quirks_mode(void* opaque, uint32_t mode) noexcept
{
    static_cast<sink_context*>(opaque)->quirks_mode = mode;
}

void parse_error(void*, webscene_html_byte_slice) noexcept
{
    // html5ever owns the count; full error strings are intentionally omitted
    // from release telemetry to avoid retaining input-derived allocations.
}

uint8_t callback_failed(void* opaque) noexcept
{
    return static_cast<uint8_t>(static_cast<sink_context*>(opaque)->failed);
}

webscene_html_sink_vtable callbacks(sink_context& context) noexcept
{
    return {
        parser_abi_version,
        sizeof(webscene_html_sink_vtable),
        &context,
        handle(context.root),
        create_element,
        create_comment,
        create_processing_instruction,
        append_doctype,
        append_node,
        append_text,
        insert_node_before,
        insert_text_before,
        append_node_based_on_parent,
        append_text_based_on_parent,
        remove_from_parent,
        reparent_children,
        add_attrs_if_missing,
        get_template_contents,
        set_quirks_mode,
        parse_error,
        callback_failed};
}

html_parse_metrics convert_result(
    const webscene_html_parse_result& result,
    const sink_context& context,
    uint64_t duration_ns)
{
    return {
        context.failed && result.status == 0 ? 2U : result.status,
        static_cast<html_quirks_mode>(std::min(result.quirks_mode, 2U)),
        duration_ns,
        result.parse_error_count,
        result.callback_count,
        result.element_count,
        result.text_append_count,
        result.comment_count,
        result.doctype_count,
        result.rust_allocation_count,
        result.rust_peak_bytes,
        result.rust_retained_bytes,
        context.error};
}

webscene_html_parse_options native_options(
    const html_parse_options& options,
    std::string_view context_namespace,
    std::string_view context_local_name) noexcept
{
    return {
        parser_abi_version,
        sizeof(webscene_html_parse_options),
        static_cast<uint8_t>(options.scripting_enabled),
        static_cast<uint8_t>(options.iframe_srcdoc),
        static_cast<uint8_t>(options.exact_errors),
        static_cast<uint8_t>(options.drop_doctype),
        static_cast<uint8_t>(options.preserve_comments),
        borrow(context_namespace),
        borrow(context_local_name)};
}

} // namespace

html_parse_metrics parse_html_document(
    native_document& document,
    dom_node& document_root,
    std::string_view input,
    const html_parse_options& options)
{
    sink_context context{document, document_root, false, 0, {}};
    const auto sink = callbacks(context);
    const auto parser_options = native_options(options, {}, {});
    const auto started = std::chrono::steady_clock::now();
    const auto result = webscene_html_parse_document(
        borrow(input), &parser_options, &sink);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    document.mark_dirty();
    return convert_result(
        result,
        context,
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
}

html_parse_metrics parse_html_fragment(
    native_document& document,
    dom_node& output_root,
    std::string_view input,
    std::string_view context_local_name,
    std::string_view context_namespace,
    const html_parse_options& options)
{
    sink_context context{document, output_root, false, 0, {}, true, nullptr};
    const auto sink = callbacks(context);
    const auto parser_options = native_options(
        options, context_namespace, context_local_name);
    const auto started = std::chrono::steady_clock::now();
    const auto result = webscene_html_parse_fragment(
        borrow(input), &parser_options, &sink);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    if (result.status == 0 && !context.failed && context.fragment_root != nullptr) {
        auto& fragment_root = *context.fragment_root;
        if (!document.parser_reparent_children(fragment_root, output_root)
            || !document.parser_remove_from_parent(fragment_root)) {
            context.failed = true;
            context.error = "could not commit the HTML fragment tree";
        } else {
            document.erase_detached_subtree(fragment_root);
        }
    }
    document.mark_dirty();
    return convert_result(
        result,
        context,
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
}

} // namespace webscene_native
