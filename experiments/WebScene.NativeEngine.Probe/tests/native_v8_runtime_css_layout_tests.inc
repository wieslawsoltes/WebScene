void test_responsive_positioned_sizing(webscene_engine* engine)
{
    resize(engine, 1280, 720, 1);
    execute(engine, R"JS(
        (() => {
          const style = document.createElement('style');
          style.textContent = `
            .dialog { position:fixed; width:100%; min-width:380px; max-width:550px; }
            @media (max-width:379px) { .dialog { min-width:100%; } }
          `;
          document.body.appendChild(style);
          const dialog = document.createElement('div');
          dialog.className = 'dialog';
          document.body.appendChild(dialog);
          globalThis.__testDialog = dialog;
        })()
    )JS", "native-responsive-positioned-setup.js");

    auto desktop = evaluate(engine, R"JS(
        (() => ({
          width: __testDialog.getBoundingClientRect().width,
          minWidth: getComputedStyle(__testDialog).getPropertyValue('min-width'),
          narrow: matchMedia('(max-width:379px)').matches,
          wide: matchMedia('(min-width:1020px)').matches
        }))()
    )JS", "native-responsive-positioned-desktop.js");
    require(desktop == R"({"width":550,"minWidth":"380px","narrow":false,"wide":true})",
        "desktop media rules or fixed min/max sizing regressed: " + desktop);

    resize(engine, 320, 720, 2);
    auto narrow = evaluate(engine, R"JS(
        (() => ({
          width: __testDialog.getBoundingClientRect().width,
          minWidth: getComputedStyle(__testDialog).getPropertyValue('min-width'),
          narrow: matchMedia('(max-width:379px)').matches,
          wide: matchMedia('(min-width:1020px)').matches
        }))()
    )JS", "native-responsive-positioned-narrow.js");
    if (narrow != R"({"width":320,"minWidth":"100%","narrow":true,"wide":false})") {
        fail("responsive rules did not recascade after resize: " + narrow);
    }
}

void test_responsive_unset_restores_auto_inset(webscene_engine* engine)
{
    resize(engine, 500, 687, 3);
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              .snackbar {
                position: fixed;
                display: flex;
                inset-inline-start: 12px;
                inset-inline-end: 12px;
                justify-content: center;
                top: 12px;
              }
              @media (min-width:568px) {
                .snackbar { bottom: 12px; top: unset; }
              }
              .wrapper { display: contents; }
              .box {
                display: flex;
                min-height: 40px;
                width: fit-content;
              }
            </style>
            <div class="snackbar">
              <div class="wrapper"><div class="box">Saved</div></div>
            </div>`;
          globalThis.__testSnackbar =
            document.querySelector('.snackbar');
          globalThis.__testSnackbarBox =
            document.querySelector('.box');
        })()
    )JS", "native-responsive-unset-inset-setup.js");

    const auto compact = evaluate(engine, R"JS(
        (() => {
          const layer = __testSnackbar.getBoundingClientRect();
          const box = __testSnackbarBox.getBoundingClientRect();
          return [layer.y, layer.height, box.y, box.height];
        })()
    )JS", "native-responsive-unset-inset-compact.js");
    require(
        compact == "[12,40,12,40]",
        "the compact snackbar did not retain its top inset: " + compact);

    resize(engine, 694, 687, 4);
    const auto wide = evaluate(engine, R"JS(
        (() => {
          const layer = __testSnackbar.getBoundingClientRect();
          const box = __testSnackbarBox.getBoundingClientRect();
          return [layer.y, layer.height, box.y, box.height];
        })()
    )JS", "native-responsive-unset-inset-wide.js");
    require(
        wide == "[635,40,635,40]",
        "top:unset did not restore the auto inset for a bottom snackbar: " + wide);
}

void test_preferred_color_scheme_updates_css_and_match_media(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `
            #color-scheme-probe { --resolved-scheme: light; }
            @media (prefers-color-scheme: dark) {
              #color-scheme-probe { --resolved-scheme: dark; }
            }
          `;
          document.body.appendChild(style);
          const probe = document.createElement('div');
          probe.id = 'color-scheme-probe';
          document.body.appendChild(probe);
          globalThis.__colorSchemeProbe = probe;
        })()
    )JS", "native-preferred-color-scheme-setup.js");

    const auto read_state = [&] {
        return evaluate(engine, R"JS(
            ({
              dark: matchMedia('(prefers-color-scheme: dark)').matches,
              light: matchMedia('(prefers-color-scheme: light)').matches,
              css: getComputedStyle(__colorSchemeProbe)
                .getPropertyValue('--resolved-scheme').trim()
            })
        )JS", "native-preferred-color-scheme-state.js");
    };

    require(
        read_state() == R"({"dark":false,"light":true,"css":"light"})",
        "native media environment did not default to light");
    require(
        webscene_engine_set_preferred_color_scheme(engine, 2U) == 0U,
        "native engine accepted an unknown preferred color scheme");
    require(
        webscene_engine_set_preferred_color_scheme(
            engine,
            WEBSCENE_PREFERRED_COLOR_SCHEME_DARK) != 0U,
        "native engine rejected the dark preferred color scheme");
    const auto dark = read_state();
    require(
        dark == R"({"dark":true,"light":false,"css":"dark"})",
        "dark host preference did not update CSS and matchMedia: " + dark);

    require(
        webscene_engine_set_preferred_color_scheme(
            engine,
            WEBSCENE_PREFERRED_COLOR_SCHEME_LIGHT) != 0U,
        "native engine rejected the light preferred color scheme");
    const auto light = read_state();
    require(
        light == R"({"dark":false,"light":true,"css":"light"})",
        "light host preference did not restore CSS and matchMedia: " + light);
}

void test_resize_listener_receives_window_event(webscene_engine* engine)
{
    execute(engine, R"JS(
        window.addEventListener('resize', event => {
          globalThis.__resizeEventContract = {
            type: event.type,
            target: event.target === window,
            currentTarget: event.currentTarget === window,
            srcElement: event.srcElement === window,
            view: event.view === window,
            bubbles: event.bubbles,
            cancelable: event.cancelable,
            hasPreventDefault: typeof event.preventDefault === 'function'
          };
        });
    )JS", "native-resize-event-setup.js");
    require(
        evaluate(engine, "true", "native-resize-event-setup-barrier.js") == "true",
        "resize Event listener setup did not complete");
    resize(engine, 640, 480, 4);
    auto consumed = false;
    for (auto attempt = 0; attempt < 100 && !consumed; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            consumed = scene->header.consumed_input_sequence >= 4U;
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (!consumed) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(consumed, "resize Event input was not consumed");
    const auto result = evaluate(
        engine,
        "globalThis.__resizeEventContract",
        "native-resize-event-result.js");
    require(
        result == R"({"type":"resize","target":true,"currentTarget":true,"srcElement":true,"view":true,"bubbles":false,"cancelable":false,"hasPreventDefault":true})",
        "window resize listener did not receive a browser-shaped Event: " + result);
}

void test_screen_tracks_viewport()
{
    auto* engine = webscene_engine_create(16);
    require(engine != nullptr, "screen regression engine creation failed");
    resize(engine, 1024, 640, 5);
    const auto initial = evaluate(engine, R"JS(
        (() => ({
          stable: screen === window.screen,
          constructor: typeof Screen === 'function' && screen instanceof Screen,
          width: screen.width,
          height: screen.height,
          availWidth: screen.availWidth,
          availHeight: screen.availHeight,
          colorDepth: screen.colorDepth,
          pixelDepth: screen.pixelDepth
        }))()
    )JS", "native-screen-initial.js");
    require(
        initial == R"({"stable":true,"constructor":true,"width":1024,"height":640,"availWidth":1024,"availHeight":640,"colorDepth":24,"pixelDepth":24})",
        "Screen did not expose stable viewport-backed dimensions: " + initial);

    resize(engine, 800, 600, 6);
    const auto resized = evaluate(engine, R"JS(
        ({
          sameObject: screen === window.screen,
          width: screen.width,
          height: screen.height,
          innerWidth,
          innerHeight
        })
    )JS", "native-screen-resized.js");
    require(
        resized == R"({"sameObject":true,"width":800,"height":600,"innerWidth":800,"innerHeight":600})",
        "Screen dimensions did not follow the host viewport: " + resized);
    webscene_engine_destroy(engine);
}

void test_absolute_portal_centers_against_positioned_ancestor(webscene_engine* engine)
{
    resize(engine, 1280, 720, 3);
    const auto result = evaluate(engine, R"JS(
        (() => {
          const style = document.createElement('style');
          style.textContent = `
            .hint-host {
              position:relative; margin-left:120px; width:1000px; height:600px;
            }
            .hint-portal { display:inline-block; width:0; height:0; }
            .hint-rail {
              position:absolute; inset-inline-start:10px; inset-inline-end:10px;
              display:flex; justify-content:center;
            }
            .hint-center { top:240px; }
            .hint-bottom { bottom:24px; }
            .hint-coachmark { width:300px; height:72px; }
            .hint-toast { width:420px; height:48px; }
          `;
          document.body.appendChild(style);
          const host = document.createElement('div');
          host.className = 'hint-host';
          const appendHint = (railClass, contentClass) => {
            const portal = document.createElement('span');
            portal.className = 'hint-portal';
            const rail = document.createElement('div');
            rail.className = `hint-rail ${railClass}`;
            const content = document.createElement('div');
            content.className = contentClass;
            rail.appendChild(content);
            portal.appendChild(rail);
            host.appendChild(portal);
            return { portal, rail, content };
          };
          const coachmark = appendHint('hint-center', 'hint-coachmark');
          const toast = appendHint('hint-bottom', 'hint-toast');
          document.body.appendChild(host);
          const rect = node => node.getBoundingClientRect();
          const hostRect = rect(host);
          const coachRail = rect(coachmark.rail);
          const coachRect = rect(coachmark.content);
          const toastRect = rect(toast.content);
          const hostCenter = hostRect.x + hostRect.width / 2;
          return {
            portalWidth: rect(coachmark.portal).width,
            railX: coachRail.x,
            railWidth: coachRail.width,
            coachCentered: Math.abs(coachRect.x + coachRect.width / 2 - hostCenter) < 0.01,
            toastCentered: Math.abs(toastRect.x + toastRect.width / 2 - hostCenter) < 0.01
          };
        })()
    )JS", "native-absolute-portal-centering.js");
    if (result != R"({"portalWidth":0,"railX":130,"railWidth":980,"coachCentered":true,"toastCentered":true})") {
        fail("absolute portal hints did not center within their positioned ancestor: " + result);
    }
}

void test_document_position(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const parent = document.createElement('div');
          const first = document.createElement('span');
          const second = document.createElement('span');
          parent.appendChild(first);
          parent.appendChild(second);
          document.body.appendChild(parent);
          return [
            parent.compareDocumentPosition(first),
            first.compareDocumentPosition(parent),
            first.compareDocumentPosition(second),
            second.compareDocumentPosition(first),
            first.compareDocumentPosition(first)
          ];
        })()
    )JS", "native-compare-document-position.js");
    require(result == "[20,10,4,2,0]", "compareDocumentPosition bit flags regressed");
}

void test_attribute_selector_invalidation(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const style = document.createElement('style');
          style.textContent = `
            .attribute-host[data-mode="wide"] .attribute-child { width: 120px; }
            .attribute-host[aria-expanded="true"] .attribute-child { height: 33px; }
          `;
          document.body.appendChild(style);
          const host = document.createElement('div');
          host.className = 'attribute-host';
          const child = document.createElement('div');
          child.className = 'attribute-child';
          host.appendChild(child);
          document.body.appendChild(host);
          host.dataset.mode = 'wide';
          host.setAttribute('aria-expanded', 'true');
          const computed = getComputedStyle(child);
          return [
            computed.getPropertyValue('width'),
            computed.getPropertyValue('height')
          ];
        })()
    )JS", "native-attribute-selector-invalidation.js");
    require(
        result == R"(["120px","33px"])",
        "attribute-dependent descendant styles were not invalidated");
}

void test_attribute_selector_list_requires_authored_attribute(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const root = document.createElement('div');
          root.innerHTML = `
            <x-td id="plain" style="width: 50px"></x-td>
            <x-td id="spanning" rowspan="2"></x-td>`;
          document.body.appendChild(root);
          return {
            first: root.querySelector('x-td[rowspan], x-td[colspan]')?.id,
            count: root.querySelectorAll('x-td[rowspan], x-td[colspan]').length
          };
        })()
    )JS", "native-attribute-selector-list.js");
    require(
        result == R"({"first":"spanning","count":1})",
        "selector-list attribute existence matching regressed: " + result);
}

void test_script_raw_text_does_not_create_style_descendants(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <div id="raw-text-target" style="display:block;width:40px;height:10px"></div>
            <script id="data-block" type="application/json">{"fragment":"<style>#raw-text-target{display:none}</style>"}</script>`;
          const dataBlock = document.getElementById('data-block');
          const target = document.getElementById('raw-text-target');
          return {
            tag: dataBlock?.tagName,
            type: dataBlock?.type,
            text: dataBlock?.textContent,
            nestedStyles: dataBlock?.querySelectorAll('style').length,
            targetDisplay: getComputedStyle(target).display,
            targetWidth: target.offsetWidth,
            targetHeight: target.offsetHeight
          };
        })()
    )JS", "native-script-raw-text.js");
    require(
        result == R"({"tag":"SCRIPT","type":"application/json","text":"{\"fragment\":\"<style>#raw-text-target{display:none}</style>\"}","nestedStyles":0,"targetDisplay":"block","targetWidth":40,"targetHeight":10})",
        "SCRIPT raw text was reparsed as descendant markup: " + result);
}

