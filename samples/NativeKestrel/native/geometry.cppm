module;
#include "../third_party/nlohmann/json.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

export module kestrel.geometry;
export import kestrel.math;
export import kestrel.drawing;

// Ported from KestrelCAD src/geometry.js at the README's pinned revision.
export namespace kestrel::geo {
inline vec3 point(const json &p) {
  return {p.at(0).get<double>(), p.at(1).get<double>(),
          p.size() > 2 ? p.at(2).get<double>() : 0};
}
inline std::vector<vec3> points(const json &array) {
  std::vector<vec3> out;
  for (auto &p : array)
    out.push_back(point(p));
  return out;
}
inline vec3 vector_property(const json &e, const char *key, vec3 fallback) {
  return e.contains(key) && !e[key].is_null() ? point(e[key]) : fallback;
}
inline double number(const json &e, const char *key, double fallback) {
  return e.contains(key) && !e[key].is_null() ? e[key].get<double>() : fallback;
}
struct conic_axes {
  vec3 x, y;
};
inline conic_axes axes(const json &e) {
  if (e.contains("axisX") && e.contains("axisY"))
    return {point(e["axisX"]), point(e["axisY"])};
  auto b = basis(vector_property(e, "normal", {0, 0, 1}));
  auto r = number(e, "rotation", 0);
  return {(b.x * std::cos(r) + b.y * std::sin(r)) *
              number(e, "rx", number(e, "radius", 1)),
          (b.x * -std::sin(r) + b.y * std::cos(r)) *
              number(e, "ry", number(e, "radius", 1))};
}
inline vec3 conic_point(const json &e, double t) {
  auto a = axes(e);
  return point(e.at("center")) + a.x * std::cos(t) + a.y * std::sin(t);
}
inline bool change_ellipse_radius(drawing& model,bool minor,double radius) {
  if(!std::isfinite(radius) || radius<=0)return false;
  const auto ids=model.selected(true);if(ids.size()!=1)return false;
  auto* entity=model.find(ids.front());if(!entity || entity->value("type",std::string{})!="ELLIPSE")return false;
  auto basis=axes(*entity);
  auto& axis=minor?basis.y:basis.x;
  const double length=std::sqrt(axis.x*axis.x+axis.y*axis.y+axis.z*axis.z);
  if(!std::isfinite(length) || length<=0)return false;
  axis=axis*(radius/length);
  return model.transaction(minor?"Edit ry":"Edit rx",[&] {
    (*entity)["axisX"]={basis.x.x,basis.x.y,basis.x.z};
    (*entity)["axisY"]={basis.y.x,basis.y.y,basis.y.z};
    (*entity)[minor?"ry":"rx"]=radius;
  });
}
inline std::vector<double> uniform_knots(size_t count, size_t degree) {
  std::vector<double> out;
  for (size_t i = 0; i < count + degree + 1; ++i)
    out.push_back(i <= degree  ? 0
                  : i >= count ? 1
                               : double(i - degree) / (count - degree));
  return out;
}
inline bool change_spline_degree(drawing& model,double value) {
  if(!std::isfinite(value))return false;
  const auto ids=model.selected(true);if(ids.size()!=1)return false;
  auto* entity=model.find(ids.front());if(!entity || entity->value("type",std::string{})!="SPLINE")return false;
  const auto& points=entity->contains("controlPoints")?entity->at("controlPoints"):entity->at("points");
  if(points.size()<2)return false;
  const auto degree=size_t(std::clamp(std::floor(value+.5),1.0,double(std::min(size_t(10),points.size()-1))));
  return model.transaction("Edit degree",[&] {(*entity)["degree"]=degree;(*entity)["knots"]=uniform_knots(points.size(),degree);});
}
inline vec3 nurbs(const json &e, double t) {
  auto p =
      points(e.contains("controlPoints") ? e["controlPoints"] : e.at("points"));
  if (p.size() < 2)
    throw std::invalid_argument("NURBS requires at least two points");
  size_t degree = std::min<size_t>(e.value("degree", 3), p.size() - 1);
  if (!degree)
    throw std::invalid_argument("NURBS degree must be positive");
  auto knots = e.contains("knots") && e["knots"].size() == p.size() + degree + 1
                   ? e["knots"].get<std::vector<double>>()
                   : uniform_knots(p.size(), degree);
  auto weights = e.contains("weights") ? e["weights"].get<std::vector<double>>()
                                       : std::vector<double>{};
  size_t k = degree;
  auto lo = knots[degree], hi = knots[p.size()],
       u = lo + (hi - lo) * std::clamp(t, 0., 1.);
  while (k < p.size() - 1 && knots[k + 1] <= u)
    ++k;
  std::vector<std::array<double, 4>> d;
  for (size_t j = 0; j <= degree; ++j) {
    auto i = k - degree + j;
    double w = i < weights.size() ? weights[i] : 1;
    d.push_back({p[i].x * w, p[i].y * w, p[i].z * w, w});
  }
  for (size_t r = 1; r <= degree; ++r)
    for (size_t j = degree; j >= r; --j) {
      auto i = k - degree + j;
      auto den = knots[i + degree - r + 1] - knots[i],
           a = den > 0 ? (u - knots[i]) / den : 0;
      for (size_t c = 0; c < 4; ++c)
        d[j][c] = (1 - a) * d[j - 1][c] + a * d[j][c];
    }
  auto w = d[degree][3] ? d[degree][3] : 1;
  return {d[degree][0] / w, d[degree][1] / w, d[degree][2] / w};
}
inline std::vector<vec3> polyline_points(const json &e, double tolerance = 1) {
  auto p = e.contains("points") ? points(e["points"]) : std::vector<vec3>{};
  std::vector<vec3> out;
  for (size_t i = 0; i < p.size(); ++i) {
    out.push_back(p[i]);
    if (i == p.size() - 1 && !e.value("closed", false))
      break;
    auto b = e.contains("bulges") && i < e["bulges"].size()
                 ? e["bulges"][i].get<double>()
                 : 0.;
    if (std::abs(b) < epsilon)
      continue;
    auto a = p[i], z = p[(i + 1) % p.size()];
    auto chord = (z - a).length();
    if (chord < epsilon)
      continue;
    auto normal = vector_property(e, "normal", {0, 0, 1}),
         left = normal.cross(z - a).normalized();
    auto center = lerp(a, z, .5) + left * (chord * (1 - b * b) / (4 * b));
    auto radius = chord * (1 + b * b) / (4 * std::abs(b)),
         delta = 4 * std::atan(b);
    int steps =
        int(std::clamp(std::ceil(std::abs(delta) *
                                 std::sqrt(radius / std::max(.01, tolerance))),
                       4., 256.));
    for (int k = 1; k < steps; ++k)
      out.push_back(center + transform(rotation(delta * k / steps, normal),
                                       a - center, 0));
  }
  return out;
}
inline std::vector<vec3> path(const json &e, double tolerance = .5) {
  auto type = e.at("type").get<std::string>();
  if (type == "LINE" || type == "HATCH")
    return points(e.at("points"));
  if (type == "POLYLINE")
    return polyline_points(e, tolerance);
  if (type == "CIRCLE" || type == "ARC" || type == "ELLIPSE") {
    auto a = number(e, "startAngle", 0),
         d = type == "ARC" || (type == "ELLIPSE" && e.contains("endAngle") &&
                               !e["endAngle"].is_null())
                 ? sweep(a, e.at("endAngle").get<double>())
                 : tau;
    auto ax = axes(e);
    auto r = std::max(ax.x.length(), ax.y.length());
    int n = int(std::clamp(
        std::ceil(d * std::sqrt(r / std::max(.001, tolerance))), 24., 512.));
    std::vector<vec3> out;
    for (int i = 0; i < n + (d < tau - epsilon ? 1 : 0); ++i)
      out.push_back(conic_point(e, a + d * i / n));
    return out;
  }
  if (type == "SPLINE") {
    auto count = e.contains("controlPoints") ? e["controlPoints"].size()
                 : e.contains("points")      ? e["points"].size()
                                             : 0;
    if (count < 2)
      return {};
    auto n = std::min<size_t>(512, std::max<size_t>(32, count * 20));
    std::vector<vec3> out;
    for (size_t i = 0; i <= n; ++i)
      out.push_back(nurbs(e, double(i) / n));
    return out;
  }
  if (type == "POINT" || type == "TEXT")
    return {point(e.at("position"))};
  return {};
}
inline bool closed(const json &e) {
  auto t = e.at("type");
  return t == "CIRCLE" || t == "HATCH" ||
         (t == "POLYLINE" && e.value("closed", false)) ||
         (t == "ELLIPSE" &&
          (!e.contains("endAngle") || e["endAngle"].is_null() ||
           sweep(number(e, "startAngle", 0), e["endAngle"].get<double>()) >
               tau - epsilon));
}

using segment = std::array<vec3, 2>;
inline std::vector<segment> hatch_segments(const json &e) {
  auto p = e.contains("points") ? points(e["points"]) : std::vector<vec3>{};
  if (p.size() < 3)
    return {};
  auto b = basis(vector_property(e, "normal", face_normal(p)));
  auto origin = p.front();
  std::vector<vec3> flat;
  for (auto v : p) {
    auto d = v - origin;
    flat.push_back({d.dot(b.x), d.dot(b.y), 0});
  }
  auto spacing = number(e, "spacing", 10);
  if (spacing == 0)
    spacing = 10;
  spacing = std::max(spacing, .001);
  std::vector<double> angles{number(e, "angle", std::numbers::pi / 4)};
  if (e.value("pattern", std::string{}) == "cross")
    angles.push_back(angles.front() + std::numbers::pi / 2);
  std::vector<segment> result;
  for (auto a : angles) {
    auto c = std::cos(a), s = std::sin(a);
    std::vector<vec3> q;
    double low = std::numeric_limits<double>::infinity(), high = -low;
    for (auto v : flat) {
      vec3 t{v.x * c + v.y * s, -v.x * s + v.y * c, 0};
      q.push_back(t);
      low = std::min(low, t.y);
      high = std::max(high, t.y);
    }
    auto step = spacing;
    if ((high - low) / step > 3000)
      step = (high - low) / 3000;
    for (double y = std::ceil(low / step) * step; y < high; y += step) {
      std::vector<double> xs;
      for (size_t i = 0, j = q.size() - 1; i < q.size(); j = i++) {
        auto v = q[i], w = q[j];
        if ((v.y <= y && w.y > y) || (w.y <= y && v.y > y))
          xs.push_back(v.x + (y - v.y) * (w.x - v.x) / (w.y - v.y));
      }
      std::sort(xs.begin(), xs.end());
      auto world = [&](double x) {
        return origin + b.x * (x * c - y * s) + b.y * (x * s + y * c);
      };
      for (size_t k = 0; k + 1 < xs.size(); k += 2)
        result.push_back({world(xs[k]), world(xs[k + 1])});
    }
  }
  return result;
}

inline json encode(vec3 p) { return {p.x, p.y, p.z}; }
struct text_axes {
  vec3 x, y, n;
};
inline text_axes axes_for_text(const json &e) {
  auto b = basis(vector_property(e, "normal", {0, 0, 1}));
  auto r = number(e, "rotation", 0);
  auto x = e.contains("direction") ? point(e["direction"]).normalized()
                                   : b.x * std::cos(r) + b.y * std::sin(r);
  return {x, b.n.cross(x).normalized(), b.n};
}
struct dimension_geometry {
  std::vector<segment> segments;
  json text;
};
inline dimension_geometry dimension(const json &e) {
  auto a = point(e.at("points").at(0)), b = point(e.at("points").at(1)),
       d = b - a;
  auto length = d.length();
  auto u = d.normalized(), normal = vector_property(e, "normal", {0, 0, 1}),
       n = normal.cross(u).normalized();
  auto offset = number(e, "offset", length * .12),
       height = number(e, "textHeight", 0);
  if (height == 0)
    height = std::max(1., length * .025);
  auto aa = a + n * offset, bb = b + n * offset, mid = lerp(aa, bb, .5);
  auto sign = double((offset > 0) - (offset < 0));
  dimension_geometry out{
      {{a + n * (sign * height * .3), aa + n * (sign * height * .8)},
       {b + n * (sign * height * .3), bb + n * (sign * height * .8)},
       {aa, bb}},
      json{}};
  for (auto [p, dir] : {std::pair{aa, 1.}, std::pair{bb, -1.}}) {
    out.segments.push_back({p, p + u * (dir * height) + n * (height * .28)});
    out.segments.push_back({p, p + u * (dir * height) - n * (height * .28)});
  }
  auto label = e.value("text", std::string{});
  if (label.empty()) {
    auto precision = e.value("precision", 0);
    if (precision < 0 || precision > 100)
      throw std::invalid_argument(
          "Dimension precision must be between 0 and 100");
    std::array<char, 512> buffer{};
    auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                length, std::chars_format::fixed, precision);
    if (result.ec != std::errc{})
      throw std::runtime_error("Dimension label formatting failed");
    label.assign(buffer.data(), result.ptr);
  }
  out.text = {{"position", encode(mid + n * (height * .5))},
              {"text", label},
              {"height", height},
              {"rotation", std::atan2(u.y, u.x)},
              {"direction", encode(u)},
              {"normal", encode(normal)},
              {"align", "center"}};
  return out;
}

