    native_document& document;
    std::function<v8_dom_runtime::viewport_metrics()> viewport_provider;
    resource_loader load_resource_callback;
    static constexpr size_t maximum_pending_fetches = 64U;
    std::vector<pending_fetch_task> pending_fetches;
    static constexpr size_t maximum_resource_prefetches = 16U;
    std::mutex resource_prefetch_mutex;
    std::unordered_map<std::string, std::shared_future<prefetched_resource>>
        prefetched_resources;
    static constexpr size_t maximum_frame_preparations = 16U;
    std::unordered_map<uint32_t, pending_frame_preparation>
        pending_frame_preparations;
    std::shared_ptr<shared_isolate_state> shared_isolate;
    v8::ArrayBuffer::Allocator* allocator{nullptr};
    v8::Isolate* isolate{nullptr};
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    v8::CpuProfiler* cpu_profiler{nullptr};
#endif
    v8::Global<v8::Context> context;
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
    static constexpr size_t maximum_pending_inspector_actions = 1024U;
    static constexpr size_t maximum_pending_inspector_action_bytes =
        16U * 1024U * 1024U;
    static constexpr size_t maximum_inspector_promise_rejections = 1024U;
    std::atomic<inspector_state*> inspector_state_value{nullptr};
    std::atomic<bool> inspector_ready{false};
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
    v8::Global<v8::FunctionTemplate> event_target_template;
    v8::Global<v8::FunctionTemplate> node_template;
    v8::Global<v8::FunctionTemplate> attr_template;
    v8::Global<v8::FunctionTemplate> character_data_template;
    v8::Global<v8::FunctionTemplate> text_template;
    v8::Global<v8::FunctionTemplate> comment_template;
    v8::Global<v8::FunctionTemplate> processing_instruction_template;
    v8::Global<v8::FunctionTemplate> document_fragment_template;
    v8::Global<v8::FunctionTemplate> shadow_root_template;
#endif
    v8::Global<v8::FunctionTemplate> element_template;
#if defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
    v8::Global<v8::FunctionTemplate> html_element_template;
    v8::Global<v8::FunctionTemplate> html_table_cell_element_template;
    v8::Global<v8::FunctionTemplate> html_form_element_template;
    v8::Global<v8::FunctionTemplate> html_select_element_template;
    v8::Global<v8::FunctionTemplate> html_script_element_template;
    v8::Global<v8::FunctionTemplate> html_slot_element_template;
    v8::Global<v8::FunctionTemplate> html_style_element_template;
    uint32_t next_standalone_event_target_id{0x80000000U};
