#include <webscene/native_web.hpp>
#include <stdexcept>
#include <iostream>
import linux.contract.ui;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main() {
  using namespace webscene::native_web;
  document doc;compiled_ui::build(doc);int count=0;
  auto subscription=doc.on(doc.find("increment"),"click",[&](auto&){doc.set_text(doc.find("count"),std::to_string(++count));});
  doc.render(640,480);auto bounds=doc.bounds(doc.find("increment"));
  require(bounds.width==160 && bounds.height==40,"shared CSS button bounds");
  doc.pointer("pointerdown",bounds.x+10,bounds.y+10,1);
  doc.pointer("pointerup",bounds.x+10,bounds.y+10,0);
  require(count==1 && doc.text_content(doc.find("count"))=="1","native click delivery");
  doc.focus(doc.find("name"));doc.text_input("Kestrel \xc3\xa9");
  require(doc.value(doc.find("name"))=="Kestrel \xc3\xa9","native committed UTF-8 text");
  doc.key("Backspace");require(doc.value(doc.find("name"))=="Kestrel ","native Unicode backspace");
  doc.attribute(doc.find("increment"),"style","width:240px");doc.render(800,600);
  require(doc.bounds(doc.find("increment")).width==240,"dynamic shared CSS");
  doc.set_dark_color_scheme(true);doc.render(320,240);
  subscription={};doc.dispose();require(doc.disposed(),"native document shutdown");
  std::cout<<"{\"compiledHtml\":true,\"sharedCSS\":true,\"nativeEvents\":true,\"javascript\":false}\n";
}
