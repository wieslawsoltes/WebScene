void test_resource_failure_diagnostics()
{
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback = [](void*, uint32_t, const char*, size_t,
        const char*, size_t, int64_t, char*, size_t) -> size_t { return 0; };
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "resource diagnostic engine creation failed");
    // Resource capture must not depend on exception or console capture.
    webscene_engine_configure_diagnostics(engine, 8U, nullptr, nullptr);
    evaluate(engine, "fetch('https://resource-diagnostics.test/missing.js?token=secret').catch(()=>{}); true",
        "caught-resource-failure.js");
    std::string record;
    for (int attempt = 0; attempt < 500 && record.empty(); ++attempt) {
        const auto size = webscene_engine_take_diagnostic(engine, nullptr, 0);
        if (size) {
            std::vector<char> buffer(size);
            webscene_engine_take_diagnostic(engine, buffer.data(), buffer.size());
            record = buffer.data();
        } else std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(record.find("\"kind\":\"resource-failure\"") != std::string::npos,
        "caught resource failure was not independently reported: " + record);
    require(record.find("\"url\":\"https://resource-diagnostics.test/missing.js\"") != std::string::npos
        && record.find("secret") == std::string::npos
        && record.find("\"method\":\"GET\"") != std::string::npos
        && record.find("\"resourceType\":\"data\"") != std::string::npos
        && record.find("\"durationMs\":") != std::string::npos,
        "resource failure lost metadata or exposed query secrets: " + record);
    require(webscene_engine_copy_runtime_failure(engine, nullptr, 0) == 0,
        "recoverable resource failure became terminal");
    webscene_engine_configure_diagnostics(engine, 0, nullptr, nullptr);
    evaluate(engine, "fetch('https://resource-diagnostics.test/disabled').catch(()=>{}); true", "disabled-resource.js");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    require(webscene_engine_take_diagnostic(engine, nullptr, 0) == 0,
        "resource diagnostics remained enabled after unsubscribe");
    webscene_engine_destroy(engine);
}
