#include "webscene_native_engine.h"
#include "webscene_native_dom.h"
#include "webscene_v8_runtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace {

constexpr uint32_t input_capacity = 8192;
constexpr uint32_t minimum_command_count = 64;
constexpr uint32_t scene_flag_checkpoint = 1U;
constexpr uint32_t scene_flag_dom_replacement = 2U;
constexpr uint32_t scene_flag_component_ready = 4U;
constexpr uint32_t canvas_layer_flag_replace = 1U;
constexpr uint32_t canvas_layer_flag_remove = 2U;

static_assert(sizeof(webscene_input_event) == 48);
static_assert(sizeof(webscene_scene_command) == 52);
static_assert(sizeof(webscene_scene_header) == 56);
static_assert(sizeof(webscene_canvas_layout) == 32);
static_assert(sizeof(webscene_canvas_layer) == 64);
static_assert(sizeof(webscene_canvas_command) == 80);
static_assert(sizeof(webscene_scene_string) == 8);
static_assert(sizeof(webscene_damage_rect) == 16);
static_assert(sizeof(webscene_scene_view) == 136);
static_assert(sizeof(webscene_interop_value_v3) == 24);
static_assert(sizeof(webscene_interop_edge_v3) == 16);
static_assert(sizeof(webscene_interop_evaluate_request_v3) == 48);
static_assert(sizeof(webscene_interop_invoke_request_v3) == 112);
static_assert(sizeof(webscene_interop_result_view_v3) == 96);
static_assert(sizeof(webscene_interop_callback_view_v3) == 88);
static_assert(sizeof(webscene_interop_callback_completion_v3) == 96);
static_assert(sizeof(webscene_interop_pool_metrics_v3) == 200);
static_assert(sizeof(webscene_runtime_work_metrics) == 168);

class input_ring final {
public:
    bool try_push(const webscene_input_event& value)
    {
        const auto write = write_.load(std::memory_order_relaxed);
        const auto next = increment(write);
        if (next == read_.load(std::memory_order_acquire)) {
            return false;
        }

        values_[write] = value;
        write_.store(next, std::memory_order_release);
        return true;
    }

    bool try_pop(webscene_input_event& value)
    {
        const auto read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire)) {
            return false;
        }

        value = values_[read];
        read_.store(increment(read), std::memory_order_release);
        return true;
    }

    bool empty() const
    {
        return read_.load(std::memory_order_acquire)
            == write_.load(std::memory_order_acquire);
    }

private:
    static constexpr uint32_t increment(uint32_t value)
    {
        return (value + 1U) % input_capacity;
    }

    std::array<webscene_input_event, input_capacity> values_{};
    alignas(64) std::atomic<uint32_t> write_{0};
    alignas(64) std::atomic<uint32_t> read_{0};
};

struct canvas_layer_version final {
    uint64_t generation{0};
    uint64_t content_hash{0};
    uint32_t command_count{0};
    uint32_t string_count{0};
    float x{0};
    float y{0};
    float width{0};
    float height{0};

    bool visually_equals(const canvas_layer_version& other) const noexcept
    {
        return content_hash == other.content_hash
            && command_count == other.command_count
            && string_count == other.string_count
            && x == other.x
            && y == other.y
            && width == other.width
            && height == other.height;
    }
};

struct scene final {
    webscene_scene_header header{};
    std::vector<webscene_scene_command> commands;
    std::vector<webscene_canvas_layer> canvas_layers;
    std::vector<webscene_canvas_command> canvas_commands;
    std::vector<webscene_scene_string> canvas_strings;
    std::vector<char> canvas_string_bytes;
    std::vector<webscene_damage_rect> damage_rects;
    std::unordered_map<uint32_t, canvas_layer_version> full_layer_versions;
    uint64_t dom_hash{0};
    uint64_t published_timestamp_nanoseconds{0};
};

uint64_t retained_scene_bytes(const scene& value)
{
    return sizeof(scene)
        + value.commands.capacity() * sizeof(webscene_scene_command)
        + value.canvas_layers.capacity() * sizeof(webscene_canvas_layer)
        + value.canvas_commands.capacity() * sizeof(webscene_canvas_command)
        + value.canvas_strings.capacity() * sizeof(webscene_scene_string)
        + value.canvas_string_bytes.capacity() * sizeof(char)
        + value.damage_rects.capacity() * sizeof(webscene_damage_rect)
        + value.full_layer_versions.size()
            * (sizeof(uint32_t) + sizeof(canvas_layer_version) + sizeof(void*) * 2U);
}

struct acknowledgement_state final {
    std::mutex mutex;
    uint64_t revision{0};
    uint64_t dom_hash{0};
    float viewport_width{0};
    float viewport_height{0};
    std::unordered_map<uint32_t, canvas_layer_version> layer_versions;
    std::shared_ptr<const scene> value;
    std::deque<std::shared_ptr<const scene>> pending_scenes;
    std::atomic<uint64_t> acknowledged_scenes{0};
    std::atomic<uint64_t> total_acknowledgement_nanoseconds{0};
    std::atomic<uint64_t> last_acknowledgement_nanoseconds{0};
    std::atomic<uint64_t> maximum_acknowledgement_nanoseconds{0};
};

struct script_request final {
    std::string source;
    std::string document_name;
};

struct url_request final {
    std::string url;
};

#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
struct inspector_output_state final {
    static constexpr size_t maximum_messages = 1024U;
    static constexpr size_t maximum_bytes = 16U * 1024U * 1024U;

    webscene_inspector_message_available_callback_v3 callback{nullptr};
    void* user_data{nullptr};
    std::mutex mutex;
    std::deque<std::string> messages;
    size_t queued_bytes{0U};
    bool overflowed{false};

    void publish(uint64_t session_id, std::string_view message)
    {
        auto notify = false;
        {
            std::lock_guard lock(mutex);
            if (overflowed) return;
            if (message.size() > maximum_bytes
                || messages.size() >= maximum_messages
                || queued_bytes > maximum_bytes - message.size()) {
                messages.clear();
                queued_bytes = 0U;
                overflowed = true;
                notify = true;
            } else {
                messages.emplace_back(message);
                queued_bytes += message.size();
                notify = true;
            }
        }
        if (notify && callback != nullptr) {
            try {
                callback(user_data, session_id);
            } catch (...) {
            }
        }
    }

