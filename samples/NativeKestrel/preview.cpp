#include "native_web_view.hpp"
#include <foco/app_builder.hpp>
#include <foco/controls.hpp>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <chrono>
#include <set>
#include <sstream>
#ifdef KESTREL_PREVIEW_SHARED_CSS
#include <webscene/shared_css.hpp>
#endif
static std::string capture_path;
static bool exercise_failure=false;
static bool exercise_layer_filter=false;
static bool exercise_objects=false;
static bool exercise_navigation=false;
static bool exercise_theme=false;
static bool exercise_picking=false;
static bool exercise_layer_edit=false;
static bool exercise_line_edit=false;
static bool exercise_line_draw=false;
static bool show_line_preview=false;
static bool exercise_color_picker=false;
static bool show_color_picker=false;
static bool show_group_dialog=false;
static bool exercise_group_dialog=false;
static bool exercise_shortcuts=false;
static bool exercise_drag_selection=false;
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
  kestrel::line_command line_tool{model};
  bool line_active{};
  bool drafting_shift{};
  std::optional<kestrel::vec3> line_pointer;
  std::optional<std::array<double,2>> line_pointer_client;
  bool overlay_dirty{};
  std::array<double,3> last_drafting_point{};
  void sync_line_prompt() {
    overlay_dirty=true;
    auto& d=view->document;
    if(line_active)d.remove_attribute(d.find("tool-banner"),"hidden");
    else d.attribute(d.find("tool-banner"),"hidden","");
    d.set_text(d.find("tool-banner-name"),line_active?"LINE":"");
    d.set_text(d.find("tool-banner-text"),line_active?line_tool.prompt():"");
    d.set_text(d.find("command-prefix"),line_active?std::string("LINE: ")+line_tool.prompt():"Command:");
    d.attribute(d.find("command-input"),"placeholder",line_active?"Enter coordinates, a value, or an option…":"Type a command or search…");
    view->refresh();
  }
#ifdef KESTREL_PREVIEW_SHARED_CSS
  std::unique_ptr<kestrel::layer_panel> layers;
  std::unique_ptr<kestrel::group_dialog> group_dialog;
  foco::ref<foco::color_picker> color_picker;
  bool color_picker_pending{};
  std::unordered_set<std::string> color_selection;
  std::optional<kestrel::json> color_test_before;
