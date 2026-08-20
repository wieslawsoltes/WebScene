#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  if defined(WEBSCENE_NATIVE_ENGINE_BUILD)
#    define WEBSCENE_API __declspec(dllexport)
#  else
#    define WEBSCENE_API __declspec(dllimport)
#  endif
#else
#  define WEBSCENE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct webscene_engine webscene_engine;
typedef struct webscene_scene_view webscene_scene_view;
typedef struct webscene_interop_result_view_v3 webscene_interop_result_view_v3;

/* Legacy direct-message callback retained for ABI compatibility. */
typedef void (*webscene_inspector_message_callback)(
    void* user_data,
    uint64_t session_id,
    const char* message,
    size_t message_length);

/* Signals that one or more Inspector messages can be pulled off the worker. */
typedef void (*webscene_inspector_message_available_callback_v3)(
    void* user_data,
    uint64_t session_id);

typedef enum webscene_input_kind {
    WEBSCENE_INPUT_POINTER_MOVE = 1,
    WEBSCENE_INPUT_POINTER_DOWN = 2,
    WEBSCENE_INPUT_POINTER_UP = 3,
    WEBSCENE_INPUT_WHEEL = 4,
    // x carries the host compositor's monotonic timestamp in milliseconds.
    WEBSCENE_INPUT_FRAME = 5,
    WEBSCENE_INPUT_RESIZE = 6,
    // x carries a DOM-compatible key code for keyboard events.
    WEBSCENE_INPUT_KEY_DOWN = 7,
    WEBSCENE_INPUT_KEY_UP = 8,
    // x carries one Unicode scalar value. Hosts enqueue one event per scalar.
    WEBSCENE_INPUT_TEXT = 9
} webscene_input_kind;

typedef enum webscene_cursor_kind {
    WEBSCENE_CURSOR_DEFAULT = 0,
    WEBSCENE_CURSOR_POINTER = 1,
    WEBSCENE_CURSOR_TEXT = 2,
    WEBSCENE_CURSOR_CROSSHAIR = 3,
    WEBSCENE_CURSOR_WAIT = 4,
    WEBSCENE_CURSOR_MOVE = 5,
    WEBSCENE_CURSOR_NOT_ALLOWED = 6,
    WEBSCENE_CURSOR_HELP = 7
} webscene_cursor_kind;

typedef enum webscene_preferred_color_scheme {
    WEBSCENE_PREFERRED_COLOR_SCHEME_LIGHT = 0,
    WEBSCENE_PREFERRED_COLOR_SCHEME_DARK = 1
} webscene_preferred_color_scheme;

enum {
    WEBSCENE_INPUT_MODIFIER_SHIFT = 1U << 0U,
    WEBSCENE_INPUT_MODIFIER_CONTROL = 1U << 1U,
    WEBSCENE_INPUT_MODIFIER_ALT = 1U << 2U,
    WEBSCENE_INPUT_MODIFIER_META = 1U << 3U,
    WEBSCENE_INPUT_KEY_REPEAT = 1U << 4U,
    // Pointer flags reserve low bits for DOM `buttons` and bits 8-15 for the
    // changed button. Keep keyboard-compatible modifiers in their own lane.
    WEBSCENE_INPUT_POINTER_MODIFIER_SHIFT = 1U << 16U,
    WEBSCENE_INPUT_POINTER_MODIFIER_CONTROL = 1U << 17U,
    WEBSCENE_INPUT_POINTER_MODIFIER_ALT = 1U << 18U,
    WEBSCENE_INPUT_POINTER_MODIFIER_META = 1U << 19U
};

typedef struct webscene_input_event {
    uint32_t kind;
    uint32_t flags;
    uint64_t sequence;
    double x;
    double y;
    // For WEBSCENE_INPUT_RESIZE, delta_x carries the positive host device scale
    // factor. Zero retains ABI-v2 compatibility with hosts that predate scale
    // reporting and is interpreted as 1.0.
    double delta_x;
    double delta_y;
} webscene_input_event;

typedef struct webscene_scene_header {
    uint64_t revision;
    uint64_t base_revision;
    uint64_t consumed_input_sequence;
    float viewport_width;
    float viewport_height;
    uint32_t command_count;
    uint32_t canvas_layer_count;
    uint32_t damage_rect_count;
    uint32_t flags;
    uint64_t content_hash;
} webscene_scene_header;

typedef struct webscene_scene_command {
    uint32_t kind;
    uint32_t flags;
    float x;
    float y;
    float width;
    float height;
    uint32_t rgba;
    uint32_t node_id;
    float radius_top_left;
    float radius_top_right;
    float radius_bottom_right;
    float radius_bottom_left;
    float stroke_width;
} webscene_scene_command;

typedef struct webscene_canvas_layout {
    uint32_t node_id;
    uint32_t flags;
    float x;
    float y;
    float width;
    float height;
    uint32_t bitmap_width;
    uint32_t bitmap_height;
} webscene_canvas_layout;

typedef struct webscene_canvas_layer {
    uint32_t node_id;
    uint32_t flags;
    uint32_t command_offset;
    uint32_t command_count;
    uint32_t string_offset;
    uint32_t string_count;
    uint32_t reserved;
    float x;
    float y;
    float width;
    float height;
    uint32_t bitmap_width;
    uint32_t bitmap_height;
    uint64_t generation;
} webscene_canvas_layer;

