#pragma once
#include "webscene_native_dom.h"
#include "compiled_variables.hpp"
#include <functional>
#include <memory>
#include <thread>

namespace webscene::native_web {
using node_id = uint32_t;
using length = webscene_native::css_length;
using length_unit = webscene_native::length_unit;
using display_mode = webscene_native::display_mode;
using flex_direction = webscene_native::flex_direction;
using align_mode = webscene_native::align_mode;
using justify_mode = webscene_native::justify_mode;
using position_mode = webscene_native::position_mode;
using grid_track = webscene_native::node_style::grid_data::track;
// Supported compiled-style writer. Generated code never accesses engine fields.
// A writer is borrowed only for the duration of declaration application.
class style {
  friend class document;
  webscene_native::node_style &value_;
  length border_widths_[4]{{3,length_unit::pixels},{3,length_unit::pixels},{3,length_unit::pixels},{3,length_unit::pixels}};
  bool border_visible_[4]{};
  const computed_variables &variables_;
  explicit style(webscene_native::node_style &value, const computed_variables &variables)
      : value_(value), variables_(variables) {}

public:
  variable_result evaluate(const std::vector<variable_expression> &expressions) const {
    return evaluate_variables(expressions, variables_);
  }
  const variable_result *variable(const std::string &name) const {
    auto found = variables_.find(name);
    return found == variables_.end() ? nullptr : &found->second;
  }
  void set_grid_template_columns(std::vector<grid_track> tracks) {
    auto &grid = value_.mutable_grid();
    grid.subgrid_columns = false;
    grid.two_columns = tracks.size() > 1;
    grid.template_columns = std::move(tracks);
  }
  void set_grid_template_rows(std::vector<grid_track> tracks) {
    auto &grid = value_.mutable_grid();
    grid.fractional_rows = false;
    for (const auto &track : tracks)
      grid.fractional_rows |= track.fraction > 0;
    grid.template_rows = std::move(tracks);
  }
  void set_width(length value) { value_.width = value; }
  void set_height(length value) { value_.height = value; }
  void set_min_width(length value) { value_.min_width = value; }
  void set_min_height(length value) { value_.min_height = value; }
  void set_max_width(length value) { value_.max_width = value; }
  void set_max_height(length value) { value_.max_height = value; }
  void set_left(length value) { value_.left = value; }
  void set_top(length value) { value_.top = value; }
  void set_right(length value) { value_.right = value; }
  void set_bottom(length value) { value_.bottom = value; }
  void set_padding_left(length value) { value_.padding_left = value; }
  void set_padding_top(length value) { value_.padding_top = value; }
  void set_padding_right(length value) { value_.padding_right = value; }
  void set_padding_bottom(length value) { value_.padding_bottom = value; }
  void set_margin_left(length value) { value_.margin_left = value; }
  void set_margin_top(length value) { value_.margin_top = value; }
  void set_margin_right(length value) { value_.margin_right = value; }
  void set_margin_bottom(length value) { value_.margin_bottom = value; }
  void set_row_gap(length value) { value_.row_gap = value; }
  void set_column_gap(length value) { value_.column_gap = value; }
  void set_flex_basis(length value) { value_.flex_basis = value; }
  void set_border_top_left_radius(length value) {
    value_.border_top_left_radius = value;
  }
  void set_border_top_right_radius(length value) {
    value_.border_top_right_radius = value;
  }
  void set_border_bottom_left_radius(length value) {
    value_.border_bottom_left_radius = value;
  }
  void set_border_bottom_right_radius(length value) {
    value_.border_bottom_right_radius = value;
  }
  void set_font_family(std::string value) {
    value_.mutable_textual().font_family = std::move(value);
  }
  void set_line_height(float value) { value_.line_height = value; }
  void set_letter_spacing(float value) {
    value_.letter_spacing = value;
    value_.letter_spacing_specified = true;
  }
  void set_word_spacing(float value) {
    value_.word_spacing = value;
    value_.word_spacing_specified = true;
  }
  void set_font_size(float value) { value_.font_size = value; }
  void set_flex_grow(float value) { value_.flex_grow = value; }
  void set_flex_shrink(float value) { value_.flex_shrink = value; }
  void set_opacity(float value) { value_.opacity = value; }
  void set_border_box(bool value) { value_.border_box = value; }
  void set_margin_left_auto(bool value) { value_.margin_left_auto = value; }
  void set_margin_top_auto(bool value) { value_.margin_top_auto = value; }
  void set_margin_right_auto(bool value) { value_.margin_right_auto = value; }
  void set_margin_bottom_auto(bool value) { value_.margin_bottom_auto = value; }
  void set_border_left_width(length width) {
    border_widths_[0] = width;
    value_.border_left_width = border_visible_[0] ? width : length{0,length_unit::pixels};
  }
  void set_border_left_solid(bool visible) {
    border_visible_[0] = visible;
    value_.border_left_width = visible ? border_widths_[0] : length{0,length_unit::pixels};
  }
  void set_border_top_width(length width) {
    border_widths_[1] = width;
    value_.border_top_width = border_visible_[1] ? width : length{0,length_unit::pixels};
  }
  void set_border_top_solid(bool visible) {
    border_visible_[1] = visible;
    value_.border_top_width = visible ? border_widths_[1] : length{0,length_unit::pixels};
  }
  void set_border_right_width(length width) {
    border_widths_[2] = width;
    value_.border_right_width = border_visible_[2] ? width : length{0,length_unit::pixels};
  }
  void set_border_right_solid(bool visible) {
    border_visible_[2] = visible;
    value_.border_right_width = visible ? border_widths_[2] : length{0,length_unit::pixels};
  }
  void set_border_bottom_width(length width) {
    border_widths_[3] = width;
    value_.border_bottom_width = border_visible_[3] ? width : length{0,length_unit::pixels};
  }
  void set_border_bottom_solid(bool visible) {
    border_visible_[3] = visible;
    value_.border_bottom_width = visible ? border_widths_[3] : length{0,length_unit::pixels};
  }
  void set_border_left_color(uint32_t color, bool current = false) {
    value_.border_left_rgba = color;
    value_.border_left_current_color = current;
  }
  void set_border_top_color(uint32_t color, bool current = false) {
    value_.border_top_rgba = color;
    value_.border_top_current_color = current;
  }
  void set_border_right_color(uint32_t color, bool current = false) {
    value_.border_right_rgba = color;
    value_.border_right_current_color = current;
  }
  void set_border_bottom_color(uint32_t color, bool current = false) {
    value_.border_bottom_rgba = color;
    value_.border_bottom_current_color = current;
  }
  void set_background_rgba(uint32_t value) { value_.background_rgba = value; }
  void set_foreground_rgba(uint32_t value) { value_.foreground_rgba = value; }
  void set_font_weight(int32_t value) { value_.font_weight = value; }
  void set_display(display_mode value) { value_.display = value; }
  void set_direction(flex_direction value) { value_.direction = value; }
  void set_align_items(align_mode value) { value_.align_items = value; }
  void set_justify_content(justify_mode value) {
    value_.justify_content = value;
  }
  void set_position(position_mode value) { value_.position = value; }
};
struct attribute_selector {
  std::string name, value;
  bool equals{};
};
struct selector_part {
  std::string tag, id;
  std::vector<std::string> classes;
  bool focus{}, hover{};
  char relation{}; // relationship to the preceding (ancestor) part: ' ' or '>'
  bool root{};
  std::vector<attribute_selector> attributes;
  bool active{}, disabled{}, focus_visible{};
};
struct selector {
  std::vector<selector_part> parts;
  uint32_t specificity{};
};
struct declaration {
  bool important{};
  void (*apply)(style &){};
  std::string custom_name;
  std::vector<variable_expression> custom_value;
};
struct rule {
  selector match;
  std::vector<declaration> declarations;
  float min_width{}, max_width{1e9f};
  node_id inline_target{};
};
struct event {
  std::string type;
  node_id target{}, current_target{};
  bool propagation_stopped{}, default_prevented{};
  float client_x{}, client_y{}, delta_y{};
  void stop_propagation() { propagation_stopped = true; }
  void prevent_default() { default_prevented = true; }
};
struct scene {
  std::vector<webscene_scene_command> commands;
  std::vector<webscene_scene_string> strings;
  std::vector<char> bytes;
  std::vector<webscene_canvas_layer> layers;
  std::vector<webscene_canvas_command> canvas;
  float width{}, height{};
  uint64_t revision{};
};
struct document_state;
class subscription {
public:
  subscription() = default;
  subscription(subscription &&) noexcept;
  subscription &operator=(subscription &&) noexcept;
  subscription(const subscription &) = delete;
  subscription &operator=(const subscription &) = delete;
  ~subscription();
  void dispose() noexcept;

private:
  friend class document;
  subscription(std::weak_ptr<document_state>, uint64_t);
  std::weak_ptr<document_state> state_;
  uint64_t id_{};
};
// A document is confined to its creating thread. Hosts dispatch work to that
// thread. IDs are local to this document and never recycled during its
// lifetime.
class document {
public:
  explicit document(webscene_text_measure_callback = nullptr, void * = nullptr);
  ~document();
  document(const document &) = delete;
  document &operator=(const document &) = delete;
  node_id body() const;
  node_id root() const;
  node_id element(node_id parent, std::string tag);
  node_id text(node_id parent, std::string value);
  void set_text(node_id, std::string);
  void attribute(node_id, std::string name, std::string value);
  void remove(node_id);
  node_id find(std::string_view id) const;
  void add_rule(rule);
  subscription on(node_id, std::string type, std::function<void(event &)>);
  // Returns false when a listener prevents the default action or disposes the
  // document.
  bool dispatch(node_id, std::string type, float client_x = 0,
                float client_y = 0, float delta_y = 0);
  void pointer(std::string type, float x, float y);
  void wheel(float x, float y, float delta_y);
  void focus(node_id);
  void key(std::string_view key, bool shift = false);
  node_id focused() const;
  void clear_canvas(node_id);
  void fill_rect(node_id, float x, float y, float width, float height,
                 uint32_t rgba);
  const scene &render(float width, float height);
  webscene_native::layout_rect bounds(node_id) const;
  std::string text_content(node_id) const;
  void dispose();

private:
  std::shared_ptr<document_state> state_;
};
} // namespace webscene::native_web
