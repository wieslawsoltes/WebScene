void test_native_text_input_focus_events_and_caret(webscene_engine* engine)
{
    resize(engine, 320, 120, 20U);
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const input = document.createElement('input');
          input.type = 'search';
          input.style.position = 'absolute';
          input.style.left = '20px';
          input.style.top = '20px';
          input.style.width = '180px';
          input.style.height = '32px';
          input.style.paddingLeft = '4px';
          input.style.fontSize = '16px';
          input.style.color = 'rgb(18, 52, 86)';
          const form = document.createElement('form');
          document.body.appendChild(form);
          form.appendChild(input);
          globalThis.__nativeTextInput = input;
          globalThis.__nativeTextForm = form;
          globalThis.__nativeTextEvents = [];
          for (const type of ['focus', 'keydown', 'keyup', 'beforeinput', 'input']) {
            input.addEventListener(type, event => __nativeTextEvents.push({
              type: event.type,
              key: event.key || null,
              code: event.code || null,
              data: event.data ?? null,
              inputType: event.inputType || null
            }));
          }
          input.focus();
        })()
    )JS", "native-text-input-setup.js");
    require(
        evaluate(engine,
            "document.activeElement === __nativeTextInput",
            "native-text-input-ready.js") == "true",
        "native text input fixture did not receive focus");

    keyboard_input(engine, WEBSCENE_INPUT_KEY_DOWN, 'T', 21U);
    keyboard_input(engine, WEBSCENE_INPUT_TEXT, 't', 22U);
    keyboard_input(engine, WEBSCENE_INPUT_KEY_UP, 'T', 23U);
    keyboard_input(engine, WEBSCENE_INPUT_TEXT, 'r', 24U);
    keyboard_input(engine, WEBSCENE_INPUT_TEXT, 'e', 25U);
    keyboard_input(engine, WEBSCENE_INPUT_TEXT, 'n', 26U);
    keyboard_input(engine, WEBSCENE_INPUT_TEXT, 'd', 27U);
    const auto typed = evaluate(engine, R"JS((() => ({
      value: __nativeTextInput.value,
      selection: [__nativeTextInput.selectionStart, __nativeTextInput.selectionEnd],
      focused: document.activeElement === __nativeTextInput,
      formContract: ['form' in __nativeTextInput, __nativeTextInput.form === __nativeTextForm,
        !('form' in document.createElement('div'))],
      keydown: __nativeTextEvents.find(event => event.type === 'keydown'),
      keyup: __nativeTextEvents.find(event => event.type === 'keyup'),
      inputs: __nativeTextEvents.filter(event => event.type === 'input')
        .map(event => [event.data, event.inputType])
    }))())JS", "native-text-input-typed.js");
    if (typed != R"({"value":"trend","selection":[5,5],"focused":true,"formContract":[true,true,true],"keydown":{"type":"keydown","key":"T","code":"KeyT","data":null,"inputType":null},"keyup":{"type":"keyup","key":"T","code":"KeyT","data":null,"inputType":null},"inputs":[["t","insertText"],["r","insertText"],["e","insertText"],["n","insertText"],["d","insertText"]]})") {
        fail("native text input did not preserve focus, value, selection, or DOM events: "
            + typed + "; error=" + last_error(engine) + "; diagnostics=" + diagnostics(engine));
    }

    animation_frame(engine, 100.0, 28U);
    require(
        evaluate(engine, "__nativeTextInput.value", "native-text-input-frame-ready.js")
            == "\"trend\"",
        "native text input frame did not drain");
    // The shared test engine has already observed later host timestamps in
    // transition fixtures. Re-arm the caret after this fixture's first frame so
    // the blink assertion measures a local 500 ms phase instead of depending
    // on timestamp ordering between otherwise independent tests.
    keyboard_input(engine, WEBSCENE_INPUT_KEY_DOWN, 35U, 28U);
    keyboard_input(engine, WEBSCENE_INPUT_KEY_UP, 35U, 29U);
    require(
        evaluate(engine, "true", "native-text-input-caret-rearmed.js") == "true",
        "native text input caret reset did not drain");
    webscene_engine_request_scene_checkpoint(engine);
    bool observed_caret = false;
    uint64_t caret_scene_revision = 0U;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind == 14U
                    && (command.rgba >> 8U) == 0x123456U
                    && command.x >= 20.0F && command.x <= 200.0F
                    && command.y >= 20.0F && command.y <= 52.0F) {
                    observed_caret = true;
                    caret_scene_revision = scene->header.revision;
                    break;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (observed_caret) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(observed_caret, "focused native input did not publish a visible caret");

    animation_frame(engine, 600.0, 30U);
    require(
        evaluate(engine, "document.activeElement === __nativeTextInput",
            "native-text-input-blink-ready.js") == "true",
        "native text input lost focus while blinking its caret");
    webscene_engine_request_scene_checkpoint(engine);
    bool observed_hidden_caret_phase = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            if (scene->header.revision > caret_scene_revision
                && (scene->header.flags & 1U) != 0U) {
                auto has_caret = false;
                for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                    const auto& command = scene->commands[index];
                    if (command.kind == 14U
                        && (command.rgba >> 8U) == 0x123456U
                        && command.x >= 20.0F && command.x <= 200.0F
                        && command.y >= 20.0F && command.y <= 52.0F) {
                        has_caret = true;
                        break;
                    }
                }
                observed_hidden_caret_phase = !has_caret;
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (observed_hidden_caret_phase) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(observed_hidden_caret_phase, "native input caret did not blink off");

    const auto press_key = [&](uint32_t key_code, uint32_t modifiers, uint64_t& sequence) {
        keyboard_input(engine, WEBSCENE_INPUT_KEY_DOWN, key_code, sequence++, modifiers);
        keyboard_input(engine, WEBSCENE_INPUT_KEY_UP, key_code, sequence++, modifiers);
        require(evaluate(engine, "true", "native-text-input-key-drain.js") == "true",
            "native keyboard navigation events did not drain");
    };
    uint64_t editing_sequence = 31U;
    press_key(37U, 0U, editing_sequence);
    press_key(37U, WEBSCENE_INPUT_MODIFIER_SHIFT, editing_sequence);
    const auto selected = evaluate(engine, R"JS((() => ({
      value: __nativeTextInput.value,
      selection: [__nativeTextInput.selectionStart, __nativeTextInput.selectionEnd],
      direction: __nativeTextInput.selectionDirection
    }))())JS", "native-text-input-shift-selection.js");
    if (selected != R"({"value":"trend","selection":[3,4],"direction":"backward"})") {
        fail("native text input did not extend its selection with Shift+ArrowLeft: " + selected);
    }

    press_key(39U, WEBSCENE_INPUT_MODIFIER_SHIFT, editing_sequence);
    press_key(36U, 0U, editing_sequence);
    press_key(40U, 0U, editing_sequence);
    press_key(38U, 0U, editing_sequence);
    press_key(35U, 0U, editing_sequence);
    press_key(37U, WEBSCENE_INPUT_MODIFIER_SHIFT, editing_sequence);
    const auto navigated = evaluate(engine, R"JS((() => ({
      value: __nativeTextInput.value,
      selection: [__nativeTextInput.selectionStart, __nativeTextInput.selectionEnd],
      direction: __nativeTextInput.selectionDirection,
      keys: __nativeTextEvents.filter(event => event.type === 'keydown').slice(-8)
        .map(event => [event.key, event.code])
    }))())JS", "native-text-input-key-navigation.js");
    if (navigated != R"({"value":"trend","selection":[4,5],"direction":"backward","keys":[["ArrowLeft","ArrowLeft"],["ArrowLeft","ArrowLeft"],["ArrowRight","ArrowRight"],["Home","Home"],["ArrowDown","ArrowDown"],["ArrowUp","ArrowUp"],["End","End"],["ArrowLeft","ArrowLeft"]]})") {
        fail("native text input did not preserve Arrow/Home/End navigation semantics: " + navigated);
    }

    press_key(46U, 0U, editing_sequence);
    press_key(8U, 0U, editing_sequence);
    const auto erased = evaluate(engine, R"JS((() => ({
      value: __nativeTextInput.value,
      selection: [__nativeTextInput.selectionStart, __nativeTextInput.selectionEnd],
      direction: __nativeTextInput.selectionDirection,
      lastInputs: __nativeTextEvents.filter(event => event.type === 'input').slice(-2)
        .map(event => [event.key, event.inputType])
    }))())JS", "native-text-input-backspace.js");
    if (erased != R"({"value":"tre","selection":[3,3],"direction":"none","lastInputs":[["Delete","deleteContentForward"],["Backspace","deleteContentBackward"]]})") {
        fail("native text input did not apply Delete and Backspace through DOM input events: "
            + erased);
    }

    execute(engine, R"JS(
        (() => {
          const textarea = document.createElement('textarea');
          textarea.value = 'firstsecond';
          textarea.selectionStart = textarea.selectionEnd = 5;
          textarea.style.width = '160px';
          textarea.style.height = '48px';
          globalThis.__nativeTextareaEvents = [];
          for (const type of ['keydown', 'beforeinput', 'input']) {
            textarea.addEventListener(type, event => __nativeTextareaEvents.push([
              event.type, event.key || null, event.data ?? null, event.inputType || null
            ]));
          }
          document.body.appendChild(textarea);
          textarea.focus();
          globalThis.__nativeTextarea = textarea;
        })()
    )JS", "native-textarea-enter-setup.js");
    require(
        evaluate(engine,
            "document.activeElement === __nativeTextarea",
            "native-textarea-enter-ready.js") == "true",
        "native textarea fixture did not receive focus");
    press_key(13U, 0U, editing_sequence);
    const auto line_break = evaluate(engine, R"JS((() => ({
      value: __nativeTextarea.value,
      selection: [__nativeTextarea.selectionStart, __nativeTextarea.selectionEnd],
      events: __nativeTextareaEvents
    }))())JS", "native-textarea-enter-result.js");
    if (line_break != R"({"value":"first\nsecond","selection":[6,6],"events":[["keydown","Enter",null,null],["beforeinput","Enter",null,"insertLineBreak"],["input","Enter",null,"insertLineBreak"]]})") {
        fail("native textarea Enter did not apply the browser line-break default: "
            + line_break);
    }
}

