from pathlib import Path

root = Path(__file__).resolve().parents[1]
tasks = root / 'experiments/WebScene.NativeEngine.Probe/native/webscene_v8_runtime_tasks.inc'
text = tasks.read_text()
include = '#include "webscene_v8_runtime_select_popup.inc"\n'
if include not in text:
    text = include + text

old_default = '''    bool apply_non_checkable_form_control_click_default(dom_node& target)\n    {\n        if (target.attributes.contains("disabled")) return false;\n        if (target.tag == "select" && !target.attributes.contains("multiple")) {\n            // WebScene has no operating-system popup surface. A direct native\n            // activation advances the collapsed selection, while Arrow keys\n            // retain the usual bounded navigation. This keeps ordinary two-\n            // choice component controls usable without a presenter widget.\n            return move_single_select_selection(target, 1, false, false, true);\n        }\n        return false;\n    }\n'''
new_default = '''    bool apply_non_checkable_form_control_click_default(dom_node& target)\n    {\n        if (target.attributes.contains("disabled")) return false;\n        // Collapsed selects have a real picker default action. Opening it is\n        // deferred until after the click event so preventDefault() can suppress\n        // the picker exactly like any other cancelable activation default.\n        if (select_popup_is_candidate(target)) return false;\n        return false;\n    }\n'''
assert old_default in text, 'select click-to-cycle implementation changed'
text = text.replace(old_default, new_default, 1)

keyboard_anchor = '''        if (!prevented && key_code == 9) {\n'''
keyboard_block = '''        if (!prevented && target->tag == "select"\n            && select_popup_is_candidate(*target)) {\n            if (select_popup_is_open(*target)) {\n                if (!select_popup_snapshot_valid(*target)) {\n                    close_select_popup(*target, false);\n                } else if (key_code == 27) {\n                    close_select_popup(*target, false);\n                    return true;\n                } else if (key_code == 13 || key_code == 32) {\n                    return select_popup_commit(*target, input);\n                } else if (key_code == 36 || key_code == 35\n                    || key_code == 38 || key_code == 40) {\n                    return select_popup_move_highlight(\n                        *target,\n                        key_code == 38 ? -1 : key_code == 40 ? 1 : 0,\n                        key_code == 36,\n                        key_code == 35);\n                } else if (key_code == 33 || key_code == 34) {\n                    for (auto step = 0; step < 5; ++step) {\n                        select_popup_move_highlight(\n                            *target, key_code == 33 ? -1 : 1);\n                    }\n                    return true;\n                } else if (!command_modifier\n                    && ((key_code >= '0' && key_code <= '9')\n                        || (key_code >= 'A' && key_code <= 'Z'))) {\n                    return select_popup_typeahead(\n                        *target, static_cast<char>(key_code));\n                } else if (key_code == 9) {\n                    // Tab accepts no pending value; close and let the normal\n                    // sequential focus algorithm continue below.\n                    close_select_popup(*target, false);\n                }\n            } else if (key_code == 13 || key_code == 32\n                || (key_code == 40\n                    && (input.flags & WEBSCENE_INPUT_MODIFIER_ALT) != 0U)) {\n                static_cast<void>(open_select_popup(*target));\n                return true;\n            }\n        }\n'''
assert keyboard_anchor in text, 'keyboard tab anchor changed'
text = text.replace(keyboard_anchor, keyboard_block + keyboard_anchor, 1)

pointer_anchor = '''        auto* input_owner = keyboard ? active_element\n            : pointer_capture_target != nullptr ? pointer_capture_target : initial_hit_target;\n'''
pointer_block = '''        if (!keyboard\n            && (input.kind == WEBSCENE_INPUT_POINTER_MOVE\n                || input.kind == WEBSCENE_INPUT_POINTER_DOWN\n                || input.kind == WEBSCENE_INPUT_POINTER_UP\n                || input.kind == WEBSCENE_INPUT_WHEEL)) {\n            if (auto handled = dispatch_select_popup_pointer_input(\n                    input, initial_hit_target); handled.has_value()) {\n                return *handled;\n            }\n        }\n'''
assert pointer_anchor in text, 'pointer input-owner anchor changed'
text = text.replace(pointer_anchor, pointer_block + pointer_anchor, 1)

