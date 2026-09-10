#pragma once
#include <webscene/native_web.hpp>
struct app_state {
  int count{}, items{};
  std::vector<webscene::native_web::subscription> subscriptions;
  void attach(webscene::native_web::document &d) {
    auto redraw = [this, &d] {
      d.set_text(d.find("count"), "Count: " + std::to_string(count));
      d.attribute(d.find("counterCard"), "class",
                  count ? "card active" : "card");
      auto chart = d.find("chart");
      d.clear_canvas(chart);
      for (int i = 0; i < 8; ++i) {
        float h = 20 + ((i * 17 + count * 13) % 85);
        d.fill_rect(chart, i * 36, 120 - h, 24, h, 0x78dcc1ffu);
      }
    };
    subscriptions.push_back(
        d.on(d.find("increment"), "click", [this, redraw](auto &) {
          ++count;
          redraw();
        }));
    subscriptions.push_back(
        d.on(d.find("reset"), "click", [this, redraw](auto &) {
          count = 0;
          redraw();
        }));
    subscriptions.push_back(d.on(d.find("add"), "click", [this, &d](auto &) {
      auto item = d.element(d.find("items"), "button");
      d.text(item, "Item " + std::to_string(++items) + " - click to remove");
      subscriptions.push_back(
          d.on(item, "click", [&d, item](auto &) { d.remove(item); }));
    }));
    redraw();
  }
};
