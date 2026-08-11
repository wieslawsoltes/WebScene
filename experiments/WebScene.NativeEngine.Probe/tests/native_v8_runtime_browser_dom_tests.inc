void test_secondary_click(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const target = document.createElement('div');
          target.style.width = '100px';
          target.style.height = '100px';
          document.body.appendChild(target);
          globalThis.__secondaryEvents = [];
          for (const type of ['pointerdown','mousedown','contextmenu','pointerup','mouseup','auxclick']) {
            target.addEventListener(type, event => __secondaryEvents.push([
              event.type, event.button, event.buttons, event.which, event.metaKey, event.shiftKey
            ]));
          }
        })()
    )JS", "native-secondary-click-setup.js");
    require(
        evaluate(engine, "globalThis.__secondaryEvents.length", "native-secondary-click-ready.js") == "0",
        "secondary-click target setup did not complete");

    const auto pointer_modifiers =
        WEBSCENE_INPUT_POINTER_MODIFIER_META | WEBSCENE_INPUT_POINTER_MODIFIER_SHIFT;
    const webscene_input_event down{
        WEBSCENE_INPUT_POINTER_DOWN, 2U | (3U << 8U) | pointer_modifiers, 3U, 10, 10, 0, 0};
    const webscene_input_event up{
        WEBSCENE_INPUT_POINTER_UP, (3U << 8U) | pointer_modifiers, 4U, 10, 10, 0, 0};
    const auto consumed_before_click = consumed_input_count(engine);
    require(webscene_engine_enqueue(engine, &down) != 0, "right-button down was rejected");
    require(webscene_engine_enqueue(engine, &up) != 0, "right-button up was rejected");
    wait_for_consumed_inputs(
        engine,
        consumed_before_click + 2,
        "secondary click inputs were not consumed");
    const auto result = evaluate(
        engine,
        "globalThis.__secondaryEvents",
        "native-secondary-click-result.js");
    if (result != R"([["pointerdown",2,2,3,true,true],["mousedown",2,2,3,true,true],["contextmenu",2,2,3,true,true],["pointerup",2,0,3,true,true],["mouseup",2,0,3,true,true],["auxclick",2,0,3,true,true]])") {
        fail("secondary-click event sequence, button fields, or modifiers regressed: " + result);
    }
}

void test_primary_click_mouse_event_detail(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const target = document.createElement('div');
          target.style.position = 'absolute';
          target.style.left = '0';
          target.style.top = '0';
          target.style.width = '100px';
          target.style.height = '100px';
          target.style.zIndex = '999999';
          document.body.appendChild(target);
          globalThis.__primaryEventDetails = [];
          for (const type of ['pointerdown', 'mousedown', 'pointerup', 'mouseup', 'click']) {
            target.addEventListener(type, event => {
              __primaryEventDetails.push([event.type, event.detail]);
            });
          }
          target.addEventListener('mousedown', event => event.preventDefault());
        })()
    )JS", "native-primary-click-detail-setup.js");
    require(
        evaluate(engine, "document.body.firstElementChild?.offsetWidth", "native-primary-click-detail-ready.js")
            == "100",
        "primary click detail target did not complete layout");
    const webscene_input_event down{
        WEBSCENE_INPUT_POINTER_DOWN, 1U | (1U << 8U), 91U, 10, 10, 0, 0};
    const webscene_input_event up{
        WEBSCENE_INPUT_POINTER_UP, (1U << 8U), 92U, 10, 10, 0, 0};
    const auto consumed_before_click = consumed_input_count(engine);
    require(webscene_engine_enqueue(engine, &down) != 0, "primary click down was rejected");
    require(webscene_engine_enqueue(engine, &up) != 0, "primary click up was rejected");
    wait_for_consumed_inputs(
        engine,
        consumed_before_click + 2,
        "primary click inputs were not consumed");
    const auto result = evaluate(
        engine,
        "({ events: globalThis.__primaryEventDetails, activeTag: document.activeElement?.tagName, activeId: document.activeElement?.id || null })",
        "native-primary-click-detail-result.js");
    if (result != R"({"events":[["pointerdown",0],["mousedown",1],["pointerup",0],["mouseup",1],["click",1]],"activeTag":"BODY","activeId":null})") {
        fail("primary event detail or canceled-mousedown focus default diverged from Chrome: " + result);
    }
}

void test_native_mouseup_honors_immediate_propagation_stop(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const parent = document.createElement('div');
          parent.style.position = 'fixed';
          parent.style.left = '0';
          parent.style.top = '0';
          parent.style.width = '160px';
          parent.style.height = '100px';
          const target = document.createElement('button');
          target.style.width = '100px';
          target.style.height = '60px';
          parent.appendChild(target);
          document.body.appendChild(parent);
          globalThis.__nativeMouseupStopTrace = [];
          target.addEventListener('mouseup', event => {
            globalThis.__nativeMouseupStopTrace.push('first-target');
            event.stopImmediatePropagation();
          });
          target.addEventListener('mouseup', () => {
            globalThis.__nativeMouseupStopTrace.push('second-target');
          });
          parent.addEventListener('mouseup', () => {
            globalThis.__nativeMouseupStopTrace.push('parent');
          });
        })()
    )JS", "native-mouseup-stop-setup.js");
    require(
        evaluate(
            engine,
            "Array.isArray(globalThis.__nativeMouseupStopTrace)",
            "native-mouseup-stop-ready.js") == "true",
        "native mouseup propagation fixture did not initialize");

    pointer_move(engine, 50, 30, 240U);
    pointer_button(
        engine,
        WEBSCENE_INPUT_POINTER_DOWN,
        50,
        30,
        241U,
        true);
    pointer_button(
        engine,
        WEBSCENE_INPUT_POINTER_UP,
        50,
        30,
        242U,
        false);
    const auto result = evaluate(
        engine,
        "globalThis.__nativeMouseupStopTrace",
        "native-mouseup-stop-result.js");
    if (result != R"(["first-target"])") {
        fail("native mouseup ignored stopImmediatePropagation: " + result);
    }
}

