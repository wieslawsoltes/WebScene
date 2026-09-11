#include <webscene/shared_css.hpp>
#include <iostream>
#include <source_location>
import webscene.test.shared_document;
using namespace webscene::native_web;
void require(bool value,std::source_location location=std::source_location::current()) {
  if(!value) throw std::runtime_error("shared document failure at " + std::to_string(location.line()));
}
int main() {
  document d;
  auto report=std::make_shared<shared_css_report>();
  shared_document_ui::build(d,report);
  d.render(300,200);
  require(d.bounds(d.find("target")).width==88);
  auto row=shared_document_ui::instantiate(d,d.body(),"row");
  auto item=row.named("item");
  d.render(300,200);
  require(d.bounds(item).width==52 && d.bounds(item).height==19);
  d.render(100,200);require(d.bounds(item).width==32);
  d.attribute(d.root(),"style","--control-width:64px");d.render(100,200);
  require(d.bounds(d.find("target")).width==64);
  d.remove_attribute(item,"style");d.render(100,200);require(d.bounds(item).height==17);
  d.attribute(d.find("target"),"class","important");d.render(100,200);
  require(d.bounds(d.find("target")).width==77);
  d.attribute(d.find("target"),"style","width:20px!important;width:10px");d.render(100,200);
  require(d.bounds(d.find("target")).width==20);
  d.remove(item);d.render(100,200);
  // Both keyframe limitations are retained from preparation, not dropped.
  require(report->diagnostics.size()==2);
  d.dispose();
  std::cout<<"Linked, embedded and inline CSS in generated HTML module passed\n";
}