void test_frame_script_dom_presence(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          const frame = document.createElement('iframe');
          const outer = document.createElement('div');
          outer.id = 'outer-only';
          document.body.appendChild(outer);
          document.body.appendChild(frame);
          const frameDocument = frame.contentDocument;
          frameDocument.open();
          frameDocument.write(
            '<html><body><script>'
            + 'globalThis.__scriptDomPresent = '
            + 'document.getElementsByTagName("script")[0].parentNode === document.body;'
            + 'globalThis.__modernInputPresent = "oninput" in document;'
            + 'globalThis.__documentAddressPresent = '
            + '[document.URL, document.documentURI, document.baseURI].every('
            + 'value => typeof value === "string" && !!value.match(/^https?:\\/\\//));'
            + '</script></body></html>');
          frameDocument.close();
          globalThis.__scriptDomFrame = frame;
        })()
    )JS", "native-frame-script-dom-setup.js");
    require(
        evaluate(
            engine,
            "[globalThis.__scriptDomFrame.contentWindow.__scriptDomPresent, "
            "globalThis.__scriptDomFrame.contentWindow.__modernInputPresent, "
            "globalThis.__scriptDomFrame.contentWindow.__documentAddressPresent]",
            "native-frame-script-dom-result.js") == "[true,true,true]",
        "hydrated frame scripts, modern input detection, or Document URL semantics regressed");
    const auto boundary = evaluate(
        engine,
        "(() => { const d = globalThis.__scriptDomFrame.contentDocument; return ["
        "!!d.querySelector('script'), !!d.getElementById('outer-only'), "
        "d.querySelectorAll('script').length, d.defaultView === globalThis.__scriptDomFrame.contentWindow]; })()",
        "native-frame-content-document-boundary.js");
    require(
        boundary == "[true,false,1,true]",
        "contentDocument queries escaped the frame document boundary: " + boundary);
}

