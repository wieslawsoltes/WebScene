#include "native_web_view.hpp"
#include <foco/app_builder.hpp>
#include <iostream>
#include <fstream>
#include <chrono>
#include <set>
#ifdef KESTREL_PREVIEW_SHARED_CSS
#include <webscene/shared_css.hpp>
#endif
static std::string capture_path;
static bool exercise_layer_filter=false;
static bool exercise_objects=false;
static bool benchmark_pan=false;
static bool benchmark_large=false;
static bool benchmark_canvas_resize=false;
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
#ifdef KESTREL_PREVIEW_SHARED_CSS
import kestrel.layer_panel;
#endif
#endif
class preview_window final : public foco::window {
public:
  std::function<void()> frame;
  bool requires_host_frames() const noexcept override { return bool(frame); }
  bool advance_host_frame(double) override {
    if (frame) frame();
    return has_pending_scene_changes();
  }
};
class preview_app final : public foco::application {
  foco::ref<preview_window> window;
  foco::ref<webscene::foco_host::view> view;
  std::array<webscene::native_web::node_id,6> tabs{};
  std::vector<webscene::native_web::subscription> handlers;
  std::vector<webscene::native_web::node_id> ribbon_roots;
#ifdef KESTREL_PREVIEW_GPU
  kestrel::drawing model;
#ifdef KESTREL_PREVIEW_SHARED_CSS
  std::unique_ptr<kestrel::layer_panel> layers;
#endif
  std::unique_ptr<kestrel::viewport> viewport;
  uint32_t gpu_width{}, gpu_height{};
  uint64_t gpu_serial{};
  unsigned ticks{};
  unsigned pan_samples{};
  uint64_t pan_initial_serial{}, pan_initial_builds{};
  foco::compositor_diagnostic_metrics pan_initial_compositor;
  foco::scene_publication_metrics pan_initial_publication;
  std::chrono::steady_clock::time_point pan_start;
  double pan_tick_ms{}, pan_tick_max_ms{};
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
    const auto publish = [&] {
      if (auto image = viewport->poll(true)) {
        ++gpu_serial;
        view->set_gpu_image(node, w, h, gpu_serial,
            webscene::foco_host::make_gpu_image(node, gpu_serial, std::move(image)));
      }
    };
    publish();
    if (gpu_dirty && viewport->submit(model)) {
      gpu_dirty = false;
      publish(); // Foco waits on the retained producer fences before sampling.
    }
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
#ifdef KESTREL_PREVIEW_SHARED_CSS
    auto css_report=std::make_shared<webscene::native_web::shared_css_report>();
    compiled_ui::build(view->document,css_report);
#else
    compiled_ui::build(view->document);
#endif
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
#ifdef KESTREL_PREVIEW_SHARED_CSS
    view->document.render(1280,800);
    std::set<std::string> diagnostics;
    for(const auto& entry:css_report->diagnostics)
      diagnostics.insert(entry.feature+": "+entry.classification+": "+entry.detail);
    for(const auto& entry:diagnostics) std::cerr << "Shared CSS: " << entry << '\n';
#endif
    window->add_child(view);
#ifdef KESTREL_PREVIEW_GPU
    // renderer.js sizes both canvases to the viewport during resize. Keep that
    // application behavior in native code; the original stylesheet is unchanged.
    for (auto id : {"scene", "overlay"}) {
#ifdef KESTREL_PREVIEW_SHARED_CSS
      view->document.attribute(view->document.find(id),"style","width:100%;height:100%");
#else
      webscene::native_web::rule sizing;
      sizing.inline_target = view->document.find(id);
      sizing.declarations.push_back({false, +[](webscene::native_web::style &style) {
        style.set_width({100, webscene::native_web::length_unit::percent});
        style.set_height({100, webscene::native_web::length_unit::percent});
      }});
      view->document.add_rule(std::move(sizing));
#endif
    }
    view->refresh();
    model.add("MESH", kestrel::geo::box({-50, -40, 0}, 100, 80, 60));
    if(benchmark_large) for(unsigned i=1;i<1000;++i)
      model.add("MESH",kestrel::geo::box({double(i%40)*8-160,double(i/40)*8-100,0},6,6,5));
#ifdef KESTREL_PREVIEW_SHARED_CSS
    layers=std::make_unique<kestrel::layer_panel>(view->document,model,[this]{gpu_dirty=true;view->refresh();});
    if(exercise_layer_filter) {
      view->document.focus(view->document.find("explorer-search"));
      foco::text_input_event input;input.text="a-wall";view->text_input_received(input);
      if(!input.handled || layers->entries().size()!=1 || layers->entries()[0].id!="architecture")
        throw std::runtime_error("native layer filter exercise failed");
    }
    if(exercise_objects) {
      view->document.dispatch(view->document.find("objects-tab"),"click");
      if(layers->entries().size()!=1) throw std::runtime_error("native object row missing");
      const auto id=layers->entries().front().id;
      view->document.dispatch(layers->entries().front().row,"click");
      if(model.selection.size()!=1 || !model.selection.contains(id)
          || view->document.attribute(layers->entries().front().row,"class")!="object-row active")
        throw std::runtime_error("native object selection exercise failed");
    }
    view->refresh();
#endif
    handlers.push_back(view->document.on(view->document.root(), "click",
        [this](auto &event) {
          if (!viewport) return;
          for (auto node = event.target; node; node = view->document.parent(node)) {
            auto action = view->document.attribute(node, "data-action");
            if (!action) continue;
            if (*action == "wireframe") viewport->options.style = kestrel::display_style::wireframe;
            else if (*action == "shaded") viewport->options.style = kestrel::display_style::shaded;
            else if (*action == "xray") viewport->options.style = kestrel::display_style::xray;
            else if (*action == "shaded-edges") viewport->options.style = kestrel::display_style::shaded_edges;
            else if (*action == "zoomin") viewport->camera.zoom_at(1.25);
            else if (*action == "zoomout") viewport->camera.zoom_at(0.8);
            else if (*action == "grid") viewport->grid_enabled = !viewport->grid_enabled;
            else if (action->starts_with("view-")) viewport->camera.set_view(action->substr(5));
            else return;
            gpu_dirty = true;
            event.prevent_default();
            return;
          }
        }));
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
      try {
        if(benchmark_pan && viewport) {
          if(!pan_samples) {
            pan_start=std::chrono::steady_clock::now();
            pan_initial_serial=gpu_serial;pan_initial_builds=viewport->scene_build_count();
            if(auto* publisher=view->composition()) {pan_initial_compositor=publisher->compositor_diagnostics();pan_initial_publication=publisher->metrics();}
          }
          viewport->camera.pan(2,0);gpu_dirty=true;
#ifdef KESTREL_PREVIEW_SHARED_CSS
          if(benchmark_canvas_resize) {
            const auto area=view->document.bounds(view->document.find("viewport"));
            const auto phase=pan_samples%120;
            const auto inset=phase<60?phase:120-phase;
            view->document.attribute(view->document.find("scene"),"style",
                "width:"+std::to_string(std::max(1,int(area.width)-int(inset)*2))+"px;height:"+
                std::to_string(std::max(1,int(area.height)-int(inset)))+"px");
            view->refresh();
          }
#endif
          ++pan_samples;
        }
        const auto tick_start=std::chrono::steady_clock::now();
        tick();
        if(benchmark_pan && pan_samples) {
          const auto milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-tick_start).count();
          pan_tick_ms+=milliseconds;pan_tick_max_ms=std::max(pan_tick_max_ms,milliseconds);
          if(pan_samples==360) {
            const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-pan_start).count();
            std::cout<<(benchmark_canvas_resize?"Canvas resize pipeline: seconds=":"Pan pipeline: seconds=")<<seconds<<" host_ticks="<<pan_samples
                <<" entities="<<model.data["entities"].size()
                <<" published_images="<<(gpu_serial-pan_initial_serial)
                <<" images_per_second="<<(gpu_serial-pan_initial_serial)/seconds
                <<" tick_mean_ms="<<pan_tick_ms/pan_samples<<" tick_max_ms="<<pan_tick_max_ms
                <<" scene_rebuilds="<<(viewport->scene_build_count()-pan_initial_builds)
                <<" (publication timing, not display presentation timing)\n";
            if(auto* publisher=view->composition()) {
              const auto publication=publisher->metrics();
              std::cout<<"Scene publication: commits="<<(publication.commit_attempt_count-pan_initial_publication.commit_attempt_count)
                  <<" publications="<<(publication.publication_count-pan_initial_publication.publication_count)
                  <<" no_ops="<<(publication.no_op_commit_count-pan_initial_publication.no_op_commit_count)<<'\n';
              const auto metrics=publisher->compositor_diagnostics();
              if(metrics.available && pan_initial_compositor.available)
                std::cout<<"Compositor: presented="<<(metrics.presented_frame_count-pan_initial_compositor.presented_frame_count)
                    <<" presented_per_second="<<(metrics.presented_frame_count-pan_initial_compositor.presented_frame_count)/seconds
                    <<" skipped="<<(metrics.skipped_presentation_count-pan_initial_compositor.skipped_presentation_count)
                    <<" occluded="<<metrics.occluded
                    <<" (backend success counts, not physical display timestamps)\n";
              else std::cout<<"Compositor diagnostics unavailable\n";
            } else std::cout<<"Compositor publisher unavailable\n";
            window->close();lifetime->shutdown(0);return;
          }
        }
        if(!capture_path.empty() && ++ticks==120) {
          if(!gpu_serial) {lifetime->shutdown(3);return;}
          auto png=foco::capture_platform_compositor_png(*window,1.f);
          if(!png) {std::cerr << png.failure().message; lifetime->shutdown(2);return;}
          std::ofstream file(capture_path,std::ios::binary);
          file.write(reinterpret_cast<const char*>(png.value().data()),png.value().size());file.close();
          std::cout << "Preview captured; GPU serial=" << gpu_serial << '\n';
          lifetime->shutdown(file?0:4);
        }
      }
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
  for(int i=1;i<argc;++i) {
    if(std::string_view(argv[i])=="--capture" && i+1<argc) capture_path=argv[++i];
    else if(std::string_view(argv[i])=="--exercise-layer-filter") exercise_layer_filter=true;
    else if(std::string_view(argv[i])=="--exercise-objects") exercise_objects=true;
    else if(std::string_view(argv[i])=="--benchmark-pan") benchmark_pan=true;
    else if(std::string_view(argv[i])=="--benchmark-pan-large") {benchmark_pan=true;benchmark_large=true;}
    else if(std::string_view(argv[i])=="--benchmark-canvas-resize") {benchmark_pan=true;benchmark_large=true;benchmark_canvas_resize=true;}
  }
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