void test_component_library_dom_discovery_primitives(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <div id="scope-root">
              <a id="trigger"><span id="nested"></span></a>
              <div class="collapse"><div><div class="collapse" id="nested-collapse"></div></div></div>
            </div>`;
          const root = document.querySelector('#scope-root');
          const nested = document.querySelector('#nested');
          const trigger = document.querySelector('#trigger');
          const escaped = document.createElement('div');
          escaped.id = '0/my id';
          root.appendChild(escaped);
          const loneSurrogate = document.createElement('div');
          loneSurrogate.id = '\ud83dsurrogateFirst';
          root.appendChild(loneSurrogate);
          const replacement = document.createElement('div');
          replacement.setAttribute('id', '\ufffdsurrogateFirst');
          root.appendChild(replacement);
          const original = Event.prototype.preventDefault;
          let prototypeCalls = 0;
          let clickContract = null;
          Event.prototype.preventDefault = function() {
            prototypeCalls++;
            return original.call(this);
          };
          document.addEventListener('click', event => {
            if (event.target !== nested) return;
            event.delegateTarget = trigger;
            event.preventDefault();
          }, { once: true, capture: true });
          trigger.addEventListener('click', event => {
            clickContract = {
              target: event.target.isEqualNode(nested),
              delegate: event.delegateTarget.isEqualNode(trigger),
              prevented: event.defaultPrevented,
              prototype: Event.prototype.isPrototypeOf(event)
            };
          }, { once: true });
          nested.click();
          Event.prototype.preventDefault = original;
          const clone = root.cloneNode(true);
          const input = document.createElement('input');
          input.setAttribute('value', 'tester');
          void input.value;
          const clonedInput = input.cloneNode(true);
          clonedInput.setAttribute('value', 'test');
          const dirtyInput = input.cloneNode(true);
          dirtyInput.value = 'dirty';
          dirtyInput.setAttribute('value', 'new default');
          return {
            scope: Array.from(root.querySelectorAll(':scope .collapse .collapse'), node => node.id),
            escape: CSS.escape('0/my id'),
            escapedMatch: root.querySelector('#' + CSS.escape('0/my id')) === escaped,
            surrogate: {
              propertyRoundTrip: loneSurrogate.id === '\ud83dsurrogateFirst',
              attributeRoundTrip: loneSurrogate.getAttribute('id') === '\ud83dsurrogateFirst',
              literalMatch: root.querySelector('#\ud83dsurrogateFirst') === loneSurrogate,
              escapedReplacement: root.querySelector('#\\d83d surrogateFirst') === replacement
            },
            click: clickContract,
            prototypeCalls,
            same: root.isSameNode(root) && !root.isSameNode(clone),
            equal: root.isEqualNode(clone),
            clonedValueReflectsAttribute: clonedInput.value === 'test',
            dirtyValueSurvivesAttribute: dirtyInput.value === 'dirty'
          };
        })()
    )JS", "native-component-library-dom-discovery-primitives.js");
    const auto expected = R"({"scope":["nested-collapse"],"escape":"\\30 \\/my\\ id","escapedMatch":true,"surrogate":{"propertyRoundTrip":true,"attributeRoundTrip":true,"literalMatch":true,"escapedReplacement":true},"click":{"target":true,"delegate":true,"prevented":true,"prototype":true},"prototypeCalls":1,"same":true,"equal":true,"clonedValueReflectsAttribute":true,"dirtyValueSurvivesAttribute":true})";
    if (result != expected) {
        fail("component-library DOM discovery primitives regressed: " + result);
    }
}

void test_document_id_index_preserves_tree_and_root_semantics(
    webscene_engine* engine)
{
    const auto result = evaluate(
        engine,
        R"JS((() => {
          const host = document.createElement('div');
          const first = document.createElement('span');
          const second = document.createElement('span');
          first.id = 'duplicate-id';
          second.id = 'duplicate-id';
          host.appendChild(first);
          host.appendChild(second);
          document.body.appendChild(host);

          const initialWinner = document.getElementById('duplicate-id') === first;
          host.insertBefore(second, first);
          const reorderedWinner = document.getElementById('duplicate-id') === second;
          second.id = 'renamed-id';
          const mutationUpdated = document.getElementById('duplicate-id') === first
            && document.getElementById('renamed-id') === second;
          host.removeChild(first);
          const detachedExcluded = document.getElementById('duplicate-id') === null;
          first.id = 'detached-id';
          const detachedMutationExcluded = document.getElementById('detached-id') === null;

          const parsed = new DOMParser().parseFromString(
            '<main id="renamed-id"></main>',
            'text/html');
          const parsedScoped = parsed.getElementById('renamed-id') !== second
            && parsed.getElementById('renamed-id').tagName.toLowerCase() === 'main';
          const outerScoped = document.getElementById('renamed-id') === second;

          const namedFirst = document.createElement('span');
          const namedSecond = document.createElement('span');
          namedFirst.setAttribute('name', 'duplicate-name');
          namedSecond.setAttribute('name', 'duplicate-name');
          host.appendChild(namedFirst);
          host.appendChild(namedSecond);
          const namedInitial = globalThis['duplicate-name'] === namedFirst;
          host.insertBefore(namedSecond, namedFirst);
          const namedReordered = globalThis['duplicate-name'] === namedSecond;
          namedSecond.setAttribute('name', 'renamed-name');
          const namedMutation = globalThis['duplicate-name'] === namedFirst
            && globalThis['renamed-name'] === namedSecond;
          namedFirst.remove();
          const namedDetached = globalThis['duplicate-name'] === undefined;

          const nonForm = document.createElement('input');
          nonForm.setAttribute('name', 'document-named');
          const namedForm = document.createElement('form');
          namedForm.setAttribute('name', 'document-named');
          host.appendChild(nonForm);
          host.appendChild(namedForm);
          const documentFormsOnly = document['document-named'] === namedForm;
          host.remove();
          return {
            initialWinner,
            reorderedWinner,
            mutationUpdated,
            detachedExcluded,
            detachedMutationExcluded,
            parsedScoped,
            outerScoped,
            namedInitial,
            namedReordered,
            namedMutation,
            namedDetached,
            documentFormsOnly
          };
        })())JS",
        "document-id-index-semantics.js");
    require(
        result
            == R"({"initialWinner":true,"reorderedWinner":true,"mutationUpdated":true,"detachedExcluded":true,"detachedMutationExcluded":true,"parsedScoped":true,"outerScoped":true,"namedInitial":true,"namedReordered":true,"namedMutation":true,"namedDetached":true,"documentFormsOnly":true})",
        "document ID index lost mutation, tree-order, or root scoping semantics: "
            + result);
}

void test_dom_selector_apis_throw_syntax_error_for_invalid_selectors(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '<div id="selector-root"><span class="item"></span></div>';
          const root = document.querySelector('#selector-root');
          const item = root.querySelector('.item');
          const capture = operation => {
            try {
              operation();
              return null;
            } catch (error) {
              return [error instanceof DOMException, error.name];
            }
          };
          return {
            errors: [
              capture(() => document.querySelector(':webscene-unknown-pseudo')),
              capture(() => document.querySelectorAll(':webscene-unknown-pseudo')),
              capture(() => root.querySelector(':webscene-unknown-pseudo')),
              capture(() => root.querySelectorAll(':webscene-unknown-pseudo')),
              capture(() => item.matches(':webscene-unknown-pseudo')),
              capture(() => item.closest(':webscene-unknown-pseudo')),
              capture(() => document.querySelector('[')),
              capture(() => item.matches(''))
            ],
            valid: document.querySelector('#selector-root') === root
              && root.querySelectorAll('.item').length === 1
              && item.matches('span.item')
              && !item.matches('button.item')
              && item.closest('#selector-root') === root
          };
        })()
    )JS", "native-dom-selector-syntax-errors.js");
    const auto expected = R"JSON({"errors":[[true,"SyntaxError"],[true,"SyntaxError"],[true,"SyntaxError"],[true,"SyntaxError"],[true,"SyntaxError"],[true,"SyntaxError"],[true,"SyntaxError"],[true,"SyntaxError"]],"valid":true})JSON";
    if (result != expected) {
        fail("DOM selector error semantics diverged from browser behavior: " + result);
    }
}

void test_dropdown_runtime_primitives(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const toggle = document.createElement('button');
          toggle.id = 'dropdown-runtime-toggle';
          toggle.className = 'open';
          toggle.setAttribute('data-toggle', 'component');
          const whitespace = document.createTextNode('\n  ');
          const menu = document.createElement('div');
          menu.id = 'dropdown-runtime-menu';
          menu.className = 'dropdown-runtime-menu';
          document.body.append(toggle, whitespace, menu);

          const style = document.createElement('style');
          style.textContent = '.dropdown-runtime-menu { display: none; }';
          document.body.appendChild(style);
          const hidden = getComputedStyle(menu).display;
          style.remove();
          const restored = getComputedStyle(menu).display;
          style.textContent = '.dropdown-runtime-menu { display: inline-block; }';
          document.body.appendChild(style);
          const reinserted = getComputedStyle(menu).display;

          const selector = '[data-toggle="component"]:not(.disabled):not(:disabled).open';
          let observed = null;
          document.addEventListener('click', event => {
            if (event.target === toggle) observed = event;
          }, { once: true });
          const click = new MouseEvent('click', { bubbles: true });
          toggle.dispatchEvent(click);
          return {
            selector: document.querySelector(selector) === toggle && toggle.matches(selector),
            siblings: toggle.nextElementSibling === menu && menu.previousElementSibling === toggle,
            visibility: [hidden, restored, reinserted],
            event: {
              identity: observed === click,
              target: click.target === toggle,
              currentTarget: click.currentTarget,
              eventPhase: click.eventPhase,
              button: click.button,
              buttons: click.buttons,
              mouse: click instanceof MouseEvent && click instanceof Event
            }
          };
        })()
    )JS", "native-dropdown-runtime-primitives.js");
    const auto expected = R"JSON({"selector":true,"siblings":true,"visibility":["none","block","inline-block"],"event":{"identity":true,"target":true,"currentTarget":null,"eventPhase":0,"button":0,"buttons":0,"mouse":true}})JSON";
    if (result != expected) {
        fail("dropdown runtime primitives regressed: " + result);
    }
}

void test_input_dispatch_failures_are_attributed_and_consumable(webscene_engine* engine)
{
    struct original_input_dispatch_metrics final {
        uint32_t struct_size;
        uint32_t reserved;
        uint64_t last_dispatch_nanoseconds;
        uint64_t maximum_dispatch_nanoseconds;
        uint64_t last_dispatch_sequence;
    };
    static_assert(
        sizeof(original_input_dispatch_metrics)
            == offsetof(webscene_input_dispatch_metrics, dispatched_inputs));
    original_input_dispatch_metrics original{
        sizeof(original_input_dispatch_metrics)};
    require(
        webscene_engine_get_input_dispatch_metrics(
            engine,
            reinterpret_cast<webscene_input_dispatch_metrics*>(&original)) != 0,
        "input-dispatch metrics rejected the original ABI prefix");

    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const target = document.createElement('button');
          target.style.width = '100px';
          target.style.height = '100px';
          target.addEventListener('click', () => {
            throw new Error('certification-input-failure-marker');
          });
          document.body.appendChild(target);
        })()
    )JS", "native-input-failure-setup.js");
    require(
        evaluate(engine, "document.body.firstElementChild?.offsetWidth", "native-input-failure-ready.js")
            == "100",
        "input-failure fixture did not complete layout");

    const webscene_input_event down{
        WEBSCENE_INPUT_POINTER_DOWN, 1U | (1U << 8U), 101U, 10, 10, 0, 0};
    const webscene_input_event up{
        WEBSCENE_INPUT_POINTER_UP, (1U << 8U), 102U, 10, 10, 0, 0};
    const auto consumed_before_click = consumed_input_count(engine);
    require(webscene_engine_enqueue(engine, &down) != 0, "throwing click down was rejected");
    require(webscene_engine_enqueue(engine, &up) != 0, "throwing click up was rejected");
    wait_for_consumed_inputs(
        engine,
        consumed_before_click + 2,
        "throwing click inputs were not consumed");

    webscene_input_dispatch_metrics dispatch_metrics{
        sizeof(webscene_input_dispatch_metrics)};
    require(
        webscene_engine_get_input_dispatch_metrics(engine, &dispatch_metrics) != 0,
        "input-dispatch metrics ABI was rejected");
    require(
        dispatch_metrics.last_dispatch_sequence == up.sequence,
        "input-dispatch timing was not attributed to the last consumed sequence");
    require(
        dispatch_metrics.last_dispatch_nanoseconds > 0
            && dispatch_metrics.maximum_dispatch_nanoseconds
                >= dispatch_metrics.last_dispatch_nanoseconds
            && dispatch_metrics.dispatched_inputs >= 2U
            && dispatch_metrics.total_dispatch_nanoseconds
                >= dispatch_metrics.last_dispatch_nanoseconds,
        "input-dispatch timing metrics were not populated monotonically");

    const auto failure = take_input_dispatch_failure(engine);
    require(
        failure.starts_with("102\n3\nNative ")
            && failure.find(" dispatch failed:") != std::string::npos
            && failure.find("certification-input-failure-marker") != std::string::npos,
        "input failure omitted its sequence, kind, or JavaScript stack: " + failure);
    require(
        take_input_dispatch_failure(engine).empty(),
        "input-dispatch failure was not consumed exactly once");
}

void test_animation_frame_dispatch_is_attributed()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "animation-frame metrics engine creation failed");
    webscene_animation_frame_metrics undersized{};
    require(
        webscene_engine_get_animation_frame_metrics(engine, &undersized) == 0,
        "animation-frame metrics accepted an undersized ABI structure");
    webscene_animation_frame_metrics before{
        sizeof(webscene_animation_frame_metrics)};
    require(
        webscene_engine_get_animation_frame_metrics(engine, &before) != 0,
        "animation-frame metrics ABI was rejected");

    const auto consumed_before = consumed_input_count(engine);
    animation_frame(engine, 12'345.5, 103U);
    wait_for_consumed_inputs(
        engine,
        consumed_before + 1U,
        "attributed animation frame was not consumed");

    webscene_animation_frame_metrics after{
        sizeof(webscene_animation_frame_metrics)};
    require(
        webscene_engine_get_animation_frame_metrics(engine, &after) != 0,
        "animation-frame metrics were unavailable after dispatch");
    require(
        after.dispatched_frames == before.dispatched_frames + 1U
            && after.total_dispatch_nanoseconds
                >= before.total_dispatch_nanoseconds
                    + after.last_dispatch_nanoseconds
            && after.last_dispatch_nanoseconds > 0U
            && after.maximum_dispatch_nanoseconds
                >= after.last_dispatch_nanoseconds
            && after.last_timestamp_microseconds == 12'345'500U,
        "animation-frame timing or timestamp attribution was inconsistent");
    webscene_engine_destroy(engine);
}

void test_runtime_work_is_attributed()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "runtime-work metrics engine creation failed");
    webscene_runtime_work_metrics undersized{};
    require(
        webscene_engine_get_runtime_work_metrics(engine, &undersized) == 0,
        "runtime-work metrics accepted an undersized ABI structure");
    webscene_runtime_work_metrics before{
        sizeof(webscene_runtime_work_metrics)};
    require(
        webscene_engine_set_runtime_work_metrics_enabled(engine, 1U) != 0
            && webscene_engine_get_runtime_work_metrics(engine, &before) != 0,
        "runtime-work metrics ABI was rejected");

    execute_and_wait(
        engine,
        R"JS(
          globalThis.__runtimeWorkTimer = 0;
          globalThis.__runtimeWorkFrame = 0;
          const cancelledTimer = setTimeout(() => {}, 1000);
          clearTimeout(cancelledTimer);
          setTimeout(() => { __runtimeWorkTimer++; }, 0);
          const cancelledFrame = requestAnimationFrame(() => {});
          cancelAnimationFrame(cancelledFrame);
          requestAnimationFrame(() => { __runtimeWorkFrame++; });
        )JS",
        "runtime-work-metrics.js");
    animation_frame(engine, 1'234.5, 104U);

    webscene_runtime_work_metrics after{
        sizeof(webscene_runtime_work_metrics)};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        require(
            webscene_engine_get_runtime_work_metrics(engine, &after) != 0,
            "runtime-work metrics became unavailable");
        if (after.timers_fired >= before.timers_fired + 1U
            && after.animation_frames_invoked
                >= before.animation_frames_invoked + 1U) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto work_is_consistent =
        after.timers_scheduled >= before.timers_scheduled + 2U
            && after.timers_fired >= before.timers_fired + 1U
            && after.timers_cancelled >= before.timers_cancelled + 1U
            && after.animation_frames_requested
                >= before.animation_frames_requested + 2U
            && after.animation_frames_invoked
                >= before.animation_frames_invoked + 1U
            && after.animation_frames_cancelled
                >= before.animation_frames_cancelled + 1U
            && after.microtask_checkpoints > before.microtask_checkpoints
            && after.worker_waits >= before.worker_waits
            && after.worker_signalled_wakes + after.worker_timeout_wakes
                <= after.worker_waits;
    if (!work_is_consistent) {
        fail(
            "timer, RAF, microtask, or worker-wake attribution was inconsistent: "
            "timers=" + std::to_string(after.timers_scheduled) + "/"
                + std::to_string(after.timers_fired) + "/"
                + std::to_string(after.timers_cancelled)
            + " raf=" + std::to_string(after.animation_frames_requested) + "/"
                + std::to_string(after.animation_frames_invoked) + "/"
                + std::to_string(after.animation_frames_cancelled)
            + " microtasks=" + std::to_string(after.microtask_checkpoints)
            + " waits=" + std::to_string(after.worker_waits) + "/"
                + std::to_string(after.worker_signalled_wakes) + "/"
                + std::to_string(after.worker_timeout_wakes));
    }

    const auto evaluation_before = after.arbitrary_evaluation_calls;
    require(
        evaluate(engine, "true", "runtime-work-evaluation.js") == "true",
        "runtime-work evaluation fixture failed");
    require(
        webscene_engine_get_runtime_work_metrics(engine, &after) != 0
            && after.arbitrary_evaluation_calls
                == evaluation_before + 1U
            && after.arbitrary_evaluation_source_bytes
                >= before.arbitrary_evaluation_source_bytes + 4U,
        "arbitrary evaluation was not attributed separately");
    webscene_engine_destroy(engine);
}

void test_scene_flow_is_attributed()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "scene-flow metrics engine creation failed");
    webscene_scene_flow_metrics undersized{};
    require(
        webscene_engine_get_scene_flow_metrics(engine, &undersized) == 0,
        "scene-flow metrics accepted an undersized ABI structure");
    webscene_scene_flow_metrics before{sizeof(webscene_scene_flow_metrics)};
    require(
        webscene_engine_get_scene_flow_metrics(engine, &before) != 0,
        "scene-flow metrics ABI was rejected");

    const webscene_scene_view* scene = nullptr;
    for (auto attempt = 0; attempt < 100 && scene == nullptr; ++attempt) {
        scene = webscene_engine_acquire_latest_scene(engine);
        if (scene == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(scene != nullptr, "scene-flow fixture did not publish an initial scene");
    const auto revision = scene->header.revision;
    require(
        webscene_scene_acknowledge(scene) != 0,
        "scene-flow fixture could not acknowledge its initial scene");
    webscene_scene_release(scene);

    webscene_scene_flow_metrics after{sizeof(webscene_scene_flow_metrics)};
    require(
        webscene_engine_get_scene_flow_metrics(engine, &after) != 0,
        "scene-flow metrics were unavailable after acknowledgement");
    require(
        after.publication_attempts >= before.publication_attempts
            && after.acknowledged_scenes == before.acknowledged_scenes + 1U
            && after.total_acknowledgement_nanoseconds
                >= before.total_acknowledgement_nanoseconds
                    + after.last_acknowledgement_nanoseconds
            && after.maximum_acknowledgement_nanoseconds
                >= after.last_acknowledgement_nanoseconds
            && after.acknowledged_revision == revision,
        "scene publication/backpressure attribution was inconsistent");
    webscene_engine_destroy(engine);
}

void test_read_only_evaluation_does_not_publish_scene()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "read-only evaluation engine creation failed");

    const webscene_scene_view* initial = nullptr;
    for (auto attempt = 0; attempt < 100 && initial == nullptr; ++attempt) {
        initial = webscene_engine_acquire_latest_scene(engine);
        if (initial == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(initial != nullptr, "read-only evaluation fixture had no initial scene");
    require(
        webscene_scene_acknowledge(initial) != 0,
        "read-only evaluation fixture could not acknowledge its initial scene");
    webscene_scene_release(initial);

    webscene_engine_metrics before_setup{};
    webscene_engine_get_metrics(engine, &before_setup);
    execute_and_wait(
        engine,
        "document.body.textContent = 'ready'",
        "read-only-evaluation-setup.js");

    const webscene_scene_view* setup_scene = nullptr;
    for (auto attempt = 0; attempt < 100 && setup_scene == nullptr; ++attempt) {
        webscene_engine_metrics current{};
        webscene_engine_get_metrics(engine, &current);
        if (current.published_scenes > before_setup.published_scenes) {
            setup_scene = webscene_engine_acquire_latest_scene(engine);
        }
        if (setup_scene == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(setup_scene != nullptr, "read-only evaluation setup scene was not published");
    require(
        webscene_scene_acknowledge(setup_scene) != 0,
        "read-only evaluation setup scene could not be acknowledged");
    webscene_scene_release(setup_scene);

    webscene_engine_metrics baseline{};
    webscene_engine_get_metrics(engine, &baseline);
    require(
        evaluate(engine, "1 + 1", "read-only-evaluation.js") == "2",
        "read-only evaluation returned the wrong value");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    webscene_engine_metrics after_read{};
    webscene_engine_get_metrics(engine, &after_read);
    if (after_read.published_scenes != baseline.published_scenes) {
        fail(
            "read-only evaluation published an unchanged scene: before="
            + std::to_string(baseline.published_scenes)
            + ", after=" + std::to_string(after_read.published_scenes));
    }

    require(
        evaluate(
            engine,
            "setTimeout(() => {}, 0); true",
            "non-visual-task-evaluation.js") == "true",
        "non-visual task evaluation returned the wrong value");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    webscene_engine_get_metrics(engine, &after_read);
    require(
        after_read.published_scenes == baseline.published_scenes,
        "a completed non-visual task published an unchanged scene");

    require(
        evaluate(
            engine,
            "document.body.textContent = 'changed'; true",
            "mutating-evaluation.js") == "true",
        "mutating evaluation returned the wrong value");
    for (auto attempt = 0; attempt < 100; ++attempt) {
        webscene_engine_get_metrics(engine, &after_read);
        if (after_read.published_scenes > baseline.published_scenes) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        after_read.published_scenes == baseline.published_scenes + 1U,
        "mutating evaluation did not publish its dirty scene");
    webscene_engine_destroy(engine);
}

void test_ordered_scene_consumer_preserves_two_diff_chain()
{
    auto* engine = webscene_engine_create(64);
    require(engine != nullptr, "ordered-scene engine creation failed");
    const auto wait_for_publications = [engine](
        uint64_t minimum,
        const char* failure) {
        for (auto attempt = 0; attempt < 200; ++attempt) {
            webscene_engine_metrics metrics{};
            webscene_engine_get_metrics(engine, &metrics);
            if (metrics.published_scenes >= minimum) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        fail(failure);
    };

    const webscene_scene_view* checkpoint = nullptr;
    for (auto attempt = 0; attempt < 200 && checkpoint == nullptr; ++attempt) {
        checkpoint = webscene_engine_acquire_next_scene(engine);
        if (checkpoint == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(checkpoint != nullptr, "ordered consumer did not acquire checkpoint");
    require(
        webscene_scene_acknowledge(checkpoint) != 0,
        "ordered consumer could not acknowledge checkpoint");
    webscene_scene_release(checkpoint);

    webscene_engine_metrics baseline{};
    webscene_engine_get_metrics(engine, &baseline);
    resize(engine, 401, 301, 1U);
    wait_for_publications(
        baseline.published_scenes + 1U,
        "ordered producer did not publish its first diff");
    const auto* first = webscene_engine_acquire_next_scene(engine);
    require(first != nullptr, "ordered consumer did not acquire first diff");
    const auto first_revision = first->header.revision;

    resize(engine, 402, 302, 2U);
    wait_for_publications(
        baseline.published_scenes + 2U,
        "ordered producer did not publish one frame ahead");
    const auto* latest = webscene_engine_acquire_latest_scene(engine);
    require(latest != nullptr, "ordered producer had no second diff");
    require(
        latest->header.base_revision == first_revision,
        "ordered second diff did not retain the first revision as its base");
    const auto second_revision = latest->header.revision;
    webscene_scene_release(latest);
    webscene_engine_memory_metrics queued_memory{
        sizeof(webscene_engine_memory_metrics)};
    require(
        webscene_engine_get_memory_metrics(engine, &queued_memory) != 0
            && queued_memory.pending_scene_count == 2U
            && queued_memory.pending_scene_bytes
                >= queued_memory.latest_scene_bytes
            && queued_memory.latest_scene_bytes > 0,
        "ordered scene memory was not bounded and attributed");

    webscene_scene_flow_metrics before_block{sizeof(webscene_scene_flow_metrics)};
    require(
        webscene_engine_get_scene_flow_metrics(engine, &before_block) != 0,
        "ordered scene-flow metrics were unavailable");
    resize(engine, 403, 303, 3U);
    webscene_scene_flow_metrics after_block{sizeof(webscene_scene_flow_metrics)};
    for (auto attempt = 0; attempt < 200; ++attempt) {
        require(
            webscene_engine_get_scene_flow_metrics(engine, &after_block) != 0,
            "ordered scene-flow metrics became unavailable");
        if (after_block.blocked_publications
            > before_block.blocked_publications) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        after_block.blocked_publications
            > before_block.blocked_publications,
        "ordered producer exceeded its two-scene bound");

    require(
        webscene_scene_acknowledge(first) != 0,
        "ordered consumer could not acknowledge first diff");
    webscene_scene_release(first);
    const auto* second = webscene_engine_acquire_next_scene(engine);
    require(
        second != nullptr && second->header.revision == second_revision,
        "ordered consumer skipped its second diff");
    require(
        webscene_scene_acknowledge(second) != 0,
        "ordered consumer could not acknowledge second diff");
    webscene_scene_release(second);
    wait_for_publications(
        baseline.published_scenes + 3U,
        "ordered producer did not resume after acknowledgement");
    const auto* third = webscene_engine_acquire_next_scene(engine);
    require(
        third != nullptr && third->header.base_revision == second_revision,
        "ordered consumer lost the resumed diff chain");
    require(
        webscene_scene_acknowledge(third) != 0,
        "ordered consumer could not acknowledge resumed diff");
    webscene_scene_release(third);
    webscene_engine_memory_metrics drained_memory{
        sizeof(webscene_engine_memory_metrics)};
    require(
        webscene_engine_get_memory_metrics(engine, &drained_memory) != 0
            && drained_memory.pending_scene_count == 0
            && drained_memory.pending_scene_bytes == 0,
        "acknowledged ordered scenes remained attributed");
    webscene_engine_destroy(engine);
}

void test_keyboard_and_pointer_focus_modality()
{
    auto* focus_engine = webscene_engine_create(16);
    require(focus_engine != nullptr, "focus-modality engine creation failed");
    resize(focus_engine, 320, 200, 200U);
    execute(focus_engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              @supports not selector(:focus-visible) {
                :focus { background-color: red; }
              }
              .always { outline: 2px solid blue; }
              :focus-visible { outline: 3px solid rgb(0, 128, 0); }
              :focus:not(:focus-visible) { background-color: rgb(0, 255, 0); }
            </style>
            <div id="keyboard" class="always" tabindex="0" style="width:120px;height:40px">keyboard</div>
            <button id="pointer" style="display:block;width:120px;height:40px">pointer</button>`;
          document.body.style.margin = '0';
          globalThis.__focusEvents = [];
          for (const target of [keyboard, pointer]) {
            target.addEventListener('focus', () => __focusEvents.push(`focus:${target.id}`));
            target.addEventListener('blur', () => __focusEvents.push(`blur:${target.id}`));
          }
        })()
    )JS", "native-focus-modality-setup.js");
    require(
        evaluate(focus_engine, "pointer.offsetTop", "native-focus-modality-ready.js") == "40",
        "focus-modality fixture did not complete layout");
    require(
        evaluate(focus_engine, "getComputedStyle(keyboard).outlineColor", "native-outline-ready.js")
            == "\"rgb(0, 0, 255)\"",
        "base outline shorthand did not reach computed style");
    const auto tab_index_reflection = evaluate(focus_engine, R"JS((() => {
      const menu = document.createElement('div');
      menu.setAttribute('tabindex', '-1');
      const authored = menu.tabIndex;
      menu.tabIndex = 2;
      return {
        keyboard: keyboard.tabIndex,
        pointer: pointer.tabIndex,
        body: document.body.tabIndex,
        authored,
        assigned: menu.tabIndex,
        reflected: menu.getAttribute('tabindex')
      };
    })())JS", "native-tab-index-reflection.js");
    if (tab_index_reflection != R"({"keyboard":0,"pointer":0,"body":-1,"authored":-1,"assigned":2,"reflected":"2"})") {
        fail("tabIndex IDL reflection regressed: " + tab_index_reflection);
    }

    keyboard_input(focus_engine, WEBSCENE_INPUT_KEY_DOWN, 9U, 201U);
    keyboard_input(focus_engine, WEBSCENE_INPUT_KEY_UP, 9U, 202U);
    const auto keyboard_result = evaluate(focus_engine, R"JS((() => ({
      active: document.activeElement?.id,
      focusVisible: document.querySelector(':focus-visible')?.id,
      notFocusVisible: document.querySelector(':focus:not(:focus-visible)')?.id ?? null,
      className: keyboard.className,
      outline: getComputedStyle(keyboard).outlineColor,
      background: getComputedStyle(keyboard).backgroundColor,
      events: __focusEvents
    }))())JS", "native-keyboard-focus-result.js");
    if (keyboard_result != R"JSON({"active":"keyboard","focusVisible":"keyboard","notFocusVisible":null,"className":"always","outline":"rgb(0, 128, 0)","background":"rgba(0, 0, 0, 0)","events":["focus:keyboard"]})JSON") {
        fail("Tab focus or :focus-visible keyboard modality regressed: " + keyboard_result);
    }

    const webscene_input_event down{
        WEBSCENE_INPUT_POINTER_DOWN, 1U | (1U << 8U), 203U, 10, 60, 0, 0};
    const webscene_input_event up{
        WEBSCENE_INPUT_POINTER_UP, (1U << 8U), 204U, 10, 60, 0, 0};
    require(webscene_engine_enqueue(focus_engine, &down) != 0, "pointer focus down was rejected");
    require(webscene_engine_enqueue(focus_engine, &up) != 0, "pointer focus up was rejected");
    const auto pointer_result = evaluate(focus_engine, R"JS((() => ({
      active: document.activeElement?.id,
      focusVisible: document.querySelector(':focus-visible')?.id ?? null,
      background: getComputedStyle(pointer).backgroundColor,
      events: __focusEvents
    }))())JS", "native-pointer-focus-result.js");
    if (pointer_result != R"JSON({"active":"pointer","focusVisible":null,"background":"rgb(0, 255, 0)","events":["focus:keyboard","blur:keyboard","focus:pointer"]})JSON") {
        fail("pointer focus or :focus-visible pointer modality regressed: " + pointer_result);
    }
    const auto provisional_frame = evaluate(focus_engine, R"JS((() => {
      const frame = document.createElement('iframe');
      document.body.appendChild(frame);
      const input = document.createElement('input');
      input.id = 'provisional-frame-input';
      frame.contentDocument.body.appendChild(input);
      return {
        hasBody: !!frame.contentDocument.body,
        child: frame.contentDocument.body.firstElementChild?.id
      };
    })())JS", "native-provisional-frame-document.js");
    if (provisional_frame != R"({"hasBody":true,"child":"provisional-frame-input"})") {
        fail("empty iframe contentDocument did not expose a mutable body: " + provisional_frame);
    }
    const auto reentrant_focus = evaluate(focus_engine, R"JS((() => {
      const target = document.createElement('input');
      target.id = 'reentrant-focus-target';
      document.body.appendChild(target);
      let focusOutCount = 0;
      pointer.addEventListener('focusout', () => {
        focusOutCount++;
        target.focus();
      });
      pointer.blur();
      return {
        active: document.activeElement?.id,
        focusOutCount
      };
    })())JS", "native-reentrant-focus.js");
    if (reentrant_focus != R"({"active":"reentrant-focus-target","focusOutCount":1})") {
        fail("focusout re-entrant focus transition regressed: " + reentrant_focus);
    }
    const auto detached_focus = evaluate(focus_engine, R"JS((() => {
      const removedHost = document.createElement('div');
      const removedInput = document.createElement('input');
      const removeEvents = [];
      removedInput.addEventListener('blur', () => removeEvents.push('blur'));
      removedInput.addEventListener('focusout', () => removeEvents.push('focusout'));
      removedHost.addEventListener('blur', () => removeEvents.push('host-blur'));
      removedHost.addEventListener('focusout', () => removeEvents.push('host-focusout'));
      removedHost.appendChild(removedInput);
      document.body.appendChild(removedHost);
      removedInput.focus();
      removedHost.remove();

      const replacedHost = document.createElement('div');
      const replacedInput = document.createElement('input');
      const innerHtmlEvents = [];
      replacedInput.addEventListener('blur', () => innerHtmlEvents.push('blur'));
      replacedInput.addEventListener('focusout', () => innerHtmlEvents.push('focusout'));
      replacedHost.addEventListener('blur', () => innerHtmlEvents.push('host-blur'));
      replacedHost.addEventListener('focusout', () => innerHtmlEvents.push('host-focusout'));
      replacedHost.appendChild(replacedInput);
      document.body.appendChild(replacedHost);
      replacedInput.focus();
      replacedHost.innerHTML = '';
      return {
        activeTag: document.activeElement?.tagName,
        focusMatch: document.querySelector(':focus')?.tagName ?? null,
        removeEvents,
        innerHtmlEvents,
        removedConnected: removedInput.isConnected,
        replacedConnected: replacedInput.isConnected
      };
    })())JS", "native-detached-focus.js");
    if (detached_focus != R"({"activeTag":"BODY","focusMatch":null,"removeEvents":["blur","focusout","host-focusout"],"innerHtmlEvents":["blur","focusout","host-focusout"],"removedConnected":false,"replacedConnected":false})") {
        fail("detached focused nodes remained document.activeElement: " + detached_focus);
    }
    webscene_engine_destroy(focus_engine);
}

