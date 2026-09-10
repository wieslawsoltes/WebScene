#pragma once
#include <algorithm>
#include "webscene_native_dom.h"
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace webscene::native_web {
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