inline json box(vec3 p, double w, double d, double h) {
  json v = json::array();
  for (auto q : {vec3{0, 0, 0}, vec3{w, 0, 0}, vec3{w, d, 0}, vec3{0, d, 0},
                 vec3{0, 0, h}, vec3{w, 0, h}, vec3{w, d, h}, vec3{0, d, h}})
    v.push_back(encode(p + q));
  return {{"type", "MESH"},
          {"primitive", "Box"},
          {"vertices", v},
          {"faces",
           {{3, 2, 1, 0},
            {4, 5, 6, 7},
            {0, 1, 5, 4},
            {1, 2, 6, 5},
            {2, 3, 7, 6},
            {3, 0, 4, 7}}}};
}
inline json cylinder(vec3 p, double radius, double height, int n = 64,
                     std::optional<double> top_radius = {}) {
  if (n < 3)
    throw std::invalid_argument("Cylinder requires at least three segments");
  double top = top_radius.value_or(radius);
  json vertices = json::array(), faces = json::array();
  for (int j = 0; j < 2; ++j)
    for (int i = 0; i < n; ++i) {
      auto a = double(i) / n * tau, r = j ? top : radius;
      vertices.push_back(
          encode(p + vec3{std::cos(a) * r, std::sin(a) * r, j * height}));
    }
  json bottom = json::array(), upper = json::array();
  for (int i = 0; i < n; ++i) {
    bottom.push_back(n - i - 1);
    upper.push_back(n + i);
  }
  faces.push_back(bottom);
  if (top > epsilon)
    faces.push_back(upper);
  for (int i = 0; i < n; ++i)
    faces.push_back({i, (i + 1) % n, (i + 1) % n + n, i + n});
  return {{"type", "MESH"},
          {"primitive", top == radius ? "Cylinder" : "Cone"},
          {"vertices", vertices},
          {"faces", faces}};
}
inline json sphere(vec3 p, double r, int n = 40, int rings = 20) {
  if (n < 3 || rings < 2)
    throw std::invalid_argument("Invalid sphere subdivisions");
  json vertices = json::array(), faces = json::array();
  vertices.push_back(encode(p + vec3{0, 0, -r}));
  for (int j = 1; j < rings; ++j) {
    auto t = -std::numbers::pi / 2 + double(j) / rings * std::numbers::pi;
    for (int i = 0; i < n; ++i)
      vertices.push_back(
          encode(p + vec3{r * std::cos(t) * std::cos(double(i) / n * tau),
                          r * std::cos(t) * std::sin(double(i) / n * tau),
                          r * std::sin(t)}));
  }
  auto top = vertices.size();
  vertices.push_back(encode(p + vec3{0, 0, r}));
  for (int i = 0; i < n; ++i) {
    faces.push_back({0, 1 + (i + 1) % n, 1 + i});
    for (int j = 0; j < rings - 2; ++j) {
      auto a = 1 + j * n + i, b = 1 + j * n + (i + 1) % n;
      faces.push_back({a, b, b + n, a + n});
    }
    faces.push_back(
        {1 + (rings - 2) * n + i, 1 + (rings - 2) * n + (i + 1) % n, int(top)});
  }
  return {{"type", "MESH"},
          {"primitive", "Sphere"},
          {"vertices", vertices},
          {"faces", faces},
          {"smooth", true}};
}
inline json torus(vec3 p, double major, double minor, int n = 64, int m = 20) {
  if (n < 3 || m < 3)
    throw std::invalid_argument("Invalid torus subdivisions");
  json vertices = json::array(), faces = json::array();
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < m; ++j) {
      auto a = double(i) / n * tau, b = double(j) / m * tau;
      vertices.push_back(
          encode(p + vec3{(major + minor * std::cos(b)) * std::cos(a),
                          (major + minor * std::cos(b)) * std::sin(a),
                          minor * std::sin(b)}));
    }
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < m; ++j)
      faces.push_back({i * m + j, ((i + 1) % n) * m + j,
                       ((i + 1) % n) * m + (j + 1) % m, i * m + (j + 1) % m});
  return {{"type", "MESH"},
          {"primitive", "Torus"},
          {"vertices", vertices},
          {"faces", faces},
          {"smooth", true}};
}
inline double volume(const json &e) {
  if (e.at("type") != "MESH")
    return 0;
  double result = 0;
  for (auto &face : e.at("faces")) {
    std::vector<vec3> p;
    for (auto &i : face)
      p.push_back(point(e.at("vertices").at(i.get<size_t>())));
    for (auto t : triangulate(p))
      result += p[t[0]].dot(p[t[1]].cross(p[t[2]])) / 6;
  }
  return result;
}
inline json extrude(std::vector<vec3> p, double height,
                    std::optional<vec3> normal = {}) {
  if (!std::isfinite(height) || std::abs(height) < epsilon)
    throw std::invalid_argument("Extrusion height cannot be zero");
  if (p.size() > 2 && (p.front() - p.back()).length() < epsilon)
    p.pop_back();
  std::vector<vec3> distinct;
  for (size_t i = 0; i < p.size(); ++i)
    if (!i || (p[i] - p[i - 1]).length() >= epsilon)
      distinct.push_back(p[i]);
  p = std::move(distinct);
  bool changed = true;
  while (changed && p.size() > 3) {
    changed = false;
    for (size_t i = 0; i < p.size(); ++i) {
      auto a = p[i] - p[(i + p.size() - 1) % p.size()],
           b = p[(i + 1) % p.size()] - p[i];
      if (a.cross(b).length() <
              epsilon * std::max(1., a.length() * b.length()) &&
          a.dot(b) >= 0) {
        p.erase(p.begin() + i);
        changed = true;
        break;
      }
    }
  }
  if (p.size() < 3)
    throw std::invalid_argument(
        "Extrusion needs three distinct boundary points");
  auto n = normal ? normal->normalized() : face_normal(p);
  if (n.length() < epsilon)
    throw std::invalid_argument("Degenerate extrusion profile");
  double extent = 1;
  for (auto q : p)
    extent = std::max(extent, (q - p.front()).length());
  for (auto q : p)
    if (std::abs((q - p.front()).dot(n)) > extent * 1e-7)
      throw std::invalid_argument("Extrusion requires a planar profile");
  if (height < 0)
    n = n * -1;
  auto h = std::abs(height);
  json vertices = json::array(), faces = json::array();
  for (auto q : p)
    vertices.push_back(encode(q));
  for (auto q : p)
    vertices.push_back(encode(q + n * h));
  auto cap = triangulate(p);
  if (cap.size() < p.size() - 2)
    throw std::invalid_argument("Extrusion requires a simple polygon");
  bool same = face_normal(p).dot(n) > 0;
  for (auto t : cap) {
    auto bottom = t, top = t;
    if (same)
      std::reverse(bottom.begin(), bottom.end());
    else
      std::reverse(top.begin(), top.end());
    for (auto &i : top)
      i += p.size();
    faces.push_back(bottom);
    faces.push_back(top);
  }
  for (size_t i = 0; i < p.size(); ++i) {
    auto j = (i + 1) % p.size();
    std::array<size_t, 4> f{i, j, j + p.size(), i + p.size()};
    if (!same)
      std::reverse(f.begin(), f.end());
    faces.push_back(f);
  }
  return {{"type", "MESH"},
          {"primitive", "Extrusion"},
          {"vertices", vertices},
          {"faces", faces}};
}

