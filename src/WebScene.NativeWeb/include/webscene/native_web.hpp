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
using overflow_mode = webscene_native::overflow_mode;
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
  std::optional<uint32_t> color_with_opacity(const std::vector<variable_expression> &expressions, float fraction) const {
    auto tokens = evaluate(expressions);
    if (!tokens || tokens->size() != 1 || !tokens->front().color) return std::nullopt;
    const auto color = *tokens->front().color;
    auto alpha = static_cast<uint32_t>((color & 255u) * fraction + .5f);
    return (color & 0xffffff00u) | std::min(alpha,255u);
  }
  std::optional<uint32_t> mix_colors(const std::vector<variable_expression> &first,
      const std::vector<variable_expression> &second, float first_weight, float second_weight) const {
    auto a = evaluate(first), b = evaluate(second);
    if (!a || !b || a->size() != 1 || b->size() != 1 ||
        !a->front().color || !b->front().color) return std::nullopt;
    const auto ca = *a->front().color, cb = *b->front().color;
    const double total = first_weight + second_weight;
    if (total <= 0) return std::nullopt;
    const double wa = first_weight / total, wb = second_weight / total;
    const double aa = (ca & 255u) / 255.0, ab = (cb & 255u) / 255.0;
    const double alpha = aa * wa + ab * wb;
    const auto channel = [&](unsigned shift) -> uint32_t {
      if (alpha == 0) return 0;
      const double value = (((ca >> shift) & 255u) * aa * wa +
          ((cb >> shift) & 255u) * ab * wb) / alpha;
      return static_cast<uint32_t>(std::clamp(value + .5, 0.0, 255.0));
    };
    return (channel(24) << 24) | (channel(16) << 16) | (channel(8) << 8) |
        static_cast<uint32_t>(std::clamp(alpha * std::min(total, 1.0) * 255 + .5, 0.0, 255.0));
  }
  const variable_result *variable(const std::string &name) const {
    auto found = variables_.find(name);
    return found == variables_.end() ? nullptr : &found->second;
  }
  void set_align_self(align_mode mode, bool specified = true) { value_.align_self = mode; value_.align_self_specified = specified; }
  void set_table_layout_fixed(bool fixed) { value_.table_layout_fixed = fixed; }
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
  void set_z_index(int32_t value, bool automatic = false) { value_.z_index = value; value_.z_index_auto = automatic; }
  void set_pointer_events(bool none, bool specified = true) { value_.pointer_events_none = none; value_.pointer_events_specified = specified; }
  void set_visibility(bool hidden, bool specified = true) { value_.visibility_hidden = hidden; value_.visibility_specified = specified; }
  void set_overflow_x(overflow_mode value) { value_.overflow_x = value; }
  void set_overflow_y(overflow_mode value) { value_.overflow_y = value; }
  void set_font_smoothing(std::string value) { value_.mutable_textual().font_smoothing = std::move(value); }
  void set_svg_text_anchor(std::string value) { value_.mutable_textual().svg_text_anchor = std::move(value); }
  void set_svg_stroke_width(std::string value) { value_.mutable_textual().svg_stroke_width = std::move(value); }
  void set_svg_fill(std::string value) { value_.mutable_textual().svg_fill = std::move(value); }
  void set_svg_stroke(std::string value) { value_.mutable_textual().svg_stroke = std::move(value); }
  void set_text_align(std::string value) { value_.mutable_textual().text_align = std::move(value); }
  void set_white_space(std::string value) { value_.mutable_textual().white_space = std::move(value); }
  void set_text_transform(std::string value) { value_.mutable_textual().text_transform = std::move(value); }
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
  void set_translation(length x, length y, bool active = true) {
    value_.transform_translate_x = x;
    value_.transform_translate_y = y;
    value_.transform_scale_x = value_.transform_scale_y = 1;
    value_.transform_rotate_degrees = 0;
    value_.transform_specified = true;
    value_.transform_stacking_context = active;
  }
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
  void set_flex_wrap(bool value) { value_.flex_wrap = value; }
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
  void set_outline_offset(length value) { value_.outline_offset = value; }
  void set_outline(const variable_result &tokens) {
    value_.outline_width = {};
    value_.outline_rgba = 0;
    value_.outline_current_color = false;
    if (!tokens || tokens->empty() || tokens->size() > 3) return;
    length width{3, length_unit::pixels};
    uint32_t color = 0;
    bool have_width = false, have_color = false, have_style = false, visible = false, current = true;
    for (const auto &token : *tokens) {
      if (token.is_keyword("solid") || token.is_keyword("none")) {
        if (have_style) return;
        have_style = true; visible = token.is_keyword("solid");
      } else if (token.color || token.is_keyword("currentcolor")) {
        if (have_color) return;
        have_color = true; current = !token.color; color = token.color.value_or(0);
      } else {
        if (have_width) return;
        have_width = true;
        if (token.is_keyword("thin")) width = {1, length_unit::pixels};
        else if (token.is_keyword("medium")) width = {3, length_unit::pixels};
        else if (token.is_keyword("thick")) width = {5, length_unit::pixels};
        else if (token.length && token.length->unit != length_unit::automatic &&
            token.length->unit != length_unit::percent && token.length->value >= 0) width = *token.length;
        else return;
      }
    }
    if (!visible) return;
    value_.outline_width = width;
    value_.outline_rgba = color;
    value_.outline_current_color = current;
  }
  void set_box_shadow(const variable_result &tokens) {
    value_.box_shadow_present = false;
    if (!tokens || tokens->size() < 3 || tokens->size() > 5) return;
    // First supported shadow grammar: x y [blur [spread]] color.
    // Typed payloads were parsed by the compiler, never by this writer.
    const auto &color = tokens->back().color;
    if (!color) return;
    float values[4]{};
    for (size_t i = 0; i + 1 < tokens->size(); ++i) {
      const auto &length = (*tokens)[i].length;
      if (!length || length->unit != length_unit::pixels) return;
      values[i] = length->value;
    }
    if (values[2] < 0) return;
    value_.box_shadow_offset_x = values[0];
    value_.box_shadow_offset_y = values[1];
    value_.box_shadow_blur_radius = values[2];
    value_.box_shadow_spread_radius = values[3];
    value_.box_shadow_rgba = *color;
    value_.box_shadow_present = true;
  }
  void reset_background() { value_.background_rgba = 0; value_.clear_background_image(); }
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
  bool first_child{}, last_child{}, only_child{};
  std::vector<selector_part> excluded;
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
  float min_height{}, max_height{1e9f};
};
struct event {
  std::string type;
  node_id target{}, current_target{};
  bool propagation_stopped{}, default_prevented{};
  float client_x{}, client_y{}, delta_y{};
  uint32_t buttons{};
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
  void remove_attribute(node_id, std::string_view name);
  // Uses the most recent rendered layout and computed overflow values.
  void scroll_to(node_id, float x, float y);
  std::pair<float, float> scroll_offset(node_id) const;
  std::optional<std::string> attribute(node_id, std::string_view name) const;
  node_id parent(node_id) const;
  void remove(node_id);
  node_id find(std::string_view id) const;
  void add_rule(rule);
  subscription on(node_id, std::string type, std::function<void(event &)>);
  // Returns false when a listener prevents the default action or disposes the
  // document.
  bool dispatch(node_id, std::string type, float client_x = 0,
                float client_y = 0, float delta_y = 0, uint32_t buttons = 0);
  void pointer(std::string type, float x, float y, uint32_t buttons = 1);
  void wheel(float x, float y, float delta_y);
  void focus(node_id);
  void key(std::string_view key, bool shift = false);
  node_id focused() const;
  void set_external_canvas(node_id, bool enabled);
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
