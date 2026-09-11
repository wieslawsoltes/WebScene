#include "../../samples/NativeKestrel/third_party/nlohmann/json.hpp"
#include <array>
#include <stdexcept>
import kestrel.render_data;
int main() {
  {
    kestrel::drawing model;
    model.data["entities"]=kestrel::json::array();
    const auto id=model.add("LINE",{{"points",{{0,0,0},{100,0,0}}}});
    kestrel::camera camera;camera.resize(800,600);camera.set_view("top");
    const auto screen=camera.project({0,0,0});
    auto snap=kestrel::nearest_object_snap(model,camera,screen.x+2,screen.y+1);
    if(!snap || snap->entity_id!=id || snap->type!="endpoint" || snap->point.x!=0)
      throw std::runtime_error("Endpoint snap failed");
    if(kestrel::nearest_object_snap(model,camera,screen.x,screen.y,id))throw std::runtime_error("Excluded object snapped");
    (*model.find(id))["hidden"]=true;
    if(kestrel::nearest_object_snap(model,camera,screen.x,screen.y))throw std::runtime_error("Hidden object snapped");
    (*model.find(id))["hidden"]=false;
    for(auto& layer:model.data["layers"])layer["locked"]=true;
    if(!kestrel::nearest_object_snap(model,camera,screen.x,screen.y))throw std::runtime_error("Visible locked reference did not snap");
  }

  {
    const auto d=kestrel::preview_dashes(-1000000000,50,1000000000,50,100,100);
    if(d.empty() || d.size()>13)throw std::runtime_error("Distant preview not bounded by viewport");
    for(const auto& line:d)if(line[0]<-.001 || line[2]>100.001 || line[1]!=50 || line[3]!=50)
      throw std::runtime_error("Preview escaped viewport");
    const auto phase=kestrel::preview_dashes(-2,5,20,5,10,10);
    if(phase.size()!=2 || std::abs(phase[0][0])>1e-9 || std::abs(phase[0][2]-3)>1e-9 || std::abs(phase[1][0]-7)>1e-9 || std::abs(phase[1][2]-10)>1e-9)
      throw std::runtime_error("Clipping shifted dash phase");
    if(!kestrel::preview_dashes(-2,-2,-2,20,10,10).empty() || !kestrel::preview_dashes(1,1,1,1,10,10).empty())
      throw std::runtime_error("Invisible preview produced commands");
  }

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
  kestrel::camera camera;
  camera.target = {1000, 2000, 3000};
  camera.update();
  auto uniforms = kestrel::make_camera_uniforms(
      camera, camera.target, 800, 600, {kestrel::display_style::shaded, true});
  if (uniforms.viewport != std::array<float, 4>{800, 600, 1, 1} ||
      uniforms.eye[3] != 1)
    throw std::runtime_error("Camera uniform mismatch");
  auto expected =
      kestrel::multiply(camera.combined, kestrel::translation(camera.target));
  for (size_t i = 0; i < 16; ++i)
    if (uniforms.mvp[i] != static_cast<float>(expected[i]))
      throw std::runtime_error("Camera relative matrix mismatch");
  kestrel::camera grid_camera;
  grid_camera.resize(800, 600);
  auto grid = kestrel::build_grid(grid_camera, {});
  if (kestrel::grid_spacing(1) != 20 || grid.lines.size() != 156)
    throw std::runtime_error("Grid spacing or count mismatch");
  if (!kestrel::build_grid(grid_camera, {}, false, false).lines.empty())
    throw std::runtime_error("Disabled grid has lines");
}