inline json revolve(std::span<const vec3> p, vec3 origin, vec3 axis,
                    double degrees = 360, int segments = 64) {
  auto a = axis.normalized();
  if (a.length() < epsilon)
    throw std::invalid_argument("Revolution axis cannot have zero length");
  if (p.size() < 3)
    throw std::invalid_argument("Revolution requires a closed profile");
  auto normal = face_normal(p);
  if (normal.length() < epsilon)
    throw std::invalid_argument("Degenerate revolution profile");
  double extent = 1;
  for (auto q : p)
    extent = std::max(extent, (q - p.front()).length());
  auto tol = extent * 1e-7;
  if (std::abs(a.dot(normal)) > 1e-6 ||
      std::abs((origin - p.front()).dot(normal)) > tol)
    throw std::invalid_argument("Revolution axis must lie in profile plane");
  for (auto q : p)
    if (std::abs((q - p.front()).dot(normal)) > tol)
      throw std::invalid_argument("Revolution profile must be planar");
  auto radial = normal.cross(a);
  double low = std::numeric_limits<double>::infinity(), high = -low;
  for (auto q : p) {
    auto d = (q - origin).dot(radial);
    low = std::min(low, d);
    high = std::max(high, d);
  }
  if (low < -tol && high > tol)
    throw std::invalid_argument("Revolution profile must not cross axis");
  if (!std::isfinite(degrees) || std::abs(degrees) < 1e-7 ||
      std::abs(degrees) > 360 || segments < 3 || segments > 512)
    throw std::invalid_argument("Invalid revolution sweep or segment count");
  bool full = std::abs(degrees) >= 359.999;
  size_t n = segments, count = p.size();
  json vertices = json::array(), faces = json::array();
  for (size_t j = 0; j <= (full ? n - 1 : n); ++j) {
    auto m =
        around(origin, rotation(degrees * std::numbers::pi / 180 * j / n, a));
    for (auto q : p)
      vertices.push_back(encode(transform(m, q)));
  }
  for (size_t j = 0; j < n; ++j)
    for (size_t i = 0; i < count; ++i) {
      auto k = (i + 1) % count, next = (j + 1) % (full ? n : n + 1);
      faces.push_back(
          {j * count + i, j * count + k, next * count + k, next * count + i});
    }
  if (!full) {
    json start = json::array(), end = json::array();
    for (size_t i = 0; i < count; ++i) {
      start.push_back(count - 1 - i);
      end.push_back(n * count + i);
    }
    faces.push_back(start);
    faces.push_back(end);
  }
  json result = {{"type", "MESH"},
                 {"primitive", "Revolution"},
                 {"vertices", vertices},
                 {"faces", faces}};
  if (volume(result) < 0)
    for (auto &face : result["faces"])
      std::reverse(face.begin(), face.end());
  return result;
}