void test_attribute_selector_operators(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const target = document.createElement('div');
          target.id = 'attribute-operators';
          target.setAttribute('data-value', 'prefix middle suffix');
          target.setAttribute('lang', 'en-US');
          document.body.appendChild(target);
          return [
            '[data-value^="prefix"]',
            '[data-value$="suffix"]',
            '[data-value*="middle"]',
            '[data-value~="middle"]',
            '[lang|="en"]'
          ].map(selector => document.querySelector(selector)?.id ?? null);
        })()
    )JS", "native-attribute-selector-operators.js");
    require(
        result == R"(["attribute-operators","attribute-operators","attribute-operators","attribute-operators","attribute-operators"])",
        "attribute selector operators regressed: " + result);
}

void test_replace_child_advances_attribute_selector_iteration(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const root = document.createElement('div');
          root.innerHTML = `
            <x-td id="row" rowspan="2"></x-td>
            <x-td id="column" colspan="2"></x-td>`;
          document.body.appendChild(root);
          const replaced = [];
          for (let count = 0; count < 5; count++) {
            const previous = root.querySelector('x-td[rowspan], x-td[colspan]');
            if (!previous) break;
            replaced.push(previous.id);
            const replacement = document.createElement('td');
            for (let index = previous.attributes.length; index--;) {
              replacement.setAttribute(
                previous.attributes[index].name,
                previous.attributes[index].value);
            }
            previous.parentNode.replaceChild(replacement, previous);
          }
          return {
            replaced,
            remaining: root.querySelectorAll('x-td[rowspan], x-td[colspan]').length,
            childTags: Array.from(root.children).map(child => child.tagName)
          };
        })()
    )JS", "native-replace-child-selector-iteration.js");
    require(
        result == R"({"replaced":["row","column"],"remaining":0,"childTags":["TD","TD"]})",
        "replaceChild did not advance selector iteration: " + result);
}

void test_insert_before_preserves_tree_identity_and_atomicity(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const parent = document.createElement('div');
          const first = document.createElement('span');
          const moved = document.createElement('span');
          const last = document.createElement('span');
          first.id = 'first';
          moved.id = 'moved';
          last.id = 'last';
          parent.appendChild(first);
          parent.appendChild(moved);
          parent.appendChild(last);
          document.body.appendChild(parent);

          const returnedSelf = parent.insertBefore(moved, moved);
          const selfOrder = Array.from(parent.children).map(node => node.id).join(',');
          const selfParent = moved.parentNode === parent;

          const other = document.createElement('div');
          const foreign = document.createElement('span');
          other.appendChild(foreign);
          document.body.appendChild(other);
          let invalidThrew = false;
          try {
            parent.insertBefore(moved, foreign);
          } catch (_error) {
            invalidThrew = true;
          }
          const rejectedOrder = Array.from(parent.children).map(node => node.id).join(',');
          const rejectedParent = moved.parentNode === parent;

          parent.insertBefore(first, last);
          const reordered = Array.from(parent.children).map(node => node.id).join(',');
          const target = document.createElement('div');
          const reference = document.createElement('span');
          reference.id = 'reference';
          target.appendChild(reference);
          document.body.appendChild(target);
          target.insertBefore(first, reference);

          return {
            returnedSelf: returnedSelf === moved,
            selfOrder,
            selfParent,
            invalidThrew,
            rejectedOrder,
            rejectedParent,
            reordered,
            sourceOrder: Array.from(parent.children).map(node => node.id).join(','),
            targetOrder: Array.from(target.children).map(node => node.id).join(','),
            reparented: first.parentNode === target
          };
        })()
    )JS", "native-insert-before-tree-identity.js");
    require(
        result == R"({"returnedSelf":true,"selfOrder":"first,moved,last","selfParent":true,"invalidThrew":true,"rejectedOrder":"first,moved,last","rejectedParent":true,"reordered":"moved,first,last","sourceOrder":"moved,last","targetOrder":"first,reference","reparented":true})",
        "insertBefore tree identity or failed-mutation atomicity regressed: " + result);
}

void test_related_tree_mutations_preserve_identity_and_atomicity(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const parent = document.createElement('div');
          const first = document.createElement('span');
          const node = document.createElement('span');
          const last = document.createElement('span');
          const foreign = document.createElement('span');
          first.id = 'first';
          node.id = 'node';
          last.id = 'last';
          parent.appendChild(first);
          parent.appendChild(node);
          parent.appendChild(last);
          document.body.appendChild(parent);

          let invalidRemoveThrew = false;
          try {
            parent.removeChild(foreign);
          } catch (_error) {
            invalidRemoveThrew = true;
          }
          const orderAfterRemove = Array.from(parent.children).map(child => child.id).join(',');
          const returnedSelf = parent.replaceChild(node, node);
          const orderAfterReplace = Array.from(parent.children).map(child => child.id).join(',');

          const root = document.createElement('div');
          const child = document.createElement('div');
          const grandchild = document.createElement('div');
          root.appendChild(child);
          child.appendChild(grandchild);
          document.body.appendChild(root);
          let ancestorAppendThrew = false;
          try {
            grandchild.appendChild(root);
          } catch (_error) {
            ancestorAppendThrew = true;
          }
          let ancestorReplaceThrew = false;
          try {
            child.replaceChild(root, grandchild);
          } catch (_error) {
            ancestorReplaceThrew = true;
          }

          return {
            invalidRemoveThrew,
            orderAfterRemove,
            removeAtomic: node.parentNode === parent,
            returnedSelf: returnedSelf === node,
            orderAfterReplace,
            replaceSelfParent: node.parentNode === parent,
            ancestorAppendThrew,
            ancestorReplaceThrew,
            hierarchyAtomic: root.parentNode === document.body
              && child.parentNode === root
              && grandchild.parentNode === child
          };
        })()
    )JS", "native-related-tree-mutation-atomicity.js");
    require(
        result == R"({"invalidRemoveThrew":true,"orderAfterRemove":"first,node,last","removeAtomic":true,"returnedSelf":true,"orderAfterReplace":"first,node,last","replaceSelfParent":true,"ancestorAppendThrew":true,"ancestorReplaceThrew":true,"hierarchyAtomic":true})",
        "related DOM tree-mutation identity or atomicity regressed: " + result);
}

void test_contextual_fragment_exposes_parent_node_members(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const range = document.createRange();
          range.selectNodeContents(document.documentElement);
          const fragment = range.createContextualFragment(
            '<div id="first"></div><span id="last"></span>');
          const first = fragment.firstElementChild;
          const last = fragment.lastElementChild;
          const selected = fragment.querySelector('#last');
          const before = {
            isFragment: fragment instanceof DocumentFragment,
            nodeType: fragment.nodeType,
            childCount: fragment.children.length,
            firstId: first?.id,
            lastId: last?.id,
            selectedIsLast: selected === last,
            directParent: first?.parentNode === fragment
          };
          const removed = fragment.removeChild(first);
          return {
            ...before,
            returnedChild: removed === first,
            removedParent: removed.parentNode === null,
            remainingCount: fragment.children.length,
            remainingFirstId: fragment.firstElementChild?.id
          };
        })()
    )JS", "native-contextual-fragment-parent-node.js");
    require(
        result == R"({"isFragment":true,"nodeType":11,"childCount":2,"firstId":"first","lastId":"last","selectedIsLast":true,"directParent":true,"returnedChild":true,"removedParent":true,"remainingCount":1,"remainingFirstId":"last"})",
        "DocumentFragment ParentNode members or child identity regressed: " + result);
}

void test_custom_element_mutation_reactions_are_pay_for_use(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const originalSubtree = globalThis.__webSceneCustomElementsNotifySubtree;
          const originalAttribute = globalThis.__webSceneCustomElementsNotifyAttribute;
          let subtreeCalls = 0;
          let attributeCalls = 0;
          Object.defineProperty(globalThis, '__webSceneCustomElementsNotifySubtree', {
            value: () => { ++subtreeCalls; }, configurable: true
          });
          Object.defineProperty(globalThis, '__webSceneCustomElementsNotifyAttribute', {
            value: () => { ++attributeCalls; }, configurable: true
          });

          const cold = document.createElement('div');
          cold.setAttribute('data-cold', 'yes');
          document.body.appendChild(cold);
          const beforeDefinition = [subtreeCalls, attributeCalls];

          try { customElements.define('invalid', class extends HTMLElement {}); }
          catch (_) {}
          cold.setAttribute('data-invalid', 'still-cold');
          document.body.appendChild(document.createElement('span'));
          const afterInvalidDefinition = [subtreeCalls, attributeCalls];

          class PayForUseElement extends HTMLElement {}
          customElements.define('native-pay-for-use', PayForUseElement);
          const hot = document.createElement('div');
          hot.setAttribute('data-hot', 'yes');
          document.body.appendChild(hot);
          const afterValidDefinition = [subtreeCalls, attributeCalls];

          Object.defineProperty(globalThis, '__webSceneCustomElementsNotifySubtree', {
            value: originalSubtree, configurable: true
          });
          Object.defineProperty(globalThis, '__webSceneCustomElementsNotifyAttribute', {
            value: originalAttribute, configurable: true
          });
          return { beforeDefinition, afterInvalidDefinition, afterValidDefinition };
        })()
    )JS", "native-custom-elements-pay-for-use.js");
    require(
        result == R"({"beforeDefinition":[0,0],"afterInvalidDefinition":[0,0],"afterValidDefinition":[1,1]})",
        "custom-element mutation reactions were not gated by the first valid definition: "
            + result);
}

void test_autonomous_custom_element_lifecycle(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '<native-lifecycle data-state="ready"></native-lifecycle>';
          const reactions = [];
          class NativeLifecycle extends HTMLElement {
            static observedAttributes = ['data-state'];
            constructor() {
              super();
              reactions.push('construct');
            }
            connectedCallback() { reactions.push('connected'); }
            disconnectedCallback() { reactions.push('disconnected'); }
            attributeChangedCallback(name, oldValue, newValue) {
              reactions.push(`${name}:${oldValue}->${newValue}`);
            }
          }
          customElements.define('native-lifecycle', NativeLifecycle);
          const element = document.querySelector('native-lifecycle');
          element.setAttribute('data-state', 'updated');
          element.remove();
          document.body.appendChild(element);
          const constructed = new NativeLifecycle();
          return {
            reactions,
            upgraded: element instanceof NativeLifecycle,
            constructed: constructed instanceof NativeLifecycle
              && constructed.localName === 'native-lifecycle',
            registry: customElements.get('native-lifecycle') === NativeLifecycle
              && customElements.getName(NativeLifecycle) === 'native-lifecycle'
          };
        })()
    )JS", "native-custom-elements-lifecycle.js");
    require(
        result == R"({"reactions":["construct","data-state:null->ready","connected","data-state:ready->updated","disconnected","connected","construct"],"upgraded":true,"constructed":true,"registry":true})",
        "autonomous custom-element construction or lifecycle regressed: " + result);
}

