#include "../../samples/NativeKestrel/third_party/nlohmann/json.hpp"
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
  kestrel::drawing drawing;
  auto id = drawing.add("MESH", kestrel::geo::box({0, 0, 0}, 2, 3, 4));
  auto wire = kestrel::build_scene(drawing, {});
  if (wire.buffers.lines.size() != 12 || !wire.buffers.triangles.empty())
    throw std::runtime_error("Wireframe scene mismatch");
  auto shaded =
      kestrel::build_scene(drawing, {}, {kestrel::display_style::shaded});
  if (!shaded.buffers.lines.empty() || shaded.buffers.triangles.size() != 36)
    throw std::runtime_error("Shaded scene mismatch");
  drawing.selection.insert(id);
  auto xray = kestrel::build_scene(drawing, {}, {kestrel::display_style::xray});
  if (xray.buffers.lines.size() != 12 || xray.buffers.lines[0].width != 2.1f ||
      xray.buffers.triangles[0].color[3] != .25f)
    throw std::runtime_error("Selected xray scene mismatch");
  for (auto &layer : drawing.data["layers"])
    layer["visible"] = false;
  if (kestrel::build_scene(drawing, {}).entity_count != 0)
    throw std::runtime_error("Hidden layer rendered");
}