void test_resize_precedes_new_viewport_pointer_input(webscene_engine* engine)
{
    resize(engine, 320, 200, 7U);
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const target = document.createElement('div');
          target.style.width = '100vw';
          target.style.height = '100vh';
          target.addEventListener('pointerdown', () => {
            globalThis.__widthObservedByPointer = innerWidth;
          });
          document.body.appendChild(target);
          globalThis.__widthObservedByPointer = 0;
        })()
    )JS", "native-resize-pointer-setup.js");
    require(
        evaluate(engine, "globalThis.__widthObservedByPointer",
            "native-resize-pointer-ready.js") == "0",
        "resize-pointer fixture did not initialize");

    // Keep the worker occupied while both independently queued event streams
    // receive work. This makes the ordering assertion deterministic: the latest
    // coalesced resize and the following pointer are both pending together.
    execute(engine, R"JS(
        (() => {
          const until = Date.now() + 20;
          while (Date.now() < until) {}
        })()
    )JS", "native-resize-pointer-worker-barrier.js");
    resize(engine, 777, 333, 8U);
    const webscene_input_event down{
        WEBSCENE_INPUT_POINTER_DOWN, 1U | (1U << 8U), 9U, 10, 10, 0, 0};
    require(webscene_engine_enqueue(engine, &down) != 0,
        "post-resize pointer down was rejected");

    const auto result = evaluate(
        engine,
        "globalThis.__widthObservedByPointer",
        "native-resize-pointer-result.js");
    if (result != "777") {
        fail("pointer input was dispatched against the stale pre-resize viewport: " + result);
    }
}