void test_monaco_browser_primitives(webscene_engine* engine)
{
    execute(
        engine,
        "globalThis.__editorMicrotasks = ['sync']; "
        "queueMicrotask(() => __editorMicrotasks.push('microtask'));",
        "native-editor-queue-microtask.js");
    const auto result = evaluate(engine, R"JS(
        (() => {
          class WebSceneEditorElement extends HTMLElement {}
          customElements.define('webscene-editor-element', WebSceneEditorElement);
          const utf8 = new TextDecoder().decode(
            new Uint8Array([0x48, 0x69, 0x20, 0xf0, 0x9f, 0x91, 0x8b]));
          const utf16 = new TextDecoder('utf-16le').decode(
            new Uint16Array([0x3c, 0x64, 0x69, 0x76, 0x3e]));
          const encoded = Array.from(new TextEncoder().encode('Aé'));
          const destination = new Uint8Array(8);
          const into = new TextEncoder().encodeInto('Aé', destination);
          const elementNode = document.createElement('span');
          const textNode = document.createTextNode('token');
          const caretHost = document.createElement('div');
          caretHost.style.position = 'absolute';
          caretHost.style.left = '0px';
          caretHost.style.top = '0px';
          caretHost.style.fontFamily = 'monospace';
          caretHost.style.fontSize = '14px';
          const caretToken = document.createElement('span');
          caretToken.appendChild(document.createTextNode('function'));
          caretHost.appendChild(caretToken);
          document.body.appendChild(caretHost);
          const caretRect = caretToken.getBoundingClientRect();
          const caretRange = document.caretRangeFromPoint(
            caretRect.left + caretRect.width * 0.4,
            caretRect.top + caretRect.height * 0.5);
          const measurement = document.createElement('span');
          measurement.style.fontFamily = 'monospace';
          measurement.style.fontSize = '14px';
          measurement.textContent = '0'.repeat(256);
          document.body.appendChild(measurement);
          const digitWidth = measurement.offsetWidth / 256;
          measurement.remove();
          return {
            microtasks: __editorMicrotasks,
            customElement: customElements.get('webscene-editor-element')
              === WebSceneEditorElement,
            utf8,
            utf16,
            encoded,
            into: [into.read, into.written, ...destination.slice(0, into.written)],
            observers: [
              typeof ResizeObserver,
              typeof MutationObserver,
              typeof IntersectionObserver
            ],
            events: [
              typeof KeyboardEvent,
              typeof InputEvent,
              typeof UIEvent
            ],
            nodeConstants: [
              elementNode.nodeType,
              elementNode.ELEMENT_NODE,
              textNode.nodeType,
              textNode.TEXT_NODE,
              elementNode.DOCUMENT_NODE,
              elementNode.DOCUMENT_FRAGMENT_NODE
            ],
            windowOffsets: [
              window.scrollX,
              window.scrollY,
              window.pageXOffset,
              window.pageYOffset
            ],
            caretHit: [
              typeof document.caretRangeFromPoint,
              caretRange.startContainer.nodeType,
              caretRange.startContainer.TEXT_NODE,
              caretRange.startContainer.parentNode === caretToken,
              caretRange.startOffset > 0
                && caretRange.startOffset < caretRange.startContainer.textContent.length
            ],
            inlineFontMetric: digitWidth > 5 && digitWidth < 20
          };
        })()
    )JS", "native-editor-browser-primitives.js");
    const auto expected =
        R"({"microtasks":["sync","microtask"],"customElement":true,"utf8":"Hi 👋","utf16":"<div>","encoded":[65,195,169],"into":[2,3,65,195,169],"observers":["function","function","function"],"events":["function","function","function"],"nodeConstants":[1,1,3,3,9,11],"windowOffsets":[0,0,0,0],"caretHit":["function",3,3,true,true],"inlineFontMetric":true})";
    require(
        result == expected,
        "Monaco browser primitives are incomplete: " + result);
}

void test_monaco_view_line_dom_mutations(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const layer = document.createElement('div');
          layer.className = 'view-lines';
          document.body.appendChild(layer);
          layer.innerHTML =
            '<div class="view-line" style="top:0px;height:19px;">'
              + '<span><span class="mtk6">const</span><span class="mtk1"> answer = </span>'
              + '<span class="mtk4">42</span><span class="mtk1">;</span></span></div>'
            + '<div class="view-line" style="top:19px;height:19px;">'
              + '<span><span class="mtk8">// native WebScene</span></span></div>'
            + '<div class="view-line" style="top:38px;height:19px;">'
              + '<span><span class="mtk6">function</span><span class="mtk1"> greet() {</span></span></div>';

          const initial = Array.from(layer.childNodes);
          const backward = [];
          for (let node = layer.lastChild; node; node = node.previousSibling) {
            backward.push(node.textContent);
          }
          layer.lastChild.insertAdjacentHTML(
            'afterend',
            '<div class="view-line" style="top:57px;height:19px;">'
              + '<span><span class="mtk1">}</span></span></div>');
          const replacement = document.createElement('div');
          replacement.className = 'view-line';
          replacement.innerHTML =
            '<span><span class="mtk8">// editable model</span></span>';
          layer.children[1].replaceWith(replacement);

          return {
            initialCount: initial.length,
            backward,
            finalCount: layer.childNodes.length,
            finalText: Array.from(layer.childNodes, node => node.textContent),
            siblingChain:
              layer.lastChild.previousSibling.previousSibling.previousSibling
                === layer.firstChild,
            replacementParent: replacement.parentNode === layer,
            oldDetached: initial[1].parentNode === null,
            tokenCount: layer.querySelectorAll('span').length
          };
        })()
    )JS", "native-monaco-view-line-dom.js");
    const auto expected =
        R"({"initialCount":3,"backward":["function greet() {","// native WebScene","const answer = 42;"],"finalCount":4,"finalText":["const answer = 42;","// editable model","function greet() {","}"],"siblingChain":true,"replacementParent":true,"oldDetached":true,"tokenCount":12})";
    require(
        result == expected,
        "Monaco view-line DOM mutation semantics regressed: " + result);
}

void test_visibility_inherits_for_computed_style_and_focus(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const host = document.createElement('div');
          host.style.visibility = 'hidden';
          const inherited = document.createElement('button');
          inherited.id = 'inherited-hidden';
          const restored = document.createElement('button');
          restored.id = 'restored-visible';
          restored.style.visibility = 'visible';
          host.appendChild(inherited);
          host.appendChild(restored);
          document.body.appendChild(host);
          inherited.focus();
          const hiddenFocus = document.activeElement?.id || '';
          restored.focus();
          return {
            host: getComputedStyle(host).visibility,
            inherited: getComputedStyle(inherited).visibility,
            restored: getComputedStyle(restored).visibility,
            hiddenFocus,
            restoredFocus: document.activeElement?.id || ''
          };
        })()
    )JS", "native-inherited-visibility-focus.js");
    require(
        result == R"({"host":"hidden","inherited":"hidden","restored":"visible","hiddenFocus":"","restoredFocus":"restored-visible"})",
        "computed visibility or hidden-element focusability regressed: " + result);
}

void test_hover_specificity_preserves_visible_theme_icon(webscene_engine* engine)
{
    execute(engine, R"JS(
        document.body.innerHTML = `
          <style>
            @media (any-hover: hover) {
              .control:hover .arrow { opacity: 1; }
            }
            .arrow { color: var(--hover-icon); opacity: 0; }
            [data-theme="dark"] { --hover-icon: #c0cacc; }
            .control { width: 60px; height: 60px; }
            .button { width: 40px; height: 40px; }
            .arrow { width: 10px; height: 40px; }
          </style>
          <div class="control" data-theme="dark">
            <button class="button">Cursor</button><button class="arrow">‹</button>
          </div>`;
    )JS", "native-hover-specificity-setup.js");
    resize(engine, 320, 200, 101U);
    const auto pointer_x = std::stod(evaluate(
        engine,
        "document.querySelector('.button').getBoundingClientRect().x + 5",
        "native-hover-specificity-x.js"));
    const auto pointer_y = std::stod(evaluate(
        engine,
        "document.querySelector('.button').getBoundingClientRect().y + 5",
        "native-hover-specificity-y.js"));
    pointer_move(engine, pointer_x, pointer_y, 102U);
    const auto result = evaluate(engine, R"JS(
        (() => {
          const arrow = document.querySelector('.arrow');
          const computed = getComputedStyle(arrow);
          return [
            computed.opacity,
            computed.color,
            arrow.matches('.control:hover .arrow')
          ];
        })()
    )JS", "native-hover-specificity-result.js");
    require(
        result == R"JSON(["1","rgb(192, 202, 204)",true])JSON",
        "a later low-specificity rule hid or recolored the hovered child: " + result);
}

void test_complex_is_specificity_ignores_non_element_siblings(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              .b.c + .d + .q.r + .s + #target {
                font-size: 10px;
                height: 10px;
                width: 10px;
              }
              :is(.a, .b.c + .d, .q) + :is(* + .p, .q.r + .s, * + .t) + #target {
                height: 20px;
                width: 20px;
              }
              .b.c + .d + .q.r + .s + #target { width: 30px; }
            </style>
            <div class="b c"></div>
            <div class="a d e"></div>
            <div class="q r"></div>
            <div class="p s t"></div>
            <div id="target"></div>`;
          const computed = getComputedStyle(document.getElementById('target'));
          return [computed.width, computed.height, computed.fontSize];
        })()
    )JS", "native-complex-is-specificity.js");
    require(
        result == R"JSON(["30px","20px","10px"])JSON",
        ":is() specificity or element-sibling matching regressed: " + result);
}

void test_inline_relative_line_height_uses_cascaded_font_size(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>div { font-size: 16px; }</style>
            <div style="line-height: normal"></div>
            <div style="line-height: 1"></div>
            <div style="line-height: 10px"></div>
            <div style="line-height: 10%"></div>`;
          const elements = Array.from(document.querySelectorAll('div'));
          const initial = elements.map(element => getComputedStyle(element).lineHeight);
          elements[1].style.fontSize = '20px';
          return { initial, updated: getComputedStyle(elements[1]).lineHeight };
        })()
    )JS", "native-inline-relative-line-height.js");
    require(
        result == R"JSON({"initial":["normal","16px","10px","1.6px"],"updated":"20px"})JSON",
        "relative line-height did not follow the winning font-size: " + result);
}

