#include "webscene_v8_runtime.h"
#include "webscene_runtime_diagnostics.h"
#include "webscene_embed_fallback.h"

#include "webscene_native_dom.h"
#include "webscene_native_websocket.h"
#if defined(WEBSCENE_NATIVE_ENGINE_HTML5EVER)
#include "webscene_html_parser.h"
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_CSSPARSER)
#include "webscene_css_parser.h"
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_SERVO_SELECTORS)
#include "webscene_selector_parser.h"
#endif

#include <libplatform/libplatform.h>
#include <v8.h>
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
#include <v8-inspector.h>
#endif
#include <v8-profiler.h>

#if defined(WEBSCENE_V8_PARTITION_ALLOC)
#include "partition_alloc/buildflags.h"
#if PA_BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)
#include "partition_alloc/partition_root.h"
#include "partition_alloc/shim/allocator_shim_default_dispatch_to_partition_alloc.h"
#endif
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <future>
#include <fstream>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(WEBSCENE_NATIVE_ENGINE_CANVAS_PAINT_STATE_BENCHMARK_COUNTERS)
namespace {
std::atomic<uint64_t> canvas_paint_string_property_probes{0U};
std::atomic<uint64_t> canvas_paint_utf8_conversions{0U};
std::atomic<uint64_t> canvas_paint_stack_comparisons{0U};
std::atomic<uint64_t> canvas_paint_cached_value_hits{0U};
}

extern "C" WEBSCENE_API void webscene_canvas_paint_state_benchmark_reset_counters(void)
{
    canvas_paint_string_property_probes.store(0U, std::memory_order_relaxed);
    canvas_paint_utf8_conversions.store(0U, std::memory_order_relaxed);
    canvas_paint_stack_comparisons.store(0U, std::memory_order_relaxed);
    canvas_paint_cached_value_hits.store(0U, std::memory_order_relaxed);
}

extern "C" WEBSCENE_API uint64_t
webscene_canvas_paint_state_benchmark_string_property_probes(void)
{
    return canvas_paint_string_property_probes.load(std::memory_order_relaxed);
}

extern "C" WEBSCENE_API uint64_t
webscene_canvas_paint_state_benchmark_utf8_conversions(void)
{
    return canvas_paint_utf8_conversions.load(std::memory_order_relaxed);
}

extern "C" WEBSCENE_API uint64_t
webscene_canvas_paint_state_benchmark_stack_comparisons(void)
{
    return canvas_paint_stack_comparisons.load(std::memory_order_relaxed);
}

extern "C" WEBSCENE_API uint64_t
webscene_canvas_paint_state_benchmark_cached_value_hits(void)
{
    return canvas_paint_cached_value_hits.load(std::memory_order_relaxed);
}
#endif

#if defined(WEBSCENE_NATIVE_ENGINE_MEDIA_REFRESH_BENCHMARK_COUNTERS)
namespace {
std::atomic<uint64_t> media_refresh_index_rule_calls{0U};
std::atomic<uint64_t> media_refresh_root_variable_refreshes{0U};
std::atomic<uint64_t> media_refresh_class_lookups{0U};
std::atomic<uint64_t> media_refresh_owned_class_lookup_keys{0U};
std::atomic<uint64_t> media_refresh_owned_class_lookup_bytes{0U};
}

extern "C" WEBSCENE_API void webscene_media_refresh_benchmark_reset_counters(void)
{
    media_refresh_index_rule_calls.store(0U, std::memory_order_relaxed);
    media_refresh_root_variable_refreshes.store(0U, std::memory_order_relaxed);
    media_refresh_class_lookups.store(0U, std::memory_order_relaxed);
    media_refresh_owned_class_lookup_keys.store(0U, std::memory_order_relaxed);
    media_refresh_owned_class_lookup_bytes.store(0U, std::memory_order_relaxed);
}

extern "C" WEBSCENE_API uint64_t webscene_media_refresh_benchmark_index_rule_calls(void)
{
    return media_refresh_index_rule_calls.load(std::memory_order_relaxed);
}

extern "C" WEBSCENE_API uint64_t webscene_media_refresh_benchmark_root_variable_refreshes(void)
{
    return media_refresh_root_variable_refreshes.load(std::memory_order_relaxed);
}

extern "C" WEBSCENE_API uint64_t webscene_media_refresh_benchmark_class_lookups(void)
{
    return media_refresh_class_lookups.load(std::memory_order_relaxed);
}

extern "C" WEBSCENE_API uint64_t
webscene_media_refresh_benchmark_owned_class_lookup_keys(void)
{
    return media_refresh_owned_class_lookup_keys.load(std::memory_order_relaxed);
}

extern "C" WEBSCENE_API uint64_t
webscene_media_refresh_benchmark_owned_class_lookup_bytes(void)
{
    return media_refresh_owned_class_lookup_bytes.load(std::memory_order_relaxed);
}
#endif

#if defined(WEBSCENE_NATIVE_ENGINE_SELECTOR_SIBLING_BENCHMARK_COUNTERS)
namespace {
std::atomic<uint64_t> selector_sibling_positional_matches{0U};
std::atomic<uint64_t> selector_sibling_scans{0U};
std::atomic<uint64_t> selector_sibling_vector_materializations{0U};
std::atomic<uint64_t> selector_sibling_pointer_copies{0U};
}

extern "C" WEBSCENE_API void webscene_selector_sibling_benchmark_reset_counters(void)
{
    selector_sibling_positional_matches.store(0U, std::memory_order_relaxed);
    selector_sibling_scans.store(0U, std::memory_order_relaxed);
    selector_sibling_vector_materializations.store(0U, std::memory_order_relaxed);
    selector_sibling_pointer_copies.store(0U, std::memory_order_relaxed);
}

extern "C" WEBSCENE_API uint64_t webscene_selector_sibling_benchmark_positional_matches(void)
{
    return selector_sibling_positional_matches.load(std::memory_order_relaxed);
}

extern "C" WEBSCENE_API uint64_t webscene_selector_sibling_benchmark_sibling_scans(void)
{
    return selector_sibling_scans.load(std::memory_order_relaxed);
}

extern "C" WEBSCENE_API uint64_t webscene_selector_sibling_benchmark_vector_materializations(void)
{
    return selector_sibling_vector_materializations.load(std::memory_order_relaxed);
}

extern "C" WEBSCENE_API uint64_t webscene_selector_sibling_benchmark_pointer_copies(void)
{
    return selector_sibling_pointer_copies.load(std::memory_order_relaxed);
}
#endif

namespace webscene_native {
namespace {

#include "webscene_v8_runtime_support.inc"
} // namespace

void prewarm_v8_process()
{
    initialize_v8_process();
}

struct v8_dom_runtime::implementation final {
#include "webscene_v8_runtime_state_types.inc"
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
#include "webscene_v8_runtime_inspector.inc"
#endif
#include "webscene_v8_runtime_lifecycle.inc"
    bool initialize()
    {
        prune_persistent_compilation_cache();
        initialize_v8_process();
        if (std::getenv("WEBSCENE_V8_SHARED_ISOLATE") != nullptr) {
            try {
                shared_isolate = acquire_shared_isolate();
            } catch (const std::exception& exception) {
                last_error = exception.what();
                return false;
            }
            isolate = shared_isolate == nullptr ? nullptr : shared_isolate->isolate;
        } else {
            allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
            v8::Isolate::CreateParams params;
            params.array_buffer_allocator = allocator;
            if (const auto maximum_heap_mib =
                    unsigned_environment_value("WEBSCENE_V8_MAX_HEAP_MIB");
                maximum_heap_mib.has_value() && *maximum_heap_mib > 0) {
                const auto initial_heap_mib =
                    unsigned_environment_value("WEBSCENE_V8_INITIAL_HEAP_MIB").value_or(0);
                params.constraints.ConfigureDefaultsFromHeapSize(
                    initial_heap_mib * 1024U * 1024U,
                    *maximum_heap_mib * 1024U * 1024U);
            }
            try {
                configure_startup_snapshot(params);
            } catch (const std::exception& exception) {
                last_error = exception.what();
                delete allocator;
                allocator = nullptr;
                return false;
            }
            isolate = v8::Isolate::New(params);
        }
        if (isolate == nullptr) {
            last_error = "V8 isolate creation failed";
            return false;
        }
        auto isolate_locker = lock_shared_isolate();
        isolate->SetData(0, shared_isolate == nullptr ? this : nullptr);
        if (std::getenv("WEBSCENE_V8_MEMORY_SAVER") != nullptr) {
            isolate->SetMemorySaverMode(true);
        }
        isolate->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);
        isolate->SetPromiseRejectCallback(promise_rejected);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (profile_bindings || profile_resize_cpu) {
            cpu_profiler = v8::CpuProfiler::New(isolate);
            cpu_profiler->SetSamplingInterval(250);
        }
#endif

        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        auto global_template = v8::ObjectTemplate::New(isolate);
        global_template->SetHandler(v8::NamedPropertyHandlerConfiguration(
            get_window_named_property,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            {},
            v8::PropertyHandlerFlags::kNonMasking));
        auto local_context = v8::Context::New(isolate, nullptr, global_template);
        local_context->SetSecurityToken(js_string(isolate, "webscene-native-origin"));
        local_context->SetAlignedPointerInEmbedderData(
            runtime_context_embedder_slot,
            this,
            v8::kEmbedderDataTypeTagDefault);
        local_context->SetAlignedPointerInEmbedderData(
            1,
            &document.body(),
            v8::kEmbedderDataTypeTagDefault);
        context.Reset(isolate, local_context);
        v8::Context::Scope context_scope(local_context);
        install_templates(local_context);
        install_globals(local_context);
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        // Advertise the compiled capability once the isolate and its default
        // context are ready. The V8Inspector object and context registrations
        // remain pay-for-use until the worker processes the first connection.
        inspector_ready.store(
            shared_isolate == nullptr,
            std::memory_order_release);
#endif
        return true;
    }

#if defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
#include "generated/webscene_dom_bindings.inc"
#endif

    void install_templates(v8::Local<v8::Context> local_context)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
        install_generated_dom_templates(local_context);
#else
        auto element = v8::FunctionTemplate::New(isolate);
        element->SetClassName(js_string(isolate, "HTMLElement"));
        element->InstanceTemplate()->SetInternalFieldCount(1);
        element->InstanceTemplate()->SetHandler(
            v8::IndexedPropertyHandlerConfiguration(form_or_select_indexed_getter));
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "style"), get_style);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "id"), get_id, set_id);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "className"), get_class_name, set_class_name);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "tagName"), get_tag_name);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "nodeName"), get_tag_name);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "localName"), get_local_name);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "nodeType"), get_element_node_type);
        element->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "__websceneNativeNodeId"),
            get_native_node_id,
            nullptr,
            v8::Local<v8::Value>(),
            static_cast<v8::PropertyAttribute>(
                v8::PropertyAttribute::ReadOnly
                | v8::PropertyAttribute::DontEnum
                | v8::PropertyAttribute::DontDelete));
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "nodeValue"), get_text_content, set_text_content);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "textContent"), get_text_content, set_text_content);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "innerText"), get_inner_text, set_text_content);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "text"), get_script_text, set_script_text);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "data"), get_text_content, set_text_content);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "namespaceURI"), get_namespace_uri);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "children"), get_children);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "childNodes"), get_children);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "content"), get_template_content);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "parentNode"), get_parent_node);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "parentElement"), get_parent_node);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "firstChild"), get_first_child);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "lastChild"), get_last_child);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "nextSibling"), get_next_sibling);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "previousSibling"), get_previous_sibling);
        element->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "nextElementSibling"), get_next_element_sibling);
        element->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "previousElementSibling"), get_previous_element_sibling);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "isConnected"), get_is_connected);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "firstElementChild"), get_first_element_child);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "lastElementChild"), get_last_element_child);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "ownerSVGElement"), get_owner_svg_element);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "dataset"), get_dataset);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "attributes"), get_attributes);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "ownerDocument"), get_owner_document);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "classList"), get_class_list);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "clientWidth"), get_client_width);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "clientHeight"), get_client_height);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "clientLeft"), get_client_left);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "clientTop"), get_client_top);
        element->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "scrollWidth"), get_scroll_width);
        element->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "scrollHeight"), get_scroll_height);
        element->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "scrollLeft"), get_scroll_left, set_scroll_left);
        element->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "scrollTop"), get_scroll_top, set_scroll_top);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "offsetWidth"), get_offset_width);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "offsetHeight"), get_client_height);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "offsetLeft"), get_offset_left);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "offsetTop"), get_offset_top);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "offsetParent"), get_offset_parent);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "width"), get_element_width, set_element_width);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "height"), get_element_height, set_element_height);
        element->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "hidden"), get_hidden, set_hidden);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "colSpan"), get_table_cell_span, set_table_cell_span);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "rowSpan"), get_table_cell_span, set_table_cell_span);
        element->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "cellSpacing"), get_reflected_string_attribute, set_reflected_string_attribute);
        element->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "cellPadding"), get_reflected_string_attribute, set_reflected_string_attribute);
        element->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "enctype"), get_reflected_string_attribute, set_reflected_string_attribute);
        element->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "htmlFor"), get_reflected_string_attribute, set_reflected_string_attribute);
        element->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "lang"), get_reflected_string_attribute, set_reflected_string_attribute);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "type"), get_element_type, set_element_type);
        element->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "name"), get_reflected_string_attribute, set_reflected_string_attribute);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "value"), get_form_value, set_form_value);
        element->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "defaultValue"), get_default_value, set_default_value);
        element->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "maxLength"), get_max_length, set_max_length);
        element->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "readOnly"), get_read_only, set_read_only);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "options"), get_select_options);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "elements"), get_form_elements);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "length"), get_form_or_select_length);
        element->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "selectedIndex"), get_selected_index, set_selected_index);
        element->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "selectionStart"), get_selection_start, set_selection_start);
        element->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "selectionEnd"), get_selection_end, set_selection_end);
        element->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "selectionDirection"),
            get_selection_direction,
            set_selection_direction);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "checked"), get_checked, set_checked);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "selected"), get_selected, set_selected);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "multiple"), get_multiple, set_multiple);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "disabled"), get_disabled, set_disabled);
        element->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "tabIndex"), get_tab_index, set_tab_index);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "src"), get_element_url, set_element_src);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "href"), get_element_url, set_element_href);
        element->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "download"),
            get_reflected_string_attribute,
            set_reflected_string_attribute);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "hash"), get_anchor_hash, set_anchor_hash);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "innerHTML"), get_inner_html, set_inner_html);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "contentWindow"), get_content_window);
        element->InstanceTemplate()->SetNativeDataProperty(js_string(isolate, "contentDocument"), get_content_document);
        // Web IDL exposes Node's type constants on Node.prototype as well as
        // on the Node constructor. Component code commonly compares
        // `node.nodeType === node.ELEMENT_NODE` while walking event ancestry.
        // Keeping these only on the constructor makes every live node fail
        // that guard even though Node.ELEMENT_NODE itself is correct.
        element->PrototypeTemplate()->Set(
            js_string(isolate, "ELEMENT_NODE"),
            v8::Integer::New(isolate, 1));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "ATTRIBUTE_NODE"),
            v8::Integer::New(isolate, 2));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "TEXT_NODE"),
            v8::Integer::New(isolate, 3));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "DOCUMENT_NODE"),
            v8::Integer::New(isolate, 9));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "DOCUMENT_FRAGMENT_NODE"),
            v8::Integer::New(isolate, 11));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "DOCUMENT_POSITION_DISCONNECTED"),
            v8::Integer::New(isolate, 1));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "DOCUMENT_POSITION_PRECEDING"),
            v8::Integer::New(isolate, 2));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "DOCUMENT_POSITION_FOLLOWING"),
            v8::Integer::New(isolate, 4));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "DOCUMENT_POSITION_CONTAINS"),
            v8::Integer::New(isolate, 8));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "DOCUMENT_POSITION_CONTAINED_BY"),
            v8::Integer::New(isolate, 16));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC"),
            v8::Integer::New(isolate, 32));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "appendChild"),
            v8::FunctionTemplate::New(isolate, append_child));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "append"),
            v8::FunctionTemplate::New(isolate, append_nodes));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "prepend"),
            v8::FunctionTemplate::New(isolate, prepend_child));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "before"),
            v8::FunctionTemplate::New(isolate, insert_before_self));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "after"),
            v8::FunctionTemplate::New(isolate, insert_after_self));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "removeChild"),
            v8::FunctionTemplate::New(isolate, remove_child));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "replaceChild"),
            v8::FunctionTemplate::New(isolate, replace_child));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "replaceChildren"),
            v8::FunctionTemplate::New(isolate, replace_children));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "remove"),
            v8::FunctionTemplate::New(isolate, remove_element));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "replaceWith"),
            v8::FunctionTemplate::New(isolate, replace_with));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "insertAdjacentElement"),
            v8::FunctionTemplate::New(isolate, insert_adjacent_element));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "insertAdjacentHTML"),
            v8::FunctionTemplate::New(isolate, insert_adjacent_html));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "insertBefore"),
            v8::FunctionTemplate::New(isolate, insert_before));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "cloneNode"),
            v8::FunctionTemplate::New(isolate, clone_node));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "getBoundingClientRect"),
            v8::FunctionTemplate::New(isolate, get_bounding_client_rect));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "getClientRects"),
            v8::FunctionTemplate::New(isolate, get_client_rects));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "scrollIntoView"),
            v8::FunctionTemplate::New(isolate, element_scroll_into_view));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "setAttribute"),
            v8::FunctionTemplate::New(isolate, set_attribute));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "setAttributeNS"),
            v8::FunctionTemplate::New(isolate, set_attribute_ns));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "removeAttribute"),
            v8::FunctionTemplate::New(isolate, remove_attribute));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "removeAttributeNS"),
            v8::FunctionTemplate::New(isolate, remove_attribute_ns));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "toggleAttribute"),
            v8::FunctionTemplate::New(isolate, toggle_attribute));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "getAttribute"),
            v8::FunctionTemplate::New(isolate, get_attribute));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "getAttributeNS"),
            v8::FunctionTemplate::New(isolate, get_attribute_ns));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "hasAttribute"),
            v8::FunctionTemplate::New(isolate, has_attribute));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "querySelectorAll"),
            v8::FunctionTemplate::New(isolate, element_query_selector_all));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "querySelector"),
            v8::FunctionTemplate::New(isolate, element_query_selector));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "getElementsByTagName"),
            v8::FunctionTemplate::New(isolate, element_get_elements_by_tag_name));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "getElementsByClassName"),
            v8::FunctionTemplate::New(isolate, element_get_elements_by_class_name));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "contains"),
            v8::FunctionTemplate::New(isolate, element_contains));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "compareDocumentPosition"),
            v8::FunctionTemplate::New(isolate, element_compare_document_position));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "isSameNode"),
            v8::FunctionTemplate::New(isolate, element_is_same_node));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "isEqualNode"),
            v8::FunctionTemplate::New(isolate, element_is_equal_node));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "matches"),
            v8::FunctionTemplate::New(isolate, element_matches));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "closest"),
            v8::FunctionTemplate::New(isolate, element_closest));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "addEventListener"),
            v8::FunctionTemplate::New(isolate, add_event_listener));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "removeEventListener"),
            v8::FunctionTemplate::New(isolate, remove_event_listener));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "dispatchEvent"),
            v8::FunctionTemplate::New(isolate, dispatch_event));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "getContext"),
            v8::FunctionTemplate::New(isolate, canvas_get_context));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "toDataURL"),
            v8::FunctionTemplate::New(isolate, canvas_to_data_url));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "toBlob"),
            v8::FunctionTemplate::New(isolate, canvas_to_blob));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "decode"),
            v8::FunctionTemplate::New(isolate, image_decode));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "setPointerCapture"),
            v8::FunctionTemplate::New(isolate, set_pointer_capture));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "releasePointerCapture"),
            v8::FunctionTemplate::New(isolate, release_pointer_capture));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "hasPointerCapture"),
            v8::FunctionTemplate::New(isolate, has_pointer_capture));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "focus"),
            v8::FunctionTemplate::New(isolate, element_focus));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "blur"),
            v8::FunctionTemplate::New(isolate, element_blur));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "select"),
            v8::FunctionTemplate::New(isolate, element_select));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "setSelectionRange"),
            v8::FunctionTemplate::New(isolate, element_set_selection_range));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "click"),
            v8::FunctionTemplate::New(isolate, element_click));
        element->PrototypeTemplate()->Set(
            js_string(isolate, "reset"),
            v8::FunctionTemplate::New(isolate, form_reset));
        element_template.Reset(isolate, element);