void test_resize_updates_device_pixel_ratio(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const frame = document.createElement('iframe');
          document.body.appendChild(frame);
          const frameDocument = frame.contentDocument;
          frameDocument.open();
          frameDocument.write('<html><body></body></html>');
          frameDocument.close();
          globalThis.__scaleFrame = frame;
        })()
    )JS", "native-scale-factor-setup.js");

    resize(engine, 640, 360, 210U, 2.0);
    const auto scaled = evaluate(engine, R"JS((() => ({
      outer: devicePixelRatio,
      frame: globalThis.__scaleFrame.contentWindow.devicePixelRatio,
      width: innerWidth,
      height: innerHeight
    }))())JS", "native-scale-factor-two.js");
    if (scaled != R"({"outer":2,"frame":2,"width":640,"height":360})") {
        fail("devicePixelRatio did not follow the host scale transition: " + scaled);
    }

    resize(engine, 640, 360, 211U, 1.0);
    const auto restored = evaluate(engine, R"JS((() => ({
      outer: devicePixelRatio,
      frame: globalThis.__scaleFrame.contentWindow.devicePixelRatio
    }))())JS", "native-scale-factor-one.js");
    if (restored != R"({"outer":1,"frame":1})") {
        fail("devicePixelRatio did not restore after the host scale transition: " + restored);
    }
}
void test_document_direction_and_visibility_are_native_properties()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "document state regression engine creation failed");
    const auto initial = evaluate(engine, R"JS(
        ({
          dir: document.dir,
          hidden: document.hidden,
          visibilityState: document.visibilityState
        })
    )JS", "native-document-state-initial.js");
    require(
        initial == R"({"dir":"","hidden":false,"visibilityState":"visible"})",
        "Document direction or initial visibility was not browser-shaped: " + initial);

    const auto reflected = evaluate(engine, R"JS(
        (() => {
          document.dir = 'rtl';
          return {
            dir: document.dir,
            attribute: document.documentElement.getAttribute('dir')
          };
        })()
    )JS", "native-document-direction.js");
    require(
        reflected == R"({"dir":"rtl","attribute":"rtl"})",
        "Document.dir did not reflect the document-element attribute: " + reflected);

    execute_and_wait(
        engine,
        R"JS(
          globalThis.__visibilityEvents = [];
          document.addEventListener('visibilitychange', () => {
            __visibilityEvents.push(document.visibilityState);
            Promise.resolve().then(() =>
              __visibilityEvents.push(`microtask:${document.visibilityState}`));
          });
        )JS",
        "native-document-visibility-events.js");

    require(
        webscene_engine_set_visible(engine, 0U) != 0,
        "document state engine rejected hidden visibility");
    const auto hidden = evaluate(engine, R"JS(
        ({
          hidden: document.hidden,
          visibilityState: document.visibilityState,
          events: __visibilityEvents
        })
    )JS", "native-document-state-hidden.js");
    require(
        hidden == R"({"hidden":true,"visibilityState":"hidden","events":["hidden","microtask:hidden"]})",
        "Document visibility did not follow the hidden host state: " + hidden);

    require(
        webscene_engine_set_visible(engine, 0U) != 0,
        "document state engine rejected duplicate hidden visibility");

    require(
        webscene_engine_set_visible(engine, 1U) != 0,
        "document state engine rejected visible visibility");
    const auto visible = evaluate(engine, R"JS(
        ({
          hidden: document.hidden,
          visibilityState: document.visibilityState,
          events: __visibilityEvents
        })
    )JS", "native-document-state-visible.js");
    require(
        visible == R"({"hidden":false,"visibilityState":"visible","events":["hidden","microtask:hidden","visible","microtask:visible"]})",
        "Document visibility did not follow the restored host state: " + visible);
    webscene_engine_destroy(engine);
}

