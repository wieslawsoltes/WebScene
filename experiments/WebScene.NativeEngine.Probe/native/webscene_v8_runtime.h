#pragma once

#include "webscene_native_engine.h"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace webscene_native {

class native_document;

struct document_start_script final {
    std::string source;
    std::string name;
    bool all_frames{true};
};

struct interop_result_data_v3 final {
    uint32_t status{WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3};
    uint32_t root_value_index{0};
    std::vector<webscene_interop_value_v3> values;
    std::vector<webscene_interop_edge_v3> edges;
    std::vector<char> utf8_bytes;
    std::string error;

    void clear()
    {
        status = WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3;
        root_value_index = 0;
        values.clear();
        edges.clear();
        utf8_bytes.clear();
        error.clear();
    }
};

struct interop_invoke_request_data_v3 final {
    uint32_t operation{0};
    uint32_t flags{0};
    uint32_t result_mode{0};
    uint64_t target_handle{0};
    uint32_t arguments_root{0};
    std::string global_name;
    std::string member_name;
    std::vector<webscene_interop_value_v3> values;
    std::vector<webscene_interop_edge_v3> edges;
    std::vector<char> utf8_bytes;
};

struct interop_callback_request_data_v3 final {
    uint64_t target_id{0};
    uint32_t method_id{0};
    uint32_t return_kind{WEBSCENE_INTEROP_CALLBACK_VOID_V3};
    interop_result_data_v3 arguments;
};

struct interop_callback_completion_data_v3 final {
    uint64_t call_id{0};
    bool succeeded{false};
    uint32_t root_value_index{0};
    std::vector<webscene_interop_value_v3> values;
    std::vector<webscene_interop_edge_v3> edges;
    std::vector<char> utf8_bytes;
    std::string error;
};

enum class interop_invoke_state_v3 : uint8_t {
    failed = 0,
    completed = 1,
    pending = 2
};

// Initializes the process-wide V8 platform without allocating a DOM runtime or
// isolate. Applications can call this during startup so the one-time V8 cost is
// outside the first component's load path. Compilation-unit cache behavior remains
// owned by each subsequently created runtime.
void prewarm_v8_process();

class v8_dom_runtime final {
public:
    struct work_metrics final {
        uint64_t timers_scheduled{0};
        uint64_t timers_fired{0};
        uint64_t timers_cancelled{0};
        uint64_t late_timers{0};
        uint64_t total_timer_lateness_nanoseconds{0};
        uint64_t animation_frames_requested{0};
        uint64_t animation_frames_invoked{0};
        uint64_t animation_frames_cancelled{0};
        uint64_t microtask_checkpoints{0};
    };

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
    using interop_callback_sink_v3 =
        std::function<uint64_t(interop_callback_request_data_v3&&)>;

    v8_dom_runtime(
        native_document& document,
        std::function<viewport_metrics()> viewport_provider,
        std::string compilation_cache_directory = {},
        resource_loader load_resource = {},
        std::function<void()> host_request_available = {},
        std::function<void()> interop_callback_available = {},
        interop_callback_sink_v3 interop_callback_sink = {});
    ~v8_dom_runtime();

    v8_dom_runtime(const v8_dom_runtime&) = delete;
    v8_dom_runtime& operator=(const v8_dom_runtime&) = delete;

    bool initialize();
    bool execute(const std::string& source, const std::string& document_name);
    bool load_url(
        const std::string& url,
        std::vector<document_start_script> document_start_scripts = {});
    void set_resource_root(std::string resource_root);
    bool evaluate_interop_v3(
        const std::string& source,
        const std::string& document_name,
        interop_result_data_v3& result);
    using interop_completion_v3 =
        std::function<void(interop_result_data_v3&&)>;

    interop_invoke_state_v3 invoke_interop_v3(
        const interop_invoke_request_data_v3& request,
        interop_result_data_v3& result,
        uint64_t operation_id,
        interop_completion_v3 completion);
    void cancel_interop_v3(uint64_t operation_id);
    bool complete_callback_v3(
        interop_callback_completion_data_v3& completion);
    void cancel_callback_v3(uint64_t call_id);
    uint64_t pending_callback_promises() const noexcept;
    bool try_take_host_request(std::string& request);
    bool try_take_console_message(std::string& message);
    bool dispatch_resize();
    bool refresh_media_environment();
    bool set_visible(bool visible);
    bool dispatch_input(const webscene_input_event& event);
    bool dispatch_transition_events();
    uint32_t current_cursor_kind() const noexcept;
    void notify_low_memory();
    void signal_animation_frame(double timestamp_ms);
    bool pump_animation_frame_task();
    bool has_pending_animation_frame_task() const noexcept;
    uint8_t host_animation_frame_demand() const noexcept;
    bool pump_task();
    bool has_pending_tasks() const noexcept;
    std::chrono::milliseconds recommended_idle_wait(
        std::chrono::milliseconds maximum) const noexcept;
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
    void set_work_metrics_enabled(bool enabled) noexcept;
    work_metrics read_work_metrics() const noexcept;
    uint64_t process_resource_shared_bytes() const noexcept;
    uint64_t external_script_source_bytes() const noexcept;
    uint64_t process_script_source_memory_hits() const noexcept;
    uint64_t process_script_source_shared_bytes() const noexcept;
    uint64_t shared_isolate_slot() const noexcept;
    uint64_t shared_isolate_active_contexts() const noexcept;
    uint64_t shared_isolate_peak_contexts() const noexcept;
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