    size_t take(char* destination, size_t destination_capacity)
    {
        std::lock_guard lock(mutex);
        if (overflowed) return SIZE_MAX;
        if (messages.empty()) return 0U;
        const auto required = messages.front().size();
        if (destination == nullptr || destination_capacity < required) {
            return required;
        }
        std::memcpy(destination, messages.front().data(), required);
        messages.pop_front();
        queued_bytes -= required;
        return required;
    }
};
#endif

// These responsibility-focused fragments intentionally remain one translation
// unit so the refactor cannot alter inlining or production code generation.
#include "webscene_native_engine_interop_types.inc"
#include "webscene_native_engine_scene_utils.inc"
} // namespace

struct webscene_engine final {
#include "webscene_native_engine_lifecycle.inc"
#include "webscene_native_engine_interop_api.inc"
#include "webscene_native_engine_diagnostics.inc"
#include "webscene_native_engine_metrics.inc"
private:
#include "webscene_native_engine_interop_work.inc"
#include "webscene_native_engine_worker.inc"
#include "webscene_native_engine_input.inc"
#include "webscene_native_engine_metric_updates.inc"
#include "webscene_native_engine_scene.inc"
#include "webscene_native_engine_errors.inc"
    uint32_t command_count_;
    std::string compilation_cache_directory_;
    webscene_resource_load_callback resource_load_callback_{nullptr};
    void* resource_load_user_data_{nullptr};
    webscene_scene_published_callback scene_published_callback_{nullptr};
    void* scene_published_user_data_{nullptr};
    webscene_host_request_available_callback
        host_request_available_callback_{nullptr};
    void* host_request_available_user_data_{nullptr};
    webscene_interop_callback_available_callback
        interop_callback_available_callback_{nullptr};
    void* interop_callback_available_user_data_{nullptr};
    webscene_animation_frame_requested_callback
        animation_frame_requested_callback_{nullptr};
    void* animation_frame_requested_user_data_{nullptr};
    mutable std::mutex configuration_mutex_;
    std::string resource_root_;
    input_ring inputs_;
    webscene_input_event pending_resize_{};
    uint64_t pending_resize_count_{0};
    webscene_input_event pending_resize_frame_{};
    uint64_t pending_resize_frame_count_{0};
    bool pending_resize_has_frame_{false};
    std::chrono::steady_clock::time_point pending_resize_frame_enqueued_at_{};
    std::mutex resize_mutex_;
    std::atomic<bool> resize_pending_{false};
    std::atomic<bool> low_memory_requested_{false};
    std::atomic<bool> host_visible_{true};
    std::atomic<bool> visibility_changed_{false};
    std::atomic<uint32_t> preferred_color_scheme_{
        WEBSCENE_PREFERRED_COLOR_SCHEME_LIGHT};
    std::atomic<bool> preferred_color_scheme_changed_{false};
    std::atomic<uint8_t> host_animation_frame_requested_{0U};
    webscene_native::native_document document_;
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
    std::unique_ptr<webscene_native::v8_dom_runtime> runtime_;
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
    std::atomic<webscene_native::v8_dom_runtime*> inspector_runtime_{nullptr};
    std::mutex inspector_output_mutex_;
    std::unordered_map<uint64_t, std::shared_ptr<inspector_output_state>>
        inspector_outputs_;
#endif
#endif
    std::deque<script_work_request> script_work_;
    std::mutex script_mutex_;
    std::shared_ptr<interop_result_pool_v3> interop_result_pool_{
        std::make_shared<interop_result_pool_v3>()};
    std::shared_ptr<interop_callback_pool_v3> interop_callback_pool_{
        std::make_shared<interop_callback_pool_v3>()};
    interop_callback_completion_pool_v3
        interop_callback_completion_pool_;
    mutable std::mutex interop_callback_mutex_;
    std::deque<std::unique_ptr<interop_callback_lease_v3>>
        queued_interop_callbacks_;
    std::unordered_set<uint64_t> pending_interop_callback_promises_;
    size_t interop_callback_queue_high_water_{0};
    std::atomic<uint64_t> next_interop_callback_id_{1U};
    mutable std::mutex interop_mutex_;
    std::unordered_map<uint64_t, std::shared_ptr<interop_operation_v3>>
        interop_operations_;
    std::vector<std::shared_ptr<interop_operation_v3>>
        interop_operation_slots_;
    std::vector<size_t> available_interop_operation_slots_;
    size_t interop_operation_slot_high_water_{0};
    mutable std::mutex interop_request_pool_mutex_;
    std::vector<webscene_native::interop_invoke_request_data_v3>
        available_interop_requests_;
    std::atomic<uint64_t> interop_request_pool_hits_{0};
    std::atomic<uint64_t> interop_request_pool_misses_{0};
    std::atomic<uint64_t> interop_request_oversize_allocations_{0};
    std::atomic<uint64_t> next_interop_operation_id_{1U};
    std::shared_ptr<const scene> latest_{};
    std::atomic<bool> ordered_scene_consumer_{false};
    std::condition_variable wake_;
    std::mutex wake_mutex_;
    bool wake_pending_{false};
    uint64_t next_revision_{1};
    uint64_t last_input_sequence_{0};
    double viewport_width_{1000};
    double viewport_height_{616};
    double device_scale_factor_{1};
    double pointer_x_{500};
    double pointer_y_{308};
    double scroll_x_{0};
    double scroll_y_{0};
    std::atomic<uint64_t> enqueued_inputs_{0};
    std::atomic<uint64_t> dropped_inputs_{0};
    std::atomic<uint64_t> consumed_inputs_{0};
    std::atomic<uint64_t> published_scenes_{0};
    std::atomic<uint64_t> acquired_scenes_{0};
    std::atomic<uint64_t> executed_scripts_{0};
    std::atomic<uint64_t> script_errors_{0};
    std::atomic<uint64_t> dom_nodes_{0};
    std::atomic<uint64_t> layout_passes_{0};
    std::atomic<uint64_t> iframe_nodes_{0};
    std::atomic<uint64_t> iframe_html_bytes_{0};
    std::atomic<uint64_t> frame_scripts_executed_{0};
    std::atomic<uint64_t> frame_script_errors_{0};
    std::atomic<uint64_t> canvas_nodes_{0};
    std::atomic<uint64_t> component_ready_{0};
    std::atomic<uint64_t> low_memory_notifications_{0};
    std::atomic<uint64_t> hidden_low_memory_notifications_{0};
    std::atomic<uint64_t> compilation_requests_{0};
    std::atomic<uint64_t> compilation_memory_hits_{0};
    std::atomic<uint64_t> compilation_persistent_hits_{0};
    std::atomic<uint64_t> compilation_persistent_misses_{0};
    std::atomic<uint64_t> compilation_cache_rejections_{0};
    std::atomic<uint64_t> compilation_cache_bytes_read_{0};
    std::atomic<uint64_t> compilation_cache_bytes_written_{0};
    std::atomic<uint64_t> compilation_time_nanoseconds_{0};
    std::atomic<uint64_t> process_compilation_memory_hits_{0};
    std::atomic<uint64_t> process_compilation_leaders_{0};
    std::atomic<uint64_t> process_compilation_waiters_{0};
    std::atomic<uint64_t> process_compilation_shared_bytes_{0};
    std::atomic<uint64_t> process_resource_memory_hits_{0};
    std::atomic<uint64_t> process_resource_load_leaders_{0};
    std::atomic<uint64_t> process_resource_load_waiters_{0};
    std::atomic<uint64_t> process_resource_shared_bytes_{0};
    std::atomic<uint64_t> process_script_source_memory_hits_{0};
    std::atomic<uint64_t> process_script_source_shared_bytes_{0};
    std::atomic<uint64_t> shared_isolate_slot_{
        std::numeric_limits<uint64_t>::max()};
    std::atomic<uint64_t> shared_isolate_active_contexts_{1};
    std::atomic<uint64_t> shared_isolate_peak_contexts_{1};
    std::atomic<uint64_t> v8_total_heap_bytes_{0};
    std::atomic<uint64_t> v8_used_heap_bytes_{0};
    std::atomic<uint64_t> v8_executable_heap_bytes_{0};
    std::atomic<uint64_t> v8_physical_heap_bytes_{0};
    std::atomic<uint64_t> v8_external_bytes_{0};
    std::atomic<uint64_t> v8_malloced_bytes_{0};
    std::atomic<uint64_t> v8_peak_malloced_bytes_{0};
    std::atomic<uint64_t> v8_code_and_metadata_bytes_{0};
    std::atomic<uint64_t> v8_bytecode_and_metadata_bytes_{0};
    std::atomic<uint64_t> v8_external_script_source_bytes_{0};
    std::atomic<uint64_t> v8_young_space_used_bytes_{0};
    std::atomic<uint64_t> v8_young_space_physical_bytes_{0};
    std::atomic<uint64_t> v8_old_space_used_bytes_{0};
    std::atomic<uint64_t> v8_old_space_physical_bytes_{0};
    std::atomic<uint64_t> v8_code_space_used_bytes_{0};
    std::atomic<uint64_t> v8_code_space_physical_bytes_{0};
    std::atomic<uint64_t> v8_map_space_used_bytes_{0};
    std::atomic<uint64_t> v8_map_space_physical_bytes_{0};
    std::atomic<uint64_t> v8_large_object_space_used_bytes_{0};
    std::atomic<uint64_t> v8_large_object_space_physical_bytes_{0};
    std::atomic<uint64_t> v8_read_only_space_used_bytes_{0};
    std::atomic<uint64_t> v8_read_only_space_physical_bytes_{0};
    std::atomic<uint64_t> v8_shared_space_used_bytes_{0};
    std::atomic<uint64_t> v8_shared_space_physical_bytes_{0};
    std::atomic<uint64_t> v8_trusted_space_used_bytes_{0};
    std::atomic<uint64_t> v8_trusted_space_physical_bytes_{0};
    std::atomic<uint64_t> latest_scene_bytes_{0};
    std::atomic<uint64_t> process_compilation_cache_bytes_{0};
    std::atomic<uint64_t> process_compilation_mapped_cache_bytes_{0};
    std::atomic<uint64_t> process_resource_cache_bytes_{0};
    std::atomic<uint64_t> process_resource_mapped_cache_bytes_{0};
    std::atomic<uint64_t> native_dom_node_count_{0};
    std::atomic<uint64_t> native_dom_node_size_bytes_{0};
    std::atomic<uint64_t> native_dom_inline_bytes_{0};
    std::atomic<uint64_t> native_dom_node_pool_reserved_bytes_{0};
    std::atomic<uint64_t> native_dom_node_pool_peak_bytes_{0};
    std::atomic<uint64_t> native_dom_table_layout_count_{0};
    std::atomic<uint64_t> native_dom_table_layout_storage_bytes_{0};
    std::atomic<uint64_t> native_dom_form_control_count_{0};
    std::atomic<uint64_t> native_dom_form_control_storage_bytes_{0};
    std::atomic<uint64_t> native_event_listener_count_{0};
    std::atomic<uint64_t> native_event_listener_storage_bytes_{0};
    std::atomic<uint64_t> native_dom_attribute_node_count_{0};
    std::atomic<uint64_t> native_dom_attribute_entry_count_{0};
    std::atomic<uint64_t> native_dom_attribute_storage_bytes_{0};
    std::atomic<uint64_t> native_wrapper_handle_count_{0};
    std::atomic<uint64_t> native_wrapper_storage_bytes_{0};
    std::atomic<uint64_t> native_text_measurement_cache_entry_count_{0};
    std::atomic<uint64_t> native_text_measurement_cache_storage_bytes_{0};
    std::atomic<uint64_t> native_dom_pseudo_storage_bytes_{0};
    std::atomic<uint64_t> native_dom_canvas_node_count_{0};
    std::atomic<uint64_t> native_dom_canvas_storage_bytes_{0};
    std::atomic<uint64_t> native_dom_animation_count_{0};
    std::atomic<uint64_t> native_dom_animation_storage_bytes_{0};
    std::atomic<uint64_t> native_dom_custom_property_node_count_{0};
    std::atomic<uint64_t> native_dom_custom_property_entry_count_{0};
    std::atomic<uint64_t> native_dom_custom_property_storage_bytes_{0};
    std::atomic<uint64_t> native_dom_background_image_count_{0};
    std::atomic<uint64_t> native_dom_background_image_storage_bytes_{0};
    std::atomic<uint64_t> native_dom_grid_count_{0};
    std::atomic<uint64_t> native_dom_grid_storage_bytes_{0};
    std::atomic<uint64_t> native_dom_textual_style_count_{0};
    std::atomic<uint64_t> native_dom_textual_style_storage_bytes_{0};
    std::atomic<uint64_t> native_dom_authored_style_node_count_{0};
    std::atomic<uint64_t> native_dom_authored_style_entry_count_{0};
    std::atomic<uint64_t> native_dom_authored_style_storage_bytes_{0};
    std::atomic<uint64_t> native_css_rule_count_{0};
    std::atomic<uint64_t> native_css_rule_storage_bytes_{0};
    std::atomic<uint64_t> native_css_index_storage_bytes_{0};
    std::atomic<uint64_t> process_shared_css_rule_count_{0};
    std::atomic<uint64_t> process_shared_css_rule_storage_bytes_{0};
    std::chrono::steady_clock::time_point last_memory_metrics_update_{};
    std::atomic<uint64_t> resource_cache_requests_{0};
    std::atomic<uint64_t> resource_cache_hits_{0};
    std::atomic<uint64_t> resource_cache_misses_{0};
    std::atomic<uint64_t> resource_cache_rejections_{0};
    std::atomic<uint64_t> resource_cache_bytes_read_{0};
    std::atomic<uint64_t> resource_cache_bytes_written_{0};
    std::atomic<uint64_t> input_events_dispatched_{0};
    std::atomic<uint64_t> input_callbacks_invoked_{0};
    std::atomic<uint64_t> busiest_canvas_width_milli_{0};
    std::atomic<uint64_t> busiest_canvas_height_milli_{0};
    std::atomic<uint64_t> coalesced_resize_inputs_{0};
    std::atomic<uint64_t> applied_resize_inputs_{0};
    std::atomic<uint64_t> last_resize_dispatch_nanoseconds_{0};
    std::atomic<uint64_t> last_scene_publication_nanoseconds_{0};
    std::atomic<uint64_t> last_resize_outer_listeners_nanoseconds_{0};
    std::atomic<uint64_t> last_resize_frame_listeners_nanoseconds_{0};
    std::atomic<uint64_t> last_resize_layout_nanoseconds_{0};
    std::atomic<uint64_t> last_resize_observers_nanoseconds_{0};
    std::atomic<uint64_t> coalesced_pointer_move_inputs_{0};
    std::atomic<uint64_t> coalesced_wheel_inputs_{0};
    std::atomic<uint64_t> applied_pointer_move_inputs_{0};
    std::atomic<uint64_t> applied_wheel_inputs_{0};
    std::atomic<uint64_t> applied_animation_frames_{0};
    std::atomic<uint64_t> coalesced_animation_frames_{0};
    std::atomic<uint64_t> submitted_resize_frame_pairs_{0};
    std::atomic<uint64_t> applied_resize_frame_pairs_{0};
    std::atomic<uint64_t> published_resize_frame_pairs_{0};
    std::atomic<uint64_t> total_resize_frame_queue_nanoseconds_{0};
    std::atomic<uint64_t> last_resize_frame_queue_nanoseconds_{0};
    std::atomic<uint64_t> maximum_resize_frame_queue_nanoseconds_{0};
    std::atomic<uint64_t> total_resize_frame_dispatch_nanoseconds_{0};
    std::atomic<uint64_t> last_resize_frame_dispatch_nanoseconds_{0};
    std::atomic<uint64_t> maximum_resize_frame_dispatch_nanoseconds_{0};
    std::atomic<uint64_t> resize_frame_animation_callbacks_{0};
    std::atomic<uint64_t> total_resize_frame_animation_batch_nanoseconds_{0};
    std::atomic<uint64_t> last_resize_frame_animation_batch_nanoseconds_{0};
    std::atomic<uint64_t> maximum_resize_frame_animation_batch_nanoseconds_{0};
    std::atomic<uint64_t> total_resize_frame_to_publication_nanoseconds_{0};
    std::atomic<uint64_t> last_resize_frame_to_publication_nanoseconds_{0};
    std::atomic<uint64_t> maximum_resize_frame_to_publication_nanoseconds_{0};
    std::atomic<uint64_t> last_animation_advance_nanoseconds_{0};
    std::atomic<uint64_t> last_layout_nanoseconds_{0};
    std::atomic<uint64_t> last_scene_build_nanoseconds_{0};
    std::atomic<uint64_t> maximum_scene_publication_nanoseconds_{0};
    std::atomic<uint64_t> scene_publication_attempts_{0};
    std::atomic<uint64_t> blocked_scene_publications_{0};
    std::atomic<uint64_t> last_input_dispatch_nanoseconds_{0};
    std::atomic<uint64_t> maximum_input_dispatch_nanoseconds_{0};
    std::atomic<uint64_t> last_input_dispatch_sequence_{0};
    std::atomic<uint64_t> dispatched_inputs_{0};
    std::atomic<uint64_t> total_input_dispatch_nanoseconds_{0};
    std::atomic<uint64_t> dispatched_animation_frames_{0};
    std::atomic<uint64_t> total_animation_frame_dispatch_nanoseconds_{0};
    std::atomic<uint64_t> last_animation_frame_dispatch_nanoseconds_{0};
    std::atomic<uint64_t> maximum_animation_frame_dispatch_nanoseconds_{0};
    std::atomic<uint64_t> last_animation_frame_timestamp_microseconds_{0};
    std::atomic<uint64_t> timers_scheduled_{0};
    std::atomic<uint64_t> timers_fired_{0};
    std::atomic<uint64_t> timers_cancelled_{0};
    std::atomic<uint64_t> late_timers_{0};
    std::atomic<uint64_t> total_timer_lateness_nanoseconds_{0};
    std::atomic<uint64_t> animation_frames_requested_{0};
    std::atomic<uint64_t> animation_frames_invoked_{0};
    std::atomic<uint64_t> animation_frames_cancelled_{0};
    std::atomic<uint64_t> microtask_checkpoints_{0};
    std::atomic<uint64_t> worker_waits_{0};
    std::atomic<uint64_t> worker_signalled_wakes_{0};
    std::atomic<uint64_t> worker_timeout_wakes_{0};
    std::atomic<uint64_t> scene_builds_{0};
    std::atomic<uint64_t> no_damage_scene_builds_{0};
    std::atomic<uint64_t> full_checkpoint_scene_builds_{0};
    std::atomic<uint64_t> arbitrary_evaluation_calls_{0};
    std::atomic<uint64_t> generated_invoke_calls_{0};
    std::atomic<uint64_t> generated_callback_calls_{0};
    std::atomic<uint64_t> arbitrary_evaluation_source_bytes_{0};
    std::atomic<uint64_t> generated_request_bytes_{0};
    std::atomic<bool> runtime_work_metrics_enabled_{false};
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
    webscene_native::v8_dom_runtime::work_metrics
        last_runtime_work_metrics_{};
#endif
    std::atomic<uint32_t> current_cursor_{WEBSCENE_CURSOR_DEFAULT};
    std::atomic<bool> checkpoint_requested_{false};
    mutable std::mutex iframe_html_mutex_;
    std::string iframe_html_;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    mutable std::mutex scene_diagnostics_mutex_;
    std::string scene_diagnostics_;
    bool scene_diagnostics_initialized_{false};
    std::chrono::steady_clock::time_point last_scene_diagnostics_update_{};
    std::string runtime_diagnostics_;
#endif
    mutable std::mutex canvas_layout_mutex_;
    std::vector<webscene_canvas_layout> canvas_layouts_;
    mutable std::mutex error_mutex_;
    std::string last_error_;
    mutable std::mutex host_request_mutex_;
    std::string pending_host_request_;
    mutable std::mutex console_message_mutex_;
    std::string pending_console_message_;
    mutable std::mutex input_dispatch_failure_mutex_;
    std::deque<std::string> input_dispatch_failures_;
    bool native_scene_active_{false};
    std::shared_ptr<acknowledgement_state> acknowledgement_{
        std::make_shared<acknowledgement_state>()};
    // Keep the worker last: every field it can observe is fully initialized before
    // the thread starts, and jthread joins before those fields are destroyed.
    std::jthread worker_;
};

