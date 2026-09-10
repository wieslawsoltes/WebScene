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
import kestrel.camera;
#include <iomanip>
#include <iostream>
int main() {
  kestrel::camera c;
  std::cout << std::setprecision(17);
  auto dump = [&] {
    for (auto x : c.combined)
      std::cout << x << ' ';
    for (auto p : {kestrel::vec3{0, 0, 0}, kestrel::vec3{120, -40, 25}}) {
      auto v = c.project(p);
      std::cout << v.x << ' ' << v.y << ' ' << v.z << ' ';
    }
    std::cout << '\n';
  };
  dump();
  c.resize(1440, 900);
  c.set_view("iso");
  dump();
  c.pan(40, -20);
  c.zoom_at(1.5, 300, 200);
  dump();
  c.perspective = true;
  c.update();
  dump();
  c.pan(-20, 30);
  c.zoom_at(.7, 800, 500);
  dump();
  std::array<kestrel::vec3, 3> points{
      {{-100, -200, -30}, {500, 250, 80}, {0, 10, 50}}};
  c.fit(points);
  dump();
  c.set_view("front");
  dump();
}
