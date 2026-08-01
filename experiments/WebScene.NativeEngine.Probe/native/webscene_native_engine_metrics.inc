    void read_metrics(webscene_engine_metrics& result) const
    {
        result.enqueued_inputs = enqueued_inputs_.load(std::memory_order_relaxed);
        result.dropped_inputs = dropped_inputs_.load(std::memory_order_relaxed);
        result.consumed_inputs = consumed_inputs_.load(std::memory_order_relaxed);
        result.published_scenes = published_scenes_.load(std::memory_order_relaxed);
        result.acquired_scenes = acquired_scenes_.load(std::memory_order_relaxed);
        result.executed_scripts = executed_scripts_.load(std::memory_order_relaxed);
        result.script_errors = script_errors_.load(std::memory_order_relaxed);
        result.dom_nodes = dom_nodes_.load(std::memory_order_relaxed);
        result.layout_passes = layout_passes_.load(std::memory_order_relaxed);
        result.iframe_nodes = iframe_nodes_.load(std::memory_order_relaxed);
        result.iframe_html_bytes = iframe_html_bytes_.load(std::memory_order_relaxed);
        result.frame_scripts_executed = frame_scripts_executed_.load(std::memory_order_relaxed);
        result.frame_script_errors = frame_script_errors_.load(std::memory_order_relaxed);
        result.canvas_nodes = canvas_nodes_.load(std::memory_order_relaxed);
        result.component_ready = component_ready_.load(std::memory_order_relaxed);
        result.compilation_requests = compilation_requests_.load(std::memory_order_relaxed);
        result.compilation_memory_hits = compilation_memory_hits_.load(std::memory_order_relaxed);
        result.compilation_persistent_hits = compilation_persistent_hits_.load(std::memory_order_relaxed);
        result.compilation_persistent_misses = compilation_persistent_misses_.load(std::memory_order_relaxed);
        result.compilation_cache_rejections = compilation_cache_rejections_.load(std::memory_order_relaxed);
        result.compilation_cache_bytes_read = compilation_cache_bytes_read_.load(std::memory_order_relaxed);
        result.compilation_cache_bytes_written = compilation_cache_bytes_written_.load(std::memory_order_relaxed);
        result.compilation_time_nanoseconds = compilation_time_nanoseconds_.load(std::memory_order_relaxed);
        result.input_events_dispatched = input_events_dispatched_.load(std::memory_order_relaxed);
        result.input_callbacks_invoked = input_callbacks_invoked_.load(std::memory_order_relaxed);
        result.busiest_canvas_width_milli = busiest_canvas_width_milli_.load(std::memory_order_relaxed);
        result.busiest_canvas_height_milli = busiest_canvas_height_milli_.load(std::memory_order_relaxed);
        result.coalesced_resize_inputs = coalesced_resize_inputs_.load(std::memory_order_relaxed);
        result.applied_resize_inputs = applied_resize_inputs_.load(std::memory_order_relaxed);
        result.last_resize_dispatch_nanoseconds =
            last_resize_dispatch_nanoseconds_.load(std::memory_order_relaxed);
        result.last_scene_publication_nanoseconds =
            last_scene_publication_nanoseconds_.load(std::memory_order_relaxed);
        result.last_resize_outer_listeners_nanoseconds =
            last_resize_outer_listeners_nanoseconds_.load(std::memory_order_relaxed);
        result.last_resize_frame_listeners_nanoseconds =
            last_resize_frame_listeners_nanoseconds_.load(std::memory_order_relaxed);
        result.last_resize_layout_nanoseconds =
            last_resize_layout_nanoseconds_.load(std::memory_order_relaxed);
        result.last_resize_observers_nanoseconds =
            last_resize_observers_nanoseconds_.load(std::memory_order_relaxed);
        result.coalesced_pointer_move_inputs =
            coalesced_pointer_move_inputs_.load(std::memory_order_relaxed);
        result.coalesced_wheel_inputs =
            coalesced_wheel_inputs_.load(std::memory_order_relaxed);
        result.applied_pointer_move_inputs =
            applied_pointer_move_inputs_.load(std::memory_order_relaxed);
        result.applied_wheel_inputs =
            applied_wheel_inputs_.load(std::memory_order_relaxed);
        result.applied_animation_frames =
            applied_animation_frames_.load(std::memory_order_relaxed);
        result.coalesced_animation_frames =
            coalesced_animation_frames_.load(std::memory_order_relaxed);
        result.last_animation_advance_nanoseconds =
            last_animation_advance_nanoseconds_.load(std::memory_order_relaxed);
        result.last_layout_nanoseconds =
            last_layout_nanoseconds_.load(std::memory_order_relaxed);
        result.last_scene_build_nanoseconds =
            last_scene_build_nanoseconds_.load(std::memory_order_relaxed);
        result.maximum_scene_publication_nanoseconds =
            maximum_scene_publication_nanoseconds_.load(std::memory_order_relaxed);
    }

    void read_input_dispatch_metrics(webscene_input_dispatch_metrics& result) const
    {
        result.last_dispatch_nanoseconds =
            last_input_dispatch_nanoseconds_.load(std::memory_order_relaxed);
        result.maximum_dispatch_nanoseconds =
            maximum_input_dispatch_nanoseconds_.load(std::memory_order_relaxed);
        result.last_dispatch_sequence =
            last_input_dispatch_sequence_.load(std::memory_order_relaxed);
        if (result.struct_size >= sizeof(webscene_input_dispatch_metrics)) {
            result.dispatched_inputs =
                dispatched_inputs_.load(std::memory_order_relaxed);
            result.total_dispatch_nanoseconds =
                total_input_dispatch_nanoseconds_.load(
                    std::memory_order_relaxed);
        }
    }

    void read_animation_frame_metrics(
        webscene_animation_frame_metrics& result) const
    {
        result.dispatched_frames =
            dispatched_animation_frames_.load(std::memory_order_relaxed);
        result.total_dispatch_nanoseconds =
            total_animation_frame_dispatch_nanoseconds_.load(
                std::memory_order_relaxed);
        result.last_dispatch_nanoseconds =
            last_animation_frame_dispatch_nanoseconds_.load(
                std::memory_order_relaxed);
        result.maximum_dispatch_nanoseconds =
            maximum_animation_frame_dispatch_nanoseconds_.load(
                std::memory_order_relaxed);
        result.last_timestamp_microseconds =
            last_animation_frame_timestamp_microseconds_.load(
                std::memory_order_relaxed);
    }

    void read_scene_flow_metrics(webscene_scene_flow_metrics& result) const
    {
        result.publication_attempts =
            scene_publication_attempts_.load(std::memory_order_relaxed);
        result.blocked_publications =
            blocked_scene_publications_.load(std::memory_order_relaxed);
        result.acknowledged_scenes =
            acknowledgement_->acknowledged_scenes.load(
                std::memory_order_relaxed);
        result.total_acknowledgement_nanoseconds =
            acknowledgement_->total_acknowledgement_nanoseconds.load(
                std::memory_order_relaxed);
        result.last_acknowledgement_nanoseconds =
            acknowledgement_->last_acknowledgement_nanoseconds.load(
                std::memory_order_relaxed);
        result.maximum_acknowledgement_nanoseconds =
            acknowledgement_->maximum_acknowledgement_nanoseconds.load(
                std::memory_order_relaxed);
        std::lock_guard lock(acknowledgement_->mutex);
        result.acknowledged_revision = acknowledgement_->revision;
    }

    void read_resize_frame_metrics(webscene_resize_frame_metrics& result) const
    {
        result.submitted_pairs =
            submitted_resize_frame_pairs_.load(std::memory_order_relaxed);
        result.applied_pairs =
            applied_resize_frame_pairs_.load(std::memory_order_relaxed);
        result.published_pairs =
            published_resize_frame_pairs_.load(std::memory_order_relaxed);
        result.total_queue_nanoseconds =
            total_resize_frame_queue_nanoseconds_.load(std::memory_order_relaxed);
        result.last_queue_nanoseconds =
            last_resize_frame_queue_nanoseconds_.load(std::memory_order_relaxed);
        result.maximum_queue_nanoseconds =
            maximum_resize_frame_queue_nanoseconds_.load(std::memory_order_relaxed);
        result.total_dispatch_nanoseconds =
            total_resize_frame_dispatch_nanoseconds_.load(std::memory_order_relaxed);
        result.last_dispatch_nanoseconds =
            last_resize_frame_dispatch_nanoseconds_.load(std::memory_order_relaxed);
        result.maximum_dispatch_nanoseconds =
            maximum_resize_frame_dispatch_nanoseconds_.load(std::memory_order_relaxed);
        result.animation_frame_callbacks =
            resize_frame_animation_callbacks_.load(std::memory_order_relaxed);
        result.total_animation_frame_batch_nanoseconds =
            total_resize_frame_animation_batch_nanoseconds_.load(
                std::memory_order_relaxed);
        result.last_animation_frame_batch_nanoseconds =
            last_resize_frame_animation_batch_nanoseconds_.load(
                std::memory_order_relaxed);
        result.maximum_animation_frame_batch_nanoseconds =
            maximum_resize_frame_animation_batch_nanoseconds_.load(
                std::memory_order_relaxed);
        result.total_to_publication_nanoseconds =
            total_resize_frame_to_publication_nanoseconds_.load(
                std::memory_order_relaxed);
        result.last_to_publication_nanoseconds =
            last_resize_frame_to_publication_nanoseconds_.load(
                std::memory_order_relaxed);
        result.maximum_to_publication_nanoseconds =
            maximum_resize_frame_to_publication_nanoseconds_.load(
                std::memory_order_relaxed);
    }

    void read_resource_cache_metrics(webscene_resource_cache_metrics& result) const
    {
        result.requests = resource_cache_requests_.load(std::memory_order_relaxed);
        result.hits = resource_cache_hits_.load(std::memory_order_relaxed);
        result.misses = resource_cache_misses_.load(std::memory_order_relaxed);
        result.rejections = resource_cache_rejections_.load(std::memory_order_relaxed);
        result.bytes_read = resource_cache_bytes_read_.load(std::memory_order_relaxed);
        result.bytes_written = resource_cache_bytes_written_.load(std::memory_order_relaxed);
    }

    void read_process_cache_metrics(webscene_process_cache_metrics& result) const
    {
        result.compilation_memory_hits =
            process_compilation_memory_hits_.load(std::memory_order_relaxed);
        result.compilation_leaders =
            process_compilation_leaders_.load(std::memory_order_relaxed);
        result.compilation_waiters =
            process_compilation_waiters_.load(std::memory_order_relaxed);
        result.compilation_shared_bytes =
            process_compilation_shared_bytes_.load(std::memory_order_relaxed);
        result.resource_memory_hits =
            process_resource_memory_hits_.load(std::memory_order_relaxed);
        result.resource_load_leaders =
            process_resource_load_leaders_.load(std::memory_order_relaxed);
        result.resource_load_waiters =
            process_resource_load_waiters_.load(std::memory_order_relaxed);
        result.resource_shared_bytes =
            process_resource_shared_bytes_.load(std::memory_order_relaxed);
        if (result.struct_size
            >= offsetof(
                webscene_process_cache_metrics,
                shared_isolate_slot)) {
            result.script_source_memory_hits =
                process_script_source_memory_hits_.load(std::memory_order_relaxed);
            result.script_source_shared_bytes =
                process_script_source_shared_bytes_.load(std::memory_order_relaxed);
        }
        if (result.struct_size >= sizeof(webscene_process_cache_metrics)) {
            result.shared_isolate_slot =
                shared_isolate_slot_.load(std::memory_order_relaxed);
            result.shared_isolate_active_contexts =
                shared_isolate_active_contexts_.load(std::memory_order_relaxed);
            result.shared_isolate_peak_contexts =
                shared_isolate_peak_contexts_.load(std::memory_order_relaxed);
        }
    }

    void read_memory_metrics(webscene_engine_memory_metrics& result) const
    {
        result.v8_total_heap_bytes =
            v8_total_heap_bytes_.load(std::memory_order_relaxed);
        result.v8_used_heap_bytes =
            v8_used_heap_bytes_.load(std::memory_order_relaxed);
        result.v8_executable_heap_bytes =
            v8_executable_heap_bytes_.load(std::memory_order_relaxed);
        result.v8_physical_heap_bytes =
            v8_physical_heap_bytes_.load(std::memory_order_relaxed);
        result.v8_external_bytes =
            v8_external_bytes_.load(std::memory_order_relaxed);
        result.v8_malloced_bytes =
            v8_malloced_bytes_.load(std::memory_order_relaxed);
        result.v8_peak_malloced_bytes =
            v8_peak_malloced_bytes_.load(std::memory_order_relaxed);
        result.latest_scene_bytes =
            latest_scene_bytes_.load(std::memory_order_relaxed);
        result.process_compilation_cache_bytes =
            process_compilation_cache_bytes_.load(std::memory_order_relaxed);
        result.process_resource_cache_bytes =
            process_resource_cache_bytes_.load(std::memory_order_relaxed);
        constexpr auto attributed_memory_metrics_size =
            offsetof(webscene_engine_memory_metrics, low_memory_notifications);
        if (result.struct_size >= attributed_memory_metrics_size) {
            result.v8_code_and_metadata_bytes =
                v8_code_and_metadata_bytes_.load(std::memory_order_relaxed);
            result.v8_bytecode_and_metadata_bytes =
                v8_bytecode_and_metadata_bytes_.load(std::memory_order_relaxed);
            result.v8_external_script_source_bytes =
                v8_external_script_source_bytes_.load(std::memory_order_relaxed);
            result.native_dom_node_count =
                native_dom_node_count_.load(std::memory_order_relaxed);
            result.native_dom_node_size_bytes =
                native_dom_node_size_bytes_.load(std::memory_order_relaxed);
            result.native_dom_inline_bytes =
                native_dom_inline_bytes_.load(std::memory_order_relaxed);
            result.native_dom_pseudo_storage_bytes =
                native_dom_pseudo_storage_bytes_.load(std::memory_order_relaxed);
            result.native_dom_canvas_node_count =
                native_dom_canvas_node_count_.load(std::memory_order_relaxed);
            result.native_dom_canvas_storage_bytes =
                native_dom_canvas_storage_bytes_.load(std::memory_order_relaxed);
            result.native_dom_animation_count =
                native_dom_animation_count_.load(std::memory_order_relaxed);
            result.native_dom_animation_storage_bytes =
                native_dom_animation_storage_bytes_.load(std::memory_order_relaxed);
            result.native_dom_custom_property_node_count =
                native_dom_custom_property_node_count_.load(std::memory_order_relaxed);
            result.native_dom_custom_property_entry_count =
                native_dom_custom_property_entry_count_.load(std::memory_order_relaxed);
            result.native_dom_custom_property_storage_bytes =
                native_dom_custom_property_storage_bytes_.load(std::memory_order_relaxed);
            result.native_dom_background_image_count =
                native_dom_background_image_count_.load(std::memory_order_relaxed);
            result.native_dom_background_image_storage_bytes =
                native_dom_background_image_storage_bytes_.load(std::memory_order_relaxed);
            result.native_dom_grid_count =
                native_dom_grid_count_.load(std::memory_order_relaxed);
            result.native_dom_grid_storage_bytes =
                native_dom_grid_storage_bytes_.load(std::memory_order_relaxed);
            result.native_dom_authored_style_node_count =
                native_dom_authored_style_node_count_.load(std::memory_order_relaxed);
            result.native_dom_authored_style_entry_count =
                native_dom_authored_style_entry_count_.load(std::memory_order_relaxed);
            result.native_dom_authored_style_storage_bytes =
                native_dom_authored_style_storage_bytes_.load(std::memory_order_relaxed);
            result.native_css_rule_count =
                native_css_rule_count_.load(std::memory_order_relaxed);
            result.native_css_rule_storage_bytes =
                native_css_rule_storage_bytes_.load(std::memory_order_relaxed);
            result.native_css_index_storage_bytes =
                native_css_index_storage_bytes_.load(std::memory_order_relaxed);
            result.process_shared_css_rule_count =
                process_shared_css_rule_count_.load(std::memory_order_relaxed);
            result.process_shared_css_rule_storage_bytes =
                process_shared_css_rule_storage_bytes_.load(std::memory_order_relaxed);
        }
        constexpr auto low_memory_metrics_size =
            offsetof(webscene_engine_memory_metrics, native_dom_attribute_node_count);
        if (result.struct_size >= low_memory_metrics_size) {
            result.low_memory_notifications =
                low_memory_notifications_.load(std::memory_order_relaxed);
        }
        constexpr auto attribute_metrics_size =
            offsetof(webscene_engine_memory_metrics, native_wrapper_handle_count);
        if (result.struct_size >= attribute_metrics_size) {
            result.native_dom_attribute_node_count =
                native_dom_attribute_node_count_.load(std::memory_order_relaxed);
            result.native_dom_attribute_entry_count =
                native_dom_attribute_entry_count_.load(std::memory_order_relaxed);
            result.native_dom_attribute_storage_bytes =
                native_dom_attribute_storage_bytes_.load(std::memory_order_relaxed);
        }
        constexpr auto native_registry_metrics_size =
            offsetof(webscene_engine_memory_metrics, v8_young_space_used_bytes);
        if (result.struct_size >= native_registry_metrics_size) {
            result.native_wrapper_handle_count =
                native_wrapper_handle_count_.load(std::memory_order_relaxed);
            result.native_wrapper_storage_bytes =
                native_wrapper_storage_bytes_.load(std::memory_order_relaxed);
            result.native_text_measurement_cache_entry_count =
                native_text_measurement_cache_entry_count_.load(std::memory_order_relaxed);
            result.native_text_measurement_cache_storage_bytes =
                native_text_measurement_cache_storage_bytes_.load(std::memory_order_relaxed);
            result.process_compilation_mapped_cache_bytes =
                process_compilation_mapped_cache_bytes_.load(std::memory_order_relaxed);
            result.process_resource_mapped_cache_bytes =
                process_resource_mapped_cache_bytes_.load(std::memory_order_relaxed);
            result.native_dom_textual_style_count =
                native_dom_textual_style_count_.load(std::memory_order_relaxed);
            result.native_dom_textual_style_storage_bytes =
                native_dom_textual_style_storage_bytes_.load(
                    std::memory_order_relaxed);
            result.native_dom_node_pool_reserved_bytes =
                native_dom_node_pool_reserved_bytes_.load(
                    std::memory_order_relaxed);
            result.native_dom_node_pool_peak_bytes =
                native_dom_node_pool_peak_bytes_.load(
                    std::memory_order_relaxed);
            result.native_dom_table_layout_count =
                native_dom_table_layout_count_.load(std::memory_order_relaxed);
            result.native_dom_table_layout_storage_bytes =
                native_dom_table_layout_storage_bytes_.load(std::memory_order_relaxed);
            result.native_dom_form_control_count =
                native_dom_form_control_count_.load(std::memory_order_relaxed);
            result.native_dom_form_control_storage_bytes =
                native_dom_form_control_storage_bytes_.load(std::memory_order_relaxed);
            result.hidden_low_memory_notifications =
                hidden_low_memory_notifications_.load(std::memory_order_relaxed);
            result.native_event_listener_count =
                native_event_listener_count_.load(std::memory_order_relaxed);
            result.native_event_listener_storage_bytes =
                native_event_listener_storage_bytes_.load(std::memory_order_relaxed);
        }
        constexpr auto v8_space_metrics_size =
            offsetof(webscene_engine_memory_metrics, pending_scene_count);
        if (result.struct_size >= v8_space_metrics_size) {
            result.v8_young_space_used_bytes =
                v8_young_space_used_bytes_.load(std::memory_order_relaxed);
            result.v8_young_space_physical_bytes =
                v8_young_space_physical_bytes_.load(std::memory_order_relaxed);
            result.v8_old_space_used_bytes =
                v8_old_space_used_bytes_.load(std::memory_order_relaxed);
            result.v8_old_space_physical_bytes =
                v8_old_space_physical_bytes_.load(std::memory_order_relaxed);
            result.v8_code_space_used_bytes =
                v8_code_space_used_bytes_.load(std::memory_order_relaxed);
            result.v8_code_space_physical_bytes =
                v8_code_space_physical_bytes_.load(std::memory_order_relaxed);
            result.v8_map_space_used_bytes =
                v8_map_space_used_bytes_.load(std::memory_order_relaxed);
            result.v8_map_space_physical_bytes =
                v8_map_space_physical_bytes_.load(std::memory_order_relaxed);
            result.v8_large_object_space_used_bytes =
                v8_large_object_space_used_bytes_.load(std::memory_order_relaxed);
            result.v8_large_object_space_physical_bytes =
                v8_large_object_space_physical_bytes_.load(
                    std::memory_order_relaxed);
            result.v8_read_only_space_used_bytes =
                v8_read_only_space_used_bytes_.load(std::memory_order_relaxed);
            result.v8_read_only_space_physical_bytes =
                v8_read_only_space_physical_bytes_.load(std::memory_order_relaxed);
            result.v8_shared_space_used_bytes =
                v8_shared_space_used_bytes_.load(std::memory_order_relaxed);
            result.v8_shared_space_physical_bytes =
                v8_shared_space_physical_bytes_.load(std::memory_order_relaxed);
            result.v8_trusted_space_used_bytes =
                v8_trusted_space_used_bytes_.load(std::memory_order_relaxed);
            result.v8_trusted_space_physical_bytes =
                v8_trusted_space_physical_bytes_.load(std::memory_order_relaxed);
        }
        if (result.struct_size >= sizeof(webscene_engine_memory_metrics)) {
            std::lock_guard lock(acknowledgement_->mutex);
            result.pending_scene_count =
                acknowledgement_->pending_scenes.size();
            result.pending_scene_bytes = 0;
            for (const auto& pending : acknowledgement_->pending_scenes) {
                result.pending_scene_bytes += retained_scene_bytes(*pending);
            }
        }
    }
