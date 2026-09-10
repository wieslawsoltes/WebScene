module;
#include <array>
#include <cstddef>
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
} // namespace kestrel
