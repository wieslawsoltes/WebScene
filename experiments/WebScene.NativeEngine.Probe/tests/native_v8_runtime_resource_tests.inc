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
