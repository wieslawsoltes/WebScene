module;
#include "../third_party/nlohmann/json.hpp"
#include <webscene/native_web.hpp>
#include <functional>
#include <algorithm>
#include <string>
#include <sstream>
#include <iomanip>
#include <locale>
#include <cmath>
#include <vector>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif
export module kestrel.layer_panel;
import kestrel.drawing;
import kestrel.layer_templates;
export namespace kestrel {
class layer_panel {
  webscene::native_web::document& document_;
  drawing& model_;
  std::function<void()> changed_;
  std::vector<webscene::native_web::subscription> handlers_;
public:
  struct entry {std::string id;webscene::native_web::node_id row,visibility,lock;};
private:
  std::vector<entry> rows_;
  std::vector<webscene::native_web::node_id> inspector_roots_;
  std::vector<webscene::native_web::node_id> shell_icons_;
  webscene::native_web::subscription filter_handler_;
  std::vector<webscene::native_web::subscription> tab_handlers_;
  bool objects_{};
  webscene::native_web::node_id empty_{}, document_tab_{};
  static std::string lowercase(std::string text) {
#ifdef __APPLE__
    auto source=CFStringCreateWithBytes(kCFAllocatorDefault,reinterpret_cast<const UInt8*>(text.data()),text.size(),kCFStringEncodingUTF8,false);
    if(!source) throw std::invalid_argument("invalid UTF-8 layer filter");
    auto lower=CFStringCreateMutableCopy(kCFAllocatorDefault,0,source);CFRelease(source);
    if(!lower) throw std::bad_alloc();
    struct release_string {CFMutableStringRef value;~release_string(){CFRelease(value);}} owned{lower};
    CFStringLowercase(lower,nullptr);
    auto length=CFStringGetLength(lower);
    text.resize(CFStringGetMaximumSizeForEncoding(length,kCFStringEncodingUTF8));
    CFIndex used=0;
    CFStringGetBytes(lower,CFRangeMake(0,length),kCFStringEncodingUTF8,0,false,reinterpret_cast<UInt8*>(text.data()),text.size(),&used);
    text.resize(used);
#else
    // Additional platform Unicode case services remain part of platform expansion.
    for(auto& c:text) if(c>='A' && c<='Z') c+=('a'-'A');
#endif
    return text;
  }
  void clear() {
    handlers_.clear();
    if(!document_.disposed())for(auto node:inspector_roots_)document_.remove(node);
    inspector_roots_.clear();
    if(!document_.disposed()) for(const auto& row:rows_) document_.remove(row.row);
    if(empty_ && !document_.disposed()) document_.remove(empty_);
    empty_=0;
    if(document_tab_ && !document_.disposed()) document_.remove(document_tab_);
    document_tab_=0;
    rows_.clear();
  }
public:
  // Document/model outlive this panel. State belongs to the drawing, not templates.
  layer_panel(webscene::native_web::document& document,drawing& model,std::function<void()> changed)
      :document_(document),model_(model),changed_(std::move(changed)) {
    const auto hydrate=[&](auto&& self,webscene::native_web::node_id node)->void {
      const auto children=document_.children(node);
      if(auto name=document_.attribute(node,"data-icon")) {
        auto icon=kestrel_layers::instantiate(document_,node,"shell-icon-"+*name);
        for(auto child:document_.children(node))
          if(std::find(children.begin(),children.end(),child)==children.end()) shell_icons_.push_back(child);
      }
      for(auto child:children) self(self,child);
    };
    hydrate(hydrate,document_.root());
    if(auto filter=document_.find("explorer-search"))
      filter_handler_=document_.on(filter,"input",[this](auto&){refresh();});
    for(auto [id,objects]:{std::pair{"layers-tab",false},std::pair{"objects-tab",true}})
      if(auto tab=document_.find(id)) tab_handlers_.push_back(document_.on(tab,"click",[this,objects](auto&){
        objects_=objects;refresh();
      }));
    refresh();
  }
  ~layer_panel() {clear();if(!document_.disposed()) for(auto node:shell_icons_) document_.remove(node);}
  const std::vector<entry>& entries() const {return rows_;}
  void refresh() {
    clear();
    if(auto inspector=document_.find("inspector");inspector && model_.selection.empty()) {
      auto header=kestrel_layers::instantiate(document_,inspector,"inspector-empty-header");
      auto general=kestrel_layers::instantiate(document_,inspector,"inspector-empty-general");
      inspector_roots_={header.named("root"),general.named("root")};
      for(const auto& layer:model_.data["layers"]) {
        auto option=kestrel_layers::instantiate(document_,general.named("layer"),"inspector-layer-option");
        document_.attribute(option.named("root"),"value",layer["id"].get<std::string>());
        document_.set_text(option.named("root"),layer["name"].get<std::string>());
      }
      const auto current=model_.data.value("currentLayer",std::string("0"));
      document_.set_value(general.named("layer"),current);
      for(const auto& layer:model_.data["layers"])if(layer["id"]==current)
        document_.set_value(general.named("color"),layer.value("color",std::string("#dce4ed")));
      const auto units=model_.data.value("units",std::string("mm"));
      const auto unit_label=units=="mm"?"Millimeters":units=="cm"?"Centimeters":units=="m"?"Meters":units=="in"?"Inches":units=="ft"?"Feet":units;
      document_.set_text(general.named("units"),unit_label);document_.attribute(general.named("units"),"title",unit_label);
      auto style=document_.find("style-select")?document_.value(document_.find("style-select")):"wireframe";
      std::replace(style.begin(),style.end(),'-',' ');document_.set_text(general.named("style"),style);document_.attribute(general.named("style"),"title",style);
      const auto workspace=document_.find("workspace-select");
      const auto label=workspace && document_.value(workspace)=="3d"?"3D modeling":"2D drafting";
      document_.set_text(general.named("workspace"),label);document_.attribute(general.named("workspace"),"title",label);
      handlers_.push_back(document_.on(general.named("layer"),"change",[this,node=general.named("layer")](auto&) {
        const auto value=document_.value(node);
        for(const auto& layer:model_.data["layers"])if(layer["id"]==value){model_.data["currentLayer"]=value;break;}
        refresh();if(changed_)changed_();
      }));
    }
    if(auto inspector=document_.find("inspector");inspector && !model_.selection.empty()) {
      const auto selected=model_.selected();const auto* one=selected.size()==1?model_.find(selected[0]):nullptr;
      std::string icon="selectall",label=std::to_string(selected.size())+" objects";
      if(one) {
        const auto type=one->value("type",std::string{});icon="drawing";label=type;
        if(type=="MESH") {icon="box";label=one->value("primitive",std::string("Mesh solid"));if(label.empty())label="Mesh solid";}
        for(auto [key,title]:{std::pair{"LINE","Line"},std::pair{"POLYLINE","Polyline"},std::pair{"CIRCLE","Circle"},
            std::pair{"ARC","Arc"},std::pair{"ELLIPSE","Ellipse"},std::pair{"SPLINE","Spline"},std::pair{"TEXT","Text"},
            std::pair{"DIMENSION","Aligned dimension"},std::pair{"HATCH","Hatch"},std::pair{"POINT","Point"}})
          if(type==key){icon=lowercase(type);label=title;break;}
      }
      auto header=kestrel_layers::instantiate(document_,inspector,"inspector-header-"+icon);
      auto general=kestrel_layers::instantiate(document_,inspector,"inspector-selected-layer");
      inspector_roots_={header.named("root"),general.named("root")};
      document_.set_text(header.named("title"),label);
      const auto handle=one?one->value("sourceHandle",std::string{}):std::string{};
      document_.set_text(header.named("handle"),handle.empty()?"⌄":"#"+handle);
      if(!one) {
        auto option=kestrel_layers::instantiate(document_,general.named("layer"),"inspector-layer-option");
        document_.attribute(option.named("root"),"value","");document_.set_text(option.named("root"),"Multiple / choose…");
      }
      for(const auto& layer:model_.data["layers"]) {
        auto option=kestrel_layers::instantiate(document_,general.named("layer"),"inspector-layer-option");
        document_.attribute(option.named("root"),"value",layer["id"].get<std::string>());document_.set_text(option.named("root"),layer["name"].get<std::string>());
      }
      document_.set_value(general.named("layer"),one?one->value("layer",std::string{}):std::string{});
      document_.set_value(general.named("linetype"),one?one->value("linetype",std::string("ByLayer")):"ByLayer");
      std::ostringstream weight;weight.imbue(std::locale::classic());
      weight<<std::fixed<<std::setprecision(4)<<(one?one->value("lineweight",0.0):0.0);
      auto weight_text=weight.str();
      while(weight_text.ends_with('0'))weight_text.pop_back();
      if(weight_text.ends_with('.'))weight_text.pop_back();
      if(weight_text=="-0")weight_text="0";
      document_.set_value(general.named("lineweight"),weight_text);
      for(auto key:{"linetype","lineweight"}) {
        const auto node=general.named(key);
        handlers_.push_back(document_.on(node,"change",[this,node,key=std::string(key)](auto&) {
          const auto value=document_.value(node);
          if(key=="lineweight") {
            try {size_t used=0;const auto number=std::stod(value,&used);
              if(used==value.size())model_.change_selected_appearance(key,number);
            } catch(const std::exception&) {}
          } else model_.change_selected_appearance(key,value);
          refresh();if(changed_)changed_();
        }));
      }
      if(one) {
        auto name=kestrel_layers::instantiate(document_,general.named("root"),"inspector-selected-name");
        document_.set_value(name.named("name"),one->value("name",std::string{}));
        handlers_.push_back(document_.on(name.named("name"),"change",[this,node=name.named("name")](auto&) {
          model_.change_selected_appearance("name",document_.value(node));refresh();if(changed_)changed_();
        }));
        const auto read=[&](const std::string& label,const std::string& value) {
          auto row=kestrel_layers::instantiate(document_,general.named("root"),"inspector-readonly");
          document_.set_text(row.named("label"),label);document_.set_text(row.named("value"),value);
          document_.attribute(row.named("value"),"title",value);
        };
        const auto& layer=model_.layer(*one);
        read("Layer state",layer.value("locked",false)?"Locked — read only":layer.value("visible",true)?"Visible / editable":"Hidden");
        const auto group=one->value("group",std::string{});
        if(!group.empty()) {
          auto group_name=one->value("groupName",std::string{});
          read("Group",group_name.empty()?group:group_name);
        }
        const bool point_entity=one->value("type",std::string{})=="POINT" && one->contains("position") && (*one)["position"].is_array();
        const auto entity_type=one->value("type",std::string{});
        const bool conic_entity=(entity_type=="CIRCLE" || entity_type=="ARC" || entity_type=="ELLIPSE") && one->contains("center") && (*one)["center"].is_array();
        const bool single_coordinate=point_entity || conic_entity;
        if(single_coordinate || (one->value("type",std::string{})=="LINE" && one->contains("points") &&
           (*one)["points"].is_array() && (*one)["points"].size()==2 &&
           (*one)["points"][0].is_array() && (*one)["points"][1].is_array())) {
          const auto coordinate=[&](size_t endpoint,size_t axis) {
            const auto& point=point_entity?(*one)["position"]:conic_entity?(*one)["center"]:(*one)["points"][endpoint];
            return axis<point.size() && point[axis].is_number()?point[axis].get<double>():0.0;
          };
          auto geometry=kestrel_layers::instantiate(document_,inspector,"inspector-geometry");
          inspector_roots_.push_back(geometry.named("root"));
          double squared_length=0;
          for(size_t endpoint=0;endpoint<(single_coordinate?1U:2U);++endpoint)for(size_t axis=0;axis<3;++axis) {
            auto row=kestrel_layers::instantiate(document_,geometry.named("root"),"inspector-coordinate");
            const auto label=std::string(point_entity?"Position ":conic_entity?"Center ":endpoint?"End ":"Start ")+"XYZ"[axis];
            const auto property=(point_entity?std::string("position"):conic_entity?std::string("center"):"points."+std::to_string(endpoint))+"."+std::to_string(axis);
            document_.set_text(row.named("label"),label);
            document_.attribute(row.named("input"),"aria-label",label);
            document_.attribute(row.named("input"),"data-prop",property);
            std::ostringstream value;value.imbue(std::locale::classic());value<<std::fixed<<std::setprecision(4)<<coordinate(endpoint,axis);
            auto formatted=value.str();while(formatted.ends_with('0'))formatted.pop_back();if(formatted.ends_with('.'))formatted.pop_back();
            document_.set_value(row.named("input"),formatted=="-0"?"0":formatted);
            handlers_.push_back(document_.on(row.named("input"),"change",[this,node=row.named("input"),endpoint,axis,point_entity,conic_entity](auto&) {
              const auto text=document_.value(node);double number=0;size_t used=0;bool valid=true;
              try {number=std::stod(text,&used);}catch(const std::exception&){valid=false;}
              if(valid && used==text.size()) {
                if(point_entity)model_.change_point_position(axis,number);
                else if(conic_entity)model_.change_conic_center(axis,number);
                else model_.change_line_endpoint(endpoint,axis,number);
              }
              refresh();if(changed_)changed_();
            }));
            if(!single_coordinate && endpoint==0) {const double delta=coordinate(1,axis)-coordinate(0,axis);squared_length+=delta*delta;}
          }
          if(conic_entity && entity_type!="ELLIPSE") {
            auto row=kestrel_layers::instantiate(document_,geometry.named("root"),"inspector-coordinate");
            document_.set_text(row.named("label"),"Radius");document_.attribute(row.named("input"),"aria-label","Radius");
            document_.attribute(row.named("input"),"data-prop","radius");document_.attribute(row.named("input"),"min","0.000001");
            const double radius=one->value("radius",0.0)?one->value("radius",0.0):drawing::conic_x_radius(*one);
            std::ostringstream text;text.imbue(std::locale::classic());text<<std::fixed<<std::setprecision(4)<<radius;
            auto value=text.str();while(value.ends_with('0'))value.pop_back();if(value.ends_with('.'))value.pop_back();
            document_.set_value(row.named("input"),value);
            handlers_.push_back(document_.on(row.named("input"),"change",[this,node=row.named("input")](auto&) {
              const auto text=document_.value(node);double radius=0;size_t used=0;bool valid=true;
              try {radius=std::stod(text,&used);}catch(const std::exception&){valid=false;}
              if(valid && used==text.size())model_.change_conic_radius(radius);
              refresh();if(changed_)changed_();
            }));
          }
          if(!single_coordinate) {
          auto length=kestrel_layers::instantiate(document_,geometry.named("root"),"inspector-readonly");
          std::ostringstream text;text.imbue(std::locale::classic());text<<std::fixed<<std::setprecision(3)<<std::sqrt(squared_length)<<" "<<model_.data.value("units",std::string("mm"));
          document_.set_text(length.named("label"),"Length");document_.set_text(length.named("value"),text.str());document_.attribute(length.named("value"),"title",text.str());
          }
        }
      }
      handlers_.push_back(document_.on(general.named("layer"),"change",[this,node=general.named("layer")](auto&) {
        model_.change_selected_layer(document_.value(node));refresh();if(changed_)changed_();
      }));
    }
    if(auto tabs=document_.find("document-tabs")) {
      auto tab=kestrel_layers::instantiate(document_,tabs,model_.dirty?"document-dirty":"document-clean");
      document_tab_=tab.named("tab");
      const auto name=model_.data.value("name",std::string("Untitled"));
      document_.attribute(document_tab_,"data-doc",model_.data.value("id",std::string("native-active")));
      document_.set_text(tab.named("name"),name);
      document_.attribute(tab.named("close"),"aria-label","Close "+name);
    }
    const auto parent=document_.find("explorer-list");
    if(!parent) throw std::logic_error("layer panel requires explorer-list");
    const auto filter=document_.find("explorer-search");
    const auto query=filter?lowercase(document_.value(filter)):std::string{};
    for(auto [id,active]:{std::pair{"layers-tab",!objects_},std::pair{"objects-tab",objects_}})
      if(auto tab=document_.find(id)) document_.attribute(tab,"class",active?"active":"");
    if(filter) document_.attribute(filter,"placeholder",objects_?"Filter objects…":"Filter layers…");
    if(auto subtitle=document_.find("explorer-subtitle")) document_.set_text(subtitle,objects_?"DRAWING OBJECTS":"LAYER NAME");
    if(objects_) {
      size_t matches=0;
      for(const auto& entity:model_.data["entities"]) {
        const auto id=entity["id"].get<std::string>();
        const auto type=entity.value("type",std::string{});
        const auto name=entity.value("name",std::string{});
        const auto layer=model_.layer(entity).value("name",std::string{});
        if(lowercase(type+" "+name+" "+layer+" "+id).find(query)==std::string::npos) continue;
        if(++matches>500) continue;
        std::string icon="drawing",label=type;
        if(type=="MESH") {icon="box";label=entity.value("primitive",std::string("Mesh solid"));if(label.empty())label="Mesh solid";}
        for(auto [key,title]:{std::pair{"LINE","Line"},std::pair{"POLYLINE","Polyline"},std::pair{"CIRCLE","Circle"},
            std::pair{"ARC","Arc"},std::pair{"ELLIPSE","Ellipse"},std::pair{"SPLINE","Spline"},std::pair{"TEXT","Text"},
            std::pair{"DIMENSION","Aligned dimension"},std::pair{"HATCH","Hatch"},std::pair{"POINT","Point"}})
          if(type==key) {icon=lowercase(type);label=title;break;}
        auto view=kestrel_layers::instantiate(document_,parent,"object-"+icon);
        auto root=view.named("row");rows_.push_back({id,root,0,0});
        document_.attribute(root,"data-object",id);
        document_.attribute(root,"class",model_.selection.contains(id)?"object-row active":"object-row ");
        document_.set_text(view.named("name"),name.empty()?label:name);
        document_.set_text(view.named("layer"),layer);
        const auto split=id.rfind('_');
        document_.set_text(view.named("suffix"),split==std::string::npos?id:id.substr(split+1));
        handlers_.push_back(document_.on(root,"click",[this,id](auto& event){
          if(event.modifiers.shift) {
            if(model_.selection.contains(id)) model_.selection.erase(id);else model_.selection.insert(id);
          } else model_.selection={id};
          refresh();if(changed_)changed_();
        }));
      }
      if(matches>500) {
        empty_=kestrel_layers::instantiate(document_,parent,"object-limit").named("empty");
        document_.set_text(empty_,"Showing the first 500 of "+std::to_string(matches)+" objects. Refine the filter.");
      }
    } else for(const auto& layer:model_.data["layers"]) {
      const auto id=layer["id"].get<std::string>();
      const auto name=layer["name"].get<std::string>();
      if(lowercase(name).find(query)==std::string::npos) continue;
      const bool visible=layer.value("visible",true),locked=layer.value("locked",false);
      auto view=kestrel_layers::instantiate(document_,parent,
          std::string(visible?"visible":"hidden")+(locked?"-locked":"-unlocked"));
      auto root=view.named("row"),visibility=view.named("visibility"),lock=view.named("lock");
      rows_.push_back({id,root,visibility,lock});
      document_.attribute(root,"data-layer",id);
      document_.attribute(root,"class",std::string("layer-row ")+(model_.data["currentLayer"]==id?"current ":"")+(visible?"":"off"));
      document_.attribute(view.named("color"),"style","background:"+layer["color"].get<std::string>());
      document_.set_text(view.named("name"),name);
      unsigned count=0;for(const auto& entity:model_.data["entities"]) if(entity["layer"]==id) ++count;
      document_.set_text(view.named("count"),count?std::to_string(count):"—");
      for(auto [node,verb]:{std::pair{visibility,visible?"Hide":"Show"},std::pair{lock,locked?"Unlock":"Lock"}}) {
        document_.attribute(node,"title",std::string(verb)+" layer");
        document_.attribute(node,"aria-label",std::string(verb)+" "+name);
      }
      handlers_.push_back(document_.on(root,"click",[this,id](auto& event) {
        if(event.modifiers.shift) {
          model_.selection.clear();
          for(const auto& entity:model_.data["entities"])
            if(entity["layer"]==id && model_.visible(entity)) model_.selection.insert(entity["id"].get<std::string>());
        } else model_.data["currentLayer"]=id;
        refresh();if(changed_) changed_();
      }));
      for(auto [node,property]:{std::pair{visibility,"visible"},std::pair{lock,"locked"}})
        handlers_.push_back(document_.on(node,"click",[this,id,property](auto& event) {
          event.stop_propagation();
          model_.transaction(property==std::string_view("visible")?"Toggle visibility":"Toggle lock",[&] {
            for(auto& layer:model_.data["layers"]) if(layer["id"]==id) layer[property]=!layer[property].get<bool>();
          });
          refresh();if(changed_) changed_();
        }));
    }
    if(!objects_ && rows_.empty()) empty_=kestrel_layers::instantiate(document_,parent,"empty").named("empty");
    const auto text=[&](const char* id,std::string value) {
      if(auto node=document_.find(id)) document_.set_text(node,std::move(value));
    };
    text("layer-count",std::to_string(model_.data["layers"].size()));
    text("object-count",std::to_string(model_.data["entities"].size()));
    text("summary-entities",std::to_string(model_.data["entities"].size()));
    text("summary-selected",std::to_string(model_.selection.size()));
    text("summary-name",model_.data.value("name",std::string("Untitled")));
    text("title-name",model_.data.value("name",std::string("Untitled")));
    text("selection-status",model_.selection.empty()?"No selection":std::to_string(model_.selection.size())+" selected");
    text("units-status",model_.data.value("units",std::string("mm")));
    text("summary-units",model_.data.value("units",std::string("mm")));
  }
};
}