std::string measure_cooperative_iframe_task_order(bool disable_cooperation)
{
#if defined(_WIN32)
    _putenv_s(
        "WEBSCENE_PROBE_DISABLE_COOPERATIVE_IFRAME_HYDRATION",
        disable_cooperation ? "1" : "");
#else
    if (disable_cooperation) {
        setenv("WEBSCENE_PROBE_DISABLE_COOPERATIVE_IFRAME_HYDRATION", "1", 1);
    } else {
        unsetenv("WEBSCENE_PROBE_DISABLE_COOPERATIVE_IFRAME_HYDRATION");
    }
#endif
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "cooperative iframe engine creation failed");
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const outerStyle = document.createElement('style');
          outerStyle.textContent = '.cascade-target { color: rgb(170, 0, 0); }';
          document.body.appendChild(outerStyle);
          const outer = document.createElement('div');
          outer.className = 'cascade-target';
          document.body.appendChild(outer);
          globalThis.__iframeTaskOrder = [];
          globalThis.__outerCascadeTarget = outer;
          setTimeout(() => __iframeTaskOrder.push('timer'), 0);
          const frame = document.createElement('iframe');
          document.body.appendChild(frame);
          const frameDocument = frame.contentDocument;
          frameDocument.open();
          frameDocument.write(
            '<html><style>.cascade-target { color: rgb(0, 0, 187); }</style><body>'
            + '<div class="cascade-target" id="inner"></div>'
            + '<script>'
            + 'parent.__iframeTaskOrder.push("blocking");'
            + 'parent.__innerCascadeColor = getComputedStyle('
            + 'document.getElementById("inner")).color;'
            + 'addEventListener("DOMContentLoaded", () => '
            + 'parent.__iframeTaskOrder.push("dom"));'
            + 'addEventListener("load", () => parent.__iframeTaskOrder.push("load"));'
            + 'const deadline = Date.now() + 20; while (Date.now() < deadline) {}'
            + '<\/script>'
            + '<script defer>parent.__iframeTaskOrder.push("deferred");<\/script>'
            + '</body></html>');
          frameDocument.close();
        })()
    )JS", "cooperative-iframe-order.js");
    auto result = std::string{};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        if (evaluate(
                engine,
                "globalThis.__iframeTaskOrder.length",
                "cooperative-iframe-progress.js") == "5") {
            result = evaluate(engine, R"JS((() => ({
              order: globalThis.__iframeTaskOrder,
              outer: getComputedStyle(globalThis.__outerCascadeTarget).color,
              inner: globalThis.__innerCascadeColor
            }))())JS", "cooperative-iframe-result.js");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    webscene_engine_destroy(engine);
#if defined(_WIN32)
    _putenv_s("WEBSCENE_PROBE_DISABLE_COOPERATIVE_IFRAME_HYDRATION", "");
#else
    unsetenv("WEBSCENE_PROBE_DISABLE_COOPERATIVE_IFRAME_HYDRATION");
#endif
    return result;
}

void test_cooperative_iframe_hydration_yields_and_isolates_cascade()
{
    const auto optimized = measure_cooperative_iframe_task_order(false);
    require(
        optimized == R"JSON({"order":["blocking","timer","deferred","dom","load"],"outer":"rgb(170, 0, 0)","inner":"rgb(0, 0, 187)"})JSON",
        "cooperative iframe hydration did not yield or isolate cascade state: "
            + optimized);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    const auto control = measure_cooperative_iframe_task_order(true);
    require(
        control == R"JSON({"order":["blocking","deferred","dom","load","timer"],"outer":"rgb(170, 0, 0)","inner":"rgb(0, 0, 187)"})JSON",
        "cooperative iframe A/B control did not restore monolithic ordering: "
            + control);
#endif
}

#if defined(WEBSCENE_NATIVE_ENGINE_HTML5EVER)
void test_html5ever_frame_does_not_duplicate_authored_loading_indicator(
    webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          const frame = document.createElement('iframe');
          document.body.appendChild(frame);
          const frameDocument = frame.contentDocument;
          frameDocument.open();
          frameDocument.write(
            '<html><body>'
            + '<div class="loading-indicator" style="display:none"></div>'
            + '<script>globalThis.__loadingIndicators = ['
            + 'document.querySelectorAll(".loading-indicator").length,'
            + 'getComputedStyle(document.querySelector(".loading-indicator")).display,'
            + 'document.elementFromPoint(5, 5)?.className || ""'
            + '];<\/script></body></html>');
          frameDocument.close();
          globalThis.__loadingIndicatorFrame = frame;
        })()
    )JS", "native-html5ever-frame-loading-indicator-setup.js");
    const auto result = evaluate(
        engine,
        "globalThis.__loadingIndicatorFrame.contentWindow.__loadingIndicators",
        "native-html5ever-frame-loading-indicator-result.js");
    require(
        result == R"([1,"none",""])",
        "html5ever duplicated or hit-tested an authored hidden loading indicator: "
            + result);
}
#endif

void test_dom_element_constructor_identity(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const outer = document.createElement('div');
          outer.style.borderLeft = '3px solid black';
          outer.style.borderTop = '5px solid black';
          document.body.appendChild(outer);
          globalThis.__outerElementIdentity = {
            node: outer instanceof Node,
            element: outer instanceof Element,
            htmlElement: outer instanceof HTMLElement,
            ownerElement: outer instanceof outer.ownerDocument.defaultView.Element,
            ownerHtmlElement:
              outer instanceof outer.ownerDocument.defaultView.HTMLElement,
            clientLeft: outer.clientLeft,
            clientTop: outer.clientTop
          };
          const frame = document.createElement('iframe');
          document.body.appendChild(frame);
          const frameDocument = frame.contentDocument;
          frameDocument.open();
          frameDocument.write(
            '<html><body><script>'
            + 'const element = document.createElement("div");'
            + 'document.body.appendChild(element);'
            + 'globalThis.__elementIdentity = {'
            + 'node: element instanceof Node,'
            + 'element: element instanceof Element,'
            + 'htmlElement: element instanceof HTMLElement,'
            + 'ownerElement: element instanceof document.defaultView.Element,'
            + 'ownerHtmlElement: element instanceof document.defaultView.HTMLElement'
            + '};'
            + '</script></body></html>');
          frameDocument.close();
          globalThis.__elementIdentityFrame = frame;
        })()
    )JS", "native-dom-element-constructor-identity-setup.js");
    const auto result = evaluate(engine, R"JS(
        ({
          outer: globalThis.__outerElementIdentity,
          frame: globalThis.__elementIdentityFrame.contentWindow.__elementIdentity
        })
    )JS", "native-dom-element-constructor-identity-result.js");
    require(
        result == R"({"outer":{"node":true,"element":true,"htmlElement":true,"ownerElement":true,"ownerHtmlElement":true,"clientLeft":3,"clientTop":5},"frame":{"node":true,"element":true,"htmlElement":true,"ownerElement":true,"ownerHtmlElement":true}})",
        "DOM wrappers did not expose Element identity or client border insets: "
            + result);
}

