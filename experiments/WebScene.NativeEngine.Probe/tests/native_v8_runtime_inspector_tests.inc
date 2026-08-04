struct inspector_test_messages final {
    std::mutex mutex;
    std::condition_variable available;
    std::vector<std::string> values;
};

struct blocking_inspector_sink final {
    std::mutex mutex;
    std::condition_variable condition;
    bool entered{false};
    bool released{false};
};

struct pull_inspector_notifications final {
    std::mutex mutex;
    std::condition_variable condition;
    size_t count{0U};
};

void receive_inspector_message_available(
    void* user_data,
    uint64_t)
{
    auto& notifications =
        *static_cast<pull_inspector_notifications*>(user_data);
    {
        std::lock_guard lock(notifications.mutex);
        ++notifications.count;
    }
    notifications.condition.notify_all();
}

void receive_blocking_inspector_message(
    void* user_data,
    uint64_t,
    const char*,
    size_t)
{
    auto& sink = *static_cast<blocking_inspector_sink*>(user_data);
    std::unique_lock lock(sink.mutex);
    if (sink.entered) return;
    sink.entered = true;
    sink.condition.notify_all();
    sink.condition.wait(lock, [&] { return sink.released; });
}

void receive_inspector_test_message(
    void* user_data,
    uint64_t,
    const char* message,
    size_t message_length)
{
    auto& messages = *static_cast<inspector_test_messages*>(user_data);
    {
        std::lock_guard lock(messages.mutex);
        messages.values.emplace_back(message, message_length);
    }
    messages.available.notify_all();
}

bool wait_for_inspector_message(
    inspector_test_messages& messages,
    std::string_view first,
    std::string_view second = {})
{
    std::unique_lock lock(messages.mutex);
    return messages.available.wait_for(
        lock,
        std::chrono::seconds(5),
        [&] {
            return std::any_of(
                messages.values.begin(),
                messages.values.end(),
                [&](const auto& message) {
                    return message.find(first) != std::string::npos
                        && (second.empty()
                            || message.find(second) != std::string::npos);
                });
        });
}

size_t count_inspector_messages(
    inspector_test_messages& messages,
    std::string_view first,
    std::string_view second = {})
{
    std::lock_guard lock(messages.mutex);
    return static_cast<size_t>(std::count_if(
        messages.values.begin(),
        messages.values.end(),
        [&](const auto& message) {
            return message.find(first) != std::string::npos
                && (second.empty()
                    || message.find(second) != std::string::npos);
        }));
}

bool wait_for_inspector_message_count(
    inspector_test_messages& messages,
    std::string_view first,
    size_t expected_count,
    std::string_view second = {})
{
    std::unique_lock lock(messages.mutex);
    return messages.available.wait_for(
        lock,
        std::chrono::seconds(5),
        [&] {
            return static_cast<size_t>(std::count_if(
                messages.values.begin(),
                messages.values.end(),
                [&](const auto& message) {
                    return message.find(first) != std::string::npos
                        && (second.empty()
                            || message.find(second) != std::string::npos);
                })) >= expected_count;
        });
}

std::string find_inspector_message(
    inspector_test_messages& messages,
    std::string_view first,
    std::string_view second = {})
{
    std::lock_guard lock(messages.mutex);
    const auto found = std::find_if(
        messages.values.begin(),
        messages.values.end(),
        [&](const auto& message) {
            return message.find(first) != std::string::npos
                && (second.empty() || message.find(second) != std::string::npos);
        });
    return found == messages.values.end() ? std::string{} : *found;
}

bool wait_for_pulled_inspector_message(
    webscene_engine* engine,
    uint64_t session_id,
    pull_inspector_notifications& notifications,
    std::string_view first,
    std::string_view second = {})
{
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto required = webscene_engine_inspector_take_message(
            engine,
            session_id,
            nullptr,
            0U);
        require(
            required != SIZE_MAX,
            "The pull-based Inspector output queue unexpectedly overflowed");
        if (required != 0U) {
            std::vector<char> short_buffer(required - 1U);
            require(
                webscene_engine_inspector_take_message(
                    engine,
                    session_id,
                    short_buffer.data(),
                    short_buffer.size()) == required,
                "A short Inspector pull did not retain the required message");
            std::vector<char> message(required);
            require(
                webscene_engine_inspector_take_message(
                    engine,
                    session_id,
                    message.data(),
                    message.size()) == required,
                "An exact Inspector pull did not copy the queued message");
            const auto text = std::string_view(message.data(), message.size());
            if (text.find(first) != std::string_view::npos
                && (second.empty()
                    || text.find(second) != std::string_view::npos)) {
                return true;
            }
            continue;
        }
        std::unique_lock lock(notifications.mutex);
        notifications.condition.wait_for(
            lock,
            std::chrono::milliseconds(25));
    }
    return false;
}

std::string inspector_json_string(
    std::string_view message,
    std::string_view property)
{
    const auto prefix = std::string{"\""} + std::string{property} + "\":\"";
    const auto start = message.find(prefix);
    if (start == std::string_view::npos) return {};
    const auto value_start = start + prefix.size();
    const auto end = message.find('"', value_start);
    return end == std::string_view::npos
        ? std::string{}
        : std::string{message.substr(value_start, end - value_start)};
}

int inspector_json_integer(
    std::string_view message,
    std::string_view property)
{
    const auto prefix = std::string{"\""} + std::string{property} + "\":";
    const auto start = message.find(prefix);
    if (start == std::string_view::npos) return -1;
    const auto value_start = start + prefix.size();
    int value = -1;
    const auto parsed = std::from_chars(
        message.data() + value_start,
        message.data() + message.size(),
        value);
    return parsed.ec == std::errc{} ? value : -1;
}

void dispatch_inspector_test_message(
    webscene_engine* engine,
    uint64_t session_id,
    std::string_view message)
{
    require(
        webscene_engine_inspector_dispatch(
            engine,
            session_id,
            message.data(),
            message.size()) != 0,
        "V8 Inspector request was rejected");
}

