#include "../../samples/NativeKestrel/third_party/nlohmann/json.hpp"
#include <stdexcept>
#include <webscene/native_web.hpp>
import kestrel.controller;
int main() {
  webscene::native_web::document document;
  kestrel::controller app(document);
  auto button = app.visibility_button(0);
  document.dispatch(button, "click");
  if (app.model.data["layers"][0]["visible"].get<bool>())
    throw std::runtime_error("Layer toggle failed");
  document.dispatch(document.find("undo"), "click");
  if (!app.model.data["layers"][0]["visible"].get<bool>())
    throw std::runtime_error("Layer undo failed");
  document.dispatch(document.find("redo"), "click");
  if (app.model.data["layers"][0]["visible"].get<bool>())
    throw std::runtime_error("Layer redo failed");
  document.render(1100, 760);
  if (document.bounds(document.find("viewport")).width <= 0)
    throw std::runtime_error("Missing viewport layout");
}