void test_provisional_frame_focus_and_document_event_identity(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const outer = document.createElement('button');
          outer.id = 'outer-focus';
          const frame = document.createElement('iframe');
          frame.id = 'focus-frame';
          document.body.appendChild(outer);
          document.body.appendChild(frame);
          const inner = document.createElement('input');
          inner.id = 'inner-focus';
          frame.contentDocument.body.appendChild(inner);
          let documentEvents = 0;
          document.addEventListener('webscene-document-event', () => documentEvents++);
          document.dispatchEvent(new Event('webscene-document-event'));
          inner.focus();
          const focused = {
            outer: document.activeElement?.id || '',
            inner: frame.contentDocument.activeElement?.id || '',
            defaultView: frame.contentDocument.defaultView === frame.contentWindow
          };
          outer.focus();
          return {
            focused,
            restoredOuter: document.activeElement?.id || '',
            restoredInner: frame.contentDocument.activeElement?.tagName || '',
            documentEvents
          };
        })()
    )JS", "native-provisional-frame-focus.js");
    require(
        result == R"({"focused":{"outer":"focus-frame","inner":"inner-focus","defaultView":true},"restoredOuter":"outer-focus","restoredInner":"BODY","documentEvents":1})",
        "provisional frame focus ownership or document EventTarget identity regressed: " + result);
}

void test_initial_frame_document_write_and_hidden_style(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const wrapper = document.createElement('div');
          const frame = document.createElement('iframe');
          wrapper.appendChild(frame);
          document.body.appendChild(wrapper);
          const frameWindow = frame.contentWindow;
          const frameDocument = frame.contentDocument;
          const initial = [
            frameWindow.document === frameDocument,
            frameDocument.defaultView === frameWindow,
            frameWindow.frameElement === frame,
            frameWindow.location.href
          ];
          frameDocument.open();
          frameDocument.write(
            '<!doctype html><html><body><div id="written">Hi</div></body></html>');
          frameDocument.close();
          wrapper.style.display = 'none';
          return {
            initial,
            stable: frame.contentDocument === frameDocument,
            text: frameDocument.querySelector('#written').textContent,
            direction: frameWindow.getComputedStyle(frameDocument.body).direction
          };
        })()
    )JS", "native-initial-frame-document-write.js");
    require(
        result == R"({"initial":[true,true,true,"about:blank"],"stable":true,"text":"Hi","direction":"ltr"})",
        "initial iframe document replacement or hidden computed style regressed: " + result);
}

void test_detached_dom_wrappers_do_not_permanently_root_nodes(webscene_engine* engine)
{
    webscene_engine_metrics before{};
    webscene_engine_get_metrics(engine, &before);
    const auto detached_event = evaluate(engine, R"JS(
        (() => {
          const target = document.createElement('div');
          document.body.appendChild(target);
          let calls = 0;
          target.addEventListener('closed.bs.alert', () => calls++);
          target.dispatchEvent(new Event('closed.bs.alert'));
          target.remove();
          const dispatched = target.dispatchEvent(
            new Event('closed.bs.alert', { bubbles: true, cancelable: true }));
          return { calls, dispatched, connected: target.isConnected };
        })()
    )JS", "native-detached-custom-event.js");
    require(
        detached_event == R"({"calls":2,"dispatched":true,"connected":false})",
        "a detached element stopped dispatching custom events: " + detached_event);
    execute(engine, R"JS(
        (() => {
          const retained = document.createElement('div');
          retained.id = 'retained-detached-node';
          retained.__webSceneExpando = 42;
          document.body.appendChild(retained);
          retained.remove();
          globalThis.__webSceneRetainedDetachedNode = retained;
          for (let index = 0; index < 700; index++) {
            const node = document.createElement('div');
            node.__webSceneDetachedIndex = index;
            document.body.appendChild(node);
            node.remove();
          }
        })()
    )JS", "native-detached-dom-gc.js");
    require(
        evaluate(engine, R"JS(
          (() => {
            const retained = globalThis.__webSceneRetainedDetachedNode;
            document.body.appendChild(retained);
            return {
              expando: retained.__webSceneExpando,
              identity: document.getElementById('retained-detached-node') === retained,
              connected: retained.isConnected
            };
          })()
        )JS", "native-detached-dom-retain.js")
            == R"({"expando":42,"identity":true,"connected":true})",
        "a reachable detached wrapper lost identity or expando state across native DOM collection");
    webscene_engine_metrics after{};
    webscene_engine_get_metrics(engine, &after);
    require(
        after.dom_nodes <= before.dom_nodes + 8U,
        "unreachable detached DOM nodes remained permanently owned after V8 collection");
    execute(engine, R"JS(
        (() => {
          globalThis.__webSceneRetainedDetachedNode.remove();
          delete globalThis.__webSceneRetainedDetachedNode;
        })()
    )JS", "native-detached-dom-cleanup.js");
}