#endif
    v8::Global<v8::ObjectTemplate> style_template;
    v8::Global<v8::ObjectTemplate> frame_document_template;
    v8::Global<v8::ObjectTemplate> frame_window_template;
    v8::Global<v8::FunctionTemplate> html_collection_template;
    v8::Global<v8::Object> dom_implementation_object;
    v8::Global<v8::Object> document_object;
    v8::Global<v8::ObjectTemplate> document_template;
    v8::Global<v8::Context> frame_context;
    // Keep native retained values in a high, positive Int64 range so they
    // remain disjoint from host-provided handles
    // (JavaScriptObjectReference reserves non-positive IDs).
    uint64_t next_interop_handle{UINT64_C(0x4000000000000000)};
    std::unordered_map<uint64_t, v8::Global<v8::Value>> interop_handles;
    std::unordered_map<uint64_t, std::unique_ptr<pending_interop_promise>>
        pending_interop_promises;
    std::unordered_map<uint64_t, pending_callback_promise>
        pending_callback_promises;
    std::unordered_map<uint64_t, v8::Global<v8::Object>> node_wrappers;
    uint32_t next_attr_id{1U};
    std::unordered_map<uint32_t, std::unique_ptr<attr_state>> attr_states;
    std::unordered_map<uint32_t, v8::Global<v8::Object>> attr_wrappers;
    std::unordered_map<std::string, attr_state*> attached_attributes;
    std::unordered_map<uint64_t, v8::Global<v8::Object>> class_list_wrappers;
    std::unordered_map<uint64_t, v8::Global<v8::Object>> style_wrappers;
    std::unordered_map<uint64_t, v8::Global<v8::Object>> stylesheet_wrappers;
    std::unordered_map<uint64_t, v8::Global<v8::Object>> computed_style_wrappers;
    std::unordered_set<uint64_t> detached_document_roots;
    std::unordered_map<uint64_t, v8::Global<v8::Object>> detached_documents;
    // Document.getElementById overwhelmingly targets unique IDs. Cache the
    // first tree-order match per document root, and rebuild lazily after any
    // DOM generation change so duplicate IDs, reparents, detached nodes, and
    // frame/document boundaries retain browser ordering semantics.
    using element_id_index = std::unordered_map<std::string, dom_node*>;
    std::unordered_map<const dom_node*, element_id_index> element_id_indexes;
    std::unordered_map<const dom_node*, element_id_index> element_named_indexes;
    std::unordered_map<const dom_node*, element_id_index>
        document_named_form_indexes;
    uint64_t element_id_index_generation{
        std::numeric_limits<uint64_t>::max()};
    std::vector<std::pair<std::string, std::string>> document_cookies;
    std::unordered_map<uint64_t, v8::Global<v8::Object>> canvas_contexts;
    std::unordered_map<uint64_t, std::unique_ptr<canvas_state>> canvas_states;
    std::unordered_map<uint32_t, v8::Global<v8::Object>> frame_documents;
    std::unordered_map<uint32_t, dom_node*> provisional_frame_bodies;
    std::unordered_map<uint32_t, v8::Global<v8::Object>> frame_windows;
    std::unordered_map<uint32_t, std::pair<float, float>> frame_window_scroll_offsets;
    std::unordered_map<uint32_t, v8::Global<v8::Object>> actual_frame_windows;
    std::unordered_map<uint32_t, v8::Global<v8::Context>> actual_frame_contexts;
    std::unordered_map<uint32_t, v8::Global<v8::Object>> actual_frame_documents;
    std::unordered_map<const dom_node*, std::string> frame_base_addresses;
    session_storage_state outer_session_storage;
    session_storage_state outer_local_storage;
    std::unordered_map<uint32_t, std::unique_ptr<session_storage_state>> frame_session_storage;
    std::vector<document_start_script> current_document_start_scripts;
    std::unordered_map<uint32_t, v8::Global<v8::Function>> frame_load_listeners;
    std::unordered_map<std::string, std::vector<event_listener_registration>>
        frame_event_listeners;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    std::unordered_map<std::string, uint64_t> event_dispatch_counts;
    std::unordered_map<std::string, std::unordered_map<uint32_t, uint64_t>>
        event_dispatch_target_counts;
    std::unordered_map<std::string, uint64_t> event_callback_counts;
    std::unordered_map<std::string, std::unordered_map<uint32_t, uint64_t>>
        event_callback_target_counts;
    std::unordered_map<std::string, std::unordered_map<size_t, uint64_t>>
        event_callback_index_counts;
    std::string last_mousemove_ancestry;
