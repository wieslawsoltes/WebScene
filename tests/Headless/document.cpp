#include <webscene/native_web.hpp>
#include <stdexcept>
#include <string>
import headless.consumer.ui;
static void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int main(){
  webscene::native_web::document document;auto view=compiled_ui::build(document);
  check(!document.find("row"),"Compiled template must remain inert");
  auto first=compiled_ui::instantiate(document,view.named("rows"),"row");
  auto second=compiled_ui::instantiate(document,view.named("rows"),"row");
  check(first.named("label")!=second.named("label"),"Template instance references alias");
  document.set_text(first.named("label"),"<b>literal & text</b>");
  document.attribute(second.named("row"),"class","row selected");
  int count=0;auto click=document.on(view.named("increment"),"click",[&](auto&){document.set_text(view.named("count"),std::to_string(++count));});
  document.render(640,480);auto box=document.bounds(view.named("increment"));
  document.pointer("pointerdown",box.x+5,box.y+5,1);document.pointer("pointerup",box.x+5,box.y+5,0);
  check(count==1&&document.text_content(view.named("count"))=="1","Native pointer did not activate compiled control");
  check(document.text_content(first.named("label"))=="<b>literal & text</b>","User text was interpreted as HTML");
  check(document.bounds(first.named("row")).height==24&&document.bounds(second.named("row")).height==32,"Shared CSS template layout mismatch");
  document.focus(view.named("input"));check(document.text_input("Aé🙂"),"Native UTF-8 input rejected");
  check(document.value(view.named("input"))=="Aé🙂","Native UTF-8 input changed bytes");
  document.set_selection(view.named("input"),1,3);document.text_input("x");check(document.value(view.named("input"))=="Ax🙂","Selection replacement failed");
  document.set_dark_color_scheme(true);document.render(800,600);check(document.bounds(view.named("main")).width==360,"Shared CSS theme did not apply");
  document.set_dark_color_scheme(false);document.render(400,300);check(document.bounds(view.named("main")).width==300,"Shared CSS theme did not revert");
  click.dispose();document.dispose();check(document.disposed(),"Native document did not dispose");
}
