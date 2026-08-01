void test_generated_binary_invocation_uses_tagged_arguments(
    webscene_engine* engine)
{
    webscene_runtime_work_metrics work_before{
        sizeof(webscene_runtime_work_metrics)};
    require(
        webscene_engine_set_runtime_work_metrics_enabled(engine, 1U) != 0
            && webscene_engine_get_runtime_work_metrics(engine, &work_before)
                != 0,
        "runtime work metrics were unavailable before generated interop");
    execute_and_wait(
        engine,
        R"JS(
          globalThis.__binaryHost = {
            updates: [],
            onRealtimeUpdate(subscriber, bar) {
              this.updates.push({ subscriber, bar });
              return bar.close;
            },
            invokeCallback(callback) { return callback(4); },
            updateCount() { return this.updates.length; },
            immediatePromise() { return Promise.resolve(73.25); },
            delayedPromise() {
              return new Promise(resolve =>
                setTimeout(() => resolve(91.5), 5));
            },
            rejectedPromise() {
              return new Promise((_resolve, reject) =>
                setTimeout(() => reject(new Error("binary rejection")), 5));
            },
            cancellablePromise() {
              return new Promise(resolve =>
                setTimeout(() => resolve(1), 50));
            }
          };
          globalThis.__compatibilityHandles = new Map([
            [17, value => value * 2]
          ]);
          globalThis.__webSceneDotNetInterop = {
            getHandleValue(handle) {
              const value = globalThis.__compatibilityHandles.get(handle);
              if (value === undefined) throw new Error("stale compatibility handle");
              return value;
            }
          };
        )JS",
        "generated-binary-host.js");

    const std::array<webscene_interop_value_v3, 1> empty_arguments{{
        {WEBSCENE_INTEROP_VALUE_ARRAY_V3, 0U, 0U, 0U, 0U}}};
    const std::string_view global_name{"__binaryHost"};
    const webscene_interop_invoke_request_v3 get_global{
        static_cast<uint32_t>(sizeof(webscene_interop_invoke_request_v3)),
        3U,
        WEBSCENE_INTEROP_GET_GLOBAL_V3,
        0U,
        0U,
        global_name.data(),
        global_name.size(),
        nullptr,
        0U,
        empty_arguments.data(),
        empty_arguments.size(),
        nullptr,
        0U,
        nullptr,
        0U,
        0U,
        WEBSCENE_INTEROP_RESULT_RETAINED_HANDLE_V3};

    auto malformed = get_global;
    malformed.struct_size =
        static_cast<uint32_t>(sizeof(webscene_interop_invoke_request_v3) - 1U);
    require_generated_rejected(
        engine,
        malformed,
        "generated invocation accepted a truncated request header");
    malformed = get_global;
    malformed.global_name_length = 1024U * 1024U + 1U;
    require_generated_rejected(
        engine,
        malformed,
        "generated invocation accepted an oversized global name");
    malformed = get_global;
    malformed.global_name = nullptr;
    require_generated_rejected(
        engine,
        malformed,
        "generated invocation accepted a missing global-name buffer");
    malformed = get_global;
    malformed.values = nullptr;
    require_generated_rejected(
        engine,
        malformed,
        "generated invocation accepted a missing value table");

    const auto* global_result = invoke_generated(engine, get_global);
    require(
        global_result->status == WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3
            && global_result->value_count == 1U
            && global_result->values[0].kind
                == WEBSCENE_INTEROP_VALUE_HANDLE_V3,
        "generated global lookup did not return a retained handle");
    const auto handle = global_result->values[0].payload;
    release_interop_result(global_result);

    constexpr std::string_view subscriber{"subscriber-7"};
    constexpr std::string_view names{"timeopenhighlowclosevolume"};
    const std::array<webscene_interop_value_v3, 9> values{{
        {WEBSCENE_INTEROP_VALUE_ARRAY_V3, 0U, 0U, 2U, 0U},
        {WEBSCENE_INTEROP_VALUE_STRING_V3, 0U, 0U,
            static_cast<uint32_t>(subscriber.size()), 0U},
        {WEBSCENE_INTEROP_VALUE_OBJECT_V3, 0U, 2U, 6U, 0U},
        {WEBSCENE_INTEROP_VALUE_NUMBER_V3, 0U, 0U, 0U,
            std::bit_cast<uint64_t>(1785413968000.0)},
        {WEBSCENE_INTEROP_VALUE_NUMBER_V3, 0U, 0U, 0U,
            std::bit_cast<uint64_t>(101.25)},
        {WEBSCENE_INTEROP_VALUE_NUMBER_V3, 0U, 0U, 0U,
            std::bit_cast<uint64_t>(103.5)},
        {WEBSCENE_INTEROP_VALUE_NUMBER_V3, 0U, 0U, 0U,
            std::bit_cast<uint64_t>(100.75)},
        {WEBSCENE_INTEROP_VALUE_NUMBER_V3, 0U, 0U, 0U,
            std::bit_cast<uint64_t>(102.875)},
        {WEBSCENE_INTEROP_VALUE_NUMBER_V3, 0U, 0U, 0U,
            std::bit_cast<uint64_t>(42.5)}
    }};
    const std::array<webscene_interop_edge_v3, 8> edges{{
        {0U, 0U, 1U, 0U},
        {0U, 0U, 2U, 0U},
        {static_cast<uint32_t>(subscriber.size()), 4U, 3U, 0U},
        {static_cast<uint32_t>(subscriber.size() + 4U), 4U, 4U, 0U},
        {static_cast<uint32_t>(subscriber.size() + 8U), 4U, 5U, 0U},
        {static_cast<uint32_t>(subscriber.size() + 12U), 3U, 6U, 0U},
        {static_cast<uint32_t>(subscriber.size() + 15U), 5U, 7U, 0U},
        {static_cast<uint32_t>(subscriber.size() + 20U), 6U, 8U, 0U}
    }};
    const auto utf8 = std::string(subscriber) + std::string(names);
    constexpr std::string_view update_member{"onRealtimeUpdate"};
    const webscene_interop_invoke_request_v3 update{
        static_cast<uint32_t>(sizeof(webscene_interop_invoke_request_v3)),
        3U,
        WEBSCENE_INTEROP_INVOKE_MEMBER_V3,
        0U,
        handle,
        nullptr,
        0U,
        update_member.data(),
        update_member.size(),
        values.data(),
        values.size(),
        edges.data(),
        edges.size(),
        utf8.data(),
        utf8.size(),
        0U,
        WEBSCENE_INTEROP_RESULT_VALUE_V3};

    malformed = update;
    auto malformed_values = values;
    malformed_values[1].offset =
        static_cast<uint32_t>(utf8.size() + 1U);
    malformed.values = malformed_values.data();
    require_generated_rejected(
        engine,
        malformed,
        "generated invocation accepted an invalid string range");
    malformed = update;
    malformed_values = values;
    malformed_values[2].length =
        static_cast<uint32_t>(edges.size() + 1U);
    malformed.values = malformed_values.data();
    require_generated_rejected(
        engine,
        malformed,
        "generated invocation accepted an invalid object edge range");
    malformed = update;
    auto malformed_edges = edges;
    malformed_edges[0].value_index =
        static_cast<uint32_t>(values.size());
    malformed.edges = malformed_edges.data();
    require_generated_rejected(
        engine,
        malformed,
        "generated invocation accepted an invalid child index");
    malformed = update;
    malformed_edges = edges;
    malformed_edges[2].name_length =
        static_cast<uint32_t>(utf8.size() + 1U);
    malformed.edges = malformed_edges.data();
    require_generated_rejected(
        engine,
        malformed,
        "generated invocation accepted an invalid property-name range");

    const auto* update_result = invoke_generated(engine, update);
    require(
        update_result->status == WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3
            && update_result->value_count == 1U
            && update_result->values[0].kind
                == WEBSCENE_INTEROP_VALUE_NUMBER_V3
            && std::bit_cast<double>(update_result->values[0].payload)
                == 102.875,
        "generated realtime update did not receive tagged DTO arguments");
    release_interop_result(update_result);

    constexpr std::string_view count_member{"updateCount"};
    auto count = get_global;
    count.operation = WEBSCENE_INTEROP_INVOKE_MEMBER_V3;
    count.target_handle = handle;
    count.global_name = nullptr;
    count.global_name_length = 0U;
    count.member_name = count_member.data();
    count.member_name_length = count_member.size();
    count.result_mode = WEBSCENE_INTEROP_RESULT_VALUE_V3;
    const auto* count_result = invoke_generated(engine, count);
    require(
        count_result->status == WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3
            && count_result->values[0].kind
                == WEBSCENE_INTEROP_VALUE_NUMBER_V3
            && std::bit_cast<double>(count_result->values[0].payload) == 1.0,
        "generated member invocation did not preserve its receiver");
    release_interop_result(count_result);

    const std::array<webscene_interop_value_v3, 2>
        compatibility_handle_values{{
            {WEBSCENE_INTEROP_VALUE_ARRAY_V3, 0U, 0U, 1U, 0U},
            {WEBSCENE_INTEROP_VALUE_HANDLE_V3, 0U, 0U, 0U, 17U}
        }};
    const std::array<webscene_interop_edge_v3, 1>
        compatibility_handle_edges{{{0U, 0U, 1U, 0U}}};
    constexpr std::string_view callback_member{"invokeCallback"};
    auto callback = count;
    callback.member_name = callback_member.data();
    callback.member_name_length = callback_member.size();
    callback.values = compatibility_handle_values.data();
    callback.value_count = compatibility_handle_values.size();
    callback.edges = compatibility_handle_edges.data();
    callback.edge_count = compatibility_handle_edges.size();
    const auto* callback_result = invoke_generated(engine, callback);
    require(
        callback_result->status == WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3
            && callback_result->values[0].kind
                == WEBSCENE_INTEROP_VALUE_NUMBER_V3
            && std::bit_cast<double>(callback_result->values[0].payload)
                == 8.0,
        "generated invocation could not resolve a host-provided handle");
    release_interop_result(callback_result);

    constexpr std::string_view promise_member{"immediatePromise"};
    auto promise = count;
    promise.flags = WEBSCENE_INTEROP_CALL_AWAIT_PROMISE_V3;
    promise.member_name = promise_member.data();
    promise.member_name_length = promise_member.size();
    const auto* promise_result = invoke_generated(engine, promise);
    require(
        promise_result->status == WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3
            && promise_result->values[0].kind
                == WEBSCENE_INTEROP_VALUE_NUMBER_V3
            && std::bit_cast<double>(promise_result->values[0].payload) == 73.25,
        "generated immediate promise did not settle into its operation");
    release_interop_result(promise_result);

    constexpr std::string_view delayed_promise_member{"delayedPromise"};
    promise.member_name = delayed_promise_member.data();
    promise.member_name_length = delayed_promise_member.size();
    const auto* delayed_promise_result = invoke_generated(engine, promise);
    require(
        delayed_promise_result->status
                == WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3
            && delayed_promise_result->values[0].kind
                == WEBSCENE_INTEROP_VALUE_NUMBER_V3
            && std::bit_cast<double>(
                delayed_promise_result->values[0].payload) == 91.5,
        "generated pending promise did not complete its native operation");
    release_interop_result(delayed_promise_result);

    constexpr std::string_view rejected_promise_member{"rejectedPromise"};
    promise.member_name = rejected_promise_member.data();
    promise.member_name_length = rejected_promise_member.size();
    const auto* rejected_promise_result = invoke_generated(engine, promise);
    require(
        rejected_promise_result->status
                == WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V3
            && rejected_promise_result->error_byte_count != 0U,
        "generated rejected promise did not fail its native operation");
    release_interop_result(rejected_promise_result);

    webscene_interop_pool_metrics_v3 before_ready_cancel{};
    before_ready_cancel.struct_size =
        static_cast<uint32_t>(sizeof(before_ready_cancel));
    require(
        webscene_engine_get_interop_pool_metrics_v3(
            engine,
            &before_ready_cancel) != 0,
        "generated ready-result cancellation metrics were unavailable");
    interop_completion_probe ready_cancel_probe;
    const auto ready_cancel_operation =
        webscene_engine_begin_invoke_v3(
            engine,
            &count,
            interop_completed,
            &ready_cancel_probe);
    require(
        ready_cancel_operation != 0U,
        "generated ready-result cancellation invocation was rejected");
    {
        std::unique_lock lock(ready_cancel_probe.mutex);
        require(
            ready_cancel_probe.ready.wait_for(
                lock,
                std::chrono::seconds(10),
                [&ready_cancel_probe] {
                    return ready_cancel_probe.operation_id != 0U;
                }),
            "generated ready-result cancellation invocation did not complete");
    }
    require(
        webscene_engine_cancel_invoke_v3(
            engine,
            ready_cancel_operation) != 0U,
        "generated ready result could not be cancelled");
    webscene_interop_pool_metrics_v3 after_ready_cancel{};
    after_ready_cancel.struct_size =
        static_cast<uint32_t>(sizeof(after_ready_cancel));
    for (auto attempt = 0; attempt < 250; ++attempt) {
        require(
            webscene_engine_get_interop_pool_metrics_v3(
                engine,
                &after_ready_cancel) != 0,
            "generated ready-result cancellation metrics became unavailable");
        if (after_ready_cancel.active_operation_slots == 0U
            && after_ready_cancel.outstanding_results
                == before_ready_cancel.outstanding_results) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        after_ready_cancel.active_operation_slots == 0U
            && after_ready_cancel.outstanding_results
                == before_ready_cancel.outstanding_results
            && webscene_engine_take_invoke_result_v3(
                engine,
                ready_cancel_operation) == nullptr,
        "cancelling a ready generated result bypassed its result pool");

    constexpr std::string_view cancellable_promise_member{
        "cancellablePromise"};
    promise.member_name = cancellable_promise_member.data();
    promise.member_name_length = cancellable_promise_member.size();
    interop_completion_probe cancelled_probe;
    const auto cancelled_operation =
        webscene_engine_begin_invoke_v3(
            engine,
            &promise,
            interop_completed,
            &cancelled_probe);
    require(
        cancelled_operation != 0U
            && webscene_engine_cancel_invoke_v3(
                engine,
                cancelled_operation) != 0U,
        "generated pending promise could not be cancelled");
    std::this_thread::sleep_for(std::chrono::milliseconds(75));
    {
        std::lock_guard lock(cancelled_probe.mutex);
        require(
            cancelled_probe.operation_id == 0U
                && webscene_engine_take_invoke_result_v3(
                    engine,
                    cancelled_operation) == nullptr,
            "cancelled generated promise published a completion");
    }

    auto release = count;
    release.operation = WEBSCENE_INTEROP_RELEASE_HANDLE_V3;
    release.member_name = nullptr;
    release.member_name_length = 0U;
    release.result_mode = WEBSCENE_INTEROP_RESULT_VOID_V3;
    const auto* release_result = invoke_generated(engine, release);
    require(
        release_result->status == WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3,
        "generated handle release failed");
    release_interop_result(release_result);

    const auto* stale_result = invoke_generated(engine, count);
    require(
        stale_result->status == WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V3,
        "generated invocation accepted a stale handle");
    release_interop_result(stale_result);
    webscene_runtime_work_metrics work_after{
        sizeof(webscene_runtime_work_metrics)};
    require(
        webscene_engine_get_runtime_work_metrics(engine, &work_after) != 0
            && work_after.generated_invoke_calls
                > work_before.generated_invoke_calls
            && work_after.generated_request_bytes
                > work_before.generated_request_bytes,
        "accepted generated operations were not attributed to the binary path");
}