typedef union webscene_canvas_command_data {
    double values[8];
    struct {
        double x;
        double y;
    } point;
    struct {
        double x;
        double y;
        double width;
        double height;
    } rect;
    struct {
        double a;
        double b;
        double c;
        double d;
        double e;
        double f;
    } transform;
    struct {
        double x1;
        double y1;
        double x2;
        double y2;
        double x3;
        double y3;
    } curve;
} webscene_canvas_command_data;

enum {
    WEBSCENE_CANVAS_COMMAND_FLAG_EVEN_ODD = 1U << 16U,
    WEBSCENE_CANVAS_COMMAND_FLAG_TEXT_MAX_WIDTH = 1U << 17U
};

/*
 * Fixed-layout native draw operation. resource_id addresses the owning
 * layer's string/resource table when the command kind requires one. Managed
 * renderers traverse this array in-place; there is no packet decoding step.
 */
typedef struct webscene_canvas_command {
    uint32_t kind;
    uint32_t flags;
    uint32_t resource_id;
    uint32_t reserved;
    webscene_canvas_command_data data;
} webscene_canvas_command;

typedef struct webscene_scene_string {
    uint32_t byte_offset;
    uint32_t byte_length;
} webscene_scene_string;

typedef enum webscene_interop_value_kind_v3 {
    WEBSCENE_INTEROP_VALUE_UNDEFINED_V3 = 0,
    WEBSCENE_INTEROP_VALUE_NULL_V3 = 1,
    WEBSCENE_INTEROP_VALUE_BOOLEAN_V3 = 2,
    WEBSCENE_INTEROP_VALUE_NUMBER_V3 = 3,
    WEBSCENE_INTEROP_VALUE_STRING_V3 = 4,
    WEBSCENE_INTEROP_VALUE_ARRAY_V3 = 5,
    WEBSCENE_INTEROP_VALUE_OBJECT_V3 = 6,
    WEBSCENE_INTEROP_VALUE_HANDLE_V3 = 7
} webscene_interop_value_kind_v3;

typedef enum webscene_interop_result_status_v3 {
    WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3 = 0,
    WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V3 = 1,
    WEBSCENE_INTEROP_RESULT_CANCELLED_V3 = 2,
    WEBSCENE_INTEROP_RESULT_INVALID_REQUEST_V3 = 3
} webscene_interop_result_status_v3;

/*
 * One fixed-layout value in an immutable interop result. For strings,
 * offset/length address utf8_bytes. For arrays and objects they address the
 * edge table. Boolean, number and retained-handle payloads use payload.
 */
typedef struct webscene_interop_value_v3 {
    uint32_t kind;
    uint32_t flags;
    uint32_t offset;
    uint32_t length;
    uint64_t payload;
} webscene_interop_value_v3;

/*
 * Array edges use value_index only. Object edges additionally address the
 * UTF-8 property name. Indices and offsets are validated by the managed
 * reader before constructing spans.
 */
typedef struct webscene_interop_edge_v3 {
    uint32_t name_offset;
    uint32_t name_length;
    uint32_t value_index;
    uint32_t reserved;
} webscene_interop_edge_v3;

/*
 * ABI 3 arbitrary-evaluation request. This retains source evaluation for
 * diagnostics and compatibility tests while returning the same leased tagged
 * result used by generated direct invocations.
 */
typedef struct webscene_interop_evaluate_request_v3 {
    uint32_t struct_size;
    uint32_t version;
    const char* source;
    size_t source_length;
    const char* document_name;
    size_t document_name_length;
    uint32_t flags;
    uint32_t reserved;
} webscene_interop_evaluate_request_v3;

typedef enum webscene_interop_operation_v3 {
    WEBSCENE_INTEROP_GET_GLOBAL_V3 = 1,
    WEBSCENE_INTEROP_INVOKE_GLOBAL_V3 = 2,
    WEBSCENE_INTEROP_CONSTRUCT_V3 = 3,
    WEBSCENE_INTEROP_GET_PROPERTY_V3 = 4,
    WEBSCENE_INTEROP_SET_PROPERTY_V3 = 5,
    WEBSCENE_INTEROP_INVOKE_MEMBER_V3 = 6,
    WEBSCENE_INTEROP_RELEASE_HANDLE_V3 = 7,
    WEBSCENE_INTEROP_CREATE_CALLBACK_TARGET_V3 = 8,
    WEBSCENE_INTEROP_CREATE_CALLBACK_FUNCTION_V3 = 9,
    WEBSCENE_INTEROP_CREATE_SYNCHRONOUS_FACTORY_V3 = 10,
    WEBSCENE_INTEROP_INVOKE_FUNCTION_V3 = 11
} webscene_interop_operation_v3;

typedef enum webscene_interop_result_mode_v3 {
    WEBSCENE_INTEROP_RESULT_VALUE_V3 = 0,
    WEBSCENE_INTEROP_RESULT_RETAINED_HANDLE_V3 = 1,
    WEBSCENE_INTEROP_RESULT_VOID_V3 = 2
} webscene_interop_result_mode_v3;

typedef enum webscene_interop_call_flags_v3 {
    WEBSCENE_INTEROP_CALL_AWAIT_PROMISE_V3 = 1
} webscene_interop_call_flags_v3;

/*
 * Generated direct-invocation request. All pointers are copied before
 * webscene_engine_begin_invoke_v3 returns. String offsets in value
 * nodes and object edges address utf8_bytes. arguments_root must identify an
 * array whose items are the call arguments.
 */
