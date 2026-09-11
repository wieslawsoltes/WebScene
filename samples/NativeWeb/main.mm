#include "FocoNativeWeb_ui.hpp"
#include "app.hpp"
#include "native_web_view.hpp"
#if defined(NATIVE_WEB_GPU_SAMPLE)
#include "native_gpu_image.hpp"
#include "native_webgpu_surface.h"
#include <thread>
#endif
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
#if defined(NATIVE_WEB_GPU_SAMPLE)
  std::unique_ptr<webscene::graphics::native_webgpu_surface> gpu_;
  void initialize_gpu() {
    auto node=view_->document.find("chart");
    gpu_=std::make_unique<webscene::graphics::native_webgpu_surface>(node,300,120);
    auto texture=gpu_->current_texture();
    if(!texture)throw std::runtime_error("Native GPU Canvas acquisition failed");
    wgpu::RenderPassColorAttachment color{};
    color.view=texture.CreateView();color.loadOp=wgpu::LoadOp::Clear;color.storeOp=wgpu::StoreOp::Store;
    color.clearValue={0.1,0.55,0.85,1};
    wgpu::RenderPassDescriptor desc{};desc.colorAttachmentCount=1;desc.colorAttachments=&color;
    auto encoder=gpu_->device().CreateCommandEncoder();auto pass=encoder.BeginRenderPass(&desc);pass.End();
    auto commands=encoder.Finish();gpu_->device().GetQueue().Submit(1,&commands);
    auto snapshot=gpu_->present();
    if(!snapshot)throw std::runtime_error("Native GPU snapshot missing");
    std::shared_ptr<const webscene_gpu_image_lease_v3> image;
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    do {
      gpu_->process_events();image=snapshot->resolve();
      if(!image)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while(!image && std::chrono::steady_clock::now()<deadline);
    if(!image||gpu_->failed())throw std::runtime_error("Native GPU first frame failed");
    view_->set_gpu_image(node,300,120,1,webscene::foco_host::make_gpu_image(node,1,std::move(image)));
  }
#endif

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
#if defined(NATIVE_WEB_GPU_SAMPLE)
    initialize_gpu();
#endif
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
