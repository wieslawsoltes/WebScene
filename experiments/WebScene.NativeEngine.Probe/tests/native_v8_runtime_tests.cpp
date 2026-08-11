#include "webscene_native_engine.h"
#include "webscene_native_dom.h"

#include <ixwebsocket/IXGetFreePort.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocketServer.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "webscene_native_engine_tests: " << message << '\n';
    std::exit(1);
}

void require(bool condition, std::string_view message)
{
    if (!condition) fail(message);
}

uint8_t measure_baseline_fixture_text(
    void*,
    const char* text,
    size_t text_length,
    const char*,
    size_t,
    float font_size,
    int32_t,
    float,
    float,
    webscene_text_metrics* metrics)
{
    if (metrics == nullptr || metrics->struct_size < sizeof(webscene_text_metrics)) return 0;
    metrics->advance_width = static_cast<float>(text_length) * font_size * 0.5F;
    if (font_size >= 20.0F) {
        metrics->ascent = 10.0F;
        metrics->descent = 10.0F;
    } else {
        metrics->ascent = 9.0F;
        metrics->descent = 1.0F;
    }
    metrics->leading = 0.0F;
    return text == nullptr ? 0 : 1;
}

// Keep one test executable and one translation unit while grouping coverage by
// feature; shared fixtures remain visible without additional test-only APIs.
#include "native_v8_runtime_document_tests.inc"
#include "native_v8_runtime_test_support.inc"
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
#include "native_v8_runtime_inspector_tests.inc"
#endif
#include "native_v8_runtime_interop_tests.inc"
#include "native_v8_runtime_input_tests.inc"
#include "native_v8_runtime_resource_tests.inc"
#include "native_v8_runtime_css_layout_tests.inc"
#include "native_v8_runtime_animation_cssom_tests.inc"
#include "native_v8_runtime_layout_scene_tests.inc"
#include "native_v8_runtime_canvas_tests.inc"
#include "native_v8_runtime_frame_scheduling_tests.inc"
#include "native_v8_runtime_browser_dom_tests.inc"
#include "native_v8_runtime_rendering_metrics_tests.inc"
#include "native_v8_runtime_websocket_tests.inc"
int main()
{
#if defined(_WIN32)
    _putenv_s("WEBSCENE_PROBE_PROFILE_STARTUP", "1");
#else
    setenv("WEBSCENE_PROBE_PROFILE_STARTUP", "1", 1);
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    require(
        (webscene_engine_get_build_features()
            & WEBSCENE_ENGINE_BUILD_FEATURE_CERTIFICATION) != 0,
        "certification build did not advertise certification telemetry");
#else
    require(
        (webscene_engine_get_build_features()
            & WEBSCENE_ENGINE_BUILD_FEATURE_CERTIFICATION) == 0,
        "ordinary build unexpectedly advertised certification telemetry");
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
    require(
        (webscene_engine_get_build_features()
            & WEBSCENE_ENGINE_BUILD_FEATURE_V8_INSPECTOR) != 0,
        "Inspector flavor did not advertise V8 Inspector support");
#else
    require(
        (webscene_engine_get_build_features()
            & WEBSCENE_ENGINE_BUILD_FEATURE_V8_INSPECTOR) == 0,
        "ordinary V8 runtime unexpectedly advertised Inspector support");
#endif
    require(webscene_engine_prewarm() != 0, "V8 prewarm failed");
    if (const auto* filter = std::getenv("WEBSCENE_NATIVE_ENGINE_TEST_FILTER");
        filter != nullptr) {
        const auto selected = std::string_view(filter);
        if (selected == "elliptical-corner-radii") {
            test_elliptical_scene_metadata_is_cold_and_scalar_compatible();
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_elliptical_corner_radii_reach_cssom(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "event-listener-options") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_event_listener_options_reach_native_input_and_resize(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        fail(std::string("unknown WEBSCENE_NATIVE_ENGINE_TEST_FILTER: ") + filter);
    }
    test_binary_reverse_callback_is_leased_and_completed();
    test_generated_binary_cross_context_promise();
    test_shared_isolate_reuses_destroyed_context_slot();
    test_flex_baseline_uses_host_font_metrics();
    test_viewport_hit_testing_traverses_zero_height_document_root();
    test_document_direction_and_visibility_are_native_properties();
    test_hidden_document_defers_presentation_work();
    test_animation_runtime_is_cold_for_static_nodes();
    test_textual_style_state_is_cold_and_copy_on_write();
    test_elliptical_scene_metadata_is_cold_and_scalar_compatible();
    test_table_and_form_state_are_cold_for_ordinary_nodes();
    test_shadow_dom_state_is_document_cold_and_pay_for_use();
    test_document_clear_releases_and_reinitializes_node_pool();
    test_native_id_lookup_tracks_creation_erasure_and_clear();
    test_compact_attribute_collection_preserves_map_semantics();
    test_component_catalog_mounts_interacts_and_unmounts();
    test_out_of_flow_client_geometry_reuse_is_scoped();
    test_screen_tracks_viewport();
    test_zero_command_engine_starts_with_clean_scene();
    test_document_start_ordering_storage_and_fail_closed_errors();
    test_four_navigation_workers_enter_startup_concurrently();
    test_parallel_resource_prefetch();
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
    test_inspector_navigation_resets_context_group();
#endif
    test_document_script_failure_remains_diagnostic();
    test_outer_document_lifecycle_for_editor_bootstrap();
    test_event_listener_exceptions_do_not_abort_document_load();
    test_dom_implementation_create_html_document();
    test_mixed_continuous_input_backlog_is_coalesced();
    test_pressed_drag_moves_remain_dispatchable_after_threshold();
    test_loaded_document_keeps_html_and_body_cascade_distinct();
    test_relative_stylesheet_background_uses_stylesheet_address();
    test_resource_cache_reuse_across_engine_generations();
    test_parsed_css_rule_payloads_are_shared_across_live_engines();
    test_process_wide_resource_load_single_flight();
    test_resource_cache_policy_matrix();
    test_due_timer_precedes_dynamic_resource_wave();
    test_animation_frame_demand_emits_idle_to_active_edges();
    test_dynamic_stylesheet_custom_properties_preserve_cascade_order();
    test_persistent_compilation_cache_reuse();
    test_executed_compilation_units_enrich_persistent_cache();
    test_process_wide_compilation_single_flight();
    auto* engine = webscene_engine_create(64);
    require(engine != nullptr, "engine creation failed");
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
    test_v8_inspector_raw_cdp_session(engine);
    test_v8_inspector_shutdown_releases_paused_engine();
#else
    require(
        webscene_engine_inspector_is_available(engine) == 0,
        "ordinary V8 runtime unexpectedly exposed Inspector support");
#endif
    test_binary_interop_result_is_leased_and_pooled(engine);
    test_binary_interop_preserves_json_edge_semantics(engine);
    test_generated_binary_invocation_uses_tagged_arguments(engine);
    test_binary_interop_stress_when_requested(engine);
    require(
        webscene_engine_request_low_memory(engine) != 0,
        "engine rejected an asynchronous low-memory request");
    test_engine_memory_metrics_are_worker_snapshots(engine);
    test_hidden_engine_reclamation_is_debounced_and_cancelable(engine);
    test_native_websocket_browser_api();
    execute(
        engine,
        "if (typeof IntersectionObserver !== 'function' || "
        "typeof IntersectionObserverEntry !== 'function') "
        "throw new Error('IntersectionObserver bootstrap missing')",
        "intersection-observer-bootstrap.js");
    test_responsive_positioned_sizing(engine);
    test_responsive_unset_restores_auto_inset(engine);
    test_preferred_color_scheme_updates_css_and_match_media(engine);
    test_resize_listener_receives_window_event(engine);
    test_absolute_portal_centers_against_positioned_ancestor(engine);
    test_attribute_selector_invalidation(engine);
    test_attribute_selector_list_requires_authored_attribute(engine);
    test_script_raw_text_does_not_create_style_descendants(engine);
    test_attribute_selector_operators(engine);
    test_replace_child_advances_attribute_selector_iteration(engine);
    test_insert_before_preserves_tree_identity_and_atomicity(engine);
    test_related_tree_mutations_preserve_identity_and_atomicity(engine);
    test_contextual_fragment_exposes_parent_node_members(engine);
    test_custom_element_mutation_reactions_are_pay_for_use(engine);
    test_autonomous_custom_element_lifecycle(engine);
    test_shadow_dom_composed_runtime_geometry(engine);
    test_monaco_browser_primitives(engine);
    test_monaco_view_line_dom_mutations(engine);
    test_class_list_is_same_live_object(engine);
    test_bounded_css_named_color_palette();
    test_visibility_inherits_for_computed_style_and_focus(engine);
    test_hover_specificity_preserves_visible_theme_icon(engine);
    test_complex_is_specificity_ignores_non_element_siblings(engine);
    test_inline_relative_line_height_uses_cascaded_font_size(engine);
    test_hover_invalidation_updates_functional_and_sibling_subjects(engine);
    test_hover_moves_between_block_and_display_contents_child(engine);
    test_single_fractional_grid_track_stays_one_column(engine);
    test_non_rendered_dom_nodes_do_not_create_layout_items(engine);
    test_calc_percent_with_pixel_offset(engine);
    test_flex_basis_reserves_fixed_track(engine);
    test_flex_flow_shorthand_controls_layout_and_cssom(engine);
    test_font_relative_box_lengths_follow_inherited_font_context(engine);
    test_floats_share_a_bounded_formatting_line(engine);
    test_wrapped_flex_resolves_each_line_independently(engine);
    test_zero_height_flex_item_grows_and_hit_tests_descendants(engine);
    test_empty_non_growing_flex_item_collapses_main_axis(engine);
    test_empty_bordered_flex_items_keep_intrinsic_cross_size(engine);
    test_appending_child_invalidates_empty_selector(engine);
    test_inline_block_preserves_vertical_padding(engine);
    test_pointer_hit_targets_and_related_targets_are_elements(engine);
    test_pointer_cursor_and_external_anchor_host_handoff(engine);
    test_z_index_orders_positioned_siblings_in_scene(engine);
    test_transform_origin_keywords_cascade_independently_from_inline_transform(engine);
    test_transform_transition_uses_host_clock_for_translate_and_scale(engine);
    test_transform_transition_interpolates_from_none(engine);
    test_cssom_serializes_resolved_numbers_without_trailing_zeroes(engine);
    test_cssom_serializes_inline_hex_colors(engine);
    test_cssom_padding_assignment_updates_longhands_and_geometry(engine);
    test_cssom_border_assignment_updates_longhands_and_geometry(engine);
    test_logical_inline_borders_reach_geometry(engine);
    test_hidden_subtree_retains_computed_height_without_boxes(engine);
    test_cssom_z_index_survives_connection_and_recascade(engine);
    test_important_custom_property_cascade_reaches_paint(engine);
    test_detached_style_retains_text_and_activates_when_connected(engine);
    test_outer_box_shadow_reaches_elevated_scene(engine);
    test_segmented_rounded_borders_share_an_unclipped_join(engine);
    test_flex_gap_and_variable_text_metrics(engine);
    test_native_overflow_scrolling_and_nowrap(engine);
    test_rounded_overflow_visual_fixture_geometry(engine);
    test_elliptical_corner_radii_reach_cssom(engine);
    test_row_flex_vertical_scroll_extent_remains_bounded(engine);
    test_toolbar_scroll_chevrons_use_single_rotation(engine);
    test_root_document_overflow_scrolls_and_paints_overlay(engine);
    test_table_menu_row_cells_stay_horizontal_and_centered(engine);
    test_semantic_table_auto_layout_and_intrinsic_cell_content(engine);
    test_fixed_table_distributes_excess_after_percentage_columns(engine);
    test_implicit_grid_contains_scrollable_table(engine);
    test_auto_height_flex_popup_expands_overflowing_flex_child(engine);
    test_constrained_column_flex_scroll_item_keeps_footer_inside(engine);
    test_later_dom_overlay_background_paints_above_retained_canvas(engine);
    test_canvas_path_even_odd_fill_rule_reaches_scene(engine);
    test_canvas_fill_rect_emits_only_relevant_paint_state(engine);
    test_canvas_path_2d_add_path_does_not_fill_stale_current_path(engine);
    test_canvas_line_dash_and_path_2d_arc_are_native(engine);
    test_detached_canvas_descendants_leave_native_scene(engine);
    test_compound_root_selector_applies_dark_custom_palette(engine);
    test_adjacent_inline_runs_share_wrapped_lines(engine);
    test_inline_flex_preserves_padding_and_line_box(engine);
    test_document_position(engine);
    test_secondary_click(engine);
    test_primary_click_mouse_event_detail(engine);
    test_event_listener_options_reach_native_input_and_resize(engine);
    test_native_mouseup_honors_immediate_propagation_stop(engine);
    test_generated_idl_attributes_are_prototype_accessors(engine);
    test_document_links_is_a_live_named_html_collection(engine);
    test_component_library_dom_discovery_primitives(engine);
    test_document_id_index_preserves_tree_and_root_semantics(engine);
    test_dom_selector_apis_throw_syntax_error_for_invalid_selectors(engine);
    test_dropdown_runtime_primitives(engine);
    test_collapsed_single_select_native_activation(engine);
    test_input_dispatch_failures_are_attributed_and_consumable(engine);
    test_animation_frame_dispatch_is_attributed();
    test_runtime_work_is_attributed();
    test_scene_flow_is_attributed();
    test_read_only_evaluation_does_not_publish_scene();
    test_ordered_scene_consumer_preserves_two_diff_chain();
    test_keyboard_and_pointer_focus_modality();
    test_navigator_platform_and_wheel_modifiers(engine);
    test_resize_precedes_new_viewport_pointer_input(engine);
    test_generated_pseudo_element_opacity(engine);
    test_negative_z_after_paints_behind_svg_content(engine);
    test_svg_current_color_is_resolved_before_scene_serialization(engine);
    test_positive_z_before_paints_above_lower_z_child(engine);
    test_element_opacity_emits_isolated_group(engine);
    test_svg_background_image_reaches_scene_with_position_and_size(engine);
    test_svg_img_element_loads_and_reaches_scene(engine);
    test_virtual_html_root_inherits_font_metrics(engine);
    test_font_shorthand_inherit_resets_control_metrics(engine);
    test_all_unset_resets_modeled_control_properties(engine);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    test_startup_profile_names_scripts_and_tasks();
#endif
    test_native_text_input_focus_events_and_caret(engine);
    test_svg_dom_parser_preserves_fill_rule(engine);
    test_frame_script_dom_presence(engine);
#if defined(WEBSCENE_NATIVE_ENGINE_HTML5EVER)
    test_html5ever_frame_does_not_duplicate_authored_loading_indicator(engine);
#endif
    test_dom_element_constructor_identity(engine);
    test_provisional_frame_focus_and_document_event_identity(engine);
    test_initial_frame_document_write_and_hidden_style(engine);
    test_detached_dom_wrappers_do_not_permanently_root_nodes(engine);
    test_resize_updates_device_pixel_ratio(engine);
    test_session_storage_in_outer_and_frame_contexts(engine);
    test_window_post_message_is_queued(engine);
    test_cross_frame_post_message_and_window_frames(engine);
    test_frame_resize_preserves_outer_percentage_height(engine);
    test_inner_window_load_acknowledgement(engine);
    test_animation_frame_uses_host_frame(engine);
    test_animation_frame_callback_list_timestamp_and_cancellation(engine);
    test_animation_frame_pending_callbacks_keep_timestamp(engine);
    test_pointer_input_precedes_following_render_opportunity();
    test_resize_and_frame_form_one_rendering_opportunity();
    test_unsupported_features_are_reported_at_native_decision_points(engine);
    webscene_engine_destroy(engine);
    engine = webscene_engine_create(64);
    require(engine != nullptr, "transition regression engine creation failed");
    test_opacity_and_color_transitions_use_host_clock_and_dispatch_events(engine);
    test_opacity_keyframes_use_host_clock_with_staggered_infinite_delays(engine);
    test_rotation_keyframes_use_host_clock_and_wrap_continuously(engine);
    webscene_engine_destroy(engine);
    test_binary_interop_result_outlives_engine();
    return 0;
}
