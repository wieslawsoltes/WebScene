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
  app.model.add("MESH", kestrel::geo::box({100, 200, 0}, 20, 40, 10));
  document.dispatch(document.find("fit"), "click");
  if (app.camera.target.x != 110 || app.camera.target.y != 220)
    throw std::runtime_error("Fit command missed geometry bounds");
  document.dispatch(document.find("shaded"), "click");
  if (app.options.style != kestrel::display_style::shaded_edges)
    throw std::runtime_error("Shaded command failed");
  document.dispatch(document.find("wireframe"), "click");
  if (app.options.style != kestrel::display_style::wireframe)
    throw std::runtime_error("Wireframe command failed");
  auto viewport=document.find("viewport");
  auto bounds=document.bounds(viewport);
  unsigned pointer_events=0;
  auto pointer=document.on(viewport,"pointerdown",[&](auto &event){
    if(event.client_x!=bounds.x+10||event.client_y!=bounds.y+20)throw std::runtime_error("Native pointer coordinates lost");
    ++pointer_events;
  });
  document.pointer("pointerdown",bounds.x+10,bounds.y+20);
  document.pointer("pointerup",bounds.x+10,bounds.y+20);
  if(pointer_events!=1)throw std::runtime_error("Native viewport pointer event missing");
}
