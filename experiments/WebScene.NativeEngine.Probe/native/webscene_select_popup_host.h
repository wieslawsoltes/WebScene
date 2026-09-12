#pragma once

#include <cstdint>

namespace webscene_native {

// Hosting is intentionally separate from select behavior/presentation. The
// default implementation keeps the WebScene-rendered popup in the current
// surface. A platform host may later place the same rendered popup scene in a
// native popup window without becoming the control implementation.
enum class select_popup_host_kind : uint8_t {
    in_surface = 0,
    platform_surface = 1,
};

struct select_popup_anchor final {
    float x{};
    float y{};
    float width{};
    float height{};
    float viewport_width{};
    float viewport_height{};
    double display_scale{1.0};
};

struct select_popup_host_token final {
    uint64_t document_generation{};
    uint32_t select_node_id{};
    uint32_t popup_generation{};
};

// This is the engine-side service boundary, not an OS-control abstraction.
// Popup scene content, option state, keyboard behavior and DOM events remain
// owned by WebScene. A platform implementation is only responsible for where
// the WebScene rendering surface lives and for returning routed input/lifetime
// notifications to the owning engine generation.
class select_popup_host {
public:
    virtual ~select_popup_host() = default;
    [[nodiscard]] virtual select_popup_host_kind kind() const noexcept = 0;
    virtual bool open(select_popup_host_token token,
                      const select_popup_anchor& anchor) = 0;
    virtual void reposition(select_popup_host_token token,
                            const select_popup_anchor& anchor) = 0;
    virtual void close(select_popup_host_token token) noexcept = 0;
};

} // namespace webscene_native