inline json offset(const json &e, double distance) {
  auto type = e.at("type").get<std::string>();
  if (type == "CIRCLE" || type == "ARC") {
    auto radius = number(e, "radius", 0);
    if (radius == 0)
      radius = axes(e).x.length();
    auto r = radius + distance;
    if (r <= epsilon)
      throw std::invalid_argument("Offset would create a non-positive radius");
    auto out = e;
    out["radius"] = r;
    for (auto key : {"axisX", "axisY"})
      if (out.contains(key))
        out[key] = encode(point(out[key]) * (r / radius));
    return out;
  }
  if (type != "LINE" && type != "POLYLINE")
    throw std::invalid_argument("Unsupported offset entity");
  bool bulged = false;
  if (e.contains("bulges"))
    for (auto &b : e["bulges"])
      if (!b.is_null() && b.get<double>() != 0)
        bulged = true;
  auto p = type == "POLYLINE" && bulged ? path(e) : points(e.at("points"));
  bool close = e.value("closed", false);
  if (p.size() < 2)
    throw std::invalid_argument("Offset needs at least two points");
  auto side = close ? (polygon_area(p) > 0 ? -distance : distance) : distance;
  std::vector<segment> segments;
  for (size_t i = 0; i < p.size() - (close ? 0 : 1); ++i) {
    auto a = p[i], b = p[(i + 1) % p.size()], v = b - a;
    auto length = std::hypot(v.x, v.y);
    if (length < epsilon)
      throw std::invalid_argument(
          "Offset profile contains a zero-length segment");
    vec3 shift{-v.y / length * side, v.x / length * side, 0};
    segments.push_back({a + shift, b + shift});
  }
  json result = e;
  result["points"] = json::array();
  result.erase("bulges");
  for (size_t i = 0; i < p.size(); ++i) {
    vec3 q;
    if (!close && i == 0)
      q = segments.front()[0];
    else if (!close && i == p.size() - 1)
      q = segments.back()[1];
    else {
      auto prev = segments[(i + segments.size() - 1) % segments.size()],
           cur = segments[i % segments.size()];
      auto hit = line_intersection(prev[0], prev[1], cur[0], cur[1]);
      q = hit && (hit->point - p[i]).length() < std::abs(distance) * 20
              ? hit->point
              : lerp(prev[1], cur[0], .5);
    }
    result["points"].push_back(encode(q));
  }
  return result;
}
inline json arc_through(vec3 a, vec3 b, vec3 c) {
  auto ab = lerp(a, b, .5), bc = lerp(b, c, .5), u = b - a, v = c - b;
  auto hit = line_intersection(ab, ab + vec3{-u.y, u.x, 0}, bc,
                               bc + vec3{-v.y, v.x, 0});
  if (!hit)
    throw std::invalid_argument("Three arc points must not be collinear");
  auto center = hit->point;
  auto aa = std::atan2(a.y - center.y, a.x - center.x),
       bb = std::atan2(b.y - center.y, b.x - center.x),
       cc = std::atan2(c.y - center.y, c.x - center.x);
  bool forward = angle(bb - aa) <= angle(cc - aa);
  return {{"type", "ARC"},
          {"center", encode(center)},
          {"radius", (center - a).length()},
          {"startAngle", forward ? aa : cc},
          {"endAngle", forward ? cc : aa}};
}

