#include <webscene/native_web.hpp>
import webscene.test.transitions;
#include <stdexcept>
int main() {
  webscene::native_web::document d;
  compiled_ui::build(d);d.advance_animations(0);d.render(100,100);
  int starts=0,ends=0,cancels=0;
  auto cancel=d.on(d.find("fade"),"transitioncancel",[&](auto&){++cancels;});
  auto start=d.on(d.find("fade"),"transitionstart",[&](auto&){++starts;});
  auto end=d.on(d.find("fade"),"transitionend",[&](auto& e){
    if(e.property_name!="opacity") throw std::runtime_error("transition property");++ends;
  });
  d.attribute(d.find("fade"),"class","changed");d.render(100,100);
  d.advance_animations(10);
  if(!d.has_active_animations() || starts) return 1;
  d.advance_animations(20);
  if(starts!=1 || ends) return 2;
  d.advance_animations(70);
  if(!d.has_active_animations()) return 3;
  d.advance_animations(120);
  if(d.has_active_animations() || ends!=1) return 4;
  d.attribute(d.find("fade"),"class","");d.render(100,100);
  d.advance_animations(150);
  d.attribute(d.find("fade"),"class","stop");d.render(100,100);
  d.advance_animations(151);
  if(d.has_active_animations() || cancels!=1 || ends!=1) return 5;
}
