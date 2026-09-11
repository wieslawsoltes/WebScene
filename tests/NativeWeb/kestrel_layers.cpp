#include "../../samples/NativeKestrel/third_party/nlohmann/json.hpp"
#include <webscene/shared_css.hpp>
#include <iostream>
import kestrel.drawing;
import kestrel.render_data;
import kestrel.examples;
import kestrel.layer_panel;
using namespace webscene::native_web;
void require(bool value) {if(!value) throw std::runtime_error("native layer panel contract failed");}
int main() {
  for(auto type:{"CIRCLE","ARC","ELLIPSE"}) {
    kestrel::drawing model;model.add(type,{{"center",{1,2,3}},{"radius",5},{"rx",5},{"ry",3},{"startAngle",0},{"endAngle",1}});
    const auto id=model.data["entities"].back()["id"].get<std::string>();model.selection.insert(id);
    document doc;doc.set_stylesheet_resolver(make_shared_stylesheet_resolver({},{}));
    auto inspector=doc.element(doc.body(),"div");doc.attribute(inspector,"id","inspector");
    auto list=doc.element(doc.body(),"div");doc.attribute(list,"id","explorer-list");
    kestrel::layer_panel panel(doc,model,[]{});
    const auto find=[&](auto&& self,node_id node)->node_id {
      if(doc.attribute(node,"data-prop")=="center.0")return node;
      for(auto child:doc.children(node))if(auto result=self(self,child))return result;
      return 0;
    };
    const auto input=find(find,inspector);require(input!=0);
    const auto before=model.data;doc.focus(input);doc.set_selection(input,0,doc.value(input).size());doc.text_input("42");doc.key("Enter");
    require(model.find(id)->at("center")[0]==42);
    require(model.undo()=="Edit center.0" && model.data==before);
    if(std::string_view(type)!="ELLIPSE") {
      (*model.find(id))["axisX"]={3,4,0};(*model.find(id))["axisY"]={-4,3,0};
      const auto original=model.data;
      require(!model.change_conic_radius(0));require(model.change_conic_radius(10));
      require(model.find(id)->at("axisX")==kestrel::json({6,8,0}));
      require(model.find(id)->at("axisY")==kestrel::json({-8,6,0}));
      require(model.undo()=="Edit radius" && model.data==original);
    }
  }
  {
    kestrel::drawing model;model.add("POINT",{{"position",{1,2,3}}});
    const auto id=model.data["entities"].back()["id"].get<std::string>();model.selection.insert(id);
    document doc;doc.set_stylesheet_resolver(make_shared_stylesheet_resolver({},{}));
    auto inspector=doc.element(doc.body(),"div");doc.attribute(inspector,"id","inspector");
    auto list=doc.element(doc.body(),"div");doc.attribute(list,"id","explorer-list");
    kestrel::layer_panel panel(doc,model,[]{});
    const auto find=[&](auto&& self,node_id node)->node_id {
      if(doc.attribute(node,"data-prop")=="position.2")return node;
      for(auto child:doc.children(node))if(auto result=self(self,child))return result;
      return 0;
    };
    const auto input=find(find,inspector);require(input!=0 && doc.value(input)=="3");
    const auto before=model.data;doc.focus(input);doc.set_selection(input,0,1);doc.text_input("-8.5");doc.key("Enter");
    require(model.find(id)->at("position")[2]==-8.5);
    require(model.undo()=="Edit position.2" && model.data==before);
    require(!model.change_point_position(3,0));
  }
  {
    kestrel::drawing model;model.add("LINE",{{"points",{{0,0,0},{10,0,0}}}});
    const auto id=model.data["entities"].back()["id"].get<std::string>();model.selection.insert(id);
    document doc;doc.set_stylesheet_resolver(make_shared_stylesheet_resolver({},{}));
    auto inspector=doc.element(doc.body(),"div");doc.attribute(inspector,"id","inspector");
    auto list=doc.element(doc.body(),"div");doc.attribute(list,"id","explorer-list");
    kestrel::layer_panel panel(doc,model,[]{});
    require(doc.text_content(inspector).find("Visible / editable")!=std::string::npos);
    (*model.find(id))["group"]="group-id";(*model.find(id))["groupName"]="External walls";panel.refresh();
    require(doc.text_content(inspector).find("External walls")!=std::string::npos);
    const auto control=[&](auto&& self,node_id root,const std::string& property)->node_id {
      if(doc.attribute(root,"data-prop")==property)return root;
      for(auto child:doc.children(root))if(auto result=self(self,child,property))return result;
      return 0;
    };
    const auto before=model.data;
    auto coordinate=control(control,inspector,"points.1.0");require(coordinate!=0);
    doc.focus(coordinate);doc.set_selection(coordinate,0,doc.value(coordinate).size());doc.text_input("25");doc.key("Enter");
    require(model.find(id)->at("points")[1][0]==25);
    require(doc.text_content(inspector).find("25.000 mm")!=std::string::npos);
    require(model.undo()=="Edit points.1.0" && model.data==before);panel.refresh();
    require(!model.change_line_endpoint(2,0,1));
    auto type=control(control,inspector,"linetype");require(type!=0);doc.focus(type);doc.key("End");
    require(model.find(id)->at("linetype")=="Center");
    auto weight=control(control,inspector,"lineweight");require(weight!=0);
    require(doc.value(weight)=="0");
    doc.focus(weight);doc.set_selection(weight,0,doc.value(weight).size());doc.text_input("1.25");doc.key("Enter");require(model.find(id)->at("lineweight")==1.25);
    auto name=control(control,inspector,"name");require(name!=0);
    doc.focus(name);doc.text_input("Native wall");doc.focus(0);require(model.find(id)->at("name")=="Native wall");
    require(model.undo()=="Edit name" && model.undo()=="Edit lineweight" && model.undo()=="Edit linetype");
    require(model.data==before);
    require(!model.change_selected_appearance("linetype","invalid"));
    require(!model.change_selected_appearance("lineweight","invalid"));
    model.data["layers"][1]["locked"]=true;
    panel.refresh();require(doc.text_content(inspector).find("Locked — read only")!=std::string::npos);
    const auto locked=model.data;
    require(!model.change_selected_appearance("name","Locked change") && model.data==locked);
  }
  {
    kestrel::drawing model;model.add("LINE",{{"points",{{0,0,0},{10,0,0}}}});
    const auto id=model.data["entities"].back()["id"].get<std::string>();model.selection.insert(id);
    const auto target=model.data["layers"][0]["id"].get<std::string>();
    model.data["layers"][0]["locked"]=true;
    const auto before=model.data;
    require(!model.change_selected_layer("missing"));
    require(model.change_selected_layer(target));
    require(model.find(id)->at("layer")==target);
    require(!model.change_selected_layer("architecture")); // Now locked, cannot edit.
    require(model.undo()=="Edit layer" && model.data==before);
  }
  {
    kestrel::drawing model;kestrel::camera camera;camera.resize(800,600);
    model.add("LINE",{{"points",{{-100,0,0},{100,0,0}}}});
    const auto id=model.data["entities"].back()["id"].get<std::string>();
    kestrel::select_window(model,camera,kestrel::display_style::wireframe,{380,280,0},{420,320,0});
    require(model.selection.empty()); // Window must contain the whole entity.
    kestrel::select_window(model,camera,kestrel::display_style::wireframe,{420,280,0},{380,320,0});
    require(model.selection.contains(id)); // Crossing intersects the middle only.
    kestrel::select_window(model,camera,kestrel::display_style::wireframe,{290,280,0},{510,320,0});
    require(model.selection.contains(id));
    model.data["layers"][0]["locked"]=true;
    model.data["entities"][0]["layer"]=model.data["layers"][0]["id"];
    kestrel::select_window(model,camera,kestrel::display_style::wireframe,{420,280,0},{380,320,0});
    require(model.selection.empty());
  }
  {
    kestrel::drawing model;kestrel::camera camera;camera.resize(800,600);
    model.add("MESH",kestrel::geo::box({-50,-50,0},100,100,20));
    const auto far_id=model.data["entities"].back()["id"].get<std::string>();
    const auto point=camera.project({10,5,0});
    require(!kestrel::pick(model,camera,kestrel::display_style::wireframe,point.x,point.y));
    auto hit=kestrel::pick(model,camera,kestrel::display_style::shaded,point.x,point.y);
    require(hit && hit->id==far_id && hit->distance==7.5);
    model.add("MESH",kestrel::geo::box({-50,-50,80},100,100,20));
    const auto near_id=model.data["entities"].back()["id"].get<std::string>();
    hit=kestrel::pick(model,camera,kestrel::display_style::shaded_edges,point.x,point.y);
    require(hit && hit->id==near_id);
    model.data["entities"].back()["hidden"]=true;
    hit=kestrel::pick(model,camera,kestrel::display_style::shaded,point.x,point.y);
    require(hit && hit->id==far_id);
  }
  {
    kestrel::drawing model;kestrel::camera camera;camera.resize(800,600);
    model.add("TEXT",{{"position",{0,0,0}},{"height",20},{"text","Room\nLabel"},{"align","center"}});
    const auto id=model.data["entities"].back()["id"].get<std::string>();
    for(auto world:{kestrel::vec3{0,8,0},kestrel::vec3{0,-22,0}}) {
      auto point=camera.project(world);
      auto hit=kestrel::pick(model,camera,kestrel::display_style::wireframe,point.x,point.y);
      require(hit && hit->id==id && hit->distance==2);
    }
    auto outside=camera.project({100,0,0});
    require(!kestrel::pick(model,camera,kestrel::display_style::wireframe,outside.x,outside.y));
  }
  {
    kestrel::drawing model;kestrel::camera camera;camera.resize(800,600);
    model.add("LINE",{{"points",{{-50,0,0},{50,0,0}}}});
    const auto id=model.data["entities"].back()["id"].get<std::string>();
    auto point=camera.project({0,0,0});
    kestrel::select_at(model,camera,kestrel::display_style::wireframe,point.x,point.y);
    require(model.selection.contains(id));
    kestrel::select_at(model,camera,kestrel::display_style::wireframe,point.x,point.y,true);
    require(model.selection.empty());
    require(!kestrel::pick(model,camera,kestrel::display_style::wireframe,point.x,point.y+10));
    model.data["entities"].back()["group"]="pair";
    model.add("LINE",{{"points",{{-50,30,0},{50,30,0}}},{"group","pair"}});
    kestrel::select_at(model,camera,kestrel::display_style::wireframe,point.x,point.y);
    require(model.selection.size()==2);
    kestrel::select_at(model,camera,kestrel::display_style::wireframe,point.x,point.y,false,true);
    require(model.selection.size()==1 && model.selection.contains(id));
    kestrel::select_at(model,camera,kestrel::display_style::wireframe,0,0,true);
    require(model.selection.size()==1);
    kestrel::select_at(model,camera,kestrel::display_style::wireframe,0,0);
    require(model.selection.empty());
    model.data["entities"][0]["hidden"]=true;

    require(!kestrel::pick(model,camera,kestrel::display_style::wireframe,point.x,point.y));
  }
  {
    kestrel::drawing drawing;
    drawing.data["layers"][0]["visible"]=false;
    drawing.data["layers"][0]["locked"]=true;
    const auto before=drawing.data;
    require(drawing.show_all_layers());
    for(const auto& layer:drawing.data["layers"])require(layer["visible"].get<bool>());
    require(drawing.data["layers"][0]["locked"].get<bool>());
    require(!drawing.show_all_layers());
    require(drawing.undo()=="Show all layers" && drawing.data==before);
    require(drawing.redo()=="Show all layers");
    require(drawing.data["layers"][0]["visible"].get<bool>());
  }
  {
    kestrel::drawing courtyard;kestrel::load_courtyard(courtyard);
    require(courtyard.data["entities"].size()==265 && courtyard.data["layers"].size()==9);
    require(courtyard.data["name"]=="Courtyard House · Ground floor");
    for(const auto& entity:courtyard.data["entities"]) require(!courtyard.layer(entity).is_null());
    const auto original=courtyard.data;
    courtyard.selection={courtyard.data["entities"][0]["id"].get<std::string>()};
    require(courtyard.erase_selected()==1 && courtyard.data["entities"].size()==264);
    require(courtyard.undo().has_value() && courtyard.data==original);
    require(courtyard.redo().has_value() && courtyard.data["entities"].size()==264);
    courtyard.selection.clear();require(courtyard.erase_selected()==0);
  }
  document d;d.set_stylesheet_resolver(make_shared_stylesheet_resolver({},{}));
  auto icon=d.element(d.body(),"span");d.attribute(icon,"data-icon","chevron-left");
  auto inspector=d.element(d.body(),"div");d.attribute(inspector,"id","inspector");
  auto title=d.element(d.body(),"span");d.attribute(title,"id","title-name");
  auto selected_status=d.element(d.body(),"span");d.attribute(selected_status,"id","selection-status");
  auto tabs=d.element(d.body(),"div");d.attribute(tabs,"id","document-tabs");
  auto list=d.element(d.body(),"div");d.attribute(list,"id","explorer-list");
  auto badge=d.element(d.body(),"span");d.attribute(badge,"id","layer-count");
  auto search=d.element(d.body(),"input");d.attribute(search,"id","explorer-search");
  auto layers_tab=d.element(d.body(),"button");d.attribute(layers_tab,"id","layers-tab");
  auto objects_tab=d.element(d.body(),"button");d.attribute(objects_tab,"id","objects-tab");
  kestrel::drawing model;unsigned changes=0;
  {
    kestrel::layer_panel panel(d,model,[&]{++changes;});
    require(d.text_content(inspector).find("No selection")!=std::string::npos);
    require(d.text_content(inspector).find("Millimeters")!=std::string::npos);
    require(d.children(icon).size()==1);
    require(d.attribute(d.children(icon)[0],"viewBox")=="0 0 24 24");
    require(panel.entries().size()==model.data["layers"].size());
    require(d.text_content(tabs).find("Untitled")!=std::string::npos);
    require(d.text_content(title)=="Untitled" && d.text_content(selected_status)=="No selection");
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
    require(d.text_content(tabs).find("·")!=std::string::npos);
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
    model.data["layers"][2]["locked"]=true;
    model.selection={"obsolete"};model.select_all_editable();
    require(model.selection.size()==1 && model.selection.contains("visible"));
    model.data["layers"][2]["locked"]=false;
    model.selection={"other"};
    const auto current=model.data["currentLayer"];
    d.dispatch(panel.entries()[1].row,"click",0,0,0,0,{},0,{true});
    require(model.selection.size()==1 && model.selection.contains("visible"));
    require(model.data["currentLayer"]==current);
    require(d.text_content(selected_status)=="1 selected");
    model.data["layers"][1]["visible"]=false;
    d.dispatch(panel.entries()[1].row,"click",0,0,0,0,{},0,{true});
    require(model.selection.empty());
    auto obsolete=panel.entries()[0].row;
    panel.refresh();
    bool removed=false;try {d.bounds(obsolete);} catch(const std::invalid_argument&) {removed=true;}
    require(removed);
    model.data["entities"]={
      {{"id","line_1"},{"type","LINE"},{"layer","architecture"},{"name","Wall <A>"}},
      {{"id","mesh_2"},{"type","MESH"},{"layer","openings"},{"primitive","Box"}}};
    d.dispatch(objects_tab,"click");
    require(panel.entries().size()==2 && d.attribute(objects_tab,"class")=="active");
    require(d.attribute(search,"placeholder")=="Filter objects…");
    require(d.attribute(panel.entries()[0].row,"data-object")=="line_1");
    d.dispatch(panel.entries()[0].row,"click");require(model.selection.size()==1 && model.selection.contains("line_1"));
    d.dispatch(panel.entries()[1].row,"click",0,0,0,0,{},0,{true});require(model.selection.size()==2);
    d.dispatch(panel.entries()[0].row,"click",0,0,0,0,{},0,{true});require(!model.selection.contains("line_1"));
    d.set_value(search,"a-wall");d.dispatch(search,"input");require(panel.entries().size()==1);
    d.set_value(search,"no match");d.dispatch(search,"input");require(panel.entries().empty());
    d.set_value(search,"");
    for(unsigned i=0;i<501;++i) model.data["entities"].push_back({{"id","point_"+std::to_string(i)},{"type","POINT"},{"layer","architecture"}});
    panel.refresh();require(panel.entries().size()==500);
    const auto& limited=d.render(300,300);
    require(std::string(limited.bytes.begin(),limited.bytes.end()).find("Showing the first 500 of 503 objects.")!=std::string::npos);
    d.dispatch(layers_tab,"click");require(panel.entries().size()==8);
    require(d.attribute(search,"placeholder")=="Filter layers…");
    d.render(300,300);
  }
  require(d.children(inspector).empty());
  require(d.children(icon).empty());
  d.dispose();
  std::cout<<"Original layer templates and native state/handlers passed\n";
}
