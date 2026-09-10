#include "webscene/native_web.hpp"
#include "webscene_css_parser.h"
#include "webscene_html_parser.h"
#include "webscene_selector_parser.h"
#include <algorithm>
#include <array>
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
static std::string ascii_keyword(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c);
  });
  return value;
}
static bool css_number(const std::string &value) {
  static const std::regex grammar(R"([+-]?([0-9]+(\.[0-9]+)?|\.[0-9]+)([eE][+-]?[0-9]+)?)");
  return std::regex_match(value, grammar);
}
static std::vector<std::string> component_values(const std::string &value) {
  std::vector<std::string> result;
  size_t start = 0;
  int depth = 0;
  for (size_t i = 0; i <= value.size(); ++i) {
    if (i < value.size()) {
      if (value[i] == '(') ++depth;
      if (value[i] == ')' && --depth < 0) throw std::runtime_error("unbalanced CSS function");
    }
    if (i == value.size() || (depth == 0 && std::isspace(static_cast<unsigned char>(value[i])))) {
      auto part = trim(value.substr(start, i-start));
      if (!part.empty()) result.push_back(std::move(part));
      start = i + 1;
    }
  }
  if (depth) throw std::runtime_error("unclosed CSS function");
  return result;
}

static std::string length(std::string value) {
  if (!value.empty() && (std::isdigit(static_cast<unsigned char>(value[0])) ||
      value[0] == '+' || value[0] == '-' || value[0] == '.')) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) {
      return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c);
    });
  }
  static const std::regex valid(
      R"(^([+-]?([0-9]+(\.[0-9]+)?|\.[0-9]+)([eE][+-]?[0-9]+)?(px|%|em|rem|vw|vh|dvw|dvh)?|auto)$)");
  if (!std::regex_match(value, valid) ||
      (value != "auto" && value.back() >= '0' && value.back() <= '9' &&
       std::stof(value) != 0))
    throw std::runtime_error("unsupported length: " + value);
  // Native windows have no collapsing browser chrome: the document viewport
  // is the dynamic viewport and is recomputed on every host resize.
  auto normalized = value;
  if (value.ends_with("dvh") || value.ends_with("dvw"))
    normalized.erase(normalized.size() - 3, 1);
  auto v = native_document::parse_length(normalized);
  if (normalized != "auto") {
    size_t consumed = 0;
    auto numeric = std::stof(normalized, &consumed);
    if (!std::isfinite(numeric)) throw std::runtime_error("non-finite length");
    const auto unit = normalized.substr(consumed);
    v = native_document::parse_length(unit.empty() ? "0" : "1" + unit);
    v.value = numeric;
  }
  return "{" + number(v.value) +
         ", static_cast<webscene::native_web::length_unit>(" +
         std::to_string(int(v.unit)) + "), " + number(v.pixel_offset) + "}";
}
static float pixel_length(const std::string &value, bool nonnegative) {
  static const std::regex grammar(R"([+-]?(?:[0-9]*\.[0-9]+|[0-9]+)(?:[eE][+-]?[0-9]+)?(?:px)?)", std::regex::icase);
  if (!std::regex_match(value, grammar)) throw std::runtime_error("expected pixel length");
  size_t consumed = 0;
  const float numeric = std::stof(value, &consumed);
  if (!std::isfinite(numeric) || (nonnegative && numeric < 0) ||
      (consumed == value.size() && numeric != 0))
    throw std::runtime_error("invalid pixel length");
  return numeric;
}
// Initial custom-value token grammar. Unsupported token forms remain errors;
// declarations are never passed as CSS strings to the application.
static std::string variable_code(const std::string &text) {
  std::string result = "{";
  size_t cursor = 0;
  auto append = [&](const std::string &entry) {
    if (result.size() > 1) result += ",";
    result += entry;
  };
  const std::string kind = "webscene::native_web::variable_expression::kind::";
  while (cursor < text.size()) {
    if (std::isspace(static_cast<unsigned char>(text[cursor]))) { ++cursor; continue; }
    if (text.compare(cursor, 4, "var(") == 0) {
      size_t start = cursor + 4, end = start, comma = std::string::npos;
      int depth = 1;
      for (; end < text.size(); ++end) {
        if (text[end] == '(') ++depth;
        if (text[end] == ')' && --depth == 0) break;
        if (text[end] == ',' && depth == 1 && comma == std::string::npos) comma = end;
      }
      if (end == text.size()) throw std::runtime_error("unclosed variable reference");
      auto name = trim(text.substr(start, (comma == std::string::npos ? end : comma) - start));
      if (!std::regex_match(name, std::regex("--[A-Za-z_][A-Za-z0-9_-]*")))
        throw std::runtime_error("unsupported custom property name: " + name);
      auto fallback = comma == std::string::npos ? "{}" : variable_code(text.substr(comma + 1, end - comma - 1));
      append("{" + kind + "reference," + quote(name) + "," + fallback + "," + (comma == std::string::npos ? "false" : "true") + "}");
      cursor = end + 1;
      continue;
    }
    size_t end = cursor;
    while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end]))) ++end;
    auto token = text.substr(cursor, end - cursor);
    if (!std::regex_match(token, std::regex(R"((#[A-Za-z0-9]+|[A-Za-z_-][A-Za-z0-9_-]*|[+-]?([0-9]+(\.[0-9]+)?|\.[0-9]+)([eE][+-]?[0-9]+)?([A-Za-z]+|%)?))")))
      throw std::runtime_error("unsupported custom-value token: " + token);
    if (token == "initial" || token == "inherit" || token == "unset" || token == "revert" || token == "revert-layer")
      throw std::runtime_error("custom-property CSS-wide keywords are not supported yet");
    std::string typed_length = "std::nullopt", typed_color = "std::nullopt";
    const bool zero = std::regex_match(token, std::regex(R"([+-]?([0-9]+(\.[0-9]+)?|\.[0-9]+)([eE][+-]?[0-9]+)?)")) && std::stof(token) == 0;
    if (std::regex_match(token, std::regex(R"([+-]?([0-9]+(\.[0-9]+)?|\.[0-9]+)([eE][+-]?[0-9]+)?(px|%|em|rem|vw|vh|dvw|dvh))", std::regex::icase)) || zero)
      typed_length = "webscene::native_web::length" + length(token);
    if (std::regex_match(token, std::regex("#([0-9a-fA-F]{3}|[0-9a-fA-F]{4}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})")) ||
        token == "transparent" || token == "black" || token == "white")
      typed_color = std::to_string(native_document::parse_color(token)) + "u";
    append("{" + kind + "token," + quote(token) + ",{},false," + typed_length + "," + typed_color + "}");
    cursor = end;
  }
  return result + "}";
}
// Build-time track lowering: emitted applications receive typed values only.
// Lower additive length expressions into typed native operations. Parsing is
// confined to this compiler; generated code evaluates values, not CSS strings.
static std::string compiled_length_expression(std::string value) {
  value = trim(value);
  for (;;) {
    size_t opening = value.starts_with("calc(") ? 4 : value.starts_with('(') ? 0 : std::string::npos;
    if (opening == std::string::npos) break;
    int nesting = 0;
    size_t closing = std::string::npos;
    for (size_t i = opening; i < value.size(); ++i) {
      if (value[i] == '(') ++nesting;
      else if (value[i] == ')' && --nesting == 0) { closing = i; break; }
    }
    if (closing == std::string::npos) throw std::runtime_error("unclosed calc group");
    if (closing + 1 != value.size()) break;
    value = trim(value.substr(opening + 1, closing - opening - 1));
  }
  int depth = 0;
  for (size_t i = value.size(); i-- > 0;) {
    if (value[i] == ')') ++depth;
    else if (value[i] == '(') --depth;
    else if (depth == 0 && (value[i] == '+' || value[i] == '-') && i > 0 && i + 1 < value.size() &&
             std::isspace(static_cast<unsigned char>(value[i-1])) &&
             std::isspace(static_cast<unsigned char>(value[i+1]))) {
      auto a = compiled_length_expression(value.substr(0,i));
      auto b = compiled_length_expression(value.substr(i+1));
      return "[&]()->std::optional<webscene::native_web::length>{auto a=" + a + ";auto b=" + b +
          ";if(!a||!b)return std::nullopt;return webscene::native_web::add_compiled_lengths(*a,*b," +
          (value[i]=='-' ? "true" : "false") + ");}()";
    }
  }
  depth = 0;
  for (size_t i = value.size(); i-- > 0;) {
    if (value[i] == ')') ++depth;
    else if (value[i] == '(') --depth;
    else if (depth == 0 && (value[i] == '*' || value[i] == '/')) {
      auto left = trim(value.substr(0, i)), right = trim(value.substr(i + 1));
      const std::regex scalar(R"([+-]?([0-9]+(\.[0-9]+)?|\.[0-9]+)([eE][+-]?[0-9]+)?)");
      if (value[i] == '*' && std::regex_match(left, scalar)) std::swap(left, right);
      if (!std::regex_match(right, scalar))
        throw std::runtime_error("compiled length product requires a literal scalar operand");
      auto factor = std::stof(right);
      if (value[i] == '/') {
        if (factor == 0) throw std::runtime_error("compiled length division by zero");
        factor = 1 / factor;
      }
      if (!std::isfinite(factor)) throw std::runtime_error("non-finite calc scalar");
      return "[&]()->std::optional<webscene::native_web::length>{auto v=" + compiled_length_expression(left) +
          ";if(!v)return std::nullopt;return webscene::native_web::scale_compiled_length(*v," + number(factor) + ");}()";
    }
  }
  if (value.starts_with("var("))
    return "[&]()->std::optional<webscene::native_web::length>{auto v=s.evaluate(" + variable_code(value) +
        ");if(v&&v->size()==1)return (*v)[0].length;return std::nullopt;}()";
  if (value == "auto") throw std::runtime_error("auto is not a calc length");
  return "std::optional<webscene::native_web::length>{webscene::native_web::length" + length(value) + "}";
}