void test_generated_binary_cross_context_promise()
{
    auto* engine = webscene_engine_create(0);
    require(
        engine != nullptr,
        "cross-context promise engine creation failed");
    execute_and_wait(
        engine,
        R"JS(
          const frame = document.createElement("iframe");
          frame.style.display = "none";
          document.body.appendChild(frame);
          const frameDocument = frame.contentDocument;
          frameDocument.open();
          frameDocument.write(
            "<script>parent.__crossContextPromise = "
            + "new Promise(resolve => setTimeout(() => resolve(84.75), 100))"
            + "<\/script>");
          frameDocument.close();
          globalThis.__crossContextHost = {
            ready() { return globalThis.__crossContextPromise; }
          };
        )JS",
        "generated-binary-cross-context-host.js");

    const std::array<webscene_interop_value_v3, 1> empty_arguments{{
        {WEBSCENE_INTEROP_VALUE_ARRAY_V3, 0U, 0U, 0U, 0U}}};
    constexpr std::string_view global_name{"__crossContextHost"};
    const webscene_interop_invoke_request_v3 get_global{
        static_cast<uint32_t>(sizeof(webscene_interop_invoke_request_v3)),
        3U,
        WEBSCENE_INTEROP_GET_GLOBAL_V3,
        0U,
        0U,
        global_name.data(),
        global_name.size(),
        nullptr,
        0U,
        empty_arguments.data(),
        empty_arguments.size(),
        nullptr,
        0U,
        nullptr,
        0U,
        0U,
        WEBSCENE_INTEROP_RESULT_RETAINED_HANDLE_V3};
    const auto* global_result = invoke_generated(engine, get_global);
    require(
        global_result->status == WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3
            && global_result->value_count == 1U
            && global_result->values[0].kind
                == WEBSCENE_INTEROP_VALUE_HANDLE_V3,
        "cross-context promise host lookup failed");
    const auto handle = global_result->values[0].payload;
    release_interop_result(global_result);

    constexpr std::string_view member_name{"ready"};
    const webscene_interop_invoke_request_v3 invoke{
        static_cast<uint32_t>(sizeof(webscene_interop_invoke_request_v3)),
        3U,
        WEBSCENE_INTEROP_INVOKE_MEMBER_V3,
        WEBSCENE_INTEROP_CALL_AWAIT_PROMISE_V3,
        handle,
        nullptr,
        0U,
        member_name.data(),
        member_name.size(),
        empty_arguments.data(),
        empty_arguments.size(),
        nullptr,
        0U,
        nullptr,
        0U,
        0U,
        WEBSCENE_INTEROP_RESULT_VALUE_V3};
    const auto* result = invoke_generated(engine, invoke);
    require(
        result->status == WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3
            && result->value_count == 1U
            && result->values[0].kind
                == WEBSCENE_INTEROP_VALUE_NUMBER_V3
            && std::bit_cast<double>(result->values[0].payload) == 84.75,
        "generated cross-context promise did not settle into its operation");
    release_interop_result(result);
    webscene_engine_destroy(engine);
}