void test_empty_inline_element_does_not_stretch_cross_size(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <div id="fixture" style="position:absolute;top:-10000px;left:-10000px;width:10000px;height:10000px">
              <a id="empty" href="#" title="empty"></a>
            </div>`;
          const fixture = document.getElementById('fixture').getBoundingClientRect();
          const empty = document.getElementById('empty').getBoundingClientRect();
          return {
            fixture: [fixture.x, fixture.y, fixture.width, fixture.height],
            empty: [empty.x, empty.y, empty.width, empty.height]
          };
        })()
    )JS", "native-empty-inline-element-geometry.js");
    require(
        result == R"JSON({"fixture":[-10000,-10000,10000,10000],"empty":[-10000,-10000,0,0]})JSON",
        "an empty inline element stretched to its containing block: " + result);
}

void test_hover_invalidation_updates_functional_and_sibling_subjects(
    webscene_engine* engine)
{
    execute(engine, R"JS(
        document.body.innerHTML = `
          <style>
            .trigger { width: 40px; height: 40px; }
            .trigger:not(:hover) + .sibling { width: 20px; height: 20px; }
            .trigger:hover + .sibling { width: 40px; height: 20px; }
          </style>
          <button class="trigger">Open</button><div class="sibling"></div>`;
    )JS", "native-hover-sibling-invalidation-setup.js");
    resize(engine, 320, 200, 103U);
    const auto pointer_x = std::stod(evaluate(
        engine,
        "document.querySelector('.trigger').getBoundingClientRect().x + 5",
        "native-hover-sibling-invalidation-x.js"));
    const auto pointer_y = std::stod(evaluate(
        engine,
        "document.querySelector('.trigger').getBoundingClientRect().y + 5",
        "native-hover-sibling-invalidation-y.js"));

    pointer_move(engine, pointer_x, pointer_y, 104U);
    const auto hovered = evaluate(
        engine,
        "getComputedStyle(document.querySelector('.sibling')).width",
        "native-hover-sibling-invalidation-hovered.js");
    pointer_move(engine, 250, 150, 105U);
    const auto unhovered = evaluate(
        engine,
        "getComputedStyle(document.querySelector('.sibling')).width",
        "native-hover-sibling-invalidation-unhovered.js");

    require(
        hovered == R"("40px")" && unhovered == R"("20px")",
        "functional or sibling hover invalidation regressed: hovered="
            + hovered + ", unhovered=" + unhovered);
}

void test_hover_moves_between_block_and_display_contents_child(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              .affected:hover { color: green; }
              #hoveredContents { display: contents; }
              #hovered, #hovered2 { width: 120px; height: 30px; }
            </style>
            <div id="hovered">first</div>
            <div id="hoveredContents">
              <div id="hovered2">second</div>
            </div>`;
          hovered.className = 'affected';
          hoveredContents.className = 'affected';
          globalThis.__hoverEvents = [];
          hovered.addEventListener('mouseover', event => __hoverEvents.push(event.target.id));
          hovered2.addEventListener('mouseover', event => __hoverEvents.push(event.target.id));
        })()
    )JS", "native-display-contents-hover-setup.js");
    resize(engine, 320, 200, 106U);
    const auto first_x = std::stod(evaluate(
        engine,
        "hovered.getBoundingClientRect().x + 5",
        "native-display-contents-hover-first-x.js"));
    const auto first_y = std::stod(evaluate(
        engine,
        "hovered.getBoundingClientRect().y + 5",
        "native-display-contents-hover-first-y.js"));
    const auto second_x = std::stod(evaluate(
        engine,
        "hovered2.getBoundingClientRect().x + 5",
        "native-display-contents-hover-second-x.js"));
    const auto second_y = std::stod(evaluate(
        engine,
        "hovered2.getBoundingClientRect().y + 5",
        "native-display-contents-hover-second-y.js"));
    pointer_move(engine, first_x, first_y, 107U);
    require(
        evaluate(
            engine,
            "__hoverEvents.length",
            "native-display-contents-hover-first-result.js") == "1",
        "the first hover move was not dispatched before entering display:contents");
    pointer_move(engine, second_x, second_y, 108U);
    const auto result = evaluate(engine, R"JS(
        ({
          events: __hoverEvents,
          first: hovered.getBoundingClientRect(),
          contents: hoveredContents.getBoundingClientRect(),
          second: hovered2.getBoundingClientRect(),
          secondColor: getComputedStyle(hovered2).color
        })
    )JS", "native-display-contents-hover-result.js");
    require(
        result.find(R"JSON("events":["hovered","hovered2"])JSON") != std::string::npos
            && result.find(R"JSON("secondColor":"rgb(0, 128, 0)")JSON") != std::string::npos,
        "hover did not move into a display:contents child: " + result);
}

void test_single_fractional_grid_track_stays_one_column(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const grid = document.createElement('div');
          grid.style.display = 'grid';
          grid.style.width = '344px';
          grid.style.height = '169px';
          const style = document.createElement('style');
          style.textContent = `
            .one-track { grid-template-columns: 1fr; grid-template-rows: 1fr; }
            .transparent-wrapper { display: contents; }
          `;
          document.body.appendChild(style);
          grid.className = 'one-track';
          const content = document.createElement('div');
          const wrapper = document.createElement('div');
          wrapper.className = 'transparent-wrapper';
          const footer = document.createElement('div');
          footer.style.height = '0px';
          wrapper.appendChild(content);
          grid.appendChild(wrapper);
          grid.appendChild(footer);
          document.body.appendChild(grid);
          return [
            grid.getBoundingClientRect().width,
            wrapper.getBoundingClientRect().width,
            wrapper.getBoundingClientRect().height,
            content.getBoundingClientRect().width,
            footer.getBoundingClientRect().width,
            content.getBoundingClientRect().height,
            getComputedStyle(wrapper).display
          ];
        })()
    )JS", "native-single-grid-track.js");
    if (result != R"([344,0,0,344,344,169,"contents"])") {
        fail("a single 1fr grid track was split into columns: " + result);
    }
}

void test_non_rendered_dom_nodes_do_not_create_layout_items(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              x-table { display: table; }
              x-td { display: table-cell; }
              #grid {
                display: grid;
                width: 200px;
                gap: 20px;
                grid-template-columns: 90px 90px;
                grid-template-rows: 90px 90px;
              }
            </style>
            <div id="comment-host"><!--this comment must not render--><div style="height:10px"></div></div>
            <div id="fractional-table" style="display:table">
              <div style="display:table-cell;width:3.6px"></div>
              <div style="display:table-cell;width:3.6px"></div>
            </div>
            <x-table id="anonymous-table"><x-td id="percentage-cell" style="width:50%;height:10px"></x-td> <x-td style="width:50px;height:10px"></x-td></x-table>
            <div id="grid">
              <div></div>
              <div></div>
              <div></div>
              <div></div>
            </div>
            <div id="relative-container" style="width:100px;height:100px">
              <span style="position:relative;left:100%">
                <div id="relative-child" style="width:100px;height:100px;position:relative;left:-100px"></div>
              </span>
            </div>`;
        })()
    )JS", "native-non-rendered-layout-items-setup.js");

    const auto result = evaluate(engine, R"JS(
        (() => {
          const near = (actual, expected) => Math.abs(actual - expected) < 0.01;
          const fractionalTable = document.getElementById('fractional-table').getBoundingClientRect();
          const fractionalCells = document.querySelectorAll('#fractional-table > div');
          const percentageCell = document.getElementById('percentage-cell').getBoundingClientRect();
          const anonymousTable = document.getElementById('anonymous-table').getBoundingClientRect();
          const relativeContainer = document.getElementById('relative-container').getBoundingClientRect();
          const relativeChild = document.getElementById('relative-child').getBoundingClientRect();
          return {
            commentInvisible: near(document.getElementById('comment-host').getBoundingClientRect().height, 10),
            fractionalTable: near(fractionalTable.width, 7.2)
              && near(fractionalCells[0].getBoundingClientRect().width, 3.6)
              && near(fractionalCells[1].getBoundingClientRect().width, 3.6),
            anonymousTable: near(anonymousTable.width, 100) && near(percentageCell.width, 50),
            gridWhitespace: near(document.getElementById('grid').getBoundingClientRect().height, 200),
            leadingInlineWhitespace: near(relativeChild.left, relativeContainer.left)
          };
        })()
    )JS", "native-non-rendered-layout-items-result.js");
    require(
        result == R"({"commentInvisible":true,"fractionalTable":true,"anonymousTable":true,"gridWhitespace":true,"leadingInlineWhitespace":true})",
        "non-rendered DOM nodes unexpectedly participated in layout: " + result);
}

void test_calc_percent_with_pixel_offset(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const parent = document.createElement('div');
          parent.style.width = '243px';
          const child = document.createElement('div');
          const style = document.createElement('style');
          style.textContent = `
            .calc-parent { position: relative; height: 80px; }
            .calc-child {
              position: absolute;
              inset-inline-start: calc(50% - 32px);
              top: calc(50% - 12px);
              width: calc(100% - 64px);
              height: 7px;
            }
          `;
          document.body.appendChild(style);
          parent.className = 'calc-parent';
          child.className = 'calc-child';
          parent.appendChild(child);
          document.body.appendChild(parent);
          const rect = child.getBoundingClientRect();
          return [rect.x, rect.y, rect.width];
        })()
    )JS", "native-calc-percent-pixel-offset.js");
    if (result != "[89.5,28,179]") {
        fail("mixed percentage/pixel calc() did not position and size the absolute child: " + result);
    }
}

void test_calc_viewport_units_with_pixel_offsets_bound_fixed_boxes(
    webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              html, body { margin: 0; }
              #popup {
                position: fixed;
                left: 0;
                top: 0;
                width: 2000px;
                height: 2000px;
                max-width: min(
                  var(--input-max-width, 10000vw),
                  var(--anchor-max-width, 10000vw),
                  var(--screen-max-width, 10000vw),
                  var(--resize-max-width, 10000vw));
                max-height: min(
                  var(--input-max-height, 100000vh),
                  var(--anchor-max-height, 100000vh),
                  var(--screen-max-height, 100000vh),
                  var(--resize-max-height, 100000vh));
              }
            </style>
            <div id="popup" style="
              --screen-max-width: 289px;
              --screen-max-height: 126px;
            "></div>`;
          const rect = document.getElementById('popup').getBoundingClientRect();
          const style = getComputedStyle(document.getElementById('popup'));
          return {
            width: rect.width,
            height: rect.height,
            expectedWidth: innerWidth - 31,
            expectedHeight: innerHeight - 74,
            maxWidth: style.maxWidth,
            maxHeight: style.maxHeight
          };
        })()
    )JS", "native-calc-viewport-pixel-offset.js");
    require(
        result.find("\"width\":289") != std::string::npos
            && result.find("\"height\":126") != std::string::npos,
        "viewport-unit calc() constraints were discarded from a fixed box: " + result);
}