click_anchor = '''            if (!click_default_prevented && !queue_external_navigation(*click_target)) {\n'''
click_open = '''            if (!click_default_prevented\n                && activated_form_control != nullptr\n                && !activate_label_control_after_click\n                && select_popup_is_candidate(*activated_form_control)) {\n                static_cast<void>(open_select_popup(*activated_form_control));\n            }\n'''
assert click_anchor in text, 'click navigation anchor changed'
text = text.replace(click_anchor, click_open + click_anchor, 1)

label_anchor = '''            if (label_checkable_activation.active) {\n                if (control_click_default_prevented) {\n                    restore_checkable_click_activation(label_checkable_activation);\n                    form_control_changed = false;\n                } else {\n                    form_control_changed = label_checkable_activation.changed;\n                }\n            }\n'''
label_replacement = label_anchor + '''            if (!control_click_default_prevented\n                && activated_form_control != nullptr\n                && select_popup_is_candidate(*activated_form_control)) {\n                static_cast<void>(open_select_popup(*activated_form_control));\n            }\n'''
assert label_anchor in text, 'label activation anchor changed'
text = text.replace(label_anchor, label_replacement, 1)
tasks.write_text(text)

contract = root / 'tests/WebPlatformSubset/contracts/html-single-select-input.html'
contract.write_text('''<!doctype html>\n<meta charset="utf-8">\n<title>Collapsed single-select activation opens without changing selection</title>\n<script src="/resources/testharness.js"></script>\n<script src="/resources/testharnessreport.js"></script>\n<script src="/resources/testdriver.js"></script>\n<script src="/resources/testdriver-actions.js"></script>\n<script src="/resources/testdriver-vendor.js"></script>\n<style>\n  body { margin: 0; }\n  #choice { position: absolute; left: 20px; top: 20px; width: 240px; height: 39px; }\n</style>\n<select id="choice">\n  <option value="one">One-way flight</option>\n  <option value="return">Return flight</option>\n</select>\n<script>\n  const observed = [];\n  for (const type of ["click", "input", "change"]) {\n    choice.addEventListener(type, () => observed.push(type));\n  }\n\n  promise_test(async () => {\n    assert_equals(choice.value, "one", "initial value");\n    await test_driver.click(choice);\n    assert_equals(document.activeElement, choice, "activation focuses the select");\n    assert_equals(choice.value, "one", "opening the picker does not choose another option");\n    assert_equals(choice.selectedIndex, 0, "selected index remains unchanged until commit");\n    assert_array_equals(observed, ["click"], "opening does not emit input/change");\n  }, "native collapsed select activation opens a picker without cycling its value");\n</script>\n''')

input_tests = root / 'experiments/WebScene.NativeEngine.Probe/tests/native_v8_runtime_input_tests.inc'
t = input_tests.read_text()
start = t.index('void test_collapsed_single_select_native_activation(webscene_engine* engine)')
brace = t.index('{', start)
depth = 0
end = None
for i in range(brace, len(t)):
    if t[i] == '{': depth += 1
    elif t[i] == '}':
        depth -= 1
        if depth == 0:
            end = i + 1
            break