static std::string grid_track_code(std::string value) {
  value = trim(value);
  const std::string prefix = "webscene::native_web::grid_track::sizing::";
  if (value == "auto" || value == "min-content")
    return "{{}, {}, 0.0f, " + prefix +
           (value == "auto" ? "automatic" : "min_content") + "}";
  if (std::regex_match(value, std::regex(R"(\+?([0-9]+(\.[0-9]+)?|\.[0-9]+)([eE][+-]?[0-9]+)?fr)")))
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
        std::regex_match(maximum, std::regex(R"(\+?([0-9]+(\.[0-9]+)?|\.[0-9]+)([eE][+-]?[0-9]+)?fr)"));
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
static std::string variable_grid_code(const std::string &member, const std::string &value) {
  std::string code = "std::vector<webscene::native_web::grid_track> tracks;bool valid=true;";
  std::string token;
  int depth = 0;
  auto flush = [&] {
    if (token.empty()) return;
    if (token.starts_with("var(")) {
      code += "{auto values=s.evaluate(" + variable_code(token) + ");";
      code += "if(!values || values->empty()) valid=false;else for(const auto& value:*values){";
      code += "if(value.length && value.length->value>=0) tracks.push_back({*value.length,*value.length,0.0f,webscene::native_web::grid_track::sizing::fixed});";
      code += "else if(value.is_keyword(\"auto\")) tracks.push_back({});else valid=false;}}";
    } else {
      auto static_tracks = grid_tracks_code(token);
      code += "{std::vector<webscene::native_web::grid_track> fixed=" + static_tracks + ";tracks.insert(tracks.end(),fixed.begin(),fixed.end());}";
    }
    token.clear();
  };
  for (char c : value) {
    if (c == '(') ++depth;
    if (c == ')' && --depth < 0) throw std::runtime_error("unbalanced variable grid");
    if (std::isspace(static_cast<unsigned char>(c)) && !depth) flush();
    else token += c;
  }
  if (depth) throw std::runtime_error("unbalanced variable grid");
  flush();
  return code + "if(!valid) tracks.clear();s.set_" + member + "(std::move(tracks));";
}
static std::string assignments(const std::string &name,
                               std::string value) {
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
  if (lengths.contains(name) && value.size() == 4) {
    auto keyword = value;
    std::ranges::transform(keyword, keyword.begin(), [](unsigned char c) {
      return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c);
    });
    if (keyword == "auto") value = keyword;
  }
  auto member = name;
  std::replace(member.begin(), member.end(), '-', '_');
  if (value.starts_with("calc(") && (name == "left" || name == "right" || name == "top" || name == "bottom")) {
    return "{auto result=" + compiled_length_expression(value) + ";s.set_" + name +
        "(result.value_or(webscene::native_web::length" + length("auto") + "));}";
  }
  if (name == "inset" && value.find("var(") != std::string::npos) {
    std::string code = "webscene::native_web::variable_result v=webscene::native_web::variable_tokens{};";
    for (const auto &component : component_values(value)) {
      if (component.starts_with("calc(")) {
        code += "{auto result=" + compiled_length_expression(component) +
            ";if(!result)v.reset();else if(v)v->emplace_back(\"\",result,std::nullopt);}";
      } else {
        code += "{auto part=s.evaluate(" + variable_code(component) +
            ");if(!part)v.reset();else if(v)v->insert(v->end(),part->begin(),part->end());}";
      }
    }
    return code +
        "bool valid=v && !v->empty() && v->size()<=4;"
        "if(valid) for(const auto& t:*v) valid=valid && (t.length.has_value() || t.is_keyword(\"auto\"));"
        "auto side=[&](size_t i){return valid ? ((*v)[i].length.value_or(webscene::native_web::length" + length("auto") + ")) : webscene::native_web::length" + length("auto") + ";};"
        "s.set_top(side(0));s.set_right(side(valid && v->size()>1?1:0));"
        "s.set_bottom(side(valid && v->size()>2?2:0));"
        "s.set_left(side(valid && v->size()>3?3:valid && v->size()>1?1:0));";
  }
  if ((name == "margin" || (name.starts_with("margin-") && lengths.contains(name))) && value.find("var(") != std::string::npos) {
    std::string code = "auto v=s.evaluate(" + variable_code(value) + ");bool valid=v && !v->empty() && v->size()<=" + (name == "margin" ? "4;" : "1;") +
        "if(valid) for(const auto& t:*v) valid=valid && (t.length || t.is_keyword(\"auto\"));";
    const std::vector<std::string> sides{"top", "right", "bottom", "left"};
    const std::vector<std::string> indices{"0", "valid && v->size()>1?1:0", "valid && v->size()>2?2:0", "valid && v->size()>3?3:valid && v->size()>1?1:0"};
    for (size_t i=0; i<sides.size(); ++i) {
      if (name != "margin" && name != "margin-" + sides[i]) continue;
      auto index = name == "margin" ? indices[i] : "0";
      code += "{auto i=" + index + ";s.set_margin_" + sides[i] + "(valid ? (*v)[i].length.value_or(webscene::native_web::length" + length("0") + ") : webscene::native_web::length" + length("0") + ");s.set_margin_" + sides[i] + "_auto(valid && (*v)[i].is_keyword(\"auto\"));}";
    }
    return code;
  }
  if (((name.starts_with("padding-") && lengths.contains(name)) || name == "row-gap" || name == "column-gap") && value.find("var(") != std::string::npos) {
    return "auto v=s.evaluate(" + variable_code(value) + ");s.set_" + member +
        "(v && v->size()==1 && (*v)[0].length && (*v)[0].length->value>=0 ? *(*v)[0].length : webscene::native_web::length" + length("0") + ");";
  }
  if (name == "gap" && value.find("var(") != std::string::npos) {
    return "auto v=s.evaluate(" + variable_code(value) + ");"
        "bool valid=v && !v->empty() && v->size()<=2;"
        "if(valid) for(const auto& t:*v) valid=valid && t.length && t.length->value>=0;"
        "auto side=[&](size_t i){return valid ? *(*v)[i].length : webscene::native_web::length" + length("0") + ";};"
        "s.set_row_gap(side(0));s.set_column_gap(side(valid && v->size()>1?1:0));";
  }
  if (name == "padding" && value.find("var(") != std::string::npos) {
    return "auto v=s.evaluate(" + variable_code(value) + ");"
        "bool valid=v && !v->empty() && v->size()<=4;"
        "if(valid) for(const auto& t:*v) valid=valid && t.length && t.length->value>=0;"
        "auto side=[&](size_t i){return valid ? *(*v)[i].length : webscene::native_web::length" + length("0") + ";};"
        "s.set_padding_top(side(0));s.set_padding_right(side(valid && v->size()>1?1:0));"
        "s.set_padding_bottom(side(valid && v->size()>2?2:0));"
        "s.set_padding_left(side(valid && v->size()>3?3:valid && v->size()>1?1:0));";
  }
  if (name == "-webkit-font-smoothing") {
    if (value == "inherit" || value == "unset") return "s.set_font_smoothing(\"\");";
    if (value != "auto" && value != "none" && value != "antialiased" && value != "subpixel-antialiased")
      throw std::runtime_error("unsupported font smoothing mode");
    return "s.set_font_smoothing(" + quote(value) + ");";
  }
  if (name == "transform") {
    if (std::regex_match(value, std::regex("none", std::regex::icase))) return "s.set_translation(" + length("0") + "," + length("0") + ",false);";
    std::smatch match;
    if (std::regex_match(value, match, std::regex(R"(translate\(\s*([^(),]+?)(?:\s*,\s*([^(),]+?))?\s*\))", std::regex::icase))) {
      auto x = trim(match[1]), y = match[2].matched ? trim(match[2]) : "0";
      if (x == "auto" || y == "auto") throw std::runtime_error("transform translation requires lengths");
      return "s.set_translation(" + length(x) + "," + length(y) + ");";
    }
    if (!std::regex_match(value, match, std::regex(R"(translate([XY])\(\s*([^()]+?)\s*\))", std::regex::icase)))
      throw std::runtime_error("compiled transform currently supports translate/translateX/translateY or none");
    auto argument = trim(match[2]);
    if (argument == "auto") throw std::runtime_error("transform translation requires a length");
    auto translated = length(argument), zero = length("0");
    bool horizontal = match[1] == "x" || match[1] == "X";
    return "s.set_translation(" + (horizontal ? translated : zero) + "," + (horizontal ? zero : translated) + ");";
  }
  if (name == "text-anchor") {
    if (value != "start" && value != "middle" && value != "end")
      throw std::runtime_error("text-anchor requires start, middle or end");
    return "s.set_svg_text_anchor(" + quote(value) + ");";
  }
  if (name == "stroke-width") {
    if (!std::regex_match(value, std::regex(R"(\+?([0-9]+(\.[0-9]+)?|\.[0-9]+)([eE][+-]?[0-9]+)?(px|%)?)")))
      throw std::runtime_error("stroke-width requires a nonnegative number, px or percentage");
    if (!std::isfinite(std::stof(value))) throw std::runtime_error("non-finite stroke-width");
    return "s.set_svg_stroke_width(" + quote(value) + ");";
  }
  if (name == "fill" || name == "stroke") {
    auto setter = name == "fill" ? "set_svg_fill" : "set_svg_stroke";
    if (value.find("var(") != std::string::npos)
      return "auto v=s.evaluate(" + variable_code(value) + ");s." + setter +
          "(v && v->size()==1 && ((*v)[0].color || (*v)[0].text==\"none\" || (*v)[0].text==\"currentColor\") ? (*v)[0].text : \"\");";
    if (value == "inherit" || value == "unset") return std::string("s.") + setter + "(\"\");";
    if (value != "none" && value != "currentColor" && value != "currentcolor") assignments("color",value);
    return std::string("s.") + setter + "(" + quote(value) + ");";
  }
  if (value.starts_with("color-mix(")) {
    std::smatch mix;
    if (!std::regex_match(value,mix,std::regex(R"(color-mix\(\s*in\s+srgb\s*,\s*(var\(--[A-Za-z_][A-Za-z0-9_-]*\)|#[A-Fa-f0-9]+)\s+([0-9]+(?:\.[0-9]+)?)%\s*,\s*transparent\s*\))")))
      throw std::runtime_error("compiled color-mix currently supports an sRGB color percentage mixed with transparent");
    const float fraction = std::stof(mix[2]) / 100.f;
    if (fraction > 1) throw std::runtime_error("color-mix percentage exceeds 100%");
    std::string code = "auto mixed=s.color_with_opacity(" + variable_code(mix[1]) + "," + number(fraction) + ");";
    if (name == "color" || name == "background" || name == "background-color")
      return code + "s.set_" + (name == "color" ? "foreground_rgba" : "background_rgba") + "(mixed.value_or(0u));";
    if (name == "border-color" || name == "border-left-color" || name == "border-top-color" || name == "border-right-color" || name == "border-bottom-color") {
      for (const std::string side : {"left","top","right","bottom"})
        if (name == "border-color" || name == "border-" + side + "-color")
          code += "s.set_border_" + side + "_color(mixed.value_or(0u),!mixed);";
      return code;
    }
    throw std::runtime_error("compiled color-mix unsupported for " + name);
  }
  if (name == "z-index") {
    if (value == "auto") return "s.set_z_index(0,true);";
    if (!std::regex_match(value, std::regex(R"([+-]?[0-9]+)"))) throw std::runtime_error("z-index requires an integer or auto");
    auto integer = std::stoll(value);
    if (integer < INT32_MIN || integer > INT32_MAX) throw std::runtime_error("z-index outside native integer range");
    return "s.set_z_index(" + std::to_string(integer) + ");";
  }
  if (name == "pointer-events" || name == "visibility") {
    auto setter = name == "pointer-events" ? "set_pointer_events" : "set_visibility";
    if (value == "inherit" || value == "unset") return std::string("s.") + setter + "(false,false);";
    bool off = name == "pointer-events" ? value == "none" : value == "hidden";
    bool on = name == "pointer-events" ? value == "auto" : value == "visible";
    if (!off && !on) throw std::runtime_error("unsupported " + name + ": " + value);
    return std::string("s.") + setter + "(" + (off ? "true" : "false") + ");";
  }
  if (name == "font") {
    if (value == "inherit" || value == "unset")
      return "s.set_font_size(-1.0f);s.set_font_weight(0);s.set_font_family(\"\");s.set_line_height(-1.0f);";
    std::smatch match;
    if (!std::regex_match(value, match,
        std::regex(R"(([0-9]+(?:\.[0-9]+)?px)(?:\s*/\s*([0-9]+(?:\.[0-9]+)?(?:px)?|normal))?\s+(.+))")))
      throw std::runtime_error("font shorthand currently requires px-size[/line-height] family or inherit");
    auto family = trim(match[3]);
    if (family.empty()) throw std::runtime_error("font shorthand requires a family");
    return assignments("font-size", match[1]) +
        assignments("line-height", match[2].matched ? match[2].str() : "normal") +
        assignments("font-family", family) + "s.set_font_weight(400);";
  }
  if (name == "flex-wrap") {
    if (value != "wrap" && value != "nowrap") throw std::runtime_error("compiled flex-wrap supports wrap and nowrap");
    return "s.set_flex_wrap(" + std::string(value == "wrap" ? "true" : "false") + ");";
  }
  if (name == "flex") {
    std::string grow = "1", shrink = "1", basis = "0%";
    if (value == "none") { grow = "0"; shrink = "0"; basis = "auto"; }
    else if (value == "auto") basis = "auto";
    else if (value == "initial") { grow = "0"; basis = "auto"; }
    else {
      std::istringstream input(value);
      std::vector<std::string> parts;
      std::string part;
      while (input >> part) parts.push_back(part);
      if (parts.empty() || parts.size() > 3) throw std::runtime_error("invalid flex shorthand");
      if (parts.size() == 1 && !css_number(parts[0])) basis = parts[0];
      else {
        if (!css_number(parts[0])) throw std::runtime_error("flex grow requires a nonnegative number");
        grow = parts[0];
        if (parts.size() >= 2) {
          if (css_number(parts[1])) shrink = parts[1];
          else if (parts.size() == 2) basis = parts[1];
          else throw std::runtime_error("flex shrink requires a nonnegative number");
        }
        if (parts.size() == 3) basis = parts[2];
      }
    }
    return assignments("flex-grow", grow) + assignments("flex-shrink", shrink) + assignments("flex-basis", basis);
  }
  if (name == "overflow") {
    std::istringstream tokens(value);
    std::string x, y, extra;
    tokens >> x;
    if (!(tokens >> y)) y = x;
    if (tokens >> extra) throw std::runtime_error("overflow requires one or two values");
    return assignments("overflow-x", x) + assignments("overflow-y", y);
  }
  if (name == "overflow-x" || name == "overflow-y") {
    static const std::map<std::string,std::string> modes{{"visible","visible"},{"hidden","hidden"},
      {"clip","clip"},{"auto","automatic"},{"scroll","scroll"}};
    auto mode = modes.find(ascii_keyword(value));
    if (mode == modes.end()) throw std::runtime_error("unsupported overflow: " + value);
    return "s.set_" + member + "(webscene::native_web::overflow_mode::" + mode->second + ");";
  }
  if (name == "text-align" || name == "white-space" || name == "text-transform") {
    static const std::map<std::string,std::set<std::string>> keywords{
      {"text-align", {"left","right","center","start","end"}},
      {"white-space", {"normal","nowrap","pre","pre-wrap","pre-line","break-spaces"}},
      {"text-transform", {"none","uppercase","lowercase","capitalize"}}};
    if (value.find("var(") != std::string::npos) {
      std::string code = "auto v=s.evaluate(" + variable_code(value) + ");std::string keyword;";
      for (const auto &keyword : keywords.at(name))
        code += "if(v && v->size()==1 && (*v)[0].is_keyword(" + quote(keyword) + ")) keyword=" + quote(keyword) + ";";
      return code + "s.set_" + member + "(keyword);";
    }
    value = ascii_keyword(value);
    if (value == "inherit" || value == "unset") return "s.set_" + member + "(\"\");";
    if (!keywords.at(name).contains(value)) throw std::runtime_error("unsupported " + name + ": " + value);
    return "s.set_" + member + "(" + quote(value) + ");";
  }
  if (name == "box-shadow") {
    if (value == "none") return "s.set_box_shadow(std::nullopt);";
    if (value.find("inset") != std::string::npos)
      throw std::runtime_error("native compiled inset shadows are not supported yet");
    if (value.find("var(") == std::string::npos &&
        !std::regex_match(value, std::regex(R"((?:-?[0-9]+(?:\.[0-9]+)?px|0)\s+(?:-?[0-9]+(?:\.[0-9]+)?px|0)(?:\s+(?:[0-9]+(?:\.[0-9]+)?px|0))?(?:\s+(?:-?[0-9]+(?:\.[0-9]+)?px|0))?\s+(?:#(?:[A-Fa-f0-9]{3}|[A-Fa-f0-9]{4}|[A-Fa-f0-9]{6}|[A-Fa-f0-9]{8})|transparent|black|white))")))
      throw std::runtime_error("box-shadow requires x y [blur [spread]] color");
    return "s.set_box_shadow(s.evaluate(" + variable_code(value) + "));";
  }
  if (name == "border" || name == "border-left" || name == "border-top" || name == "border-right" || name == "border-bottom") {
    std::string width = "3px", style = "none", color = "currentColor";
    if (value == "0") width = "0";
    else if (value != "none") {
      std::smatch match;
      if (!std::regex_match(value, match, std::regex(R"(([^\s]+)\s+(solid)\s+(.+))")))
        throw std::runtime_error("border shorthand currently requires width solid color, 0 or none");
      width = match[1]; style = match[2]; color = match[3];
    }
    std::string code;
    for (const std::string side : {"left", "top", "right", "bottom"}) {
      if (name != "border" && name != "border-" + side) continue;
      auto base = "border-" + side;
      // Scope each color expression to keep temporary names independent.
      code += "{" + assignments(base + "-width", width) + assignments(base + "-style", style) + assignments(base + "-color", color) + "}";
    }
    return code;
  }
  for (const std::string side : {"left", "top", "right", "bottom"}) {
    if (name == "border-" + side + "-width") {
      pixel_length(value, true);
      return "s.set_border_" + side + "_width(" + length(value) + ");";
    }
    if (name == "border-" + side + "-style") {
      if (value != "solid" && value != "none" && value != "hidden")
        throw std::runtime_error("native compiled borders currently support solid, none and hidden");
      return "s.set_border_" + side + "_solid(" + (value == "solid" ? "true" : "false") + ");";
    }
  }
  if (name == "border-color" || name == "border-left-color" || name == "border-top-color" ||
      name == "border-right-color" || name == "border-bottom-color") {
    std::string code;
    bool variable = value.find("var(") != std::string::npos;
    std::string color, current;
    if (variable) {
      code = "auto v=s.evaluate(" + variable_code(value) + ");";
      color = "(v && v->size()==1 && (*v)[0].color ? *(*v)[0].color : 0u)";
      current = "!(v && v->size()==1 && (*v)[0].color)";
    } else if (value == "currentColor" || value == "currentcolor" || value == "initial" || value == "unset") {
      color = "0u"; current = "true";
    } else {
      if (value == "inherit") throw std::runtime_error("inherited border color is not supported yet");
      // Reuse strict color validation, but retain currentColor as a dependency.
      assignments("color", value);
      color = std::to_string(native_document::parse_color(value)) + "u";
      current = "false";
    }
    for (const std::string side : {"left", "top", "right", "bottom"})
      if (name == "border-color" || name == "border-" + side + "-color")
        code += "s.set_border_" + side + "_color(" + color + "," + current + ");";
    return code;
  }
  if (value.find("var(") != std::string::npos) {
    if (name == "grid-template-columns" || name == "grid-template-rows")
      return variable_grid_code(member, value);
    const bool color = name == "color" || name == "background" || name == "background-color";
    const bool dimension = name == "width" || name == "height" || name == "left" ||
                           name == "right" || name == "top" || name == "bottom";
    if (!color && !dimension)
      throw std::runtime_error("compiled var() not supported for property: " + name);
    std::string code = "auto v=s.evaluate(" + variable_code(value) + ");";
    if (color) {
      auto setter = name == "color" ? "foreground_rgba" : "background_rgba";
      return code + "s.set_" + setter + "(v && v->size()==1 && (*v)[0].color ? *(*v)[0].color : 0u);";
    }
    auto nonnegative = name == "width" || name == "height" ? " && (*v)[0].length->value>=0" : "";
    return code + "s.set_" + member + "(v && v->size()==1 && (*v)[0].length" + nonnegative +
        " ? *(*v)[0].length : webscene::native_web::length{});";
  }

  if (name == "grid-template-columns" || name == "grid-template-rows")
    return "s.set_" + member + "(" + grid_tracks_code(value) + ");";
  if (lengths.contains(name)) {
    const auto compiled = length(value);
    const bool signed_length = name.starts_with("margin-") || name == "left" ||
        name == "right" || name == "top" || name == "bottom";
    if (!signed_length && value != "auto" && std::stof(value) < 0)
      throw std::runtime_error("negative length is invalid for " + name);
    if (value == "auto" && (name.starts_with("padding-") || name.ends_with("-gap") ||
                           name.ends_with("-radius")))
      throw std::runtime_error("auto is invalid for " + name);
    auto result = "s." + member + " = " + compiled + ";";
    if (name.starts_with("margin-"))
      result +=
          "s." + member + "_auto = " + (value == "auto" ? "true;" : "false;");
    return result;
  }
  if (name == "padding" || name == "margin" || name == "border-radius" || name == "inset" || name == "border-width" || name == "border-style") {
    auto values = component_values(value);
    if (values.empty() || values.size() > 4)
      throw std::runtime_error("invalid box shorthand");
    std::string a = values[0], b = values.size() > 1 ? values[1] : a,
                c = values.size() > 2 ? values[2] : a,
                d = values.size() > 3 ? values[3] : b;
    std::vector<std::string> names =
        (name == "border-width" || name == "border-style")
            ? std::vector<std::string>{"border-top-" + name.substr(7), "border-right-" + name.substr(7),
                                       "border-bottom-" + name.substr(7), "border-left-" + name.substr(7)}
            : name == "inset"
            ? std::vector<std::string>{"top", "right", "bottom", "left"}
            : name == "border-radius"
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
  if (name == "color" && (value == "inherit" || value == "unset"))
    return "s.set_foreground_rgba(0u);";
  if (name == "background" && value == "none") return "s.reset_background();";
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
    auto keyword = value;
    std::ranges::transform(keyword, keyword.begin(), [](unsigned char c) {
      return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c);
    });
    auto val = it->second.second.find(keyword);
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
  if (name == "line-height") {
    if (value == "inherit" || value == "unset") return "s.set_line_height(-1.0f);";
    if (value == "normal") return "s.set_line_height(-2.0f);";
    if (std::regex_match(value, std::regex(R"(\+?([0-9]+(\.[0-9]+)?|\.[0-9]+)([eE][+-]?[0-9]+)?)")))
      return "s.set_line_height(" + number(-3 - std::stof(value)) + ");";
    return "s.set_line_height(" + number(pixel_length(value, true)) + ");";
  }
  if (name == "letter-spacing" || name == "word-spacing") {
    if (value == "normal") return "s.set_" + member + "(0.0f);";
    return "s.set_" + member + "(" + number(pixel_length(value, false)) + ");";
  }
  if (name == "font-family") {
    if (value.empty())
      throw std::runtime_error("Empty font family");
    return "s.set_font_family(" +
           quote(value == "inherit" || value == "unset" ? "" : value) + ");";
  }
  if (name == "font-size") {
    if (value == "inherit" || value == "unset") return "s.set_font_size(-1.0f);";
    return "s.font_size = " + number(pixel_length(value, true)) + ";";
  }
  if (name == "font-weight") {
    std::ranges::transform(value, value.begin(), [](unsigned char c) {
      return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c);
    });
    if (value == "normal") return "s.set_font_weight(400);";
    if (value == "bold") return "s.set_font_weight(700);";
  }
  if (name == "font-weight" && (value == "inherit" || value == "unset"))
    return "s.set_font_weight(0);";
  if (name == "font-weight" || name == "opacity" || name == "flex-grow" ||
      name == "flex-shrink") {
    const bool percentage = name == "opacity" && value.ends_with('%');
    const auto numeric = percentage ? value.substr(0, value.size() - 1) : value;
    if (!css_number(numeric))
      throw std::runtime_error("invalid numeric value");
    auto v = std::stof(numeric);
    if (!std::isfinite(v) || ((name == "flex-grow" || name == "flex-shrink") && v < 0) ||
        (name == "font-weight" && (v < 1 || v > 1000 || v != int(v))))
      throw std::runtime_error("numeric value out of range");
    if (name == "opacity") v = std::clamp(percentage ? v / 100.0f : v, 0.0f, 1.0f);
    return "s." + member + " = " + number(v) + ";";
  }
  throw std::runtime_error("unsupported Native Web CSS property: " + name);
}
static std::string selector_code(const selector_syntax_selector &sel) {
  std::string result = "{{";
  for (size_t i = 0; i < sel.compounds.size(); ++i) {
    std::string tag, id;
    std::vector<std::string> classes;
    std::vector<std::string> attributes, excluded;
    bool focus = false, hover = false, root = false, active = false, disabled = false, focus_visible = false, first_child = false, last_child = false, only_child = false;
    auto input = sel.compounds[i];
    size_t p = 0;
    while (p < input.size()) {
      if (input.compare(p, 5, ":not(") == 0) {
        auto start = p + 5, end = start;
        int depth = 1;
        char quoted = 0;
        for (; end < input.size(); ++end) {
          auto c = input[end];
          if (quoted) {
            if (c == '\\') ++end;
            else if (c == quoted) quoted = 0;
            continue;
          }
          if (c == '\'' || c == '"') { quoted = c; continue; }
          if (c == '(') ++depth;
          if (c == ')' && --depth == 0) break;
        }
        if (end == input.size()) throw std::runtime_error("unclosed :not selector");
        auto inner = parse_selector_syntax(input.substr(start, end - start));
        if (!inner || inner.selectors.empty()) throw std::runtime_error("invalid :not selector");
        for (const auto &entry : inner.selectors) {
          if (entry.compounds.size() != 1) throw std::runtime_error(":not currently requires compound selectors");
          excluded.push_back("(webscene::native_web::selector" + selector_code(entry) + ").parts.front()");
        }
        p = end + 1;
        continue;
      }
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
        else if (name == "first-child") first_child = true;
        else if (name == "last-child") last_child = true;
        else if (name == "only-child") only_child = true;
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
    result += "}," + std::string(active ? "true" : "false") + "," + (disabled ? "true" : "false") + "," + (focus_visible ? "true" : "false") + "," + (first_child ? "true" : "false") + "," + (last_child ? "true" : "false") + "," + (only_child ? "true" : "false") + ",{";
    for (size_t index = 0; index < excluded.size(); ++index) {
      if (index) result += ",";
      result += excluded[index];
    }
    result += "}}";
  }
  return result + "}," + std::to_string(sel.specificity) + "}";
}
struct compiler {
  bool preview{};
  std::string namespace_name{"compiled_ui"};
  void warning(const std::string &message) { std::cerr << source.string() << ": warning: preview: " << message << '\n'; }

  std::ostringstream out;
  fs::path source;
  std::string content;
  size_t location{1};
  size_t column{1};
  bool stylesheet_locations{false};
  std::set<std::string> ids;
  std::vector<std::pair<std::string, std::string>> names;
  std::vector<fs::path> dependencies;
  unsigned count{};
  bool in_template{};
  std::vector<std::pair<std::string, const dom_node *>> templates;
  void locate(std::string_view text) {
    column = 1;
    auto at = content.find(text);
    location =
        at == std::string::npos
            ? 1
            : 1 + std::count(content.begin(), content.begin() + at, '\n');
  }
  void declarations(const css_syntax_output &css, const css_syntax_rule &r) {
    out << "{";
    bool emitted = false;
    for (size_t j = 0; j < r.declaration_count; ++j) {
      const auto &d = css.declarations.at(r.first_declaration + j);
      if (stylesheet_locations) { location = d.source_line; column = d.source_column; }
      else locate(d.name);
      try {
        std::string code;
        if (d.name.starts_with("--"))
          code = "nullptr," + quote(d.name) + "," + variable_code(trim(d.value));
        else code = "+[](webscene::native_web::style& s){" +
          std::regex_replace(assignments(d.name, trim(d.value)), std::regex(R"(s\.([a-z_]+) = ([^;]+);)"), "s.set_$1($2);") + "}";
        if (emitted) out << ",";
        out << "{" << (d.important ? "true" : "false") << "," << code << "}";
        emitted = true;
      } catch (const std::exception &e) {
        if (!preview) throw;
        warning(d.name + ": " + d.value + ": " + e.what());
      }
    }
    out << "}";
  }
  void stylesheet(const std::string &text, bool exact_locations = false) {
    const auto saved_locations = stylesheet_locations;
    stylesheet_locations = exact_locations;
    auto css = parse_css_syntax_stylesheet(text);
    if (!css) throw std::runtime_error("invalid CSS stylesheet: " + css.error);
    if (css.metrics.parse_error_count) {
      if (exact_locations) { location = css.metrics.first_error_line; column = css.metrics.first_error_column; }
      throw std::runtime_error("invalid CSS syntax (" + std::to_string(css.metrics.parse_error_count) + " parse errors)");
    }
    std::vector<std::array<float, 4>> bounds(css.rules.size(), {0, 1e9f, 0, 1e9f});
    for (size_t i = 0; i < css.rules.size(); ++i) {
      const auto &r = css.rules[i];
      if (exact_locations) { location = r.source_line; column = r.source_column; }
      auto &range = bounds[i];
      if (r.parent_index != css_syntax_no_parent)
        range = bounds.at(r.parent_index);
      if (r.kind == css_syntax_at_rule) {
        std::smatch match;
        if (r.name != "media" ||
            !std::regex_match(
                r.prelude, match,
                std::regex(R"(\s*\((min|max)-(width|height)\s*:\s*([0-9]+)px\)\s*)")))
          { if (!preview) throw std::runtime_error("only min/max width or height media conditions in px are supported");
            warning("skipped @" + r.name + " " + r.prelude); range[0] = 1e9f; range[1] = -1; continue; }
        auto v = std::stof(match[3]);
        const size_t axis = match[2] == "width" ? 0 : 2;
        if (match[1] == "min")
          range[axis] = std::max(range[axis], v);
        else
          range[axis + 1] = std::min(range[axis + 1], v);
        continue;
      }
      if (range[0] > range[1]) continue;
      auto selectors = parse_selector_syntax(r.prelude);
      if (!selectors)
        throw std::runtime_error(selectors.error);
      for (const auto &sel : selectors.selectors) {
        std::string selector;
        try { selector = selector_code(sel); }
        catch (const std::exception &e) { if (!preview) throw; warning(e.what()); continue; }
        out << "d.add_rule({" << selector << ",";
        declarations(css, r);
        out << "," << number(range[0]) << "," << number(range[1])
            << ",0," << number(range[2]) << "," << number(range[3]) << "});\n";
      }
    }
    stylesheet_locations = saved_locations;
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
      if (n.text_content.empty())
        return;
      out << "d.text(" << parent << "," << quote(n.text_content) << ");\n";
      return;
    }
    static const std::set<std::string> tags = {
        "body", "main",   "section", "div", "span", "br", "p",      "h1",     "h2",
        "svg", "g", "text", "tspan", "path", "polygon", "rect", "circle", "ellipse", "line", "polyline", "h3",   "button", "canvas",  "ul",  "li",   "header", "footer", "nav"};
    if (preview && (n.tag == "script" || n.tag == "noscript")) { warning("skipped " + n.tag); return; }
    if (!tags.contains(n.tag)) {
      if (!preview) throw std::runtime_error("unsupported Native Web element: " + n.tag);
      warning("generic native element: " + n.tag);
    }
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
      if (k.starts_with("on")) {
        if (!preview) throw std::runtime_error("JavaScript attributes are not supported in Native Web");
        warning("skipped JavaScript attribute " + k); continue;
      }
      if (k == "hidden") {
        auto state = v;
        std::ranges::transform(state, state.begin(), [](unsigned char c) { return std::tolower(c); });
        if (state == "until-found") throw std::runtime_error("hidden until-found requires native find/reveal support");
      }
      if (k != "hidden" && k != "id" && k != "class" && k != "width" && k != "height" &&
          k != "tabindex" && k != "disabled" && k != "type" && k != "role" &&
          !k.starts_with("aria-") && !k.starts_with("data-") &&
          !(std::set<std::string>{"viewBox","viewbox","xmlns","d","points","fill","stroke","stroke-width","stroke-linecap","stroke-linejoin","x","y","x1","x2","y1","y2","cx","cy","r","rx","ry"}.contains(k)))
        { if (!preview) throw std::runtime_error("unsupported attribute: " + k); warning("generic native attribute: " + k); }
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
    out << "namespace " << namespace_name << " {\nstruct view {\n";
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
      if (n.tag == "script") {
        if (!preview) throw std::runtime_error("Native Web profile excludes scripts");
        warning("skipped script"); return;
      }
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
        stylesheet(content, true);
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
          if (child->tag == "#text") {
            if (!child->text_content.empty()) {
              auto local = "n" + std::to_string(++count);
              out << "auto " << local << " = d.text(parent," << quote(child->text_content) << ");\n";
              roots.push_back(local);
            }
            continue;
          }
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
// Read-only compatibility audit using exactly the compiler's lowering paths.
// Success checks CSS support only; it does not certify HTML or runtime parity.
static int check_css(const fs::path &path) {
  auto css = parse_css_syntax_stylesheet(read(path));
  if (!css) throw std::runtime_error("invalid CSS stylesheet: " + css.error);
  if (css.metrics.parse_error_count) {
    std::cerr << path.string() << ':' << css.metrics.first_error_line << ':'
              << css.metrics.first_error_column << ": error: invalid CSS syntax ("
              << css.metrics.parse_error_count << " parse errors)\n";
    return 1;
  }
  std::map<std::string, std::set<std::string>> errors;
  auto check = [&](const std::string &context, auto action, const std::string &owner = "") {
    try { action(); }
    catch (const std::exception &e) {
      auto &owners = errors[context + ": " + e.what()];
      if (!owner.empty()) owners.insert(owner);
    }
  };
  for (const auto &rule : css.rules) {
    auto location = path.string() + ":" + std::to_string(rule.source_line) + ":" + std::to_string(rule.source_column);
    if (rule.kind == css_syntax_at_rule) {
      check("@" + rule.name + " " + rule.prelude, [&] {
        if (rule.name != "media" || !std::regex_match(rule.prelude,
            std::regex(R"(\s*\((min|max)-(width|height)\s*:\s*([0-9]+)px\)\s*)")))
          throw std::runtime_error("unsupported at-rule or condition");
      }, location);
    } else {
      check(rule.prelude, [&] {
        auto selectors = parse_selector_syntax(rule.prelude);
        if (!selectors) throw std::runtime_error(selectors.error);
        for (const auto &selector : selectors.selectors) selector_code(selector);
      }, location);
    }
  }
  for (size_t index = 0; index < css.declarations.size(); ++index) {
    const auto &declaration = css.declarations[index];
    std::string owner;
    for (const auto &rule : css.rules)
      if (index >= rule.first_declaration && index < rule.first_declaration + rule.declaration_count) {
        owner = rule.kind == css_syntax_at_rule ? "@" + rule.name + " " + rule.prelude : rule.prelude;
        for (auto parent = rule.parent_index; parent != css_syntax_no_parent;) {
          const auto &ancestor = css.rules.at(parent);
          auto label = ancestor.kind == css_syntax_at_rule
              ? "@" + ancestor.name + " " + ancestor.prelude : ancestor.prelude;
          owner = label + " > " + owner;
          parent = ancestor.parent_index;
        }
        break;
      }
    check(declaration.name + ":" + declaration.value, [&] {
      if (declaration.name.starts_with("--")) variable_code(trim(declaration.value));
      else assignments(declaration.name, trim(declaration.value));
    }, owner + " at " + path.string() + ":" + std::to_string(declaration.source_line) + ":" + std::to_string(declaration.source_column));
  }
  for (const auto &[error, owners] : errors) {
    std::cerr << path.string() << ": error: " << error << '\n';
    for (const auto &owner : owners) std::cerr << "  in rule: " << owner << '\n';
  }
  std::cout << css.rules.size() << " rules, " << css.declarations.size()
            << " declarations, " << errors.size() << " distinct unsupported constructs\n";
  return errors.empty() ? 0 : 1;
}
int main(int argc, char **argv) {
  bool preview = argc > 1 && std::string_view(argv[argc-1]) == "--preview";
  if (preview) --argc;
  if (argc != 3 && argc != 5 && argc != 7) {
    std::cerr
        << "usage: webscene-uic input.html output [--module module.name] [--namespace identifier] [--preview]\n       webscene-uic --check-css input.css\n";
    return 2;
  }
  compiler c;
  c.preview = preview;
  try {
    if (argc == 3 && std::string_view(argv[1]) == "--check-css") {
      c.source = argv[2];
      return check_css(argv[2]);
    }
    std::string module_name;
    for (int option = 3; option < argc; option += 2) {
      if (std::string_view(argv[option]) == "--namespace") {
        c.namespace_name = argv[option + 1];
        if (!std::regex_match(c.namespace_name, std::regex("[A-Za-z][A-Za-z0-9_]*")))
          throw std::runtime_error("Invalid generated namespace");
        continue;
      }
      if (std::string_view(argv[option]) != "--module") throw std::runtime_error("Unknown compiler option");
      module_name = argv[option + 1];
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
              << ":" << c.column << ": error: " << e.what() << "\n";
    return 1;
  }
}