inline json transform_entity(const json &e, const matrix &m) {
  auto inv = inverse(m);
  if (!inv)
    throw std::invalid_argument("A transform cannot collapse an axis to zero");
  auto out = e;
  auto apply = [&](vec3 p, double w = 1) {
    return kestrel::transform(m, p, w);
  };
  for (auto key : {"points", "controlPoints", "vertices"})
    if (out.contains(key))
      for (auto &p : out[key])
        p = encode(apply(point(p)));
  if (out.contains("position"))
    out["position"] = encode(apply(point(out["position"])));
  auto sy = apply({0, 1, 0}, 0).length(),
       det = apply({1, 0, 0}, 0)
                 .dot(apply({0, 1, 0}, 0).cross(apply({0, 0, 1}, 0)));
  auto type = e.at("type").get<std::string>();
  if (out.contains("center")) {
    auto a = axes(e);
    auto x = apply(a.x, 0), y = apply(a.y, 0);
    auto rx = x.length(), ry = y.length();
    out["center"] = encode(apply(point(out["center"])));
    out["axisX"] = encode(x);
    out["axisY"] = encode(y);
    out["radius"] = rx;
    out["rx"] = rx;
    out["ry"] = ry;
    out["normal"] = encode(x.cross(y).normalized());
    if ((std::abs(rx - ry) > epsilon * std::max(rx, 1.) ||
         std::abs(x.dot(y)) > epsilon * rx * ry) &&
        (type == "CIRCLE" || type == "ARC"))
      out["type"] = "ELLIPSE";
  } else if (type == "POLYLINE" || type == "HATCH" || type == "DIMENSION" ||
             type == "TEXT") {
    auto normal = vector_property(
        e, "normal",
        type == "HATCH" ? face_normal(points(e.at("points"))) : vec3{0, 0, 1});
    auto &v = *inv;
    auto nn = vec3{v[0] * normal.x + v[1] * normal.y + v[2] * normal.z,
                   v[4] * normal.x + v[5] * normal.y + v[6] * normal.z,
                   v[8] * normal.x + v[9] * normal.y + v[10] * normal.z}
                  .normalized();
    out["normal"] = encode(nn);
    bool bulged = false;
    if (e.contains("bulges"))
      for (auto &b : e["bulges"])
        if (!b.is_null() && b.get<double>() != 0)
          bulged = true;
    if (type == "POLYLINE" && bulged) {
      auto b = basis(normal);
      auto x = apply(b.x, 0), y = apply(b.y, 0);
      if (std::abs(x.length() - y.length()) >
              epsilon * std::max(x.length(), 1.) ||
          std::abs(x.dot(y)) > epsilon * x.length() * y.length()) {
        out["points"] = json::array();
        for (auto p : path(e, .1))
          out["points"].push_back(encode(apply(p)));
        out.erase("bulges");
      }
    }
    if (type == "HATCH") {
      auto b = basis(normal), bb = basis(nn);
      auto a = number(e, "angle", std::numbers::pi / 4);
      auto dir = apply(b.x * std::cos(a) + b.y * std::sin(a), 0),
           x = apply(b.x, 0), y = apply(b.y, 0);
      out["angle"] = std::atan2(dir.dot(bb.y), dir.dot(bb.x));
      auto spacing = number(e, "spacing", 10);
      if (spacing == 0)
        spacing = 10;
      out["spacing"] =
          spacing * x.cross(y).length() / std::max(dir.length(), epsilon);
    }
    if (type == "DIMENSION") {
      auto delta = point(e["points"][1]) - point(e["points"][0]);
      auto old = normal.cross(delta).normalized(),
           side = nn.cross(point(out["points"][1]) - point(out["points"][0]))
                      .normalized();
      out["offset"] =
          number(e, "offset", delta.length() * .12) * apply(old, 0).dot(side);
    }
  }
  for (auto key : {"height", "textHeight"})
    if (out.contains(key) && !out[key].is_null())
      out[key] = out[key].get<double>() * sy;
  if (type == "TEXT") {
    auto a = axes_for_text(e);
    auto x = apply(a.x, 0), y = apply(a.y, 0);
    auto b = basis(point(out["normal"]));
    out["direction"] = encode(x.normalized());
    out["rotation"] = std::atan2(x.dot(b.y), x.dot(b.x));
    auto height = number(e, "height", 10);
    if (height == 0)
      height = 10;
    out["height"] = height * y.length();
  }
  if (det < 0) {
    if (out.contains("faces"))
      for (auto &f : out["faces"])
        std::reverse(f.begin(), f.end());
    if (out.contains("bulges"))
      for (auto &b : out["bulges"])
        b = -b.get<double>();
  }
  return out;
}

