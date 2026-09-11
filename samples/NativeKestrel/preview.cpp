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
static bool exercise_failure=false;
static bool exercise_layer_filter=false;
static bool exercise_objects=false;
static bool exercise_navigation=false;
static bool benchmark_pan=false;
static bool benchmark_courtyard=false;
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
import kestrel.examples;
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
  bool navigation_exercised{};
  uint64_t navigation_serial{};
  unsigned pan_samples{}, resize_samples{}, missing_resize_images{};
  uint32_t published_width{}, published_height{};
  uint64_t pan_initial_serial{}, pan_initial_builds{};
  foco::compositor_diagnostic_metrics pan_initial_compositor;
  foco::scene_publication_metrics pan_initial_publication;
  std::chrono::steady_clock::time_point pan_start;
  double pan_tick_ms{}, pan_tick_max_ms{};
  bool gpu_dirty{true}, panning{}, orbiting{};
  float pan_x{}, pan_y{};
  void fit_drawing() {
    if(!viewport)return;
    std::vector<kestrel::vec3> points;
    for(const auto& entity:model.data["entities"]) if(model.visible(entity)) {
      const auto geometry=kestrel::geo::geometry(entity,1);
      points.insert(points.end(),geometry.points.begin(),geometry.points.end());
    }
    viewport->camera.fit(points);gpu_dirty=true;
  }
  void tick() {
    auto node = view->document.find("scene");
    auto bounds = view->document.bounds(node);
    if (bounds.width < 1 || bounds.height < 1) return;
    auto w = static_cast<uint32_t>(bounds.width);
    auto h = static_cast<uint32_t>(bounds.height);
    if (!viewport) {
      viewport = std::make_unique<kestrel::viewport>(node, w, h);
      viewport->options.style = (benchmark_pan && !benchmark_courtyard) || exercise_objects || exercise_layer_filter
          ? kestrel::display_style::shaded_edges : kestrel::display_style::wireframe;
      if((!benchmark_pan || benchmark_courtyard) && !exercise_objects && !exercise_layer_filter) {
        fit_drawing();
      }
    }
    if (w != gpu_width || h != gpu_height) {
      viewport->resize(w, h);
      if(benchmark_canvas_resize && pan_samples) ++resize_samples;
      gpu_width = w; gpu_height = h; gpu_dirty = true;
    }
    const auto publish = [&] {
      if (auto image = viewport->poll(true)) {
        const auto metadata=image->value.describe();
        if(metadata.width!=w || metadata.height!=h)
          throw std::runtime_error("GPU image dimensions do not match the current canvas");
        published_width=metadata.width;published_height=metadata.height;
        ++gpu_serial;
        if(gpu_serial==1) if(auto label=view->document.find("engine-label"))
          view->document.set_text(label,"WebGPU");
        const auto overlay=view->document.find("overlay");
        view->document.attribute(overlay,"width",std::to_string(w));
        view->document.attribute(overlay,"height",std::to_string(h));
        view->document.clear_canvas(overlay);
        for(const auto& item:viewport->scene_content().texts) {
          const auto& text=item.text;
          const auto position=kestrel::geo::point(text.at("position"));
          const auto p=viewport->camera.project(position);
          if(p.z<0 || p.z>1 || p.x < -500 || p.x>w+500 || p.y < -200 || p.y>h+200)continue;
          const auto axes=kestrel::geo::axes_for_text(text);
          const auto height=text.value("height",10.0);
          const auto q=viewport->camera.project(position+axes.x*height);
          const auto r=viewport->camera.project(position+axes.y*height);
          const auto pixels=std::hypot(q.x-p.x,q.y-p.y);
          if(pixels<2 || pixels>2000)continue;
          double xx=(q.x-p.x)/pixels,xy=(q.y-p.y)/pixels,yx=-(r.x-p.x)/pixels,yy=-(r.y-p.y)/pixels;
          if(std::abs(xx*yy-yx*xy)<.015)continue;
          if(item.dimension && xx<0) {xx=-xx;xy=-xy;yx=-yx;yy=-yy;}
          uint32_t color=0;for(float channel:item.color) color=(color<<8)|uint32_t(std::clamp(channel,0.f,1.f)*255+.5f);
          const auto content=text.value("text",std::string{});
          size_t start=0;unsigned line=0;
          do {
            const auto end=content.find('\n',start);
            view->document.fill_text(overlay,content.substr(start,end==std::string::npos?end:end-start),0,float(line++*pixels*1.35),
                std::to_string(pixels)+"px \"Segoe UI\", Arial, sans-serif",color,text.value("align",std::string("left")),"alphabetic",{xx,xy,yx,yy,p.x,p.y});
            if(end==std::string::npos)break;start=end+1;
          } while(start<=content.size());
        }
        view->set_gpu_image(node, w, h, gpu_serial,
            webscene::foco_host::make_gpu_image(node, gpu_serial, std::move(image)));
      }
    };
    publish();
    if (gpu_dirty && viewport->submit(model)) {
      gpu_dirty = false;
      publish(); // Foco waits on the retained producer fences before sampling.
    }
    if(benchmark_canvas_resize && pan_samples &&
        (published_width!=w || published_height!=h)) ++missing_resize_images;
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
    if((!benchmark_pan || benchmark_courtyard) && !exercise_objects && !exercise_layer_filter)
      kestrel::load_courtyard(model);
    else model.add("MESH", kestrel::geo::box({-50, -40, 0}, 100, 80, 60));
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
            else if (*action == "fit") fit_drawing();
            else if (*action == "zoomin") viewport->camera.zoom_at(1.35);
            else if (*action == "zoomout") viewport->camera.zoom_at(1.0/1.35);
            else if (*action == "grid") viewport->grid_enabled = !viewport->grid_enabled;
            else if (*action == "selectall" || *action == "clear-selection") {
              if(*action=="selectall") model.select_all_editable();else model.selection.clear();
#ifdef KESTREL_PREVIEW_SHARED_CSS
              if(layers) layers->refresh();
#endif
              view->refresh();
            }
            else if (*action == "erase" || *action == "undo" || *action == "redo") {
              if(*action=="erase") model.erase_selected();
              else if(*action=="undo") model.undo();else model.redo();
#ifdef KESTREL_PREVIEW_SHARED_CSS
              if(layers) layers->refresh();
#endif
              view->refresh();
            }
            else if (action->starts_with("view-")) viewport->camera.set_view(action->substr(5));
            else return;
            gpu_dirty = true;
            event.prevent_default();
            return;
          }
        }));
    handlers.push_back(view->document.on(view->document.find("view-select"),"change",
        [this](auto&) {
          if(!viewport)return;
          viewport->camera.set_view(view->document.value(view->document.find("view-select")));
          gpu_dirty=true;
        }));
    handlers.push_back(view->document.on(view->document.find("style-select"),"change",
        [this](auto&) {
          if(!viewport)return;
          const auto style=view->document.value(view->document.find("style-select"));
          if(style=="wireframe") viewport->options.style=kestrel::display_style::wireframe;
          else if(style=="shaded") viewport->options.style=kestrel::display_style::shaded;
          else if(style=="shaded-edges") viewport->options.style=kestrel::display_style::shaded_edges;
          else if(style=="xray") viewport->options.style=kestrel::display_style::xray;
          else return;
          gpu_dirty=true;
        }));
    handlers.push_back(view->document.on(view->document.find("viewport"), "pointerdown",
        [this](auto &event) {
          if (!viewport || !(event.buttons & 4u)) return;
          panning = true; orbiting = event.modifiers.shift; pan_x = event.client_x; pan_y = event.client_y;
          event.prevent_default();
        }));
    handlers.push_back(view->document.on(view->document.root(), "pointermove",
        [this](auto &event) {
          if (!panning || !viewport) return;
          if (!(event.buttons & 4u)) { panning = false; return; }
          if(orbiting) {
            viewport->camera.orbit(event.client_x - pan_x, event.client_y - pan_y);
            view->document.set_value(view->document.find("view-select"),"iso");
          } else viewport->camera.pan(event.client_x - pan_x, event.client_y - pan_y);
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
        if(exercise_failure) throw std::runtime_error("Requested preview failure exercise");
        if(benchmark_pan && viewport) {
          if(!pan_samples) {
            pan_start=std::chrono::steady_clock::now();
            pan_initial_serial=gpu_serial;pan_initial_builds=viewport->scene_build_count();
            if(auto* publisher=view->composition()) {pan_initial_compositor=publisher->compositor_diagnostics();pan_initial_publication=publisher->metrics();}
          }
          viewport->camera.pan(benchmark_courtyard && (pan_samples/60)%2 ? -2 : 2,0);gpu_dirty=true;
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
        if(exercise_navigation && viewport && gpu_serial && !navigation_exercised) {
          const auto area=view->document.bounds(view->document.find("scene"));
          const auto host=view->bounds();
          const float x=host.x+area.x+area.width/2, y=host.y+area.y+area.height/2;
          auto send=[&](foco::pointer_event_kind kind,float dx,float dy,uint32_t buttons,bool shift) {
            foco::pointer_event event;event.kind=kind;event.position={x+dx,y+dy};
            event.buttons=buttons;
            if(shift) event.modifiers=foco::key_modifiers::shift;
            view->pointer_event_received(event);
          };
          const auto yaw=viewport->camera.yaw;
          const auto target=viewport->camera.target;
          send(foco::pointer_event_kind::pressed,0,0,4,true);
          send(foco::pointer_event_kind::moved,40,-30,4,false);
          if(std::abs(viewport->camera.yaw-(yaw-.28))>1e-10 ||
             (viewport->camera.target-target).length()>1e-10)
            throw std::runtime_error("Hosted Shift+middle orbit failed");
          send(foco::pointer_event_kind::released,40,-30,0,false);
          const auto revision=viewport->camera.revision;
          send(foco::pointer_event_kind::moved,60,-40,0,false);
          if(viewport->camera.revision!=revision)
            throw std::runtime_error("Navigation continued after release");
          send(foco::pointer_event_kind::pressed,0,0,4,false);
          send(foco::pointer_event_kind::moved,15,10,4,true);
          if((viewport->camera.target-target).length()<1e-8 ||
             std::abs(viewport->camera.yaw-(yaw-.28))>1e-10)
            throw std::runtime_error("Hosted middle pan failed");
          send(foco::pointer_event_kind::cancelled,15,10,0,false);
          const auto cancelled_revision=viewport->camera.revision;
          send(foco::pointer_event_kind::moved,30,20,4,false);
          if(viewport->camera.revision!=cancelled_revision)
            throw std::runtime_error("Navigation continued after cancellation");
          const auto view_select=view->document.find("view-select");
          const auto press_key=[&](foco::key key) {
            foco::key_event event;event.value=key;view->key_event_received(event);
            if(!event.handled) throw std::runtime_error("Hosted select key was not handled");
          };
          view->document.focus(view_select);
          if(view->document.focused()!=view_select) throw std::runtime_error("View select could not receive focus");
          press_key(foco::key::home);press_key(foco::key::down);press_key(foco::key::down);
          if(std::abs(viewport->camera.pitch)>1e-10)
            throw std::runtime_error("View dropdown did not update native camera");
          press_key(foco::key::up);
          if(view->document.value(view_select)!="iso") throw std::runtime_error("View select keyboard navigation failed");
          const auto style_select=view->document.find("style-select");
          view->document.focus(style_select);press_key(foco::key::home);press_key(foco::key::down);
          if(viewport->options.style!=kestrel::display_style::shaded_edges)
            throw std::runtime_error("Style dropdown did not update native renderer");
          navigation_serial=gpu_serial;navigation_exercised=true;
        }
        const auto tick_start=std::chrono::steady_clock::now();
        tick();
        if(exercise_navigation && navigation_exercised && gpu_serial>navigation_serial) {
          std::cout << "Hosted navigation passed; updated GPU image published\n";
          exercise_navigation=false;
          if(capture_path.empty()) {window->close();lifetime->shutdown(0);return;}
        }
        if(benchmark_pan && pan_samples) {
          const auto milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-tick_start).count();
          pan_tick_ms+=milliseconds;pan_tick_max_ms=std::max(pan_tick_max_ms,milliseconds);
          if(pan_samples==360) {
            const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-pan_start).count();
            std::cout<<(benchmark_canvas_resize?"Canvas resize pipeline: seconds=":"Pan pipeline: seconds=")<<seconds<<" host_ticks="<<pan_samples
                <<" entities="<<model.data["entities"].size()
                <<" text_records="<<viewport->scene_content().texts.size()
                <<" published_images="<<(gpu_serial-pan_initial_serial)
                <<" images_per_second="<<(gpu_serial-pan_initial_serial)/seconds
                <<" tick_mean_ms="<<pan_tick_ms/pan_samples<<" tick_max_ms="<<pan_tick_max_ms
                <<" scene_rebuilds="<<(viewport->scene_build_count()-pan_initial_builds)
                <<" (publication timing, not display presentation timing)\n";
            if(benchmark_canvas_resize) {
              std::cout<<"Resize validation: dimension_changes="<<resize_samples
                  <<" ticks_without_matching_image="<<missing_resize_images<<'\n';
              if(resize_samples<300 || missing_resize_images)
                throw std::runtime_error("Canvas resize workload did not sustain matching GPU images");
            }
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
        if(!capture_path.empty() && ++ticks==(benchmark_canvas_resize?61U:120U)) {
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
    else if(std::string_view(argv[i])=="--exercise-failure") exercise_failure=true;
    else if(std::string_view(argv[i])=="--exercise-navigation") exercise_navigation=true;
    else if(std::string_view(argv[i])=="--exercise-objects") exercise_objects=true;
    else if(std::string_view(argv[i])=="--benchmark-pan") benchmark_pan=true;
    else if(std::string_view(argv[i])=="--benchmark-courtyard") {benchmark_pan=true;benchmark_courtyard=true;}
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
