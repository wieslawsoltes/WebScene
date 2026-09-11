#include <webscene/native_web.hpp>
#include <iostream>
using namespace webscene::native_web;
void require(bool condition) {if(!condition) throw std::runtime_error("native text input contract failed");}
int main() {
  document d;
  auto field=d.element(d.body(),"input");d.attribute(field,"value","Find");
  d.focus(field);require(d.focused()==field && d.value(field)=="Find");
  d.attribute(field,"value","Updated");require(d.value(field)=="Updated");
  d.attribute(field,"value","Find");
  unsigned inputs=0,before=0;
  auto pre=d.on(field,"beforeinput",[&](auto& e){++before;require(e.data=="é🙂" && e.input_type=="insertText");});
  auto post=d.on(field,"input",[&](auto& e){++inputs;require(e.data=="é🙂");});
  require(d.text_input("é🙂") && d.value(field)=="Findé🙂" && inputs==1 && before==1);
  d.attribute(field,"value","new default");require(d.value(field)=="Findé🙂");
  pre.dispose();post.dispose();
  auto cancel=d.on(field,"beforeinput",[](auto& e){e.prevent_default();});
  require(d.text_input("cancelled") && d.value(field)=="Findé🙂");cancel.dispose();
  d.attribute(field,"readonly","");d.text_input("ignored");require(d.value(field)=="Findé🙂");
  d.remove_attribute(field,"readonly");d.set_value(field,"New");d.text_input("\r\nValue");require(d.value(field)=="NewValue");
  auto area=d.element(d.body(),"textarea");d.text(area,"a\r\nb\rc");
  require(d.value(area)=="a\nb\nc");d.set_text(area,"replacement");require(d.value(area)=="replacement");
  d.set_text(area,"a\nb\nc");d.focus(area);d.text_input("\nnext");require(d.value(area)=="a\nb\nc\nnext");
  auto removal=d.on(area,"beforeinput",[&](auto&){d.remove(area);});
  require(d.text_input("removed") && d.focused()==0);
  auto hidden=d.element(d.body(),"input");d.attribute(hidden,"type","hidden");d.attribute(hidden,"tabindex","0");
  d.focus(hidden);require(d.focused()==0);
  d.focus(field);
  auto close=d.on(field,"beforeinput",[&](auto&){d.dispose();});
  require(d.text_input("close") && d.disposed());
  std::cout<<"Native committed text, cancellation and lifecycle passed\n";
}