typedef struct webscene_interop_invoke_request_v3 {
    uint32_t struct_size;
    uint32_t version;
    uint32_t operation;
    uint32_t flags;
    uint64_t target_handle;
    const char* global_name;
    size_t global_name_length;
    const char* member_name;
    size_t member_name_length;
    const webscene_interop_value_v3* values;
    size_t value_count;
    const webscene_interop_edge_v3* edges;
    size_t edge_count;
    const char* utf8_bytes;
    size_t utf8_byte_count;
    uint32_t arguments_root;
    uint32_t result_mode;
} webscene_interop_invoke_request_v3;

typedef void (*webscene_interop_completed_callback_v3)(
    void* user_data,
    uint64_t operation_id);

/*
 * The result and every table/string pointer remain valid until
 * webscene_interop_result_release_v3 is called with the lease_id copied from
 * this view. The result can outlive the engine that produced it. Callers must
 * retain lease_id separately before release; passing a stale pointer with its
 * old lease_id is a safe no-op even after the pooled address is reused.
 */
struct webscene_interop_result_view_v3 {
    uint32_t struct_size;
    uint32_t version;
    uint32_t status;
    uint32_t flags;
    uint64_t operation_id;
    const webscene_interop_value_v3* values;
    const webscene_interop_edge_v3* edges;
    const char* utf8_bytes;
    const char* error_bytes;
    uint64_t lease_id;
    uint32_t value_count;
    uint32_t edge_count;
    uint32_t utf8_byte_count;
    uint32_t error_byte_count;
    uint32_t root_value_index;
    uint32_t pooled_capacity;
    uint32_t reserved0;
    uint32_t reserved1;
};

typedef enum webscene_interop_callback_return_kind_v3 {
    WEBSCENE_INTEROP_CALLBACK_VOID_V3 = 0,
    WEBSCENE_INTEROP_CALLBACK_PROMISE_V3 = 1,
    WEBSCENE_INTEROP_CALLBACK_SYNCHRONOUS_V3 = 2
} webscene_interop_callback_return_kind_v3;

/*
 * Immutable JavaScript-to-managed callback invocation. The tagged argument
 * arena remains valid until webscene_interop_callback_release_v3 receives the
 * matching lease_id. Taken callback leases may outlive their engine.
 */
struct webscene_interop_callback_view_v3 {
    uint32_t struct_size;
    uint32_t version;
    uint64_t call_id;
    uint64_t target_id;
    uint32_t method_id;
    uint32_t return_kind;
    const webscene_interop_value_v3* values;
    const webscene_interop_edge_v3* edges;
    const char* utf8_bytes;
    uint64_t lease_id;
    uint32_t value_count;
    uint32_t edge_count;
    uint32_t utf8_byte_count;
    uint32_t arguments_root;
    uint32_t pooled_capacity;
    uint32_t reserved0;
};

/*
 * Managed callback completion. All arena pointers are copied before
 * webscene_engine_complete_callback_v3 returns. A successful completion uses
 * root_value_index; a failed completion uses error_bytes.
 */
typedef struct webscene_interop_callback_completion_v3 {
    uint32_t struct_size;
    uint32_t version;
    uint64_t call_id;
    uint32_t succeeded;
    uint32_t reserved;
    const webscene_interop_value_v3* values;
    size_t value_count;
    const webscene_interop_edge_v3* edges;
    size_t edge_count;
    const char* utf8_bytes;
    size_t utf8_byte_count;
    const char* error_bytes;
    size_t error_byte_count;
    uint32_t root_value_index;
    uint32_t reserved1;
} webscene_interop_callback_completion_v3;

typedef struct webscene_interop_pool_metrics_v3 {
    uint32_t struct_size;
    uint32_t version;
    uint64_t outstanding_results;
    uint64_t pooled_bytes;
    uint64_t pool_hits;
    uint64_t pool_misses;
    uint64_t oversize_allocations;
    uint64_t high_water_outstanding_results;
    uint64_t pooled_request_records;
    uint64_t request_pool_hits;
    uint64_t request_pool_misses;
    uint64_t request_oversize_allocations;
    uint64_t active_operation_slots;
    uint64_t available_operation_slots;
    uint64_t operation_slot_high_water;
    uint64_t pooled_result_bytes_4k;
    uint64_t pooled_result_bytes_16k;
    uint64_t pooled_result_bytes_64k;
    uint64_t pooled_result_bytes_256k;
    uint64_t pooled_result_bytes_1m;
    uint64_t taken_result_leases;
    uint64_t operation_result_leases;
    uint64_t queued_callbacks;
    uint64_t taken_callback_leases;
    uint64_t pending_callback_promises;
    uint64_t callback_queue_high_water;
} webscene_interop_pool_metrics_v3;

typedef struct webscene_damage_rect {
    float x;
    float y;
    float width;
    float height;
} webscene_damage_rect;

/*
 * The acquired pointer is the immutable scene. Every pointer below remains
 * valid until webscene_scene_release is called. A renderer can construct spans
 * over the arrays directly without further native calls or copying.
 */
struct webscene_scene_view {
    uint32_t struct_size;
    uint32_t abi_version;
    webscene_scene_header header;
    const webscene_scene_command* commands;
    const webscene_canvas_layer* canvas_layers;
    const webscene_canvas_command* canvas_commands;
    const webscene_scene_string* strings;
    const char* string_bytes;
    const webscene_damage_rect* damage_rects;
    const void* lease_token;
    uint32_t canvas_command_count;
    uint32_t string_count;
    uint32_t string_byte_count;
    uint32_t reserved;
};