#endif
  std::unique_ptr<kestrel::viewport> viewport;
  uint32_t gpu_width{}, gpu_height{};
  uint64_t gpu_serial{};
  uint64_t line_edit_serial{};
  std::optional<kestrel::json> line_edit_before;
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
  bool modeling{},displayed_modeling{};
  std::string displayed_style;
  void sync_view_state() {
    if(!viewport)return;
    const std::string style=viewport->options.style==kestrel::display_style::wireframe?"wireframe":
      viewport->options.style==kestrel::display_style::shaded?"shaded":viewport->options.style==kestrel::display_style::shaded_edges?"shaded-edges":"xray";
    if(style==displayed_style && modeling==displayed_modeling)return;
    displayed_style=style;displayed_modeling=modeling;
    view->document.set_value(view->document.find("style-select"),style);
    view->document.set_value(view->document.find("workspace-select"),modeling?"3d":"2d");
    view->document.set_text(view->document.find("drawing-kind"),modeling?"3D MODEL SPACE":"MODEL SPACE");
#ifdef KESTREL_PREVIEW_SHARED_CSS
    if(layers)layers->refresh();
#endif
    view->refresh();
  }
  void set_camera_view(const std::string& name) {
    if(!viewport)return;
    viewport->camera.set_view(name);
    view->document.set_value(view->document.find("view-select"),name);
    if(name!="top" && name!="bottom") {
      modeling=true;
      if(viewport->options.style==kestrel::display_style::wireframe &&
          std::any_of(model.data["entities"].begin(),model.data["entities"].end(),[](const auto& e){return e["type"]=="MESH";}))
        viewport->options.style=kestrel::display_style::shaded_edges;
    } else if(name=="top")modeling=false;
    sync_view_state();gpu_dirty=true;
  }
  bool selection_pressed{},selection_dragging{};
  float selection_x{},selection_y{};
  webscene::native_web::input_modifiers selection_modifiers;
  void fit_drawing() {
    if(!viewport)return;
    std::vector<kestrel::vec3> points;
    for(const auto& entity:model.data["entities"]) if(model.visible(entity)) {
      const auto geometry=kestrel::geo::geometry(entity,1);
      points.insert(points.end(),geometry.points.begin(),geometry.points.end());
    }
    viewport->camera.fit(points);gpu_dirty=true;
  }
  std::optional<kestrel::vec3> drafting_pointer(double x,double y) {
    auto point=viewport->camera.unproject(x,y,0);
    if(!point)return {};
    std::optional<std::array<double,3>> base;
    if(!line_tool.points().empty())base=line_tool.points().back();
    const auto result=kestrel::constrain_drafting_point({point->x,point->y,point->z},base,false,100,drafting_shift,false,15);
    return kestrel::vec3{result[0],result[1],result[2]};
  }
  void redraw_overlay(uint32_t w,uint32_t h) {
    if(line_active && line_pointer_client) {
      const auto area=view->document.bounds(view->document.find("scene"));
      const auto x=(*line_pointer_client)[0]-area.x,y=(*line_pointer_client)[1]-area.y;
      line_pointer=(x>=0 && y>=0 && x<=area.width && y<=area.height)?drafting_pointer(x,y):std::nullopt;
    }

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
    if(line_active && line_pointer && !line_tool.points().empty()) {
      const auto point=line_tool.points().back();
      const auto a=viewport->camera.project({point[0],point[1],point[2]});
      const auto b=viewport->camera.project(*line_pointer);
      if(a.z>=0 && a.z<=1 && b.z>=0 && b.z<=1) {
        const uint32_t color=viewport->options.light_theme?0x16869cff:0x8ce0e5ff;
        for(const auto& dash:kestrel::preview_dashes(a.x,a.y,b.x,b.y,w,h))
          view->document.stroke_line(overlay,dash[0],dash[1],dash[2],dash[3],1.45f,color);
      }
    }
    overlay_dirty=false;
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
        redraw_overlay(w,h);
        view->set_gpu_image(node, w, h, gpu_serial,
            webscene::foco_host::make_gpu_image(node, gpu_serial, std::move(image)));
      }
    };
    publish();
    if (gpu_dirty && viewport->submit(model)) {
      gpu_dirty = false;
      publish(); // Foco waits on the retained producer fences before sampling.
    }
    if(overlay_dirty) {redraw_overlay(w,h);view->refresh();}
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
    group_dialog=std::make_unique<kestrel::group_dialog>(view->document,model,[this]{layers->refresh();gpu_dirty=true;view->refresh();});
    color_picker=foco::make_ref<foco::color_picker>();
    color_picker->set_is_compact(true);color_picker->set_alpha_enabled(false);
    color_picker->set_horizontal_alignment(foco::horizontal_alignment::left);
    color_picker->set_vertical_alignment(foco::vertical_alignment::top);
    color_picker->set_visibility(foco::visibility::collapsed);window->add_child(color_picker);
    color_picker->on_color_changed([this](foco::color value) {
      if(model.selection!=color_selection) {color_picker->set_open(false);return;}
      std::ostringstream text;text<<'#'<<std::hex<<std::setfill('0')<<std::setw(2)<<unsigned(value.r)<<std::setw(2)<<unsigned(value.g)<<std::setw(2)<<unsigned(value.b);
      model.change_selected_appearance("color",text.str());layers->refresh();gpu_dirty=true;view->refresh();
    });
    handlers.push_back(view->document.on(view->document.root(),"click",[this](auto& event) {
      if(view->document.attribute(event.target,"data-prop")!="color" || model.selection.empty())return;
      const auto bounds=view->document.bounds(event.target);
      const auto color=foco::color::parse(view->document.value(event.target));if(!color)return;
      color_selection=model.selection;color_picker->set_color(*color,false);
      color_picker->set_margin({bounds.x,bounds.y,0,0});color_picker->set_width(bounds.width);color_picker->set_height(bounds.height);
      color_picker->set_visibility(foco::visibility::visible);color_picker_pending=true;
      event.prevent_default();event.stop_propagation();
    }));
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
    handlers.push_back(view->document.on(view->document.find("command-input"),"keydown",[this](auto& event) {
      if(!line_active || event.key!="Enter")return;
      const auto input=view->document.find("command-input");
      auto command=view->document.value(input);
      const auto first=command.find_first_not_of(" \t\r\n");
      command=first==std::string::npos?"":command.substr(first,command.find_last_not_of(" \t\r\n")-first+1);
      for(auto& c:command)if(c>='a' && c<='z')c-=('a'-'A');
      if(command=="U" || command=="UNDO")line_tool.undo_point();
      else if(command.empty() || command=="ENTER" || command=="ESC" || command=="CANCEL") {
        line_active=false;line_tool.cancel();
      } else if(command.find(',')!=std::string::npos || command.find('<')!=std::string::npos) {
        try {
          const auto base=line_tool.points().empty()?last_drafting_point:line_tool.points().back();
          const auto point=kestrel::parse_drafting_point(command,base);
          if(line_tool.point(point) && line_tool.points().size()>1)last_drafting_point=point;
        } catch(const std::invalid_argument& error) {
          view->document.set_value(input,"");
          view->document.set_text(view->document.find("tool-banner-text"),error.what());
          view->refresh();event.prevent_default();event.stop_propagation();return;
        }
      } else return;
      view->document.set_value(input,"");sync_line_prompt();
#ifdef KESTREL_PREVIEW_SHARED_CSS
      if(layers)layers->refresh();
#endif
      gpu_dirty=true;event.prevent_default();event.stop_propagation();
    }));
    for(auto type:{"keydown","keyup"})handlers.push_back(view->document.on(view->document.root(),type,[this](auto& event) {
      if(drafting_shift!=event.modifiers.shift) {drafting_shift=event.modifiers.shift;overlay_dirty=true;}
    }));
    handlers.push_back(view->document.on(view->document.root(),"keydown",[this](auto& event) {
      if(!viewport)return;
      for(auto id:{"modal","command-palette"})if(auto dialog=view->document.find(id))
        if(view->document.attribute(dialog,"open"))return;
      if(event.key=="Escape") {
        line_active=false;line_tool.cancel();sync_line_prompt();
        panning=false;selection_pressed=false;view->document.attribute(view->document.find("selection-window"),"hidden","");model.selection.clear();
      } else {
        for(auto node=event.target;node;node=view->document.parent(node)) {
          const auto tag=view->document.tag_name(node);
          if(tag=="input" || tag=="textarea" || tag=="select" || view->document.attribute(node,"contenteditable")=="true")return;
        }
        if(event.modifiers.control || event.modifiers.meta) {
          if(event.key=="a")model.select_all_editable();
          else if(event.key=="z") {if(event.modifiers.shift)model.redo();else model.undo();}
          else if(event.key=="y")model.redo();
          else return;
        } else if(event.key=="Delete" || event.key=="Backspace")model.erase_selected();
        else if(event.key=="F7")viewport->grid_enabled=!viewport->grid_enabled;
        else return;
      }
#ifdef KESTREL_PREVIEW_SHARED_CSS
      if(layers)layers->refresh();
#endif
      gpu_dirty=true;view->refresh();event.prevent_default();
    }));
    handlers.push_back(view->document.on(view->document.root(), "click",
        [this](auto &event) {
          if (!viewport) return;
          for (auto node = event.target; node; node = view->document.parent(node)) {
            auto action = view->document.attribute(node, "data-action");
            if (!action) continue;
            if(*action=="theme") {
              const bool light=view->document.attribute(view->document.root(),"data-theme")!="light";
              view->document.attribute(view->document.root(),"data-theme",light?"light":"dark");
              viewport->options.light_theme=light;
              const auto update_icon=[&](auto&& self,webscene::native_web::node_id current,bool theme_button)->void {
                theme_button=theme_button || view->document.attribute(current,"data-action")=="theme";
                if(theme_button && view->document.attribute(current,"d"))
                  view->document.attribute(current,"d",light?"M20 15A9 9 0 0 1 9 4a9 9 0 1 0 11 11z":"M16 12a4 4 0 1 1-8 0 4 4 0 0 1 8 0ZM12 1v3M12 20v3M1 12h3M20 12h3M4 4l2 2M18 18l2 2M4 20l2-2M18 6l2-2");
                for(auto child:view->document.children(current))self(self,child,theme_button);
              };
              update_icon(update_icon,view->document.root(),false);view->refresh();
            }
            else if(*action=="toggle-explorer" || *action=="toggle-properties") {
              const auto workbench=view->document.find("workbench");
              const std::string name=*action=="toggle-explorer"?"hide-explorer":"hide-properties";
              std::istringstream input(view->document.attribute(workbench,"class").value_or(""));
              std::string token,classes;bool removed=false;
              while(input>>token) {
                if(token==name) {removed=true;continue;}
                if(!classes.empty())classes+=' ';classes+=token;
              }
              if(!removed) {if(!classes.empty())classes+=' ';classes+=name;}
              view->document.attribute(workbench,"class",classes);view->refresh();
            }
            else if (*action == "line") {
              line_tool.cancel();line_pointer.reset();line_pointer_client.reset();line_active=true;selection_pressed=false;
              if(std::abs(viewport->camera.direction.z)<.015)set_camera_view("top");
              sync_line_prompt();view->document.focus(view->document.find("viewport"));
            }
            else if (*action == "group") {
#ifdef KESTREL_PREVIEW_SHARED_CSS
              group_dialog->open();
#endif
            }
            else if (*action == "wireframe") viewport->options.style = kestrel::display_style::wireframe;
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
            else if (*action == "erase" || *action == "undo" || *action == "redo" || *action=="all-layers-on" || *action=="ungroup") {
              if(*action=="all-layers-on") model.show_all_layers();
              else if(*action=="erase") model.erase_selected();
              else if(*action=="ungroup") model.ungroup_selected();
              else if(*action=="undo") model.undo();else model.redo();
#ifdef KESTREL_PREVIEW_SHARED_CSS
              if(layers) layers->refresh();
#endif
              view->refresh();
            }
            else if (action->starts_with("view-")) set_camera_view(action->substr(5));
            else return;
            sync_view_state();gpu_dirty = true;
            event.prevent_default();
            return;
          }
        }));
    handlers.push_back(view->document.on(view->document.find("view-select"),"change",
        [this](auto&) {
          if(!viewport)return;
          set_camera_view(view->document.value(view->document.find("view-select")));
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
          sync_view_state();gpu_dirty=true;
        }));
    handlers.push_back(view->document.on(view->document.find("viewport"), "pointerdown",
        [this](auto &event) {
          if(!viewport)return;
          if((event.buttons&1u) && (event.target==view->document.find("viewport") || event.target==view->document.find("scene") || event.target==view->document.find("overlay"))) {
            if(line_active) {
              const auto area=view->document.bounds(view->document.find("scene"));
              drafting_shift=event.modifiers.shift;
              if(auto point=drafting_pointer(event.client_x-area.x,event.client_y-area.y)) {
                try {
                  if(line_tool.point({point->x,point->y,point->z}) && line_tool.points().size()>1)
                    last_drafting_point={point->x,point->y,point->z};
                  sync_line_prompt();
#ifdef KESTREL_PREVIEW_SHARED_CSS
                  if(layers)layers->refresh();
#endif
                  gpu_dirty=true;
                } catch(const std::invalid_argument& error) {
                  view->document.set_text(view->document.find("tool-banner-text"),error.what());view->refresh();
                }
              }
              event.prevent_default();return;
            }
            selection_pressed=true;selection_dragging=false;selection_x=event.client_x;selection_y=event.client_y;selection_modifiers=event.modifiers;
          }
          if(!(event.buttons&4u))return;
          panning = true; orbiting = event.modifiers.shift; pan_x = event.client_x; pan_y = event.client_y;
          event.prevent_default();
        }));
    handlers.push_back(view->document.on(view->document.root(), "pointermove",
        [this](auto &event) {
          if(line_active && viewport) {
            drafting_shift=event.modifiers.shift;
            line_pointer_client=std::array<double,2>{event.client_x,event.client_y};
            const auto area=view->document.bounds(view->document.find("scene"));
            const auto x=event.client_x-area.x,y=event.client_y-area.y;
            line_pointer=(x>=0 && y>=0 && x<=area.width && y<=area.height)?drafting_pointer(x,y):std::nullopt;
            overlay_dirty=true;
          }
          if(selection_pressed) {
            selection_dragging=selection_dragging || std::hypot(event.client_x-selection_x,event.client_y-selection_y)>4;
#ifdef KESTREL_PREVIEW_SHARED_CSS
            if(selection_dragging) {
              const auto area=view->document.bounds(view->document.find("viewport"));
              const auto box=view->document.find("selection-window");
              view->document.remove_attribute(box,"hidden");
              view->document.attribute(box,"class",event.client_x<selection_x?"crossing":"");
              view->document.attribute(box,"style","left:"+std::to_string(std::min(selection_x,event.client_x)-area.x)+"px;top:"+
                  std::to_string(std::min(selection_y,event.client_y)-area.y)+"px;width:"+std::to_string(std::abs(event.client_x-selection_x))+
                  "px;height:"+std::to_string(std::abs(event.client_y-selection_y))+"px");
              view->refresh();
            }
#endif
          }
          if (!panning || !viewport) return;
          if (!(event.buttons & 4u)) { panning = false; return; }
          if(orbiting) {
            viewport->camera.orbit(event.client_x - pan_x, event.client_y - pan_y);
            view->document.set_value(view->document.find("view-select"),"iso");
            modeling=true;sync_view_state();
          } else viewport->camera.pan(event.client_x - pan_x, event.client_y - pan_y);
          pan_x = event.client_x; pan_y = event.client_y;
          gpu_dirty = true;
        }));
    handlers.push_back(view->document.on(view->document.root(),"pointerup",[this](auto& event) {
      if(!std::exchange(selection_pressed,false) || !viewport)return;
      const auto area=view->document.bounds(view->document.find("scene"));
      const bool control=selection_modifiers.control || selection_modifiers.meta;
      view->document.attribute(view->document.find("selection-window"),"hidden","");
      if(selection_dragging) kestrel::select_window(model,viewport->camera,viewport->options.style,
          {selection_x-area.x,selection_y-area.y,0},{event.client_x-area.x,event.client_y-area.y,0},selection_modifiers.shift || control);
      else kestrel::select_at(model,viewport->camera,viewport->options.style,event.client_x-area.x,event.client_y-area.y,
          selection_modifiers.shift || control,control);
#ifdef KESTREL_PREVIEW_SHARED_CSS
      if(layers)layers->refresh();
#endif
      gpu_dirty=true;view->refresh();
    }));
    handlers.push_back(view->document.on(view->document.root(),"pointercancel",[this](auto&){selection_pressed=false;view->document.attribute(view->document.find("selection-window"),"hidden","");view->refresh();}));
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
#ifdef KESTREL_PREVIEW_SHARED_CSS
        const bool opening_picker=color_picker_pending;
        if(show_group_dialog && gpu_serial) {
          model.selection.clear();for(const auto& entity:model.data["entities"])if(model.editable(entity)) {
            model.selection.insert(entity["id"].get<std::string>());if(model.selection.size()==2)break;
          }
          if(!group_dialog->open())throw std::runtime_error("Native group dialog failed to open");
          const auto modal_bounds=view->document.bounds(view->document.find("modal"));
          std::cout<<"Group dialog bounds: "<<modal_bounds.x<<","<<modal_bounds.y<<" "<<modal_bounds.width<<"x"<<modal_bounds.height<<"\n";
          if(exercise_group_dialog) {
            const auto before=model.data;
            const auto input=view->document.find("field-name");
            if(view->document.focused()!=input)throw std::runtime_error("Group input did not receive focus");
            view->document.set_selection(input,0,view->document.value(input).size());
            foco::text_input_event text;text.text="Hosted group";view->text_input_received(text);
            const auto submit=view->document.bounds(view->document.find("modal-submit"));
            foco::pointer_event pointer;pointer.position={float(view->bounds().x+submit.x+submit.width/2),float(view->bounds().y+submit.y+submit.height/2)};
            pointer.kind=foco::pointer_event_kind::pressed;pointer.buttons=1;view->pointer_event_received(pointer);
            pointer.kind=foco::pointer_event_kind::released;pointer.buttons=0;view->pointer_event_received(pointer);
            if(group_dialog->is_open())throw std::runtime_error("Group submit did not close dialog");
            for(const auto& id:model.selection)if(model.find(id)->value("groupName",std::string{})!="Hosted group")throw std::runtime_error("Hosted group name was not applied");
            view->document.focus(view->document.find("viewport"));
            foco::key_event undo;undo.value=foco::key::z;undo.modifiers=foco::key_modifiers::platform;view->key_event_received(undo);
            if(model.data!=before)throw std::runtime_error("Hosted group undo did not restore drawing");
            exercise_group_dialog=false;std::cout<<"Hosted compiled Group text input, pointer submit and undo passed\n";
          }
          show_group_dialog=false;
        }
        if(color_picker_pending) {color_picker_pending=false;color_picker->set_open(true);}
        else if(color_picker && !color_picker->is_open())color_picker->set_visibility(foco::visibility::collapsed);
        if(color_test_before && color_picker->is_open() && !show_color_picker && !opening_picker) {
          const auto find_spectrum=[&](auto&& self,foco::element& element)->foco::color_spectrum* {
            if(auto* spectrum=dynamic_cast<foco::color_spectrum*>(&element))return spectrum;
            for(const auto& child:element.children())if(auto* found=self(self,*child))return found;
            return nullptr;
          };
          auto* spectrum=find_spectrum(find_spectrum,*window);
          if(!spectrum || spectrum->bounds().width<=0 || spectrum->bounds().height<=0)
            throw std::runtime_error("Native color spectrum was not laid out");
          const auto bounds=spectrum->bounds();
          foco::pointer_event pointer;pointer.kind=foco::pointer_event_kind::pressed;pointer.buttons=1;
          pointer.position={bounds.x+bounds.width*.25f,bounds.y+bounds.height*.25f};
          if(window->hit_test(pointer.position)!=spectrum)throw std::runtime_error("Native spectrum pointer hit test failed");
          spectrum->raise_pointer_event(pointer);
          if(model.data==*color_test_before)throw std::runtime_error("Native spectrum pointer press did not edit color");
          if(!color_picker->is_open())throw std::runtime_error("Native color picker closed during editing");
          const auto intermediate=model.data;
          pointer.kind=foco::pointer_event_kind::moved;
          pointer.position={bounds.x+bounds.width*.75f,bounds.y+bounds.height*.75f};
          if(window->hit_test(pointer.position)!=spectrum)throw std::runtime_error("Native spectrum pointer hit test failed");
          spectrum->raise_pointer_event(pointer);
          if(model.data==intermediate)throw std::runtime_error("Native spectrum pointer drag did not edit color");
          pointer.kind=foco::pointer_event_kind::released;pointer.buttons=0;
          if(window->hit_test(pointer.position)!=spectrum)throw std::runtime_error("Native spectrum pointer hit test failed");
          spectrum->raise_pointer_event(pointer);
          if(!color_picker->is_open())throw std::runtime_error("Native color picker closed during repeated editing");
          const auto color=spectrum->selected_color();
          std::ostringstream expected;expected<<'#'<<std::hex<<std::setfill('0')<<std::setw(2)<<unsigned(color.r)<<std::setw(2)<<unsigned(color.g)<<std::setw(2)<<unsigned(color.b);
          for(const auto& id:model.selection)if(model.find(id)->value("color",std::string{})!=expected.str())throw std::runtime_error("Native color picker commit failed");
          color_picker->set_open(false);
          view->document.focus(view->document.find("viewport"));
          foco::key_event key;key.value=foco::key::z;key.modifiers=foco::key_modifiers::platform;view->key_event_received(key);
          if(model.data!=intermediate)throw std::runtime_error("Native color picker intermediate undo failed");
          key.handled=false;view->key_event_received(key);
          if(model.data!=*color_test_before)throw std::runtime_error("Native color picker undo failed");
          color_test_before.reset();std::cout<<"Native Foco color picker routed pointer drag, commit and undo passed\n";
        }
