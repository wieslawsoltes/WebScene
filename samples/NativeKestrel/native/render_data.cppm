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
export module kestrel.render_data;
export import kestrel.geometry;

export namespace kestrel {
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
} // namespace kestrel
