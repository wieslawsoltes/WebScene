#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>
import kestrel.math;
int main() {
  using namespace kestrel;
  auto m = multiply(translation({10, -3, 5}),
                    multiply(rotation(0.8, {1, 2, 3}), scale({2, 3, 4})));
  auto inv = inverse(m);
  if (!inv)
    throw std::runtime_error("invertible transform rejected");
  for (auto p : {vec3{0, 0, 0}, vec3{100, -40, 2}, vec3{1e5, 2e5, -3e5}})
    if ((transform(*inv, transform(m, p)) - p).length() > 1e-8)
      throw std::runtime_error("transform round trip");
  if (inverse(scale({1, 0, 1})))
    throw std::runtime_error("singular transform accepted");
  auto cross =
      line_intersection({0, 0, 0}, {10, 10, 0}, {0, 10, 0}, {10, 0, 0});
  if (!cross || (cross->point - vec3{5, 5, 0}).length() > epsilon)
    throw std::runtime_error("line intersection");
  if (line_intersection({0, 0, 0}, {10, 0, 0}, {0, 2, 0}, {10, 2, 0}))
    throw std::runtime_error("parallel intersection");
  std::array<vec3, 4> polygon{{{0, 0, 0}, {10, 0, 0}, {10, 4, 0}, {0, 4, 0}}};
  if (polygon_area(polygon) != 40 || std::abs(sweep(0, 0) - tau) > epsilon)
    throw std::runtime_error("area/sweep");
  std::array<vec3, 5> concave{
      {{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {2, 2, 0}, {0, 4, 0}}};
  auto triangles = triangulate(concave);
  double area = 0;
  for (auto t : triangles) {
    std::array<vec3, 3> face{concave[t[0]], concave[t[1]], concave[t[2]]};
    area += polygon_area(face);
  }
  if (triangles.size() != 3 || std::abs(area - polygon_area(concave)) > epsilon)
    throw std::runtime_error("concave triangulation");
  for (auto &p : concave) {
    p.z = p.y;
    p.y = 0;
  }
  triangles = triangulate(concave);
  if (triangles.size() != 3)
    throw std::runtime_error("vertical triangulation");
  auto closest = segment_distance({20, 3, 0}, {0, 0, 0}, {10, 0, 0});
  if (closest.t != 1 || closest.point.x != 10)
    throw std::runtime_error("segment endpoint clamp");
  if (!inside({5, 2, 0}, polygon) || inside({20, 2, 0}, polygon))
    throw std::runtime_error("polygon containment");
}