struct mesh_edge {
  size_t a{}, b{}, count{1};
  vec3 normal;
  bool crease{};
};
inline std::vector<mesh_edge> mesh_edges(const json &entity) {
  auto vertices = points(entity.at("vertices"));
  std::vector<mesh_edge> edges;
  std::map<std::pair<size_t, size_t>, size_t> lookup;
  for (auto &face : entity.at("faces")) {
    if (face.size() < 3)
      continue;
    std::vector<vec3> polygon;
    for (auto &index : face)
      polygon.push_back(vertices.at(index.get<size_t>()));
    auto normal = face_normal(polygon);
    for (size_t i = 0; i < face.size(); ++i) {
      auto a = face[i].get<size_t>(),
           b = face[(i + 1) % face.size()].get<size_t>();
      auto key = std::pair{std::min(a, b), std::max(a, b)};
      auto [it, inserted] = lookup.emplace(key, edges.size());
      if (inserted)
        edges.push_back({a, b, 1, normal, false});
      else {
        auto &edge = edges[it->second];
        ++edge.count;
        if (std::abs(edge.normal.dot(normal)) < .98)
          edge.crease = true;
      }
    }
  }
  return edges;
}
inline std::vector<segment> feature_edges(std::span<const vec3> vertices,
                                          std::span<const mesh_edge> edges,
                                          bool all = false) {
  std::vector<segment> result;
  if (all) {
    for (auto &e : edges)
      result.push_back({vertices[e.a], vertices[e.b]});
    return result;
  }
  auto base = vertices.empty() ? vec3{} : vertices.front();
  double scale = 1;
  for (auto p : vertices)
    scale = std::max(scale, (p - base).length());
  auto eps = scale * 1e-7;
  struct interval {
    double low, high;
    vec3 normal;
  };
  struct group {
    vec3 direction, origin;
    std::vector<interval> intervals;
  };
  std::vector<group> groups;
  std::map<std::array<double, 6>, size_t> lookup;
  for (auto &edge : edges) {
    if (edge.count > 1) {
      if (edge.crease)
        result.push_back({vertices[edge.a], vertices[edge.b]});
      continue;
    }
    auto a = vertices[edge.a], b = vertices[edge.b],
         direction = (b - a).normalized();
    if (direction.length() < .5)
      continue;
    double first = std::abs(direction.x) > 1e-5   ? direction.x
                   : std::abs(direction.y) > 1e-5 ? direction.y
                                                  : direction.z;
    if (first < 0)
      direction = direction * -1;
    auto moment = (a - base).cross(direction);
    auto round = [](double x) { return std::floor(x + .5); };
    std::array<double, 6> key{
        round(direction.x * 1e6), round(direction.y * 1e6),
        round(direction.z * 1e6), round(moment.x / eps),
        round(moment.y / eps),    round(moment.z / eps)};
    auto [it, inserted] = lookup.emplace(key, groups.size());
    if (inserted)
      groups.push_back({direction, a, {}});
    auto &g = groups[it->second];
    auto ta = (a - g.origin).dot(g.direction),
         tb = (b - g.origin).dot(g.direction);
    g.intervals.push_back({std::min(ta, tb), std::max(ta, tb), edge.normal});
  }
  for (auto &g : groups) {
    std::vector<double> values, times;
    for (auto &e : g.intervals) {
      values.push_back(e.low);
      values.push_back(e.high);
    }
    std::sort(values.begin(), values.end());
    for (size_t i = 0; i < values.size(); ++i)
      if (!i || std::abs(values[i] - values[i - 1]) > eps)
        times.push_back(values[i]);
    for (size_t i = 0; i + 1 < times.size(); ++i) {
      auto low = times[i], high = times[i + 1], mid = (low + high) / 2;
      size_t count = 0;
      vec3 normal;
      bool crease = false;
      for (auto &e : g.intervals)
        if (mid > e.low - eps && mid < e.high + eps) {
          if (!count)
            normal = e.normal;
          else if (std::abs(e.normal.dot(normal)) < .98)
            crease = true;
          ++count;
        }
      if (count == 1 || crease)
        result.push_back(
            {g.origin + g.direction * low, g.origin + g.direction * high});
    }
  }
  return result;
}

