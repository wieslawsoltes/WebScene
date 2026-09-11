#include <webscene/native_web.hpp>
#include <iostream>
#include <stdexcept>
import sample.ui;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main() {
  webscene::native_web::document document;
  compiled_ui::build(document);
  auto name=document.find("name"),notes=document.find("notes");
  unsigned edits=0;
  auto listener=document.on(name,"input",[&](auto&){++edits;});
  document.focus(name);document.set_selection(name,0,3);
  require(document.text_input("Grace"),"Text input rejected");
  require(document.value(name)=="Grace" && edits==1,"Compiled input did not use live value/event state");
  document.key("Backspace");
  require(document.value(name)=="Grac" && edits==2,"Keyboard editing failed");
  document.render(700,400);
  require(document.bounds(notes).x>document.bounds(name).x,"Wide layout failed");
  document.render(300,400);
  require(document.bounds(notes).y>document.bounds(name).y,"Responsive layout failed");
  std::cout<<"Installed native document: editing, events and responsive layout passed\n";
}