void test_current_color_and_color_mix_reach_element_and_pseudo_paint(
    webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              html, body { margin: 0; }
              .theme { color: rgb(12, 34, 56); }
              .current { width: 20px; height: 20px; background: currentColor; }
              .mixed {
                position: relative;
                width: 20px;
                height: 20px;
                --accent: #00bcd4;
                --accent-wash: color-mix(in srgb, var(--accent), transparent 80%);
              }
              .mixed::after {
                content: "";
                display: block;
                position: absolute;
                inset: 0;
                background: var(--accent-wash);
              }
            </style>
            <div class="theme"><div id="current" class="current"></div></div>
            <div id="mixed" class="mixed"></div>`;
          return {
            current: getComputedStyle(document.getElementById('current')).backgroundColor,
            mixedVariable: getComputedStyle(document.getElementById('mixed'))
              .getPropertyValue('--accent-wash')
          };
        })()
    )JS", "native-currentcolor-color-mix-setup.js");
    require(
        result.find("\"current\":\"rgb(12, 34, 56)\"") != std::string::npos,
        "inherited currentColor did not resolve for an element background: " + result);
    require(
        result.find("color-mix(in srgb, var(--accent), transparent 80%)")
            != std::string::npos,
        "authored color-mix custom property was not retained: " + result);

    webscene_engine_request_scene_checkpoint(engine);
    auto observed_current = false;
    auto observed_mix = false;
    for (auto attempt = 0; attempt < 100 && (!observed_current || !observed_mix); ++attempt) {
        if (const auto* scene = webscene_engine_acquire_latest_scene(engine); scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto rgba = scene->commands[index].rgba;
                observed_current = observed_current || rgba == 0x0C2238FFU;
                observed_mix = observed_mix || rgba == 0x00BCD433U;
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (!observed_current || !observed_mix) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(observed_current, "currentColor element background did not reach native paint");
    require(observed_mix, "color-mix custom property did not reach pseudo-element paint");
}

void test_adjacent_inline_spans_wrap_through_generated_whitespace(
    webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              html, body { margin: 0; }
              p { width: 120px; margin: 0; font: 14px/20px sans-serif; }
              #first::after { content: " "; }
              #second { font-weight: 700; }
            </style>
            <p id="paragraph"><span id="first">one two three</span><span id="second">four five six seven</span></p>`;
          const paragraph = document.getElementById('paragraph').getBoundingClientRect();
          const first = document.getElementById('first').getBoundingClientRect();
          const second = document.getElementById('second').getBoundingClientRect();
          return {
            paragraphHeight: paragraph.height,
            firstWidth: first.width,
            secondWidth: second.width,
            secondY: second.y
          };
        })()
    )JS", "native-inline-generated-whitespace-wrap.js");
    const auto paragraph_height = std::stof(evaluate(engine, R"JS(
        document.getElementById('paragraph').getBoundingClientRect().height
    )JS", "native-inline-generated-whitespace-height.js"));
    const auto second_width = std::stof(evaluate(engine, R"JS(
        document.getElementById('second').getBoundingClientRect().width
    )JS", "native-inline-generated-whitespace-width.js"));
    require(
        paragraph_height >= 40.0 && paragraph_height <= 60.0
            && second_width > 20.0,
        "generated whitespace forced adjacent inline text into an atomic zero-width box: "
            + result);
}

void test_flex_basis_reserves_fixed_track(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `
            .row { display: flex; align-items: center; width: 273px; height: 31px; }
            .label { flex: 0 0 30px; height: 12px; }
            .track { flex: 1 1 0%; }
            .column { display: flex; flex-direction: column; width: 31px; height: 200px; }
            .vertical-basis { flex: 0 0 80px; width: 12px; }
            .contents-row { display: flex; width: 200px; height: 31px; }
            .contents-wrapper { display: contents; }
            .contents-item { flex: 0 0 80px; height: 12px; }
            .symbol .cell.volume { display: flex; }
            .symbol .cell.hidden { display: none; }
          `;
          document.body.appendChild(style);
          const row = document.createElement('div');
          row.className = 'row';
          const label = document.createElement('div');
          label.className = 'label';
          const track = document.createElement('div');
          track.className = 'track';
          row.appendChild(label);
          row.appendChild(track);
          document.body.appendChild(row);
          const column = document.createElement('div');
          column.className = 'column';
          const verticalBasis = document.createElement('div');
          verticalBasis.className = 'vertical-basis';
          column.appendChild(verticalBasis);
          document.body.appendChild(column);
          const symbol = document.createElement('div');
          symbol.className = 'symbol';
          const hiddenVolume = document.createElement('span');
          hiddenVolume.className = 'cell volume hidden';
          symbol.appendChild(hiddenVolume);
          document.body.appendChild(symbol);
          const contentsRow = document.createElement('div');
          contentsRow.className = 'contents-row';
          const contentsWrapper = document.createElement('div');
          contentsWrapper.className = 'contents-wrapper';
          const contentsItem = document.createElement('div');
          contentsItem.className = 'contents-item';
          contentsWrapper.appendChild(contentsItem);
          contentsRow.appendChild(contentsWrapper);
          document.body.appendChild(contentsRow);
          const wrapperRect = contentsWrapper.getBoundingClientRect();
          const contentsRect = contentsItem.getBoundingClientRect();
          return [
            label.getBoundingClientRect().width,
            label.getBoundingClientRect().height,
            track.getBoundingClientRect().width,
            verticalBasis.getBoundingClientRect().width,
            verticalBasis.getBoundingClientRect().height,
            getComputedStyle(hiddenVolume).display,
            wrapperRect.width,
            wrapperRect.height,
            contentsRect.width,
            contentsRect.height,
            getComputedStyle(label).flexGrow,
            getComputedStyle(track).flexGrow
          ];
        })()
    )JS", "native-flex-basis-track.js");
    if (result != "[30,12,243,12,80,\"none\",0,0,80,12,\"0\",\"1\"]") {
        fail("flex-basis did not remain confined to the flex main axis: " + result);
    }
}

void test_absolute_flex_child_uses_container_static_position(
    webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              html, body { margin: 0; }
              #host { position: relative; display: flex; width: 40px; height: 20px; }
              #image { width: 18px; height: 18px; }
              #fallback { position: absolute; width: 18px; height: 18px; opacity: 0; }
            </style>
            <div id="host"><div id="image"></div><div id="fallback"></div></div>`;
          const host = document.getElementById('host').getBoundingClientRect();
          const fallback = document.getElementById('fallback').getBoundingClientRect();
          return [fallback.x - host.x, fallback.y - host.y, fallback.width, fallback.height];
        })()
    )JS", "native-absolute-flex-static-position.js");
    require(
        result == "[0,0,18,18]",
        "an absolute flex child used the cursor after preceding flow items as its static position: "
            + result);
}

void test_flex_flow_shorthand_controls_layout_and_cssom(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `
            .tabs {
              display: flex;
              flex-flow: column nowrap;
              width: 206px;
              height: 200px;
            }
            .tab { width: 206px; height: 40px; flex: 0 0 auto; }
          `;
          document.body.appendChild(style);
          const tabs = document.createElement('div');
          tabs.className = 'tabs';
          const first = document.createElement('button');
          const second = document.createElement('button');
          first.className = second.className = 'tab';
          tabs.appendChild(first);
          tabs.appendChild(second);
          document.body.appendChild(tabs);

          const firstRect = first.getBoundingClientRect();
          const secondRect = second.getBoundingClientRect();
          const computed = getComputedStyle(tabs);
          const stylesheetValues = [
            computed.flexDirection,
            computed.flexWrap,
            computed.flexFlow,
            computed.getPropertyValue('flex-flow'),
            firstRect.x,
            firstRect.y,
            secondRect.x,
            secondRect.y
          ];

          tabs.style.flexFlow = 'row-reverse wrap';
          const inlineValues = [
            tabs.style.flexFlow,
            getComputedStyle(tabs).flexDirection,
            getComputedStyle(tabs).flexWrap
          ];
          tabs.style.removeProperty('flex-flow');
          const removedValues = [
            tabs.style.flexFlow,
            getComputedStyle(tabs).flexDirection,
            getComputedStyle(tabs).flexWrap
          ];
          return [stylesheetValues, inlineValues, removedValues];
        })()
    )JS", "native-flex-flow-shorthand.js");
    const auto expected =
        R"([["column","nowrap","column nowrap","column nowrap",0,0,0,40],["row-reverse wrap","row-reverse","wrap"],["","column","nowrap"]])";
    if (result != expected) {
        fail("flex-flow shorthand diverged from CSS layout or CSSOM: " + result);
    }
}

void test_font_relative_box_lengths_follow_inherited_font_context(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const container = document.createElement('div');
          container.style.position = 'absolute';
          container.style.left = '0';
          container.style.top = '0';
          container.style.width = '400px';
          container.style.height = '200px';
          container.style.fontSize = '4px';
          const bounded = document.createElement('div');
          bounded.style.position = 'absolute';
          bounded.style.left = '50%';
          bounded.style.right = '50%';
          bounded.style.top = '0';
          bounded.style.height = '20px';
          const sized = document.createElement('div');
          sized.style.display = 'flex';
          sized.style.width = '10em';
          sized.style.minHeight = '2em';
          sized.style.padding = '1em';
          sized.style.gap = '3em';
          sized.style.setProperty('flex-basis', '9em');
          const authoredBasisBeforeConnection = sized.style.getPropertyValue('flex-basis');
          container.appendChild(bounded);
          container.appendChild(sized);
          document.body.appendChild(container);

          bounded.style.right = '25em';
          const initialContainer = container.getBoundingClientRect();
          const constrained = bounded.getBoundingClientRect();
          const constrainedValues = [
            parseFloat(getComputedStyle(bounded).right),
            constrained.left - initialContainer.left,
            initialContainer.right - constrained.right,
            constrained.width
          ];

          bounded.style.inset = '1em 2em 3em 4em';
          const initialInsets = ['top', 'right', 'bottom', 'left'].map(
            name => parseFloat(getComputedStyle(bounded).getPropertyValue(name)));
          container.style.fontSize = '8px';
          const mutatedContainer = container.getBoundingClientRect();
          const mutated = bounded.getBoundingClientRect();
          const mutatedInsets = ['top', 'right', 'bottom', 'left'].map(
            name => parseFloat(getComputedStyle(bounded).getPropertyValue(name)));
          const sizedStyle = getComputedStyle(sized);
          return [
            authoredBasisBeforeConnection,
            sized.style.getPropertyValue('flex-basis'),
            ...constrainedValues,
            ...initialInsets,
            ...mutatedInsets,
            mutated.left - mutatedContainer.left,
            mutatedContainer.right - mutated.right,
            parseFloat(sizedStyle.width),
            parseFloat(sizedStyle.minHeight),
            parseFloat(sizedStyle.paddingLeft),
            parseFloat(sizedStyle.rowGap),
            parseFloat(sizedStyle.columnGap),
            parseFloat(sizedStyle.getPropertyValue('flex-basis'))
          ];
        })()
    )JS", "native-font-relative-box-lengths.js");
    if (result != R"(["9em","9em",100,200,100,100,4,8,12,16,8,16,24,32,32,16,80,16,8,24,24,72])") {
        fail("font-relative box lengths diverged between CSSOM and layout: " + result);
    }
}

void test_floats_share_a_bounded_formatting_line(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            .float-host { width:100px; height:12px; }
            .float-item {
              float:left; width:48px; height:10px;
              border:1px solid blue;
            }
            .float-item.right { float:right; }
          `;
          document.body.appendChild(style);
          const host = document.createElement('div');
          host.className = 'float-host';
          const first = document.createElement('div');
          const second = document.createElement('div');
          first.className = second.className = 'float-item';
          host.appendChild(first);
          host.appendChild(second);
          document.body.appendChild(host);
          const rightHost = document.createElement('div');
          rightHost.className = 'float-host';
          const rightFirst = document.createElement('div');
          const rightSecond = document.createElement('div');
          rightFirst.className = rightSecond.className = 'float-item right';
          rightHost.appendChild(rightFirst);
          rightHost.appendChild(rightSecond);
          document.body.appendChild(rightHost);
          const hostRect = host.getBoundingClientRect();
          const firstRect = first.getBoundingClientRect();
          const secondRect = second.getBoundingClientRect();
          const rightHostRect = rightHost.getBoundingClientRect();
          const rightFirstRect = rightFirst.getBoundingClientRect();
          const rightSecondRect = rightSecond.getBoundingClientRect();
          const cssom = document.createElement('div');
          cssom.style.cssFloat = 'right';
          const cssFloat = cssom.style.cssFloat;
          const propertyValue = cssom.style.getPropertyValue('float');
          cssom.style.removeProperty('float');
          return {
            first: [firstRect.x - hostRect.x, firstRect.y - hostRect.y,
              firstRect.width, firstRect.height],
            second: [secondRect.x - hostRect.x, secondRect.y - hostRect.y,
              secondRect.width, secondRect.height],
            right: [rightFirstRect.x - rightHostRect.x,
              rightSecondRect.x - rightHostRect.x],
            computed: getComputedStyle(first).cssFloat,
            cssom: [cssFloat, propertyValue, cssom.style.cssFloat]
          };
        })()
    )JS", "native-left-float-line.js");
    require(
        result == R"({"first":[0,0,50,12],"second":[50,0,50,12],"right":[50,0],"computed":"left","cssom":["right","right",""]})",
        "consecutive floats or float CSSOM did not share the bounded line: " + result);
}