void test_binary_interop_result_is_leased_and_pooled(webscene_engine* engine)
{
    webscene_interop_pool_metrics_v3 before{};
    before.struct_size = static_cast<uint32_t>(sizeof(before));
    require(
        webscene_engine_get_interop_pool_metrics_v3(engine, &before) != 0,
        "binary interop metrics were unavailable");

    const auto* result = invoke_binary(
        engine,
        R"JS(({
          symbol: "AAPL",
          values: [true, 187.5, { __webSceneHandle: 42 }],
          missing: { __webSceneUndefined: true }
        }))JS",
        "native-binary-interop-result.js");
    require(
        result->struct_size == sizeof(webscene_interop_result_view_v3)
            && result->version == 3U
            && result->status == WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3
            && result->value_count >= 7U
            && result->root_value_index < result->value_count,
        "binary interop result header was invalid");
    const auto& root = result->values[result->root_value_index];
    require(
        root.kind == WEBSCENE_INTEROP_VALUE_OBJECT_V3
            && root.offset <= result->edge_count
            && root.length <= result->edge_count - root.offset,
        "binary interop root object was invalid");

    bool saw_symbol = false;
    bool saw_values = false;
    bool saw_missing = false;
    for (uint32_t index = 0; index < root.length; ++index) {
        const auto& edge = result->edges[root.offset + index];
        require(
            edge.name_offset <= result->utf8_byte_count
                && edge.name_length
                    <= result->utf8_byte_count - edge.name_offset
                && edge.value_index < result->value_count,
            "binary interop object edge was invalid");
        const std::string_view name(
            result->utf8_bytes + edge.name_offset,
            edge.name_length);
        const auto& value = result->values[edge.value_index];
        if (name == "symbol") {
            saw_symbol = value.kind == WEBSCENE_INTEROP_VALUE_STRING_V3
                && std::string_view(
                    result->utf8_bytes + value.offset,
                    value.length) == "AAPL";
        } else if (name == "values") {
            saw_values = value.kind == WEBSCENE_INTEROP_VALUE_ARRAY_V3
                && value.length == 3U;
        } else if (name == "missing") {
            saw_missing = value.kind == WEBSCENE_INTEROP_VALUE_UNDEFINED_V3;
        }
    }
    require(
        saw_symbol && saw_values && saw_missing,
        "binary interop result lost tagged object values");

    webscene_interop_pool_metrics_v3 leased{};
    leased.struct_size = static_cast<uint32_t>(sizeof(leased));
    require(
        webscene_engine_get_interop_pool_metrics_v3(engine, &leased) != 0
            && leased.outstanding_results
                == before.outstanding_results + 1U,
        "binary interop result lease was not attributed");

    const auto released_lease_id = result->lease_id;
    release_interop_result(result);
    webscene_interop_result_release_v3(result, released_lease_id);
    webscene_interop_pool_metrics_v3 released{};
    released.struct_size = static_cast<uint32_t>(sizeof(released));
    require(
        webscene_engine_get_interop_pool_metrics_v3(engine, &released) != 0
            && released.outstanding_results == before.outstanding_results
            && released.pooled_bytes >= before.pooled_bytes,
        "binary interop result was not returned to its pool");

    const auto* reused = invoke_binary(
        engine,
        R"JS(["MSFT", 415.25])JS",
        "native-binary-interop-reuse.js");
    require(
        reused == result && reused->lease_id != released_lease_id,
        "binary interop pool did not reuse the released result record "
        "with a fresh lease generation");
    webscene_interop_result_release_v3(result, released_lease_id);
    webscene_interop_pool_metrics_v3 stale_release{};
    stale_release.struct_size =
        static_cast<uint32_t>(sizeof(stale_release));
    require(
        webscene_engine_get_interop_pool_metrics_v3(
            engine,
            &stale_release) != 0
            && stale_release.outstanding_results
                == before.outstanding_results + 1U
            && reused->status == WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3,
        "a stale release generation released the reused live result");
    release_interop_result(reused);
    webscene_interop_pool_metrics_v3 after{};
    after.struct_size = static_cast<uint32_t>(sizeof(after));
    require(
        webscene_engine_get_interop_pool_metrics_v3(engine, &after) != 0
            && after.pool_hits > before.pool_hits,
        "binary interop result pool did not reuse a released arena");
}