void test_session_storage_in_outer_and_frame_contexts(webscene_engine* engine)
{
    const auto outer = evaluate(engine, R"JS(
        (() => {
          sessionStorage.clear();
          localStorage.clear();
          sessionStorage.setItem('view', 'calendar');
          sessionStorage.setItem('count', 1);
          sessionStorage.setItem('view', 'date');
          const beforeRemove = {
            length: sessionStorage.length,
            keys: [sessionStorage.key(0), sessionStorage.key(1), sessionStorage.key(2)],
            view: sessionStorage.getItem('view'),
            count: sessionStorage.getItem('count'),
            missing: sessionStorage.getItem('missing')
          };
          sessionStorage.removeItem('count');
          localStorage.setItem('theme', 'dark');
          return {
            beforeRemove,
            length: sessionStorage.length,
            first: sessionStorage.key(0),
            localTheme: localStorage.getItem('theme')
          };
        })()
    )JS", "native-session-storage-outer.js");
    if (outer != R"({"beforeRemove":{"length":2,"keys":["view","count",null],"view":"date","count":"1","missing":null},"length":1,"first":"view","localTheme":"dark"})") {
        fail("outer sessionStorage semantics regressed: " + outer);
    }

    execute(engine, R"JS(
        (() => {
          const frame = document.createElement('iframe');
          document.body.appendChild(frame);
          const frameDocument = frame.contentDocument;
          frameDocument.open();
          frameDocument.write(
            '<html><body><script>'
            + 'globalThis.__sessionStorageResult = {'
            + 'outerValue: sessionStorage.getItem("view"),'
            + 'initialLength: sessionStorage.length,'
            + 'localTheme: localStorage.getItem("theme")};'
            + 'sessionStorage.setItem("go-to-date", "month");'
            + 'localStorage.setItem("layout", "compact");'
            + 'globalThis.__sessionStorageResult.value = sessionStorage.getItem("go-to-date");'
            + 'globalThis.__sessionStorageResult.length = sessionStorage.length;'
            + '</script></body></html>');
          frameDocument.close();
          globalThis.__sessionStorageFrame = frame;
        })()
    )JS", "native-session-storage-frame-setup.js");
    const auto frame = evaluate(
        engine,
        "globalThis.__sessionStorageFrame.contentWindow.__sessionStorageResult",
        "native-session-storage-frame-result.js");
    if (frame != R"({"outerValue":null,"initialLength":0,"localTheme":"dark","value":"month","length":1})") {
        fail("frame sessionStorage semantics or isolation regressed: " + frame);
    }
    require(
        evaluate(
            engine,
            "localStorage.getItem('layout')",
            "native-local-storage-frame-sharing.js") == R"("compact")",
        "same-origin localStorage did not remain shared between outer and frame contexts");

    const auto coercion = evaluate(engine, R"JS(
        (() => {
          localStorage.clear();
          const objectKey = { toString() { return 'object-key'; } };
          const objectValue = { toString() { return 'object-value'; } };
          localStorage.setItem(undefined, null);
          localStorage.setItem(true, 42);
          localStorage.setItem(objectKey, objectValue);
          localStorage.setItem('\ud800', '\udc00');
          localStorage.setItem('stable', 'first');
          localStorage.setItem('stable', 'replacement');
          const beforeReinsert = Array.from(
            { length: localStorage.length },
            (_, index) => localStorage.key(index))
            .map(key => key.length === 1 && key.charCodeAt(0) === 0xd800
              ? '<surrogate>' : key);
          localStorage.removeItem(true);
          localStorage.setItem(true, false);
          const afterReinsert = Array.from(
            { length: localStorage.length },
            (_, index) => localStorage.key(index))
            .map(key => key.length === 1 && key.charCodeAt(0) === 0xd800
              ? '<surrogate>' : key);
          let exceptional = false;
          try {
            localStorage.setItem(
              { toString() { throw new Error('storage coercion'); } },
              'must-not-be-written');
          } catch (error) {
            exceptional = error.message === 'storage coercion';
          }
          return {
            undefinedValue: localStorage.getItem(undefined),
            booleanValue: localStorage.getItem(true),
            objectValue: localStorage.getItem(objectKey),
            surrogateKey: localStorage.key(2).charCodeAt(0),
            surrogateValue: localStorage.getItem('\ud800').charCodeAt(0),
            beforeReinsert,
            afterReinsert,
            exceptional,
            absent: localStorage.getItem('must-not-be-written'),
            invalidKeys: [localStorage.key(-1), localStorage.key(999)]
          };
        })()
    )JS", "native-storage-coercion.js");
    require(
        coercion == R"({"undefinedValue":"null","booleanValue":"false","objectValue":"object-value","surrogateKey":55296,"surrogateValue":56320,"beforeReinsert":["undefined","true","object-key","<surrogate>","stable"],"afterReinsert":["undefined","object-key","<surrogate>","stable","true"],"exceptional":true,"absent":null,"invalidKeys":[null,null]})",
        "Web Storage coercion, Unicode, exception, or insertion ordering regressed: "
            + coercion);
}

void test_window_post_message_is_queued(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          let state = 'initial';
          addEventListener('message', () => {
            state = 'delivered';
            globalThis.__postMessageFinalState = state;
          }, { once: true });
          postMessage('idle-work', '*');
          globalThis.__postMessageSynchronousState = state;
        })()
    )JS", "native-window-post-message-queue.js");
    const auto result = evaluate(engine, R"JS(
        ({
          synchronous: globalThis.__postMessageSynchronousState,
          final: globalThis.__postMessageFinalState
        })
    )JS", "native-window-post-message-queue-result.js");
    require(
        result == R"({"synchronous":"initial","final":"delivered"})",
        "Window.postMessage delivered synchronously or missed its queued task: " + result);
}

