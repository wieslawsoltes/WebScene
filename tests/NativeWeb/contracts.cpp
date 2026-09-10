#include "native_web_contracts_ui.hpp"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
static void check(bool value, const char *label) {
  if (!value)
    throw std::runtime_error(label);
}
int main() {
  using namespace webscene::native_web;
  document d;
  auto refs = compiled_ui::build(d);
  auto target = refs.named("target"), other = refs.named("other");
  d.render(800, 600);
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
  std::cout << "Native Web contracts passed\n";
}