#endif
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
          const auto toggle_panel=[&](std::string_view action) {
            const auto find_action=[&](auto&& self,webscene::native_web::node_id node)->webscene::native_web::node_id {
              if(view->document.attribute(node,"data-action")==action)return node;
              for(auto child:view->document.children(node)) if(auto found=self(self,child))return found;
              return 0;
            };
            // These original controls remain available while their panel is hidden.
            const auto scope=view->document.find(action=="toggle-explorer"?"viewport-controls":"statusbar");
            const auto button=find_action(find_action,scope);
            if(!button)throw std::runtime_error("Original panel toggle missing");
            const auto bounds=view->document.bounds(button);
            if(bounds.width<=0 || bounds.height<=0)throw std::runtime_error("Original panel toggle has no hit area");
            foco::pointer_event click;click.position={host.x+bounds.x+bounds.width/2,host.y+bounds.y+bounds.height/2};
            click.kind=foco::pointer_event_kind::pressed;click.buttons=1;view->pointer_event_received(click);
            click.kind=foco::pointer_event_kind::released;click.buttons=0;view->pointer_event_received(click);
            view->document.render(host.width,host.height);
            return view->document.bounds(view->document.find("scene")).width;
          };
          const auto initial_width=view->document.bounds(view->document.find("scene")).width;
          const auto no_explorer=toggle_panel("toggle-explorer");
          const auto no_panels=toggle_panel("toggle-properties");
          if(no_explorer<=initial_width || no_panels<=no_explorer)
            throw std::runtime_error("Original panel CSS did not expand viewport");
          toggle_panel("toggle-explorer");
          const auto restored_width=toggle_panel("toggle-properties");
          if(std::abs(restored_width-initial_width)>.1f)
            throw std::runtime_error("Panel toggle did not restore viewport width");
          navigation_serial=gpu_serial;navigation_exercised=true;
        }
        if(exercise_line_draw && viewport && gpu_serial) {
          const auto find_line=[&](auto&& self,webscene::native_web::node_id node)->webscene::native_web::node_id {
            if(view->document.attribute(node,"data-action")=="line")return node;
            for(auto child:view->document.children(node))if(auto found=self(self,child))return found;
            return 0;
          };
          const auto button=find_line(find_line,view->document.root());
          if(!button)throw std::runtime_error("Original Line button missing");
          const auto host=view->bounds();
          const auto click=[&](float x,float y) {
            foco::pointer_event event;event.position={host.x+x,host.y+y};
            event.kind=foco::pointer_event_kind::pressed;event.buttons=1;view->pointer_event_received(event);
            event.kind=foco::pointer_event_kind::released;event.buttons=0;view->pointer_event_received(event);
          };
          auto box=view->document.bounds(button);click(box.x+box.width/2,box.y+box.height/2);
          if(!line_active || view->document.attribute(view->document.find("tool-banner"),"hidden"))
            throw std::runtime_error("Original Line pointer activation failed");
          const auto before=model.data;
          const auto area=view->document.bounds(view->document.find("scene"));
          const auto first=viewport->camera.unproject(area.width*.4,area.height*.45,0);
          const auto second=viewport->camera.unproject(area.width*.6,area.height*.55,0);
          if(!first || !second)throw std::runtime_error("Line fixture has no drafting plane");
          click(area.x+area.width*.4,area.y+area.height*.45);
          if(model.data!=before || line_tool.points().size()!=1)throw std::runtime_error("First hosted Line point failed");
          click(area.x+area.width*.6,area.y+area.height*.55);
          if(model.data["entities"].size()!=before["entities"].size()+1)throw std::runtime_error("Hosted Line segment missing");
          const auto& entity=model.data["entities"].back();
          const auto near=[](const auto& p,const auto& expected) {
            return std::abs(p[0].template get<double>()-expected.x)<.01 &&
              std::abs(p[1].template get<double>()-expected.y)<.01 && std::abs(p[2].template get<double>()-expected.z)<.01;
          };
          if(entity["type"]!="LINE" || !near(entity["points"][0],*first) || !near(entity["points"][1],*second))
            throw std::runtime_error("Hosted Line coordinates differ from viewport projection");
          const auto command=view->document.find("command-input");
          view->document.focus(command);
          foco::text_input_event text;text.text="u";view->text_input_received(text);
          foco::key_event enter;enter.value=foco::key::enter;view->key_event_received(enter);
          if(model.data!=before || line_tool.points().size()!=1 || !view->document.value(command).empty())
            throw std::runtime_error("Hosted Line command Undo failed");
          click(area.x+area.width*.6,area.y+area.height*.55);
          if(model.data["entities"].size()!=before["entities"].size()+1)throw std::runtime_error("Line continuation after Undo failed");
          view->document.focus(view->document.find("viewport"));
          foco::key_event key;key.value=foco::key::escape;view->key_event_received(key);
          if(line_active || !line_tool.points().empty())throw std::runtime_error("Hosted Line cancellation failed");
          key={};key.value=foco::key::z;key.modifiers=foco::key_modifiers::platform;view->key_event_received(key);
          if(model.data!=before)throw std::runtime_error("Hosted Line Undo failed");
          box=view->document.bounds(button);click(box.x+box.width/2,box.y+box.height/2);
          const auto type_command=[&](std::string value) {
            view->document.focus(command);foco::text_input_event input;input.text=std::move(value);view->text_input_received(input);
            foco::key_event key;key.value=foco::key::enter;view->key_event_received(key);
          };
          type_command("100,200,5");type_command("@10,20");
          if(model.data["entities"].size()!=before["entities"].size()+1 ||
              model.data["entities"].back()["points"]!=kestrel::json{{100,200,5},{110,220,5}})
            throw std::runtime_error("Hosted typed relative Line failed");
          const auto valid=model.data;type_command("1x,2");
          if(model.data!=valid || line_tool.points().size()!=2)throw std::runtime_error("Invalid typed coordinate changed drawing");
          type_command("");
          if(line_active || !line_tool.points().empty() || model.data!=valid)throw std::runtime_error("Empty Enter did not finish Line");
          view->document.focus(view->document.find("viewport"));
          key={};key.value=foco::key::z;key.modifiers=foco::key_modifiers::platform;view->key_event_received(key);
          if(model.data!=before)throw std::runtime_error("Typed Line Undo failed");
          if(show_line_preview) {
            box=view->document.bounds(button);click(box.x+box.width/2,box.y+box.height/2);
            click(area.x+area.width*.3,area.y+area.height*.35);
            const auto pending=model.data;
            foco::pointer_event move;move.kind=foco::pointer_event_kind::moved;
            move.position={host.x+area.x+area.width*.7f,host.y+area.y+area.height*.65f};
            view->pointer_event_received(move);
            if(!line_pointer || !overlay_dirty || model.data!=pending)
              throw std::runtime_error("Preview pointer movement changed geometry or failed to invalidate overlay");
            redraw_overlay(gpu_width,gpu_height);view->refresh();
            const auto& scene=view->document.render(host.width,host.height);
            size_t strokes=0;for(const auto& command:scene.canvas)if(command.kind==20)++strokes;
            if(strokes<2)throw std::runtime_error("Pending Line emitted no dashed Canvas strokes");
            view->document.focus(view->document.find("viewport"));
            foco::key_event modifier_key;modifier_key.value=foco::key::a;modifier_key.modifiers=foco::key_modifiers::shift;
            view->key_event_received(modifier_key);redraw_overlay(gpu_width,gpu_height);
            if(!drafting_shift || !line_pointer)throw std::runtime_error("Hosted key modifiers did not update stationary preview");
            modifier_key={};modifier_key.value=foco::key::a;
            view->key_released_received(modifier_key);redraw_overlay(gpu_width,gpu_height);
            if(drafting_shift || !modifier_key.handled)throw std::runtime_error("Hosted key release did not clear stationary modifier");
            const auto released=viewport->camera.unproject(area.width*.7,area.height*.65,0);
            if(!line_pointer || !released || std::hypot(line_pointer->x-released->x,line_pointer->y-released->y)>.01)
              throw std::runtime_error("Hosted key release left a constrained preview");
            move.modifiers=foco::key_modifiers::shift;view->pointer_event_received(move);
            redraw_overlay(gpu_width,gpu_height);
            const auto anchor=line_tool.points().back();
            if(!line_pointer || (std::abs(line_pointer->x-anchor[0])>1e-8 && std::abs(line_pointer->y-anchor[1])>1e-8))
              throw std::runtime_error("Shift preview did not constrain an axis");
            const auto constrained=*line_pointer;
            foco::pointer_event commit=move;commit.kind=foco::pointer_event_kind::pressed;commit.buttons=1;view->pointer_event_received(commit);
            commit.kind=foco::pointer_event_kind::released;commit.buttons=0;view->pointer_event_received(commit);
            const auto& endpoint=model.data["entities"].back()["points"][1];
            if(model.data["entities"].size()!=pending["entities"].size()+1 || !near(endpoint,constrained))
              throw std::runtime_error("Shift committed endpoint differs from preview");
            type_command("U");
            if(model.data!=pending || line_tool.points().size()!=1)throw std::runtime_error("Shift segment Undo failed");
            move.modifiers=foco::key_modifiers::none;view->pointer_event_received(move);
            redraw_overlay(gpu_width,gpu_height);
            const auto unconstrained=viewport->camera.unproject(area.width*.7,area.height*.65,0);
            if(!line_pointer || !unconstrained || std::hypot(line_pointer->x-unconstrained->x,line_pointer->y-unconstrained->y)>.01)
              throw std::runtime_error("Shift release on pointer movement did not restore unconstrained preview");
            viewport->camera.pan(40,15);redraw_overlay(gpu_width,gpu_height);
            if(!line_pointer)throw std::runtime_error("Pan lost preview pointer");
            const auto projected=viewport->camera.project(*line_pointer);
            if(std::abs(projected.x-area.width*.7)>.01 || std::abs(projected.y-area.height*.65)>.01)
              throw std::runtime_error("Camera pan detached preview from pointer");
            viewport->camera.pan(-40,-15);redraw_overlay(gpu_width,gpu_height);
            view->document.focus(view->document.find("viewport"));
            key={};key.value=foco::key::escape;view->key_event_received(key);
            redraw_overlay(gpu_width,gpu_height);view->refresh();
            const auto& cleared=view->document.render(host.width,host.height);
            for(const auto& command:cleared.canvas)if(command.kind==20)throw std::runtime_error("Escape left preview strokes");
            if(model.data!=pending)throw std::runtime_error("Preview cancellation changed geometry");
            box=view->document.bounds(button);click(box.x+box.width/2,box.y+box.height/2);
            click(area.x+area.width*.3,area.y+area.height*.35);view->pointer_event_received(move);
            redraw_overlay(gpu_width,gpu_height);view->refresh();
            std::cout<<"Hosted pending Line preview emitted "<<strokes<<" strokes without committing geometry\n";
          }
          exercise_line_draw=false;
          std::cout<<"Hosted original Line ribbon, pointer geometry, Escape and Undo passed\n";
        }
        if(exercise_drag_selection && viewport && gpu_serial) {
          const auto area=view->document.bounds(view->document.find("scene"));
          const auto host=view->bounds();const auto box=view->document.find("selection-window");
          const auto pointer=[&](foco::pointer_event_kind kind,float x,float y,uint32_t buttons) {
            foco::pointer_event event;event.kind=kind;event.buttons=buttons;event.position={host.x+area.x+x,host.y+area.y+y};
            view->pointer_event_received(event);
          };
          model.selection.clear();
          pointer(foco::pointer_event_kind::pressed,area.width-30,area.height-45,1);
          pointer(foco::pointer_event_kind::moved,30,55,1);
          view->document.render(host.width,host.height);
          if(view->document.attribute(box,"hidden") || view->document.attribute(box,"class")!="crossing" || view->document.bounds(box).width<100)
            throw std::runtime_error("Crossing rectangle did not appear");
          pointer(foco::pointer_event_kind::released,30,55,0);
          if(model.selection.size()<2 || !view->document.attribute(box,"hidden"))
            throw std::runtime_error("Hosted drag selection failed");
          const auto selected=model.selection;
          pointer(foco::pointer_event_kind::pressed,30,55,1);
          pointer(foco::pointer_event_kind::moved,100,120,1);
          pointer(foco::pointer_event_kind::cancelled,100,120,0);
          if(model.selection!=selected || !view->document.attribute(box,"hidden"))
            throw std::runtime_error("Drag cancellation changed selection or left its rectangle visible");
          exercise_drag_selection=false;std::cout<<"Hosted drag selection passed; selected="<<model.selection.size()<<'\n';
        }
        if(exercise_shortcuts && viewport && gpu_serial) {
          const auto press=[&](foco::key key,foco::key_modifiers modifiers={}) {
            foco::key_event event;event.value=key;event.modifiers=modifiers;view->key_event_received(event);
            if(!event.handled)throw std::runtime_error("Shortcut key was not forwarded");
          };
          const auto before=model.data;
          view->document.focus(view->document.find("viewport"));
          press(foco::key::a,foco::key_modifiers::control);
          if(model.selection.empty())throw std::runtime_error("Select-all shortcut failed");
          press(foco::key::delete_key);
          if(model.data["entities"].size()>=before["entities"].size())throw std::runtime_error("Erase shortcut failed");
          press(foco::key::z,foco::key_modifiers::platform);
          if(model.data!=before)throw std::runtime_error("Undo shortcut failed");
          press(foco::key::y,foco::key_modifiers::control);
          if(model.data==before)throw std::runtime_error("Redo shortcut failed");
          press(foco::key::z,foco::key_modifiers::control);
          const auto search=view->document.find("explorer-search");
          view->document.set_value(search,"abc");view->document.focus(search);
          press(foco::key::backspace);
          if(view->document.value(search)!="ab" || model.data!=before)throw std::runtime_error("Typing shortcut isolation failed");
          view->document.set_value(search,"");view->document.dispatch(search,"input");
          view->document.focus(view->document.find("viewport"));
          press(foco::key::a,foco::key_modifiers::control);press(foco::key::escape);
          if(!model.selection.empty())throw std::runtime_error("Escape selection clearing failed");
          const bool grid=viewport->grid_enabled;press(foco::key::f7);
          if(viewport->grid_enabled==grid)throw std::runtime_error("Grid shortcut failed");
          press(foco::key::f7);
          exercise_shortcuts=false;std::cout<<"Hosted native shortcuts passed\n";
        }
        if(exercise_picking && viewport && gpu_serial) {
          const auto area=view->document.bounds(view->document.find("scene"));
          bool found=false;
          for(const auto& entity:model.data["entities"]) {
            if(!model.visible(entity) || (exercise_line_edit && entity.value("type",std::string{})!="LINE"))continue;
            const auto geometry=kestrel::geo::geometry(entity);
            for(const auto& segment:geometry.segments) {
              const auto point=viewport->camera.project((segment[0]+segment[1])*.5);
              if(point.x<30 || point.x>area.width-30 || point.y<50 || point.y>area.height-40)continue;
              foco::pointer_event click;click.position={float(view->bounds().x+area.x+point.x),float(view->bounds().y+area.y+point.y)};
              click.kind=foco::pointer_event_kind::pressed;click.buttons=1;view->pointer_event_received(click);
              click.kind=foco::pointer_event_kind::released;click.buttons=0;view->pointer_event_received(click);
              found=!model.selection.empty();
              if(exercise_line_edit)found=model.selection.size()==1 && model.find(*model.selection.begin())->value("type",std::string{})=="LINE";
              if(found)break;
            }
            if(found)break;
          }
          if(!found)throw std::runtime_error("Hosted geometry click did not select an entity");
#ifdef KESTREL_PREVIEW_SHARED_CSS
          if(exercise_color_picker) {
            const auto find_color=[&](auto&& self,webscene::native_web::node_id node)->webscene::native_web::node_id {
              if(view->document.attribute(node,"data-prop")=="color")return node;
              for(auto child:view->document.children(node))if(auto result=self(self,child))return result;
              return 0;
            };
            const auto input=find_color(find_color,view->document.find("inspector"));if(!input)throw std::runtime_error("Color swatch missing");
            color_test_before=model.data;view->refresh();view->document.dispatch(input,"click");exercise_color_picker=false;
          }
#endif
          if(exercise_line_edit) {
            const auto find_endpoint=[&](auto&& self,webscene::native_web::node_id node)->webscene::native_web::node_id {
              if(view->document.attribute(node,"data-prop")=="points.1.0")return node;
              for(auto child:view->document.children(node))if(auto found=self(self,child))return found;
              return 0;
            };
            const auto input=find_endpoint(find_endpoint,view->document.find("inspector"));
            if(!input)throw std::runtime_error("Hosted line endpoint control missing");
            line_edit_before=model.data;line_edit_serial=gpu_serial;
            const auto selected=*model.selection.begin();
            const double value=model.find(selected)->at("points")[1][0].get<double>()+500;
            view->document.focus(input);view->document.set_selection(input,0,view->document.value(input).size());
            foco::text_input_event text;text.text=std::to_string(value);view->text_input_received(text);
            foco::key_event key;key.value=foco::key::enter;view->key_event_received(key);
            if(model.find(selected)->at("points")[1][0]!=value)throw std::runtime_error("Hosted endpoint commit failed");
            exercise_line_edit=false;
          }
          if(exercise_layer_edit) {
            const auto before=model.data;
            const auto selected=*model.selection.begin();
            const auto original_layer=model.find(selected)->at("layer").get<std::string>();
            const auto find_select=[&](auto&& self,webscene::native_web::node_id node,std::string_view property)->webscene::native_web::node_id {
              if(view->document.attribute(node,"data-prop")==property)return node;
              for(auto child:view->document.children(node))if(auto result=self(self,child,property))return result;
              return 0;
            };
            const auto control=find_select(find_select,view->document.find("inspector"),"layer");
            if(!control)throw std::runtime_error("Inspector layer control missing");
            view->document.focus(control);
            foco::key_event key;key.value=foco::key::home;view->key_event_received(key);
            const auto first_layer=model.data["layers"][0]["id"].get<std::string>();
            if(first_layer==original_layer || model.find(selected)->at("layer")!=first_layer)
              throw std::runtime_error("Inspector layer keyboard change failed");
            view->document.focus(view->document.find("viewport"));
            key={};key.value=foco::key::z;key.modifiers=foco::key_modifiers::platform;view->key_event_received(key);
            if(model.data!=before)throw std::runtime_error("Inspector layer undo did not restore drawing");
            const auto restored=find_select(find_select,view->document.find("inspector"),"layer");
            if(!restored || view->document.value(restored)!=original_layer)
              throw std::runtime_error("Inspector did not reflect layer undo");
            for(auto property:{"lineweight","name"}) {
              const auto input=find_select(find_select,view->document.find("inspector"),property);
              if(!input)throw std::runtime_error("Inspector text control missing");
              view->document.focus(input);
              view->document.set_selection(input,0,view->document.value(input).size());
              foco::text_input_event text;text.text=std::string_view(property)=="name"?"Hosted native wall":"1.25";
              view->text_input_received(text);
              key={};key.value=foco::key::enter;view->key_event_received(key);
              if(std::string_view(property)=="name" ? model.find(selected)->value("name",std::string{})!="Hosted native wall"
                  : model.find(selected)->value("lineweight",0.0)!=1.25)
                throw std::runtime_error("Hosted inspector text commit failed");
              view->document.focus(view->document.find("viewport"));
              key={};key.value=foco::key::z;key.modifiers=foco::key_modifiers::platform;view->key_event_received(key);
              if(model.data!=before)throw std::runtime_error("Hosted inspector text undo failed");
            }
            std::cout<<"Hosted inspector text commits and undo passed\n";
            if(model.find(selected)->value("type",std::string{})!="POLYLINE")
              throw std::runtime_error("Hosted checkbox fixture did not select a polyline");
            const bool was_closed=model.find(selected)->value("closed",false);
            const auto checkbox=find_select(find_select,view->document.find("inspector"),"closed");
            if(!checkbox)throw std::runtime_error("Hosted Closed checkbox missing");
            view->refresh();
            const auto box=view->document.bounds(checkbox);
            if(box.width<=0 || box.height<=0)throw std::runtime_error("Hosted checkbox has no hit area");
            foco::pointer_event click;click.position={float(view->bounds().x+box.x+box.width*.5),float(view->bounds().y+box.y+box.height*.5)};
            click.kind=foco::pointer_event_kind::pressed;click.buttons=1;view->pointer_event_received(click);
            click.kind=foco::pointer_event_kind::released;click.buttons=0;view->pointer_event_received(click);
            if(model.find(selected)->value("closed",false)==was_closed)throw std::runtime_error("Hosted Closed checkbox pointer activation failed");
            view->document.focus(view->document.find("viewport"));
            key={};key.value=foco::key::z;key.modifiers=foco::key_modifiers::platform;view->key_event_received(key);
            if(model.data!=before)throw std::runtime_error("Hosted Closed checkbox undo failed");
            std::cout<<"Hosted Closed checkbox pointer activation and undo passed\n";
            exercise_layer_edit=false;std::cout<<"Hosted inspector layer edit and undo passed\n";
          }
          exercise_picking=false;std::cout<<"Hosted geometry selection passed\n";
        }
        if(line_edit_before && gpu_serial>line_edit_serial) {
          view->document.focus(view->document.find("viewport"));
          foco::key_event key;key.value=foco::key::z;key.modifiers=foco::key_modifiers::platform;view->key_event_received(key);
          if(model.data!=*line_edit_before)throw std::runtime_error("Hosted endpoint undo failed");
          line_edit_before.reset();std::cout<<"Hosted line endpoint commit, GPU publication and undo passed\n";
        }
        if(exercise_theme && viewport && gpu_serial) {
          const auto find_theme=[&](auto&& self,webscene::native_web::node_id node)->webscene::native_web::node_id {
            if(view->document.attribute(node,"data-action")=="theme")return node;
            for(auto child:view->document.children(node))if(auto found=self(self,child))return found;
            return 0;
          };
          const auto button=find_theme(find_theme,view->document.root());
          if(!button)throw std::runtime_error("Original theme button missing");
          view->document.dispatch(button,"click");
          if(!viewport->options.light_theme || view->document.attribute(view->document.root(),"data-theme")!="light")
            throw std::runtime_error("Native light theme failed");
          exercise_theme=false;
          std::cout<<"Native theme changed to light\n";
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
    else if(std::string_view(argv[i])=="--exercise-drag-selection") exercise_drag_selection=true;
    else if(std::string_view(argv[i])=="--exercise-shortcuts") exercise_shortcuts=true;
    else if(std::string_view(argv[i])=="--show-color-picker") {exercise_picking=true;exercise_color_picker=true;show_color_picker=true;}
    else if(std::string_view(argv[i])=="--exercise-color-picker") {exercise_picking=true;exercise_color_picker=true;}
    else if(std::string_view(argv[i])=="--show-line-preview") {exercise_line_draw=true;show_line_preview=true;}
    else if(std::string_view(argv[i])=="--exercise-line-draw") exercise_line_draw=true;
    else if(std::string_view(argv[i])=="--exercise-line-edit") {exercise_picking=true;exercise_line_edit=true;}
    else if(std::string_view(argv[i])=="--exercise-layer-edit") {exercise_picking=true;exercise_layer_edit=true;}
    else if(std::string_view(argv[i])=="--exercise-picking") exercise_picking=true;
    else if(std::string_view(argv[i])=="--exercise-theme") exercise_theme=true;
    else if(std::string_view(argv[i])=="--show-group-dialog") show_group_dialog=true;
    else if(std::string_view(argv[i])=="--exercise-group-dialog") {show_group_dialog=true;exercise_group_dialog=true;}
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
