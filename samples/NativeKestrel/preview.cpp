#include "native_web_view.hpp"
#include <foco/app_builder.hpp>
#include <iostream>
#include <array>
#include <functional>
#include <cmath>
#include <algorithm>
#ifdef KESTREL_PREVIEW_GPU
#include "native_gpu_image.hpp"
#include "native_webgpu_surface.h"
#include "third_party/nlohmann/json.hpp"
#include <memory>
#endif
import kestrel.original.preview;
import kestrel.preview.tabs;
import kestrel.preview.groups;
#ifdef KESTREL_PREVIEW_GPU
import kestrel.viewport;
#endif
class preview_window final : public foco::window {
public:
  std::function<void()> frame;
  bool requires_host_frames() const noexcept override { return bool(frame); }
  bool advance_host_frame(double) override { if (frame) frame(); return false; }
};
class preview_app final : public foco::application {
  foco::ref<preview_window> window;
  foco::ref<webscene::foco_host::view> view;
  std::array<webscene::native_web::node_id,6> tabs{};
  std::vector<webscene::native_web::subscription> handlers;
  std::vector<webscene::native_web::node_id> ribbon_roots;
#ifdef KESTREL_PREVIEW_GPU
  kestrel::drawing model;
  std::unique_ptr<kestrel::viewport> viewport;
  uint32_t gpu_width{}, gpu_height{};
  uint64_t gpu_serial{};
  bool gpu_dirty{true}, panning{};
  float pan_x{}, pan_y{};
  void tick() {
    auto node = view->document.find("scene");
    auto bounds = view->document.bounds(node);
    if (bounds.width < 1 || bounds.height < 1) return;
    auto w = static_cast<uint32_t>(bounds.width);
    auto h = static_cast<uint32_t>(bounds.height);
    if (!viewport) {
      viewport = std::make_unique<kestrel::viewport>(node, w, h);
      viewport->options.style = kestrel::display_style::shaded_edges;
    }
    if (w != gpu_width || h != gpu_height) {
      viewport->resize(w, h);
      gpu_width = w; gpu_height = h; gpu_dirty = true;
    }
    if (auto image = viewport->poll()) {
      ++gpu_serial;
      view->set_gpu_image(node, w, h, gpu_serial,
          webscene::foco_host::make_gpu_image(node, gpu_serial, std::move(image)));
    }
    if (gpu_dirty && viewport->submit(model)) gpu_dirty = false;
  }
#endif
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
  ~preview_app() { if (window) window->frame = {}; }
  foco::result<void> started(foco::application_lifetime &base) override {
    auto *lifetime = dynamic_cast<foco::windowed_application_lifetime *>(&base);
    if (!lifetime) return foco::error{foco::error_code::invalid_argument,"Desktop required"};
    window = foco::make_ref<preview_window>();
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
#ifdef KESTREL_PREVIEW_GPU
    // renderer.js sizes both canvases to the viewport during resize. Keep that
    // application behavior in native code; the original stylesheet is unchanged.
    for (auto id : {"scene", "overlay"}) {
      webscene::native_web::rule sizing;
      sizing.inline_target = view->document.find(id);
      sizing.declarations.push_back({false, +[](webscene::native_web::style &style) {
        style.set_width({100, webscene::native_web::length_unit::percent});
        style.set_height({100, webscene::native_web::length_unit::percent});
      }});
      view->document.add_rule(std::move(sizing));
    }
    view->refresh();
    model.add("MESH", kestrel::geo::box({-50, -40, 0}, 100, 80, 60));
    handlers.push_back(view->document.on(view->document.find("viewport"), "pointerdown",
        [this](auto &event) {
          if (!viewport || !(event.buttons & 4u)) return;
          panning = true; pan_x = event.client_x; pan_y = event.client_y;
          event.prevent_default();
        }));
    handlers.push_back(view->document.on(view->document.root(), "pointermove",
        [this](auto &event) {
          if (!panning || !viewport) return;
          if (!(event.buttons & 4u)) { panning = false; return; }
          viewport->camera.pan(event.client_x - pan_x, event.client_y - pan_y);
          pan_x = event.client_x; pan_y = event.client_y;
          gpu_dirty = true;
        }));
    for (auto type : {"pointerup", "pointercancel"})
      handlers.push_back(view->document.on(view->document.root(), type,
          [this](auto &) { panning = false; }));
    handlers.push_back(view->document.on(view->document.find("viewport"), "wheel",
        [this](auto &event) {
          if (!viewport) return;
          auto bounds = view->document.bounds(view->document.find("scene"));
          viewport->camera.zoom_at(
              std::exp(std::clamp(-double(event.delta_y) * .0014, -.6, .6)),
              event.client_x - bounds.x, event.client_y - bounds.y);
          gpu_dirty = true;
          event.prevent_default();
        }));
    window->frame = [this, lifetime] {
      try { tick(); }
      catch (const std::exception &error) {
        std::cerr << "Native preview viewport: " << error.what() << '\n';
        lifetime->shutdown(5);
      }
    };
#endif
    return window->show(*lifetime);
  }
};
int main(int argc, char **argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--check-input-coalescing") {
    auto view = foco::make_ref<webscene::foco_host::view>();
    auto node = view->document.element(view->document.body(), "button");
    view->document.set_text(node, "Input");
    view->document.render(800, 600);
    unsigned delivered = 0;
    auto handler = view->document.on(node, "pointermove", [&](auto &) { ++delivered; });
    auto bounds = view->document.bounds(node);
    foco::pointer_event event;
    event.kind = foco::pointer_event_kind::moved;
    event.position = {bounds.x + 1, bounds.y + 1};
    for (int i = 0; i < 8; ++i) view->pointer_event_received(event);
    if (delivered != 8 || !view->requires_host_frames()) return 2;
    view->advance_host_frame(0);
    if (view->requires_host_frames()) return 3;
    std::cout << "Input delivered immediately; pending host refresh drained\n";
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--dump-layout") {
    webscene::native_web::document document;
    compiled_ui::build(document);
    for (auto size : {std::pair{1280.f, 800.f}, std::pair{1280.f, 1000.f}}) {
      document.render(size.first, size.second);
      std::cout << "viewport " << size.first << "x" << size.second << '\n';
      for (auto id : {"shell", "titlebar", "ribbon-tabs", "ribbon", "documentbar",
                      "workbench", "command-dock", "statusbar"}) {
        auto node = document.find(id);
        if (!node) { std::cout << id << " missing\n"; continue; }
        auto bounds = document.bounds(node);
        std::cout << id << " " << bounds.x << "," << bounds.y << " "
                  << bounds.width << "x" << bounds.height << '\n';
      }
    }
    return 0;
  }
  auto result = foco::AppBuilder::Configure<preview_app>().WithSkia().WithCocoa()
      .TryStartWithClassicDesktopLifetime(argc, argv);
  if (!result) { std::cerr << result.failure().message; return 1; }
  return result.value();
}
