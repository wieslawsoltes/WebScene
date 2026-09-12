#include <native_webgpu_surface.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace webscene::graphics;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
auto paint(native_webgpu_surface& surface,double red,double green,double blue) {
  auto texture=surface.current_texture();require(bool(texture),"surface admission unexpectedly failed");
  require(surface.current_texture().Get()==texture.Get(),"current texture changed inside a frame");
  wgpu::RenderPassColorAttachment color{};color.view=texture.CreateView();
  color.loadOp=wgpu::LoadOp::Clear;color.storeOp=wgpu::StoreOp::Store;color.clearValue={red,green,blue,1};
  wgpu::RenderPassDescriptor pass{};pass.colorAttachmentCount=1;pass.colorAttachments=&color;
  auto encoder=surface.device().CreateCommandEncoder();auto render=encoder.BeginRenderPass(&pass);render.End();
  auto command=encoder.Finish();surface.device().GetQueue().Submit(1,&command);
  auto snapshot=surface.present();require(bool(snapshot),"snapshot admission unexpectedly failed");return snapshot;
}
auto resolve(native_webgpu_surface& surface,std::shared_ptr<webscene_gpu_image_snapshot> snapshot) {
  const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(20);
  while(snapshot->state()==webscene_gpu_image_snapshot::status::pending && std::chrono::steady_clock::now()<end) {
    surface.process_events();std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  require(snapshot->state()==webscene_gpu_image_snapshot::status::ready,"GPU submission failed or timed out");
  auto image=snapshot->resolve();require(bool(image),"completed snapshot did not resolve");return image;
}
void pixel(const offscreen_capture& image,int red,int green,int blue) {
  require(image.pixels.size()==size_t(image.metadata.width)*image.metadata.height*4,"non-tight row readback");
  for(size_t i=0;i<image.pixels.size();i+=4)
    require(image.pixels[i]==red && image.pixels[i+1]==green && image.pixels[i+2]==blue && image.pixels[i+3]==255,"actual GPU pixel mismatch");
}
int main() {
  std::shared_ptr<const webscene_gpu_image_lease_v3> surviving;
  uint64_t captures=0,allocations=0;
  bool hardware=false;
  {
    native_webgpu_surface surface(7,17,4,64*1024);
    wgpu::AdapterInfo info{};require(surface.adapter().GetInfo(&info)==wgpu::Status::Success,"adapter identity unavailable");
    hardware=info.adapterType==wgpu::AdapterType::DiscreteGPU || info.adapterType==wgpu::AdapterType::IntegratedGPU;
    require(info.backendType==wgpu::BackendType::Vulkan,"Linux test did not run Vulkan");
    // GPU completion must retire an abandoned snapshot while application
    // frames are paused. No ProcessEvents or render timer drives this wait.
    {
      auto abandoned=paint(surface,1,0,0);abandoned.reset();
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
      while(surface.busy_images() && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      require(surface.busy_images()==0,"hidden GPU completion did not retire dropped snapshot");
    }
    auto red=resolve(surface,paint(surface,1,0,0));
    auto green=resolve(surface,paint(surface,0,1,0));
    auto blue=resolve(surface,paint(surface,0,0,1));
    require(!surface.current_texture(),"bounded three-slot pool admitted fourth retained image");
    require(surface.capture_count()==0,"normal rendering performed CPU readback");
    pixel(capture_offscreen_image(*red),255,0,0);pixel(capture_offscreen_image(*green),0,255,0);
    pixel(capture_offscreen_image(*blue),0,0,255);
    const auto generation=red->value.describe().allocation_generation;
    green.reset();blue.reset();
    surface.resize(65,7);
    auto resized=resolve(surface,paint(surface,0,1,0));
    require(resized->value.describe().allocation_generation>generation,"resize did not advance generation");
    require(resized->value.describe().width==65 && resized->value.describe().height==7,"resized metadata mismatch");
    pixel(capture_offscreen_image(*resized),0,255,0);
    pixel(capture_offscreen_image(*red),255,0,0);
    require(surface.resident_bytes()<=64*1024,"resident allocation budget exceeded");
    bool wrong_thread=false;
    std::thread foreign([&]{try{surface.resize(2,2);}catch(const std::logic_error&){wrong_thread=true;}});foreign.join();
    require(wrong_thread,"surface accepted mutation from a foreign thread");
    {
      native_webgpu_surface other(8,2,2);
      auto consumer=red->value.begin_consumer();require(bool(consumer),"consumer unavailable");
      bool rejected=false;
      try{(void)dawn_canvas_images::resolve(*consumer,other.device());}catch(const std::invalid_argument&){rejected=true;}
      consumer->complete();require(rejected,"cross-device image accepted");
    }
    bool budget_rejected=false;
    try{(void)capture_offscreen_image(*red,1);}catch(const std::invalid_argument&){budget_rejected=true;}
    require(budget_rejected,"capture ignored byte budget");
    captures=surface.capture_count();allocations=surface.created_images();surviving=std::move(red);
  }
  pixel(capture_offscreen_image(*surviving),255,0,0);
  surviving.reset();
  for(int i=0;i<3;++i) {
    native_webgpu_surface surface(10+i,13,5);
    auto image=resolve(surface,paint(surface,0,1,0));pixel(capture_offscreen_image(*image),0,255,0);
  }
  std::cout<<"{\"backend\":\"Vulkan\",\"hardware\":"<<(hardware?"true":"false")
    <<",\"readbacksBeforeCapture\":0,\"explicitCaptures\":"<<captures
    <<",\"allocations\":"<<allocations<<",\"hiddenCompletion\":true,\"pixelValidation\":true,\"retainedResize\":true,\"repeatedShutdown\":true}\n";
}
