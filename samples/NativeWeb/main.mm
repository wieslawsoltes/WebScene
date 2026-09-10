#include "FocoNativeWeb_ui.hpp"
#include "app.hpp"
#include "native_web_view.hpp"
#import <AppKit/AppKit.h>
#import <dispatch/dispatch.h>
#include <foco/app_builder.hpp>
#include <fstream>
#include <iostream>

static bool smoke = false;
static int verification_exit = 0;
static std::string capture;
class native_app final : public foco::application {
  foco::ref<foco::window> window_;
  foco::ref<webscene::foco_host::view> view_;
  app_state state_;

public:
  foco::result<void> started(foco::application_lifetime &base) override {
    auto *lifetime = dynamic_cast<foco::windowed_application_lifetime *>(&base);
    if (!lifetime)
      return foco::error{foco::error_code::invalid_argument,
                         "desktop lifetime required"};
    window_ = foco::make_ref<foco::window>();
    window_->set_title("Foco · Native Web");
    window_->set_width(1000);
    window_->set_height(700);
    view_ = foco::make_ref<webscene::foco_host::view>();
    compiled_ui::build(view_->document);
    state_.attach(view_->document);
    auto resource = [NSBundle mainBundle].resourcePath;
    std::ifstream about(std::string([resource UTF8String]) + "/about.txt");
    if (!about)
      return foco::error{foco::error_code::unavailable,
                         "Missing bundled about.txt resource"};
    std::string introduction;
    std::getline(about, introduction);
    view_->document.set_text(view_->document.find("intro"), introduction);
    window_->add_child(view_);
    auto result = window_->show(*lifetime);
    if (!result)
      return result;
    if (smoke || !capture.empty())
      dispatch_after(
          dispatch_time(DISPATCH_TIME_NOW, 1500 * NSEC_PER_MSEC),
          dispatch_get_main_queue(), ^{
            auto finish = [lifetime](int code) {
              verification_exit = code;
              lifetime->shutdown(code);
            };
            auto &d = view_->document;
            NSWindow *native = nil;
            for (NSWindow *w in NSApp.windows)
              if ([w.title isEqualToString:@"Foco · Native Web"])
                native = w;
            auto click = [&](const char *id) {
              auto b = d.bounds(d.find(id));
              NSPoint point = NSMakePoint(
                  b.x + 10, native.contentView.bounds.size.height - b.y - 10);
              for (auto type :
                   {NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp}) {
                NSEvent *e = [NSEvent mouseEventWithType:type
                                                location:point
                                           modifierFlags:0
                                               timestamp:0
                                            windowNumber:native.windowNumber
                                                 context:nil
                                             eventNumber:1
                                              clickCount:1
                                                pressure:1];
                if (type == NSEventTypeLeftMouseDown)
                  [native.contentView mouseDown:e];
                else
                  [native.contentView mouseUp:e];
              }
            };
            if (!native) {
              finish(4);
              return;
            }
            click("increment");
            const auto key = [&](NSString *characters, unsigned short code) {
              NSEvent *e = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                            location:NSZeroPoint
                                       modifierFlags:0
                                           timestamp:0
                                        windowNumber:native.windowNumber
                                             context:nil
                                          characters:characters
                         charactersIgnoringModifiers:characters
                                           isARepeat:NO
                                             keyCode:code];
              [native.contentView keyDown:e];
            };
            key(@"\r", 36);
            if (state_.count != 2) {
              std::cerr << "native Enter activation failed\n";
              finish(5);
              return;
            }
            key(@"\t", 48);
            if (d.focused() != d.find("reset")) {
              std::cerr << "native Tab focus failed\n";
              finish(6);
              return;
            }
            key(@" ", 49);
            if (state_.count != 0) {
              std::cerr << "native Space activation failed\n";
              finish(7);
              return;
            }
            click("increment");
            click("add");
            view_->refresh();
            if (!capture.empty()) {
              auto png = foco::capture_platform_compositor_png(*window_, 1.f);
              if (png) {
                std::ofstream f(capture, std::ios::binary);
                f.write(reinterpret_cast<const char *>(png.value().data()),
                        png.value().size());
              } else {
                std::cerr << png.failure().message << "\n";
                finish(2);
                return;
              }
            }
            std::cout << "Foco Native Web Cocoa/Metal passed; count="
                      << state_.count << " items=" << state_.items << "\n";
            finish(state_.count == 1 && state_.items == 1 ? 0 : 3);
          });
    return {};
  }
};
int main(int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "--smoke")
      smoke = true;
    else if (arg == "--capture" && i + 1 < argc)
      capture = argv[++i];
  }
  if (smoke || !capture.empty())
    verification_exit = 10;
  const auto result = foco::AppBuilder::Configure<native_app>()
                          .WithSkia()
                          .WithCocoa()
                          .TryStartWithClassicDesktopLifetime(argc, argv);
  if (!result) {
    std::cerr << result.failure().message << "\n";
    return 1;
  }
  return result.value() ? result.value() : verification_exit;
}
