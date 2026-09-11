#include "native_gpu_image.hpp"
#include <stdexcept>
#include <iostream>
using namespace webscene::graphics;
void require(bool value) {if(!value) throw std::runtime_error("native Foco GPU dependency contract");}
struct dependencies final : webscene_gpu_producer_dependencies {
  size_t count() const noexcept override {return 1;}
  bool metal_event(size_t index,void*& event,uint64_t& value) const override {
    if(index)return false;
    event=const_cast<dependencies*>(this);value=42;return true;
  }
};
int main() {
  owned_image_pool pool(std::make_shared<image_provider_lifetime>());
  auto writer=pool.acquire();require(bool(writer));
  writer->set_metadata({1,1,1,1,1,1,16,16});writer->begin();
  auto pixels=writer->publish();writer->complete();writer.reset();require(bool(pixels));
  auto producer=std::make_shared<dependencies>();std::weak_ptr<dependencies> weak=producer;
  auto lease=std::make_shared<webscene_gpu_image_lease_v3>(std::move(*pixels),producer,true);
  auto frame=std::dynamic_pointer_cast<const foco::webscene_gpu_frame>(webscene::foco_host::make_gpu_image(1,1,lease));
  require(bool(frame));void* consumer=nullptr;
  require(frame->begin_consumer(frame->images[0].lease,&consumer)==0 && consumer);
  const auto count=frame->dependency_count;const auto event=frame->get_metal_event;const auto complete=frame->complete_consumer;
  frame.reset();lease.reset();producer.reset();require(!weak.expired());
  uint32_t size=0;require(count(consumer,&size) && size==1);
  foco::webscene_gpu_metal_event_view view;
  require(event(consumer,0,&view) && view.event==weak.lock().get() && view.value==42);
  require(!event(consumer,1,&view) && !count(nullptr,&size));
  complete(consumer);require(weak.expired());
  auto next=pool.acquire();require(bool(next));next->set_metadata({1,2,1,2,1,2,16,16});next->begin();
  auto plain=next->publish();next->complete();next.reset();
  auto invalid=std::make_shared<webscene_gpu_image_lease_v3>(std::move(*plain),nullptr,true);
  bool rejected=false;try {webscene::foco_host::make_gpu_image(1,2,invalid);}catch(const std::invalid_argument&){rejected=true;}
  require(rejected);
  std::cout<<"Native Foco fence ownership and validation passed\n";
}
