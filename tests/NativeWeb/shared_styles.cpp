#include <webscene/shared_css.hpp>
#include "webscene_css_stylesheet.h"
#include "webscene_css_media.h"
#include <source_location>
#include <iostream>
import webscene.test.shared_styles;

using namespace webscene::native_web;
void require(bool value, std::source_location where=std::source_location::current()) {
  if(!value) throw std::runtime_error("shared stylesheet contract failed at line " + std::to_string(where.line()));
}
int main() {
  auto sheet=webscene_native::css::prepare_stylesheet(R"CSS(
    body { margin:0; padding:0; }
    #target { display:block; width:80px; height:20px; padding:0; border:0; }
    #target.changed {width:120px;}
    #target:focus {height:30px;}
    #target:hover {width:90px;}
    .row {height:17px; width:40px;}
    canvas {width:40px; height:40px;}
    @media (max-width:150px) {#target {width:60px;}}
    @media (prefers-reduced-motion:reduce) {#target:focus {height:10px;}}
  )CSS","asset://shared.css",[](const auto&){return true;});
  require(bool(sheet));
  document d;
  compiled_ui::build(d);
  auto report=std::make_shared<shared_css_report>();
  d.set_stylesheet_resolver(make_shared_stylesheet_resolver({*sheet},report));
  const auto target=d.find("target");
  d.render(300,200);
  require(d.bounds(target).width==80 && d.bounds(target).height==20);
  d.pointer("pointermove",1,1,0);d.render(300,200);require(d.bounds(target).width==90);
  d.pointer("pointermove",299,199,0);d.render(300,200);require(d.bounds(target).width==80);
  d.attribute(target,"class","changed");d.set_text(target,"Native update");
  d.render(300,200);require(d.bounds(target).width==120);
  d.focus(target);d.render(300,200);require(d.bounds(target).height==30);
  d.render(100,200);require(d.bounds(target).width==120); // More specific class wins.
  d.remove_attribute(target,"class");d.render(100,200);require(d.bounds(target).width==60);
  d.set_reduced_motion(true);d.render(100,200);require(d.bounds(target).height==10);
  auto row=compiled_ui::instantiate(d,d.body(),"row");
  d.render(100,200);require(row.roots.size()==1 && d.bounds(row.roots[0]).height==17);
  d.remove(row.roots[0]);d.render(100,200);
  auto passes=d.layout_passes();
  d.fill_rect(d.find("drawing"),0,0,10,10,0xff0000ff);d.render(100,200);
  require(d.layout_passes()==passes);
  bool rejected=false;
  try {d.add_rule({});} catch(const std::logic_error&) {rejected=true;}
  require(rejected && report->diagnostics.empty());
  // Replacing the sheet changes styling through the same document invalidation.
  auto replacement=webscene_native::css::prepare_stylesheet("#target {width:44px;height:12px}","asset://new.css",[](const auto&){return true;});
  d.set_stylesheet_resolver(make_shared_stylesheet_resolver({*replacement},report));
  d.render(100,200);require(d.bounds(target).width==44);
  d.dispose();require(d.disposed());
  document typed;typed.add_rule({});rejected=false;
  try {typed.set_stylesheet_resolver(make_shared_stylesheet_resolver({*sheet},report));}
  catch(const std::logic_error&) {rejected=true;}
  require(rejected);
  std::cout<<"Compiled HTML and shared native CSS integration passed\n";
}
