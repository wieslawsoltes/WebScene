module;
#include "../third_party/nlohmann/json.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <string>
#include <type_traits>
#include <vector>
#include <map>
export module kestrel.render_data;
export import kestrel.geometry;
export import kestrel.camera;

export namespace kestrel {
// Clip before dash expansion so work scales with the viewport, not world extent.
inline std::vector<std::array<double,4>> preview_dashes(double x,double y,double end_x,double end_y,double width,double height) {
  for(auto v:{x,y,end_x,end_y,width,height})if(!std::isfinite(v))return {};
  if(width<=0 || height<=0)return {};
  const auto dx=end_x-x,dy=end_y-y,length=std::hypot(dx,dy);
  if(!std::isfinite(length) || length==0)return {};
  double enter=0,leave=1;
  const auto clip=[&](double p,double q) {
    if(p==0)return q>=0;
    const auto r=q/p;
    if(p<0)enter=std::max(enter,r);else leave=std::min(leave,r);
    return enter<=leave;
  };
  if(!clip(-dx,x) || !clip(dx,width-x) || !clip(-dy,y) || !clip(dy,height-y))return {};
  const auto first=enter*length,last=leave*length;
  std::vector<std::array<double,4>> result;
  // Reduce phase once; repeated additions stay at viewport-sized magnitudes.
  const auto phase=std::fmod(first,9.0);
  for(double offset=-phase;offset<last-first;offset+=9) {
    const auto a=std::max(0.0,offset),b=std::min(last-first,offset+5);
    if(b<=a)continue;
    result.push_back({x+dx*enter+dx/length*a,y+dy*enter+dy/length*a,
                      x+dx*enter+dx/length*b,y+dy*enter+dy/length*b});
  }
  return result;
}
// Matches renderer.js line-instance and triangle-vertex shader layouts.
struct line_instance {
  std::array<float, 3> start, end;
  std::array<float, 4> color;
  float width, dash;
};
struct triangle_vertex {
  std::array<float, 3> position, normal;
  std::array<float, 4> color;
};
static_assert(sizeof(line_instance) == 12 * sizeof(float));
static_assert(sizeof(triangle_vertex) == 10 * sizeof(float));
static_assert(offsetof(line_instance, color) == 6 * sizeof(float));
static_assert(offsetof(triangle_vertex, color) == 6 * sizeof(float));
static_assert(std::is_trivially_copyable_v<line_instance>);
static_assert(std::is_trivially_copyable_v<triangle_vertex>);

struct render_data {
  vec3 origin;
  std::vector<line_instance> lines;
  std::vector<triangle_vertex> triangles;

