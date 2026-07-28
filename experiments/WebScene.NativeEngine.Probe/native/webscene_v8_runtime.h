#pragma once

#include "webscene_native_engine.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace webscene_native {

class native_document;

// Initializes the process-wide V8 platform without allocating a DOM runtime or
// isolate. Applications can call this during startup so the one-time V8 cost is
// outside the first component's load path. Compilation-unit cache behavior remains
// owned by each subsequently created runtime.
void prewarm_v8_process();

class v8_dom_runtime final {
public:
    struct memory_metrics final {
        uint64_t total_heap_bytes{0};
        uint64_t used_heap_bytes{0};
        uint64_t executable_heap_bytes{0};
        uint64_t physical_heap_bytes{0};
        uint64_t external_bytes{0};
        uint64_t malloced_bytes{0};
        uint64_t peak_malloced_bytes{0};
        uint64_t code_and_metadata_bytes{0};
        uint64_t bytecode_and_metadata_bytes{0};
        uint64_t external_script_source_bytes{0};
        uint64_t young_space_used_bytes{0};
        uint64_t young_space_physical_bytes{0};
        uint64_t old_space_used_bytes{0};
        uint64_t old_space_physical_bytes{0};
        uint64_t code_space_used_bytes{0};
        uint64_t code_space_physical_bytes{0};
        uint64_t map_space_used_bytes{0};
        uint64_t map_space_physical_bytes{0};
        uint64_t large_object_space_used_bytes{0};
        uint64_t large_object_space_physical_bytes{0};
        uint64_t read_only_space_used_bytes{0};
        uint64_t read_only_space_physical_bytes{0};
        uint64_t shared_space_used_bytes{0};
        uint64_t shared_space_physical_bytes{0};
        uint64_t trusted_space_used_bytes{0};
        uint64_t trusted_space_physical_bytes{0};
        uint64_t process_compilation_cache_bytes{0};
        uint64_t process_compilation_mapped_cache_bytes{0};
        uint64_t process_resource_cache_bytes{0};
        uint64_t process_resource_mapped_cache_bytes{0};
        uint64_t native_dom_node_count{0};
        uint64_t native_dom_node_size_bytes{0};
        uint64_t native_dom_inline_bytes{0};
        uint64_t native_dom_node_pool_reserved_bytes{0};
        uint64_t native_dom_node_pool_peak_bytes{0};
        uint64_t native_dom_table_layout_count{0};
        uint64_t native_dom_table_layout_storage_bytes{0};
        uint64_t native_dom_form_control_count{0};
        uint64_t native_dom_form_control_storage_bytes{0};
        uint64_t native_event_listener_count{0};
        uint64_t native_event_listener_storage_bytes{0};
        uint64_t native_dom_attribute_node_count{0};
        uint64_t native_dom_attribute_entry_count{0};
        uint64_t native_dom_attribute_storage_bytes{0};
        uint64_t native_dom_pseudo_storage_bytes{0};
        uint64_t native_dom_animation_count{0};
        uint64_t native_dom_animation_storage_bytes{0};
        uint64_t native_dom_custom_property_node_count{0};
        uint64_t native_dom_custom_property_entry_count{0};
        uint64_t native_dom_custom_property_storage_bytes{0};
        uint64_t native_dom_background_image_count{0};
        uint64_t native_dom_background_image_storage_bytes{0};
        uint64_t native_dom_grid_count{0};
        uint64_t native_dom_grid_storage_bytes{0};
        uint64_t native_dom_textual_style_count{0};
        uint64_t native_dom_textual_style_storage_bytes{0};
        uint64_t native_dom_authored_style_node_count{0};
        uint64_t native_dom_authored_style_entry_count{0};
        uint64_t native_dom_authored_style_storage_bytes{0};
        uint64_t native_css_rule_count{0};
        uint64_t native_css_rule_storage_bytes{0};
        uint64_t native_css_index_storage_bytes{0};
        uint64_t process_shared_css_rule_count{0};
        uint64_t process_shared_css_rule_storage_bytes{0};
        uint64_t native_dom_canvas_node_count{0};
        uint64_t native_dom_canvas_storage_bytes{0};
        uint64_t native_wrapper_handle_count{0};
        uint64_t native_wrapper_storage_bytes{0};
        uint64_t native_text_measurement_cache_entry_count{0};
        uint64_t native_text_measurement_cache_storage_bytes{0};
    };