typedef enum webscene_resource_kind {
    WEBSCENE_RESOURCE_DOCUMENT = 0,
    WEBSCENE_RESOURCE_SCRIPT = 1,
    WEBSCENE_RESOURCE_STYLESHEET = 2,
    // Text-backed SVG image resources used by CSS background-image. Binary
    // image formats require a future byte-resource envelope.
    WEBSCENE_RESOURCE_IMAGE = 3
} webscene_resource_kind;

/*
 * Synchronous text-resource callback used by the native DOM runtime. The
 * callback follows the other copy APIs: a null/short destination reports the
 * required response-envelope byte count. The envelope contains status,
 * cacheability/freshness metadata, validators, and UTF-8 content. Returning
 * zero reports a load failure. The URL is already absolute and normalized by
 * WebScene.
 */
typedef size_t (*webscene_resource_load_callback)(
    void* user_data,
    uint32_t kind,
    const char* url,
    size_t url_length,
    const char* entity_tag,
    size_t entity_tag_length,
    int64_t last_modified_unix_seconds,
    char* destination,
    size_t destination_capacity);

/*
 * Asynchronous notification emitted after an immutable scene has been
 * published. Consumers use this edge to schedule a compositor paint; they
 * still acquire and acknowledge the scene through the normal scene API.
 * The callback runs on the engine worker and must not block.
 */
typedef void (*webscene_scene_published_callback)(
    void* user_data,
    uint64_t revision,
    uint64_t consumed_input_sequence,
    float viewport_width,
    float viewport_height);

typedef struct webscene_text_metrics {
    uint32_t struct_size;
    float advance_width;
    float ascent;
    float descent;
    float leading;
    float actual_bounding_box_left;
    float actual_bounding_box_right;
    float actual_bounding_box_ascent;
    float actual_bounding_box_descent;
} webscene_text_metrics;

/*
 * Synchronous host text shaper used by native layout. Layout and paint must
 * consume the same glyph advances; otherwise kerning, combining marks, font
 * weight, and fallback faces can make inline boxes clip or drift. The callback
 * runs on the engine worker and must not block or call back into the engine.
 */
typedef uint8_t (*webscene_text_measure_callback)(
    void* user_data,
    const char* text,
    size_t text_length,
    const char* font_family,
    size_t font_family_length,
    float font_size,
    int32_t font_weight,
    float letter_spacing,
    float word_spacing,
    webscene_text_metrics* metrics);

/*
 * Edge notification emitted after a managed host request is queued. The
 * callback runs on the engine worker and must only signal non-blocking host
 * work; requests are still consumed through webscene_engine_take_host_request.
 */
typedef void (*webscene_host_request_available_callback)(void* user_data);

/*
 * Edge notification emitted when JavaScript queues an interop callback for
 * its managed host. The callback runs on the engine worker and must only
 * signal non-blocking host work; managed callbacks are drained after the
 * current engine call returns.
 */
typedef void (*webscene_interop_callback_available_callback)(void* user_data);

/*
 * Edge notification emitted when the engine's host animation-frame demand
 * transitions from idle to active. The callback runs on the engine worker and
 * must only wake a compositor; demand is still queried through
 * webscene_engine_requires_animation_frame and released by a frame input.
 */
typedef void (*webscene_animation_frame_requested_callback)(void* user_data);

typedef struct webscene_engine_options {
    uint32_t struct_size;
    uint32_t simulated_chart_command_count;
    const char* compilation_cache_directory;
    size_t compilation_cache_directory_length;
    webscene_resource_load_callback resource_load_callback;
    void* resource_load_user_data;
    webscene_scene_published_callback scene_published_callback;
    void* scene_published_user_data;
    webscene_text_measure_callback text_measure_callback;
    void* text_measure_user_data;
    webscene_host_request_available_callback host_request_available_callback;
    void* host_request_available_user_data;
    webscene_interop_callback_available_callback interop_callback_available_callback;
    void* interop_callback_available_user_data;
    webscene_animation_frame_requested_callback animation_frame_requested_callback;
    void* animation_frame_requested_user_data;
} webscene_engine_options;

enum {
    WEBSCENE_DOCUMENT_SCRIPT_ALL_FRAMES = 1U << 0U
};

/*
 * Fixed-layout document-start program descriptor. The engine copies every
 * source and name before webscene_engine_load_url_with_options returns; callers
 * retain no buffers for queued navigation work.
 */
typedef struct webscene_document_script {
    uint32_t struct_size;
    uint32_t flags;
    const char* source;
    size_t source_length;
    const char* name;
    size_t name_length;
} webscene_document_script;

typedef struct webscene_navigation_options {
    uint32_t struct_size;
    uint32_t document_script_count;
    const webscene_document_script* document_scripts;
} webscene_navigation_options;

