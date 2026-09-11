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
  {
    kestrel::camera orbit;
    orbit.set_view("iso");
    const auto yaw=orbit.yaw, pitch=orbit.pitch;
    const auto target=orbit.target;
    const auto revision=orbit.revision;
    orbit.orbit(20,-10);
    if(std::abs(orbit.yaw-(yaw-.14))>1e-12 ||
       std::abs(orbit.pitch-(pitch-.07))>1e-12 ||
       (orbit.target-target).length()>1e-12 || orbit.revision!=revision+1)
      throw std::runtime_error("Native orbit differs from browser calculation");
    orbit.orbit(0,10000);
    if(std::abs(orbit.pitch-(std::numbers::pi/2-.002))>1e-12)
      throw std::runtime_error("Orbit upper pitch limit");
    orbit.orbit(0,-10000);
    if(std::abs(orbit.pitch-(-std::numbers::pi/2+.002))>1e-12)
      throw std::runtime_error("Orbit lower pitch limit");
  }
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
