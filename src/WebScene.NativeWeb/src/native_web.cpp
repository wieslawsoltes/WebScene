#include "webscene/native_web.hpp"
#include "webscene_native_style_defaults.h"
#include "webscene_native_form_state.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace webscene::native_web {
using namespace webscene_native;
struct listener {
  node_id node;
  std::string type;
  std::function<void(event &)> callback;
};
struct document_state {
  native_document dom;
  std::thread::id owner{std::this_thread::get_id()};
  bool alive{true};
  bool styles_dirty{true};
  bool reduced_motion{};
  uint64_t rendered_scene_generation{};
  bool keyboard_modality{true};
  node_id focus{}, hover{}, pressed{}, body_id{};
  uint64_t next_listener{1};
  std::mutex listener_mutex;
  std::map<uint64_t, listener> listeners;
  std::vector<rule> rules;
  std::unique_ptr<stylesheet_resolver> resolver;
  std::vector<native_document::transition_event_record> pending_transition_events;
  scene output;
  document_state(webscene_text_measure_callback cb, void *data)
      : dom(cb, data) {
    dom.body().tag = "html";
    auto &body = dom.create_element("body");
    dom.append_child(dom.body(), body);
    body_id = body.id;
  }
  void check() const {
    if (owner != std::this_thread::get_id())
      throw std::logic_error("document accessed off its owner thread");
    if (!alive)
      throw std::logic_error("document is disposed");
  }
  dom_node &node(node_id id) {
    check();
    auto *n = dom.find_by_native_id(id);
    if (!n)
      throw std::invalid_argument("invalid document node");
    return *n;
  }
  bool connected(const dom_node &n) const {
    for (auto *p = &n; p; p = p->parent)
      if (p == &dom.body())
        return true;
    return false;
  }
};
subscription::subscription(std::weak_ptr<document_state> s, uint64_t id)
    : state_(s), id_(id) {}
subscription::subscription(subscription &&other) noexcept
    : state_(std::move(other.state_)), id_(std::exchange(other.id_, 0)) {}
subscription &subscription::operator=(subscription &&other) noexcept {
  if (this != &other) {
    dispose();
    state_ = std::move(other.state_);
    id_ = std::exchange(other.id_, 0);
  }
  return *this;
}
subscription::~subscription() { dispose(); }
void subscription::dispose() noexcept {
  if (auto s = state_.lock()) {
    std::lock_guard lock(s->listener_mutex);
    s->listeners.erase(id_);
  }
  id_ = 0;
}
document::document(webscene_text_measure_callback cb, void *data)
    : state_(std::make_shared<document_state>(cb, data)) {}
