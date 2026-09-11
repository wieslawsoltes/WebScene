#include "../../samples/NativeKestrel/third_party/nlohmann/json.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>
import kestrel.drawing;
#include <iostream>
static void check(bool v, const char *message) {
  if (!v)
    throw std::runtime_error(message);
}
int main() {
  {
    using point=std::array<double,3>;
    check(kestrel::parse_drafting_point("1,2",{5,6,7})==point{1,2,7},"absolute XY elevation lost");
    check(kestrel::parse_drafting_point("@1,2",{5,6,7})==point{6,8,7},"relative XY failed");
    check(kestrel::parse_drafting_point("1,2,3",{5,6,7})==point{1,2,3},"absolute XYZ failed");
    check(kestrel::parse_drafting_point("0x10,0b11,0o7")==point{16,3,7},"Number radix forms failed");
    check(kestrel::parse_drafting_point("+1e2,.5,1.")==point{100,.5,1},"decimal grammar failed");
    check(kestrel::parse_drafting_point("\xc2\xa0" "1,2" "\xef\xbb\xbf")==point{1,2,0},"Unicode trim failed");
    check(kestrel::parse_drafting_point("1e-999,0")==point{0,0,0},"decimal underflow failed");
    auto polar=kestrel::parse_drafting_point("@10<90",{5,6,7});
    check(std::abs(polar[0]-5)<1e-8 && std::abs(polar[1]-16)<1e-8 && polar[2]==7,"relative polar failed");
    for(auto invalid:{"1", "1,", "1,2,3,4", "1x,2", "-0x10,2", "0x1p2,2", "0b2,2", "0o8,2", "0x,2", "1e13,2", "nan,2", "1<2<3"}) {
      bool rejected=false;try { kestrel::parse_drafting_point(invalid); } catch(const std::invalid_argument&) { rejected=true; }
      check(rejected,"invalid coordinate accepted");
    }
  }

  {
    kestrel::drawing model;
    kestrel::line_command line(model);
    const auto before=model.data;
    check(line.point({0,0,0}) && model.data==before,"first Line point creates geometry");
    check(!line.point({1e-9,0,0}),"near duplicate Line point accepted");
    line.color="#123456";line.lineweight=.5;
    check(line.point({10,0,0}) && line.point({10,10,0}),"continuous Line failed");
    const auto& entity=model.data["entities"].back();
    check(entity["color"]=="#123456" && entity["lineweight"]==.5,"Line defaults lost");
    check(line.undo_point() && line.points().size()==2,"Line Undo did not remove endpoint");
    check(line.undo_point() && model.data==before,"Line Undo did not restore drawing");
    check(line.undo_point() && !line.undo_point(),"first point Undo failed");
    model.data["layers"][0]["locked"]=true;
    model.data["currentLayer"]=model.data["layers"][0]["id"];
    const auto locked=model.data;
    line.point({0,0,0});bool rejected=false;
    try { line.point({1,0,0}); } catch(const std::invalid_argument&) { rejected=true; }
    check(rejected && model.data==locked && line.points().size()==1,"locked Line partially committed");
    line.cancel();check(line.points().empty() && model.data==locked,"cancel changed committed geometry");
  }

  {
    kestrel::drawing model;
    const auto a=model.add("POINT",{{"position",{0,0,0}},{"group","old"}});
    const auto b=model.add("POINT",{{"position",{1,0,0}}});
    const auto c=model.add("POINT",{{"position",{2,0,0}},{"group","old"}});
    const auto before=model.data;
    check(!model.group_entities({a,a},"duplicate"),"duplicate selection counted twice");
    check(!model.group_entities({a,"missing"},"invalid") && model.data==before,"invalid group partially applied");
    model.selection={c}; // A dialog submits its captured IDs, not current selection.
    check(model.group_entities({a,b},"  Building  "),"group creation failed");
    check(model.find(a)->at("group")==model.find(b)->at("group"),"members have different groups");
    check(model.find(a)->at("groupName")=="Building" && model.find(c)->at("group")=="old","name or original peers changed");
    check(model.undo()=="Create group" && model.data==before,"group undo failed");
    check(model.group_entities({a,b}," \t"),"blank group name failed");
    check(model.find(a)->at("groupName")=="Group","blank group name fallback failed");
    check(model.undo()=="Create group" && model.data==before,"fallback group undo failed");
    check(model.group_entities({a,b},"\xc2\xa0\xe3\x80\x80" "Bâtiment 🏠" "\xef\xbb\xbf"),"Unicode group failed");
    check(model.find(a)->at("groupName")=="Bâtiment 🏠","Unicode whitespace or content changed");
    check(model.undo()=="Create group" && model.data==before,"Unicode group undo failed");
    check(model.group_entities({a,b},"\xe3\x80\x80\xc2\xa0"),"Unicode blank name failed");
    check(model.find(a)->at("groupName")=="Group","Unicode blank fallback failed");
    check(model.undo()=="Create group" && model.data==before,"Unicode blank undo failed");
    (*model.find(b))["hidden"]=true;const auto hidden=model.data;
    check(!model.group_entities({a,b},"Hidden") && model.data==hidden,"noneditable captured member grouped");
  }
  {
    kestrel::drawing model;
    const auto a=model.add("POINT",{{"position",{0,0,0}},{"group","g"},{"groupName","Group"}});
    const auto b=model.add("POINT",{{"position",{1,0,0}},{"group","g"},{"groupName","Group"}});
    const auto c=model.add("POINT",{{"position",{2,0,0}},{"group","other"}});
    auto locked_layer=model.data["layers"][0];locked_layer["id"]="locked-group-layer";
    locked_layer["name"]="Locked group";locked_layer["locked"]=true;
    model.data["layers"].push_back(locked_layer);
    const auto locked=model.add("POINT",{{"position",{3,0,0}},{"group","g"},{"layer","locked-group-layer"}});
    const auto hidden=model.add("POINT",{{"position",{4,0,0}},{"group","g"},{"hidden",true}});
    model.selection.insert(a);const auto before=model.data;
    check(model.ungroup_selected(),"ungroup did not change model");
    check(!model.find(a)->contains("group") && !model.find(b)->contains("groupName"),"group peers not detached");
    check(model.find(c)->at("group")=="other","unrelated group changed");
    check(model.find(locked)->at("group")=="g" && model.find(hidden)->at("group")=="g","noneditable group members changed");
    check(model.undo()=="Ungroup" && model.data==before,"ungroup undo failed");
    model.selection.clear();check(!model.ungroup_selected(),"empty ungroup changed history");
  }
  kestrel::drawing d;
  std::string line;
  check(!d.transaction("empty", [] {}), "empty transaction changed history");
  check(d.transaction("Line",
                      [&] {
                        line = d.add("LINE",
                                     {{"points", {{0, 0, 0}, {10, 20, 0}}}});
                      }),
        "line edit missing");
  check(d.revision == 1 && d.dirty && d.find(line), "drawing state");
  d.selection.insert(line);
  d.source_document = {{"archive", "retained"}};
  auto before = d.serialize();
  bool rejected = false;
  try {
    d.transaction("Invalid",
                  [&] { d.find(line)->at("points") = kestrel::json::array(); });
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected && d.serialize() == before && d.selection.contains(line) &&
            d.revision == 1,
        "rollback failed");
  check(d.undo() == "Line" && !d.find(line) && d.selection.empty(),
        "undo/selection pruning");
  check(d.source_document["archive"] == "retained",
        "undo erased source archive");
  check(d.redo() == "Line" && d.find(line), "redo failed");
  d.undo();
  d.transaction("Point", [&] { d.add("POINT", {{"position", {2, 3}}}); });
  check(!d.redo(), "new edit did not clear redo history");
  auto saved = d.serialize();
  kestrel::drawing loaded;
  loaded.apply(kestrel::json::parse(saved.dump()));
  check(loaded.serialize() == saved, "persistence round trip");
  loaded.data["layers"][1]["locked"] = true;
  check(!loaded.editable(loaded.data["entities"][0]), "locked layer editable");
  auto invalid = saved;
  invalid["entities"][0]["type"] = "MESH";
  invalid["entities"][0]["vertices"] = {{0, 0, 0}};
  invalid["entities"][0]["faces"] = {{0, 1, 2}};
  rejected = false;
  try {
    loaded.apply(invalid);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected, "out-of-range mesh face accepted");
  kestrel::drawing bounded;
  for (int i = 0; i < 90; ++i)
    bounded.transaction("Point",
                        [&] { bounded.add("POINT", {{"position", {i, 0}}}); });
  int count = 0;
  while (bounded.undo())
    ++count;
  check(count == 80 && bounded.data["entities"].size() == 10,
        "history entry limit");
  std::cout << "Kestrel drawing: transactions, rollback, persistence, layers "
               "and history passed\n";
}
