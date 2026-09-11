#include <webscene/shared_css.hpp>
#include "webscene_css_stylesheet.h"
#include "webscene_css_media.h"
#include <source_location>
#include <iostream>
#include <fstream>
#include <iterator>
import webscene.test.shared_styles;
import webscene.test.prepared_styles;

using namespace webscene::native_web;
void require(bool value, std::source_location where=std::source_location::current()) {
  if(!value) throw std::runtime_error("shared stylesheet contract failed at line " + std::to_string(where.line()));
}
void exercise(const webscene_native::css::prepared_stylesheet& input) {
  const auto* sheet=&input;
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
  require(rejected && report->diagnostics.size()==sheet->diagnostics.size());
  auto container=d.element(d.body(),"div");d.attribute(container,"id","scroll-parent");
  auto scroller=d.element(container,"div");d.attribute(scroller,"id","scroller");
  auto tall=d.element(scroller,"div");d.attribute(tall,"class","tall");
  d.attribute(scroller,"style","scrollbar-width:auto");
  const auto has_rail=[&](uint32_t color,float width) {
    const auto& scene=d.render(100,200);
    for(const auto& command:scene.commands)
      if(command.node_id==scroller && command.rgba==color && command.width==width) return true;
    return false;
  };
  require(has_rail(0x0000ffff,4)); // Inherited colors; author !important beats inline normal.
  d.attribute(scroller,"class","hide");require(!has_rail(0x0000ffff,4));
  d.scroll_to(scroller,0,20);require(d.scroll_offset(scroller).second==20);
  d.attribute(scroller,"class","auto");require(has_rail(0x7f7f7f40,4));
  d.attribute(scroller,"style","scrollbar-width:auto!important;scrollbar-color:#00ff00 #000000");
  require(has_rail(0x000000ff,6));
  d.remove_attribute(scroller,"style");d.remove_attribute(scroller,"class");
  require(has_rail(0x0000ffff,4));
  auto mark=d.find("mark");
  const auto serialized=[&] {
    const auto& scene=d.render(100,200);
    return std::string(scene.bytes.begin(),scene.bytes.end());
  };
  d.attribute(mark,"class","wide");
  require(serialized().find("stroke-width=\"3px\"")!=std::string::npos);
  d.remove_attribute(mark,"class");
  require(serialized().find("stroke-width=\"3px\"")==std::string::npos);
  require(serialized().find("stroke-width=\"1\"")!=std::string::npos);
  d.attribute(mark,"class","inherited");
  require(serialized().find("stroke-width=\"1\"")==std::string::npos);
  d.attribute(mark,"class","bad");d.attribute(mark,"style","stroke-width:4px");
  require(serialized().find("stroke-width=\"4px\"")!=std::string::npos);
  // Replacing the sheet changes styling through the same document invalidation.
  auto replacement=webscene_native::css::prepare_stylesheet("#target {width:44px;height:12px}","asset://new.css",[](const auto&){return true;});
  d.set_stylesheet_resolver(make_shared_stylesheet_resolver({*replacement},report));
  d.render(100,200);require(d.bounds(target).width==44);
  d.dispose();require(d.disposed());
  document typed;typed.add_rule({});rejected=false;
  try {typed.set_stylesheet_resolver(make_shared_stylesheet_resolver({*sheet},report));}
  catch(const std::logic_error&) {rejected=true;}
  require(rejected);

}

void compare_selector(const webscene_native::css::compiled_css_selector& a,
                      const webscene_native::css::compiled_css_selector& b) {
  require(a.compounds==b.compounds && a.combinators==b.combinators && a.specificity==b.specificity);
  require(a.compiled_compounds.size()==b.compiled_compounds.size());
  for(size_t i=0;i<a.compiled_compounds.size();++i) {
    const auto& x=a.compiled_compounds[i];const auto& y=b.compiled_compounds[i];
    require(x.tag==y.tag && x.identities==y.identities && x.attributes==y.attributes &&
        x.valid==y.valid && x.pseudo_element==y.pseudo_element && x.pseudos.size()==y.pseudos.size());
    for(size_t j=0;j<x.pseudos.size();++j)
      require(x.pseudos[j].name==y.pseudos[j].name && x.pseudos[j].argument==y.pseudos[j].argument);
  }
}
int main() {
  auto generated=compiled_css::build();
  std::ifstream file(generated.source_address);
  require(bool(file));
  const std::string text((std::istreambuf_iterator<char>(file)),{});
  auto parsed=webscene_native::css::prepare_stylesheet(text,generated.source_address,[](const auto&){return true;});
  require(bool(parsed));
  require(parsed->rules.size()==generated.rules.size());
  require(parsed->keyframes.size()==generated.keyframes.size());
  require(generated.keyframes.at("fade").opacity_stops.size()==2);
  require(generated.keyframes.at("fade").opacity_stops[1].opacity==1);
  require(generated.keyframes.at("turn").rotation_stops.size()==2);
  require(generated.keyframes.at("turn").rotation_stops[0].degrees==-15);
  require(generated.diagnostics.size()==parsed->diagnostics.size());
  for(size_t i=0;i<generated.diagnostics.size();++i) {
    const auto& a=parsed->diagnostics[i];const auto& b=generated.diagnostics[i];
    require(a.feature==b.feature && a.classification==b.classification && a.detail==b.detail);
  }
  for(size_t i=0;i<parsed->rules.size();++i) {
    const auto& a=*parsed->rules[i];const auto& b=*generated.rules[i];
    require(a.selector==b.selector && a.specificity==b.specificity && a.media_queries==b.media_queries);
    compare_selector(a.compiled_selector,b.compiled_selector);
    compare_selector(a.compiled_pseudo_origin,b.compiled_pseudo_origin);
    require(a.declarations.size()==b.declarations.size());
    for(size_t j=0;j<a.declarations.size();++j)
      require(a.declarations[j].name==b.declarations[j].name &&
          a.declarations[j].value==b.declarations[j].value && a.declarations[j].important==b.declarations[j].important);
  }
  exercise(*parsed);exercise(generated);
  std::cout<<"Parsed and generated CSS on compiled HTML integration passed\n";
}