document::~document() { state_->alive = false; }
bool document::disposed() const noexcept { return !state_->alive; }
void document::dispose() {
  state_->check();
  state_->alive = false;
  std::lock_guard lock(state_->listener_mutex);
  state_->listeners.clear();
  state_->rules.clear();
  state_->resolver.reset();
  state_->pending_transition_events.clear();
  state_->output = {};
  state_->dom.clear();
}
node_id document::root() const {
  state_->check();
  return state_->dom.body().id;
}
node_id document::body() const {
  state_->check();
  return state_->body_id;
}
static void invalidate_textarea_default(dom_node* node) {
  for(auto* ancestor=node;ancestor;ancestor=ancestor->parent)
    if(ancestor->tag=="textarea" && !ancestor->form_control().dirty_value)
      ancestor->mutable_form_control().value_initialized=false;
}
node_id document::element(node_id parent, std::string tag) {
  auto &p = state_->node(parent);
  auto &n = state_->dom.create_element(std::move(tag));
  state_->dom.append_child(p, n);
  invalidate_textarea_default(&p);
  state_->styles_dirty = true;
  return n.id;
}
node_id document::text(node_id parent, std::string value) {
  const auto id = element(parent, "#text");
  state_->node(id).text_content = std::move(value);
  return id;
}
void document::set_text(node_id id, std::string value) {
  auto &n = state_->node(id);
  invalidate_textarea_default(&n);
  if (n.tag == "#text")
    n.text_content = std::move(value);
  else {
    auto children = n.children;
    for (auto *child : children)
      remove(child->id);
    text(id, std::move(value));
  }
  state_->styles_dirty = true;
  state_->dom.mark_dirty();
}
std::optional<std::string> document::attribute(node_id id, std::string_view name) const {
  const auto &node = state_->node(id);
  auto found = node.attributes.find(std::string(name));
  if (found == node.attributes.end()) return std::nullopt;
  return found->second;
}
node_id document::parent(node_id id) const {
  auto *parent = state_->node(id).parent;
  return parent ? parent->id : 0;
}
void document::attribute(node_id id, std::string name, std::string value) {
  auto &n = state_->node(id);
  if ((name == "style" && !state_->resolver) || name.starts_with("on"))
    throw std::invalid_argument(
        "use compiled styles and native event subscriptions");
  if(name=="value" && n.tag=="input" && !n.form_control().dirty_value) n.mutable_form_control().value_initialized=false;
  if (name == "id")
    n.id_attribute = value;
  if (name == "class")
    n.class_name = value;
  n.attributes[std::move(name)] = std::move(value);
  state_->styles_dirty = true;
  state_->dom.mark_dirty();
}
void document::scroll_to(node_id id, float x, float y) {
  if (!std::isfinite(x) || !std::isfinite(y)) throw std::invalid_argument("non-finite scroll offset");
  auto &n = state_->node(id);
  const auto permitted = [](overflow_mode mode) {
    return mode == overflow_mode::hidden || mode == overflow_mode::automatic || mode == overflow_mode::scroll;
  };
  x = permitted(n.style.overflow_x) ? std::clamp(x, 0.0f, std::max(0.0f, n.scroll_content_width - n.scroll_viewport_width)) : 0;
  y = permitted(n.style.overflow_y) ? std::clamp(y, 0.0f, std::max(0.0f, n.scroll_content_height - n.scroll_viewport_height)) : 0;
  if (n.scroll_left == x && n.scroll_top == y) return;
  n.scroll_left = x;
  n.scroll_top = y;
  state_->styles_dirty = true;
  state_->dom.mark_dirty();
}
std::pair<float, float> document::scroll_offset(node_id id) const {
  const auto &n = state_->node(id);
  return {n.scroll_left, n.scroll_top};
}
void document::remove_attribute(node_id id, std::string_view name) {
  auto &n = state_->node(id);
  if (!n.attributes.erase(std::string(name))) return;
  if(name=="value" && n.tag=="input" && !n.form_control().dirty_value) n.mutable_form_control().value_initialized=false;
  if (name == "id") n.id_attribute.clear();
  if (name == "class") n.class_name.clear();
  state_->styles_dirty = true;
  state_->dom.mark_dirty();
}
void document::remove(node_id id) {
  auto &n = state_->node(id);
  if (id == body() || id == root())
    throw std::invalid_argument("cannot remove document body");
  invalidate_textarea_default(n.parent);
  std::vector<node_id> removed;
  const auto collect = [&](auto &&self, dom_node &node) -> void {
    removed.push_back(node.id);
    for (auto *c : node.children)
      self(self, *c);
  };
  collect(collect, n);
  {
    std::lock_guard lock(state_->listener_mutex);
    std::erase_if(state_->listeners, [&](const auto &v) {
      return std::ranges::find(removed, v.second.node) != removed.end();
    });
  }
  if (std::ranges::find(removed, state_->focus) != removed.end())
    state_->focus = 0;
  if (std::ranges::find(removed, state_->hover) != removed.end())
    state_->hover = 0;
  if (std::ranges::find(removed, state_->pressed) != removed.end())
    state_->pressed = 0;
  std::erase_if(state_->rules, [&](const auto &r) {
    return r.inline_target &&
           std::ranges::find(removed, r.inline_target) != removed.end();
  });
  state_->dom.parser_remove_from_parent(n);
  state_->dom.erase_detached_subtree(n);
  state_->styles_dirty = true;
  state_->dom.mark_dirty();
}
node_id document::find(std::string_view id) const {
  state_->check();
  auto *n = state_->dom.find_by_id(std::string(id));
  return n ? n->id : 0;
}
void document::add_rule(rule r) {
  state_->check();
  if(state_->resolver) throw std::logic_error("typed rules cannot be mixed with a stylesheet resolver");
  state_->rules.push_back(std::move(r));
  state_->styles_dirty = true;
  state_->dom.mark_dirty();
}
void document::set_stylesheet_resolver(std::unique_ptr<stylesheet_resolver> resolver) {
  state_->check();
  if(resolver && !state_->rules.empty())
    throw std::logic_error("stylesheet resolver cannot replace existing typed rules");
  state_->resolver=std::move(resolver);
  state_->styles_dirty=true;
  state_->dom.mark_dirty();
}
subscription document::on(node_id node, std::string type,
                          std::function<void(event &)> cb) {
  state_->node(node);
  std::lock_guard lock(state_->listener_mutex);
  auto id = state_->next_listener++;
  state_->listeners.emplace(id, listener{node, std::move(type), std::move(cb)});
  return subscription(state_, id);
}
bool document::dispatch(node_id target, std::string type, float client_x, float client_y, float delta_y, uint32_t buttons, std::string property_name, float elapsed_time_seconds, input_modifiers modifiers, std::string data, std::string input_type) {
  auto &n = state_->node(target);
  std::vector<node_id> path;
  for (auto *p = &n; p; p = p->parent)
    path.push_back(p->id);
  event e{std::move(type), target};
  e.client_x = client_x;
  e.client_y = client_y;
  e.delta_y = delta_y;
  e.buttons = buttons;
  e.property_name = std::move(property_name);
  e.elapsed_time_seconds = elapsed_time_seconds;
  e.modifiers = modifiers;
  e.data=std::move(data);e.input_type=std::move(input_type);
  for (auto id : path) {
    if (!state_->alive)
      return false;
    if (!state_->dom.find_by_native_id(id))
      continue;
    e.current_target = id;
    std::vector<uint64_t> listeners;
    {
      std::lock_guard lock(state_->listener_mutex);
      for (const auto &[token, l] : state_->listeners)
        if (l.node == id && l.type == e.type)
          listeners.push_back(token);
    }
    for (auto token : listeners) {
      std::function<void(event &)> cb;
      {
        std::lock_guard lock(state_->listener_mutex);
        if (auto it = state_->listeners.find(token);
            it != state_->listeners.end())
          cb = it->second.callback;
      }
      if (cb)
        cb(e);
      if (!state_->alive)
        return false;
    }
    if (e.propagation_stopped)
      break;
  }
  return !e.default_prevented;
}

