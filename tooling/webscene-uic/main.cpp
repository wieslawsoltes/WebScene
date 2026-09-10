#include "webscene/native_web.hpp"
#include "webscene_css_parser.h"
#include "webscene_html_parser.h"
#include "webscene_selector_parser.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
using namespace webscene_native;
namespace fs = std::filesystem;
static std::string read(const fs::path &path) {
  std::ifstream f(path);
  if (!f)
    throw std::runtime_error("cannot read " + path.string());
  return {std::istreambuf_iterator<char>(f), {}};
}
static std::string quote(std::string_view value) {
  std::ostringstream out;
  out << '"';
  for (unsigned char c : value) {
    if (c == '"' || c == '\\')
      out << '\\' << c;
    else if (c >= 32 && c < 127)
      out << c;
    else
      out << '\\' << std::oct << std::setw(3) << std::setfill('0')
          << unsigned(c) << std::dec;
  }
  out << '"';
  return out.str();
}
static std::string number(float value) {
  std::ostringstream out;
  out << std::setprecision(9) << value;
  auto s = out.str();
  if (s.find_first_of(".eE") == s.npos)
    s += ".0";
  return s + "f";
}
static std::string trim(std::string value) {
  auto b = value.find_first_not_of(" \t\r\n\f");
  if (b == value.npos)
    return {};
  return value.substr(b, value.find_last_not_of(" \t\r\n\f") - b + 1);
}
static std::string length(const std::string &value) {
  static const std::regex valid(
      R"(^(-?[0-9]+(\.[0-9]+)?(px|%|em|rem|vw|vh)?|auto)$)");
  if (!std::regex_match(value, valid) ||
      (value != "auto" && value.back() >= '0' && value.back() <= '9' &&
       std::stof(value) != 0))
    throw std::runtime_error("unsupported length: " + value);
  auto v = native_document::parse_length(value);
  return "{" + number(v.value) +
         ", static_cast<webscene::native_web::length_unit>(" +
         std::to_string(int(v.unit)) + "), " + number(v.pixel_offset) + "}";
}
// Build-time track lowering: emitted applications receive typed values only.
static std::string grid_track_code(std::string value) {
  value = trim(value);
  const std::string prefix = "webscene::native_web::grid_track::sizing::";
  if (value == "auto" || value == "min-content")
    return "{{}, {}, 0.0f, " + prefix +
           (value == "auto" ? "automatic" : "min_content") + "}";
  if (std::regex_match(value, std::regex(R"([0-9]+(\.[0-9]+)?fr)")))
    return "{{}, {}, " + number(std::stof(value)) + ", " + prefix +
           "fractional}";
  if (value.starts_with("minmax(") && value.ends_with(")")) {
    auto comma = value.find(',');
    if (comma == value.npos || value.find(',', comma + 1) != value.npos)
      throw std::runtime_error("invalid grid minmax: " + value);
    auto minimum = trim(value.substr(7, comma - 7));
    auto maximum = trim(value.substr(comma + 1, value.size() - comma - 2));
    if (minimum.starts_with("-") || maximum.starts_with("-"))
      throw std::runtime_error("negative grid minmax track: " + value);
    bool fraction =
        std::regex_match(maximum, std::regex(R"([0-9]+(\.[0-9]+)?fr)"));
    return "{" + length(minimum) + ", " +
           (fraction ? length("auto") : length(maximum)) + ", " +
           number(fraction ? std::stof(maximum) : 0) + ", " + prefix +
           "minmax}";
  }
  if (!value.empty() && value.front() == '-')
    throw std::runtime_error("negative grid track: " + value);
  return "{" + length(value) + ", " + length(value) + ", 0.0f, " + prefix +
         "fixed}";
}
static std::string grid_tracks_code(const std::string &value, bool in_repeat = false) {
  if (value == "none")
    return "{}";
  std::string result = "{", token;
  int depth = 0;
  auto flush = [&] {
    if (token.empty())
      return;
    if (result.size() > 1)
      result += ",";
    if (token.starts_with("repeat(") && token.ends_with(")")) {
      if (in_repeat) throw std::runtime_error("nested grid repeat is invalid");
      auto comma = token.find(',');
      if (comma == token.npos) throw std::runtime_error("grid repeat requires count and tracks");
      auto count_text = trim(token.substr(7, comma - 7));
      if (!std::regex_match(count_text, std::regex("[0-9]+")))
        throw std::runtime_error("grid repeat currently requires an integer count");
      auto count = std::stoul(count_text);
      if (!count || count > 1024) throw std::runtime_error("grid repeat count must be 1..1024");
      auto body = trim(token.substr(comma + 1, token.size() - comma - 2));
      if (body == "none") throw std::runtime_error("grid repeat requires tracks");
      auto tracks = grid_tracks_code(body, true);
      tracks = tracks.substr(1, tracks.size() - 2);
      for (unsigned i = 0; i < count; ++i) {
        if (i) result += ",";
        result += tracks;
      }
    } else {
      result += grid_track_code(token);
    }
    token.clear();
  };
  for (char c : value) {
    if (c == '(')
      ++depth;
    if (c == ')' && --depth < 0)
      throw std::runtime_error("unbalanced grid track");
    if (std::isspace(static_cast<unsigned char>(c)) && depth == 0)
      flush();
    else
      token += c;
  }
  if (depth != 0)
    throw std::runtime_error("unbalanced grid track");
  flush();
  if (result.size() == 1)
    throw std::runtime_error("empty grid track list");
  return result + "}";
}
static std::string assignments(const std::string &name,
                               const std::string &value) {
  static const std::set<std::string> lengths = {"width",
                                                "height",
                                                "min-width",
                                                "min-height",
                                                "max-width",
                                                "max-height",
                                                "left",
                                                "top",
                                                "right",
                                                "bottom",
                                                "padding-left",
                                                "padding-top",
                                                "padding-right",
                                                "padding-bottom",
                                                "margin-left",
                                                "margin-top",
                                                "margin-right",
                                                "margin-bottom",
                                                "row-gap",
                                                "column-gap",
                                                "flex-basis",
                                                "border-top-left-radius",
                                                "border-top-right-radius",
                                                "border-bottom-left-radius",
                                                "border-bottom-right-radius"};
  auto member = name;
  std::replace(member.begin(), member.end(), '-', '_');
  if (name == "grid-template-columns" || name == "grid-template-rows")
    return "s.set_" + member + "(" + grid_tracks_code(value) + ");";
  if (lengths.contains(name)) {
    auto result = "s." + member + " = " + length(value) + ";";
    if (name.starts_with("margin-"))
      result +=
          "s." + member + "_auto = " + (value == "auto" ? "true;" : "false;");
    return result;
  }
  if (name == "padding" || name == "margin" || name == "border-radius") {
    std::istringstream in(value);
    std::vector<std::string> values;
    std::string v;
    while (in >> v)
      values.push_back(v);
    if (values.empty() || values.size() > 4)
      throw std::runtime_error("invalid box shorthand");
    std::string a = values[0], b = values.size() > 1 ? values[1] : a,
                c = values.size() > 2 ? values[2] : a,
                d = values.size() > 3 ? values[3] : b;
    std::vector<std::string> names =
        name == "border-radius"
            ? std::vector<std::string>{"border-top-left-radius",
                                       "border-top-right-radius",
                                       "border-bottom-right-radius",
                                       "border-bottom-left-radius"}
            : std::vector<std::string>{name + "-top", name + "-right",
                                       name + "-bottom", name + "-left"};
    return assignments(names[0], a) + assignments(names[1], b) +
           assignments(names[2], c) + assignments(names[3], d);
  }
  if (name == "gap") {
    std::istringstream in(value);
    std::string a, b, c;
    in >> a;
    if (!(in >> b))
      b = a;
    if (in >> c)
      throw std::runtime_error("invalid gap");
    return assignments("row-gap", a) + assignments("column-gap", b);
  }
  if (name == "background" || name == "background-color" || name == "color") {
    if (!std::regex_match(value, std::regex("#([0-9a-fA-F]{3}|[0-9a-fA-F]{4}|["
                                            "0-9a-fA-F]{6}|[0-9a-fA-F]{8})")) &&
        value != "transparent" && value != "black" && value != "white")
      throw std::runtime_error("color profile requires #rgb, #rgba, #rrggbb, "
                               "#rrggbbaa, black, white or transparent");
    return std::string("s.") +
           (name == "color" ? "foreground_rgba" : "background_rgba") + " = " +
           std::to_string(native_document::parse_color(value)) + "u;";
  }
  static const std::map<
      std::string, std::pair<std::string, std::map<std::string, std::string>>>
      enums = {
          {"display",
           {"display",
            {{"block", "block"},
             {"inline", "inline_flow"},
             {"inline-block", "inline_block"},
             {"flex", "flex"},
             {"inline-flex", "inline_flex"},
             {"grid", "grid"},
             {"inline-grid", "inline_grid"},
             {"none", "none"}}}},
          {"flex-direction",
           {"direction", {{"row", "row"}, {"column", "column"}}}},
          {"align-items",
           {"align_items",
            {{"start", "start"},
             {"flex-start", "start"},
             {"center", "center"},
             {"end", "end"},
             {"flex-end", "end"},
             {"stretch", "stretch"}}}},
          {"justify-content",
           {"justify_content",
            {{"start", "start"},
             {"flex-start", "start"},
             {"center", "center"},
             {"end", "end"},
             {"flex-end", "end"},
             {"space-between", "space_between"}}}},
          {"position",
           {"position",
            {{"static", "normal"},
             {"relative", "relative"},
             {"absolute", "absolute"},
             {"fixed", "fixed"}}}},
          {"box-sizing",
           {"border_box", {{"border-box", "true"}, {"content-box", "false"}}}}};
  if (auto it = enums.find(name); it != enums.end()) {
    auto val = it->second.second.find(value);
    if (val == it->second.second.end())
      throw std::runtime_error("unsupported " + name + ": " + value);
    auto m = it->second.first;
    const std::map<std::string, std::string> enum_types{
        {"display", "display_mode"},
        {"direction", "flex_direction"},
        {"align_items", "align_mode"},
        {"justify_content", "justify_mode"},
        {"position", "position_mode"}};
    return "s." + m + " = " +
           (m == "border_box" ? val->second
                              : "webscene::native_web::" + enum_types.at(m) +
                                    "::" + val->second) +
           ";";
  }
  if (name == "font-family") {
    if (value.empty())
      throw std::runtime_error("Empty font family");
    return "s.set_font_family(" +
           quote(value == "inherit" || value == "unset" ? "" : value) + ");";
  }
  if (name == "font-size") {
    if (!value.ends_with("px"))
      throw std::runtime_error("font-size currently requires px");
    auto l = native_document::parse_length(value);
    length(value);
    if (l.value < 0)
      throw std::runtime_error("negative font size");
    return "s.font_size = " + number(l.value) + ";";
  }
  if (name == "font-weight" || name == "opacity" || name == "flex-grow" ||
      name == "flex-shrink") {
    if (!std::regex_match(value, std::regex(R"([0-9]+(\.[0-9]+)?)")))
      throw std::runtime_error("invalid numeric value");
    auto v = std::stof(value);
    if ((name == "opacity" && v > 1) ||
        (name == "font-weight" && (v < 1 || v > 1000 || v != int(v))))
      throw std::runtime_error("numeric value out of range");
    return "s." + member + " = " + number(v) + ";";
  }
  throw std::runtime_error("unsupported Native Web CSS property: " + name);
}
static std::string selector_code(const selector_syntax_selector &sel) {
  std::string result = "{{";
  for (size_t i = 0; i < sel.compounds.size(); ++i) {
    std::string tag, id;
    std::vector<std::string> classes;
    std::vector<std::string> attributes;
    bool focus = false, hover = false, root = false, active = false, disabled = false, focus_visible = false;
    auto input = sel.compounds[i];
    size_t p = 0;
    while (p < input.size()) {
      if (input[p] == '[') {
        // The selector parser validates CSS syntax first. This profile supports
        // presence and exact equality, retaining explicit diagnostics for
        // others.
        std::smatch match;
        auto remaining = input.substr(p);
        static const std::regex attribute(
            R"attr(^\[([a-zA-Z_][a-zA-Z0-9_-]*)(?:\s*=\s*(?:"([^"\\]*)"|'([^'\\]*)'|([a-zA-Z0-9_-]+)))?\s*\])attr");
        if (!std::regex_search(remaining, match, attribute))
          throw std::runtime_error("unsupported attribute selector: " +
                                   sel.serialized);
        bool equals = match[2].matched || match[3].matched || match[4].matched;
        auto value = match[2].matched   ? match[2].str()
                     : match[3].matched ? match[3].str()
                                        : match[4].str();
        attributes.push_back("{" + quote(match[1].str()) + "," + quote(value) +
                             "," + (equals ? "true" : "false") + "}");
        p += match.length();
        continue;
      }
      char prefix = input[p];
      if (prefix == '.' || prefix == '#' || prefix == ':')
        ++p;
      else
        prefix = 0;
      size_t b = p;
      while (p < input.size() &&
             (std::isalnum(static_cast<unsigned char>(input[p])) ||
              input[p] == '_' || input[p] == '-' || input[p] == '*'))
        ++p;
      if (b == p)
        throw std::runtime_error("unsupported selector: " + sel.serialized);
      auto name = input.substr(b, p - b);
      if (prefix == '.')
        classes.push_back(name);
      else if (prefix == '#')
        id = name;
      else if (prefix == ':') {
        if (name == "focus")
          focus = true;
        else if (name == "root")
          root = true;
        else if (name == "focus-visible")
          focus_visible = true;
        else if (name == "active")
          active = true;
        else if (name == "disabled")
          disabled = true;
        else if (name == "hover")
          hover = true;
        else
          throw std::runtime_error("unsupported pseudo-class: " + name);
      } else
        tag = name;
    }
    char relation = i ? sel.combinators.at(i - 1) : 0;
    if (relation && relation != ' ' && relation != '>')
      throw std::runtime_error("unsupported combinator");
    if (i)
      result += ",";
    result += "{" + quote(tag) + "," + quote(id) + ",{";
    for (size_t j = 0; j < classes.size(); ++j) {
      if (j)
        result += ",";
      result += quote(classes[j]);
    }
    result += "}," + std::string(focus ? "true" : "false") + "," +
              (hover ? "true" : "false") + "," + std::to_string(int(relation)) +
              "," + (root ? "true" : "false") + ",{";
    for (size_t j = 0; j < attributes.size(); ++j) {
      if (j)
        result += ",";
      result += attributes[j];
    }
    result += "}," + std::string(active ? "true" : "false") + "," + (disabled ? "true" : "false") + "," + (focus_visible ? "true" : "false") + "}";
  }
  return result + "}," + std::to_string(sel.specificity) + "}";
}
struct compiler {
  std::ostringstream out;
  fs::path source;
  std::string content;
  size_t location{1};
  std::set<std::string> ids;
  std::vector<std::pair<std::string, std::string>> names;
  std::vector<fs::path> dependencies;
  unsigned count{};
  bool in_template{};
  std::vector<std::pair<std::string, const dom_node *>> templates;
  void locate(std::string_view text) {
    auto at = content.find(text);
    location =
        at == std::string::npos
            ? 1
            : 1 + std::count(content.begin(), content.begin() + at, '\n');
  }
  void declarations(const css_syntax_output &css, const css_syntax_rule &r) {
    out << "{";
    for (size_t j = 0; j < r.declaration_count; ++j) {
      const auto &d = css.declarations.at(r.first_declaration + j);
      locate(d.name);
      if (j)
        out << ",";
      out << "{" << (d.important ? "true" : "false")
          << ", +[](webscene::native_web::style& s){"
          << std::regex_replace(assignments(d.name, trim(d.value)),
                                std::regex(R"(s\.([a-z_]+) = ([^;]+);)"),
                                "s.set_$1($2);")
          << "}}";
    }
    out << "}";
  }
  void stylesheet(const std::string &text) {
    auto css = parse_css_syntax_stylesheet(text);
    if (!css || css.metrics.parse_error_count)
      throw std::runtime_error("invalid CSS stylesheet: " + css.error);
    std::vector<std::pair<float, float>> bounds(css.rules.size(), {0, 1e9f});
    for (size_t i = 0; i < css.rules.size(); ++i) {
      const auto &r = css.rules[i];
      auto &range = bounds[i];
      if (r.parent_index != css_syntax_no_parent)
        range = bounds.at(r.parent_index);
      if (r.kind == css_syntax_at_rule) {
        std::smatch match;
        if (r.name != "media" ||
            !std::regex_match(
                r.prelude, match,
                std::regex(R"(\s*\((min|max)-width\s*:\s*([0-9]+)px\)\s*)")))
          throw std::runtime_error("only @media (min-width: Npx) / (max-width: "
                                   "Npx) supported initially");
        auto v = std::stof(match[2]);
        if (match[1] == "min")
          range.first = std::max(range.first, v);
        else
          range.second = std::min(range.second, v);
        continue;
      }
      auto selectors = parse_selector_syntax(r.prelude);
      if (!selectors)
        throw std::runtime_error(selectors.error);
      for (const auto &sel : selectors.selectors) {
        out << "d.add_rule({" << selector_code(sel) << ",";
        declarations(css, r);
        out << "," << number(range.first) << "," << number(range.second)
            << "});\n";
      }
    }
  }
  void node(const dom_node &n, const std::string &parent) {
    if (n.tag == "template") {
      if (in_template)
        throw std::runtime_error("nested compiled templates are not supported");
      return; // Inert; emitted as an instantiation function.
    }
    if (n.tag == "#comment" || n.tag == "#doctype")
      return;
    if (n.tag == "#text") {
      if (trim(n.text_content).empty())
        return;
      out << "d.text(" << parent << "," << quote(n.text_content) << ");\n";
      return;
    }
    static const std::set<std::string> tags = {
        "body", "main",   "section", "div", "span", "p",      "h1",     "h2",
        "h3",   "button", "canvas",  "ul",  "li",   "header", "footer", "nav"};
    if (!tags.contains(n.tag))
      throw std::runtime_error("unsupported Native Web element: " + n.tag);
    auto local = "n" + std::to_string(++count);
    locate("<" + n.tag);
    out << "#line " << location << " " << quote(source.string()) << "\n";
    if (n.tag == "body")
      out << "auto " << local << " = d.body();\n";
    else
      out << "auto " << local << " = d.element(" << parent << ","
          << quote(n.tag) << ");\n";
    std::vector<std::pair<std::string, std::string>> attrs(n.attributes.begin(),
                                                           n.attributes.end());
    std::ranges::sort(attrs);
    for (const auto &[k, v] : attrs) {
      if (k == "style") {
        auto css = parse_css_syntax_declarations(v);
        if (!css || css.metrics.parse_error_count)
          throw std::runtime_error("invalid inline style");
        css_syntax_rule r{};
        r.first_declaration = 0;
        r.declaration_count = css.declarations.size();
        out << "d.add_rule({{},";
        declarations(css, r);
        out << ",0,1e9f," << local << "});\n";
        continue;
      }
      if (k.starts_with("on"))
        throw std::runtime_error(
            "JavaScript attributes are not supported in Native Web");
      if (k != "id" && k != "class" && k != "width" && k != "height" &&
          k != "tabindex" && k != "disabled" && k != "type" && k != "role" &&
          !k.starts_with("aria-") && !k.starts_with("data-"))
        throw std::runtime_error("unsupported attribute: " + k);
      if (in_template && k == "id")
        throw std::runtime_error(
            "template elements use data-ref instead of document-global id");
      if (k == "id" || (in_template && k == "data-ref")) {
        if (!ids.insert(v).second)
          throw std::runtime_error("duplicate id: " + v);
        names.emplace_back(v, local);
      }
      out << "d.attribute(" << local << "," << quote(k) << "," << quote(v)
          << ");\n";
    }
    for (auto *c : n.children)
      node(*c, local);
  }
  void compile(const fs::path &input, const fs::path &output,
               const std::string &module_name = {}) {
    source = fs::absolute(input);
    content = read(source);
    dependencies.push_back(source);
    native_document dom;
    auto &root = dom.create_element("html");
    html_parse_options options;
    options.scripting_enabled = false;
    auto parsed = parse_html_document(dom, root, content, options);
    if (!parsed)
      throw std::runtime_error(parsed.error);
    out << "// Generated by webscene-uic. Do not edit.\n";
    if (module_name.empty()) {
      out << "#pragma once\n#include <webscene/native_web.hpp>\n";
    } else {
      out << "module;\n#include <webscene/native_web.hpp>\n"
          << "export module " << module_name << ";\nexport ";
    }
    out << "namespace compiled_ui {\nstruct view {\n";
    // Collect metadata and code separately, so references are returned without
    // exposing engine nodes.
    std::ostringstream prefix;
    prefix.swap(out);
    const dom_node *body = nullptr;
    const auto walk = [&](auto &&self, const dom_node &n) -> void {
      if (n.tag == "template") {
        auto id = n.attributes.find("id");
        if (id == n.attributes.end() || id->second.empty())
          throw std::runtime_error("compiled template requires a nonempty id");
        for (const auto &entry : templates)
          if (entry.first == id->second)
            throw std::runtime_error("duplicate template: " + id->second);
        templates.emplace_back(id->second, &n);
        return;
      }
      if (n.tag == "script")
        throw std::runtime_error("Native Web profile excludes scripts");
      if (n.tag == "body")
        body = &n;
      if (n.tag == "style") {
        std::string css;
        for (auto *c : n.children)
          css += c->text_content;
        stylesheet(css);
      }
      if (n.tag == "link") {
        auto rel = n.attributes.find("rel"), href = n.attributes.find("href");
        if (rel == n.attributes.end() || rel->second != "stylesheet" ||
            href == n.attributes.end())
          throw std::runtime_error("only local stylesheet links supported");
        auto p = fs::weakly_canonical(source.parent_path() / href->second);
        dependencies.push_back(p);
        auto saved = content;
        auto saved_path = source;
        content = read(p);
        source = p;
        stylesheet(content);
        content = saved;
        source = saved_path;
      }
      for (auto *c : n.children)
        self(self, *c);
    };
    walk(walk, root);
    if (!body)
      throw std::runtime_error("document has no body");
    for (const auto &[key, value] : body->parent->attributes)
      out << "d.attribute(d.root()," << quote(key) << "," << quote(value)
          << ");\n";
    node(*body, "d.body()");
    for (size_t i = 0; i < names.size(); ++i)
      prefix << "webscene::native_web::node_id element_" << i << "{};\n";
    prefix << "webscene::native_web::node_id named(std::string_view name) "
              "const {\n";
    for (size_t i = 0; i < names.size(); ++i)
      prefix << "if(name==" << quote(names[i].first) << ") return element_" << i
             << ";\n";
    prefix << "return 0; }\n};\ninline view "
              "build(webscene::native_web::document& d) {\n"
           << out.str() << "return {";
    for (size_t i = 0; i < names.size(); ++i) {
      if (i)
        prefix << ",";
      prefix << names[i].second;
    }
    prefix << "};\n}\n";
    prefix
        << "struct template_view {\n"
           "std::vector<webscene::native_web::node_id> roots;\n"
           "std::vector<std::pair<std::string_view,webscene::native_web::node_"
           "id>> references;\n"
           "webscene::native_web::node_id named(std::string_view name) const "
           "{\n"
           "for(auto [key,value]:references) if(key==name) return value; "
           "return 0; }\n};\n"
           "inline template_view instantiate(webscene::native_web::document& "
           "d, "
           "webscene::native_web::node_id parent, std::string_view name) {\n";
    for (const auto &[name, element] : templates) {
      in_template = true;
      ids.clear();
      names.clear();
      out.str("");
      out.clear();
      std::vector<std::string> roots;
      if (element->template_contents) {
        for (auto *child : element->template_contents->children) {
          if (child->tag == "template")
            throw std::runtime_error(
                "nested compiled templates are not supported");
          if (child->tag == "#text" && !trim(child->text_content).empty())
            throw std::runtime_error(
                "template root text must be wrapped in an element");
          auto before = count;
          node(*child, "parent");
          if (count != before)
            roots.push_back("n" + std::to_string(before + 1));
        }
      }
      prefix << "if(name==" << quote(name) << ") {\n"
             << out.str() << "return {{";
      for (size_t i = 0; i < roots.size(); ++i) {
        if (i)
          prefix << ",";
        prefix << roots[i];
      }
      prefix << "},{";
      for (size_t i = 0; i < names.size(); ++i) {
        if (i)
          prefix << ",";
        prefix << "{" << quote(names[i].first) << "," << names[i].second << "}";
      }
      prefix << "}};\n}\n";
    }
    prefix << "throw std::invalid_argument(\"Unknown compiled "
              "template\");\n}\n}\n";
    fs::create_directories(fs::absolute(output).parent_path());
    std::ofstream f(output);
    if (!f)
      throw std::runtime_error("cannot write output");
    f << prefix.str();
    f.close();
    if (!f)
      throw std::runtime_error("failed writing output");
    const auto escape = [](std::string s) {
      std::string r;
      for (auto c : s) {
        if (c == ' ' || c == '#' || c == '\\')
          r += '\\';
        if (c == '$')
          r += '$';
        r += c;
      }
      return r;
    };
    std::ofstream dep(output.string() + ".d");
    dep << escape(fs::absolute(output).string()) << ":";
    for (auto p : dependencies)
      dep << " " << escape(p.string());
    dep << "\n";
  }
};
int main(int argc, char **argv) {
  if (argc != 3 && argc != 5) {
    std::cerr
        << "usage: webscene-uic input.html output [--module module.name]\n";
    return 2;
  }
  compiler c;
  try {
    std::string module_name;
    if (argc == 5) {
      if (std::string(argv[3]) != "--module")
        throw std::runtime_error("Expected --module option");
      module_name = argv[4];
      bool start = true;
      for (unsigned char ch : module_name) {
        if (ch == '.' && !start) {
          start = true;
          continue;
        }
        if (!(std::isalpha(ch) || ch == '_' || (!start && std::isdigit(ch))))
          throw std::runtime_error("Invalid module name");
        start = false;
      }
      if (start)
        throw std::runtime_error("Invalid module name");
    }
    c.compile(argv[1], argv[2], module_name);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << c.source.string() << ":" << c.location
              << ":1: error: " << e.what() << "\n";
    return 1;
  }
}
