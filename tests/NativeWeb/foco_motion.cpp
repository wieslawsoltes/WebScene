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
  publisher.set_host_frame_request_callback({});
  std::cout<<"Foco native reduced-motion propagation passed\n";
}
