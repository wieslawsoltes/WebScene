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