#endif

        auto style = v8::ObjectTemplate::New(isolate);
        style->SetInternalFieldCount(2);
        style->SetNativeDataProperty(
            js_string(isolate, "cssText"), get_style_css_text, set_style_css_text);
        const char* properties[] = {
            "width", "height", "minWidth", "minHeight", "maxWidth", "maxHeight",
            "left", "top", "right", "bottom", "inset", "insetInlineStart", "insetInlineEnd",
            "display", "position", "contain", "cssFloat", "flexDirection", "flexFlow",
            "flexGrow", "flexShrink", "flexBasis", "flexWrap",
            "alignItems", "alignSelf", "justifyContent", "gap", "rowGap", "columnGap",
            "gridGap", "gridRowGap", "gridColumnGap",
            "padding", "paddingInline", "paddingBlock",
            "paddingLeft", "paddingTop", "paddingRight", "paddingBottom",
            "paddingInlineStart", "paddingInlineEnd", "paddingBlockStart", "paddingBlockEnd",
            "margin", "marginInline", "marginBlock",
            "marginLeft", "marginTop", "marginRight", "marginBottom",
            "boxSizing", "borderRadius", "boxShadow", "transform", "transformOrigin",
            "transition", "transitionProperty", "transitionDuration", "transitionDelay",
            "transitionTimingFunction", "animation", "animationName",
            "animationDuration", "animationDelay", "animationTimingFunction",
            "animationDirection", "animationFillMode", "animationIterationCount",
            "animationPlayState", "appearance", "zIndex", "opacity",
            "background", "backgroundColor", "backgroundImage", "backgroundRepeat",
            "backgroundAttachment", "backgroundClip", "backgroundOrigin",
            "backgroundPosition", "backgroundPositionX", "backgroundPositionY", "backgroundSize",
            "border", "borderWidth", "borderStyle", "borderColor",
            "borderTop", "borderRight", "borderBottom", "borderLeft",
            "borderTopWidth", "borderRightWidth",
            "borderBottomWidth", "borderLeftWidth", "borderTopStyle", "borderTopColor",
            "borderRightColor", "borderBottomColor", "borderLeftColor",
            "borderTopLeftRadius", "borderTopRightRadius", "borderBottomRightRadius",
            "borderBottomLeftRadius", "borderCollapse", "borderSpacing", "clear",
            "columnCount", "columns", "emptyCells", "fillOpacity", "float",
            "gridArea", "gridColumn", "gridColumnEnd", "gridColumnStart",
            "gridRow", "gridRowEnd", "gridRowStart", "order", "orphans",
            "outlineColor", "outlineWidth", "outlineStyle",
            "overflow", "overflowX", "overflowY", "color",
            "font", "fontSize", "fontFamily", "webkitFontSmoothing", "fontStretch",
            "fontWeight", "lineHeight",
            "letterSpacing", "wordSpacing", "resize", "tableLayout", "textAlign",
            "textDecoration", "textOverflow", "verticalAlign", "whiteSpace", "widows", "zoom",
            "visibility", "direction", "pointerEvents", "cursor"
        };
        for (const auto* property : properties) {
            style->SetNativeDataProperty(js_string(isolate, property), get_style_property, set_style_property);
            const auto css_name = canonical_css_property_name(property);
            if (css_name != property) {
                style->SetNativeDataProperty(
                    js_string(isolate, css_name.c_str()),
                    get_style_property,
                    set_style_property);
            }
        }
        style->Set(
            js_string(isolate, "setProperty"),
            v8::FunctionTemplate::New(isolate, style_set_property));
        style->Set(
            js_string(isolate, "getPropertyValue"),
            v8::FunctionTemplate::New(isolate, style_get_property));
        style->Set(
            js_string(isolate, "removeProperty"),
            v8::FunctionTemplate::New(isolate, style_remove_property));
        style_template.Reset(isolate, style);

        auto frame_document = v8::ObjectTemplate::New(isolate);
        frame_document->SetInternalFieldCount(1);
        frame_document->Set(
            js_string(isolate, "open"),
            v8::FunctionTemplate::New(isolate, frame_document_open));
        frame_document->Set(
            js_string(isolate, "write"),
            v8::FunctionTemplate::New(isolate, frame_document_write));
        frame_document->Set(
            js_string(isolate, "close"),
            v8::FunctionTemplate::New(isolate, frame_document_close));
        // The owner realm can inspect contentDocument before the real frame
        // context has hydrated. Keep the provisional document API-shaped so
        // geometry probes see an empty document instead of a TypeError.
        frame_document->Set(
            js_string(isolate, "querySelectorAll"),
            v8::FunctionTemplate::New(isolate, document_query_selector_all));
        frame_document->Set(
            js_string(isolate, "querySelector"),
            v8::FunctionTemplate::New(isolate, document_query_selector));
        frame_document->Set(
            js_string(isolate, "getElementById"),
            v8::FunctionTemplate::New(isolate, get_element_by_id));
        frame_document->Set(
            js_string(isolate, "getElementsByTagName"),
            v8::FunctionTemplate::New(isolate, document_get_elements_by_tag_name));
        frame_document->Set(
            js_string(isolate, "getElementsByClassName"),
            v8::FunctionTemplate::New(isolate, document_get_elements_by_class_name));
        frame_document->Set(
            js_string(isolate, "getElementsByName"),
            v8::FunctionTemplate::New(isolate, document_get_elements_by_name));
        frame_document->Set(
            js_string(isolate, "compareDocumentPosition"),
            v8::FunctionTemplate::New(isolate, element_compare_document_position));
        frame_document->Set(
            js_string(isolate, "createElement"),
            v8::FunctionTemplate::New(isolate, create_element));
        frame_document->Set(
            js_string(isolate, "createElementNS"),
            v8::FunctionTemplate::New(isolate, create_element_ns));
        frame_document->Set(
            js_string(isolate, "createTextNode"),
            v8::FunctionTemplate::New(isolate, create_text_node));
        frame_document->Set(
            js_string(isolate, "createDocumentFragment"),
            v8::FunctionTemplate::New(isolate, create_document_fragment));
        frame_document->Set(
            js_string(isolate, "createEvent"),
            v8::FunctionTemplate::New(isolate, document_create_event));
        frame_document->Set(
            js_string(isolate, "execCommand"),
            v8::FunctionTemplate::New(isolate, document_exec_command, {}, {}, 1,
                v8::ConstructorBehavior::kThrow));
        frame_document->SetNativeDataProperty(
            js_string(isolate, "body"), get_body);
        frame_document->SetNativeDataProperty(
            js_string(isolate, "head"), get_body);
        frame_document->SetNativeDataProperty(
            js_string(isolate, "documentElement"), get_body);
        frame_document->SetNativeDataProperty(
            js_string(isolate, "nodeType"), get_document_node_type);
        frame_document->SetNativeDataProperty(
            js_string(isolate, "nodeName"), get_document_node_name);
        frame_document->SetNativeDataProperty(
            js_string(isolate, "firstChild"), get_document_boundary_child);
        frame_document->SetNativeDataProperty(
            js_string(isolate, "lastChild"), get_document_boundary_child);
        frame_document->SetNativeDataProperty(
            js_string(isolate, "childNodes"), get_document_child_nodes);
        frame_document->SetNativeDataProperty(
            js_string(isolate, "activeElement"),
            get_provisional_frame_active_element);
        frame_document->SetNativeDataProperty(
            js_string(isolate, "defaultView"),
            get_provisional_frame_default_view);
        frame_document_template.Reset(isolate, frame_document);

        auto frame_window = v8::ObjectTemplate::New(isolate);
        frame_window->SetInternalFieldCount(1);
        frame_window->Set(
            js_string(isolate, "addEventListener"),
            v8::FunctionTemplate::New(isolate, frame_window_add_event_listener));
        frame_window->Set(
            js_string(isolate, "removeEventListener"),
            v8::FunctionTemplate::New(isolate, remove_event_listener));
        frame_window->Set(
            js_string(isolate, "getComputedStyle"),
            v8::FunctionTemplate::New(isolate, get_computed_style));
        frame_window->Set(
            js_string(isolate, "focus"),
            v8::FunctionTemplate::New(isolate, window_focus));
        frame_window->Set(
            js_string(isolate, "scrollTo"),
            v8::FunctionTemplate::New(isolate, window_scroll_to));
        frame_window->Set(
            js_string(isolate, "scroll"),
            v8::FunctionTemplate::New(isolate, window_scroll_to));
        frame_window->SetNativeDataProperty(
            js_string(isolate, "frameElement"),
            get_provisional_frame_element);
        frame_window->SetNativeDataProperty(
            js_string(isolate, "innerWidth"),
            get_inner_width);
        frame_window->SetNativeDataProperty(
            js_string(isolate, "innerHeight"),
            get_inner_height);
        frame_window->SetNativeDataProperty(
            js_string(isolate, "devicePixelRatio"),
            get_device_pixel_ratio);
        frame_window->SetNativeDataProperty(
            js_string(isolate, "scrollX"),
            get_window_scroll_x);
        frame_window->SetNativeDataProperty(
            js_string(isolate, "scrollY"),
            get_window_scroll_y);
        frame_window->SetNativeDataProperty(
            js_string(isolate, "pageXOffset"),
            get_window_scroll_x);
        frame_window->SetNativeDataProperty(
            js_string(isolate, "pageYOffset"),
            get_window_scroll_y);
        frame_window_template.Reset(isolate, frame_window);

        auto document_template = v8::ObjectTemplate::New(isolate);
        document_template->SetInternalFieldCount(1);
        document_template->SetHandler(v8::NamedPropertyHandlerConfiguration(
            get_document_named_property,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            {},
            v8::PropertyHandlerFlags::kNonMasking));
        document_template->SetNativeDataProperty(js_string(isolate, "body"), get_body);
        document_template->SetNativeDataProperty(js_string(isolate, "documentElement"), get_body);
        document_template->SetNativeDataProperty(js_string(isolate, "head"), get_body);
        document_template->SetNativeDataProperty(js_string(isolate, "doctype"), get_document_doctype);
        document_template->SetNativeDataProperty(
            js_string(isolate, "scrollingElement"),
            get_scrolling_element);
        document_template->SetNativeDataProperty(js_string(isolate, "nodeType"), get_document_node_type);
        document_template->SetNativeDataProperty(js_string(isolate, "nodeName"), get_document_node_name);
        document_template->SetNativeDataProperty(
            js_string(isolate, "firstChild"), get_document_boundary_child);
        document_template->SetNativeDataProperty(
            js_string(isolate, "lastChild"), get_document_boundary_child);
        document_template->SetNativeDataProperty(
            js_string(isolate, "childNodes"), get_document_child_nodes);
        document_template->SetNativeDataProperty(
            js_string(isolate, "firstElementChild"), get_first_element_child);
        document_template->SetNativeDataProperty(
            js_string(isolate, "lastElementChild"), get_last_element_child);
        document_template->SetNativeDataProperty(js_string(isolate, "defaultView"), get_default_view);
        document_template->SetNativeDataProperty(js_string(isolate, "location"), get_document_location);
        document_template->SetNativeDataProperty(
            js_string(isolate, "dir"),
            get_document_dir,
            set_document_dir);
        document_template->SetNativeDataProperty(
            js_string(isolate, "hidden"),
            get_document_hidden);
        document_template->SetNativeDataProperty(
            js_string(isolate, "visibilityState"),
            get_document_visibility_state);
        document_template->SetNativeDataProperty(js_string(isolate, "links"), get_document_links);
        document_template->SetNativeDataProperty(js_string(isolate, "styleSheets"), get_document_style_sheets);
        document_template->SetNativeDataProperty(
            js_string(isolate, "cookie"),
            get_document_cookie,
            set_document_cookie);
        document_template->SetNativeDataProperty(
            js_string(isolate, "__webSceneDocumentCookie"),
            get_document_cookie,
            set_document_cookie,
            v8::Local<v8::Value>(),
            v8::PropertyAttribute::DontEnum);
        document_template->SetNativeDataProperty(js_string(isolate, "activeElement"), get_active_element);
        document_template->SetNativeDataProperty(
            js_string(isolate, "implementation"),
            get_document_implementation);
        document_template->Set(
            js_string(isolate, "createElement"),
            v8::FunctionTemplate::New(isolate, create_element));
        document_template->Set(
            js_string(isolate, "createElementNS"),
            v8::FunctionTemplate::New(isolate, create_element_ns));
        document_template->Set(
            js_string(isolate, "createTextNode"),
            v8::FunctionTemplate::New(isolate, create_text_node));
        document_template->Set(
            js_string(isolate, "createComment"),
            v8::FunctionTemplate::New(isolate, create_comment));
        document_template->Set(
            js_string(isolate, "createProcessingInstruction"),
            v8::FunctionTemplate::New(isolate, create_processing_instruction));
        document_template->Set(
            js_string(isolate, "createAttribute"),
            v8::FunctionTemplate::New(isolate, create_attribute));
        document_template->Set(
            js_string(isolate, "createAttributeNS"),
            v8::FunctionTemplate::New(isolate, create_attribute_ns));
        document_template->Set(
            js_string(isolate, "createDocumentFragment"),
            v8::FunctionTemplate::New(isolate, create_document_fragment));
        document_template->Set(
            js_string(isolate, "createEvent"),
            v8::FunctionTemplate::New(isolate, document_create_event));
        document_template->Set(
            js_string(isolate, "appendChild"),
            v8::FunctionTemplate::New(isolate, append_child));
        document_template->Set(
            js_string(isolate, "cloneNode"),
            v8::FunctionTemplate::New(isolate, clone_document));
        document_template->Set(
            js_string(isolate, "importNode"),
            v8::FunctionTemplate::New(isolate, document_import_node));
        document_template->Set(
            js_string(isolate, "contains"),
            v8::FunctionTemplate::New(isolate, document_contains));
        document_template->Set(
            js_string(isolate, "compareDocumentPosition"),
            v8::FunctionTemplate::New(isolate, element_compare_document_position));
        document_template->Set(
            js_string(isolate, "getElementById"),
            v8::FunctionTemplate::New(isolate, get_element_by_id));
        document_template->Set(
            js_string(isolate, "querySelectorAll"),
            v8::FunctionTemplate::New(isolate, document_query_selector_all));
        document_template->Set(
            js_string(isolate, "querySelector"),
            v8::FunctionTemplate::New(isolate, document_query_selector));
        document_template->Set(
            js_string(isolate, "elementFromPoint"),
            v8::FunctionTemplate::New(isolate, document_element_from_point));
        document_template->Set(
            js_string(isolate, "caretRangeFromPoint"),
            v8::FunctionTemplate::New(isolate, document_caret_range_from_point));
        document_template->Set(
            js_string(isolate, "getElementsByTagName"),
            v8::FunctionTemplate::New(isolate, document_get_elements_by_tag_name));
        document_template->Set(
            js_string(isolate, "getElementsByClassName"),
            v8::FunctionTemplate::New(isolate, document_get_elements_by_class_name));
        document_template->Set(
            js_string(isolate, "getElementsByName"),
            v8::FunctionTemplate::New(isolate, document_get_elements_by_name));
        document_template->Set(
            js_string(isolate, "createRange"),
            v8::FunctionTemplate::New(isolate, create_range));
        document_template->Set(
            js_string(isolate, "addEventListener"),
            v8::FunctionTemplate::New(isolate, add_event_listener));
        document_template->Set(
            js_string(isolate, "removeEventListener"),
            v8::FunctionTemplate::New(isolate, remove_event_listener));
        document_template->Set(
            js_string(isolate, "dispatchEvent"),
            v8::FunctionTemplate::New(isolate, dispatch_event));
        document_template->Set(
            js_string(isolate, "hasFocus"),
            v8::FunctionTemplate::New(isolate, document_has_focus));
        document_template->Set(
            js_string(isolate, "getSelection"),
            v8::FunctionTemplate::New(isolate, get_selection));
        this->document_template.Reset(isolate, document_template);
        auto implementation_value = v8::Object::New(isolate);
        implementation_value->Set(
            local_context,
            js_string(isolate, "createHTMLDocument"),
            v8::Function::New(local_context, create_html_document).ToLocalChecked()).Check();
        implementation_value->Set(
            local_context,
            js_string(isolate, "createDocument"),
            v8::Function::New(local_context, create_xml_document).ToLocalChecked()).Check();
        dom_implementation_object.Reset(isolate, implementation_value);
        auto document_value = document_template->NewInstance(local_context).ToLocalChecked();
        document_value->SetAlignedPointerInInternalField(
            0,
            &document.body(),
            v8::kEmbedderDataTypeTagDefault);
        // Modern input-event feature detection checks property presence before
        // installing its legacy IE attachEvent fallback.
        document_value->Set(
            local_context,
            js_string(isolate, "oninput"),
            v8::Null(isolate)).Check();
        document_object.Reset(isolate, document_value);
    }

    static session_storage_state* unwrap_session_storage(v8::Local<v8::Object> object)
    {
        return object->InternalFieldCount() < 1
            ? nullptr
            : static_cast<session_storage_state*>(object->GetAlignedPointerFromInternalField(
                0,
                v8::kEmbedderDataTypeTagDefault));
    }

    static session_storage_state* require_session_storage(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* storage = unwrap_session_storage(info.This());
        if (storage == nullptr) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Illegal invocation")));
        }
        return storage;
    }

    static bool storage_string(
        v8::Isolate* isolate,
        v8::Local<v8::Value> value,
        std::string& result)
    {
        v8::Local<v8::String> text;
        if (!value->ToString(isolate->GetCurrentContext()).ToLocal(&text)) {
            return false;
        }
        result.assign(text->Utf8LengthV2(isolate), '\0');
        const auto written = text->WriteUtf8V2(
            isolate,
            result.data(),
            result.size(),
            v8::String::WriteFlags::kNone);
        result.resize(written);
        return true;
    }

    static void session_storage_get_item(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        current(info.GetIsolate())->record_feature(
            "web-api", "Storage.getItem", "supported", {}, "web-api-binding");
        auto* storage = require_session_storage(info);
        if (storage == nullptr) return;
        std::string key;
        if (!storage_string(
                info.GetIsolate(),
                info.Length() > 0 ? info[0] : v8::Undefined(info.GetIsolate()),
                key)) return;
        const auto known = storage->values.find(key);
        info.GetReturnValue().Set(known == storage->values.end()
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(js_dom_string(info.GetIsolate(), known->second)));
    }

    static void session_storage_set_item(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        current(info.GetIsolate())->record_feature(
            "web-api", "Storage.setItem", "supported", {}, "web-api-binding");
        auto* storage = require_session_storage(info);
        if (storage == nullptr) return;
        std::string key;
        std::string value;
        if (!storage_string(
                info.GetIsolate(),
                info.Length() > 0 ? info[0] : v8::Undefined(info.GetIsolate()),
                key)
            || !storage_string(
                info.GetIsolate(),
                info.Length() > 1 ? info[1] : v8::Undefined(info.GetIsolate()),
                value)) return;
        if (!storage->values.contains(key)) storage->keys.push_back(key);
        storage->values[key] = value;
    }

    static void session_storage_remove_item(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        current(info.GetIsolate())->record_feature(
            "web-api", "Storage.removeItem", "supported", {}, "web-api-binding");
        auto* storage = require_session_storage(info);
        if (storage == nullptr) return;
        std::string key;
        if (!storage_string(
                info.GetIsolate(),
                info.Length() > 0 ? info[0] : v8::Undefined(info.GetIsolate()),
                key)) return;
        if (storage->values.erase(key) == 0U) return;
        std::erase(storage->keys, key);
    }

    static void session_storage_clear(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        current(info.GetIsolate())->record_feature(
            "web-api", "Storage.clear", "supported", {}, "web-api-binding");
        auto* storage = require_session_storage(info);
        if (storage == nullptr) return;
        storage->keys.clear();
        storage->values.clear();
    }

    static void session_storage_key(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        current(info.GetIsolate())->record_feature(
            "web-api", "Storage.key", "supported", {}, "web-api-binding");
        auto* storage = require_session_storage(info);
        if (storage == nullptr) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto maybe_index = (info.Length() > 0
            ? info[0]
            : v8::Undefined(info.GetIsolate()))->Uint32Value(context);
        if (maybe_index.IsNothing()) return;
        const auto index = maybe_index.FromJust();
        info.GetReturnValue().Set(index >= storage->keys.size()
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(js_dom_string(
                info.GetIsolate(), storage->keys[index])));
    }

    static void get_session_storage_length(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        current(info.GetIsolate())->record_feature(
            "web-api", "Storage.length", "supported", {}, "web-api-binding");
        auto* storage = unwrap_session_storage(info.Holder());
        if (storage != nullptr) {
            info.GetReturnValue().Set(v8::Integer::NewFromUnsigned(
                info.GetIsolate(),
                static_cast<uint32_t>(storage->keys.size())));
        }
    }

    v8::Local<v8::Object> create_session_storage(
        v8::Local<v8::Context> local_context,
        session_storage_state& storage)
    {
        auto storage_template = v8::ObjectTemplate::New(isolate);
        storage_template->SetInternalFieldCount(1);
        storage_template->Set(
            js_string(isolate, "getItem"),
            v8::FunctionTemplate::New(isolate, session_storage_get_item));
        storage_template->Set(
            js_string(isolate, "setItem"),
            v8::FunctionTemplate::New(isolate, session_storage_set_item));
        storage_template->Set(
            js_string(isolate, "removeItem"),
            v8::FunctionTemplate::New(isolate, session_storage_remove_item));
        storage_template->Set(
            js_string(isolate, "clear"),
            v8::FunctionTemplate::New(isolate, session_storage_clear));
        storage_template->Set(
            js_string(isolate, "key"),
            v8::FunctionTemplate::New(isolate, session_storage_key));
        storage_template->SetNativeDataProperty(
            js_string(isolate, "length"),
            get_session_storage_length,
            nullptr,
            v8::Local<v8::Value>(),
            v8::PropertyAttribute::ReadOnly);
        auto result = storage_template->NewInstance(local_context).ToLocalChecked();
        result->SetAlignedPointerInInternalField(
            0,
            &storage,
            v8::kEmbedderDataTypeTagDefault);
        return result;
    }

    static void fetch_text(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr || info.Length() == 0) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "fetch requires a URL")));
            return;
        }
        self->record_feature(
            "web-api",
            "Window.fetch",
            "partially-supported",
            "asynchronous GET/HEAD text responses through the host resource loader",
            "web-api-binding");
        const auto specifier = to_utf8(info.GetIsolate(), info[0]);
        const auto& base = self->current_base_address();
        const resource_request_context request_context{
            WEBSCENE_RESOURCE_INITIATOR_FETCH,
            resource_origin(base),
            base,
            WEBSCENE_FETCH_MODE_CORS,
            WEBSCENE_REQUEST_DESTINATION_NONE,
            "GET",
            {},
            {}};
        std::string content;
        std::string resolved;
        if (!self->load_text_resource(
                specifier,
                base,
                WEBSCENE_RESOURCE_DOCUMENT,
                content,
                resolved,
                nullptr,
                request_context)) {
            const auto message = "Unable to fetch WebScene resource: " + specifier;
            info.GetIsolate()->ThrowException(v8::Exception::Error(
                js_string(info.GetIsolate(), message.c_str())));
            return;
        }
        info.GetReturnValue().Set(js_dom_string(info.GetIsolate(), content));
    }

    static void fetch_resource(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr || info.Length() == 0) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "fetch requires a URL")));
            return;
        }
        const auto specifier = to_utf8(info.GetIsolate(), info[0]);
        auto method = info.Length() > 1
            ? to_utf8(info.GetIsolate(), info[1])
            : std::string{"GET"};
        std::transform(method.begin(), method.end(), method.begin(), [](unsigned char value) {
            return static_cast<char>(std::toupper(value));
        });
        const auto body = info.Length() > 2
            ? to_utf8(info.GetIsolate(), info[2])
            : std::string{};
        const auto content_type = info.Length() > 3
            ? to_utf8(info.GetIsolate(), info[3])
            : std::string{};
        const auto& base = self->current_base_address();
        auto resolved = resolve_resource_url(specifier, base);
        resource_request_context request_context{
            WEBSCENE_RESOURCE_INITIATOR_FETCH,
            resource_origin(base),
            base,
            WEBSCENE_FETCH_MODE_CORS,
            WEBSCENE_REQUEST_DESTINATION_NONE,
            method,
            body,
            content_type};
        const auto local_context = info.GetIsolate()->GetCurrentContext();
        if (self->pending_fetches.size() >= maximum_pending_fetches) {
            info.GetIsolate()->ThrowException(v8::Exception::Error(
                js_string(info.GetIsolate(), "Too many pending fetch requests")));
            return;
        }
        auto resolver = v8::Promise::Resolver::New(local_context).ToLocalChecked();
        pending_fetch_task pending;
        pending.context.Reset(info.GetIsolate(), local_context);
        pending.resolver.Reset(info.GetIsolate(), resolver);
        const auto notify = self->runtime_work_available;
        pending.future = std::async(
            std::launch::async,
            [self,
                specifier,
                resolved = std::move(resolved),
                method,
                request_context = std::move(request_context),
                notify]() mutable {
                async_fetch_result result;
                result.resolved_url = resolved;
                try {
                    resource_response response;
                    auto loaded = false;
                    if (self->load_resource_callback) {
                        if (method == "GET" || method == "HEAD") {
                            auto shared = self->load_resource_single_flight(
                                WEBSCENE_RESOURCE_DATA,
                                result.resolved_url,
                                request_context,
                                {},
                                0,
                                loaded);
                            if (loaded && shared != nullptr) response = *shared;
                        } else {
                            loaded = self->load_resource_callback(
                                WEBSCENE_RESOURCE_DATA,
                                result.resolved_url,
                                request_context,
                                {},
                                0,
                                response);
                        }
                    } else {
                        auto path = self->resolve_resource_path(specifier);
                        std::ifstream stream(path, std::ios::binary);
                        if (stream) {
                            response.content.assign(
                                std::istreambuf_iterator<char>(stream),
                                std::istreambuf_iterator<char>());
                            result.resolved_url = path.string();
                            loaded = true;
                        }
                    }
                    result.loaded = loaded;
                    if (loaded && method != "HEAD") {
                        result.body = std::move(response.content);
                    } else if (!loaded) {
                        result.error = "Unable to fetch WebScene resource: " + specifier;
                    }
                } catch (const std::exception& error) {
                    result.error = error.what();
                } catch (...) {
                    result.error = "Unable to fetch WebScene resource: " + specifier;
                }
                if (notify) notify();
                return result;
            }).share();
        self->pending_fetches.push_back(std::move(pending));
        self->record_feature(
            "web-api",
            "Window.fetch",
            "supported",
            "host resource I/O completes off the rendering worker and resolves as a later task",
            "web-api-binding");
        info.GetReturnValue().Set(resolver->GetPromise());
    }

    void install_document_constructor(
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Object> global,
        v8::Local<v8::Object> document_value)
    {
        auto constructor_template = v8::FunctionTemplate::New(isolate, document_constructor);
        constructor_template->SetClassName(js_string(isolate, "Document"));
        auto constructor = constructor_template->GetFunction(local_context).ToLocalChecked();
        global->Set(local_context, js_string(isolate, "Document"), constructor).Check();
        auto prototype = constructor->Get(
            local_context,
            js_string(isolate, "prototype")).ToLocalChecked().As<v8::Object>();
        prototype->Set(local_context, js_string(isolate, "execCommand"),
            v8::Function::New(local_context, document_exec_command, {}, 1,
                v8::ConstructorBehavior::kThrow).ToLocalChecked()).Check();
        const auto cookie_name = js_string(isolate, "cookie");
        auto cookie_getter = v8::Function::New(
            local_context,
            document_cookie_getter_bridge).ToLocalChecked();
        auto cookie_setter = v8::Function::New(
            local_context,
            document_cookie_setter_bridge).ToLocalChecked();
        v8::PropertyDescriptor cookie_descriptor(cookie_getter, cookie_setter);
        cookie_descriptor.set_enumerable(true);
        cookie_descriptor.set_configurable(true);
        prototype->DefineProperty(
            local_context,
            cookie_name,
            cookie_descriptor).Check();
        document_value->SetPrototype(local_context, prototype).Check();
    }

    static void document_cookie_getter_bridge(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        const auto local_context = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Value> value;
        if (info.This()->Get(
                local_context,
                js_string(info.GetIsolate(), "__webSceneDocumentCookie")).ToLocal(&value)) {
            info.GetReturnValue().Set(value);
        }
    }

    static void document_cookie_setter_bridge(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (info.Length() == 0) return;
        const auto local_context = info.GetIsolate()->GetCurrentContext();
        info.This()->Set(
            local_context,
            js_string(info.GetIsolate(), "__webSceneDocumentCookie"),
            info[0]).Check();
    }

    static constexpr std::string_view intersection_observer_bootstrap_source()
    {
        return R"JS(
          (() => {
            const normalizeThresholds = value => {
              const source = value === undefined ? [0] : Array.isArray(value) ? value : [value];
              const values = Array.from(new Set(source.map(item => {
                const threshold = Number(item);
                if (!Number.isFinite(threshold) || threshold < 0 || threshold > 1) {
                  throw new RangeError('IntersectionObserver threshold must be between 0 and 1');
                }
                return threshold;
              }))).sort((left, right) => left - right);
              return values.length ? values : [0];
            };
            const normalizeRootMargin = value => {
              const parts = String(value === undefined ? '0px' : value).trim().split(/\s+/).filter(Boolean);
              if (parts.length < 1 || parts.length > 4 || parts.some(part =>
                !/^-?(?:\d+|\d*\.\d+)(?:px|%)$/.test(part))) {
                throw new SyntaxError('IntersectionObserver rootMargin must use px or % lengths');
              }
              if (parts.length === 1) parts.push(parts[0], parts[0], parts[0]);
              else if (parts.length === 2) parts.push(parts[0], parts[1]);
              else if (parts.length === 3) parts.push(parts[1]);
              return parts.join(' ');
            };
            const marginPixels = (value, reference) => value.endsWith('%')
              ? Number.parseFloat(value) * reference / 100
              : Number.parseFloat(value);
            const cloneRect = rect => ({
              x: Number(rect.x), y: Number(rect.y),
              left: Number(rect.left), top: Number(rect.top),
              right: Number(rect.right), bottom: Number(rect.bottom),
              width: Number(rect.width), height: Number(rect.height)
            });
            class WebSceneIntersectionObserverEntry {
              constructor(target, rootBounds, targetRect, intersectionRect, isIntersecting, ratio) {
                this.time = typeof performance?.now === 'function' ? performance.now() : Date.now();
                this.target = target;
                this.rootBounds = rootBounds;
                this.boundingClientRect = targetRect;
                this.intersectionRect = intersectionRect;
                this.isIntersecting = isIntersecting;
                this.intersectionRatio = ratio;
              }
            }
            class WebSceneIntersectionObserver {
              constructor(callback, options = {}) {
                if (typeof callback !== 'function') {
                  throw new TypeError('IntersectionObserver callback must be a function');
                }
                this.root = options.root == null ? null : options.root;
                this.rootMargin = normalizeRootMargin(options.rootMargin);
                this.thresholds = normalizeThresholds(options.threshold);
                this._callback = callback;
                this._observations = [];
                this._timer = 0;
                this._queuedEntries = [];
              }
              _thresholdBucket(ratio) {
                let bucket = 0;
                while (bucket < this.thresholds.length && ratio >= this.thresholds[bucket]) bucket++;
                return bucket;
              }
              _rootRect() {
                const base = this.root
                  ? cloneRect(this.root.getBoundingClientRect())
                  : { x: 0, y: 0, left: 0, top: 0, right: Number(innerWidth),
                      bottom: Number(innerHeight), width: Number(innerWidth), height: Number(innerHeight) };
                const margin = this.rootMargin.split(/\s+/);
                const top = marginPixels(margin[0], base.height);
                const right = marginPixels(margin[1], base.width);
                const bottom = marginPixels(margin[2], base.height);
                const left = marginPixels(margin[3], base.width);
                return {
                  x: base.left - left, y: base.top - top,
                  left: base.left - left, top: base.top - top,
                  right: base.right + right, bottom: base.bottom + bottom,
                  width: Math.max(0, base.width + left + right),
                  height: Math.max(0, base.height + top + bottom)
                };
              }
              _check() {
                if (!this._observations.length) return;
                const rootBounds = this._rootRect();
                const entries = [];
                for (const observation of this._observations) {
                  const targetRect = cloneRect(observation.target.getBoundingClientRect());
                  const left = Math.max(rootBounds.left, targetRect.left);
                  const top = Math.max(rootBounds.top, targetRect.top);
                  const right = Math.min(rootBounds.right, targetRect.right);
                  const bottom = Math.min(rootBounds.bottom, targetRect.bottom);
                  const width = Math.max(0, right - left);
                  const height = Math.max(0, bottom - top);
                  const isIntersecting = width > 0 && height > 0;
                  const area = Math.max(0, targetRect.width) * Math.max(0, targetRect.height);
                  const ratio = area > 0 ? width * height / area : isIntersecting ? 1 : 0;
                  const bucket = this._thresholdBucket(ratio);
                  if (!observation.initialized || observation.isIntersecting !== isIntersecting
                      || observation.bucket !== bucket) {
                    observation.initialized = true;
                    observation.isIntersecting = isIntersecting;
                    observation.bucket = bucket;
                    entries.push(new WebSceneIntersectionObserverEntry(
                      observation.target, rootBounds, targetRect,
                      { x: left, y: top, left, top, right, bottom, width, height },
                      isIntersecting, ratio));
                  }
                }
                if (entries.length) this._callback(entries, this);
              }
              observe(target) {
                if (!target || typeof target.getBoundingClientRect !== 'function') {
                  throw new TypeError('IntersectionObserver.observe requires an Element');
                }
                if (this._observations.some(observation => observation.target === target)) return;
                this._observations.push({ target, initialized: false, isIntersecting: false, bucket: -1 });
                setTimeout(() => this._check(), 0);
                if (!this._timer) this._timer = setInterval(() => this._check(), 32);
              }
              unobserve(target) {
                const index = this._observations.findIndex(observation => observation.target === target);
                if (index >= 0) this._observations.splice(index, 1);
                if (!this._observations.length && this._timer) {
                  clearInterval(this._timer);
                  this._timer = 0;
                }
              }
              disconnect() {
                this._observations.length = 0;
                this._queuedEntries.length = 0;
                if (this._timer) clearInterval(this._timer);
                this._timer = 0;
              }
              takeRecords() {
                const records = this._queuedEntries.slice();
                this._queuedEntries.length = 0;
                return records;
              }
            }
            Object.defineProperties(globalThis, {
              IntersectionObserver: { value: WebSceneIntersectionObserver, writable: true, configurable: true },
              IntersectionObserverEntry: { value: WebSceneIntersectionObserverEntry, writable: true, configurable: true }
            });
          })();
        )JS";
    }

    void install_intersection_observer_polyfill(v8::Local<v8::Context> local_context)
    {
        if constexpr (bootstrap_snapshot_enabled) return;
        const auto source = intersection_observer_bootstrap_source();
        auto script = v8::Script::Compile(
            local_context,
            js_string(isolate, std::string(source).c_str())).ToLocalChecked();
        script->Run(local_context).ToLocalChecked();
    }

    static void websocket_open(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        if (self == nullptr
            || info.Length() < 4
            || !info[2]->IsArray()
            || !info[3]->IsFunction()) {
            isolate->ThrowException(v8::Exception::TypeError(
                js_string(isolate, "Invalid native WebSocket open request")));
            return;
        }

        const auto url = to_utf8(isolate, info[0]);
        const auto origin = to_utf8(isolate, info[1]);
        std::vector<std::string> protocols;
        auto protocol_values = info[2].As<v8::Array>();
        protocols.reserve(protocol_values->Length());
        for (uint32_t index = 0; index < protocol_values->Length(); ++index) {
            v8::Local<v8::Value> value;
            if (!protocol_values->Get(local_context, index).ToLocal(&value)) return;
            protocols.push_back(to_utf8(isolate, value));
        }

        const auto socket_id = self->websocket_transport.open(
            url,
            origin,
            std::move(protocols));
        if (socket_id == 0) {
            isolate->ThrowException(v8::Exception::Error(
                js_string(isolate, "Unable to create native WebSocket")));
            return;
        }
        self->websocket_bindings.emplace(
            socket_id,
            websocket_binding{
                v8::Global<v8::Context>(isolate, local_context),
                v8::Global<v8::Function>(isolate, info[3].As<v8::Function>())});
        info.GetReturnValue().Set(v8::Number::New(
            isolate,
            static_cast<double>(socket_id)));
    }

    static void websocket_send(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        if (self == nullptr || info.Length() < 3 || !info[0]->IsNumber()) {
            isolate->ThrowException(v8::Exception::TypeError(
                js_string(isolate, "Invalid native WebSocket send request")));
            return;
        }
        const auto socket_id = static_cast<uint64_t>(
            info[0]->IntegerValue(local_context).FromMaybe(0));
        const auto binary = info[2]->BooleanValue(isolate);
        const uint8_t* bytes = nullptr;
        size_t byte_count = 0;
        std::string text;
        if (info[1]->IsArrayBuffer()) {
            const auto backing = info[1].As<v8::ArrayBuffer>()->GetBackingStore();
            bytes = static_cast<const uint8_t*>(backing->Data());
            byte_count = backing->ByteLength();
        } else if (info[1]->IsArrayBufferView()) {
            const auto view = info[1].As<v8::ArrayBufferView>();
            const auto backing = view->Buffer()->GetBackingStore();
            bytes = static_cast<const uint8_t*>(backing->Data()) + view->ByteOffset();
            byte_count = view->ByteLength();
        } else {
            text = to_utf8(isolate, info[1]);
            bytes = reinterpret_cast<const uint8_t*>(text.data());
            byte_count = text.size();
        }
        static constexpr uint8_t empty_payload = 0;
        if (bytes == nullptr) bytes = &empty_payload;
        info.GetReturnValue().Set(v8::Boolean::New(
            isolate,
            self->websocket_transport.send(
                socket_id,
                bytes,
                byte_count,
                binary)));
    }

    static void websocket_close(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        if (self == nullptr || info.Length() < 3 || !info[0]->IsNumber()) return;
        const auto socket_id = static_cast<uint64_t>(
            info[0]->IntegerValue(local_context).FromMaybe(0));
        const auto close_code = static_cast<uint16_t>(
            std::clamp(
                info[1]->Int32Value(local_context).FromMaybe(1000),
                0,
                65535));
        info.GetReturnValue().Set(v8::Boolean::New(
            isolate,
            self->websocket_transport.close(
                socket_id,
                close_code,
                to_utf8(isolate, info[2]))));
    }

    static void websocket_buffered_amount(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        if (self == nullptr || info.Length() < 1 || !info[0]->IsNumber()) {
            info.GetReturnValue().Set(v8::Number::New(isolate, 0));
            return;
        }
        const auto socket_id = static_cast<uint64_t>(
            info[0]->IntegerValue(local_context).FromMaybe(0));
        info.GetReturnValue().Set(v8::Number::New(
            isolate,
            static_cast<double>(
                self->websocket_transport.buffered_amount(socket_id))));
    }

    bool drain_websocket_event()
    {
        native_websocket_transport::event value;
        if (!websocket_transport.try_pop(value)) return true;
        const auto binding_iterator = websocket_bindings.find(value.socket_id);
        if (binding_iterator == websocket_bindings.end()) {
            if (value.type == native_websocket_transport::event_type::closed) {
                websocket_transport.release(value.socket_id);
            }
            return true;
        }
        auto event_context = binding_iterator->second.context.Get(isolate);
        auto callback = binding_iterator->second.callback.Get(isolate);
        if (event_context.IsEmpty() || callback.IsEmpty()) {
            websocket_bindings.erase(binding_iterator);
            websocket_transport.release(value.socket_id);
            return true;
        }
        v8::Context::Scope event_scope(event_context);
        auto envelope = v8::Object::New(isolate);
        const auto set_string = [&](const char* name, const std::string& text) {
            envelope->Set(
                event_context,
                js_string(isolate, name),
                js_string(isolate, text.c_str())).Check();
        };
        switch (value.type) {
            case native_websocket_transport::event_type::opened:
                set_string("type", "open");
                set_string("protocol", value.protocol);
                set_string("extensions", value.extensions);
                break;
            case native_websocket_transport::event_type::message: {
                set_string("type", "message");
                envelope->Set(
                    event_context,
                    js_string(isolate, "binary"),
                    v8::Boolean::New(isolate, value.binary)).Check();
                if (value.binary) {
                    auto buffer = v8::ArrayBuffer::New(isolate, value.payload.size());
                    if (!value.payload.empty()) {
                        std::memcpy(
                            buffer->GetBackingStore()->Data(),
                            value.payload.data(),
                            value.payload.size());
                    }
                    envelope->Set(
                        event_context,
                        js_string(isolate, "data"),
                        v8::Uint8Array::New(buffer, 0, value.payload.size())).Check();
                } else {
                    envelope->Set(
                        event_context,
                        js_string(isolate, "data"),
                        v8::String::NewFromUtf8(
                            isolate,
                            reinterpret_cast<const char*>(value.payload.data()),
                            v8::NewStringType::kNormal,
                            static_cast<int>(value.payload.size())).ToLocalChecked()).Check();
                }
                break;
            }
            case native_websocket_transport::event_type::error:
                set_string("type", "error");
                set_string("message", value.reason);
                break;
            case native_websocket_transport::event_type::closed:
                set_string("type", "close");
                set_string("reason", value.reason);
                envelope->Set(
                    event_context,
                    js_string(isolate, "code"),
                    v8::Integer::New(isolate, value.close_code)).Check();
                envelope->Set(
                    event_context,
                    js_string(isolate, "wasClean"),
                    v8::Boolean::New(isolate, value.was_clean)).Check();
                break;
        }

        v8::Local<v8::Value> arguments[] = {envelope};
        v8::TryCatch try_catch(isolate);
        if (callback->Call(
                event_context,
                event_context->Global(),
                1,
                arguments).IsEmpty()) {
            last_error = "WebSocket event dispatch failed: "
                + describe_reported_exception(try_catch, event_context);
            if (value.type == native_websocket_transport::event_type::closed) {
                websocket_bindings.erase(value.socket_id);
                websocket_transport.release(value.socket_id);
            }
            return false;
        }
        perform_microtask_checkpoint();
        if (value.type == native_websocket_transport::event_type::closed) {
            websocket_bindings.erase(value.socket_id);
            websocket_transport.release(value.socket_id);
        }
        return true;
    }

    void install_websocket_globals(v8::Local<v8::Context> local_context)
    {
        auto global = local_context->Global();
        global->Set(
            local_context,
            js_string(isolate, "__webSceneWebSocketOpen"),
            v8::Function::New(local_context, websocket_open).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "__webSceneWebSocketSend"),
            v8::Function::New(local_context, websocket_send).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "__webSceneWebSocketClose"),
            v8::Function::New(local_context, websocket_close).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "__webSceneWebSocketBufferedAmount"),
            v8::Function::New(
                local_context,
                websocket_buffered_amount).ToLocalChecked()).Check();

        if constexpr (bootstrap_snapshot_enabled) return;

        constexpr std::string_view source = R"JS(
          (() => {
            const diagnostics = {
              created: 0, opened: 0, messages: 0, bytesReceived: 0,
              errors: 0, closed: 0
            };

            class WebSceneEventTarget {
              constructor() {
                Object.defineProperty(this, '_listeners', {
                  value: new Map(), configurable: false
                });
              }
              addEventListener(type, listener, options = {}) {
                if (typeof listener !== 'function'
                    && typeof listener?.handleEvent !== 'function') return;
                const name = String(type);
                const listeners = this._listeners.get(name) || [];
                if (listeners.some(entry => entry.listener === listener)) return;
                const entry = {
                  listener,
                  once: Boolean(options && typeof options === 'object'
                    ? options.once : false)
                };
                listeners.push(entry);
                this._listeners.set(name, listeners);
                const signal = options && typeof options === 'object'
                  ? options.signal : null;
                if (signal?.aborted) {
                  this.removeEventListener(name, listener);
                } else if (signal?.addEventListener) {
                  signal.addEventListener(
                    'abort',
                    () => this.removeEventListener(name, listener),
                    { once: true });
                }
              }
              removeEventListener(type, listener) {
                const listeners = this._listeners.get(String(type));
                if (!listeners) return;
                const index = listeners.findIndex(entry => entry.listener === listener);
                if (index >= 0) listeners.splice(index, 1);
              }
              dispatchEvent(event) {
                if (!event || !event.type) {
                  throw new TypeError('dispatchEvent requires an Event');
                }
                const type = String(event.type);
                Object.defineProperties(event, {
                  target: { value: this, configurable: true },
                  currentTarget: { value: this, configurable: true }
                });
                const handler = this[`on${type}`];
                if (typeof handler === 'function') handler.call(this, event);
                for (const entry of (this._listeners.get(type) || []).slice()) {
                  if (typeof entry.listener === 'function') {
                    entry.listener.call(this, event);
                  } else {
                    entry.listener.handleEvent(event);
                  }
                  if (entry.once) this.removeEventListener(type, entry.listener);
                }
                return !event.defaultPrevented;
              }
            }

            class WebSceneMessageEvent {
              constructor(type, init = {}) {
                this.type = String(type);
                this.data = init.data;
                this.origin = init.origin || '';
                this.lastEventId = init.lastEventId || '';
                this.source = init.source || null;
                this.ports = init.ports || [];
                this.defaultPrevented = false;
              }
              preventDefault() { this.defaultPrevented = true; }
            }
            class WebSceneCloseEvent {
              constructor(type, init = {}) {
                this.type = String(type);
                this.code = Number(init.code) || 0;
                this.reason = String(init.reason || '');
                this.wasClean = Boolean(init.wasClean);
                this.defaultPrevented = false;
              }
              preventDefault() { this.defaultPrevented = true; }
            }
            class WebSceneErrorEvent {
              constructor(type, init = {}) {
                this.type = String(type);
                this.message = String(init.message || '');
                this.error = init.error;
                this.defaultPrevented = false;
              }
              preventDefault() { this.defaultPrevented = true; }
            }

            const validProtocol = value => {
              if (!value || /[()<>@,;:\\"\/\[\]?={} \t]/.test(value)) return false;
              for (const character of value) {
                const code = character.charCodeAt(0);
                if (code < 0x21 || code > 0x7e) return false;
              }
              return true;
            };
            const documentOrigin = () => {
              const protocol = String(globalThis.location?.protocol || '');
              const host = String(globalThis.location?.host || '');
              return (protocol === 'http:' || protocol === 'https:') && host
                ? `${protocol}//${host}` : '';
            };

            class WebSceneWebSocket extends WebSceneEventTarget {
              constructor(url, protocols = []) {
                super();
                if (arguments.length === 0) {
                  throw new TypeError("Failed to construct 'WebSocket': 1 argument required");
                }
                let resolved = __webSceneResolveUrl(
                  String(url),
                  String(globalThis.location?.href || ''));
                if (resolved.startsWith('http:')) resolved = `ws:${resolved.slice(5)}`;
                else if (resolved.startsWith('https:')) resolved = `wss:${resolved.slice(6)}`;
                if ((!resolved.startsWith('ws:') && !resolved.startsWith('wss:'))
                    || resolved.includes('#')) {
                  throw new DOMException('Invalid WebSocket URL', 'SyntaxError');
                }
                const requested = typeof protocols === 'string'
                  ? [protocols] : Array.from(protocols);
                const seen = new Set();
                for (let index = 0; index < requested.length; ++index) {
                  requested[index] = String(requested[index]);
                  if (!validProtocol(requested[index])
                      || seen.has(requested[index])) {
                    throw new DOMException(
                      'Invalid or duplicate WebSocket subprotocol',
                      'SyntaxError');
                  }
                  seen.add(requested[index]);
                }

                Object.defineProperties(this, {
                  url: { value: resolved, enumerable: true },
                  protocol: { value: '', writable: true, enumerable: true },
                  extensions: { value: '', writable: true, enumerable: true },
                  readyState: {
                    value: WebSceneWebSocket.CONNECTING,
                    writable: true,
                    enumerable: true
                  },
                  onopen: { value: null, writable: true, enumerable: true },
                  onmessage: { value: null, writable: true, enumerable: true },
                  onerror: { value: null, writable: true, enumerable: true },
                  onclose: { value: null, writable: true, enumerable: true },
                  _binaryType: { value: 'blob', writable: true },
                  _id: {
                    value: __webSceneWebSocketOpen(
                      resolved,
                      documentOrigin(),
                      requested,
                      envelope => this._receive(envelope))
                  }
                });
                ++diagnostics.created;
                __webSceneRecordWebApi(
                  'WebSocket.constructor',
                  'supported',
                  'native RFC 6455 client with ws/wss, subprotocol, binary, compression, and close support');
              }
              get binaryType() { return this._binaryType; }
              set binaryType(value) {
                value = String(value);
                if (value === 'blob' || value === 'arraybuffer') {
                  this._binaryType = value;
                }
              }
              get bufferedAmount() {
                return __webSceneWebSocketBufferedAmount(this._id);
              }
              send(data) {
                if (this.readyState === WebSceneWebSocket.CONNECTING) {
                  throw new DOMException(
                    "Failed to execute 'send': WebSocket is still CONNECTING",
                    'InvalidStateError');
                }
                if (this.readyState !== WebSceneWebSocket.OPEN) return;
                if (data instanceof ArrayBuffer) {
                  __webSceneWebSocketSend(this._id, new Uint8Array(data), true);
                } else if (ArrayBuffer.isView(data)) {
                  __webSceneWebSocketSend(
                    this._id,
                    new Uint8Array(data.buffer, data.byteOffset, data.byteLength),
                    true);
                } else if (typeof Blob === 'function' && data instanceof Blob) {
                  __webSceneWebSocketSend(this._id, String(data), true);
                } else {
                  __webSceneWebSocketSend(this._id, String(data), false);
                }
              }
              close(code, reason = '') {
                if (code !== undefined
                    && code !== 1000
                    && !(code >= 3000 && code <= 4999)) {
                  throw new DOMException('Invalid WebSocket close code', 'InvalidAccessError');
                }
                reason = String(reason);
                if (new TextEncoder().encode(reason).byteLength > 123) {
                  throw new DOMException(
                    'WebSocket close reason exceeds 123 UTF-8 bytes',
                    'SyntaxError');
                }
                if (this.readyState === WebSceneWebSocket.CLOSING
                    || this.readyState === WebSceneWebSocket.CLOSED) return;
                this.readyState = WebSceneWebSocket.CLOSING;
                __webSceneWebSocketClose(this._id, code === undefined ? 1000 : code, reason);
              }
              _receive(envelope) {
                switch (envelope.type) {
                  case 'open':
                    if (this.readyState !== WebSceneWebSocket.CONNECTING) return;
                    this.protocol = envelope.protocol || '';
                    this.extensions = envelope.extensions || '';
                    this.readyState = WebSceneWebSocket.OPEN;
                    ++diagnostics.opened;
                    this.dispatchEvent({ type: 'open', defaultPrevented: false });
                    break;
                  case 'message': {
                    if (this.readyState !== WebSceneWebSocket.OPEN) return;
                    let data = envelope.data;
                    if (envelope.binary) {
                      diagnostics.bytesReceived += data.byteLength;
                      if (this.binaryType === 'arraybuffer') {
                        data = data.buffer.slice(
                          data.byteOffset,
                          data.byteOffset + data.byteLength);
                      } else {
                        data = new Blob([data]);
                      }
                    } else {
                      diagnostics.bytesReceived += String(data).length;
                    }
                    ++diagnostics.messages;
                    this.dispatchEvent(new WebSceneMessageEvent('message', { data }));
                    break;
                  }
                  case 'error':
                    ++diagnostics.errors;
                    this.dispatchEvent(new WebSceneErrorEvent(
                      'error',
                      { message: envelope.message || 'WebSocket connection error' }));
                    break;
                  case 'close':
                    this.readyState = WebSceneWebSocket.CLOSED;
                    ++diagnostics.closed;
                    this.dispatchEvent(new WebSceneCloseEvent('close', envelope));
                    break;
                }
              }
            }
            for (const [name, value] of Object.entries({
              CONNECTING: 0, OPEN: 1, CLOSING: 2, CLOSED: 3
            })) {
              Object.defineProperty(WebSceneWebSocket, name, {
                value, enumerable: true
              });
              Object.defineProperty(WebSceneWebSocket.prototype, name, {
                value, enumerable: true
              });
            }

            Object.defineProperties(globalThis, {
              EventTarget: {
                value: globalThis.EventTarget || WebSceneEventTarget,
                writable: true,
                configurable: true
              },
              MessageEvent: {
                value: globalThis.MessageEvent || WebSceneMessageEvent,
                writable: true,
                configurable: true
              },
              CloseEvent: {
                value: globalThis.CloseEvent || WebSceneCloseEvent,
                writable: true,
                configurable: true
              },
              ErrorEvent: {
                value: globalThis.ErrorEvent || WebSceneErrorEvent,
                writable: true,
                configurable: true
              },
              WebSocket: {
                value: WebSceneWebSocket,
                writable: true,
                configurable: true
              },
              __webSceneWebSocketDiagnostics: {
                value: () => ({ ...diagnostics }),
                configurable: true
              }
            });
          })();
        )JS";
        auto script = v8::Script::Compile(
            local_context,
            js_string(isolate, std::string(source).c_str())).ToLocalChecked();
        script->Run(local_context).ToLocalChecked();
    }

    void install_editor_web_platform_globals(v8::Local<v8::Context> local_context)
    {
        if constexpr (bootstrap_snapshot_enabled) return;
        // These are general browser primitives used by Monaco and other
        // component runtimes. Keep them inside WebScene's native realm so
        // applications do not have to patch third-party bundles.
        constexpr std::string_view source_parts[] = {R"JS(
          (() => {
            const enqueueMicrotask = callback => {
              if (typeof callback !== 'function') {
                throw new TypeError('queueMicrotask requires a function');
              }
              Promise.resolve().then(callback);
            };

            class WebSceneTextEncoder {
              constructor() {
                Object.defineProperty(this, 'encoding', {
                  value: 'utf-8', enumerable: true
                });
              }
              encode(input = '') {
                const bytes = [];
                for (const character of String(input)) {
                  let scalar = character.codePointAt(0);
                  if (scalar >= 0xd800 && scalar <= 0xdfff) scalar = 0xfffd;
                  if (scalar <= 0x7f) {
                    bytes.push(scalar);
                  } else if (scalar <= 0x7ff) {
                    bytes.push(0xc0 | (scalar >> 6), 0x80 | (scalar & 0x3f));
                  } else if (scalar <= 0xffff) {
                    bytes.push(
                      0xe0 | (scalar >> 12),
                      0x80 | ((scalar >> 6) & 0x3f),
                      0x80 | (scalar & 0x3f));
                  } else {
                    bytes.push(
                      0xf0 | (scalar >> 18),
                      0x80 | ((scalar >> 12) & 0x3f),
                      0x80 | ((scalar >> 6) & 0x3f),
                      0x80 | (scalar & 0x3f));
                  }
                }
                return new Uint8Array(bytes);
              }
              encodeInto(input, destination) {
                if (!(destination instanceof Uint8Array)) {
                  throw new TypeError('TextEncoder.encodeInto requires a Uint8Array');
                }
                let read = 0;
                let written = 0;
                for (const character of String(input)) {
                  const encoded = this.encode(character);
                  if (written + encoded.length > destination.length) break;
                  destination.set(encoded, written);
                  written += encoded.length;
                  read += character.length;
                }
                return { read, written };
              }
            }

            const decoderLabels = new Map([
              ['utf-8', 'utf-8'], ['utf8', 'utf-8'], ['unicode-1-1-utf-8', 'utf-8'],
              ['utf-16', 'utf-16le'], ['utf-16le', 'utf-16le'], ['utf16le', 'utf-16le'],
              ['utf-16be', 'utf-16be'], ['utf16be', 'utf-16be']
            ]);
            class WebSceneTextDecoder {
              constructor(label = 'utf-8', options = {}) {
                const normalized = String(label).trim().toLowerCase();
                const encoding = decoderLabels.get(normalized);
                if (!encoding) throw new RangeError(`Unsupported encoding: ${label}`);
                Object.defineProperties(this, {
                  encoding: { value: encoding, enumerable: true },
                  fatal: { value: Boolean(options.fatal), enumerable: true },
                  ignoreBOM: { value: Boolean(options.ignoreBOM), enumerable: true }
                });
              }
              decode(input = new Uint8Array()) {
                let bytes;
                if (input instanceof ArrayBuffer) {
                  bytes = new Uint8Array(input);
                } else if (ArrayBuffer.isView(input)) {
                  bytes = new Uint8Array(
                    input.buffer, input.byteOffset, input.byteLength);
                } else {
                  throw new TypeError('TextDecoder.decode requires an ArrayBuffer view');
                }
                if (this.encoding === 'utf-16le' || this.encoding === 'utf-16be') {
                  const littleEndian = this.encoding === 'utf-16le';
                  let cursor = 0;
                  if (!this.ignoreBOM && bytes.length >= 2) {
                    if (bytes[0] === 0xff && bytes[1] === 0xfe) cursor = 2;
                    else if (bytes[0] === 0xfe && bytes[1] === 0xff) cursor = 2;
                  }
                  let result = '';
                  for (; cursor + 1 < bytes.length; cursor += 2) {
                    result += String.fromCharCode(littleEndian
                      ? bytes[cursor] | (bytes[cursor + 1] << 8)
                      : (bytes[cursor] << 8) | bytes[cursor + 1]);
                  }
                  if (cursor < bytes.length) {
                    if (this.fatal) throw new TypeError('Invalid UTF-16 data');
                    result += '\ufffd';
                  }
                  return result;
                }

                let cursor = 0;
                if (!this.ignoreBOM && bytes.length >= 3
                    && bytes[0] === 0xef && bytes[1] === 0xbb
                    && bytes[2] === 0xbf) {
                  cursor = 3;
                }
                let result = '';
                const invalid = () => {
                  if (this.fatal) throw new TypeError('Invalid UTF-8 data');
                  result += '\ufffd';
                };
                while (cursor < bytes.length) {
                  const first = bytes[cursor++];
                  if (first <= 0x7f) {
                    result += String.fromCharCode(first);
                    continue;
                  }
                  let scalar = 0;
                  let continuationCount = 0;
                  let minimum = 0;
                  if (first >= 0xc2 && first <= 0xdf) {
                    scalar = first & 0x1f;
                    continuationCount = 1;
                    minimum = 0x80;
                  } else if (first >= 0xe0 && first <= 0xef) {
                    scalar = first & 0x0f;
                    continuationCount = 2;
                    minimum = 0x800;
                  } else if (first >= 0xf0 && first <= 0xf4) {
                    scalar = first & 0x07;
                    continuationCount = 3;
                    minimum = 0x10000;
                  } else {
                    invalid();
                    continue;
                  }
                  if (cursor + continuationCount > bytes.length) {
                    cursor = bytes.length;
                    invalid();
                    continue;
                  }
                  let valid = true;
                  for (let index = 0; index < continuationCount; ++index) {
                    const continuation = bytes[cursor + index];
                    if ((continuation & 0xc0) !== 0x80) {
                      valid = false;
                      break;
                    }
                    scalar = (scalar << 6) | (continuation & 0x3f);
                  }
                  if (!valid || scalar < minimum || scalar > 0x10ffff
                      || (scalar >= 0xd800 && scalar <= 0xdfff)) {
                    invalid();
                    continue;
                  }
                  cursor += continuationCount;
                  result += String.fromCodePoint(scalar);
                }
                return result;
              }
            }

            const nodeFilterConstants = {
              FILTER_ACCEPT: 1, FILTER_REJECT: 2, FILTER_SKIP: 3,
              SHOW_ALL: 0xffffffff, SHOW_ELEMENT: 0x1, SHOW_ATTRIBUTE: 0x2,
              SHOW_TEXT: 0x4, SHOW_CDATA_SECTION: 0x8,
              SHOW_ENTITY_REFERENCE: 0x10, SHOW_ENTITY: 0x20,
              SHOW_PROCESSING_INSTRUCTION: 0x40, SHOW_COMMENT: 0x80,
              SHOW_DOCUMENT: 0x100, SHOW_DOCUMENT_TYPE: 0x200,
              SHOW_DOCUMENT_FRAGMENT: 0x400, SHOW_NOTATION: 0x800
            };
            function WebSceneNodeFilter() {
              throw new TypeError('Illegal constructor');
            }
            Object.assign(WebSceneNodeFilter, nodeFilterConstants);
            Object.assign(WebSceneNodeFilter.prototype, nodeFilterConstants);

            class WebSceneTreeWalker {
              constructor(root, whatToShow = nodeFilterConstants.SHOW_ALL, filter = null) {
                if (!root || typeof root.nodeType !== 'number') {
                  throw new TypeError('TreeWalker root must be a Node');
                }
                this.root = root;
                this.whatToShow = Number(whatToShow) >>> 0;
                this.filter = filter ?? null;
                this._currentNode = root;
              }
              get currentNode() { return this._currentNode; }
              set currentNode(value) {
                if (!value || typeof value.nodeType !== 'number') {
                  throw new TypeError('TreeWalker.currentNode must be a Node');
                }
                this._currentNode = value;
              }
              _filterNode(node) {
                const mask = node.nodeType > 0 && node.nodeType <= 32
                  ? (1 << (node.nodeType - 1)) >>> 0
                  : 0;
                if ((this.whatToShow & mask) === 0) {
                  return nodeFilterConstants.FILTER_SKIP;
                }
                if (this.filter == null) return nodeFilterConstants.FILTER_ACCEPT;
                const callback = typeof this.filter === 'function'
                  ? this.filter
                  : this.filter.acceptNode;
                if (typeof callback !== 'function') {
                  throw new TypeError('TreeWalker filter must be callable');
                }
                return Number(callback.call(this.filter, node));
              }
              _visibleChildren(parent) {
                const result = [];
                const append = node => {
                  for (let child = node.firstChild; child; child = child.nextSibling) {
                    const decision = this._filterNode(child);
                    if (decision === nodeFilterConstants.FILTER_ACCEPT) {
                      result.push(child);
                    } else if (decision === nodeFilterConstants.FILTER_SKIP) {
                      append(child);
                    }
                  }
                };
                append(parent);
                return result;
              }
              _acceptedNodes() {
                const result = [];
                const visit = node => {
                  for (let child = node.firstChild; child; child = child.nextSibling) {
                    const decision = this._filterNode(child);
                    if (decision === nodeFilterConstants.FILTER_ACCEPT) {
                      result.push(child);
                      visit(child);
                    } else if (decision === nodeFilterConstants.FILTER_SKIP) {
                      visit(child);
                    }
                  }
                };
                visit(this.root);
                return result;
              }
              parentNode() {
                if (this._currentNode === this.root) return null;
                for (let parent = this._currentNode.parentNode;
                     parent;
                     parent = parent.parentNode) {
                  if (parent === this.root) {
                    this._currentNode = parent;
                    return parent;
                  }
                  if (this._filterNode(parent) === nodeFilterConstants.FILTER_ACCEPT) {
                    this._currentNode = parent;
                    return parent;
                  }
                }
                return null;
              }
              firstChild() {
                const child = this._visibleChildren(this._currentNode)[0] ?? null;
                if (child) this._currentNode = child;
                return child;
              }
              lastChild() {
                const children = this._visibleChildren(this._currentNode);
                const child = children[children.length - 1] ?? null;
                if (child) this._currentNode = child;
                return child;
              }
              _sibling(direction) {
                if (this._currentNode === this.root) return null;
                for (let parent = this._currentNode.parentNode;
                     parent;
                     parent = parent.parentNode) {
                  const isVisibleParent = parent === this.root
                    || this._filterNode(parent) === nodeFilterConstants.FILTER_ACCEPT;
                  if (!isVisibleParent) continue;
                  const siblings = this._visibleChildren(parent);
                  const index = siblings.indexOf(this._currentNode);
                  if (index >= 0) {
                    const sibling = siblings[index + direction] ?? null;
                    if (sibling) this._currentNode = sibling;
                    return sibling;
                  }
                  if (parent === this.root) break;
                }
                return null;
              }
              previousSibling() { return this._sibling(-1); }
              nextSibling() { return this._sibling(1); }
              previousNode() {
                const nodes = this._acceptedNodes();
                const index = nodes.indexOf(this._currentNode);
                const previous = index > 0 ? nodes[index - 1] : null;
                if (previous) this._currentNode = previous;
                return previous;
              }
              nextNode() {
                const nodes = this._acceptedNodes();
                const index = this._currentNode === this.root
                  ? -1
                  : nodes.indexOf(this._currentNode);
                const next = index >= -1 ? nodes[index + 1] ?? null : null;
                if (next) this._currentNode = next;
                return next;
              }
            }

            const installTreeWalkerPlatform = () => {
              const createTreeWalker = function(
                  root,
                  whatToShow = nodeFilterConstants.SHOW_ALL,
                  filter = null) {
                return new WebSceneTreeWalker(root, whatToShow, filter);
              };
              const documentPrototype = globalThis.Document?.prototype
                ?? Object.getPrototypeOf(document);
              Object.defineProperty(documentPrototype, 'createTreeWalker', {
                value: createTreeWalker, writable: true, configurable: true
              });
            };
            )JS",
            R"JS(

            const installCustomElementsPlatform = () => {
            if (globalThis.__webSceneCustomElementsNotifySubtree) return;
            const activateCustomElements = globalThis.__webSceneActivateCustomElements;
            const NativeHTMLElement = globalThis.HTMLElement;
            const nativeCreateElement = document.createElement;
            const definitions = new Map();
            const constructorDefinitions = new Map();
            const pendingDefinitions = new Map();
            const elementStates = new WeakMap();
            const constructionStack = [];
            let registryActive = false;
            const reservedNames = new Set([
              'annotation-xml', 'color-profile', 'font-face',
              'font-face-src', 'font-face-uri', 'font-face-format',
              'font-face-name', 'missing-glyph'
            ]);
            // HTML's PotentialCustomElementName production. WebScene's DOM
            // strings are WTF-8 backed; JavaScript sees the corresponding
            // UTF-16 code units, which this expression validates directly.
            const potentialName = /^[a-z](?:[.0-9_a-z-]|[\u00b7\u00c0-\u00d6\u00d8-\u00f6\u00f8-\u037d\u037f-\u1fff\u200c-\u200d\u203f-\u2040\u2070-\u218f\u2c00-\ufeff\u{10000}-\u{effff}])*-(?:[.0-9_a-z-]|[\u00b7\u00c0-\u00d6\u00d8-\u00f6\u00f8-\u037d\u037f-\u1fff\u200c-\u200d\u203f-\u2040\u2070-\u218f\u2c00-\ufeff\u{10000}-\u{effff}])*$/u;

            const normalizeName = name => String(name);
            const isValidName = name => potentialName.test(name)
              && !reservedNames.has(name);
            const syntaxError = name => new DOMException(
              `'${name}' is not a valid custom element name`, 'SyntaxError');
            const reportReactionError = error => {
              try {
                console.error(error && (error.stack || error.message) || String(error));
              } catch (_) {}
            };
            const elementName = element => String(element && element.localName || '');
            const elementChildren = element => {
              const children = element && element.childNodes;
              if (!children || typeof children.length !== 'number') return [];
              return Array.from(children);
            };
            const walkElements = (root, callback) => {
              if (!root) return;
              const visit = node => {
                if (!node) return;
                if (node.nodeType === 1) callback(node);
                for (const child of elementChildren(node)) visit(child);
                if (node.nodeType === 11) {
                  // DocumentFragment.childNodes is already visited above; the
                  // branch documents that registry.upgrade includes fragments.
                }
              };
              visit(root);
            };
            const invokeReaction = (element, callback, args) => {
              if (typeof callback !== 'function') return;
              try {
                Reflect.apply(callback, element, args);
              } catch (error) {
                reportReactionError(error);
              }
            };

            function WebSceneHTMLElement() {
              if (!new.target) {
                throw new TypeError(
                  "Failed to construct 'HTMLElement': Please use the 'new' operator");
              }
              if (new.target === WebSceneHTMLElement) {
                throw new TypeError('Illegal constructor');
              }
              const current = constructionStack[constructionStack.length - 1];
              if (current && current.definition.constructor === new.target) {
                if (current.constructed) {
                  throw new TypeError('Custom element constructor called super() more than once');
                }
                current.constructed = true;
                Object.setPrototypeOf(current.element, current.definition.prototype);
                return current.element;
              }
              const definition = constructorDefinitions.get(new.target);
              if (!definition) throw new TypeError('Illegal constructor');
              const element = Reflect.apply(
                nativeCreateElement, document, [definition.name]);
              Object.setPrototypeOf(element, definition.prototype);
              elementStates.set(element, {
                definition, state: 'custom', connected: false
              });
              return element;
            }
            Object.setPrototypeOf(WebSceneHTMLElement, NativeHTMLElement);
            let HTMLElementConstructor;
            HTMLElementConstructor = new Proxy(WebSceneHTMLElement, {
              construct(target, args, newTarget) {
                const current = constructionStack[constructionStack.length - 1];
                const registered = current && current.definition.constructor === newTarget
                  || constructorDefinitions.has(newTarget);
                if (newTarget === HTMLElementConstructor || !registered) {
                  throw new TypeError('Illegal constructor');
                }
                return Reflect.construct(target, args, newTarget);
              }
            });
            Object.defineProperty(HTMLElementConstructor, 'name', {
              value: 'HTMLElement', configurable: true
            });
            HTMLElementConstructor.prototype = NativeHTMLElement.prototype;
            Object.defineProperty(HTMLElementConstructor.prototype, 'constructor', {
              value: HTMLElementConstructor, writable: true, configurable: true
            });
            Object.defineProperty(globalThis, 'HTMLElement', {
              value: HTMLElementConstructor, writable: true, configurable: true
            });

            const upgradeElement = (element, forcedDefinition = undefined) => {
              const known = elementStates.get(element);
              if (known) return known.state === 'custom' ? element : undefined;
              const definition = forcedDefinition || definitions.get(elementName(element));
              if (!definition) return undefined;
              const state = { definition, state: 'failed', connected: false };
              elementStates.set(element, state);
              const construction = {
                element, definition, constructed: false
              };
              constructionStack.push(construction);
              try {
                Object.setPrototypeOf(element, definition.prototype);
                const result = Reflect.construct(
                  definition.constructor, [], definition.constructor);
                if (!construction.constructed || result !== element) {
                  throw new TypeError(
                    'Custom element constructor did not produce the element being upgraded');
                }
                state.state = 'custom';
              } catch (error) {
                Object.setPrototypeOf(element, NativeHTMLElement.prototype);
                reportReactionError(error);
                return undefined;
              } finally {
                constructionStack.pop();
              }
              if (definition.attributeChangedCallback) {
                for (const name of definition.observedAttributes) {
                  if (!element.hasAttribute(name)) continue;
                  invokeReaction(
                    element,
                    definition.attributeChangedCallback,
                    [name, null, element.getAttribute(name), null]);
                }
              }
              return element;
            };)JS",
            R"JS(
            const connectElement = element => {
              const upgraded = upgradeElement(element);
              const state = elementStates.get(element);
              if (!upgraded || !state || state.state !== 'custom'
                  || state.connected || !element.isConnected) return;
              state.connected = true;
              invokeReaction(
                element, state.definition.connectedCallback, []);
            };
            const disconnectElement = element => {
              const state = elementStates.get(element);
              if (!state || state.state !== 'custom' || !state.connected) return;
              state.connected = false;
              invokeReaction(
                element, state.definition.disconnectedCallback, []);
            };
            const notifySubtree = (root, phase) => {
              if (phase === 'disconnected') {
                walkElements(root, disconnectElement);
                return;
              }
              walkElements(root, element => {
                upgradeElement(element);
                if (element.isConnected) connectElement(element);
              });
            };
            const notifyAttribute = (
              element, name, oldValue, newValue, namespace = null) => {
              const state = elementStates.get(element);
              if (!state || state.state !== 'custom') return;
              const definition = state.definition;
              if (!definition.attributeChangedCallback
                  || !definition.observedAttributeSet.has(name)) return;
              invokeReaction(
                element,
                definition.attributeChangedCallback,
                [name, oldValue, newValue, namespace]);
            };

            function WebSceneCustomElementRegistry() {
              throw new TypeError('Illegal constructor');
            }
            Object.defineProperty(WebSceneCustomElementRegistry, 'name', {
              value: 'CustomElementRegistry', configurable: true
            });
            Object.defineProperty(
              WebSceneCustomElementRegistry.prototype,
              Symbol.toStringTag,
              { value: 'CustomElementRegistry', configurable: true });
            const customElementsRegistry = Object.create(
              WebSceneCustomElementRegistry.prototype);
            Object.defineProperties(WebSceneCustomElementRegistry.prototype, {
              define: { value(name, constructor) {
                const normalized = normalizeName(name);
                if (!isValidName(normalized)) throw syntaxError(normalized);
                if (typeof constructor !== 'function') {
                  throw new TypeError('Custom element constructor must be callable');
                }
                if (definitions.has(normalized)
                    || constructorDefinitions.has(constructor)) {
                  throw new DOMException(
                    'A custom element with this name or constructor is already defined',
                    'NotSupportedError');
                }
                const prototype = constructor.prototype;
                if (!prototype || typeof prototype !== 'object') {
                  throw new TypeError('Custom element constructor has no object prototype');
                }
                const callback = name => {
                  const value = prototype[name];
                  if (value !== undefined && value !== null
                      && typeof value !== 'function') {
                    throw new TypeError(`${name} must be callable`);
                  }
                  return value == null ? undefined : value;
                };
                const attributeChangedCallback = callback('attributeChangedCallback');
                const observedAttributes = attributeChangedCallback
                  ? Array.from(constructor.observedAttributes || [], String)
                  : [];
                const definition = {
                  name: normalized,
                  constructor,
                  prototype,
                  connectedCallback: callback('connectedCallback'),
                  disconnectedCallback: callback('disconnectedCallback'),
                  adoptedCallback: callback('adoptedCallback'),
                  attributeChangedCallback,
                  observedAttributes,
                  observedAttributeSet: new Set(observedAttributes)
                };
                definitions.set(normalized, definition);
                constructorDefinitions.set(constructor, definition);
                if (!registryActive) {
                  registryActive = true;
                  activateCustomElements();
                }
                for (const element of document.querySelectorAll(normalized)) {
                  upgradeElement(element, definition);
                  connectElement(element);
                }
                const pending = pendingDefinitions.get(normalized);
                if (pending) {
                  pending.resolve(constructor);
                  pendingDefinitions.delete(normalized);
                }
              }, writable: true, configurable: true },
              get: { value(name) {
                return definitions.get(normalizeName(name))?.constructor;
              }, writable: true, configurable: true },
              getName: { value(constructor) {
                if (typeof constructor !== 'function') {
                  throw new TypeError('Custom element constructor must be callable');
                }
                return constructorDefinitions.get(constructor)?.name ?? null;
              }, writable: true, configurable: true },
              whenDefined: { value(name) {
                const normalized = normalizeName(name);
                if (!isValidName(normalized)) return Promise.reject(syntaxError(normalized));
                const definition = definitions.get(normalized);
                if (definition) return Promise.resolve(definition.constructor);
                let pending = pendingDefinitions.get(normalized);
                if (!pending) {
                  let resolve;
                  const promise = new Promise(callback => { resolve = callback; });
                  pending = { promise, resolve };
                  pendingDefinitions.set(normalized, pending);
                }
                return pending.promise;
              }, writable: true, configurable: true },
              upgrade: { value(root) {
                if (!root || typeof root !== 'object') {
                  throw new TypeError('CustomElementRegistry.upgrade requires a Node');
                }
                walkElements(root, upgradeElement);
              }, writable: true, configurable: true }
            });

            Object.defineProperty(document, 'createElement', {
              value(...args) {
                if (args[1] != null && args[1].is !== undefined) {
                  throw new DOMException(
                    'Customized built-in elements are not supported',
                    'NotSupportedError');
                }
                const element = Reflect.apply(nativeCreateElement, this, args);
                if (registryActive) upgradeElement(element);
                return element;
              }, writable: true, configurable: true
            });
            Object.defineProperties(globalThis, {
              __webSceneCustomElementsNotifySubtree: {
                value: notifySubtree, configurable: true
              },
              __webSceneCustomElementsNotifyAttribute: {
                value: notifyAttribute, configurable: true
              }
            });
            Object.defineProperty(globalThis, 'customElements', {
              value: customElementsRegistry, configurable: true
            });
            Object.defineProperty(globalThis, 'CustomElementRegistry', {
              value: WebSceneCustomElementRegistry,
              writable: true,
              configurable: true
            });
            };

            Object.defineProperties(globalThis, {
              __webSceneInstallCustomElementsPlatform: {
                value: installCustomElementsPlatform, configurable: true
              },
              __webSceneInstallTreeWalkerPlatform: {
                value: installTreeWalkerPlatform, configurable: true
              },
              queueMicrotask: {
                value: enqueueMicrotask, writable: true, configurable: true
              },
              TextEncoder: {
                value: WebSceneTextEncoder, writable: true, configurable: true
              },
              TextDecoder: {
                value: WebSceneTextDecoder, writable: true, configurable: true
              },
              NodeFilter: {
                value: WebSceneNodeFilter, writable: true, configurable: true
              },
              TreeWalker: {
                value: WebSceneTreeWalker, writable: true, configurable: true
              }
            });
          })();
        )JS"};
        std::string source;
        for (const auto part : source_parts) source.append(part);
        auto script = v8::Script::Compile(
            local_context,
            js_string(isolate, source.c_str())).ToLocalChecked();
        script->Run(local_context).ToLocalChecked();
    }

    void install_custom_elements_platform(v8::Local<v8::Context> local_context)
    {
        auto global = local_context->Global();
        global->DefineOwnProperty(
            local_context,
            js_string(isolate, "__webSceneActivateCustomElements"),
            v8::Function::New(
                local_context,
                [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                    if (auto* self = current(info.GetIsolate()); self != nullptr) {
                        self->custom_elements_active = true;
                    }
                }).ToLocalChecked(),
            v8::PropertyAttribute::DontEnum).Check();
        v8::Local<v8::Value> raw_installer;
        if (!global->Get(
                local_context,
                js_string(isolate, "__webSceneInstallCustomElementsPlatform"))
                .ToLocal(&raw_installer)
            || !raw_installer->IsFunction()) {
            last_error = "The custom-elements bootstrap installer is unavailable.";
            return;
        }
        v8::TryCatch try_catch(isolate);
        static_cast<void>(raw_installer.As<v8::Function>()->Call(
            local_context,
            global,
            0,
            nullptr));
        global->Delete(
            local_context,
            js_string(isolate, "__webSceneActivateCustomElements")).Check();
        if (try_catch.HasCaught()) {
            last_error = "Custom-elements bootstrap failed: "
                + describe_reported_exception(try_catch, local_context);
        }
    }

    void install_tree_walker_platform(v8::Local<v8::Context> local_context)
    {
        auto global = local_context->Global();
        v8::Local<v8::Value> raw_installer;
        if (!global->Get(
                local_context,
                js_string(isolate, "__webSceneInstallTreeWalkerPlatform"))
                .ToLocal(&raw_installer)
            || !raw_installer->IsFunction()) {
            last_error = "The TreeWalker bootstrap installer is unavailable.";
            return;
        }
        v8::TryCatch try_catch(isolate);
        static_cast<void>(raw_installer.As<v8::Function>()->Call(
            local_context,
            global,
            0,
            nullptr));
        if (try_catch.HasCaught()) {
            last_error = "TreeWalker bootstrap failed: "
                + describe_reported_exception(try_catch, local_context);
        }
    }

    void install_globals(v8::Local<v8::Context> local_context)
    {
        auto global = local_context->Global();
        global->Set(local_context, js_string(isolate, "window"), global).Check();
        global->Set(local_context, js_string(isolate, "self"), global).Check();
        global->Set(
            local_context,
            js_string(isolate, "document"),
            document_object.Get(isolate)).Check();
        global->Set(
            local_context,
            js_string(isolate, "sessionStorage"),
            create_session_storage(local_context, outer_session_storage)).Check();
        global->Set(
            local_context,
            js_string(isolate, "localStorage"),
            create_session_storage(local_context, outer_local_storage)).Check();
        global->Set(
            local_context,
            js_string(isolate, "getComputedStyle"),
            v8::Function::New(local_context, get_computed_style).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "matchMedia"),
            v8::Function::New(local_context, match_media).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "open"),
            v8::Function::New(local_context, window_open).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "requestAnimationFrame"),
            v8::Function::New(local_context, request_animation_frame).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "cancelAnimationFrame"),
            v8::Function::New(local_context, clear_timeout).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "setTimeout"),
            v8::Function::New(local_context, set_timeout).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "clearTimeout"),
            v8::Function::New(local_context, clear_timeout).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "setInterval"),
            v8::Function::New(local_context, set_interval).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "clearInterval"),
            v8::Function::New(local_context, clear_timeout).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "addEventListener"),
            v8::Function::New(local_context, add_event_listener).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "removeEventListener"),
            v8::Function::New(local_context, remove_event_listener).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "dispatchEvent"),
            v8::Function::New(local_context, dispatch_event).ToLocalChecked()).Check();
        install_window_post_message(local_context, global);
        auto event_template = v8::FunctionTemplate::New(isolate, event_constructor);
        event_template->PrototypeTemplate()->Set(
            js_string(isolate, "preventDefault"),
            v8::FunctionTemplate::New(isolate, event_prevent_default));
        event_template->PrototypeTemplate()->Set(
            js_string(isolate, "initEvent"),
            v8::FunctionTemplate::New(isolate, event_init_event));
        event_template->PrototypeTemplate()->SetAccessorProperty(
            js_string(isolate, "returnValue"),
            v8::FunctionTemplate::New(isolate, event_return_value_get),
            v8::FunctionTemplate::New(isolate, event_return_value_set));
        event_template->PrototypeTemplate()->Set(
            js_string(isolate, "stopPropagation"),
            v8::FunctionTemplate::New(isolate, event_stop_propagation));
        event_template->PrototypeTemplate()->Set(
            js_string(isolate, "stopImmediatePropagation"),
            v8::FunctionTemplate::New(isolate, event_stop_immediate_propagation));
        event_template->PrototypeTemplate()->Set(
            js_string(isolate, "composedPath"),
            v8::FunctionTemplate::New(isolate, event_composed_path));
        global->Set(
            local_context,
            js_string(isolate, "Event"),
            event_template->GetFunction(local_context).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "CustomEvent"),
            event_template->GetFunction(local_context).ToLocalChecked()).Check();
        auto mouse_event_template = v8::FunctionTemplate::New(isolate, mouse_event_constructor);
        mouse_event_template->Inherit(event_template);
        global->Set(
            local_context,
            js_string(isolate, "MouseEvent"),
            mouse_event_template->GetFunction(local_context).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "KeyboardEvent"),
            event_template->GetFunction(local_context).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "PointerEvent"),
            mouse_event_template->GetFunction(local_context).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "WheelEvent"),
            mouse_event_template->GetFunction(local_context).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "FocusEvent"),
            event_template->GetFunction(local_context).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "InputEvent"),
            event_template->GetFunction(local_context).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "UIEvent"),
            event_template->GetFunction(local_context).ToLocalChecked()).Check();
        auto css = v8::Object::New(isolate);
        css->Set(
            local_context,
            js_string(isolate, "escape"),
            v8::Function::New(local_context, css_escape, {}, 1).ToLocalChecked()).Check();
        css->Set(
            local_context,
            js_string(isolate, "supports"),
            v8::Function::New(local_context, css_supports).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "CSS"), css).Check();
        auto mutation_observer_template = v8::FunctionTemplate::New(
            isolate,
            observer_constructor,
            js_string(isolate, "MutationObserver.constructor"));
        global->Set(
            local_context,
            js_string(isolate, "MutationObserver"),
            mutation_observer_template->GetFunction(local_context).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "ResizeObserver"),
            v8::FunctionTemplate::New(isolate, resize_observer_constructor)
                ->GetFunction(local_context).ToLocalChecked()).Check();
        auto dom_parser_template = v8::FunctionTemplate::New(isolate);
        dom_parser_template->PrototypeTemplate()->Set(
            js_string(isolate, "parseFromString"),
            v8::FunctionTemplate::New(isolate, dom_parser_parse));
        global->Set(
            local_context,
            js_string(isolate, "DOMParser"),
            dom_parser_template->GetFunction(local_context).ToLocalChecked()).Check();
        auto xml_serializer_template = v8::FunctionTemplate::New(isolate);
        xml_serializer_template->PrototypeTemplate()->Set(
            js_string(isolate, "serializeToString"),
            v8::FunctionTemplate::New(isolate, xml_serializer_serialize));
        global->Set(
            local_context,
            js_string(isolate, "XMLSerializer"),
            xml_serializer_template->GetFunction(local_context).ToLocalChecked()).Check();
