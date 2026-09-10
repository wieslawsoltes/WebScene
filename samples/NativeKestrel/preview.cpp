#include "native_web_view.hpp"
#include <foco/app_builder.hpp>
#include <iostream>
#include <array>
import kestrel.original.preview;
import kestrel.preview.tabs;
import kestrel.preview.groups;
class preview_app final : public foco::application {
  foco::ref<foco::window> window;
  foco::ref<webscene::foco_host::view> view;
  std::array<webscene::native_web::node_id,6> tabs{};
  std::vector<webscene::native_web::subscription> handlers;
  std::vector<webscene::native_web::node_id> ribbon_roots;
  void select_tab(size_t selected) {
    for (auto root : ribbon_roots) view->document.remove(root);
    const std::array<const char*,6> names{"Home","Insert","Annotate","Model","View","Manage"};
    auto group = kestrel_groups::instantiate(view->document, view->document.find("ribbon"), std::string("ribbon-") + names[selected]);
    ribbon_roots = std::move(group.roots);
    for (auto [name, id] : group.references) view->document.attribute(id,"id",std::string(name));
    for (size_t i=0;i<tabs.size();++i) {
      view->document.attribute(tabs[i], "class", i==selected ? "active" : "");
      view->document.attribute(tabs[i], "aria-selected", i==selected ? "true" : "false");
    }
    view->refresh();
  }
public:
  foco::result<void> started(foco::application_lifetime &base) override {
    auto *lifetime = dynamic_cast<foco::windowed_application_lifetime *>(&base);
    if (!lifetime) return foco::error{foco::error_code::invalid_argument,"Desktop required"};
    window = foco::make_ref<foco::window>();
    window->set_title("Kestrel original HTML/CSS — diagnostic preview (unsupported features omitted)");
    window->set_width(1280); window->set_height(800);
    view = foco::make_ref<webscene::foco_host::view>();
    compiled_ui::build(view->document);
    const std::array<const char*,6> names{"Home","Insert","Annotate","Model","View","Manage"};
    auto parent = view->document.find("ribbon-tab-list");
    for(size_t i=0;i<tabs.size();++i) {
      auto tab = kestrel_tabs::instantiate(view->document,parent,"ribbon-tab");
      tabs[i] = tab.named("tab");
      view->document.set_text(tabs[i],names[i]);
      view->document.attribute(tabs[i],"data-ribbon",names[i]);
      handlers.push_back(view->document.on(tabs[i],"click",[this,i](auto&){select_tab(i);}));
    }
    select_tab(0);
    window->add_child(view);
    return window->show(*lifetime);
  }
};
int main(int argc, char **argv) {
  auto result = foco::AppBuilder::Configure<preview_app>().WithSkia().WithCocoa()
      .TryStartWithClassicDesktopLifetime(argc, argv);
  if (!result) { std::cerr << result.failure().message; return 1; }
  return result.value();
}
