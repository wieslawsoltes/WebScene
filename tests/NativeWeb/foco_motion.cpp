#include "../../src/WebScene.NativeWeb/foco/native_web_view.hpp"
#include <stdexcept>
#include <iostream>
int main() {
  using namespace webscene::native_web;
  auto root=foco::make_ref<foco::window>();
  auto view=foco::make_ref<webscene::foco_host::view>();
  root->add_child(view);
  foco::scene_publisher publisher;
  foco::detail::window_composition_attachment attachment(*root,publisher);
  int wakes=0;
  publisher.set_host_frame_request_callback([&] {++wakes;});
  auto node=view->document.element(view->document.body(),"div");
  rule normal;normal.inline_target=node;
  normal.declarations.push_back({false,+[](style& s) {s.set_width({40,length_unit::pixels});}});
  view->document.add_rule(std::move(normal));
  rule reduced;reduced.inline_target=node;reduced.reduced_motion=true;
  reduced.declarations.push_back({false,+[](style& s) {s.set_width({70,length_unit::pixels});}});
  view->document.add_rule(std::move(reduced));
  root->measure({300,300});root->arrange({0,0,300,300});view->refresh();
  auto require=[](bool ok) {if(!ok) throw std::runtime_error("Foco reduced-motion propagation");};
  {
    struct frame_owner final : foco::composition_resource_attachment {};
    auto gpu_root=foco::make_ref<foco::window>();
    auto gpu_view=foco::make_ref<webscene::foco_host::view>();
    gpu_root->add_child(gpu_view);
    foco::scene_publisher gpu_publisher;
    foco::detail::window_composition_attachment gpu_attachment(*gpu_root,gpu_publisher);
    foco::retained_scene retained;
    auto canvas=gpu_view->document.element(gpu_view->document.body(),"canvas");
    gpu_root->measure({200,200});gpu_root->arrange({0,0,200,200});
    for(uint64_t generation=1;generation<=4;++generation) {
      auto owner=std::make_shared<frame_owner>();
      std::weak_ptr<const foco::composition_resource_attachment> weak=owner;
      gpu_view->set_gpu_image(canvas,100,100,generation,owner);
      gpu_publisher.commit(*gpu_root,foco::theme_variant::dark);
      if(generation>1) require(gpu_publisher.metrics().publisher_visit_count==1);
      const auto* scene=foco_scene_acquire_next(gpu_publisher.mailbox());
      require(scene && retained.apply(*scene));
      require(foco_scene_acknowledge(scene));
      foco_scene_release(scene);
      const auto id=(uint64_t{1}<<63u)|gpu_view->id();
      require(retained.resources().contains(id));
      require(retained.resources().at(id).attachment==owner);
      owner.reset();require(!weak.expired());
    }
  }
  {
    auto input=foco::make_ref<webscene::foco_host::view>();
    auto button=input->document.element(input->document.body(),"button");
    rule box;box.inline_target=button;
    box.declarations.push_back({false,+[](style& s){s.set_width({40,length_unit::pixels});s.set_height({30,length_unit::pixels});}});
    input->document.add_rule(std::move(box));input->measure({100,100});input->arrange({0,0,100,100});
    unsigned received=0;
    auto capture=[&](auto& event) {
      require(event.modifiers.shift && event.modifiers.control && event.modifiers.alt && event.modifiers.meta);
      ++received;
    };
    auto down=input->document.on(button,"pointerdown",capture);
    auto click=input->document.on(button,"click",capture);
    auto wheel=input->document.on(button,"wheel",capture);
    foco::pointer_event event;event.position={1,1};event.buttons=1;
    event.modifiers=foco::key_modifiers::shift|foco::key_modifiers::control|foco::key_modifiers::alt|foco::key_modifiers::platform;
    event.kind=foco::pointer_event_kind::pressed;input->pointer_event_received(event);
    event.kind=foco::pointer_event_kind::released;event.buttons=0;input->pointer_event_received(event);
    event.kind=foco::pointer_event_kind::wheel;event.wheel_delta=1;input->pointer_event_received(event);
    require(received==3);
    foco::key_event key;key.modifiers=event.modifiers;key.value=foco::key::enter;
    input->key_event_received(key);
    key.value=foco::key::space;input->key_event_received(key);
    require(received==5 && key.handled);
    auto field=input->document.element(input->document.body(),"input");
    input->document.focus(field);
    foco::text_input_event text;text.text="Café🙂";input->text_input_received(text);
    require(text.handled && input->document.value(field)=="Café🙂");
    foco::key_event editing;editing.value=foco::key::backspace;input->key_event_received(editing);
    require(editing.handled && input->document.value(field)=="Café");
    input->document.set_selection(field,0,0);editing.value=foco::key::delete_key;input->key_event_received(editing);
    require(input->document.value(field)=="afé");
    // A focus callback may close the document during in-view Tab navigation.
    auto close=input->document.on(button,"focus",[&](auto&){input->document.dispose();});
    input->document.focus(0);
    require(!input->try_move_focus_within(false));
    require(input->document.disposed());
  }
  require(view->document.bounds(node).width==40);
  const auto before=wakes;
  publisher.set_reduced_motion(true);
  require(wakes==before+1 && view->requires_host_frames());
  require(view->advance_host_frame(1));
  require(view->document.bounds(node).width==70 && !view->requires_host_frames());
  publisher.set_reduced_motion(true);
  require(wakes==before+1 && !view->advance_host_frame(2));
  publisher.set_reduced_motion(false);view->advance_host_frame(3);
  require(view->document.bounds(node).width==40);
  rule animation;animation.inline_target=node;
  animation.declarations.push_back({false,+[](style& s) {s.set_opacity_transition(100);s.set_opacity(1);}});
  view->document.add_rule(std::move(animation));view->refresh();
  rule fade;fade.inline_target=node;
  fade.declarations.push_back({false,+[](style& s) {s.set_opacity(0);}});
  view->document.add_rule(std::move(fade));view->refresh();
  require(view->requires_host_frames());
  view->advance_host_frame(53);
  require(view->requires_host_frames());
  view->advance_host_frame(103);
  require(!view->requires_host_frames());
  int cancelled=0;
  auto cancellation=view->document.on(node,"transitioncancel",[&](auto&){++cancelled;});
  rule restore;restore.inline_target=node;
  restore.declarations.push_back({false,+[](style& s) {s.set_opacity(1);}});
  view->document.add_rule(std::move(restore));view->refresh();view->advance_host_frame(153);
  rule stop;stop.inline_target=node;
  stop.declarations.push_back({false,+[](style& s) {s.clear_transitions();}});
  view->document.add_rule(std::move(stop));view->refresh();
  require(view->requires_host_frames()); // Pending cancellation must be delivered.
  view->advance_host_frame(154);
  require(cancelled==1 && !view->requires_host_frames());
  rule final_fade;final_fade.inline_target=node;
  final_fade.declarations.push_back({false,+[](style& s) {s.set_opacity_transition(100);s.set_opacity(0);}});
  auto shutdown=view->document.on(node,"transitionend",[&](auto&){view->document.dispose();});
  view->document.add_rule(std::move(final_fade));view->refresh();
  require(view->requires_host_frames());
  view->advance_host_frame(254);
  require(view->document.disposed() && !view->requires_host_frames());
  require(!view->composition_command_stream());
  require(!view->advance_host_frame(255));
  publisher.set_host_frame_request_callback({});
  std::cout<<"Foco native reduced-motion propagation passed\n";
}