#if defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
        install_generated_dom_constructors(local_context, global);
        global->Set(
            local_context,
            js_string(isolate, "AbortController"),
            v8::FunctionTemplate::New(isolate, abort_controller_constructor)
                ->GetFunction(local_context).ToLocalChecked()).Check();
#else
        auto element_constructor = element_template.Get(isolate)->GetFunction(local_context).ToLocalChecked();
        element_constructor->Set(
            local_context,
            js_string(isolate, "ELEMENT_NODE"),
            v8::Integer::New(isolate, 1)).Check();
        element_constructor->Set(
            local_context,
            js_string(isolate, "TEXT_NODE"),
            v8::Integer::New(isolate, 3)).Check();
        element_constructor->Set(
            local_context,
            js_string(isolate, "DOCUMENT_NODE"),
            v8::Integer::New(isolate, 9)).Check();
        element_constructor->Set(
            local_context,
            js_string(isolate, "DOCUMENT_FRAGMENT_NODE"),
            v8::Integer::New(isolate, 11)).Check();
        element_constructor->Set(
            local_context,
            js_string(isolate, "DOCUMENT_POSITION_DISCONNECTED"),
            v8::Integer::New(isolate, 1)).Check();
        element_constructor->Set(
            local_context,
            js_string(isolate, "DOCUMENT_POSITION_PRECEDING"),
            v8::Integer::New(isolate, 2)).Check();
        element_constructor->Set(
            local_context,
            js_string(isolate, "DOCUMENT_POSITION_FOLLOWING"),
            v8::Integer::New(isolate, 4)).Check();
        element_constructor->Set(
            local_context,
            js_string(isolate, "DOCUMENT_POSITION_CONTAINS"),
            v8::Integer::New(isolate, 8)).Check();
        element_constructor->Set(
            local_context,
            js_string(isolate, "DOCUMENT_POSITION_CONTAINED_BY"),
            v8::Integer::New(isolate, 16)).Check();
        element_constructor->Set(
            local_context,
            js_string(isolate, "DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC"),
            v8::Integer::New(isolate, 32)).Check();
        global->Set(local_context, js_string(isolate, "Node"), element_constructor).Check();
        global->Set(local_context, js_string(isolate, "Element"), element_constructor).Check();
        global->Set(local_context, js_string(isolate, "HTMLElement"), element_constructor).Check();
        global->Set(local_context, js_string(isolate, "HTMLButtonElement"), element_constructor).Check();
        global->Set(local_context, js_string(isolate, "HTMLCanvasElement"), element_constructor).Check();
        global->Set(local_context, js_string(isolate, "HTMLIFrameElement"), element_constructor).Check();
        global->Set(local_context, js_string(isolate, "HTMLImageElement"), element_constructor).Check();
        global->Set(local_context, js_string(isolate, "HTMLInputElement"), element_constructor).Check();
        global->Set(local_context, js_string(isolate, "HTMLParagraphElement"), element_constructor).Check();
        global->Set(local_context, js_string(isolate, "SVGElement"), element_constructor).Check();
        global->Set(local_context, js_string(isolate, "DocumentFragment"), element_constructor).Check();
        global->Set(local_context, js_string(isolate, "Window"), element_constructor).Check();