void test_binary_interop_preserves_json_edge_semantics(
    webscene_engine* engine)
{
    const auto* result = invoke_binary(
        engine,
        R"JS(({
          nullValue: null,
          unicode: "AAPL-€-𐐷",
          date: new Date(0),
          numbers: [NaN, Infinity, -Infinity],
          ordered: { z: 1, a: 2 }
        }))JS",
        "native-binary-interop-semantics.js");
    require(
        result->status == WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3,
        "binary edge-semantics result failed");
    const auto& root = result->values[result->root_value_index];
    require(
        root.kind == WEBSCENE_INTEROP_VALUE_OBJECT_V3
            && root.length == 5U,
        "binary edge-semantics root was invalid");
    std::vector<std::string> names;
    for (uint32_t index = 0; index < root.length; ++index) {
        const auto& edge = result->edges[root.offset + index];
        names.emplace_back(
            result->utf8_bytes + edge.name_offset,
            edge.name_length);
        const auto& value = result->values[edge.value_index];
        if (names.back() == "nullValue") {
            require(
                value.kind == WEBSCENE_INTEROP_VALUE_NULL_V3,
                "binary null was not preserved");
        } else if (names.back() == "unicode") {
            require(
                value.kind == WEBSCENE_INTEROP_VALUE_STRING_V3
                    && std::string_view(
                        result->utf8_bytes + value.offset,
                        value.length) == "AAPL-€-𐐷",
                "binary Unicode UTF-8 was not preserved");
        } else if (names.back() == "date") {
            require(
                value.kind == WEBSCENE_INTEROP_VALUE_STRING_V3
                    && std::string_view(
                        result->utf8_bytes + value.offset,
                        value.length)
                        == "1970-01-01T00:00:00.000Z",
                "binary Date conversion diverged from JSON");
        } else if (names.back() == "numbers") {
            require(
                value.kind == WEBSCENE_INTEROP_VALUE_ARRAY_V3
                    && value.length == 3U,
                "binary non-finite array was invalid");
            for (uint32_t item = 0; item < value.length; ++item) {
                const auto& child = result->values[
                    result->edges[value.offset + item].value_index];
                require(
                    child.kind == WEBSCENE_INTEROP_VALUE_NULL_V3,
                    "binary non-finite number did not match JSON null");
            }
        }
    }
    require(
        names == std::vector<std::string>{
            "nullValue",
            "unicode",
            "date",
            "numbers",
            "ordered"},
        "binary object property order changed");
    release_interop_result(result);

    const auto* undefined = invoke_binary(
        engine,
        "undefined",
        "native-binary-interop-undefined.js");
    require(
        undefined->status == WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3
            && undefined->values[undefined->root_value_index].kind
                == WEBSCENE_INTEROP_VALUE_UNDEFINED_V3,
        "binary root undefined was not tagged distinctly");
    release_interop_result(undefined);

    const auto* cyclic = invoke_binary(
        engine,
        "(() => { const value = {}; value.self = value; return value; })()",
        "native-binary-interop-cycle.js");
    require(
        cyclic->status == WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V3,
        "binary cyclic value did not fail explicitly");
    release_interop_result(cyclic);

    const auto* exception = invoke_binary(
        engine,
        "(() => { throw new Error('binary exception'); })()",
        "native-binary-interop-exception.js");
    require(
        exception->status == WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V3
            && exception->error_byte_count != 0U,
        "binary JavaScript exception did not become an error result");
    release_interop_result(exception);
}