struct triangle {
  std::array<vec3, 3> points;
  vec3 normal;
};
struct snap_point {
  vec3 point;
  std::string type;
};
struct entity_geometry {
  std::vector<segment> segments, wire_segments;
  std::vector<triangle> triangles;
  std::vector<json> texts;
  std::vector<snap_point> snaps;
  std::vector<vec3> points;
};
inline entity_geometry geometry(const json &e, double tolerance = .5) {
  entity_geometry out;
  auto type = e.at("type").get<std::string>();
  auto triangulate_into = [&](const std::vector<vec3> &p) {
    auto normal = face_normal(p);
    for (auto t : triangulate(p))
      out.triangles.push_back({{p[t[0]], p[t[1]], p[t[2]]}, normal});
  };
  if (type == "MESH") {
    auto vertices = points(e.at("vertices"));
    auto edges = mesh_edges(e);
    for (auto &face : e.at("faces")) {
      std::vector<vec3> p;
      for (auto &i : face)
        p.push_back(vertices.at(i.get<size_t>()));
      if (p.size() >= 3)
        triangulate_into(p);
    }
    for (auto &edge : edges)
      out.wire_segments.push_back({vertices[edge.a], vertices[edge.b]});
    out.segments =
        feature_edges(vertices, edges, e.value("showAllEdges", false));
    for (auto p : vertices)
      out.snaps.push_back({p, "endpoint"});
  } else if (type == "DIMENSION") {
    auto d = dimension(e);
    out.segments = std::move(d.segments);
    out.texts.push_back(std::move(d.text));
    for (auto p : points(e.at("points")))
      out.snaps.push_back({p, "endpoint"});
  } else if (type == "TEXT") {
    out.texts.push_back(e);
    out.snaps.push_back({point(e.at("position")), "insertion"});
  } else if (type == "POINT") {
    auto p = point(e.at("position"));
    auto size = number(e, "size", 2);
    if (size == 0)
      size = 2;
    out.snaps.push_back({p, "node"});
    out.segments = {{p + vec3{-size, 0, 0}, p + vec3{size, 0, 0}},
                    {p + vec3{0, -size, 0}, p + vec3{0, size, 0}}};
  } else {
    auto p = path(e, tolerance);
    auto is_closed = closed(e);
    for (size_t i = 1; i < p.size(); ++i)
      out.segments.push_back({p[i - 1], p[i]});
    if (is_closed && p.size() > 2)
      out.segments.push_back({p.back(), p.front()});
    if (type == "HATCH") {
      if (e.value("pattern", std::string{}) == "solid")
        triangulate_into(p);
      else {
        auto hatch = hatch_segments(e);
        out.segments.insert(out.segments.end(), hatch.begin(), hatch.end());
      }
    }
    if (e.contains("center")) {
      out.snaps.push_back({point(e["center"]), "center"});
      for (int i = 0; i < 4; ++i) {
        auto t = i * std::numbers::pi / 2;
        if (type != "ARC" ||
            angle(t - number(e, "startAngle", 0)) <=
                sweep(number(e, "startAngle", 0), number(e, "endAngle", 0)))
          out.snaps.push_back({conic_point(e, t), "quadrant"});
      }
      if (type == "ARC" && !p.empty()) {
        out.snaps.push_back({p.front(), "endpoint"});
        out.snaps.push_back({p.back(), "endpoint"});
      }
    } else if (type == "SPLINE") {
      if (!p.empty()) {
        out.snaps.push_back({p.front(), "endpoint"});
        out.snaps.push_back({p.back(), "endpoint"});
      }
    } else {
      auto control = e.contains("points") ? points(e["points"]) : p;
      for (size_t i = 0; i < control.size(); ++i) {
        out.snaps.push_back({control[i], "endpoint"});
        if (i + 1 < control.size() || is_closed)
          out.snaps.push_back(
              {lerp(control[i], control[(i + 1) % control.size()], .5),
               "midpoint"});
      }
    }
  }
  for (auto s : out.segments)
    out.points.insert(out.points.end(), s.begin(), s.end());
  for (auto t : out.triangles)
    out.points.insert(out.points.end(), t.points.begin(), t.points.end());
  for (auto &t : out.texts) {
    auto axes = axes_for_text(t);
    auto h = number(t, "height", 10);
    if (h == 0)
      h = 10;
    auto text = t.value("text", std::string{});
    size_t lines = 1, length = 0, max_length = 0;
    // Count UTF-16 code units to preserve browser string-length semantics.
    for (unsigned char c : text) {
      if (c == '\n') {
        max_length = std::max(max_length, length);
        length = 0;
        ++lines;
      } else if ((c & 0xc0) != 0x80)
        length += (c >= 0xf0 ? 2 : 1);
    }
    max_length = std::max(max_length, length);
    auto w = max_length * h * .66;
    auto align = t.value("align", std::string{});
    auto left = align == "center" ? -w / 2 : align == "right" ? -w : 0.;
    for (auto x : {left, left + w})
      for (auto y : {-h * .25 - (lines - 1) * h * 1.35, h * .85})
        out.points.push_back(point(t.at("position")) + axes.x * x + axes.y * y);
  }
  return out;
}
struct mesh_extent {std::array<double,3> center{},size{};};
inline mesh_extent mesh_bounds(const json& entity) {
  mesh_extent result;const auto& vertices=entity.at("vertices");if(vertices.empty())return result;
  std::array<double,3> low{INFINITY,INFINITY,INFINITY},high{-INFINITY,-INFINITY,-INFINITY};
  for(const auto& vertex:vertices)for(size_t axis=0;axis<3;++axis) {
    const double value=axis<vertex.size()?vertex[axis].get<double>():0;
    low[axis]=std::min(low[axis],value);high[axis]=std::max(high[axis],value);
  }
  for(size_t axis=0;axis<3;++axis){result.center[axis]=(low[axis]+high[axis])*.5;result.size[axis]=high[axis]-low[axis];}
  return result;
}
inline bool change_mesh_center(drawing& model,size_t axis,double value) {
  if(axis>2 || !std::isfinite(value))return false;
  const auto ids=model.selected(true);if(ids.size()!=1)return false;
  const auto* entity=model.find(ids.front());if(!entity || entity->value("type",std::string{})!="MESH")return false;
  const double shift=value-mesh_bounds(*entity).center[axis];
  const vec3 delta{axis==0?shift:0,axis==1?shift:0,axis==2?shift:0};
  auto transformed=transform_entity(*entity,translation(delta));
  return model.transaction("Edit meshCenter."+std::to_string(axis),[&]{model.replace(ids.front(),std::move(transformed));});
}
} // namespace kestrel::geo
