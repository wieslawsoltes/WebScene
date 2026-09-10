module;
#include "../third_party/nlohmann/json.hpp"
#include <optional>
#include <string>
#include <vector>
#include <webscene/native_web.hpp>
export module kestrel.controller;
import kestrel.ui;
export import kestrel.render_data;
export namespace kestrel {
class controller {
  webscene::native_web::document &document;
  struct layer_row {
    std::string id;
    webscene::native_web::node_id name, visibility;
  };
  bool pan_enabled{}, dragging{}, line_enabled{};
  std::optional<vec3> line_start;
  float pointer_x{}, pointer_y{};
  std::vector<layer_row> rows;
  std::vector<webscene::native_web::subscription> subscriptions;

public:
  drawing model;
  camera camera;
  render_options options;
  bool render_dirty = true;
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
    subscriptions.push_back(
        document.on(view.named("fit"), "click", [this](auto &) {
          std::vector<vec3> bounds;
          for (auto &entity : model.data["entities"])
            if (model.visible(entity)) {
              auto geometry = geo::geometry(entity, 1);
              bounds.insert(bounds.end(), geometry.points.begin(),
                            geometry.points.end());
            }
          camera.fit(bounds);
          render_dirty = true;
        }));
    subscriptions.push_back(
        document.on(view.named("wireframe"), "click", [this](auto &) {
          options.style = display_style::wireframe;
          refresh();
        }));
    subscriptions.push_back(
        document.on(view.named("shaded"), "click", [this](auto &) {
          options.style = display_style::shaded_edges;
          refresh();
        }));
    subscriptions.push_back(
        document.on(view.named("pan"), "click", [this](auto &) {
          pan_enabled = !pan_enabled;
          line_enabled = false;
          line_start.reset();
          document.attribute(document.find("line"), "class", "");
          dragging = false;
          document.attribute(document.find("pan"), "class",
                             pan_enabled ? "selected" : "");
        }));
    subscriptions.push_back(
        document.on(view.named("line"), "click", [this](auto &) {
          line_enabled = true;
          pan_enabled = false;
          dragging = false;
          line_start.reset();
          document.attribute(document.find("pan"), "class", "");
          document.attribute(document.find("line"), "class", "selected");
          document.set_text(document.find("status"),
                            "Line: specify first point");
        }));
    subscriptions.push_back(
        document.on(view.named("cancel"), "click", [this](auto &) {
          line_enabled = false;
          pan_enabled = false;
          dragging = false;
          line_start.reset();
          document.attribute(document.find("line"), "class", "");
          document.attribute(document.find("pan"), "class", "");
          document.set_text(document.find("status"), "Ready");
        }));
    subscriptions.push_back(
        document.on(view.named("viewport"), "pointerdown", [this](auto &event) {
          if (line_enabled) {
            auto bounds = document.bounds(document.find("viewport"));
            camera.resize(bounds.width, bounds.height);
            auto point = camera.unproject(event.client_x - bounds.x,
                                          event.client_y - bounds.y, 0);
            if (!point)
              return;
            if (!line_start) {
              line_start = *point;
              document.set_text(document.find("status"),
                                "Line: specify next point");
            } else if ((*point - *line_start).length() > epsilon) {
              auto start = *line_start, end = *point;
              model.transaction("Line", [&] {
                model.add("LINE",
                          {{"points", {geo::encode(start), geo::encode(end)}}});
              });
              line_start = end;
              refresh();
            }
            event.prevent_default();
            return;
          }
          if (!pan_enabled)
            return;
          dragging = true;
          pointer_x = event.client_x;
          pointer_y = event.client_y;
          event.prevent_default();
        }));
    subscriptions.push_back(
        document.on(view.named("app"), "pointermove", [this](auto &event) {
          if (!dragging)
            return;
          camera.pan(event.client_x - pointer_x, event.client_y - pointer_y);
          pointer_x = event.client_x;
          pointer_y = event.client_y;
          render_dirty = true;
        }));
    subscriptions.push_back(document.on(view.named("app"), "pointerup",
                                        [this](auto &) { dragging = false; }));
    refresh();
  }
  controller(const controller &) = delete;
  controller &operator=(const controller &) = delete;
  void refresh() {
    render_dirty = true;
    document.attribute(document.find("wireframe"), "class",
                       options.style == display_style::wireframe ? "selected"
                                                                 : "");
    document.attribute(document.find("shaded"), "class",
                       options.style != display_style::wireframe ? "selected"
                                                                 : "");
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
