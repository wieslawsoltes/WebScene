void test_flex_baseline_uses_host_font_metrics()
{
    webscene_native::native_document document(measure_baseline_fixture_text, nullptr);
    auto& row = document.create_element("div");
    row.style.display = webscene_native::display_mode::flex;
    row.style.align_items = webscene_native::align_mode::baseline;
    row.style.width = {100.0F, webscene_native::length_unit::pixels};

    auto& large = document.create_element("#text");
    large.text_content = "Large";
    large.style.font_size = 20.0F;
    large.style.line_height = 20.0F;
    auto& small = document.create_element("#text");
    small.text_content = "Small";
    small.style.font_size = 10.0F;
    small.style.line_height = 10.0F;

    require(
        document.append_child(document.body(), row)
            && document.append_child(row, large)
            && document.append_child(row, small),
        "font-metric baseline fixture could not build its flex row");
    document.layout(200.0F, 100.0F);

    require(
        std::abs((small.layout.y - large.layout.y) - 1.0F) < 0.01F,
        "flex baseline alignment ignored the host shaper's ascent/descent metrics");
}

void test_viewport_hit_testing_traverses_zero_height_document_root()
{
    webscene_native::native_document document;
    auto& frame_root = document.create_element("body");
    auto& button = document.create_element("button");
    require(
        document.append_child(frame_root, button),
        "zero-height hit-test fixture could not append its button");
    frame_root.visible = true;
    frame_root.style.clip = true;
    frame_root.layout = {0, 0, 320, 0};
    button.visible = true;
    button.layout = {40, 40, 80, 24};

    require(
        document.hit_test(frame_root, 60, 52) == &button,
        "viewport hit testing clipped positioned content to a zero-height document root");
}

void test_animation_runtime_is_cold_for_static_nodes()
{
    webscene_native::native_document document;
    auto& root = document.body();
    for (auto index = 0; index < 128; ++index) {
        auto& node = document.create_element("div");
        node.style.opacity = 0.75F;
        node.style.transform_scale_x = 1.1F;
        require(
            document.append_child(root, node),
            "static animation-allocation fixture could not append a node");
        document.update_style_animations(node);
    }
    auto metrics = document.read_allocation_metrics();
    require(
        metrics.animation_runtime_count == 0
            && metrics.animation_runtime_storage_bytes == 0,
        "static nodes eagerly allocated animation runtime state");

    auto& animated = document.create_element("div");
    require(
        document.append_child(root, animated),
        "animated allocation fixture could not append its node");
    animated.style.mutable_animations().opacity_transition.duration_ms = 100;
    document.update_style_animations(animated);
    metrics = document.read_allocation_metrics();
    require(
        metrics.animation_runtime_count == 1
            && metrics.animation_runtime_storage_bytes
                >= sizeof(webscene_native::dom_node::animation_runtime_data),
        "an animated node did not allocate its cold runtime state");
}

void test_textual_style_state_is_cold_and_copy_on_write()
{
    webscene_native::native_document document;
    auto& root = document.body();
    for (auto index = 0; index < 128; ++index) {
        auto& node = document.create_element("div");
        node.style.width = {100, webscene_native::length_unit::pixels};
        node.style.opacity = 0.75F;
        require(
            document.append_child(root, node),
            "textual-style allocation fixture could not append a node");
    }
    auto metrics = document.read_allocation_metrics();
    require(
        metrics.textual_style_data_count == 0
            && metrics.textual_style_storage_bytes == 0,
        "tokenless styles eagerly allocated textual CSS state");

    auto& styled = document.create_element("button");
    styled.style.mutable_textual().cursor = "pointer";
    styled.style.mutable_textual().font_family = "Inter";
    require(
        document.append_child(root, styled),
        "textual-style fixture could not append its styled node");
    auto clone = styled.style;
    clone.mutable_textual().cursor = "wait";
    require(
        styled.style.textual().cursor == "pointer"
            && clone.textual().cursor == "wait",
        "textual CSS state did not detach on style mutation");

    metrics = document.read_allocation_metrics();
    require(
        metrics.textual_style_data_count == 1
            && metrics.textual_style_storage_bytes
                >= sizeof(webscene_native::node_style::textual_style_data),
        "authored textual CSS state was not attributed as cold storage");
}

void test_table_and_form_state_are_cold_for_ordinary_nodes()
{
    webscene_native::native_document document;
    auto& root = document.body();
    for (auto index = 0; index < 128; ++index) {
        auto& node = document.create_element("div");
        require(
            document.append_child(root, node),
            "cold node-state fixture could not append an ordinary node");
    }
    auto metrics = document.read_allocation_metrics();
    require(
        metrics.table_layout_node_count == 0
            && metrics.table_layout_storage_bytes == 0
            && metrics.form_control_node_count == 0
            && metrics.form_control_storage_bytes == 0,
        "ordinary nodes eagerly allocated table or form-control state");

    auto& table = document.create_element("table");
    table.mutable_table_layout().column_widths = {80.0F, 120.0F};
    auto& input = document.create_element("input");
    input.mutable_form_control().value = "sample";
    input.mutable_form_control().value_initialized = true;
    require(
        document.append_child(root, table) && document.append_child(root, input),
        "cold node-state fixture could not append its specialized nodes");
    metrics = document.read_allocation_metrics();
    require(
        metrics.table_layout_node_count == 1
            && metrics.table_layout_storage_bytes
                >= sizeof(webscene_native::dom_node::table_layout_data)
            && metrics.form_control_node_count == 1
            && metrics.form_control_storage_bytes
                >= sizeof(webscene_native::dom_node::form_control_data),
        "specialized table or form-control state was not attributed");
}

