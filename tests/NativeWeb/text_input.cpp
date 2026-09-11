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
  d.set_value(field,"Aé🙂Z");
  unsigned deletes=0;
  auto deletion=d.on(field,"input",[&](auto& e) {
    require(e.data.empty() && (e.input_type=="deleteContentBackward" || e.input_type=="deleteContentForward"));++deletes;
  });
  d.key("Backspace");require(d.value(field)=="Aé🙂");
  d.key("Backspace");require(d.value(field)=="Aé");
  bool invalid=false;try {d.set_selection(field,2,2);} catch(const std::invalid_argument&) {invalid=true;}
  require(invalid);
  d.set_selection(field,1,1);d.key("Delete");require(d.value(field)=="A" && deletes==3);
  d.key("Delete");require(deletes==3);
  d.set_value(field,"abc");d.set_selection(field,0,2);d.key("Delete");require(d.value(field)=="c");
  auto prevent_delete=d.on(field,"beforeinput",[](auto& e){e.prevent_default();});
  d.set_selection(field,1,1);d.key("Backspace");require(d.value(field)=="c");prevent_delete.dispose();
  d.attribute(field,"readonly","");d.key("Backspace");require(d.value(field)=="c");d.remove_attribute(field,"readonly");
  deletion.dispose();
  d.set_value(field,"AéB");d.set_selection(field,1,3);d.text_input("X");require(d.value(field)=="AXB");
  require(d.selection(field)==std::pair<size_t,size_t>{2,2});
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