typedef struct webscene_engine_metrics {
    uint64_t enqueued_inputs;
    uint64_t dropped_inputs;
    uint64_t consumed_inputs;
    uint64_t published_scenes;
    uint64_t acquired_scenes;
    uint64_t executed_scripts;
    uint64_t script_errors;
    uint64_t dom_nodes;
    uint64_t layout_passes;
    uint64_t iframe_nodes;
    uint64_t iframe_html_bytes;
    uint64_t frame_scripts_executed;
    uint64_t frame_script_errors;
    uint64_t canvas_nodes;
    uint64_t component_ready;
    uint64_t compilation_requests;
    uint64_t compilation_memory_hits;
    uint64_t compilation_persistent_hits;
    uint64_t compilation_persistent_misses;
    uint64_t compilation_cache_rejections;
    uint64_t compilation_cache_bytes_read;
    uint64_t compilation_cache_bytes_written;
    uint64_t compilation_time_nanoseconds;
    uint64_t input_events_dispatched;
    uint64_t input_callbacks_invoked;
    uint64_t busiest_canvas_width_milli;
    uint64_t busiest_canvas_height_milli;
    uint64_t coalesced_resize_inputs;
    uint64_t applied_resize_inputs;
    uint64_t last_resize_dispatch_nanoseconds;
    uint64_t last_scene_publication_nanoseconds;
    uint64_t last_resize_outer_listeners_nanoseconds;
    uint64_t last_resize_frame_listeners_nanoseconds;
    uint64_t last_resize_layout_nanoseconds;
    uint64_t last_resize_observers_nanoseconds;
    uint64_t coalesced_pointer_move_inputs;
    uint64_t coalesced_wheel_inputs;
    uint64_t applied_pointer_move_inputs;
    uint64_t applied_wheel_inputs;
    uint64_t applied_animation_frames;
    uint64_t coalesced_animation_frames;
    uint64_t last_animation_advance_nanoseconds;
    uint64_t last_layout_nanoseconds;
    uint64_t last_scene_build_nanoseconds;
    uint64_t maximum_scene_publication_nanoseconds;
} webscene_engine_metrics;

typedef struct webscene_input_dispatch_metrics {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t last_dispatch_nanoseconds;
    uint64_t maximum_dispatch_nanoseconds;
    uint64_t last_dispatch_sequence;
    uint64_t dispatched_inputs;
    uint64_t total_dispatch_nanoseconds;
} webscene_input_dispatch_metrics;

typedef struct webscene_animation_frame_metrics {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t dispatched_frames;
    uint64_t total_dispatch_nanoseconds;
    uint64_t last_dispatch_nanoseconds;
    uint64_t maximum_dispatch_nanoseconds;
    uint64_t last_timestamp_microseconds;
} webscene_animation_frame_metrics;

typedef struct webscene_scene_flow_metrics {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t publication_attempts;
    uint64_t blocked_publications;
    uint64_t acknowledged_scenes;
    uint64_t total_acknowledgement_nanoseconds;
    uint64_t last_acknowledgement_nanoseconds;
    uint64_t maximum_acknowledgement_nanoseconds;
    uint64_t acknowledged_revision;
} webscene_scene_flow_metrics;

typedef struct webscene_resize_frame_metrics {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t submitted_pairs;
    uint64_t applied_pairs;
    uint64_t published_pairs;
    uint64_t total_queue_nanoseconds;
    uint64_t last_queue_nanoseconds;
    uint64_t maximum_queue_nanoseconds;
    uint64_t total_dispatch_nanoseconds;
    uint64_t last_dispatch_nanoseconds;
    uint64_t maximum_dispatch_nanoseconds;
    uint64_t animation_frame_callbacks;
    uint64_t total_animation_frame_batch_nanoseconds;
    uint64_t last_animation_frame_batch_nanoseconds;
    uint64_t maximum_animation_frame_batch_nanoseconds;
    uint64_t total_to_publication_nanoseconds;
    uint64_t last_to_publication_nanoseconds;
    uint64_t maximum_to_publication_nanoseconds;
} webscene_resize_frame_metrics;

typedef struct webscene_resource_cache_metrics {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t requests;
    uint64_t hits;
    uint64_t misses;
    uint64_t rejections;
    uint64_t bytes_read;
    uint64_t bytes_written;
} webscene_resource_cache_metrics;

/*
 * Monotonic work counters used to compare equivalent benchmark intervals.
 * JavaScript task counters are copied from the worker-owned runtime state at
 * existing metric update boundaries; gauges and retained sizes live in the
 * separate memory and interop-pool structures.
 */
typedef struct webscene_runtime_work_metrics {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t timers_scheduled;
    uint64_t timers_fired;
    uint64_t timers_cancelled;
    uint64_t late_timers;
    uint64_t total_timer_lateness_nanoseconds;
    uint64_t animation_frames_requested;
    uint64_t animation_frames_invoked;
    uint64_t animation_frames_cancelled;
    uint64_t microtask_checkpoints;
    uint64_t worker_waits;
    uint64_t worker_signalled_wakes;
    uint64_t worker_timeout_wakes;
    uint64_t scene_builds;
    uint64_t no_damage_scene_builds;
    uint64_t full_checkpoint_scene_builds;
    uint64_t arbitrary_evaluation_calls;
    uint64_t generated_invoke_calls;
    uint64_t generated_callback_calls;
    uint64_t arbitrary_evaluation_source_bytes;
    uint64_t generated_request_bytes;
} webscene_runtime_work_metrics;

typedef struct webscene_process_cache_metrics {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t compilation_memory_hits;
    uint64_t compilation_leaders;
    uint64_t compilation_waiters;
    uint64_t compilation_shared_bytes;
    uint64_t resource_memory_hits;
    uint64_t resource_load_leaders;
    uint64_t resource_load_waiters;
    uint64_t resource_shared_bytes;
    /* ABI v2 tail; immutable external script-source sharing. */
    uint64_t script_source_memory_hits;
    uint64_t script_source_shared_bytes;
    /* ABI v3 tail; stable shared-isolate pool ownership diagnostics. */
    uint64_t shared_isolate_slot;
    uint64_t shared_isolate_active_contexts;
    uint64_t shared_isolate_peak_contexts;
} webscene_process_cache_metrics;

/*
 * Last worker-thread snapshot of memory retained by this engine plus the
 * process-wide immutable caches shared by all engines. Process cache byte
 * counts must be counted once per process, not once per engine.
 */