#endif
        global->SetLazyDataProperty(
            local_context,
            js_string(isolate, "HTMLCollection"),
            get_html_collection_constructor).Check();
        install_document_constructor(
            local_context,
            global,
            document_object.Get(isolate));
        global->Set(
            local_context,
            js_string(isolate, "getSelection"),
            v8::Function::New(local_context, get_selection).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "__webSceneCreateObjectUrl"),
            v8::Function::New(local_context, create_object_url).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "__webSceneResolveUrl"),
            v8::Function::New(local_context, resolve_url).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "__webSceneRecordWebApi"),
            v8::Function::New(local_context, record_web_api_use).ToLocalChecked()).Check();
        global->SetNativeDataProperty(
            local_context,
            js_string(isolate, "innerWidth"),
            get_inner_width).Check();
        global->SetNativeDataProperty(
            local_context,
            js_string(isolate, "innerHeight"),
            get_inner_height).Check();
        global->SetNativeDataProperty(
            local_context,
            js_string(isolate, "devicePixelRatio"),
            get_device_pixel_ratio).Check();
        global->SetNativeDataProperty(
            local_context,
            js_string(isolate, "scrollX"),
            get_window_scroll_x).Check();
        global->SetNativeDataProperty(
            local_context,
            js_string(isolate, "scrollY"),
            get_window_scroll_y).Check();
        global->SetNativeDataProperty(
            local_context,
            js_string(isolate, "pageXOffset"),
            get_window_scroll_x).Check();
        global->SetNativeDataProperty(
            local_context,
            js_string(isolate, "pageYOffset"),
            get_window_scroll_y).Check();
        install_screen(local_context, global);
        global->Set(local_context, js_string(isolate, "parent"), global).Check();
        global->Set(local_context, js_string(isolate, "top"), global).Check();
        global->Set(local_context, js_string(isolate, "opener"), v8::Null(isolate)).Check();
        global->Set(local_context, js_string(isolate, "name"), js_string(isolate, "")).Check();

        auto performance = v8::Object::New(isolate);
        performance->Set(
            local_context,
            js_string(isolate, "now"),
            v8::Function::New(local_context, performance_now).ToLocalChecked()).Check();
        performance->Set(
            local_context,
            js_string(isolate, "getEntriesByName"),
            v8::Function::New(local_context, performance_get_entries_by_name).ToLocalChecked()).Check();
        performance->Set(local_context, js_string(isolate, "mark"),
            v8::Function::New(local_context, performance_entry).ToLocalChecked()).Check();
        performance->Set(local_context, js_string(isolate, "measure"),
            v8::Function::New(local_context, performance_entry).ToLocalChecked()).Check();
        performance->Set(local_context, js_string(isolate, "clearMarks"),
            v8::Function::New(local_context, console_log).ToLocalChecked()).Check();
        performance->Set(local_context, js_string(isolate, "clearMeasures"),
            v8::Function::New(local_context, console_log).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "performance"), performance).Check();
        global->Set(
            local_context,
            js_string(isolate, "__webSceneCreateObjectUrl"),
            v8::Function::New(local_context, create_object_url).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "Image"),
            v8::FunctionTemplate::New(isolate, image_constructor)
                ->GetFunction(local_context).ToLocalChecked()).Check();
        auto path_template = v8::FunctionTemplate::New(isolate, path_2d_constructor);
        global->Set(
            local_context,
            js_string(isolate, "Path2D"),
            path_template->GetFunction(local_context).ToLocalChecked()).Check();
        auto matrix_template = v8::FunctionTemplate::New(isolate, dom_matrix_constructor);
        global->Set(
            local_context,
            js_string(isolate, "DOMMatrix"),
            matrix_template->GetFunction(local_context).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "DOMMatrixReadOnly"),
            matrix_template->GetFunction(local_context).ToLocalChecked()).Check();

        auto location = v8::Object::New(isolate);
        location->Set(
            local_context,
            js_string(isolate, "href"),
            js_string(isolate, "http://127.0.0.1/")).Check();
        location->Set(local_context, js_string(isolate, "protocol"), js_string(isolate, "http:")).Check();
        location->Set(local_context, js_string(isolate, "pathname"), js_string(isolate, "/")).Check();
        location->Set(local_context, js_string(isolate, "search"), js_string(isolate, "")).Check();
        location->Set(local_context, js_string(isolate, "hash"), js_string(isolate, "")).Check();
        location->Set(
            local_context,
            js_string(isolate, "toString"),
            v8::Function::New(local_context, location_to_string).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "location"), location).Check();

        install_navigator(isolate, local_context, global);

        install_console(local_context, global);
        install_host_bridge(local_context);

        constexpr std::string_view crypto_source = R"JS(
            class WebSceneBlob {
              constructor(parts = [], options = {}) {
                __webSceneRecordWebApi(
                  'Blob.constructor', 'partially-supported',
                  'byte-preserving construction, type, size, object URLs, and downloads without slicing or streaming');
                const chunks = [];
                let size = 0;
                for (const part of parts) {
                  let bytes;
                  if (part instanceof ArrayBuffer) {
                    bytes = new Uint8Array(part);
                  } else if (ArrayBuffer.isView(part)) {
                    bytes = new Uint8Array(part.buffer, part.byteOffset, part.byteLength);
                  } else {
                    bytes = new TextEncoder().encode(String(part));
                  }
                  chunks.push(bytes);
                  size += bytes.byteLength;
                }
                this._bytes = new Uint8Array(size);
                let offset = 0;
                for (const chunk of chunks) {
                  this._bytes.set(chunk, offset);
                  offset += chunk.byteLength;
                }
                this.size = size;
                this.type = String(options.type || '').toLowerCase();
                this._text = Array.from(parts, String).join('');
              }
              toString() { return this._text; }
            }
            class WebSceneURLSearchParams {
              constructor(init = null) {
                this._owner = init && typeof init === 'object'
                  && 'href' in init ? init : null;
                this._pairs = [];
                if (!this._owner && init !== null && typeof init === 'object') {
                  if (typeof init[Symbol.iterator] === 'function') {
                    for (const entry of init) {
                      const pair = Array.from(entry);
                      if (pair.length !== 2) {
                        throw new TypeError(
                          'URLSearchParams sequence entries must contain exactly two items');
                      }
                      this._pairs.push([String(pair[0]), String(pair[1])]);
                    }
                  } else {
                    for (const name of Object.keys(init)) {
                      this._pairs.push([String(name), String(init[name])]);
                    }
                  }
                  return;
                }
                const query = String(this._owner ? this._owner.search : init ?? '')
                  .replace(/^\?/, '');
                for (const item of query ? query.split('&') : []) {
                  const separator = item.indexOf('=');
                  const decode = value => {
                    value = String(value).replace(/\+/g, ' ');
                    try { return decodeURIComponent(value); }
                    catch { return value; }
                  };
                  this._pairs.push(separator < 0
                    ? [decode(item), '']
                    : [decode(item.slice(0, separator)),
                      decode(item.slice(separator + 1))]);
                }
              }
              _updateOwner() {
                if (!this._owner) return;
                const serialized = this.toString();
                this._owner.search = serialized ? `?${serialized}` : '';
                this._owner.href = this._owner.origin + this._owner.pathname
                  + this._owner.search + this._owner.hash;
              }
              append(name, value) {
                __webSceneRecordWebApi(
                  'URLSearchParams.append', 'partially-supported',
                  'ordered query parsing and common mutation/query operations');
                this._pairs.push([String(name), String(value)]);
                this._updateOwner();
              }
              get(name) {
                name = String(name);
                const pair = this._pairs.find(entry => entry[0] === name);
                return pair ? pair[1] : null;
              }
              getAll(name) {
                name = String(name);
                return this._pairs
                  .filter(entry => entry[0] === name)
                  .map(entry => entry[1]);
              }
              has(name) {
                name = String(name);
                return this._pairs.some(entry => entry[0] === name);
              }
              set(name, value) {
                name = String(name);
                value = String(value);
                const first = this._pairs.findIndex(entry => entry[0] === name);
                this._pairs = this._pairs.filter(entry => entry[0] !== name);
                this._pairs.splice(first < 0 ? this._pairs.length : first, 0, [name, value]);
                this._updateOwner();
              }
              delete(name) {
                name = String(name);
                this._pairs = this._pairs.filter(entry => entry[0] !== name);
                this._updateOwner();
              }
              sort() {
                this._pairs = this._pairs
                  .map((entry, index) => ({ entry, index }))
                  .sort((left, right) => left.entry[0] < right.entry[0] ? -1
                    : left.entry[0] > right.entry[0] ? 1
                    : left.index - right.index)
                  .map(item => item.entry);
                this._updateOwner();
              }
              entries() { return this._pairs.map(entry => [...entry])[Symbol.iterator](); }
              keys() { return this._pairs.map(entry => entry[0])[Symbol.iterator](); }
              values() { return this._pairs.map(entry => entry[1])[Symbol.iterator](); }
              forEach(callback, thisArg = undefined) {
                for (const [name, value] of this._pairs) {
                  callback.call(thisArg, value, name, this);
                }
              }
              [Symbol.iterator]() { return this.entries(); }
              get size() { return this._pairs.length; }
              toString() {
                const encode = value => encodeURIComponent(String(value))
                  .replace(/%20/g, '+')
                  .replace(/[!'()~]/g, character =>
                    `%${character.charCodeAt(0).toString(16).toUpperCase()}`);
                return this._pairs.map(([name, value]) =>
                  `${encode(name)}=${encode(value)}`).join('&');
              }
            }
            class WebSceneFormData {
              constructor(form = undefined) {
                __webSceneRecordWebApi(
                  'FormData.constructor', 'partially-supported',
                  'ordered string and Blob fields with multipart fetch serialization');
                this._entries = [];
                if (form !== undefined && form !== null) {
                  throw new TypeError('Constructing FormData from a form is not yet supported');
                }
              }
              append(name, value, filename = undefined) {
                this._entries.push([
                  String(name), value instanceof Blob ? value : String(value),
                  filename === undefined ? undefined : String(filename)
                ]);
              }
              delete(name) {
                name = String(name);
                this._entries = this._entries.filter(entry => entry[0] !== name);
              }
              get(name) {
                name = String(name);
                const entry = this._entries.find(item => item[0] === name);
                return entry ? entry[1] : null;
              }
              getAll(name) {
                name = String(name);
                return this._entries.filter(item => item[0] === name).map(item => item[1]);
              }
              has(name) {
                name = String(name);
                return this._entries.some(item => item[0] === name);
              }
              set(name, value, filename = undefined) {
                name = String(name);
                const index = this._entries.findIndex(item => item[0] === name);
                this.delete(name);
                const entry = [
                  name, value instanceof Blob ? value : String(value),
                  filename === undefined ? undefined : String(filename)
                ];
                this._entries.splice(index < 0 ? this._entries.length : index, 0, entry);
              }
              entries() { return this._entries.map(entry => [entry[0], entry[1]])[Symbol.iterator](); }
              keys() { return this._entries.map(entry => entry[0])[Symbol.iterator](); }
              values() { return this._entries.map(entry => entry[1])[Symbol.iterator](); }
              forEach(callback, thisArg = undefined) {
                for (const [name, value] of this._entries) {
                  callback.call(thisArg, value, name, this);
                }
              }
              [Symbol.iterator]() { return this.entries(); }
              _encode(boundary) {
                const escape = value => String(value)
                  .replace(/\r|\n/g, ' ')
                  .replace(/"/g, '%22');
                let result = '';
                for (const [name, value, filename] of this._entries) {
                  result += `--${boundary}\r\nContent-Disposition: form-data; name="${escape(name)}"`;
                  if (value instanceof Blob) {
                    result += `; filename="${escape(filename ?? 'blob')}"\r\n`;
                    if (value.type) result += `Content-Type: ${value.type}\r\n`;
                    result += `\r\n${value.toString()}\r\n`;
                  } else {
                    result += `\r\n\r\n${value}\r\n`;
                  }
                }
                return `${result}--${boundary}--\r\n`;
              }
            }
            class WebSceneURL {
              constructor(value, base = globalThis.location?.href || '') {
                __webSceneRecordWebApi(
                  'URL.constructor', 'partially-supported',
                  'hierarchical URL resolution and common components without the complete URL parser');
                this.href = __webSceneResolveUrl(String(value), String(base));
                const match = /^(?:([a-z][a-z0-9+.-]*:))?(?:\/\/([^/?#]*))?([^?#]*)(\?[^#]*)?(#.*)?$/i.exec(this.href) || [];
                this.protocol = match[1] || '';
                this.host = match[2] || '';
                const separator = this.host.lastIndexOf(':');
                this.hostname = separator > 0 ? this.host.slice(0, separator) : this.host;
                this.port = separator > 0 ? this.host.slice(separator + 1) : '';
                this.pathname = match[3] || '';
                this.search = match[4] || '';
                this.hash = match[5] || '';
                this.origin = this.protocol && this.host ? `${this.protocol}//${this.host}` : 'null';
                this.searchParams = new WebSceneURLSearchParams(this);
              }
              toString() {
                const search = this.search && !this.search.startsWith('?')
                  ? `?${this.search}` : this.search;
                const hash = this.hash && !this.hash.startsWith('#')
                  ? `#${this.hash}` : this.hash;
                return `${this.protocol}${this.host ? `//${this.host}` : ''}`
                  + `${this.pathname}${search}${hash}`;
              }
              toJSON() { return this.toString(); }
              static createObjectURL(blob) { return __webSceneCreateObjectUrl(blob); }
              static revokeObjectURL() {}
            }
            class WebSceneDOMException extends Error {
              constructor(message = '', name = 'Error') {
                super(String(message));
                this.name = String(name);
                const legacyCodes = {
                  IndexSizeError: 1,
                  HierarchyRequestError: 3,
                  WrongDocumentError: 4,
                  InvalidCharacterError: 5,
                  NoModificationAllowedError: 7,
                  NotFoundError: 8,
                  NotSupportedError: 9,
                  InUseAttributeError: 10,
                  InvalidStateError: 11,
                  SyntaxError: 12,
                  InvalidModificationError: 13,
                  NamespaceError: 14,
                  InvalidAccessError: 15,
                  TypeMismatchError: 17,
                  SecurityError: 18,
                  NetworkError: 19,
                  AbortError: 20,
                  URLMismatchError: 21,
                  QuotaExceededError: 22,
                  TimeoutError: 23,
                  InvalidNodeTypeError: 24,
                  DataCloneError: 25
                };
                this.code = legacyCodes[this.name] || 0;
              }
            }
            Object.defineProperties(globalThis, {
              Blob: { value: WebSceneBlob, configurable: true },
              URL: { value: WebSceneURL, configurable: true },
              URLSearchParams: { value: WebSceneURLSearchParams, configurable: true },
              FormData: { value: WebSceneFormData, configurable: true },
              DOMException: { value: WebSceneDOMException, configurable: true },
              crypto: { value: {
                getRandomValues(array) {
                  __webSceneRecordWebApi(
                    'Crypto.getRandomValues', 'partially-supported',
                    'integer TypedArray filling without a cryptographic entropy guarantee');
                  if (!ArrayBuffer.isView(array) || array instanceof DataView) {
                    throw new TypeError('Expected an integer TypedArray');
                  }
                  for (let index = 0; index < array.length; index++) {
                    array[index] = Math.floor(Math.random() * 256);
                  }
                  return array;
                }
              }, configurable: true }
            });
        )JS";
        auto crypto_script = v8::Script::Compile(
            local_context,
            js_string(isolate, std::string(crypto_source).c_str())).ToLocalChecked();
        crypto_script->Run(local_context).ToLocalChecked();
        install_clipboard_api(local_context);
        install_websocket_globals(local_context);
        install_editor_web_platform_globals(local_context);
        install_tree_walker_platform(local_context);
        install_custom_elements_platform(local_context);
        install_fetch_globals(local_context);
        install_intersection_observer_polyfill(local_context);
    }

    void install_fetch_globals(v8::Local<v8::Context> local_context)
    {
        auto global = local_context->Global();
        global->Set(
            local_context,
            js_string(isolate, "__webSceneFetchText"),
            v8::Function::New(local_context, fetch_text).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "__webSceneFetchResource"),
            v8::Function::New(local_context, fetch_resource).ToLocalChecked()).Check();
        if constexpr (bootstrap_snapshot_enabled) return;
        constexpr std::string_view source = R"JS(
          (() => {
            class WebSceneHeaders {
              constructor(initial = undefined) {
                this._values = new Map();
                if (initial && typeof initial === 'object') {
                  const entries = typeof initial[Symbol.iterator] === 'function'
                    ? initial : Object.entries(initial);
                  for (const [name, value] of entries) this.set(name, value);
                }
              }
              append(name, value) {
                const key = String(name).toLowerCase();
                const text = String(value);
                this._values.set(
                  key,
                  this._values.has(key)
                    ? `${this._values.get(key)}, ${text}`
                    : text);
              }
              set(name, value) {
                this._values.set(String(name).toLowerCase(), String(value));
              }
              get(name) {
                return this._values.get(String(name).toLowerCase()) ?? null;
              }
              has(name) {
                return this._values.has(String(name).toLowerCase());
              }
              delete(name) {
                this._values.delete(String(name).toLowerCase());
              }
              entries() { return this._values.entries(); }
              keys() { return this._values.keys(); }
              values() { return this._values.values(); }
              forEach(callback, thisArg = undefined) {
                this._values.forEach(
                  (value, name) => callback.call(thisArg, value, name, this));
              }
              [Symbol.iterator]() { return this.entries(); }
            }

            class WebSceneResponse {
              constructor(body = '', options = {}) {
                this._body = String(body ?? '');
                this.bodyUsed = false;
                this.status = Number(options.status ?? 200);
                this.statusText = String(options.statusText ?? 'OK');
                this.url = String(options.url ?? '');
                this.redirected = false;
                this.type = 'basic';
                this.headers = new WebSceneHeaders(options.headers);
              }
              get ok() { return this.status >= 200 && this.status < 300; }
              text() {
                if (this.bodyUsed) {
                  return Promise.reject(new TypeError('Response body already used'));
                }
                this.bodyUsed = true;
                return Promise.resolve(this._body);
              }
              json() {
                return this.text().then(value => JSON.parse(value));
              }
              clone() {
                if (this.bodyUsed) throw new TypeError('Response body already used');
                return new WebSceneResponse(this._body, {
                  status: this.status,
                  statusText: this.statusText,
                  url: this.url,
                  headers: this.headers
                });
              }
            }

            class WebSceneRequest {
              constructor(input, options = {}) {
                const rawUrl = String(
                  input && typeof input === 'object' && 'url' in input
                    ? input.url : input);
                this.url = globalThis.__webSceneDocumentBasePath
                    && !rawUrl.startsWith('/')
                    && !/^[a-z][a-z0-9+.-]*:/i.test(rawUrl)
                  ? globalThis.__webSceneDocumentBasePath + rawUrl
                  : rawUrl;
                this.method = String(options.method ?? input?.method ?? 'GET')
                  .toUpperCase();
                this.headers = new WebSceneHeaders(
                  options.headers ?? input?.headers);
                this.body = options.body ?? input?.body ?? null;
              }
            }

            function webSceneFetch(input, options = {}) {
              const request = new WebSceneRequest(input, options);
              if ((request.method === 'GET' || request.method === 'HEAD')
                  && request.body !== null) {
                return Promise.reject(new TypeError(
                  'Request with GET/HEAD method cannot have body'));
              }
              try {
                    let body = '';
                    if (request.body instanceof FormData) {
                      const boundary = `----WebSceneFormBoundary${Math.floor(
                        Math.random() * Number.MAX_SAFE_INTEGER).toString(16)}`;
                      body = request.body._encode(boundary);
                      if (!request.headers.has('content-type')) {
                        request.headers.set(
                          'content-type',
                          `multipart/form-data; boundary=${boundary}`);
                      }
                    } else if (request.body instanceof URLSearchParams) {
                      body = request.body.toString();
                      if (!request.headers.has('content-type')) {
                        request.headers.set(
                          'content-type',
                          'application/x-www-form-urlencoded;charset=UTF-8');
                      }
                    } else if (request.body instanceof Blob) {
                      body = request.body.toString();
                      if (request.body.type && !request.headers.has('content-type')) {
                        request.headers.set('content-type', request.body.type);
                      }
                    } else if (request.body !== null) {
                      body = String(request.body);
                      if (!request.headers.has('content-type')) {
                        request.headers.set('content-type', 'text/plain;charset=UTF-8');
                      }
                    }
                    if (request.url.startsWith('data:')) {
                      const separator = request.url.indexOf(',');
                      if (separator < 0) throw new TypeError('Malformed data URL');
                      const metadata = request.url.slice(5, separator);
                      const payload = request.url.slice(separator + 1);
                      if (/;base64(?:;|$)/i.test(metadata)) {
                        throw new TypeError('Base64 data fetch is not yet supported');
                      }
                      const result = {
                        body: decodeURIComponent(payload),
                        url: request.url
                      };
                      return Promise.resolve(new WebSceneResponse(
                        result.body,
                        { status: 200, url: result.url }));
                    } else {
                      return __webSceneFetchResource(
                        request.url,
                        request.method,
                        body,
                        request.headers.get('content-type') ?? '')
                        .then(result => new WebSceneResponse(
                          result.body,
                          { status: 200, url: result.url }));
                    }
              } catch (error) {
                return Promise.reject(error);
              }
            }

            class WebSceneXMLHttpRequest {
              constructor() {
                this.readyState = 0;
                this.status = 0;
                this.statusText = '';
                this.responseText = '';
                this.responseXML = null;
                this.response = null;
                this.onload = null;
                this.onerror = null;
                this.onreadystatechange = null;
                this._method = 'GET';
                this._url = '';
                this._mimeType = '';
              }
              open(method, url) {
                this._method = String(method).toUpperCase();
                this._url = String(url);
                this.readyState = 1;
                this.onreadystatechange?.(new Event('readystatechange'));
              }
              overrideMimeType(value) {
                this._mimeType = String(value);
              }
              send() {
                if (this._method !== 'GET') {
                  throw new TypeError(`WebScene XMLHttpRequest does not support ${this._method}`);
                }
                queueMicrotask(() => {
                  try {
                    const requestUrl = globalThis.__webSceneDocumentBasePath
                        && !this._url.startsWith('/')
                        && !/^[a-z][a-z0-9+.-]*:/i.test(this._url)
                      ? globalThis.__webSceneDocumentBasePath + this._url
                      : this._url;
                    const body = __webSceneFetchText(requestUrl);
                    this.responseText = body;
                    this.response = body;
                    if (this._mimeType.includes('xml')) {
                      this.responseXML = new DOMParser().parseFromString(body, 'text/xml');
                    }
                    this.status = 200;
                    this.statusText = 'OK';
                    this.readyState = 4;
                    this.onreadystatechange?.(new Event('readystatechange'));
                    this.onload?.(new Event('load'));
                  } catch (error) {
                    this.status = 0;
                    this.readyState = 4;
                    this.onreadystatechange?.(new Event('readystatechange'));
                    this.onerror?.(new Event('error'));
                  }
                });
              }
            }

            Object.defineProperties(globalThis, {
              Headers: {
                value: WebSceneHeaders, writable: true, configurable: true
              },
              Request: {
                value: WebSceneRequest, writable: true, configurable: true
              },
              Response: {
                value: WebSceneResponse, writable: true, configurable: true
              },
              fetch: {
                value: webSceneFetch, writable: true, configurable: true
              },
              XMLHttpRequest: {
                value: WebSceneXMLHttpRequest, writable: true, configurable: true
              }
            });
          })();
        )JS";
        auto script = v8::Script::Compile(
            local_context,
            js_string(isolate, std::string(source).c_str())).ToLocalChecked();
        script->Run(local_context).ToLocalChecked();
    }

    void install_window_post_message(
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Object> global)
    {
        global->Set(
            local_context,
            js_string(isolate, "postMessage"),
            v8::Function::New(
                local_context,
                window_post_message).ToLocalChecked()).Check();
        global->SetNativeDataProperty(
            local_context,
            js_string(isolate, "frames"),
            get_window_frames).Check();
        global->Set(
            local_context,
            js_string(isolate, "focus"),
            v8::Function::New(local_context, window_focus).ToLocalChecked()).Check();
        auto scroll_to = v8::Function::New(
            local_context,
            window_scroll_to).ToLocalChecked();
        global->Set(
            local_context,
            js_string(isolate, "scrollTo"),
            scroll_to).Check();
        global->Set(
            local_context,
            js_string(isolate, "scroll"),
            scroll_to).Check();
    }

    void install_host_bridge(v8::Local<v8::Context> local_context)
    {
        auto bridge = v8::Object::New(isolate);
        const std::array<std::pair<const char*, int32_t>, 3> methods{{
            {"GetBars", 1},
            {"SubscribeBars", 2},
            {"UnsubscribeBars", 3}}};
        for (const auto& [name, kind] : methods) {
            bridge->Set(
                local_context,
                js_string(isolate, name),
                v8::Function::New(
                    local_context,
                    host_bridge_call,
                    v8::Integer::New(isolate, kind)).ToLocalChecked()).Check();
        }
        local_context->Global()->Set(
            local_context,
            js_string(isolate, "dotnetBridge"),
            bridge).Check();
    }

    void install_clipboard_api(v8::Local<v8::Context> local_context)
    {
        auto global = local_context->Global();
        global->Set(
            local_context,
            js_string(isolate, "__webSceneWriteClipboard"),
            v8::Function::New(
                local_context,
                write_clipboard).ToLocalChecked()).Check();
        constexpr std::string_view source = R"JS(
          (() => {
            class WebSceneClipboardItem {
              constructor(items, options = {}) {
                if (items === null || typeof items !== 'object') {
                  throw new TypeError('ClipboardItem data must be an object');
                }
                this._items = Object.assign(Object.create(null), items);
                this.types = Object.keys(items);
                this.presentationStyle = String(options.presentationStyle || 'unspecified');
              }
              getType(type) {
                type = String(type);
                if (!Object.prototype.hasOwnProperty.call(this._items, type)) {
                  return Promise.reject(new DOMException(
                    `Clipboard item does not contain ${type}`, 'NotFoundError'));
                }
                return Promise.resolve(this._items[type]).then(value => {
                  if (value instanceof Blob || value?._canvasNodeId !== undefined) {
                    return value;
                  }
                  return new Blob([value], { type });
                });
              }
              static supports(type) {
                return ['image/png', 'text/plain', 'text/html'].includes(String(type));
              }
            }
            const clipboard = {
              async write(items) {
                __webSceneRecordWebApi(
                  'Clipboard.write', 'partially-supported',
                  'typed image/text handoff to the desktop host without clipboard reads');
                if (!Array.isArray(items) || items.length === 0) {
                  throw new TypeError('Clipboard.write requires at least one item');
                }
                const item = items[0];
                if (!(item instanceof WebSceneClipboardItem)) {
                  throw new TypeError('Clipboard.write requires ClipboardItem values');
                }
                for (const type of item.types) {
                  const blob = await item.getType(type);
                  if (!__webSceneWriteClipboard(type, blob)) {
                    throw new DOMException('The host rejected the clipboard write', 'NotAllowedError');
                  }
                }
              }
            };
            Object.defineProperty(globalThis, 'ClipboardItem', {
              value: WebSceneClipboardItem, configurable: true
            });
            Object.defineProperty(navigator, 'clipboard', {
              value: clipboard, enumerable: true, configurable: true
            });
          })();
        )JS";
        auto script = v8::Script::Compile(
            local_context,
            js_string(isolate, std::string(source).c_str())).ToLocalChecked();
        script->Run(local_context).ToLocalChecked();
    }

    bool try_take_host_request(std::string& request)
    {
        std::lock_guard lock(host_request_mutex);
        if (host_requests.empty()) return false;
        request = std::move(host_requests.front());
        host_requests.pop_front();
        return true;
    }

    uint32_t current_cursor_kind() const noexcept
    {
        return current_cursor_kind_value;
    }

    bool try_take_console_message(std::string& message)
    {
        std::lock_guard lock(console_message_mutex);
        if (console_messages.empty()) return false;
        message = std::move(console_messages.front());
        console_messages.pop_front();
        return true;
    }

    bool enqueue_host_request(
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Object> request)
    {
        v8::Local<v8::String> json;
        if (!v8::JSON::Stringify(local_context, request).ToLocal(&json)) return false;
        {
            std::lock_guard lock(host_request_mutex);
            constexpr size_t maximum_host_requests = 1024U;
            if (host_requests.size() >= maximum_host_requests) return false;
            host_requests.push_back(to_utf8(isolate, json));
        }
        // Notify after releasing the queue lock: the host may immediately call
        // back into try_take_host_request from another thread.
        if (host_request_available) host_request_available();
        return true;
    }

    bool queue_external_navigation(dom_node& target)
    {
        auto* anchor = &target;
        while (anchor != nullptr && anchor->tag != "a") anchor = anchor->parent;
        if (anchor == nullptr) return true;
        v8::Context::Scope navigation_context_scope(context_for_node(*anchor));
        const auto authored = anchor->attributes.find("href");
        if (authored == anchor->attributes.end() || authored->second.empty()) return true;
        if (anchor->attributes.contains("download")) {
            auto local_context = frame_context.IsEmpty()
                ? context.Get(isolate)
                : frame_context.Get(isolate);
            auto request = v8::Object::New(isolate);
            request->Set(
                local_context,
                js_string(isolate, "kind"),
                js_string(isolate, "download")).Check();
            const auto file_name = anchor->attributes.at("download").empty()
                ? std::string{"download"}
                : anchor->attributes.at("download");
            request->Set(
                local_context,
                js_string(isolate, "suggestedFileName"),
                js_string(isolate, file_name.c_str())).Check();
            constexpr std::string_view canvas_snapshot_prefix =
                "webscene-canvas-snapshot:";
            if (authored->second.starts_with(canvas_snapshot_prefix)) {
                uint32_t canvas_node_id = 0;
                const auto suffix = std::string_view(authored->second).substr(
                    canvas_snapshot_prefix.size());
                const auto parsed = std::from_chars(
                    suffix.data(), suffix.data() + suffix.size(), canvas_node_id);
                if (parsed.ec != std::errc{} || parsed.ptr != suffix.data() + suffix.size()) {
                    return false;
                }
                request->Set(
                    local_context,
                    js_string(isolate, "canvasNodeId"),
                    v8::Integer::NewFromUnsigned(isolate, canvas_node_id)).Check();
                record_feature(
                    "canvas",
                    "HTMLCanvasElement.toDataURL",
                    "partially-supported",
                    "opaque canvas snapshot handoff to the desktop host",
                    "native-binding");
                return enqueue_host_request(local_context, request);
            }
            const auto object_url_canvas =
                object_url_canvas_node_ids.find(authored->second);
            if (object_url_canvas != object_url_canvas_node_ids.end()) {
                request->Set(
                    local_context,
                    js_string(isolate, "canvasNodeId"),
                    v8::Integer::NewFromUnsigned(
                        isolate, object_url_canvas->second)).Check();
                record_feature(
                    "canvas",
                    "HTMLCanvasElement.toBlob",
                    "partially-supported",
                    "canvas-backed object URL handoff to the desktop host",
                    "default-action");
                return enqueue_host_request(local_context, request);
            }
            const auto download_payload =
                object_url_download_payloads.find(authored->second);
            const auto object_url = object_urls.find(authored->second);
            const auto& download_url =
                download_payload != object_url_download_payloads.end()
                    ? download_payload->second
                    : object_url == object_urls.end()
                        ? authored->second
                        : object_url->second;
            request->Set(
                local_context,
                js_string(isolate, "url"),
                js_string(isolate, download_url.c_str())).Check();
            record_feature(
                "html",
                "anchor-download",
                "supported",
                "download activation emits a typed host save request",
                "default-action");
            return enqueue_host_request(local_context, request);
        }
        return queue_external_url(authored->second);
    }

    bool queue_external_url(const std::string& authored)
    {
        const auto& base = current_base_address();
        const auto resolved = resolve_resource_url(authored, base);
        const auto lower = lower_html_name(resolved);
        if (!lower.starts_with("https://") && !lower.starts_with("http://")) return true;

        auto local_context = isolate->GetCurrentContext();
        auto request = v8::Object::New(isolate);
        request->Set(
            local_context,
            js_string(isolate, "kind"),
            js_string(isolate, "openExternalUrl")).Check();
        request->Set(
            local_context,
            js_string(isolate, "url"),
            js_string(isolate, resolved.c_str())).Check();
        request->Set(
            local_context,
            js_string(isolate, "disposition"),
            js_string(isolate, "systemDefaultBrowser")).Check();
        record_feature(
            "html",
            "anchor-external-navigation",
            "supported",
            "http(s) activation emits a host request without replacing the WebScene document",
            "default-action");
        return enqueue_host_request(local_context, request);
    }

    static void window_open(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto local_context = info.GetIsolate()->GetCurrentContext();
        if (info.Length() < 1 || !self->queue_external_url(to_utf8(info.GetIsolate(), info[0]))) {
            info.GetIsolate()->ThrowException(v8::Exception::Error(
                js_string(info.GetIsolate(), "WebScene rejected the external window URL")));
            return;
        }
        self->record_feature(
            "web-api",
            "Window.open",
            "partially-supported",
            "HTTP(S) system-browser handoff without an embedded browsing context",
            "web-api-binding");
        // Component libraries commonly clear opener on the returned WindowProxy.
        // Return a small writable proxy-shaped object while the real navigation
        // is deliberately owned by the desktop host.
        auto proxy = v8::Object::New(info.GetIsolate());
        proxy->Set(local_context, js_string(info.GetIsolate(), "opener"), v8::Null(info.GetIsolate())).Check();
        proxy->Set(local_context, js_string(info.GetIsolate(), "closed"), v8::False(info.GetIsolate())).Check();
        info.GetReturnValue().Set(proxy);
    }

    static void record_web_api_use(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (info.Length() < 2) return;
        auto feature = to_utf8(info.GetIsolate(), info[0]);
        auto classification = to_utf8(info.GetIsolate(), info[1]);
        if (feature.empty()
            || (classification != "supported"
                && classification != "partially-supported"
                && classification != "unsupported"
                && classification != "invalid-authoring"
                && classification != "unobserved-code-path")) {
            return;
        }
        auto semantic_slice = info.Length() > 2
            ? to_utf8(info.GetIsolate(), info[2])
            : std::string{};
        current(info.GetIsolate())->record_feature(
            "web-api",
            std::move(feature),
            std::move(classification),
            std::move(semantic_slice),
            "web-api-polyfill");
        info.GetReturnValue().Set(v8::True(info.GetIsolate()));
    }

    static void host_bridge_call(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto local_context = info.GetIsolate()->GetCurrentContext();
        const auto kind = info.Data()->IsInt32()
            ? info.Data().As<v8::Int32>()->Value()
            : 0;
        auto request = v8::Object::New(info.GetIsolate());
        const auto set_string = [&](const char* name, int argument) {
            request->Set(
                local_context,
                js_string(info.GetIsolate(), name),
                argument < info.Length()
                    ? js_string(info.GetIsolate(), to_utf8(info.GetIsolate(), info[argument]).c_str())
                    : js_string(info.GetIsolate(), "")).Check();
        };
        request->Set(
            local_context,
            js_string(info.GetIsolate(), "requestId"),
            v8::Number::New(
                info.GetIsolate(),
                static_cast<double>(++self->next_host_request_id))).Check();
        if (kind == 1) {
            request->Set(local_context, js_string(info.GetIsolate(), "kind"), js_string(info.GetIsolate(), "getBars")).Check();
            set_string("symbol", 0);
            set_string("resolution", 1);
            request->Set(
                local_context,
                js_string(info.GetIsolate(), "from"),
                info.Length() > 2 ? info[2] : v8::Undefined(info.GetIsolate())).Check();
            request->Set(
                local_context,
                js_string(info.GetIsolate(), "to"),
                info.Length() > 3 ? info[3] : v8::Undefined(info.GetIsolate())).Check();
        } else if (kind == 2) {
            request->Set(local_context, js_string(info.GetIsolate(), "kind"), js_string(info.GetIsolate(), "subscribeBars")).Check();
            set_string("symbol", 0);
            set_string("resolution", 1);
            set_string("subscriberUid", 2);
        } else if (kind == 3) {
            request->Set(local_context, js_string(info.GetIsolate(), "kind"), js_string(info.GetIsolate(), "unsubscribeBars")).Check();
            set_string("subscriberUid", 0);
        } else {
            info.GetIsolate()->ThrowException(v8::Exception::Error(
                js_string(info.GetIsolate(), "Unknown WebScene managed bridge operation")));
            return;
        }

        if (!self->enqueue_host_request(local_context, request)) {
            info.GetIsolate()->ThrowException(v8::Exception::Error(
                js_string(info.GetIsolate(), "WebScene managed bridge request queue is unavailable")));
            return;
        }
        info.GetReturnValue().Set(v8::True(info.GetIsolate()));
    }