struct webscene_scene_lease final {
    std::shared_ptr<const scene> value;
    std::shared_ptr<acknowledgement_state> acknowledgement;
    webscene_scene_view view{};

    webscene_scene_lease(
        std::shared_ptr<const scene> scene_value,
        std::shared_ptr<acknowledgement_state> acknowledgement_value)
        : value(std::move(scene_value))
        , acknowledgement(std::move(acknowledgement_value))
    {
        view = webscene_scene_view{
            static_cast<uint32_t>(sizeof(webscene_scene_view)),
            2U,
            value->header,
            value->commands.data(),
            value->canvas_layers.data(),
            value->canvas_commands.data(),
            value->canvas_strings.data(),
            value->canvas_string_bytes.data(),
            value->damage_rects.data(),
            this,
            static_cast<uint32_t>(value->canvas_commands.size()),
            static_cast<uint32_t>(value->canvas_strings.size()),
            static_cast<uint32_t>(value->canvas_string_bytes.size()),
            0U};
    }

    bool acknowledge()
    {
        std::lock_guard lock(acknowledgement->mutex);
        const auto is_checkpoint = (value->header.flags & scene_flag_checkpoint) != 0U;
        if (value->header.revision <= acknowledgement->revision
            || (!is_checkpoint && value->header.base_revision != acknowledgement->revision)) {
            return value->header.revision == acknowledgement->revision;
        }
        if (acknowledgement->pending_scenes.empty()
            || acknowledgement->pending_scenes.front()->header.revision
                != value->header.revision) {
            return false;
        }
        acknowledgement->revision = value->header.revision;
        acknowledgement->dom_hash = value->dom_hash;
        acknowledgement->viewport_width = value->header.viewport_width;
        acknowledgement->viewport_height = value->header.viewport_height;
        acknowledgement->layer_versions = value->full_layer_versions;
        // Diff scenes with unchanged DOM intentionally carry no DOM commands.
        // Preserve the last replacement snapshot as the comparison base for a
        // later transform/style-only DOM change.
        if ((value->header.flags & scene_flag_dom_replacement) != 0U) {
            acknowledgement->value = value;
        }
        if (value->published_timestamp_nanoseconds != 0U) {
            const auto now_nanoseconds = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            const auto elapsed = now_nanoseconds
                >= value->published_timestamp_nanoseconds
                ? now_nanoseconds - value->published_timestamp_nanoseconds
                : 0U;
            acknowledgement->acknowledged_scenes.fetch_add(
                1,
                std::memory_order_relaxed);
            acknowledgement->total_acknowledgement_nanoseconds.fetch_add(
                elapsed,
                std::memory_order_relaxed);
            acknowledgement->last_acknowledgement_nanoseconds.store(
                elapsed,
                std::memory_order_relaxed);
            store_maximum(
                acknowledgement->maximum_acknowledgement_nanoseconds,
                elapsed);
        }
        acknowledgement->pending_scenes.pop_front();
        return true;
    }
};

