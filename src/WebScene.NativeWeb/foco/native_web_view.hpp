#pragma once
#include <cstring>
#include <foco/platform.hpp>
#include <foco/webscene_packet.hpp>
#include <webscene/native_web.hpp>

namespace webscene::foco_host {
// One composition surface hosts the entire native DOM; no per-element Foco
// controls.
class view final : public foco::control {
public:
  native_web::document document;
  view() {
    set_clip_to_bounds(true);
    set_focusable(true);
  }
  void set_gpu_image(native_web::node_id node, uint32_t width, uint32_t height,
                     uint64_t generation,
                     std::shared_ptr<const foco::composition_resource_attachment> owner) {
    if (gpu_node_ != node) {
      if (gpu_node_) document.set_external_canvas(gpu_node_, false);
      document.set_external_canvas(node, true);
    }
    gpu_node_ = node; gpu_width_ = width; gpu_height_ = height;
    gpu_generation_ = generation; gpu_owner_ = std::move(owner);
    gpu_dirty_ = true;
    refresh();
  }
  bool requires_host_frames() const noexcept override {
    if(document.disposed()) return !packet_.empty() || bool(gpu_owner_);
    return input_refresh_pending_ || host_reduced_motion()!=reduced_motion_ || document.has_active_animations();
  }
  bool advance_host_frame(double timestamp_ms) override {
    if (!requires_host_frames()) return false;
    const auto previous = revision_;
    if(!document.disposed()) document.advance_animations(timestamp_ms);
    refresh();
    return revision_ != previous;
  }
  void refresh() {
    input_refresh_pending_ = false;
    if(document.disposed()) {
      if(!packet_.empty() || gpu_owner_) {
        packet_.clear();gpu_owner_.reset();gpu_node_=0;gpu_dirty_=false;
        ++revision_;invalidate_render();
      }
      clear_cursor();
      return;
    }
    auto b = bounds();
    if (b.width <= 0 || b.height <= 0)
      return;
    reduced_motion_=host_reduced_motion();
    document.set_reduced_motion(reduced_motion_);
    const auto &scene = document.render(b.width, b.height);
    update_cursor();
    if (scene.revision == document_revision_ && !gpu_dirty_)
      return;
    namespace packet = foco::webscene_packet;
    static_assert(sizeof(webscene_scene_command) ==
                  sizeof(packet::dom_command));
    static_assert(sizeof(webscene_canvas_command) ==
                  sizeof(packet::canvas_command));
    static_assert(sizeof(webscene_scene_string) == sizeof(packet::string_ref));
    std::vector<packet::layer> layers;
    for (const auto &l : scene.layers)
      layers.push_back({l.node_id, packet::layer_replace, l.command_offset,
                        l.command_count, l.string_offset, l.string_count,
                        l.flags, l.x, l.y, l.width, l.height, l.bitmap_width,
                        l.bitmap_height, l.generation});
    auto commands = scene.commands;
    if (gpu_owner_ && gpu_node_) {
      const auto area = document.bounds(gpu_node_);
      std::erase_if(layers, [&](const auto& layer){ return layer.node_id == gpu_node_; });
      for (auto &command : commands) {
        if (command.kind != 257 || command.node_id != gpu_node_) continue;
        command.kind = 256;
        command.rgba = 0;
        command.x = area.x; command.y = area.y;
        command.width = area.width; command.height = area.height;
      }
    }
    packet::header h{};
    h.magic_value = packet::magic;
    h.version_value = packet::version;
    h.flags = packet::checkpoint | packet::dom_replacement;
    h.revision = ++revision_;
    h.viewport_width = scene.width;
    h.viewport_height = scene.height;
    packet_.assign(sizeof(h), std::byte{});
    const auto append = [&](const auto &data) -> uint32_t {
      using T = typename std::decay_t<decltype(data)>::value_type;
      packet_.resize((packet_.size() + alignof(T) - 1) & ~(alignof(T) - 1));
      auto offset = packet_.size();
      packet_.resize(offset + data.size() * sizeof(T));
      if (!data.empty())
        std::memcpy(packet_.data() + offset, data.data(),
                    data.size() * sizeof(T));
      return static_cast<uint32_t>(offset);
    };
    h.dom_command_count = commands.size();
    h.dom_command_offset = append(commands);
    h.layer_count = layers.size();
    h.layer_offset = append(layers);
    h.canvas_command_count = scene.canvas.size();
    h.canvas_command_offset = append(scene.canvas);
    h.string_count = scene.strings.size();
    h.string_offset = append(scene.strings);
    h.string_byte_count = scene.bytes.size();
    h.string_byte_offset = append(scene.bytes);
    h.byte_count = packet_.size();
    std::memcpy(packet_.data(), &h, sizeof(h));
    document_revision_ = scene.revision;
    gpu_dirty_ = false;
    invalidate_render();
  }
  std::optional<foco::composition_command_stream_view>
  composition_command_stream() const noexcept override {
    if (document.disposed() || packet_.empty())
      return std::nullopt;
    return foco::composition_command_stream_view{
        packet_, revision_,
        static_cast<uint32_t>(foco::command_stream_format::webscene_scene_v1),
        static_cast<uint32_t>(std::max(1.f, bounds().width)),
        static_cast<uint32_t>(std::max(1.f, bounds().height)), gpu_owner_};
  }
  foco::size measure_override(foco::size available) override {
    return {std::isfinite(available.width) ? available.width : 1000.f,
            std::isfinite(available.height) ? available.height : 700.f};
  }
  void arrange_override(foco::size size) override {
    if(document.disposed()) {refresh();return;}
    document.render(std::max(1.f, size.width), std::max(1.f, size.height));
    refresh();
  }
  void pointer_event_received(foco::pointer_event &e) override {
    if(document.disposed()) {refresh();return;}
    const webscene::native_web::input_modifiers modifiers{
      foco::has_modifier(e.modifiers,foco::key_modifiers::shift),
      foco::has_modifier(e.modifiers,foco::key_modifiers::control),
      foco::has_modifier(e.modifiers,foco::key_modifiers::alt),
      foco::has_modifier(e.modifiers,foco::key_modifiers::platform)};
    pointer_position_=foco::point{e.position.x-bounds().x,e.position.y-bounds().y};
    update_cursor();
    if(e.kind==foco::pointer_event_kind::wheel){
      document.wheel(e.position.x-bounds().x,e.position.y-bounds().y,
                     -e.wheel_delta*(e.wheel_is_precise?1.f:48.f),modifiers);
      request_input_refresh();e.handled=true;return;
    }
    auto type = e.kind == foco::pointer_event_kind::pressed    ? "pointerdown"
                : e.kind == foco::pointer_event_kind::cancelled ? "pointercancel"
                : e.kind == foco::pointer_event_kind::released ? "pointerup"
                                                               : "pointermove";
    document.pointer(type, e.position.x - bounds().x,
                     e.position.y - bounds().y, e.buttons,modifiers);
    request_input_refresh();
    e.handled = true;
  }
  bool try_move_focus_within(bool reverse) override {
    if(document.disposed()) return false;
    document.key("Tab", reverse);
    refresh();
    return !document.disposed() && document.focused() != 0;
  }
  void key_event_received(foco::key_event &e) override {
    if(document.disposed()) {refresh();return;}
    std::string_view key = e.value == foco::key::tab     ? "Tab"
                           : e.value == foco::key::enter ? "Enter"
                           : e.value == foco::key::space ? " "
                                                         : "";
    if (!key.empty()) {
      document.key(key,webscene::native_web::input_modifiers{
        foco::has_modifier(e.modifiers,foco::key_modifiers::shift),
        foco::has_modifier(e.modifiers,foco::key_modifiers::control),
        foco::has_modifier(e.modifiers,foco::key_modifiers::alt),
        foco::has_modifier(e.modifiers,foco::key_modifiers::platform)});
      request_input_refresh();
      e.handled = true;
    }
  }
  std::string_view type_name() const noexcept override {
    return "NativeWebView";
  }

private:
  void update_cursor() {
    if (pointer_position_) {
      const auto cursor=document.cursor_at(pointer_position_->x,pointer_position_->y);
      set_cursor(cursor=="pointer" ? foco::cursor_kind::hand :
          cursor=="grab" ? foco::cursor_kind::grab :
          cursor=="grabbing" ? foco::cursor_kind::grabbing :
          cursor=="not-allowed" ? foco::cursor_kind::not_allowed :
          cursor=="text" ? foco::cursor_kind::ibeam :
          cursor=="crosshair" ? foco::cursor_kind::cross :
          cursor=="col-resize" || cursor=="ew-resize" ? foco::cursor_kind::size_west_east :
          cursor=="row-resize" || cursor=="ns-resize" ? foco::cursor_kind::size_north_south :
          foco::cursor_kind::arrow);
    }
  }
  bool host_reduced_motion() const noexcept {
    const auto *sink=visual_animations();
    return sink && sink->reduced_motion();
  }
  bool reduced_motion_{};
  bool input_refresh_pending_{};
  std::optional<foco::point> pointer_position_;
  void request_input_refresh() {
    if (input_refresh_pending_) return;
    input_refresh_pending_ = true;
    // Wake the existing compositor driver; subsequent input coalesces until
    // advance_host_frame generates the latest document packet.
    invalidate_render();
  }
  std::vector<std::byte> packet_;
  uint64_t revision_{}, document_revision_{}, gpu_generation_{};
  uint32_t gpu_node_{}, gpu_width_{}, gpu_height_{};
  bool gpu_dirty_{};
  std::shared_ptr<const foco::composition_resource_attachment> gpu_owner_;
};
} // namespace webscene::foco_host
