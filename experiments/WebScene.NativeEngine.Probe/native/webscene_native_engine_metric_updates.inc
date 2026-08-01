#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
    void update_compilation_metrics()
    {
        if (runtime_ == nullptr) return;
        compilation_requests_.store(runtime_->compilation_requests(), std::memory_order_relaxed);
        compilation_memory_hits_.store(runtime_->compilation_memory_hits(), std::memory_order_relaxed);
        compilation_persistent_hits_.store(runtime_->compilation_persistent_hits(), std::memory_order_relaxed);
        compilation_persistent_misses_.store(runtime_->compilation_persistent_misses(), std::memory_order_relaxed);
        compilation_cache_rejections_.store(runtime_->compilation_cache_rejections(), std::memory_order_relaxed);
        compilation_cache_bytes_read_.store(runtime_->compilation_cache_bytes_read(), std::memory_order_relaxed);
        compilation_cache_bytes_written_.store(runtime_->compilation_cache_bytes_written(), std::memory_order_relaxed);
        compilation_time_nanoseconds_.store(runtime_->compilation_time_nanoseconds(), std::memory_order_relaxed);
        process_compilation_memory_hits_.store(
            runtime_->process_compilation_memory_hits(),
            std::memory_order_relaxed);
        process_compilation_leaders_.store(
            runtime_->process_compilation_leaders(),
            std::memory_order_relaxed);
        process_compilation_waiters_.store(
            runtime_->process_compilation_waiters(),
            std::memory_order_relaxed);
        process_compilation_shared_bytes_.store(
            runtime_->process_compilation_shared_bytes(),
            std::memory_order_relaxed);
        process_resource_memory_hits_.store(
            runtime_->process_resource_memory_hits(),
            std::memory_order_relaxed);
        process_resource_load_leaders_.store(
            runtime_->process_resource_load_leaders(),
            std::memory_order_relaxed);
        process_resource_load_waiters_.store(
            runtime_->process_resource_load_waiters(),
            std::memory_order_relaxed);
        process_resource_shared_bytes_.store(
            runtime_->process_resource_shared_bytes(),
            std::memory_order_relaxed);
        process_script_source_memory_hits_.store(
            runtime_->process_script_source_memory_hits(),
            std::memory_order_relaxed);
        process_script_source_shared_bytes_.store(
            runtime_->process_script_source_shared_bytes(),
            std::memory_order_relaxed);
        shared_isolate_slot_.store(
            runtime_->shared_isolate_slot(),
            std::memory_order_relaxed);
        shared_isolate_active_contexts_.store(
            runtime_->shared_isolate_active_contexts(),
            std::memory_order_relaxed);
        shared_isolate_peak_contexts_.store(
            runtime_->shared_isolate_peak_contexts(),
            std::memory_order_relaxed);
        resource_cache_requests_.store(runtime_->resource_cache_requests(), std::memory_order_relaxed);
        resource_cache_hits_.store(runtime_->resource_cache_hits(), std::memory_order_relaxed);
        resource_cache_misses_.store(runtime_->resource_cache_misses(), std::memory_order_relaxed);
        resource_cache_rejections_.store(runtime_->resource_cache_rejections(), std::memory_order_relaxed);
        resource_cache_bytes_read_.store(runtime_->resource_cache_bytes_read(), std::memory_order_relaxed);
        resource_cache_bytes_written_.store(runtime_->resource_cache_bytes_written(), std::memory_order_relaxed);
        input_events_dispatched_.store(runtime_->input_events_dispatched(), std::memory_order_relaxed);
        input_callbacks_invoked_.store(runtime_->input_callbacks_invoked(), std::memory_order_relaxed);

        // Heap code/metadata statistics walk V8's code spaces and are orders of
        // magnitude more expensive than the atomic compilation/resource counters
        // above. This method runs after animation frames and input dispatch, so
        // collecting a full memory census on every turn directly reduces interactive
        // frame cadence (and scales that cost with every active document). Memory
        // metrics are diagnostic snapshots, not scheduling inputs; keep the cached
        // ABI view reasonably fresh without placing the census on the hot path.
        const auto memory_metrics_now = std::chrono::steady_clock::now();
        constexpr auto memory_metrics_refresh_interval =
            std::chrono::seconds(5);
        if (last_memory_metrics_update_
                != std::chrono::steady_clock::time_point{}
            && memory_metrics_now - last_memory_metrics_update_
                < memory_metrics_refresh_interval) {
            return;
        }
        last_memory_metrics_update_ = memory_metrics_now;
        const auto memory = runtime_->read_memory_metrics();
        v8_total_heap_bytes_.store(memory.total_heap_bytes, std::memory_order_relaxed);
        v8_used_heap_bytes_.store(memory.used_heap_bytes, std::memory_order_relaxed);
        v8_executable_heap_bytes_.store(memory.executable_heap_bytes, std::memory_order_relaxed);
        v8_physical_heap_bytes_.store(memory.physical_heap_bytes, std::memory_order_relaxed);
        v8_external_bytes_.store(memory.external_bytes, std::memory_order_relaxed);
        v8_malloced_bytes_.store(memory.malloced_bytes, std::memory_order_relaxed);
        v8_peak_malloced_bytes_.store(memory.peak_malloced_bytes, std::memory_order_relaxed);
        v8_code_and_metadata_bytes_.store(
            memory.code_and_metadata_bytes,
            std::memory_order_relaxed);
        v8_bytecode_and_metadata_bytes_.store(
            memory.bytecode_and_metadata_bytes,
            std::memory_order_relaxed);
        v8_external_script_source_bytes_.store(
            memory.external_script_source_bytes,
            std::memory_order_relaxed);
        v8_young_space_used_bytes_.store(
            memory.young_space_used_bytes,
            std::memory_order_relaxed);
        v8_young_space_physical_bytes_.store(
            memory.young_space_physical_bytes,
            std::memory_order_relaxed);
        v8_old_space_used_bytes_.store(
            memory.old_space_used_bytes,
            std::memory_order_relaxed);
        v8_old_space_physical_bytes_.store(
            memory.old_space_physical_bytes,
            std::memory_order_relaxed);
        v8_code_space_used_bytes_.store(
            memory.code_space_used_bytes,
            std::memory_order_relaxed);
        v8_code_space_physical_bytes_.store(
            memory.code_space_physical_bytes,
            std::memory_order_relaxed);
        v8_map_space_used_bytes_.store(
            memory.map_space_used_bytes,
            std::memory_order_relaxed);
        v8_map_space_physical_bytes_.store(
            memory.map_space_physical_bytes,
            std::memory_order_relaxed);
        v8_large_object_space_used_bytes_.store(
            memory.large_object_space_used_bytes,
            std::memory_order_relaxed);
        v8_large_object_space_physical_bytes_.store(
            memory.large_object_space_physical_bytes,
            std::memory_order_relaxed);
        v8_read_only_space_used_bytes_.store(
            memory.read_only_space_used_bytes,
            std::memory_order_relaxed);
        v8_read_only_space_physical_bytes_.store(
            memory.read_only_space_physical_bytes,
            std::memory_order_relaxed);
        v8_shared_space_used_bytes_.store(
            memory.shared_space_used_bytes,
            std::memory_order_relaxed);
        v8_shared_space_physical_bytes_.store(
            memory.shared_space_physical_bytes,
            std::memory_order_relaxed);
        v8_trusted_space_used_bytes_.store(
            memory.trusted_space_used_bytes,
            std::memory_order_relaxed);
        v8_trusted_space_physical_bytes_.store(
            memory.trusted_space_physical_bytes,
            std::memory_order_relaxed);
        process_compilation_cache_bytes_.store(
            memory.process_compilation_cache_bytes,
            std::memory_order_relaxed);
        process_compilation_mapped_cache_bytes_.store(
            memory.process_compilation_mapped_cache_bytes,
            std::memory_order_relaxed);
        process_resource_cache_bytes_.store(
            memory.process_resource_cache_bytes,
            std::memory_order_relaxed);
        process_resource_mapped_cache_bytes_.store(
            memory.process_resource_mapped_cache_bytes,
            std::memory_order_relaxed);
        native_dom_node_count_.store(
            memory.native_dom_node_count,
            std::memory_order_relaxed);
        native_dom_node_size_bytes_.store(
            memory.native_dom_node_size_bytes,
            std::memory_order_relaxed);
        native_dom_inline_bytes_.store(
            memory.native_dom_inline_bytes,
            std::memory_order_relaxed);
        native_dom_node_pool_reserved_bytes_.store(
            memory.native_dom_node_pool_reserved_bytes,
            std::memory_order_relaxed);
        native_dom_node_pool_peak_bytes_.store(
            memory.native_dom_node_pool_peak_bytes,
            std::memory_order_relaxed);
        native_dom_table_layout_count_.store(
            memory.native_dom_table_layout_count,
            std::memory_order_relaxed);
        native_dom_table_layout_storage_bytes_.store(
            memory.native_dom_table_layout_storage_bytes,
            std::memory_order_relaxed);
        native_dom_form_control_count_.store(
            memory.native_dom_form_control_count,
            std::memory_order_relaxed);
        native_dom_form_control_storage_bytes_.store(
            memory.native_dom_form_control_storage_bytes,
            std::memory_order_relaxed);
        native_event_listener_count_.store(
            memory.native_event_listener_count,
            std::memory_order_relaxed);
        native_event_listener_storage_bytes_.store(
            memory.native_event_listener_storage_bytes,
            std::memory_order_relaxed);
        native_dom_attribute_node_count_.store(
            memory.native_dom_attribute_node_count,
            std::memory_order_relaxed);
        native_dom_attribute_entry_count_.store(
            memory.native_dom_attribute_entry_count,
            std::memory_order_relaxed);
        native_dom_attribute_storage_bytes_.store(
            memory.native_dom_attribute_storage_bytes,
            std::memory_order_relaxed);
        native_wrapper_handle_count_.store(
            memory.native_wrapper_handle_count,
            std::memory_order_relaxed);
        native_wrapper_storage_bytes_.store(
            memory.native_wrapper_storage_bytes,
            std::memory_order_relaxed);
        native_text_measurement_cache_entry_count_.store(
            memory.native_text_measurement_cache_entry_count,
            std::memory_order_relaxed);
        native_text_measurement_cache_storage_bytes_.store(
            memory.native_text_measurement_cache_storage_bytes,
            std::memory_order_relaxed);
        native_dom_pseudo_storage_bytes_.store(
            memory.native_dom_pseudo_storage_bytes,
            std::memory_order_relaxed);
        native_dom_canvas_node_count_.store(
            memory.native_dom_canvas_node_count,
            std::memory_order_relaxed);
        native_dom_canvas_storage_bytes_.store(
            memory.native_dom_canvas_storage_bytes,
            std::memory_order_relaxed);
        native_dom_animation_count_.store(
            memory.native_dom_animation_count,
            std::memory_order_relaxed);
        native_dom_animation_storage_bytes_.store(
            memory.native_dom_animation_storage_bytes,
            std::memory_order_relaxed);
        native_dom_custom_property_node_count_.store(
            memory.native_dom_custom_property_node_count,
            std::memory_order_relaxed);
        native_dom_custom_property_entry_count_.store(
            memory.native_dom_custom_property_entry_count,
            std::memory_order_relaxed);
        native_dom_custom_property_storage_bytes_.store(
            memory.native_dom_custom_property_storage_bytes,
            std::memory_order_relaxed);
        native_dom_background_image_count_.store(
            memory.native_dom_background_image_count,
            std::memory_order_relaxed);
        native_dom_background_image_storage_bytes_.store(
            memory.native_dom_background_image_storage_bytes,
            std::memory_order_relaxed);
        native_dom_grid_count_.store(
            memory.native_dom_grid_count,
            std::memory_order_relaxed);
        native_dom_grid_storage_bytes_.store(
            memory.native_dom_grid_storage_bytes,
            std::memory_order_relaxed);
        native_dom_textual_style_count_.store(
            memory.native_dom_textual_style_count,
            std::memory_order_relaxed);
        native_dom_textual_style_storage_bytes_.store(
            memory.native_dom_textual_style_storage_bytes,
            std::memory_order_relaxed);
        native_dom_authored_style_node_count_.store(
            memory.native_dom_authored_style_node_count,
            std::memory_order_relaxed);
        native_dom_authored_style_entry_count_.store(
            memory.native_dom_authored_style_entry_count,
            std::memory_order_relaxed);
        native_dom_authored_style_storage_bytes_.store(
            memory.native_dom_authored_style_storage_bytes,
            std::memory_order_relaxed);
        native_css_rule_count_.store(
            memory.native_css_rule_count,
            std::memory_order_relaxed);
        native_css_rule_storage_bytes_.store(
            memory.native_css_rule_storage_bytes,
            std::memory_order_relaxed);
        native_css_index_storage_bytes_.store(
            memory.native_css_index_storage_bytes,
            std::memory_order_relaxed);
        process_shared_css_rule_count_.store(
            memory.process_shared_css_rule_count,
            std::memory_order_relaxed);
        process_shared_css_rule_storage_bytes_.store(
            memory.process_shared_css_rule_storage_bytes,
            std::memory_order_relaxed);
    }
#endif
