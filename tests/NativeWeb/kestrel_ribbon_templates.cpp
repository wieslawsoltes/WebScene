#include <webscene/native_web.hpp>
#include <array>
#include <stdexcept>
import kestrel.test.ribbon;
int main() {
  using namespace webscene::native_web;
  document d;
  compiled_ui::build(d);
  auto parent = d.element(d.body(), "div");
  std::array<node_id, 6> tabs{};
  std::array<const char *, 6> names{"Home", "Insert", "Annotate", "Model", "View", "Manage"};
  std::vector<subscription> handlers;
  size_t selected = 0;
  auto select = [&](size_t index) {
    selected = index;
    for (size_t i = 0; i < tabs.size(); ++i) {
      d.attribute(tabs[i], "class", i == index ? "active" : "");
      d.attribute(tabs[i], "aria-selected", i == index ? "true" : "false");
    }
  };
  for (size_t i = 0; i < tabs.size(); ++i) {
    auto instance = compiled_ui::instantiate(d, parent, "ribbon-tab");
    tabs[i] = instance.named("tab");
    d.attribute(tabs[i], "data-ribbon", names[i]);
    d.set_text(tabs[i], names[i]);
    handlers.push_back(d.on(tabs[i], "click", [&, i](event &) { select(i); }));
  }
  select(0);
  d.render(1000, 500);
  if (d.text_content(parent) != "HomeInsertAnnotateModelViewManage")
    throw std::runtime_error("Original ribbon tab labels or ordering changed");
  d.dispatch(tabs[3], "click");
  if (selected != 3) throw std::runtime_error("Native tab handler failed");
  for (auto id : tabs) d.remove(id);
  handlers.clear();
  if (!d.text_content(parent).empty()) throw std::runtime_error("Tab disposal failed");
}
