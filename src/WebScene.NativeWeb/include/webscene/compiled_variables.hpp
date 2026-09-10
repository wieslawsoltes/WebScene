#pragma once
#include <algorithm>
#include <cmath>
#include "webscene_native_dom.h"
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace webscene::native_web {
// Arithmetic on compiled lengths. No CSS text is inspected at runtime.
// The native representation retains one relative term plus an absolute offset.
inline std::optional<webscene_native::css_length> add_compiled_lengths(
    webscene_native::css_length left, webscene_native::css_length right,
    bool subtract = false) {
  using unit = webscene_native::length_unit;
  const auto numeric = [](unit value) {
    return value == unit::pixels || value == unit::percent || value == unit::em ||
           value == unit::rem || value == unit::viewport_width || value == unit::viewport_height;
  };
  if (!numeric(left.unit) || !numeric(right.unit)) return std::nullopt;
  if (subtract) { right.value = -right.value; right.pixel_offset = -right.pixel_offset; }
  if (left.unit == right.unit) {
    left.value += right.value;
    left.pixel_offset += right.pixel_offset;
  } else if (right.unit == unit::pixels) {
    left.pixel_offset += right.value + right.pixel_offset;
  } else if (left.unit == unit::pixels) {
    right.pixel_offset += left.value + left.pixel_offset;
    left = right;
  } else {
    return std::nullopt;
  }
  if (!std::isfinite(left.value) || !std::isfinite(left.pixel_offset)) return std::nullopt;
  return left;
}

inline std::optional<webscene_native::css_length> scale_compiled_length(
    webscene_native::css_length value, float factor) {
  using unit = webscene_native::length_unit;
  if (value.unit != unit::pixels && value.unit != unit::percent && value.unit != unit::em &&
      value.unit != unit::rem && value.unit != unit::viewport_width && value.unit != unit::viewport_height)
    return std::nullopt;
  value.value *= factor;
  value.pixel_offset *= factor;
  if (!std::isfinite(value.value) || !std::isfinite(value.pixel_offset)) return std::nullopt;
  return value;
}

// Tokens are supplied by the compiler. Evaluation substitutes token sequences;
// it never lexes CSS text. Property lowering consumes the resulting typed IR.
struct variable_token {
  std::string text;
  std::optional<webscene_native::css_length> length;
  std::optional<uint32_t> color;
  variable_token(const char *value) : text(value) {}
  variable_token(std::string value) : text(std::move(value)) {}
  variable_token(std::string value, std::optional<webscene_native::css_length> l,
                 std::optional<uint32_t> c) : text(std::move(value)), length(l), color(c) {}
  bool is_keyword(std::string_view keyword) const {
    if (text.size() != keyword.size()) return false;
    const auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
    for (size_t i = 0; i < text.size(); ++i)
      if (lower(text[i]) != lower(keyword[i])) return false;
    return true;
  }
  bool operator==(const variable_token &other) const {
    return text == other.text && color == other.color &&
      length.has_value() == other.length.has_value() &&
      (!length || (length->value == other.length->value && length->unit == other.length->unit &&
                   length->pixel_offset == other.length->pixel_offset));
  }
};
struct variable_expression {
  enum class kind { token, reference };
  kind type{kind::token};
  std::string value;
  std::vector<variable_expression> fallback;
  bool has_fallback{};
  std::optional<webscene_native::css_length> length;
  std::optional<uint32_t> color;
};
using variable_tokens = std::vector<variable_token>;
using variable_result = std::optional<variable_tokens>;
using computed_variables = std::map<std::string, variable_result>;
using specified_variables = std::map<std::string, std::vector<variable_expression>>;

inline variable_result evaluate_variables(const std::vector<variable_expression> &expressions,
                                           const computed_variables &variables) {
  variable_tokens output;
  for (const auto &expression : expressions) {
    if (expression.type == variable_expression::kind::token) {
      output.emplace_back(expression.value, expression.length, expression.color);
      continue;
    }
    auto found = variables.find(expression.value);
    variable_result value = found == variables.end() ? variable_result{} : found->second;
    if (!value && expression.has_fallback)
      value = evaluate_variables(expression.fallback, variables);
    if (!value) return std::nullopt;
    output.insert(output.end(), value->begin(), value->end());
  }
  return output;
}

// Inherited entries are already computed at their defining element. A child
// override must not rebind references inside an inherited value.
inline computed_variables compute_variables(const specified_variables &local,
                                             computed_variables inherited = {}) {
  std::map<std::string, std::set<std::string>> dependencies;
  auto collect = [&](auto &&self, const std::vector<variable_expression> &expressions,
                     std::set<std::string> &result) -> void {
    for (const auto &expression : expressions) {
      if (expression.type == variable_expression::kind::reference)
        result.insert(expression.value);
      // CSS cycles include references in fallbacks, even unused fallbacks.
      self(self, expression.fallback, result);
    }
  };
  for (const auto &[name, expressions] : local)
    collect(collect, expressions, dependencies[name]);
  std::set<std::string> visited, cyclic;
  std::vector<std::string> stack;
  auto visit = [&](auto &&self, const std::string &name) -> void {
    auto cycle = std::find(stack.begin(), stack.end(), name);
    if (cycle != stack.end()) {
      cyclic.insert(cycle, stack.end());
      return;
    }
    if (!local.contains(name) || !visited.insert(name).second) return;
    stack.push_back(name);
    for (const auto &dependency : dependencies[name]) self(self, dependency);
    stack.pop_back();
  };
  for (const auto &[name, unused] : local) visit(visit, name);
  std::set<std::string> resolved;
  auto resolve = [&](auto &&self, const std::string &name) -> variable_result {
    if (!local.contains(name) || resolved.contains(name)) {
      auto found = inherited.find(name);
      return found == inherited.end() ? variable_result{} : found->second;
    }
    if (cyclic.contains(name)) {
      resolved.insert(name);
      return inherited[name] = std::nullopt;
    }
    auto evaluate = [&](auto &&evaluate_self,
                        const std::vector<variable_expression> &expressions) -> variable_result {
      variable_tokens output;
      for (const auto &expression : expressions) {
        if (expression.type == variable_expression::kind::token) {
          output.emplace_back(expression.value, expression.length, expression.color);
          continue;
        }
        auto value = self(self, expression.value);
        if (!value && expression.has_fallback)
          value = evaluate_self(evaluate_self, expression.fallback);
        if (!value) return std::nullopt;
        output.insert(output.end(), value->begin(), value->end());
      }
      return output;
    };
    auto value = evaluate(evaluate, local.at(name));
    resolved.insert(name);
    inherited[name] = value;
    return value;
  };
  for (const auto &[name, unused] : local) resolve(resolve, name);
  return inherited;
}
} // namespace webscene::native_web
