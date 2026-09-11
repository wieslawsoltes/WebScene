#include "../../samples/NativeKestrel/third_party/nlohmann/json.hpp"
#include <webscene/shared_css.hpp>
#include <iostream>
import kestrel.drawing;
import kestrel.layer_panel;
using namespace webscene::native_web;
void require(bool value) {if(!value) throw std::runtime_error("native layer panel contract failed");}
int main() {
  document d;d.set_stylesheet_resolver(make_shared_stylesheet_resolver({},{}));
  auto list=d.element(d.body(),"div");d.attribute(list,"id","explorer-list");
  auto badge=d.element(d.body(),"span");d.attribute(badge,"id","layer-count");
  kestrel::drawing model;unsigned changes=0;
  {
    kestrel::layer_panel panel(d,model,[&]{++changes;});
    require(panel.entries().size()==model.data["layers"].size());
    auto first=panel.entries().front();
    d.dispatch(first.visibility,"click");
    require(!model.data["layers"][0]["visible"].get<bool>() && changes==1);
    require(model.data["currentLayer"]=="architecture"); // Toggle must not select its row.
    require(d.attribute(panel.entries()[0].visibility,"aria-label")=="Show 0");
    require(d.attribute(panel.entries()[0].row,"class")->find("off")!=std::string::npos);
    d.dispatch(panel.entries()[0].lock,"click");
    require(model.data["layers"][0]["locked"].get<bool>());
    d.dispatch(panel.entries()[0].row,"click");require(model.data["currentLayer"]=="0");
    model.undo();panel.refresh();require(!model.data["layers"][0]["locked"].get<bool>());
    auto obsolete=panel.entries()[0].row;
    panel.refresh();
    bool removed=false;try {d.bounds(obsolete);} catch(const std::invalid_argument&) {removed=true;}
    require(removed);
    d.render(300,300);
  }
  d.dispose();
  std::cout<<"Original layer templates and native state/handlers passed\n";
}