typedef struct webscene_engine_memory_metrics {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t v8_total_heap_bytes;
    uint64_t v8_used_heap_bytes;
    uint64_t v8_executable_heap_bytes;
    uint64_t v8_physical_heap_bytes;
    uint64_t v8_external_bytes;
    uint64_t v8_malloced_bytes;
    uint64_t v8_peak_malloced_bytes;
    uint64_t latest_scene_bytes;
    uint64_t process_compilation_cache_bytes;
    uint64_t process_resource_cache_bytes;
    /* ABI v2 tail; callers using the original prefix remain supported. */
    uint64_t v8_code_and_metadata_bytes;
    uint64_t v8_bytecode_and_metadata_bytes;
    uint64_t v8_external_script_source_bytes;
    /* Optional retained-native-allocation attribution tail. */
    uint64_t native_dom_node_count;
    uint64_t native_dom_node_size_bytes;
    uint64_t native_dom_inline_bytes;
    uint64_t native_dom_pseudo_storage_bytes;
    uint64_t native_dom_canvas_node_count;
    uint64_t native_dom_canvas_storage_bytes;
    uint64_t native_dom_animation_count;
    uint64_t native_dom_animation_storage_bytes;
    uint64_t native_dom_custom_property_node_count;
    uint64_t native_dom_custom_property_entry_count;
    uint64_t native_dom_custom_property_storage_bytes;
    uint64_t native_dom_background_image_count;
    uint64_t native_dom_background_image_storage_bytes;
    uint64_t native_dom_grid_count;
    uint64_t native_dom_grid_storage_bytes;
    uint64_t native_dom_authored_style_node_count;
    uint64_t native_dom_authored_style_entry_count;
    uint64_t native_dom_authored_style_storage_bytes;
    uint64_t native_css_rule_count;
    uint64_t native_css_rule_storage_bytes;
    uint64_t native_css_index_storage_bytes;
    uint64_t process_shared_css_rule_count;
    uint64_t process_shared_css_rule_storage_bytes;
    uint64_t low_memory_notifications;
    uint64_t native_dom_attribute_node_count;
    uint64_t native_dom_attribute_entry_count;
    uint64_t native_dom_attribute_storage_bytes;
    /* Additive metrics tail; native registries, caches and mapped storage. */
    uint64_t native_wrapper_handle_count;
    uint64_t native_wrapper_storage_bytes;
    uint64_t native_text_measurement_cache_entry_count;
    uint64_t native_text_measurement_cache_storage_bytes;
    uint64_t process_compilation_mapped_cache_bytes;
    uint64_t process_resource_mapped_cache_bytes;
    uint64_t native_dom_textual_style_count;
    uint64_t native_dom_textual_style_storage_bytes;
    uint64_t native_dom_node_pool_reserved_bytes;
    uint64_t native_dom_node_pool_peak_bytes;
    uint64_t native_dom_table_layout_count;
    uint64_t native_dom_table_layout_storage_bytes;
    uint64_t native_dom_form_control_count;
    uint64_t native_dom_form_control_storage_bytes;
    uint64_t hidden_low_memory_notifications;
    uint64_t native_event_listener_count;
    uint64_t native_event_listener_storage_bytes;
    /* ABI additive tail; aggregate V8 heap-space attribution. */
    uint64_t v8_young_space_used_bytes;
    uint64_t v8_young_space_physical_bytes;
    uint64_t v8_old_space_used_bytes;
    uint64_t v8_old_space_physical_bytes;
    uint64_t v8_code_space_used_bytes;
    uint64_t v8_code_space_physical_bytes;
    uint64_t v8_map_space_used_bytes;
    uint64_t v8_map_space_physical_bytes;
    uint64_t v8_large_object_space_used_bytes;
    uint64_t v8_large_object_space_physical_bytes;
    uint64_t v8_read_only_space_used_bytes;
    uint64_t v8_read_only_space_physical_bytes;
    uint64_t v8_shared_space_used_bytes;
    uint64_t v8_shared_space_physical_bytes;
    uint64_t v8_trusted_space_used_bytes;
    uint64_t v8_trusted_space_physical_bytes;
    /* ABI additive tail; bounded immutable scene pipeline attribution. */
    uint64_t pending_scene_count;
    uint64_t pending_scene_bytes;
} webscene_engine_memory_metrics;

/*
 * Pays the process-wide native runtime initialization cost without creating a
 * document, isolate, or chart. This does not read or mutate compilation caches.
 */
#define WEBSCENE_ENGINE_BUILD_FEATURE_CERTIFICATION (1U << 0U)
#define WEBSCENE_ENGINE_BUILD_FEATURE_V8_INSPECTOR (1U << 1U)

WEBSCENE_API uint32_t webscene_engine_get_abi_version(void);
/*
 * Reports compile-time features of the loaded native binary. Certification
 * telemetry/profiling and V8 Inspector hooks/state are absent unless their
 * respective bits are present.
 */
WEBSCENE_API uint32_t webscene_engine_get_build_features(void);
WEBSCENE_API uint8_t webscene_engine_prewarm(void);
WEBSCENE_API webscene_engine* webscene_engine_create(uint32_t simulated_chart_command_count);
WEBSCENE_API webscene_engine* webscene_engine_create_with_options(const webscene_engine_options* options);
WEBSCENE_API void webscene_engine_destroy(webscene_engine* engine);
WEBSCENE_API uint8_t webscene_engine_set_resource_root(
    webscene_engine* engine,
    const char* resource_root,
    size_t resource_root_length);
