void test_media_query_callback_can_create_more_queries()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "reentrant media query engine creation failed");
    resize(engine, 800, 600, 1);
    wait_for_consumed_inputs(engine, 1, "initial media viewport not applied");
    evaluate(engine, R"JS(
        globalThis.mediaChanges = [];
        const first = matchMedia('(max-width:500px)');
        const second = matchMedia('(max-width:500px)');
        first.addEventListener('change', () => {
            mediaChanges.push('first');
            // Force growth of the runtime's query owner collection during dispatch.
            for (let index = 0; index < 4096; index++) matchMedia('(min-width:1px)');
        });
        second.addEventListener('change', () => mediaChanges.push('second'));
        true
    )JS", "media-query-reentrancy.js");
    resize(engine, 400, 600, 2);
    wait_for_consumed_inputs(engine, 2, "compact media viewport not applied");
    require(evaluate(engine, "mediaChanges", "media-query-reentrancy-result.js") == R"(["first","second"])",
        "creating queries during change dispatch invalidated existing query delivery");
    webscene_engine_destroy(engine);
}