#endif
    std::vector<event_listener_registration> resize_listeners;
    std::vector<std::unique_ptr<media_query_list_state>> media_query_lists;
    std::vector<timer_task> timers;
    std::deque<pending_window_message> pending_window_messages;
    std::vector<pending_programmatic_scroll_event>
        pending_programmatic_scroll_events;
    native_websocket_transport websocket_transport;
    std::unordered_map<uint64_t, websocket_binding> websocket_bindings;
    std::vector<connected_resource_task> connected_resources;
    std::mutex host_request_mutex;
    std::deque<std::string> host_requests;
    std::function<void()> host_request_available;
    std::function<void()> interop_callback_available;
    v8_dom_runtime::interop_callback_sink_v3 interop_callback_sink;
    std::function<void()> runtime_work_available;
    runtime_diagnostics* diagnostics{nullptr};
    std::mutex console_message_mutex;
    std::deque<std::string> console_messages;
    uint64_t next_host_request_id{0};
    std::vector<std::unique_ptr<resize_observer_state>> resize_observers;
    bool resize_observers_pending{false};
    // DOM mutations stay on the native fast path until a realm registers its
    // first custom element. Once activated, the flag remains enabled for this
    // runtime so reactions in any outer or frame realm cannot be skipped.
    bool custom_elements_active{false};
    std::vector<std::string> loaded_resource_names;
    uint32_t next_timer_id{1};
    uint32_t active_timer_id{0};
    bool active_timer_cancelled{false};
    bool prefer_timer_task{true};
    std::atomic<bool> work_metrics_enabled{false};
    v8_dom_runtime::work_metrics work{};
    uint32_t next_object_url_id{1};
    std::unordered_map<std::string, std::string> object_urls;
    // Object URLs serve two browser contracts. Frame/image navigation consumes
    // the authored Blob text, while an <a download> activation needs the exact
    // byte payload and MIME type. Keep both representations under the same URL.
    std::unordered_map<std::string, std::string> object_url_download_payloads;
    // Canvas.toBlob() intentionally carries no encoded bytes. Preserve its
    // retained canvas identity across URL.createObjectURL() so an anchor
    // download can ask the desktop compositor for the exact PNG pixels.
    std::unordered_map<std::string, uint32_t> object_url_canvas_node_ids;
    std::vector<dom_node*> pending_frame_hydrations;
    std::unordered_map<uint32_t, frame_hydration_state> frame_hydration_states;
    bool prefer_frame_hydration_task{true};
    static constexpr size_t detached_dom_gc_threshold = 512U;
    static constexpr size_t maximum_detached_dom_release_roots_per_slice = 16U;
    std::vector<uint32_t> detached_dom_roots;
    std::vector<uint32_t> detached_dom_gc_scan_roots;
    size_t detached_dom_gc_scan_index{0U};
    size_t detached_nodes_since_gc{0U};
    int64_t external_dom_memory_accounted_bytes{0};
    uint64_t released_detached_dom_nodes{0U};
    bool detached_dom_gc_requested{false};
    bool detached_dom_gc_deferred_once{false};
    size_t detached_dom_gc_cycle_retained_roots{0U};
    uint8_t detached_dom_gc_retry_count{0U};
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    uint64_t reconnected_wrapper_retention_subtrees{0U};
    uint64_t reconnected_wrapper_retention_nodes{0U};
    uint64_t detached_dom_release_batches{0U};
    uint64_t detached_dom_release_roots{0U};
    uint64_t detached_dom_release_slices{0U};
    uint64_t detached_dom_release_max_roots_per_slice{0U};
    uint64_t detached_dom_idle_gc_notifications{0U};
#endif
    std::string last_error;
    std::deque<pending_promise_rejection> pending_promise_rejections;
    std::filesystem::path resource_root;
    std::string document_base_address;
    std::string frame_base_address;
    std::vector<css_rule> css_rules;
    static constexpr size_t maximum_compiled_css_selector_lists = 512U;
    mutable std::unordered_map<
        std::string,
        compiled_css_selector_list,
        transparent_string_hash,
        transparent_string_equal>
        compiled_css_selector_lists;
    static constexpr size_t maximum_compiled_class_token_lists = 4096U;
    mutable std::unordered_map<std::string, std::vector<std::string>>
        compiled_class_token_lists;
    uint32_t active_stylesheet_owner_id{0};
    uint32_t active_stylesheet_shadow_scope_root_id{0};
    std::unordered_map<std::string, css_opacity_keyframes> opacity_keyframes;
    css_index_string_map<std::vector<size_t>> css_rules_by_class;
    css_index_string_map<std::vector<size_t>> css_rules_by_id;
    css_index_string_map<std::vector<size_t>> css_rules_by_tag;
    css_index_string_map<std::vector<size_t>> css_rules_by_attribute;
    css_index_string_map<std::vector<size_t>> css_rules_by_variable_reference;
    std::vector<size_t> css_focus_rules;
    std::vector<size_t> unindexed_css_rules;
    css_index_string_set css_attribute_dependencies;
    css_index_string_set css_descendant_attribute_dependencies;
    std::vector<hover_selector_dependency> hover_selector_dependencies;
    std::unordered_map<std::string, std::string> css_variables;
    std::unordered_set<std::string> important_css_variables;
    const dom_node* active_css_cascade_root{nullptr};
    std::unordered_map<const dom_node*, css_cascade_state> inactive_css_cascades;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    mutable std::mutex feature_observation_mutex;
    std::unordered_map<std::string, feature_observation> feature_observations;
    std::unordered_map<std::string, std::unordered_set<uint32_t>>
        composition_observed_nodes;
    // Only the runtime worker writes observations. Cached node pointers remain
    // stable across unordered_map rehashes, so repeat decisions can increment
    // atomically without taking the report snapshot mutex on every hot API call.
    std::unordered_map<std::string, feature_observation*> feature_observation_writer_cache;
    std::unordered_map<const char*, feature_observation*>
        canvas_feature_observation_writer_cache;
