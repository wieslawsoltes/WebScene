#include "webscene_native_engine.h"
#include "webscene_native_dom.h"

#include <ixwebsocket/IXGetFreePort.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocketServer.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
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

std::string last_error(webscene_engine* engine)
{
    const auto required = webscene_engine_copy_last_error(engine, nullptr, 0);
    std::vector<char> buffer(required > 0 ? required : 1U, '\0');
    webscene_engine_copy_last_error(engine, buffer.data(), buffer.size());
    return buffer.data();
}

std::string diagnostics(webscene_engine* engine)
{
    const auto required = webscene_engine_copy_scene_diagnostics(engine, nullptr, 0);
    std::vector<char> buffer(std::max<size_t>(required + 64U * 1024U, 64U * 1024U), '\0');
    const auto copied = webscene_engine_copy_scene_diagnostics(
        engine, buffer.data(), buffer.size());
    require(copied > 0U && copied <= buffer.size(), "scene diagnostics could not be copied");
    return std::string(buffer.data(), copied - 1U);
}

size_t isolate_shard_slot(webscene_engine* engine)
{
    constexpr std::string_view source{"void 0"};
    constexpr std::string_view name{"shared-isolate-slot-barrier.js"};
    require(
        webscene_engine_execute_script(
            engine,
            source.data(),
            source.size(),
            name.data(),
            name.size()) != 0,
        "V8 shard diagnostics barrier was rejected");
    for (auto attempt = 0; attempt < 250; ++attempt) {
        webscene_process_cache_metrics metrics{
            sizeof(webscene_process_cache_metrics)};
        require(
            webscene_engine_get_process_cache_metrics(engine, &metrics) != 0,
            "V8 shard process metrics were unavailable");
        if (metrics.shared_isolate_slot
            != std::numeric_limits<uint64_t>::max()) {
            return static_cast<size_t>(metrics.shared_isolate_slot);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    fail("V8 shard ownership was not published by the engine worker");
}

uint64_t isolate_shard_peak_contexts(webscene_engine* engine)
{
    constexpr std::string_view source{"void 0"};
    constexpr std::string_view name{"shared-isolate-peak-barrier.js"};
    require(
        webscene_engine_execute_script(
            engine,
            source.data(),
            source.size(),
            name.data(),
            name.size()) != 0,
        "V8 shard peak diagnostics barrier was rejected");
    for (auto attempt = 0; attempt < 250; ++attempt) {
        webscene_process_cache_metrics metrics{
            sizeof(webscene_process_cache_metrics)};
        require(
            webscene_engine_get_process_cache_metrics(engine, &metrics) != 0,
            "V8 shard peak process metrics were unavailable");
        if (metrics.shared_isolate_slot
            != std::numeric_limits<uint64_t>::max()) {
            return metrics.shared_isolate_peak_contexts;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    fail("V8 shard peak occupancy was not published by the engine worker");
}

void test_shared_isolate_reuses_destroyed_context_slot()
{
    if (std::getenv("WEBSCENE_V8_SHARED_ISOLATE") == nullptr) return;
    const auto* count_value =
        std::getenv("WEBSCENE_V8_SHARED_ISOLATE_COUNT");
    if (count_value == nullptr
        || std::strtoull(count_value, nullptr, 10) < 2U) {
        return;
    }

    std::vector<webscene_engine*> engines(4U, nullptr);
    for (auto*& engine : engines) {
        engine = webscene_engine_create(0);
        require(engine != nullptr, "shared-isolate lifecycle engine creation failed");
    }
    const std::array<size_t, 4> slots{
        isolate_shard_slot(engines[0]),
        isolate_shard_slot(engines[1]),
        isolate_shard_slot(engines[2]),
        isolate_shard_slot(engines[3])};
    const auto slot_zero_count =
        static_cast<size_t>(std::count(slots.begin(), slots.end(), 0U));
    const auto slot_one_count =
        static_cast<size_t>(std::count(slots.begin(), slots.end(), 1U));
    require(
        slot_zero_count == 2U && slot_one_count == 2U,
        "initial four-engine placement did not preserve the 2/2 shard balance");

    for (size_t index = 0; index < engines.size(); ++index) {
        const auto expected_slot = slots[index];
        webscene_engine_destroy(engines[index]);
        engines[index] = nullptr;
        auto* replacement = webscene_engine_create(0);
        require(
            replacement != nullptr,
            "replacement shared-isolate engine creation failed");
        require(
            isolate_shard_slot(replacement) == expected_slot,
            "replacement engine did not refill the randomly vacated shard slot");
        engines[index] = replacement;
    }

    // A shard whose final two contexts close remains warm for a bounded
    // lease. Reopening it must preserve the isolate's observed peak rather
    // than constructing a fresh isolate that happens to occupy the same slot.
    const auto warm_slot = slots.front();
    std::array<size_t, 2> warm_indices{};
    size_t warm_index_count = 0U;
    for (size_t index = 0U; index < slots.size(); ++index) {
        if (slots[index] == warm_slot) {
            require(
                warm_index_count < warm_indices.size(),
                "more than two contexts occupied the warm-lease test shard");
            warm_indices[warm_index_count++] = index;
        }
    }
    require(
        warm_index_count == warm_indices.size(),
        "warm-lease test shard did not begin with two contexts");
    for (const auto index : warm_indices) {
        webscene_engine_destroy(engines[index]);
        engines[index] = nullptr;
    }
    for (const auto index : warm_indices) {
        engines[index] = webscene_engine_create(0);
        require(
            engines[index] != nullptr,
            "warm shared-isolate replacement engine creation failed");
        require(
            isolate_shard_slot(engines[index]) == warm_slot,
            "fully empty warm shard was not reused");
    }
    require(
        isolate_shard_peak_contexts(engines[warm_indices.front()]) == 2U,
        "fully empty shard was recreated instead of reused during its lease");

    // Short leases are selected by the explicit shared-suite invocation so
    // expiry can be covered without adding a long delay to ordinary tests.
    const auto* warm_lease_value =
        std::getenv("WEBSCENE_V8_SHARED_ISOLATE_WARM_LEASE_MS");
    const auto warm_lease_milliseconds = warm_lease_value == nullptr
        ? 0ULL
        : std::strtoull(warm_lease_value, nullptr, 10);
    if (warm_lease_milliseconds > 0U
        && warm_lease_milliseconds <= 1000U) {
        for (const auto index : warm_indices) {
            webscene_engine_destroy(engines[index]);
            engines[index] = nullptr;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(
            warm_lease_milliseconds + 50U));
        engines[warm_indices.front()] = webscene_engine_create(0);
        require(
            engines[warm_indices.front()] != nullptr,
            "expired shared-isolate replacement engine creation failed");
        require(
            isolate_shard_slot(engines[warm_indices.front()]) == warm_slot,
            "expired shard slot was not available for a fresh isolate");
        require(
            isolate_shard_peak_contexts(engines[warm_indices.front()]) == 1U,
            "expired empty shard was retained beyond its warm lease");
        engines[warm_indices.back()] = webscene_engine_create(0);
        require(
            engines[warm_indices.back()] != nullptr
                && isolate_shard_slot(engines[warm_indices.back()])
                    == warm_slot,
            "fresh post-expiry shard did not refill to its context capacity");
    }

    const auto* maximum_count_value =
        std::getenv("WEBSCENE_V8_SHARED_ISOLATE_MAX_COUNT");
    const auto maximum_count = maximum_count_value != nullptr
        ? std::strtoull(maximum_count_value, nullptr, 10)
        : static_cast<unsigned long long>(
            std::max(1U, std::thread::hardware_concurrency()));
    if (maximum_count >= 10U) {
        engines.reserve(20U);
        while (engines.size() < 20U) {
            auto* engine = webscene_engine_create(0);
            require(
                engine != nullptr,
                "dynamic shared-isolate engine creation failed");
            engines.push_back(engine);
        }

        std::array<size_t, 10> occupancy{};
        for (auto* engine : engines) {
            const auto slot = isolate_shard_slot(engine);
            require(slot < occupancy.size(), "dynamic shard slot exceeded maximum");
            ++occupancy[slot];
        }
        require(
            std::all_of(
                occupancy.begin(),
                occupancy.end(),
                [](size_t count) { return count == 2U; }),
            "twenty dynamic engines did not preserve two contexts per shard");

        for (size_t index = 0; index < engines.size(); ++index) {
            const auto expected_slot = isolate_shard_slot(engines[index]);
            webscene_engine_destroy(engines[index]);
            engines[index] = webscene_engine_create(0);
            require(
                engines[index] != nullptr,
                "dynamic replacement shared-isolate engine creation failed");
            require(
                isolate_shard_slot(engines[index]) == expected_slot,
                "dynamic replacement did not refill the randomly vacated shard");
        }
    }

    for (auto* engine : engines) {
        if (engine != nullptr) webscene_engine_destroy(engine);
    }
}

std::string feature_use(webscene_engine* engine)
{
    const auto required = webscene_engine_copy_feature_use(engine, nullptr, 0);
    std::vector<char> buffer(required > 0 ? required : 1U, '\0');
    const auto copied = webscene_engine_copy_feature_use(
        engine, buffer.data(), buffer.size());
    require(copied > 0U && copied <= buffer.size(), "feature-use report could not be copied");
    return std::string(buffer.data(), copied - 1U);
}

std::string event_listener_inventory(webscene_engine* engine)
{
    const auto required = webscene_engine_copy_event_listener_inventory(engine, nullptr, 0);
    std::vector<char> buffer(required > 0 ? required : 1U, '\0');
    const auto copied = webscene_engine_copy_event_listener_inventory(
        engine, buffer.data(), buffer.size());
    require(copied > 0U && copied <= buffer.size(), "event-listener inventory could not be copied");
    return std::string(buffer.data(), copied - 1U);
}

std::string take_input_dispatch_failure(webscene_engine* engine)
{
    const auto required = webscene_engine_take_input_dispatch_failure(engine, nullptr, 0);
    if (required == 0U) return {};
    std::vector<char> buffer(required, '\0');
    const auto copied = webscene_engine_take_input_dispatch_failure(
        engine, buffer.data(), buffer.size());
    require(copied == required, "input-dispatch failure could not be copied");
    return std::string(buffer.data(), copied - 1U);
}

std::string take_host_request(webscene_engine* engine)
{
    const auto required = webscene_engine_take_host_request(engine, nullptr, 0);
    if (required == 0U) return {};
    std::vector<char> buffer(required, '\0');
    const auto copied = webscene_engine_take_host_request(
        engine, buffer.data(), buffer.size());
    require(copied == required, "host request could not be copied");
    return std::string(buffer.data(), copied - 1U);
}

void execute(webscene_engine* engine, std::string_view source, std::string_view name)
{
    require(webscene_engine_execute_script(
        engine,
        source.data(), source.size(),
        name.data(), name.size()) != 0,
        "script was rejected");
}

void execute_and_wait(
    webscene_engine* engine,
    std::string_view source,
    std::string_view name)
{
    webscene_engine_metrics before{};
    webscene_engine_get_metrics(engine, &before);
    execute(engine, source, name);
    for (auto attempt = 0; attempt < 250; ++attempt) {
        webscene_engine_metrics after{};
        webscene_engine_get_metrics(engine, &after);
        if (after.executed_scripts > before.executed_scripts) return;
        if (after.script_errors > before.script_errors) {
            fail("script execution failed: " + last_error(engine));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    fail("script execution did not complete");
}

std::string evaluate(webscene_engine* engine, std::string_view source, std::string_view name)
{
    std::vector<char> buffer(64U * 1024U, '\0');
    const auto required = webscene_engine_evaluate_json(
        engine,
        source.data(), source.size(),
        name.data(), name.size(),
        buffer.data(), buffer.size(),
        10'000U);
    if (required == 0U || required > buffer.size()) {
        fail("evaluation failed: " + last_error(engine));
    }
    return std::string(buffer.data(), required - 1U);
}

void notify_interop_callback_test(void* user_data)
{
    static_cast<std::atomic<uint32_t>*>(user_data)
        ->fetch_add(1U, std::memory_order_relaxed);
}

void test_interop_callback_notification()
{
    struct previous_engine_options final {
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
    };
    static_assert(
        sizeof(previous_engine_options)
        == offsetof(
            webscene_engine_options,
            interop_callback_available_callback));
    previous_engine_options previous_options{};
    previous_options.struct_size = sizeof(previous_engine_options);
    auto* previous_engine = webscene_engine_create_with_options(
        reinterpret_cast<const webscene_engine_options*>(&previous_options));
    require(
        previous_engine != nullptr,
        "interop callback ABI rejected the previous engine options tail");
    webscene_engine_destroy(previous_engine);

    std::atomic<uint32_t> notifications{0U};
    webscene_engine_options options{};
    options.struct_size = sizeof(webscene_engine_options);
    options.interop_callback_available_callback =
        notify_interop_callback_test;
    options.interop_callback_available_user_data = &notifications;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "interop callback notification engine creation failed");

    require(
        evaluate(
            engine,
            "(() => {"
            "__webSceneNotifyInteropCallbackAvailable();"
            "__webSceneNotifyInteropCallbackAvailable();"
            "return true;"
            "})()",
            "native-interop-callback-notification.js") == "true",
        "native interop callback notification script did not complete");
    require(
        notifications.load(std::memory_order_relaxed) == 2U,
        "native interop callback notification did not reach the host");
    webscene_engine_destroy(engine);
}

uint64_t consumed_input_count(webscene_engine* engine)
{
    webscene_engine_metrics metrics{};
    webscene_engine_get_metrics(engine, &metrics);
    return metrics.consumed_inputs;
}

void wait_for_consumed_inputs(
    webscene_engine* engine,
    uint64_t minimum,
    std::string_view failure_message)
{
    for (auto attempt = 0; attempt < 250; ++attempt) {
        if (consumed_input_count(engine) >= minimum) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    fail(failure_message);
}

void wait_for_animation_frame_demand(
    webscene_engine* engine,
    bool expected,
    std::string_view failure_message)
{
    for (auto attempt = 0; attempt < 250; ++attempt) {
        if ((webscene_engine_requires_animation_frame(engine) != 0)
            == expected) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    fail(
        std::string(failure_message)
        + " (demand="
        + std::to_string(webscene_engine_requires_animation_frame(engine))
        + ")");
}

void resize(
    webscene_engine* engine,
    double width,
    double height,
    uint64_t sequence,
    double device_scale_factor = 1.0)
{
    const webscene_input_event input{
        WEBSCENE_INPUT_RESIZE,
        0U,
        sequence,
        width,
        height,
        device_scale_factor,
        0};
    require(webscene_engine_enqueue(engine, &input) != 0, "resize was rejected");
}

void pointer_move(
    webscene_engine* engine,
    double x,
    double y,
    uint64_t sequence,
    bool pressed = false)
{
    const webscene_input_event input{
        WEBSCENE_INPUT_POINTER_MOVE,
        pressed ? 1U : 0U,
        sequence,
        x,
        y,
        0,
        0};
    require(webscene_engine_enqueue(engine, &input) != 0, "pointer move was rejected");
}

void pointer_button(
    webscene_engine* engine,
    uint32_t kind,
    double x,
    double y,
    uint64_t sequence,
    bool pressed)
{
    const webscene_input_event input{
        kind,
        pressed ? 1U | (1U << 8U) : 1U << 8U,
        sequence,
        x,
        y,
        0,
        0};
    require(webscene_engine_enqueue(engine, &input) != 0, "pointer button was rejected");
}

void animation_frame(webscene_engine* engine, double timestamp, uint64_t sequence)
{
    const webscene_input_event input{
        WEBSCENE_INPUT_FRAME,
        0U,
        sequence,
        timestamp,
        0,
        0,
        0};
    require(webscene_engine_enqueue(engine, &input) != 0, "animation frame was rejected");
}

void animation_frame_and_wait(
    webscene_engine* engine,
    double timestamp,
    uint64_t sequence)
{
    const auto consumed_before = consumed_input_count(engine);
    animation_frame(engine, timestamp, sequence);
    wait_for_consumed_inputs(
        engine,
        consumed_before + 1U,
        "animation frame was not consumed");
}

void test_zero_command_engine_starts_with_clean_scene()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "zero-command engine creation failed");
    resize(engine, 800, 600, 1U);
    auto observed = false;
    for (auto attempt = 0; attempt < 100 && !observed; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            if (scene->header.consumed_input_sequence >= 1U) {
                require(
                    scene->header.command_count == 0U,
                    "product startup leaked synthetic benchmark rectangles");
                observed = true;
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (!observed) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(observed, "zero-command startup scene was not published");
    webscene_engine_destroy(engine);
}

void keyboard_input(
    webscene_engine* engine,
    uint32_t kind,
    uint32_t value,
    uint64_t sequence,
    uint32_t flags = 0U)
{
    const webscene_input_event input{
        kind,
        flags,
        sequence,
        static_cast<double>(value),
        0,
        0,
        0};
    require(webscene_engine_enqueue(engine, &input) != 0, "keyboard input was rejected");
}

void wheel_input(
    webscene_engine* engine,
    double x,
    double y,
    double delta_y,
    uint64_t sequence,
    uint32_t flags = 0U)
{
    const webscene_input_event input{
        WEBSCENE_INPUT_WHEEL,
        flags,
        sequence,
        x,
        y,
        0,
        delta_y};
    require(webscene_engine_enqueue(engine, &input) != 0, "wheel input was rejected");
}

void test_navigator_platform_and_wheel_modifiers(webscene_engine* engine)
{
    const auto navigator_result = evaluate(
        engine,
        "({ platform: navigator.platform, clientPlatform: navigator.userAgentData.platform, "
        "userAgent: navigator.userAgent, maxTouchPoints: navigator.maxTouchPoints })",
        "native-navigator-platform.js");
#if defined(__APPLE__)
    require(
        navigator_result.find(R"("platform":"MacIntel")") != std::string::npos
            && navigator_result.find(R"("clientPlatform":"macOS")") != std::string::npos
            && navigator_result.find("Macintosh") != std::string::npos,
        "native navigator did not expose macOS platform identity: " + navigator_result);
#elif defined(_WIN32)
    require(
        navigator_result.find(R"("platform":"Win32")") != std::string::npos,
        "native navigator did not expose Windows platform identity: " + navigator_result);
#else
    require(
        navigator_result.find(R"("platform":"Linux x86_64")") != std::string::npos,
        "native navigator did not expose Linux platform identity: " + navigator_result);
#endif

    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const target = document.createElement('button');
          target.id = 'cancelled-focus-target';
          target.tabIndex = -1;
          target.style.width = '100px';
          target.style.height = '100px';
          globalThis.__wheelModifiers = [];
          target.addEventListener('wheel', event => __wheelModifiers.push([
            event.metaKey, event.ctrlKey, event.shiftKey, event.altKey, event.deltaY
          ]));
          document.body.appendChild(target);
        })()
    )JS", "native-wheel-modifier-setup.js");
    require(
        evaluate(
            engine,
            "document.body.firstElementChild?.getBoundingClientRect().width || 0",
            "native-wheel-modifier-ready.js") == "100",
        "wheel modifier target did not complete layout");
    wheel_input(
        engine,
        20,
        20,
        -100,
        5U,
        WEBSCENE_INPUT_POINTER_MODIFIER_META | WEBSCENE_INPUT_POINTER_MODIFIER_SHIFT);
    wheel_input(
        engine,
        20,
        20,
        100,
        6U,
        WEBSCENE_INPUT_POINTER_MODIFIER_CONTROL | WEBSCENE_INPUT_POINTER_MODIFIER_ALT);
    const auto modifiers = evaluate(
        engine,
        "globalThis.__wheelModifiers",
        "native-wheel-modifier-result.js");
    if (modifiers != "[[true,false,true,false,-100],[false,true,false,true,100]]") {
        fail("wheel modifiers were not preserved at the DOM event boundary: " + modifiers);
    }
}

void test_mixed_continuous_input_backlog_is_coalesced()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "mixed-input coalescing engine creation failed");
    resize(engine, 640, 360, 1U);
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const target = document.createElement('div');
          target.style.width = '640px';
          target.style.height = '360px';
          globalThis.__mixedWheelCount = 0;
          globalThis.__mixedWheelDelta = 0;
          globalThis.__mixedPointerCount = 0;
          globalThis.__mixedFirstWheelBlocked = false;
          target.addEventListener('wheel', event => {
            __mixedWheelCount++;
            __mixedWheelDelta += event.deltaY;
            if (!__mixedFirstWheelBlocked) {
              __mixedFirstWheelBlocked = true;
              const until = performance.now() + 100;
              while (performance.now() < until) {}
            }
          });
          target.addEventListener('pointermove', () => __mixedPointerCount++);
          document.body.appendChild(target);
        })()
    )JS", "native-mixed-input-coalescing-setup.js");
    require(
        evaluate(engine, "document.body.firstElementChild.offsetWidth",
            "native-mixed-input-coalescing-ready.js") == "640",
        "mixed-input coalescing fixture did not complete layout");

    webscene_engine_metrics before{};
    webscene_engine_get_metrics(engine, &before);
    wheel_input(engine, 100, 100, 1, 2U);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    uint64_t sequence = 3U;
    for (auto index = 0; index < 100; ++index) {
        wheel_input(engine, 100, 100, 1, sequence++);
        pointer_move(engine, 100 + index % 3, 100 + index % 5, sequence++);
    }
    wait_for_consumed_inputs(
        engine,
        before.consumed_inputs + 201U,
        "mixed wheel/pointer backlog was not consumed");

    webscene_engine_metrics after{};
    webscene_engine_get_metrics(engine, &after);
    require(
        after.coalesced_wheel_inputs - before.coalesced_wheel_inputs >= 90U
            && after.coalesced_pointer_move_inputs
                - before.coalesced_pointer_move_inputs >= 90U
            && after.applied_wheel_inputs - before.applied_wheel_inputs <= 4U
            && after.applied_pointer_move_inputs
                - before.applied_pointer_move_inputs <= 3U,
        "alternating continuous input was not coalesced as one bounded backlog");
    const auto result = evaluate(
        engine,
        "({ wheelCount: __mixedWheelCount, wheelDelta: __mixedWheelDelta, "
        "pointerCount: __mixedPointerCount })",
        "native-mixed-input-coalescing-result.js");
    require(
        result.find(R"("wheelDelta":101)") != std::string::npos,
        "mixed-input coalescing lost accumulated wheel distance: " + result);
    webscene_engine_destroy(engine);
}

void test_pressed_drag_moves_remain_dispatchable_after_threshold()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "pressed-drag coalescing engine creation failed");
    resize(engine, 640, 360, 1U);
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const target = document.createElement('div');
          target.style.width = '640px';
          target.style.height = '360px';
          globalThis.__pressedDragMoves = [];
          target.addEventListener('pointermove', event => {
            if (event.buttons === 1) __pressedDragMoves.push(event.clientX);
          });
          document.body.appendChild(target);
        })()
    )JS", "native-pressed-drag-coalescing-setup.js");
    require(
        evaluate(engine, "document.body.firstElementChild.offsetWidth",
            "native-pressed-drag-coalescing-ready.js") == "640",
        "pressed-drag coalescing fixture did not complete layout");

    webscene_engine_metrics before{};
    webscene_engine_get_metrics(engine, &before);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_DOWN, 10, 20, 2U, true);
    pointer_move(engine, 20, 20, 3U, true);
    pointer_move(engine, 30, 20, 4U, true);
    pointer_move(engine, 40, 20, 5U, true);
    pointer_move(engine, 50, 20, 6U, true);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 50, 20, 7U, false);
    wait_for_consumed_inputs(
        engine,
        before.consumed_inputs + 6U,
        "pressed drag inputs were not consumed");

    const auto result = evaluate(
        engine,
        "globalThis.__pressedDragMoves",
        "native-pressed-drag-coalescing-result.js");
    require(
        result.find("50") != std::string::npos,
        "the post-threshold pressed drag position was consumed without dispatch: "
            + result);
    webscene_engine_destroy(engine);
}

void test_pointer_cursor_and_external_anchor_host_handoff(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              html, body { margin: 0; width: 100%; height: 100%; }
              .badge { display: block; width: 180px; height: 48px; cursor: pointer; }
            </style>
            <a id="badge" class="badge" href="https://example.com/?source=webscene">
              <span>Hosted chart</span>
            </a>
            <a id="cancelled" class="badge" href="https://example.invalid/blocked">blocked</a>`;
          document.getElementById('cancelled').addEventListener('click', event => event.preventDefault());
        })()
    )JS", "native-external-link-setup.js");
    require(
        evaluate(engine, "getComputedStyle(document.getElementById('badge')).cursor",
            "native-external-link-ready.js") == R"("pointer")",
        "cursor:pointer did not participate in the native CSS cascade");

    pointer_move(engine, 20, 20, 501U);
    require(
        evaluate(engine, "document.elementFromPoint(20, 20).id || document.elementFromPoint(20, 20).parentElement.id",
            "native-external-link-hover-barrier.js") == R"("badge")",
        "badge was not the hover hit target");
    require(
        webscene_engine_get_cursor(engine) == WEBSCENE_CURSOR_POINTER,
        "cursor:pointer was not projected to the host hand cursor");

    const auto location_before = evaluate(engine, "location.href", "native-location-before.js");
    pointer_button(engine, WEBSCENE_INPUT_POINTER_DOWN, 20, 20, 502U, true);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 20, 20, 503U, false);
    require(
        evaluate(engine, "true", "native-external-link-click-barrier.js") == "true",
        "external anchor click did not drain");
    const auto request = take_host_request(engine);
    require(
        request.find(R"("kind":"openExternalUrl")") != std::string::npos
            && request.find(R"("url":"https://example.com/?source=webscene")")
                != std::string::npos
            && request.find(R"("disposition":"systemDefaultBrowser")")
                != std::string::npos,
        "external anchor activation did not emit the typed host request: " + request);
    require(
        evaluate(engine, "location.href", "native-location-after.js") == location_before,
        "external anchor activation replaced the trusted WebScene document");

    const auto popup = evaluate(
        engine,
        "(() => { const url = new URL('https://example.com/chart/'); "
        "url.searchParams.append('utm_medium', 'library'); "
        "const opened = window.open(url.toString(), '_blank'); "
        "opened.opener = null; return { closed: opened.closed, location: location.href }; })()",
        "native-window-open-handoff.js");
    const auto popup_request = take_host_request(engine);
    require(
        popup.find(R"("closed":false)") != std::string::npos
            && popup.find(location_before.substr(1U, location_before.size() - 2U))
                != std::string::npos
            && popup_request.find(R"("kind":"openExternalUrl")") != std::string::npos
            && popup_request.find(R"("url":"https://example.com/chart/?utm_medium=library")")
                != std::string::npos,
        "window.open did not use the safe external host handoff: " + popup_request);

    pointer_button(engine, WEBSCENE_INPUT_POINTER_DOWN, 20, 70, 504U, true);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 20, 70, 505U, false);
    require(
        evaluate(engine, "true", "native-cancelled-link-barrier.js") == "true"
            && take_host_request(engine).empty(),
        "preventDefault did not suppress external navigation handoff");

    pointer_move(engine, 400, 300, 506U);
    require(
        evaluate(engine, "true", "native-cursor-exit-barrier.js") == "true"
            && webscene_engine_get_cursor(engine) == WEBSCENE_CURSOR_DEFAULT,
        "leaving cursor:pointer did not restore the default host cursor");
}

struct resource_server final {
    std::unordered_map<std::string, std::string> content;
    std::atomic<int> active{0};
    std::atomic<int> peak{0};
    std::atomic<int> requests{0};
};

size_t load_test_resource(
    void* user_data,
    uint32_t,
    const char* url,
    size_t url_length,
    const char*,
    size_t,
    int64_t,
    char* destination,
    size_t destination_capacity)
{
    auto& server = *static_cast<resource_server*>(user_data);
    const auto address = std::string(url, url_length);
    const auto known = server.content.find(address);
    if (known == server.content.end()) return 0U;

    if (destination == nullptr && !address.ends_with("index.html")) {
        server.requests.fetch_add(1, std::memory_order_relaxed);
        const auto active = server.active.fetch_add(1, std::memory_order_relaxed) + 1;
        auto peak = server.peak.load(std::memory_order_relaxed);
        while (active > peak
            && !server.peak.compare_exchange_weak(
                peak,
                active,
                std::memory_order_relaxed)) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        server.active.fetch_sub(1, std::memory_order_relaxed);
    }
    else if (destination == nullptr) {
        server.requests.fetch_add(1, std::memory_order_relaxed);
    }

    constexpr size_t header_size = 2U + sizeof(uint32_t) + sizeof(int64_t) * 2U;
    const auto required = header_size + known->second.size();
    if (destination == nullptr || destination_capacity < required) return required;
    destination[0] = 1;
    destination[1] = 1;
    const uint32_t entity_tag_length = 0U;
    const int64_t last_modified = 0;
    const auto fresh_until = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 3600;
    std::memcpy(destination + 2U, &entity_tag_length, sizeof(entity_tag_length));
    std::memcpy(
        destination + 2U + sizeof(entity_tag_length),
        &last_modified,
        sizeof(last_modified));
    std::memcpy(
        destination + 2U + sizeof(entity_tag_length) + sizeof(last_modified),
        &fresh_until,
        sizeof(fresh_until));
    std::copy(known->second.begin(), known->second.end(), destination + header_size);
    return required;
}

void test_parallel_resource_prefetch()
{
    resource_server server{
        .content = {
            {"https://prefetch.test/index.html", R"(
                <html><body>
                  <script src="one.js"></script>
                  <script src="two.js"></script>
                  <script src="three.js"></script>
                </body></html>)"},
            {"https://prefetch.test/one.js", "globalThis.__prefetchValues = [1];"},
            {"https://prefetch.test/two.js", "globalThis.__prefetchValues.push(2);"},
            {"https://prefetch.test/three.js", "globalThis.__prefetchValues.push(3);"},
            {"https://prefetch.test/data.json", R"({"answer":42})"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "prefetch engine creation failed");
    const std::string url = "https://prefetch.test/index.html";
    require(
        webscene_engine_load_url(engine, url.data(), url.size()) != 0,
        "prefetch document load was rejected");
    require(
        evaluate(
            engine,
            "[document.URL, document.documentURI, document.baseURI]",
            "native-document-address-result.js")
            == R"(["https://prefetch.test/index.html","https://prefetch.test/index.html","https://prefetch.test/index.html"])",
        "the loaded document did not expose its fetched address through the Document API");
    const auto result = evaluate(
        engine,
        R"JS((() => {
          const scripts = Array.from(document.getElementsByTagName('script'));
          return {
            values: globalThis.__prefetchValues,
            sources: scripts.map(script => script.getAttribute('src')),
            parented: scripts.every(script => script.parentNode === document.body)
          };
        })())JS",
        "native-parallel-prefetch-result.js");
    require(
        result == R"({"values":[1,2,3],"sources":["one.js","two.js","three.js"],"parented":true})",
        "prefetched scripts or their DOM nodes did not preserve document order");
    execute(
        engine,
        R"JS(
          globalThis.__fetchResult = null;
          fetch("data.json")
            .then(response => response.json().then(value => ({
              ok: response.ok,
              status: response.status,
              answer: value.answer
            })))
            .then(value => { globalThis.__fetchResult = value; });
        )JS",
        "native-fetch-resource.js");
    require(
        evaluate(
            engine,
            "globalThis.__fetchResult",
            "native-fetch-resource-result.js")
            == R"({"ok":true,"status":200,"answer":42})",
        "fetch did not resolve a relative URL through the host resource loader");
    require(
        server.peak.load(std::memory_order_relaxed) >= 2,
        "external document resources were not fetched concurrently");
    webscene_engine_destroy(engine);
}

void test_document_script_failure_remains_diagnostic()
{
    resource_server server{
        .content = {
            {"https://script-error.test/index.html", R"(
                <html><body><script>throw new Error('release page failure');</script></body></html>)"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "script-error engine creation failed");
    const std::string url = "https://script-error.test/index.html";
    require(
        webscene_engine_load_url(engine, url.data(), url.size()) != 0,
        "script-error document load was rejected");

    webscene_engine_metrics metrics{};
    auto diagnostic = std::string{};
    for (auto attempt = 0; attempt < 200; ++attempt) {
        webscene_engine_get_metrics(engine, &metrics);
        diagnostic = last_error(engine);
        if (metrics.frame_script_errors != 0
            && diagnostic.find("release page failure") != std::string::npos) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(metrics.frame_script_errors == 1,
        "document script failure was not counted");
    require(diagnostic.find("release page failure") != std::string::npos,
        "document script failure was cleared before the host could diagnose it: "
            + diagnostic);
    webscene_engine_destroy(engine);
}

void test_outer_document_lifecycle_for_editor_bootstrap()
{
    resource_server server{
        .content = {
            {"https://editor-lifecycle.test/index.html", R"HTML(
                <!doctype html>
                <html><head>
                  <script>
                    globalThis.__editorLifecycle = [
                      ['script', document.readyState]
                    ];
                    document.addEventListener('DOMContentLoaded', () => {
                      __editorLifecycle.push(['domcontentloaded', document.readyState]);
                    });
                    window.addEventListener('DOMContentLoaded', () => {
                      __editorLifecycle.push(['window-domcontentloaded', document.readyState]);
                    });
                    window.addEventListener('load', () => {
                      __editorLifecycle.push(['load', document.readyState]);
                    });
                  </script>
                  <script defer>
                    __editorLifecycle.push(['defer', document.readyState]);
                  </script>
                </head><body><div id="editor"></div></body></html>
            )HTML"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "editor lifecycle engine creation failed");
    const std::string url = "https://editor-lifecycle.test/index.html";
    require(
        webscene_engine_load_url(engine, url.data(), url.size()) != 0,
        "editor lifecycle document load was rejected");
    const auto result = evaluate(
        engine,
        "({ state: document.readyState, events: __editorLifecycle })",
        "native-editor-lifecycle-result.js");
    require(
        result == R"({"state":"complete","events":[["script","loading"],["defer","interactive"],["domcontentloaded","interactive"],["window-domcontentloaded","interactive"],["load","complete"]]})",
        "outer document lifecycle did not follow browser ordering: " + result);
    webscene_engine_destroy(engine);
}

void test_event_listener_exceptions_do_not_abort_document_load()
{
    resource_server server{
        .content = {
            {"https://listener-error.test/index.html", R"HTML(
                <!doctype html>
                <html><head>
                  <script>
                    globalThis.__listenerEvents = [];
                    document.addEventListener('DOMContentLoaded', () => {
                      __listenerEvents.push('before-error');
                      throw new Error('expected listener failure');
                    });
                    document.addEventListener('DOMContentLoaded', () => {
                      __listenerEvents.push('after-error');
                    });
                    window.addEventListener('load', () => {
                      __listenerEvents.push('load');
                    });
                  </script>
                </head><body><div id="content">loaded</div></body></html>
            )HTML"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "listener-error engine creation failed");
    const std::string url = "https://listener-error.test/index.html";
    require(
        webscene_engine_load_url(engine, url.data(), url.size()) != 0,
        "event listener exception incorrectly rejected the document load");
    const auto result = evaluate(
        engine,
        "({ state: document.readyState, events: __listenerEvents })",
        "native-listener-error-result.js");
    require(
        result == R"({"state":"complete","events":["before-error","after-error","load"]})",
        "event listener exception stopped lifecycle dispatch: " + result);

    webscene_engine_metrics metrics{};
    webscene_engine_get_metrics(engine, &metrics);
    require(metrics.frame_script_errors == 1,
        "event listener exception was not retained as a diagnostic");
    webscene_engine_destroy(engine);
}

void test_dom_implementation_create_html_document()
{
    resource_server server{
        .content = {
            {"https://detached-document.test/index.html", R"(
                <html data-shell="live"><head></head><body>
                  <main id="live-root"></main><a href="/release">Release</a>
                </body></html>)"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "detached-document engine creation failed");
    const std::string url = "https://detached-document.test/index.html";
    require(
        webscene_engine_load_url(engine, url.data(), url.size()) != 0,
        "detached-document load was rejected");

    const auto result = evaluate(
        engine,
        R"JS((() => {
          const implementation = document.implementation;
          const detached = implementation.createHTMLDocument('Release notes');
          detached.body.innerHTML = '<form class="entry"></form><form class="entry"></form>';
          const detachedNode = detached.createElement('aside');
          detachedNode.className = 'future-release';
          detached.body.appendChild(detachedNode);
          const style = detached.createElement('a').style;
          const cssomAliasPresent = 'transition-timing-function' in style;
          style['transition-timing-function'] = 'ease-in-out';
          const cookieDescriptor = Object.getOwnPropertyDescriptor(Document.prototype, 'cookie');
          let cookieCache = document.cookie;
          Object.defineProperty(document, 'cookie', {
            get() { return cookieCache; },
            set(value) {
              cookieDescriptor.set.call(document, value);
              cookieCache = cookieDescriptor.get.call(document);
            },
            configurable: true
          });
          document.cookie = 'release_theme=dark; Path=/; SameSite=Lax';
          document.cookie = 'release_seen=1; Path=/';
          document.cookie = 'release_theme=light; Path=/';
          const wrappedCookieAccessors = typeof cookieDescriptor.get === 'function'
            && typeof cookieDescriptor.set === 'function'
            && document.cookie === 'release_theme=light; release_seen=1';
          delete document.cookie;
          return {
            stableImplementation: implementation === document.implementation,
            distinct: detached !== document,
            nodeType: detached.nodeType,
            defaultView: detached.defaultView,
            location: detached.location,
            documentElement: detached.documentElement.tagName,
            head: detached.head.tagName,
            body: detached.body.tagName,
            title: detached.head.querySelector('title').textContent,
            forms: detached.getElementsByTagName('form').length,
            entries: detached.getElementsByClassName('entry').length,
            futureRelease: detached.querySelector('.future-release') === detachedNode,
            cssomAliasPresent,
            cssomAliasValue: style.transitionTimingFunction,
            liveDocumentBindings: document.documentElement.getAttribute('data-shell') === 'live'
              && document.body.tagName === 'BODY',
            liveLocation: document.location === window.location
              && document.location.search === ''
              && document.location.pathname === '/index.html',
            liveLinks: document.links.length === 1
              && document.links.item(0).textContent === 'Release',
            liveCookies: document.cookie === 'release_theme=light; release_seen=1',
            wrappedCookieAccessors,
            detachedCookies: detached.cookie,
            liveRootUnaffected: document.getElementById('live-root') !== null
              && document.querySelector('.future-release') === null,
            address: [detached.URL, detached.documentURI, detached.baseURI]
          };
        })())JS",
        "native-create-html-document-result.js");
    require(
        result == R"({"stableImplementation":true,"distinct":true,"nodeType":9,"defaultView":null,"location":null,"documentElement":"HTML","head":"HEAD","body":"BODY","title":"Release notes","forms":2,"entries":2,"futureRelease":true,"cssomAliasPresent":true,"cssomAliasValue":"ease-in-out","liveDocumentBindings":true,"liveLocation":true,"liveLinks":true,"liveCookies":true,"wrappedCookieAccessors":true,"detachedCookies":"","liveRootUnaffected":true,"address":["about:blank","about:blank","about:blank"]})",
        "DOMImplementation.createHTMLDocument did not create an isolated usable document: " + result);
    webscene_engine_destroy(engine);
}

void test_loaded_document_keeps_html_and_body_cascade_distinct()
{
    resource_server server{
        .content = {
            {"https://root-cascade.test/index.html", R"(
                <!doctype html>
                <html data-root="html"><head></head><body data-root="body">
                  <main id="content" style="height: 1200px"></main>
                  <style>
                    html {
                      z-index: inherit;
                      position: inherit;
                      overflow: inherit;
                      background-color: inherit;
                    }
                    body { overflow: scroll; background-color: #ffc0cb; }
                  </style>
                </body></html>)"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "root-cascade engine creation failed");
    const std::string url = "https://root-cascade.test/index.html";
    require(
        webscene_engine_load_url(engine, url.data(), url.size()) != 0,
        "root-cascade document load was rejected");

    const auto result = evaluate(
        engine,
        R"JS((() => {
          const root = document.documentElement;
          const body = document.body;
          const rootStyle = getComputedStyle(root);
          const bodyStyle = getComputedStyle(body);
          const rootHasScrollRange = root.clientHeight > 0
            && root.scrollHeight > root.clientHeight;
          root.scrollTop = 23;
          const rootScrolls = root.scrollTop === 23;
          root.scrollTop = 0;
          root.style.zIndex = '7';
          const authoredZ = getComputedStyle(root).zIndex;
          root.style.removeProperty('z-index');
          return {
            tags: [root.tagName, document.head.tagName, body.tagName],
            parented: body.parentNode === root && document.head.parentNode === root,
            distinct: root !== body,
            scrollingElement: document.scrollingElement === root,
            rootHasScrollRange,
            rootScrolls,
            roots: document.querySelectorAll('html').length,
            bodies: document.querySelectorAll('body').length,
            attributes: [root.getAttribute('data-root'), body.getAttribute('data-root')],
            rootStyle: [rootStyle.zIndex, rootStyle.position, rootStyle.overflow,
              rootStyle.backgroundColor],
            bodyStyle: [bodyStyle.overflow, bodyStyle.backgroundColor],
            authoredZ,
            restoredZ: getComputedStyle(root).zIndex
          };
        })())JS",
        "native-root-cascade-result.js");
    require(
        result == R"JSON({"tags":["HTML","HEAD","BODY"],"parented":true,"distinct":true,"scrollingElement":true,"rootHasScrollRange":true,"rootScrolls":true,"roots":1,"bodies":1,"attributes":["html","body"],"rootStyle":["auto","static","visible","rgba(0, 0, 0, 0)"],"bodyStyle":["scroll","rgb(255, 192, 203)"],"authoredZ":"7","restoredZ":"auto"})JSON",
        "loaded HTML/BODY cascade or computed root initials were collapsed: " + result);
    const auto consumed_before_wheel = consumed_input_count(engine);
    wheel_input(engine, 10, 10, 111, 34U);
    wait_for_consumed_inputs(
        engine,
        consumed_before_wheel + 1,
        "loaded-document root wheel input was not consumed");
    const auto wheel_result = evaluate(
        engine,
        R"JS(({
          scrollTop: document.scrollingElement.scrollTop,
          contentTop: document.getElementById('content').getBoundingClientRect().top
        }))JS",
        "native-root-cascade-wheel-result.js");
    require(
        wheel_result == R"({"scrollTop":111,"contentTop":-111})",
        "loaded-document wheel input did not use the scrollingElement viewport owner: "
            + wheel_result);
    webscene_engine_destroy(engine);
}

void test_relative_stylesheet_background_uses_stylesheet_address()
{
    resource_server server{
        .content = {
            {"https://relative-style.test/app/index.html", R"(
                <html><head>
                  <link rel="stylesheet" href="assets/theme.css">
                </head><body><div class="badge"></div></body></html>)"},
            {"https://relative-style.test/app/assets/theme.css", R"(
                .badge {
                  width: 100px;
                  height: 60px;
                  background-image: url("../../images/badge.svg");
                  background-repeat: no-repeat;
                  background-position: 0 2px;
                  background-size: 48px auto;
                })"},
            {"https://relative-style.test/images/badge.svg",
                R"(<svg xmlns="http://www.w3.org/2000/svg" width="8" height="4"><rect width="8" height="4" fill="#00ff00"/></svg>)"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "relative stylesheet resource engine creation failed");
    const std::string url = "https://relative-style.test/app/index.html";
    require(
        webscene_engine_load_url(engine, url.data(), url.size()) != 0,
        "relative stylesheet resource document load failed");
    const auto computed = evaluate(engine, R"JS((() => {
      const style = getComputedStyle(document.querySelector('.badge'));
      return {
        image: style.backgroundImage,
        repeat: style.backgroundRepeat,
        position: style.backgroundPosition,
        size: style.backgroundSize
      };
    })())JS", "relative-stylesheet-background-cssom.js");
    require(
        computed.find("https://relative-style.test/images/badge.svg") != std::string::npos
            && computed.find(R"("repeat":"no-repeat")") != std::string::npos,
        "relative CSS url() was not resolved against the stylesheet address: " + computed);

    webscene_engine_request_scene_checkpoint(engine);
    auto found = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind == 6U
                    && std::abs(command.y - 2.0F) < 0.01F
                    && std::abs(command.width - 48.0F) < 0.01F
                    && std::abs(command.height - 24.0F) < 0.01F) {
                    found = true;
                    break;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (found) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(found, "stylesheet-relative SVG background did not reach the native scene");
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    const auto report = feature_use(engine);
    require(
        report.find(R"("feature":"load:failed")") == std::string::npos,
        "stylesheet-relative background resource was reported as a failed load");
    require(
        report.find(R"("feature":"function:url","classification":"partially-supported","count":1,"semanticSlice":"first URL-backed SVG CSS background layer")")
            != std::string::npos,
        "CSS url() was not reported as the bounded implemented background slice");
#endif
    webscene_engine_destroy(engine);
}

void test_resource_cache_reuse_across_engine_generations()
{
    const auto cache_directory = std::filesystem::temp_directory_path()
        / ("webscene-native-resource-memory-cache-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(cache_directory);
    const auto cache_path = cache_directory.string();
    resource_server server{
        .content = {
            {"https://resource-cache.test/index.html", R"(
                <html><body><script src="value.js"></script></body></html>)"},
            {"https://resource-cache.test/value.js",
                "globalThis.__cachedResourceValue = 42; globalThis.__webSceneComponentReady = true;"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        cache_path.data(),
        cache_path.size(),
        load_test_resource,
        &server};
    const std::string url = "https://resource-cache.test/index.html";

    auto* first = webscene_engine_create_with_options(&options);
    require(first != nullptr, "first resource-cache engine creation failed");
    require(
        webscene_engine_load_url(first, url.data(), url.size()) != 0,
        "first resource-cache document load failed");
    require(
        evaluate(first, "globalThis.__cachedResourceValue", "resource-cache-first.js") == "42",
        "first resource-cache script did not execute");
    const auto requests_after_first =
        server.requests.load(std::memory_order_relaxed);
    webscene_engine_destroy(first);

    auto* second = webscene_engine_create_with_options(&options);
    require(second != nullptr, "second resource-cache engine creation failed");
    require(
        webscene_engine_load_url(second, url.data(), url.size()) != 0,
        "second resource-cache document load failed");
    require(
        evaluate(second, "globalThis.__cachedResourceValue", "resource-cache-second.js") == "42",
        "process-cached resource did not execute in the second engine");
    require(
        requests_after_first == 2
            && server.requests.load(std::memory_order_relaxed)
                == requests_after_first,
        "document and script bodies were not reused from the process resource cache");
    webscene_engine_destroy(second);

    std::error_code cleanup_error;
    std::filesystem::remove_all(cache_directory, cleanup_error);
}

void test_parsed_css_rule_payloads_are_shared_across_live_engines()
{
    resource_server server{
        .content = {
            {"https://shared-css.test/index.html", R"(
                <html><head><link rel="stylesheet" href="shared.css"></head>
                <body><div class="shared-alpha">shared</div></body></html>)"},
            {"https://shared-css.test/shared.css", R"(
                .shared-alpha { color: rgb(1, 2, 3); padding: 4px; }
                .shared-beta:hover { opacity: 0.75; }
                @media (min-width: 1px) {
                    .shared-gamma { display: block; }
                })"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    constexpr std::string_view url = "https://shared-css.test/index.html";
    auto* first = webscene_engine_create_with_options(&options);
    auto* second = webscene_engine_create_with_options(&options);
    require(first != nullptr && second != nullptr,
        "shared-CSS engine creation failed");
    require(
        webscene_engine_load_url(first, url.data(), url.size()) != 0
            && webscene_engine_load_url(second, url.data(), url.size()) != 0,
        "shared-CSS documents did not load");

    webscene_engine_memory_metrics first_memory{
        sizeof(webscene_engine_memory_metrics)};
    webscene_engine_memory_metrics second_memory{
        sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        webscene_engine_get_memory_metrics(first, &first_memory);
        webscene_engine_get_memory_metrics(second, &second_memory);
        if (first_memory.native_css_rule_count >= 3U
            && second_memory.native_css_rule_count >= 3U
            && second_memory.process_shared_css_rule_count >= 3U) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        first_memory.native_css_rule_count >= 3U
            && second_memory.native_css_rule_count
                == first_memory.native_css_rule_count,
        "equivalent live engines did not retain the same logical CSS rules");
    require(
        second_memory.process_shared_css_rule_count
            < first_memory.native_css_rule_count
                + second_memory.native_css_rule_count,
        "equivalent CSS rule payloads were duplicated across live engines");
    require(
        second_memory.process_shared_css_rule_storage_bytes > 0U,
        "shared CSS payload allocation attribution was absent");
    webscene_engine_destroy(second);
    webscene_engine_destroy(first);
}

void test_process_wide_resource_load_single_flight()
{
    constexpr size_t engine_count = 4U;
    const auto shared_script =
        std::string{
            "globalThis.__sharedResourceUnicode = \"🙂\";\n"
            "globalThis.__sharedResourceExecutions = "
            "(globalThis.__sharedResourceExecutions || 0) + 1;\n/*"}
        + std::string(128U * 1024U, 'x')
        + "*/";
    const auto shared_ascii_script =
        std::string{"globalThis.__sharedAsciiResource = true;\n/*"}
        + std::string(8U * 1024U, 'a')
        + "*/";
    const auto unique_suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto origin = "https://resource-single-flight-" + unique_suffix + ".test/";
    const auto document_url = origin + "index.html";
    const auto script_url = origin + "shared.js";
    resource_server server{
        .content = {
            {document_url,
                "<html><body><script src=\"shared.js\"></script>"
                "<script src=\"shared-ascii.js\"></script></body></html>"},
            {script_url, shared_script},
            {origin + "shared-ascii.js", shared_ascii_script}
        }};
    const auto cache_directory = std::filesystem::temp_directory_path()
        / ("webscene-native-resource-single-flight-test-" + unique_suffix);
    std::filesystem::create_directories(cache_directory);
    const auto cache_path = cache_directory.string();
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        cache_path.data(),
        cache_path.size(),
        load_test_resource,
        &server};

    std::array<webscene_engine*, engine_count> engines{};
    for (auto& engine : engines) {
        engine = webscene_engine_create_with_options(&options);
        require(engine != nullptr, "resource single-flight engine creation failed");
    }

    std::atomic<size_t> ready{0U};
    std::atomic<bool> start{false};
    std::array<std::thread, engine_count> workers;
    std::array<std::string, engine_count> results;
    for (size_t index = 0U; index < engine_count; ++index) {
        workers[index] = std::thread([&, index] {
            ready.fetch_add(1U, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            require(
                webscene_engine_load_url(
                    engines[index],
                    document_url.data(),
                    document_url.size()) != 0,
                "resource single-flight document load was rejected");
            results[index] = evaluate(
                engines[index],
                "globalThis.__sharedResourceUnicode === \"🙂\" "
                "&& globalThis.__sharedAsciiResource "
                "? globalThis.__sharedResourceExecutions : -1",
                "resource-single-flight-result-" + unique_suffix + ".js");
        });
    }
    while (ready.load(std::memory_order_acquire) != engine_count) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);
    for (auto& worker : workers) worker.join();

    uint64_t leaders = 0U;
    uint64_t waiters = 0U;
    uint64_t memory_hits = 0U;
    uint64_t shared_bytes = 0U;
    uint64_t script_source_hits = 0U;
    uint64_t script_source_shared_bytes = 0U;
    for (size_t index = 0U; index < engine_count; ++index) {
        require(results[index] == "1", "resource sharing leaked mutable script state");
        webscene_process_cache_metrics metrics{
            sizeof(webscene_process_cache_metrics)};
        require(
            webscene_engine_get_process_cache_metrics(engines[index], &metrics) != 0,
            "resource single-flight metrics were unavailable");
        leaders += metrics.resource_load_leaders;
        waiters += metrics.resource_load_waiters;
        memory_hits += metrics.resource_memory_hits;
        shared_bytes += metrics.resource_shared_bytes;
        script_source_hits += metrics.script_source_memory_hits;
        script_source_shared_bytes += metrics.script_source_shared_bytes;

        webscene_engine_memory_metrics memory{
            sizeof(webscene_engine_memory_metrics)};
        require(
            webscene_engine_get_memory_metrics(engines[index], &memory) != 0,
            "external script-source memory metrics were unavailable");
        require(
            memory.v8_external_script_source_bytes
                >= shared_script.size() + shared_ascii_script.size(),
            "large UTF-8 script source was copied into the V8 heap");
        require(
            memory.process_resource_mapped_cache_bytes >= shared_script.size(),
            "large persisted script resource was not retained as file-backed memory");
    }
    require(
        server.requests.load(std::memory_order_relaxed) == 3,
        "identical resources were loaded more than once across four engines");
    require(leaders == 3U, "resource loads did not elect one producer per URL");
    require(waiters > 0U, "concurrent resource loads did not record waiters");
    require(memory_hits > 0U, "resource waiters did not consume shared results");
    require(shared_bytes > 0U, "resource waiters did not share immutable response bytes");
    if (std::getenv("WEBSCENE_V8_SHARED_ISOLATE") == nullptr) {
        require(
            script_source_hits >= 2U * (engine_count - 1U),
            "identical live engines did not reuse the external script-source backing");
        require(
            script_source_shared_bytes
                >= (engine_count - 1U)
                    * (shared_script.size() + shared_ascii_script.size()),
            "shared external script-source bytes were not attributed");
    }

    for (auto* engine : engines) webscene_engine_destroy(engine);
    std::error_code cleanup_error;
    std::filesystem::remove_all(cache_directory, cleanup_error);
}

enum class cache_policy_mode {
    fresh,
    stale_validator,
    no_store
};

struct cache_policy_server final {
    std::string url;
    cache_policy_mode mode{cache_policy_mode::fresh};
    std::string entity_tag{"\"v1\""};
    int value{1};
    int requests{0};
    int conditional_requests{0};
    bool online{true};
};

size_t load_cache_policy_resource(
    void* user_data,
    uint32_t,
    const char* url,
    size_t url_length,
    const char* entity_tag,
    size_t entity_tag_length,
    int64_t,
    char* destination,
    size_t destination_capacity)
{
    auto& server = *static_cast<cache_policy_server*>(user_data);
    if (std::string_view(url, url_length) != server.url || !server.online) return 0U;
    const auto request_tag = std::string_view(entity_tag, entity_tag_length);
    const auto not_modified = server.mode == cache_policy_mode::stale_validator
        && request_tag == server.entity_tag;
    const auto cacheable = server.mode != cache_policy_mode::no_store;
    const auto body = not_modified
        ? std::string{}
        : "<html><body><script>globalThis.__policyValue="
            + std::to_string(server.value)
            + ";globalThis.__webSceneComponentReady=true;</script></body></html>";
    constexpr size_t header_size = 2U + sizeof(uint32_t) + sizeof(int64_t) * 2U;
    const auto required = header_size + server.entity_tag.size() + body.size();
    if (destination == nullptr || destination_capacity < required) {
        if (destination == nullptr) {
            ++server.requests;
            if (!request_tag.empty()) ++server.conditional_requests;
        }
        return required;
    }
    destination[0] = not_modified ? 2 : 1;
    destination[1] = cacheable ? 1 : 0;
    const auto tag_length = static_cast<uint32_t>(server.entity_tag.size());
    const int64_t last_modified = 1'700'000'000;
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const int64_t fresh_until = server.mode == cache_policy_mode::fresh ? now + 3600 : 0;
    std::memcpy(destination + 2U, &tag_length, sizeof(tag_length));
    std::memcpy(destination + 2U + sizeof(tag_length), &last_modified, sizeof(last_modified));
    std::memcpy(
        destination + 2U + sizeof(tag_length) + sizeof(last_modified),
        &fresh_until,
        sizeof(fresh_until));
    std::copy(
        server.entity_tag.begin(),
        server.entity_tag.end(),
        destination + header_size);
    std::copy(
        body.begin(),
        body.end(),
        destination + header_size + server.entity_tag.size());
    return required;
}

int load_policy_value(cache_policy_server& server)
{
    const auto cache_path = (std::filesystem::current_path()
        / "webscene-native-cache-policy-matrix").string();
    std::filesystem::create_directories(cache_path);
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        cache_path.data(),
        cache_path.size(),
        load_cache_policy_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "cache-policy engine creation failed");
    require(
        webscene_engine_load_url(engine, server.url.data(), server.url.size()) != 0,
        "cache-policy document load was rejected");
    const auto value = evaluate(engine, "globalThis.__policyValue", "cache-policy-value.js");
    webscene_engine_destroy(engine);
    return std::stoi(value);
}

void test_resource_cache_policy_matrix()
{
    const auto cache_directory = std::filesystem::current_path()
        / "webscene-native-cache-policy-matrix";
    std::error_code cleanup_error;
    std::filesystem::remove_all(cache_directory, cleanup_error);

    cache_policy_server fresh{"https://cache-policy.test/fresh.html"};
    require(load_policy_value(fresh) == 1, "fresh resource initial load failed");
    fresh.value = 2;
    require(load_policy_value(fresh) == 1, "fresh resource was needlessly revalidated");
    require(fresh.requests == 1, "fresh resource reached the loader more than once");

    cache_policy_server revalidated{
        "https://cache-policy.test/revalidated.html",
        cache_policy_mode::stale_validator};
    require(load_policy_value(revalidated) == 1, "stale resource initial load failed");
    require(load_policy_value(revalidated) == 1, "304 did not reuse the cached body");
    require(
        revalidated.requests == 2 && revalidated.conditional_requests == 1,
        "stale resource did not revalidate with its validator");

    cache_policy_server changed{
        "https://cache-policy.test/changed.html",
        cache_policy_mode::stale_validator};
    require(load_policy_value(changed) == 1, "changed resource initial load failed");
    changed.entity_tag = "\"v2\"";
    changed.value = 2;
    require(load_policy_value(changed) == 2, "changed 200 response did not replace the body");
    require(changed.conditional_requests == 1, "changed resource omitted its prior validator");

    cache_policy_server no_cache{
        "https://cache-policy.test/no-cache.html",
        cache_policy_mode::stale_validator};
    require(load_policy_value(no_cache) == 1, "no-cache initial load failed");
    require(load_policy_value(no_cache) == 1, "no-cache 304 did not reuse the body");
    require(no_cache.requests == 2, "no-cache resource was not revalidated");

    cache_policy_server no_store{
        "https://cache-policy.test/no-store.html",
        cache_policy_mode::no_store};
    require(load_policy_value(no_store) == 1, "no-store initial load failed");
    no_store.value = 2;
    no_store.entity_tag = "\"v2\"";
    require(load_policy_value(no_store) == 2, "no-store response was incorrectly retained");
    require(no_store.requests == 2, "no-store resource bypassed the loader");

    cache_policy_server offline{
        "https://cache-policy.test/offline.html",
        cache_policy_mode::stale_validator};
    require(load_policy_value(offline) == 1, "offline resource initial load failed");
    offline.online = false;
    require(load_policy_value(offline) == 1, "permitted offline warm cache did not serve its body");
    offline.online = true;
    offline.value = 2;
    offline.entity_tag = "\"v2\"";
    require(
        load_policy_value(offline) == 1,
        "failed revalidation did not receive a short stale-cache backoff");
    require(
        offline.requests == 1,
        "stale-cache backoff retried the recovered origin immediately");

    std::filesystem::remove_all(cache_directory, cleanup_error);
}

void test_due_timer_precedes_dynamic_resource_wave()
{
    resource_server server{
        .content = {
            {"https://task-fairness.test/index.html", R"(
                <html><body><script>
                  globalThis.__taskOrder = [];
                  setTimeout(() => globalThis.__taskOrder.push('timer'), 0);
                  const script = document.createElement('script');
                  script.src = 'dynamic.js';
                  script.onload = () => globalThis.__taskOrder.push('load');
                  document.body.appendChild(script);
                </script></body></html>)"},
            {"https://task-fairness.test/dynamic.js",
                "globalThis.__taskOrder.push('script');"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "task-fairness engine creation failed");
    const std::string url = "https://task-fairness.test/index.html";
    require(
        webscene_engine_load_url(engine, url.data(), url.size()) != 0,
        "task-fairness document load failed");

    auto order = std::string{};
    for (auto attempt = 0; attempt < 200; ++attempt) {
        order = evaluate(engine, "globalThis.__taskOrder", "task-fairness-result.js");
        if (order == R"(["timer","script","load"])") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        order == R"(["timer","script","load"])",
        "a due timer was starved behind the dynamic resource task source");
    webscene_engine_destroy(engine);
}

void test_dynamic_stylesheet_custom_properties_preserve_cascade_order()
{
    resource_server server{
        .content = {
            {"https://dynamic-cascade.test/index.html", R"(
                <html><body><script>
                  const symbol = document.createElement('div');
                  symbol.className = 'symbol';
                  const volume = document.createElement('span');
                  volume.className = 'cell volume hidden';
                  symbol.appendChild(volume);
                  document.body.appendChild(symbol);
                  globalThis.__dynamicCascadeDisplay = 'loading';
                  const link = document.createElement('link');
                  link.rel = 'stylesheet';
                  link.href = 'watchlist.css';
                  link.onload = () => {
                    globalThis.__dynamicCascadeDisplay =
                      getComputedStyle(volume).display;
                  };
                  document.head.appendChild(link);
                </script></body></html>)"},
            {"https://dynamic-cascade.test/watchlist.css", R"(
                :root { --watchlist-volume-display: flex; }
                .symbol .cell.volume {
                  display: var(--watchlist-volume-display, flex);
                }
                .symbol .cell.hidden { display: none; }
            )"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "dynamic-cascade engine creation failed");
    const std::string url = "https://dynamic-cascade.test/index.html";
    require(
        webscene_engine_load_url(engine, url.data(), url.size()) != 0,
        "dynamic-cascade document load failed");

    auto display = std::string{};
    for (auto attempt = 0; attempt < 200; ++attempt) {
        display = evaluate(
            engine,
            "globalThis.__dynamicCascadeDisplay",
            "dynamic-cascade-result.js");
        if (display == R"("none")") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        display == R"("none")",
        "custom-property recascade overrode a later display declaration: "
            + display);
    webscene_engine_destroy(engine);
}

void test_persistent_compilation_cache_reuse()
{
    const auto cache_directory = std::filesystem::temp_directory_path()
        / ("webscene-native-v8-cache-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(cache_directory);
    const auto cache_path = cache_directory.string();
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        cache_path.data(),
        cache_path.size(),
        nullptr,
        nullptr};
    constexpr std::string_view source =
        "globalThis.__persistentCacheValue = "
        "(globalThis.__persistentCacheValue || 0) + 1;";
    constexpr std::string_view document_name = "native-persistent-cache-script.js";
    constexpr std::string_view result_name = "native-persistent-cache-result.js";

    auto* first = webscene_engine_create_with_options(&options);
    require(first != nullptr, "first persistent-cache engine creation failed");
    execute(first, source, document_name);
    require(
        evaluate(first, "globalThis.__persistentCacheValue", result_name) == "1",
        "freshly compiled script produced an unexpected result");
    webscene_engine_metrics first_metrics{};
    webscene_engine_get_metrics(first, &first_metrics);
    require(
        first_metrics.compilation_cache_bytes_written > 0U,
        "first engine did not persist V8 compilation data");
    webscene_engine_destroy(first);

    auto* second = webscene_engine_create_with_options(&options);
    require(second != nullptr, "second persistent-cache engine creation failed");
    execute(second, source, document_name);
    require(
        evaluate(second, "globalThis.__persistentCacheValue", result_name) == "1",
        "persisted compilation data changed script semantics in a fresh isolate");
    webscene_engine_metrics second_metrics{};
    webscene_engine_get_metrics(second, &second_metrics);
    webscene_process_cache_metrics second_process_metrics{
        sizeof(webscene_process_cache_metrics)};
    require(
        webscene_engine_get_process_cache_metrics(second, &second_process_metrics) != 0,
        "process compilation-cache metrics were unavailable");
    require(
        second_metrics.compilation_persistent_hits >= 2U
            || second_process_metrics.compilation_memory_hits >= 2U
            || (std::getenv("WEBSCENE_V8_SHARED_ISOLATE") != nullptr
                && second_metrics.compilation_memory_hits >= 2U),
        "second engine did not reuse process or persisted V8 compilation data");
    require(
        second_metrics.compilation_cache_bytes_read > 0U
            || second_process_metrics.compilation_shared_bytes > 0U
            || (std::getenv("WEBSCENE_V8_SHARED_ISOLATE") != nullptr
                && second_metrics.compilation_memory_hits >= 2U),
        "second engine did not consume reusable V8 compilation data");
    webscene_engine_destroy(second);

    std::error_code cleanup_error;
    std::filesystem::remove_all(cache_directory, cleanup_error);
}

void test_executed_compilation_units_enrich_persistent_cache()
{
    const auto cache_directory = std::filesystem::temp_directory_path()
        / ("webscene-native-hot-v8-cache-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(cache_directory);
    const auto cache_path = cache_directory.string();
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        cache_path.data(),
        cache_path.size(),
        nullptr,
        nullptr};

    std::string source = "globalThis.__hotCacheValue = 0;\n";
    for (auto function = 0; function < 128; ++function) {
        source += "function hot" + std::to_string(function)
            + "(value) { let total = value; for (let i = 0; i < 16; i++) "
              "total = ((total * 33) ^ i) | 0; return total; }\n"
              "globalThis.__hotCacheValue ^= hot"
            + std::to_string(function) + "(" + std::to_string(function) + ");\n";
    }
    constexpr std::string_view document_name = "native-hot-persistent-cache-script.js";

    auto* first = webscene_engine_create_with_options(&options);
    require(first != nullptr, "hot-cache engine creation failed");
    execute(first, source, document_name);
    require(
        evaluate(first, "typeof globalThis.__hotCacheValue", "hot-cache-barrier.js")
            == R"("number")",
        "hot compilation unit did not execute");
    auto cache_file = std::filesystem::path{};
    uintmax_t initial_size = 0U;
    for (const auto& entry : std::filesystem::directory_iterator(cache_directory)) {
        if (entry.path().extension() == ".v8cache"
            && entry.file_size() > initial_size) {
            cache_file = entry.path();
            initial_size = entry.file_size();
        }
    }
    require(!cache_file.empty(), "initial hot compilation cache was not written");
    webscene_engine_destroy(first);
    const auto enriched_size = std::filesystem::file_size(cache_file);
    require(
        enriched_size > initial_size,
        "executed lazy functions did not enrich the persisted compilation unit");

    auto* second = webscene_engine_create_with_options(&options);
    require(second != nullptr, "second hot-cache engine creation failed");
    execute(second, source, document_name);
    require(
        evaluate(second, "typeof globalThis.__hotCacheValue", "hot-cache-second-barrier.js")
            == R"("number")",
        "enriched compilation unit did not execute in the second engine");
    webscene_engine_metrics metrics{};
    webscene_engine_get_metrics(second, &metrics);
    webscene_process_cache_metrics process_metrics{
        sizeof(webscene_process_cache_metrics)};
    require(
        webscene_engine_get_process_cache_metrics(second, &process_metrics) != 0,
        "hot-cache process metrics were unavailable");
    require(
        metrics.compilation_persistent_hits > 0U
            || metrics.compilation_memory_hits > 0U
            || process_metrics.compilation_memory_hits > 0U,
        "hot compilation unit was not reused by the second engine");
    webscene_engine_destroy(second);

    std::error_code cleanup_error;
    std::filesystem::remove_all(cache_directory, cleanup_error);
}

void test_process_wide_compilation_single_flight()
{
    constexpr size_t engine_count = 4U;
    const auto cache_directory = std::filesystem::temp_directory_path()
        / ("webscene-native-v8-single-flight-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(cache_directory);
    const auto cache_path = cache_directory.string();
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        cache_path.data(),
        cache_path.size(),
        nullptr,
        nullptr};

    std::string source =
        "globalThis.__singleFlightExecutions = "
        "(globalThis.__singleFlightExecutions || 0) + 1;\n";
    for (auto function = 0; function < 12'000; ++function) {
        source += "function sharedCold" + std::to_string(function)
            + "(value){return value+" + std::to_string(function) + ";}\n";
    }
    const auto unique_suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto document_name = "native-process-single-flight-" + unique_suffix + ".js";
    const auto barrier_name = "native-process-single-flight-barrier-" + unique_suffix + ".js";

    std::array<webscene_engine*, engine_count> engines{};
    for (auto& engine : engines) {
        engine = webscene_engine_create_with_options(&options);
        require(engine != nullptr, "single-flight engine creation failed");
    }

    std::atomic<size_t> ready{0U};
    std::atomic<bool> start{false};
    std::array<std::thread, engine_count> workers;
    std::array<std::string, engine_count> results;
    for (size_t index = 0U; index < engine_count; ++index) {
        workers[index] = std::thread([&, index] {
            ready.fetch_add(1U, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            execute(engines[index], source, document_name);
            results[index] = evaluate(
                engines[index],
                "globalThis.__singleFlightExecutions",
                barrier_name);
        });
    }
    while (ready.load(std::memory_order_acquire) != engine_count) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);
    for (auto& worker : workers) worker.join();

    uint64_t leaders = 0U;
    uint64_t waiters = 0U;
    uint64_t memory_hits = 0U;
    uint64_t shared_bytes = 0U;
    uint64_t persistent_misses = 0U;
    for (size_t index = 0U; index < engine_count; ++index) {
        require(results[index] == "1", "single-flight sharing leaked mutable globals");
        webscene_process_cache_metrics process_metrics{
            sizeof(webscene_process_cache_metrics)};
        require(
            webscene_engine_get_process_cache_metrics(
                engines[index],
                &process_metrics) != 0,
            "single-flight process metrics were unavailable");
        webscene_engine_metrics metrics{};
        webscene_engine_get_metrics(engines[index], &metrics);
        leaders += process_metrics.compilation_leaders;
        waiters += process_metrics.compilation_waiters;
        memory_hits += process_metrics.compilation_memory_hits;
        shared_bytes += process_metrics.compilation_shared_bytes;
        persistent_misses += metrics.compilation_persistent_misses;
    }

    require(
        leaders == 2U,
        "four engines did not elect exactly one producer for each cold compilation unit");
    if (std::getenv("WEBSCENE_V8_SHARED_ISOLATE") == nullptr) {
        require(
            memory_hits == (engine_count - 1U) * 2U,
            "cold compilation results were not consumed by every non-producing isolate");
    }
    // A shared isolate intentionally serializes V8 compilation entry. The
    // followers still reuse the producer's immutable cached data, but they can
    // arrive after production completes rather than blocking as waiters.
    if (std::getenv("WEBSCENE_V8_SHARED_ISOLATE") == nullptr) {
        require(
            waiters > 0U,
            "the concurrent cold-start test did not observe compilation waiters");
    }
    if (std::getenv("WEBSCENE_V8_SHARED_ISOLATE") == nullptr) {
        require(
            shared_bytes > 0U,
            "non-producing isolates did not consume shared immutable cache bytes");
    }
    require(
        persistent_misses == 2U,
        "more than one engine read the cold persistent cache for a compilation unit");
    webscene_engine_memory_metrics memory{
        sizeof(webscene_engine_memory_metrics)};
    require(
        webscene_engine_get_memory_metrics(engines.front(), &memory) != 0,
        "single-flight memory metrics were unavailable");
    require(
        memory.process_compilation_mapped_cache_bytes > 0U,
        "large persisted V8 compilation data was not retained as file-backed memory");

    for (auto* engine : engines) webscene_engine_destroy(engine);
    std::error_code cleanup_error;
    std::filesystem::remove_all(cache_directory, cleanup_error);
}

void test_responsive_positioned_sizing(webscene_engine* engine)
{
    resize(engine, 1280, 720, 1);
    execute(engine, R"JS(
        (() => {
          const style = document.createElement('style');
          style.textContent = `
            .dialog { position:fixed; width:100%; min-width:380px; max-width:550px; }
            @media (max-width:379px) { .dialog { min-width:100%; } }
          `;
          document.body.appendChild(style);
          const dialog = document.createElement('div');
          dialog.className = 'dialog';
          document.body.appendChild(dialog);
          globalThis.__testDialog = dialog;
        })()
    )JS", "native-responsive-positioned-setup.js");

    auto desktop = evaluate(engine, R"JS(
        (() => ({
          width: __testDialog.getBoundingClientRect().width,
          minWidth: getComputedStyle(__testDialog).getPropertyValue('min-width'),
          narrow: matchMedia('(max-width:379px)').matches,
          wide: matchMedia('(min-width:1020px)').matches
        }))()
    )JS", "native-responsive-positioned-desktop.js");
    require(desktop == R"({"width":550,"minWidth":"380px","narrow":false,"wide":true})",
        "desktop media rules or fixed min/max sizing regressed: " + desktop);

    resize(engine, 320, 720, 2);
    auto narrow = evaluate(engine, R"JS(
        (() => ({
          width: __testDialog.getBoundingClientRect().width,
          minWidth: getComputedStyle(__testDialog).getPropertyValue('min-width'),
          narrow: matchMedia('(max-width:379px)').matches,
          wide: matchMedia('(min-width:1020px)').matches
        }))()
    )JS", "native-responsive-positioned-narrow.js");
    if (narrow != R"({"width":320,"minWidth":"100%","narrow":true,"wide":false})") {
        fail("responsive rules did not recascade after resize: " + narrow);
    }
}

void test_responsive_unset_restores_auto_inset(webscene_engine* engine)
{
    resize(engine, 500, 687, 3);
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              .snackbar {
                position: fixed;
                display: flex;
                inset-inline-start: 12px;
                inset-inline-end: 12px;
                justify-content: center;
                top: 12px;
              }
              @media (min-width:568px) {
                .snackbar { bottom: 12px; top: unset; }
              }
              .wrapper { display: contents; }
              .box {
                display: flex;
                min-height: 40px;
                width: fit-content;
              }
            </style>
            <div class="snackbar">
              <div class="wrapper"><div class="box">Saved</div></div>
            </div>`;
          globalThis.__testSnackbar =
            document.querySelector('.snackbar');
          globalThis.__testSnackbarBox =
            document.querySelector('.box');
        })()
    )JS", "native-responsive-unset-inset-setup.js");

    const auto compact = evaluate(engine, R"JS(
        (() => {
          const layer = __testSnackbar.getBoundingClientRect();
          const box = __testSnackbarBox.getBoundingClientRect();
          return [layer.y, layer.height, box.y, box.height];
        })()
    )JS", "native-responsive-unset-inset-compact.js");
    require(
        compact == "[12,40,12,40]",
        "the compact snackbar did not retain its top inset: " + compact);

    resize(engine, 694, 687, 4);
    const auto wide = evaluate(engine, R"JS(
        (() => {
          const layer = __testSnackbar.getBoundingClientRect();
          const box = __testSnackbarBox.getBoundingClientRect();
          return [layer.y, layer.height, box.y, box.height];
        })()
    )JS", "native-responsive-unset-inset-wide.js");
    require(
        wide == "[635,40,635,40]",
        "top:unset did not restore the auto inset for a bottom snackbar: " + wide);
}

void test_preferred_color_scheme_updates_css_and_match_media(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `
            #color-scheme-probe { --resolved-scheme: light; }
            @media (prefers-color-scheme: dark) {
              #color-scheme-probe { --resolved-scheme: dark; }
            }
          `;
          document.body.appendChild(style);
          const probe = document.createElement('div');
          probe.id = 'color-scheme-probe';
          document.body.appendChild(probe);
          globalThis.__colorSchemeProbe = probe;
        })()
    )JS", "native-preferred-color-scheme-setup.js");

    const auto read_state = [&] {
        return evaluate(engine, R"JS(
            ({
              dark: matchMedia('(prefers-color-scheme: dark)').matches,
              light: matchMedia('(prefers-color-scheme: light)').matches,
              css: getComputedStyle(__colorSchemeProbe)
                .getPropertyValue('--resolved-scheme').trim()
            })
        )JS", "native-preferred-color-scheme-state.js");
    };

    require(
        read_state() == R"({"dark":false,"light":true,"css":"light"})",
        "native media environment did not default to light");
    require(
        webscene_engine_set_preferred_color_scheme(engine, 2U) == 0U,
        "native engine accepted an unknown preferred color scheme");
    require(
        webscene_engine_set_preferred_color_scheme(
            engine,
            WEBSCENE_PREFERRED_COLOR_SCHEME_DARK) != 0U,
        "native engine rejected the dark preferred color scheme");
    const auto dark = read_state();
    require(
        dark == R"({"dark":true,"light":false,"css":"dark"})",
        "dark host preference did not update CSS and matchMedia: " + dark);

    require(
        webscene_engine_set_preferred_color_scheme(
            engine,
            WEBSCENE_PREFERRED_COLOR_SCHEME_LIGHT) != 0U,
        "native engine rejected the light preferred color scheme");
    const auto light = read_state();
    require(
        light == R"({"dark":false,"light":true,"css":"light"})",
        "light host preference did not restore CSS and matchMedia: " + light);
}

void test_resize_listener_receives_window_event(webscene_engine* engine)
{
    execute(engine, R"JS(
        window.addEventListener('resize', event => {
          globalThis.__resizeEventContract = {
            type: event.type,
            target: event.target === window,
            currentTarget: event.currentTarget === window,
            srcElement: event.srcElement === window,
            view: event.view === window,
            bubbles: event.bubbles,
            cancelable: event.cancelable,
            hasPreventDefault: typeof event.preventDefault === 'function'
          };
        });
    )JS", "native-resize-event-setup.js");
    require(
        evaluate(engine, "true", "native-resize-event-setup-barrier.js") == "true",
        "resize Event listener setup did not complete");
    resize(engine, 640, 480, 4);
    auto consumed = false;
    for (auto attempt = 0; attempt < 100 && !consumed; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            consumed = scene->header.consumed_input_sequence >= 4U;
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (!consumed) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(consumed, "resize Event input was not consumed");
    const auto result = evaluate(
        engine,
        "globalThis.__resizeEventContract",
        "native-resize-event-result.js");
    require(
        result == R"({"type":"resize","target":true,"currentTarget":true,"srcElement":true,"view":true,"bubbles":false,"cancelable":false,"hasPreventDefault":true})",
        "window resize listener did not receive a browser-shaped Event: " + result);
}

void test_screen_tracks_viewport()
{
    auto* engine = webscene_engine_create(16);
    require(engine != nullptr, "screen regression engine creation failed");
    resize(engine, 1024, 640, 5);
    const auto initial = evaluate(engine, R"JS(
        (() => ({
          stable: screen === window.screen,
          constructor: typeof Screen === 'function' && screen instanceof Screen,
          width: screen.width,
          height: screen.height,
          availWidth: screen.availWidth,
          availHeight: screen.availHeight,
          colorDepth: screen.colorDepth,
          pixelDepth: screen.pixelDepth
        }))()
    )JS", "native-screen-initial.js");
    require(
        initial == R"({"stable":true,"constructor":true,"width":1024,"height":640,"availWidth":1024,"availHeight":640,"colorDepth":24,"pixelDepth":24})",
        "Screen did not expose stable viewport-backed dimensions: " + initial);

    resize(engine, 800, 600, 6);
    const auto resized = evaluate(engine, R"JS(
        ({
          sameObject: screen === window.screen,
          width: screen.width,
          height: screen.height,
          innerWidth,
          innerHeight
        })
    )JS", "native-screen-resized.js");
    require(
        resized == R"({"sameObject":true,"width":800,"height":600,"innerWidth":800,"innerHeight":600})",
        "Screen dimensions did not follow the host viewport: " + resized);
    webscene_engine_destroy(engine);
}

void test_absolute_portal_centers_against_positioned_ancestor(webscene_engine* engine)
{
    resize(engine, 1280, 720, 3);
    const auto result = evaluate(engine, R"JS(
        (() => {
          const style = document.createElement('style');
          style.textContent = `
            .hint-host {
              position:relative; margin-left:120px; width:1000px; height:600px;
            }
            .hint-portal { display:inline-block; width:0; height:0; }
            .hint-rail {
              position:absolute; inset-inline-start:10px; inset-inline-end:10px;
              display:flex; justify-content:center;
            }
            .hint-center { top:240px; }
            .hint-bottom { bottom:24px; }
            .hint-coachmark { width:300px; height:72px; }
            .hint-toast { width:420px; height:48px; }
          `;
          document.body.appendChild(style);
          const host = document.createElement('div');
          host.className = 'hint-host';
          const appendHint = (railClass, contentClass) => {
            const portal = document.createElement('span');
            portal.className = 'hint-portal';
            const rail = document.createElement('div');
            rail.className = `hint-rail ${railClass}`;
            const content = document.createElement('div');
            content.className = contentClass;
            rail.appendChild(content);
            portal.appendChild(rail);
            host.appendChild(portal);
            return { portal, rail, content };
          };
          const coachmark = appendHint('hint-center', 'hint-coachmark');
          const toast = appendHint('hint-bottom', 'hint-toast');
          document.body.appendChild(host);
          const rect = node => node.getBoundingClientRect();
          const hostRect = rect(host);
          const coachRail = rect(coachmark.rail);
          const coachRect = rect(coachmark.content);
          const toastRect = rect(toast.content);
          const hostCenter = hostRect.x + hostRect.width / 2;
          return {
            portalWidth: rect(coachmark.portal).width,
            railX: coachRail.x,
            railWidth: coachRail.width,
            coachCentered: Math.abs(coachRect.x + coachRect.width / 2 - hostCenter) < 0.01,
            toastCentered: Math.abs(toastRect.x + toastRect.width / 2 - hostCenter) < 0.01
          };
        })()
    )JS", "native-absolute-portal-centering.js");
    if (result != R"({"portalWidth":0,"railX":130,"railWidth":980,"coachCentered":true,"toastCentered":true})") {
        fail("absolute portal hints did not center within their positioned ancestor: " + result);
    }
}

void test_document_position(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const parent = document.createElement('div');
          const first = document.createElement('span');
          const second = document.createElement('span');
          parent.appendChild(first);
          parent.appendChild(second);
          document.body.appendChild(parent);
          return [
            parent.compareDocumentPosition(first),
            first.compareDocumentPosition(parent),
            first.compareDocumentPosition(second),
            second.compareDocumentPosition(first),
            first.compareDocumentPosition(first)
          ];
        })()
    )JS", "native-compare-document-position.js");
    require(result == "[20,10,4,2,0]", "compareDocumentPosition bit flags regressed");
}

void test_attribute_selector_invalidation(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const style = document.createElement('style');
          style.textContent = `
            .attribute-host[data-mode="wide"] .attribute-child { width: 120px; }
            .attribute-host[aria-expanded="true"] .attribute-child { height: 33px; }
          `;
          document.body.appendChild(style);
          const host = document.createElement('div');
          host.className = 'attribute-host';
          const child = document.createElement('div');
          child.className = 'attribute-child';
          host.appendChild(child);
          document.body.appendChild(host);
          host.dataset.mode = 'wide';
          host.setAttribute('aria-expanded', 'true');
          const computed = getComputedStyle(child);
          return [
            computed.getPropertyValue('width'),
            computed.getPropertyValue('height')
          ];
        })()
    )JS", "native-attribute-selector-invalidation.js");
    require(
        result == R"(["120px","33px"])",
        "attribute-dependent descendant styles were not invalidated");
}

void test_attribute_selector_list_requires_authored_attribute(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const root = document.createElement('div');
          root.innerHTML = `
            <x-td id="plain" style="width: 50px"></x-td>
            <x-td id="spanning" rowspan="2"></x-td>`;
          document.body.appendChild(root);
          return {
            first: root.querySelector('x-td[rowspan], x-td[colspan]')?.id,
            count: root.querySelectorAll('x-td[rowspan], x-td[colspan]').length
          };
        })()
    )JS", "native-attribute-selector-list.js");
    require(
        result == R"({"first":"spanning","count":1})",
        "selector-list attribute existence matching regressed: " + result);
}

void test_script_raw_text_does_not_create_style_descendants(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <div id="raw-text-target" style="display:block;width:40px;height:10px"></div>
            <script id="data-block" type="application/json">{"fragment":"<style>#raw-text-target{display:none}</style>"}</script>`;
          const dataBlock = document.getElementById('data-block');
          const target = document.getElementById('raw-text-target');
          return {
            tag: dataBlock?.tagName,
            type: dataBlock?.type,
            text: dataBlock?.textContent,
            nestedStyles: dataBlock?.querySelectorAll('style').length,
            targetDisplay: getComputedStyle(target).display,
            targetWidth: target.offsetWidth,
            targetHeight: target.offsetHeight
          };
        })()
    )JS", "native-script-raw-text.js");
    require(
        result == R"({"tag":"SCRIPT","type":"application/json","text":"{\"fragment\":\"<style>#raw-text-target{display:none}</style>\"}","nestedStyles":0,"targetDisplay":"block","targetWidth":40,"targetHeight":10})",
        "SCRIPT raw text was reparsed as descendant markup: " + result);
}

void test_attribute_selector_operators(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const target = document.createElement('div');
          target.id = 'attribute-operators';
          target.setAttribute('data-value', 'prefix middle suffix');
          target.setAttribute('lang', 'en-US');
          document.body.appendChild(target);
          return [
            '[data-value^="prefix"]',
            '[data-value$="suffix"]',
            '[data-value*="middle"]',
            '[data-value~="middle"]',
            '[lang|="en"]'
          ].map(selector => document.querySelector(selector)?.id ?? null);
        })()
    )JS", "native-attribute-selector-operators.js");
    require(
        result == R"(["attribute-operators","attribute-operators","attribute-operators","attribute-operators","attribute-operators"])",
        "attribute selector operators regressed: " + result);
}

void test_replace_child_advances_attribute_selector_iteration(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const root = document.createElement('div');
          root.innerHTML = `
            <x-td id="row" rowspan="2"></x-td>
            <x-td id="column" colspan="2"></x-td>`;
          document.body.appendChild(root);
          const replaced = [];
          for (let count = 0; count < 5; count++) {
            const previous = root.querySelector('x-td[rowspan], x-td[colspan]');
            if (!previous) break;
            replaced.push(previous.id);
            const replacement = document.createElement('td');
            for (let index = previous.attributes.length; index--;) {
              replacement.setAttribute(
                previous.attributes[index].name,
                previous.attributes[index].value);
            }
            previous.parentNode.replaceChild(replacement, previous);
          }
          return {
            replaced,
            remaining: root.querySelectorAll('x-td[rowspan], x-td[colspan]').length,
            childTags: Array.from(root.children).map(child => child.tagName)
          };
        })()
    )JS", "native-replace-child-selector-iteration.js");
    require(
        result == R"({"replaced":["row","column"],"remaining":0,"childTags":["TD","TD"]})",
        "replaceChild did not advance selector iteration: " + result);
}

void test_insert_before_preserves_tree_identity_and_atomicity(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const parent = document.createElement('div');
          const first = document.createElement('span');
          const moved = document.createElement('span');
          const last = document.createElement('span');
          first.id = 'first';
          moved.id = 'moved';
          last.id = 'last';
          parent.appendChild(first);
          parent.appendChild(moved);
          parent.appendChild(last);
          document.body.appendChild(parent);

          const returnedSelf = parent.insertBefore(moved, moved);
          const selfOrder = Array.from(parent.children).map(node => node.id).join(',');
          const selfParent = moved.parentNode === parent;

          const other = document.createElement('div');
          const foreign = document.createElement('span');
          other.appendChild(foreign);
          document.body.appendChild(other);
          let invalidThrew = false;
          try {
            parent.insertBefore(moved, foreign);
          } catch (_error) {
            invalidThrew = true;
          }
          const rejectedOrder = Array.from(parent.children).map(node => node.id).join(',');
          const rejectedParent = moved.parentNode === parent;

          parent.insertBefore(first, last);
          const reordered = Array.from(parent.children).map(node => node.id).join(',');
          const target = document.createElement('div');
          const reference = document.createElement('span');
          reference.id = 'reference';
          target.appendChild(reference);
          document.body.appendChild(target);
          target.insertBefore(first, reference);

          return {
            returnedSelf: returnedSelf === moved,
            selfOrder,
            selfParent,
            invalidThrew,
            rejectedOrder,
            rejectedParent,
            reordered,
            sourceOrder: Array.from(parent.children).map(node => node.id).join(','),
            targetOrder: Array.from(target.children).map(node => node.id).join(','),
            reparented: first.parentNode === target
          };
        })()
    )JS", "native-insert-before-tree-identity.js");
    require(
        result == R"({"returnedSelf":true,"selfOrder":"first,moved,last","selfParent":true,"invalidThrew":true,"rejectedOrder":"first,moved,last","rejectedParent":true,"reordered":"moved,first,last","sourceOrder":"moved,last","targetOrder":"first,reference","reparented":true})",
        "insertBefore tree identity or failed-mutation atomicity regressed: " + result);
}

void test_related_tree_mutations_preserve_identity_and_atomicity(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const parent = document.createElement('div');
          const first = document.createElement('span');
          const node = document.createElement('span');
          const last = document.createElement('span');
          const foreign = document.createElement('span');
          first.id = 'first';
          node.id = 'node';
          last.id = 'last';
          parent.appendChild(first);
          parent.appendChild(node);
          parent.appendChild(last);
          document.body.appendChild(parent);

          let invalidRemoveThrew = false;
          try {
            parent.removeChild(foreign);
          } catch (_error) {
            invalidRemoveThrew = true;
          }
          const orderAfterRemove = Array.from(parent.children).map(child => child.id).join(',');
          const returnedSelf = parent.replaceChild(node, node);
          const orderAfterReplace = Array.from(parent.children).map(child => child.id).join(',');

          const root = document.createElement('div');
          const child = document.createElement('div');
          const grandchild = document.createElement('div');
          root.appendChild(child);
          child.appendChild(grandchild);
          document.body.appendChild(root);
          let ancestorAppendThrew = false;
          try {
            grandchild.appendChild(root);
          } catch (_error) {
            ancestorAppendThrew = true;
          }
          let ancestorReplaceThrew = false;
          try {
            child.replaceChild(root, grandchild);
          } catch (_error) {
            ancestorReplaceThrew = true;
          }

          return {
            invalidRemoveThrew,
            orderAfterRemove,
            removeAtomic: node.parentNode === parent,
            returnedSelf: returnedSelf === node,
            orderAfterReplace,
            replaceSelfParent: node.parentNode === parent,
            ancestorAppendThrew,
            ancestorReplaceThrew,
            hierarchyAtomic: root.parentNode === document.body
              && child.parentNode === root
              && grandchild.parentNode === child
          };
        })()
    )JS", "native-related-tree-mutation-atomicity.js");
    require(
        result == R"({"invalidRemoveThrew":true,"orderAfterRemove":"first,node,last","removeAtomic":true,"returnedSelf":true,"orderAfterReplace":"first,node,last","replaceSelfParent":true,"ancestorAppendThrew":true,"ancestorReplaceThrew":true,"hierarchyAtomic":true})",
        "related DOM tree-mutation identity or atomicity regressed: " + result);
}

void test_monaco_browser_primitives(webscene_engine* engine)
{
    execute(
        engine,
        "globalThis.__editorMicrotasks = ['sync']; "
        "queueMicrotask(() => __editorMicrotasks.push('microtask'));",
        "native-editor-queue-microtask.js");
    const auto result = evaluate(engine, R"JS(
        (() => {
          class WebSceneEditorElement extends HTMLElement {}
          customElements.define('webscene-editor-element', WebSceneEditorElement);
          const utf8 = new TextDecoder().decode(
            new Uint8Array([0x48, 0x69, 0x20, 0xf0, 0x9f, 0x91, 0x8b]));
          const utf16 = new TextDecoder('utf-16le').decode(
            new Uint16Array([0x3c, 0x64, 0x69, 0x76, 0x3e]));
          const encoded = Array.from(new TextEncoder().encode('Aé'));
          const destination = new Uint8Array(8);
          const into = new TextEncoder().encodeInto('Aé', destination);
          const elementNode = document.createElement('span');
          const textNode = document.createTextNode('token');
          const caretHost = document.createElement('div');
          caretHost.style.position = 'absolute';
          caretHost.style.left = '0px';
          caretHost.style.top = '0px';
          caretHost.style.fontFamily = 'monospace';
          caretHost.style.fontSize = '14px';
          const caretToken = document.createElement('span');
          caretToken.appendChild(document.createTextNode('function'));
          caretHost.appendChild(caretToken);
          document.body.appendChild(caretHost);
          const caretRect = caretToken.getBoundingClientRect();
          const caretRange = document.caretRangeFromPoint(
            caretRect.left + caretRect.width * 0.4,
            caretRect.top + caretRect.height * 0.5);
          const measurement = document.createElement('span');
          measurement.style.fontFamily = 'monospace';
          measurement.style.fontSize = '14px';
          measurement.textContent = '0'.repeat(256);
          document.body.appendChild(measurement);
          const digitWidth = measurement.offsetWidth / 256;
          measurement.remove();
          return {
            microtasks: __editorMicrotasks,
            customElement: customElements.get('webscene-editor-element')
              === WebSceneEditorElement,
            utf8,
            utf16,
            encoded,
            into: [into.read, into.written, ...destination.slice(0, into.written)],
            observers: [
              typeof ResizeObserver,
              typeof MutationObserver,
              typeof IntersectionObserver
            ],
            events: [
              typeof KeyboardEvent,
              typeof InputEvent,
              typeof UIEvent
            ],
            nodeConstants: [
              elementNode.nodeType,
              elementNode.ELEMENT_NODE,
              textNode.nodeType,
              textNode.TEXT_NODE,
              elementNode.DOCUMENT_NODE,
              elementNode.DOCUMENT_FRAGMENT_NODE
            ],
            windowOffsets: [
              window.scrollX,
              window.scrollY,
              window.pageXOffset,
              window.pageYOffset
            ],
            caretHit: [
              typeof document.caretRangeFromPoint,
              caretRange.startContainer.nodeType,
              caretRange.startContainer.TEXT_NODE,
              caretRange.startContainer.parentNode === caretToken,
              caretRange.startOffset > 0
                && caretRange.startOffset < caretRange.startContainer.textContent.length
            ],
            inlineFontMetric: digitWidth > 5 && digitWidth < 20
          };
        })()
    )JS", "native-editor-browser-primitives.js");
    const auto expected =
        R"({"microtasks":["sync","microtask"],"customElement":true,"utf8":"Hi 👋","utf16":"<div>","encoded":[65,195,169],"into":[2,3,65,195,169],"observers":["function","function","function"],"events":["function","function","function"],"nodeConstants":[1,1,3,3,9,11],"windowOffsets":[0,0,0,0],"caretHit":["function",3,3,true,true],"inlineFontMetric":true})";
    require(
        result == expected,
        "Monaco browser primitives are incomplete: " + result);
}

void test_monaco_view_line_dom_mutations(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const layer = document.createElement('div');
          layer.className = 'view-lines';
          document.body.appendChild(layer);
          layer.innerHTML =
            '<div class="view-line" style="top:0px;height:19px;">'
              + '<span><span class="mtk6">const</span><span class="mtk1"> answer = </span>'
              + '<span class="mtk4">42</span><span class="mtk1">;</span></span></div>'
            + '<div class="view-line" style="top:19px;height:19px;">'
              + '<span><span class="mtk8">// native WebScene</span></span></div>'
            + '<div class="view-line" style="top:38px;height:19px;">'
              + '<span><span class="mtk6">function</span><span class="mtk1"> greet() {</span></span></div>';

          const initial = Array.from(layer.childNodes);
          const backward = [];
          for (let node = layer.lastChild; node; node = node.previousSibling) {
            backward.push(node.textContent);
          }
          layer.lastChild.insertAdjacentHTML(
            'afterend',
            '<div class="view-line" style="top:57px;height:19px;">'
              + '<span><span class="mtk1">}</span></span></div>');
          const replacement = document.createElement('div');
          replacement.className = 'view-line';
          replacement.innerHTML =
            '<span><span class="mtk8">// editable model</span></span>';
          layer.children[1].replaceWith(replacement);

          return {
            initialCount: initial.length,
            backward,
            finalCount: layer.childNodes.length,
            finalText: Array.from(layer.childNodes, node => node.textContent),
            siblingChain:
              layer.lastChild.previousSibling.previousSibling.previousSibling
                === layer.firstChild,
            replacementParent: replacement.parentNode === layer,
            oldDetached: initial[1].parentNode === null,
            tokenCount: layer.querySelectorAll('span').length
          };
        })()
    )JS", "native-monaco-view-line-dom.js");
    const auto expected =
        R"({"initialCount":3,"backward":["function greet() {","// native WebScene","const answer = 42;"],"finalCount":4,"finalText":["const answer = 42;","// editable model","function greet() {","}"],"siblingChain":true,"replacementParent":true,"oldDetached":true,"tokenCount":12})";
    require(
        result == expected,
        "Monaco view-line DOM mutation semantics regressed: " + result);
}

void test_visibility_inherits_for_computed_style_and_focus(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const host = document.createElement('div');
          host.style.visibility = 'hidden';
          const inherited = document.createElement('button');
          inherited.id = 'inherited-hidden';
          const restored = document.createElement('button');
          restored.id = 'restored-visible';
          restored.style.visibility = 'visible';
          host.appendChild(inherited);
          host.appendChild(restored);
          document.body.appendChild(host);
          inherited.focus();
          const hiddenFocus = document.activeElement?.id || '';
          restored.focus();
          return {
            host: getComputedStyle(host).visibility,
            inherited: getComputedStyle(inherited).visibility,
            restored: getComputedStyle(restored).visibility,
            hiddenFocus,
            restoredFocus: document.activeElement?.id || ''
          };
        })()
    )JS", "native-inherited-visibility-focus.js");
    require(
        result == R"({"host":"hidden","inherited":"hidden","restored":"visible","hiddenFocus":"","restoredFocus":"restored-visible"})",
        "computed visibility or hidden-element focusability regressed: " + result);
}

void test_hover_specificity_preserves_visible_theme_icon(webscene_engine* engine)
{
    execute(engine, R"JS(
        document.body.innerHTML = `
          <style>
            @media (any-hover: hover) {
              .control:hover .arrow { opacity: 1; }
            }
            .arrow { color: var(--hover-icon); opacity: 0; }
            [data-theme="dark"] { --hover-icon: #c0cacc; }
            .control { width: 60px; height: 60px; }
            .button { width: 40px; height: 40px; }
            .arrow { width: 10px; height: 40px; }
          </style>
          <div class="control" data-theme="dark">
            <button class="button">Cursor</button><button class="arrow">‹</button>
          </div>`;
    )JS", "native-hover-specificity-setup.js");
    resize(engine, 320, 200, 101U);
    const auto pointer_x = std::stod(evaluate(
        engine,
        "document.querySelector('.button').getBoundingClientRect().x + 5",
        "native-hover-specificity-x.js"));
    const auto pointer_y = std::stod(evaluate(
        engine,
        "document.querySelector('.button').getBoundingClientRect().y + 5",
        "native-hover-specificity-y.js"));
    pointer_move(engine, pointer_x, pointer_y, 102U);
    const auto result = evaluate(engine, R"JS(
        (() => {
          const arrow = document.querySelector('.arrow');
          const computed = getComputedStyle(arrow);
          return [
            computed.opacity,
            computed.color,
            arrow.matches('.control:hover .arrow')
          ];
        })()
    )JS", "native-hover-specificity-result.js");
    require(
        result == R"JSON(["1","rgb(192, 202, 204)",true])JSON",
        "a later low-specificity rule hid or recolored the hovered child: " + result);
}

void test_hover_invalidation_updates_functional_and_sibling_subjects(
    webscene_engine* engine)
{
    execute(engine, R"JS(
        document.body.innerHTML = `
          <style>
            .trigger { width: 40px; height: 40px; }
            .trigger:not(:hover) + .sibling { width: 20px; height: 20px; }
            .trigger:hover + .sibling { width: 40px; height: 20px; }
          </style>
          <button class="trigger">Open</button><div class="sibling"></div>`;
    )JS", "native-hover-sibling-invalidation-setup.js");
    resize(engine, 320, 200, 103U);
    const auto pointer_x = std::stod(evaluate(
        engine,
        "document.querySelector('.trigger').getBoundingClientRect().x + 5",
        "native-hover-sibling-invalidation-x.js"));
    const auto pointer_y = std::stod(evaluate(
        engine,
        "document.querySelector('.trigger').getBoundingClientRect().y + 5",
        "native-hover-sibling-invalidation-y.js"));

    pointer_move(engine, pointer_x, pointer_y, 104U);
    const auto hovered = evaluate(
        engine,
        "getComputedStyle(document.querySelector('.sibling')).width",
        "native-hover-sibling-invalidation-hovered.js");
    pointer_move(engine, 250, 150, 105U);
    const auto unhovered = evaluate(
        engine,
        "getComputedStyle(document.querySelector('.sibling')).width",
        "native-hover-sibling-invalidation-unhovered.js");

    require(
        hovered == R"("40px")" && unhovered == R"("20px")",
        "functional or sibling hover invalidation regressed: hovered="
            + hovered + ", unhovered=" + unhovered);
}

void test_single_fractional_grid_track_stays_one_column(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const grid = document.createElement('div');
          grid.style.display = 'grid';
          grid.style.width = '344px';
          grid.style.height = '169px';
          const style = document.createElement('style');
          style.textContent = `
            .one-track { grid-template-columns: 1fr; grid-template-rows: 1fr; }
            .transparent-wrapper { display: contents; }
          `;
          document.body.appendChild(style);
          grid.className = 'one-track';
          const content = document.createElement('div');
          const wrapper = document.createElement('div');
          wrapper.className = 'transparent-wrapper';
          const footer = document.createElement('div');
          footer.style.height = '0px';
          wrapper.appendChild(content);
          grid.appendChild(wrapper);
          grid.appendChild(footer);
          document.body.appendChild(grid);
          return [
            grid.getBoundingClientRect().width,
            content.getBoundingClientRect().width,
            footer.getBoundingClientRect().width,
            content.getBoundingClientRect().height,
            getComputedStyle(wrapper).display
          ];
        })()
    )JS", "native-single-grid-track.js");
    if (result != R"([344,344,344,169,"contents"])") {
        fail("a single 1fr grid track was split into columns: " + result);
    }
}

void test_calc_percent_with_pixel_offset(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const parent = document.createElement('div');
          parent.style.width = '243px';
          const child = document.createElement('div');
          const style = document.createElement('style');
          style.textContent = `
            .calc-parent { position: relative; height: 80px; }
            .calc-child {
              position: absolute;
              inset-inline-start: calc(50% - 32px);
              top: calc(50% - 12px);
              width: calc(100% - 64px);
              height: 7px;
            }
          `;
          document.body.appendChild(style);
          parent.className = 'calc-parent';
          child.className = 'calc-child';
          parent.appendChild(child);
          document.body.appendChild(parent);
          const rect = child.getBoundingClientRect();
          return [rect.x, rect.y, rect.width];
        })()
    )JS", "native-calc-percent-pixel-offset.js");
    if (result != "[89.5,28,179]") {
        fail("mixed percentage/pixel calc() did not position and size the absolute child: " + result);
    }
}

void test_flex_basis_reserves_fixed_track(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `
            .row { display: flex; align-items: center; width: 273px; height: 31px; }
            .label { flex: 0 0 30px; height: 12px; }
            .track { flex: 1 1 0%; }
            .column { display: flex; flex-direction: column; width: 31px; height: 200px; }
            .vertical-basis { flex: 0 0 80px; width: 12px; }
            .contents-row { display: flex; width: 200px; height: 31px; }
            .contents-wrapper { display: contents; }
            .contents-item { flex: 0 0 80px; height: 12px; }
            .symbol .cell.volume { display: flex; }
            .symbol .cell.hidden { display: none; }
          `;
          document.body.appendChild(style);
          const row = document.createElement('div');
          row.className = 'row';
          const label = document.createElement('div');
          label.className = 'label';
          const track = document.createElement('div');
          track.className = 'track';
          row.appendChild(label);
          row.appendChild(track);
          document.body.appendChild(row);
          const column = document.createElement('div');
          column.className = 'column';
          const verticalBasis = document.createElement('div');
          verticalBasis.className = 'vertical-basis';
          column.appendChild(verticalBasis);
          document.body.appendChild(column);
          const symbol = document.createElement('div');
          symbol.className = 'symbol';
          const hiddenVolume = document.createElement('span');
          hiddenVolume.className = 'cell volume hidden';
          symbol.appendChild(hiddenVolume);
          document.body.appendChild(symbol);
          const contentsRow = document.createElement('div');
          contentsRow.className = 'contents-row';
          const contentsWrapper = document.createElement('div');
          contentsWrapper.className = 'contents-wrapper';
          const contentsItem = document.createElement('div');
          contentsItem.className = 'contents-item';
          contentsWrapper.appendChild(contentsItem);
          contentsRow.appendChild(contentsWrapper);
          document.body.appendChild(contentsRow);
          const wrapperRect = contentsWrapper.getBoundingClientRect();
          const contentsRect = contentsItem.getBoundingClientRect();
          return [
            label.getBoundingClientRect().width,
            label.getBoundingClientRect().height,
            track.getBoundingClientRect().width,
            verticalBasis.getBoundingClientRect().width,
            verticalBasis.getBoundingClientRect().height,
            getComputedStyle(hiddenVolume).display,
            wrapperRect.width,
            wrapperRect.height,
            contentsRect.width,
            contentsRect.height,
            getComputedStyle(label).flexGrow,
            getComputedStyle(track).flexGrow
          ];
        })()
    )JS", "native-flex-basis-track.js");
    if (result != "[30,12,243,12,80,\"none\",0,0,80,12,\"0\",\"1\"]") {
        fail("flex-basis did not remain confined to the flex main axis: " + result);
    }
}

void test_flex_flow_shorthand_controls_layout_and_cssom(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `
            .tabs {
              display: flex;
              flex-flow: column nowrap;
              width: 206px;
              height: 200px;
            }
            .tab { width: 206px; height: 40px; flex: 0 0 auto; }
          `;
          document.body.appendChild(style);
          const tabs = document.createElement('div');
          tabs.className = 'tabs';
          const first = document.createElement('button');
          const second = document.createElement('button');
          first.className = second.className = 'tab';
          tabs.appendChild(first);
          tabs.appendChild(second);
          document.body.appendChild(tabs);

          const firstRect = first.getBoundingClientRect();
          const secondRect = second.getBoundingClientRect();
          const computed = getComputedStyle(tabs);
          const stylesheetValues = [
            computed.flexDirection,
            computed.flexWrap,
            computed.flexFlow,
            computed.getPropertyValue('flex-flow'),
            firstRect.x,
            firstRect.y,
            secondRect.x,
            secondRect.y
          ];

          tabs.style.flexFlow = 'row-reverse wrap';
          const inlineValues = [
            tabs.style.flexFlow,
            getComputedStyle(tabs).flexDirection,
            getComputedStyle(tabs).flexWrap
          ];
          tabs.style.removeProperty('flex-flow');
          const removedValues = [
            tabs.style.flexFlow,
            getComputedStyle(tabs).flexDirection,
            getComputedStyle(tabs).flexWrap
          ];
          return [stylesheetValues, inlineValues, removedValues];
        })()
    )JS", "native-flex-flow-shorthand.js");
    const auto expected =
        R"([["column","nowrap","column nowrap","column nowrap",0,0,0,40],["row-reverse wrap","row-reverse","wrap"],["","column","nowrap"]])";
    if (result != expected) {
        fail("flex-flow shorthand diverged from CSS layout or CSSOM: " + result);
    }
}

void test_font_relative_box_lengths_follow_inherited_font_context(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const container = document.createElement('div');
          container.style.position = 'absolute';
          container.style.left = '0';
          container.style.top = '0';
          container.style.width = '400px';
          container.style.height = '200px';
          container.style.fontSize = '4px';
          const bounded = document.createElement('div');
          bounded.style.position = 'absolute';
          bounded.style.left = '50%';
          bounded.style.right = '50%';
          bounded.style.top = '0';
          bounded.style.height = '20px';
          const sized = document.createElement('div');
          sized.style.display = 'flex';
          sized.style.width = '10em';
          sized.style.minHeight = '2em';
          sized.style.padding = '1em';
          sized.style.gap = '3em';
          sized.style.setProperty('flex-basis', '9em');
          const authoredBasisBeforeConnection = sized.style.getPropertyValue('flex-basis');
          container.appendChild(bounded);
          container.appendChild(sized);
          document.body.appendChild(container);

          bounded.style.right = '25em';
          const initialContainer = container.getBoundingClientRect();
          const constrained = bounded.getBoundingClientRect();
          const constrainedValues = [
            parseFloat(getComputedStyle(bounded).right),
            constrained.left - initialContainer.left,
            initialContainer.right - constrained.right,
            constrained.width
          ];

          bounded.style.inset = '1em 2em 3em 4em';
          const initialInsets = ['top', 'right', 'bottom', 'left'].map(
            name => parseFloat(getComputedStyle(bounded).getPropertyValue(name)));
          container.style.fontSize = '8px';
          const mutatedContainer = container.getBoundingClientRect();
          const mutated = bounded.getBoundingClientRect();
          const mutatedInsets = ['top', 'right', 'bottom', 'left'].map(
            name => parseFloat(getComputedStyle(bounded).getPropertyValue(name)));
          const sizedStyle = getComputedStyle(sized);
          return [
            authoredBasisBeforeConnection,
            sized.style.getPropertyValue('flex-basis'),
            ...constrainedValues,
            ...initialInsets,
            ...mutatedInsets,
            mutated.left - mutatedContainer.left,
            mutatedContainer.right - mutated.right,
            parseFloat(sizedStyle.width),
            parseFloat(sizedStyle.minHeight),
            parseFloat(sizedStyle.paddingLeft),
            parseFloat(sizedStyle.rowGap),
            parseFloat(sizedStyle.columnGap),
            parseFloat(sizedStyle.getPropertyValue('flex-basis'))
          ];
        })()
    )JS", "native-font-relative-box-lengths.js");
    if (result != R"(["9em","9em",100,200,100,100,4,8,12,16,8,16,24,32,32,16,80,16,8,24,24,72])") {
        fail("font-relative box lengths diverged between CSSOM and layout: " + result);
    }
}

void test_floats_share_a_bounded_formatting_line(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            .float-host { width:100px; height:12px; }
            .float-item {
              float:left; width:48px; height:10px;
              border:1px solid blue;
            }
            .float-item.right { float:right; }
          `;
          document.body.appendChild(style);
          const host = document.createElement('div');
          host.className = 'float-host';
          const first = document.createElement('div');
          const second = document.createElement('div');
          first.className = second.className = 'float-item';
          host.appendChild(first);
          host.appendChild(second);
          document.body.appendChild(host);
          const rightHost = document.createElement('div');
          rightHost.className = 'float-host';
          const rightFirst = document.createElement('div');
          const rightSecond = document.createElement('div');
          rightFirst.className = rightSecond.className = 'float-item right';
          rightHost.appendChild(rightFirst);
          rightHost.appendChild(rightSecond);
          document.body.appendChild(rightHost);
          const hostRect = host.getBoundingClientRect();
          const firstRect = first.getBoundingClientRect();
          const secondRect = second.getBoundingClientRect();
          const rightHostRect = rightHost.getBoundingClientRect();
          const rightFirstRect = rightFirst.getBoundingClientRect();
          const rightSecondRect = rightSecond.getBoundingClientRect();
          const cssom = document.createElement('div');
          cssom.style.cssFloat = 'right';
          const cssFloat = cssom.style.cssFloat;
          const propertyValue = cssom.style.getPropertyValue('float');
          cssom.style.removeProperty('float');
          return {
            first: [firstRect.x - hostRect.x, firstRect.y - hostRect.y,
              firstRect.width, firstRect.height],
            second: [secondRect.x - hostRect.x, secondRect.y - hostRect.y,
              secondRect.width, secondRect.height],
            right: [rightFirstRect.x - rightHostRect.x,
              rightSecondRect.x - rightHostRect.x],
            computed: getComputedStyle(first).cssFloat,
            cssom: [cssFloat, propertyValue, cssom.style.cssFloat]
          };
        })()
    )JS", "native-left-float-line.js");
    require(
        result == R"({"first":[0,0,50,12],"second":[50,0,50,12],"right":[50,0],"computed":"left","cssom":["right","right",""]})",
        "consecutive floats or float CSSOM did not share the bounded line: " + result);
}

void test_wrapped_flex_resolves_each_line_independently(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            .host { display:flex; flex-wrap:wrap; width:100px; height:12px; }
            .half { width:48px; border:1px solid blue; }
            .huge { width:148px; border:1px solid purple; }
            .margin-left { margin-left:80px; }
            .force-wrap { margin-right:1px; }
            .inflexible { flex:none; }
          `;
          document.body.appendChild(style);
          const makeHost = (...children) => {
            const host = document.createElement('div');
            host.className = 'host';
            for (const child of children) host.appendChild(child);
            document.body.appendChild(host);
            return host;
          };
          const makeItem = (className) => {
            const item = document.createElement('div');
            item.className = className;
            return item;
          };
          const relativeRect = (host, item) => {
            const parent = host.getBoundingClientRect();
            const child = item.getBoundingClientRect();
            return [child.x - parent.x, child.y - parent.y, child.width, child.height];
          };

          const marginItem = makeItem('half margin-left');
          const marginHost = makeHost(marginItem);
          const first = makeItem('half force-wrap');
          const second = makeItem('half');
          const wrapHost = makeHost(first, second);
          const huge = makeItem('huge');
          const hugeHost = makeHost(huge);
          const inflexible = makeItem('huge inflexible');
          const inflexibleHost = makeHost(inflexible);
          return {
            marginShrink: relativeRect(marginHost, marginItem),
            wrapped: [relativeRect(wrapHost, first), relativeRect(wrapHost, second)],
            hugeShrink: relativeRect(hugeHost, huge),
            inflexible: relativeRect(inflexibleHost, inflexible)
          };
        })()
    )JS", "native-wrapped-flex-lines.js");
    require(
        result == R"({"marginShrink":[80,0,20,12],"wrapped":[[0,0,50,6],[0,6,50,6]],"hugeShrink":[0,0,100,12],"inflexible":[0,0,150,12]})",
        "wrapped flex lines did not resolve shrink and cross-size independently: " + result);
}

void test_zero_height_flex_item_grows_and_hit_tests_descendants(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            .host { display:flex; flex-direction:column; width:200px; height:120px; }
            .space { display:flex; flex:1 1 auto; flex-direction:column; height:0; }
            .tree { height:100%; overflow:hidden; }
            .row { display:flex; height:38px; }
            .title { display:inline-block; width:80px; height:18px; margin:10px; }
          `;
          document.body.appendChild(style);
          const host = document.createElement('div');
          host.className = 'host';
          const space = document.createElement('div');
          space.className = 'space';
          const tree = document.createElement('div');
          tree.className = 'tree';
          const row = document.createElement('div');
          row.className = 'row';
          const title = document.createElement('span');
          title.className = 'title';
          title.textContent = 'Trend Line';
          row.appendChild(title);
          tree.appendChild(row);
          space.appendChild(tree);
          host.appendChild(space);
          document.body.appendChild(host);
          const hit = document.elementFromPoint(20, 19);
          return {
            spaceHeight: space.getBoundingClientRect().height,
            treeHeight: tree.getBoundingClientRect().height,
            hitClass: hit?.className || null,
            hitText: hit?.textContent || null
          };
        })()
    )JS", "native-zero-height-flex-hit-test.js");
    if (result != R"({"spaceHeight":120,"treeHeight":120,"hitClass":"title","hitText":"Trend Line"})") {
        fail("a growable zero-height flex item did not fill or expose descendants to hit testing: "
            + result);
    }
}

void test_empty_non_growing_flex_item_collapses_main_axis(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            .column {
              align-items:flex-start;
              display:flex;
              flex-direction:column-reverse;
              height:120px;
              width:200px;
            }
            .scroll {
              display:flex;
              flex-direction:column-reverse;
              max-height:100%;
              pointer-events:auto;
            }
            .inner {
              align-items:flex-start;
              contain:layout;
              display:flex;
              flex-direction:column;
            }
            .row {
              align-items:flex-start;
              display:flex;
              height:40px;
              width:120px;
            }
          `;
          document.body.appendChild(style);

          const column = document.createElement('section');
          column.className = 'column';
          column.style.pointerEvents = 'none';
          const scroll = document.createElement('div');
          scroll.className = 'scroll';
          const inner = document.createElement('div');
          inner.className = 'inner';
          scroll.appendChild(inner);
          column.appendChild(scroll);
          document.body.appendChild(column);

          const row = document.createElement('div');
          row.className = 'row';
          const empty = document.createElement('div');
          row.appendChild(empty);
          document.body.appendChild(row);

          return {
            column: column.getBoundingClientRect().height,
            scroll: [
              scroll.getBoundingClientRect().width,
              scroll.getBoundingClientRect().height
            ],
            inner: [
              inner.getBoundingClientRect().width,
              inner.getBoundingClientRect().height
            ],
            rowItem: [
              empty.getBoundingClientRect().width,
              empty.getBoundingClientRect().height
            ],
            hit: document.elementFromPoint(100, 60)?.tagName || null
          };
        })()
    )JS", "native-empty-flex-item-collapse.js");
    require(
        result == R"({"column":120,"scroll":[0,0],"inner":[0,0],"rowItem":[0,40],"hit":"BODY"})",
        "empty non-growing flex items expanded across the main axis: " + result);
}

void test_appending_child_invalidates_empty_selector(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `
            [data-role=status-pill] {
              display:inline-flex;
              min-width:6px;
            }
            [data-role=status-pill]:empty {
              display:none;
            }
            [data-role=status-pill]_hidden {
              display:none;
            }
            .status-item {
              display:flex;
              height:18px;
              width:18px;
            }
          `;
          document.body.appendChild(style);
          const pill = document.createElement('button');
          pill.setAttribute('data-role', 'status-pill');
          document.body.appendChild(pill);
          const before = getComputedStyle(pill).display;
          const item = document.createElement('span');
          item.className = 'status-item';
          pill.appendChild(item);
          const rect = pill.getBoundingClientRect();
          return {
            before,
            after: getComputedStyle(pill).display,
            width: rect.width,
            height: rect.height
          };
        })()
    )JS", "dynamic-empty-selector-invalidation.js");
    if (result != R"({"before":"none","after":"inline-flex","width":18,"height":18})") {
        fail("appending a child did not invalidate :empty on its parent: " + result);
    }
}

void test_inline_block_preserves_vertical_padding(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            .toolbar {
              align-items:center;
              display:flex;
              height:38px;
            }
            #documentation {
              background:#2962ff;
              border-radius:80px;
              color:#fff;
              display:inline-block;
              font-size:14px;
              line-height:18px;
              padding:5px 12px;
            }
          `;
          document.body.appendChild(style);
          const toolbar = document.createElement('div');
          toolbar.className = 'toolbar';
          const button = document.createElement('button');
          button.id = 'documentation';
          button.textContent = 'Documentation';
          toolbar.appendChild(button);
          document.body.appendChild(toolbar);
          const rect = button.getBoundingClientRect();
          return {
            y: rect.y,
            height: rect.height,
            paddingTop: getComputedStyle(button).paddingTop,
            paddingBottom: getComputedStyle(button).paddingBottom
          };
        })()
    )JS", "inline-block-vertical-padding.js");
    if (result != R"({"y":5,"height":28,"paddingTop":"5px","paddingBottom":"5px"})") {
        fail("inline-block text box discarded its vertical padding: " + result);
    }
}

void test_pointer_hit_targets_and_related_targets_are_elements(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const row = document.createElement('div');
          row.id = 'pointer-row';
          row.style.width = '120px';
          row.style.height = '40px';
          row.textContent = 'Parent row';
          const outside = document.createElement('div');
          outside.id = 'pointer-outside';
          outside.style.position = 'fixed';
          outside.style.left = '200px';
          outside.style.top = '0';
          outside.style.width = '80px';
          outside.style.height = '40px';
          outside.textContent = 'Outside';
          globalThis.__pointerElementTargets = { entered: null, leftFor: null };
          row.addEventListener('pointerenter', event => {
            globalThis.__pointerElementTargets.entered = event.target?.id || null;
          });
          row.addEventListener('pointerleave', event => {
            globalThis.__pointerElementTargets.leftFor = event.relatedTarget?.id || null;
          });
          document.body.appendChild(row);
          document.body.appendChild(outside);
        })()
    )JS", "native-pointer-element-target-setup.js");
    require(
        evaluate(engine,
            "document.elementFromPoint(10, 10)?.id || null",
            "native-pointer-element-target-hit.js") == "\"pointer-row\"",
        "elementFromPoint exposed an internal text node instead of its nearest element");

    pointer_move(engine, 10, 10, 601U);
    require(
        evaluate(engine, "true", "native-pointer-element-target-enter-drain.js") == "true",
        "pointer entry did not drain");
    pointer_move(engine, 210, 10, 602U);
    const auto state = evaluate(engine,
        "globalThis.__pointerElementTargets",
        "native-pointer-element-target-result.js");
    require(
        state == R"({"entered":"pointer-row","leftFor":"pointer-outside"})",
        "pointer boundary target/relatedTarget did not retain browser element identity: " + state);
}

void test_z_index_orders_positioned_siblings_in_scene(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            .stack-host { display:flex; width:160px; height:24px; }
            .stack-wrapper { width:80px; height:24px; }
            .stack-overlay {
              position:relative; z-index:2; width:80px; height:24px;
              background:rgb(18,52,86);
            }
            .stack-value {
              position:relative; z-index:1; width:80px; height:24px;
              margin-left:-80px; background:rgb(171,205,239);
            }
            .stack-elevated { position:relative; z-index:6; width:80px; height:24px; }
            .stack-negative-child {
              position:relative; z-index:-3; width:80px; height:24px;
              background:rgb(69,103,137); border:1px solid rgb(120,154,188);
              border-right-width:0; border-radius:4px 0 0 4px;
            }
          `;
          document.body.appendChild(style);
          const host = document.createElement('div');
          host.className = 'stack-host';
          const wrapper = document.createElement('div');
          wrapper.className = 'stack-wrapper';
          const overlay = document.createElement('div');
          overlay.className = 'stack-overlay';
          const value = document.createElement('div');
          value.className = 'stack-value';
          wrapper.appendChild(overlay);
          host.appendChild(wrapper);
          host.appendChild(value);
          document.body.appendChild(host);
          const elevated = document.createElement('div');
          elevated.className = 'stack-elevated';
          const negativeChild = document.createElement('div');
          negativeChild.className = 'stack-negative-child';
          elevated.appendChild(negativeChild);
          document.body.appendChild(elevated);
        })()
    )JS", "native-z-index-paint-setup.js");
    const auto fixture = evaluate(
        engine,
        "({ width: document.querySelector('.stack-host').offsetWidth, "
        "z: getComputedStyle(document.querySelector('.stack-overlay')).zIndex, "
        "valueZ: getComputedStyle(document.querySelector('.stack-value')).zIndex, "
        "valueBackground: getComputedStyle(document.querySelector('.stack-value')).backgroundColor, "
        "hit: document.elementFromPoint(10, 10)?.className || null })",
        "native-z-index-paint-ready.js");
    if (fixture.find("\"z\":\"2\"") == std::string::npos
        || fixture.find("\"hit\":\"stack-overlay\"") == std::string::npos) {
        fail("z-index paint fixture did not apply its stacking order: " + fixture);
    }

    webscene_engine_request_scene_checkpoint(engine);
    auto observed_order = false;
    auto observed_nested_foreground = false;
    auto observed_partial_rounded_border = false;
    auto observed_commands = std::string{};
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            auto value_index = -1;
            auto overlay_index = -1;
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind == 9U && (command.rgba >> 8U) == 0xABCDEFU) {
                    value_index = static_cast<int>(index);
                    observed_commands += "underlay=" + std::to_string(index) + ":9 ";
                }
                if ((command.rgba >> 8U) == 0x123456U) {
                    observed_commands += "overlay=" + std::to_string(index) + ":"
                        + std::to_string(command.kind) + " ";
                }
                if (command.kind == 9U && (command.rgba >> 8U) == 0x123456U) {
                    overlay_index = static_cast<int>(index);
                }
                if ((command.rgba >> 8U) == 0x456789U) {
                    observed_commands += "nested=" + std::to_string(index) + ":"
                        + std::to_string(command.kind) + " ";
                    observed_nested_foreground = command.kind == 10U
                        && command.radius_top_left > 0
                        && command.radius_top_right == 0
                        && command.radius_bottom_right == 0
                        && command.radius_bottom_left > 0;
                }
                if ((command.rgba >> 8U) == 0x789ABCU) {
                    observed_commands += "border=" + std::to_string(index) + ":"
                        + std::to_string(command.kind) + ":"
                        + std::to_string(command.flags >> 28U) + " ";
                    observed_partial_rounded_border = command.kind == 11U
                        && (command.flags >> 28U) == 13U
                        && command.radius_top_left > 0
                        && command.radius_top_right == 0
                        && command.radius_bottom_right == 0
                        && command.radius_bottom_left > 0
                        && std::abs(command.stroke_width - 1.0F) < 0.01F;
                }
            }
            observed_order = value_index >= 0 && overlay_index > value_index;
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (observed_order && observed_nested_foreground && observed_partial_rounded_border) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (!observed_order || !observed_nested_foreground || !observed_partial_rounded_border) {
        fail("stacking contexts did not preserve elevated sibling and negative-child paint: "
            + observed_commands + "fixture=" + fixture);
    }
}

void test_flex_gap_and_variable_text_metrics(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `
            .checkbox { display:flex; align-items:flex-start; gap:8px; }
            .box { width:18px; height:18px; flex:0 0 18px; }
            .label { font-size:14px; line-height:18px; }
            .precision { width:60px; font-size:14px; line-height:18px; }
            .actions { display:flex; gap:12px; }
            .action { display:inline-flex; padding:0 11px; border:1px solid; }
            .footer { display:flex; width:300px; }
            .footer-buttons { display:flex; gap:12px; margin-left:auto; }
            .weekdays { display:flex; justify-content:space-around; width:262px; }
            .time-menu { position:fixed; left:400px; top:200px; max-height:231px;
              box-sizing:border-box; }
            .time-scroll { height:100%; overflow-x:hidden; overflow-y:auto; }
            .time-box { padding:6px 0; }
            .time-item { align-items:center; box-sizing:border-box; display:flex;
              font-size:14px; padding:2px 10px 2px 8px; white-space:nowrap; width:100%; }
            .time-row { box-sizing:border-box; display:flex; flex:0 1 100%;
              max-width:100%; min-width:0; padding:0 4px; }
            .time-label { display:flex; flex:0 0 auto; max-width:100%; overflow:hidden; }
            .time-control { display:inline-block; max-width:100px; font-size:14px; line-height:18px; }
            .time-input-shell { display:flex; }
            .time-input { width:100%; font-size:inherit; line-height:inherit; }
            .time-icon { flex:0 0 18px; width:18px; }
          `;
          document.body.appendChild(style);

          const checkbox = document.createElement('label');
          checkbox.className = 'checkbox';
          const box = document.createElement('span');
          box.className = 'box';
          const label = document.createElement('span');
          label.className = 'label';
          label.textContent = 'Body';
          checkbox.appendChild(box);
          checkbox.appendChild(label);
          document.body.appendChild(checkbox);

          const precision = document.createElement('div');
          precision.className = 'precision';
          precision.textContent = 'Precision';
          document.body.appendChild(precision);

          const actions = document.createElement('div');
          actions.className = 'actions';
          for (const text of ['Template', 'Cancel', 'Ok']) {
            const button = document.createElement('button');
            button.className = 'action';
            button.textContent = text;
            actions.appendChild(button);
          }
          document.body.appendChild(actions);

          const footer = document.createElement('div');
          footer.className = 'footer';
          const template = document.createElement('button');
          template.textContent = 'Template';
          const footerButtons = document.createElement('div');
          footerButtons.className = 'footer-buttons';
          footerButtons.appendChild(document.createElement('button')).textContent = 'Cancel';
          footerButtons.appendChild(document.createElement('button')).textContent = 'Ok';
          footer.appendChild(template);
          footer.appendChild(footerButtons);
          document.body.appendChild(footer);

          const weekdays = document.createElement('div');
          weekdays.className = 'weekdays';
          for (const day of ['Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa', 'Su']) {
            weekdays.appendChild(document.createElement('span')).textContent = day;
          }
          document.body.appendChild(weekdays);

          const portal = document.createElement('span');
          const timeMenu = document.createElement('div');
          timeMenu.className = 'time-menu';
          const timeScroll = document.createElement('div');
          timeScroll.className = 'time-scroll';
          const timeBox = document.createElement('div');
          timeBox.className = 'time-box';
          const timeItem = document.createElement('div');
          timeItem.className = 'time-item';
          const timeRow = document.createElement('span');
          timeRow.className = 'time-row';
          const timeLabel = document.createElement('span');
          timeLabel.className = 'time-label';
          timeLabel.textContent = '00:00';
          timeRow.appendChild(timeLabel);
          timeItem.appendChild(timeRow);
          timeBox.appendChild(timeItem);
          timeScroll.appendChild(timeBox);
          timeMenu.appendChild(timeScroll);
          portal.appendChild(timeMenu);
          document.body.appendChild(portal);

          const timeControl = document.createElement('div');
          timeControl.className = 'time-control';
          const timeInputShell = document.createElement('div');
          timeInputShell.className = 'time-input-shell';
          const timeInput = document.createElement('input');
          timeInput.className = 'time-input';
          timeInput.value = '00:00';
          const timeIcon = document.createElement('span');
          timeIcon.className = 'time-icon';
          timeInputShell.appendChild(timeInput);
          timeInputShell.appendChild(timeIcon);
          timeControl.appendChild(timeInputShell);
          document.body.appendChild(timeControl);

          const boxRect = box.getBoundingClientRect();
          const labelRect = label.getBoundingClientRect();
          const buttons = [...actions.children].map(node => node.getBoundingClientRect());
          const footerRect = footer.getBoundingClientRect();
          const templateRect = template.getBoundingClientRect();
          const footerButtonsRect = footerButtons.getBoundingClientRect();
          const weekdayRects = [...weekdays.children].map(node => node.getBoundingClientRect());
          return {
            checkboxGap: Math.round((labelRect.left - boxRect.right) * 100) / 100,
            aligned: Math.abs(labelRect.top - boxRect.top) < 0.01,
            computedGap: getComputedStyle(checkbox).gap,
            precisionOneLine: precision.getBoundingClientRect().height === 18,
            buttonGaps: Math.round(buttons[1].left - buttons[0].right) === 12
              && Math.round(buttons[2].left - buttons[1].right) === 12,
            cancelWideEnough: buttons[1].width >= 60 && buttons[1].width < 75,
            autoMarginPushesActions: footerButtonsRect.left - templateRect.right > 100
              && Math.abs(footerButtonsRect.right - footerRect.right) < 0.01,
            spaceAroundDistributes: weekdayRects[0].left > 5
              && weekdayRects[6].right < weekdays.getBoundingClientRect().right - 5
              && weekdayRects[1].left - weekdayRects[0].right > 10,
            timeMenuPreservesLabel: timeMenu.getBoundingClientRect().width >= 60
              && timeLabel.getBoundingClientRect().width >= 35,
            textInputHasReplacedIntrinsicWidth: timeControl.getBoundingClientRect().width === 100
              && timeInput.getBoundingClientRect().width >= 75,
            inheritedInputTypography: getComputedStyle(timeInput).fontSize === '14px'
              && getComputedStyle(timeInput).lineHeight === '18px'
          };
        })()
    )JS", "native-flex-gap-text-metrics.js");
    const auto expected = R"({"checkboxGap":8,"aligned":true,"computedGap":"8px 8px","precisionOneLine":true,"buttonGaps":true,"cancelWideEnough":true,"autoMarginPushesActions":true,"spaceAroundDistributes":true,"timeMenuPreservesLabel":true,"textInputHasReplacedIntrinsicWidth":true,"inheritedInputTypography":true})";
    if (result != expected) {
        fail("flex gaps or variable text metrics regressed: " + result);
    }
}

void test_outer_box_shadow_reaches_elevated_scene(webscene_engine* engine)
{
    const auto fixture = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `.shadow-probe {
            position:fixed; left:20px; top:30px; width:100px; height:40px;
            border-radius:6px; background:rgb(20,30,40);
            box-shadow:3px 5px 12px 2px rgba(1,2,3,0.5);
          }
          .padded-uppercase {
            position:fixed; left:150px; top:30px; font-size:11px;
            padding-left:6px; color:rgb(4,5,6); text-transform:uppercase;
          }`;
          document.body.appendChild(style);
          const probe = document.createElement('div');
          probe.className = 'shadow-probe';
          document.body.appendChild(probe);
          const text = document.createElement('span');
          text.className = 'padded-uppercase';
          text.textContent = 'Candles';
          document.body.appendChild(text);
          return {
            shadow: getComputedStyle(probe).boxShadow,
            upperWidth: Math.round(text.getBoundingClientRect().width * 100) / 100
          };
        })()
    )JS", "native-box-shadow-setup.js");
    require(fixture.find("12px") != std::string::npos
            && fixture.find(R"("upperWidth":54.84)") != std::string::npos,
        "computed shadow or transformed intrinsic text width was wrong: " + fixture);

    webscene_engine_request_scene_checkpoint(engine);
    auto observed = false;
    auto observed_padded_text = false;
    for (auto attempt = 0; attempt < 100 && (!observed || !observed_padded_text); ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind == 18U
                    && (command.rgba >> 8U) == 0x010203U
                    && std::abs(command.x - 21.0F) < 0.01F
                    && std::abs(command.y - 33.0F) < 0.01F
                    && std::abs(command.width - 104.0F) < 0.01F
                    && std::abs(command.height - 44.0F) < 0.01F
                    && std::abs(command.stroke_width - 12.0F) < 0.01F) {
                    observed = true;
                }
                if (command.kind == 3U
                    && (command.rgba >> 8U) == 0x040506U
                    && std::abs(command.x - 156.0F) < 0.01F) {
                    observed_padded_text = true;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (!observed || !observed_padded_text) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(observed, "elevated outer box-shadow command was absent from the native scene");
    require(observed_padded_text,
        "padded text did not paint from its content-box inset in the native scene");
}

void test_segmented_rounded_borders_share_an_unclipped_join(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              .segment { position: fixed; top: 20px; box-sizing: border-box; height: 24px; border: 1px solid rgb(0, 120, 130); }
              #leading { left: 20px; width: 120px; border-right-width: 0; border-radius: 4px 0 0 4px; }
              #trailing { left: 140px; width: 60px; border-left-width: 0; border-radius: 0 4px 4px 0; }
            </style>
            <div id="segments"><div id="leading" class="segment"></div><div id="trailing" class="segment"></div></div>`;
        })()
    )JS", "segmented-rounded-border-setup.js");

    webscene_engine_request_scene_checkpoint(engine);
    std::vector<webscene_scene_command> borders;
    for (auto attempt = 0; attempt < 100 && borders.size() != 2U; ++attempt) {
        borders.clear();
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind == 8U || command.kind == 11U) borders.push_back(command);
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (borders.size() != 2U) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(borders.size() == 2U, "segmented rounded borders did not reach the native scene");
    std::sort(borders.begin(), borders.end(), [](const auto& left, const auto& right) {
        return left.x < right.x;
    });
    constexpr uint32_t border_color_partition = 1U << 27U;
    constexpr uint32_t border_top = 1U << 28U;
    constexpr uint32_t border_right = 1U << 29U;
    constexpr uint32_t border_bottom = 1U << 30U;
    constexpr uint32_t border_left = 1U << 31U;
    const auto side_mask = border_top | border_right | border_bottom | border_left;
    require(
        (borders[0].flags & border_color_partition) == 0U
            && (borders[1].flags & border_color_partition) == 0U,
        "open segmented borders were incorrectly classified as colour partitions");
    require(
        (borders[0].flags & side_mask) == (border_top | border_bottom | border_left)
            && (borders[1].flags & side_mask) == (border_top | border_right | border_bottom),
        "segmented border side ownership was not preserved: "
            + std::to_string(borders[0].flags & side_mask) + "@"
            + std::to_string(borders[0].x) + "+" + std::to_string(borders[0].width) + ","
            + std::to_string(borders[1].flags & side_mask) + "@"
            + std::to_string(borders[1].x) + "+" + std::to_string(borders[1].width));
    require(
        std::abs((borders[0].x + borders[0].width) - borders[1].x) < 0.01F,
        "adjacent rounded border centerlines left a clipped seam at their open edges");
}

void test_transform_origin_keywords_cascade_independently_from_inline_transform(
    webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `
            .indicator { position:fixed; left:10px; top:20px; width:100px; height:4px;
              transform-origin:left; }
            .segment { position:absolute; top:0; height:4px; }
            .leading { left:0; width:20px; transform-origin:left; }
            .middle { left:0; width:100px; transform-origin:center; }
            .trailing { right:0; width:20px; transform-origin:right; }
          `;
          document.body.appendChild(style);
          const indicator = document.createElement('div');
          indicator.className = 'indicator';
          indicator.style.transform = 'translateX(20px) scaleX(0.8)';
          for (const [className, transform] of [
            ['segment leading', 'scaleX(1.25)'],
            ['segment middle', 'scaleX(0.625)'],
            ['segment trailing', 'scaleX(1.25)']
          ]) {
            const segment = document.createElement('div');
            segment.className = className;
            segment.style.transform = transform;
            indicator.appendChild(segment);
          }
          document.body.appendChild(indicator);
        })()
    )JS", "transform-origin-keyword-cascade.js");

    webscene_engine_request_scene_checkpoint(engine);
    std::vector<webscene_scene_command> scales;
    for (auto attempt = 0; attempt < 100 && scales.size() != 4U; ++attempt) {
        scales.clear();
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                if (scene->commands[index].kind == 15U) scales.push_back(scene->commands[index]);
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (scales.size() != 4U) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    require(scales.size() == 4U,
        "segmented indicator did not publish four nested scale transforms");
    const auto close = [](float actual, float expected) {
        return std::abs(actual - expected) < 0.01F;
    };
    require(
        close(scales[0].x, 30.0F) && close(scales[0].width, 0.8F)
            && close(scales[1].x, 30.0F) && close(scales[1].width, 1.25F)
            && close(scales[2].x, 80.0F) && close(scales[2].width, 0.625F)
            && close(scales[3].x, 130.0F) && close(scales[3].width, 1.25F),
        "left/center/right transform-origin keywords did not survive inline transform cascade");
}

void test_transform_transition_uses_host_clock_for_translate_and_scale(
    webscene_engine* engine)
{
    execute_and_wait(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              #temporal-indicator {
                position: fixed; left: 10px; top: 20px; width: 100px; height: 4px;
                transform: translateX(0px) scaleX(.5);
                transform-origin: left;
                transition: transform 100ms linear;
              }
              #temporal-indicator.selected {
                transform: translateX(80px) scaleX(1);
              }
            </style>
            <div id="temporal-indicator"></div>`;
        })()
    )JS", "transform-transition-host-clock-setup.js");
    animation_frame_and_wait(engine, 0.0, 7001U);
    const auto initial = evaluate(engine,
        "getComputedStyle(document.getElementById('temporal-indicator')).getPropertyValue('transform')",
        "transform-transition-initial.js");
    require(
        initial == "\"matrix(0.5, 0, 0, 1, 0, 0)\"",
        "transform transition fixture did not begin at its authored transform: " + initial);

    execute_and_wait(engine,
        "document.getElementById('temporal-indicator').classList.add('selected')",
        "transform-transition-activate.js");
    require(
        evaluate(engine,
            "getComputedStyle(document.getElementById('temporal-indicator')).getPropertyValue('transform')",
            "transform-transition-before-frame.js") == "\"matrix(0.5, 0, 0, 1, 0, 0)\"",
        "transform transition jumped before the next host frame");

    animation_frame_and_wait(engine, 50.0, 7002U);
    const auto midpoint = evaluate(engine, R"JS(
        (() => {
          const indicator = document.getElementById('temporal-indicator');
          const matrix = getComputedStyle(indicator).getPropertyValue('transform')
            .slice(7, -1).split(',').map(Number);
          const rect = indicator.getBoundingClientRect();
          return { matrix, rect: [rect.x, rect.width] };
        })()
    )JS", "transform-transition-midpoint.js");
    require(
        midpoint == R"({"matrix":[0.75,0,0,1,40,0],"rect":[50,75]})",
        "translate/scale transition did not publish midpoint geometry: " + midpoint);

    animation_frame_and_wait(engine, 100.0, 7003U);
    const auto completed = evaluate(engine, R"JS(
        (() => {
          const indicator = document.getElementById('temporal-indicator');
          const matrix = getComputedStyle(indicator).getPropertyValue('transform')
            .slice(7, -1).split(',').map(Number);
          const rect = indicator.getBoundingClientRect();
          return { matrix, rect: [rect.x, rect.width] };
        })()
    )JS", "transform-transition-complete.js");
    require(
        completed == R"({"matrix":[1,0,0,1,80,0],"rect":[90,100]})",
        "translate/scale transition did not finish at its target geometry: " + completed);
}

void test_transform_transition_interpolates_from_none(webscene_engine* engine)
{
    execute_and_wait(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              #none-transition {
                width: 20px;
                height: 20px;
                transform: none;
                transition: transform 100ms linear;
              }
              #none-transition.active {
                transform: translateX(20px) rotate(0deg);
              }
            </style>
            <div id="none-transition"></div>`;
          requestAnimationFrame(() =>
            document.getElementById('none-transition').classList.add('active'));
        })()
    )JS", "transform-transition-from-none-setup.js");
    require(
        evaluate(engine,
            "getComputedStyle(document.getElementById('none-transition')).transform",
            "transform-transition-from-none-initial.js")
            == "\"matrix(1, 0, 0, 1, 0, 0)\"",
        "transform transition from none did not begin at the identity matrix");
    animation_frame_and_wait(engine, 200.0, 7011U);
    require(
        evaluate(engine,
            "getComputedStyle(document.getElementById('none-transition')).transform",
            "transform-transition-from-none-before-frame.js")
            == "\"matrix(1, 0, 0, 1, 0, 0)\"",
        "transform transition from none jumped before the next host frame");

    animation_frame_and_wait(engine, 250.0, 7012U);
    const auto midpoint = evaluate(engine,
        "getComputedStyle(document.getElementById('none-transition')).transform",
        "transform-transition-from-none-midpoint.js");
    require(
        midpoint == "\"matrix(1, 0, 0, 1, 10, 0)\"",
        "transform transition from none did not publish midpoint geometry: " + midpoint);
    animation_frame_and_wait(engine, 300.0, 7013U);
    const auto progressed = evaluate(engine,
        "getComputedStyle(document.getElementById('none-transition')).transform",
        "transform-transition-from-none-progressed.js");
    require(
        progressed == "\"matrix(1, 0, 0, 1, 20, 0)\"",
        "transform transition from none did not finish at its target geometry: " + progressed);
    execute_and_wait(engine,
        "document.getElementById('none-transition').classList.remove('active')",
        "transform-transition-back-to-none-activate.js");
    require(
        evaluate(engine,
            "getComputedStyle(document.getElementById('none-transition')).transform",
            "transform-transition-back-to-none-initial.js")
            == "\"matrix(1, 0, 0, 1, 20, 0)\"",
        "transform transition back to none did not retain its initial painted value");
    animation_frame_and_wait(engine, 350.0, 7014U);
    const auto reverse_midpoint = evaluate(engine,
        "getComputedStyle(document.getElementById('none-transition')).transform",
        "transform-transition-back-to-none-midpoint.js");
    require(
        reverse_midpoint == "\"matrix(1, 0, 0, 1, 10, 0)\"",
        "transform transition back to none did not publish midpoint geometry: "
            + reverse_midpoint);
    animation_frame_and_wait(engine, 400.0, 7015U);
    const auto reverse_completed = evaluate(engine,
        "getComputedStyle(document.getElementById('none-transition')).transform",
        "transform-transition-back-to-none-complete.js");
    require(
        reverse_completed == "\"none\""
            || reverse_completed == "\"matrix(1, 0, 0, 1, 0, 0)\"",
        "transform transition back to none did not finish at the identity value: "
            + reverse_completed);
}

void test_class_list_is_same_live_object(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const node = document.createElement('div');
          const other = document.createElement('div');
          document.body.appendChild(node);
          const tokens = node.classList;
          const same = tokens === node.classList;
          const distinct = tokens !== other.classList;
          node.className = 'shown';
          tokens.value = 'widgetbar-wrap unselectable';
          const assignedValue = tokens.value;
          const assignedAttribute = node.getAttribute('class');
          const assignedTokens =
            tokens.contains('widgetbar-wrap') && tokens.contains('unselectable');
          tokens.value = 'shown';
          const originalContains = tokens.contains;
          let calls = 0;
          tokens.contains = function(token) {
            calls++;
            return originalContains.call(this, token);
          };
          const present = node.classList.contains('shown');
          node.remove();
          document.body.appendChild(node);
          node.setAttribute('class', 'after');
          return {
            same,
            distinct,
            present,
            calls,
            assignedValue,
            assignedAttribute,
            assignedTokens,
            reattached: node.classList === tokens,
            live: tokens.contains('after') && !tokens.contains('shown')
          };
        })()
    )JS", "dom-token-list-same-object.js");
    require(
        result == R"({"same":true,"distinct":true,"present":true,"calls":1,"assignedValue":"widgetbar-wrap unselectable","assignedAttribute":"widgetbar-wrap unselectable","assignedTokens":true,"reattached":true,"live":true})",
        "Element.classList did not preserve same-object identity and live method shadowing: "
            + result);
}

void test_opacity_and_color_transitions_use_host_clock_and_dispatch_events(
    webscene_engine* engine)
{
    execute_and_wait(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              #temporal-paint {
                position: fixed; left: 10px; top: 10px; width: 40px; height: 20px;
                opacity: .2; color: rgb(10, 20, 30);
                transition-property: opacity, color;
                transition-duration: 100ms, 200ms;
                transition-delay: 20ms, 0ms;
                transition-timing-function: linear, linear;
              }
              #temporal-paint.selected { opacity: 1; color: rgb(110, 120, 130); }
              #temporal-paint.reversed { opacity: .2; color: rgb(10, 20, 30); }
            </style>
            <div id="temporal-paint">paint</div>`;
          const target = document.getElementById('temporal-paint');
          globalThis.__transitionEvents = [];
          for (const type of ['transitionrun', 'transitionstart', 'transitionend', 'transitioncancel']) {
            target.addEventListener(type, event => {
              globalThis.__transitionEvents.push(`${type}:${event.propertyName}`);
            });
          }
        })()
    )JS", "paint-transition-host-clock-setup.js");
    animation_frame_and_wait(engine, 0.0, 7011U);
    execute_and_wait(engine,
        "document.getElementById('temporal-paint').classList.add('selected')",
        "paint-transition-activate.js");

    const auto metadata = evaluate(engine, R"JS(
        (() => {
          const style = getComputedStyle(document.getElementById('temporal-paint'));
          return [style.transitionProperty, style.transitionDuration,
            style.transitionDelay, style.transitionTimingFunction];
        })()
    )JS", "paint-transition-metadata.js");
    require(
        metadata == R"(["opacity, color","100ms, 200ms","20ms, 0ms","linear, linear"])" ,
        "computed transition metadata was not observable: " + metadata);

    animation_frame_and_wait(engine, 50.0, 7012U);
    const auto midpoint = evaluate(engine, R"JS(
        (() => {
          const style = getComputedStyle(document.getElementById('temporal-paint'));
          return {
            opacity: Math.round(Number(style.opacity) * 1000) / 1000,
            color: style.color,
            events: globalThis.__transitionEvents
          };
        })()
    )JS", "paint-transition-midpoint.js");
    require(
        midpoint.find(R"("opacity":0.44)") != std::string::npos
            && midpoint.find("\"color\":\"rgb(35, 45, 55)\"") != std::string::npos
            && midpoint.find("transitionrun:opacity") != std::string::npos
            && midpoint.find("transitionrun:color") != std::string::npos
            && midpoint.find("transitionstart:opacity") != std::string::npos
            && midpoint.find("transitionstart:color") != std::string::npos,
        "opacity/color midpoint or transition events were not host-clock driven: " + midpoint);

    // Reverse while both properties are running. The old transitions must be
    // cancelled and the new transitions must start from the painted midpoint.
    execute_and_wait(engine,
        "(() => { const target = document.getElementById('temporal-paint');"
        " target.style.setProperty('opacity', '.2');"
        " target.style.setProperty('color', 'rgb(10, 20, 30)'); })()",
        "paint-transition-reverse.js");
    require(
        evaluate(engine,
            "document.getElementById('temporal-paint').style.getPropertyValue('opacity')",
            "paint-transition-reverse-flush.js") == "\".2\"",
        "inline reversal mutation did not reach the DOM before the next host frame");
    animation_frame_and_wait(engine, 100.0, 7013U);
    const auto reversed = evaluate(engine, R"JS(
        (() => {
          const style = getComputedStyle(document.getElementById('temporal-paint'));
          return { opacity: Math.round(Number(style.opacity) * 1000) / 1000, color: style.color,
            events: globalThis.__transitionEvents };
        })()
    )JS", "paint-transition-reversed-midpoint.js");
    require(
        reversed.find("transitioncancel:opacity") != std::string::npos
            && reversed.find("transitioncancel:color") != std::string::npos
            && reversed.find(R"("opacity":0.368)") != std::string::npos
            && reversed.find("\"color\":\"rgb(29, 39, 49)\"") != std::string::npos,
        "transition cancellation/reversal did not retain the painted midpoint: " + reversed);

    animation_frame_and_wait(engine, 300.0, 7014U);
    const auto completed = evaluate(engine, R"JS(
        (() => {
          const style = getComputedStyle(document.getElementById('temporal-paint'));
          return { opacity: Math.round(Number(style.opacity) * 1000) / 1000, color: style.color,
            events: globalThis.__transitionEvents };
        })()
    )JS", "paint-transition-complete.js");
    require(
        completed.find(R"("opacity":0.2)") != std::string::npos
            && completed.find("\"color\":\"rgb(10, 20, 30)\"") != std::string::npos
            && completed.find("transitionend:opacity") != std::string::npos
            && completed.find("transitionend:color") != std::string::npos,
        "opacity/color transitions did not publish their final state: " + completed);
}

void test_opacity_keyframes_use_host_clock_with_staggered_infinite_delays(
    webscene_engine* engine)
{
    execute_and_wait(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              @keyframes dot-pulse {
                0%, to { opacity: 1; }
                60% { opacity: .4; }
                30% { opacity: .2; }
              }
              .dot { animation: dot-pulse 1000ms linear infinite; }
              #dot-a { animation-delay: 100ms; }
              #dot-b { animation-delay: 300ms; }
              #dot-c { animation-delay: 450ms; }
            </style>
            <div id="dot-host">
              <span id="dot-a" class="dot"></span>
              <span id="dot-b" class="dot"></span>
              <span id="dot-c" class="dot"></span>
            </div>`;
        })()
    )JS", "opacity-keyframes-host-clock-setup.js");

    const auto metadata = evaluate(engine, R"JS(
        (() => {
          const style = getComputedStyle(document.getElementById('dot-a'));
          return [style.animationName, style.animationDuration, style.animationDelay,
            style.animationTimingFunction, style.animationIterationCount];
        })()
    )JS", "opacity-keyframes-metadata.js");
    require(
        metadata == R"(["dot-pulse","1000ms","100ms","linear","infinite"])",
        "computed CSS animation metadata was not observable: " + metadata);
    wait_for_animation_frame_demand(
        engine,
        true,
        "visible infinite CSS animation did not request a host frame");

    animation_frame_and_wait(engine, 550.0, 7021U);
    const auto first = evaluate(engine, R"JS(
        (() => [...document.querySelectorAll('.dot')].map(node =>
          Math.round(Number(getComputedStyle(node).opacity) * 1000) / 1000))()
    )JS", "opacity-keyframes-first-frame.js");
    require(first == "[0.6,1,1]",
        "staggered animation delays did not retain pre-start opacity: " + first);

    animation_frame_and_wait(engine, 900.0, 7022U);
    const auto second = evaluate(engine, R"JS(
        (() => [...document.querySelectorAll('.dot')].map(node =>
          Math.round(Number(getComputedStyle(node).opacity) * 1000) / 1000))()
    )JS", "opacity-keyframes-second-frame.js");
    require(second == "[0.333,0.2,0.6]",
        "keyframe stops were not interpolated on the host clock: " + second);

    animation_frame_and_wait(engine, 1450.0, 7023U);
    const auto wrapped = evaluate(engine, R"JS(
        (() => [...document.querySelectorAll('.dot')].map(node =>
          Math.round(Number(getComputedStyle(node).opacity) * 1000) / 1000))()
    )JS", "opacity-keyframes-wrapped-frame.js");
    require(wrapped == "[0.867,0.775,0.55]",
        "infinite opacity animation did not wrap continuously: " + wrapped);

    execute_and_wait(
        engine,
        "document.getElementById('dot-host').style.display = 'none'",
        "opacity-keyframes-hide.js");
    wait_for_animation_frame_demand(
        engine,
        false,
        "display:none CSS animations retained host-frame demand");

    execute_and_wait(
        engine,
        "document.getElementById('dot-host').style.display = 'block'",
        "opacity-keyframes-show.js");
    wait_for_animation_frame_demand(
        engine,
        true,
        "restored CSS animations did not resume host-frame demand");

    execute_and_wait(
        engine,
        "document.body.innerHTML = '<div>static replacement</div>'",
        "opacity-keyframes-detach.js");
    wait_for_animation_frame_demand(
        engine,
        false,
        "detached CSS animations retained host-frame demand");
}

void test_rotation_keyframes_use_host_clock_and_wrap_continuously(
    webscene_engine* engine)
{
    execute_and_wait(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              @keyframes spinner-rotation { to { transform: rotate(1turn); } }
              #spinner {
                box-sizing: border-box;
                width: 56px; height: 56px;
                border: 4px solid rgb(0, 188, 174);
                border-right-color: rgb(128, 128, 128);
                border-bottom-color: rgb(128, 128, 128);
                border-radius: 50%;
                animation: spinner-rotation 900ms linear infinite;
              }
            </style>
            <div id="spinner"></div>`;
        })()
    )JS", "rotation-keyframes-host-clock-setup.js");

    // Publish the insertion frame so the animation timeline starts at the
    // current host timestamp rather than at the first sampled intermediate frame.
    animation_frame_and_wait(engine, 1450.0, 7030U);
    const auto metadata = evaluate(engine, R"JS(
        (() => {
          const style = getComputedStyle(document.getElementById('spinner'));
          return [style.animationName, style.animationDuration,
            style.animationTimingFunction, style.animationIterationCount];
        })()
    )JS", "rotation-keyframes-metadata.js");
    require(
        metadata == R"(["spinner-rotation","900ms","linear","infinite"])" ,
        "computed rotate() animation metadata was not observable: " + metadata);

    webscene_engine_request_scene_checkpoint(engine);
    auto initial_scene_published = false;
    for (auto attempt = 0; attempt < 100 && !initial_scene_published; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            initial_scene_published =
                scene->header.consumed_input_sequence >= 7030U;
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (!initial_scene_published) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(initial_scene_published,
        "spinner insertion frame did not publish before temporal sampling");
    webscene_engine_metrics layout_before_animation{};
    webscene_engine_get_metrics(engine, &layout_before_animation);

    const auto angle_expression = R"JS(
        (() => {
          const value = getComputedStyle(document.getElementById('spinner'))
            .getPropertyValue('transform');
          const matrix = value.slice(7, -1).split(',').map(Number);
          let angle = Math.atan2(matrix[1], matrix[0]) * 180 / Math.PI;
          if (angle < 0) angle += 360;
          return Math.round(angle * 1000) / 1000;
        })()
    )JS";

    animation_frame_and_wait(engine, 1675.0, 7031U);
    const auto quarter_turn = evaluate(
        engine, angle_expression, "rotation-keyframes-quarter-turn.js");
    require(
        std::abs(std::strtof(quarter_turn.c_str(), nullptr) - 90.0F) < 0.1F,
        "rotate() keyframes did not publish a quarter turn: " + quarter_turn);

    webscene_engine_request_scene_checkpoint(engine);
    auto painted_rotation = 0.0F;
    auto rounded_border_count = 0U;
    auto rounded_side_union = 0U;
    auto partitioned_border_count = 0U;
    constexpr uint32_t border_side_mask = 0xF0000000U;
    constexpr uint32_t border_color_partition = 1U << 27U;
    for (auto attempt = 0; attempt < 100 && rounded_border_count != 2U; ++attempt) {
        rounded_border_count = 0U;
        rounded_side_union = 0U;
        partitioned_border_count = 0U;
        painted_rotation = 0.0F;
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            if (scene->header.consumed_input_sequence >= 7031U) {
                for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                    const auto& command = scene->commands[index];
                    if (command.kind == 19U) painted_rotation = command.stroke_width;
                    if (command.kind == 8U || command.kind == 11U) {
                        ++rounded_border_count;
                        if ((command.flags & border_color_partition) != 0U) {
                            ++partitioned_border_count;
                        }
                        rounded_side_union |= command.flags & border_side_mask;
                        require(
                            command.radius_top_left > 20.0F
                                && command.radius_top_right > 20.0F
                                && command.radius_bottom_right > 20.0F
                                && command.radius_bottom_left > 20.0F,
                            "spinner border-radius did not reach native paint commands");
                    }
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (rounded_border_count != 2U) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(
        std::abs(painted_rotation - 90.0F) < 0.1F,
        "computed spinner rotation did not reach the native scene");
    require(
        rounded_border_count == 2U
            && partitioned_border_count == 2U
            && rounded_side_union == border_side_mask,
        "multi-colour rounded spinner border was flattened into square side rectangles");
    webscene_engine_metrics layout_after_animation{};
    webscene_engine_get_metrics(engine, &layout_after_animation);
    require(
        layout_after_animation.layout_passes
            == layout_before_animation.layout_passes,
        "paint-only rotation repeated full document layout");

    animation_frame_and_wait(engine, 1900.0, 7032U);
    const auto half_turn = evaluate(
        engine, angle_expression, "rotation-keyframes-half-turn.js");
    require(
        std::abs(std::strtof(half_turn.c_str(), nullptr) - 180.0F) < 0.1F,
        "rotate() keyframes did not publish a half turn: " + half_turn);

    animation_frame_and_wait(engine, 2575.0, 7033U);
    const auto wrapped_quarter_turn = evaluate(
        engine, angle_expression, "rotation-keyframes-wrapped-quarter-turn.js");
    require(
        std::abs(std::strtof(wrapped_quarter_turn.c_str(), nullptr) - 90.0F) < 0.1F,
        "infinite rotate() keyframes did not wrap continuously: "
            + wrapped_quarter_turn);
}

void test_cssom_serializes_resolved_numbers_without_trailing_zeroes(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              .canonical-number { font-size: 18px; line-height: 2em !important; }
            </style>
            <p id="canonical-number" class="canonical-number" style="line-height: 1em"></p>
            <div id="normal-line" style="font-size: 16px; line-height: normal"></div>
            <div id="percent-line" style="font-size: 16px; line-height: 10%"></div>`;
          document.getElementById('normal-line').style.left = '10px';
        })()
    )JS", "native-cssom-canonical-number-setup.js");
    const auto value = evaluate(engine,
        "getComputedStyle(document.getElementById('canonical-number')).lineHeight",
        "native-cssom-canonical-number-read.js");
    require(value == "\"36px\"",
        "resolved CSSOM numbers retained implementation trailing zeroes: " + value);
    const auto reflection = evaluate(engine, R"JS(
        (() => {
          const normal = document.getElementById('normal-line');
          const percent = document.getElementById('percent-line');
          return [
            normal.style.lineHeight,
            normal.style.left,
            getComputedStyle(normal).lineHeight,
            percent.style.lineHeight,
            getComputedStyle(percent).lineHeight
          ];
        })()
    )JS", "native-cssom-specified-and-resolved-read.js");
    require(reflection == R"(["normal","10px","normal","10%","1.6px"])",
        "specified and resolved CSSOM values were conflated: " + reflection);
}

void test_cssom_serializes_inline_hex_colors(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const target = document.createElement('div');
          target.style.color = '#FFFFFF';
          target.style.backgroundColor = '#12345680';
          target.style.setProperty('--theme-color', '#FFFFFF');
          const color = target.style.color;
          const parserCompatible =
            (/^rgb\s*\(\s*(\d+),\s*(\d+),\s*(\d+)\s*\)$/).test(color) ||
            (/^rgba\s*\(\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+(?:\.\d+)?)\s*\)$/).test(color);
          const result = {
            color,
            colorByName: target.style.getPropertyValue('color'),
            background: target.style.backgroundColor,
            parserCompatible,
            cssText: target.style.cssText,
            attribute: target.getAttribute('style'),
            custom: target.style.getPropertyValue('--theme-color')
          };
          target.style.color = 'white';
          result.keyword = target.style.color;
          return result;
        })()
    )JS", "native-cssom-inline-hex-color.js");
    require(
        result == R"JSON({"color":"rgb(255, 255, 255)","colorByName":"rgb(255, 255, 255)","background":"rgba(18, 52, 86, 0.502)","parserCompatible":true,"cssText":"--theme-color: #FFFFFF; background-color: rgba(18, 52, 86, 0.502); color: rgb(255, 255, 255);","attribute":"--theme-color: #FFFFFF; background-color: rgba(18, 52, 86, 0.502); color: rgb(255, 255, 255);","custom":"#FFFFFF","keyword":"white"})JSON",
        "inline hex colors were not serialized using browser CSSOM form: " + result);
}

void test_cssom_padding_assignment_updates_longhands_and_geometry(webscene_engine* engine)
{
    const auto assigned = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML =
            '<div id="padding-probe" style="width:100px;height:20px"></div>';
          const probe = document.getElementById('padding-probe');
          probe.style.padding = '3px';
          probe.style.setProperty('padding-left', '7px');
          const style = getComputedStyle(probe);
          const rect = probe.getBoundingClientRect();
          return [
            probe.style.getPropertyValue('padding'),
            style.paddingTop,
            style.paddingRight,
            style.paddingBottom,
            style.paddingLeft,
            style.width,
            style.height,
            rect.width,
            rect.height
          ];
        })()
    )JS", "native-cssom-padding-assignment.js");
    require(assigned == R"JSON(["3px","3px","3px","3px","7px","100px","20px",110,26])JSON",
        "padding CSSOM assignment did not update declarations or geometry: " + assigned);

    const auto removed = evaluate(engine, R"JS(
        (() => {
          const probe = document.getElementById('padding-probe');
          probe.style.cssText = 'width:100px;height:20px;padding:3px';
          probe.style.padding = '';
          const style = getComputedStyle(probe);
          const rect = probe.getBoundingClientRect();
          return [
            style.paddingTop,
            style.paddingRight,
            style.paddingBottom,
            style.paddingLeft,
            rect.width,
            rect.height
          ];
        })()
    )JS", "native-cssom-padding-removal.js");
    require(removed == R"JSON(["0px","0px","0px","0px",100,20])JSON",
        "padding CSSOM removal left stale declarations or geometry: " + removed);
}

void test_cssom_border_assignment_updates_longhands_and_geometry(webscene_engine* engine)
{
    const auto value = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML =
            '<div id="border-probe" style="width:100px;height:20px"></div>';
          const probe = document.getElementById('border-probe');
          probe.style.border = '3px';
          probe.style.borderStyle = 'solid';
          const first = getComputedStyle(probe);
          const firstRect = probe.getBoundingClientRect();
          const result = [
            probe.style.getPropertyValue('border'),
            first.borderTopWidth,
            first.borderRightWidth,
            first.borderBottomWidth,
            first.borderLeftWidth,
            first.width,
            first.height,
            firstRect.width,
            firstRect.height
          ];
          probe.style.borderWidth = '1px 2px 3px 4px';
          const expanded = getComputedStyle(probe);
          result.push(
            expanded.borderTopWidth,
            expanded.borderRightWidth,
            expanded.borderBottomWidth,
            expanded.borderLeftWidth
          );
          probe.style.borderTop = '5px solid red';
          const overridden = getComputedStyle(probe);
          const finalRect = probe.getBoundingClientRect();
          result.push(
            overridden.borderTopWidth,
            overridden.borderTopColor,
            finalRect.width,
            finalRect.height
          );
          return result;
        })()
    )JS", "native-cssom-border-assignment.js");
    require(
        value == R"JSON(["3px","3px","3px","3px","3px","100px","20px",106,26,"1px","2px","3px","4px","5px","rgb(255, 0, 0)",106,28])JSON",
        "border CSSOM assignment did not update longhands or geometry: " + value);
}

void test_logical_inline_borders_reach_geometry(webscene_engine* engine)
{
    const auto value = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              .widgetbar-pages {
                width:100px;
                height:20px;
                border-inline-end:1px solid;
                border-inline-start:1px solid;
                border-color:rgb(235,235,235);
              }
            </style>
            <div class="widgetbar-pages"></div>`;
          const pages = document.querySelector('.widgetbar-pages');
          const style = getComputedStyle(pages);
          const rect = pages.getBoundingClientRect();
          return [
            style.borderLeftWidth,
            style.borderRightWidth,
            style.borderLeftColor,
            style.borderRightColor,
            rect.width
          ];
        })()
    )JS", "native-logical-inline-borders.js");
    require(
        value == R"JSON(["1px","1px","rgb(235, 235, 235)","rgb(235, 235, 235)",102])JSON",
        "logical inline borders did not reach physical geometry: " + value);
}

void test_hidden_subtree_retains_computed_height_without_boxes(webscene_engine* engine)
{
    const auto value = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML =
            '<div id="parent" style="display:none">'
            + '<input id="input" type="text" style="height:20px">'
            + '<textarea id="textarea" style="height:20px"></textarea>'
            + '<div id="block" style="height:20px"></div></div>';
          const parent = document.getElementById('parent');
          const input = document.getElementById('input');
          const textarea = document.getElementById('textarea');
          const block = document.getElementById('block');
          const hidden = [
            getComputedStyle(input).height,
            getComputedStyle(textarea).height,
            getComputedStyle(block).height,
            input.offsetHeight,
            textarea.offsetHeight,
            block.offsetHeight,
            input.getClientRects().length,
            textarea.getClientRects().length,
            block.getClientRects().length
          ];
          parent.style.display = 'block';
          hidden.push(
            getComputedStyle(input).height,
            getComputedStyle(textarea).height,
            getComputedStyle(block).height,
            input.getBoundingClientRect().height > 0,
            textarea.getBoundingClientRect().height > 0,
            block.getBoundingClientRect().height > 0
          );
          return hidden;
        })()
    )JS", "native-hidden-subtree-computed-height.js");
    require(
        value == R"JSON(["20px","20px","20px",0,0,0,0,0,0,"20px","20px","20px",true,true,true])JSON",
        "hidden subtree conflated computed dimensions with suppressed geometry: " + value);
}

void test_cssom_z_index_survives_connection_and_recascade(webscene_engine* engine)
{
    const auto value = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '<style>#target { z-index: 7; }</style>';
          const target = document.createElement('div');
          target.id = 'target';
          target.style.position = 'absolute';
          target.style.zIndex = 1000;
          document.body.appendChild(target);
          const result = [
            target.style.zIndex,
            target.style.getPropertyValue('z-index'),
            getComputedStyle(target).zIndex
          ];
          target.style.setProperty('z-index', '-2');
          result.push(target.style.zIndex, getComputedStyle(target).zIndex);
          target.style.removeProperty('z-index');
          result.push(target.style.zIndex, getComputedStyle(target).zIndex);
          return result;
        })()
    )JS", "native-cssom-z-index-connection.js");
    require(
        value == R"JSON(["1000","1000","1000","-2","-2","","7"])JSON",
        "z-index CSSOM assignment was lost during connection or recascade: " + value);
}

void test_important_custom_property_cascade_reaches_paint(webscene_engine* engine)
{
    const auto value = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              :root { --probe-color: green !important; }
              :root { --probe-color: red; }
              .probe { width: 10px; height: 10px; background-color: var(--probe-color); }
            </style>
            <div class="probe"></div>`;
          const probe = document.querySelector('.probe');
          return [
            getComputedStyle(document.documentElement).getPropertyValue('--probe-color'),
            getComputedStyle(probe).getPropertyValue('--probe-color'),
            getComputedStyle(probe).backgroundColor
          ];
        })()
    )JS", "native-important-custom-property-cascade.js");
    require(value == R"JSON(["green","green","rgb(0, 128, 0)"])JSON",
        "a later normal custom property overrode an earlier important value: " + value);
}

void test_detached_style_retains_text_and_activates_when_connected(webscene_engine* engine)
{
    const auto value = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '<div id="detached-style-host"></div>';
          const detached = document.createElement('div');
          detached.innerHTML =
            '<style>.detached-style-probe { --detached-token: active; }</style>';
          const style = detached.firstChild;
          document.getElementById('detached-style-host').appendChild(style);
          const probe = document.createElement('div');
          probe.className = 'detached-style-probe';
          document.body.appendChild(probe);
          return [
            style.textContent.includes('--detached-token'),
            style.isConnected,
            getComputedStyle(probe).getPropertyValue('--detached-token')
          ];
        })()
    )JS", "native-detached-style-activation.js");
    require(value == R"JSON([true,true,"active"])JSON",
        "detached STYLE lost text or failed to activate after connection: " + value);
}

void test_native_overflow_scrolling_and_nowrap(webscene_engine* engine)
{
    resize(engine, 400, 200, 40U);
    const auto initial = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            .scroller { width:160px; height:64px; overflow-x:hidden; }
            .scroller.no-scrollbar::-webkit-scrollbar {
              display:none; width:0; height:0;
            }
            .hidden-scroller {
              position:absolute; left:180px; top:0; background:rgb(1, 2, 3);
            }
            .row { height:32px; }
            .title { display:block; width:80px; overflow:hidden; white-space:nowrap;
              font-size:14px; line-height:18px; }
          `;
          document.body.appendChild(style);
          const scroller = document.createElement('div');
          scroller.className = 'scroller';
          for (let index = 0; index < 5; index++) {
            const row = document.createElement('div');
            row.className = 'row';
            const title = document.createElement('span');
            title.className = 'title';
            title.appendChild(document.createTextNode(index === 0
              ? 'Arnaud Legoux Moving Average'
              : `Indicator ${index}`));
            row.appendChild(title);
            scroller.appendChild(row);
          }
          document.body.appendChild(scroller);
          globalThis.__overflowScroller = scroller;
          globalThis.__overflowScrollEvents = 0;
          scroller.addEventListener('scroll', () => __overflowScrollEvents++);
          return {
            overflowX: getComputedStyle(scroller).getPropertyValue('overflow-x'),
            overflowY: getComputedStyle(scroller).getPropertyValue('overflow-y'),
            clientHeight: scroller.clientHeight,
            scrollHeight: scroller.scrollHeight,
            scrollTop: scroller.scrollTop,
            whiteSpace: getComputedStyle(scroller.querySelector('.title')).whiteSpace,
            titleTextHeight: scroller.querySelector('.title').firstChild.getBoundingClientRect().height
          };
        })()
    )JS", "native-overflow-scroll-setup.js");
    if (initial != R"({"overflowX":"hidden","overflowY":"auto","clientHeight":64,"scrollHeight":160,"scrollTop":0,"whiteSpace":"nowrap","titleTextHeight":18})") {
        fail("overflow range or nowrap layout was not established: " + initial);
    }

    const auto consumed_before_wheel = consumed_input_count(engine);
    wheel_input(engine, 20, 20, 60, 41U);
    wait_for_consumed_inputs(
        engine,
        consumed_before_wheel + 1,
        "overflow wheel input was not consumed");
    const auto scrolled = evaluate(engine, R"JS(
        (() => ({
          scrollTop: __overflowScroller.scrollTop,
          firstTop: __overflowScroller.firstElementChild.getBoundingClientRect().top,
          lastTop: __overflowScroller.children[4].getBoundingClientRect().top
        }))()
    )JS", "native-overflow-scroll-result.js");
    if (scrolled != R"({"scrollTop":60,"firstTop":-60,"lastTop":68})") {
        fail("wheel input did not scroll the nearest overflow viewport: " + scrolled);
    }

    const auto revealed = evaluate(engine, R"JS(
        (() => {
          const last = __overflowScroller.children[4];
          last.scrollIntoView({ block: 'nearest' });
          return {
            method: typeof last.scrollIntoView,
            scrollTop: __overflowScroller.scrollTop,
            firstTop: __overflowScroller.firstElementChild.getBoundingClientRect().top,
            lastTop: last.getBoundingClientRect().top
          };
        })()
    )JS", "native-scroll-into-view-result.js");
    if (revealed != R"({"method":"function","scrollTop":96,"firstTop":-96,"lastTop":32})") {
        fail("scrollIntoView did not reveal the nearest overflow item: " + revealed);
    }

    webscene_engine_request_scene_checkpoint(engine);
    auto observed_scrollbar_rail = false;
    auto observed_scrollbar_thumb = false;
    for (auto attempt = 0; attempt < 100
        && (!observed_scrollbar_rail || !observed_scrollbar_thumb); ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind != 10U) continue;
                if (command.rgba == 0x7F7F7F40U) {
                    observed_scrollbar_rail = std::abs(command.x - 152.0F) < 0.1F
                        && std::abs(command.y - 2.0F) < 0.1F
                        && std::abs(command.width - 6.0F) < 0.1F
                        && std::abs(command.height - 60.0F) < 0.1F;
                } else if (command.rgba == 0xA0A0A0D0U) {
                    observed_scrollbar_thumb = std::abs(command.x - 152.0F) < 0.1F
                        && std::abs(command.y - 38.0F) < 0.1F
                        && std::abs(command.width - 6.0F) < 0.1F
                        && std::abs(command.height - 24.0F) < 0.1F;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (!observed_scrollbar_rail || !observed_scrollbar_thumb) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(
        observed_scrollbar_rail && observed_scrollbar_thumb,
        "native overflow viewport did not paint a proportional scrollbar at its bounded maximum");

    const auto hidden_scrollbar_state = evaluate(engine, R"JS(
        (() => {
          const hiddenScroller = document.createElement('div');
          hiddenScroller.className = 'scroller no-scrollbar hidden-scroller';
          for (let index = 0; index < 5; index++) {
            const row = document.createElement('div');
            row.className = 'row';
            row.textContent = `Hidden ${index}`;
            hiddenScroller.appendChild(row);
          }
          document.body.appendChild(hiddenScroller);
          return {
            clientHeight:hiddenScroller.clientHeight,
            scrollHeight:hiddenScroller.scrollHeight
          };
        })()
    )JS", "native-hidden-scrollbar-state.js");
    require(
        hidden_scrollbar_state == R"({"clientHeight":64,"scrollHeight":160})",
        "author-hidden scrollbar changed overflow geometry: " + hidden_scrollbar_state);

    webscene_engine_request_scene_checkpoint(engine);
    auto hidden_scrollbar_scene_observed = false;
    auto hidden_scrollbar_painted = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            auto hidden_scroller_node_id = 0U;
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.rgba == 0x010203FFU) {
                    hidden_scrollbar_scene_observed = true;
                    hidden_scroller_node_id = command.node_id;
                }
            }
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind == 10U
                    && (command.rgba == 0x7F7F7F40U
                        || command.rgba == 0xA0A0A0D0U)
                    && command.node_id == hidden_scroller_node_id) {
                    hidden_scrollbar_painted = true;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
            if (hidden_scrollbar_scene_observed) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        hidden_scrollbar_scene_observed,
        "author-hidden scrollbar scene was not published");
    require(
        !hidden_scrollbar_painted,
        "::-webkit-scrollbar { display:none } still painted the host overlay scrollbar");

    const auto boundary_events = evaluate(
        engine,
        "globalThis.__overflowScrollEvents",
        "native-overflow-boundary-events-before.js");
    const auto consumed_before_boundary_wheel = consumed_input_count(engine);
    wheel_input(engine, 20, 20, 1000000, 42U);
    wait_for_consumed_inputs(
        engine,
        consumed_before_boundary_wheel + 1,
        "saturated overflow wheel input was not consumed");
    const auto bounded = evaluate(engine, R"JS(
        ({ scrollTop: __overflowScroller.scrollTop,
           scrollEvents: __overflowScrollEvents })
    )JS", "native-overflow-boundary-result.js");
    const auto expected_bounded = std::string("{\"scrollTop\":96,\"scrollEvents\":")
        + boundary_events + "}";
    require(
        bounded == expected_bounded,
        "repeated wheel input escaped the finite range or dispatched at an unchanged boundary: "
            + bounded);
}

void test_row_flex_vertical_scroll_extent_remains_bounded(webscene_engine* engine)
{
    resize(engine, 400, 200, 43U);
    const auto initial = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const scroller = document.createElement('div');
          scroller.style.cssText =
            'display:flex;flex-direction:row;width:160px;height:64px;overflow:auto';
          const content = document.createElement('div');
          content.style.cssText = 'flex:none;width:80px;height:160px';
          scroller.appendChild(content);
          document.body.appendChild(scroller);
          globalThis.__rowFlexScroller = scroller;
          return {
            clientHeight: scroller.clientHeight,
            scrollHeight: scroller.scrollHeight,
            scrollTop: scroller.scrollTop
          };
        })()
    )JS", "native-row-flex-overflow-setup.js");
    require(
        initial == R"({"clientHeight":64,"scrollHeight":160,"scrollTop":0})",
        "row-flex vertical overflow range was not established: " + initial);

    const auto consumed_before_first_wheel = consumed_input_count(engine);
    wheel_input(engine, 20, 20, 1000000, 43U);
    wait_for_consumed_inputs(
        engine,
        consumed_before_first_wheel + 1,
        "row-flex boundary wheel input was not consumed");
    const auto first_boundary = evaluate(engine, R"JS(
        ({
          scrollHeight: __rowFlexScroller.scrollHeight,
          scrollTop: __rowFlexScroller.scrollTop
        })
    )JS", "native-row-flex-overflow-first-boundary.js");
    require(
        first_boundary == R"({"scrollHeight":160,"scrollTop":96})",
        "row-flex scroll extent grew after reaching its boundary: " + first_boundary);

    const auto consumed_before_second_wheel = consumed_input_count(engine);
    wheel_input(engine, 20, 20, 1000000, 44U);
    wait_for_consumed_inputs(
        engine,
        consumed_before_second_wheel + 1,
        "repeated row-flex boundary wheel input was not consumed");
    const auto repeated_boundary = evaluate(engine, R"JS(
        ({
          scrollHeight: __rowFlexScroller.scrollHeight,
          scrollTop: __rowFlexScroller.scrollTop
        })
    )JS", "native-row-flex-overflow-repeated-boundary.js");
    require(
        repeated_boundary == R"({"scrollHeight":160,"scrollTop":96})",
        "repeated row-flex scrolling escaped the finite range: " + repeated_boundary);
}

void test_toolbar_scroll_chevrons_use_single_rotation(webscene_engine* engine)
{
    resize(engine, 320, 200, 44U);
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              body { margin:0; }
              .scrollTop {
                position:absolute; left:20px; top:20px; width:40px; height:24px;
                display:flex; align-items:center; justify-content:center;
                background:rgb(17, 34, 51);
              }
              .scrollTop .iconWrap { transform:rotate(180deg); }
              svg { width:10px; height:6px; }
            </style>
            <div class="scrollTop">
              <div class="iconWrap">
                <svg viewBox="0 0 10 6">
                  <path d="M1 1 L5 5 L9 1" fill="none" stroke="currentColor"/>
                </svg>
              </div>
            </div>`;
          const icon = document.querySelector('.iconWrap');
          return {
            transform:getComputedStyle(icon).transform,
            svgWidth:icon.firstElementChild.getBoundingClientRect().width,
            svgHeight:icon.firstElementChild.getBoundingClientRect().height
          };
        })()
    )JS", "native-toolbar-scroll-chevron.js");
    require(
        result == R"JSON({"transform":"matrix(-1, 0, 0, -1, 0, 0)","svgWidth":10,"svgHeight":6})JSON",
        "toolbar top-chevron layout or rotation was not established: " + result);

    webscene_engine_request_scene_checkpoint(engine);
    auto current_scene_observed = false;
    auto wrapper_rotation = false;
    auto svg_rotation = 0.0F;
    auto svg_observed = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            current_scene_observed = false;
            wrapper_rotation = false;
            svg_observed = false;
            svg_rotation = 0;
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.rgba == 0x112233FFU) current_scene_observed = true;
                if (command.kind == 19U
                    && std::abs(command.stroke_width - 180.0F) < 0.1F) {
                    wrapper_rotation = true;
                }
                if (command.kind == 6U) {
                    svg_observed = true;
                    svg_rotation = command.stroke_width;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
            if (current_scene_observed) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(current_scene_observed, "toolbar top-chevron scene was not published");
    require(wrapper_rotation, "toolbar top-chevron wrapper did not emit rotate(180deg)");
    require(svg_observed, "toolbar top-chevron SVG was absent from the native scene");
    require(
        std::abs(svg_rotation) < 0.1F,
        "toolbar top-chevron inherited its wrapper rotation twice");
}

void test_root_document_overflow_scrolls_and_paints_overlay(webscene_engine* engine)
{
    resize(engine, 320, 200, 43U);
    const auto initial = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '<main id="root-content"></main>';
          document.body.style.margin = '0';
          document.body.style.overflow = 'visible';
          const content = document.getElementById('root-content');
          content.style.width = '320px';
          content.style.height = '600px';
          globalThis.__rootScrollEvents = 0;
          document.body.addEventListener('scroll', () => __rootScrollEvents++);
          return {
            clientHeight: document.body.clientHeight,
            scrollHeight: document.body.scrollHeight,
            scrollTop: document.body.scrollTop
          };
        })()
    )JS", "native-root-overflow-setup.js");
    require(
        initial == R"({"clientHeight":200,"scrollHeight":600,"scrollTop":0})",
        "root document did not expose a finite viewport and content extent: " + initial);

    const auto consumed_before_wheel = consumed_input_count(engine);
    wheel_input(engine, 100, 100, 150, 44U);
    wait_for_consumed_inputs(
        engine,
        consumed_before_wheel + 1,
        "root overflow wheel input was not consumed");
    const auto scrolled = evaluate(engine, R"JS(
        (() => ({
          scrollTop: document.body.scrollTop,
          contentTop: document.getElementById('root-content').getBoundingClientRect().top,
          scrollEvents: __rootScrollEvents
        }))()
    )JS", "native-root-overflow-result.js");
    require(
        scrolled == R"({"scrollTop":150,"contentTop":-150,"scrollEvents":1})",
        "root viewport did not scroll by a bounded wheel delta: " + scrolled);

    webscene_engine_request_scene_checkpoint(engine);
    auto observed_root_rail = false;
    auto observed_root_thumb = false;
    for (auto attempt = 0; attempt < 100
        && (!observed_root_rail || !observed_root_thumb); ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind != 10U) continue;
                if (command.rgba == 0x7F7F7F40U) {
                    observed_root_rail = std::abs(command.x - 312.0F) < 0.1F
                        && std::abs(command.y - 2.0F) < 0.1F
                        && std::abs(command.width - 6.0F) < 0.1F
                        && std::abs(command.height - 196.0F) < 0.1F;
                } else if (command.rgba == 0xA0A0A0D0U) {
                    observed_root_thumb = std::abs(command.x - 312.0F) < 0.1F
                        && std::abs(command.y - 51.0F) < 0.6F
                        && std::abs(command.width - 6.0F) < 0.1F
                        && std::abs(command.height - 65.333F) < 0.6F;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (!observed_root_rail || !observed_root_thumb) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(
        observed_root_rail && observed_root_thumb,
        "root document overflow did not paint a proportional moving scrollbar");
}

void test_table_menu_row_cells_stay_horizontal_and_centered(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `
            tr.menu-item { height: 32px; vertical-align: middle; }
            td.icon-cell { width: 28px; }
            .icon { width: 18px; height: 18px; }
            .content { display: flex; align-items: center; height: 20px; }
            .label { font-size: 14px; }
          `;
          document.body.appendChild(style);
          const table = document.createElement('table');
          const body = document.createElement('tbody');
          const row = document.createElement('tr');
          row.className = 'menu-item';
          const iconCell = document.createElement('td');
          iconCell.className = 'icon-cell';
          const icon = document.createElement('div');
          icon.className = 'icon';
          iconCell.appendChild(icon);
          const contentCell = document.createElement('td');
          const content = document.createElement('div');
          content.className = 'content';
          const label = document.createElement('span');
          label.className = 'label';
          label.textContent = 'Auto (fits data to screen)';
          content.appendChild(label);
          contentCell.appendChild(content);
          row.appendChild(iconCell);
          row.appendChild(contentCell);
          body.appendChild(row);
          table.appendChild(body);
          document.body.appendChild(table);
          const rr = row.getBoundingClientRect();
          const ir = iconCell.getBoundingClientRect();
          const cr = contentCell.getBoundingClientRect();
          const lr = label.getBoundingClientRect();
          return [
            rr.height,
            ir.right <= cr.left,
            ir.top >= rr.top && ir.bottom <= rr.bottom,
            cr.top >= rr.top && cr.bottom <= rr.bottom,
            Math.abs((lr.top + lr.height / 2) - (rr.top + rr.height / 2)) < 1
          ];
        })()
    )JS", "native-table-menu-row.js");
    if (result != "[32,true,true,true,true]") {
        fail("semantic table menu row did not lay out horizontally and centrally: " + result);
    }
}

void test_semantic_table_auto_layout_and_intrinsic_cell_content(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.width = '800px';
          const style = document.createElement('style');
          style.textContent = `
            table { width: 800px; white-space: nowrap; }
            th, td { padding: 10px; }
            .content { display: inline-block; width: max-content; }
          `;
          document.body.appendChild(style);
          const table = document.createElement('table');
          const head = document.createElement('thead');
          const groupRow = document.createElement('tr');
          const dateHeading = document.createElement('th');
          dateHeading.rowSpan = 2;
          dateHeading.textContent = 'Date · 1h';
          const priceHeading = document.createElement('th');
          priceHeading.colSpan = 5;
          priceHeading.textContent = 'AAPL · Cboe One';
          const volumeHeading = document.createElement('th');
          volumeHeading.rowSpan = 2;
          volumeHeading.textContent = 'Volume';
          groupRow.appendChild(dateHeading);
          groupRow.appendChild(priceHeading);
          groupRow.appendChild(volumeHeading);
          const labels = document.createElement('tr');
          for (const label of ['Open', 'High', 'Low', 'Close', 'Change']) {
            const cell = document.createElement('th');
            cell.textContent = label;
            labels.appendChild(cell);
          }
          head.appendChild(groupRow);
          head.appendChild(labels);
          const body = document.createElement('tbody');
          const row = document.createElement('tr');
          for (const value of ["Mon 20 Jul '26 19:30", '329.03', '329.75', '326.00', '326.71', '−2.29 (−0.70%)', '597.35 K']) {
            const cell = document.createElement('td');
            const content = document.createElement('span');
            content.className = 'content';
            content.textContent = value;
            cell.appendChild(content);
            row.appendChild(cell);
          }
          body.appendChild(row);
          table.appendChild(head);
          table.appendChild(body);
          document.body.appendChild(table);
          const rect = node => node.getBoundingClientRect();
          const bodyCells = [...row.children].map(rect);
          const labelCells = [...labels.children].map(rect);
          return {
            displays: [table, head, labels, body, row.firstElementChild]
              .map(node => getComputedStyle(node).display),
            reflectedSpans: [dateHeading.rowSpan, priceHeading.colSpan, volumeHeading.rowSpan],
            maxContentWidth: rect(row.firstElementChild.firstElementChild).width,
            rowWidth: rect(row).width,
            dateWidth: bodyCells[0].width,
            ordered: bodyCells.every((cell, index) => index === 0 || bodyCells[index - 1].right <= cell.left + 0.01),
            fillsTable: Math.abs(bodyCells.at(-1).right - rect(table).right) < 0.1,
            subgroupStartsAfterDate: Math.abs(labelCells[0].left - bodyCells[0].right) < 0.1,
            volumeStartsAfterSubgroup: Math.abs(rect(volumeHeading).left - bodyCells[6].left) < 0.1,
            rowSpanDoesNotInflateTrack:
              Math.abs(rect(head).height - rect(groupRow).height - rect(labels).height) < 0.1
              && rect(dateHeading).height > rect(groupRow).height
              && Math.abs(rect(dateHeading).height - rect(head).height) < 0.1
          };
        })()
    )JS", "native-semantic-table-auto-layout.js");
    require(
        result.find("\"displays\":[\"table\",\"table-header-group\",\"table-row\",\"table-row-group\",\"table-cell\"]")
                != std::string::npos
            && result.find(R"("reflectedSpans":[2,5,2])") != std::string::npos
            && result.find(R"("ordered":true)") != std::string::npos
            && result.find(R"("fillsTable":true)") != std::string::npos
            && result.find(R"("subgroupStartsAfterDate":true)") != std::string::npos
            && result.find(R"("volumeStartsAfterSubgroup":true)") != std::string::npos
            && result.find(R"("rowSpanDoesNotInflateTrack":true)") != std::string::npos,
        "semantic auto-layout table did not preserve its shared column grid: " + result);
    const auto max_content_marker = result.find(R"("maxContentWidth":)");
    require(max_content_marker != std::string::npos, "max-content table metric was absent: " + result);
    const auto max_content_width = std::strtod(
        result.c_str() + max_content_marker + std::strlen(R"("maxContentWidth":)"),
        nullptr);
    require(max_content_width > 100, "max-content table text collapsed: " + result);
}

void test_fixed_table_distributes_excess_after_percentage_columns(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `
            table.fixed-distribution { width: 300px; border-collapse: collapse; table-layout: fixed; height: 20px; }
            .fixed-distribution td { padding: 0; }
            .fixed-distribution td:nth-child(1) { width: 20px; }
            .fixed-distribution td:nth-child(2) { width: 10px; }
            .fixed-distribution td:nth-child(3) { width: 10%; }
          `;
          document.body.appendChild(style);
          const table = document.createElement('table');
          table.className = 'fixed-distribution';
          const row = document.createElement('tr');
          for (let index = 0; index < 3; index++) row.appendChild(document.createElement('td'));
          table.appendChild(row);
          document.body.appendChild(table);
          return {
            table: table.offsetWidth,
            row: row.offsetWidth,
            cells: Array.from(row.children, cell => cell.offsetWidth)
          };
        })()
    )JS", "native-fixed-table-width-distribution.js");
    require(
        result == R"({"table":300,"row":300,"cells":[180,90,30]})",
        "fixed table excess-width distribution regressed: " + result);
}

void test_implicit_grid_contains_scrollable_table(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const shell = document.createElement('div');
          shell.style.display = 'flex';
          shell.style.flexDirection = 'column';
          shell.style.width = '800px';
          shell.style.height = '600px';
          const header = document.createElement('div');
          header.style.height = '50px';
          header.style.flexShrink = '0';
          const grid = document.createElement('div');
          grid.style.display = 'grid';
          grid.style.flexGrow = '1';
          grid.style.overflow = 'hidden';
          const center = document.createElement('div');
          center.style.height = '100%';
          center.style.overflow = 'hidden';
          const scroller = document.createElement('div');
          scroller.style.maxHeight = '100%';
          scroller.style.overflow = 'auto';
          const table = document.createElement('table');
          table.style.width = '100%';
          const body = document.createElement('tbody');
          for (let index = 0; index < 40; index++) {
            const row = document.createElement('tr');
            const cell = document.createElement('x-cell');
            cell.style.display = 'table-cell';
            cell.style.height = '30px';
            cell.textContent = `Row ${index}`;
            row.appendChild(cell);
            body.appendChild(row);
          }
          table.appendChild(body);
          scroller.appendChild(table);
          center.appendChild(scroller);
          grid.appendChild(center);
          shell.appendChild(header);
          shell.appendChild(grid);
          document.body.appendChild(shell);
          const rect = node => node.getBoundingClientRect();
          return {
            shell: rect(shell).height,
            header: rect(header).height,
            grid: rect(grid).height,
            center: rect(center).height,
            scroller: rect(scroller).height,
            table: rect(table).height,
            scrollHeight: scroller.scrollHeight
          };
        })()
    )JS", "native-grid-table-overflow.js");
    require(
        result.find(R"("shell":600)") != std::string::npos
            && result.find(R"("header":50)") != std::string::npos
            && result.find(R"("grid":550)") != std::string::npos
            && result.find(R"("center":550)") != std::string::npos
            && result.find(R"("scroller":550)") != std::string::npos
            && result.find(R"("table":1200)") != std::string::npos
            && result.find(R"("scrollHeight":1200)") != std::string::npos,
        "implicit grid did not contain its scrollable table: " + result);
}

void test_auto_height_flex_popup_expands_overflowing_flex_child(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const popup = document.createElement('div');
          popup.style.display = 'flex';
          popup.style.width = '344px';
          const content = document.createElement('div');
          content.style.display = 'flex';
          content.style.flex = '1 1 auto';
          content.style.flexDirection = 'column';
          content.style.overflow = 'hidden';
          const summary = document.createElement('div');
          summary.style.height = '80px';
          const details = document.createElement('div');
          details.style.height = '56px';
          content.appendChild(summary);
          content.appendChild(details);
          popup.appendChild(content);
          document.body.appendChild(popup);
          return [
            popup.getBoundingClientRect().height,
            content.getBoundingClientRect().height,
            details.getBoundingClientRect().bottom <= content.getBoundingClientRect().bottom
          ];
        })()
    )JS", "native-auto-height-flex-popup.js");
    require(
        result == "[136,136,true]",
        "an auto-height flex popup clipped its overflowing flex child: " + result);
}

void test_constrained_column_flex_scroll_item_keeps_footer_inside(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const host = document.createElement('div');
          host.style.display = 'flex';
          host.style.flexDirection = 'column';
          host.style.height = '200px';
          host.style.width = '300px';
          const header = document.createElement('div');
          header.style.flexGrow = '0';
          header.style.flexShrink = '0';
          header.style.height = '60px';
          const content = document.createElement('div');
          content.style.overflow = 'auto';
          const tall = document.createElement('div');
          tall.style.height = '200px';
          content.appendChild(tall);
          const footer = document.createElement('div');
          footer.style.flexGrow = '0';
          footer.style.flexShrink = '0';
          footer.style.height = '60px';
          host.appendChild(header);
          host.appendChild(content);
          host.appendChild(footer);
          document.body.appendChild(host);
          const hr = host.getBoundingClientRect();
          const cr = content.getBoundingClientRect();
          const fr = footer.getBoundingClientRect();
          return [hr.height, cr.height, content.scrollHeight, fr.bottom <= hr.bottom];
        })()
    )JS", "native-constrained-column-flex-scroll.js");
    require(
        result == "[200,80,200,true]",
        "a constrained column flex scroller expanded through its footer: " + result);
}

void test_later_dom_overlay_background_paints_above_retained_canvas(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const chartHost = document.createElement('div');
          const canvas = document.createElement('canvas');
          canvas.width = 80;
          canvas.height = 40;
          canvas.style.width = '80px';
          canvas.style.height = '40px';
          canvas.getContext('2d').fillRect(0, 0, 80, 40);
          chartHost.appendChild(canvas);
          const elevatedChartChild = document.createElement('div');
          elevatedChartChild.style.position = 'relative';
          elevatedChartChild.style.zIndex = '100';
          elevatedChartChild.style.color = '#abcdef';
          elevatedChartChild.textContent = 'retained chart legend';
          chartHost.appendChild(elevatedChartChild);
          document.body.appendChild(chartHost);
          const overlayHost = document.createElement('div');
          const overlay = document.createElement('div');
          overlay.id = 'retained-overlay';
          overlay.style.width = '80px';
          overlay.style.height = '40px';
          overlay.style.backgroundColor = '#2468ac';
          overlayHost.appendChild(overlay);
          const elevatedOverlayChild = document.createElement('div');
          elevatedOverlayChild.style.position = 'relative';
          elevatedOverlayChild.style.zIndex = '8';
          overlayHost.appendChild(elevatedOverlayChild);
          document.body.appendChild(overlayHost);
        })()
    )JS", "native-retained-canvas-overlay.js");
    require(
        evaluate(engine, "document.querySelector('#retained-overlay').offsetWidth", "native-retained-canvas-overlay-ready.js") == "80",
        "retained-canvas overlay fixture did not lay out");

    webscene_engine_request_scene_checkpoint(engine);
    auto foreground_overlay = false;
    auto overlay_covers_earlier_chart_dom = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            auto chart_text_index = scene->header.command_count;
            auto overlay_index = scene->header.command_count;
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind == 3U && (command.rgba >> 8U) == 0xABCDEFU) {
                    chart_text_index = index;
                }
                if (command.kind == 9U && (command.rgba >> 8U) == 0x2468ACU) {
                    foreground_overlay = true;
                    overlay_index = index;
                }
            }
            overlay_covers_earlier_chart_dom = chart_text_index < overlay_index
                && overlay_index < scene->header.command_count;
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (foreground_overlay && overlay_covers_earlier_chart_dom) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(foreground_overlay, "a later DOM overlay background remained behind its retained canvas");
    require(
        overlay_covers_earlier_chart_dom,
        "a descendant z-index reordered an earlier ancestor above a later DOM overlay");
}

void test_canvas_path_even_odd_fill_rule_reaches_scene(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const canvas = document.createElement('canvas');
          canvas.width = 40;
          canvas.height = 40;
          canvas.style.width = '40px';
          canvas.style.height = '40px';
          document.body.appendChild(canvas);
          const context = canvas.getContext('2d');
          context.beginPath();
          context.rect(2, 2, 36, 36);
          context.rect(12, 12, 16, 16);
          context.fillStyle = '#ffffff';
          context.fill('evenodd');
        })()
    )JS", "native-canvas-even-odd-setup.js");
    webscene_engine_request_scene_checkpoint(engine);
    bool observed_fill = false;
    bool observed_even_odd = false;
    std::string observed_commands;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->canvas_command_count; ++index) {
                const auto& command = scene->canvas_commands[index];
                observed_commands += std::to_string(command.kind) + ":"
                    + std::to_string(command.flags) + ",";
                if (command.kind == 21U) {
                    observed_fill = true;
                    observed_even_odd =
                        (command.flags & WEBSCENE_CANVAS_COMMAND_FLAG_EVEN_ODD) != 0U;
                    break;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (observed_even_odd) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (!observed_fill) {
        fail("canvas fill was not emitted to the native scene; commands="
            + observed_commands);
    }
    require(observed_even_odd, "canvas even-odd fill rule was lost before the native scene");
}

void test_canvas_fill_rect_emits_only_relevant_paint_state(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const canvas = document.createElement('canvas');
          canvas.width = 40;
          canvas.height = 40;
          document.body.appendChild(canvas);
          const context = canvas.getContext('2d');
          context.fillStyle = '#204060';
          context.globalAlpha = 0.5;
          context.lineWidth = 7;
          context.strokeStyle = '#80a0c0';
          context.fillRect(1, 2, 30, 20);
          context.strokeRect(3, 4, 12, 8);
        })()
    )JS", "native-canvas-fill-rect-state-slice.js");
    webscene_engine_request_scene_checkpoint(engine);

    auto observed_fill_rect = false;
    auto observed_unrelated_fill_state = false;
    auto observed_stroke_style = false;
    auto observed_line_width = false;
    auto observed_stroke_rect = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->canvas_command_count; ++index) {
                const auto kind = scene->canvas_commands[index].kind;
                if (!observed_fill_rect) {
                    if (kind == 22U) {
                        observed_fill_rect = true;
                    } else if (kind == 41U || kind == 42U
                        || kind == 43U || kind == 44U || kind == 45U
                        || kind == 47U || kind == 48U || kind == 49U
                        || kind == 50U || kind == 51U || kind == 52U) {
                        observed_unrelated_fill_state = true;
                    }
                } else {
                    if (kind == 41U) observed_stroke_style = true;
                    if (kind == 42U
                        && std::abs(scene->canvas_commands[index].data.values[0] - 7.0)
                            < 0.001) {
                        observed_line_width = true;
                    }
                    if (kind == 23U) observed_stroke_rect = true;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (observed_stroke_rect) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    require(observed_fill_rect, "Canvas2D fillRect was absent from the scene packet");
    require(
        !observed_unrelated_fill_state,
        "Canvas2D fillRect eagerly emitted unrelated stroke, line, text, or image state");
    require(
        observed_stroke_style && observed_line_width && observed_stroke_rect,
        "deferred Canvas2D stroke state was not emitted before the later strokeRect");
}

void test_canvas_path_2d_add_path_does_not_fill_stale_current_path(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const canvas = document.createElement('canvas');
          canvas.width = 80;
          canvas.height = 80;
          document.body.appendChild(canvas);
          const context = canvas.getContext('2d');
          context.beginPath();
          context.rect(0, 0, 22, 19);
          context.clip();
          const source = new Path2D('M20 20h40v40H20z');
          const scaled = new Path2D();
          scaled.addPath(source, new DOMMatrix().scaleSelf(0.5, 0.25));
          context.fillStyle = '#1e53e5';
          context.fill(scaled);
        })()
    )JS", "native-canvas-path2d-add-path.js");
    webscene_engine_request_scene_checkpoint(engine);
    bool observed_svg_fill = false;
    bool observed_stale_fill = false;
    bool observed_transform = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->canvas_command_count; ++index) {
                const auto& command = scene->canvas_commands[index];
                if (command.kind == 21U) observed_stale_fill = true;
                if (command.kind == 28U) {
                    observed_svg_fill = true;
                    observed_transform = (command.flags & 0xFFFFU) == 6U
                        && std::abs(command.data.values[0] - 0.5) < 0.001
                        && std::abs(command.data.values[3] - 0.25) < 0.001;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (observed_svg_fill) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (!observed_svg_fill || !observed_transform || observed_stale_fill) {
        fail("Path2D.addPath fill did not preserve its transform independently of the current path: svg="
            + std::to_string(observed_svg_fill) + ", transform="
            + std::to_string(observed_transform) + ", stale="
            + std::to_string(observed_stale_fill));
    }
}

void test_canvas_line_dash_and_path_2d_arc_are_native(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const canvas = document.createElement('canvas');
          canvas.width = 80;
          canvas.height = 80;
          document.body.appendChild(canvas);
          const context = canvas.getContext('2d');
          context.setLineDash([2, 3, 4]);
          const dash = context.getLineDash();
          context.setLineDash([2, -1]);
          const retained = context.getLineDash();
          const path = new Path2D();
          path.arc(40, 40, 20, 0, Math.PI * 2);
          context.stroke(path);
          return { dash, retained };
        })()
    )JS", "native-canvas-line-dash-path2d-arc.js");
    require(
        result == R"({"dash":[2,3,4,2,3,4],"retained":[2,3,4,2,3,4]})",
        "Canvas2D line-dash normalization or invalid-value handling regressed: " + result);

    webscene_engine_request_scene_checkpoint(engine);
    bool observed_dash = false;
    bool observed_arc_path = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->canvas_command_count; ++index) {
                const auto& command = scene->canvas_commands[index];
                if (command.kind == 19U) {
                    observed_dash = command.flags == 7U
                        && command.data.values[0] == 6
                        && command.data.values[1] == 2
                        && command.data.values[2] == 3
                        && command.data.values[3] == 4
                        && command.data.values[4] == 2
                        && command.data.values[5] == 3
                        && command.data.values[6] == 4;
                }
                if (command.kind == 29U) observed_arc_path = true;
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (observed_dash && observed_arc_path) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(observed_dash, "Canvas2D setLineDash did not reach the native scene packet");
    require(observed_arc_path, "Path2D.arc did not reach native SVG-path replay");

#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    const auto report = feature_use(engine);
    require(
        report.find(R"("feature":"CanvasRenderingContext2D.setLineDash","classification":"supported")")
                != std::string::npos
            && report.find(R"("feature":"Path2D.arc","classification":"supported")")
                != std::string::npos,
        "supported canvas feature decisions were absent from the feature inventory: " + report);
#endif
}

size_t count_canvas_layouts_with_bitmap(
    webscene_engine* engine,
    uint32_t bitmap_width,
    uint32_t bitmap_height)
{
    const auto required = webscene_engine_copy_canvas_layouts(engine, nullptr, 0);
    std::vector<webscene_canvas_layout> layouts(required);
    const auto copied = required == 0
        ? 0U
        : webscene_engine_copy_canvas_layouts(engine, layouts.data(), layouts.size());
    return static_cast<size_t>(std::count_if(
        layouts.begin(),
        layouts.begin() + static_cast<std::ptrdiff_t>(copied),
        [&](const auto& layout) {
            return layout.bitmap_width == bitmap_width
                && layout.bitmap_height == bitmap_height;
        }));
}

bool wait_for_canvas_layout_count(
    webscene_engine* engine,
    uint32_t bitmap_width,
    uint32_t bitmap_height,
    size_t expected)
{
    webscene_engine_request_scene_checkpoint(engine);
    for (auto attempt = 0; attempt < 250; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (count_canvas_layouts_with_bitmap(engine, bitmap_width, bitmap_height) == expected) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

void test_detached_canvas_descendants_leave_native_scene(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          const host = document.createElement('div');
          host.id = 'detached-canvas-host';
          const first = document.createElement('canvas');
          first.width = 173;
          first.height = 61;
          first.style.width = '173px';
          first.style.height = '61px';
          first.getContext('2d').fillRect(0, 0, 173, 61);
          const second = document.createElement('canvas');
          second.width = 173;
          second.height = 62;
          second.style.width = '173px';
          second.style.height = '62px';
          second.getContext('2d').fillRect(0, 0, 173, 62);
          host.appendChild(first);
          host.appendChild(second);
          document.body.appendChild(host);
        })();
    )JS", "native-detached-canvas-setup.js");
    require(
        wait_for_canvas_layout_count(engine, 173U, 61U, 1U)
            && wait_for_canvas_layout_count(engine, 173U, 62U, 1U),
        "connected descendant canvases did not enter the native scene");

    const auto state = evaluate(engine, R"JS(
        (() => {
          const host = document.querySelector('#detached-canvas-host');
          const canvases = [...host.querySelectorAll('canvas')];
          host.remove();
          return {
            hostParent: host.parentNode,
            childParentsRetained: canvases.every(canvas => canvas.parentNode === host),
            queryInvisible: document.querySelectorAll('#detached-canvas-host canvas').length === 0
          };
        })()
    )JS", "native-detached-canvas-remove.js");
    require(
        state.find(R"("hostParent":null)") != std::string::npos
            && state.find(R"("childParentsRetained":true)") != std::string::npos
            && state.find(R"("queryInvisible":true)") != std::string::npos,
        "canvas subtree did not retain detached DOM identity: " + state);
    require(
        wait_for_canvas_layout_count(engine, 173U, 61U, 0U)
            && wait_for_canvas_layout_count(engine, 173U, 62U, 0U),
        "canvas descendants of a detached subtree remained in the native scene");
}

void test_svg_dom_parser_preserves_fill_rule(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          const frame = document.createElement('iframe');
          document.body.appendChild(frame);
          const frameDocument = frame.contentDocument;
          frameDocument.open();
          frameDocument.write(
            '<html><body><script>'
            + 'const parsed = new DOMParser().parseFromString('
            + '`<svg><path fill="currentColor" fill-rule="evenodd" d="M0 0h10v10H0z"/></svg>`,'
            + '`image/svg+xml`);'
            + 'globalThis.__parsedFillRule = parsed.querySelector(`path`).getAttribute(`fill-rule`);'
            + '</script></body></html>');
          frameDocument.close();
          globalThis.__fillRuleFrame = frame;
        })()
    )JS", "native-svg-fill-rule-setup.js");
    const auto parsed_fill_rule = evaluate(engine,
        "globalThis.__fillRuleFrame.contentWindow.__parsedFillRule",
        "native-svg-fill-rule-result.js");
    if (parsed_fill_rule != "\"evenodd\"") {
        fail("DOMParser discarded an SVG fill-rule attribute: " + parsed_fill_rule);
    }
}

void test_compound_root_selector_applies_dark_custom_palette(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.documentElement.classList.add('theme-dark');
          const surface = document.createElement('div');
          surface.className = 'surface';
          document.body.appendChild(surface);
          const style = document.createElement('style');
          style.textContent = `
            :root { --custom-surface: #252728; }
            .theme-dark:root { --custom-surface: #0b181a; }
            .surface { background-color: var(--custom-surface); width: 10px; height: 10px; }
          `;
          document.body.appendChild(style);
          return getComputedStyle(surface).backgroundColor;
        })()
    )JS", "native-compound-root-custom-palette.js");
    if (result != "\"rgb(11, 24, 26)\"") {
        fail("compound :root selector did not override the custom palette: " + result);
    }
}

void test_adjacent_inline_runs_share_wrapped_lines(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const paragraph = document.createElement('p');
          paragraph.style.width = '288px';
          paragraph.style.fontSize = '14px';
          paragraph.style.lineHeight = '21px';
          const normal = document.createElement('span');
          normal.appendChild(document.createTextNode("All's well — market is open."));
          const bold = document.createElement('span');
          bold.style.fontWeight = '700';
          bold.appendChild(document.createTextNode("It'll close in 16 hours and 19 minutes."));
          paragraph.appendChild(normal);
          paragraph.appendChild(bold);
          document.body.appendChild(paragraph);
          return [
            paragraph.getBoundingClientRect().height,
            normal.getBoundingClientRect().height,
            bold.getBoundingClientRect().height,
            bold.getBoundingClientRect().width > 200
          ];
        })()
    )JS", "native-adjacent-inline-runs.js");
    if (result != "[42,21,42,true]") {
        fail("adjacent inline runs did not share a two-line formatting context: " + result);
    }
}

void test_inline_flex_preserves_padding_and_line_box(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            .webscene-inline-flex-item-test { padding-right: 6px; }
            .webscene-inline-flex-title-test { padding-right: 1px; }
          `;
          document.body.appendChild(style);
          const wrapper = document.createElement('div');
          wrapper.style.alignItems = 'center';
          wrapper.style.display = 'flex';
          wrapper.style.height = '24px';
          const values = document.createElement('div');
          values.style.display = 'block';
          values.style.fontSize = '13px';
          values.style.lineHeight = '16px';
          values.style.whiteSpace = 'nowrap';
          const itemElements = [];
          const titleElements = [];
          const valueElements = [];
          for (const [title, value] of [['O', '197.43'], ['H', '198.81']]) {
            const item = document.createElement('span');
            item.className = 'webscene-inline-flex-item-test';
            item.style.alignItems = 'center';
            item.style.display = 'inline-flex';
            const titleElement = document.createElement('span');
            titleElement.className = 'webscene-inline-flex-title-test';
            titleElement.style.display = 'inline-flex';
            titleElement.textContent = title;
            const valueElement = document.createElement('span');
            valueElement.style.display = 'inline-flex';
            valueElement.textContent = value;
            item.appendChild(titleElement);
            item.appendChild(valueElement);
            values.appendChild(item);
            itemElements.push(item);
            titleElements.push(titleElement);
            valueElements.push(valueElement);
          }
          wrapper.appendChild(values);
          document.body.appendChild(wrapper);
          const wrapperRect = wrapper.getBoundingClientRect();
          const valuesRect = values.getBoundingClientRect();
          const items = itemElements.map(node => node.getBoundingClientRect());
          const titles = titleElements.map(node => node.getBoundingClientRect());
          const valueRects = valueElements.map(node => node.getBoundingClientRect());
          return {
            centered: Math.abs(valuesRect.y - (wrapperRect.y + 4)) < 0.01,
            lineBoxHeight: Math.round(valuesRect.height),
            itemHeights: items.map(rect => Math.round(rect.height)),
            firstTrailingPadding: Math.round(
              items[0].width - titles[0].width - valueRects[0].width),
            firstTitlePadding: Math.round(
              valueRects[0].x - titles[0].x - (titles[0].width - 1)),
            adjacent: Math.abs(items[1].x - items[0].right) < 0.01
          };
        })()
    )JS", "native-inline-flex-line-box.js");
    if (result != R"JSON({"centered":true,"lineBoxHeight":16,"itemHeights":[16,16],"firstTrailingPadding":6,"firstTitlePadding":1,"adjacent":true})JSON") {
        fail("inline-flex boxes lost their line height or padding: " + result);
    }
}

void test_native_text_input_focus_events_and_caret(webscene_engine* engine)
{
    resize(engine, 320, 120, 20U);
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const input = document.createElement('input');
          input.type = 'search';
          input.style.position = 'absolute';
          input.style.left = '20px';
          input.style.top = '20px';
          input.style.width = '180px';
          input.style.height = '32px';
          input.style.paddingLeft = '4px';
          input.style.fontSize = '16px';
          input.style.color = 'rgb(18, 52, 86)';
          const form = document.createElement('form');
          document.body.appendChild(form);
          form.appendChild(input);
          globalThis.__nativeTextInput = input;
          globalThis.__nativeTextForm = form;
          globalThis.__nativeTextEvents = [];
          for (const type of ['focus', 'keydown', 'keyup', 'beforeinput', 'input']) {
            input.addEventListener(type, event => __nativeTextEvents.push({
              type: event.type,
              key: event.key || null,
              code: event.code || null,
              data: event.data ?? null,
              inputType: event.inputType || null
            }));
          }
          input.focus();
        })()
    )JS", "native-text-input-setup.js");
    require(
        evaluate(engine,
            "document.activeElement === __nativeTextInput",
            "native-text-input-ready.js") == "true",
        "native text input fixture did not receive focus");

    keyboard_input(engine, WEBSCENE_INPUT_KEY_DOWN, 'T', 21U);
    keyboard_input(engine, WEBSCENE_INPUT_TEXT, 't', 22U);
    keyboard_input(engine, WEBSCENE_INPUT_KEY_UP, 'T', 23U);
    keyboard_input(engine, WEBSCENE_INPUT_TEXT, 'r', 24U);
    keyboard_input(engine, WEBSCENE_INPUT_TEXT, 'e', 25U);
    keyboard_input(engine, WEBSCENE_INPUT_TEXT, 'n', 26U);
    keyboard_input(engine, WEBSCENE_INPUT_TEXT, 'd', 27U);
    const auto typed = evaluate(engine, R"JS((() => ({
      value: __nativeTextInput.value,
      selection: [__nativeTextInput.selectionStart, __nativeTextInput.selectionEnd],
      focused: document.activeElement === __nativeTextInput,
      formContract: ['form' in __nativeTextInput, __nativeTextInput.form === __nativeTextForm,
        !('form' in document.createElement('div'))],
      keydown: __nativeTextEvents.find(event => event.type === 'keydown'),
      keyup: __nativeTextEvents.find(event => event.type === 'keyup'),
      inputs: __nativeTextEvents.filter(event => event.type === 'input')
        .map(event => [event.data, event.inputType])
    }))())JS", "native-text-input-typed.js");
    if (typed != R"({"value":"trend","selection":[5,5],"focused":true,"formContract":[true,true,true],"keydown":{"type":"keydown","key":"T","code":"KeyT","data":null,"inputType":null},"keyup":{"type":"keyup","key":"T","code":"KeyT","data":null,"inputType":null},"inputs":[["t","insertText"],["r","insertText"],["e","insertText"],["n","insertText"],["d","insertText"]]})") {
        fail("native text input did not preserve focus, value, selection, or DOM events: "
            + typed + "; error=" + last_error(engine) + "; diagnostics=" + diagnostics(engine));
    }

    animation_frame(engine, 100.0, 28U);
    require(
        evaluate(engine, "__nativeTextInput.value", "native-text-input-frame-ready.js")
            == "\"trend\"",
        "native text input frame did not drain");
    // The shared test engine has already observed later host timestamps in
    // transition fixtures. Re-arm the caret after this fixture's first frame so
    // the blink assertion measures a local 500 ms phase instead of depending
    // on timestamp ordering between otherwise independent tests.
    keyboard_input(engine, WEBSCENE_INPUT_KEY_DOWN, 35U, 28U);
    keyboard_input(engine, WEBSCENE_INPUT_KEY_UP, 35U, 29U);
    require(
        evaluate(engine, "true", "native-text-input-caret-rearmed.js") == "true",
        "native text input caret reset did not drain");
    webscene_engine_request_scene_checkpoint(engine);
    bool observed_caret = false;
    uint64_t caret_scene_revision = 0U;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind == 14U
                    && (command.rgba >> 8U) == 0x123456U
                    && command.x >= 20.0F && command.x <= 200.0F
                    && command.y >= 20.0F && command.y <= 52.0F) {
                    observed_caret = true;
                    caret_scene_revision = scene->header.revision;
                    break;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (observed_caret) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(observed_caret, "focused native input did not publish a visible caret");

    animation_frame(engine, 600.0, 30U);
    require(
        evaluate(engine, "document.activeElement === __nativeTextInput",
            "native-text-input-blink-ready.js") == "true",
        "native text input lost focus while blinking its caret");
    webscene_engine_request_scene_checkpoint(engine);
    bool observed_hidden_caret_phase = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            if (scene->header.revision > caret_scene_revision
                && (scene->header.flags & 1U) != 0U) {
                auto has_caret = false;
                for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                    const auto& command = scene->commands[index];
                    if (command.kind == 14U
                        && (command.rgba >> 8U) == 0x123456U
                        && command.x >= 20.0F && command.x <= 200.0F
                        && command.y >= 20.0F && command.y <= 52.0F) {
                        has_caret = true;
                        break;
                    }
                }
                observed_hidden_caret_phase = !has_caret;
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (observed_hidden_caret_phase) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(observed_hidden_caret_phase, "native input caret did not blink off");

    const auto press_key = [&](uint32_t key_code, uint32_t modifiers, uint64_t& sequence) {
        keyboard_input(engine, WEBSCENE_INPUT_KEY_DOWN, key_code, sequence++, modifiers);
        keyboard_input(engine, WEBSCENE_INPUT_KEY_UP, key_code, sequence++, modifiers);
        require(evaluate(engine, "true", "native-text-input-key-drain.js") == "true",
            "native keyboard navigation events did not drain");
    };
    uint64_t editing_sequence = 31U;
    press_key(37U, 0U, editing_sequence);
    press_key(37U, WEBSCENE_INPUT_MODIFIER_SHIFT, editing_sequence);
    const auto selected = evaluate(engine, R"JS((() => ({
      value: __nativeTextInput.value,
      selection: [__nativeTextInput.selectionStart, __nativeTextInput.selectionEnd],
      direction: __nativeTextInput.selectionDirection
    }))())JS", "native-text-input-shift-selection.js");
    if (selected != R"({"value":"trend","selection":[3,4],"direction":"backward"})") {
        fail("native text input did not extend its selection with Shift+ArrowLeft: " + selected);
    }

    press_key(39U, WEBSCENE_INPUT_MODIFIER_SHIFT, editing_sequence);
    press_key(36U, 0U, editing_sequence);
    press_key(40U, 0U, editing_sequence);
    press_key(38U, 0U, editing_sequence);
    press_key(35U, 0U, editing_sequence);
    press_key(37U, WEBSCENE_INPUT_MODIFIER_SHIFT, editing_sequence);
    const auto navigated = evaluate(engine, R"JS((() => ({
      value: __nativeTextInput.value,
      selection: [__nativeTextInput.selectionStart, __nativeTextInput.selectionEnd],
      direction: __nativeTextInput.selectionDirection,
      keys: __nativeTextEvents.filter(event => event.type === 'keydown').slice(-8)
        .map(event => [event.key, event.code])
    }))())JS", "native-text-input-key-navigation.js");
    if (navigated != R"({"value":"trend","selection":[4,5],"direction":"backward","keys":[["ArrowLeft","ArrowLeft"],["ArrowLeft","ArrowLeft"],["ArrowRight","ArrowRight"],["Home","Home"],["ArrowDown","ArrowDown"],["ArrowUp","ArrowUp"],["End","End"],["ArrowLeft","ArrowLeft"]]})") {
        fail("native text input did not preserve Arrow/Home/End navigation semantics: " + navigated);
    }

    press_key(46U, 0U, editing_sequence);
    press_key(8U, 0U, editing_sequence);
    const auto erased = evaluate(engine, R"JS((() => ({
      value: __nativeTextInput.value,
      selection: [__nativeTextInput.selectionStart, __nativeTextInput.selectionEnd],
      direction: __nativeTextInput.selectionDirection,
      lastInputs: __nativeTextEvents.filter(event => event.type === 'input').slice(-2)
        .map(event => [event.key, event.inputType])
    }))())JS", "native-text-input-backspace.js");
    if (erased != R"({"value":"tre","selection":[3,3],"direction":"none","lastInputs":[["Delete","deleteContentForward"],["Backspace","deleteContentBackward"]]})") {
        fail("native text input did not apply Delete and Backspace through DOM input events: "
            + erased);
    }

    execute(engine, R"JS(
        (() => {
          const textarea = document.createElement('textarea');
          textarea.value = 'firstsecond';
          textarea.selectionStart = textarea.selectionEnd = 5;
          textarea.style.width = '160px';
          textarea.style.height = '48px';
          globalThis.__nativeTextareaEvents = [];
          for (const type of ['keydown', 'beforeinput', 'input']) {
            textarea.addEventListener(type, event => __nativeTextareaEvents.push([
              event.type, event.key || null, event.data ?? null, event.inputType || null
            ]));
          }
          document.body.appendChild(textarea);
          textarea.focus();
          globalThis.__nativeTextarea = textarea;
        })()
    )JS", "native-textarea-enter-setup.js");
    require(
        evaluate(engine,
            "document.activeElement === __nativeTextarea",
            "native-textarea-enter-ready.js") == "true",
        "native textarea fixture did not receive focus");
    press_key(13U, 0U, editing_sequence);
    const auto line_break = evaluate(engine, R"JS((() => ({
      value: __nativeTextarea.value,
      selection: [__nativeTextarea.selectionStart, __nativeTextarea.selectionEnd],
      events: __nativeTextareaEvents
    }))())JS", "native-textarea-enter-result.js");
    if (line_break != R"({"value":"first\nsecond","selection":[6,6],"events":[["keydown","Enter",null,null],["beforeinput","Enter",null,"insertLineBreak"],["input","Enter",null,"insertLineBreak"]]})") {
        fail("native textarea Enter did not apply the browser line-break default: "
            + line_break);
    }
}

void test_frame_script_dom_presence(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          const frame = document.createElement('iframe');
          const outer = document.createElement('div');
          outer.id = 'outer-only';
          document.body.appendChild(outer);
          document.body.appendChild(frame);
          const frameDocument = frame.contentDocument;
          frameDocument.open();
          frameDocument.write(
            '<html><body><script>'
            + 'globalThis.__scriptDomPresent = '
            + 'document.getElementsByTagName("script")[0].parentNode === document.body;'
            + 'globalThis.__modernInputPresent = "oninput" in document;'
            + 'globalThis.__documentAddressPresent = '
            + '[document.URL, document.documentURI, document.baseURI].every('
            + 'value => typeof value === "string" && !!value.match(/^https?:\\/\\//));'
            + '</script></body></html>');
          frameDocument.close();
          globalThis.__scriptDomFrame = frame;
        })()
    )JS", "native-frame-script-dom-setup.js");
    require(
        evaluate(
            engine,
            "[globalThis.__scriptDomFrame.contentWindow.__scriptDomPresent, "
            "globalThis.__scriptDomFrame.contentWindow.__modernInputPresent, "
            "globalThis.__scriptDomFrame.contentWindow.__documentAddressPresent]",
            "native-frame-script-dom-result.js") == "[true,true,true]",
        "hydrated frame scripts, modern input detection, or Document URL semantics regressed");
    const auto boundary = evaluate(
        engine,
        "(() => { const d = globalThis.__scriptDomFrame.contentDocument; return ["
        "!!d.querySelector('script'), !!d.getElementById('outer-only'), "
        "d.querySelectorAll('script').length, d.defaultView === globalThis.__scriptDomFrame.contentWindow]; })()",
        "native-frame-content-document-boundary.js");
    require(
        boundary == "[true,false,1,true]",
        "contentDocument queries escaped the frame document boundary: " + boundary);
}

void test_dom_element_constructor_identity(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const outer = document.createElement('div');
          outer.style.borderLeft = '3px solid black';
          outer.style.borderTop = '5px solid black';
          document.body.appendChild(outer);
          globalThis.__outerElementIdentity = {
            node: outer instanceof Node,
            element: outer instanceof Element,
            htmlElement: outer instanceof HTMLElement,
            ownerElement: outer instanceof outer.ownerDocument.defaultView.Element,
            ownerHtmlElement:
              outer instanceof outer.ownerDocument.defaultView.HTMLElement,
            clientLeft: outer.clientLeft,
            clientTop: outer.clientTop
          };
          const frame = document.createElement('iframe');
          document.body.appendChild(frame);
          const frameDocument = frame.contentDocument;
          frameDocument.open();
          frameDocument.write(
            '<html><body><script>'
            + 'const element = document.createElement("div");'
            + 'document.body.appendChild(element);'
            + 'globalThis.__elementIdentity = {'
            + 'node: element instanceof Node,'
            + 'element: element instanceof Element,'
            + 'htmlElement: element instanceof HTMLElement,'
            + 'ownerElement: element instanceof document.defaultView.Element,'
            + 'ownerHtmlElement: element instanceof document.defaultView.HTMLElement'
            + '};'
            + '</script></body></html>');
          frameDocument.close();
          globalThis.__elementIdentityFrame = frame;
        })()
    )JS", "native-dom-element-constructor-identity-setup.js");
    const auto result = evaluate(engine, R"JS(
        ({
          outer: globalThis.__outerElementIdentity,
          frame: globalThis.__elementIdentityFrame.contentWindow.__elementIdentity
        })
    )JS", "native-dom-element-constructor-identity-result.js");
    require(
        result == R"({"outer":{"node":true,"element":true,"htmlElement":true,"ownerElement":true,"ownerHtmlElement":true,"clientLeft":3,"clientTop":5},"frame":{"node":true,"element":true,"htmlElement":true,"ownerElement":true,"ownerHtmlElement":true}})",
        "DOM wrappers did not expose Element identity or client border insets: "
            + result);
}

void test_provisional_frame_focus_and_document_event_identity(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const outer = document.createElement('button');
          outer.id = 'outer-focus';
          const frame = document.createElement('iframe');
          frame.id = 'focus-frame';
          document.body.appendChild(outer);
          document.body.appendChild(frame);
          const inner = document.createElement('input');
          inner.id = 'inner-focus';
          frame.contentDocument.body.appendChild(inner);
          let documentEvents = 0;
          document.addEventListener('webscene-document-event', () => documentEvents++);
          document.dispatchEvent(new Event('webscene-document-event'));
          inner.focus();
          const focused = {
            outer: document.activeElement?.id || '',
            inner: frame.contentDocument.activeElement?.id || '',
            defaultView: frame.contentDocument.defaultView === frame.contentWindow
          };
          outer.focus();
          return {
            focused,
            restoredOuter: document.activeElement?.id || '',
            restoredInner: frame.contentDocument.activeElement?.tagName || '',
            documentEvents
          };
        })()
    )JS", "native-provisional-frame-focus.js");
    require(
        result == R"({"focused":{"outer":"focus-frame","inner":"inner-focus","defaultView":true},"restoredOuter":"outer-focus","restoredInner":"BODY","documentEvents":1})",
        "provisional frame focus ownership or document EventTarget identity regressed: " + result);
}

void test_initial_frame_document_write_and_hidden_style(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const wrapper = document.createElement('div');
          const frame = document.createElement('iframe');
          wrapper.appendChild(frame);
          document.body.appendChild(wrapper);
          const frameWindow = frame.contentWindow;
          const frameDocument = frame.contentDocument;
          const initial = [
            frameWindow.document === frameDocument,
            frameDocument.defaultView === frameWindow,
            frameWindow.frameElement === frame,
            frameWindow.location.href
          ];
          frameDocument.open();
          frameDocument.write(
            '<!doctype html><html><body><div id="written">Hi</div></body></html>');
          frameDocument.close();
          wrapper.style.display = 'none';
          return {
            initial,
            stable: frame.contentDocument === frameDocument,
            text: frameDocument.querySelector('#written').textContent,
            direction: frameWindow.getComputedStyle(frameDocument.body).direction
          };
        })()
    )JS", "native-initial-frame-document-write.js");
    require(
        result == R"({"initial":[true,true,true,"about:blank"],"stable":true,"text":"Hi","direction":"ltr"})",
        "initial iframe document replacement or hidden computed style regressed: " + result);
}

void test_detached_dom_wrappers_do_not_permanently_root_nodes(webscene_engine* engine)
{
    webscene_engine_metrics before{};
    webscene_engine_get_metrics(engine, &before);
    execute(engine, R"JS(
        (() => {
          const retained = document.createElement('div');
          retained.id = 'retained-detached-node';
          retained.__webSceneExpando = 42;
          document.body.appendChild(retained);
          retained.remove();
          globalThis.__webSceneRetainedDetachedNode = retained;
          for (let index = 0; index < 700; index++) {
            const node = document.createElement('div');
            node.__webSceneDetachedIndex = index;
            document.body.appendChild(node);
            node.remove();
          }
        })()
    )JS", "native-detached-dom-gc.js");
    require(
        evaluate(engine, R"JS(
          (() => {
            const retained = globalThis.__webSceneRetainedDetachedNode;
            document.body.appendChild(retained);
            return {
              expando: retained.__webSceneExpando,
              identity: document.getElementById('retained-detached-node') === retained,
              connected: retained.isConnected
            };
          })()
        )JS", "native-detached-dom-retain.js")
            == R"({"expando":42,"identity":true,"connected":true})",
        "a reachable detached wrapper lost identity or expando state across native DOM collection");
    webscene_engine_metrics after{};
    webscene_engine_get_metrics(engine, &after);
    require(
        after.dom_nodes <= before.dom_nodes + 8U,
        "unreachable detached DOM nodes remained permanently owned after V8 collection");
    execute(engine, R"JS(
        (() => {
          globalThis.__webSceneRetainedDetachedNode.remove();
          delete globalThis.__webSceneRetainedDetachedNode;
        })()
    )JS", "native-detached-dom-cleanup.js");
}

void test_session_storage_in_outer_and_frame_contexts(webscene_engine* engine)
{
    const auto outer = evaluate(engine, R"JS(
        (() => {
          sessionStorage.clear();
          localStorage.clear();
          sessionStorage.setItem('view', 'calendar');
          sessionStorage.setItem('count', 1);
          sessionStorage.setItem('view', 'date');
          const beforeRemove = {
            length: sessionStorage.length,
            keys: [sessionStorage.key(0), sessionStorage.key(1), sessionStorage.key(2)],
            view: sessionStorage.getItem('view'),
            count: sessionStorage.getItem('count'),
            missing: sessionStorage.getItem('missing')
          };
          sessionStorage.removeItem('count');
          localStorage.setItem('theme', 'dark');
          return {
            beforeRemove,
            length: sessionStorage.length,
            first: sessionStorage.key(0),
            localTheme: localStorage.getItem('theme')
          };
        })()
    )JS", "native-session-storage-outer.js");
    if (outer != R"({"beforeRemove":{"length":2,"keys":["view","count",null],"view":"date","count":"1","missing":null},"length":1,"first":"view","localTheme":"dark"})") {
        fail("outer sessionStorage semantics regressed: " + outer);
    }

    execute(engine, R"JS(
        (() => {
          const frame = document.createElement('iframe');
          document.body.appendChild(frame);
          const frameDocument = frame.contentDocument;
          frameDocument.open();
          frameDocument.write(
            '<html><body><script>'
            + 'globalThis.__sessionStorageResult = {'
            + 'outerValue: sessionStorage.getItem("view"),'
            + 'initialLength: sessionStorage.length,'
            + 'localTheme: localStorage.getItem("theme")};'
            + 'sessionStorage.setItem("go-to-date", "month");'
            + 'localStorage.setItem("layout", "compact");'
            + 'globalThis.__sessionStorageResult.value = sessionStorage.getItem("go-to-date");'
            + 'globalThis.__sessionStorageResult.length = sessionStorage.length;'
            + '</script></body></html>');
          frameDocument.close();
          globalThis.__sessionStorageFrame = frame;
        })()
    )JS", "native-session-storage-frame-setup.js");
    const auto frame = evaluate(
        engine,
        "globalThis.__sessionStorageFrame.contentWindow.__sessionStorageResult",
        "native-session-storage-frame-result.js");
    if (frame != R"({"outerValue":null,"initialLength":0,"localTheme":"dark","value":"month","length":1})") {
        fail("frame sessionStorage semantics or isolation regressed: " + frame);
    }
    require(
        evaluate(
            engine,
            "localStorage.getItem('layout')",
            "native-local-storage-frame-sharing.js") == R"("compact")",
        "same-origin localStorage did not remain shared between outer and frame contexts");
}

void test_window_post_message_is_queued(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          let state = 'initial';
          addEventListener('message', () => {
            state = 'delivered';
            globalThis.__postMessageFinalState = state;
          }, { once: true });
          postMessage('idle-work', '*');
          globalThis.__postMessageSynchronousState = state;
        })()
    )JS", "native-window-post-message-queue.js");
    const auto result = evaluate(engine, R"JS(
        ({
          synchronous: globalThis.__postMessageSynchronousState,
          final: globalThis.__postMessageFinalState
        })
    )JS", "native-window-post-message-queue-result.js");
    require(
        result == R"({"synchronous":"initial","final":"delivered"})",
        "Window.postMessage delivered synchronously or missed its queued task: " + result);
}

void test_cross_frame_post_message_and_window_frames(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          const frame = document.createElement('iframe');
          document.body.appendChild(frame);
          const frameDocument = frame.contentDocument;
          frameDocument.open();
          frameDocument.write(
            '<html><body><script>'
            + 'globalThis.__messageListenerReady = true;'
            + 'addEventListener("message", event => {'
            + 'globalThis.__frameReceived = event.data;'
            + 'parent.postMessage({'
            + 'payload: event.data,'
            + 'sourceIsParent: event.source === parent,'
            + 'origin: event.origin'
            + '}, "*");'
            + '});'
            + '</script></body></html>');
          frameDocument.close();
          globalThis.__postMessageFrame = frame;
        })()
    )JS", "native-cross-frame-post-message-setup.js");
    require(
        evaluate(
            engine,
            "(() => { const child = globalThis.__postMessageFrame.contentWindow; return { contains: Array.from(frames).includes(child), ready: typeof child.postMessage, listener: child.__messageListenerReady }; })()",
            "native-cross-frame-post-message-ready.js")
            == R"({"contains":true,"ready":"function","listener":true})",
        "window.frames did not expose the hydrated child realm");
    execute(engine, R"JS(
        (() => {
          globalThis.__crossFrameSynchronous = 'initial';
          addEventListener('message', event => {
            const child = globalThis.__postMessageFrame.contentWindow;
            globalThis.__crossFrameResult = {
              reply: event.data,
              sourceIsFrame: event.source === child,
              framesContainsChild: Array.from(frames).includes(child),
              frameFocusType: typeof child.focus
            };
          }, { once: true });
          globalThis.__postMessageFrame.contentWindow.postMessage(
            'chart-datafeed-request',
            '*');
          globalThis.__crossFrameSynchronous =
            globalThis.__crossFrameResult ? 'delivered' : 'initial';
        })()
    )JS", "native-cross-frame-post-message-send.js");
    require(
        evaluate(
            engine,
            "globalThis.__postMessageFrame.contentWindow.__frameReceived",
            "native-cross-frame-post-message-frame-result.js")
            == R"("chart-datafeed-request")",
        "the queued outer-to-frame message was not delivered");
    const auto result = evaluate(engine, R"JS(
        ({
          synchronous: globalThis.__crossFrameSynchronous,
          result: globalThis.__crossFrameResult
        })
    )JS", "native-cross-frame-post-message-result.js");
    require(
        result == R"({"synchronous":"initial","result":{"reply":{"payload":"chart-datafeed-request","sourceIsParent":true,"origin":"null"},"sourceIsFrame":true,"framesContainsChild":true,"frameFocusType":"function"}})",
        "cross-frame Window.postMessage source/origin/frames semantics regressed: " + result);
    execute(engine, R"JS(
        globalThis.__postMessageFrame.remove();
        delete globalThis.__postMessageFrame;
    )JS", "native-cross-frame-post-message-cleanup.js");
}

void test_frame_resize_preserves_outer_percentage_height(webscene_engine* engine)
{
    resize(engine, 500, 400, 10U);
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = '#resize-host { width: 100%; height: 100%; }';
          document.body.appendChild(style);
          const host = document.createElement('div');
          host.id = 'resize-host';
          const frame = document.createElement('iframe');
          frame.style.width = '100%';
          frame.style.height = '100%';
          host.appendChild(frame);
          document.body.appendChild(host);
          const frameDocument = frame.contentDocument;
          frameDocument.open();
          frameDocument.write(
            '<html><style>body { width: 100%; height: 100%; }</style>'
            + '<body><div style="height:100%"></div></body></html>');
          frameDocument.close();
          globalThis.__resizeHost = host;
          globalThis.__resizeHostFrame = frame;
        })()
    )JS", "native-frame-resize-percentage-setup.js");
    require(
        evaluate(
            engine,
            "globalThis.__resizeHostFrame.contentWindow.innerHeight",
            "native-frame-resize-percentage-ready.js") == "400",
        "percentage-height frame fixture did not hydrate");

    resize(engine, 500, 650, 11U);
    const auto result = evaluate(
        engine,
        "[globalThis.__resizeHost.getBoundingClientRect().height, "
        "globalThis.__resizeHostFrame.getBoundingClientRect().height, "
        "globalThis.__resizeHostFrame.contentWindow.innerHeight]",
        "native-frame-resize-percentage-result.js");
    if (result != "[650,650,650]") {
        fail("frame resize erased the outer percentage-height cascade: " + result);
    }
}

void test_inner_window_load_acknowledgement(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const frame = document.createElement('iframe');
          document.body.appendChild(frame);
          frame.contentWindow.addEventListener('innerWindowLoad', event => {
            event.detail.received = true;
          });
          const detail = { received: false };
          window.dispatchEvent(new CustomEvent('innerWindowLoad', { detail }));
          return detail.received;
        })()
    )JS", "native-inner-window-load-acknowledgement.js");
    require(
        result == "true",
        "inner-window listener acknowledgement was not reflected in CustomEvent.detail");
}

void test_startup_profile_names_scripts_and_tasks()
{
    // Use a dedicated engine so its first component-ready publication captures
    // this script's startup profile. Reusing the broad DOM regression engine
    // made the assertion depend on whether an earlier test had already frozen
    // its one-time readiness diagnostic snapshot.
    auto* engine = webscene_engine_create(64);
    require(engine != nullptr, "startup-profile engine creation failed");
    execute(engine, R"JS(
        globalThis.__profiledTimerRan = false;
        let checksum = 0;
        for (let index = 0; index < 1000000; ++index) {
          checksum = (checksum + index) | 0;
        }
        globalThis.__profiledChecksum = checksum;
        globalThis.__webSceneComponentReady = true;
        setTimeout(function profiledTimer() {
          let timerChecksum = 0;
          for (let index = 0; index < 1000000; ++index) {
            timerChecksum = (timerChecksum + index) | 0;
          }
          globalThis.__profiledChecksum = timerChecksum;
          globalThis.__profiledTimerRan = true;
        }, 0);
    )JS", "native-startup-profile.js");
    require(
        evaluate(engine, "globalThis.__profiledTimerRan", "native-startup-profile-result.js")
            == "true",
        "profiled timer did not run");
    webscene_engine_metrics metrics{};
    webscene_engine_get_metrics(engine, &metrics);
    require(
        metrics.component_ready != 0U,
        "component readiness was not visible until a scene publication");
    auto value = std::string{};
    for (auto attempt = 0; attempt < 100; ++attempt) {
        value = diagnostics(engine);
        if (value.find("native-startup-profile.js") != std::string::npos
            && value.find("profiledTimer") != std::string::npos) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (value.find("script-top=[") == std::string::npos
        || value.find("native-startup-profile.js") == std::string::npos) {
        fail("startup diagnostics did not attribute top-level script execution: " + value);
    }
    if (value.find("task-top=[") == std::string::npos
        || value.find("profiledTimer") == std::string::npos) {
        fail("startup diagnostics did not attribute timer callback execution: " + value);
    }
    if (value.find("total=") == std::string::npos) {
        fail("startup diagnostics did not report total runtime age: " + value);
    }
    webscene_engine_destroy(engine);
}

void test_animation_frame_uses_host_frame(webscene_engine* engine)
{
    execute(engine, R"JS(
        globalThis.__hostFrameTimestamps = [];
        requestAnimationFrame(timestamp => {
          __hostFrameTimestamps.push(timestamp);
          requestAnimationFrame(nextTimestamp => {
            __hostFrameTimestamps.push(nextTimestamp);
          });
        });
    )JS", "native-host-animation-frame-setup.js");
    require(
        evaluate(
            engine,
            "globalThis.__hostFrameTimestamps.length",
            "native-host-animation-frame-ready.js") == "0",
        "requestAnimationFrame setup did not complete");
    wait_for_animation_frame_demand(
        engine,
        true,
        "pending requestAnimationFrame did not request a host frame");

    animation_frame(engine, 123.5, 5U);
    const auto first = evaluate(
        engine,
        "globalThis.__hostFrameTimestamps",
        "native-host-animation-frame-first.js");
    if (first != "[123.5]") {
        fail("host frame did not release exactly one requestAnimationFrame batch: " + first);
    }
    wait_for_animation_frame_demand(
        engine,
        true,
        "nested requestAnimationFrame did not retain host-frame demand");

    animation_frame(engine, 456.25, 6U);
    const auto second = evaluate(
        engine,
        "globalThis.__hostFrameTimestamps",
        "native-host-animation-frame-second.js");
    if (second != "[123.5,456.25]") {
        fail("nested requestAnimationFrame did not wait for the next host frame: " + second);
    }
    wait_for_animation_frame_demand(
        engine,
        false,
        "completed requestAnimationFrame chain retained host-frame demand");
}

void test_animation_frame_callback_list_timestamp_and_cancellation(webscene_engine* engine)
{
    execute(engine, R"JS(
        globalThis.__frameBatchEvents = [];
        let cancelled;
        requestAnimationFrame(timestamp => {
          __frameBatchEvents.push(['first', timestamp]);
          cancelAnimationFrame(cancelled);
          requestAnimationFrame(nextTimestamp => {
            __frameBatchEvents.push(['nested', nextTimestamp]);
          });
        });
        cancelled = requestAnimationFrame(timestamp => {
          __frameBatchEvents.push(['cancelled', timestamp]);
        });
        requestAnimationFrame(timestamp => {
          __frameBatchEvents.push(['last', timestamp]);
        });
    )JS", "native-animation-frame-batch-setup.js");
    require(
        evaluate(
            engine,
            "globalThis.__frameBatchEvents.length",
            "native-animation-frame-batch-ready.js") == "0",
        "animation-frame batch setup did not complete");

    animation_frame(engine, 500.75, 7U);
    auto first = std::string{};
    for (auto attempt = 0; attempt < 100; ++attempt) {
        first = evaluate(
            engine,
            "globalThis.__frameBatchEvents",
            "native-animation-frame-batch-first.js");
        if (first != "[]") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (first != R"([["first",500.75],["last",500.75]])") {
        fail("animation-frame callback list was not one cancelable timestamp batch: "
            + first);
    }

    animation_frame(engine, 525.5, 8U);
    const auto second = evaluate(
        engine,
        "globalThis.__frameBatchEvents",
        "native-animation-frame-batch-second.js");
    if (second
        != R"([["first",500.75],["last",500.75],["nested",525.5]])") {
        fail("nested animation-frame callback escaped its following host frame: "
            + second);
    }
}

void test_animation_frame_pending_callbacks_keep_timestamp(webscene_engine* engine)
{
    execute(engine, R"JS(
        globalThis.__pendingFrameEvents = [];
        requestAnimationFrame(timestamp => {
          __pendingFrameEvents.push(['slow', timestamp]);
          const deadline = performance.now() + 40;
          while (performance.now() < deadline) {}
          requestAnimationFrame(nextTimestamp => {
            __pendingFrameEvents.push(['nested', nextTimestamp]);
          });
        });
        requestAnimationFrame(timestamp => {
          __pendingFrameEvents.push(['pending', timestamp]);
        });
    )JS", "native-animation-frame-pending-setup.js");
    require(
        evaluate(
            engine,
            "globalThis.__pendingFrameEvents.length",
            "native-animation-frame-pending-ready.js") == "0",
        "pending animation-frame setup did not complete");

    const auto consumed_before = consumed_input_count(engine);
    animation_frame(engine, 600.25, 9U);
    wait_for_consumed_inputs(
        engine,
        consumed_before + 1,
        "first pending-callback animation frame was not consumed");
    animation_frame(engine, 625.5, 10U);
    wait_for_consumed_inputs(
        engine,
        consumed_before + 2,
        "second pending-callback animation frame was not consumed");

    auto result = std::string{};
    for (auto attempt = 0; attempt < 100; ++attempt) {
        result = evaluate(
            engine,
            "globalThis.__pendingFrameEvents",
            "native-animation-frame-pending-result.js");
        if (result
            == R"([["slow",600.25],["pending",600.25],["nested",625.5]])") {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    fail("a later host frame retimestamped a pending callback: " + result);
}

void test_pointer_input_precedes_following_render_opportunity()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "input/frame ordering engine creation failed");
    resize(engine, 320, 200, 1U);
    execute(engine, R"JS(
        globalThis.__inputFrameEvents = [];
        globalThis.__inputMoveCount = 0;
        document.body.innerHTML =
          '<div id="input-frame-target" style="position:fixed;inset:0"></div>';
        document.getElementById('input-frame-target').addEventListener('mousemove', () => {
          __inputMoveCount++;
          requestAnimationFrame(timestamp => {
            __inputFrameEvents.push(timestamp);
          });
        }, { once: true });
    )JS", "native-input-frame-order-setup.js");
    require(
        evaluate(
            engine,
            "globalThis.__inputFrameEvents.length",
            "native-input-frame-order-ready.js") == "0",
        "input/frame ordering setup did not complete");

    const auto consumed_before = consumed_input_count(engine);
    pointer_move(engine, 20, 20, 11U);
    animation_frame(engine, 700.5, 0U);
    wait_for_consumed_inputs(
        engine,
        consumed_before + 2,
        "adjacent pointer/frame inputs were not consumed");

    auto result = std::string{};
    for (auto attempt = 0; attempt < 100; ++attempt) {
        result = evaluate(
            engine,
            "({ events: globalThis.__inputFrameEvents, moves: globalThis.__inputMoveCount })",
            "native-input-frame-order-result.js");
        if (result == R"({"events":[700.5],"moves":1})") {
            webscene_engine_destroy(engine);
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    fail("pointer RAF missed its following rendering opportunity: " + result);
}

void test_resize_and_frame_form_one_rendering_opportunity()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "resize/frame pairing engine creation failed");
    resize(engine, 320, 200, 1U);
    execute(engine, R"JS(
        globalThis.__resizeFrameEvents = [];
        addEventListener('resize', () => {
          __resizeFrameEvents.push(['resize', innerWidth, innerHeight]);
          requestAnimationFrame(timestamp => {
            __resizeFrameEvents.push(['frame', timestamp, innerWidth, innerHeight]);
            requestAnimationFrame(nextTimestamp => {
              __resizeFrameEvents.push(['nested', nextTimestamp]);
            });
          });
        }, { once: true });
    )JS", "native-resize-frame-pair-setup.js");
    require(
        evaluate(
            engine,
            "globalThis.__resizeFrameEvents",
            "native-resize-frame-pair-ready.js") == "[]",
        "resize/frame pairing setup did not complete");

    webscene_engine_metrics before{};
    webscene_engine_get_metrics(engine, &before);
    webscene_resize_frame_metrics resize_frame_before{
        sizeof(webscene_resize_frame_metrics)};
    require(
        webscene_engine_get_resize_frame_metrics(
            engine,
            &resize_frame_before) != 0,
        "resize/frame metrics ABI was rejected");
    const webscene_input_event resize_event{
        WEBSCENE_INPUT_RESIZE,
        0U,
        11U,
        640,
        360,
        1,
        0};
    const webscene_input_event frame_event{
        WEBSCENE_INPUT_FRAME,
        0U,
        0U,
        777.25,
        0,
        0,
        0};
    require(
        webscene_engine_enqueue_resize_frame(
            engine,
            &resize_event,
            &frame_event) != 0,
        "paired resize/frame submission was rejected");
    wait_for_consumed_inputs(
        engine,
        before.consumed_inputs + 2U,
        "paired resize/frame inputs were not consumed");

    auto result = std::string{};
    for (auto attempt = 0; attempt < 100; ++attempt) {
        result = evaluate(
            engine,
            "globalThis.__resizeFrameEvents",
            "native-resize-frame-pair-result.js");
        if (result
            == R"([["resize",640,360],["frame",777.25,640,360]])") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (result != R"([["resize",640,360],["frame",777.25,640,360]])") {
        fail("paired resize did not release RAF after resize observers: " + result);
    }
    const auto consumed_after_pair = consumed_input_count(engine);
    animation_frame(engine, 800.5, 0U);
    wait_for_consumed_inputs(
        engine,
        consumed_after_pair + 1U,
        "nested resize RAF host frame was not consumed");
    for (auto attempt = 0; attempt < 100; ++attempt) {
        result = evaluate(
            engine,
            "globalThis.__resizeFrameEvents",
            "native-resize-frame-nested-result.js");
        if (result
            == R"([["resize",640,360],["frame",777.25,640,360],["nested",800.5]])") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (result
        != R"([["resize",640,360],["frame",777.25,640,360],["nested",800.5]])") {
        fail("nested resize RAF did not wait for the next host frame: " + result);
    }

    webscene_engine_metrics after{};
    webscene_engine_get_metrics(engine, &after);
    require(
        after.applied_resize_inputs == before.applied_resize_inputs + 1U
            && after.applied_animation_frames
                == before.applied_animation_frames + 2U
            && after.coalesced_resize_inputs == before.coalesced_resize_inputs
            && after.coalesced_animation_frames
                == before.coalesced_animation_frames,
        "paired resize/frame accounting was inconsistent");
    webscene_resize_frame_metrics resize_frame_after{
        sizeof(webscene_resize_frame_metrics)};
    require(
        webscene_engine_get_resize_frame_metrics(
            engine,
            &resize_frame_after) != 0
            && resize_frame_after.submitted_pairs
                == resize_frame_before.submitted_pairs + 1U
            && resize_frame_after.applied_pairs
                == resize_frame_before.applied_pairs + 1U
            && resize_frame_after.published_pairs
                == resize_frame_before.published_pairs + 1U
            && resize_frame_after.total_queue_nanoseconds
                >= resize_frame_before.total_queue_nanoseconds
                    + resize_frame_after.last_queue_nanoseconds
            && resize_frame_after.total_dispatch_nanoseconds
                >= resize_frame_before.total_dispatch_nanoseconds
                    + resize_frame_after.last_dispatch_nanoseconds
            && resize_frame_after.animation_frame_callbacks
                >= resize_frame_before.animation_frame_callbacks + 1U
            && resize_frame_after.total_animation_frame_batch_nanoseconds
                >= resize_frame_before.total_animation_frame_batch_nanoseconds
                    + resize_frame_after.last_animation_frame_batch_nanoseconds
            && resize_frame_after.maximum_animation_frame_batch_nanoseconds
                >= resize_frame_after.last_animation_frame_batch_nanoseconds
            && resize_frame_after.total_to_publication_nanoseconds
                >= resize_frame_before.total_to_publication_nanoseconds
                    + resize_frame_after.last_to_publication_nanoseconds
            && resize_frame_after.maximum_to_publication_nanoseconds
                >= resize_frame_after.last_to_publication_nanoseconds,
        "resize/frame stage timing was not attributed monotonically");

    const webscene_input_event invalid_frame{
        WEBSCENE_INPUT_POINTER_MOVE,
        0U,
        12U,
        0,
        0,
        0,
        0};
    require(
        webscene_engine_enqueue_resize_frame(
            engine,
            &resize_event,
            &invalid_frame) == 0,
        "paired resize/frame API accepted a non-frame record");
    webscene_engine_destroy(engine);
}

void test_secondary_click(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const target = document.createElement('div');
          target.style.width = '100px';
          target.style.height = '100px';
          document.body.appendChild(target);
          globalThis.__secondaryEvents = [];
          for (const type of ['pointerdown','mousedown','contextmenu','pointerup','mouseup','auxclick']) {
            target.addEventListener(type, event => __secondaryEvents.push([
              event.type, event.button, event.buttons, event.which, event.metaKey, event.shiftKey
            ]));
          }
        })()
    )JS", "native-secondary-click-setup.js");
    require(
        evaluate(engine, "globalThis.__secondaryEvents.length", "native-secondary-click-ready.js") == "0",
        "secondary-click target setup did not complete");

    const auto pointer_modifiers =
        WEBSCENE_INPUT_POINTER_MODIFIER_META | WEBSCENE_INPUT_POINTER_MODIFIER_SHIFT;
    const webscene_input_event down{
        WEBSCENE_INPUT_POINTER_DOWN, 2U | (3U << 8U) | pointer_modifiers, 3U, 10, 10, 0, 0};
    const webscene_input_event up{
        WEBSCENE_INPUT_POINTER_UP, (3U << 8U) | pointer_modifiers, 4U, 10, 10, 0, 0};
    const auto consumed_before_click = consumed_input_count(engine);
    require(webscene_engine_enqueue(engine, &down) != 0, "right-button down was rejected");
    require(webscene_engine_enqueue(engine, &up) != 0, "right-button up was rejected");
    wait_for_consumed_inputs(
        engine,
        consumed_before_click + 2,
        "secondary click inputs were not consumed");
    const auto result = evaluate(
        engine,
        "globalThis.__secondaryEvents",
        "native-secondary-click-result.js");
    if (result != R"([["pointerdown",2,2,3,true,true],["mousedown",2,2,3,true,true],["contextmenu",2,2,3,true,true],["pointerup",2,0,3,true,true],["mouseup",2,0,3,true,true],["auxclick",2,0,3,true,true]])") {
        fail("secondary-click event sequence, button fields, or modifiers regressed: " + result);
    }
}

void test_primary_click_mouse_event_detail(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const target = document.createElement('div');
          target.style.position = 'absolute';
          target.style.left = '0';
          target.style.top = '0';
          target.style.width = '100px';
          target.style.height = '100px';
          target.style.zIndex = '999999';
          document.body.appendChild(target);
          globalThis.__primaryEventDetails = [];
          for (const type of ['pointerdown', 'mousedown', 'pointerup', 'mouseup', 'click']) {
            target.addEventListener(type, event => {
              __primaryEventDetails.push([event.type, event.detail]);
            });
          }
          target.addEventListener('mousedown', event => event.preventDefault());
        })()
    )JS", "native-primary-click-detail-setup.js");
    require(
        evaluate(engine, "document.body.firstElementChild?.offsetWidth", "native-primary-click-detail-ready.js")
            == "100",
        "primary click detail target did not complete layout");
    const webscene_input_event down{
        WEBSCENE_INPUT_POINTER_DOWN, 1U | (1U << 8U), 91U, 10, 10, 0, 0};
    const webscene_input_event up{
        WEBSCENE_INPUT_POINTER_UP, (1U << 8U), 92U, 10, 10, 0, 0};
    const auto consumed_before_click = consumed_input_count(engine);
    require(webscene_engine_enqueue(engine, &down) != 0, "primary click down was rejected");
    require(webscene_engine_enqueue(engine, &up) != 0, "primary click up was rejected");
    wait_for_consumed_inputs(
        engine,
        consumed_before_click + 2,
        "primary click inputs were not consumed");
    const auto result = evaluate(
        engine,
        "({ events: globalThis.__primaryEventDetails, activeTag: document.activeElement?.tagName, activeId: document.activeElement?.id || null })",
        "native-primary-click-detail-result.js");
    if (result != R"({"events":[["pointerdown",0],["mousedown",1],["pointerup",0],["mouseup",1],["click",1]],"activeTag":"BODY","activeId":null})") {
        fail("primary event detail or canceled-mousedown focus default diverged from Chrome: " + result);
    }
}

void test_native_mouseup_honors_immediate_propagation_stop(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const parent = document.createElement('div');
          parent.style.position = 'fixed';
          parent.style.left = '0';
          parent.style.top = '0';
          parent.style.width = '160px';
          parent.style.height = '100px';
          const target = document.createElement('button');
          target.style.width = '100px';
          target.style.height = '60px';
          parent.appendChild(target);
          document.body.appendChild(parent);
          globalThis.__nativeMouseupStopTrace = [];
          target.addEventListener('mouseup', event => {
            globalThis.__nativeMouseupStopTrace.push('first-target');
            event.stopImmediatePropagation();
          });
          target.addEventListener('mouseup', () => {
            globalThis.__nativeMouseupStopTrace.push('second-target');
          });
          parent.addEventListener('mouseup', () => {
            globalThis.__nativeMouseupStopTrace.push('parent');
          });
        })()
    )JS", "native-mouseup-stop-setup.js");
    require(
        evaluate(
            engine,
            "Array.isArray(globalThis.__nativeMouseupStopTrace)",
            "native-mouseup-stop-ready.js") == "true",
        "native mouseup propagation fixture did not initialize");

    pointer_move(engine, 50, 30, 240U);
    pointer_button(
        engine,
        WEBSCENE_INPUT_POINTER_DOWN,
        50,
        30,
        241U,
        true);
    pointer_button(
        engine,
        WEBSCENE_INPUT_POINTER_UP,
        50,
        30,
        242U,
        false);
    const auto result = evaluate(
        engine,
        "globalThis.__nativeMouseupStopTrace",
        "native-mouseup-stop-result.js");
    if (result != R"(["first-target"])") {
        fail("native mouseup ignored stopImmediatePropagation: " + result);
    }
}

void test_component_library_dom_discovery_primitives(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <div id="scope-root">
              <a id="trigger"><span id="nested"></span></a>
              <div class="collapse"><div><div class="collapse" id="nested-collapse"></div></div></div>
            </div>`;
          const root = document.querySelector('#scope-root');
          const nested = document.querySelector('#nested');
          const trigger = document.querySelector('#trigger');
          const escaped = document.createElement('div');
          escaped.id = '0/my id';
          root.appendChild(escaped);
          const loneSurrogate = document.createElement('div');
          loneSurrogate.id = '\ud83dsurrogateFirst';
          root.appendChild(loneSurrogate);
          const replacement = document.createElement('div');
          replacement.setAttribute('id', '\ufffdsurrogateFirst');
          root.appendChild(replacement);
          const original = Event.prototype.preventDefault;
          let prototypeCalls = 0;
          let clickContract = null;
          Event.prototype.preventDefault = function() {
            prototypeCalls++;
            return original.call(this);
          };
          document.addEventListener('click', event => {
            if (event.target !== nested) return;
            event.delegateTarget = trigger;
            event.preventDefault();
          }, { once: true, capture: true });
          trigger.addEventListener('click', event => {
            clickContract = {
              target: event.target.isEqualNode(nested),
              delegate: event.delegateTarget.isEqualNode(trigger),
              prevented: event.defaultPrevented,
              prototype: Event.prototype.isPrototypeOf(event)
            };
          }, { once: true });
          nested.click();
          Event.prototype.preventDefault = original;
          const clone = root.cloneNode(true);
          return {
            scope: Array.from(root.querySelectorAll(':scope .collapse .collapse'), node => node.id),
            escape: CSS.escape('0/my id'),
            escapedMatch: root.querySelector('#' + CSS.escape('0/my id')) === escaped,
            surrogate: {
              propertyRoundTrip: loneSurrogate.id === '\ud83dsurrogateFirst',
              attributeRoundTrip: loneSurrogate.getAttribute('id') === '\ud83dsurrogateFirst',
              literalMatch: root.querySelector('#\ud83dsurrogateFirst') === loneSurrogate,
              escapedReplacement: root.querySelector('#\\d83d surrogateFirst') === replacement
            },
            click: clickContract,
            prototypeCalls,
            same: root.isSameNode(root) && !root.isSameNode(clone),
            equal: root.isEqualNode(clone)
          };
        })()
    )JS", "native-component-library-dom-discovery-primitives.js");
    const auto expected = R"({"scope":["nested-collapse"],"escape":"\\30 \\/my\\ id","escapedMatch":true,"surrogate":{"propertyRoundTrip":true,"attributeRoundTrip":true,"literalMatch":true,"escapedReplacement":true},"click":{"target":true,"delegate":true,"prevented":true,"prototype":true},"prototypeCalls":1,"same":true,"equal":true})";
    if (result != expected) {
        fail("component-library DOM discovery primitives regressed: " + result);
    }
}

void test_dom_selector_apis_throw_syntax_error_for_invalid_selectors(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '<div id="selector-root"><span class="item"></span></div>';
          const root = document.querySelector('#selector-root');
          const item = root.querySelector('.item');
          const capture = operation => {
            try {
              operation();
              return null;
            } catch (error) {
              return [error instanceof DOMException, error.name];
            }
          };
          return {
            errors: [
              capture(() => document.querySelector(':webscene-unknown-pseudo')),
              capture(() => document.querySelectorAll(':webscene-unknown-pseudo')),
              capture(() => root.querySelector(':webscene-unknown-pseudo')),
              capture(() => root.querySelectorAll(':webscene-unknown-pseudo')),
              capture(() => item.matches(':webscene-unknown-pseudo')),
              capture(() => item.closest(':webscene-unknown-pseudo')),
              capture(() => document.querySelector('[')),
              capture(() => item.matches(''))
            ],
            valid: document.querySelector('#selector-root') === root
              && root.querySelectorAll('.item').length === 1
              && item.matches('span.item')
              && !item.matches('button.item')
              && item.closest('#selector-root') === root
          };
        })()
    )JS", "native-dom-selector-syntax-errors.js");
    const auto expected = R"JSON({"errors":[[true,"SyntaxError"],[true,"SyntaxError"],[true,"SyntaxError"],[true,"SyntaxError"],[true,"SyntaxError"],[true,"SyntaxError"],[true,"SyntaxError"],[true,"SyntaxError"]],"valid":true})JSON";
    if (result != expected) {
        fail("DOM selector error semantics diverged from browser behavior: " + result);
    }
}

void test_dropdown_runtime_primitives(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const toggle = document.createElement('button');
          toggle.id = 'dropdown-runtime-toggle';
          toggle.className = 'open';
          toggle.setAttribute('data-toggle', 'component');
          const whitespace = document.createTextNode('\n  ');
          const menu = document.createElement('div');
          menu.id = 'dropdown-runtime-menu';
          menu.className = 'dropdown-runtime-menu';
          document.body.append(toggle, whitespace, menu);

          const style = document.createElement('style');
          style.textContent = '.dropdown-runtime-menu { display: none; }';
          document.body.appendChild(style);
          const hidden = getComputedStyle(menu).display;
          style.remove();
          const restored = getComputedStyle(menu).display;
          style.textContent = '.dropdown-runtime-menu { display: inline-block; }';
          document.body.appendChild(style);
          const reinserted = getComputedStyle(menu).display;

          const selector = '[data-toggle="component"]:not(.disabled):not(:disabled).open';
          let observed = null;
          document.addEventListener('click', event => {
            if (event.target === toggle) observed = event;
          }, { once: true });
          const click = new MouseEvent('click', { bubbles: true });
          toggle.dispatchEvent(click);
          return {
            selector: document.querySelector(selector) === toggle && toggle.matches(selector),
            siblings: toggle.nextElementSibling === menu && menu.previousElementSibling === toggle,
            visibility: [hidden, restored, reinserted],
            event: {
              identity: observed === click,
              target: click.target === toggle,
              currentTarget: click.currentTarget,
              eventPhase: click.eventPhase,
              button: click.button,
              buttons: click.buttons,
              mouse: click instanceof MouseEvent && click instanceof Event
            }
          };
        })()
    )JS", "native-dropdown-runtime-primitives.js");
    const auto expected = R"JSON({"selector":true,"siblings":true,"visibility":["none","block","inline-block"],"event":{"identity":true,"target":true,"currentTarget":null,"eventPhase":0,"button":0,"buttons":0,"mouse":true}})JSON";
    if (result != expected) {
        fail("dropdown runtime primitives regressed: " + result);
    }
}

void test_input_dispatch_failures_are_attributed_and_consumable(webscene_engine* engine)
{
    struct original_input_dispatch_metrics final {
        uint32_t struct_size;
        uint32_t reserved;
        uint64_t last_dispatch_nanoseconds;
        uint64_t maximum_dispatch_nanoseconds;
        uint64_t last_dispatch_sequence;
    };
    static_assert(
        sizeof(original_input_dispatch_metrics)
            == offsetof(webscene_input_dispatch_metrics, dispatched_inputs));
    original_input_dispatch_metrics original{
        sizeof(original_input_dispatch_metrics)};
    require(
        webscene_engine_get_input_dispatch_metrics(
            engine,
            reinterpret_cast<webscene_input_dispatch_metrics*>(&original)) != 0,
        "input-dispatch metrics rejected the original ABI prefix");

    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const target = document.createElement('button');
          target.style.width = '100px';
          target.style.height = '100px';
          target.addEventListener('click', () => {
            throw new Error('certification-input-failure-marker');
          });
          document.body.appendChild(target);
        })()
    )JS", "native-input-failure-setup.js");
    require(
        evaluate(engine, "document.body.firstElementChild?.offsetWidth", "native-input-failure-ready.js")
            == "100",
        "input-failure fixture did not complete layout");

    const webscene_input_event down{
        WEBSCENE_INPUT_POINTER_DOWN, 1U | (1U << 8U), 101U, 10, 10, 0, 0};
    const webscene_input_event up{
        WEBSCENE_INPUT_POINTER_UP, (1U << 8U), 102U, 10, 10, 0, 0};
    const auto consumed_before_click = consumed_input_count(engine);
    require(webscene_engine_enqueue(engine, &down) != 0, "throwing click down was rejected");
    require(webscene_engine_enqueue(engine, &up) != 0, "throwing click up was rejected");
    wait_for_consumed_inputs(
        engine,
        consumed_before_click + 2,
        "throwing click inputs were not consumed");

    webscene_input_dispatch_metrics dispatch_metrics{
        sizeof(webscene_input_dispatch_metrics)};
    require(
        webscene_engine_get_input_dispatch_metrics(engine, &dispatch_metrics) != 0,
        "input-dispatch metrics ABI was rejected");
    require(
        dispatch_metrics.last_dispatch_sequence == up.sequence,
        "input-dispatch timing was not attributed to the last consumed sequence");
    require(
        dispatch_metrics.last_dispatch_nanoseconds > 0
            && dispatch_metrics.maximum_dispatch_nanoseconds
                >= dispatch_metrics.last_dispatch_nanoseconds
            && dispatch_metrics.dispatched_inputs >= 2U
            && dispatch_metrics.total_dispatch_nanoseconds
                >= dispatch_metrics.last_dispatch_nanoseconds,
        "input-dispatch timing metrics were not populated monotonically");

    const auto failure = take_input_dispatch_failure(engine);
    require(
        failure.starts_with("102\n3\nNative ")
            && failure.find(" dispatch failed:") != std::string::npos
            && failure.find("certification-input-failure-marker") != std::string::npos,
        "input failure omitted its sequence, kind, or JavaScript stack: " + failure);
    require(
        take_input_dispatch_failure(engine).empty(),
        "input-dispatch failure was not consumed exactly once");
}

void test_animation_frame_dispatch_is_attributed()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "animation-frame metrics engine creation failed");
    webscene_animation_frame_metrics undersized{};
    require(
        webscene_engine_get_animation_frame_metrics(engine, &undersized) == 0,
        "animation-frame metrics accepted an undersized ABI structure");
    webscene_animation_frame_metrics before{
        sizeof(webscene_animation_frame_metrics)};
    require(
        webscene_engine_get_animation_frame_metrics(engine, &before) != 0,
        "animation-frame metrics ABI was rejected");

    const auto consumed_before = consumed_input_count(engine);
    animation_frame(engine, 12'345.5, 103U);
    wait_for_consumed_inputs(
        engine,
        consumed_before + 1U,
        "attributed animation frame was not consumed");

    webscene_animation_frame_metrics after{
        sizeof(webscene_animation_frame_metrics)};
    require(
        webscene_engine_get_animation_frame_metrics(engine, &after) != 0,
        "animation-frame metrics were unavailable after dispatch");
    require(
        after.dispatched_frames == before.dispatched_frames + 1U
            && after.total_dispatch_nanoseconds
                >= before.total_dispatch_nanoseconds
                    + after.last_dispatch_nanoseconds
            && after.last_dispatch_nanoseconds > 0U
            && after.maximum_dispatch_nanoseconds
                >= after.last_dispatch_nanoseconds
            && after.last_timestamp_microseconds == 12'345'500U,
        "animation-frame timing or timestamp attribution was inconsistent");
    webscene_engine_destroy(engine);
}

void test_scene_flow_is_attributed()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "scene-flow metrics engine creation failed");
    webscene_scene_flow_metrics undersized{};
    require(
        webscene_engine_get_scene_flow_metrics(engine, &undersized) == 0,
        "scene-flow metrics accepted an undersized ABI structure");
    webscene_scene_flow_metrics before{sizeof(webscene_scene_flow_metrics)};
    require(
        webscene_engine_get_scene_flow_metrics(engine, &before) != 0,
        "scene-flow metrics ABI was rejected");

    const webscene_scene_view* scene = nullptr;
    for (auto attempt = 0; attempt < 100 && scene == nullptr; ++attempt) {
        scene = webscene_engine_acquire_latest_scene(engine);
        if (scene == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(scene != nullptr, "scene-flow fixture did not publish an initial scene");
    const auto revision = scene->header.revision;
    require(
        webscene_scene_acknowledge(scene) != 0,
        "scene-flow fixture could not acknowledge its initial scene");
    webscene_scene_release(scene);

    webscene_scene_flow_metrics after{sizeof(webscene_scene_flow_metrics)};
    require(
        webscene_engine_get_scene_flow_metrics(engine, &after) != 0,
        "scene-flow metrics were unavailable after acknowledgement");
    require(
        after.publication_attempts >= before.publication_attempts
            && after.acknowledged_scenes == before.acknowledged_scenes + 1U
            && after.total_acknowledgement_nanoseconds
                >= before.total_acknowledgement_nanoseconds
                    + after.last_acknowledgement_nanoseconds
            && after.maximum_acknowledgement_nanoseconds
                >= after.last_acknowledgement_nanoseconds
            && after.acknowledged_revision == revision,
        "scene publication/backpressure attribution was inconsistent");
    webscene_engine_destroy(engine);
}

void test_read_only_evaluation_does_not_publish_scene()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "read-only evaluation engine creation failed");
    execute_and_wait(
        engine,
        "document.body.textContent = 'ready'",
        "read-only-evaluation-setup.js");

    const webscene_scene_view* initial = nullptr;
    for (auto attempt = 0; attempt < 100 && initial == nullptr; ++attempt) {
        initial = webscene_engine_acquire_latest_scene(engine);
        if (initial == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(initial != nullptr, "read-only evaluation fixture had no initial scene");
    require(
        webscene_scene_acknowledge(initial) != 0,
        "read-only evaluation fixture could not acknowledge its initial scene");
    webscene_scene_release(initial);

    webscene_engine_metrics baseline{};
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* pending = webscene_engine_acquire_latest_scene(engine);
        if (pending != nullptr) {
            webscene_scene_acknowledge(pending);
            webscene_scene_release(pending);
        }
        webscene_engine_get_metrics(engine, &baseline);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        webscene_engine_metrics settled{};
        webscene_engine_get_metrics(engine, &settled);
        if (settled.published_scenes == baseline.published_scenes) {
            baseline = settled;
            break;
        }
    }
    require(
        evaluate(engine, "1 + 1", "read-only-evaluation.js") == "2",
        "read-only evaluation returned the wrong value");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    webscene_engine_metrics after_read{};
    webscene_engine_get_metrics(engine, &after_read);
    if (after_read.published_scenes != baseline.published_scenes) {
        fail(
            "read-only evaluation published an unchanged scene: before="
            + std::to_string(baseline.published_scenes)
            + ", after=" + std::to_string(after_read.published_scenes));
    }

    require(
        evaluate(
            engine,
            "setTimeout(() => {}, 0); true",
            "non-visual-task-evaluation.js") == "true",
        "non-visual task evaluation returned the wrong value");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    webscene_engine_get_metrics(engine, &after_read);
    require(
        after_read.published_scenes == baseline.published_scenes,
        "a completed non-visual task published an unchanged scene");

    require(
        evaluate(
            engine,
            "document.body.textContent = 'changed'; true",
            "mutating-evaluation.js") == "true",
        "mutating evaluation returned the wrong value");
    for (auto attempt = 0; attempt < 100; ++attempt) {
        webscene_engine_get_metrics(engine, &after_read);
        if (after_read.published_scenes > baseline.published_scenes) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        after_read.published_scenes == baseline.published_scenes + 1U,
        "mutating evaluation did not publish its dirty scene");
    webscene_engine_destroy(engine);
}

void test_ordered_scene_consumer_preserves_two_diff_chain()
{
    auto* engine = webscene_engine_create(64);
    require(engine != nullptr, "ordered-scene engine creation failed");
    const auto wait_for_publications = [engine](
        uint64_t minimum,
        const char* failure) {
        for (auto attempt = 0; attempt < 200; ++attempt) {
            webscene_engine_metrics metrics{};
            webscene_engine_get_metrics(engine, &metrics);
            if (metrics.published_scenes >= minimum) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        fail(failure);
    };

    const webscene_scene_view* checkpoint = nullptr;
    for (auto attempt = 0; attempt < 200 && checkpoint == nullptr; ++attempt) {
        checkpoint = webscene_engine_acquire_next_scene(engine);
        if (checkpoint == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(checkpoint != nullptr, "ordered consumer did not acquire checkpoint");
    require(
        webscene_scene_acknowledge(checkpoint) != 0,
        "ordered consumer could not acknowledge checkpoint");
    webscene_scene_release(checkpoint);

    webscene_engine_metrics baseline{};
    webscene_engine_get_metrics(engine, &baseline);
    resize(engine, 401, 301, 1U);
    wait_for_publications(
        baseline.published_scenes + 1U,
        "ordered producer did not publish its first diff");
    const auto* first = webscene_engine_acquire_next_scene(engine);
    require(first != nullptr, "ordered consumer did not acquire first diff");
    const auto first_revision = first->header.revision;

    resize(engine, 402, 302, 2U);
    wait_for_publications(
        baseline.published_scenes + 2U,
        "ordered producer did not publish one frame ahead");
    const auto* latest = webscene_engine_acquire_latest_scene(engine);
    require(latest != nullptr, "ordered producer had no second diff");
    require(
        latest->header.base_revision == first_revision,
        "ordered second diff did not retain the first revision as its base");
    const auto second_revision = latest->header.revision;
    webscene_scene_release(latest);
    webscene_engine_memory_metrics queued_memory{
        sizeof(webscene_engine_memory_metrics)};
    require(
        webscene_engine_get_memory_metrics(engine, &queued_memory) != 0
            && queued_memory.pending_scene_count == 2U
            && queued_memory.pending_scene_bytes
                >= queued_memory.latest_scene_bytes
            && queued_memory.latest_scene_bytes > 0,
        "ordered scene memory was not bounded and attributed");

    webscene_scene_flow_metrics before_block{sizeof(webscene_scene_flow_metrics)};
    require(
        webscene_engine_get_scene_flow_metrics(engine, &before_block) != 0,
        "ordered scene-flow metrics were unavailable");
    resize(engine, 403, 303, 3U);
    webscene_scene_flow_metrics after_block{sizeof(webscene_scene_flow_metrics)};
    for (auto attempt = 0; attempt < 200; ++attempt) {
        require(
            webscene_engine_get_scene_flow_metrics(engine, &after_block) != 0,
            "ordered scene-flow metrics became unavailable");
        if (after_block.blocked_publications
            > before_block.blocked_publications) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        after_block.blocked_publications
            > before_block.blocked_publications,
        "ordered producer exceeded its two-scene bound");

    require(
        webscene_scene_acknowledge(first) != 0,
        "ordered consumer could not acknowledge first diff");
    webscene_scene_release(first);
    const auto* second = webscene_engine_acquire_next_scene(engine);
    require(
        second != nullptr && second->header.revision == second_revision,
        "ordered consumer skipped its second diff");
    require(
        webscene_scene_acknowledge(second) != 0,
        "ordered consumer could not acknowledge second diff");
    webscene_scene_release(second);
    wait_for_publications(
        baseline.published_scenes + 3U,
        "ordered producer did not resume after acknowledgement");
    const auto* third = webscene_engine_acquire_next_scene(engine);
    require(
        third != nullptr && third->header.base_revision == second_revision,
        "ordered consumer lost the resumed diff chain");
    require(
        webscene_scene_acknowledge(third) != 0,
        "ordered consumer could not acknowledge resumed diff");
    webscene_scene_release(third);
    webscene_engine_memory_metrics drained_memory{
        sizeof(webscene_engine_memory_metrics)};
    require(
        webscene_engine_get_memory_metrics(engine, &drained_memory) != 0
            && drained_memory.pending_scene_count == 0
            && drained_memory.pending_scene_bytes == 0,
        "acknowledged ordered scenes remained attributed");
    webscene_engine_destroy(engine);
}

void test_keyboard_and_pointer_focus_modality()
{
    auto* focus_engine = webscene_engine_create(16);
    require(focus_engine != nullptr, "focus-modality engine creation failed");
    resize(focus_engine, 320, 200, 200U);
    execute(focus_engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              @supports not selector(:focus-visible) {
                :focus { background-color: red; }
              }
              .always { outline: 2px solid blue; }
              :focus-visible { outline: 3px solid rgb(0, 128, 0); }
              :focus:not(:focus-visible) { background-color: rgb(0, 255, 0); }
            </style>
            <div id="keyboard" class="always" tabindex="0" style="width:120px;height:40px">keyboard</div>
            <button id="pointer" style="display:block;width:120px;height:40px">pointer</button>`;
          document.body.style.margin = '0';
          globalThis.__focusEvents = [];
          for (const target of [keyboard, pointer]) {
            target.addEventListener('focus', () => __focusEvents.push(`focus:${target.id}`));
            target.addEventListener('blur', () => __focusEvents.push(`blur:${target.id}`));
          }
        })()
    )JS", "native-focus-modality-setup.js");
    require(
        evaluate(focus_engine, "pointer.offsetTop", "native-focus-modality-ready.js") == "40",
        "focus-modality fixture did not complete layout");
    require(
        evaluate(focus_engine, "getComputedStyle(keyboard).outlineColor", "native-outline-ready.js")
            == "\"rgb(0, 0, 255)\"",
        "base outline shorthand did not reach computed style");
    const auto tab_index_reflection = evaluate(focus_engine, R"JS((() => {
      const menu = document.createElement('div');
      menu.setAttribute('tabindex', '-1');
      const authored = menu.tabIndex;
      menu.tabIndex = 2;
      return {
        keyboard: keyboard.tabIndex,
        pointer: pointer.tabIndex,
        body: document.body.tabIndex,
        authored,
        assigned: menu.tabIndex,
        reflected: menu.getAttribute('tabindex')
      };
    })())JS", "native-tab-index-reflection.js");
    if (tab_index_reflection != R"({"keyboard":0,"pointer":0,"body":-1,"authored":-1,"assigned":2,"reflected":"2"})") {
        fail("tabIndex IDL reflection regressed: " + tab_index_reflection);
    }

    keyboard_input(focus_engine, WEBSCENE_INPUT_KEY_DOWN, 9U, 201U);
    keyboard_input(focus_engine, WEBSCENE_INPUT_KEY_UP, 9U, 202U);
    const auto keyboard_result = evaluate(focus_engine, R"JS((() => ({
      active: document.activeElement?.id,
      focusVisible: document.querySelector(':focus-visible')?.id,
      notFocusVisible: document.querySelector(':focus:not(:focus-visible)')?.id ?? null,
      className: keyboard.className,
      outline: getComputedStyle(keyboard).outlineColor,
      background: getComputedStyle(keyboard).backgroundColor,
      events: __focusEvents
    }))())JS", "native-keyboard-focus-result.js");
    if (keyboard_result != R"JSON({"active":"keyboard","focusVisible":"keyboard","notFocusVisible":null,"className":"always","outline":"rgb(0, 128, 0)","background":"rgba(0, 0, 0, 0)","events":["focus:keyboard"]})JSON") {
        fail("Tab focus or :focus-visible keyboard modality regressed: " + keyboard_result);
    }

    const webscene_input_event down{
        WEBSCENE_INPUT_POINTER_DOWN, 1U | (1U << 8U), 203U, 10, 60, 0, 0};
    const webscene_input_event up{
        WEBSCENE_INPUT_POINTER_UP, (1U << 8U), 204U, 10, 60, 0, 0};
    require(webscene_engine_enqueue(focus_engine, &down) != 0, "pointer focus down was rejected");
    require(webscene_engine_enqueue(focus_engine, &up) != 0, "pointer focus up was rejected");
    const auto pointer_result = evaluate(focus_engine, R"JS((() => ({
      active: document.activeElement?.id,
      focusVisible: document.querySelector(':focus-visible')?.id ?? null,
      background: getComputedStyle(pointer).backgroundColor,
      events: __focusEvents
    }))())JS", "native-pointer-focus-result.js");
    if (pointer_result != R"JSON({"active":"pointer","focusVisible":null,"background":"rgb(0, 255, 0)","events":["focus:keyboard","blur:keyboard","focus:pointer"]})JSON") {
        fail("pointer focus or :focus-visible pointer modality regressed: " + pointer_result);
    }
    const auto provisional_frame = evaluate(focus_engine, R"JS((() => {
      const frame = document.createElement('iframe');
      document.body.appendChild(frame);
      const input = document.createElement('input');
      input.id = 'provisional-frame-input';
      frame.contentDocument.body.appendChild(input);
      return {
        hasBody: !!frame.contentDocument.body,
        child: frame.contentDocument.body.firstElementChild?.id
      };
    })())JS", "native-provisional-frame-document.js");
    if (provisional_frame != R"({"hasBody":true,"child":"provisional-frame-input"})") {
        fail("empty iframe contentDocument did not expose a mutable body: " + provisional_frame);
    }
    const auto reentrant_focus = evaluate(focus_engine, R"JS((() => {
      const target = document.createElement('input');
      target.id = 'reentrant-focus-target';
      document.body.appendChild(target);
      let focusOutCount = 0;
      pointer.addEventListener('focusout', () => {
        focusOutCount++;
        target.focus();
      });
      pointer.blur();
      return {
        active: document.activeElement?.id,
        focusOutCount
      };
    })())JS", "native-reentrant-focus.js");
    if (reentrant_focus != R"({"active":"reentrant-focus-target","focusOutCount":1})") {
        fail("focusout re-entrant focus transition regressed: " + reentrant_focus);
    }
    const auto detached_focus = evaluate(focus_engine, R"JS((() => {
      const removedHost = document.createElement('div');
      const removedInput = document.createElement('input');
      const removeEvents = [];
      removedInput.addEventListener('blur', () => removeEvents.push('blur'));
      removedInput.addEventListener('focusout', () => removeEvents.push('focusout'));
      removedHost.addEventListener('blur', () => removeEvents.push('host-blur'));
      removedHost.addEventListener('focusout', () => removeEvents.push('host-focusout'));
      removedHost.appendChild(removedInput);
      document.body.appendChild(removedHost);
      removedInput.focus();
      removedHost.remove();

      const replacedHost = document.createElement('div');
      const replacedInput = document.createElement('input');
      const innerHtmlEvents = [];
      replacedInput.addEventListener('blur', () => innerHtmlEvents.push('blur'));
      replacedInput.addEventListener('focusout', () => innerHtmlEvents.push('focusout'));
      replacedHost.addEventListener('blur', () => innerHtmlEvents.push('host-blur'));
      replacedHost.addEventListener('focusout', () => innerHtmlEvents.push('host-focusout'));
      replacedHost.appendChild(replacedInput);
      document.body.appendChild(replacedHost);
      replacedInput.focus();
      replacedHost.innerHTML = '';
      return {
        activeTag: document.activeElement?.tagName,
        focusMatch: document.querySelector(':focus')?.tagName ?? null,
        removeEvents,
        innerHtmlEvents,
        removedConnected: removedInput.isConnected,
        replacedConnected: replacedInput.isConnected
      };
    })())JS", "native-detached-focus.js");
    if (detached_focus != R"({"activeTag":"BODY","focusMatch":null,"removeEvents":["blur","focusout","host-focusout"],"innerHtmlEvents":["blur","focusout","host-focusout"],"removedConnected":false,"replacedConnected":false})") {
        fail("detached focused nodes remained document.activeElement: " + detached_focus);
    }
    webscene_engine_destroy(focus_engine);
}

void test_resize_precedes_new_viewport_pointer_input(webscene_engine* engine)
{
    resize(engine, 320, 200, 7U);
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const target = document.createElement('div');
          target.style.width = '100vw';
          target.style.height = '100vh';
          target.addEventListener('pointerdown', () => {
            globalThis.__widthObservedByPointer = innerWidth;
          });
          document.body.appendChild(target);
          globalThis.__widthObservedByPointer = 0;
        })()
    )JS", "native-resize-pointer-setup.js");
    require(
        evaluate(engine, "globalThis.__widthObservedByPointer",
            "native-resize-pointer-ready.js") == "0",
        "resize-pointer fixture did not initialize");

    // Keep the worker occupied while both independently queued event streams
    // receive work. This makes the ordering assertion deterministic: the latest
    // coalesced resize and the following pointer are both pending together.
    execute(engine, R"JS(
        (() => {
          const until = Date.now() + 20;
          while (Date.now() < until) {}
        })()
    )JS", "native-resize-pointer-worker-barrier.js");
    resize(engine, 777, 333, 8U);
    const webscene_input_event down{
        WEBSCENE_INPUT_POINTER_DOWN, 1U | (1U << 8U), 9U, 10, 10, 0, 0};
    require(webscene_engine_enqueue(engine, &down) != 0,
        "post-resize pointer down was rejected");

    const auto result = evaluate(
        engine,
        "globalThis.__widthObservedByPointer",
        "native-resize-pointer-result.js");
    if (result != "777") {
        fail("pointer input was dispatched against the stale pre-resize viewport: " + result);
    }
}

void test_resize_updates_device_pixel_ratio(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const frame = document.createElement('iframe');
          document.body.appendChild(frame);
          const frameDocument = frame.contentDocument;
          frameDocument.open();
          frameDocument.write('<html><body></body></html>');
          frameDocument.close();
          globalThis.__scaleFrame = frame;
        })()
    )JS", "native-scale-factor-setup.js");

    resize(engine, 640, 360, 210U, 2.0);
    const auto scaled = evaluate(engine, R"JS((() => ({
      outer: devicePixelRatio,
      frame: globalThis.__scaleFrame.contentWindow.devicePixelRatio,
      width: innerWidth,
      height: innerHeight
    }))())JS", "native-scale-factor-two.js");
    if (scaled != R"({"outer":2,"frame":2,"width":640,"height":360})") {
        fail("devicePixelRatio did not follow the host scale transition: " + scaled);
    }

    resize(engine, 640, 360, 211U, 1.0);
    const auto restored = evaluate(engine, R"JS((() => ({
      outer: devicePixelRatio,
      frame: globalThis.__scaleFrame.contentWindow.devicePixelRatio
    }))())JS", "native-scale-factor-one.js");
    if (restored != R"({"outer":1,"frame":1})") {
        fail("devicePixelRatio did not restore after the host scale transition: " + restored);
    }
}

void test_generated_pseudo_element_opacity(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `
            .status-halo {
              position: relative;
              width: 18px;
              height: 18px;
              --status-background: rgb(83, 186, 176);
            }
            .status-halo::after {
              content: '';
              position: absolute;
              inset: 0;
              width: 18px;
              height: 18px;
              background: var(--status-background);
              opacity: 0.25;
            }
          `;
          document.body.appendChild(style);
          const status = document.createElement('div');
          status.className = 'status-halo';
          document.body.appendChild(status);
        })()
    )JS", "native-pseudo-opacity-setup.js");
    require(
        evaluate(engine, "document.querySelector('.status-halo').offsetWidth",
            "native-pseudo-opacity-ready.js") == "18",
        "pseudo-opacity fixture did not lay out");

    webscene_engine_request_scene_checkpoint(engine);
    uint32_t observed_alpha = 0;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if ((command.kind == 1U || command.kind == 7U)
                    && (command.rgba >> 8U) == 0x53BAB0U
                    && command.width == 18.0F
                    && command.height == 18.0F) {
                    observed_alpha = command.rgba & 0xFFU;
                    break;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (observed_alpha != 0U) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (observed_alpha < 63U || observed_alpha > 64U) {
        fail("generated pseudo-element opacity was not applied to scene alpha: "
            + std::to_string(observed_alpha));
    }
}

void test_negative_z_after_paints_behind_svg_content(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            .negative-after-button {
              display: flex;
              position: relative;
              z-index: 2;
              width: 28px;
              height: 22px;
            }
            .negative-after-button::after {
              content: '';
              position: absolute;
              inset: 1px 0;
              z-index: -1;
              background: #0d1f22;
            }
            .negative-after-button svg {
              width: 18px;
              height: 18px;
              color: #9dabae;
            }
          `;
          document.body.appendChild(style);
          const button = document.createElement('button');
          button.className = 'negative-after-button';
          button.innerHTML = '<svg viewBox="0 0 18 18"><path fill="currentColor" d="M1 1h16v16H1z"/></svg>';
          document.body.appendChild(button);
        })()
    )JS", "native-negative-after-svg-setup.js");
    require(
        evaluate(engine, "document.querySelector('.negative-after-button').offsetWidth",
            "native-negative-after-svg-ready.js") == "28",
        "negative-z pseudo fixture did not lay out");

    webscene_engine_request_scene_checkpoint(engine);
    auto pseudo_index = -1;
    auto svg_index = -1;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if ((command.kind == 9U || command.kind == 10U)
                    && (command.rgba >> 8U) == 0x0D1F22U) {
                    pseudo_index = static_cast<int>(index);
                }
                if (command.kind == 6U && (command.rgba >> 8U) == 0x9DABAEU) {
                    svg_index = static_cast<int>(index);
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (pseudo_index >= 0 && svg_index >= 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (pseudo_index < 0 || svg_index < 0 || pseudo_index >= svg_index) {
        fail("negative-z ::after did not paint behind its SVG content: pseudo="
            + std::to_string(pseudo_index) + ", svg=" + std::to_string(svg_index));
    }
}

void test_svg_current_color_is_resolved_before_scene_serialization(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const row = document.createElement('div');
          row.style.color = 'rgb(157, 171, 174)';
          row.style.width = '24px';
          row.style.height = '24px';
          row.innerHTML = '<svg viewBox="0 0 24 24" width="24" height="24">'
            + '<path fill="currentColor" d="M7 4l9 8-9 8z"/></svg>';
          document.body.appendChild(row);
        })()
    )JS", "native-svg-currentcolor-setup.js");
    require(
        evaluate(engine,
            "document.querySelector('svg').offsetWidth",
            "native-svg-currentcolor-ready.js") == "24",
        "SVG currentColor fixture did not lay out");

    webscene_engine_request_scene_checkpoint(engine);
    auto found_resolved_svg = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind != 6U || command.flags >= scene->string_count) continue;
                const auto resource = scene->strings[command.flags];
                const std::string_view bytes(
                    scene->string_bytes + resource.byte_offset,
                    resource.byte_length);
                found_resolved_svg = bytes.find("fill=\"#9dabae\"") != std::string_view::npos
                    && bytes.find("color=\"#9dabae\"") != std::string_view::npos
                    && bytes.find("currentColor") == std::string_view::npos;
                if (found_resolved_svg) break;
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (found_resolved_svg) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(found_resolved_svg,
        "inherited SVG currentColor did not resolve to an explicit immutable-scene color");
}

void test_positive_z_before_paints_above_lower_z_child(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            #pseudo-stack-host {
              background: red;
              height: 120px;
              position: relative;
              width: 180px;
            }
            #pseudo-stack-host::before {
              background: rgb(0, 192, 0);
              border-radius: 12px;
              content: '';
              height: 50px;
              left: 30px;
              position: absolute;
              top: 20px;
              width: 90px;
              z-index: 2;
            }
            #pseudo-stack-child {
              background: rgb(0, 0, 192);
              height: 50px;
              left: 40px;
              position: absolute;
              top: 30px;
              width: 90px;
              z-index: 1;
            }
          `;
          document.body.appendChild(style);
          const host = document.createElement('div');
          host.id = 'pseudo-stack-host';
          host.innerHTML = '<div id="pseudo-stack-child"></div>';
          document.body.appendChild(host);
        })()
    )JS", "native-positive-before-stack-setup.js");
    require(
        evaluate(engine, "document.querySelector('#pseudo-stack-host').offsetWidth",
            "native-positive-before-stack-ready.js") == "180",
        "positive-z pseudo stacking fixture did not lay out");

    webscene_engine_request_scene_checkpoint(engine);
    auto pseudo_index = -1;
    auto child_index = -1;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind == 10U
                    && (command.rgba >> 8U) == 0x00C000U) {
                    pseudo_index = static_cast<int>(index);
                }
                if ((command.kind == 1U || command.kind == 9U)
                    && (command.rgba >> 8U) == 0x0000C0U) {
                    child_index = static_cast<int>(index);
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (pseudo_index >= 0 && child_index >= 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (pseudo_index < 0 || child_index < 0 || pseudo_index <= child_index) {
        fail("positive-z ::before did not paint above its lower-z child: pseudo="
            + std::to_string(pseudo_index) + ", child=" + std::to_string(child_index));
    }
}

void test_element_opacity_emits_isolated_group(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            #opacity-group {
              background: rgb(255, 0, 0);
              height: 90px;
              opacity: 0.5;
              position: relative;
              width: 140px;
            }
            #opacity-child {
              background: rgb(0, 0, 255);
              height: 50px;
              left: 30px;
              position: absolute;
              top: 20px;
              width: 80px;
            }
          `;
          document.body.appendChild(style);
          const group = document.createElement('div');
          group.id = 'opacity-group';
          const child = document.createElement('div');
          child.id = 'opacity-child';
          group.appendChild(child);
          document.body.appendChild(group);
        })()
    )JS", "native-opacity-group-setup.js");
    require(
        evaluate(engine, "document.querySelector('#opacity-group').offsetWidth",
            "native-opacity-group-ready.js") == "140",
        "opacity group fixture did not lay out");

    webscene_engine_request_scene_checkpoint(engine);
    auto begin_index = -1;
    auto parent_index = -1;
    auto child_index = -1;
    auto end_index = -1;
    auto parent_alpha = 0U;
    auto child_alpha = 0U;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind == 30U && command.node_id != 0U) {
                    begin_index = static_cast<int>(index);
                    require((command.rgba & 0xFFU) == 128U,
                        "opacity group did not encode the expected half alpha");
                } else if (command.kind == 31U && begin_index >= 0) {
                    end_index = static_cast<int>(index);
                } else if ((command.kind == 1U || command.kind == 9U)
                    && (command.rgba >> 8U) == 0xFF0000U) {
                    parent_index = static_cast<int>(index);
                    parent_alpha = command.rgba & 0xFFU;
                } else if ((command.kind == 1U || command.kind == 9U)
                    && (command.rgba >> 8U) == 0x0000FFU) {
                    child_index = static_cast<int>(index);
                    child_alpha = command.rgba & 0xFFU;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (begin_index >= 0 && parent_index >= 0 && child_index >= 0 && end_index >= 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        begin_index >= 0 && begin_index < parent_index
        && parent_index < child_index && child_index < end_index,
        "element opacity did not bracket the completed subtree as one scene group");
    require(parent_alpha == 255U && child_alpha == 255U,
        "element opacity was still multiplied into individual subtree paints");
}

void test_svg_background_image_reaches_scene_with_position_and_size(webscene_engine* engine)
{
    const auto resource_directory = std::filesystem::temp_directory_path()
        / ("webscene-css-background-svg-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(resource_directory);
    const auto svg_path = resource_directory / "badge.svg";
    {
        std::ofstream svg(svg_path, std::ios::binary);
        svg << R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 2 1"><rect width="2" height="1" fill="#00ff00"/></svg>)";
    }
    const auto root = resource_directory.string();
    require(
        webscene_engine_set_resource_root(engine, root.data(), root.size()) != 0,
        "CSS background fixture resource root was rejected");
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            .svg-background {
              width: 100px;
              height: 60px;
              background-image: url("badge.svg");
              background-repeat: no-repeat;
              background-position: 0 2px;
              background-size: 48px auto;
            }
          `;
          document.body.appendChild(style);
          const target = document.createElement('div');
          target.className = 'svg-background';
          document.body.appendChild(target);
        })()
    )JS", "native-svg-background-setup.js");
    require(
        evaluate(engine, R"JS((() => {
          const style = getComputedStyle(document.querySelector('.svg-background'));
          return {
            image: style.backgroundImage,
            repeat: style.backgroundRepeat,
            position: style.backgroundPosition,
            size: style.backgroundSize
          };
        })())JS", "native-svg-background-cssom.js")
            .find(R"("repeat":"no-repeat")") != std::string::npos,
        "CSS background longhands were not exposed through computed style");

    webscene_engine_request_scene_checkpoint(engine);
    auto found = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind == 6U
                    && std::abs(command.x) < 0.01F
                    && std::abs(command.y - 2.0F) < 0.01F
                    && std::abs(command.width - 48.0F) < 0.01F
                    && std::abs(command.height - 24.0F) < 0.01F
                    && command.flags < scene->string_count) {
                    const auto resource = scene->strings[command.flags];
                    const std::string_view bytes(
                        scene->string_bytes + resource.byte_offset,
                        resource.byte_length);
                    found = bytes.find("<svg") != std::string_view::npos
                        && bytes.find("0 0 2 1") != std::string_view::npos;
                    break;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (found) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::error_code cleanup_error;
    std::filesystem::remove_all(resource_directory, cleanup_error);
    require(found, "URL-backed SVG background did not reach the scene at 0,2 48x24");
}

void test_svg_img_element_loads_and_reaches_scene(webscene_engine* engine)
{
    const auto resource_directory = std::filesystem::temp_directory_path()
        / ("webscene-img-svg-"
            + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(resource_directory);
    const auto svg_path = resource_directory / "symbol.svg";
    {
        std::ofstream svg(svg_path, std::ios::binary);
        svg << R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 12"><rect width="24" height="12" fill="#2962ff"/></svg>)";
    }
    const auto root = resource_directory.string();
    require(
        webscene_engine_set_resource_root(engine, root.data(), root.size()) != 0,
        "IMG SVG fixture resource root was rejected");
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const image = document.createElement('img');
          image.id = 'native-svg-image';
          image.style.width = '18px';
          image.style.height = '18px';
          globalThis.__nativeSvgImageLoads = 0;
          image.onload = () => __nativeSvgImageLoads++;
          image.src = 'symbol.svg';
          document.body.appendChild(image);
        })()
    )JS", "native-svg-img-setup.js");

    std::string image_state;
    for (auto attempt = 0; attempt < 200; ++attempt) {
        image_state = evaluate(engine, R"JS((() => {
          const image = document.querySelector('#native-svg-image');
          return {
            complete: image.complete,
            currentSrc: image.currentSrc,
            naturalWidth: image.naturalWidth,
            naturalHeight: image.naturalHeight,
            loads: __nativeSvgImageLoads
          };
        })())JS", "native-svg-img-state.js");
        if (image_state.find(R"("complete":true)") != std::string::npos
            && image_state.find(R"("loads":1)") != std::string::npos) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    require(
        image_state.find(R"("currentSrc":)") != std::string::npos
            && image_state.find("symbol.svg") != std::string::npos
            && image_state.find(R"("naturalWidth":24)") != std::string::npos
            && image_state.find(R"("naturalHeight":12)") != std::string::npos
            && image_state.find(R"("loads":1)") != std::string::npos,
        "SVG IMG element did not expose an asynchronous loaded state: "
            + image_state);

    webscene_engine_request_scene_checkpoint(engine);
    auto found = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind == 6U
                    && std::abs(command.x) < 0.01F
                    && std::abs(command.y) < 0.01F
                    && std::abs(command.width - 18.0F) < 0.01F
                    && std::abs(command.height - 18.0F) < 0.01F
                    && command.flags < scene->string_count) {
                    const auto resource = scene->strings[command.flags];
                    const std::string_view bytes(
                        scene->string_bytes + resource.byte_offset,
                        resource.byte_length);
                    found = bytes.find("<svg") != std::string_view::npos
                        && bytes.find("0 0 24 12") != std::string_view::npos;
                    break;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (found) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::error_code cleanup_error;
    std::filesystem::remove_all(resource_directory, cleanup_error);
    require(found, "loaded SVG IMG element did not reach the native scene");
}

void test_virtual_html_root_inherits_font_metrics(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = 'html { font-size: 10px; line-height: 1; }';
          document.body.appendChild(style);
          const row = document.createElement('div');
          row.innerHTML = '<span id="root-font-probe">x</span>';
          document.body.appendChild(row);
        })()
    )JS", "native-virtual-html-font-setup.js");
    const auto state = evaluate(engine, R"JS((() => {
      const body = getComputedStyle(document.body);
      const probe = getComputedStyle(document.getElementById('root-font-probe'));
      return {
        bodyFont: body.fontSize,
        bodyLine: body.lineHeight,
        probeFont: probe.fontSize,
        probeLine: probe.lineHeight
      };
    })())JS", "native-virtual-html-font-read.js");
    require(
        state.find(R"("bodyFont":"10px")") != std::string::npos
            && state.find(R"("bodyLine":"10px")") != std::string::npos
            && state.find(R"("probeFont":"10px")") != std::string::npos
            && state.find(R"("probeLine":"10px")") != std::string::npos,
        "virtual HTML root font metrics did not inherit: " + state);
}

void test_font_shorthand_inherit_resets_control_metrics(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `
            .font-parent {
              font-size: 20px;
              line-height: 30px;
              font-weight: 700;
              font-family: WebSceneFontProbe;
            }
            .font-child { font: inherit; }
          `;
          document.body.appendChild(style);
          const parent = document.createElement('div');
          parent.className = 'font-parent';
          const child = document.createElement('button');
          child.id = 'font-inherit-probe';
          child.className = 'font-child';
          child.textContent = 'Inherited control';
          parent.appendChild(child);
          document.body.appendChild(parent);
        })()
    )JS", "native-font-inherit-setup.js");
    const auto state = evaluate(engine, R"JS((() => {
      const style = getComputedStyle(document.getElementById('font-inherit-probe'));
      return {
        size: style.fontSize,
        line: style.lineHeight,
        weight: style.fontWeight,
        family: style.fontFamily
      };
    })())JS", "native-font-inherit-read.js");
    require(
        state.find(R"("size":"20px")") != std::string::npos
            && state.find(R"("line":"30px")") != std::string::npos
            && state.find(R"("weight":"700")") != std::string::npos
            && state.find("WebSceneFontProbe") != std::string::npos,
        "font:inherit did not reset control metrics to inherited values: " + state);
}

void test_all_unset_resets_modeled_control_properties(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `
            .all-parent {
              color: rgb(10, 20, 30);
              font-size: 19px;
              line-height: 27px;
            }
            .all-reset {
              width: 200px;
              height: 60px;
              padding: 11px;
              margin: 13px;
              border: 5px solid red;
              background: red;
              opacity: 0.25;
              transform: translate(12px, 8px);
              --all-token: retained;
              all: unset;
              display: flex;
              width: 90px;
            }
          `;
          document.body.appendChild(style);
          const parent = document.createElement('div');
          parent.className = 'all-parent';
          const control = document.createElement('button');
          control.id = 'all-unset-probe';
          control.className = 'all-reset';
          control.style.height = '44px';
          control.textContent = 'Reset control';
          parent.appendChild(control);
          document.body.appendChild(parent);
        })()
    )JS", "native-all-unset-setup.js");
    const auto state = evaluate(engine, R"JS((() => {
      const style = getComputedStyle(document.getElementById('all-unset-probe'));
      return {
        display: style.display,
        width: style.width,
        height: style.height,
        padding: style.getPropertyValue('padding-left'),
        margin: style.getPropertyValue('margin-left'),
        border: style.borderTopWidth,
        background: style.backgroundColor,
        opacity: style.opacity,
        transform: style.transform,
        color: style.color,
        size: style.fontSize,
        line: style.lineHeight,
        token: style.getPropertyValue('--all-token')
      };
    })())JS", "native-all-unset-read.js");
    require(
        state.find(R"("display":"flex")") != std::string::npos
            && state.find(R"("width":"90px")") != std::string::npos
            && state.find(R"("height":"44px")") != std::string::npos
            && state.find(R"("padding":"0px")") != std::string::npos
            && state.find(R"("margin":"0px")") != std::string::npos
            && state.find(R"("border":"0px")") != std::string::npos
            && state.find("\"background\":\"rgba(0, 0, 0, 0)\"") != std::string::npos
            && state.find(R"("opacity":"1")") != std::string::npos
            && state.find(R"("transform":"none")") != std::string::npos
            && state.find("\"color\":\"rgb(10, 20, 30)\"") != std::string::npos
            && state.find(R"("size":"19px")") != std::string::npos
            && state.find(R"("line":"27px")") != std::string::npos
            && state.find(R"("token":"retained")") != std::string::npos,
        "all:unset did not reset modeled properties while preserving inline/custom state: "
            + state);

#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    const auto report = feature_use(engine);
    require(
        report.find(R"("feature":"property:all","classification":"partially-supported","count":1,"semanticSlice":"non-important unset across modeled properties, excluding custom properties")")
            != std::string::npos,
        "all:unset was not classified at its native decision point: " + report);
#endif
}

void test_unsupported_features_are_reported_at_native_decision_points(webscene_engine* engine)
{
#if !defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    const auto report = feature_use(engine);
    require(
        report.find(R"("telemetryEnabled":false)") != std::string::npos
            && report.find("telemetry-disabled") != std::string::npos,
        "production feature-use API did not report compile-time telemetry exclusion");
    const auto scene_report = diagnostics(engine);
    require(
        scene_report.find("telemetry disabled at compile time") != std::string::npos,
        "production diagnostics API did not report compile-time telemetry exclusion");
#else
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `
            .unsupported-probe { backdrop-filter: blur(2px); }
            .alias-probe {
              display: flex;
              -moz-transform: translate(12px, 3px);
              transform-origin: left center;
              grid-gap: 7px;
            }
            .clip-composition { overflow: hidden; }
            .shadow-composition {
              border-radius: 6px;
              box-shadow: 0 2px 6px rgba(0, 0, 0, .5);
            }
            .relative-unit-probe { width: 12em; height: 1rem; margin-left: 1vw; }
            .quoted-unit-probe::before { content: "99q"; }
            @media screen and (min-width: 10px) {
              .media-probe { width: calc(8px + 2px); }
            }
            @media (prefers-color-scheme: dark) {
              .unsupported-media-probe { width: 1px; }
            }
            @supports selector(:focus-visible) {
              .supported-query-probe:focus-visible { opacity: 1; }
            }
            @supports (display: grid) {
              .unsupported-query-probe { display: grid; }
            }
            @layer components {
              .layer-probe { color: rgb(1, 2, 3); }
            }
            @container size (width > 10px) {
              .container-probe { color: hsl(0, 0%, 0%); }
            }
            .selector-probe:is(.one, .two):nth-child(2) > [data-probe^="value"]::marker {
              transform: translateX(2px) scale(1.1);
            }
          `;
          document.body.appendChild(style);
          const target = document.createElement('div');
          target.className = 'unsupported-probe';
          target.addEventListener('click', () => {});
          target.addEventListener('webscene-never-dispatched', () => {});
          target.addEventListener('webscene-passive', () => {}, { passive: true });
          target.addEventListener('webscene-dispatched', () => {});
          target.dispatchEvent(new Event('webscene-dispatched'));
          target.setAttribute('aria-label', 'feature probe');
          target.setAttribute('draggable', 'true');
          document.body.appendChild(target);
          const alias = document.createElement('div');
          alias.className = 'alias-probe';
          document.body.appendChild(alias);
          const clip = document.createElement('div');
          clip.className = 'clip-composition';
          const shadow = document.createElement('div');
          shadow.className = 'shadow-composition';
          clip.appendChild(shadow);
          document.body.appendChild(clip);
          const canvas = document.createElement('canvas');
          document.body.appendChild(canvas);
          canvas.getContext('2d').ellipse(10, 10, 5, 5, 0, 0, Math.PI * 2);
          const input = document.createElement('input');
          input.setAttribute('autocomplete', 'off');
          input.setAttribute('placeholder', 'feature probe');
          document.body.appendChild(input);
          sessionStorage.setItem('feature-probe', 'value');
          sessionStorage.getItem('feature-probe');
          sessionStorage.removeItem('feature-probe');
          new Blob(['feature-probe']);
          new URL('/feature-probe', location.href);
          crypto.getRandomValues(new Uint8Array(4));
          matchMedia('(min-width: 1px)');
          performance.now();
          performance.mark('feature-probe');
          performance.getEntriesByName('feature-probe');
          void (typeof localStorage);
        })()
    )JS", "native-feature-use-probe.js");
    require(
        evaluate(engine, "document.querySelector('.unsupported-probe').offsetWidth >= 0",
            "native-feature-use-barrier.js") == "true",
        "feature-use fixture did not settle");

    const auto report = feature_use(engine);
    require(
        report.find(R"("schema":"webscene-native-feature-use-v2")") != std::string::npos
            && report.find(R"("complete":false)") != std::string::npos
            && report.find(R"("schema":"webscene-native-composition-use-v1")")
                != std::string::npos
            && report.find(R"("id":"css-transform-origin-independent-cascade")")
                != std::string::npos
            && report.find(R"({"id":"css-transform-origin-independent-cascade","count":)")
                != std::string::npos
            && report.find(R"({"id":"css-shadow-radius-overflow","count":)")
                != std::string::npos
            && report.find("canvas-supported-operations") == std::string::npos
            && report.find(R"("feature":"property:backdrop-filter")") != std::string::npos
            && report.find(R"("feature":"CanvasRenderingContext2D.ellipse")") != std::string::npos
            && report.find(R"("feature":"property:-moz-transform","classification":"supported")")
                != std::string::npos
            && report.find(R"("feature":"property:grid-gap","classification":"supported")")
                != std::string::npos
            && report.find(R"("feature":"at-rule:@media","classification":"supported")")
                != std::string::npos
            && report.find(R"("feature":"media-feature:prefers-color-scheme","classification":"supported")")
                != std::string::npos
            && report.find(R"("feature":"at-rule:@supports","classification":"supported")")
                != std::string::npos
            && report.find(R"("feature":"at-rule:@supports","classification":"unsupported")")
                != std::string::npos
            && report.find(R"("feature":"at-rule:@layer","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"at-rule:@container","classification":"unsupported")")
                != std::string::npos
            && report.find(R"("feature":"selector:pseudo-class:nth-child","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"selector:pseudo-class:is","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"selector:attribute-operator:^=","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"selector:pseudo-element:marker","classification":"unsupported")")
                != std::string::npos
            && report.find(R"("feature":"function:calc","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"length-unit:em","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"length-unit:rem","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"length-unit:vw","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"length-unit:q")") == std::string::npos
            && report.find(R"("feature":"attribute:aria-label","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"attribute:draggable","classification":"unsupported")")
                != std::string::npos
            && report.find(R"("feature":"attribute:autocomplete","classification":"unsupported")")
                != std::string::npos
            && report.find(R"("feature":"attribute:placeholder","classification":"unsupported")")
                != std::string::npos
            && report.find(R"("feature":"function:hsl","classification":"unsupported")")
                != std::string::npos
            && report.find(R"("feature":"method:EventTarget.addEventListener","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"type:webscene-never-dispatched","classification":"unobserved-code-path")")
                != std::string::npos
            && report.find(R"("feature":"type:webscene-dispatched","classification":"supported")")
                != std::string::npos
            && report.find(R"("feature":"type:webscene-dispatched","classification":"unobserved-code-path")")
                == std::string::npos
            && report.find(R"("feature":"Storage.setItem","classification":"supported")")
                != std::string::npos
            && report.find(R"("feature":"Blob.constructor","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"Crypto.getRandomValues","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"Window.matchMedia","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"Performance.getEntriesByName","classification":"unsupported")")
                != std::string::npos
            && report.find(R"("feature":"global:localStorage","classification":"unsupported")")
                == std::string::npos
            && report.find(R"("classification":"unsupported")") != std::string::npos,
        "unsupported native decisions were absent from the structured report: " + report);

    const auto listeners = event_listener_inventory(engine);
    require(
        listeners.find(R"("schema":"webscene-event-listener-inventory-v1")") != std::string::npos
            && listeners.find(R"("complete":true)") != std::string::npos
            && listeners.find(R"("eventTypes":["click","webscene-dispatched")")
                != std::string::npos,
        "registered listener targets were absent from the structured inventory: " + listeners);
#endif
}

void test_engine_memory_metrics_are_worker_snapshots(webscene_engine* engine)
{
    struct original_memory_metrics final {
        uint32_t struct_size;
        uint32_t reserved;
        uint64_t values[10];
    };
    static_assert(
        sizeof(original_memory_metrics)
            == offsetof(webscene_engine_memory_metrics, v8_code_and_metadata_bytes));

    execute(
        engine,
        "globalThis.__websceneMemoryProbe = new Array(4096).fill('probe')",
        "memory-metrics.js");
    webscene_engine_memory_metrics undersized{};
    require(
        webscene_engine_get_memory_metrics(engine, &undersized) == 0,
        "memory metrics accepted an undersized ABI structure");
    original_memory_metrics original{sizeof(original_memory_metrics)};
    require(
        webscene_engine_get_memory_metrics(
            engine,
            reinterpret_cast<webscene_engine_memory_metrics*>(&original)) != 0,
        "memory metrics rejected the original ABI structure prefix");
    webscene_engine_memory_metrics previous_tail{};
    previous_tail.struct_size =
        offsetof(webscene_engine_memory_metrics, v8_young_space_used_bytes);
    previous_tail.native_event_listener_storage_bytes =
        (std::numeric_limits<uint64_t>::max)();
    require(
        webscene_engine_get_memory_metrics(engine, &previous_tail) != 0
            && previous_tail.native_event_listener_storage_bytes
                != (std::numeric_limits<uint64_t>::max)(),
        "memory metrics rejected or truncated the previous additive ABI tail");

    webscene_engine_memory_metrics metrics{
        sizeof(webscene_engine_memory_metrics)};
    metrics.v8_code_and_metadata_bytes =
        (std::numeric_limits<uint64_t>::max)();
    metrics.v8_bytecode_and_metadata_bytes =
        (std::numeric_limits<uint64_t>::max)();
    for (auto attempt = 0; attempt < 100 && metrics.v8_total_heap_bytes == 0; ++attempt) {
        require(
            webscene_engine_get_memory_metrics(engine, &metrics) != 0,
            "memory metrics ABI was unavailable");
        if (metrics.v8_total_heap_bytes == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(
        metrics.v8_total_heap_bytes > 0
            && metrics.v8_used_heap_bytes > 0
            && metrics.v8_physical_heap_bytes > 0,
        "V8 heap memory was absent from the worker snapshot");
    require(
        metrics.v8_used_heap_bytes <= metrics.v8_total_heap_bytes,
        "V8 used heap exceeded total heap");
    require(
        metrics.v8_code_and_metadata_bytes
                != (std::numeric_limits<uint64_t>::max)()
            && metrics.v8_bytecode_and_metadata_bytes
                != (std::numeric_limits<uint64_t>::max)(),
        "V8 code/bytecode memory fields were absent from the worker snapshot");
    const auto attributed_space_used =
        metrics.v8_young_space_used_bytes
        + metrics.v8_old_space_used_bytes
        + metrics.v8_code_space_used_bytes
        + metrics.v8_map_space_used_bytes
        + metrics.v8_large_object_space_used_bytes
        + metrics.v8_read_only_space_used_bytes
        + metrics.v8_shared_space_used_bytes
        + metrics.v8_trusted_space_used_bytes;
    const auto attributed_space_physical =
        metrics.v8_young_space_physical_bytes
        + metrics.v8_old_space_physical_bytes
        + metrics.v8_code_space_physical_bytes
        + metrics.v8_map_space_physical_bytes
        + metrics.v8_large_object_space_physical_bytes
        + metrics.v8_read_only_space_physical_bytes
        + metrics.v8_shared_space_physical_bytes
        + metrics.v8_trusted_space_physical_bytes;
    require(
        attributed_space_used > 0
            && attributed_space_physical > 0
            && attributed_space_used
                <= metrics.v8_used_heap_bytes
                    + metrics.v8_read_only_space_used_bytes
            && attributed_space_physical
                <= metrics.v8_physical_heap_bytes
                    + metrics.v8_read_only_space_physical_bytes,
        "V8 heap-space attribution was absent or exceeded aggregate heap metrics");
    require(
        metrics.native_dom_node_count > 0
            && metrics.native_dom_node_size_bytes == sizeof(webscene_native::dom_node)
            && metrics.native_dom_inline_bytes
                == metrics.native_dom_node_count * metrics.native_dom_node_size_bytes,
        "native DOM allocation attribution was absent or internally inconsistent");
    require(
        metrics.native_dom_node_size_bytes <= 1000,
        "cold pseudo-element, canvas, animation, custom-property, background, grid, table, or form state regressed into every native DOM node");
    require(
        metrics.native_dom_textual_style_count <= metrics.native_dom_node_count
            && (metrics.native_dom_textual_style_count == 0
                || metrics.native_dom_textual_style_storage_bytes
                    >= metrics.native_dom_textual_style_count
                        * sizeof(webscene_native::node_style::textual_style_data)),
        "sparse textual-style allocation attribution was internally inconsistent");
    require(
        metrics.native_dom_table_layout_count <= metrics.native_dom_node_count
            && (metrics.native_dom_table_layout_count == 0
                || metrics.native_dom_table_layout_storage_bytes
                    >= metrics.native_dom_table_layout_count
                        * sizeof(webscene_native::dom_node::table_layout_data))
            && metrics.native_dom_form_control_count <= metrics.native_dom_node_count
            && (metrics.native_dom_form_control_count == 0
                || metrics.native_dom_form_control_storage_bytes
                    >= metrics.native_dom_form_control_count
                        * sizeof(webscene_native::dom_node::form_control_data)),
        "sparse table/form allocation attribution was internally inconsistent");
    require(
        metrics.native_dom_attribute_node_count <= metrics.native_dom_node_count
            && (metrics.native_dom_attribute_entry_count == 0
                || metrics.native_dom_attribute_storage_bytes
                    >= metrics.native_dom_attribute_entry_count
                        * sizeof(webscene_native::attribute_collection::value_type)),
        "native compact-attribute allocation attribution was internally inconsistent");
    require(
        metrics.native_dom_animation_count <= metrics.native_dom_node_count
            && (metrics.native_dom_animation_count == 0
                || metrics.native_dom_animation_storage_bytes
                    >= metrics.native_dom_animation_count
                        * sizeof(webscene_native::node_style::animation_data)),
        "native animation allocation attribution was internally inconsistent");
}

void test_hidden_engine_reclamation_is_debounced_and_cancelable(
    webscene_engine* engine)
{
    const auto read_hidden_notifications = [&]() {
        webscene_engine_memory_metrics metrics{sizeof(webscene_engine_memory_metrics)};
        require(
            webscene_engine_get_memory_metrics(engine, &metrics) != 0,
            "hidden-memory policy could not read engine metrics");
        return metrics.hidden_low_memory_notifications;
    };

    require(
        webscene_engine_set_visible(engine, 0) != 0,
        "engine rejected hidden host state");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    require(
        webscene_engine_set_visible(engine, 1) != 0,
        "engine rejected restored visible host state");
    const auto before_cancel_window = read_hidden_notifications();
    std::this_thread::sleep_for(std::chrono::milliseconds(550));
    require(
        read_hidden_notifications() == before_cancel_window,
        "transient hidden state was not canceled before reclamation");

    require(
        webscene_engine_set_visible(engine, 0) != 0,
        "engine rejected sustained hidden host state");
    auto observed = false;
    for (auto attempt = 0; attempt < 200; ++attempt) {
        if (read_hidden_notifications() > before_cancel_window) {
            observed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    require(
        observed,
        "sustained hidden state did not schedule worker-thread reclamation");
    require(
        webscene_engine_set_visible(engine, 1) != 0,
        "engine rejected final visible host state");
}

void test_native_websocket_browser_api()
{
    require(ix::initNetSystem(), "IXWebSocket network initialization failed");
    const auto port = ix::getFreePort();
    require(port > 0, "could not reserve a local WebSocket test port");

    std::mutex server_state_mutex;
    std::string observed_origin;
    ix::WebSocketServer server(port, "127.0.0.1");
    server.setOnClientMessageCallback(
        [&](std::shared_ptr<ix::ConnectionState>,
            ix::WebSocket& socket,
            const ix::WebSocketMessagePtr& message) {
            if (message->type == ix::WebSocketMessageType::Open) {
                std::lock_guard lock(server_state_mutex);
                const auto origin = message->openInfo.headers.find("Origin");
                if (origin != message->openInfo.headers.end()) {
                    observed_origin = origin->second;
                }
            } else if (message->type == ix::WebSocketMessageType::Message) {
                socket.send(message->str, message->binary);
            }
        });
    const auto listening = server.listen();
    require(listening.first, "local WebSocket server could not listen");
    server.start();

    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "WebSocket test engine creation failed");
    const auto source = std::string(R"JS(
        location.protocol = 'https:';
        location.host = 'websocket-origin.test';
        globalThis.__webSceneWebSocketTest = {
          events: [], text: null, binary: null, closed: false, closeCode: 0,
          closeReason: '', error: null
        };
        const socket = new WebSocket('ws://127.0.0.1:)JS")
        + std::to_string(port)
        + R"JS(/echo');
        socket.binaryType = 'arraybuffer';
        socket.addEventListener('open', () => {
          __webSceneWebSocketTest.events.push('open');
          socket.send('native-text');
        });
        socket.addEventListener('message', event => {
          __webSceneWebSocketTest.events.push('message');
          if (typeof event.data === 'string') {
            __webSceneWebSocketTest.text = event.data;
            socket.send(new Uint8Array([0, 1, 254, 255]));
          } else {
            __webSceneWebSocketTest.binary =
              Array.from(new Uint8Array(event.data));
            socket.close(1000, 'complete');
          }
        });
        socket.addEventListener('error', event => {
          __webSceneWebSocketTest.error = event.message || 'error';
        });
        socket.addEventListener('close', event => {
          __webSceneWebSocketTest.events.push('close');
          __webSceneWebSocketTest.closed = true;
          __webSceneWebSocketTest.closeCode = event.code;
          __webSceneWebSocketTest.closeReason = event.reason;
        });
    )JS";
    execute(engine, source, "native-websocket-browser-api.js");

    std::string state;
    for (auto attempt = 0; attempt < 500; ++attempt) {
        state = evaluate(
            engine,
            "globalThis.__webSceneWebSocketTest",
            "native-websocket-browser-api-state.js");
        if (state.find("\"closed\":true") != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(
        state.find("\"events\":[\"open\",\"message\",\"message\",\"close\"]")
                != std::string::npos
            && state.find("\"text\":\"native-text\"") != std::string::npos
            && state.find("\"binary\":[0,1,254,255]") != std::string::npos
            && state.find("\"closeCode\":1000") != std::string::npos
            && state.find("\"closeReason\":\"complete\"") != std::string::npos
            && state.find("\"error\":null") != std::string::npos,
        "browser WebSocket API did not complete text/binary/close echo lifecycle");
    {
        std::lock_guard lock(server_state_mutex);
        require(
            observed_origin == "https://websocket-origin.test",
            "browser WebSocket API did not forward the document origin");
    }

    webscene_engine_destroy(engine);
    server.stop();
    require(ix::uninitNetSystem(), "IXWebSocket network cleanup failed");
}

} // namespace

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
        webscene_engine_get_build_features() == 0,
        "ordinary build unexpectedly advertised certification telemetry");
#endif
    require(webscene_engine_prewarm() != 0, "V8 prewarm failed");
    test_interop_callback_notification();
    test_shared_isolate_reuses_destroyed_context_slot();
    test_flex_baseline_uses_host_font_metrics();
    test_viewport_hit_testing_traverses_zero_height_document_root();
    test_animation_runtime_is_cold_for_static_nodes();
    test_textual_style_state_is_cold_and_copy_on_write();
    test_table_and_form_state_are_cold_for_ordinary_nodes();
    test_document_clear_releases_and_reinitializes_node_pool();
    test_compact_attribute_collection_preserves_map_semantics();
    test_out_of_flow_client_geometry_reuse_is_scoped();
    test_screen_tracks_viewport();
    test_zero_command_engine_starts_with_clean_scene();
    test_parallel_resource_prefetch();
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
    test_dynamic_stylesheet_custom_properties_preserve_cascade_order();
    test_persistent_compilation_cache_reuse();
    test_executed_compilation_units_enrich_persistent_cache();
    test_process_wide_compilation_single_flight();
    auto* engine = webscene_engine_create(64);
    require(engine != nullptr, "engine creation failed");
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
    test_monaco_browser_primitives(engine);
    test_monaco_view_line_dom_mutations(engine);
    test_class_list_is_same_live_object(engine);
    test_visibility_inherits_for_computed_style_and_focus(engine);
    test_hover_specificity_preserves_visible_theme_icon(engine);
    test_hover_invalidation_updates_functional_and_sibling_subjects(engine);
    test_single_fractional_grid_track_stays_one_column(engine);
    test_calc_percent_with_pixel_offset(engine);
    test_flex_basis_reserves_fixed_track(engine);
    test_flex_flow_shorthand_controls_layout_and_cssom(engine);
    test_font_relative_box_lengths_follow_inherited_font_context(engine);
    test_floats_share_a_bounded_formatting_line(engine);
    test_wrapped_flex_resolves_each_line_independently(engine);
    test_zero_height_flex_item_grows_and_hit_tests_descendants(engine);
    test_empty_non_growing_flex_item_collapses_main_axis(engine);
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
    test_native_mouseup_honors_immediate_propagation_stop(engine);
    test_component_library_dom_discovery_primitives(engine);
    test_dom_selector_apis_throw_syntax_error_for_invalid_selectors(engine);
    test_dropdown_runtime_primitives(engine);
    test_input_dispatch_failures_are_attributed_and_consumable(engine);
    test_animation_frame_dispatch_is_attributed();
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
    return 0;
}
