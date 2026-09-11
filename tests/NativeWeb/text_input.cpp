#include <webscene/native_web.hpp>
#include <iostream>
using namespace webscene::native_web;
void require(bool condition) {if(!condition) throw std::runtime_error("native text input contract failed");}
int main() {
  {
    document colors;
    auto swatch=colors.element(colors.body(),"input");
    colors.attribute(swatch,"type","color");colors.attribute(swatch,"value","#12Ab34");
    rule box;box.inline_target=swatch;
    box.declarations.push_back({false,+[](style& s){
      s.set_width({40,length_unit::pixels});s.set_height({24,length_unit::pixels});
    }});colors.add_rule(std::move(box));
    auto verify=[&](uint32_t rgba) {
      bool found=false;
      for(const auto& command:colors.render(100,100).commands) {
        if(command.node_id!=swatch) continue;
        require(command.kind!=3U && command.kind!=14U);
        if(command.kind==1U && command.rgba==rgba && command.width==34 && command.height==18) found=true;
      }
      require(found);
    };
    verify(0x12AB34FFU);colors.set_value(swatch,"#ff0088");verify(0xFF0088FFU);
    colors.set_value(swatch,"invalid");verify(0x000000FFU);
  }
  {
    document hints;
    auto search=hints.element(hints.body(),"input");
    hints.attribute(search,"type","search");hints.attribute(search,"placeholder","Filter objects…");
    auto contains=[&](const std::string& text) {
      const auto& scene=hints.render(300,100);
      return std::string(scene.bytes.begin(),scene.bytes.end()).find(text)!=std::string::npos;
    };
    require(contains("Filter objects…") && hints.value(search).empty());
    hints.focus(search);require(contains("Filter objects…"));
    hints.text_input("Box");require(!contains("Filter objects…") && contains("Box"));
    hints.set_value(search,"");require(contains("Filter objects…"));
    hints.attribute(search,"placeholder","Filter layers…");require(contains("Filter layers…"));
    hints.remove_attribute(search,"placeholder");require(!contains("Filter layers…"));
    hints.attribute(search,"placeholder","Not a range label");hints.attribute(search,"type","range");
    require(!contains("Not a range label"));
  }
  {
    document canvas_doc;auto canvas=canvas_doc.element(canvas_doc.body(),"canvas");
    canvas_doc.render(200,100);const auto layouts=canvas_doc.layout_passes();
    canvas_doc.fill_text(canvas,"Courtyard · 12000",12,30,"14px sans-serif",0xFFFFFFFF,"center","middle");
    const auto& rendered=canvas_doc.render(200,100);
    require(canvas_doc.layout_passes()==layouts);
    require(std::string(rendered.bytes.begin(),rendered.bytes.end()).find("Courtyard · 12000")!=std::string::npos);
    bool found=false;for(const auto& command:rendered.canvas)
      if(command.kind==25 && command.data.values[0]==12 && command.data.values[1]==30)found=true;
    require(found);canvas_doc.clear_canvas(canvas);
    require(canvas_doc.render(200,100).canvas.empty());
  }
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
