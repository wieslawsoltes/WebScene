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

void test_chart_printable_key_does_not_duplicate_into_newly_focused_search_input(
    webscene_engine* engine)
{
    execute_and_wait(engine, R"JS(
      (() => {
        document.body.innerHTML = '<main id="chart" tabindex="0">Chart</main>';
        chart.focus();
        globalThis.__chartSearchEvents = [];
        chart.addEventListener('keydown', event => {
          if (event.key !== 'T') return;
          const input = document.createElement('input');
          input.id = 'symbol-search';
          input.value = event.key;
          document.body.appendChild(input);
          input.focus();
          for (const type of ['beforeinput', 'input']) {
            input.addEventListener(type, () => __chartSearchEvents.push(type));
          }
        });
      })()
    )JS", "chart-type-to-search-setup.js");
    const auto consumed_before = consumed_input_count(engine);
    keyboard_input(engine, WEBSCENE_INPUT_KEY_DOWN, 'T', 680U);
    keyboard_input(engine, WEBSCENE_INPUT_TEXT, 'T', 681U);
    keyboard_input(engine, WEBSCENE_INPUT_KEY_UP, 'T', 682U);
    wait_for_consumed_inputs(
        engine,
        consumed_before + 3U,
        "chart type-to-search key sequence was not consumed");
    const auto result = evaluate(
        engine,
        R"JS(({
          value: document.getElementById('symbol-search')?.value,
          start: document.getElementById('symbol-search')?.selectionStart,
          end: document.getElementById('symbol-search')?.selectionEnd,
          events: __chartSearchEvents
        }))JS",
        "chart-type-to-search-result.js");
    require(
        result == R"({"value":"T","start":1,"end":1,"events":[]})",
        "chart key text was retargeted into the newly focused search input: " + result);
    require(
        evaluate(
            engine,
            R"JS((() => {
              const input = document.createElement('input');
              input.value = 'T';
              input.setSelectionRange(0, 0);
              document.body.appendChild(input);
              input.focus();
              return [input.selectionStart, input.selectionEnd];
            })())JS",
            "explicit-selection-before-focus-result.js") == R"([0,0])",
        "focus overwrote an explicitly authored text selection");
}

void test_enter_dispatches_browser_keypress_for_interval_commit(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '<style>body{margin:0}'
            + '#interval{position:absolute;left:0;top:0;width:120px;height:40px}'
            + '#cancelled{position:absolute;left:0;top:50px;width:120px;height:40px}</style>'
            + '<form id="intervalForm"><input id="interval" value="1"></form>'
            + '<textarea id="cancelled">stable</textarea>';
          globalThis.__interval = document.getElementById('interval');
          globalThis.__intervalForm = document.getElementById('intervalForm');
          globalThis.__cancelled = document.getElementById('cancelled');
          globalThis.__intervalEvents = [];
          globalThis.__selectedInterval = '1m';
          for (const type of ['keydown', 'keypress', 'keyup']) {
            __interval.addEventListener(type, event => {
              __intervalEvents.push({
                type,
                key: event.key,
                code: event.code,
                keyCode: event.keyCode,
                charCode: event.charCode,
                which: event.which
              });
            });
          }
          __intervalForm.addEventListener('submit', event => {
            event.preventDefault();
            __selectedInterval = __interval.value + 'm';
            globalThis.__submitterWasNull = event.submitter === null;
          });
          __cancelled.addEventListener('keypress', event => event.preventDefault());
        })()
    )JS", "native-enter-keypress-setup.js");
    require(
        evaluate(engine, "__interval.getBoundingClientRect().width",
            "native-enter-keypress-layout.js") == "120",
        "interval input was not laid out before keyboard activation");

    pointer_button(engine, WEBSCENE_INPUT_POINTER_DOWN, 20, 20, 509U, true);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 20, 20, 510U, false);
    keyboard_input(engine, WEBSCENE_INPUT_KEY_DOWN, 13U, 511U);
    keyboard_input(engine, WEBSCENE_INPUT_KEY_UP, 13U, 512U);
    const auto committed = evaluate(engine, R"JS(({
      selected: __selectedInterval,
      submitterWasNull: __submitterWasNull,
      events: __intervalEvents
    }))JS", "native-enter-keypress-commit.js");
    require(
        committed == R"({"selected":"1m","submitterWasNull":true,"events":[{"type":"keydown","key":"Enter","code":"Enter","keyCode":13,"charCode":0,"which":13},{"type":"keypress","key":"Enter","code":"Enter","keyCode":13,"charCode":13,"which":13},{"type":"keyup","key":"Enter","code":"Enter","keyCode":13,"charCode":0,"which":13}]})",
        "Enter did not perform the form submission used to commit an interval: "
            + committed);

    pointer_button(engine, WEBSCENE_INPUT_POINTER_DOWN, 20, 65, 513U, true);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 20, 65, 514U, false);
    keyboard_input(engine, WEBSCENE_INPUT_KEY_DOWN, 13U, 515U);
    keyboard_input(engine, WEBSCENE_INPUT_KEY_UP, 13U, 516U);
    require(
        evaluate(engine, "__cancelled.value", "native-cancelled-enter-value.js")
            == R"("stable")",
        "preventDefault on keypress did not suppress the textarea Enter default action");
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

