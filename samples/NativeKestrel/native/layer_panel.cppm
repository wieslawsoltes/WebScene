module;
#include "../third_party/nlohmann/json.hpp"
#include <webscene/native_web.hpp>
#include <functional>
#include <algorithm>
#include <string>
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
