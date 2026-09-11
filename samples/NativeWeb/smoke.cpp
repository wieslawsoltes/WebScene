#include <webscene/native_web.hpp>
import webscene.sample.ui;
#include "app.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
static void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
int main(int argc, char **argv) {
  const auto start = std::chrono::steady_clock::now();
  webscene::native_web::document d;
  auto view = compiled_ui::build(d);
  app_state app;
  app.attach(d);
  if (argc > 1 && std::string_view(argv[1]) == "--snapshot") {
    std::cout << "[";
    for (int i = 0; i < 2; ++i) {
      int width = i ? 500 : 1000, height = i ? 900 : 700;
      d.render(width, height);
      if (i)
        std::cout << ",";
      std::cout << "{\"width\":" << width << ",\"height\":" << height
                << ",\"nodes\":[";
      bool first = true;
      for (auto id :
           {"app", "workspace", "counterCard", "chart", "increment", "count"}) {
        auto b = d.bounds(d.find(id));
        if (!first)
          std::cout << ",";
        first = false;
        std::cout << "{\"id\":\"" << id << "\",\"x\":" << b.x
                  << ",\"y\":" << b.y << ",\"width\":" << b.width
                  << ",\"height\":" << b.height << "}";
      }
      std::cout << "]}";
    }
    std::cout << "]\n";
    return 0;
  }
  const auto &wide = d.render(1000, 700);
  const auto ready = std::chrono::steady_clock::now();
  auto chart = d.bounds(d.find("chart"));
  auto counter = d.bounds(d.find("counterCard"));
  require(wide.commands.size() > 10, "scene missing");
  require(!wide.layers.empty(), "canvas missing");
  require(chart.x > counter.x, "wide layout");
  d.render(500, 900);
  require(d.bounds(d.find("chart")).y > d.bounds(d.find("counterCard")).y,
          "responsive layout");
  d.dispatch(view.named("increment"), "click");
  require(app.count == 1, "native click");
  require(d.text_content(d.find("count")) == "Count: 1", "text update");
  d.focus(d.find("increment"));
  d.key("Enter");
  require(app.count == 2, "keyboard activation");
  d.key("Tab");
  require(d.focused() == d.find("reset"), "tab focus");
  d.dispatch(d.find("add"), "click");
  require(d.text_content(d.find("items")).find("Item 1") != std::string::npos,
          "insert node");
  auto disposable = d.element(d.find("items"), "button");
  bool called = false;
  auto sub = d.on(disposable, "click", [&](auto &) { called = true; });
  sub.dispose();
  d.dispatch(disposable, "click");
  require(!called, "disposed callback");
  d.remove(disposable);
  bool invalid = false;
  try {
    d.set_text(disposable, "stale");
  } catch (const std::invalid_argument &) {
    invalid = true;
  }
  require(invalid, "stale node");
  auto t = std::chrono::steady_clock::now();
  for (int i = 0; i < 100; ++i) {
    d.dispatch(d.find("increment"), "click");
    d.render(1000, 700);
  }
  auto end = std::chrono::steady_clock::now();
  std::cout << "Native Web passed: commands=" << wide.commands.size()
            << " startup_us="
            << std::chrono::duration_cast<std::chrono::microseconds>(ready -
                                                                     start)
                   .count()
            << " interaction_mean_us="
            << std::chrono::duration_cast<std::chrono::microseconds>(end - t)
                       .count() /
                   100.0
            << "\n";
  d.dispose();
  sub.dispose();
  return 0;
}