void test_controlled_switch_native_activation_matches_browser_semantics()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "controlled-switch engine creation failed");
    resize(engine, 500, 240, 8100U);
    execute_and_wait(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              html, body { margin: 0; width: 100%; height: 100%; }
              .switcher {
                display: inline-block; position: relative; width: 38px; height: 20px;
                vertical-align: middle; z-index: 0;
              }
              .switcher input {
                position: absolute; top: 0; left: 0; width: 100%; height: 100%;
                opacity: 0; z-index: 1;
              }
              .thumb-wrapper {
                display: block; border-radius: 20px; position: relative;
                width: 100%; height: 100%;
              }
              .track {
                position: absolute; left: 0; top: 0; width: 100%; height: 100%;
                border-radius: 20px; background: rgb(163, 166, 175);
              }
              input:checked + .thumb-wrapper .track { background: rgb(41, 98, 255); }
              .thumb {
                display: block; width: 14px; height: 14px; border-radius: 14px;
                transform: translate(3px, 3px);
                transition: transform 250ms linear;
                background: white;
              }
              input:checked + .thumb-wrapper .thumb {
                transform: translate(21px, 3px);
              }
              .switcher:focus-within::before {
                content: ''; display: block; position: absolute;
                top: -2px; right: -2px; bottom: -2px; left: -2px;
                border-radius: 16px; outline: rgb(41, 98, 255) solid 2px;
              }
              #controlled-shell { position: absolute; left: 20px; top: 20px; }
              #cancelled-shell { position: absolute; left: 100px; top: 20px; }
              #react-shell {
                --default-color: rgb(80, 80, 80);
                --switch-color: var(--hover-color, var(--default-color));
                position: absolute; left: 180px; top: 20px;
              }
              #react-shell.checked { --default-color: rgb(255, 255, 255); }
              #react-shell .react-input:hover + .react-wrapper {
                --hover-color: rgb(20, 20, 20);
              }
              #react-shell .react-input.checked:hover + .react-wrapper {
                --hover-color: rgb(255, 255, 255);
              }
              #react-track { background-color: var(--switch-color); }
            </style>
            <div id="controlled-shell" class="switcher">
              <input id="controlled" type="checkbox" role="switch" tabindex="-1">
              <span class="thumb-wrapper"><span class="track"></span><span id="thumb" class="thumb"></span></span>
            </div>
            <div id="cancelled-shell" class="switcher">
              <input id="cancelled" type="checkbox" role="switch" tabindex="-1">
              <span class="thumb-wrapper"><span class="track"></span><span class="thumb"></span></span>
            </div>
            <div id="react-shell" class="switcher">
              <input id="react-controlled" class="react-input" type="checkbox" role="switch">
              <span class="thumb-wrapper react-wrapper">
                <span id="react-track" class="track"></span><span class="thumb"></span>
              </span>
            </div>`;

          const installControlledTracker = (input, cancel) => {
            const descriptor = Object.getOwnPropertyDescriptor(
              input.constructor.prototype, 'checked');
            const state = {
              tracked: String(input.checked), setterCalls: 0,
              syntheticChanges: 0, inputEvents: 0, changeEvents: 0, events: []
            };
            Object.defineProperty(input, 'checked', {
              configurable: true,
              get() { return descriptor.get.call(this); },
              set(value) {
                state.setterCalls++;
                state.tracked = String(value);
                descriptor.set.call(this, value);
              }
            });
            input.addEventListener('click', event => {
              state.events.push(`click:${input.checked}:${state.tracked}`);
              if (cancel) event.preventDefault();
            });
            input.addEventListener('input', () => {
              state.inputEvents++;
              state.events.push('input');
            });
            input.addEventListener('change', () => {
              state.changeEvents++;
              state.events.push('change');
            });
            document.addEventListener('click', event => {
              if (event.target !== input) return;
              const next = String(input.checked);
              if (next !== state.tracked) {
                state.tracked = next;
                state.syntheticChanges++;
                state.events.push('synthetic-change');
              }
            });
            return state;
          };
          globalThis.__controlledState = installControlledTracker(
            document.getElementById('controlled'), false);
          globalThis.__cancelledState = installControlledTracker(
            document.getElementById('cancelled'), true);
          const reactControlled = document.getElementById('react-controlled');
          reactControlled.addEventListener('click', () => {
            document.getElementById('react-shell').classList.toggle(
              'checked', reactControlled.checked);
            reactControlled.classList.toggle(
              'checked', reactControlled.checked);
          });
        })()
    )JS", "controlled-switch-browser-semantics-setup.js");
    animation_frame_and_wait(engine, 10000.0, 8101U);

    pointer_button(engine, WEBSCENE_INPUT_POINTER_DOWN, 39, 30, 8102U, true);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 39, 30, 8103U, false);
    const auto activated = evaluate(engine, R"JS((() => {
      const controlled = document.getElementById('controlled');
      const shell = document.getElementById('controlled-shell');
      const thumb = document.getElementById('thumb');
      const matrix = getComputedStyle(thumb).transform.slice(7, -1).split(',').map(Number);
      return {
        checked: controlled.checked,
        ownDescriptor: !!Object.getOwnPropertyDescriptor(controlled, 'checked'),
        setterCalls: __controlledState.setterCalls,
        syntheticChanges: __controlledState.syntheticChanges,
        tracked: __controlledState.tracked,
        events: __controlledState.events,
        focused: document.activeElement === controlled,
        focusWithin: shell.matches(':focus-within'),
        translate: [matrix[4], matrix[5]]
      };
    })())JS", "controlled-switch-browser-semantics-activated.js");
    require(
        activated == R"JSON({"checked":true,"ownDescriptor":true,"setterCalls":0,"syntheticChanges":1,"tracked":"true","events":["click:true:false","synthetic-change","input","change"],"focused":true,"focusWithin":true,"translate":[3,3]})JSON",
        "native activation did not preserve React-controlled switch semantics: "
            + activated);
    require(
        webscene_engine_requires_animation_frame(engine) != 0,
        "controlled switch transform transition did not request a host frame");

    webscene_engine_request_scene_checkpoint(engine);
    auto observed_rounded_focus_outline = false;
    for (auto attempt = 0; attempt < 100 && !observed_rounded_focus_outline; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                observed_rounded_focus_outline = observed_rounded_focus_outline
                    || ((command.kind == 8U || command.kind == 11U)
                        && (command.rgba >> 8U) == 0x2962FFU
                        && std::abs(command.stroke_width - 2.0F) < 0.01F
                        && command.radius_top_left > 12.0F
                        && command.radius_top_right > 12.0F
                        && command.radius_bottom_right > 12.0F
                        && command.radius_bottom_left > 12.0F);
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (!observed_rounded_focus_outline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(
        observed_rounded_focus_outline,
        "focused switch did not publish its rounded blue outline");

    animation_frame_and_wait(engine, 10125.0, 8104U);
    const auto midpoint = evaluate(engine, R"JS((() => {
      const thumb = document.getElementById('thumb');
      const matrix = getComputedStyle(thumb).transform.slice(7, -1).split(',').map(Number);
      return { x: matrix[4], y: matrix[5], inFlight: matrix[4] > 3 && matrix[4] < 21 };
    })())JS", "controlled-switch-transition-midpoint.js");
    require(
        midpoint.find(R"("y":3)") != std::string::npos
            && midpoint.find(R"("inFlight":true)") != std::string::npos,
        "controlled switch thumb did not expose a smooth transition midpoint: "
            + midpoint);
    animation_frame_and_wait(engine, 10250.0, 8105U);
    require(
        evaluate(engine, R"JS((() => {
          const thumb = document.getElementById('thumb');
          const matrix = getComputedStyle(thumb).transform.slice(7, -1).split(',').map(Number);
          return [matrix[4], matrix[5]];
        })())JS", "controlled-switch-transition-complete.js") == "[21,3]",
        "controlled switch thumb did not finish at its checked transform");

    // Compositor callbacks continue while rendering is idle, but they must not
    // submit empty V8 frames. Observe that inexpensive boundary explicitly,
    // then activate the control after a long idle gap. Using the last demanded
    // frame (10250) as the new transition start would make the first 20016
    // sample jump directly to the unchecked endpoint.
    webscene_engine_observe_compositor_frame(engine, 20000.0);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_DOWN, 39, 30, 8106U, true);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 39, 30, 8107U, false);
    wait_for_animation_frame_demand(
        engine,
        true,
        "idle controlled-switch transition did not request a host frame");
    animation_frame_and_wait(engine, 20016.0, 8108U);
    const auto idle_first_sample = evaluate(engine, R"JS((() => {
      const matrix = getComputedStyle(document.getElementById('thumb')).transform
        .slice(7, -1).split(',').map(Number);
      return { x: matrix[4], inFlight: matrix[4] > 3 && matrix[4] < 21 };
    })())JS", "controlled-switch-idle-first-sample.js");
    require(
        idle_first_sample.find(R"("inFlight":true)") != std::string::npos,
        "controlled switch transition jumped after an idle compositor gap: "
            + idle_first_sample);

    std::vector<std::string> idle_transition_samples;
    for (uint64_t index = 2U; index <= 16U; ++index) {
        animation_frame_and_wait(
            engine,
            20000.0 + static_cast<double>(index * 16U),
            8107U + index);
        idle_transition_samples.push_back(evaluate(engine, R"JS((() => {
          const matrix = getComputedStyle(document.getElementById('thumb')).transform
            .slice(7, -1).split(',').map(Number);
          return String(Math.round(matrix[4] * 1000) / 1000);
        })())JS", "controlled-switch-idle-dense-sample.js"));
    }
    std::sort(idle_transition_samples.begin(), idle_transition_samples.end());
    const auto distinct_idle_samples = std::unique(
        idle_transition_samples.begin(), idle_transition_samples.end())
        - idle_transition_samples.begin();
    require(
        distinct_idle_samples >= 10,
        "controlled switch transition did not publish dense vsync samples after idle");

    pointer_move(engine, 199, 30, 8190U);
    require(
        evaluate(engine,
            "getComputedStyle(document.getElementById('react-track')).backgroundColor",
            "controlled-switch-unchecked-hover-color.js") == "\"rgb(20, 20, 20)\"",
        "controlled switch fixture did not enter its unchecked hover color");
    pointer_button(engine, WEBSCENE_INPUT_POINTER_DOWN, 199, 30, 8191U, true);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 199, 30, 8192U, false);
    const auto checked_hover_background = evaluate(engine, R"JS((() => ({
      checked: document.getElementById('react-controlled').checked,
      background: getComputedStyle(document.getElementById('react-track')).backgroundColor
    }))())JS", "controlled-switch-checked-hover-color.js");
    require(
        checked_hover_background
            == R"JSON({"checked":true,"background":"rgb(255, 255, 255)"})JSON",
        "checked sibling hover color remained stale until pointer exit: "
            + checked_hover_background);

    pointer_button(engine, WEBSCENE_INPUT_POINTER_DOWN, 119, 30, 8200U, true);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 119, 30, 8201U, false);
    const auto cancelled = evaluate(engine, R"JS((() => {
      const cancelled = document.getElementById('cancelled');
      return ({
      checked: cancelled.checked,
      setterCalls: __cancelledState.setterCalls,
      syntheticChanges: __cancelledState.syntheticChanges,
      tracked: __cancelledState.tracked,
      inputEvents: __cancelledState.inputEvents,
      changeEvents: __cancelledState.changeEvents,
      events: __cancelledState.events
      });
    })())JS", "controlled-switch-browser-semantics-cancelled.js");
    require(
        cancelled == R"JSON({"checked":false,"setterCalls":0,"syntheticChanges":1,"tracked":"true","inputEvents":0,"changeEvents":0,"events":["click:true:false","synthetic-change"]})JSON",
        "preventDefault did not restore native checkedness without firing form events: "
            + cancelled);

    webscene_engine_destroy(engine);
}

void test_tradingview_switch_repeated_transitions_publish_dense_scenes()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "repeated-switch engine creation failed");
    resize(engine, 240, 120, 8300U);
    execute_and_wait(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              html, body { margin: 0; width: 100%; height: 100%; }
              .switcher { position: absolute; left: 20px; top: 20px; }
              .input {
                cursor: default; height: 100%; left: 0; opacity: 0;
                position: absolute; top: 0; width: 100%; z-index: 1;
              }
              .switchView {
                --switch-height: 20px;
                --switch-width: 38px;
                --switch-padding: 3px;
                --switch-thumb: calc(var(--switch-height) - var(--switch-padding) * 2);
                display: block; height: var(--switch-height);
                position: relative; width: var(--switch-width);
              }
              .track {
                background-color: rgb(80, 80, 80); border-radius: 20px;
                display: block; height: 100%; left: 0;
                position: absolute; top: 0; width: 100%;
              }
              .thumb {
                background: rgb(255, 255, 255); border-radius: 50%; display: block;
                height: var(--switch-thumb);
                transform: translate(var(--switch-padding), var(--switch-padding));
                transition-duration: .25s;
                transition-property: transform;
                transition-timing-function: ease-out;
                width: var(--switch-thumb);
              }
              .checked .thumb {
                transform: translate(
                  calc(var(--switch-width) - var(--switch-thumb) - var(--switch-padding)),
                  var(--switch-padding));
              }
            </style>
            <label class="switcher">
              <input id="input" class="input" type="checkbox" role="switch">
              <span id="view" class="switchView">
                <span class="track"></span><span id="thumb" class="thumb"></span>
              </span>
            </label>`;
          const input = document.getElementById('input');
          input.addEventListener('click', () =>
            document.getElementById('view').classList.toggle('checked', input.checked));
        })()
    )JS", "tradingview-repeated-switch-setup.js");
    animation_frame_and_wait(engine, 1000.0, 8301U);

    const auto acquire_thumb_x = [&](uint64_t minimum_sequence) {
        auto thumb_x = -1.0F;
        for (auto attempt = 0; attempt < 200 && thumb_x < 0; ++attempt) {
            const auto* scene = webscene_engine_acquire_latest_scene(engine);
            if (scene != nullptr) {
                if (scene->header.consumed_input_sequence >= minimum_sequence) {
                    for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                        const auto& command = scene->commands[index];
                        if ((command.kind == 7U || command.kind == 10U)
                            && command.rgba == 0xFFFFFFFFU
                            && std::abs(command.width - 14.0F) < 0.1F
                            && std::abs(command.height - 14.0F) < 0.1F) {
                            thumb_x = command.x;
                            break;
                        }
                    }
                }
                webscene_scene_acknowledge(scene);
                webscene_scene_release(scene);
            }
            if (thumb_x < 0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return thumb_x;
    };

    webscene_engine_request_scene_checkpoint(engine);
    require(acquire_thumb_x(8301U) >= 22.9F,
        "initial TradingView-shaped switch scene did not contain its thumb");

    auto timestamp = 2000.0;
    uint64_t sequence = 8310U;
    for (auto cycle = 0; cycle < 4; ++cycle) {
        webscene_engine_observe_compositor_frame(engine, timestamp);
        pointer_button(engine, WEBSCENE_INPUT_POINTER_DOWN, 39, 30, sequence++, true);
        pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 39, 30, sequence++, false);
        wait_for_animation_frame_demand(
            engine,
            true,
            "repeated TradingView switch activation did not request host frames");

        std::vector<float> painted_samples;
        for (auto sample = 1; sample <= 16; ++sample) {
            const auto frame_sequence = sequence++;
            animation_frame_and_wait(
                engine,
                timestamp + static_cast<double>(sample * 16),
                frame_sequence);
            const auto thumb_x = acquire_thumb_x(frame_sequence);
            require(thumb_x >= 0,
                "repeated TradingView switch frame did not publish a thumb scene");
            painted_samples.push_back(thumb_x);
        }

        auto distinct_samples = painted_samples;
        std::sort(distinct_samples.begin(), distinct_samples.end());
        const auto distinct_count = std::unique(
            distinct_samples.begin(), distinct_samples.end(),
            [](float left, float right) { return std::abs(left - right) < 0.01F; })
            - distinct_samples.begin();
        require(
            distinct_count >= 10,
            "repeated TradingView switch transition did not publish dense retained scenes");
        for (size_t index = 1; index < painted_samples.size(); ++index) {
            const auto monotonic = cycle % 2 == 0
                ? painted_samples[index] + 0.01F >= painted_samples[index - 1]
                : painted_samples[index] <= painted_samples[index - 1] + 0.01F;
            require(monotonic,
                "repeated TradingView switch transition reversed between compositor frames");
        }
        wait_for_animation_frame_demand(
            engine,
            false,
            "completed TradingView switch transition retained host-frame demand");
        timestamp += 1000.0;
    }

    webscene_engine_destroy(engine);
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
              .horizontal-resize { display:block;width:180px;height:48px;cursor:ew-resize; }
              .vertical-resize { display:block;width:180px;height:48px;cursor:ns-resize; }
            </style>
            <a id="badge" class="badge" href="https://example.com/?source=webscene">
              <span>Hosted chart</span>
            </a>
            <a id="cancelled" class="badge" href="https://example.invalid/blocked">blocked</a>
            <div id="horizontal" class="horizontal-resize"></div>
            <div id="vertical" class="vertical-resize"></div>`;
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

    pointer_move(engine, 20, 110, 506U);
    require(
        evaluate(engine, "true", "native-horizontal-resize-cursor-barrier.js") == "true"
            && webscene_engine_get_cursor(engine) == WEBSCENE_CURSOR_EW_RESIZE,
        "cursor:ew-resize was not projected to the host horizontal resize cursor");

    pointer_move(engine, 20, 155, 507U);
    require(
        evaluate(engine, "true", "native-vertical-resize-cursor-barrier.js") == "true"
            && webscene_engine_get_cursor(engine) == WEBSCENE_CURSOR_NS_RESIZE,
        "cursor:ns-resize was not projected to the host vertical resize cursor");

    pointer_move(engine, 400, 300, 508U);
    require(
        evaluate(engine, "true", "native-cursor-exit-barrier.js") == "true"
            && webscene_engine_get_cursor(engine) == WEBSCENE_CURSOR_DEFAULT,
        "leaving cursor:pointer did not restore the default host cursor");

    const auto tradingview_download_state = evaluate(engine, R"JS(
        (() => {
          // TradingView's downloadFile helper uses a detached anchor, assigns
          // the reflected IDL properties, and gives it canvas.toDataURL().
          const download = document.createElement('a');
          download.style.display = 'none';
          download.href = 'data:image/png;base64,iVBORw==';
          download.download = 'AAPL_2026-08-25_14-30-00.png';
          download.click();
          return {
            connected: download.isConnected,
            downloadProperty: download.download,
            hasOwnDownload: Object.prototype.hasOwnProperty.call(download, 'download'),
            hrefAttribute: download.getAttribute('href'),
            downloadAttribute: download.getAttribute('download')
          };
        })()
    )JS", "native-tradingview-download-anchor.js");
    require(
        tradingview_download_state
            == R"({"connected":false,"downloadProperty":"AAPL_2026-08-25_14-30-00.png","hasOwnDownload":false,"hrefAttribute":"data:image/png;base64,iVBORw==","downloadAttribute":"AAPL_2026-08-25_14-30-00.png"})",
        "TradingView-style anchor properties did not reflect before detached activation: "
            + tradingview_download_state);
    const auto download_request = take_host_request(engine);
    require(
        download_request.find(R"("kind":"download")") != std::string::npos
            && download_request.find(
                R"("suggestedFileName":"AAPL_2026-08-25_14-30-00.png")")
                != std::string::npos
            && download_request.find(R"("url":"data:image/png;base64,iVBORw==")")
                != std::string::npos,
        "TradingView-style detached data-URL download did not reach the host save request: "
            + download_request);

    execute(engine, R"JS(
        (() => {
          globalThis.__blobFrameLoaded = '';
          globalThis.__sharedBlobUrl = URL.createObjectURL(new Blob(
            ["<script>parent.__blobFrameLoaded='ok'</script>"],
            { type: 'text/html' }));
          const frame = document.createElement('iframe');
          frame.setAttribute('src', globalThis.__sharedBlobUrl);
          document.body.appendChild(frame);
        })()
    )JS", "native-blob-frame-setup.js");
    auto blob_frame_loaded = std::string{};
    for (auto attempt = 0; attempt < 100; ++attempt) {
        blob_frame_loaded = evaluate(
            engine,
            "globalThis.__blobFrameLoaded",
            "native-blob-frame-result.js");
        if (blob_frame_loaded == R"("ok")") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        blob_frame_loaded == R"("ok")",
        "Blob object URL navigation did not hydrate the iframe as HTML: "
            + blob_frame_loaded);

    execute(engine, R"JS(
        (() => {
          const download = document.createElement('a');
          download.setAttribute('href', globalThis.__sharedBlobUrl);
          download.setAttribute('download', 'frame.html');
          document.body.appendChild(download);
          download.click();
        })()
    )JS", "native-shared-blob-download.js");
    require(
        evaluate(engine, "true", "native-shared-blob-download-barrier.js") == "true",
        "shared Blob download activation did not drain");
    const auto shared_blob_download = take_host_request(engine);
    require(
        shared_blob_download.find(R"("kind":"download")") != std::string::npos
            && shared_blob_download.find(R"("suggestedFileName":"frame.html")")
                != std::string::npos
            && shared_blob_download.find(
                R"("url":"data:text/html;base64,PHNjcmlwdD5wYXJlbnQuX19ibG9iRnJhbWVMb2FkZWQ9J29rJzwvc2NyaXB0Pg==")")
                != std::string::npos,
        "one Blob object URL did not preserve its distinct navigation and download representations: "
            + shared_blob_download);

    const auto screenshot_download_state = evaluate(engine, R"JS(
        (() => {
          const canvas = document.createElement('canvas');
          canvas.width = 320;
          canvas.height = 180;
          canvas.getContext('2d').fillRect(0, 0, 320, 180);
          globalThis.__screenshotCanvas = canvas;
          const download = document.createElement('a');
          download.href = canvas.toDataURL();
          download.download = 'AAPL_2026-08-25_14-30-01.png';
          download.click();
          return {
            hasToDataURL: typeof canvas.toDataURL === 'function',
            hasToBlob: typeof canvas.toBlob === 'function',
            token: download.getAttribute('href')
          };
        })()
    )JS", "native-tradingview-canvas-download.js");
    require(
        screenshot_download_state.find(R"("hasToDataURL":true)") != std::string::npos
            && screenshot_download_state.find(R"("hasToBlob":true)") != std::string::npos
            && screenshot_download_state.find("webscene-canvas-snapshot:")
                != std::string::npos,
        "canvas export methods did not expose the host snapshot token: "
            + screenshot_download_state);
    const auto screenshot_download_request = take_host_request(engine);
    require(
        screenshot_download_request.find(R"("kind":"download")") != std::string::npos
            && screenshot_download_request.find(
                R"("suggestedFileName":"AAPL_2026-08-25_14-30-01.png")")
                != std::string::npos
            && screenshot_download_request.find(R"("canvasNodeId":)")
                != std::string::npos,
        "TradingView canvas download did not reach the host capture request: "
            + screenshot_download_request);

    auto detached_canvas_retained = false;
    webscene_engine_request_scene_checkpoint(engine);
    for (auto attempt = 0; attempt < 100 && !detached_canvas_retained; ++attempt) {
        if (const auto* scene = webscene_engine_acquire_latest_scene(engine);
            scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.canvas_layer_count; ++index) {
                const auto& layer = scene->canvas_layers[index];
                if (layer.bitmap_width == 320U
                    && layer.bitmap_height == 180U
                    && (layer.reserved & (1U << 31U)) != 0U) {
                    detached_canvas_retained = true;
                    break;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (!detached_canvas_retained) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(
        detached_canvas_retained,
        "detached screenshot canvas was not retained for node-scoped export");

    execute(engine, R"JS(
        (() => {
          globalThis.__screenshotCopyState = 'pending';
          const blob = new Promise(resolve =>
            __screenshotCanvas.toBlob(value => {
              if (value !== null) resolve(value);
            }));
          navigator.clipboard.write([
            new ClipboardItem({ 'image/png': blob })
          ]).then(
            () => { globalThis.__screenshotCopyState = 'done'; },
            error => { globalThis.__screenshotCopyState = String(error); });
        })()
    )JS", "native-tradingview-canvas-copy.js");
    auto screenshot_copy_state = std::string{};
    for (auto attempt = 0; attempt < 100; ++attempt) {
        screenshot_copy_state = evaluate(
            engine,
            "globalThis.__screenshotCopyState",
            "native-tradingview-canvas-copy-result.js");
        if (screenshot_copy_state == R"("done")") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        screenshot_copy_state == R"("done")",
        "TradingView ClipboardItem promise did not resolve: " + screenshot_copy_state);
    const auto screenshot_clipboard_request = take_host_request(engine);
    require(
        screenshot_clipboard_request.find(R"("kind":"writeClipboard")")
                != std::string::npos
            && screenshot_clipboard_request.find(R"("contentType":"image/png")")
                != std::string::npos
            && screenshot_clipboard_request.find(R"("canvasNodeId":)")
                != std::string::npos,
        "TradingView canvas copy did not reach the typed host clipboard request: "
            + screenshot_clipboard_request);

    const auto screenshot_node_id = static_cast<uint32_t>(std::stoul(evaluate(
        engine,
        "globalThis.__screenshotCanvas.__websceneNativeNodeId",
        "native-screenshot-export-node-id.js")));
    require(
        webscene_engine_release_canvas_export(engine, screenshot_node_id) != 0U,
        "the host could not release the retained screenshot canvas");
    execute(engine, "true", "native-screenshot-export-release-barrier.js");
    webscene_engine_request_scene_checkpoint(engine);
    auto released = false;
    for (auto attempt = 0; attempt < 100 && !released; ++attempt) {
        if (const auto* scene = webscene_engine_acquire_latest_scene(engine);
            scene != nullptr) {
            released = true;
            for (uint32_t index = 0; index < scene->header.canvas_layer_count; ++index) {
                if (scene->canvas_layers[index].node_id == screenshot_node_id) {
                    released = false;
                    break;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (!released) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(released,
        "the exported screenshot canvas remained in later retained scenes");
}

void test_css_linear_gradient_reaches_the_retained_scene(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = `<style>
            body { margin: 0; }
            #swatch {
              width: 40px;
              height: 24px;
              border: 1px solid rgb(120, 130, 140);
              border-radius: 4px;
            }
          </style><div id="swatch"></div>`;
          // TradingView's opacity control writes this exact named CSSOM
          // property on its gradient child rather than using a stylesheet.
          swatch.style.backgroundImage =
            'linear-gradient(90deg, transparent, #000000)';
        })()
    )JS", "native-linear-gradient-setup.js");
    require(
        evaluate(engine, "getComputedStyle(swatch).backgroundImage",
            "native-linear-gradient-cssom.js")
            .find("linear-gradient") != std::string::npos,
        "an inline backgroundImage linear-gradient did not survive the CSS cascade");

    execute(engine, R"JS(
        swatch.style.backgroundImage =
          'linear-gradient(90deg, transparent, #112233), '
          + 'linear-gradient(135deg, #707070 50%, #121212 50%)';
        swatch.style.backgroundSize = 'auto, 8px 8px';
    )JS", "native-linear-gradient-mutation.js");

    webscene_engine_request_scene_checkpoint(engine);
    auto found = false;
    auto found_border = false;
    auto observed_resource = std::string{};
    for (auto attempt = 0; attempt < 100 && (!found || !found_border); ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            auto swatch_node_id = 0U;
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind != 21U || command.flags >= scene->string_count) continue;
                const auto& descriptor = scene->strings[command.flags];
                const std::string resource(
                    scene->string_bytes + descriptor.byte_offset,
                    descriptor.byte_length);
                observed_resource = resource;
                found = command.node_id != 0U
                    && std::abs(command.width - 42.0F) < 0.1F
                    && std::abs(command.height - 26.0F) < 0.1F
                    && resource.find("linear-gradient") != std::string::npos
                    && resource.find("transparent") != std::string::npos
                    && resource.find("#112233") != std::string::npos
                    && resource.find("#707070") != std::string::npos
                    && resource.starts_with("webscene-bg-v2\t")
                    && resource.ends_with("\tauto, 8px 8px");
                if (found) swatch_node_id = command.node_id;
            }
            for (uint32_t index = 0;
                swatch_node_id != 0U && index < scene->header.command_count;
                ++index) {
                const auto& command = scene->commands[index];
                if ((command.kind == 8U || command.kind == 11U)
                    && command.node_id == swatch_node_id
                    && command.rgba == 0x78828CFFU
                    && std::abs(command.stroke_width - 1.0F) < 0.01F) {
                    found_border = true;
                    break;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (!found || !found_border) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(found,
        "a mutated inline backgroundImage linear-gradient was not published "
            "as a retained gradient command: " + observed_resource);
    require(found_border,
        "the rounded opacity swatch border did not remain above its layered gradient");
}