void test_wrapped_flex_resolves_each_line_independently(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            .host { display:flex; flex-wrap:wrap; width:100px; height:12px; }
            .half { width:48px; border:1px solid blue; }
            .huge { width:148px; border:1px solid purple; }
            .margin-left { margin-left:80px; }
            .force-wrap { margin-right:1px; }
            .inflexible { flex:none; }
          `;
          document.body.appendChild(style);
          const makeHost = (...children) => {
            const host = document.createElement('div');
            host.className = 'host';
            for (const child of children) host.appendChild(child);
            document.body.appendChild(host);
            return host;
          };
          const makeItem = (className) => {
            const item = document.createElement('div');
            item.className = className;
            return item;
          };
          const relativeRect = (host, item) => {
            const parent = host.getBoundingClientRect();
            const child = item.getBoundingClientRect();
            return [child.x - parent.x, child.y - parent.y, child.width, child.height];
          };

          const marginItem = makeItem('half margin-left');
          const marginHost = makeHost(marginItem);
          const first = makeItem('half force-wrap');
          const second = makeItem('half');
          const wrapHost = makeHost(first, second);
          const huge = makeItem('huge');
          const hugeHost = makeHost(huge);
          const inflexible = makeItem('huge inflexible');
          const inflexibleHost = makeHost(inflexible);
          return {
            marginShrink: relativeRect(marginHost, marginItem),
            wrapped: [relativeRect(wrapHost, first), relativeRect(wrapHost, second)],
            hugeShrink: relativeRect(hugeHost, huge),
            inflexible: relativeRect(inflexibleHost, inflexible)
          };
        })()
    )JS", "native-wrapped-flex-lines.js");
    require(
        result == R"({"marginShrink":[80,0,20,12],"wrapped":[[0,0,50,6],[0,6,50,6]],"hugeShrink":[0,0,100,12],"inflexible":[0,0,150,12]})",
        "wrapped flex lines did not resolve shrink and cross-size independently: " + result);
}

void test_zero_height_flex_item_grows_and_hit_tests_descendants(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            .host { display:flex; flex-direction:column; width:200px; height:120px; }
            .space { display:flex; flex:1 1 auto; flex-direction:column; height:0; }
            .tree { height:100%; overflow:hidden; }
            .row { display:flex; height:38px; }
            .title { display:inline-block; width:80px; height:18px; margin:10px; }
          `;
          document.body.appendChild(style);
          const host = document.createElement('div');
          host.className = 'host';
          const space = document.createElement('div');
          space.className = 'space';
          const tree = document.createElement('div');
          tree.className = 'tree';
          const row = document.createElement('div');
          row.className = 'row';
          const title = document.createElement('span');
          title.className = 'title';
          title.textContent = 'Trend Line';
          row.appendChild(title);
          tree.appendChild(row);
          space.appendChild(tree);
          host.appendChild(space);
          document.body.appendChild(host);
          const hit = document.elementFromPoint(20, 19);
          return {
            spaceHeight: space.getBoundingClientRect().height,
            treeHeight: tree.getBoundingClientRect().height,
            hitClass: hit?.className || null,
            hitText: hit?.textContent || null
          };
        })()
    )JS", "native-zero-height-flex-hit-test.js");
    if (result != R"({"spaceHeight":120,"treeHeight":120,"hitClass":"title","hitText":"Trend Line"})") {
        fail("a growable zero-height flex item did not fill or expose descendants to hit testing: "
            + result);
    }
}

void test_empty_non_growing_flex_item_collapses_main_axis(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            .column {
              align-items:flex-start;
              display:flex;
              flex-direction:column-reverse;
              height:120px;
              width:200px;
            }
            .scroll {
              display:flex;
              flex-direction:column-reverse;
              max-height:100%;
              pointer-events:auto;
            }
            .inner {
              align-items:flex-start;
              contain:layout;
              display:flex;
              flex-direction:column;
            }
            .row {
              align-items:flex-start;
              display:flex;
              height:40px;
              width:120px;
            }
          `;
          document.body.appendChild(style);

          const column = document.createElement('section');
          column.className = 'column';
          column.style.pointerEvents = 'none';
          const scroll = document.createElement('div');
          scroll.className = 'scroll';
          const inner = document.createElement('div');
          inner.className = 'inner';
          scroll.appendChild(inner);
          column.appendChild(scroll);
          document.body.appendChild(column);

          const row = document.createElement('div');
          row.className = 'row';
          const empty = document.createElement('div');
          row.appendChild(empty);
          document.body.appendChild(row);

          return {
            column: column.getBoundingClientRect().height,
            scroll: [
              scroll.getBoundingClientRect().width,
              scroll.getBoundingClientRect().height
            ],
            inner: [
              inner.getBoundingClientRect().width,
              inner.getBoundingClientRect().height
            ],
            rowItem: [
              empty.getBoundingClientRect().width,
              empty.getBoundingClientRect().height
            ],
            hit: document.elementFromPoint(100, 60)?.tagName || null
          };
        })()
    )JS", "native-empty-flex-item-collapse.js");
    require(
        result == R"({"column":120,"scroll":[0,0],"inner":[0,0],"rowItem":[0,40],"hit":"BODY"})",
        "empty non-growing flex items expanded across the main axis: " + result);
}

void test_empty_bordered_flex_items_keep_intrinsic_cross_size(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              .row { display:flex; height:100px; align-items:stretch; }
              .item { border:5px solid green; width:50px; }
              .start { align-self:flex-start; }
            </style>
            <div id="row" class="row">
              <div id="item" class="item start"></div>
            </div>`;
          const row = document.getElementById('row');
          const item = document.getElementById('item');
          const before = [item.offsetWidth, item.offsetHeight, item.offsetTop];
          row.style.alignItems = 'flex-end';
          item.style.alignSelf = 'auto';
          return {
            before,
            after: [item.offsetWidth, item.offsetHeight, item.offsetTop],
            alignItems: getComputedStyle(row).alignItems,
            alignSelf: getComputedStyle(item).alignSelf
          };
        })()
    )JS", "native-empty-bordered-flex-cross-size.js");
    require(
        result == R"({"before":[60,10,0],"after":[60,10,90],"alignItems":"flex-end","alignSelf":"auto"})",
        "empty bordered flex items lost their intrinsic cross size during dynamic alignment: "
            + result);
}

void test_appending_child_invalidates_empty_selector(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `
            [data-role=status-pill] {
              display:inline-flex;
              min-width:6px;
            }
            [data-role=status-pill]:empty {
              display:none;
            }
            [data-role=status-pill]_hidden {
              display:none;
            }
            .status-item {
              display:flex;
              height:18px;
              width:18px;
            }
          `;
          document.body.appendChild(style);
          const pill = document.createElement('button');
          pill.setAttribute('data-role', 'status-pill');
          document.body.appendChild(pill);
          const before = getComputedStyle(pill).display;
          const item = document.createElement('span');
          item.className = 'status-item';
          pill.appendChild(item);
          const rect = pill.getBoundingClientRect();
          return {
            before,
            after: getComputedStyle(pill).display,
            width: rect.width,
            height: rect.height
          };
        })()
    )JS", "dynamic-empty-selector-invalidation.js");
    if (result != R"({"before":"none","after":"inline-flex","width":18,"height":18})") {
        fail("appending a child did not invalidate :empty on its parent: " + result);
    }
}

void test_inline_block_preserves_vertical_padding(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            .toolbar {
              align-items:center;
              display:flex;
              height:38px;
            }
            #documentation {
              background:#2962ff;
              border-radius:80px;
              color:#fff;
              display:inline-block;
              font-size:14px;
              line-height:18px;
              padding:5px 12px;
            }
          `;
          document.body.appendChild(style);
          const toolbar = document.createElement('div');
          toolbar.className = 'toolbar';
          const button = document.createElement('button');
          button.id = 'documentation';
          button.textContent = 'Documentation';
          toolbar.appendChild(button);
          document.body.appendChild(toolbar);
          const rect = button.getBoundingClientRect();
          return {
            y: rect.y,
            height: rect.height,
            paddingTop: getComputedStyle(button).paddingTop,
            paddingBottom: getComputedStyle(button).paddingBottom
          };
        })()
    )JS", "inline-block-vertical-padding.js");
    if (result != R"({"y":5,"height":28,"paddingTop":"5px","paddingBottom":"5px"})") {
        fail("inline-block text box discarded its vertical padding: " + result);
    }
}

void test_pointer_hit_targets_and_related_targets_are_elements(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const row = document.createElement('div');
          row.id = 'pointer-row';
          row.style.width = '120px';
          row.style.height = '40px';
          row.textContent = 'Parent row';
          const outside = document.createElement('div');
          outside.id = 'pointer-outside';
          outside.style.position = 'fixed';
          outside.style.left = '200px';
          outside.style.top = '0';
          outside.style.width = '80px';
          outside.style.height = '40px';
          outside.textContent = 'Outside';
          globalThis.__pointerElementTargets = { entered: null, leftFor: null };
          row.addEventListener('pointerenter', event => {
            globalThis.__pointerElementTargets.entered = event.target?.id || null;
          });
          row.addEventListener('pointerleave', event => {
            globalThis.__pointerElementTargets.leftFor = event.relatedTarget?.id || null;
          });
          document.body.appendChild(row);
          document.body.appendChild(outside);
        })()
    )JS", "native-pointer-element-target-setup.js");
    require(
        evaluate(engine,
            "document.elementFromPoint(10, 10)?.id || null",
            "native-pointer-element-target-hit.js") == "\"pointer-row\"",
        "elementFromPoint exposed an internal text node instead of its nearest element");

    pointer_move(engine, 10, 10, 601U);
    require(
        evaluate(engine, "true", "native-pointer-element-target-enter-drain.js") == "true",
        "pointer entry did not drain");
    pointer_move(engine, 210, 10, 602U);
    const auto state = evaluate(engine,
        "globalThis.__pointerElementTargets",
        "native-pointer-element-target-result.js");
    require(
        state == R"({"entered":"pointer-row","leftFor":"pointer-outside"})",
        "pointer boundary target/relatedTarget did not retain browser element identity: " + state);
}

void test_z_index_orders_positioned_siblings_in_scene(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            .stack-host { display:flex; width:160px; height:24px; }
            .stack-wrapper { width:80px; height:24px; }
            .stack-overlay {
              position:relative; z-index:2; width:80px; height:24px;
              background:rgb(18,52,86);
            }
            .stack-value {
              position:relative; z-index:1; width:80px; height:24px;
              margin-left:-80px; background:rgb(171,205,239);
            }
            .stack-elevated { position:relative; z-index:6; width:80px; height:24px; }
            .stack-negative-child {
              position:relative; z-index:-3; width:80px; height:24px;
              background:rgb(69,103,137); border:1px solid rgb(120,154,188);
              border-right-width:0; border-radius:4px 0 0 4px;
            }
          `;
          document.body.appendChild(style);
          const host = document.createElement('div');
          host.className = 'stack-host';
          const wrapper = document.createElement('div');
          wrapper.className = 'stack-wrapper';
          const overlay = document.createElement('div');
          overlay.className = 'stack-overlay';
          const value = document.createElement('div');
          value.className = 'stack-value';
          wrapper.appendChild(overlay);
          host.appendChild(wrapper);
          host.appendChild(value);
          document.body.appendChild(host);
          const elevated = document.createElement('div');
          elevated.className = 'stack-elevated';
          const negativeChild = document.createElement('div');
          negativeChild.className = 'stack-negative-child';
          elevated.appendChild(negativeChild);
          document.body.appendChild(elevated);
        })()
    )JS", "native-z-index-paint-setup.js");
    const auto fixture = evaluate(
        engine,
        "({ width: document.querySelector('.stack-host').offsetWidth, "
        "z: getComputedStyle(document.querySelector('.stack-overlay')).zIndex, "
        "valueZ: getComputedStyle(document.querySelector('.stack-value')).zIndex, "
        "valueBackground: getComputedStyle(document.querySelector('.stack-value')).backgroundColor, "
        "hit: document.elementFromPoint(10, 10)?.className || null })",
        "native-z-index-paint-ready.js");
    if (fixture.find("\"z\":\"2\"") == std::string::npos
        || fixture.find("\"hit\":\"stack-overlay\"") == std::string::npos) {
        fail("z-index paint fixture did not apply its stacking order: " + fixture);
    }

    webscene_engine_request_scene_checkpoint(engine);
    auto observed_order = false;
    auto observed_nested_foreground = false;
    auto observed_partial_rounded_border = false;
    auto observed_commands = std::string{};
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            auto value_index = -1;
            auto overlay_index = -1;
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind == 9U && (command.rgba >> 8U) == 0xABCDEFU) {
                    value_index = static_cast<int>(index);
                    observed_commands += "underlay=" + std::to_string(index) + ":9 ";
                }
                if ((command.rgba >> 8U) == 0x123456U) {
                    observed_commands += "overlay=" + std::to_string(index) + ":"
                        + std::to_string(command.kind) + " ";
                }
                if (command.kind == 9U && (command.rgba >> 8U) == 0x123456U) {
                    overlay_index = static_cast<int>(index);
                }
                if ((command.rgba >> 8U) == 0x456789U) {
                    observed_commands += "nested=" + std::to_string(index) + ":"
                        + std::to_string(command.kind) + " ";
                    observed_nested_foreground = command.kind == 10U
                        && command.radius_top_left > 0
                        && command.radius_top_right == 0
                        && command.radius_bottom_right == 0
                        && command.radius_bottom_left > 0;
                }
                if ((command.rgba >> 8U) == 0x789ABCU) {
                    observed_commands += "border=" + std::to_string(index) + ":"
                        + std::to_string(command.kind) + ":"
                        + std::to_string(command.flags >> 28U) + " ";
                    observed_partial_rounded_border = command.kind == 11U
                        && (command.flags >> 28U) == 13U
                        && command.radius_top_left > 0
                        && command.radius_top_right == 0
                        && command.radius_bottom_right == 0
                        && command.radius_bottom_left > 0
                        && std::abs(command.stroke_width - 1.0F) < 0.01F;
                }
            }
            observed_order = value_index >= 0 && overlay_index > value_index;
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (observed_order && observed_nested_foreground && observed_partial_rounded_border) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (!observed_order || !observed_nested_foreground || !observed_partial_rounded_border) {
        fail("stacking contexts did not preserve elevated sibling and negative-child paint: "
            + observed_commands + "fixture=" + fixture);
    }
}