#endif
    uint64_t frame_script_execution_count{0};
    uint64_t frame_script_error_count{0};
    std::shared_ptr<std::atomic<uint64_t>> external_script_source_byte_count{
        std::make_shared<std::atomic<uint64_t>>(0U)};
    static constexpr size_t maximum_compilation_cache_entries = 256U;
    static constexpr size_t maximum_compilation_cache_source_bytes = 256U * 1024U * 1024U;
    static constexpr size_t minimum_hot_cache_source_bytes = 4U * 1024U;
    static constexpr uint64_t maximum_persistent_cache_entry_bytes = 64U * 1024U * 1024U;
    static constexpr uint64_t minimum_mapped_cache_body_bytes = 64U * 1024U;
    static constexpr size_t maximum_persistent_cache_entries = 1024U;
    static constexpr uint64_t maximum_persistent_cache_bytes = 512U * 1024U * 1024U;
    std::function<void(const std::string&, const std::string&)> stylesheet_consumer;
    static constexpr uint64_t maximum_resource_cache_entry_bytes = 64U * 1024U * 1024U;
    static constexpr uint32_t maximum_resource_entity_tag_bytes = 4096U;
    static constexpr size_t maximum_resource_cache_entries = 2048U;
    static constexpr uint64_t maximum_resource_cache_bytes = 512U * 1024U * 1024U;
    static constexpr size_t maximum_process_resource_cache_entries = 512U;
    static constexpr size_t maximum_process_resource_cache_bytes = 64U * 1024U * 1024U;
    static constexpr size_t maximum_process_resource_load_entries = 512U;
    static constexpr size_t maximum_process_compilation_cache_entries = 512U;
    static constexpr size_t maximum_process_compilation_cache_bytes = 256U * 1024U * 1024U;
    static constexpr size_t minimum_shared_script_source_bytes = 4U * 1024U;
    static constexpr size_t maximum_process_script_source_entries = 2048U;
    inline static std::mutex process_resource_cache_mutex;
    inline static std::unordered_map<std::string, process_resource_cache_entry>
        process_resource_cache;
    inline static size_t process_resource_cache_bytes{0U};
    inline static size_t process_resource_mapped_cache_bytes{0U};
    inline static uint64_t process_resource_cache_access{0U};
    inline static std::mutex process_resource_load_mutex;
    inline static std::unordered_map<
        std::string,
        std::shared_ptr<process_resource_load_entry>> process_resource_loads;
    inline static uint64_t process_resource_load_access{0U};
    inline static std::mutex process_compilation_cache_mutex;
    inline static std::unordered_map<
        std::string,
        std::shared_ptr<process_compilation_entry>> process_compilation_cache;
    inline static size_t process_compilation_cache_bytes{0U};
    inline static size_t process_compilation_mapped_cache_bytes{0U};
    inline static uint64_t process_compilation_cache_access{0U};
    inline static std::mutex process_script_source_mutex;
    inline static std::unordered_map<
        std::string,
        process_script_source_entry> process_script_sources;
    inline static std::unordered_map<
        std::string,
        process_two_byte_script_source_entry> process_two_byte_script_sources;
    inline static uint64_t process_script_source_access{0U};
    std::string compilation_cache_directory;
    std::unordered_map<std::string, compilation_cache_entry> compilation_cache;
    std::deque<std::string> compilation_cache_order;
    std::unordered_set<std::string> pending_compilation_cache_touches;
    size_t compilation_cache_bytes{0};
    uint64_t compilation_request_count{0};
    uint64_t compilation_memory_hit_count{0};
    uint64_t compilation_persistent_hit_count{0};
    uint64_t compilation_persistent_miss_count{0};
    uint64_t compilation_cache_rejection_count{0};
    uint64_t compilation_cache_bytes_read_count{0};
    uint64_t compilation_cache_bytes_written_count{0};
    uint64_t compilation_time_nanosecond_count{0};
    uint64_t html_parse_duration_ns{0};
    uint64_t html_parse_error_count{0};
    uint64_t html_parse_callback_count{0};
    uint64_t html_parse_element_count{0};
    uint64_t html_parse_text_append_count{0};
    uint64_t html_parse_comment_count{0};
    uint64_t html_parse_doctype_count{0};
    uint64_t html_parse_rust_allocation_count{0};
    uint64_t html_parse_rust_peak_bytes{0};
    uint64_t html_parse_rust_retained_bytes{0};
    uint64_t process_compilation_memory_hit_count{0};
    uint64_t process_compilation_leader_count{0};
    uint64_t process_compilation_waiter_count{0};
    uint64_t process_compilation_shared_byte_count{0};
    std::atomic<uint64_t> resource_cache_request_count{0};
    std::atomic<uint64_t> resource_cache_hit_count{0};
    std::atomic<uint64_t> resource_cache_miss_count{0};
    std::atomic<uint64_t> resource_cache_rejection_count{0};
    std::atomic<uint64_t> resource_cache_bytes_read_count{0};
    std::atomic<uint64_t> resource_cache_bytes_written_count{0};
    std::atomic<uint64_t> resource_cache_memory_hit_count{0};
    std::atomic<uint64_t> process_resource_memory_hit_count{0};
    std::atomic<uint64_t> process_resource_load_leader_count{0};
    std::atomic<uint64_t> process_resource_load_waiter_count{0};
    std::atomic<uint64_t> process_resource_shared_byte_count{0};
    uint64_t process_script_source_memory_hit_count{0};
    uint64_t process_script_source_shared_byte_count{0};
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    uint64_t css_profile_candidate_checks{0};
    uint64_t css_profile_matched_rules{0};
    uint64_t css_profile_declaration_applications{0};
    uint64_t css_profile_reset_nanoseconds{0};
    uint64_t css_profile_matching_nanoseconds{0};
    uint64_t css_profile_declarations_nanoseconds{0};
    uint64_t css_profile_finalize_nanoseconds{0};
    size_t last_traced_unindexed_css_count{0};