void test_binary_interop_result_outlives_engine()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "binary lease engine creation failed");
    const auto* result = invoke_binary(
        engine,
        R"JS(({ value: "retained" }))JS",
        "native-binary-interop-retained.js");
    require(
        result->status == WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3,
        "retained binary result did not succeed");

    webscene_engine_destroy(engine);
    const auto& root = result->values[result->root_value_index];
    require(
        root.kind == WEBSCENE_INTEROP_VALUE_OBJECT_V3
            && root.length == 1U,
        "binary result became invalid when its engine was destroyed");
    const auto& property = result->edges[root.offset];
    const auto& value = result->values[property.value_index];
    require(
        value.kind == WEBSCENE_INTEROP_VALUE_STRING_V3
            && std::string_view(
                result->utf8_bytes + value.offset,
                value.length) == "retained",
        "binary result payload did not outlive its engine");
    release_interop_result(result);
}

void test_binary_interop_stress_when_requested(webscene_engine* engine)
{
    if (std::getenv("WEBSCENE_INTEROP_STRESS") == nullptr) return;

    constexpr size_t operation_count = 100'000U;
    constexpr size_t retained_lease_count = 32U;
    std::array<const webscene_interop_result_view_v3*, retained_lease_count>
        retained{};
    execute_and_wait(
        engine,
        R"JS(
          globalThis.__binaryStress = value => value % 3 === 0
            ? Promise.resolve({ ok: true, value })
            : ({ ok: true, value });
        )JS",
        "generated-binary-interop-stress-host.js");
    std::array<webscene_interop_value_v3, 2> direct_values{{
        {WEBSCENE_INTEROP_VALUE_ARRAY_V3, 0U, 0U, 1U, 0U},
        {WEBSCENE_INTEROP_VALUE_NUMBER_V3, 0U, 0U, 0U, 0U}
    }};
    const std::array<webscene_interop_edge_v3, 1> direct_edges{{
        {0U, 0U, 1U, 0U}
    }};
    constexpr std::string_view direct_global{"__binaryStress"};
    const webscene_interop_invoke_request_v3 direct_request{
        static_cast<uint32_t>(sizeof(webscene_interop_invoke_request_v3)),
        3U,
        WEBSCENE_INTEROP_INVOKE_GLOBAL_V3,
        WEBSCENE_INTEROP_CALL_AWAIT_PROMISE_V3,
        0U,
        direct_global.data(),
        direct_global.size(),
        nullptr,
        0U,
        direct_values.data(),
        direct_values.size(),
        direct_edges.data(),
        direct_edges.size(),
        nullptr,
        0U,
        0U,
        WEBSCENE_INTEROP_RESULT_VALUE_V3};
    for (size_t index = 0; index < operation_count; ++index) {
        const auto slot = index % retained.size();
        if (retained[slot] != nullptr) {
            release_interop_result(retained[slot]);
        }
        if (index % 2U == 0U) {
            retained[slot] = invoke_binary(
                engine,
                index % 4U == 0U
                    ? R"JS(({ ok: true, value: 42 }))JS"
                    : R"JS(({ nested: { values: [1, 2, 3, 4] } }))JS",
                "native-binary-interop-stress.js");
        } else {
            direct_values[1].payload =
                std::bit_cast<uint64_t>(static_cast<double>(index));
            retained[slot] = invoke_generated(engine, direct_request);
        }
    }
    for (const auto* result : retained) {
        release_interop_result(result);
    }

    webscene_interop_pool_metrics_v3 metrics{};
    metrics.struct_size = static_cast<uint32_t>(sizeof(metrics));
    require(
        webscene_engine_get_interop_pool_metrics_v3(engine, &metrics) != 0
            && metrics.outstanding_results == 0U
            && metrics.pooled_bytes <= 8U * 1024U * 1024U
            && metrics.high_water_outstanding_results
                >= retained_lease_count,
        "binary interop stress violated pool bounds or leaked a lease");
}

void notify_interop_callback_test(void* user_data)
{
    static_cast<std::atomic<uint32_t>*>(user_data)
        ->fetch_add(1U, std::memory_order_relaxed);
}