WEBSCENE_API uint8_t webscene_engine_load_url(
    webscene_engine* engine,
    const char* url,
    size_t url_length);
WEBSCENE_API uint8_t webscene_engine_load_url_with_options(
    webscene_engine* engine,
    const char* url,
    size_t url_length,
    const webscene_navigation_options* options);
WEBSCENE_API uint8_t webscene_engine_enqueue(webscene_engine* engine, const webscene_input_event* event);
/*
 * Atomically submits a viewport update and its corresponding host rendering
 * opportunity. The worker applies resize listeners/observers before releasing
 * requestAnimationFrame callbacks, without racing two independently awakened
 * enqueue calls. Both records contribute to the ordinary input metrics.
 */
WEBSCENE_API uint8_t webscene_engine_enqueue_resize_frame(
    webscene_engine* engine,
    const webscene_input_event* resize_event,
    const webscene_input_event* frame_event);
/*
 * Requests a V8 low-memory collection on this engine's worker thread. This is
 * intended for hidden/idle components or host memory-pressure handling; it
 * queues work and does not block the caller on garbage collection.
 */
WEBSCENE_API uint8_t webscene_engine_request_low_memory(webscene_engine* engine);
/*
 * Declares whether the host is actively presenting this engine. A transition
 * to hidden schedules one debounced low-memory collection on the engine
 * worker; returning visible before the deadline cancels it.
 */
WEBSCENE_API uint8_t webscene_engine_set_visible(webscene_engine* engine, uint8_t visible);
/*
 * Updates the host's effective color preference. The worker re-evaluates CSS
 * media rules and subsequent Window.matchMedia snapshots against this value.
 */
WEBSCENE_API uint8_t webscene_engine_set_preferred_color_scheme(
    webscene_engine* engine,
    uint32_t preferred_color_scheme);
/* Returns the CSS cursor resolved at the latest hit-tested pointer position. */
WEBSCENE_API uint32_t webscene_engine_get_cursor(const webscene_engine* engine);
/*
 * Returns a demand bitmask for the next compositor frame: bit 0 is a pending
 * JavaScript RAF, bit 1 is a native CSS animation, and bit 2 is a focused
 * caret. Zero means a host frame would be empty.
 */
WEBSCENE_API uint8_t webscene_engine_requires_animation_frame(
    const webscene_engine* engine);
WEBSCENE_API uint8_t webscene_engine_execute_script(
    webscene_engine* engine,
    const char* source,
    size_t source_length,
    const char* document_name,
    size_t document_name_length);
/* Raw V8 Inspector/CDP sessions are available for dedicated isolates. */
WEBSCENE_API uint64_t webscene_engine_inspector_connect(
    webscene_engine* engine,
    webscene_inspector_message_callback message_callback,
    void* user_data,
    uint8_t wait_for_debugger);
/*
 * Preferred non-reentrant session contract. The callback only signals
 * availability; use webscene_engine_inspector_take_message to copy messages.
 */
WEBSCENE_API uint64_t webscene_engine_inspector_connect_v3(
    webscene_engine* engine,
    webscene_inspector_message_available_callback_v3 message_available_callback,
    void* user_data,
    uint8_t wait_for_debugger);
/*
 * Returns zero when no message is queued, SIZE_MAX when the bounded output
 * queue overflowed, or the required message size. A null/short destination
 * leaves the front message queued; a sufficiently large destination pops it.
 */
WEBSCENE_API size_t webscene_engine_inspector_take_message(
    webscene_engine* engine,
    uint64_t session_id,
    char* destination,
    size_t destination_capacity);
WEBSCENE_API uint8_t webscene_engine_inspector_dispatch(
    webscene_engine* engine,
    uint64_t session_id,
    const char* message,
    size_t message_length);
WEBSCENE_API uint8_t webscene_engine_inspector_disconnect(
    webscene_engine* engine,
    uint64_t session_id);
WEBSCENE_API uint8_t webscene_engine_inspector_is_available(
    const webscene_engine* engine);
/* Diagnostics: nonzero only after the first Inspector connection attempt. */
WEBSCENE_API uint8_t webscene_engine_inspector_state_created(
    const webscene_engine* engine);
WEBSCENE_API uint64_t webscene_engine_begin_evaluate_v3(
    webscene_engine* engine,
    const webscene_interop_evaluate_request_v3* request,
    webscene_interop_completed_callback_v3 completed,
    void* user_data);
WEBSCENE_API uint64_t webscene_engine_begin_invoke_v3(
    webscene_engine* engine,
    const webscene_interop_invoke_request_v3* request,
    webscene_interop_completed_callback_v3 completed,
    void* user_data);
WEBSCENE_API const webscene_interop_result_view_v3*
webscene_engine_take_invoke_result_v3(
    webscene_engine* engine,
    uint64_t operation_id);
WEBSCENE_API uint8_t webscene_engine_cancel_invoke_v3(
    webscene_engine* engine,
    uint64_t operation_id);
WEBSCENE_API void webscene_interop_result_release_v3(
    const webscene_interop_result_view_v3* result,
    uint64_t lease_id);
WEBSCENE_API const webscene_interop_callback_view_v3*
webscene_engine_take_callback_v3(webscene_engine* engine);
WEBSCENE_API uint8_t webscene_engine_complete_callback_v3(
    webscene_engine* engine,
    const webscene_interop_callback_completion_v3* completion);
WEBSCENE_API uint8_t webscene_engine_cancel_callback_v3(
    webscene_engine* engine,
    uint64_t call_id);