void test_flex_gap_and_variable_text_metrics(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `
            .checkbox { display:flex; align-items:flex-start; gap:8px; }
            .box { width:18px; height:18px; flex:0 0 18px; }
            .label { font-size:14px; line-height:18px; }
            .precision { width:60px; font-size:14px; line-height:18px; }
            .actions { display:flex; gap:12px; }
            .action { display:inline-flex; padding:0 11px; border:1px solid; }
            .footer { display:flex; width:300px; }
            .footer-buttons { display:flex; gap:12px; margin-left:auto; }
            .weekdays { display:flex; justify-content:space-around; width:262px; }
            .time-menu { position:fixed; left:400px; top:200px; max-height:231px;
              box-sizing:border-box; }
            .time-scroll { height:100%; overflow-x:hidden; overflow-y:auto; }
            .time-box { padding:6px 0; }
            .time-item { align-items:center; box-sizing:border-box; display:flex;
              font-size:14px; padding:2px 10px 2px 8px; white-space:nowrap; width:100%; }
            .time-row { box-sizing:border-box; display:flex; flex:0 1 100%;
              max-width:100%; min-width:0; padding:0 4px; }
            .time-label { display:flex; flex:0 0 auto; max-width:100%; overflow:hidden; }
            .time-control { display:inline-block; max-width:100px; font-size:14px; line-height:18px; }
            .time-input-shell { display:flex; }
            .time-input { width:100%; font-size:inherit; line-height:inherit; }
            .time-icon { flex:0 0 18px; width:18px; }
          `;
          document.body.appendChild(style);

          const checkbox = document.createElement('label');
          checkbox.className = 'checkbox';
          const box = document.createElement('span');
          box.className = 'box';
          const label = document.createElement('span');
          label.className = 'label';
          label.textContent = 'Body';
          checkbox.appendChild(box);
          checkbox.appendChild(label);
          document.body.appendChild(checkbox);

          const precision = document.createElement('div');
          precision.className = 'precision';
          precision.textContent = 'Precision';
          document.body.appendChild(precision);

          const actions = document.createElement('div');
          actions.className = 'actions';
          for (const text of ['Template', 'Cancel', 'Ok']) {
            const button = document.createElement('button');
            button.className = 'action';
            button.textContent = text;
            actions.appendChild(button);
          }
          document.body.appendChild(actions);

          const footer = document.createElement('div');
          footer.className = 'footer';
          const template = document.createElement('button');
          template.textContent = 'Template';
          const footerButtons = document.createElement('div');
          footerButtons.className = 'footer-buttons';
          footerButtons.appendChild(document.createElement('button')).textContent = 'Cancel';
          footerButtons.appendChild(document.createElement('button')).textContent = 'Ok';
          footer.appendChild(template);
          footer.appendChild(footerButtons);
          document.body.appendChild(footer);

          const weekdays = document.createElement('div');
          weekdays.className = 'weekdays';
          for (const day of ['Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa', 'Su']) {
            weekdays.appendChild(document.createElement('span')).textContent = day;
          }
          document.body.appendChild(weekdays);

          const portal = document.createElement('span');
          const timeMenu = document.createElement('div');
          timeMenu.className = 'time-menu';
          const timeScroll = document.createElement('div');
          timeScroll.className = 'time-scroll';
          const timeBox = document.createElement('div');
          timeBox.className = 'time-box';
          const timeItem = document.createElement('div');
          timeItem.className = 'time-item';
          const timeRow = document.createElement('span');
          timeRow.className = 'time-row';
          const timeLabel = document.createElement('span');
          timeLabel.className = 'time-label';
          timeLabel.textContent = '00:00';
          timeRow.appendChild(timeLabel);
          timeItem.appendChild(timeRow);
          timeBox.appendChild(timeItem);
          timeScroll.appendChild(timeBox);
          timeMenu.appendChild(timeScroll);
          portal.appendChild(timeMenu);
          document.body.appendChild(portal);

          const timeControl = document.createElement('div');
          timeControl.className = 'time-control';
          const timeInputShell = document.createElement('div');
          timeInputShell.className = 'time-input-shell';
          const timeInput = document.createElement('input');
          timeInput.className = 'time-input';
          timeInput.value = '00:00';
          const timeIcon = document.createElement('span');
          timeIcon.className = 'time-icon';
          timeInputShell.appendChild(timeInput);
          timeInputShell.appendChild(timeIcon);
          timeControl.appendChild(timeInputShell);
          document.body.appendChild(timeControl);

          const boxRect = box.getBoundingClientRect();
          const labelRect = label.getBoundingClientRect();
          const buttons = [...actions.children].map(node => node.getBoundingClientRect());
          const footerRect = footer.getBoundingClientRect();
          const templateRect = template.getBoundingClientRect();
          const footerButtonsRect = footerButtons.getBoundingClientRect();
          const weekdayRects = [...weekdays.children].map(node => node.getBoundingClientRect());
          return {
            checkboxGap: Math.round((labelRect.left - boxRect.right) * 100) / 100,
            aligned: Math.abs(labelRect.top - boxRect.top) < 0.01,
            computedGap: getComputedStyle(checkbox).gap,
            precisionOneLine: precision.getBoundingClientRect().height === 18,
            buttonGaps: Math.round(buttons[1].left - buttons[0].right) === 12
              && Math.round(buttons[2].left - buttons[1].right) === 12,
            cancelWideEnough: buttons[1].width >= 60 && buttons[1].width < 75,
            autoMarginPushesActions: footerButtonsRect.left - templateRect.right > 100
              && Math.abs(footerButtonsRect.right - footerRect.right) < 0.01,
            spaceAroundDistributes: weekdayRects[0].left > 5
              && weekdayRects[6].right < weekdays.getBoundingClientRect().right - 5
              && weekdayRects[1].left - weekdayRects[0].right > 10,
            timeMenuPreservesLabel: timeMenu.getBoundingClientRect().width >= 60
              && timeLabel.getBoundingClientRect().width >= 35,
            textInputHasReplacedIntrinsicWidth: timeControl.getBoundingClientRect().width === 100
              && timeInput.getBoundingClientRect().width >= 75,
            inheritedInputTypography: getComputedStyle(timeInput).fontSize === '14px'
              && getComputedStyle(timeInput).lineHeight === '18px'
          };
        })()
    )JS", "native-flex-gap-text-metrics.js");
    const auto expected = R"({"checkboxGap":8,"aligned":true,"computedGap":"8px 8px","precisionOneLine":true,"buttonGaps":true,"cancelWideEnough":true,"autoMarginPushesActions":true,"spaceAroundDistributes":true,"timeMenuPreservesLabel":true,"textInputHasReplacedIntrinsicWidth":true,"inheritedInputTypography":true})";
    if (result != expected) {
        fail("flex gaps or variable text metrics regressed: " + result);
    }
}

void test_outer_box_shadow_reaches_elevated_scene(webscene_engine* engine)
{
    const auto fixture = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `.shadow-probe {
            position:fixed; left:20px; top:30px; width:100px; height:40px;
            border-radius:6px; background:rgb(20,30,40);
            box-shadow:3px 5px 12px 2px rgba(1,2,3,0.5);
          }
          .padded-uppercase {
            position:fixed; left:150px; top:30px; font-size:11px;
            padding-left:6px; color:rgb(4,5,6); text-transform:uppercase;
          }`;
          document.body.appendChild(style);
          const probe = document.createElement('div');
          probe.className = 'shadow-probe';
          document.body.appendChild(probe);
          const text = document.createElement('span');
          text.className = 'padded-uppercase';
          text.textContent = 'Candles';
          document.body.appendChild(text);
          return {
            shadow: getComputedStyle(probe).boxShadow,
            upperWidth: Math.round(text.getBoundingClientRect().width * 100) / 100
          };
        })()
    )JS", "native-box-shadow-setup.js");
    require(fixture.find("12px") != std::string::npos
            && fixture.find(R"("upperWidth":54.84)") != std::string::npos,
        "computed shadow or transformed intrinsic text width was wrong: " + fixture);

    webscene_engine_request_scene_checkpoint(engine);
    auto observed = false;
    auto observed_padded_text = false;
    for (auto attempt = 0; attempt < 100 && (!observed || !observed_padded_text); ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind == 18U
                    && (command.rgba >> 8U) == 0x010203U
                    && std::abs(command.x - 21.0F) < 0.01F
                    && std::abs(command.y - 33.0F) < 0.01F
                    && std::abs(command.width - 104.0F) < 0.01F
                    && std::abs(command.height - 44.0F) < 0.01F
                    && std::abs(command.stroke_width - 12.0F) < 0.01F) {
                    observed = true;
                }
                if (command.kind == 3U
                    && (command.rgba >> 8U) == 0x040506U
                    && std::abs(command.x - 156.0F) < 0.01F) {
                    observed_padded_text = true;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (!observed || !observed_padded_text) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(observed, "elevated outer box-shadow command was absent from the native scene");
    require(observed_padded_text,
        "padded text did not paint from its content-box inset in the native scene");
}

