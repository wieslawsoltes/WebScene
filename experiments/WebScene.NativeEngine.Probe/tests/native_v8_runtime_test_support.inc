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
    if (webscene_engine_execute_script(
            engine,
            source.data(), source.size(),
            name.data(), name.size()) == 0) {
        fail("script was rejected: " + std::string(name));
    }
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

struct interop_completion_probe final {
    std::mutex mutex;
    std::condition_variable ready;
    uint64_t operation_id{0};
};

void interop_completed(void* user_data, uint64_t operation_id)
{
    auto& probe = *static_cast<interop_completion_probe*>(user_data);
    {
        std::lock_guard lock(probe.mutex);
        probe.operation_id = operation_id;
        // The probe is stack-owned by the waiter. Notify before releasing the
        // mutex so a completion that wins the race with wait_for cannot leave
        // the callback touching a condition_variable the waiter has destroyed.
        probe.ready.notify_all();
    }
}

const webscene_interop_result_view_v3* invoke_binary(
    webscene_engine* engine,
    std::string_view source,
    std::string_view name)
{
    interop_completion_probe probe;
    const webscene_interop_evaluate_request_v3 request{
        static_cast<uint32_t>(sizeof(webscene_interop_evaluate_request_v3)),
        3U,
        source.data(),
        source.size(),
        name.data(),
        name.size(),
        0U,
        0U};
    const auto operation_id = webscene_engine_begin_evaluate_v3(
        engine,
        &request,
        interop_completed,
        &probe);
    require(operation_id != 0U, "binary invocation was rejected");
    {
        std::unique_lock lock(probe.mutex);
        require(
            probe.ready.wait_for(
                lock,
                std::chrono::seconds(10),
                [&probe] { return probe.operation_id != 0U; }),
            "binary invocation did not complete");
    }
    require(
        probe.operation_id == operation_id,
        "binary invocation completed with a different operation ID");
    const auto* result = webscene_engine_take_invoke_result_v3(
        engine,
        operation_id);
    require(result != nullptr, "binary invocation result was unavailable");
    require(
        webscene_engine_take_invoke_result_v3(engine, operation_id) == nullptr,
        "binary invocation result could be taken more than once");
    return result;
}