#include "webscene_v8_runtime_navigation.inc"
    // Keep these fragments in one translation unit: their order and direct
    // visibility preserve the runtime's existing release code generation.
#include "webscene_v8_runtime_interop.inc"
#include "webscene_v8_runtime_tasks.inc"
#include "webscene_v8_runtime_resources.inc"
#include "webscene_v8_runtime_dom_core.inc"
#include "webscene_v8_runtime_diagnostics.inc"
#include "webscene_v8_runtime_dom_properties.inc"
#include "webscene_v8_runtime_canvas.inc"
#include "webscene_v8_runtime_document.inc"
#include "webscene_v8_runtime_css_parsing.inc"
#include "webscene_v8_runtime_css_cascade.inc"
#include "webscene_v8_runtime_cache_and_frames.inc"
#include "webscene_v8_runtime_html.inc"
#include "webscene_v8_runtime_style.inc"
#include "webscene_v8_runtime_browser_apis.inc"
    static void promise_rejected(v8::PromiseRejectMessage message)
    {
        auto* isolate = v8::Isolate::GetCurrent();
        auto* self = current(isolate);
        auto promise = message.GetPromise();
        if (message.GetEvent() == v8::kPromiseHandlerAddedAfterReject) {
            std::erase_if(
                self->pending_promise_rejections,
                [&promise, isolate](auto& rejection) {
                    return rejection.promise.Get(isolate)->StrictEquals(promise);
                });
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
            if (auto* inspector_state = self->inspector_state_if_created()) {
                std::erase_if(
                    inspector_state->promise_rejections,
                    [&promise, isolate, self](auto& rejection) {
                    if (!rejection.promise.Get(isolate)->StrictEquals(promise)) {
                        return false;
                    }
                    self->revoke_inspector_exception(
                        rejection.context.Get(isolate),
                        rejection.inspector_exception_id,
                        "Promise rejection was handled asynchronously");
                    return true;
                });
            }
#endif
            return;
        }
        if (message.GetEvent() != v8::kPromiseRejectWithNoHandler) return;
        auto value = message.GetValue();
        auto rejection = self->exception_diagnostic(value,
            value.IsEmpty() ? v8::Local<v8::Message>{} : v8::Exception::CreateMessage(isolate, value),
            isolate->GetCurrentContext());
        auto error = "Unhandled promise rejection: " + rejection.message;
        if (!rejection.stack.empty()) error += "\n" + rejection.stack;
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        auto local_context = isolate->GetCurrentContext();
        const auto inspector_exception_id = value.IsEmpty()
            ? 0U
            : self->report_inspector_promise_rejection(
                local_context,
                value,
                error);
        if (inspector_exception_id != 0U) {
            auto* inspector_state = self->inspector_state_if_created();
            if (inspector_state != nullptr
                && inspector_state->promise_rejections.size()
                    == self->maximum_inspector_promise_rejections) {
                // This cap bounds only WebScene's late-handler bookkeeping.
                // The rejection is still unhandled, so evicting its tracking
                // record must not tell CDP that the exception was revoked.
                inspector_state->promise_rejections.pop_front();
            }
            if (inspector_state != nullptr) {
                inspector_state->promise_rejections.push_back({
                    v8::Global<v8::Promise>(isolate, promise),
                    v8::Global<v8::Context>(isolate, local_context),
                    inspector_exception_id});
            }
        }
#endif
        if (self->pending_promise_rejections.size() >= runtime_diagnostics::maximum_records) {
            self->pending_promise_rejections.pop_front();
            if (self->diagnostics != nullptr && self->diagnostics->enabled(WEBSCENE_DIAGNOSTIC_EXCEPTIONS))
                self->diagnostics->note_dropped();
        }
        self->pending_promise_rejections.push_back({
            v8::Global<v8::Promise>(isolate, promise),
            std::move(error),
            self->diagnostics != nullptr && self->diagnostics->enabled(WEBSCENE_DIAGNOSTIC_EXCEPTIONS)
                ? std::move(rejection) : runtime_diagnostic{}});
    }

