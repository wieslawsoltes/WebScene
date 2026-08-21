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

void test_concurrent_input_producers_remain_consumable()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "concurrent-input engine creation failed");

    constexpr uint32_t producer_count = 8U;
    constexpr uint32_t inputs_per_producer = 768U;
    constexpr uint64_t expected_inputs =
        static_cast<uint64_t>(producer_count) * inputs_per_producer;
    std::atomic<uint32_t> ready{0U};
    std::atomic<bool> start{false};
    std::atomic<uint64_t> sequence{1U};
    std::atomic<uint64_t> accepted{0U};
    std::vector<std::thread> producers;
    producers.reserve(producer_count);

    for (uint32_t producer = 0; producer < producer_count; ++producer) {
        producers.emplace_back([&, producer] {
            ready.fetch_add(1U, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (uint32_t index = 0; index < inputs_per_producer; ++index) {
                const webscene_input_event input{
                    WEBSCENE_INPUT_POINTER_MOVE,
                    0U,
                    sequence.fetch_add(1U, std::memory_order_relaxed),
                    static_cast<double>(producer * inputs_per_producer + index),
                    static_cast<double>(producer),
                    0,
                    0};
                require(
                    webscene_engine_enqueue(engine, &input) != 0,
                    "concurrent input was rejected before ring capacity");
                accepted.fetch_add(1U, std::memory_order_relaxed);
            }
        });
    }

    while (ready.load(std::memory_order_acquire) != producer_count) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);
    for (auto& producer : producers) producer.join();

    require(
        accepted.load(std::memory_order_relaxed) == expected_inputs,
        "concurrent producers did not submit every input");
    wait_for_consumed_inputs(
        engine,
        expected_inputs,
        "concurrently accepted input became invisible to the worker");

    webscene_engine_metrics metrics{};
    webscene_engine_get_metrics(engine, &metrics);
    require(
        metrics.enqueued_inputs == expected_inputs
            && metrics.consumed_inputs == expected_inputs
            && metrics.dropped_inputs == 0U,
        "concurrent input accounting did not remain lossless");
    webscene_engine_destroy(engine);
}

void notify_animation_frame_requested_test(void* user_data)
{
    static_cast<std::atomic<uint32_t>*>(user_data)
        ->fetch_add(1U, std::memory_order_relaxed);
}

void test_animation_frame_demand_emits_idle_to_active_edges()
{
    std::atomic<uint32_t> notifications{0U};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.animation_frame_requested_callback =
        notify_animation_frame_requested_test;
    options.animation_frame_requested_user_data = &notifications;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "animation-frame notification engine creation failed");

    execute(
        engine,
        "requestAnimationFrame(() => {}); requestAnimationFrame(() => {});",
        "native-animation-frame-notification-first.js");
    wait_for_animation_frame_demand(
        engine,
        true,
        "initial requestAnimationFrame did not request a host frame");
    for (auto attempt = 0;
         attempt < 250 && notifications.load(std::memory_order_relaxed) < 1U;
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        notifications.load(std::memory_order_relaxed) == 1U,
        "one idle-to-active RAF transition emitted more than one notification");

    animation_frame_and_wait(engine, 100.0, 1U);
    wait_for_animation_frame_demand(
        engine,
        false,
        "released RAF callbacks retained host-frame demand");

    execute(
        engine,
        "requestAnimationFrame(() => {});",
        "native-animation-frame-notification-second.js");
    wait_for_animation_frame_demand(
        engine,
        true,
        "second requestAnimationFrame did not request a host frame");
    for (auto attempt = 0;
         attempt < 250 && notifications.load(std::memory_order_relaxed) < 2U;
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        notifications.load(std::memory_order_relaxed) == 2U,
        "a second idle-to-active RAF transition did not emit one notification");

    webscene_engine_destroy(engine);
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

void test_collapsed_single_select_native_activation(webscene_engine* engine)
{
    resize(engine, 320, 140, 600U);
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              body { margin: 0; }
              #choice { position:absolute;left:20px;top:20px;width:240px;height:39px;
                box-sizing:border-box;padding:8px 10px;color:rgb(18,52,86);font-size:16px; }
            </style>
            <select id="choice">
              <option value="one">One-way flight</option>
              <option value="return">Return flight</option>
            </select>`;
          globalThis.__selectEvents = [];
          for (const type of ['click', 'input', 'change', 'keydown', 'keyup']) {
            choice.addEventListener(type, () => __selectEvents.push(type));
          }
        })()
    )JS", "native-collapsed-select-setup.js");
    const auto initial = evaluate(engine, R"JS((() => {
          const select = choice.getBoundingClientRect();
          const options = Array.from(choice.options, option => option.getBoundingClientRect());
          return [choice.value, select.width, select.height,
            options.every(rect => rect.width === 0 && rect.height === 0)];
        })())JS", "native-collapsed-select-initial.js");
    require(
        initial == R"(["one",240,39,true])",
        "single select did not begin as one collapsed native control: " + initial);

    pointer_button(engine, WEBSCENE_INPUT_POINTER_DOWN, 40, 35, 601U, true);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 40, 35, 602U, false);
    const auto clicked = evaluate(engine, R"JS(({
      value: choice.value,
      index: choice.selectedIndex,
      active: document.activeElement === choice,
      events: __selectEvents
    }))JS", "native-collapsed-select-clicked.js");
    require(
        clicked == R"({"value":"return","index":1,"active":true,"events":["click","input","change"]})",
        "native select activation did not change value/focus/events: " + clicked);

    keyboard_input(engine, WEBSCENE_INPUT_KEY_DOWN, 38U, 603U);
    keyboard_input(engine, WEBSCENE_INPUT_KEY_UP, 38U, 604U);
    const auto keyed = evaluate(engine, R"JS(({
      value: choice.value,
      index: choice.selectedIndex,
      events: __selectEvents
    }))JS", "native-collapsed-select-keyed.js");
    require(
        keyed == R"({"value":"one","index":0,"events":["click","input","change","keydown","input","change","keyup"]})",
        "ArrowUp did not navigate the focused native select: " + keyed);

    webscene_engine_request_scene_checkpoint(engine);
    auto observed_selected_label = false;
    auto observed_unselected_label = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind != 3U || command.flags >= scene->string_count) continue;
                const auto resource = scene->strings[command.flags];
                const std::string_view text(
                    scene->string_bytes + resource.byte_offset,
                    resource.byte_length);
                observed_selected_label = observed_selected_label
                    || text.ends_with("One-way flight");
                observed_unselected_label = observed_unselected_label
                    || text.ends_with("Return flight");
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (observed_selected_label) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        observed_selected_label && !observed_unselected_label,
        "the collapsed select scene did not project exactly its selected label");
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