void test_segmented_rounded_borders_share_an_unclipped_join(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              .segment { position: fixed; top: 20px; box-sizing: border-box; height: 24px; border: 1px solid rgb(0, 120, 130); }
              #leading { left: 20px; width: 120px; border-right-width: 0; border-radius: 4px 0 0 4px; }
              #trailing { left: 140px; width: 60px; border-left-width: 0; border-radius: 0 4px 4px 0; }
            </style>
            <div id="segments"><div id="leading" class="segment"></div><div id="trailing" class="segment"></div></div>`;
        })()
    )JS", "segmented-rounded-border-setup.js");

    webscene_engine_request_scene_checkpoint(engine);
    std::vector<webscene_scene_command> borders;
    for (auto attempt = 0; attempt < 100 && borders.size() != 2U; ++attempt) {
        borders.clear();
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind == 8U || command.kind == 11U) borders.push_back(command);
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (borders.size() != 2U) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(borders.size() == 2U, "segmented rounded borders did not reach the native scene");
    std::sort(borders.begin(), borders.end(), [](const auto& left, const auto& right) {
        return left.x < right.x;
    });
    constexpr uint32_t border_color_partition = 1U << 27U;
    constexpr uint32_t border_top = 1U << 28U;
    constexpr uint32_t border_right = 1U << 29U;
    constexpr uint32_t border_bottom = 1U << 30U;
    constexpr uint32_t border_left = 1U << 31U;
    const auto side_mask = border_top | border_right | border_bottom | border_left;
    require(
        (borders[0].flags & border_color_partition) == 0U
            && (borders[1].flags & border_color_partition) == 0U,
        "open segmented borders were incorrectly classified as colour partitions");
    require(
        (borders[0].flags & side_mask) == (border_top | border_bottom | border_left)
            && (borders[1].flags & side_mask) == (border_top | border_right | border_bottom),
        "segmented border side ownership was not preserved: "
            + std::to_string(borders[0].flags & side_mask) + "@"
            + std::to_string(borders[0].x) + "+" + std::to_string(borders[0].width) + ","
            + std::to_string(borders[1].flags & side_mask) + "@"
            + std::to_string(borders[1].x) + "+" + std::to_string(borders[1].width));
    require(
        std::abs((borders[0].x + borders[0].width) - borders[1].x) < 0.01F,
        "adjacent rounded border centerlines left a clipped seam at their open edges");
}

void test_transform_origin_keywords_cascade_independently_from_inline_transform(
    webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `
            .indicator { position:fixed; left:10px; top:20px; width:100px; height:4px;
              transform-origin:left; }
            .segment { position:absolute; top:0; height:4px; }
            .leading { left:0; width:20px; transform-origin:left; }
            .middle { left:0; width:100px; transform-origin:center; }
            .trailing { right:0; width:20px; transform-origin:right; }
          `;
          document.body.appendChild(style);
          const indicator = document.createElement('div');
          indicator.className = 'indicator';
          indicator.style.transform = 'translateX(20px) scaleX(0.8)';
          for (const [className, transform] of [
            ['segment leading', 'scaleX(1.25)'],
            ['segment middle', 'scaleX(0.625)'],
            ['segment trailing', 'scaleX(1.25)']
          ]) {
            const segment = document.createElement('div');
            segment.className = className;
            segment.style.transform = transform;
            indicator.appendChild(segment);
          }
          document.body.appendChild(indicator);
        })()
    )JS", "transform-origin-keyword-cascade.js");

    webscene_engine_request_scene_checkpoint(engine);
    std::vector<webscene_scene_command> scales;
    for (auto attempt = 0; attempt < 100 && scales.size() != 4U; ++attempt) {
        scales.clear();
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                if (scene->commands[index].kind == 15U) scales.push_back(scene->commands[index]);
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (scales.size() != 4U) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    require(scales.size() == 4U,
        "segmented indicator did not publish four nested scale transforms");
    const auto close = [](float actual, float expected) {
        return std::abs(actual - expected) < 0.01F;
    };
    require(
        close(scales[0].x, 30.0F) && close(scales[0].width, 0.8F)
            && close(scales[1].x, 30.0F) && close(scales[1].width, 1.25F)
            && close(scales[2].x, 80.0F) && close(scales[2].width, 0.625F)
            && close(scales[3].x, 130.0F) && close(scales[3].width, 1.25F),
        "left/center/right transform-origin keywords did not survive inline transform cascade");
}

void test_transform_translate_calc_arguments_preserve_nested_functions(
    webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              #switch-thumb {
                --switch-width: 38px;
                --switch-thumb: 14px;
                --switch-padding: 3px;
                position: fixed;
                left: 10px;
                top: 20px;
                width: var(--switch-thumb);
                height: var(--switch-thumb);
                transform: translate(
                  calc(var(--switch-width) - var(--switch-thumb) - var(--switch-padding)),
                  var(--switch-padding));
              }
            </style>
            <span id="switch-thumb"></span>`;
          const thumb = document.getElementById('switch-thumb');
          const rect = thumb.getBoundingClientRect();
          return {
            transform: getComputedStyle(thumb).transform,
            rect: [rect.x, rect.y, rect.width, rect.height]
          };
        })()
    )JS", "transform-translate-nested-calc-arguments.js");
    require(
        result == R"JSON({"transform":"matrix(1, 0, 0, 1, 21, 3)","rect":[31,23,14,14]})JSON",
        "nested calc() transform arguments were split before their closing parenthesis: "
            + result);
}

void test_transform_transition_uses_host_clock_for_translate_and_scale(
    webscene_engine* engine)
{
    execute_and_wait(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              #temporal-indicator {
                position: fixed; left: 10px; top: 20px; width: 100px; height: 4px;
                transform: translateX(0px) scaleX(.5);
                transform-origin: left;
                transition: transform 100ms linear;
              }
              #temporal-indicator.selected {
                transform: translateX(80px) scaleX(1);
              }
            </style>
            <div id="temporal-indicator"></div>`;
        })()
    )JS", "transform-transition-host-clock-setup.js");
    animation_frame_and_wait(engine, 0.0, 7001U);
    const auto initial = evaluate(engine,
        "getComputedStyle(document.getElementById('temporal-indicator')).getPropertyValue('transform')",
        "transform-transition-initial.js");
    require(
        initial == "\"matrix(0.5, 0, 0, 1, 0, 0)\"",
        "transform transition fixture did not begin at its authored transform: " + initial);

    execute_and_wait(engine,
        "document.getElementById('temporal-indicator').classList.add('selected')",
        "transform-transition-activate.js");
    require(
        evaluate(engine,
            "getComputedStyle(document.getElementById('temporal-indicator')).getPropertyValue('transform')",
            "transform-transition-before-frame.js") == "\"matrix(0.5, 0, 0, 1, 0, 0)\"",
        "transform transition jumped before the next host frame");

    animation_frame_and_wait(engine, 50.0, 7002U);
    const auto midpoint = evaluate(engine, R"JS(
        (() => {
          const indicator = document.getElementById('temporal-indicator');
          const matrix = getComputedStyle(indicator).getPropertyValue('transform')
            .slice(7, -1).split(',').map(Number);
          const rect = indicator.getBoundingClientRect();
          return { matrix, rect: [rect.x, rect.width] };
        })()
    )JS", "transform-transition-midpoint.js");
    require(
        midpoint == R"({"matrix":[0.75,0,0,1,40,0],"rect":[50,75]})",
        "translate/scale transition did not publish midpoint geometry: " + midpoint);

    animation_frame_and_wait(engine, 100.0, 7003U);
    const auto completed = evaluate(engine, R"JS(
        (() => {
          const indicator = document.getElementById('temporal-indicator');
          const matrix = getComputedStyle(indicator).getPropertyValue('transform')
            .slice(7, -1).split(',').map(Number);
          const rect = indicator.getBoundingClientRect();
          return { matrix, rect: [rect.x, rect.width] };
        })()
    )JS", "transform-transition-complete.js");
    require(
        completed == R"({"matrix":[1,0,0,1,80,0],"rect":[90,100]})",
        "translate/scale transition did not finish at its target geometry: " + completed);
}

void test_transform_transition_interpolates_from_none(webscene_engine* engine)
{
    execute_and_wait(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              #none-transition {
                width: 20px;
                height: 20px;
                transform: none;
                transition: transform 100ms linear;
              }
              #none-transition.active {
                transform: translateX(20px) rotate(0deg);
              }
            </style>
            <div id="none-transition"></div>`;
          requestAnimationFrame(() =>
            document.getElementById('none-transition').classList.add('active'));
        })()
    )JS", "transform-transition-from-none-setup.js");
    require(
        evaluate(engine,
            "getComputedStyle(document.getElementById('none-transition')).transform",
            "transform-transition-from-none-initial.js")
            == "\"matrix(1, 0, 0, 1, 0, 0)\"",
        "transform transition from none did not begin at the identity matrix");
    animation_frame_and_wait(engine, 200.0, 7011U);
    require(
        evaluate(engine,
            "getComputedStyle(document.getElementById('none-transition')).transform",
            "transform-transition-from-none-before-frame.js")
            == "\"matrix(1, 0, 0, 1, 0, 0)\"",
        "transform transition from none jumped before the next host frame");

    animation_frame_and_wait(engine, 250.0, 7012U);
    const auto midpoint = evaluate(engine,
        "getComputedStyle(document.getElementById('none-transition')).transform",
        "transform-transition-from-none-midpoint.js");
    require(
        midpoint == "\"matrix(1, 0, 0, 1, 10, 0)\"",
        "transform transition from none did not publish midpoint geometry: " + midpoint);
    animation_frame_and_wait(engine, 300.0, 7013U);
    const auto progressed = evaluate(engine,
        "getComputedStyle(document.getElementById('none-transition')).transform",
        "transform-transition-from-none-progressed.js");
    require(
        progressed == "\"matrix(1, 0, 0, 1, 20, 0)\"",
        "transform transition from none did not finish at its target geometry: " + progressed);
    execute_and_wait(engine,
        "document.getElementById('none-transition').classList.remove('active')",
        "transform-transition-back-to-none-activate.js");
    require(
        evaluate(engine,
            "getComputedStyle(document.getElementById('none-transition')).transform",
            "transform-transition-back-to-none-initial.js")
            == "\"matrix(1, 0, 0, 1, 20, 0)\"",
        "transform transition back to none did not retain its initial painted value");
    animation_frame_and_wait(engine, 350.0, 7014U);
    const auto reverse_midpoint = evaluate(engine,
        "getComputedStyle(document.getElementById('none-transition')).transform",
        "transform-transition-back-to-none-midpoint.js");
    require(
        reverse_midpoint == "\"matrix(1, 0, 0, 1, 10, 0)\"",
        "transform transition back to none did not publish midpoint geometry: "
            + reverse_midpoint);
    animation_frame_and_wait(engine, 400.0, 7015U);
    const auto reverse_completed = evaluate(engine,
        "getComputedStyle(document.getElementById('none-transition')).transform",
        "transform-transition-back-to-none-complete.js");
    require(
        reverse_completed == "\"none\""
            || reverse_completed == "\"matrix(1, 0, 0, 1, 0, 0)\"",
        "transform transition back to none did not finish at the identity value: "
            + reverse_completed);
}

void test_class_list_is_same_live_object(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const node = document.createElement('div');
          const other = document.createElement('div');
          document.body.appendChild(node);
          const tokens = node.classList;
          const same = tokens === node.classList;
          const distinct = tokens !== other.classList;
          node.className = 'shown';
          tokens.value = 'widgetbar-wrap unselectable';
          const assignedValue = tokens.value;
          const assignedAttribute = node.getAttribute('class');
          const assignedTokens =
            tokens.contains('widgetbar-wrap') && tokens.contains('unselectable');
          tokens.value = 'shown';
          const originalContains = tokens.contains;
          let calls = 0;
          tokens.contains = function(token) {
            calls++;
            return originalContains.call(this, token);
          };
          const present = node.classList.contains('shown');
          node.remove();
          document.body.appendChild(node);
          node.setAttribute('class', 'after');
          return {
            same,
            distinct,
            present,
            calls,
            assignedValue,
            assignedAttribute,
            assignedTokens,
            reattached: node.classList === tokens,
            live: tokens.contains('after') && !tokens.contains('shown')
          };
        })()
    )JS", "dom-token-list-same-object.js");
    require(
        result == R"({"same":true,"distinct":true,"present":true,"calls":1,"assignedValue":"widgetbar-wrap unselectable","assignedAttribute":"widgetbar-wrap unselectable","assignedTokens":true,"reattached":true,"live":true})",
        "Element.classList did not preserve same-object identity and live method shadowing: "
            + result);
}

void test_bounded_css_named_color_palette()
{
    require(webscene_native::native_document::parse_color("silver") == 0xC0C0C0FFU,
        "CSS silver did not resolve to its standard RGBA value");
    require(webscene_native::native_document::parse_color("maroon") == 0x800000FFU,
        "CSS maroon did not resolve to its standard RGBA value");
    require(webscene_native::native_document::parse_color("purple") == 0x800080FFU,
        "CSS purple did not resolve to its standard RGBA value");
    require(webscene_native::native_document::parse_color("olive") == 0x808000FFU,
        "CSS olive did not resolve to its standard RGBA value");
    require(webscene_native::native_document::parse_color("navy") == 0x000080FFU,
        "CSS navy did not resolve to its standard RGBA value");
    require(webscene_native::native_document::parse_color("teal") == 0x008080FFU,
        "CSS teal did not resolve to its standard RGBA value");
    require(webscene_native::native_document::parse_color("orange") == 0xFFA500FFU,
        "CSS orange did not resolve to its standard RGBA value");
}