void test_hidden_document_defers_presentation_work()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "hidden-presentation engine creation failed");
    const webscene_scene_view* initial = nullptr;
    for (auto attempt = 0; attempt < 100 && initial == nullptr; ++attempt) {
        initial = webscene_engine_acquire_latest_scene(engine);
        if (initial == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(initial != nullptr, "hidden-presentation fixture had no initial scene");
    require(
        webscene_scene_acknowledge(initial) != 0,
        "hidden-presentation fixture could not acknowledge its initial scene");
    webscene_scene_release(initial);
    require(
        webscene_engine_set_runtime_work_metrics_enabled(engine, 1U) != 0,
        "hidden-presentation fixture could not enable work metrics");
    require(
        webscene_engine_set_visible(engine, 0U) != 0,
        "hidden-presentation fixture rejected hidden state");
    require(
        evaluate(engine, "document.hidden", "hidden-presentation-ready.js")
            == "true",
        "hidden-presentation state did not reach the document");

    webscene_engine_metrics before{};
    webscene_engine_get_metrics(engine, &before);
    webscene_runtime_work_metrics work_before{
        sizeof(webscene_runtime_work_metrics)};
    require(
        webscene_engine_get_runtime_work_metrics(engine, &work_before) != 0,
        "hidden-presentation work metrics were unavailable");
    execute_and_wait(
        engine,
        "setTimeout(() => { document.body.textContent = 'mutated while hidden'; }, 0)",
        "hidden-presentation-mutation.js");
    webscene_runtime_work_metrics work_hidden{
        sizeof(webscene_runtime_work_metrics)};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        require(
            webscene_engine_get_runtime_work_metrics(engine, &work_hidden) != 0,
            "hidden-presentation work metrics became unavailable");
        if (work_hidden.timers_fired > work_before.timers_fired) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(75));

    webscene_engine_metrics hidden{};
    webscene_engine_get_metrics(engine, &hidden);
    require(
        webscene_engine_get_runtime_work_metrics(engine, &work_hidden) != 0
            && work_hidden.timers_fired == work_before.timers_fired + 1U
            && hidden.published_scenes == before.published_scenes
            && work_hidden.scene_builds == work_before.scene_builds,
        "hidden DOM work produced presentation scenes");

    require(
        webscene_engine_set_visible(engine, 1U) != 0,
        "hidden-presentation fixture rejected restored visibility");
    webscene_engine_metrics visible{};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        webscene_engine_get_metrics(engine, &visible);
        if (visible.published_scenes > hidden.published_scenes) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        visible.published_scenes == hidden.published_scenes + 1U
            && evaluate(
                engine,
                "document.body.textContent",
                "hidden-presentation-restored.js")
                == "\"mutated while hidden\"",
        "restored visibility did not publish the deferred document state");
    webscene_engine_destroy(engine);
}
void test_component_catalog_mounts_interacts_and_unmounts()
{
    struct component_case final {
        std::string_view id;
        std::string_view visible_text;
        std::string_view button_text;
        std::string_view expected_text;
    };
    static constexpr std::array cases{
        component_case{"ComponentHost.Basic", "Basic component host", "Clicked 0 times", "Clicked 1 time"},
        component_case{"Hybrid.ReactIslands", "Independent component root", "Island count: 0", "Island count: 1"},
        component_case{"TypeScriptDesktop", "Desktop workspace", "Form", "Profile form"},
        component_case{"ReactDashboard", "Operations overview", "Inspect chart", "Chart details"},
        component_case{"PluginWorkbench", "Plugin workbench", "Load plugin", "Unload plugin"},
        component_case{"MultiInstanceWorkstation", "Isolated chart instance", "Local count: 0", "Local count: 1"},
        component_case{"WebToNativeMigration", "Existing React order panel", "#1048 · Contoso", "Legacy component note"},
        component_case{"OfflineKiosk", "Station kiosk", "Print ticket", "K-5499"},
        component_case{"HeadlessComponentTests", "Headless component tests", "Pointer events: 0", "Pointer events: 1"},
        component_case{"HostBridge.Services", "Host bridge services", "Invoke service", "Completed"},
        component_case{"AccessibilityGallery", "Accessibility gallery", "Save profile", "Profile saved successfully"},
        component_case{"CanvasWorkbench.Advanced", "Interactive canvas workbench", "Add indicator", "Moving Average"}
    };
    const auto repository_root = std::filesystem::path(__FILE__)
        .parent_path().parent_path().parent_path().parent_path();
    const auto quote = [](std::string_view value) {
        std::string result{"'"};
        for (const auto character : value) {
            if (character == '\\' || character == '\'') result.push_back('\\');
            result.push_back(character);
        }
        result.push_back('\'');
        return result;
    };
    for (const auto& fixture : cases) {
        const auto bundle_path = repository_root / "samples/components"
            / fixture.id / "dist/main.js";
        std::ifstream stream(bundle_path, std::ios::binary);
        require(stream.good(), "component catalog bundle was not found: " + std::string(fixture.id));
        const std::string bundle{
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
        auto* engine = webscene_engine_create(64);
        require(engine != nullptr, "component catalog engine creation failed");
        const auto release_engine = std::unique_ptr<webscene_engine, decltype(&webscene_engine_destroy)>(
            engine, webscene_engine_destroy);
        execute_and_wait(engine, R"JS(
          globalThis.webscene = {
            profileVersion: '1.0',
            host: new Proxy({}, { get: (_, capability) => ({
              invoke(method, argumentsValue = {}) {
                (globalThis.__componentInvocations ||= []).push({ capability, method, argumentsValue });
                return Promise.resolve({ accepted: true });
              }
            }) })
          };
        )JS", "component-catalog-host.js");
        execute_and_wait(engine, bundle, std::string(fixture.id) + "-bundle.js");
        require(
            evaluate(
                engine,
                "mount({instanceId:" + quote(fixture.id) + "}); true",
                std::string(fixture.id) + "-mount.js") == "true",
            "component catalog mount failed: " + std::string(fixture.id));
        auto mounted = false;
        for (auto attempt = 0; attempt < 500 && !mounted; ++attempt) {
            mounted = evaluate(
                engine,
                "!!document.querySelector('main') && document.body.textContent.includes("
                    + quote(fixture.visible_text) + ") && document.body.textContent.trim().length > 80",
                std::string(fixture.id) + "-mounted.js") == "true";
            if (!mounted) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        require(mounted, "component catalog visible mount failed: " + std::string(fixture.id));

        const auto interaction = R"JS((() => {
          const expected = )JS" + quote(fixture.button_text) + R"JS(;
          const button = Array.from(document.querySelectorAll('button'))
            .find(candidate => candidate.textContent.includes(expected));
          if (!button) return false;
          let keyObserved = false;
          button.addEventListener('keydown', event => { keyObserved = event.key === 'Enter'; }, { once: true });
          button.focus();
          const keyEvent = new Event('keydown', { bubbles: true });
          Object.defineProperty(keyEvent, 'key', { value: 'Enter' });
          button.dispatchEvent(keyEvent);
          if (document.activeElement !== button || !keyObserved) return false;
          button.click();
          return true;
        })())JS";
        require(
            evaluate(engine, interaction, std::string(fixture.id) + "-interaction.js") == "true",
            "component catalog keyboard/pointer interaction failed: " + std::string(fixture.id));
        auto updated = false;
        for (auto attempt = 0; attempt < 500 && !updated; ++attempt) {
            updated = evaluate(
                engine,
                "document.body.textContent.includes(" + quote(fixture.expected_text) + ")",
                std::string(fixture.id) + "-updated.js") == "true";
            if (!updated) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        require(updated, "component catalog update failed: " + std::string(fixture.id));
        require(
            evaluate(engine, "unmount(); document.body.children.length === 0",
                std::string(fixture.id) + "-unmount.js") == "true",
            "component catalog unmount failed: " + std::string(fixture.id));
    }
}
void test_generated_idl_attributes_are_prototype_accessors(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const descriptor = Object.getOwnPropertyDescriptor(Element.prototype, 'id');
          const element = document.createElement('div');
          element.id = 'prototype-accessor';
          let rejectedPlainReceiver = false;
          try { descriptor.get.call({}); }
          catch (error) { rejectedPlainReceiver = error instanceof TypeError; }
          return {
            getter: typeof descriptor.get,
            setter: typeof descriptor.set,
            enumerable: descriptor.enumerable,
            configurable: descriptor.configurable,
            own: Object.prototype.hasOwnProperty.call(element, 'id'),
            value: element.getAttribute('id'),
            rejectedPlainReceiver
          };
        })()
    )JS", "native-generated-idl-prototype-accessors.js");
    require(
        result == R"JSON({"getter":"function","setter":"function","enumerable":true,"configurable":true,"own":false,"value":"prototype-accessor","rejectedPlainReceiver":true})JSON",
        "generated IDL attributes did not retain Web IDL prototype shape: " + result);
}