extern "C" {

uint32_t webscene_engine_get_abi_version(void)
{
    return 3U;
}

uint32_t webscene_engine_get_build_features(void)
{
    uint32_t features = 0U;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    features |= WEBSCENE_ENGINE_BUILD_FEATURE_CERTIFICATION;
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
    features |= WEBSCENE_ENGINE_BUILD_FEATURE_V8_INSPECTOR;
#endif
    return features;
}

uint8_t webscene_engine_prewarm(void)
{
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
    try {
        webscene_native::prewarm_v8_process();
        return 1U;
    } catch (...) {
        return 0U;
    }
#else
    return 1U;
#endif
}

webscene_engine* webscene_engine_create(uint32_t simulated_chart_command_count)
{
    try {
        return new webscene_engine(simulated_chart_command_count);
    } catch (...) {
        return nullptr;
    }
}

webscene_engine* webscene_engine_create_with_options(const webscene_engine_options* options)
{
    constexpr auto legacy_options_size = offsetof(webscene_engine_options, resource_load_callback);
    if (options == nullptr || options->struct_size < legacy_options_size) {
        return nullptr;
    }
    try {
        std::string cache_directory;
        if (options->compilation_cache_directory != nullptr
            && options->compilation_cache_directory_length > 0U) {
            cache_directory.assign(
                options->compilation_cache_directory,
                options->compilation_cache_directory_length);
        }
        constexpr auto resource_callback_options_size =
            offsetof(webscene_engine_options, scene_published_callback);
        const auto has_resource_callback =
            options->struct_size >= resource_callback_options_size;
        constexpr auto scene_callback_options_size =
            offsetof(webscene_engine_options, text_measure_callback);
        const auto has_scene_published_callback =
            options->struct_size >= scene_callback_options_size;
        constexpr auto text_measure_options_size =
            offsetof(webscene_engine_options, host_request_available_callback);
        const auto has_text_measure_callback =
            options->struct_size >= text_measure_options_size;
        constexpr auto host_request_available_options_size =
            offsetof(webscene_engine_options, interop_callback_available_callback);
        const auto has_host_request_available_callback =
            options->struct_size >= host_request_available_options_size;
        constexpr auto interop_callback_available_options_size =
            offsetof(
                webscene_engine_options,
                animation_frame_requested_callback);
        const auto has_interop_callback_available_callback =
            options->struct_size >= interop_callback_available_options_size;
        const auto has_animation_frame_requested_callback =
            options->struct_size >= sizeof(webscene_engine_options);
        return new webscene_engine(
            options->simulated_chart_command_count,
            std::move(cache_directory),
            has_resource_callback ? options->resource_load_callback : nullptr,
            has_resource_callback ? options->resource_load_user_data : nullptr,
            has_scene_published_callback ? options->scene_published_callback : nullptr,
            has_scene_published_callback ? options->scene_published_user_data : nullptr,
            has_text_measure_callback ? options->text_measure_callback : nullptr,
            has_text_measure_callback ? options->text_measure_user_data : nullptr,
            has_host_request_available_callback
                ? options->host_request_available_callback
                : nullptr,
            has_host_request_available_callback
                ? options->host_request_available_user_data
                : nullptr,
            has_interop_callback_available_callback
                ? options->interop_callback_available_callback
                : nullptr,
            has_interop_callback_available_callback
                ? options->interop_callback_available_user_data
                : nullptr,
            has_animation_frame_requested_callback
                ? options->animation_frame_requested_callback
                : nullptr,
            has_animation_frame_requested_callback
                ? options->animation_frame_requested_user_data
                : nullptr);
    } catch (...) {
        return nullptr;
    }
}

void webscene_engine_destroy(webscene_engine* engine)
{
    delete engine;
}

uint8_t webscene_engine_set_resource_root(
    webscene_engine* engine,
    const char* resource_root,
    size_t resource_root_length)
{
    return engine != nullptr
        && engine->set_resource_root(resource_root, resource_root_length)
        ? 1U
        : 0U;
}

uint8_t webscene_engine_load_url(
    webscene_engine* engine,
    const char* url,
    size_t url_length)
{
    return engine != nullptr && engine->load_url(url, url_length) ? 1U : 0U;
}

uint8_t webscene_engine_enqueue(webscene_engine* engine, const webscene_input_event* event)
{
    return engine != nullptr && event != nullptr && engine->enqueue(*event) ? 1U : 0U;
}

uint8_t webscene_engine_enqueue_resize_frame(
    webscene_engine* engine,
    const webscene_input_event* resize_event,
    const webscene_input_event* frame_event)
{
    return engine != nullptr
        && resize_event != nullptr
        && frame_event != nullptr
        && engine->enqueue_resize_frame(*resize_event, *frame_event)
        ? 1U
        : 0U;
}

uint32_t webscene_engine_get_cursor(const webscene_engine* engine)
{
    return engine == nullptr ? WEBSCENE_CURSOR_DEFAULT : engine->cursor();
}

uint8_t webscene_engine_requires_animation_frame(
    const webscene_engine* engine)
{
    return engine == nullptr ? 0U : engine->animation_frame_demand();
}

uint8_t webscene_engine_execute_script(
    webscene_engine* engine,
    const char* source,
    size_t source_length,
    const char* document_name,
    size_t document_name_length)
{
    return engine != nullptr
        && engine->execute_script(source, source_length, document_name, document_name_length)
        ? 1U
        : 0U;
}

uint64_t webscene_engine_inspector_connect(
    webscene_engine* engine,
    webscene_inspector_message_callback message_callback,
    void* user_data,
    uint8_t wait_for_debugger)
{
    return engine == nullptr
        ? 0U
        : engine->connect_inspector(
            message_callback,
            user_data,
            wait_for_debugger != 0U);
}

uint64_t webscene_engine_inspector_connect_v3(
    webscene_engine* engine,
    webscene_inspector_message_available_callback_v3 message_available_callback,
    void* user_data,
    uint8_t wait_for_debugger)
{
    return engine == nullptr
        ? 0U
        : engine->connect_inspector_v2(
            message_available_callback,
            user_data,
            wait_for_debugger != 0U);
}

size_t webscene_engine_inspector_take_message(
    webscene_engine* engine,
    uint64_t session_id,
    char* destination,
    size_t destination_capacity)
{
    return engine == nullptr
        ? 0U
        : engine->take_inspector_message(
            session_id,
            destination,
            destination_capacity);
}

uint8_t webscene_engine_inspector_dispatch(
    webscene_engine* engine,
    uint64_t session_id,
    const char* message,
    size_t message_length)
{
    return engine != nullptr
        && engine->dispatch_inspector(session_id, message, message_length)
        ? 1U
        : 0U;
}

uint8_t webscene_engine_inspector_disconnect(
    webscene_engine* engine,
    uint64_t session_id)
{
    return engine != nullptr && engine->disconnect_inspector(session_id)
        ? 1U
        : 0U;
}

uint8_t webscene_engine_inspector_is_available(const webscene_engine* engine)
{
    return engine != nullptr && engine->inspector_available() ? 1U : 0U;
}

uint64_t webscene_engine_begin_evaluate_v3(
    webscene_engine* engine,
    const webscene_interop_evaluate_request_v3* request,
    webscene_interop_completed_callback_v3 completed,
    void* user_data)
{
    return engine == nullptr
        ? 0U
        : engine->begin_evaluate_v3(request, completed, user_data);
}

uint64_t webscene_engine_begin_invoke_v3(
    webscene_engine* engine,
    const webscene_interop_invoke_request_v3* request,
    webscene_interop_completed_callback_v3 completed,
    void* user_data)
{
    return engine == nullptr
        ? 0U
        : engine->begin_invoke_v3(
            request,
            completed,
            user_data);
}

const webscene_interop_result_view_v3*
webscene_engine_take_invoke_result_v3(
    webscene_engine* engine,
    uint64_t operation_id)
{
    return engine == nullptr
        ? nullptr
        : engine->take_invoke_result_v3(operation_id);
}

uint8_t webscene_engine_cancel_invoke_v3(
    webscene_engine* engine,
    uint64_t operation_id)
{
    return engine != nullptr && engine->cancel_invoke_v3(operation_id)
        ? 1U
        : 0U;
}

void webscene_interop_result_release_v3(
    const webscene_interop_result_view_v3* result,
    uint64_t lease_id)
{
    if (result == nullptr || lease_id == 0U) return;
    interop_result_lease_v3* lease_pointer = nullptr;
    {
        std::lock_guard lock(taken_interop_results_mutex);
        const auto taken = taken_interop_results.find(result);
        if (taken == taken_interop_results.end()
            || taken->second.first != lease_id) {
            return;
        }
        lease_pointer = taken->second.second;
        taken_interop_results.erase(taken);
    }
    auto lease = std::unique_ptr<interop_result_lease_v3>(lease_pointer);
    auto pool = lease->owner;
    pool->release(std::move(lease));
}

const webscene_interop_callback_view_v3*
webscene_engine_take_callback_v3(webscene_engine* engine)
{
    return engine == nullptr ? nullptr : engine->take_callback_v3();
}

uint8_t webscene_engine_complete_callback_v3(
    webscene_engine* engine,
    const webscene_interop_callback_completion_v3* completion)
{
    return engine != nullptr
        && engine->complete_callback_v3(completion)
        ? 1U
        : 0U;
}

uint8_t webscene_engine_cancel_callback_v3(
    webscene_engine* engine,
    uint64_t call_id)
{
    return engine != nullptr && engine->cancel_callback_v3(call_id)
        ? 1U
        : 0U;
}

void webscene_interop_callback_release_v3(
    const webscene_interop_callback_view_v3* callback,
    uint64_t lease_id)
{
    if (callback == nullptr || lease_id == 0U) return;
    interop_callback_lease_v3* lease_pointer = nullptr;
    {
        std::lock_guard lock(taken_interop_callbacks_mutex);
        const auto taken = taken_interop_callbacks.find(callback);
        if (taken == taken_interop_callbacks.end()
            || taken->second.first != lease_id) {
            return;
        }
        lease_pointer = taken->second.second;
        taken_interop_callbacks.erase(taken);
    }
    auto lease = std::unique_ptr<interop_callback_lease_v3>(
        lease_pointer);
    auto pool = lease->owner;
    pool->release(std::move(lease));
}

uint8_t webscene_engine_get_interop_pool_metrics_v3(
    const webscene_engine* engine,
    webscene_interop_pool_metrics_v3* metrics)
{
    return engine != nullptr
        && metrics != nullptr
        && engine->get_interop_pool_metrics_v3(*metrics)
        ? 1U
        : 0U;
}

size_t webscene_engine_take_host_request(
    webscene_engine* engine,
    char* destination,
    size_t destination_capacity)
{
    return engine == nullptr
        ? 0U
        : engine->take_host_request(destination, destination_capacity);
}

size_t webscene_engine_take_console_message(
    webscene_engine* engine,
    char* destination,
    size_t destination_capacity)
{
    return engine == nullptr
        ? 0U
        : engine->take_console_message(destination, destination_capacity);
}

size_t webscene_engine_take_input_dispatch_failure(
    webscene_engine* engine,
    char* destination,
    size_t destination_capacity)
{
    return engine == nullptr
        ? 0U
        : engine->take_input_dispatch_failure(destination, destination_capacity);
}

size_t webscene_engine_copy_last_error(
    const webscene_engine* engine,
    char* destination,
    size_t destination_capacity)
{
    return engine == nullptr ? 0U : engine->copy_last_error(destination, destination_capacity);
}

size_t webscene_engine_copy_first_iframe_html(
    const webscene_engine* engine,
    char* destination,
    size_t destination_capacity)
{
    return engine == nullptr
        ? 0U
        : engine->copy_first_iframe_html(destination, destination_capacity);
}

size_t webscene_engine_copy_scene_diagnostics(
    const webscene_engine* engine,
    char* destination,
    size_t destination_capacity)
{
    return engine == nullptr
        ? 0U
        : engine->copy_scene_diagnostics(destination, destination_capacity);
}

size_t webscene_engine_copy_feature_use(
    const webscene_engine* engine,
    char* destination,
    size_t destination_capacity)
{
    return engine == nullptr
        ? 0U
        : engine->copy_feature_use(destination, destination_capacity);
}

size_t webscene_engine_copy_event_listener_inventory(
    const webscene_engine* engine,
    char* destination,
    size_t destination_capacity)
{
    return engine == nullptr
        ? 0U
        : engine->copy_event_listener_inventory(destination, destination_capacity);
}

size_t webscene_engine_copy_canvas_layouts(
    const webscene_engine* engine,
    webscene_canvas_layout* destination,
    size_t destination_capacity)
{
    return engine == nullptr
        ? 0U
        : engine->copy_canvas_layouts(destination, destination_capacity);
}

uint8_t webscene_engine_request_scene_checkpoint(webscene_engine* engine)
{
    return engine != nullptr && engine->request_scene_checkpoint() ? 1U : 0U;
}

uint8_t webscene_engine_request_low_memory(webscene_engine* engine)
{
    return engine != nullptr && engine->request_low_memory() ? 1U : 0U;
}

uint8_t webscene_engine_set_visible(webscene_engine* engine, uint8_t visible)
{
    return engine != nullptr && engine->set_visible(visible != 0) ? 1U : 0U;
}

uint8_t webscene_engine_set_preferred_color_scheme(
    webscene_engine* engine,
    uint32_t preferred_color_scheme)
{
    return engine != nullptr
        && engine->set_preferred_color_scheme(preferred_color_scheme)
        ? 1U
        : 0U;
}

const webscene_scene_view* webscene_engine_acquire_latest_scene(webscene_engine* engine)
{
    if (engine == nullptr) {
        return nullptr;
    }

    auto scene_value = engine->acquire_latest();
    if (!scene_value) {
        return nullptr;
    }

    try {
        auto* lease = new webscene_scene_lease(
            std::move(scene_value),
            engine->acknowledgement_state_handle());
        return &lease->view;
    } catch (...) {
        return nullptr;
    }
}

const webscene_scene_view* webscene_engine_acquire_next_scene(webscene_engine* engine)
{
    if (engine == nullptr) {
        return nullptr;
    }

    auto scene_value = engine->acquire_next();
    if (!scene_value) {
        return nullptr;
    }

    try {
        auto* lease = new webscene_scene_lease(
            std::move(scene_value),
            engine->acknowledgement_state_handle());
        return &lease->view;
    } catch (...) {
        return nullptr;
    }
}

void webscene_scene_release(const webscene_scene_view* scene_view)
{
    if (scene_view == nullptr) return;
    delete static_cast<webscene_scene_lease*>(const_cast<void*>(scene_view->lease_token));
}

uint8_t webscene_scene_acknowledge(const webscene_scene_view* scene_view)
{
    if (scene_view == nullptr || scene_view->lease_token == nullptr) return 0U;
    auto* lease = static_cast<webscene_scene_lease*>(const_cast<void*>(scene_view->lease_token));
    return lease->acknowledge() ? 1U : 0U;
}

uint8_t webscene_scene_get_header(
    const webscene_scene_view* scene_view,
    webscene_scene_header* header)
{
    if (scene_view == nullptr || header == nullptr) {
        return 0U;
    }

    *header = scene_view->header;
    return 1U;
}

const webscene_scene_command* webscene_scene_get_commands(
    const webscene_scene_view* scene_view,
    uint32_t* count)
{
    if (count != nullptr) *count = scene_view == nullptr ? 0U : scene_view->header.command_count;
    return scene_view == nullptr ? nullptr : scene_view->commands;
}

void webscene_engine_get_metrics(
    const webscene_engine* engine,
    webscene_engine_metrics* metrics)
{
    if (engine == nullptr || metrics == nullptr) {
        return;
    }
    engine->read_metrics(*metrics);
}

uint8_t webscene_engine_get_input_dispatch_metrics(
    const webscene_engine* engine,
    webscene_input_dispatch_metrics* metrics)
{
    constexpr auto original_struct_size =
        offsetof(webscene_input_dispatch_metrics, dispatched_inputs);
    if (engine == nullptr || metrics == nullptr
        || metrics->struct_size < original_struct_size) {
        return 0U;
    }
    engine->read_input_dispatch_metrics(*metrics);
    return 1U;
}

uint8_t webscene_engine_get_animation_frame_metrics(
    const webscene_engine* engine,
    webscene_animation_frame_metrics* metrics)
{
    if (engine == nullptr || metrics == nullptr
        || metrics->struct_size < sizeof(webscene_animation_frame_metrics)) {
        return 0U;
    }
    engine->read_animation_frame_metrics(*metrics);
    return 1U;
}

uint8_t webscene_engine_get_scene_flow_metrics(
    const webscene_engine* engine,
    webscene_scene_flow_metrics* metrics)
{
    if (engine == nullptr || metrics == nullptr
        || metrics->struct_size < sizeof(webscene_scene_flow_metrics)) {
        return 0U;
    }
    engine->read_scene_flow_metrics(*metrics);
    return 1U;
}

uint8_t webscene_engine_get_resize_frame_metrics(
    const webscene_engine* engine,
    webscene_resize_frame_metrics* metrics)
{
    if (engine == nullptr || metrics == nullptr
        || metrics->struct_size < sizeof(webscene_resize_frame_metrics)) {
        return 0U;
    }
    engine->read_resize_frame_metrics(*metrics);
    return 1U;
}

uint8_t webscene_engine_get_resource_cache_metrics(
    const webscene_engine* engine,
    webscene_resource_cache_metrics* metrics)
{
    if (engine == nullptr || metrics == nullptr
        || metrics->struct_size < sizeof(webscene_resource_cache_metrics)) {
        return 0U;
    }
    engine->read_resource_cache_metrics(*metrics);
    return 1U;
}

uint8_t webscene_engine_get_runtime_work_metrics(
    const webscene_engine* engine,
    webscene_runtime_work_metrics* metrics)
{
    if (engine == nullptr || metrics == nullptr
        || metrics->struct_size < sizeof(webscene_runtime_work_metrics)) {
        return 0U;
    }
    engine->read_runtime_work_metrics(*metrics);
    return 1U;
}

uint8_t webscene_engine_set_runtime_work_metrics_enabled(
    webscene_engine* engine,
    uint8_t enabled)
{
    if (engine == nullptr) return 0U;
    engine->set_runtime_work_metrics_enabled(enabled != 0U);
    return 1U;
}

uint8_t webscene_engine_get_process_cache_metrics(
    const webscene_engine* engine,
    webscene_process_cache_metrics* metrics)
{
    constexpr auto original_struct_size =
        offsetof(webscene_process_cache_metrics, script_source_memory_hits);
    if (engine == nullptr || metrics == nullptr
        || metrics->struct_size < original_struct_size) {
        return 0U;
    }
    engine->read_process_cache_metrics(*metrics);
    return 1U;
}

uint8_t webscene_engine_get_memory_metrics(
    const webscene_engine* engine,
    webscene_engine_memory_metrics* metrics)
{
    constexpr auto original_struct_size =
        offsetof(webscene_engine_memory_metrics, v8_code_and_metadata_bytes);
    if (engine == nullptr || metrics == nullptr
        || metrics->struct_size < original_struct_size) {
        return 0U;
    }
    engine->read_memory_metrics(*metrics);
    return 1U;
}

} // extern "C"
