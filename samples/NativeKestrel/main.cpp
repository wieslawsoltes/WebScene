#include "native_gpu_image.hpp"
#include "native_web_view.hpp"
#include "native_webgpu_surface.h"
#include "third_party/nlohmann/json.hpp"
#include <functional>
#include <foco/app_builder.hpp>
#include <fstream>
#include <iostream>
#include <memory>
#include <webscene/native_web.hpp>
import kestrel.controller;
import kestrel.viewport;

static std::string capture_path;
static bool exercise_commands = false;
// Use the same Foco host-frame contract as FocoUI's NativeKestrel sample.
// Cocoa's display driver owns cadence and compositor scheduling.
class kestrel_window final : public foco::window {
public:
  std::function<void()> frame;
  bool requires_host_frames() const noexcept override { return bool(frame); }
  bool advance_host_frame(double) override {
    if (frame) frame();
    return false;
  }
};
class kestrel_app final : public foco::application {
  foco::ref<kestrel_window> window;
  foco::ref<webscene::foco_host::view> view;
  std::unique_ptr<kestrel::controller> controller;
  std::unique_ptr<kestrel::viewport> viewport;
  uint32_t width{}, height{};
  uint64_t serial{};
  unsigned ticks{};
  foco::windowed_application_lifetime *lifetime{};
  void tick() {
    if (!capture_path.empty() && ++ticks == 120) {
      if (!serial || (exercise_commands && serial < 2)) {
        lifetime->shutdown(3);
        return;
      }
      auto png = foco::capture_platform_compositor_png(*window, 1.f);
      if (!png) {
        std::cerr << png.failure().message;
        lifetime->shutdown(2);
        return;
      }
      std::ofstream file(capture_path, std::ios::binary);
      file.write(reinterpret_cast<const char *>(png.value().data()),
                 png.value().size());
      std::cout << "Kestrel native frame captured; serial=" << serial << '\n';
      file.close();
      lifetime->shutdown(file ? 0 : 4);
      return;
    }
    auto node = view->document.find("viewport");
    auto bounds = view->document.bounds(node);
    if (bounds.width < 1 || bounds.height < 1)
      return;
    if (exercise_commands && ticks == 40) {
      auto &document = view->document;
      auto count = controller->model.data["entities"].size();
      auto click = [&](const char *id) {
        auto area = document.bounds(document.find(id));
        document.pointer("pointerdown", area.x + 5, area.y + 5);
        document.pointer("pointerup", area.x + 5, area.y + 5);
      };
      click("line");
      for (auto point : {std::pair{100.f, 100.f}, std::pair{300.f, 200.f}}) {
        document.pointer("pointerdown", bounds.x + point.first,
                         bounds.y + point.second);
        document.pointer("pointerup", bounds.x + point.first,
                         bounds.y + point.second);
      }
      if (controller->model.data["entities"].size() != count + 1)
        throw std::runtime_error("Hosted line creation failed");
      click("cancel");
      click("undo");
      if (controller->model.data["entities"].size() != count)
        throw std::runtime_error("Hosted undo failed");
      click("redo");
      if (controller->model.data["entities"].size() != count + 1)
        throw std::runtime_error("Hosted redo failed");
      view->refresh();
    }
    auto w = static_cast<uint32_t>(bounds.width),
         h = static_cast<uint32_t>(bounds.height);
    if (!viewport)
      viewport = std::make_unique<kestrel::viewport>(node, w, h);
    if (w != width || h != height) {
      viewport->resize(w, h);
      controller->camera.resize(w, h);
      width = w;
      height = h;
      controller->render_dirty = true;
    }
    if (auto image = viewport->poll()) {
      ++serial;
      view->set_gpu_image(
          node, width, height, serial,
          webscene::foco_host::make_gpu_image(node, serial, std::move(image)));
    }
    if (controller->render_dirty) {
      viewport->camera = controller->camera;
      viewport->options = controller->options;
      if (viewport->submit(controller->model))
        controller->render_dirty = false;
    }
  }

public:
  ~kestrel_app() { if (window) window->frame = {}; }
  foco::result<void> started(foco::application_lifetime &base) override {
    lifetime = dynamic_cast<foco::windowed_application_lifetime *>(&base);
    if (!lifetime)
      return foco::error{foco::error_code::invalid_argument,
                         "Desktop lifetime required"};
    window = foco::make_ref<kestrel_window>();
    window->set_title("Kestrel CAD · Native Web");
    window->set_width(1100);
    window->set_height(760);
    view = foco::make_ref<webscene::foco_host::view>();
    controller = std::make_unique<kestrel::controller>(view->document);
    controller->model.add("MESH",
                          kestrel::geo::box({-50, -40, 0}, 100, 80, 60));
    controller->options.style = kestrel::display_style::shaded_edges;
    controller->refresh();
    window->add_child(view);
    window->frame = [this] {
      try {
        tick();
      } catch (const std::exception &e) {
        std::cerr << "Kestrel viewport: " << e.what() << '\n';
        lifetime->shutdown(5);
      }
    };
    return window->show(*lifetime);
  }
};
int main(int argc, char **argv) {
  for (int i = 1; i < argc; ++i)
    if (std::string_view(argv[i]) == "--exercise-commands")
      exercise_commands = true;
  for (int i = 1; i + 1 < argc; ++i)
    if (std::string_view(argv[i]) == "--capture")
      capture_path = argv[++i];
  auto result = foco::AppBuilder::Configure<kestrel_app>()
                    .WithSkia()
                    .WithCocoa()
                    .TryStartWithClassicDesktopLifetime(argc, argv);
  if (!result) {
    std::cerr << result.failure().message << '\n';
    return 1;
  }
  return result.value();
}