void test_document_clear_releases_and_reinitializes_node_pool()
{
    webscene_native::native_document document;
    for (auto cycle = 0; cycle < 4; ++cycle) {
        auto& root = document.body();
        for (auto index = 0; index < 512; ++index) {
            auto& node = document.create_element("div");
            require(
                document.append_child(root, node),
                "node-pool clear fixture could not append a node");
        }
        require(
            document.node_count() == 513,
            "node-pool clear fixture did not create the expected document");
        const auto populated = document.read_allocation_metrics();
        require(
            populated.node_pool_reserved_bytes >= populated.node_object_bytes
                && populated.node_pool_peak_bytes
                    >= populated.node_pool_reserved_bytes,
            "node-pool allocation metrics did not include live node chunks");
        document.clear();
        const auto cleared = document.read_allocation_metrics();
        require(
            document.node_count() == 1
                && document.body().id == 1
                && document.body().tag == "body",
            "document clear did not release and reinitialize its node pool");
        require(
            cleared.node_pool_reserved_bytes
                    < populated.node_pool_reserved_bytes
                && cleared.node_pool_peak_bytes
                    >= populated.node_pool_peak_bytes,
            "document clear retained its previous node-pool high-water chunks");
    }
}

void test_native_id_lookup_tracks_creation_erasure_and_clear()
{
    webscene_native::native_document document;
    auto& retained = document.create_element("div");
    auto& detached_root = document.create_element("section");
    auto& detached_child = document.create_element("span");
    const auto retained_id = retained.id;
    const auto detached_root_id = detached_root.id;
    const auto detached_child_id = detached_child.id;

    require(
        document.append_child(document.body(), retained)
            && document.append_child(detached_root, detached_child),
        "native-id fixture could not build its retained and detached trees");
    require(
        document.find_by_native_id(retained_id) == &retained
            && document.find_by_native_id(detached_root_id) == &detached_root
            && document.find_by_native_id(detached_child_id) == &detached_child,
        "native-id direct index did not resolve newly-created nodes");

    require(
        document.erase_detached_subtree(detached_root) == 2,
        "native-id fixture did not erase its detached subtree");
    require(
        document.find_by_native_id(retained_id) == &retained
            && document.find_by_native_id(detached_root_id) == nullptr
            && document.find_by_native_id(detached_child_id) == nullptr,
        "native-id direct index retained erased subtree pointers");

    auto& later = document.create_element("button");
    const auto later_id = later.id;
    require(
        later_id > detached_child_id
            && document.find_by_native_id(later_id) == &later,
        "native-id direct index did not preserve monotonic sparse IDs");

    document.clear();
    require(
        document.body().id == 1
            && document.find_by_native_id(1) == &document.body()
            && document.find_by_native_id(retained_id) == nullptr
            && document.find_by_native_id(later_id) == nullptr,
        "native-id direct index was not reset with its document");
}

void test_compact_attribute_collection_preserves_map_semantics()
{
    webscene_native::attribute_collection left;
    left["id"] = "dialog";
    left["class"] = "open";
    left["id"] = "updated";
    require(
        left.size() == 2 && left.at("id") == "updated",
        "compact attributes duplicated or failed to replace an existing name");

    webscene_native::attribute_collection right;
    right["class"] = "open";
    right["id"] = "updated";
    require(
        left == right,
        "compact attribute equality incorrectly depended on insertion order");
    require(
        left.erase("class") == 1
            && left.erase("class") == 0
            && !left.contains("class"),
        "compact attribute erase did not preserve map semantics");
}

void test_out_of_flow_client_geometry_reuse_is_scoped()
{
    webscene_native::native_document document;
    auto& toolbar = document.create_element("div");
    auto& overlay = document.create_element("div");
    auto& overlay_child = document.create_element("div");
    toolbar.style.width = {320, webscene_native::length_unit::pixels};
    toolbar.style.height = {32, webscene_native::length_unit::pixels};
    overlay.style.position = webscene_native::position_mode::absolute;
    overlay.style.width = {200, webscene_native::length_unit::pixels};
    overlay.style.height = {100, webscene_native::length_unit::pixels};
    require(
        document.append_child(document.body(), toolbar)
            && document.append_child(document.body(), overlay)
            && document.append_child(overlay, overlay_child),
        "out-of-flow geometry fixture could not build its tree");
    document.layout(640, 360);
    const auto passes = document.layout_passes();

    overlay.style.width = {240, webscene_native::length_unit::pixels};
    document.mark_out_of_flow_geometry_dirty(overlay);
    require(
        document.dirty()
            && document.can_reuse_client_geometry(toolbar)
            && document.can_reuse_client_geometry(document.body()),
        "an isolated positioned-box mutation invalidated unrelated client geometry");
    require(
        !document.can_reuse_client_geometry(overlay)
            && !document.can_reuse_client_geometry(overlay_child),
        "a positioned-box mutation incorrectly reused geometry inside its dirty subtree");
    require(
        document.layout_passes() == passes,
        "geometry reuse performed an eager layout");

    document.layout(640, 360);
    document.mark_dirty();
    require(
        !document.can_reuse_client_geometry(toolbar),
        "a global invalidation incorrectly reused cached client geometry");
}
