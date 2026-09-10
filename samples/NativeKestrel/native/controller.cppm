module;
#include "../third_party/nlohmann/json.hpp"
#include <string>
#include <vector>
#include <webscene/native_web.hpp>
export module kestrel.controller;
import kestrel.ui;
export import kestrel.drawing;
export namespace kestrel {
class controller {
  webscene::native_web::document &document;
  struct layer_row {
    std::string id;
    webscene::native_web::node_id name, visibility;
  };
  std::vector<layer_row> rows;
  std::vector<webscene::native_web::subscription> subscriptions;

public:
  drawing model;
  explicit controller(webscene::native_web::document &document)
      : document(document) {
    auto view = compiled_ui::build(document);
    for (auto &layer : model.data["layers"]) {
      auto row =
          compiled_ui::instantiate(document, view.named("layers"), "layer-row");
      auto id = layer["id"].get<std::string>();
      rows.push_back({id, row.named("name"), row.named("visibility")});
      subscriptions.push_back(
          document.on(row.named("visibility"), "click", [this, id](auto &) {
            model.transaction("Layer visibility", [&] {
              for (auto &l : model.data["layers"])
                if (l["id"] == id)
                  l["visible"] = !l["visible"].get<bool>();
            });
            refresh();
          }));
      subscriptions.push_back(
          document.on(row.named("name"), "click", [this, id](auto &) {
            model.transaction("Current layer",
                              [&] { model.data["currentLayer"] = id; });
            refresh();
          }));
    }
    subscriptions.push_back(
        document.on(view.named("undo"), "click", [this](auto &) {
          model.undo();
          refresh();
        }));
    subscriptions.push_back(
        document.on(view.named("redo"), "click", [this](auto &) {
          model.redo();
          refresh();
        }));
    refresh();
  }
  controller(const controller &) = delete;
  controller &operator=(const controller &) = delete;
  void refresh() {
    for (auto &row : rows)
      for (auto &layer : model.data["layers"])
        if (layer["id"] == row.id) {
          document.set_text(row.name, layer["name"].get<std::string>());
          document.set_text(row.visibility,
                            layer.value("visible", true) ? "Hide" : "Show");
          document.attribute(row.name, "class",
                             model.data["currentLayer"] == row.id
                                 ? "layer-name selected"
                                 : "layer-name");
        }
  }
  webscene::native_web::node_id visibility_button(size_t index) const {
    return rows.at(index).visibility;
  }
};
} // namespace kestrel