void test_cross_frame_post_message_and_window_frames(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          const frame = document.createElement('iframe');
          document.body.appendChild(frame);
          const frameDocument = frame.contentDocument;
          frameDocument.open();
          frameDocument.write(
            '<html><body><script>'
            + 'globalThis.__messageListenerReady = true;'
            + 'addEventListener("message", event => {'
            + 'globalThis.__frameReceived = event.data;'
            + 'parent.postMessage({'
            + 'payload: event.data,'
            + 'sourceIsParent: event.source === parent,'
            + 'origin: event.origin'
            + '}, "*");'
            + '});'
            + '</script></body></html>');
          frameDocument.close();
          globalThis.__postMessageFrame = frame;
        })()
    )JS", "native-cross-frame-post-message-setup.js");
    require(
        evaluate(
            engine,
            "(() => { const child = globalThis.__postMessageFrame.contentWindow; return { contains: Array.from(frames).includes(child), ready: typeof child.postMessage, listener: child.__messageListenerReady }; })()",
            "native-cross-frame-post-message-ready.js")
            == R"({"contains":true,"ready":"function","listener":true})",
        "window.frames did not expose the hydrated child realm");
    execute(engine, R"JS(
        (() => {
          globalThis.__crossFrameSynchronous = 'initial';
          addEventListener('message', event => {
            const child = globalThis.__postMessageFrame.contentWindow;
            globalThis.__crossFrameResult = {
              reply: event.data,
              sourceIsFrame: event.source === child,
              framesContainsChild: Array.from(frames).includes(child),
              frameFocusType: typeof child.focus
            };
          }, { once: true });
          globalThis.__postMessageFrame.contentWindow.postMessage(
            'chart-datafeed-request',
            '*');
          globalThis.__crossFrameSynchronous =
            globalThis.__crossFrameResult ? 'delivered' : 'initial';
        })()
    )JS", "native-cross-frame-post-message-send.js");
    require(
        evaluate(
            engine,
            "globalThis.__postMessageFrame.contentWindow.__frameReceived",
            "native-cross-frame-post-message-frame-result.js")
            == R"("chart-datafeed-request")",
        "the queued outer-to-frame message was not delivered");
    const auto result = evaluate(engine, R"JS(
        ({
          synchronous: globalThis.__crossFrameSynchronous,
          result: globalThis.__crossFrameResult
        })
    )JS", "native-cross-frame-post-message-result.js");
    require(
        result == R"({"synchronous":"initial","result":{"reply":{"payload":"chart-datafeed-request","sourceIsParent":true,"origin":"null"},"sourceIsFrame":true,"framesContainsChild":true,"frameFocusType":"function"}})",
        "cross-frame Window.postMessage source/origin/frames semantics regressed: " + result);
    execute(engine, R"JS(
        globalThis.__postMessageFrame.remove();
        delete globalThis.__postMessageFrame;
    )JS", "native-cross-frame-post-message-cleanup.js");
}

void test_frame_resize_preserves_outer_percentage_height(webscene_engine* engine)
{
    resize(engine, 500, 400, 10U);
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = '#resize-host { width: 100%; height: 100%; }';
          document.body.appendChild(style);
          const host = document.createElement('div');
          host.id = 'resize-host';
          const frame = document.createElement('iframe');
          frame.style.width = '100%';
          frame.style.height = '100%';
          host.appendChild(frame);
          document.body.appendChild(host);
          const frameDocument = frame.contentDocument;
          frameDocument.open();
          frameDocument.write(
            '<html><style>body { width: 100%; height: 100%; }</style>'
            + '<body><div style="height:100%"></div></body></html>');
          frameDocument.close();
          globalThis.__resizeHost = host;
          globalThis.__resizeHostFrame = frame;
        })()
    )JS", "native-frame-resize-percentage-setup.js");
    require(
        evaluate(
            engine,
            "globalThis.__resizeHostFrame.contentWindow.innerHeight",
            "native-frame-resize-percentage-ready.js") == "400",
        "percentage-height frame fixture did not hydrate");

    resize(engine, 500, 650, 11U);
    const auto result = evaluate(
        engine,
        "[globalThis.__resizeHost.getBoundingClientRect().height, "
        "globalThis.__resizeHostFrame.getBoundingClientRect().height, "
        "globalThis.__resizeHostFrame.contentWindow.innerHeight]",
        "native-frame-resize-percentage-result.js");
    if (result != "[650,650,650]") {
        fail("frame resize erased the outer percentage-height cascade: " + result);
    }
}

void test_inner_window_load_acknowledgement(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const frame = document.createElement('iframe');
          document.body.appendChild(frame);
          frame.contentWindow.addEventListener('innerWindowLoad', event => {
            event.detail.received = true;
          });
          const detail = { received: false };
          window.dispatchEvent(new CustomEvent('innerWindowLoad', { detail }));
          return detail.received;
        })()
    )JS", "native-inner-window-load-acknowledgement.js");
    require(
        result == "true",
        "inner-window listener acknowledgement was not reflected in CustomEvent.detail");
}