#include "webscene_v8_runtime_state.inc"
};

void v8_dom_runtime::set_stylesheet_consumer(
    std::function<void(const std::string&, const std::string&)> consumer)
{
    impl_->stylesheet_consumer = std::move(consumer);
}

v8_dom_runtime::v8_dom_runtime(
    native_document& document,
    std::function<viewport_metrics()> viewport_provider,
    std::string compilation_cache_directory,
    resource_loader load_resource,
    std::function<void()> host_request_available,
    std::function<void()> interop_callback_available,
    interop_callback_sink_v3 interop_callback_sink,
    std::function<void()> runtime_work_available,
    runtime_diagnostics* diagnostics)
    : impl_(std::make_unique<implementation>(
        document,
        std::move(viewport_provider),
        std::move(compilation_cache_directory),
        std::move(load_resource),
          std::move(host_request_available),
          std::move(interop_callback_available),
          std::move(interop_callback_sink),
          std::move(runtime_work_available), diagnostics))
{
}

v8_dom_runtime::~v8_dom_runtime() = default;

bool v8_dom_runtime::initialize()
{
    return impl_->initialize();
}

bool v8_dom_runtime::execute(const std::string& source, const std::string& document_name)
{
    return impl_->execute(source, document_name);
}

bool v8_dom_runtime::load_url(
    const std::string& url,
    std::vector<document_start_script> document_start_scripts)
{
    return impl_->load_url(url, std::move(document_start_scripts));
}