  static std::array<float, 3> packed(vec3 point) {
    return {static_cast<float>(point.x), static_cast<float>(point.y),
            static_cast<float>(point.z)};
  }
  void add_line(geo::segment segment, std::array<float, 4> color, float width,
                float dash) {
    // Subtract in double precision before uploading float GPU coordinates.
    lines.push_back({packed(segment[0] - origin), packed(segment[1] - origin),
                     color, width, dash});
  }
  void add_triangle(const geo::triangle &triangle, std::array<float, 4> color) {
    for (auto point : triangle.points)
      triangles.push_back(
          {packed(point - origin), packed(triangle.normal), color});
  }
};

enum class display_style { wireframe, shaded, shaded_edges, xray };
struct render_options {
  display_style style{display_style::wireframe};
  bool light_theme{}, lineweights{};
};
inline std::array<float, 4> render_color(std::string hex, bool light = false,
                                         float alpha = 1) {
  if (hex.empty())
    hex = "#dce4ed";
  unsigned value = 0;
  auto result =
      std::from_chars(hex.data() + 1, hex.data() + hex.size(), value, 16);
  if (result.ec != std::errc{})
    throw std::invalid_argument("Invalid render color");
  std::array<float, 4> color{float((value >> 16) & 255) / 255,
                             float((value >> 8) & 255) / 255,
                             float(value & 255) / 255, alpha};
  if (light) {
    auto lum = color[0] * .2126 + color[1] * .7152 + color[2] * .0722;
    if (lum > .72)
      return {38.f / 255, 59.f / 255, 78.f / 255, alpha};
    if (lum > .4)
      for (size_t i = 0; i < 3; ++i)
        color[i] = float(std::floor(color[i] * .72 * 255 + .5) / 255);
  }
  return color;
}
struct render_text {
  json text;
  std::array<float, 4> color;
  bool dimension{}, selected{};
};
struct render_scene {
  render_data buffers;
  std::vector<render_text> texts;
  size_t entity_count{};
};
inline render_scene build_scene(const drawing &document, vec3 origin,
                                render_options options = {}) {
  render_scene scene;
  scene.buffers.origin = origin;
  for (auto &e : document.data.at("entities")) {
    if (!document.visible(e))
      continue;
    ++scene.entity_count;
    auto g = geo::geometry(e);
    auto &layer = document.layer(e);
    bool selected = document.selection.contains(e.at("id").get<std::string>()),
         locked = layer.value("locked", false), mesh = e.at("type") == "MESH";
    auto raw = e.value("color", std::string{});
    if (raw.empty() || raw == "bylayer" || raw == "byblock")
      raw = layer.value("color", std::string{"#dce4ed"});
    auto color =
        selected ? render_color("#63d7eb", false, locked ? .42f : 1)
                 : render_color(raw, options.light_theme, locked ? .42f : 1);
    auto linetype = e.value("linetype", std::string{});
    if (linetype.empty() || linetype == "ByLayer")
      linetype = layer.value("linetype", std::string{});
    std::transform(linetype.begin(), linetype.end(), linetype.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    float dash = linetype.find("center") != std::string::npos ? 2
                 : linetype.find("dash") != std::string::npos ? 1
                                                              : 0;
    auto weight = geo::number(e, "lineweight", 0);
    if (weight == 0)
      weight = geo::number(layer, "lineweight", .25);
    if (weight == 0)
      weight = .25;
    float width = selected              ? 2.1f
                  : options.lineweights ? float(std::max(.8, weight * 4))
                  : mesh                ? .75f
                                        : 1.05f;
    if (!(mesh && options.style == display_style::shaded)) {
      auto edge_color =
          mesh && options.style != display_style::wireframe && !selected
              ? render_color(options.light_theme ? "#314958" : "#13222e", false,
                             .62f)
              : color;
      for (auto segment : mesh &&options.style == display_style::wireframe
                              ? g.wire_segments
                              : g.segments)
        scene.buffers.add_line(segment, edge_color, width, dash);
    }
    if (!mesh || options.style != display_style::wireframe) {
      auto fill = color;
      if (mesh && options.style == display_style::xray)
        fill[3] = .25f;
      for (auto &t : g.triangles)
        scene.buffers.add_triangle(t, fill);
    }
    auto text_color = color;
    text_color[3] = locked ? .4f : 1;
    for (auto &t : g.texts)
      scene.texts.push_back(
          {t, text_color, e.at("type") == "DIMENSION", selected});
  }
  return scene;
}

struct camera_uniforms {
  std::array<float, 16> mvp;
  std::array<float, 4> eye, viewport;
};
static_assert(sizeof(camera_uniforms) == 96);
static_assert(offsetof(camera_uniforms, eye) == 64);
static_assert(offsetof(camera_uniforms, viewport) == 80);
inline camera_uniforms make_camera_uniforms(const camera &camera, vec3 origin,
                                            float width, float height,
                                            render_options options = {}) {
  camera_uniforms out;
  auto relative = multiply(camera.combined, translation(origin));
  for (size_t i = 0; i < relative.size(); ++i)
    out.mvp[i] = static_cast<float>(relative[i]);
  auto eye = camera.eye - origin;
  out.eye = {static_cast<float>(eye.x), static_cast<float>(eye.y),
             static_cast<float>(eye.z), 1};
  out.viewport = {width, height,
                  options.style == display_style::wireframe ? 0.f : 1.f,
                  options.light_theme ? 1.f : 0.f};
  return out;
}

inline double grid_spacing(double zoom) {
  auto spacing =
      std::pow(10., std::floor(std::log10(28 / std::max(zoom, 1e-9))));
  if (spacing * zoom < 14)
    spacing *= 2;
  if (spacing * zoom < 14)
    spacing *= 2.5;
  return spacing;
}
inline render_data build_grid(const camera &camera, vec3 origin,
                              bool light = false, bool enabled = true) {
  render_data data;
  data.origin = origin;
  if (!enabled)
    return data;
  auto spacing = grid_spacing(camera.zoom), cx = camera.target.x,
       cy = camera.target.y;
  auto range = std::min(
      std::max(camera.width, camera.height) / camera.zoom * .95, spacing * 90);
  auto n = static_cast<int>(std::ceil(range / spacing));
  auto x0 = std::floor(cx / spacing) * spacing,
       y0 = std::floor(cy / spacing) * spacing;
  auto minor = render_color(light ? "#d9e0e7" : "#253446", false, .7f),
       major = render_color(light ? "#c7d1db" : "#34485c", false, .8f);
  for (int i = -n; i <= n; ++i) {
    auto x = x0 + i * spacing, y = y0 + i * spacing;
    auto major_x = std::fmod(std::floor(x / spacing + .5), 5) == 0;
    auto major_y = std::fmod(std::floor(y / spacing + .5), 5) == 0;
    data.add_line({vec3{x, y0 - range, -.02}, vec3{x, y0 + range, -.02}},
                  major_x ? major : minor, .55f, 0);
    data.add_line({vec3{x0 - range, y, -.02}, vec3{x0 + range, y, -.02}},
                  major_y ? major : minor, .55f, 0);
  }
  if (std::abs(cx) < range)
    data.add_line({vec3{0, y0 - range, 0}, vec3{0, y0 + range, 0}},
                  render_color("#52947d", false, .55f), .9f, 0);
  if (std::abs(cy) < range)
    data.add_line({vec3{x0 - range, 0, 0}, vec3{x0 + range, 0, 0}},
                  render_color("#a16169", false, .55f), .9f, 0);
  return data;
}
// Native equivalent of app.js hit/selectAt. Spatial indexing can be added
// without changing the screen-space distance and depth ordering below.
struct object_snap_result {
  vec3 point,screen;
  std::string type,entity_id;
  double distance{};
};
// Baseline candidate selection; a retained spatial index can supply this same result.
inline std::optional<object_snap_result> nearest_object_snap(const drawing& model,const camera& camera,
    double x,double y,std::string_view exclude_id={}) {
  std::optional<object_snap_result> best;
  for(const auto& entity:model.data.at("entities")) {
    const auto id=entity.at("id").get<std::string>();
    if(id==exclude_id || !model.visible(entity))continue;
    const auto geometry=geo::geometry(entity);
    for(const auto& snap:geometry.snaps) {
      const auto screen=camera.project(snap.point);
      if(screen.z<0 || screen.z>1)continue;
      const auto distance=std::hypot(screen.x-x,screen.y-y);
      if(distance<11 && (!best || distance<best->distance))
        best=object_snap_result{snap.point,screen,snap.type,id,distance};
    }
  }
  return best;
}
// Native callers must invalidate after direct model.data writes outside transactions.
class object_snap_index {
  const drawing* owner_{};
  uint64_t revision_{};
  std::vector<object_snap_result> candidates_;
  size_t builds_{};
  const kestrel::camera* camera_{};
  uint64_t camera_revision_{};
  size_t projections_{},examined_{};
  std::map<std::pair<double,double>,std::vector<size_t>> cells_;

public:
  void invalidate() { owner_=nullptr; }
  size_t geometry_builds() const { return builds_; }
  size_t projection_builds() const { return projections_; }
  size_t examined_candidates() const { return examined_; }
  std::optional<object_snap_result> nearest(const drawing& model,const camera& camera,double x,double y,std::string_view excluded={}) {
    if(owner_!=&model || revision_!=model.revision) {
      std::vector<object_snap_result> next;
      for(const auto& entity:model.data.at("entities")) {
        if(!model.visible(entity))continue;
        const auto id=entity.at("id").get<std::string>();
        for(const auto& snap:geo::geometry(entity).snaps)next.push_back({snap.point,{},snap.type,id,0});
      }
      candidates_=std::move(next);owner_=&model;revision_=model.revision;++builds_;camera_=nullptr;
    }
    if(camera_!=&camera || camera_revision_!=camera.revision) {
      cells_.clear();
      for(size_t i=0;i<candidates_.size();++i) {
        auto& candidate=candidates_[i];candidate.screen=camera.project(candidate.point);
        const auto p=candidate.screen;
        if(p.z<0 || p.z>1 || !std::isfinite(p.x) || !std::isfinite(p.y))continue;
        cells_[{std::floor(p.x/11),std::floor(p.y/11)}].push_back(i);
      }
      camera_=&camera;camera_revision_=camera.revision;++projections_;
    }
    examined_=0;
    if(!std::isfinite(x) || !std::isfinite(y))return {};
    std::optional<object_snap_result> best;size_t best_order=candidates_.size();
    const auto cx=std::floor(x/11),cy=std::floor(y/11);
    for(int row=-1;row<=1;++row)for(int column=-1;column<=1;++column) {
      const auto cell=cells_.find({cx+column,cy+row});if(cell==cells_.end())continue;
      for(auto order:cell->second) {
        ++examined_;const auto& candidate=candidates_[order];
        if(candidate.entity_id==excluded)continue;
        const auto distance=std::hypot(candidate.screen.x-x,candidate.screen.y-y);
        if(distance<11 && (!best || distance<best->distance || (distance==best->distance && order<best_order))) {
          best=candidate;best->distance=distance;best_order=order;
        }
      }
    }
    return best;
  }
};
struct pick_result {std::string id;double distance,depth;};
inline std::optional<pick_result> pick(const drawing& model,const camera& camera,
    display_style style,double x,double y) {
  std::optional<pick_result> best;
  const auto segment_distance=[&](vec3 a,vec3 b) {
    const auto dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy;
    const auto t=length>0?std::clamp(((x-a.x)*dx+(y-a.y)*dy)/length,0.0,1.0):0;
    return std::pair{std::hypot(x-a.x-t*dx,y-a.y-t*dy),a.z*(1-t)+b.z*t};
  };
  const auto inside=[&](const auto& points) {
    bool result=false;
    for(size_t i=0,j=points.size()-1;i<points.size();j=i++) {
      const auto a=points[i],b=points[j];
      if((a.y>y)!=(b.y>y) && x<(b.x-a.x)*(y-a.y)/(b.y-a.y)+a.x) result=!result;
    }
    return result;
  };
  for(const auto& entity:model.data["entities"]) {
    if(!model.visible(entity))continue;
    const auto geometry=geo::geometry(entity);
    double distance=std::numeric_limits<double>::infinity(),depth=1;
    const auto& segments=entity["type"]=="MESH" && style==display_style::wireframe?
      geometry.wire_segments:geometry.segments;
    for(const auto& segment:segments) {
      auto [d,z]=segment_distance(camera.project(segment[0]),camera.project(segment[1]));
      if(d<distance){distance=d;depth=z;}
    }
    for(const auto& text:geometry.texts) {
      const auto axes=geo::axes_for_text(text);const auto position=geo::point(text.at("position"));
      const auto height=text.value("height",10.0);
      const auto content=text.value("text",std::string{});size_t lines=1,longest=0,current=0;
      for(char c:content){if(c=='\n'){longest=std::max(longest,current);current=0;++lines;}else if((static_cast<unsigned char>(c)&0xc0)!=0x80)++current;}
      longest=std::max(longest,current);
      const auto width=std::max(height,double(longest)*height*.66);
      const auto align=text.value("align",std::string("left"));
      const auto left=align=="center"?-width/2:align=="right"?-width:0;
      const auto bottom=-height*.25-double(lines-1)*height*1.35,top=height*.9;
      std::array<vec3,4> quad{camera.project(position+axes.x*left+axes.y*bottom),
        camera.project(position+axes.x*(left+width)+axes.y*bottom),
        camera.project(position+axes.x*(left+width)+axes.y*top),camera.project(position+axes.x*left+axes.y*top)};
      bool hit=inside(quad);for(size_t i=0;i<4;++i)hit=hit || segment_distance(quad[i],quad[(i+1)%4]).first<4;
      if(hit){distance=2;depth=camera.project(position).z;}
    }
    if(distance>8 && entity["type"]=="MESH" && style!=display_style::wireframe) {
      double face_depth=std::numeric_limits<double>::infinity();
      for(const auto& triangle:geometry.triangles) {
        std::array<vec3,3> face;for(size_t i=0;i<3;++i)face[i]=camera.project(triangle.points[i]);
        if(!inside(face))continue;
        const auto a=face[0],b=face[1],c=face[2];
        const auto den=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);if(std::abs(den)<1e-10)continue;
        const auto u=((b.y-c.y)*(x-c.x)+(c.x-b.x)*(y-c.y))/den;
        const auto v=((c.y-a.y)*(x-c.x)+(a.x-c.x)*(y-c.y))/den;
        const auto z=u*a.z+v*b.z+(1-u-v)*c.z;if(z>=0 && z<=1)face_depth=std::min(face_depth,z);
      }
      if(std::isfinite(face_depth)){distance=7.5;depth=face_depth;}
    }
    if(distance<=9 && (!best || distance<best->distance-.3 ||
        (std::abs(distance-best->distance)<.3 && depth<best->depth)))
      best=pick_result{entity.at("id").get<std::string>(),distance,depth};
  }
  return best;
}
inline void select_at(drawing& model,const camera& camera,display_style style,
    double x,double y,bool add=false,bool ignore_group=false) {
  const auto hit=pick(model,camera,style,x,y);
  if(!hit){if(!add)model.selection.clear();return;}
  std::vector<std::string> ids{hit->id};
  for(const auto& e:model.data["entities"])if(e.at("id")==hit->id && !ignore_group && e.contains("group") && !e["group"].is_null() && e["group"]!="") {
    ids.clear();for(const auto& other:model.data["entities"])if(other.value("group",json{})==e["group"] && model.visible(other))ids.push_back(other.at("id").get<std::string>());break;
  }
  const bool remove=add && model.selection.contains(hit->id);
  if(!add)model.selection.clear();
  for(const auto& id:ids)if(remove)model.selection.erase(id);else model.selection.insert(id);
}

inline void select_window(drawing& model,const camera& camera,display_style style,
    vec3 start,vec3 end,bool add=false) {
  const double left=std::min(start.x,end.x),right=std::max(start.x,end.x),
    top=std::min(start.y,end.y),bottom=std::max(start.y,end.y);
  const bool crossing=end.x<start.x;
  const auto within=[&](vec3 p){return p.x>=left && p.x<=right && p.y>=top && p.y<=bottom;};
  const std::array<vec3,4> corners{vec3{left,top,0},vec3{right,top,0},vec3{right,bottom,0},vec3{left,bottom,0}};
  std::array<geo::segment,4> edges;for(size_t i=0;i<4;++i)edges[i]={corners[i],corners[(i+1)%4]};
  std::vector<std::string> ids;
  for(const auto& entity:model.data["entities"]) {
    if(!model.editable(entity))continue;
    const auto g=geo::geometry(entity);if(g.points.empty())continue;
    double x0=INFINITY,y0=INFINITY,x1=-INFINITY,y1=-INFINITY;
    for(auto point:g.points){point=camera.project(point);x0=std::min(x0,point.x);y0=std::min(y0,point.y);x1=std::max(x1,point.x);y1=std::max(y1,point.y);}
    bool hit=!crossing && x0>=left && y0>=top && x1<=right && y1<=bottom;
    if(crossing && !(x1<left || x0>right || y1<top || y0>bottom)) {
      hit=!g.texts.empty();
      const auto& segments=entity["type"]=="MESH" && style==display_style::wireframe?g.wire_segments:g.segments;
      for(const auto& segment:segments) {
        const auto a=camera.project(segment[0]),b=camera.project(segment[1]);
        hit=hit || within(a) || within(b);
        for(const auto& edge:edges)if(auto h=line_intersection(a,b,edge[0],edge[1]))
          hit=hit || (h->t>=0 && h->t<=1 && h->u>=0 && h->u<=1);
      }
      if(style!=display_style::wireframe)for(const auto& triangle:g.triangles) {
        const auto a=camera.project(triangle.points[0]),b=camera.project(triangle.points[1]),c=camera.project(triangle.points[2]);
        const auto cross=[](vec3 p,vec3 q,double x,double y){return (q.x-p.x)*(y-p.y)-(q.y-p.y)*(x-p.x);};
        const auto ab=cross(a,b,left,top),bc=cross(b,c,left,top),ca=cross(c,a,left,top);
        if(std::abs(cross(a,b,c.x,c.y))>1e-10)hit=hit || ((ab>=0 && bc>=0 && ca>=0)||(ab<=0 && bc<=0 && ca<=0));
      }
    }
    if(!hit)continue;
    ids.push_back(entity.at("id").get<std::string>());
    if(entity.contains("group") && entity["group"].is_string() && entity["group"]!="")
      for(const auto& other:model.data["entities"])if(other.value("group",json{})==entity["group"] && model.editable(other))ids.push_back(other.at("id").get<std::string>());
  }
  if(!add)model.selection.clear();
  for(const auto& id:ids)model.selection.insert(id);
}

} // namespace kestrel
