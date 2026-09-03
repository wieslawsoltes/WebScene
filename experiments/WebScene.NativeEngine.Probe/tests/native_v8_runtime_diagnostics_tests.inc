void test_runtime_diagnostics()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "diagnostics engine creation failed");
    const auto drain = [&] {
        std::vector<std::string> records;
        for (;;) {
            const auto size = webscene_engine_take_diagnostic(engine, nullptr, 0);
            if (size == 0) break;
            std::vector<char> data(size);
            require(webscene_engine_take_diagnostic(engine, data.data(), data.size()) == size,
                "diagnostic short probe consumed record");
            records.emplace_back(data.data());
        }
        return records;
    };
    require(evaluate(engine, R"JS(
        globalThis.formatCalls = 0;
        globalThis.hostile = { toString() { ++formatCalls; throw Error('format'); },
                              get stack() { ++formatCalls; throw Error('stack'); } };
        console.log(hostile); console.trace(hostile); console.assert(false, hostile);
        formatCalls
    )JS", "disabled-console.js") == "0", "disabled console executed application code");
    require(drain().empty(), "disabled diagnostics captured records");
    require(webscene_engine_take_console_message(engine, nullptr, 0) == 0, "legacy console enabled by default");
    std::atomic<int> wakeups{0};
    webscene_engine_configure_diagnostics(engine, WEBSCENE_DIAGNOSTIC_EXCEPTIONS | WEBSCENE_DIAGNOSTIC_CONSOLE,
        [](void* data) { static_cast<std::atomic<int>*>(data)->fetch_add(1); }, &wakeups);
    require(evaluate(engine, "console.info('hello', 42, hostile); console.assert(true, 'skip'); formatCalls",
        "console-safe.js") == "0", "console snapshot invoked application code");
    auto records = drain();
    require(records.size() == 1 && records[0].find("hello") != std::string::npos
        && records[0].find("\"level\":\"info\"") != std::string::npos, "console record missing");
    require(wakeups.load() > 0, "no diagnostics wakeup");
    // Authored catch and same-checkpoint rejection handling must stay invisible.
    evaluate(engine, "try { throw Error('caught'); } catch {} Promise.reject(Error('handled')).catch(()=>{}); true", "caught.js");
    require(drain().empty(), "caught exception reported as uncaught");
    const auto* invocation_failure = invoke_binary(engine, "throw Error('host call failed')", "host-call.js");
    require(invocation_failure->status != WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3, "host call did not fail");
    release_interop_result(invocation_failure);
    require(drain().empty(), "host invocation was duplicated as a global uncaught exception");
    records.clear();
    evaluate(engine, "setTimeout(()=>{throw Error('timer exploded')},0); true", "timer-origin.js");
    for (int attempt = 0; attempt < 250 && records.size() != 1; ++attempt) {
        records = drain();
        if (records.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(records.size() == 1 && records[0].find("timer exploded") != std::string::npos
        && records[0].find("timer-origin.js") != std::string::npos, "timer error metadata missing or duplicated");
    require(webscene_engine_copy_runtime_failure(engine, nullptr, 0) == 0,
        "ordinary timer exception changed terminal status");
    records.clear();
    evaluate(engine, "window.onerror=()=>{throw Error('secondary handler error')}; setTimeout(()=>{throw Error('primary error')},0); true", "error-handler.js");
    for (int attempt = 0; attempt < 250 && records.size() < 2; ++attempt) {
        auto next = drain(); records.insert(records.end(), next.begin(), next.end());
        if (records.size() < 2) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(records.size() == 2 && records[1].find("secondary handler error") != std::string::npos,
        "an exception from the page's error handler was swallowed");
    evaluate(engine, "window.onerror = null; true", "clear-error-handler.js");
    records.clear();
    evaluate(engine, "setTimeout(()=>{Promise.reject(Error('unhandled one')); Promise.reject(Error('unhandled two'));},0); true", "promises.js");
    for (int attempt = 0; attempt < 250 && records.size() < 2; ++attempt) {
        auto next = drain();
        records.insert(records.end(), next.begin(), next.end());
        if (records.size() < 2) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(records.size() == 2 && records[0].find("\"promiseRejection\":true") != std::string::npos
        && records[1].find("unhandled two") != std::string::npos, "unhandled rejection checkpoint lost an error");
    evaluate(engine, "console.debug('x'.repeat(100000)); console.trace('trace'); true", "bounded-text.js");
    records = drain();
    require(records.size() == 2 && records[0].find("\"truncated\":true") != std::string::npos
        && records[0].size() < 20000 && records[1].find("bounded-text.js") != std::string::npos,
        "console text/stack was not bounded or attributed");
    evaluate(engine, "for(let i=0;i<1100;i++) console.log('bounded',i); true", "overflow.js");
    records = drain();
    require(records.size() == 1025 && records[0].find("droppedCount") != std::string::npos,
        "diagnostics queue is not bounded or does not report loss");
    webscene_engine_configure_diagnostics(engine, 0, nullptr, nullptr);
    const auto count = wakeups.load();
    evaluate(engine, "console.log('disabled again'); true", "disabled-again.js");
    require(wakeups.load() == count && drain().empty(), "unregistered capture remained active");
    webscene_engine_destroy(engine);
}

void test_runtime_diagnostics_frame_and_failure()
{
    resource_server server{.content = {
        {"https://diagnostics.test/index.html", R"HTML(<html><body><script>
            const frame = document.createElement('iframe'); document.body.appendChild(frame);
            const child = frame.contentDocument; child.open();
            child.write('<html><body><script>console.warn("frame console"); setTimeout(()=>{throw Error("frame timer")},0);<\/script></body></html>');
            child.close();
        </script></body></html>)HTML"}
    }};
    const webscene_engine_options options{sizeof(webscene_engine_options), 0U, nullptr, 0U, load_test_resource, &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "frame diagnostic engine creation failed");
    webscene_engine_configure_diagnostics(engine, WEBSCENE_DIAGNOSTIC_EXCEPTIONS | WEBSCENE_DIAGNOSTIC_CONSOLE, nullptr, nullptr);
    const std::string url = "https://diagnostics.test/index.html";
    // Exercise the document-written child-frame lifecycle used by hosted widgets.
    constexpr std::string_view setup_source = "void 0";
    constexpr std::string_view setup_name = "frame-diagnostics-start.js";
    const webscene_document_script setup_script{sizeof(webscene_document_script), WEBSCENE_DOCUMENT_SCRIPT_ALL_FRAMES,
        setup_source.data(), setup_source.size(), setup_name.data(), setup_name.size()};
    const webscene_navigation_options setup_navigation{sizeof(webscene_navigation_options), 1, &setup_script};
    require(webscene_engine_load_url_with_options(engine, url.data(), url.size(), &setup_navigation) != 0, "frame diagnostic load rejected");
    bool frame_console = false, frame_error = false;
    std::string captured;
    const auto take = [&] {
        const auto size = webscene_engine_take_diagnostic(engine, nullptr, 0);
        if (size == 0) return std::string{};
        std::vector<char> data(size);
        webscene_engine_take_diagnostic(engine, data.data(), data.size());
        return std::string(data.data());
    };
    for (int attempt = 0; attempt < 500 && (!frame_console || !frame_error); ++attempt) {
        const auto record = take();
        if (record.empty()) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); continue; }
        captured += record + '\n';
        if (record.find("frame console") != std::string::npos || record.find("frame timer") != std::string::npos) {
            require(record.find("https://diagnostics.test/") != std::string::npos
                && record.find("\"frameId\":0") == std::string::npos, "child record lost frame identity: " + record);
            frame_console |= record.find("frame console") != std::string::npos;
            frame_error |= record.find("frame timer") != std::string::npos;
        }
        require(record.find("\"kind\":\"failure\"") == std::string::npos, "ordinary frame error became fatal");
    }
    require(frame_console && frame_error, "frame diagnostic notifications missing; requests="
        + std::to_string(server.requests.load()) + "; last error=" + last_error(engine) + "; records=" + captured);
    constexpr std::string_view source = "throw Error('bootstrap terminal')";
    constexpr std::string_view name = "host-bootstrap.js";
    const webscene_document_script script{sizeof(webscene_document_script), WEBSCENE_DOCUMENT_SCRIPT_ALL_FRAMES,
        source.data(), source.size(), name.data(), name.size()};
    const webscene_navigation_options navigation{sizeof(webscene_navigation_options), 1, &script};
    require(webscene_engine_load_url_with_options(engine, url.data(), url.size(), &navigation) != 0,
        "failing startup load rejected before execution");
    bool exception = false, failure = false;
    for (int attempt = 0; attempt < 500 && !failure; ++attempt) {
        const auto record = take();
        if (record.empty()) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); continue; }
        exception |= record.find("\"kind\":\"exception\"") != std::string::npos;
        failure |= record.find("\"kind\":\"failure\"") != std::string::npos;
        require(record.find("bootstrap terminal") != std::string::npos, "startup diagnostic lost error message");
    }
    require(exception && failure, "startup failure did not expose exception plus terminal failure");
    const auto failure_size = webscene_engine_copy_runtime_failure(engine, nullptr, 0);
    require(failure_size > 0, "terminal status was lost when its queued notification was consumed");
    std::vector<char> failure_copy(failure_size);
    require(webscene_engine_copy_runtime_failure(engine, failure_copy.data(), failure_copy.size()) == failure_size
        && std::string(failure_copy.data()).find("bootstrap terminal") != std::string::npos,
        "terminal status snapshot changed or lost its message");
    webscene_engine_destroy(engine);
}
