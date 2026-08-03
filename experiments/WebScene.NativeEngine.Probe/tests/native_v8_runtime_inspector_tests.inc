struct inspector_test_messages final {
    std::mutex mutex;
    std::condition_variable available;
    std::vector<std::string> values;
};

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

    inspector_test_messages messages;
    const auto session_id = webscene_engine_inspector_connect(
        engine,
        receive_inspector_test_message,
        &messages,
        0U);
    require(session_id != 0U, "V8 Inspector session creation failed");

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

    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":3,"method":"Runtime.evaluate","params":{"expression":"21 * 2","returnByValue":true}})");
    require(
        wait_for_inspector_message(messages, R"("id":3)", R"("value":42)"),
        "Runtime.evaluate result did not round-trip through raw CDP");

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

    require(
        webscene_engine_inspector_disconnect(engine, session_id) != 0,
        "V8 Inspector session disconnect was rejected");
}
