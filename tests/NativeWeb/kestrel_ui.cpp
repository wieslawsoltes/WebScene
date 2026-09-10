#include <webscene/native_web.hpp>
#include "../../samples/NativeKestrel/third_party/nlohmann/json.hpp"
#include <stdexcept>
import kestrel.drawing;
import kestrel.ui;
int main() {
  webscene::native_web::document document;
  auto view=compiled_ui::build(document);
  kestrel::drawing drawing;
  std::vector<webscene::native_web::subscription> subscriptions;
  for(auto &layer:drawing.data["layers"]){
    auto row=compiled_ui::instantiate(document,view.named("layers"),"layer-row");
    document.set_text(row.named("name"),layer["name"].get<std::string>());
    auto id=layer["id"].get<std::string>();
    subscriptions.push_back(document.on(row.named("visibility"),"click",[&,id](auto&){
      for(auto &l:drawing.data["layers"])if(l["id"]==id)l["visible"]=!l["visible"].get<bool>();
    }));
    document.dispatch(row.named("visibility"),"click");
    if(layer["visible"].get<bool>())throw std::runtime_error("Native layer event failed");
  }
  document.render(1100,760);
  if(document.bounds(view.named("viewport")).width<=0)throw std::runtime_error("Missing viewport layout");
}
