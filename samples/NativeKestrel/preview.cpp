#include "native_web_view.hpp"
#include <foco/app_builder.hpp>
#include <iostream>
import kestrel.original.preview;
class preview_app final : public foco::application {
  foco::ref<foco::window> window;
  foco::ref<webscene::foco_host::view> view;
public:
  foco::result<void> started(foco::application_lifetime &base) override {
    auto *lifetime = dynamic_cast<foco::windowed_application_lifetime *>(&base);
    if (!lifetime) return foco::error{foco::error_code::invalid_argument,"Desktop required"};
    window = foco::make_ref<foco::window>();
    window->set_title("Kestrel original HTML/CSS — diagnostic preview (unsupported features omitted)");
    window->set_width(1280); window->set_height(800);
    view = foco::make_ref<webscene::foco_host::view>();
    compiled_ui::build(view->document);
    window->add_child(view);
    return window->show(*lifetime);
  }
};
int main(int argc, char **argv) {
  auto result = foco::AppBuilder::Configure<preview_app>().WithSkia().WithCocoa()
      .TryStartWithClassicDesktopLifetime(argc, argv);
  if (!result) { std::cerr << result.failure().message; return 1; }
  return result.value();
}
