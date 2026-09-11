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
  auto search=d.element(d.body(),"input");d.attribute(search,"id","explorer-search");
  kestrel::drawing model;unsigned changes=0;
  {
    kestrel::layer_panel panel(d,model,[&]{++changes;});
    require(panel.entries().size()==model.data["layers"].size());
    d.focus(search);d.text_input("a-wall");require(panel.entries().size()==1 && panel.entries()[0].id=="architecture");
    d.text_input("not-found");require(panel.entries().empty());
    const auto& empty=d.render(300,300);
    require(std::string(empty.bytes.begin(),empty.bytes.end()).find("No layers match this filter.")!=std::string::npos);
    d.set_value(search,"");d.dispatch(search,"input");require(panel.entries().size()==8);
#ifdef __APPLE__
    model.data["layers"][0]["name"]="ÉCOLE";
    d.set_value(search,"école");d.dispatch(search,"input");require(panel.entries().size()==1 && panel.entries()[0].id=="0");
    model.data["layers"][0]["name"]="0";d.set_value(search,"");d.dispatch(search,"input");
#endif
    require(d.focused()==search && changes==0); // Filtering leaves model/render state alone.
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
    model.data["entities"]={
      {{"id","visible"},{"layer","architecture"},{"hidden",false}},
      {{"id","hidden"},{"layer","architecture"},{"hidden",true}},
      {{"id","other"},{"layer","openings"},{"hidden",false}}};
    model.selection={"other"};
    const auto current=model.data["currentLayer"];
    d.dispatch(panel.entries()[1].row,"click",0,0,0,0,{},0,{true});
    require(model.selection.size()==1 && model.selection.contains("visible"));
    require(model.data["currentLayer"]==current);
    model.data["layers"][1]["visible"]=false;
    d.dispatch(panel.entries()[1].row,"click",0,0,0,0,{},0,{true});
    require(model.selection.empty());
    auto obsolete=panel.entries()[0].row;
    panel.refresh();
    bool removed=false;try {d.bounds(obsolete);} catch(const std::invalid_argument&) {removed=true;}
    require(removed);
    d.render(300,300);
  }
  d.dispose();
  std::cout<<"Original layer templates and native state/handlers passed\n";
}