void test_binary_reverse_callback_is_leased_and_completed()
{
    std::atomic<uint32_t> notifications{0U};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.interop_callback_available_callback =
        notify_interop_callback_test;
    options.interop_callback_available_user_data = &notifications;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "binary reverse callback engine creation failed");

    constexpr std::string_view promise_name{"load"};
    constexpr std::string_view void_name{"update"};
    std::array<webscene_interop_value_v3, 10> registration_values{{
        {WEBSCENE_INTEROP_VALUE_ARRAY_V3, 0U, 0U, 1U, 0U},
        {WEBSCENE_INTEROP_VALUE_ARRAY_V3, 0U, 1U, 2U, 0U},
        {WEBSCENE_INTEROP_VALUE_ARRAY_V3, 0U, 3U, 3U, 0U},
        {WEBSCENE_INTEROP_VALUE_STRING_V3, 0U, 0U,
            static_cast<uint32_t>(promise_name.size()), 0U},
        {WEBSCENE_INTEROP_VALUE_NUMBER_V3, 0U, 0U, 0U,
            std::bit_cast<uint64_t>(7.0)},
        {WEBSCENE_INTEROP_VALUE_NUMBER_V3, 0U, 0U, 0U,
            std::bit_cast<uint64_t>(
                static_cast<double>(
                    WEBSCENE_INTEROP_CALLBACK_PROMISE_V3))},
        {WEBSCENE_INTEROP_VALUE_ARRAY_V3, 0U, 6U, 3U, 0U},
        {WEBSCENE_INTEROP_VALUE_STRING_V3,
            0U,
            static_cast<uint32_t>(promise_name.size()),
            static_cast<uint32_t>(void_name.size()), 0U},
        {WEBSCENE_INTEROP_VALUE_NUMBER_V3, 0U, 0U, 0U,
            std::bit_cast<uint64_t>(8.0)},
        {WEBSCENE_INTEROP_VALUE_NUMBER_V3, 0U, 0U, 0U,
            std::bit_cast<uint64_t>(
                static_cast<double>(
                    WEBSCENE_INTEROP_CALLBACK_VOID_V3))}
    }};
    std::array<webscene_interop_edge_v3, 9> registration_edges{{
        {0U, 0U, 1U, 0U},
        {0U, 0U, 2U, 0U},
        {0U, 0U, 6U, 0U},
        {0U, 0U, 3U, 0U},
        {0U, 0U, 4U, 0U},
        {0U, 0U, 5U, 0U},
        {0U, 0U, 7U, 0U},
        {0U, 0U, 8U, 0U},
        {0U, 0U, 9U, 0U}
    }};
    const auto registration_utf8 =
        std::string(promise_name) + std::string(void_name);
    const webscene_interop_invoke_request_v3 registration{
        static_cast<uint32_t>(sizeof(webscene_interop_invoke_request_v3)),
        3U,
        WEBSCENE_INTEROP_CREATE_CALLBACK_TARGET_V3,
        0U,
        41U,
        nullptr,
        0U,
        nullptr,
        0U,
        registration_values.data(),
        registration_values.size(),
        registration_edges.data(),
        registration_edges.size(),
        registration_utf8.data(),
        registration_utf8.size(),
        0U,
        WEBSCENE_INTEROP_RESULT_RETAINED_HANDLE_V3};
    const auto* registered = invoke_generated(engine, registration);
    require(
        registered->status == WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3
            && registered->value_count == 1U
            && registered->values[0].kind
                == WEBSCENE_INTEROP_VALUE_HANDLE_V3,
        "binary callback target registration failed: "
            + std::string(
                registered->error_bytes == nullptr
                    ? ""
                    : registered->error_bytes,
                registered->error_byte_count));
    const auto callback_handle = registered->values[0].payload;
    release_interop_result(registered);

    execute_and_wait(
        engine,
        "globalThis.__installManagedBridge = value => { "
        "globalThis.__managedBridge = value; };",
        "binary-reverse-install.js");
    std::array<webscene_interop_value_v3, 2> install_values{{
        {WEBSCENE_INTEROP_VALUE_ARRAY_V3, 0U, 0U, 1U, 0U},
        {WEBSCENE_INTEROP_VALUE_HANDLE_V3, 0U, 0U, 0U, callback_handle}
    }};
    const std::array<webscene_interop_edge_v3, 1> install_edges{{
        {0U, 0U, 1U, 0U}
    }};
    constexpr std::string_view install_name{"__installManagedBridge"};
    const webscene_interop_invoke_request_v3 install{
        static_cast<uint32_t>(sizeof(webscene_interop_invoke_request_v3)),
        3U,
        WEBSCENE_INTEROP_INVOKE_GLOBAL_V3,
        0U,
        0U,
        install_name.data(),
        install_name.size(),
        nullptr,
        0U,
        install_values.data(),
        install_values.size(),
        install_edges.data(),
        install_edges.size(),
        nullptr,
        0U,
        0U,
        WEBSCENE_INTEROP_RESULT_VOID_V3};
    const auto* installed = invoke_generated(engine, install);
    require(
        installed->status == WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3,
        "binary callback target could not be passed to JavaScript");
    release_interop_result(installed);

    const auto notifications_before =
        notifications.load(std::memory_order_relaxed);
    execute_and_wait(
        engine,
        "globalThis.__managedResult = -1;"
        "globalThis.__managedFunctionArgument = -1;"
        "globalThis.__managedBridge.load("
        "{ symbol: 'AAPL', nested: [1, true] }, "
        "value => { globalThis.__managedFunctionArgument = value; })"
        ".then(value => { globalThis.__managedResult = value; });"
        "globalThis.__managedBridge.update(1);"
        "globalThis.__managedBridge.update(2);",
        "binary-reverse-call.js");

    const webscene_interop_callback_view_v3* callback = nullptr;
    for (auto attempt = 0; attempt < 250 && callback == nullptr; ++attempt) {
        callback = webscene_engine_take_callback_v3(engine);
        if (callback == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(callback != nullptr, "binary reverse callback was not queued");
    require(
        callback->struct_size == sizeof(webscene_interop_callback_view_v3)
            && callback->version == 3U
            && callback->target_id == 41U
            && callback->method_id == 7U
            && callback->return_kind
                == WEBSCENE_INTEROP_CALLBACK_PROMISE_V3
            && callback->arguments_root < callback->value_count
            && callback->values[callback->arguments_root].kind
                == WEBSCENE_INTEROP_VALUE_ARRAY_V3
            && callback->values[callback->arguments_root].length == 2U,
        "binary reverse callback metadata or argument root was invalid");
    const auto& callback_root =
        callback->values[callback->arguments_root];
    const auto function_index =
        callback->edges[callback_root.offset + 1U].value_index;
    require(
        function_index < callback->value_count
            && callback->values[function_index].kind
                == WEBSCENE_INTEROP_VALUE_HANDLE_V3,
        "binary reverse callback did not retain a function argument");
    const auto function_handle =
        callback->values[function_index].payload;

    std::array<webscene_interop_value_v3, 2> function_values{{
        {WEBSCENE_INTEROP_VALUE_ARRAY_V3, 0U, 0U, 1U, 0U},
        {WEBSCENE_INTEROP_VALUE_NUMBER_V3, 0U, 0U, 0U,
            std::bit_cast<uint64_t>(19.5)}
    }};
    const std::array<webscene_interop_edge_v3, 1> function_edges{{
        {0U, 0U, 1U, 0U}
    }};
    const webscene_interop_invoke_request_v3 invoke_function{
        static_cast<uint32_t>(
            sizeof(webscene_interop_invoke_request_v3)),
        3U,
        WEBSCENE_INTEROP_INVOKE_FUNCTION_V3,
        0U,
        function_handle,
        nullptr,
        0U,
        nullptr,
        0U,
        function_values.data(),
        function_values.size(),
        function_edges.data(),
        function_edges.size(),
        nullptr,
        0U,
        0U,
        WEBSCENE_INTEROP_RESULT_VOID_V3};
    const auto* function_invoked =
        invoke_generated(engine, invoke_function);
    require(
        function_invoked->status
            == WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3,
        "binary reverse callback function handle invocation failed");
    release_interop_result(function_invoked);
    require(
        evaluate(
            engine,
            "globalThis.__managedFunctionArgument",
            "binary-reverse-function-result.js") == "19.5",
        "binary reverse callback function did not receive tagged arguments");

    const auto callback_call_id = callback->call_id;
    const auto callback_lease = callback->lease_id;
    webscene_interop_callback_release_v3(callback, callback_lease);
    webscene_interop_callback_release_v3(callback, callback_lease);

    const webscene_interop_value_v3 completion_value{
        WEBSCENE_INTEROP_VALUE_NUMBER_V3,
        0U,
        0U,
        0U,
        std::bit_cast<uint64_t>(73.25)};
    const webscene_interop_callback_completion_v3 completion{
        static_cast<uint32_t>(
            sizeof(webscene_interop_callback_completion_v3)),
        3U,
        callback_call_id,
        1U,
        0U,
        &completion_value,
        1U,
        nullptr,
        0U,
        nullptr,
        0U,
        nullptr,
        0U,
        0U,
        0U};
    require(
        webscene_engine_complete_callback_v3(
            engine,
            &completion) != 0U,
        "binary reverse promise completion was rejected");
    require(
        webscene_engine_complete_callback_v3(
            engine,
            &completion) == 0U,
        "binary reverse promise accepted a duplicate completion");

    bool settled = false;
    for (auto attempt = 0; attempt < 250 && !settled; ++attempt) {
        settled = evaluate(
            engine,
            "globalThis.__managedResult",
            "binary-reverse-result.js") == "73.25";
        if (!settled) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(settled, "binary reverse promise did not settle in JavaScript");

    std::array<const webscene_interop_callback_view_v3*, 2> void_callbacks{};
    for (auto& queued : void_callbacks) {
        for (auto attempt = 0; attempt < 250 && queued == nullptr; ++attempt) {
            queued = webscene_engine_take_callback_v3(engine);
            if (queued == nullptr) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
        require(
            queued != nullptr
                && queued->target_id == 41U
                && queued->method_id == 8U
                && queued->return_kind
                    == WEBSCENE_INTEROP_CALLBACK_VOID_V3,
            "binary void reverse callback metadata was invalid");
    }
    require(
        notifications.load(std::memory_order_relaxed)
            == notifications_before + 1U,
        "binary reverse callbacks were not notified on one empty-to-nonempty edge");
    for (const auto* queued : void_callbacks) {
        webscene_interop_callback_release_v3(
            queued,
            queued->lease_id);
    }

    const std::array<webscene_interop_value_v3, 1> release_values{{
        {WEBSCENE_INTEROP_VALUE_ARRAY_V3, 0U, 0U, 0U, 0U}
    }};
    const auto release_handle = [&](uint64_t handle) {
        const webscene_interop_invoke_request_v3 release{
            static_cast<uint32_t>(
                sizeof(webscene_interop_invoke_request_v3)),
            3U,
            WEBSCENE_INTEROP_RELEASE_HANDLE_V3,
            0U,
            handle,
            nullptr,
            0U,
            nullptr,
            0U,
            release_values.data(),
            release_values.size(),
            nullptr,
            0U,
            nullptr,
            0U,
            0U,
            WEBSCENE_INTEROP_RESULT_VOID_V3};
        const auto* released = invoke_generated(engine, release);
        require(
            released->status
                == WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3,
            "binary reverse callback handle release failed");
        release_interop_result(released);
    };

    std::array<webscene_interop_value_v3, 2> function_registration_values{{
        {WEBSCENE_INTEROP_VALUE_ARRAY_V3, 0U, 0U, 1U, 0U},
        {WEBSCENE_INTEROP_VALUE_NUMBER_V3, 0U, 0U, 0U,
            std::bit_cast<uint64_t>(
                static_cast<double>(
                    WEBSCENE_INTEROP_CALLBACK_VOID_V3))}
    }};
    const std::array<webscene_interop_edge_v3, 1>
        function_registration_edges{{{0U, 0U, 1U, 0U}}};
    const webscene_interop_invoke_request_v3 function_registration{
        static_cast<uint32_t>(
            sizeof(webscene_interop_invoke_request_v3)),
        3U,
        WEBSCENE_INTEROP_CREATE_CALLBACK_FUNCTION_V3,
        0U,
        52U,
        nullptr,
        0U,
        nullptr,
        0U,
        function_registration_values.data(),
        function_registration_values.size(),
        function_registration_edges.data(),
        function_registration_edges.size(),
        nullptr,
        0U,
        0U,
        WEBSCENE_INTEROP_RESULT_RETAINED_HANDLE_V3};
    const auto* registered_function =
        invoke_generated(engine, function_registration);
    require(
        registered_function->status
                == WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3
            && registered_function->values[
                registered_function->root_value_index].kind
                == WEBSCENE_INTEROP_VALUE_HANDLE_V3,
        "binary standalone callback function registration failed");
    const auto registered_function_handle =
        registered_function->values[
            registered_function->root_value_index].payload;
    release_interop_result(registered_function);

    std::array<webscene_interop_value_v3, 2> factory_registration_values{{
        {WEBSCENE_INTEROP_VALUE_ARRAY_V3, 0U, 0U, 1U, 0U},
        {WEBSCENE_INTEROP_VALUE_HANDLE_V3, 0U, 0U, 0U, callback_handle}
    }};
    const std::array<webscene_interop_edge_v3, 1>
        factory_registration_edges{{{0U, 0U, 1U, 0U}}};
    const webscene_interop_invoke_request_v3 factory_registration{
        static_cast<uint32_t>(
            sizeof(webscene_interop_invoke_request_v3)),
        3U,
        WEBSCENE_INTEROP_CREATE_SYNCHRONOUS_FACTORY_V3,
        0U,
        53U,
        nullptr,
        0U,
        nullptr,
        0U,
        factory_registration_values.data(),
        factory_registration_values.size(),
        factory_registration_edges.data(),
        factory_registration_edges.size(),
        nullptr,
        0U,
        0U,
        WEBSCENE_INTEROP_RESULT_RETAINED_HANDLE_V3};
    const auto* registered_factory =
        invoke_generated(engine, factory_registration);
    require(
        registered_factory->status
                == WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3
            && registered_factory->values[
                registered_factory->root_value_index].kind
                == WEBSCENE_INTEROP_VALUE_HANDLE_V3,
        "binary synchronous callback factory registration failed");
    const auto registered_factory_handle =
        registered_factory->values[
            registered_factory->root_value_index].payload;
    release_interop_result(registered_factory);

    const webscene_interop_invoke_request_v3 invoke_registered_function{
        static_cast<uint32_t>(
            sizeof(webscene_interop_invoke_request_v3)),
        3U,
        WEBSCENE_INTEROP_INVOKE_FUNCTION_V3,
        0U,
        registered_function_handle,
        nullptr,
        0U,
        nullptr,
        0U,
        release_values.data(),
        release_values.size(),
        nullptr,
        0U,
        nullptr,
        0U,
        0U,
        WEBSCENE_INTEROP_RESULT_VOID_V3};
    const auto* invoked_registered_function =
        invoke_generated(engine, invoke_registered_function);
    require(
        invoked_registered_function->status
            == WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3,
        "binary standalone callback function invocation failed");
    release_interop_result(invoked_registered_function);

    auto invoke_registered_factory = invoke_registered_function;
    invoke_registered_factory.target_handle =
        registered_factory_handle;
    invoke_registered_factory.result_mode =
        WEBSCENE_INTEROP_RESULT_RETAINED_HANDLE_V3;
    const auto* invoked_registered_factory =
        invoke_generated(engine, invoke_registered_factory);
    require(
        invoked_registered_factory->status
                == WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3
            && invoked_registered_factory->values[
                invoked_registered_factory->root_value_index].kind
                == WEBSCENE_INTEROP_VALUE_HANDLE_V3,
        "binary synchronous callback factory invocation failed");
    const auto factory_result_handle =
        invoked_registered_factory->values[
            invoked_registered_factory->root_value_index].payload;
    release_interop_result(invoked_registered_factory);

    std::array<const webscene_interop_callback_view_v3*, 2>
        registered_callbacks{};
    for (auto& queued : registered_callbacks) {
        for (auto attempt = 0; attempt < 250 && queued == nullptr; ++attempt) {
            queued = webscene_engine_take_callback_v3(engine);
            if (queued == nullptr) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
    }
    require(
        registered_callbacks[0] != nullptr
            && registered_callbacks[0]->target_id == 52U
            && registered_callbacks[0]->method_id == 0U
            && registered_callbacks[0]->return_kind
                == WEBSCENE_INTEROP_CALLBACK_VOID_V3
            && registered_callbacks[1] != nullptr
            && registered_callbacks[1]->target_id == 53U
            && registered_callbacks[1]->method_id == 0U
            && registered_callbacks[1]->return_kind
                == WEBSCENE_INTEROP_CALLBACK_SYNCHRONOUS_V3,
        "binary standalone function or synchronous factory callback metadata "
        "was invalid");
    for (const auto* queued : registered_callbacks) {
        webscene_interop_callback_release_v3(
            queued,
            queued->lease_id);
    }

    release_handle(function_handle);
    release_handle(registered_function_handle);
    release_handle(registered_factory_handle);
    release_handle(factory_result_handle);
    release_handle(callback_handle);

    webscene_interop_pool_metrics_v3 metrics{};
    metrics.struct_size = sizeof(metrics);
    require(
        webscene_engine_get_interop_pool_metrics_v3(
            engine,
            &metrics) != 0U
            && metrics.queued_callbacks == 0U
            && metrics.taken_callback_leases == 0U
            && metrics.pending_callback_promises == 0U
            && metrics.callback_queue_high_water >= 3U,
        "binary reverse callback leases or promises leaked");
    webscene_engine_destroy(engine);
}

uint64_t consumed_input_count(webscene_engine* engine)
{
    webscene_engine_metrics metrics{};
    webscene_engine_get_metrics(engine, &metrics);
    return metrics.consumed_inputs;
}
