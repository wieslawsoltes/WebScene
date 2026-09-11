#include <webscene/native_web.hpp>
#include <iostream>
using namespace webscene::native_web;
void require(bool condition) {if(!condition) throw std::runtime_error("native text input contract failed");}
int main() {
  {
    document d;auto label=d.element(d.body(),"label");d.attribute(label,"for","name");d.set_text(label,"Name");
    auto input=d.element(d.body(),"input");d.attribute(input,"id","name");
    const auto click=[&](node_id node) {d.render(300,200);auto b=d.bounds(node);d.pointer("pointerdown",b.x+1,b.y+1);d.pointer("pointerup",b.x+1,b.y+1,0);};
    click(label);require(d.focused()==input);
    d.focus(0);auto prevent=d.on(label,"click",[](auto& event){event.prevent_default();});click(label);require(d.focused()==0);prevent={};
    d.attribute(input,"disabled","");click(label);require(d.focused()==0);d.remove_attribute(input,"disabled");
    auto wrapper=d.element(d.body(),"label");auto text=d.element(wrapper,"span");d.set_text(text,"Toggle");
    auto box=d.element(wrapper,"input");d.attribute(box,"type","checkbox");
    click(text);require(d.checked(box) && d.focused()==box);
    click(box);require(!d.checked(box));
  }
  {
    document d;auto outside=d.element(d.body(),"button");
    auto dialog=d.element(d.body(),"dialog");auto first=d.element(dialog,"input");auto last=d.element(dialog,"button");
    rule box;box.inline_target=dialog;box.declarations.push_back({false,+[](style& s){s.set_width({200,length_unit::pixels});s.set_height({100,length_unit::pixels});}});d.add_rule(std::move(box));
    d.focus(outside);require(!d.set_modal(outside,true));
    require(d.set_modal(dialog,true));require(d.focused()==0 && d.attribute(dialog,"open").has_value());
    d.render(800,600);require(d.bounds(dialog).x==300 && d.bounds(dialog).y==250);
    d.render(600,400);require(d.bounds(dialog).x==200 && d.bounds(dialog).y==150);
    d.focus(first);require(d.focused()==first);d.focus(outside);require(d.focused()==first);
    d.key("Tab");require(d.focused()==last);d.key("Tab");require(d.focused()==first);
    auto nested=d.element(d.body(),"dialog");auto nested_input=d.element(nested,"input");
    require(d.set_modal(nested,true));d.focus(nested_input);d.focus(first);require(d.focused()==nested_input);
    require(d.set_modal(nested,false));d.focus(first);require(d.focused()==first);
    require(d.set_modal(dialog,false));require(d.focused()==0 && !d.attribute(dialog,"open").has_value());
    d.focus(outside);require(d.focused()==outside);
    require(d.set_modal(dialog,true));d.remove(dialog);d.focus(outside);require(d.focused()==outside);
  }
  {
    document d;auto outside=d.element(d.body(),"button");auto dialog=d.element(d.body(),"dialog");
    d.focus(outside);auto blur=d.on(outside,"blur",[&](auto&){d.dispose();});
    require(!d.set_modal(dialog,true) && d.disposed());
  }
  {
    document d;auto first=d.element(d.body(),"button");
    auto background=d.element(d.body(),"div");d.attribute(background,"inert","");
    auto input=d.element(background,"input");d.attribute(input,"tabindex","1");
    auto last=d.element(d.body(),"button");
    d.focus(first);d.focus(input);require(d.focused()==first);
    d.key("Tab");require(d.focused()==last);
    d.key("Tab",true);require(d.focused()==first);
    d.remove_attribute(background,"inert");d.focus(input);require(d.focused()==input);
    d.attribute(input,"inert","");d.focus(first);d.focus(input);require(d.focused()==first);
    d.key("Tab");require(d.focused()==last);
  }
  {
    document d;auto box=d.element(d.body(),"input");d.attribute(box,"type","checkbox");d.attribute(box,"value","must not paint");
    rule size;size.inline_target=box;size.declarations.push_back({false,+[](style& s){s.set_width({16,length_unit::pixels});s.set_height({16,length_unit::pixels});}});d.add_rule(std::move(size));
    const auto marks=[&] {
      unsigned count=0;for(const auto& command:d.render(100,100).commands)if(command.node_id==box) {
        require(command.kind!=3U && command.kind!=14U);
        if(command.kind==2U)++count;
      }return count;
    };
    require(marks()==0);d.attribute(box,"checked","");require(marks()==2);
    d.set_checked(box,false);require(marks()==0);d.set_checked(box,true);require(marks()==2);
  }
  {
    document d;auto box=d.element(d.body(),"input");d.attribute(box,"type","checkbox");
    unsigned changes=0;auto change=d.on(box,"change",[&](auto&){++changes;});
    d.focus(box);require(d.focused()==box && !d.checked(box));d.key(" ");require(d.checked(box) && changes==1);
    auto cancel=d.on(box,"click",[&](auto& event){require(!d.checked(box));event.prevent_default();});
    d.key(" ");require(d.checked(box) && changes==1);cancel={};
    d.set_checked(box,false);require(changes==1);d.attribute(box,"disabled","");d.key(" ");require(!d.checked(box));
    d.remove_attribute(box,"disabled");auto dispose=d.on(box,"input",[&](auto&){d.dispose();});
    d.key(" ");require(d.disposed() && changes==1);
  }
  {
    document d;auto input=d.element(d.body(),"input");auto other=d.element(d.body(),"button");
    unsigned changes=0;auto handler=d.on(input,"change",[&](auto&){++changes;});
    d.set_value(input,"old");d.focus(input);d.set_selection(input,0,3);d.text_input("new");
    require(changes==0);d.key("Enter");require(changes==1 && d.value(input)=="new");
    d.focus(other);require(changes==1);
    d.focus(input);d.key("Backspace");d.focus(other);require(changes==2);
    d.focus(input);d.text_input("x");d.key("Backspace");d.focus(other);require(changes==2);
    d.focus(input);d.set_value(input,"script");d.focus(other);require(changes==2);
    d.focus(input);d.text_input("!");
    auto dispose=d.on(input,"change",[&](auto&){d.dispose();});
    d.focus(other);require(d.disposed() && changes==3);
  }
  {
    document d;auto area=d.element(d.body(),"textarea");auto other=d.element(d.body(),"button");
    unsigned changes=0;auto handler=d.on(area,"change",[&](auto&){++changes;d.remove(area);});
    d.focus(area);d.text_input("multi\nline");d.key("Enter");require(changes==0);
    d.focus(other);require(changes==1 && d.focused()==other);
  }
  {
    document d;auto input=d.element(d.body(),"input");d.set_value(input,"abc");d.focus(input);
    unsigned calls=0;
    auto handler=d.on(d.root(),"keydown",[&](auto& event) {
      require(event.target==input && event.current_target==d.root());
      require(event.key=="Backspace" && event.modifiers.shift);
      ++calls;event.prevent_default();
    });
    d.key("Backspace",true);require(calls==1 && d.value(input)=="abc");
    handler={};d.key("Backspace");require(d.value(input)=="ab");
    auto dispose=d.on(input,"keydown",[&](auto&){d.dispose();});
    d.key("Delete");require(d.disposed());
  }
  {
    document d;auto select=d.element(d.body(),"select");
    auto first=d.element(select,"option");d.attribute(first,"value","top");d.set_text(first,"Top");
    auto group=d.element(select,"optgroup");auto second=d.element(group,"option");
    d.set_text(second,"  SE\n isometric  ");
    require(d.value(select)=="top");
    unsigned events=0;auto handler=d.on(select,"change",[&](auto&){++events;});
    d.set_value(select,"SE isometric");require(d.value(select)=="SE isometric" && events==0);
    d.set_value(select,"missing");require(d.value(select).empty());
    d.set_value(select,"top");require(d.value(select)=="top");
    auto disabled=d.element(select,"option");d.attribute(disabled,"value","disabled");d.attribute(disabled,"disabled","");
    auto last=d.element(select,"option");d.attribute(last,"value","last");
    d.focus(select);d.key("ArrowDown");require(d.value(select)=="SE isometric" && events==1);
    d.key("ArrowDown");require(d.value(select)=="last" && events==2);
    d.key("ArrowDown");require(d.value(select)=="last" && events==2);
    d.key("Home");require(d.value(select)=="top" && events==3);
    d.attribute(group,"disabled","");d.key("ArrowDown");require(d.value(select)=="last" && events==4);
    d.key("ArrowUp");require(d.value(select)=="top" && events==5);
    auto dispose=d.on(select,"input",[&](auto&){d.dispose();});
    d.key("End");require(d.disposed() && events==5);
  }
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
