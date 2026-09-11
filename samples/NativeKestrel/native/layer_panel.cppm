module;
#include "../third_party/nlohmann/json.hpp"
#include <webscene/native_web.hpp>
#include <functional>
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
  webscene::native_web::subscription filter_handler_;
  webscene::native_web::node_id empty_{};
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
    rows_.clear();
  }
public:
  // Document/model outlive this panel. State belongs to the drawing, not templates.
  layer_panel(webscene::native_web::document& document,drawing& model,std::function<void()> changed)
      :document_(document),model_(model),changed_(std::move(changed)) {
    if(auto filter=document_.find("explorer-search"))
      filter_handler_=document_.on(filter,"input",[this](auto&){refresh();});
    refresh();
  }
  ~layer_panel() {clear();}
  const std::vector<entry>& entries() const {return rows_;}
  void refresh() {
    clear();
    const auto parent=document_.find("explorer-list");
    if(!parent) throw std::logic_error("layer panel requires explorer-list");
    const auto filter=document_.find("explorer-search");
    const auto query=filter?lowercase(document_.value(filter)):std::string{};
    for(const auto& layer:model_.data["layers"]) {
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
    if(rows_.empty()) empty_=kestrel_layers::instantiate(document_,parent,"empty").named("empty");
    const auto text=[&](const char* id,std::string value) {
      if(auto node=document_.find(id)) document_.set_text(node,std::move(value));
    };
    text("layer-count",std::to_string(model_.data["layers"].size()));
    text("object-count",std::to_string(model_.data["entities"].size()));
    text("summary-entities",std::to_string(model_.data["entities"].size()));
    text("summary-selected",std::to_string(model_.selection.size()));
    text("summary-name",model_.data.value("name",std::string("Untitled")));
    text("summary-units",model_.data.value("units",std::string("mm")));
  }
};
}