void test_startup_profile_names_scripts_and_tasks()
{
    // Use a dedicated engine so its first component-ready publication captures
    // this script's startup profile. Reusing the broad DOM regression engine
    // made the assertion depend on whether an earlier test had already frozen
    // its one-time readiness diagnostic snapshot.
    auto* engine = webscene_engine_create(64);
    require(engine != nullptr, "startup-profile engine creation failed");
    execute(engine, R"JS(
        globalThis.__profiledTimerRan = false;
        let checksum = 0;
        for (let index = 0; index < 1000000; ++index) {
          checksum = (checksum + index) | 0;
        }
        globalThis.__profiledChecksum = checksum;
        globalThis.__webSceneComponentReady = true;
        setTimeout(function profiledTimer() {
          let timerChecksum = 0;
          for (let index = 0; index < 1000000; ++index) {
            timerChecksum = (timerChecksum + index) | 0;
          }
          globalThis.__profiledChecksum = timerChecksum;
          globalThis.__profiledTimerRan = true;
        }, 0);
    )JS", "native-startup-profile.js");
    require(
        evaluate(engine, "globalThis.__profiledTimerRan", "native-startup-profile-result.js")
            == "true",
        "profiled timer did not run");
    webscene_engine_metrics metrics{};
    webscene_engine_get_metrics(engine, &metrics);
    require(
        metrics.component_ready != 0U,
        "component readiness was not visible until a scene publication");
    auto value = std::string{};
    for (auto attempt = 0; attempt < 100; ++attempt) {
        value = diagnostics(engine);
        if (value.find("native-startup-profile.js") != std::string::npos
            && value.find("profiledTimer") != std::string::npos) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (value.find("script-top=[") == std::string::npos
        || value.find("native-startup-profile.js") == std::string::npos) {
        fail("startup diagnostics did not attribute top-level script execution: " + value);
    }
    if (value.find("task-top=[") == std::string::npos
        || value.find("profiledTimer") == std::string::npos) {
        fail("startup diagnostics did not attribute timer callback execution: " + value);
    }
    if (value.find("total=") == std::string::npos) {
        fail("startup diagnostics did not report total runtime age: " + value);
    }
    webscene_engine_destroy(engine);
}

void test_animation_frame_uses_host_frame(webscene_engine* engine)
{
    execute(engine, R"JS(
        globalThis.__hostFrameTimestamps = [];
        requestAnimationFrame(timestamp => {
          __hostFrameTimestamps.push(timestamp);
          requestAnimationFrame(nextTimestamp => {
            __hostFrameTimestamps.push(nextTimestamp);
          });
        });
    )JS", "native-host-animation-frame-setup.js");
    require(
        evaluate(
            engine,
            "globalThis.__hostFrameTimestamps.length",
            "native-host-animation-frame-ready.js") == "0",
        "requestAnimationFrame setup did not complete");
    wait_for_animation_frame_demand(
        engine,
        true,
        "pending requestAnimationFrame did not request a host frame");

    animation_frame(engine, 123.5, 5U);
    const auto first = evaluate(
        engine,
        "globalThis.__hostFrameTimestamps",
        "native-host-animation-frame-first.js");
    if (first != "[123.5]") {
        fail("host frame did not release exactly one requestAnimationFrame batch: " + first);
    }
    wait_for_animation_frame_demand(
        engine,
        true,
        "nested requestAnimationFrame did not retain host-frame demand");

    animation_frame(engine, 456.25, 6U);
    const auto second = evaluate(
        engine,
        "globalThis.__hostFrameTimestamps",
        "native-host-animation-frame-second.js");
    if (second != "[123.5,456.25]") {
        fail("nested requestAnimationFrame did not wait for the next host frame: " + second);
    }
    wait_for_animation_frame_demand(
        engine,
        false,
        "completed requestAnimationFrame chain retained host-frame demand");
}

void test_animation_frame_callback_list_timestamp_and_cancellation(webscene_engine* engine)
{
    execute(engine, R"JS(
        globalThis.__frameBatchEvents = [];
        let cancelled;
        requestAnimationFrame(timestamp => {
          __frameBatchEvents.push(['first', timestamp]);
          cancelAnimationFrame(cancelled);
          requestAnimationFrame(nextTimestamp => {
            __frameBatchEvents.push(['nested', nextTimestamp]);
          });
        });
        cancelled = requestAnimationFrame(timestamp => {
          __frameBatchEvents.push(['cancelled', timestamp]);
        });
        requestAnimationFrame(timestamp => {
          __frameBatchEvents.push(['last', timestamp]);
        });
    )JS", "native-animation-frame-batch-setup.js");
    require(
        evaluate(
            engine,
            "globalThis.__frameBatchEvents.length",
            "native-animation-frame-batch-ready.js") == "0",
        "animation-frame batch setup did not complete");

    animation_frame(engine, 500.75, 7U);
    auto first = std::string{};
    for (auto attempt = 0; attempt < 100; ++attempt) {
        first = evaluate(
            engine,
            "globalThis.__frameBatchEvents",
            "native-animation-frame-batch-first.js");
        if (first != "[]") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (first != R"([["first",500.75],["last",500.75]])") {
        fail("animation-frame callback list was not one cancelable timestamp batch: "
            + first);
    }

    animation_frame(engine, 525.5, 8U);
    const auto second = evaluate(
        engine,
        "globalThis.__frameBatchEvents",
        "native-animation-frame-batch-second.js");
    if (second
        != R"([["first",500.75],["last",500.75],["nested",525.5]])") {
        fail("nested animation-frame callback escaped its following host frame: "
            + second);
    }
}

void test_animation_frame_pending_callbacks_keep_timestamp(webscene_engine* engine)
{
    execute(engine, R"JS(
        globalThis.__pendingFrameEvents = [];
        requestAnimationFrame(timestamp => {
          __pendingFrameEvents.push(['slow', timestamp]);
          const deadline = performance.now() + 40;
          while (performance.now() < deadline) {}
          requestAnimationFrame(nextTimestamp => {
            __pendingFrameEvents.push(['nested', nextTimestamp]);
          });
        });
        requestAnimationFrame(timestamp => {
          __pendingFrameEvents.push(['pending', timestamp]);
        });
    )JS", "native-animation-frame-pending-setup.js");
    require(
        evaluate(
            engine,
            "globalThis.__pendingFrameEvents.length",
            "native-animation-frame-pending-ready.js") == "0",
        "pending animation-frame setup did not complete");

    const auto consumed_before = consumed_input_count(engine);
    animation_frame(engine, 600.25, 9U);
    wait_for_consumed_inputs(
        engine,
        consumed_before + 1,
        "first pending-callback animation frame was not consumed");
    animation_frame(engine, 625.5, 10U);
    wait_for_consumed_inputs(
        engine,
        consumed_before + 2,
        "second pending-callback animation frame was not consumed");

    auto result = std::string{};
    for (auto attempt = 0; attempt < 100; ++attempt) {
        result = evaluate(
            engine,
            "globalThis.__pendingFrameEvents",
            "native-animation-frame-pending-result.js");
        if (result
            == R"([["slow",600.25],["pending",600.25],["nested",625.5]])") {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    fail("a later host frame retimestamped a pending callback: " + result);
}

void test_pointer_input_precedes_following_render_opportunity()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "input/frame ordering engine creation failed");
    resize(engine, 320, 200, 1U);
    execute(engine, R"JS(
        globalThis.__inputFrameEvents = [];
        globalThis.__inputMoveCount = 0;
        document.body.innerHTML =
          '<div id="input-frame-target" style="position:fixed;inset:0"></div>';
        document.getElementById('input-frame-target').addEventListener('mousemove', () => {
          __inputMoveCount++;
          requestAnimationFrame(timestamp => {
            __inputFrameEvents.push(timestamp);
          });
        }, { once: true });
    )JS", "native-input-frame-order-setup.js");
    require(
        evaluate(
            engine,
            "globalThis.__inputFrameEvents.length",
            "native-input-frame-order-ready.js") == "0",
        "input/frame ordering setup did not complete");

    const auto consumed_before = consumed_input_count(engine);
    pointer_move(engine, 20, 20, 11U);
    animation_frame(engine, 700.5, 0U);
    wait_for_consumed_inputs(
        engine,
        consumed_before + 2,
        "adjacent pointer/frame inputs were not consumed");

    auto result = std::string{};
    for (auto attempt = 0; attempt < 100; ++attempt) {
        result = evaluate(
            engine,
            "({ events: globalThis.__inputFrameEvents, moves: globalThis.__inputMoveCount })",
            "native-input-frame-order-result.js");
        if (result == R"({"events":[700.5],"moves":1})") {
            webscene_engine_destroy(engine);
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    fail("pointer RAF missed its following rendering opportunity: " + result);
}

void test_resize_and_frame_form_one_rendering_opportunity()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "resize/frame pairing engine creation failed");
    resize(engine, 320, 200, 1U);
    execute(engine, R"JS(
        globalThis.__resizeFrameEvents = [];
        addEventListener('resize', () => {
          __resizeFrameEvents.push(['resize', innerWidth, innerHeight]);
          requestAnimationFrame(timestamp => {
            __resizeFrameEvents.push(['frame', timestamp, innerWidth, innerHeight]);
            requestAnimationFrame(nextTimestamp => {
              __resizeFrameEvents.push(['nested', nextTimestamp]);
            });
          });
        }, { once: true });
    )JS", "native-resize-frame-pair-setup.js");
    require(
        evaluate(
            engine,
            "globalThis.__resizeFrameEvents",
            "native-resize-frame-pair-ready.js") == "[]",
        "resize/frame pairing setup did not complete");

    webscene_engine_metrics before{};
    webscene_engine_get_metrics(engine, &before);
    webscene_resize_frame_metrics resize_frame_before{
        sizeof(webscene_resize_frame_metrics)};
    require(
        webscene_engine_get_resize_frame_metrics(
            engine,
            &resize_frame_before) != 0,
        "resize/frame metrics ABI was rejected");
    const webscene_input_event resize_event{
        WEBSCENE_INPUT_RESIZE,
        0U,
        11U,
        640,
        360,
        1,
        0};
    const webscene_input_event frame_event{
        WEBSCENE_INPUT_FRAME,
        0U,
        0U,
        777.25,
        0,
        0,
        0};
    require(
        webscene_engine_enqueue_resize_frame(
            engine,
            &resize_event,
            &frame_event) != 0,
        "paired resize/frame submission was rejected");
    wait_for_consumed_inputs(
        engine,
        before.consumed_inputs + 2U,
        "paired resize/frame inputs were not consumed");

    auto result = std::string{};
    for (auto attempt = 0; attempt < 100; ++attempt) {
        result = evaluate(
            engine,
            "globalThis.__resizeFrameEvents",
            "native-resize-frame-pair-result.js");
        if (result
            == R"([["resize",640,360],["frame",777.25,640,360]])") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (result != R"([["resize",640,360],["frame",777.25,640,360]])") {
        fail("paired resize did not release RAF after resize observers: " + result);
    }
    const auto consumed_after_pair = consumed_input_count(engine);
    animation_frame(engine, 800.5, 0U);
    wait_for_consumed_inputs(
        engine,
        consumed_after_pair + 1U,
        "nested resize RAF host frame was not consumed");
    for (auto attempt = 0; attempt < 100; ++attempt) {
        result = evaluate(
            engine,
            "globalThis.__resizeFrameEvents",
            "native-resize-frame-nested-result.js");
        if (result
            == R"([["resize",640,360],["frame",777.25,640,360],["nested",800.5]])") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (result
        != R"([["resize",640,360],["frame",777.25,640,360],["nested",800.5]])") {
        fail("nested resize RAF did not wait for the next host frame: " + result);
    }

    webscene_engine_metrics after{};
    webscene_engine_get_metrics(engine, &after);
    require(
        after.applied_resize_inputs == before.applied_resize_inputs + 1U
            && after.applied_animation_frames
                == before.applied_animation_frames + 2U
            && after.coalesced_resize_inputs == before.coalesced_resize_inputs
            && after.coalesced_animation_frames
                == before.coalesced_animation_frames,
        "paired resize/frame accounting was inconsistent");
    webscene_resize_frame_metrics resize_frame_after{
        sizeof(webscene_resize_frame_metrics)};
    require(
        webscene_engine_get_resize_frame_metrics(
            engine,
            &resize_frame_after) != 0
            && resize_frame_after.submitted_pairs
                == resize_frame_before.submitted_pairs + 1U
            && resize_frame_after.applied_pairs
                == resize_frame_before.applied_pairs + 1U
            && resize_frame_after.published_pairs
                == resize_frame_before.published_pairs + 1U
            && resize_frame_after.total_queue_nanoseconds
                >= resize_frame_before.total_queue_nanoseconds
                    + resize_frame_after.last_queue_nanoseconds
            && resize_frame_after.total_dispatch_nanoseconds
                >= resize_frame_before.total_dispatch_nanoseconds
                    + resize_frame_after.last_dispatch_nanoseconds
            && resize_frame_after.animation_frame_callbacks
                >= resize_frame_before.animation_frame_callbacks + 1U
            && resize_frame_after.total_animation_frame_batch_nanoseconds
                >= resize_frame_before.total_animation_frame_batch_nanoseconds
                    + resize_frame_after.last_animation_frame_batch_nanoseconds
            && resize_frame_after.maximum_animation_frame_batch_nanoseconds
                >= resize_frame_after.last_animation_frame_batch_nanoseconds
            && resize_frame_after.total_to_publication_nanoseconds
                >= resize_frame_before.total_to_publication_nanoseconds
                    + resize_frame_after.last_to_publication_nanoseconds
            && resize_frame_after.maximum_to_publication_nanoseconds
                >= resize_frame_after.last_to_publication_nanoseconds,
        "resize/frame stage timing was not attributed monotonically");

    const webscene_input_event invalid_frame{
        WEBSCENE_INPUT_POINTER_MOVE,
        0U,
        12U,
        0,
        0,
        0,
        0};
    require(
        webscene_engine_enqueue_resize_frame(
            engine,
            &resize_event,
            &invalid_frame) == 0,
        "paired resize/frame API accepted a non-frame record");
    webscene_engine_destroy(engine);
}