bool v8_dom_runtime::set_visible(bool visible)
{
    return impl_->set_visible(visible)
        && impl_->promote_pending_promise_error();
}

void v8_dom_runtime::set_resource_root(std::string resource_root)
{
    impl_->resource_root = std::filesystem::path(std::move(resource_root)).lexically_normal();
}

bool v8_dom_runtime::evaluate_interop_v3(
    const std::string& source,
    const std::string& document_name,
    interop_result_data_v3& result)
{
    return impl_->evaluate_interop_v3(source, document_name, result);
}

interop_invoke_state_v3 v8_dom_runtime::invoke_interop_v3(
    const interop_invoke_request_data_v3& request,
    interop_result_data_v3& result,
    uint64_t operation_id,
    interop_completion_v3 completion)
{
    return impl_->invoke_interop_v3(
        request,
        result,
        operation_id,
        std::move(completion));
}

void v8_dom_runtime::cancel_interop_v3(uint64_t operation_id)
{
    impl_->cancel_interop_v3(operation_id);
}

bool v8_dom_runtime::complete_callback_v3(
    interop_callback_completion_data_v3& completion)
{
    return impl_->complete_callback_v3(completion);
}

void v8_dom_runtime::cancel_callback_v3(uint64_t call_id)
{
    impl_->cancel_callback_v3(call_id);
}

uint64_t v8_dom_runtime::pending_callback_promises() const noexcept
{
    return impl_->pending_callback_promise_count();
}

bool v8_dom_runtime::try_take_host_request(std::string& request)
{
    return impl_->try_take_host_request(request);
}

bool v8_dom_runtime::try_take_console_message(std::string& message)
{
    return impl_->try_take_console_message(message);
}

bool v8_dom_runtime::inspector_available() const noexcept
{
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
    return false;
#else
    return impl_->inspector_ready.load(std::memory_order_acquire);
#endif
}

uint64_t v8_dom_runtime::connect_inspector(
    inspector_message_sink message_sink,
    bool wait_for_debugger,
    std::function<void()> action_queued)
{
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
    static_cast<void>(message_sink);
    static_cast<void>(wait_for_debugger);
    static_cast<void>(action_queued);
    return 0U;
#else
    return impl_->connect_inspector(
        std::move(message_sink),
        wait_for_debugger,
        action_queued);
#endif
}

bool v8_dom_runtime::dispatch_inspector_message(
    uint64_t session_id,
    std::string message)
{
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
    static_cast<void>(session_id);
    static_cast<void>(message);
    return false;
#else
    return impl_->dispatch_inspector_message(session_id, std::move(message));
#endif
}

bool v8_dom_runtime::disconnect_inspector(uint64_t session_id)
{
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
    static_cast<void>(session_id);
    return false;
#else
    return impl_->disconnect_inspector(session_id);
#endif
}

bool v8_dom_runtime::pump_inspector_task(std::stop_token shutdown_token)
{
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
    return false;
#else
    if (!impl_->inspector_available_for_dispatch()) return false;
    auto* inspector_state = impl_->inspector_state_if_created();
    if (inspector_state == nullptr) return false;
    inspector_state->shutdown_token = shutdown_token;
    auto isolate_locker = impl_->lock_shared_isolate();
    v8::Isolate::Scope isolate_scope(impl_->isolate);
    v8::HandleScope handle_scope(impl_->isolate);
    auto local_context = impl_->context.Get(impl_->isolate);
    v8::Context::Scope context_scope(local_context);
    const auto processed = impl_->process_inspector_tasks(false);
    if (processed) {
        // Inspector Runtime.evaluate is a host-entered JavaScript task. V8 uses
        // an explicit microtask policy in WebScene, so the worker must establish
        // the same task boundary as navigation, input, timer, and interop calls.
        // This runs only after an outer Inspector action has completed; commands
        // pumped by the nested pause loop call process_inspector_tasks directly
        // and therefore cannot run page microtasks while JavaScript is paused.
        impl_->perform_microtask_checkpoint();
    }
    return processed;
#endif
}

bool v8_dom_runtime::has_pending_inspector_tasks() const noexcept
{
#if !defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
    return false;
#else
    return impl_->has_pending_inspector_tasks();
#endif
}

bool v8_dom_runtime::dispatch_resize()
{
    return impl_->dispatch_resize()
        && impl_->promote_pending_promise_error();
}

bool v8_dom_runtime::deliver_resize_observers()
{
    if (impl_->isolate == nullptr) return true;
    auto isolate_locker = impl_->lock_shared_isolate();
    v8::Isolate::Scope isolate_scope(impl_->isolate);
    v8::HandleScope handle_scope(impl_->isolate);
    auto local_context = impl_->frame_context.IsEmpty()
        ? impl_->context.Get(impl_->isolate)
        : impl_->frame_context.Get(impl_->isolate);
    v8::Context::Scope context_scope(local_context);
    return impl_->deliver_resize_observer_checkpoint()
        && impl_->promote_pending_promise_error();
}

bool v8_dom_runtime::refresh_media_environment()
{
    return impl_->refresh_media_environment()
        && impl_->promote_pending_promise_error();
}

uint64_t v8_dom_runtime::last_resize_outer_listeners_nanoseconds() const noexcept
{
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    return impl_->last_resize_outer_listeners_ns;
#else
    return 0;
#endif
}

uint64_t v8_dom_runtime::last_resize_frame_listeners_nanoseconds() const noexcept
{
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    return impl_->last_resize_frame_listeners_ns;
#else
    return 0;
#endif
}

uint64_t v8_dom_runtime::last_resize_layout_nanoseconds() const noexcept
{
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    return impl_->last_resize_layout_ns;
#else
    return 0;
#endif
}

uint64_t v8_dom_runtime::last_resize_observers_nanoseconds() const noexcept
{
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    return impl_->last_resize_observers_ns;
#else
    return 0;
#endif
}

bool v8_dom_runtime::dispatch_input(const webscene_input_event& event)
{
    return impl_->dispatch_input(event)
        && impl_->promote_pending_promise_error();
}

bool v8_dom_runtime::dispatch_transition_events()
{
    return impl_->dispatch_transition_events()
        && impl_->promote_pending_promise_error();
}

uint32_t v8_dom_runtime::current_cursor_kind() const noexcept
{
    return impl_->current_cursor_kind();
}

void v8_dom_runtime::notify_low_memory()
{
    if (impl_->isolate == nullptr) return;
    impl_->compact_retained_native_capacity();
    auto isolate_locker = impl_->lock_shared_isolate();
    v8::Isolate::Scope isolate_scope(impl_->isolate);
    impl_->isolate->LowMemoryNotification();
}

void v8_dom_runtime::signal_animation_frame(double timestamp_ms)
{
    const auto now = std::chrono::steady_clock::now();
    const auto timestamp = std::isfinite(timestamp_ms) && timestamp_ms >= 0
        ? timestamp_ms
        : std::chrono::duration<double, std::milli>(now.time_since_epoch()).count();
    impl_->last_animation_frame_timestamp_ms = timestamp;
    if (impl_->is_text_control(impl_->active_element)
        && impl_->active_element->mutable_form_control().input_focused) {
        const auto elapsed = std::max(0.0, timestamp - impl_->caret_blink_epoch_ms);
        const auto visible = std::fmod(elapsed, 1000.0) < 500.0;
        if (impl_->active_element->mutable_form_control().caret_visible != visible) {
            impl_->active_element->mutable_form_control().caret_visible = visible;
            impl_->document.mark_dirty();
        }
    }
    for (auto& timer : impl_->timers) {
        if (!timer.animation_frame
            || timer.deadline != std::chrono::steady_clock::time_point::max()) {
            continue;
        }
        // Once a callback has joined a rendering opportunity it retains that
        // opportunity's timestamp until it runs. A later compositor signal
        // releases only callbacks requested for the next frame; it must not
        // retimestamp callbacks that the fair task scheduler has not drained
        // yet.
        timer.deadline = now;
        timer.animation_timestamp_ms = timestamp;
    }
}

bool v8_dom_runtime::pump_animation_frame_task()
{
    auto isolate_locker = impl_->lock_shared_isolate();
    v8::Isolate::Scope isolate_scope(impl_->isolate);
    v8::HandleScope handle_scope(impl_->isolate);
    auto local_context = impl_->context.Get(impl_->isolate);
    v8::Context::Scope context_scope(local_context);
    return impl_->drain_animation_frame_task()
        && impl_->promote_pending_promise_error();
}

bool v8_dom_runtime::has_pending_animation_frame_task() const noexcept
{
    return impl_->has_due_animation_frame_task();
}

uint8_t v8_dom_runtime::host_animation_frame_demand() const noexcept
{
    auto demand = impl_->has_waiting_animation_frame_task()
        ? uint8_t{1U}
        : uint8_t{0U};
    if (impl_->is_text_control(impl_->active_element)
        && impl_->active_element->mutable_form_control().input_focused) {
        demand |= 4U;
    }
    return demand;
}

bool v8_dom_runtime::pump_task()
{
    auto isolate_locker = impl_->lock_shared_isolate();
    v8::Isolate::Scope isolate_scope(impl_->isolate);
    v8::HandleScope handle_scope(impl_->isolate);
    auto local_context = impl_->context.Get(impl_->isolate);
    v8::Context::Scope context_scope(local_context);
    return impl_->drain_tasks()
        && impl_->promote_pending_promise_error();
}

bool v8_dom_runtime::has_pending_tasks() const noexcept
{
    return impl_->has_pending_detached_dom_collection()
        || impl_->websocket_transport.has_pending_events()
        || !impl_->pending_window_messages.empty()
        || impl_->has_ready_fetch_task()
        || !impl_->pending_programmatic_scroll_events.empty()
        || !impl_->pending_frame_hydrations.empty()
        || !impl_->connected_resources.empty()
        || impl_->resize_observers_pending
        || impl_->has_due_timer();
}

std::chrono::milliseconds v8_dom_runtime::recommended_idle_wait(
    std::chrono::milliseconds maximum) const noexcept
{
    const auto now = std::chrono::steady_clock::now();
    auto wait = maximum;
    for (const auto& timer : impl_->timers) {
        // An unreleased requestAnimationFrame is woken by the host frame input,
        // not by wall-clock polling.
        if (timer.deadline == std::chrono::steady_clock::time_point::max()) {
            continue;
        }
        if (timer.deadline <= now) return std::chrono::milliseconds::zero();
        wait = std::min(
            wait,
            std::chrono::ceil<std::chrono::milliseconds>(timer.deadline - now));
    }
    return std::max(wait, std::chrono::milliseconds(1));
}

bool v8_dom_runtime::component_ready()
{
    auto isolate_locker = impl_->lock_shared_isolate();
    v8::Isolate::Scope isolate_scope(impl_->isolate);
    v8::HandleScope handle_scope(impl_->isolate);
    auto local_context = impl_->context.Get(impl_->isolate);
    v8::Context::Scope context_scope(local_context);
    v8::Local<v8::Value> explicit_ready;
    return local_context->Global()->Get(
            local_context,
            js_string(impl_->isolate, "__webSceneComponentReady")).ToLocal(&explicit_ready)
        && explicit_ready->BooleanValue(impl_->isolate);
}

std::string v8_dom_runtime::diagnostics()
{
    std::ostringstream description;
    if (impl_->shared_isolate != nullptr) {
        description << "v8-shard={slot=" << impl_->shared_isolate->slot_index
            << ",active="
            << impl_->shared_isolate->active_contexts.load(
                std::memory_order_relaxed)
            << ",peak="
            << impl_->shared_isolate->peak_contexts.load(
                std::memory_order_relaxed)
            << '}';
    } else {
        description << "v8-shard=dedicated";
    }
#if !defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    description << " | certification telemetry disabled at compile time";
    return std::move(description).str();
#else
    description << " | resources=[";
    const auto start = impl_->loaded_resource_names.size() > 24U
        ? impl_->loaded_resource_names.size() - 24U
        : 0U;
    for (auto index = start; index < impl_->loaded_resource_names.size(); ++index) {
        if (index != start) description << ',';
        description << impl_->loaded_resource_names[index];
    }
    description << "] | resource-memory-hits="
        << impl_->resource_cache_memory_hit_count.load(std::memory_order_relaxed)
        << ", detached-dom={pending-roots=" << impl_->detached_dom_roots.size()
        << ",scan-roots="
        << (impl_->detached_dom_gc_scan_roots.size()
            - impl_->detached_dom_gc_scan_index)
        << ",pending-nodes=" << impl_->detached_nodes_since_gc
        << ",released=" << impl_->released_detached_dom_nodes << '}';
    if (impl_->profile_startup) {
        const auto format = [](const binding_callback_stats& value) {
            std::ostringstream result;
            result << value.calls << '/' << std::fixed << std::setprecision(3)
                << value.nanoseconds / 1'000'000.0 << "ms";
            return std::move(result).str();
        };
        const auto format_top = [&format](const auto& profiles) {
            std::vector<std::pair<std::string, binding_callback_stats>> ordered(
                profiles.begin(),
                profiles.end());
            std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
                return left.second.nanoseconds > right.second.nanoseconds;
            });
            std::ostringstream result;
            result << '[';
            const auto count = std::min<size_t>(ordered.size(), 8U);
            for (size_t index = 0; index < count; ++index) {
                if (index != 0U) result << ',';
                result << ordered[index].first << ':' << format(ordered[index].second);
            }
            result << ']';
            return std::move(result).str();
        };
        const auto format_phase_top = [&format](const auto& profiles) {
            std::vector<std::pair<std::string, startup_nested_phase_stats>> ordered(
                profiles.begin(),
                profiles.end());
            std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
                return left.second.elapsed.nanoseconds > right.second.elapsed.nanoseconds;
            });
            std::ostringstream result;
            result << '[';
            const auto count = std::min<size_t>(ordered.size(), 8U);
            for (size_t index = 0; index < count; ++index) {
                if (index != 0U) result << ',';
                const auto& [name, phases] = ordered[index];
                result << name << ":{task=" << format(phases.elapsed)
                    << ",dirty=" << phases.clean_to_dirty_transitions
                    << '/' << phases.tasks_left_dirty
                    << ",io=" << format(phases.resource_read)
                    << ",css-parse=" << format(phases.css_parse)
                    << ",layout-pass=" << phases.layout_passes
                    << ",layout=" << format(phases.layout)
                    << ",css-apply=" << format(phases.css_apply)
                    << ",css-inc=" << format(phases.css_incremental_apply)
                    << ",subtree=" << format(phases.subtree_recascade)
                    << ",sheet=" << format(phases.stylesheet_recascade)
                    << ",sheet-nodes=" << phases.stylesheet_nodes
                    << ",variable-nodes=" << phases.stylesheet_variable_nodes
                    << ",script=" << format(phases.script_execute)
                    << '}';
            }
            result << ']';
            return std::move(result).str();
        };
        description << ", startup-profile={hydrate="
            << format(impl_->startup_frame_hydrate)
            << ",frame-prepare=" << format(impl_->startup_frame_prepare)
            << ",frame-prepare-wait=" << format(impl_->startup_frame_prepare_wait)
            << ",frame-prepare-lead=" << std::fixed << std::setprecision(3)
            << impl_->startup_frame_prepare_lead_nanoseconds / 1'000'000.0
            << "ms"
            << ",frame-slices=" << impl_->startup_frame_hydration_slices
            << ",frame-yields=" << impl_->startup_frame_hydration_yields
            << ",frame-max-slice=" << std::fixed << std::setprecision(3)
            << impl_->startup_frame_hydration_max_slice_nanoseconds / 1'000'000.0
            << "ms"
            << ",io=" << format(impl_->startup_resource_read)
            << ",css-parse=" << format(impl_->startup_css_parse)
            << ",css-apply=" << format(impl_->startup_css_apply)
            << ",css-incremental=" << format(impl_->startup_css_incremental_apply)
            << ",subtree-recascade=" << format(impl_->startup_subtree_recascade)
            << ",stylesheet-recascade=" << format(impl_->startup_stylesheet_recascade)
            << ",stylesheet-nodes=" << impl_->startup_stylesheet_recascade_nodes
            << ",stylesheet-candidate-nodes="
            << impl_->startup_stylesheet_candidate_nodes
            << ",stylesheet-variable-nodes="
            << impl_->startup_stylesheet_variable_nodes
            << ",css-rules=" << impl_->css_rules.size()
            << ",css-unindexed=" << impl_->unindexed_css_rules.size()
            << ",script=" << format(impl_->startup_script_execute)
            << ",script-compile=" << format(impl_->startup_script_compile)
            << ",script-run=" << format(impl_->startup_script_run)
            << ",layout=" << format(impl_->startup_layout)
            << ",resources=" << impl_->startup_connected_resources
            << ",raf=" << impl_->startup_raf_executed << '/'
            << impl_->startup_raf_scheduled
            << ",timers=" << impl_->startup_timer_executed << '/'
            << impl_->startup_timer_scheduled
            << ",max-timer=" << std::fixed << std::setprecision(1)
            << impl_->startup_max_timer_delay_ms << "ms"
            << ",task-callbacks=" << format(impl_->startup_task_callbacks)
            << ",script-top=" << format_top(impl_->startup_script_profiles)
            << ",task-top=" << format_top(impl_->startup_task_profiles)
            << ",script-phase-top="
            << format_phase_top(impl_->startup_script_phase_profiles)
            << ",task-phase-top="
            << format_phase_top(impl_->startup_task_phase_profiles)
            << ",frame-phase-top="
            << format_phase_top(impl_->startup_frame_phase_profiles);
        const auto profile_now = std::chrono::steady_clock::now();
        if (impl_->startup_profile_started != std::chrono::steady_clock::time_point{}) {
            description << ",total=" << std::fixed << std::setprecision(1)
                << std::chrono::duration<double, std::milli>(
                    profile_now - impl_->startup_profile_started).count()
                << "ms";
        }
        if (impl_->startup_frame_started != std::chrono::steady_clock::time_point{}) {
            description << ",frame-start=" << std::fixed << std::setprecision(1)
                << std::chrono::duration<double, std::milli>(
                    impl_->startup_frame_started - impl_->startup_profile_started).count()
                << "ms,frame-wall=" << std::fixed << std::setprecision(1)
                << std::chrono::duration<double, std::milli>(
                    profile_now - impl_->startup_frame_started).count()
                << "ms";
        }
        description << '}';
        if (impl_->profile_bindings) {
            description << ",startup-bindings=[";
            for (size_t index = 0; index < binding_category_count; ++index) {
                if (index != 0) description << ',';
                const auto& stats = impl_->binding_totals[index];
                description << binding_category_names[index]
                    << ":c" << stats.calls
                    << "/ms" << std::fixed << std::setprecision(3)
                    << static_cast<double>(stats.nanoseconds) / 1'000'000.0;
            }
            description << "],binding-top="
                << format_top(impl_->startup_binding_profiles)
                << ",forced-layout-top="
                << format_top(impl_->startup_forced_layout_profiles);
        }
    }
    description << " | html-parser={implementation="
#if defined(WEBSCENE_NATIVE_ENGINE_HTML5EVER)
        << "html5ever"
#else
        << "legacy"
#endif
        << ",ms=" << std::fixed << std::setprecision(3)
        << static_cast<double>(impl_->html_parse_duration_ns) / 1'000'000.0
        << ",callbacks=" << impl_->html_parse_callback_count
        << ",errors=" << impl_->html_parse_error_count
        << ",elements=" << impl_->html_parse_element_count
        << ",text-appends=" << impl_->html_parse_text_append_count
        << ",comments=" << impl_->html_parse_comment_count
        << ",doctypes=" << impl_->html_parse_doctype_count
        << ",rust-allocations=" << impl_->html_parse_rust_allocation_count
        << ",rust-peak-bytes=" << impl_->html_parse_rust_peak_bytes
        << ",rust-retained-bytes=" << impl_->html_parse_rust_retained_bytes
        << '}';
    const auto node_metrics = impl_->document.read_allocation_metrics();
    description << " | dom-node-kinds={element=" << node_metrics.element_node_count
        << ",text=" << node_metrics.text_node_count
        << ",comment=" << node_metrics.comment_node_count
        << ",doctype=" << node_metrics.document_type_node_count
        << ",other=" << node_metrics.other_node_count
        << ",bytes-per-node=" << node_metrics.node_object_size_bytes << '}';
    return description.str();
#endif
}

std::string v8_dom_runtime::event_diagnostics() const
{
#if !defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    return "event telemetry disabled at compile time";
#else
    std::vector<std::string> types;
    types.reserve(impl_->event_dispatch_counts.size());
    for (const auto& [type, count] : impl_->event_dispatch_counts) {
        static_cast<void>(count);
        types.push_back(type);
    }
    std::sort(types.begin(), types.end());
    std::ostringstream result;
    result << "events=[";
    for (size_t index = 0; index < types.size(); ++index) {
        if (index != 0) result << ',';
        const auto& type = types[index];
        const auto listeners = impl_->frame_event_listeners.find(type);
        result << type << ":d" << impl_->event_dispatch_counts.at(type)
            << "/c" << (impl_->event_callback_counts.contains(type)
                ? impl_->event_callback_counts.at(type) : 0U)
            << "/l" << (listeners == impl_->frame_event_listeners.end()
                ? 0U : listeners->second.size());
        const auto dispatch_targets = impl_->event_dispatch_target_counts.find(type);
        if (dispatch_targets != impl_->event_dispatch_target_counts.end()) {
            result << "/h{";
            bool first = true;
            for (const auto& [target, count] : dispatch_targets->second) {
                if (!first) result << ';';
                first = false;
                result << target << ':' << count;
            }
            result << '}';
        }
        const auto targets = impl_->event_callback_target_counts.find(type);
        if (targets != impl_->event_callback_target_counts.end()) {
            result << "@{";
            bool first = true;
            for (const auto& [target, count] : targets->second) {
                if (!first) result << ';';
                first = false;
                result << target << ':' << count;
            }
            result << '}';
        }
        if (listeners != impl_->frame_event_listeners.end()) {
            std::unordered_map<uint32_t, size_t> target_counts;
            for (const auto& listener : listeners->second) {
                ++target_counts[listener.target];
            }
            result << "/t{";
            bool first = true;
            for (const auto& [target, count] : target_counts) {
                if (!first) result << ';';
                first = false;
                result << target << ':' << count;
            }
            result << '}';
        }
        if (type == "mousemove") {
            if (listeners != impl_->frame_event_listeners.end()) {
                result << "/n{";
                for (size_t name_index = 0;
                    name_index < listeners->second.size();
                    ++name_index) {
                    if (name_index != 0) result << ';';
                    const auto& listener = listeners->second[name_index];
                    const auto callbacks = impl_->event_callback_index_counts.contains(type)
                        && impl_->event_callback_index_counts.at(type).contains(name_index)
                        ? impl_->event_callback_index_counts.at(type).at(name_index)
                        : 0U;
                    result << name_index << ':' << listener.name
                        << "@t" << listener.target << "/c" << callbacks
                        << "/s" << listener.registration_sequence
                        << (listener.callback.IsEmpty() ? "/empty" : "");
                }
                result << '}';
            }
        }
    }
    result << ']';
    result << ", layout-client-reuses="
        << impl_->client_geometry_layout_reuse_count;
    result << ", intrinsic-size-cache-hits="
        << impl_->document.intrinsic_size_cache_hits();
    result << ", intrinsic-size-cache-misses="
        << impl_->document.intrinsic_size_cache_misses();
    result << ", wrapper-retention-subtrees="
        << impl_->reconnected_wrapper_retention_subtrees;
    result << ", wrapper-retention-nodes="
        << impl_->reconnected_wrapper_retention_nodes;
    result << ", detached-dom-release-batches="
        << impl_->detached_dom_release_batches;
    result << ", detached-dom-release-roots="
        << impl_->detached_dom_release_roots;
    result << ", detached-dom-release-slices="
        << impl_->detached_dom_release_slices;
    result << ", detached-dom-release-max-roots-per-slice="
        << impl_->detached_dom_release_max_roots_per_slice;
    result << ", detached-dom-idle-gc-notifications="
        << impl_->detached_dom_idle_gc_notifications;
    result << ", style-recascade-schedule-requests="
        << impl_->style_recascade_schedule_requests;
    result << ", style-recascade-coalesced-requests="
        << impl_->style_recascade_coalesced_requests;
    result << ", style-recascade-flush-batches="
        << impl_->style_recascade_flush_batches;
    result << ", style-recascade-flush-roots="
        << impl_->style_recascade_flush_roots;
    result << ", style-recascade-flush-node-roots="
        << impl_->style_recascade_flush_node_roots;
    result << ", style-recascade-flush-subtree-roots="
        << impl_->style_recascade_flush_subtree_roots;
    result << ", style-recascade-noop-removals="
        << impl_->style_recascade_noop_removals;
    if (impl_->profile_bindings) {
        uint64_t total_nanoseconds = 0;
        result << ", resize-bindings=[";
        for (size_t index = 0; index < binding_category_count; ++index) {
            if (index != 0) result << ',';
            const auto& stats = impl_->last_resize_binding_profile[index];
            total_nanoseconds += stats.nanoseconds;
            result << binding_category_names[index]
                << ":c" << stats.calls
                << "/ms" << std::fixed << std::setprecision(3)
                << static_cast<double>(stats.nanoseconds) / 1'000'000.0;
        }
        result << "]/profiled-ms" << std::fixed << std::setprecision(3)
            << static_cast<double>(total_nanoseconds) / 1'000'000.0;
        const auto append_counts = [&result](
            std::string_view label,
            const std::unordered_map<std::string, uint64_t>& counts) {
            std::vector<std::pair<std::string, uint64_t>> ordered(
                counts.begin(),
                counts.end());
            std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
                if (left.second != right.second) return left.second > right.second;
                return left.first < right.first;
            });
            result << ", " << label << "={";
            for (size_t index = 0; index < ordered.size(); ++index) {
                if (index != 0) result << ';';
                result << ordered[index].first << ':' << ordered[index].second;
            }
            result << '}';
        };
        append_counts(
            "resize-style",
            impl_->last_resize_style_property_counts);
        append_counts(
            "resize-style-targets",
            impl_->last_resize_style_target_counts);
        append_counts(
            "resize-attributes",
            impl_->last_resize_attribute_counts);
        append_counts(
            "resize-geometry",
            impl_->last_resize_geometry_counts);
        result << ", resize-redundant-style-writes="
            << impl_->last_resize_redundant_style_writes;
    }
    if (!impl_->last_resize_cpu_profile.empty()) {
        result << ", resize-cpu=" << impl_->last_resize_cpu_profile;
    }
    if (!impl_->last_mousemove_ancestry.empty()) {
        result << ", mousemove-ancestry=" << impl_->last_mousemove_ancestry;
    }
    return result.str();