void test_document_links_is_a_live_named_html_collection(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '<a id="later" name="shared" href="/later"></a>';
          const links = document.links;
          const area = document.createElement('area');
          area.id = 'earlier';
          area.name = 'shared';
          area.href = '/earlier';
          document.body.prepend(area);
          let illegalConstructor = false;
          let illegalItemReceiver = false;
          let illegalNamedItemReceiver = false;
          let illegalLengthReceiver = false;
          try { new HTMLCollection(); }
          catch (error) { illegalConstructor = error instanceof TypeError; }
          try { HTMLCollection.prototype.item.call({}, 0); }
          catch (error) { illegalItemReceiver = error instanceof TypeError; }
          try { HTMLCollection.prototype.namedItem.call({}, 'earlier'); }
          catch (error) { illegalNamedItemReceiver = error instanceof TypeError; }
          try {
            Object.getOwnPropertyDescriptor(HTMLCollection.prototype, 'length').get.call({});
          } catch (error) { illegalLengthReceiver = error instanceof TypeError; }
          const beforeRemoval = {
            sameObject: links === document.links,
            brand: links instanceof HTMLCollection
              && Object.prototype.toString.call(links) === '[object HTMLCollection]',
            length: links.length,
            first: links[0]?.id,
            second: links.item(1)?.id,
            id: links.namedItem('earlier')?.id,
            nameFirst: links.namedItem('shared')?.id,
            namedProperty: links.earlier?.id,
            lengthOnPrototype: !Object.hasOwn(links, 'length')
              && typeof Object.getOwnPropertyDescriptor(
                HTMLCollection.prototype, 'length')?.get === 'function'
          };
          area.removeAttribute('href');
          const afterHrefRemoval = [
            links.length,
            links[0]?.id,
            links.namedItem('earlier'),
            links.earlier
          ];
          area.setAttribute('href', '/earlier');
          area.remove();
          return {
            beforeRemoval,
            afterHrefRemoval,
            afterDetach: [links.length, links[0]?.id],
            illegalConstructor,
            illegalItemReceiver,
            illegalNamedItemReceiver,
            illegalLengthReceiver
          };
        })()
    )JS", "native-document-links-html-collection.js");
    require(
        result == R"JSON({"beforeRemoval":{"sameObject":true,"brand":true,"length":2,"first":"earlier","second":"later","id":"earlier","nameFirst":"earlier","namedProperty":"earlier","lengthOnPrototype":true},"afterHrefRemoval":[1,"later",null,null],"afterDetach":[1,"later"],"illegalConstructor":true,"illegalItemReceiver":true,"illegalNamedItemReceiver":true,"illegalLengthReceiver":true})JSON",
        "Document.links did not retain live named HTMLCollection semantics: " + result);
}