static std::optional<int> tab_index(const dom_node &n) {
  const auto found = n.attributes.find("tabindex");
  if (found == n.attributes.end()) return std::nullopt;
  const auto &value = found->second;
  size_t at = value.find_first_not_of(" \t\n\r\f");
  if (at == std::string::npos) return std::nullopt;
  bool negative = value[at] == '-';
  if (value[at] == '+' || negative) ++at;
  if (at == value.size() || value[at] < '0' || value[at] > '9') return std::nullopt;
  int result = 0;
  while (at < value.size() && value[at] >= '0' && value[at] <= '9') {
    int digit = value[at++] - '0';
    result = result > (std::numeric_limits<int>::max() - digit) / 10
        ? std::numeric_limits<int>::max() : result * 10 + digit;
  }
  return negative ? -result : result;
}
static bool focusable(const dom_node &n) {
  if(n.tag=="input" && n.attributes.contains("type") && n.attributes.at("type")=="hidden") return false;
  for (auto *ancestor = &n; ancestor; ancestor = ancestor->parent)
    if (ancestor->style.display == display_mode::none) return false;
  return (n.tag == "button" || forms::is_text_control(&n) || tab_index(n).has_value()) &&
         !n.attributes.contains("disabled");
}
void document::focus(node_id id) {
  state_->check();
  if (id && !focusable(state_->node(id)))
    return;
  auto old = state_->focus;
  if (old == id)
    return;
  if(auto* previous=state_->dom.find_by_native_id(old)) {
    if(previous->has_form_control()) {auto& control=previous->mutable_form_control();control.input_focused=false;control.caret_visible=false;}
  }
  if(auto* next=state_->dom.find_by_native_id(id);next && forms::is_text_control(next)) {
    forms::ensure_text_value(*next);next->mutable_form_control().input_focused=true;next->mutable_form_control().caret_visible=true;
  }
  state_->focus = id;
  state_->styles_dirty = true;
  state_->dom.mark_dirty();
  if (old && state_->dom.find_by_native_id(old))
    dispatch(old, "blur");
  if (state_->alive && id && state_->dom.find_by_native_id(id))
    dispatch(id, "focus");
}
node_id document::focused() const {
  state_->check();
  return state_->focus;
}
std::string document::value(node_id id) const {
  auto& node=state_->node(id);
  if(node.tag!="input" && node.tag!="textarea") throw std::invalid_argument("value requires a text control");
  forms::ensure_text_value(node);return node.form_control().value;
}
void document::set_value(node_id id,std::string value) {
  auto& node=state_->node(id);
  if(node.tag!="input" && node.tag!="textarea") throw std::invalid_argument("value requires a text control");
  auto& control=node.mutable_form_control();
  control.value=std::move(value);control.value_initialized=true;control.dirty_value=true;
  control.selection_start=control.selection_end=control.value.size();
  control.selection_direction=text_selection_direction::none;
  state_->styles_dirty=true;state_->dom.mark_dirty();
}
void document::set_selection(node_id id,size_t start,size_t end) {
  auto& node=state_->node(id);
  if(node.tag!="input" && node.tag!="textarea") throw std::invalid_argument("selection requires a text control");
  forms::ensure_text_value(node);auto& control=node.mutable_form_control();
  const auto boundary=[&](size_t offset) {
    return offset<=control.value.size() && (offset==control.value.size() ||
        (static_cast<unsigned char>(control.value[offset])&0xc0U)!=0x80U);
  };
  if(start>end || !boundary(start) || !boundary(end)) throw std::invalid_argument("invalid UTF-8 selection range");
  control.selection_start=start;control.selection_end=end;
  control.selection_direction=text_selection_direction::none;control.selection_explicitly_set=true;
  state_->dom.mark_scene_changed();
}
std::pair<size_t,size_t> document::selection(node_id id) const {
  auto& node=state_->node(id);
  if(node.tag!="input" && node.tag!="textarea") throw std::invalid_argument("selection requires a text control");
  forms::ensure_text_value(node);const auto& control=node.form_control();
  return {control.selection_start,control.selection_end};
}
bool document::text_input(std::string text) {
  state_->check();
  const auto id=state_->focus;
  auto* node=state_->dom.find_by_native_id(id);
  if(!forms::is_text_control(node) || state_->dom.is_inert(*node)) return false;
  if(node->attributes.contains("readonly")) return true;
  if(node->tag=="input") std::erase_if(text,[](char c){return c=='\r' || c=='\n';});
  if(text.empty()) return true;
  if(!dispatch(id,"beforeinput",0,0,0,0,{},0,{},text,"insertText")) return true;
  node=state_->dom.find_by_native_id(id);
  if(!node || !state_->connected(*node) || !forms::is_text_control(node) ||
     node->attributes.contains("readonly") || state_->dom.is_inert(*node)) return true;
  forms::ensure_text_value(*node);
  auto& control=node->mutable_form_control();
  const auto start=std::min(control.selection_start,control.value.size());
  const auto end=std::min(std::max(control.selection_start,control.selection_end),control.value.size());
  control.value.replace(start,end-start,text);control.dirty_value=true;
  control.selection_start=control.selection_end=start+text.size();
  control.selection_direction=text_selection_direction::none;control.selection_explicitly_set=true;control.caret_visible=true;
  state_->styles_dirty=true;state_->dom.mark_dirty();
  dispatch(id,"input",0,0,0,0,{},0,{},std::move(text),"insertText");
  return true;
}
bool document::has_active_animations() const {
  state_->check();return state_->dom.has_active_animations() || !state_->pending_transition_events.empty();
}
bool document::advance_animations(double timestamp_ms) {
  state_->check();
  if(!std::isfinite(timestamp_ms) || timestamp_ms<0)
    throw std::invalid_argument("invalid animation timestamp");
  state_->dom.signal_animation_frame(timestamp_ms);
  const auto changed=state_->dom.advance_animations();
  auto events=std::exchange(state_->pending_transition_events,{});
  auto fresh=state_->dom.take_transition_events();
  events.insert(events.end(),std::make_move_iterator(fresh.begin()),std::make_move_iterator(fresh.end()));
  for(const auto& event:events) {
    if(!state_->alive) break;
    if(state_->dom.find_by_native_id(event.node_id))
      dispatch(event.node_id,event.type,0,0,0,0,event.property_name,event.elapsed_time_seconds);
  }
  return changed;
}
void document::set_reduced_motion(bool enabled) {
  state_->check();
  if(state_->reduced_motion==enabled) return;
  state_->reduced_motion=enabled;
  state_->styles_dirty=true;
  state_->dom.mark_dirty();
}
std::string document::cursor_at(float x, float y) const {
  state_->check();
  auto *node=state_->dom.hit_test(state_->dom.body(),x,y);
  for(auto *current=node;current;current=current->parent)
    if(!current->style.textual().cursor.empty()) return current->style.textual().cursor;
  return "auto";
}
void document::wheel(float x, float y, float delta_y, input_modifiers modifiers) {
  state_->check();
  if (!std::isfinite(delta_y)) throw std::invalid_argument("non-finite wheel delta");
  auto *node = state_->dom.hit_test(state_->dom.body(), x, y);
  if (!node) return;
  std::vector<node_id> ancestors;
  for (auto *p = node; p; p = p->parent) ancestors.push_back(p->id);
  if (!dispatch(node->id, "wheel", x, y, delta_y, 0, {}, 0, modifiers)) return;
  for (auto id : ancestors) {
    auto *current = state_->dom.find_by_native_id(id);
    if (!current || !current->style.scroll_y_enabled) continue;
    auto before = current->scroll_top;
    scroll_to(id, current->scroll_left, before + delta_y);
    if (current->scroll_top != before) break;
  }
}
void document::pointer(std::string type, float x, float y, uint32_t buttons, input_modifiers modifiers) {
  state_->check();
  auto *n = state_->dom.hit_test(state_->dom.body(), x, y);
  auto id = n ? n->id : 0;
  if (state_->hover != id) {
    state_->hover = id;
    state_->styles_dirty = true;
    state_->dom.mark_dirty();
  }
  if (type == "pointerdown" || type == "pointerup") {
    for (auto *p = n; p; p = p->parent)
      if (p->tag == "button" && p->attributes.contains("disabled")) {
        state_->pressed = 0;
        return;
      }
  }
  if (type == "pointerdown") {
    state_->keyboard_modality = false;
    // Only the primary button participates in click activation.
    // Auxiliary presses still reach native handlers (for example CAD panning).
    state_->pressed = (buttons & 1u) ? id : 0;
    state_->styles_dirty = true;
    state_->dom.mark_dirty();
  }
  if (type == "pointercancel") {
    state_->pressed = 0;
    state_->styles_dirty = true;
    state_->dom.mark_dirty();
  }
  const bool default_allowed = !id || dispatch(id, type, x, y, 0, buttons, {}, 0, modifiers);
  if (!state_->alive)
    return;
  if (type == "pointerdown" && default_allowed) {
    for (auto *p = state_->dom.find_by_native_id(id); p; p = p->parent) {
      if (focusable(*p)) {
        focus(p->id);
        break;
      }
    }
  }
  if (type == "pointerup") {
    const auto pressed = std::exchange(state_->pressed, 0);
    state_->styles_dirty = true;
    state_->dom.mark_dirty();
    if (id && id == pressed && state_->dom.find_by_native_id(id))
      dispatch(id, "click", x, y, 0, buttons, {}, 0, modifiers);
  }
}
void document::key(std::string_view key, bool shift) {
  this->key(key,input_modifiers{shift});
}
void document::key(std::string_view key, input_modifiers modifiers) {
  const bool shift=modifiers.shift;
  state_->check();
  if (!state_->keyboard_modality) {
    state_->keyboard_modality = true;
    state_->styles_dirty = true;
    state_->dom.mark_dirty();
  }
  if(key=="Backspace" || key=="Delete") {
    if(modifiers.control || modifiers.alt || modifiers.meta) return;
    const auto id=state_->focus;
    auto* node=state_->dom.find_by_native_id(id);
    if(!forms::is_text_control(node) || node->attributes.contains("readonly") || state_->dom.is_inert(*node)) return;
    forms::ensure_text_value(*node);
    const auto range=[&](const auto& control) {
      auto start=std::min(control.selection_start,control.value.size());
      auto end=std::min(std::max(control.selection_start,control.selection_end),control.value.size());
      if(start==end) {
        if(key=="Backspace") start=forms::previous_utf8_boundary(control.value,start);
        else end=forms::next_utf8_boundary(control.value,end);
      }
      return std::pair{start,end};
    };
    if(range(node->form_control()).first==range(node->form_control()).second) return;
    const std::string type=key=="Backspace"?"deleteContentBackward":"deleteContentForward";
    if(!dispatch(id,"beforeinput",0,0,0,0,{},0,modifiers,{},type)) return;
    node=state_->dom.find_by_native_id(id);
    if(!node || !state_->connected(*node) || !forms::is_text_control(node) ||
       node->attributes.contains("readonly") || state_->dom.is_inert(*node)) return;
    auto& control=node->mutable_form_control();auto [start,end]=range(control);
    if(start==end) return;
    control.value.erase(start,end-start);control.dirty_value=true;
    control.selection_start=control.selection_end=start;control.selection_direction=text_selection_direction::none;
    control.selection_explicitly_set=true;control.caret_visible=true;
    state_->styles_dirty=true;state_->dom.mark_dirty();
    dispatch(id,"input",0,0,0,0,{},0,modifiers,{},type);
    return;
  }
  if (key == "Tab") {
    std::vector<node_id> nodes;
    const auto visit = [&](auto &&self, dom_node &n) -> void {
      if (n.style.display == display_mode::none)
        return;
      if (focusable(n) && tab_index(n).value_or(0) >= 0)
        nodes.push_back(n.id);
      for (auto *c : n.children)
        self(self, *c);
    };
    visit(visit, state_->dom.body());
    std::stable_sort(nodes.begin(), nodes.end(), [&](node_id a, node_id b) {
      const auto left = tab_index(state_->node(a)).value_or(0);
      const auto right = tab_index(state_->node(b)).value_or(0);
      if ((left > 0) != (right > 0)) return left > 0;
      return left > 0 && left < right;
    });
    if (nodes.empty())
      return;
    auto it = std::ranges::find(nodes, state_->focus);
    auto index = it == nodes.end() ? (shift ? 0 : -1)
                                   : static_cast<int>(it - nodes.begin());
    focus(nodes[(index + (shift ? -1 : 1) + nodes.size()) % nodes.size()]);
  } else if ((key == "Enter" || key == " ") && state_->focus) {
    if (state_->node(state_->focus).tag == "button" &&
        !state_->node(state_->focus).attributes.contains("disabled"))
      dispatch(state_->focus, "click",0,0,0,0,{},0,modifiers);
  }
}
void document::set_external_canvas(node_id id, bool enabled) {
  auto &node = state_->node(id);
  if (node.tag != "canvas") throw std::invalid_argument("not a canvas");
  node.mutable_canvas().externally_composited = enabled;
  state_->dom.mark_scene_changed();
}
void document::clear_canvas(node_id id) {
  auto &n = state_->node(id);
  if (n.tag != "canvas")
    throw std::invalid_argument("not a canvas");
  auto &c = n.mutable_canvas();
  c.commands.clear();
  c.strings.clear();
  c.string_indices.clear();
  ++c.generation;
  state_->dom.mark_scene_changed();
}
void document::fill_rect(node_id id, float x, float y, float w, float h,
                         uint32_t rgba) {
  auto &n = state_->node(id);
  if (n.tag != "canvas")
    throw std::invalid_argument("not a canvas");
  webscene_canvas_command c{};
  c.kind = 22;
  c.data.values[0] = x;
  c.data.values[1] = y;
  c.data.values[2] = w;
  c.data.values[3] = h;
  auto &canvas = n.mutable_canvas();
  char color[10];
  std::snprintf(color, sizeof(color), "#%08x", rgba);
  webscene_canvas_command paint{};
  paint.kind = 40;
  paint.resource_id = static_cast<uint32_t>(canvas.strings.size());
  canvas.strings.emplace_back(color);
  canvas.commands.push_back(paint);
  canvas.commands.push_back(c);
  ++canvas.generation;
  state_->dom.mark_scene_changed();
}
void document::fill_text(node_id id, std::string text, float x, float y,
                         std::string font, uint32_t rgba,
                         std::string align, std::string baseline) {
  auto &node=state_->node(id);
  if(node.tag!="canvas") throw std::invalid_argument("not a canvas");
  if(!std::isfinite(x)||!std::isfinite(y)) return;
  auto &canvas=node.mutable_canvas();
  const auto string_command=[&](uint32_t kind,std::string value) {
    webscene_canvas_command command{};command.kind=kind;
    command.resource_id=static_cast<uint32_t>(canvas.strings.size());
    canvas.strings.push_back(std::move(value));canvas.commands.push_back(command);
  };
  char color[10];std::snprintf(color,sizeof(color),"#%08x",rgba);
  string_command(40,color);string_command(48,std::move(font));
  string_command(49,std::move(align));string_command(50,std::move(baseline));
  string_command(25,std::move(text));
  canvas.commands.back().data.values[0]=x;canvas.commands.back().data.values[1]=y;
  ++canvas.generation;state_->dom.mark_scene_changed();
}
static bool class_has(const std::string &list, const std::string &name) {
  size_t i = 0;
  while (i < list.size()) {
    auto b = list.find_first_not_of(" \t\r\n\f", i);
    if (b == std::string::npos)
      return false;
    auto e = list.find_first_of(" \t\r\n\f", b);
    if (list.substr(b, e - b) == name)
      return true;
    if (e == std::string::npos)
      return false;
    i = e;
  }
  return false;
}
static bool matches_part(const dom_node &n, const selector_part &p,
                         document_state &s) {
  for (const auto &excluded : p.excluded)
    if (matches_part(n, excluded, s)) return false;
  if (p.first_child || p.last_child || p.only_child) {
    const dom_node *first = nullptr, *last = nullptr;
    if (n.parent) {
      for (const auto *child : n.parent->children) {
        if (child->tag.starts_with("#")) continue;
        if (!first) first = child;
        last = child;
      }
    } else first = last = &n;
    if ((p.first_child || p.only_child) && first != &n) return false;
    if ((p.last_child || p.only_child) && last != &n) return false;
  }
  for (const auto &attribute : p.attributes) {
    auto found = n.attributes.find(attribute.name);
    if (found == n.attributes.end() || (attribute.equals && found->second != attribute.value)) return false;
  }
  if (p.root && n.id != s.dom.body().id) return false;
  if (!p.tag.empty() && p.tag != "*" && n.tag != p.tag)
    return false;
  if (!p.id.empty() &&
      (!n.attributes.contains("id") || n.attributes.at("id") != p.id))
    return false;
  for (const auto &c : p.classes)
    if (!n.attributes.contains("class") ||
        !class_has(n.attributes.at("class"), c))
      return false;
  if (p.focus_visible && (n.id != s.focus || !s.keyboard_modality))
    return false;
  if (p.focus && n.id != s.focus)
    return false;
  if (p.disabled && (!(n.tag == "button" || n.tag == "input" ||
      n.tag == "select" || n.tag == "textarea" || n.tag == "option" ||
      n.tag == "optgroup" || n.tag == "fieldset") ||
      !n.attributes.contains("disabled"))) return false;
  if (p.active) {
    auto *pressed = s.dom.find_by_native_id(s.pressed);
    while (pressed && pressed->id != n.id) pressed = pressed->parent;
    if (!pressed) return false;
  }
  if (p.hover) {
    auto *h = s.dom.find_by_native_id(s.hover);
    bool found = false;
    for (; h; h = h->parent)
      if (h->id == n.id)
        found = true;
    if (!found)
      return false;
  }
  return n.tag != "#text";
}
static bool matches(const dom_node *n, const selector &sel, size_t i,
                    document_state &s) {
  if (!n || !matches_part(*n, sel.parts[i], s))
    return false;
  if (!i)
    return true;
  if (sel.parts[i].relation == '>')
    return matches(n->parent, sel, i - 1, s);
  for (auto *p = n->parent; p; p = p->parent)
    if (matches(p, sel, i - 1, s))
      return true;
  return false;
}
const scene &document::render(float width, float height) {
  state_->check();
  if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 ||
      height <= 0)
    throw std::invalid_argument("invalid viewport");
  auto &s = *state_;
  auto &out = s.output;
  if (!s.styles_dirty && !s.dom.dirty() &&
      s.rendered_scene_generation == s.dom.scene_generation() &&
      out.width == width && out.height == height)
    return out;
  const auto cascade = [&](auto &&self, dom_node &n, const computed_variables &inherited) -> void {
    if(n.tag=="input" || n.tag=="textarea") forms::ensure_text_value(n);
    n.style = node_style{};
    n.style.display = native_default_display_for_node(n);
    if (n.parent) {
      const auto &inherited_bar=n.parent->style.scrollbar();
      if (inherited_bar.thumb_rgba != n.style.scrollbar().thumb_rgba ||
          inherited_bar.track_rgba != n.style.scrollbar().track_rgba) {
        auto &bar=n.style.mutable_scrollbar();
        bar.thumb_rgba=inherited_bar.thumb_rgba;
        bar.track_rgba=inherited_bar.track_rgba;
      }
    }
    struct candidate {
      bool important;
      uint64_t specificity;
      size_t order;
      const declaration *value;
    };
    std::vector<candidate> declarations;
    size_t order = 0;
    for (const auto &r : s.rules) {
      bool match = (!r.reduced_motion || *r.reduced_motion==s.reduced_motion) &&
                   width >= r.min_width && width <= r.max_width &&
                   height >= r.min_height && height <= r.max_height &&
                   (r.inline_target ? r.inline_target == n.id
                                    : !r.match.parts.empty() &&
                                          matches(&n, r.match,
                                                  r.match.parts.size() - 1, s));
      for (const auto &d : r.declarations) {
        if (match)
          declarations.push_back(
              {d.important,
               r.inline_target ? (1ull << 32) : r.match.specificity, order,
               &d});
        ++order;
      }
    }
    std::ranges::sort(declarations, {}, [](const auto &d) {
      return std::tuple(d.important, d.specificity, d.order);
    });
    specified_variables local;
    for (const auto &d : declarations)
      if (!d.value->custom_name.empty())
        local[d.value->custom_name] = d.value->custom_value;
    const auto variables = compute_variables(local, inherited);
    style writer(n.style, variables);
    for (const auto &d : declarations)
      if (d.value->custom_name.empty() && d.value->apply)
        d.value->apply(writer);
    auto x = n.style.overflow_x, y = n.style.overflow_y;
    const auto scrollable = [](overflow_mode value) {
      return value != overflow_mode::visible && value != overflow_mode::clip;
    };
    if (x == overflow_mode::visible && scrollable(y)) x = overflow_mode::automatic;
    else if (x == overflow_mode::clip && scrollable(y)) x = overflow_mode::hidden;
    if (y == overflow_mode::visible && scrollable(x)) y = overflow_mode::automatic;
    else if (y == overflow_mode::clip && scrollable(x)) y = overflow_mode::hidden;
    if (x == overflow_mode::visible || x == overflow_mode::clip) n.scroll_left = 0;
    if (y == overflow_mode::visible || y == overflow_mode::clip) n.scroll_top = 0;
    n.style.overflow_x = x;
    n.style.overflow_y = y;
    n.style.clip = x != overflow_mode::visible || y != overflow_mode::visible;
    n.style.scroll_x_enabled = x == overflow_mode::automatic || x == overflow_mode::scroll;
    n.style.scroll_y_enabled = y == overflow_mode::automatic || y == overflow_mode::scroll;
    s.dom.update_style_animations(n);
    for (auto *c : n.children)
      self(self, *c, variables);
  };
  if (s.styles_dirty || out.width != width || out.height != height) {
    if(s.resolver)
      s.resolver->resolve(s.dom,{width,height,s.hover,s.focus,s.keyboard_modality,s.reduced_motion});
    else
      cascade(cascade, s.dom.body(), {});
    auto events=s.dom.take_transition_events();
    s.pending_transition_events.insert(s.pending_transition_events.end(),
        std::make_move_iterator(events.begin()),std::make_move_iterator(events.end()));
    s.styles_dirty = false;
    s.dom.mark_dirty();
  }
  if (s.dom.dirty() || out.width != width || out.height != height)
    s.dom.layout(width, height);
  out.commands.clear();
  out.strings.clear();
  out.bytes.clear();
  out.layers.clear();
  out.canvas.clear();
  s.dom.build_scene(out.commands, out.strings, out.bytes, true);
  std::vector<webscene_scene_string> canvas_strings;
  std::vector<char> canvas_bytes;
  s.dom.build_canvas_display_lists(out.layers, out.canvas, canvas_strings,
                                   canvas_bytes);
  for (auto &layer : out.layers)
    layer.string_offset += static_cast<uint32_t>(out.strings.size());
  for (auto ref : canvas_strings) {
    ref.byte_offset += static_cast<uint32_t>(out.bytes.size());
    out.strings.push_back(ref);
  }
  out.bytes.insert(out.bytes.end(), canvas_bytes.begin(), canvas_bytes.end());
  out.width = width;
  out.height = height;
  s.rendered_scene_generation = s.dom.scene_generation();
  ++out.revision;
  return out;
}
uint64_t document::layout_passes() const {
  state_->check();
  return state_->dom.layout_passes();
}
layout_rect document::bounds(node_id id) const {
  return state_->node(id).layout;
}
std::string document::text_content(node_id id) const {
  std::string result;
  const auto visit = [&](auto &&self, dom_node &n) -> void {
    result += n.text_content;
    for (auto *c : n.children)
      self(self, *c);
  };
  visit(visit, state_->node(id));
  return result;
}
} // namespace webscene::native_web