#endif
}

std::string v8_dom_runtime::feature_use_json() const
{
    return impl_->feature_use_json();
}

std::string v8_dom_runtime::event_listener_inventory_json() const
{
    return impl_->event_listener_inventory_json();
}

const std::string& v8_dom_runtime::last_error() const noexcept
{
    return impl_->last_error;
}

uint64_t v8_dom_runtime::frame_scripts_executed() const noexcept
{
    return impl_->frame_script_execution_count;
}

uint64_t v8_dom_runtime::frame_script_errors() const noexcept
{
    return impl_->frame_script_error_count;
}

uint64_t v8_dom_runtime::compilation_requests() const noexcept
{
    return impl_->compilation_request_count;
}

uint64_t v8_dom_runtime::compilation_memory_hits() const noexcept
{
    return impl_->compilation_memory_hit_count;
}

uint64_t v8_dom_runtime::compilation_persistent_hits() const noexcept
{
    return impl_->compilation_persistent_hit_count;
}

uint64_t v8_dom_runtime::compilation_persistent_misses() const noexcept
{
    return impl_->compilation_persistent_miss_count;
}

uint64_t v8_dom_runtime::compilation_cache_rejections() const noexcept
{
    return impl_->compilation_cache_rejection_count;
}

uint64_t v8_dom_runtime::compilation_cache_bytes_read() const noexcept
{
    return impl_->compilation_cache_bytes_read_count;
}

uint64_t v8_dom_runtime::compilation_cache_bytes_written() const noexcept
{
    return impl_->compilation_cache_bytes_written_count;
}

uint64_t v8_dom_runtime::compilation_time_nanoseconds() const noexcept
{
    return impl_->compilation_time_nanosecond_count;
}

uint64_t v8_dom_runtime::process_compilation_memory_hits() const noexcept
{
    return impl_->process_compilation_memory_hit_count;
}

uint64_t v8_dom_runtime::process_compilation_leaders() const noexcept
{
    return impl_->process_compilation_leader_count;
}

uint64_t v8_dom_runtime::process_compilation_waiters() const noexcept
{
    return impl_->process_compilation_waiter_count;
}

uint64_t v8_dom_runtime::process_compilation_shared_bytes() const noexcept
{
    return impl_->process_compilation_shared_byte_count;
}

uint64_t v8_dom_runtime::process_resource_memory_hits() const noexcept
{
    return impl_->process_resource_memory_hit_count.load(std::memory_order_relaxed);
}

uint64_t v8_dom_runtime::process_resource_load_leaders() const noexcept
{
    return impl_->process_resource_load_leader_count.load(std::memory_order_relaxed);
}

uint64_t v8_dom_runtime::process_resource_load_waiters() const noexcept
{
    return impl_->process_resource_load_waiter_count.load(std::memory_order_relaxed);
}

v8_dom_runtime::work_metrics v8_dom_runtime::read_work_metrics() const noexcept
{
    return impl_->work;
}

void v8_dom_runtime::set_work_metrics_enabled(bool enabled) noexcept
{
    impl_->work_metrics_enabled.store(enabled, std::memory_order_release);
}

uint64_t v8_dom_runtime::process_resource_shared_bytes() const noexcept
{
    return impl_->process_resource_shared_byte_count.load(std::memory_order_relaxed);
}

uint64_t v8_dom_runtime::process_script_source_memory_hits() const noexcept
{
    return impl_->process_script_source_memory_hit_count;
}

uint64_t v8_dom_runtime::process_script_source_shared_bytes() const noexcept
{
    return impl_->process_script_source_shared_byte_count;
}

uint64_t v8_dom_runtime::shared_isolate_slot() const noexcept
{
    return impl_->shared_isolate == nullptr
        ? std::numeric_limits<uint64_t>::max()
        : static_cast<uint64_t>(impl_->shared_isolate->slot_index);
}

uint64_t v8_dom_runtime::shared_isolate_active_contexts() const noexcept
{
    return impl_->shared_isolate == nullptr
        ? 1U
        : static_cast<uint64_t>(
            impl_->shared_isolate->active_contexts.load(
                std::memory_order_relaxed));
}

uint64_t v8_dom_runtime::shared_isolate_peak_contexts() const noexcept
{
    return impl_->shared_isolate == nullptr
        ? 1U
        : static_cast<uint64_t>(
            impl_->shared_isolate->peak_contexts.load(
                std::memory_order_relaxed));
}

uint64_t v8_dom_runtime::resource_cache_requests() const noexcept
{
    return impl_->resource_cache_request_count.load(std::memory_order_relaxed);
}

uint64_t v8_dom_runtime::resource_cache_hits() const noexcept
{
    return impl_->resource_cache_hit_count.load(std::memory_order_relaxed);
}

uint64_t v8_dom_runtime::resource_cache_misses() const noexcept
{
    return impl_->resource_cache_miss_count.load(std::memory_order_relaxed);
}

uint64_t v8_dom_runtime::resource_cache_rejections() const noexcept
{
    return impl_->resource_cache_rejection_count.load(std::memory_order_relaxed);
}

uint64_t v8_dom_runtime::resource_cache_bytes_read() const noexcept
{
    return impl_->resource_cache_bytes_read_count.load(std::memory_order_relaxed);
}

uint64_t v8_dom_runtime::resource_cache_bytes_written() const noexcept
{
    return impl_->resource_cache_bytes_written_count.load(std::memory_order_relaxed);
}

uint64_t v8_dom_runtime::input_events_dispatched() const noexcept
{
    return impl_->input_event_dispatch_count;
}

uint64_t v8_dom_runtime::input_callbacks_invoked() const noexcept
{
    return impl_->input_callback_invocation_count;
}

v8_dom_runtime::memory_metrics v8_dom_runtime::read_memory_metrics() const noexcept
{
    memory_metrics result{};
    if (impl_->isolate != nullptr) {
        auto isolate_locker = impl_->lock_shared_isolate();
        v8::HeapStatistics statistics;
        impl_->isolate->GetHeapStatistics(&statistics);
        result.total_heap_bytes = statistics.total_heap_size();
        result.used_heap_bytes = statistics.used_heap_size();
        result.executable_heap_bytes = statistics.total_heap_size_executable();
        result.physical_heap_bytes = statistics.total_physical_size();
        result.external_bytes = statistics.external_memory();
        result.malloced_bytes = statistics.malloced_memory();
        result.peak_malloced_bytes = statistics.peak_malloced_memory();
        result.external_script_source_bytes =
            impl_->external_script_source_byte_count->load(
                std::memory_order_relaxed);
        // This API walks V8's code and metadata spaces. Four settled chart
        // documents calling it periodically consumed measurable CPU even
        // though these fields are diagnostic-only and ordinary production
        // scheduling never reads them. Keep the detailed census available to
        // certification, tests, and explicit profiling without placing it in
        // the default runtime's recurring metrics path.
        if (std::getenv("WEBSCENE_V8_DETAILED_MEMORY_METRICS") != nullptr) {
            v8::HeapCodeStatistics code_statistics;
            if (impl_->isolate->GetHeapCodeAndMetadataStatistics(
                    &code_statistics)) {
                result.code_and_metadata_bytes =
                    code_statistics.code_and_metadata_size();
                result.bytecode_and_metadata_bytes =
                    code_statistics.bytecode_and_metadata_size();
                result.external_script_source_bytes = std::max<uint64_t>(
                    result.external_script_source_bytes,
                    code_statistics.external_script_source_size());
            }
        }
        const auto add_space = [](
                                   uint64_t& used_total,
                                   uint64_t& physical_total,
                                   v8::HeapSpaceStatistics& space) {
            used_total += space.space_used_size();
            physical_total += space.physical_space_size();
        };
        const auto heap_space_count = impl_->isolate->NumberOfHeapSpaces();
        for (size_t index = 0; index < heap_space_count; ++index) {
            v8::HeapSpaceStatistics space;
            if (!impl_->isolate->GetHeapSpaceStatistics(&space, index)) {
                continue;
            }
            const std::string_view name{space.space_name()};
            if (name == "new_space" || name == "new_large_object_space") {
                add_space(
                    result.young_space_used_bytes,
                    result.young_space_physical_bytes,
                    space);
            } else if (name == "old_space") {
                add_space(
                    result.old_space_used_bytes,
                    result.old_space_physical_bytes,
                    space);
            } else if (
                name == "code_space" || name == "code_large_object_space") {
                add_space(
                    result.code_space_used_bytes,
                    result.code_space_physical_bytes,
                    space);
            } else if (name == "map_space") {
                add_space(
                    result.map_space_used_bytes,
                    result.map_space_physical_bytes,
                    space);
            } else if (name == "large_object_space") {
                add_space(
                    result.large_object_space_used_bytes,
                    result.large_object_space_physical_bytes,
                    space);
            } else if (name == "read_only_space") {
                add_space(
                    result.read_only_space_used_bytes,
                    result.read_only_space_physical_bytes,
                    space);
            } else if (name.starts_with("shared_")) {
                add_space(
                    result.shared_space_used_bytes,
                    result.shared_space_physical_bytes,
                    space);
            } else if (
                name == "trusted_space"
                || name == "trusted_large_object_space") {
                add_space(
                    result.trusted_space_used_bytes,
                    result.trusted_space_physical_bytes,
                    space);
            }
        }
        v8::SharedMemoryStatistics shared_statistics;
        v8::V8::GetSharedMemoryStatistics(&shared_statistics);
        result.read_only_space_used_bytes =
            shared_statistics.read_only_space_used_size();
        result.read_only_space_physical_bytes =
            shared_statistics.read_only_space_physical_size();
    }
    {
        std::lock_guard lock(implementation::process_compilation_cache_mutex);
        result.process_compilation_cache_bytes =
            implementation::process_compilation_cache_bytes;
        result.process_compilation_mapped_cache_bytes =
            implementation::process_compilation_mapped_cache_bytes;
    }
    {
        std::lock_guard lock(implementation::process_resource_cache_mutex);
        result.process_resource_cache_bytes =
            implementation::process_resource_cache_bytes;
        result.process_resource_mapped_cache_bytes =
            implementation::process_resource_mapped_cache_bytes;
    }
    const auto native_dom = impl_->document.read_allocation_metrics();
    result.native_dom_node_count = native_dom.node_count;
    result.native_dom_node_size_bytes = native_dom.node_object_size_bytes;
    result.native_dom_inline_bytes = native_dom.node_object_bytes;
    result.native_dom_node_pool_reserved_bytes =
        native_dom.node_pool_reserved_bytes;
    result.native_dom_node_pool_peak_bytes =
        native_dom.node_pool_peak_bytes;
    result.native_dom_table_layout_count =
        native_dom.table_layout_node_count;
    result.native_dom_table_layout_storage_bytes =
        native_dom.table_layout_storage_bytes;
    result.native_dom_form_control_count =
        native_dom.form_control_node_count;
    result.native_dom_form_control_storage_bytes =
        native_dom.form_control_storage_bytes;
    result.native_dom_attribute_node_count =
        native_dom.attribute_node_count;
    result.native_dom_attribute_entry_count =
        native_dom.attribute_entry_count;
    result.native_dom_attribute_storage_bytes =
        native_dom.attribute_storage_bytes;
    result.native_dom_pseudo_storage_bytes =
        native_dom.pseudo_element_storage_bytes;
    result.native_dom_animation_count = native_dom.animation_data_count;
    result.native_dom_animation_storage_bytes =
        native_dom.animation_storage_bytes
        + native_dom.animation_runtime_storage_bytes;
    result.native_dom_custom_property_node_count =
        native_dom.custom_property_node_count;
    result.native_dom_custom_property_entry_count =
        native_dom.custom_property_entry_count;
    result.native_dom_custom_property_storage_bytes =
        native_dom.custom_property_storage_bytes;
    result.native_dom_background_image_count =
        native_dom.background_image_data_count;
    result.native_dom_background_image_storage_bytes =
        native_dom.background_image_storage_bytes;
    result.native_dom_grid_count = native_dom.grid_data_count;
    result.native_dom_grid_storage_bytes = native_dom.grid_storage_bytes;
    result.native_dom_textual_style_count =
        native_dom.textual_style_data_count;
    result.native_dom_textual_style_storage_bytes =
        native_dom.textual_style_storage_bytes;
    result.native_dom_authored_style_node_count =
        native_dom.authored_style_node_count;
    result.native_dom_authored_style_entry_count =
        native_dom.authored_style_entry_count;
    result.native_dom_authored_style_storage_bytes =
        native_dom.authored_style_storage_bytes;
    result.native_dom_canvas_node_count = native_dom.canvas_node_count;
    result.native_dom_canvas_storage_bytes = native_dom.canvas_storage_bytes;
    const auto wrapper_map_storage = [](const auto& wrappers) {
        return wrappers.bucket_count() * sizeof(void*)
            + wrappers.size()
                * (sizeof(typename std::decay_t<decltype(wrappers)>::value_type)
                    + 2U * sizeof(void*));
    };
    result.native_wrapper_handle_count =
        impl_->node_wrappers.size()
        + impl_->class_list_wrappers.size()
        + impl_->style_wrappers.size()
        + impl_->computed_style_wrappers.size();
    result.native_wrapper_storage_bytes =
        wrapper_map_storage(impl_->node_wrappers)
        + wrapper_map_storage(impl_->class_list_wrappers)
        + wrapper_map_storage(impl_->style_wrappers)
        + wrapper_map_storage(impl_->computed_style_wrappers);
    const auto listener_map_storage = [](const auto& map) {
        using map_type = std::decay_t<decltype(map)>;
        using vector_type = typename map_type::mapped_type;
        using element_type = typename vector_type::value_type;
        uint64_t bytes = map.bucket_count() * sizeof(void*);
        for (const auto& [name, values] : map) {
            bytes += sizeof(typename map_type::value_type)
                + 2U * sizeof(void*) + name.capacity() + 1U
                + values.capacity() * sizeof(element_type);
        }
        return bytes;
    };
    result.native_event_listener_count = std::accumulate(
        impl_->frame_event_listeners.begin(),
        impl_->frame_event_listeners.end(),
        uint64_t{0},
        [](uint64_t count, const auto& entry) {
            return count + entry.second.size();
        });
    result.native_event_listener_storage_bytes =
        listener_map_storage(impl_->frame_event_listeners);
    for (const auto& [_, listeners] : impl_->frame_event_listeners) {
        for (const auto& listener : listeners) {
            result.native_event_listener_storage_bytes +=
                listener.name.capacity() + 1U;
        }
    }
    result.native_text_measurement_cache_entry_count =
        native_dom.text_measurement_cache_entry_count;
    result.native_text_measurement_cache_storage_bytes =
        native_dom.text_measurement_cache_storage_bytes;

    result.native_css_rule_count = impl_->css_rules.size();
    result.native_css_rule_storage_bytes =
        impl_->css_rules.capacity() * sizeof(implementation::css_rule);
    for (const auto& [root, cascade] : impl_->inactive_css_cascades) {
        static_cast<void>(root);
        result.native_css_rule_count += cascade.rules.size();
        result.native_css_rule_storage_bytes +=
            cascade.rules.capacity() * sizeof(implementation::css_rule);
    }
    const auto string_bytes = [](const std::string& value) {
        return value.capacity() + 1U;
    };
    for (const auto& [name, keyframes] : impl_->opacity_keyframes) {
        result.native_css_rule_storage_bytes += string_bytes(name)
            + sizeof(decltype(impl_->opacity_keyframes)::value_type)
            + keyframes.opacity_stops.capacity()
                * sizeof(node_style::opacity_keyframe)
            + keyframes.rotation_stops.capacity()
                * sizeof(node_style::rotation_keyframe);
    }
    for (const auto& [root, cascade] : impl_->inactive_css_cascades) {
        static_cast<void>(root);
        for (const auto& [name, keyframes] : cascade.opacity_keyframes) {
            result.native_css_rule_storage_bytes += string_bytes(name)
                + sizeof(decltype(cascade.opacity_keyframes)::value_type)
                + keyframes.opacity_stops.capacity()
                    * sizeof(node_style::opacity_keyframe)
                + keyframes.rotation_stops.capacity()
                    * sizeof(node_style::rotation_keyframe);
        }
    }
    {
        std::lock_guard lock(
            implementation::shared_css_rule_payload_mutex);
        for (auto& [hash, candidates] :
            implementation::shared_css_rule_payloads) {
            (void)hash;
            for (auto iterator = candidates.begin();
                iterator != candidates.end();) {
                auto payload = iterator->lock();
                if (payload == nullptr) {
                    iterator = candidates.erase(iterator);
                    continue;
                }
                ++result.process_shared_css_rule_count;
                result.process_shared_css_rule_storage_bytes +=
                    sizeof(implementation::css_rule_payload)
                    + 2U * sizeof(void*)
                    + string_bytes(payload->selector)
                    + payload->compiled_selector.compounds.capacity()
                        * sizeof(std::string)
                    + payload->compiled_selector.combinators.capacity()
                        * sizeof(char)
                    + payload->compiled_selector.compiled_compounds.capacity()
                        * sizeof(implementation::compiled_css_compound)
                    + payload->declarations.capacity()
                        * sizeof(implementation::css_declaration)
                    + payload->media_queries.capacity() * sizeof(std::string);
                for (const auto& compound :
                    payload->compiled_selector.compounds) {
                    result.process_shared_css_rule_storage_bytes +=
                        string_bytes(compound);
                }
                for (const auto& compound :
                    payload->compiled_selector.compiled_compounds) {
                    result.process_shared_css_rule_storage_bytes +=
                        string_bytes(compound.tag)
                        + compound.identities.capacity()
                            * sizeof(std::pair<char, std::string>)
                        + compound.attributes.capacity() * sizeof(std::string)
                        + compound.pseudos.capacity()
                            * sizeof(implementation::compiled_css_pseudo);
                    for (const auto& [marker, identity] : compound.identities) {
                        static_cast<void>(marker);
                        result.process_shared_css_rule_storage_bytes +=
                            string_bytes(identity);
                    }
                    for (const auto& attribute : compound.attributes) {
                        result.process_shared_css_rule_storage_bytes +=
                            string_bytes(attribute);
                    }
                    for (const auto& pseudo : compound.pseudos) {
                        result.process_shared_css_rule_storage_bytes +=
                            string_bytes(pseudo.name)
                            + string_bytes(pseudo.argument);
                    }
                }
                for (const auto& declaration : payload->declarations) {
                    result.process_shared_css_rule_storage_bytes +=
                        string_bytes(declaration.name)
                        + string_bytes(declaration.value);
                }
                for (const auto& query : payload->media_queries) {
                    result.process_shared_css_rule_storage_bytes +=
                        string_bytes(query);
                }
                ++iterator;
            }
        }
    }
    const auto indexed_rule_storage = [&](const auto& index) {
        uint64_t bytes = index.bucket_count() * sizeof(void*);
        for (const auto& [key, values] : index) {
            bytes += sizeof(typename std::decay_t<decltype(index)>::value_type)
                + 2U * sizeof(void*) + string_bytes(key)
                + values.capacity() * sizeof(size_t);
        }
        return bytes;
    };
    result.native_css_index_storage_bytes =
        indexed_rule_storage(impl_->css_rules_by_class)
        + indexed_rule_storage(impl_->css_rules_by_id)
        + indexed_rule_storage(impl_->css_rules_by_tag)
        + indexed_rule_storage(impl_->css_rules_by_attribute)
        + indexed_rule_storage(impl_->css_rules_by_variable_reference)
        + impl_->css_focus_rules.capacity() * sizeof(size_t)
        + impl_->unindexed_css_rules.capacity() * sizeof(size_t)
        + impl_->hover_selector_dependencies.capacity()
            * sizeof(implementation::hover_selector_dependency);
    result.native_css_index_storage_bytes +=
        impl_->compiled_class_token_lists.bucket_count() * sizeof(void*)
        + impl_->compiled_css_selector_lists.bucket_count() * sizeof(void*);
    for (const auto& [class_name, tokens] : impl_->compiled_class_token_lists) {
        result.native_css_index_storage_bytes +=
            sizeof(decltype(impl_->compiled_class_token_lists)::value_type)
            + 2U * sizeof(void*) + string_bytes(class_name)
            + tokens.capacity() * sizeof(std::string);
        for (const auto& token : tokens) {
            result.native_css_index_storage_bytes += string_bytes(token);
        }
    }
    for (const auto& [source, selector_list] :
        impl_->compiled_css_selector_lists) {
        result.native_css_index_storage_bytes +=
            sizeof(decltype(impl_->compiled_css_selector_lists)::value_type)
            + 2U * sizeof(void*) + string_bytes(source)
            + selector_list.selectors.capacity()
                * sizeof(implementation::compiled_css_selector);
        for (const auto& selector : selector_list.selectors) {
            result.native_css_index_storage_bytes +=
                selector.compounds.capacity() * sizeof(std::string)
                + selector.combinators.capacity() * sizeof(char)
                + selector.compiled_compounds.capacity()
                    * sizeof(implementation::compiled_css_compound);
            for (const auto& compound_source : selector.compounds) {
                result.native_css_index_storage_bytes +=
                    string_bytes(compound_source);
            }
            for (const auto& compound : selector.compiled_compounds) {
                result.native_css_index_storage_bytes +=
                    string_bytes(compound.tag)
                    + compound.identities.capacity()
                        * sizeof(std::pair<char, std::string>)
                    + compound.attributes.capacity() * sizeof(std::string)
                    + compound.pseudos.capacity()
                        * sizeof(implementation::compiled_css_pseudo);
                for (const auto& [marker, identity] : compound.identities) {
                    static_cast<void>(marker);
                    result.native_css_index_storage_bytes += string_bytes(identity);
                }
                for (const auto& attribute : compound.attributes) {
                    result.native_css_index_storage_bytes += string_bytes(attribute);
                }
                for (const auto& pseudo : compound.pseudos) {
                    result.native_css_index_storage_bytes +=
                        string_bytes(pseudo.name) + string_bytes(pseudo.argument);
                }
            }
        }
    }
    for (const auto& [root, cascade] : impl_->inactive_css_cascades) {
        static_cast<void>(root);
        result.native_css_index_storage_bytes +=
            indexed_rule_storage(cascade.rules_by_class)
            + indexed_rule_storage(cascade.rules_by_id)
            + indexed_rule_storage(cascade.rules_by_tag)
            + indexed_rule_storage(cascade.rules_by_attribute)
            + indexed_rule_storage(cascade.rules_by_variable_reference)
            + cascade.focus_rules.capacity() * sizeof(size_t)
            + cascade.unindexed_rules.capacity() * sizeof(size_t)
            + cascade.hover_dependencies.capacity()
                * sizeof(implementation::hover_selector_dependency);
    }
    return result;
}

uint64_t v8_dom_runtime::external_script_source_bytes() const noexcept
{
    return impl_->external_script_source_byte_count->load(
        std::memory_order_relaxed);
}

const std::string& v8_dom_runtime::frame_last_error() const noexcept
{
    return impl_->frame_last_error_value;
}

} // namespace webscene_native
