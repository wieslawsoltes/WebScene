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
  kestrel::json fixture;
  input >> fixture;
  using namespace kestrel;
  using namespace kestrel::geo;
  auto meshes =
      std::array{box({1, 2, 3}, 4, 5, 6), cylinder({1, 2, 3}, 4, 5, 12),
                 cylinder({1, 2, 3}, 4, 5, 12, 0), sphere({1, 2, 3}, 4, 12, 6),
                 torus({1, 2, 3}, 8, 2, 12, 6)};
  for (size_t i = 0; i < meshes.size(); ++i) {
    auto &a = meshes[i];
    auto &b = fixture["meshes"][i];
    if (a["faces"] != b["faces"] || a["primitive"] != b["primitive"] ||
        a["vertices"].size() != b["vertices"].size())
      throw std::runtime_error("Mesh topology mismatch");
    for (size_t j = 0; j < a["vertices"].size(); ++j)
      if ((point(a["vertices"][j]) - point(b["vertices"][j])).length() > 1e-10)
        throw std::runtime_error("Mesh vertex mismatch");
  }
  for (auto &sample : fixture["extrusions"]) {
    auto a = extrude(points(sample["points"]), sample["height"]);
    auto &b = sample["mesh"];
    if (a["faces"] != b["faces"] ||
        a["vertices"].size() != b["vertices"].size())
      throw std::runtime_error("Extrusion topology mismatch");
    for (size_t j = 0; j < a["vertices"].size(); ++j)
      if ((point(a["vertices"][j]) - point(b["vertices"][j])).length() > 1e-10)
        throw std::runtime_error("Extrusion vertex mismatch");
    if (std::abs(volume(a) - sample["volume"].get<double>()) > 1e-8)
      throw std::runtime_error("Extrusion volume mismatch");
  }
  for (auto height : {0., std::numeric_limits<double>::infinity()}) {
    bool rejected = false;
    try {
      extrude({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, height);
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    if (!rejected)
      throw std::runtime_error("Invalid extrusion accepted");
  }
  for (auto &sample : fixture["revolutions"]) {
    auto profile = points(sample["points"]);
    auto a = revolve(profile, point(sample["origin"]), point(sample["axis"]),
                     sample["degrees"], sample["segments"]);
    auto &b = sample["mesh"];
    if (a["faces"] != b["faces"] ||
        a["vertices"].size() != b["vertices"].size())
      throw std::runtime_error("Revolution topology mismatch");
    for (size_t j = 0; j < a["vertices"].size(); ++j)
      if ((point(a["vertices"][j]) - point(b["vertices"][j])).length() > 1e-10)
        throw std::runtime_error("Revolution vertex mismatch");
    if (std::abs(volume(a) - sample["volume"].get<double>()) > 1e-8)
      throw std::runtime_error("Revolution volume mismatch");
  }
  for (auto &sample : fixture["invalidRevolutions"]) {
    bool rejected = false;
    try {
      auto profile = points(sample["points"]);
      revolve(profile, point(sample["origin"]), point(sample["axis"]),
              sample["degrees"], sample["segments"]);
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    if (!rejected)
      throw std::runtime_error("Invalid revolution accepted");
  }
  std::cout << "Kestrel: five upstream mesh primitives matched\n";
}