WEBSCENE_API void webscene_interop_callback_release_v3(
    const webscene_interop_callback_view_v3* callback,
    uint64_t lease_id);
WEBSCENE_API uint8_t webscene_engine_get_interop_pool_metrics_v3(
    const webscene_engine* engine,
    webscene_interop_pool_metrics_v3* metrics);
/*
 * Removes one actual managed-datafeed request from the native V8 bridge. The
 * payload is UTF-8 JSON. A too-small/null destination reports the required
 * size without consuming the request; a successful full copy consumes it.
 */
WEBSCENE_API size_t webscene_engine_take_host_request(
    webscene_engine* engine,
    char* destination,
    size_t destination_capacity);
/*
 * Removes one V8 console entry. The UTF-8 payload is `<level>\n<message>`;
 * querying with a null/short destination reports the required byte count
 * without consuming the entry.
 */
WEBSCENE_API size_t webscene_engine_take_console_message(
    webscene_engine* engine,
    char* destination,
    size_t destination_capacity);
/*
 * Removes one failed asynchronous input dispatch. The UTF-8 payload is
 * `<sequence>\n<kind>\n<error>` so consumers can attribute the JavaScript
 * exception to the exact native input event. A null/short destination reports
 * the required size without consuming the failure.
 */
WEBSCENE_API size_t webscene_engine_take_input_dispatch_failure(
    webscene_engine* engine,
    char* destination,
    size_t destination_capacity);
WEBSCENE_API size_t webscene_engine_copy_last_error(
    const webscene_engine* engine,
    char* destination,
    size_t destination_capacity);
WEBSCENE_API size_t webscene_engine_copy_first_iframe_html(
    const webscene_engine* engine,
    char* destination,
    size_t destination_capacity);
WEBSCENE_API size_t webscene_engine_copy_scene_diagnostics(
    const webscene_engine* engine,
    char* destination,
    size_t destination_capacity);
/*
 * Copies a UTF-8 webscene-native-feature-use-v2 JSON snapshot. Feature and
 * composition observations are counted at native decision points;
 * `complete:false` means one or more inventory categories remain uninstrumented.
 */
WEBSCENE_API size_t webscene_engine_copy_feature_use(
    const webscene_engine* engine,
    char* destination,
    size_t destination_capacity);
/* Copies registered element listener target/type inventory as stable UTF-8 JSON. */
WEBSCENE_API size_t webscene_engine_copy_event_listener_inventory(
    const webscene_engine* engine,
    char* destination,
    size_t destination_capacity);
WEBSCENE_API size_t webscene_engine_copy_canvas_layouts(
    const webscene_engine* engine,
    webscene_canvas_layout* destination,
    size_t destination_capacity);
/*
 * Starts a new immutable-scene diff chain with a complete checkpoint. Call
 * this after the previous scene consumer has stopped, before attaching a new
 * renderer (for example after compositor/context recreation).
 */
WEBSCENE_API uint8_t webscene_engine_request_scene_checkpoint(webscene_engine* engine);
WEBSCENE_API const webscene_scene_view* webscene_engine_acquire_latest_scene(webscene_engine* engine);
/*
 * Enables the bounded ordered consumer lane and acquires its oldest pending
 * diff. Unlike acquire_latest, this preserves every base-revision link while
 * allowing the producer to publish one additional immutable diff ahead.
 */
WEBSCENE_API const webscene_scene_view* webscene_engine_acquire_next_scene(webscene_engine* engine);
WEBSCENE_API uint8_t webscene_scene_acknowledge(const webscene_scene_view* scene);
WEBSCENE_API void webscene_scene_release(const webscene_scene_view* scene);
WEBSCENE_API uint8_t webscene_scene_get_header(
    const webscene_scene_view* scene,
    webscene_scene_header* header);
WEBSCENE_API const webscene_scene_command* webscene_scene_get_commands(
    const webscene_scene_view* scene,
    uint32_t* count);
WEBSCENE_API void webscene_engine_get_metrics(
    const webscene_engine* engine,
    webscene_engine_metrics* metrics);
WEBSCENE_API uint8_t webscene_engine_get_input_dispatch_metrics(
    const webscene_engine* engine,
    webscene_input_dispatch_metrics* metrics);
WEBSCENE_API uint8_t webscene_engine_get_animation_frame_metrics(
    const webscene_engine* engine,
    webscene_animation_frame_metrics* metrics);
WEBSCENE_API uint8_t webscene_engine_get_scene_flow_metrics(
    const webscene_engine* engine,
    webscene_scene_flow_metrics* metrics);
WEBSCENE_API uint8_t webscene_engine_get_resize_frame_metrics(
    const webscene_engine* engine,
    webscene_resize_frame_metrics* metrics);
WEBSCENE_API uint8_t webscene_engine_get_resource_cache_metrics(
    const webscene_engine* engine,
    webscene_resource_cache_metrics* metrics);
WEBSCENE_API uint8_t webscene_engine_get_runtime_work_metrics(
    const webscene_engine* engine,
    webscene_runtime_work_metrics* metrics);
WEBSCENE_API uint8_t webscene_engine_set_runtime_work_metrics_enabled(
    webscene_engine* engine,
    uint8_t enabled);
WEBSCENE_API uint8_t webscene_engine_get_process_cache_metrics(
    const webscene_engine* engine,
    webscene_process_cache_metrics* metrics);
WEBSCENE_API uint8_t webscene_engine_get_memory_metrics(
    const webscene_engine* engine,
    webscene_engine_memory_metrics* metrics);

#ifdef __cplusplus
}
#endif
