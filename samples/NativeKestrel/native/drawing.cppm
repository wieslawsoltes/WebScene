module;
#include "../third_party/nlohmann/json.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <regex>
#include <span>
#include <stdexcept>
#include <unordered_set>
#include <vector>

export module kestrel.drawing;

// Native port of KestrelCAD src/model.js. See ../LICENSE and README.md.
export namespace kestrel {
using json = nlohmann::ordered_json;
inline std::string uid(std::string prefix = "e") {
  static std::atomic<uint64_t> sequence{};
  return prefix + "_native_" +
         std::to_string(
             std::chrono::system_clock::now().time_since_epoch().count()) +
         "_" + std::to_string(++sequence);
}
inline json default_layers() {
  json layers = json::array();
  const char *ids[] = {"0",         "architecture", "openings",
                       "furniture", "dimensions",   "annotation",
                       "hatch",     "construction"};
  const char *names[] = {"0",           "A-WALL",      "A-OPENING",
                         "A-FURNITURE", "A-DIMENSION", "A-ANNOTATION",
                         "A-HATCH",     "A-CENTER"};
  const char *colors[] = {"#dce4ed", "#dee6ed", "#59c8d9", "#a4b5c7",
                          "#65c4b6", "#c2cbd5", "#687d91", "#c69a66"};
  const double weights[] = {.25, .35, .20, .18, .15, .18, .13, .13};
  for (int i = 0; i < 8; ++i)
    layers.push_back({{"id", ids[i]},
                      {"name", names[i]},
                      {"color", colors[i]},
                      {"visible", true},
                      {"locked", false},
                      {"linetype", i == 7 ? "Center" : "Continuous"},
                      {"lineweight", weights[i]}});
  return layers;
}
inline bool finite_number(const json &v) {
  return v.is_number() && std::isfinite(v.get<double>());
}
inline void validate_project(json &data) {
  auto require = [](bool value, const std::string &message) {
    if (!value)
      throw std::invalid_argument(message);
  };
  require(data.is_object() &&
              data.value("format", std::string{}) == "kestrel-cad" &&
              (data.value("version", 0) == 1 || data.value("version", 0) == 2),
          "Unsupported Kestrel project");
  require(data.contains("entities") && data["entities"].is_array() &&
              data["entities"].size() <= 200000,
          "Invalid entity table");
  require(data.contains("layers") && data["layers"].is_array() &&
              !data["layers"].empty() && data["layers"].size() <= 2048,
          "Invalid layer table");
  if (data.contains("name"))
    require(data["name"].is_string() &&
                data["name"].get_ref<const std::string &>().size() <= 512,
            "Invalid drawing name");
  static const std::regex identifier("[-a-zA-Z0-9_.:]{1,128}"),
      color("#[0-9a-fA-F]{6}");
  auto matches = [](const json &v, const std::regex &re) {
    return v.is_string() &&
           std::regex_match(v.get_ref<const std::string &>(), re);
  };
  std::unordered_set<std::string> layers, ids;
  for (auto &l : data["layers"]) {
    require(l.is_object() && l.contains("id") && matches(l["id"], identifier) &&
                layers.insert(l["id"].get<std::string>()).second,
            "Invalid or duplicate layer identifier");
    require(l.contains("name") && l["name"].is_string() &&
                l["name"].get_ref<const std::string &>().size() <= 255,
            "Invalid layer name");
    if (!l.contains("color") || !matches(l["color"], color))
      l["color"] = "#dce4ed";
    l["visible"] = !l.contains("visible") || l["visible"] != false;
    l["locked"] = l.value("locked", false);
    l["lineweight"] = l.contains("lineweight") && finite_number(l["lineweight"])
                          ? std::clamp(l["lineweight"].get<double>(), .01, 5.)
                          : .25;
  }
  auto point = [&](json &p) {
    require(p.is_array() && p.size() >= 2 && p.size() <= 3,
            "Invalid coordinate shape");
    for (auto &v : p)
      require(finite_number(v) && std::abs(v.get<double>()) <= 1e12,
              "Invalid coordinate");
    if (p.size() == 2)
      p.push_back(0);
  };
  const std::unordered_set<std::string> types = {
      "LINE",      "POLYLINE", "CIRCLE", "ARC",   "ELLIPSE",
      "SPLINE",    "HATCH",    "POINT",  "TEXT",  "MTEXT",
      "DIMENSION", "MESH",     "INSERT", "TABLE", "LEADER"};
  size_t vertices = 0;
  for (auto &e : data["entities"]) {
    require(e.is_object() && types.contains(e.value("type", std::string{})),
            "Unsupported entity type");
    auto type = e["type"].get<std::string>();
    if (!e.contains("id"))
      e["id"] = uid();
    require(matches(e["id"], identifier) &&
                ids.insert(e["id"].get<std::string>()).second,
            "Invalid or duplicate entity identifier");
    if (!layers.contains(e.value("layer", std::string{})))
      e["layer"] = data["layers"][0]["id"];
    for (auto key : {"points", "vertices", "controlPoints"})
      if (e.contains(key)) {
        require(e[key].is_array(), "Invalid coordinate array");
        vertices += e[key].size();
        for (auto &p : e[key])
          point(p);
      }
    for (auto key :
         {"position", "center", "axisX", "axisY", "normal", "direction"})
      if (e.contains(key))
        point(e[key]);
    for (auto key : {"radius", "rx", "ry", "height", "textHeight", "spacing"})
      if (e.contains(key) && !e[key].is_null())
        require(finite_number(e[key]) && e[key].get<double>() > 0,
                "Invalid positive dimension");
    for (auto key : {"rotation", "startAngle", "endAngle", "offset", "angle"})
      if (e.contains(key) && !e[key].is_null())
        require(finite_number(e[key]), "Invalid angle or offset");
    if (type == "MESH") {
      require(e.contains("faces") && e["faces"].is_array() &&
                  e.contains("vertices") && e["faces"].size() <= 2000000,
              "Invalid mesh");
      for (auto &f : e["faces"]) {
        require(f.is_array() && f.size() >= 3 && f.size() <= 10000,
                "Invalid mesh face");
        for (auto &i : f)
          require(i.is_number_integer() && i.get<int64_t>() >= 0 &&
                      i.get<uint64_t>() < e["vertices"].size(),
                  "Invalid mesh index");
      }
    }
    if (type == "LINE" || type == "POLYLINE" || type == "DIMENSION" ||
        type == "HATCH")
      require(e.contains("points") &&
                  e["points"].size() >= (type == "HATCH" ? 3 : 2),
              "Too few entity points");
    if (type == "CIRCLE" || type == "ARC" || type == "ELLIPSE")
      require(e.contains("center"), "Missing curve center");
    if (type == "SPLINE") {
      auto count = e.contains("controlPoints") ? e["controlPoints"].size()
                   : e.contains("points")      ? e["points"].size()
                                               : 0;
      require(count >= 2, "Too few spline points");
      if (e.contains("degree"))
        require(e["degree"].is_number_integer() &&
                    e["degree"].get<int>() >= 1 &&
                    e["degree"].get<int>() <= std::min<size_t>(10, count - 1),
                "Invalid spline degree");
      if (e.contains("knots")) {
        require(e["knots"].is_array(), "Invalid knot sequence");
        double previous = -INFINITY;
        for (auto &v : e["knots"]) {
          require(finite_number(v) && v.get<double>() >= previous,
                  "Invalid knot order");
          previous = v.get<double>();
        }
      }
      if (e.contains("weights")) {
        require(e["weights"].is_array() && e["weights"].size() == count,
                "Invalid spline weights");
        for (auto &v : e["weights"])
          require(finite_number(v) && v.get<double>() > 0,
                  "Invalid spline weight");
      }
    }
    if (type == "POINT" || type == "TEXT" || type == "MTEXT")
      require(e.contains("position"), "Missing insertion point");
    if (type == "TEXT" || type == "MTEXT")
      require(e.contains("text") && e["text"].is_string() &&
                  e["text"].get_ref<const std::string &>().size() <= 100000,
              "Invalid text entity");
    if (e.contains("color") && e["color"] != "bylayer" &&
        e["color"] != "byblock" && !matches(e["color"], color))
      e["color"] = "bylayer";
  }
  require(vertices <= 3000000, "Project vertex limit exceeded");
  if (data.contains("camera") && !data["camera"].is_null()) {
    auto &c = data["camera"];
    require(c.contains("target"), "Missing camera target");
    point(c["target"]);
    for (auto key : {"zoom", "yaw", "pitch"})
      require(c.contains(key) && finite_number(c[key]), "Invalid camera");
    require(c["zoom"].get<double>() > 0, "Invalid camera zoom");
  }
  const std::unordered_set<std::string> units = {"mm", "cm", "m",
                                                 "in", "ft", "unitless"};
  if (!units.contains(data.value("units", std::string{})))
    data["units"] = "mm";
}
class drawing {
  struct history {
    std::string label;
    json state;
    size_t bytes;
  };
  std::vector<history> undo_, redo_;
  void reindex() {
    if (data["layers"].empty())
      throw std::invalid_argument("Drawing requires a layer");
    bool current = false;
    for (auto &l : data["layers"])
      current |= l["id"] == data["currentLayer"];
    if (!current)
      data["currentLayer"] = data["layers"][0]["id"];
    std::erase_if(selection, [&](auto &id) { return !find(id); });
  }
  void changed(const std::string &label) {
    ++revision;
    dirty = true;
    if (on_change)
      on_change(label);
  }

public:
  json data = {{"format", "kestrel-cad"},
               {"version", 2},
               {"production", nullptr},
               {"name", "Untitled"},
               {"units", "mm"},
               {"currentLayer", "architecture"},
               {"layers", default_layers()},
               {"entities", json::array()},
               {"camera", nullptr}};
  json source_document = nullptr;
  std::unordered_set<std::string> selection;
  uint64_t revision{};
  bool dirty{};
  std::function<void(const std::string &)> on_change;
  // Extension validators/constraints are supplied by the corresponding native
  // modules as they are ported. They participate in transaction rollback.
  std::function<void(drawing &)> enforce;
  json *find(const std::string &id) {
    for (auto &e : data["entities"])
      if (e["id"] == id)
        return &e;
    return nullptr;
  }
  const json &layer(const json &e) const {
    for (auto &l : data["layers"])
      if (l["id"] == e.value("layer", std::string{}))
        return l;
    return data["layers"][0];
  }
  bool visible(const json &e) const {
    return layer(e).value("visible", true) && !e.value("hidden", false);
  }
  bool editable(const json &e) const {
    return visible(e) && !layer(e).value("locked", false);
  }
  void select_all_editable() {
    selection.clear();
    for(const auto& entity:data["entities"])
      if(editable(entity)) selection.insert(entity["id"].get<std::string>());
  }
  std::vector<std::string> selected(bool editable_only = false) {
    std::vector<std::string> result;
    for (auto &id : selection)
      if (auto *e = find(id); e && (!editable_only || editable(*e)))
        result.push_back(id);
    return result;
  }
  std::string add_layer(std::string name, std::string color = "#70b8d3") {
    auto first = name.find_first_not_of(" \t\r\n"),
         last = name.find_last_not_of(" \t\r\n");
    if (first == std::string::npos)
      throw std::invalid_argument("A layer name is required");
    name = name.substr(first, last - first + 1);
    auto fold = [](std::string text) {
      for (auto &c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return text;
    };
    for (auto &l : data["layers"])
      if (fold(l["name"].get<std::string>()) == fold(name))
        throw std::invalid_argument("Layer names must be unique");
    auto id = uid("layer");
    data["layers"].push_back({{"id", id},
                              {"name", name},
                              {"color", color},
                              {"visible", true},
                              {"locked", false},
                              {"linetype", "Continuous"},
                              {"lineweight", .25}});
    reindex();
    return id;
  }
  std::string add(std::string type, json properties = json::object()) {
    json e = {{"id", uid()},
              {"type", std::move(type)},
              {"layer", data["currentLayer"]},
              {"color", "bylayer"},
              {"linetype", "ByLayer"}};
    e.update(properties);
    auto id = e["id"].get<std::string>();
    data["entities"].push_back(std::move(e));
    return id;
  }
  void remove(const std::unordered_set<std::string> &ids) {
    std::erase_if(data["entities"].get_ref<json::array_t &>(), [&](auto &e) {
      return ids.contains(e["id"].template get<std::string>());
    });
    reindex();
  }
  void replace(const std::string &id, json e) {
    if (auto *old = find(id)) {
      e["id"] = id;
      *old = std::move(e);
    }
  }
  json serialize(bool include_source = true) const {
    auto result = data;
    if (include_source && !source_document.is_null())
      result["sourceDocument"] = source_document;
    return result;
  }
  void apply(json value, bool retain_source = false) {
    auto source = value.value("sourceDocument", json{});
    value.erase("sourceDocument");
    validate_project(value);
    data = std::move(value);
    if (!source.is_null() || !retain_source)
      source_document = std::move(source);
    reindex();
  }
  template <class F> bool transaction(std::string label, F operation) {
    auto before = data;
    auto old_selection = selection;
    try {
      operation();
      reindex();
      if (enforce)
        enforce(*this);
      validate_project(data);
    } catch (...) {
      data = std::move(before);
      selection = std::move(old_selection);
      throw;
    }
    if (before == data)
      return false;
    auto bytes = before.dump().size();
    undo_.push_back({label, std::move(before), bytes});
    redo_.clear();
    size_t total = 0;
    for (auto &h : undo_)
      total += h.bytes;
    while (undo_.size() > 80 || (total > 32000000 && undo_.size() > 1)) {
      total -= undo_.front().bytes;
      undo_.erase(undo_.begin());
    }
    changed(label);
    return true;
  }
  bool change_selected_layer(const std::string& layer_id) {
    if(!std::any_of(data["layers"].begin(),data["layers"].end(),[&](const auto& layer){return layer["id"]==layer_id;}))return false;
    const auto ids=selected(true);if(ids.empty())return false;
    return transaction("Edit layer",[&] {for(const auto& id:ids)if(auto* entity=find(id))(*entity)["layer"]=layer_id;});
  }
  bool change_selected_appearance(const std::string& key,const json& value) {
    if(key=="linetype") {
      if(!value.is_string())return false;
      const auto type=value.get<std::string>();
      if(type!="ByLayer" && type!="Continuous" && type!="Dashed" && type!="Center")return false;
    } else if(key=="lineweight") {
      if(!value.is_number() || !std::isfinite(value.get<double>()))return false;
    } else if(key=="name") {
      if(!value.is_string())return false;
    } else return false;
    const auto ids=selected(true);if(ids.empty())return false;
    return transaction("Edit "+key,[&] {for(const auto& id:ids)if(auto* entity=find(id))(*entity)[key]=value;});
  }
  bool show_all_layers() {
    return transaction("Show all layers",[&] {
      for(auto& layer:data["layers"])layer["visible"]=true;
    });
  }
  size_t erase_selected() {
    const auto ids=selected(true);
    if(ids.empty())return 0;
    transaction("Erase "+std::to_string(ids.size())+" objects",[&] {
      remove(std::unordered_set<std::string>(ids.begin(),ids.end()));
    });
    return ids.size();
  }
  std::optional<std::string> undo() {
    if (undo_.empty())
      return {};
    auto item = std::move(undo_.back());
    undo_.pop_back();
    redo_.push_back({item.label, data, data.dump().size()});
    data = std::move(item.state);
    reindex();
    changed("Undo " + item.label);
    return item.label;
  }
  std::optional<std::string> redo() {
    if (redo_.empty())
      return {};
    auto item = std::move(redo_.back());
    redo_.pop_back();
    undo_.push_back({item.label, data, data.dump().size()});
    data = std::move(item.state);
    reindex();
    changed("Redo " + item.label);
    return item.label;
  }
};
} // namespace kestrel