void test_v8_inspector_raw_cdp_session(webscene_engine* engine)
{
    if (std::getenv("WEBSCENE_V8_SHARED_ISOLATE") != nullptr) {
        require(
            webscene_engine_inspector_is_available(engine) == 0,
            "shared-isolate engine exposed an unsafe per-worker inspector");
        require(
            webscene_engine_inspector_state_created(engine) == 0,
            "shared-isolate engine allocated unused Inspector state");
        return;
    }

    for (auto attempt = 0;
         attempt < 500 && webscene_engine_inspector_is_available(engine) == 0;
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        webscene_engine_inspector_is_available(engine) != 0,
        "dedicated V8 isolate did not publish Inspector availability");
    require(
        webscene_engine_inspector_state_created(engine) == 0,
        "ordinary dedicated isolate eagerly allocated Inspector state");

    static_cast<void>(evaluate(
        engine,
        "console.log('webscene-inspector-before-attach');",
        "webscene-inspector-before-attach.js"));

    inspector_test_messages messages;
    const auto session_id = webscene_engine_inspector_connect(
        engine,
        receive_inspector_test_message,
        &messages,
        0U);
    require(session_id != 0U, "V8 Inspector session creation failed");
    require(
        webscene_engine_inspector_state_created(engine) != 0,
        "first Inspector connection did not allocate its lazy state");

    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":1,"method":"Runtime.enable"})");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":2,"method":"Debugger.enable"})");
    require(
        wait_for_inspector_message(messages, R"("id":1)"),
        "Runtime.enable response was not forwarded");
    require(
        wait_for_inspector_message(messages, R"("id":2)"),
        "Debugger.enable response was not forwarded");
    require(
        count_inspector_messages(
            messages,
            R"("method":"Runtime.consoleAPICalled")",
            "webscene-inspector-before-attach") == 0U,
        "Inspector retained console instrumentation while no session was attached");

    inspector_test_messages secondary_messages;
    const auto secondary_session_id = webscene_engine_inspector_connect(
        engine,
        receive_inspector_test_message,
        &secondary_messages,
        0U);
    require(
        secondary_session_id != 0U && secondary_session_id != session_id,
        "A second V8 Inspector client did not receive an independent session");
    dispatch_inspector_test_message(
        engine,
        secondary_session_id,
        R"({"id":1,"method":"Runtime.enable"})");
    dispatch_inspector_test_message(
        engine,
        secondary_session_id,
        R"({"id":2,"method":"Debugger.enable"})");
    dispatch_inspector_test_message(
        engine,
        secondary_session_id,
        R"({"id":3,"method":"Runtime.evaluate","params":{"expression":"6 * 7","returnByValue":true}})");
    require(
        wait_for_inspector_message(secondary_messages, R"("id":1)"),
        "The second Inspector session did not enable Runtime");
    require(
        wait_for_inspector_message(secondary_messages, R"("id":2)"),
        "The second Inspector session did not enable Debugger");
    require(
        wait_for_inspector_message(
            secondary_messages,
            R"("id":3)",
            R"("value":42)"),
        "The second Inspector session did not evaluate independently");
    require(
        webscene_engine_inspector_disconnect(engine, secondary_session_id) != 0,
        "The second Inspector session disconnect was rejected");

    inspector_test_messages waiting_messages;
    const auto waiting_session_id = webscene_engine_inspector_connect(
        engine,
        receive_inspector_test_message,
        &waiting_messages,
        1U);
    require(
        waiting_session_id != 0U && waiting_session_id != session_id,
        "The waiting Inspector session was not created independently");
    webscene_engine_metrics before_waiting_script{};
    webscene_engine_get_metrics(engine, &before_waiting_script);
    constexpr std::string_view waiting_script{
        "globalThis.webSceneInspectorWaitingBarrier = 73;"};
    constexpr std::string_view waiting_script_name{
        "webscene-inspector-waiting-barrier.js"};
    require(
        webscene_engine_execute_script(
            engine,
            waiting_script.data(),
            waiting_script.size(),
            waiting_script_name.data(),
            waiting_script_name.size()) != 0,
        "The waiting Inspector barrier script was rejected");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    webscene_engine_metrics during_waiting_script{};
    webscene_engine_get_metrics(engine, &during_waiting_script);
    require(
        during_waiting_script.executed_scripts
            == before_waiting_script.executed_scripts,
        "Script work ran before the acknowledged waiting Inspector session resumed");
    dispatch_inspector_test_message(
        engine,
        waiting_session_id,
        R"({"id":1,"method":"Runtime.enable"})");
    require(
        wait_for_inspector_message(waiting_messages, R"("id":1)"),
        "The waiting Inspector session did not enter its message loop");
    require(
        webscene_engine_inspector_disconnect(engine, waiting_session_id) != 0,
        "The waiting Inspector session disconnect was rejected");
    require(
        evaluate(
            engine,
            "globalThis.webSceneInspectorWaitingBarrier",
            "webscene-inspector-waiting-disconnect-result.js") == "73",
        "Disconnecting the waiting Inspector session did not release the worker "
        "while another session remained attached");

    pull_inspector_notifications pull_notifications;
    const auto pull_session_id = webscene_engine_inspector_connect_v3(
        engine,
        receive_inspector_message_available,
        &pull_notifications,
        0U);
    require(
        pull_session_id != 0U,
        "The pull-based Inspector session creation failed");
    dispatch_inspector_test_message(
        engine,
        pull_session_id,
        R"({"id":1,"method":"Runtime.enable"})");
    dispatch_inspector_test_message(
        engine,
        pull_session_id,
        R"({"id":2,"method":"Runtime.evaluate","params":{"expression":"6 * 7","returnByValue":true}})");
    require(
        wait_for_pulled_inspector_message(
            engine,
            pull_session_id,
            pull_notifications,
            R"("id":1)"),
        "The pull-based Inspector session did not return Runtime.enable");
    require(
        wait_for_pulled_inspector_message(
            engine,
            pull_session_id,
            pull_notifications,
            R"("id":2)",
            R"("value":42)"),
        "The pull-based Inspector session did not return Runtime.evaluate");
    require(
        webscene_engine_inspector_disconnect(engine, pull_session_id) != 0,
        "The pull-based Inspector session disconnect was rejected");
    require(
        webscene_engine_inspector_take_message(
            engine,
            pull_session_id,
            nullptr,
            0U) == 0U,
        "The pull-based Inspector output survived session disconnect");

    pull_inspector_notifications overflow_notifications;
    const auto overflow_session_id = webscene_engine_inspector_connect_v3(
        engine,
        receive_inspector_message_available,
        &overflow_notifications,
        0U);
    require(
        overflow_session_id != 0U,
        "The output-overflow Inspector session creation failed");
    constexpr std::string_view overflow_request{
        R"({"id":1,"method":"Runtime.evaluate","params":{"expression":"0"}})"};
    auto accepted_overflow_requests = 0U;
    const auto overflow_dispatch_deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(5);
    while (accepted_overflow_requests < 1050U
        && std::chrono::steady_clock::now() < overflow_dispatch_deadline) {
        if (webscene_engine_inspector_dispatch(
                engine,
                overflow_session_id,
                overflow_request.data(),
                overflow_request.size()) != 0) {
            ++accepted_overflow_requests;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(
        accepted_overflow_requests == 1050U,
        "The output-overflow Inspector fixture could not dispatch enough responses");
    auto output_overflowed = false;
    for (auto attempt = 0; attempt < 2500; ++attempt) {
        if (webscene_engine_inspector_take_message(
                engine,
                overflow_session_id,
                nullptr,
                0U) == SIZE_MAX) {
            output_overflowed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        output_overflowed,
        "The pull-based Inspector output queue did not report bounded overflow");
    require(
        webscene_engine_inspector_disconnect(engine, overflow_session_id) != 0,
        "The output-overflow Inspector session disconnect was rejected");

    {
        const std::string oversized_message(16U * 1024U * 1024U + 1U, 'x');
        require(
            webscene_engine_inspector_dispatch(
                engine,
                session_id,
                oversized_message.data(),
                oversized_message.size()) == 0,
            "The native Inspector ABI accepted a message larger than 16 MiB");
    }

    blocking_inspector_sink byte_limited_sink;
    const auto byte_limited_session_id = webscene_engine_inspector_connect(
        engine,
        receive_blocking_inspector_message,
        &byte_limited_sink,
        0U);
    require(
        byte_limited_session_id != 0U,
        "Inspector byte-limit session creation failed");
    dispatch_inspector_test_message(
        engine,
        byte_limited_session_id,
        R"({"id":1,"method":"Runtime.enable"})");
    {
        std::unique_lock lock(byte_limited_sink.mutex);
        require(
            byte_limited_sink.condition.wait_for(
                lock,
                std::chrono::seconds(5),
                [&] { return byte_limited_sink.entered; }),
            "Inspector byte-limit callback did not enter");
    }
    const std::string aggregate_limit_message(16U * 1024U * 1024U, 'x');
    require(
        webscene_engine_inspector_dispatch(
            engine,
            byte_limited_session_id,
            aggregate_limit_message.data(),
            aggregate_limit_message.size()) != 0,
        "The native Inspector inbound queue rejected its documented byte limit");
    constexpr std::string_view aggregate_overflow_message{"x"};
    require(
        webscene_engine_inspector_dispatch(
            engine,
            byte_limited_session_id,
            aggregate_overflow_message.data(),
            aggregate_overflow_message.size()) == 0,
        "The native Inspector inbound queue exceeded its aggregate byte limit");
    require(
        webscene_engine_inspector_disconnect(engine, byte_limited_session_id) != 0,
        "Inspector disconnect was rejected while its byte limit was saturated");
    {
        std::lock_guard lock(byte_limited_sink.mutex);
        byte_limited_sink.released = true;
    }
    byte_limited_sink.condition.notify_all();

    inspector_test_messages saturation_waiting_messages;
    const auto saturation_waiting_session_id = webscene_engine_inspector_connect(
        engine,
        receive_inspector_test_message,
        &saturation_waiting_messages,
        1U);
    require(
        saturation_waiting_session_id != 0U,
        "The saturation waiting Inspector session was not created");

    blocking_inspector_sink blocked_sink;
    const auto saturated_session_id = webscene_engine_inspector_connect(
        engine,
        receive_blocking_inspector_message,
        &blocked_sink,
        0U);
    require(saturated_session_id != 0U, "Inspector saturation session creation failed");
    dispatch_inspector_test_message(
        engine,
        saturated_session_id,
        R"({"id":1,"method":"Runtime.enable"})");
    {
        std::unique_lock lock(blocked_sink.mutex);
        require(
            blocked_sink.condition.wait_for(
                lock,
                std::chrono::seconds(5),
                [&] { return blocked_sink.entered; }),
            "Inspector saturation callback did not enter");
    }
    constexpr std::string_view saturated_request{
        R"({"id":2,"method":"Runtime.evaluate","params":{"expression":"0"}})"};
    size_t accepted_saturated_requests = 0U;
    while (webscene_engine_inspector_dispatch(
        engine,
        saturated_session_id,
        saturated_request.data(),
        saturated_request.size()) != 0) {
        ++accepted_saturated_requests;
        require(
            accepted_saturated_requests <= 1024U,
            "The native Inspector inbound queue did not enforce its safety limit");
    }
    require(
        accepted_saturated_requests == 1024U,
        "The native Inspector inbound queue rejected work before its documented limit");
    require(
        webscene_engine_inspector_disconnect(
            engine,
            saturation_waiting_session_id) != 0,
        "The waiting Inspector owner disconnect was rejected while another "
        "session saturated the inbound queue");
    require(
        webscene_engine_inspector_disconnect(engine, saturated_session_id) != 0,
        "Inspector disconnect was rejected while its inbound queue was saturated");
    {
        std::lock_guard lock(blocked_sink.mutex);
        blocked_sink.released = true;
    }
    blocked_sink.condition.notify_all();

    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":3,"method":"Runtime.evaluate","params":{"expression":"21 * 2","returnByValue":true}})");
    require(
        wait_for_inspector_message(messages, R"("id":3)", R"("value":42)"),
        "Runtime.evaluate result did not round-trip through raw CDP");

    blocking_inspector_sink connect_race_blocker;
    const auto connect_race_blocker_id = webscene_engine_inspector_connect(
        engine,
        receive_blocking_inspector_message,
        &connect_race_blocker,
        0U);
    require(
        connect_race_blocker_id != 0U,
        "The connect/disconnect race blocker session was not created");
    dispatch_inspector_test_message(
        engine,
        connect_race_blocker_id,
        R"({"id":1,"method":"Runtime.enable"})");
    {
        std::unique_lock lock(connect_race_blocker.mutex);
        require(
            connect_race_blocker.condition.wait_for(
                lock,
                std::chrono::seconds(5),
                [&] { return connect_race_blocker.entered; }),
            "The connect/disconnect race blocker callback did not enter");
    }
    inspector_test_messages canceled_connect_messages;
    const auto canceled_connect_session_id = webscene_engine_inspector_connect(
        engine,
        receive_inspector_test_message,
        &canceled_connect_messages,
        0U);
    require(
        canceled_connect_session_id != 0U,
        "The pending Inspector connection was not queued");
    require(
        webscene_engine_inspector_disconnect(
            engine,
            canceled_connect_session_id) != 0,
        "Disconnecting the pending Inspector connection was rejected");
    require(
        webscene_engine_inspector_disconnect(engine, connect_race_blocker_id) != 0,
        "Disconnecting the connect/disconnect race blocker was rejected");
    {
        std::lock_guard lock(connect_race_blocker.mutex);
        connect_race_blocker.released = true;
    }
    connect_race_blocker.condition.notify_all();
    dispatch_inspector_test_message(
        engine,
        canceled_connect_session_id,
        R"({"id":77,"method":"Runtime.enable"})");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":78,"method":"Runtime.evaluate","params":{"expression":"7 * 6","returnByValue":true}})");
    require(
        wait_for_inspector_message(messages, R"("id":78)", R"("value":42)"),
        "The connect/disconnect ordering barrier did not complete");
    require(
        count_inspector_messages(canceled_connect_messages, R"("id":77)") == 0U,
        "A disconnected pending connection was created after its disconnect");

    constexpr std::string_view source{
        "function webSceneInspectorPause(value) {\n"
        "  debugger;\n"
        "  return value + 1;\n"
        "}\n"
        "globalThis.webSceneInspectorResult = webSceneInspectorPause(6);"};
    constexpr std::string_view name{"webscene-inspector-target.js"};
    require(
        webscene_engine_execute_script(
            engine,
            source.data(),
            source.size(),
            name.data(),
            name.size()) != 0,
        "Inspector breakpoint fixture was rejected");
    require(
        wait_for_inspector_message(
            messages,
            R"("method":"Debugger.scriptParsed")",
            name),
        "Debugger.scriptParsed did not expose the WebScene source URL");
    require(
        wait_for_inspector_message(messages, R"("method":"Debugger.paused")"),
        "debugger statement did not pause the WebScene isolate");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":4,"method":"Debugger.resume"})");
    require(
        wait_for_inspector_message(messages, R"("id":4)"),
        "Debugger.resume response was not forwarded");

    const auto pauses_before_step = count_inspector_messages(
        messages,
        R"("method":"Debugger.paused")");
    constexpr std::string_view step_source{
        "function webSceneInspectorStep(value) {\n"
        "  debugger;\n"
        "  const incremented = value + 1;\n"
        "  return incremented * 2;\n"
        "}\n"
        "globalThis.webSceneInspectorStepResult = webSceneInspectorStep(6);"};
    constexpr std::string_view step_name{"webscene-inspector-step-target.js"};
    require(
        webscene_engine_execute_script(
            engine,
            step_source.data(),
            step_source.size(),
            step_name.data(),
            step_name.size()) != 0,
        "Inspector stepping fixture was rejected");
    require(
        wait_for_inspector_message_count(
            messages,
            R"("method":"Debugger.paused")",
            pauses_before_step + 1U),
        "debugger statement did not pause the stepping fixture");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":40,"method":"Debugger.stepOver"})");
    require(
        wait_for_inspector_message(messages, R"("id":40)"),
        "Debugger.stepOver response was not forwarded");
    require(
        wait_for_inspector_message_count(
            messages,
            R"("method":"Debugger.paused")",
            pauses_before_step + 2U),
        "Debugger.stepOver did not produce the next V8 pause");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":41,"method":"Debugger.resume"})");
    require(
        wait_for_inspector_message(messages, R"("id":41)"),
        "Debugger.resume after step did not respond");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":42,"method":"Runtime.evaluate","params":{"expression":"globalThis.webSceneInspectorStepResult","returnByValue":true}})");
    require(
        wait_for_inspector_message(messages, R"("id":42)", R"("value":14)"),
        "stepped WebScene execution did not finish after resume");

    const auto pauses_before_evaluation_step = count_inspector_messages(
        messages,
        R"("method":"Debugger.paused")");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":43,"method":"Runtime.evaluate","params":{"expression":"(() => { let value = 6; debugger; value += 1; return value * 2; })()\n//# sourceURL=webscene-inspector-evaluation-step-target.js","returnByValue":true}})");
    require(
        wait_for_inspector_message_count(
            messages,
            R"("method":"Debugger.paused")",
            pauses_before_evaluation_step + 1U),
        "Runtime.evaluate did not pause its stepping fixture");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":44,"method":"Debugger.stepOver"})");
    require(
        wait_for_inspector_message(messages, R"("id":44)"),
        "Runtime.evaluate stepOver response was not forwarded");
    require(
        wait_for_inspector_message_count(
            messages,
            R"("method":"Debugger.paused")",
            pauses_before_evaluation_step + 2U),
        "Runtime.evaluate stepOver did not produce the next pause");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":45,"method":"Debugger.resume"})");
    require(
        wait_for_inspector_message(messages, R"("id":45)"),
        "Runtime.evaluate resume after step did not respond");
    require(
        wait_for_inspector_message(messages, R"("id":43)", R"("value":14)"),
        "Runtime.evaluate did not complete after step and resume");

    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":46,"method":"Runtime.evaluate","params":{"expression":"Promise.resolve().then(() => { globalThis.webSceneInspectorMicrotaskResult = 73; }); 'queued'","returnByValue":true}})");
    require(
        wait_for_inspector_message(messages, R"("id":46)", R"("value":"queued")"),
        "Runtime.evaluate microtask fixture did not respond");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":47,"method":"Runtime.evaluate","params":{"expression":"globalThis.webSceneInspectorMicrotaskResult","returnByValue":true}})");
    require(
        wait_for_inspector_message(messages, R"("id":47)", R"("value":73)"),
        "Inspector task boundary did not perform a V8 microtask checkpoint");

    constexpr std::string_view live_edit_name{
        "webscene-inspector-live-edit-target.js"};
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":48,"method":"Runtime.evaluate","params":{"expression":"globalThis.webSceneInspectorLiveEdit = function webSceneInspectorLiveEdit(value) { return value + 1; };\n//# sourceURL=webscene-inspector-live-edit-target.js","returnByValue":true}})");
    require(
        wait_for_inspector_message(messages, R"("id":48)"),
        "Inspector live-edit fixture did not evaluate");
    require(
        wait_for_inspector_message(
            messages,
            R"("method":"Debugger.scriptParsed")",
            live_edit_name),
        "Inspector live-edit fixture did not publish its script identity");
    const auto live_edit_script_message = find_inspector_message(
        messages,
        R"("method":"Debugger.scriptParsed")",
        live_edit_name);
    const auto live_edit_script_id = inspector_json_string(
        live_edit_script_message,
        "scriptId");
    require(
        !live_edit_script_id.empty(),
        "Inspector live-edit fixture did not expose a script id");
    dispatch_inspector_test_message(
        engine,
        session_id,
        std::string{"{\"id\":49,\"method\":\"Debugger.setScriptSource\",\"params\":{\"scriptId\":\""}
            + live_edit_script_id
            + "\",\"scriptSource\":\"globalThis.webSceneInspectorLiveEdit = function webSceneInspectorLiveEdit(value) { return value + ; };\\n//# sourceURL=webscene-inspector-live-edit-target.js\",\"dryRun\":true}}" );
    require(
        wait_for_inspector_message(messages, R"("id":49)", R"("status":"CompileError")"),
        "Inspector live-edit dry run did not return compile diagnostics");
    const auto live_edit_source = std::string{
        "globalThis.webSceneInspectorLiveEdit = function webSceneInspectorLiveEdit(value) { return value + 2; };\\n//# sourceURL=webscene-inspector-live-edit-target.js"};
    dispatch_inspector_test_message(
        engine,
        session_id,
        std::string{"{\"id\":50,\"method\":\"Debugger.setScriptSource\",\"params\":{\"scriptId\":\""}
            + live_edit_script_id
            + "\",\"scriptSource\":\"" + live_edit_source
            + "\",\"dryRun\":true}}" );
    require(
        wait_for_inspector_message(messages, R"("id":50)", R"("status":"Ok")"),
        "Inspector live-edit valid dry run was rejected");
    dispatch_inspector_test_message(
        engine,
        session_id,
        std::string{"{\"id\":51,\"method\":\"Debugger.setScriptSource\",\"params\":{\"scriptId\":\""}
            + live_edit_script_id
            + "\",\"scriptSource\":\"" + live_edit_source
            + "\",\"dryRun\":false}}" );
    require(
        wait_for_inspector_message(messages, R"("id":51)", R"("status":"Ok")"),
        "Inspector live edit was not applied");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"JSON({"id":52,"method":"Runtime.evaluate","params":{"expression":"globalThis.webSceneInspectorLiveEdit(5)","returnByValue":true}})JSON");
    require(
        wait_for_inspector_message(messages, R"("id":52)", R"("value":7)"),
        "Inspector live-edited function did not execute its replacement source");

    const auto pauses_before_caught_exception = count_inspector_messages(
        messages,
        R"("method":"Debugger.paused")");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":53,"method":"Debugger.setPauseOnExceptions","params":{"state":"all"}})");
    require(
        wait_for_inspector_message(messages, R"("id":53)"),
        "Inspector did not enable caught-exception pausing");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":54,"method":"Runtime.evaluate","params":{"expression":"try { throw new Error('webscene-inspector-caught'); } catch (error) { globalThis.webSceneInspectorCaught = error.message; }","returnByValue":true}})");
    require(
        wait_for_inspector_message_count(
            messages,
            R"("method":"Debugger.paused")",
            pauses_before_caught_exception + 1U),
        "A caught exception did not pause the WebScene isolate");
    require(
        wait_for_inspector_message(
            messages,
            R"("reason":"exception")",
            "webscene-inspector-caught"),
        "Caught-exception pause did not expose its V8 exception data");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":55,"method":"Debugger.resume"})");
    require(
        wait_for_inspector_message(messages, R"("id":55)"),
        "Caught-exception pause did not resume");
    require(
        wait_for_inspector_message(messages, R"("id":54)"),
        "Caught-exception Runtime.evaluate did not complete after resume");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":56,"method":"Debugger.setPauseOnExceptions","params":{"state":"none"}})");
    require(
        wait_for_inspector_message(messages, R"("id":56)"),
        "Inspector did not restore exception pausing to none");

    const auto pauses_before_step_into = count_inspector_messages(
        messages,
        R"("method":"Debugger.paused")");
    constexpr std::string_view step_into_source{
        "function webSceneInspectorStepIntoInner(value) {\n"
        "  const incremented = value + 1;\n"
        "  return incremented;\n"
        "}\n"
        "function webSceneInspectorStepIntoOuter(value) {\n"
        "  debugger;\n"
        "  return webSceneInspectorStepIntoInner(value);\n"
        "}\n"
        "globalThis.webSceneInspectorStepIntoResult = webSceneInspectorStepIntoOuter(10);"};
    constexpr std::string_view step_into_name{
        "webscene-inspector-step-into-target.js"};
    require(
        webscene_engine_execute_script(
            engine,
            step_into_source.data(),
            step_into_source.size(),
            step_into_name.data(),
            step_into_name.size()) != 0,
        "Inspector step-into fixture was rejected");
    require(
        wait_for_inspector_message_count(
            messages,
            R"("method":"Debugger.paused")",
            pauses_before_step_into + 1U),
        "Inspector step-into fixture did not pause");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":57,"method":"Debugger.stepInto"})");
    require(
        wait_for_inspector_message(messages, R"("id":57)"),
        "Debugger.stepInto response was not forwarded");
    require(
        wait_for_inspector_message_count(
            messages,
            R"("method":"Debugger.paused")",
            pauses_before_step_into + 2U),
        "Debugger.stepInto did not reach the outer call statement");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":58,"method":"Debugger.stepInto"})");
    require(
        wait_for_inspector_message(messages, R"("id":58)"),
        "The second Debugger.stepInto response was not forwarded");
    require(
        wait_for_inspector_message_count(
            messages,
            R"("method":"Debugger.paused")",
            pauses_before_step_into + 3U),
        "Debugger.stepInto did not enter the called function");
    require(
        wait_for_inspector_message(
            messages,
            R"("method":"Debugger.paused")",
            "webSceneInspectorStepIntoInner"),
        "Debugger.stepInto pause did not identify the called function");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":59,"method":"Debugger.resume"})");
    require(
        wait_for_inspector_message(messages, R"("id":59)"),
        "Step-into fixture did not resume");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":60,"method":"Runtime.evaluate","params":{"expression":"globalThis.webSceneInspectorStepIntoResult","returnByValue":true}})");
    require(
        wait_for_inspector_message(messages, R"("id":60)", R"("value":11)"),
        "Step-into execution did not complete");

    const auto pauses_before_step_out = count_inspector_messages(
        messages,
        R"("method":"Debugger.paused")");
    constexpr std::string_view step_out_source{
        "function webSceneInspectorStepOutInner(value) {\n"
        "  debugger;\n"
        "  return value + 1;\n"
        "}\n"
        "function webSceneInspectorStepOutOuter(value) {\n"
        "  const inner = webSceneInspectorStepOutInner(value);\n"
        "  return inner + 1;\n"
        "}\n"
        "globalThis.webSceneInspectorStepOutResult = webSceneInspectorStepOutOuter(10);"};
    constexpr std::string_view step_out_name{
        "webscene-inspector-step-out-target.js"};
    require(
        webscene_engine_execute_script(
            engine,
            step_out_source.data(),
            step_out_source.size(),
            step_out_name.data(),
            step_out_name.size()) != 0,
        "Inspector step-out fixture was rejected");
    require(
        wait_for_inspector_message_count(
            messages,
            R"("method":"Debugger.paused")",
            pauses_before_step_out + 1U),
        "Inspector step-out fixture did not pause in the inner function");
    require(
        wait_for_inspector_message(
            messages,
            R"("method":"Debugger.paused")",
            "webSceneInspectorStepOutInner"),
        "Inspector step-out pause did not identify the inner function");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":61,"method":"Debugger.stepOut"})");
    require(
        wait_for_inspector_message(messages, R"("id":61)"),
        "Debugger.stepOut response was not forwarded");
    require(
        wait_for_inspector_message_count(
            messages,
            R"("method":"Debugger.paused")",
            pauses_before_step_out + 2U),
        "Debugger.stepOut did not pause in the caller");
    require(
        wait_for_inspector_message(
            messages,
            R"("method":"Debugger.paused")",
            "webSceneInspectorStepOutOuter"),
        "Debugger.stepOut pause did not identify the caller");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":62,"method":"Debugger.resume"})");
    require(
        wait_for_inspector_message(messages, R"("id":62)"),
        "Step-out fixture did not resume");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":63,"method":"Runtime.evaluate","params":{"expression":"globalThis.webSceneInspectorStepOutResult","returnByValue":true}})");
    require(
        wait_for_inspector_message(messages, R"("id":63)", R"("value":12)"),
        "Step-out execution did not complete");

    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":64,"method":"Runtime.enable"})");
    require(
        wait_for_inspector_message(messages, R"("id":64)"),
        "Inspector iframe lifecycle could not re-enable Runtime");
    const auto contexts_created_before_frame = count_inspector_messages(
        messages,
        R"("method":"Runtime.executionContextCreated")");
    const auto contexts_destroyed_before_frame = count_inspector_messages(
        messages,
        R"("method":"Runtime.executionContextDestroyed")");
    execute(
        engine,
        R"JS(
          globalThis.__inspectorLifecycleFrame = document.createElement('iframe');
          document.body.appendChild(globalThis.__inspectorLifecycleFrame);
          const frameDocument = globalThis.__inspectorLifecycleFrame.contentDocument;
          frameDocument.open();
          frameDocument.write('<html><body><script>globalThis.__inspectorFrameLoaded = 42;</script></body></html>');
          frameDocument.close();
        )JS",
        "webscene-inspector-frame-lifecycle.js");
    for (auto attempt = 0;
         attempt < 100
            && count_inspector_messages(
                messages,
                R"("method":"Runtime.executionContextCreated")")
                < contexts_created_before_frame + 1U;
         ++attempt) {
        static_cast<void>(evaluate(
            engine,
            "0",
            "webscene-inspector-frame-task-pump.js"));
    }
    if (!wait_for_inspector_message_count(
            messages,
            R"("method":"Runtime.executionContextCreated")",
            contexts_created_before_frame + 1U)) {
        const auto frame_state = evaluate(
            engine,
            "({ connected: globalThis.__inspectorLifecycleFrame?.isConnected, "
            "error: globalThis.__inspectorLifecycleFrame?.getAttribute('data-webscene-frame-error'), "
            "ready: globalThis.__inspectorLifecycleFrame?.contentDocument?.readyState, "
            "window: typeof globalThis.__inspectorLifecycleFrame?.contentWindow })",
            "webscene-inspector-frame-state.js");
        const auto frame_script_message = find_inspector_message(
            messages,
            R"("method":"Debugger.scriptParsed")",
            "webscene-frame-inline");
        fail(
            "Inspector did not report the iframe execution context: "
            + frame_state + " script=" + frame_script_message);
    }
    const auto frame_context_message = find_inspector_message(
        messages,
        R"("method":"Runtime.executionContextCreated")",
        "WebScene frame");
    const auto frame_context_id = inspector_json_integer(
        frame_context_message,
        "id");
    require(frame_context_id > 0, "Inspector iframe execution context id was invalid");
    dispatch_inspector_test_message(
        engine,
        session_id,
        std::string{"{\"id\":65,\"method\":\"Runtime.evaluate\",\"params\":{\"contextId\":"}
            + std::to_string(frame_context_id)
            + ",\"expression\":\"globalThis.__inspectorFrameObject = { value: 42 }\",\"returnByValue\":false,\"objectGroup\":\"frame-lifecycle\"}}" );
    require(
        wait_for_inspector_message(messages, R"("id":65)", R"("objectId")"),
        "Inspector could not evaluate in the iframe execution context");
    const auto frame_object_response = find_inspector_message(
        messages,
        R"("id":65)",
        R"("objectId")");
    const auto frame_object_id = inspector_json_string(
        frame_object_response,
        "objectId");
    require(!frame_object_id.empty(), "Inspector iframe remote object id was empty");
    dispatch_inspector_test_message(
        engine,
        session_id,
        std::string{"{\"id\":650,\"method\":\"Runtime.evaluate\",\"params\":{\"contextId\":"}
            + std::to_string(frame_context_id)
            + R"(,"expression":"parent.__websceneDetachedFrameCallbacks = 0; const record = () => ++parent.__websceneDetachedFrameCallbacks; setTimeout(record, 100); setInterval(record, 100); requestAnimationFrame(record); const originalTimeout = globalThis.setTimeout; globalThis.setTimeout = callback => originalTimeout(callback, 100); const channel = new MessageChannel(); channel.port1.onmessage = record; channel.port2.postMessage('queued'); globalThis.setTimeout = originalTimeout;","returnByValue":true}})" );
    require(
        wait_for_inspector_message(messages, R"("id":650)"),
        "Inspector could not schedule detached-frame task fixtures");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":66,"method":"Runtime.evaluate","params":{"expression":"document.body.removeChild(globalThis.__inspectorLifecycleFrame); delete globalThis.__inspectorLifecycleFrame;","returnByValue":true}})");
    require(
        wait_for_inspector_message(messages, R"("id":66)"),
        "Inspector iframe removal did not complete");
    for (auto attempt = 0;
         attempt < 100
            && count_inspector_messages(
                messages,
                R"("method":"Runtime.executionContextDestroyed")")
                < contexts_destroyed_before_frame + 1U;
         ++attempt) {
        static_cast<void>(evaluate(
            engine,
            "0",
            "webscene-inspector-frame-removal-pump.js"));
    }
    require(
        wait_for_inspector_message_count(
            messages,
            R"("method":"Runtime.executionContextDestroyed")",
            contexts_destroyed_before_frame + 1U),
        "Inspector did not report the removed iframe execution context");
    require(
        wait_for_inspector_message(
            messages,
            R"("method":"Runtime.executionContextDestroyed")",
            "\"executionContextId\":" + std::to_string(frame_context_id)),
        "Inspector destroyed a different execution context than the removed iframe");
    dispatch_inspector_test_message(
        engine,
        session_id,
        std::string{"{\"id\":67,\"method\":\"Runtime.getProperties\",\"params\":{\"objectId\":\""}
            + frame_object_id + "\",\"ownProperties\":true}}" );
    require(
        wait_for_inspector_message(messages, R"("id":67)", R"("error")"),
        "A removed iframe's V8 remote object remained usable");
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    for (auto attempt = 0; attempt < 10; ++attempt) {
        static_cast<void>(evaluate(
            engine,
            "0",
            "webscene-inspector-detached-frame-task-pump.js"));
    }
    require(
        evaluate(
            engine,
            "globalThis.__websceneDetachedFrameCallbacks",
            "webscene-inspector-detached-frame-result.js") == "0",
        "A timer, animation frame, or MessageChannel callback ran after its iframe context was destroyed");

    constexpr std::string_view exception_source{
        "throw new Error('webscene-inspector-exception');"};
    constexpr std::string_view exception_name{
        "webscene-inspector-exception-target.js"};
    require(
        webscene_engine_execute_script(
            engine,
            exception_source.data(),
            exception_source.size(),
            exception_name.data(),
            exception_name.size()) != 0,
        "Inspector exception fixture was rejected");
    require(
        wait_for_inspector_message(
            messages,
            R"("method":"Runtime.exceptionThrown")",
            "webscene-inspector-exception"),
        "uncaught WebScene exception was not forwarded through Runtime.exceptionThrown");
    require(
        wait_for_inspector_message(
            messages,
            R"("method":"Runtime.exceptionThrown")",
            exception_name),
        "Runtime.exceptionThrown did not retain the WebScene script URL");
    const auto exception_message = find_inspector_message(
        messages,
        R"("method":"Runtime.exceptionThrown")",
        "webscene-inspector-exception");
    require(
        exception_message.find(R"("lineNumber":0)") != std::string::npos,
        "Runtime.exceptionThrown did not convert the V8 line to zero-based CDP coordinates");
    require(
        exception_message.find(R"("columnNumber":0)") != std::string::npos,
        "Runtime.exceptionThrown changed V8's already-zero-based column coordinate");

    require(
        evaluate(
            engine,
            R"JS(
                globalThis.__websceneThrowingResize = () => {
                    throw new Error('webscene-inspector-resize-callback');
                };
                addEventListener('resize', globalThis.__websceneThrowingResize);
                'ready';
            )JS",
            "webscene-inspector-resize-callback.js") == R"("ready")",
        "Inspector resize-callback fixture was rejected");
    const webscene_input_event inspector_resize{
        WEBSCENE_INPUT_RESIZE,
        0U,
        90'001U,
        801.0,
        601.0,
        1.0,
        0.0};
    require(
        webscene_engine_enqueue(engine, &inspector_resize) != 0,
        "Inspector resize callback input was rejected");
    require(
        wait_for_inspector_message(
            messages,
            R"("method":"Runtime.exceptionThrown")",
            "webscene-inspector-resize-callback"),
        "resize callback exception was not forwarded through Runtime.exceptionThrown");
    require(
        evaluate(
            engine,
            R"JS(
                removeEventListener('resize', globalThis.__websceneThrowingResize);
                globalThis.__websceneThrowingObserver = new ResizeObserver(() => {
                    globalThis.__websceneThrowingObserver.disconnect();
                    throw new Error('webscene-inspector-resize-observer');
                });
                globalThis.__websceneThrowingObserver.observe(document.body);
                document.body.style.width = '799px';
                'ready';
            )JS",
            "webscene-inspector-resize-observer.js") == R"("ready")",
        "Inspector ResizeObserver fixture was rejected");
    const webscene_input_event inspector_observer_resize{
        WEBSCENE_INPUT_RESIZE,
        0U,
        90'002U,
        802.0,
        602.0,
        1.0,
        0.0};
    require(
        webscene_engine_enqueue(engine, &inspector_observer_resize) != 0,
        "Inspector ResizeObserver input was rejected");
    require(
        wait_for_inspector_message(
            messages,
            R"("method":"Runtime.exceptionThrown")",
            "webscene-inspector-resize-observer"),
        "ResizeObserver exception was not forwarded through Runtime.exceptionThrown");
    require(
        evaluate(
            engine,
            R"JS(
                addEventListener('keydown', () => {
                    throw new Error('webscene-inspector-input-callback');
                });
                'ready';
            )JS",
            "webscene-inspector-input-callback.js") == R"("ready")",
        "Inspector input-callback fixture was rejected");
    const webscene_input_event inspector_key{
        WEBSCENE_INPUT_KEY_DOWN,
        0U,
        90'003U,
        65.0,
        0.0,
        0.0,
        0.0};
    require(
        webscene_engine_enqueue(engine, &inspector_key) != 0,
        "Inspector input callback event was rejected");
    require(
        wait_for_inspector_message(
            messages,
            R"("method":"Runtime.exceptionThrown")",
            "webscene-inspector-input-callback"),
        "input callback exception was not forwarded through Runtime.exceptionThrown");

    constexpr std::string_view rejection_source{
        "Promise.reject(new Error('webscene-inspector-rejection'));"};
    constexpr std::string_view rejection_name{
        "webscene-inspector-rejection-target.js"};
    require(
        webscene_engine_execute_script(
            engine,
            rejection_source.data(),
            rejection_source.size(),
            rejection_name.data(),
            rejection_name.size()) != 0,
        "Inspector promise-rejection fixture was rejected");
    require(
        wait_for_inspector_message(
            messages,
            R"("method":"Runtime.exceptionThrown")",
            "webscene-inspector-rejection"),
        "unhandled promise rejection was not forwarded through Runtime.exceptionThrown");

    constexpr std::string_view late_handled_rejection_source{R"JS(
        globalThis.__websceneLateHandledRejection =
            Promise.reject(new Error('webscene-inspector-late-handled-rejection'));
        setTimeout(() => {
            globalThis.__websceneLateHandledRejection.catch(() => {});
        }, 25);
    )JS"};
    constexpr std::string_view late_handled_rejection_name{
        "webscene-inspector-late-handled-rejection-target.js"};
    require(
        webscene_engine_execute_script(
            engine,
            late_handled_rejection_source.data(),
            late_handled_rejection_source.size(),
            late_handled_rejection_name.data(),
            late_handled_rejection_name.size()) != 0,
        "late-handled Inspector promise-rejection fixture was rejected");
    require(
        wait_for_inspector_message(
            messages,
            R"("method":"Runtime.exceptionThrown")",
            "webscene-inspector-late-handled-rejection"),
        "late-handled promise rejection was not initially reported");
    require(
        wait_for_inspector_message(
            messages,
            R"("method":"Runtime.exceptionRevoked")",
            "Promise rejection was handled asynchronously"),
        "late promise handler did not revoke the promoted Inspector exception");

    const auto revocations_before_rejection_cap = count_inspector_messages(
        messages,
        R"("method":"Runtime.exceptionRevoked")");
    const auto exceptions_before_rejection_cap = count_inspector_messages(
        messages,
        R"("method":"Runtime.exceptionThrown")",
        "webscene-inspector-capped-rejection-");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":68,"method":"Runtime.evaluate","params":{"expression":"globalThis.__websceneCappedRejections = Array.from({ length: 1025 }, (_, index) => Promise.reject(new Error('webscene-inspector-capped-rejection-' + index)));","returnByValue":true}})");
    require(
        wait_for_inspector_message(messages, R"("id":68)"),
        "Inspector rejection-cap fixture did not evaluate");
    require(
        wait_for_inspector_message_count(
            messages,
            R"("method":"Runtime.exceptionThrown")",
            exceptions_before_rejection_cap + 1025U,
            "webscene-inspector-capped-rejection-"),
        "Inspector did not report every rejection in the cap fixture");
    require(
        count_inspector_messages(
            messages,
            R"("method":"Runtime.exceptionRevoked")")
            == revocations_before_rejection_cap,
        "The Inspector tracking cap falsely revoked a still-unhandled rejection");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":69,"method":"Runtime.evaluate","params":{"expression":"globalThis.__websceneCappedRejections[1024].catch(() => {});","returnByValue":true}})");
    require(
        wait_for_inspector_message_count(
            messages,
            R"("method":"Runtime.exceptionRevoked")",
            revocations_before_rejection_cap + 1U,
            "Promise rejection was handled asynchronously"),
        "A retained rejection record was not revoked after its late handler was added");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":70,"method":"Runtime.evaluate","params":{"expression":"globalThis.__websceneCappedRejections[0].catch(() => {});","returnByValue":true}})");
    require(
        wait_for_inspector_message(messages, R"("id":70)"),
        "Inspector could not handle the evicted rejection record");
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    require(
        count_inspector_messages(
            messages,
            R"("method":"Runtime.exceptionRevoked")")
            == revocations_before_rejection_cap + 1U,
        "An evicted rejection record emitted a spurious late revocation");

    constexpr std::string_view console_source{
        "console.warn('webscene-inspector-console', { answer: 42 });"};
    constexpr std::string_view console_name{
        "webscene-inspector-console-target.js"};
    require(
        webscene_engine_execute_script(
            engine,
            console_source.data(),
            console_source.size(),
            console_name.data(),
            console_name.size()) != 0,
        "Inspector console fixture was rejected");
    require(
        wait_for_inspector_message(
            messages,
            R"("method":"Runtime.consoleAPICalled")",
            "webscene-inspector-console"),
        "WebScene console call was not forwarded through Runtime.consoleAPICalled");
    require(
        wait_for_inspector_message(
            messages,
            "webscene-inspector-console",
            R"("objectId")"),
        "Runtime.consoleAPICalled did not expose an expandable V8 object");

    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":5,"method":"Debugger.setAsyncCallStackDepth","params":{"maxDepth":8}})");
    require(
        wait_for_inspector_message(messages, R"("id":5)"),
        "async call-stack depth configuration was not accepted");
    constexpr std::string_view async_source{
        "setTimeout(function webSceneInspectorTimer() {\n"
        "  debugger;\n"
        "}, 0);"};
    constexpr std::string_view async_name{"webscene-inspector-async-target.js"};
    require(
        webscene_engine_execute_script(
            engine,
            async_source.data(),
            async_source.size(),
            async_name.data(),
            async_name.size()) != 0,
        "Inspector async-stack fixture was rejected");
    require(
        wait_for_inspector_message(
            messages,
            R"("method":"Debugger.paused")",
            R"("asyncStackTrace")"),
        "WebScene timer pause did not retain its scheduled async stack");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":6,"method":"Debugger.resume"})");
    require(
        wait_for_inspector_message(messages, R"("id":6)"),
        "async timer Debugger.resume response was not forwarded");

    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":7,"method":"Profiler.enable"})");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":8,"method":"Profiler.start"})");
    require(
        wait_for_inspector_message(messages, R"("id":7)"),
        "Profiler.enable response was not forwarded");
    require(
        wait_for_inspector_message(messages, R"("id":8)"),
        "Profiler.start response was not forwarded");
    constexpr std::string_view profiler_source{
        "let webSceneProfileTotal = 0;\n"
        "for (let index = 0; index < 100000; ++index) {\n"
        "  webSceneProfileTotal += Math.sqrt(index);\n"
        "}\n"
        "globalThis.webSceneProfileTotal = webSceneProfileTotal;"};
    constexpr std::string_view profiler_name{
        "webscene-inspector-profiler-target.js"};
    require(
        webscene_engine_execute_script(
            engine,
            profiler_source.data(),
            profiler_source.size(),
            profiler_name.data(),
            profiler_name.size()) != 0,
        "Inspector CPU-profiler fixture was rejected");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":9,"method":"Profiler.stop"})");
    require(
        wait_for_inspector_message(messages, R"("id":9)", R"("nodes")"),
        "Profiler.stop did not return a V8 CPU profile");

    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":10,"method":"HeapProfiler.enable"})");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":11,"method":"HeapProfiler.startSampling","params":{"samplingInterval":16384}})");
    require(
        wait_for_inspector_message(messages, R"("id":10)"),
        "HeapProfiler.enable response was not forwarded");
    require(
        wait_for_inspector_message(messages, R"("id":11)"),
        "HeapProfiler.startSampling response was not forwarded");
    constexpr std::string_view allocation_source{
        "globalThis.webSceneInspectorAllocations = Array.from(\n"
        "  { length: 256 }, (_, index) => ({ index, payload: 'x'.repeat(1024) }));"};
    constexpr std::string_view allocation_name{
        "webscene-inspector-allocation-target.js"};
    require(
        webscene_engine_execute_script(
            engine,
            allocation_source.data(),
            allocation_source.size(),
            allocation_name.data(),
            allocation_name.size()) != 0,
        "Inspector allocation-profiler fixture was rejected");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":12,"method":"HeapProfiler.stopSampling"})");
    require(
        wait_for_inspector_message(messages, R"("id":12)", R"("head")"),
        "HeapProfiler.stopSampling did not return a V8 allocation profile");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":13,"method":"Runtime.getHeapUsage"})");
    require(
        wait_for_inspector_message(messages, R"("id":13)", R"("usedSize")"),
        "Runtime.getHeapUsage did not return V8 heap metrics");

    const auto pauses_before_disconnect = count_inspector_messages(
        messages,
        R"("method":"Debugger.paused")");
    constexpr std::string_view disconnect_source{
        "debugger;\n"
        "globalThis.webSceneInspectorDisconnectResult = 97;"};
    constexpr std::string_view disconnect_name{
        "webscene-inspector-disconnect-target.js"};
    require(
        webscene_engine_execute_script(
            engine,
            disconnect_source.data(),
            disconnect_source.size(),
            disconnect_name.data(),
            disconnect_name.size()) != 0,
        "Inspector disconnect-while-paused fixture was rejected");
    require(
        wait_for_inspector_message_count(
            messages,
            R"("method":"Debugger.paused")",
            pauses_before_disconnect + 1U),
        "Inspector disconnect fixture did not pause");
    require(
        webscene_engine_inspector_disconnect(engine, session_id) != 0,
        "V8 Inspector session disconnect while paused was rejected");
    for (auto attempt = 0; attempt < 200; ++attempt) {
        if (evaluate(
                engine,
                "globalThis.webSceneInspectorDisconnectResult",
                "webscene-inspector-disconnect-result.js") == "97") {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    fail("Disconnecting the last Inspector session did not release the paused isolate");
}

void test_v8_inspector_shutdown_releases_paused_engine()
{
    if (std::getenv("WEBSCENE_V8_SHARED_ISOLATE") != nullptr) return;

    auto* engine = webscene_engine_create(64);
    require(engine != nullptr, "Inspector shutdown fixture engine creation failed");
    for (auto attempt = 0;
         attempt < 500 && webscene_engine_inspector_is_available(engine) == 0;
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        webscene_engine_inspector_is_available(engine) != 0,
        "Inspector shutdown fixture did not become available");

    inspector_test_messages messages;
    const auto session_id = webscene_engine_inspector_connect(
        engine,
        receive_inspector_test_message,
        &messages,
        0U);
    require(session_id != 0U, "Inspector shutdown fixture session creation failed");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":1,"method":"Runtime.enable"})");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":2,"method":"Debugger.enable"})");
    require(
        wait_for_inspector_message(messages, R"("id":1)"),
        "Inspector shutdown fixture did not enable Runtime");
    require(
        wait_for_inspector_message(messages, R"("id":2)"),
        "Inspector shutdown fixture did not enable Debugger");

    constexpr std::string_view source{
        "debugger;\n"
        "globalThis.webSceneInspectorShutdownResult = 101;"};
    constexpr std::string_view name{
        "webscene-inspector-shutdown-while-paused.js"};
    require(
        webscene_engine_execute_script(
            engine,
            source.data(),
            source.size(),
            name.data(),
            name.size()) != 0,
        "Inspector shutdown-while-paused fixture was rejected");
    require(
        wait_for_inspector_message(messages, R"("method":"Debugger.paused")"),
        "Inspector shutdown fixture did not pause the engine");

    std::atomic<bool> destroyed{false};
    std::thread destroyer([&] {
        webscene_engine_destroy(engine);
        destroyed.store(true, std::memory_order_release);
    });
    for (auto attempt = 0;
         attempt < 2500 && !destroyed.load(std::memory_order_acquire);
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        destroyed.load(std::memory_order_acquire),
        "Destroying an Inspector-paused engine did not complete within five seconds");
    destroyer.join();
}
