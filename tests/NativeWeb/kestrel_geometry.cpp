#include "../../samples/NativeKestrel/third_party/nlohmann/json.hpp"
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
import kestrel.geometry;
#include <fstream>
#include <iostream>
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  std::ifstream input(argv[1]);
  kestrel::json fixtures;
  input >> fixtures;
  size_t compared = 0;
  for (auto &fixture : fixtures["cases"]) {
    auto actual = kestrel::geo::path(fixture["entity"], fixture["tolerance"]);
    auto expected = kestrel::geo::points(fixture["path"]);
    if (actual.size() != expected.size())
      throw std::runtime_error("Tessellation count mismatch: " +
                               fixture["name"].get<std::string>());
    for (size_t i = 0; i < actual.size(); ++i) {
      if ((actual[i] - expected[i]).length() > 1e-8)
        throw std::runtime_error("Tessellation vertex mismatch");
      ++compared;
    }
    if (kestrel::geo::closed(fixture["entity"]) !=
        fixture["closed"].get<bool>())
      throw std::runtime_error("Closed path mismatch");
  }
  for (auto &fixture : fixtures["hatches"]) {
    auto actual = kestrel::geo::hatch_segments(fixture["entity"]);
    auto &expected = fixture["segments"];
    if (actual.size() != expected.size())
      throw std::runtime_error("Hatch segment count mismatch");
    for (size_t i = 0; i < actual.size(); ++i)
      for (size_t j = 0; j < 2; ++j)
        if ((actual[i][j] - kestrel::geo::point(expected[i][j])).length() >
            1e-8)
          throw std::runtime_error("Hatch endpoint mismatch");
  }
  for (auto &fixture : fixtures["dimensions"]) {
    auto actual = kestrel::geo::dimension(fixture["entity"]);
    auto &expected = fixture["geometry"];
    if (actual.segments.size() != expected["segments"].size())
      throw std::runtime_error("Dimension segment count mismatch");
    for (size_t i = 0; i < actual.segments.size(); ++i)
      for (size_t j = 0; j < 2; ++j)
        if ((actual.segments[i][j] -
             kestrel::geo::point(expected["segments"][i][j]))
                .length() > 1e-8)
          throw std::runtime_error("Dimension endpoint mismatch");
    for (auto key : {"position", "direction", "normal"})
      if ((kestrel::geo::point(actual.text[key]) -
           kestrel::geo::point(expected["text"][key]))
              .length() > 1e-8)
        throw std::runtime_error("Dimension text vector mismatch");
    for (auto key : {"height", "rotation"})
      if (std::abs(actual.text[key].get<double>() -
                   expected["text"][key].get<double>()) > 1e-8)
        throw std::runtime_error("Dimension text scalar mismatch");
    if (actual.text["text"] != expected["text"]["text"] ||
        actual.text["align"] != expected["text"]["align"])
      throw std::runtime_error("Dimension label mismatch");
  }
  auto compare_entity = [](const kestrel::json &actual,
                           const kestrel::json &expected) {
    if (actual["type"] != expected["type"])
      throw std::runtime_error("Edit entity type mismatch");
    for (auto key : {"center", "axisX", "axisY"})
      if (expected.contains(key) && (kestrel::geo::point(actual[key]) -
                                     kestrel::geo::point(expected[key]))
                                            .length() > 1e-8)
        throw std::runtime_error("Edit vector mismatch");
    for (auto key : {"radius", "startAngle", "endAngle"})
      if (expected.contains(key) &&
          std::abs(actual[key].get<double>() - expected[key].get<double>()) >
              1e-8)
        throw std::runtime_error("Edit scalar mismatch");
    if (expected.contains("points")) {
      if (actual["points"].size() != expected["points"].size())
        throw std::runtime_error("Offset point count mismatch");
      for (size_t i = 0; i < expected["points"].size(); ++i)
        if ((kestrel::geo::point(actual["points"][i]) -
             kestrel::geo::point(expected["points"][i]))
                .length() > 1e-8)
          throw std::runtime_error("Offset point mismatch");
    }
  };
  for (auto &f : fixtures["offsets"])
    compare_entity(kestrel::geo::offset(f["entity"], f["distance"]),
                   f["result"]);
  for (auto &f : fixtures["arcs"])
    compare_entity(
        kestrel::geo::arc_through(kestrel::geo::point(f["points"][0]),
                                  kestrel::geo::point(f["points"][1]),
                                  kestrel::geo::point(f["points"][2])),
        f["result"]);
  std::function<void(const kestrel::json &, const kestrel::json &)>
      compare_json;
  compare_json = [&](const auto &a, const auto &b) {
    if (a.is_number() && b.is_number()) {
      if (std::abs(a.template get<double>() - b.template get<double>()) > 1e-8)
        throw std::runtime_error("Transform numeric mismatch");
      return;
    }
    if (a.is_array() && b.is_array()) {
      if (a.size() != b.size())
        throw std::runtime_error("Transform array mismatch");
      for (size_t i = 0; i < a.size(); ++i)
        compare_json(a[i], b[i]);
      return;
    }
    if (a.is_object() && b.is_object()) {
      if (a.size() != b.size())
        throw std::runtime_error("Transform property mismatch");
      for (auto it = b.begin(); it != b.end(); ++it)
        compare_json(a.at(it.key()), it.value());
      return;
    }
    if (a != b)
      throw std::runtime_error("Transform value mismatch");
  };
  for (auto &f : fixtures["transforms"])
    compare_json(kestrel::geo::transform_entity(
                     f["entity"], f["matrix"].get<kestrel::matrix>()),
                 f["result"]);
  for (auto &f : fixtures["geometry"]) {
    auto g = kestrel::geo::geometry(f["entity"]);
    kestrel::json actual;
    for (auto key :
         {"segments", "wireSegments", "triangles", "texts", "snaps", "points"})
      actual[key] = kestrel::json::array();
    auto encode = kestrel::geo::encode;
    for (auto v : g.segments)
      actual["segments"].push_back({encode(v[0]), encode(v[1])});
    for (auto v : g.wire_segments)
      actual["wireSegments"].push_back({encode(v[0]), encode(v[1])});
    for (auto v : g.triangles)
      actual["triangles"].push_back(
          {{"points",
            {encode(v.points[0]), encode(v.points[1]), encode(v.points[2])}},
           {"normal", encode(v.normal)}});
    for (auto &v : g.texts)
      actual["texts"].push_back(v);
    for (auto &v : g.snaps)
      actual["snaps"].push_back({{"point", encode(v.point)}, {"type", v.type}});
    for (auto v : g.points)
      actual["points"].push_back(encode(v));
    compare_json(actual, f["result"]);
  }
  std::cout << "Kestrel curves: " << compared << " upstream vertices matched\n";
}