void append_json_string(std::string_view value, std::string& output)
{
    constexpr char hex[] = "0123456789abcdef";
    output.push_back('"');
    for (const auto byte : value) {
        const auto character = static_cast<unsigned char>(byte);
        switch (character) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (character < 0x20U) {
                output += "\\u00";
                output.push_back(hex[character >> 4U]);
                output.push_back(hex[character & 0x0FU]);
            } else {
                output.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    output.push_back('"');
}

std::string_view interop_utf8(
    const webscene_interop_result_view_v3& view,
    uint32_t offset,
    uint32_t length)
{
    require(
        offset <= view.utf8_byte_count
            && length <= view.utf8_byte_count - offset
            && (length == 0U || view.utf8_bytes != nullptr),
        "binary evaluation contained an invalid UTF-8 range");
    return {view.utf8_bytes + offset, length};
}

void append_json_value(
    const webscene_interop_result_view_v3& view,
    uint32_t value_index,
    std::string& output)
{
    require(
        value_index < view.value_count && view.values != nullptr,
        "binary evaluation contained an invalid value index");
    const auto& value = view.values[value_index];
    const auto require_edges = [&] {
        require(
            value.offset <= view.edge_count
                && value.length <= view.edge_count - value.offset
                && (value.length == 0U || view.edges != nullptr),
            "binary evaluation contained an invalid edge range");
    };
    switch (value.kind) {
    case WEBSCENE_INTEROP_VALUE_UNDEFINED_V3:
    case WEBSCENE_INTEROP_VALUE_NULL_V3:
        output += "null";
        return;
    case WEBSCENE_INTEROP_VALUE_BOOLEAN_V3:
        output += value.payload == 0U ? "false" : "true";
        return;
    case WEBSCENE_INTEROP_VALUE_NUMBER_V3: {
        const auto number = std::bit_cast<double>(value.payload);
        if (!std::isfinite(number)) {
            output += "null";
            return;
        }
        if (number == 0.0) {
            output.push_back('0');
            return;
        }
        std::array<char, 64> buffer{};
        const auto converted = std::to_chars(
            buffer.data(),
            buffer.data() + buffer.size(),
            number,
            std::chars_format::general);
        require(
            converted.ec == std::errc{},
            "binary evaluation number could not be formatted");
        output.append(buffer.data(), converted.ptr);
        return;
    }
    case WEBSCENE_INTEROP_VALUE_STRING_V3:
        append_json_string(
            interop_utf8(view, value.offset, value.length),
            output);
        return;
    case WEBSCENE_INTEROP_VALUE_HANDLE_V3:
        output += "{\"__webSceneHandle\":";
        output += std::to_string(value.payload);
        output.push_back('}');
        return;
    case WEBSCENE_INTEROP_VALUE_ARRAY_V3:
        require_edges();
        output.push_back('[');
        for (uint32_t index = 0; index < value.length; ++index) {
            if (index != 0U) output.push_back(',');
            append_json_value(
                view,
                view.edges[value.offset + index].value_index,
                output);
        }
        output.push_back(']');
        return;
    case WEBSCENE_INTEROP_VALUE_OBJECT_V3:
        require_edges();
        output.push_back('{');
        for (uint32_t index = 0; index < value.length; ++index) {
            if (index != 0U) output.push_back(',');
            const auto& edge = view.edges[value.offset + index];
            append_json_string(
                interop_utf8(view, edge.name_offset, edge.name_length),
                output);
            output.push_back(':');
            append_json_value(view, edge.value_index, output);
        }
        output.push_back('}');
        return;
    default:
        fail("binary evaluation contained an unsupported value kind");
    }
}

void release_interop_result(
    const webscene_interop_result_view_v3* result)
{
    if (result == nullptr) return;
    const auto lease_id = result->lease_id;
    webscene_interop_result_release_v3(result, lease_id);
}

std::string evaluate(
    webscene_engine* engine,
    std::string_view source,
    std::string_view name)
{
    const auto* result = invoke_binary(engine, source, name);
    require(
        result->struct_size >= sizeof(webscene_interop_result_view_v3)
            && result->version == 3U,
        "binary evaluation returned an invalid result header");
    if (result->status != WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3) {
        const std::string error{
            result->error_bytes == nullptr ? "" : result->error_bytes,
            result->error_byte_count};
        release_interop_result(result);
        fail("evaluation failed: " + error);
    }
    std::string json;
    append_json_value(*result, result->root_value_index, json);
    release_interop_result(result);
    return json;
}

std::string evaluate_until_equals(
    webscene_engine* engine,
    std::string_view source,
    std::string_view name,
    std::string_view expected,
    int maximum_attempts = 250)
{
    std::string result;
    for (auto attempt = 0; attempt < maximum_attempts; ++attempt) {
        result = evaluate(engine, source, name);
        if (result == expected) return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return result;
}

const webscene_interop_result_view_v3* invoke_generated(
    webscene_engine* engine,
    const webscene_interop_invoke_request_v3& request)
{
    interop_completion_probe probe;
    const auto operation_id = webscene_engine_begin_invoke_v3(
        engine,
        &request,
        interop_completed,
        &probe);
    require(
        operation_id != 0U,
        "generated binary invocation was rejected: " + last_error(engine));
    {
        std::unique_lock lock(probe.mutex);
        require(
            probe.ready.wait_for(
                lock,
                std::chrono::seconds(10),
                [&probe] { return probe.operation_id != 0U; }),
            "generated binary invocation did not complete");
    }
    require(
        probe.operation_id == operation_id,
        "generated binary invocation completed with a different operation ID");
    const auto* result = webscene_engine_take_invoke_result_v3(
        engine,
        operation_id);
    require(
        result != nullptr,
        "generated binary invocation result was unavailable");
    return result;
}

void require_generated_rejected(
    webscene_engine* engine,
    const webscene_interop_invoke_request_v3& request,
    std::string_view message)
{
    interop_completion_probe probe;
    require(
        webscene_engine_begin_invoke_v3(
            engine,
            &request,
            interop_completed,
            &probe) == 0U,
        message);
}