    struct viewport_metrics final {
        float width{1};
        float height{1};
        double device_scale_factor{1};
        uint32_t preferred_color_scheme{WEBSCENE_PREFERRED_COLOR_SCHEME_LIGHT};
    };

    struct resource_response final {
        std::string content;
        std::string entity_tag;
        int64_t last_modified_unix_seconds{0};
        int64_t fresh_until_unix_seconds{0};
        bool cacheable{true};
        bool not_modified{false};
    };

    using resource_loader = std::function<bool(
        uint32_t kind,
        const std::string& url,
        const std::string& entity_tag,
        int64_t last_modified_unix_seconds,
        resource_response& response)>;

    v8_dom_runtime(
        native_document& document,
        std::function<viewport_metrics()> viewport_provider,
        std::string compilation_cache_directory = {},
        resource_loader load_resource = {});
    ~v8_dom_runtime();

    v8_dom_runtime(const v8_dom_runtime&) = delete;
    v8_dom_runtime& operator=(const v8_dom_runtime&) = delete;

    bool initialize();
    bool execute(const std::string& source, const std::string& document_name);
    bool load_url(const std::string& url);
    void set_resource_root(std::string resource_root);
    bool evaluate_json(
        const std::string& source,
        const std::string& document_name,
        std::string& result);
    bool try_take_host_request(std::string& request);
    bool try_take_console_message(std::string& message);
    bool dispatch_resize();
    bool refresh_media_environment();
    bool dispatch_input(const webscene_input_event& event);
    bool dispatch_transition_events();
    uint32_t current_cursor_kind() const noexcept;
    void notify_low_memory();
    void signal_animation_frame(double timestamp_ms);
    bool pump_animation_frame_task();
    bool has_pending_animation_frame_task() const noexcept;
    bool pump_task();
    bool has_pending_tasks() const noexcept;
    bool component_ready();
    std::string diagnostics();
    std::string event_diagnostics() const;
    std::string feature_use_json() const;
    std::string event_listener_inventory_json() const;
    const std::string& last_error() const noexcept;
    uint64_t frame_scripts_executed() const noexcept;
    uint64_t frame_script_errors() const noexcept;
    uint64_t compilation_requests() const noexcept;
    uint64_t compilation_memory_hits() const noexcept;
    uint64_t compilation_persistent_hits() const noexcept;
    uint64_t compilation_persistent_misses() const noexcept;
    uint64_t compilation_cache_rejections() const noexcept;
    uint64_t compilation_cache_bytes_read() const noexcept;
    uint64_t compilation_cache_bytes_written() const noexcept;
    uint64_t compilation_time_nanoseconds() const noexcept;
    uint64_t process_compilation_memory_hits() const noexcept;
    uint64_t process_compilation_leaders() const noexcept;
    uint64_t process_compilation_waiters() const noexcept;
    uint64_t process_compilation_shared_bytes() const noexcept;
    uint64_t process_resource_memory_hits() const noexcept;
    uint64_t process_resource_load_leaders() const noexcept;
    uint64_t process_resource_load_waiters() const noexcept;
    uint64_t process_resource_shared_bytes() const noexcept;
    uint64_t process_script_source_memory_hits() const noexcept;
    uint64_t process_script_source_shared_bytes() const noexcept;
    uint64_t resource_cache_requests() const noexcept;
    uint64_t resource_cache_hits() const noexcept;
    uint64_t resource_cache_misses() const noexcept;
    uint64_t resource_cache_rejections() const noexcept;
    uint64_t resource_cache_bytes_read() const noexcept;
    uint64_t resource_cache_bytes_written() const noexcept;
    uint64_t input_events_dispatched() const noexcept;
    uint64_t input_callbacks_invoked() const noexcept;
    uint64_t last_resize_outer_listeners_nanoseconds() const noexcept;
    uint64_t last_resize_frame_listeners_nanoseconds() const noexcept;
    uint64_t last_resize_layout_nanoseconds() const noexcept;
    uint64_t last_resize_observers_nanoseconds() const noexcept;
    memory_metrics read_memory_metrics() const noexcept;
    const std::string& frame_last_error() const noexcept;

private:
    struct implementation;
    std::unique_ptr<implementation> impl_;
};

} // namespace webscene_native