#endif
    uint32_t resource_writes_since_prune{0};
    uint32_t persistent_writes_since_prune{0};
    uint64_t input_event_dispatch_count{0};
    uint64_t input_callback_invocation_count{0};
    dom_node* pointer_capture_target{nullptr};
    dom_node* pointer_down_target{nullptr};
    dom_node* scrollbar_drag_target{nullptr};
    bool scrollbar_drag_vertical{false};
    double scrollbar_drag_pointer_origin{0};
    float scrollbar_drag_scroll_origin{0};
    float scrollbar_drag_maximum{0};
    float scrollbar_drag_thumb_travel{0};
    dom_node* hover_target{nullptr};
    uint32_t current_cursor_kind_value{WEBSCENE_CURSOR_DEFAULT};
    dom_node* current_related_target{nullptr};
    double pointer_down_x{0};
    double pointer_down_y{0};
    double last_pointer_x{0};
    double last_pointer_y{0};
    double current_movement_x{0};
    double current_movement_y{0};
    bool has_pointer_position{false};
    uint64_t current_input_sequence{0};
    uint32_t pending_text_input_target_id{0U};
    bool pending_text_input_from_keydown{false};
    dom_node* active_element{nullptr};
    bool focus_visible{false};
    bool document_visible{true};
    double last_animation_frame_timestamp_ms{0};
    double caret_blink_epoch_ms{0};
    std::string frame_last_error_value;
    const std::string empty_string;
    struct pending_style_recascade final {
        dom_node* root{nullptr};
        dom_node* cascade_root{nullptr};
        bool subtree{false};
        std::string reason;
    };
    bool batch_style_recascades{true};
    bool batch_connected_resource_style_recascades{true};
    bool prefetch_iframe_documents{true};
    bool prepare_iframe_subresources{true};
    bool defer_compilation_cache_touches{true};
    bool cooperative_iframe_hydration{true};
    bool filter_stylesheet_candidates{true};
    uint32_t style_recascade_batch_depth{0};
    std::vector<pending_style_recascade> pending_style_recascades;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    uint64_t style_recascade_schedule_requests{0U};
    uint64_t style_recascade_coalesced_requests{0U};
    uint64_t style_recascade_flush_batches{0U};
    uint64_t style_recascade_flush_roots{0U};
    uint64_t style_recascade_flush_node_roots{0U};
    uint64_t style_recascade_flush_subtree_roots{0U};
    uint64_t style_recascade_noop_removals{0U};
