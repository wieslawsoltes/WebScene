#include <array>
#include <stdexcept>
import kestrel.render_data;
int main() {
  kestrel::render_data data;
  data.origin = {1e12, 1e12, 1e12};
  data.add_line({data.origin + kestrel::vec3{.25, .5, .75},
                 data.origin + kestrel::vec3{1, 2, 3}},
                {1, .5, 0, 1}, 2.1f, 2);
  const auto &line = data.lines.at(0);
  if (line.start != std::array<float, 3>{.25f, .5f, .75f} ||
      line.end != std::array<float, 3>{1, 2, 3} || line.width != 2.1f ||
      line.dash != 2)
    throw std::runtime_error(
        "Line upload loses origin-relative precision or attributes");
  data.add_triangle({{data.origin, data.origin + kestrel::vec3{1, 0, 0},
                      data.origin + kestrel::vec3{0, 1, 0}},
                     {0, 0, 1}},
                    {.2f, .3f, .4f, .25f});
  if (data.triangles.size() != 3 ||
      data.triangles[1].position != std::array<float, 3>{1, 0, 0} ||
      data.triangles[2].normal != std::array<float, 3>{0, 0, 1} ||
      data.triangles[2].color[3] != .25f)
    throw std::runtime_error("Triangle upload layout mismatch");
}
