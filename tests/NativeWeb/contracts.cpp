#include "native_web_contracts_ui.hpp"
#include <atomic>
#include <webscene/compiled_variables.hpp>
#include <iostream>
#include <stdexcept>
#include <thread>
static void check(bool value, const char *label) {
  if (!value)
    throw std::runtime_error(label);
}
int main() {
  using namespace webscene::native_web;
  {
    using expression = variable_expression;
    auto token = [](std::string value) { return expression{expression::kind::token, value}; };
    auto ref = [](std::string name) { return expression{expression::kind::reference, name}; };
    specified_variables variables;
    variables["--accent"] = {token("#5ac6d2")};
    variables["--border"] = {token("1px"), token("solid"), ref("--accent")};
    auto parent = compute_variables(variables);
    check(parent.at("--border") == variable_tokens({"1px", "solid", "#5ac6d2"}), "compiled variable substitution");
    auto child = compute_variables({{"--accent", {token("#157a8b")}}}, parent);
    check(child.at("--border") == parent.at("--border"), "inherited variable references stay computed");
    expression fallback{expression::kind::reference, "--missing", {token("fallback")}, true};
    variables["--fallback"] = {fallback};
    variables["--a"] = {ref("--b")};
    variables["--b"] = {ref("--a")};
    variables["--rescue"] = {{expression::kind::reference, "--a", {token("safe")}, true}};
    variables["--hidden-cycle"] = {{expression::kind::reference, "--accent", {ref("--hidden-cycle")}, true}};
    auto result = compute_variables(variables);
    check(!result.at("--a") && !result.at("--b"), "cycles invalidate variables");
    check(!result.at("--hidden-cycle"), "unused fallback participates in cycles");
    check(result.at("--rescue") == variable_tokens({"safe"}), "invalid variable uses fallback");
    check(result.at("--fallback") == variable_tokens({"fallback"}), "missing variable uses fallback");
  }
  {
    document themed;
    auto panel = themed.element(themed.body(), "div");
    auto root_rule = [](const char *value, bool light) {
      rule r;
      selector_part part;
      part.root = true;
      if (light) part.attributes.push_back({"data-theme", "light", true});
      r.match.parts.push_back(part);
      r.match.specificity = light ? 20 : 10;
      r.declarations.push_back({false, nullptr, "--panel-width",
          {{variable_expression::kind::token, value}}});
      return r;
    };
    themed.add_rule(root_rule("wide", false));
    themed.add_rule(root_rule("narrow", true));
    rule consumer;
    selector_part panel_selector;
    panel_selector.tag = "div";
    consumer.match.parts.push_back(panel_selector);
    consumer.declarations.push_back({false, +[](style &s) {
      auto value = s.variable("--panel-width");
      if (value && *value)
        s.set_width({**value == variable_tokens{"wide"} ? 222.f : 195.f, length_unit::pixels});
    }});
    themed.add_rule(std::move(consumer));
    themed.render(800, 600);
    check(themed.bounds(panel).width == 222, "cascade inherits compiled variable");
    themed.attribute(themed.root(), "data-theme", "light");
    themed.render(800, 600);
    check(themed.bounds(panel).width == 195, "theme selector recomputes inherited variables");
    auto important = root_rule("wide", false);
    important.declarations.front().important = true;
    themed.add_rule(std::move(important));
    themed.render(800, 600);
    check(themed.bounds(panel).width == 222, "important variable beats theme specificity");
  }
  document d;
  auto refs = compiled_ui::build(d);
  rule compiled_variable_probe;
  selector_part probe_root;
  probe_root.root = true;
  compiled_variable_probe.match.parts.push_back(probe_root);
  compiled_variable_probe.declarations.push_back({false, +[](style &s) {
    auto border = s.variable("--border");
    check(border && *border && (**border).size() == 3 &&
          (**border)[0].length && (**border)[0].length->value == 1 &&
          (**border)[1].text == "solid" && (**border)[2].color == 0x5ac6d2ffu,
          "compiler-generated variable expression reaches native cascade");
  }});
  d.add_rule(std::move(compiled_variable_probe));
  check(d.root() != d.body(), "HTML root differs from body");
  check(d.find("html-root") == d.root(),
        "compiled HTML root attributes preserved");
  auto target = refs.named("target"), other = refs.named("other");
  const auto &initial_scene = d.render(800, 600);
  check(d.bounds(d.body()).x == 3, "root style applies to HTML element");
  check(d.bounds(d.find("grid-left")).width == 222, "compiled grid variable resolves");
  check(d.bounds(d.find("grid-right")).width == 252, "compiled grid preserves fixed track");
  check(d.bounds(d.find("variable-probe")).width == 222, "compiled var width resolves");
  check(d.bounds(d.find("variable-probe")).height == 13, "compiled var fallback plus top border resolves");
  d.attribute(d.root(), "data-theme", "light");
  d.render(800, 600);
  check(d.bounds(d.find("grid-left")).width == 195, "compiled grid variable updates with theme");
  check(d.bounds(d.find("variable-probe")).width == 195, "compiled var updates with theme");
  check(d.bounds(d.body()).x == 7,
        "theme attribute mutation updates root style");
  d.attribute(d.root(), "data-theme", "dark");
  d.render(800, 600);
  check(std::string(initial_scene.bytes.begin(), initial_scene.bytes.end())
                .find("Arial, sans-serif") != std::string::npos,
        "compiled font family reaches renderer");
  check(d.bounds(target).width == 60, "important beats inline");
  check(d.bounds(other).width == 30, "child selector");
  d.attribute(target, "class", "");
  d.render(800, 600);
  check(d.bounds(target).width == 70, "inline wins ordinary selector");
  d.focus(target);
  d.render(800, 600);
  check(d.bounds(target).height == 30, "focus selector");
  d.render(350, 600);
  check(d.bounds(target).height == 40, "responsive specificity");
  d.key("Tab");
  check(d.focused() == other, "forward tab");
  d.key("Tab", true);
  check(d.focused() == target, "reverse tab");
  d.focus(0);
  auto prevent =
      d.on(target, "pointerdown", [](event &e) { e.prevent_default(); });
  auto target_bounds = d.bounds(target);
  d.pointer("pointerdown", target_bounds.x + 2, target_bounds.y + 2);
  check(d.focused() == 0, "prevent default focus");
  prevent.dispose();
  int calls = 0;
  auto a = d.on(target, "click", [&](event &) { ++calls; });
  auto bounds = d.bounds(target);
  d.pointer("pointerdown", bounds.x + 2, bounds.y + 2);
  d.pointer("pointerup", bounds.x + 2, bounds.y + 2);
  check(calls == 1, "hit testing / bubble click");
  bool parent_called = false;
  auto parent = d.on(d.body(), "click", [&](event &) { parent_called = true; });
  auto stop = d.on(target, "click", [](event &e) { e.stop_propagation(); });
  d.dispatch(target, "click");
  check(!parent_called, "stop propagation");
  auto deleting = d.on(other, "click", [&](event &) { d.remove(other); });
  d.dispatch(other, "click");
  bool stale = false;
  try {
    d.bounds(other);
  } catch (const std::invalid_argument &) {
    stale = true;
  }
  check(stale, "remove during callback");
  std::atomic<bool> wrong_thread = false;
  std::thread worker([&] {
    try {
      d.element(d.body(), "div");
    } catch (const std::logic_error &) {
      wrong_thread = true;
    }
  });
  worker.join();
  check(wrong_thread, "thread confinement");
  auto canvas = refs.named("canvas");
  d.fill_rect(canvas, 1, 2, 3, 4, 0x123456ff);
  const auto &scene = d.render(350, 600);
  check(scene.layers.size() == 1, "canvas layer");
  check(scene.canvas.size() == 2 && scene.canvas[0].kind == 40 &&
            scene.canvas[1].kind == 22,
        "native canvas protocol");
  bool found_text = false;
  for (const auto &command : scene.commands)
    if (command.kind == 3) {
      check(command.flags < scene.strings.size(), "DOM string index");
      auto ref = scene.strings[command.flags];
      std::string text(scene.bytes.data() + ref.byte_offset, ref.byte_length);
      if (text.ends_with("\tGo"))
        found_text = true;
    }
  check(found_text, "canvas preserves DOM text table");
  auto layer = scene.layers[0];
  auto color = scene.strings[layer.string_offset];
  check(std::string(scene.bytes.data() + color.byte_offset,
                    color.byte_length) == "#123456ff",
        "canvas color table offset");
  d.clear_canvas(canvas);
  check(d.render(350, 600).layers.empty(), "canvas clearing");
  a.dispose();
  int before = calls;
  d.dispatch(target, "click");
  check(calls == before, "unsubscribe");
  auto shutdown = d.on(target, "close", [&](event &) { d.dispose(); });
  d.dispatch(target, "close");
  shutdown.dispose();
  bool disposed = false;
  try {
    d.render(100, 100);
  } catch (const std::logic_error &) {
    disposed = true;
  }
  check(disposed, "disposed document");
  {
    document grid_document;
    auto grid = grid_document.element(grid_document.body(), "div");
    grid_document.attribute(grid, "id", "workbench");
    auto left = grid_document.element(grid, "div");
    auto center = grid_document.element(grid, "div");
    auto right = grid_document.element(grid, "div");
    rule layout;
    selector_part part;
    part.id = "workbench";
    layout.match.parts.push_back(part);
    layout.declarations.push_back(
        {false, +[](style &s) {
           s.set_display(display_mode::grid);
           s.set_grid_template_columns(
               {{{222, length_unit::pixels},
                 {222, length_unit::pixels},
                 0,
                 grid_track::sizing::fixed},
                {{250, length_unit::pixels}, {}, 1, grid_track::sizing::minmax},
                {{252, length_unit::pixels},
                 {252, length_unit::pixels},
                 0,
                 grid_track::sizing::fixed}});
         }});
    grid_document.add_rule(std::move(layout));
    grid_document.render(1000, 600);
    check(std::abs(grid_document.bounds(left).width - 222) < 1,
          "compiled grid left track");
    check(std::abs(grid_document.bounds(right).width - 252) < 1,
          "compiled grid right track");
    auto old_width = grid_document.bounds(center).width;
    grid_document.render(1200, 600);
    check(std::abs(grid_document.bounds(center).width - old_width - 200) < 1,
          "compiled grid fractional track responds to resize");
  }
  {
    document states;
    auto button = states.element(states.body(), "button");
    states.set_text(button, "Press");
    rule active_rule;
    selector_part active_part;
    active_part.tag = "button";
    active_part.active = true;
    active_rule.match.parts.push_back(active_part);
    active_rule.declarations.push_back({false, +[](style &s) {
      s.set_width({123, length_unit::pixels});
    }});
    states.add_rule(std::move(active_rule));
    states.render(500, 300);
    auto area = states.bounds(button);
    states.pointer("pointerdown", area.x + 1, area.y + 1);
    states.render(500, 300);
    check(states.bounds(button).width == 123, "pressed selector applies");
    states.pointer("pointercancel", area.x + 1, area.y + 1);
    states.render(500, 300);
    check(states.bounds(button).width != 123, "cancel clears pressed selector");
    rule visible_rule;
    selector_part visible_part;
    visible_part.tag = "button";
    visible_part.focus_visible = true;
    visible_rule.match.parts.push_back(visible_part);
    visible_rule.declarations.push_back({false, +[](style &s) {
      s.set_width({140, length_unit::pixels});
    }});
    states.add_rule(std::move(visible_rule));
    states.render(500, 300);
    check(states.bounds(button).width != 140, "pointer focus has no keyboard indicator");
    states.key("Tab");
    states.render(500, 300);
    check(states.bounds(button).width == 140, "keyboard focus indicator applies");
    states.pointer("pointerdown", area.x + 1, area.y + 1);
    states.pointer("pointerup", area.x + 1, area.y + 1);
    states.render(500, 300);
    check(states.bounds(button).width != 140, "pointer switches keyboard modality");
    rule disabled_rule;
    selector_part disabled_part;
    disabled_part.tag = "button";
    disabled_part.disabled = true;
    disabled_rule.match.parts.push_back(disabled_part);
    disabled_rule.declarations.push_back({false, +[](style &s) {
      s.set_width({150, length_unit::pixels});
    }});
    states.add_rule(std::move(disabled_rule));
    states.attribute(button, "disabled", "");
    states.render(500, 300);
    check(states.bounds(button).width == 150, "disabled selector applies");
  }
  std::cout << "Native Web contracts passed\n";
}