#endif

    class style_recascade_batch_scope final {
    public:
        explicit style_recascade_batch_scope(
            implementation& owner,
            bool enabled = true) noexcept
            : owner_(owner.batch_style_recascades && enabled ? &owner : nullptr)
        {
            if (owner_ != nullptr) ++owner_->style_recascade_batch_depth;
        }

        ~style_recascade_batch_scope()
        {
            finish();
        }

        void finish()
        {
            if (owner_ == nullptr) return;
            auto* owner = owner_;
            owner_ = nullptr;
            if (--owner->style_recascade_batch_depth == 0U) {
                owner->flush_pending_style_recascades();
            }
        }

    private:
        implementation* owner_;
    };
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    uint64_t last_resize_outer_listeners_ns{0};
    uint64_t last_resize_frame_listeners_ns{0};
    uint64_t last_resize_layout_ns{0};
    uint64_t last_resize_observers_ns{0};
    uint64_t client_geometry_layout_reuse_count{0};
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    bool profile_bindings{false};
    bool profile_resize_cpu{false};
    bool resize_cpu_profile_captured{false};
    bool profile_startup{false};
    bool profile_css{false};
    bool resize_profile_active{false};
    std::array<binding_callback_stats, binding_category_count> binding_totals{};
    std::unordered_map<std::string, binding_callback_stats> startup_binding_profiles;
    std::unordered_map<std::string, binding_callback_stats> startup_forced_layout_profiles;
    std::array<binding_callback_stats, binding_category_count> last_resize_binding_profile{};
    std::unordered_map<std::string, uint64_t> last_resize_style_property_counts;
    std::unordered_map<std::string, uint64_t> last_resize_style_target_counts;
    std::unordered_map<std::string, uint64_t> last_resize_attribute_counts;
    std::unordered_map<std::string, uint64_t> last_resize_geometry_counts;
    uint64_t last_resize_redundant_style_writes{0};
    std::string last_resize_cpu_profile;
    binding_callback_stats startup_resource_read{};
    binding_callback_stats startup_css_parse{};
    binding_callback_stats startup_css_apply{};
    binding_callback_stats startup_css_incremental_apply{};
    binding_callback_stats startup_subtree_recascade{};
    binding_callback_stats startup_stylesheet_recascade{};
    binding_callback_stats startup_script_execute{};
    binding_callback_stats startup_script_compile{};
    binding_callback_stats startup_script_run{};
    binding_callback_stats startup_layout{};
    binding_callback_stats startup_frame_hydrate{};
    binding_callback_stats startup_frame_prepare{};
    binding_callback_stats startup_frame_prepare_wait{};
    uint64_t startup_frame_prepare_lead_nanoseconds{0};
    uint64_t startup_frame_hydration_slices{0};
    uint64_t startup_frame_hydration_yields{0};
    uint64_t startup_frame_hydration_max_slice_nanoseconds{0};
    uint64_t startup_stylesheet_recascade_nodes{0};
    uint64_t startup_stylesheet_candidate_nodes{0};
    uint64_t startup_stylesheet_variable_nodes{0};
    uint64_t startup_connected_resources{0};
    uint64_t startup_raf_scheduled{0};
    uint64_t startup_raf_executed{0};
    uint64_t startup_timer_scheduled{0};
    uint64_t startup_timer_executed{0};
    double startup_max_timer_delay_ms{0};
    binding_callback_stats startup_task_callbacks{};
    std::unordered_map<std::string, binding_callback_stats> startup_script_profiles;
    std::unordered_map<std::string, binding_callback_stats> startup_task_profiles;
    std::unordered_map<std::string, startup_nested_phase_stats>
        startup_script_phase_profiles;
    std::unordered_map<std::string, startup_nested_phase_stats>
        startup_task_phase_profiles;
    std::unordered_map<std::string, startup_nested_phase_stats>
        startup_frame_phase_profiles;
    std::chrono::steady_clock::time_point startup_profile_started{};
    std::chrono::steady_clock::time_point startup_frame_started{};

    startup_phase_snapshot capture_startup_phase_snapshot() const noexcept
    {
        return startup_phase_snapshot{
            startup_resource_read,
            startup_css_parse,
            startup_css_apply,
            startup_css_incremental_apply,
            startup_subtree_recascade,
            startup_stylesheet_recascade,
            startup_layout,
            startup_script_execute,
            startup_stylesheet_recascade_nodes,
            startup_stylesheet_variable_nodes,
            document.layout_passes(),
            document.dirty()};
    }

    void record_startup_nested_phases(
        std::unordered_map<std::string, startup_nested_phase_stats>& profiles,
        const std::string& name,
        const startup_phase_snapshot& before,
        uint64_t elapsed_nanoseconds)
    {
        auto& profile = profiles[name];
        ++profile.elapsed.calls;
        profile.elapsed.nanoseconds += elapsed_nanoseconds;
        accumulate_callback_delta(
            profile.resource_read,
            startup_resource_read,
            before.resource_read);
        accumulate_callback_delta(
            profile.css_parse,
            startup_css_parse,
            before.css_parse);
        accumulate_callback_delta(profile.css_apply, startup_css_apply, before.css_apply);
        accumulate_callback_delta(
            profile.css_incremental_apply,
            startup_css_incremental_apply,
            before.css_incremental_apply);
        accumulate_callback_delta(
            profile.subtree_recascade,
            startup_subtree_recascade,
            before.subtree_recascade);
        accumulate_callback_delta(
            profile.stylesheet_recascade,
            startup_stylesheet_recascade,
            before.stylesheet_recascade);
        accumulate_callback_delta(profile.layout, startup_layout, before.layout);
        accumulate_callback_delta(
            profile.script_execute,
            startup_script_execute,
            before.script_execute);
        profile.stylesheet_nodes +=
            startup_stylesheet_recascade_nodes - before.stylesheet_nodes;
        profile.stylesheet_variable_nodes +=
            startup_stylesheet_variable_nodes - before.stylesheet_variable_nodes;
        profile.layout_passes += document.layout_passes() - before.layout_passes;
        const auto dirty = document.dirty();
        if (!before.document_dirty && dirty) ++profile.clean_to_dirty_transitions;
        if (dirty) ++profile.tasks_left_dirty;
    }
#endif