assert end is not None
replacement = r'''void test_collapsed_single_select_native_activation(webscene_engine* engine)
{
    resize(engine, 340, 230, 600U);
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = `<style>
            body { margin: 0; background: #202124; color: #f2f2f2; }
            #choice { position:absolute; left:20px; top:20px; width:240px; height:39px;
              background:#2b2d31; color:#f2f2f2; }
            #choice2 { position:absolute; left:20px; top:80px; width:240px; height:39px;
              background:#2b2d31; color:#f2f2f2; }
          </style>
          <select id="choice">
            <option value="one">One-way flight</option>
            <option value="return">Return flight</option>
            <option value="multi">Multi-city</option>
          </select>
          <select id="choice2">
            <option value="a">Alpha</option>
            <optgroup label="Unavailable" disabled><option value="b">Blocked</option></optgroup>
            <option value="c">Charlie</option>
          </select>`;
          globalThis.choice = document.getElementById('choice');
          globalThis.choice2 = document.getElementById('choice2');
          globalThis.observed = [];
          globalThis.bodyClicks = 0;
          for (const type of ['click','input','change']) choice.addEventListener(type, () => observed.push(type));
          document.body.addEventListener('click', e => { if (e.target === document.body) bodyClicks++; });
        })()
    )JS", "native-select-popup-setup.js");
    require(evaluate(engine, "choice.offsetWidth", "native-select-popup-ready.js") == "240",
        "select popup fixture did not complete layout");

    const auto click = [&](double x,double y,uint64_t& sequence) {
        pointer_button(engine, WEBSCENE_INPUT_POINTER_DOWN, x, y, sequence++, true);
        pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, x, y, sequence++, false);
    };
    uint64_t sequence = 601U;
    click(60, 40, sequence);
    auto opened = evaluate(engine, R"JS(({
      value: choice.value,
      index: choice.selectedIndex,
      active: document.activeElement === choice,
      observed
    }))JS", "native-select-popup-open.js");
    require(opened == R"JSON({"value":"one","index":0,"active":true,"observed":["click"]})JSON",
        "opening a collapsed select changed its value or emitted change events: " + opened);

    // The popup starts immediately below the 39px-high control. Its second
    // 30px row is therefore safely hit around y=108.
    click(60, 108, sequence);
    auto chosen = evaluate(engine, R"JS(({
      value: choice.value,
      index: choice.selectedIndex,
      observed
    }))JS", "native-select-popup-pointer-choice.js");
    require(chosen == R"JSON({"value":"return","index":1,"observed":["click","input","change"]})JSON",
        "pointer selection did not commit the popup option exactly once: " + chosen);

    execute(engine, "observed = [];", "native-select-popup-reset.js");
    click(60, 40, sequence);
    click(320, 205, sequence);
    auto cancelled = evaluate(engine, R"JS(({
      value: choice.value,
      observed,
      bodyClicks
    }))JS", "native-select-popup-outside-cancel.js");
    require(cancelled == R"JSON({"value":"return","observed":["click"],"bodyClicks":0})JSON",
        "outside dismissal changed selection or leaked its click through the popup: " + cancelled);

    click(60, 100, sequence);
    keyboard_input(engine, WEBSCENE_INPUT_KEY_DOWN, 40, sequence++);
    require(evaluate(engine, "choice2.value", "native-select-popup-pending-arrow.js") == R"("a")",
        "open-popup ArrowDown committed before acceptance");
    keyboard_input(engine, WEBSCENE_INPUT_KEY_DOWN, 13, sequence++);
    auto keyboard = evaluate(engine, "choice2.value", "native-select-popup-keyboard-commit.js");
    require(keyboard == R"("c")",
        "keyboard navigation did not skip a disabled optgroup option and commit Charlie: " + keyboard);

    click(60, 100, sequence);
    keyboard_input(engine, WEBSCENE_INPUT_KEY_DOWN, 38, sequence++);
    keyboard_input(engine, WEBSCENE_INPUT_KEY_DOWN, 27, sequence++);
    require(evaluate(engine, "choice2.value", "native-select-popup-escape.js") == R"("c")",
        "Escape cancellation committed the pending highlight");
}
'''
t = t[:start] + replacement + t[end:]
input_tests.write_text(t)

print('select popup source patches applied')
