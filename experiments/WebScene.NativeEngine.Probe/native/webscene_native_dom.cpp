#include "webscene_native_dom.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <sstream>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_set>

namespace webscene_native {
namespace {

bool is_out_of_flow(position_mode position) noexcept
{
    return position == position_mode::absolute || position == position_mode::fixed;
}

bool is_flex_container(display_mode display) noexcept
{
    return display == display_mode::flex || display == display_mode::inline_flex;
}

bool is_grid_container(display_mode display) noexcept
{
    return display == display_mode::grid || display == display_mode::inline_grid;
}

bool is_inline_level(display_mode display) noexcept
{
    return display == display_mode::inline_flow
        || display == display_mode::inline_block
        || display == display_mode::inline_flex
        || display == display_mode::inline_grid
        || display == display_mode::inline_table;
}

bool pseudo_generates_box(const node_style::pseudo_element& pseudo) noexcept
{
    return pseudo.generated && !pseudo.display_none && !pseudo.visibility_hidden;
}

std::string resolved_list_style(const dom_node& node, bool position)
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        const auto& value = position
            ? current->style.textual().list_style_position
            : current->style.textual().list_style_type;
        if (!value.empty() && value != "inherit" && value != "unset") return value;
    }
    if (position) return "outside";
    return node.parent != nullptr && node.parent->tag == "ol" ? "decimal" : "disc";
}

std::string list_marker_text(const dom_node& node)
{
    if (node.tag != "li" && node.style.display != display_mode::list_item) {
        return {};
    }
    const auto type = resolved_list_style(node, false);
    if (type == "none") return {};
    if (type != "decimal" && type != "decimal-leading-zero") {
        return type == "square" ? "\xE2\x96\xAA " : "\xE2\x80\xA2 ";
    }

    auto value = 1;
    if (node.parent != nullptr) {
        if (const auto start = node.parent->attributes.find("start");
            start != node.parent->attributes.end()) {
            value = std::max(1, std::atoi(start->second.c_str()));
        }
        for (const auto* sibling : node.parent->children) {
            if (sibling == &node) break;
            if (sibling->tag == "li"
                || (node.tag != "li" && sibling->style.display == display_mode::list_item)) {
                ++value;
            }
        }
    }
    if (const auto authored = node.attributes.find("value");
        authored != node.attributes.end()) {
        value = std::atoi(authored->second.c_str());
    }
    std::ostringstream marker;
    if (type == "decimal-leading-zero" && value >= 0 && value < 10) marker << '0';
    marker << value << ". ";
    return marker.str();
}

dom_node make_list_marker_layout_node(const dom_node& originating, std::string content)
{
    dom_node result{};
    result.id = originating.id;
    result.tag = "#text";
    result.text_content = std::move(content);
    result.parent = const_cast<dom_node*>(&originating);
    result.visible = originating.visible;
    result.style.display = display_mode::inline_flow;
    return result;
}

dom_node make_pseudo_layout_node(
    const dom_node& originating,
    const node_style::pseudo_element& pseudo)
{
    dom_node result{};
    result.id = originating.id;
    result.tag = "#text";
    result.text_content = pseudo.content;
    result.parent = const_cast<dom_node*>(&originating);
    result.visible = originating.visible && pseudo_generates_box(pseudo);
    result.generated_pseudo_box = true;
    result.style.width = pseudo.width;
    result.style.height = pseudo.height;
    result.style.left = pseudo.left;
    result.style.top = pseudo.top;
    result.style.right = pseudo.right;
    result.style.bottom = pseudo.bottom;
    result.style.margin_left = pseudo.margin_left;
    result.style.margin_top = pseudo.margin_top;
    result.style.margin_right = pseudo.margin_right;
    result.style.margin_bottom = pseudo.margin_bottom;
    result.style.border_left_width = pseudo.border_left_width;
    result.style.border_top_width = pseudo.border_top_width;
    result.style.border_right_width = pseudo.border_right_width;
    result.style.border_bottom_width = pseudo.border_bottom_width;
    result.style.border_box = pseudo.border_box;
    result.style.display = pseudo.display_none ? display_mode::none : pseudo.display;
    result.style.position = pseudo.position;
    result.style.align_self = pseudo.align_self;
    result.style.align_self_specified = pseudo.align_self_specified;
    result.style.z_index = pseudo.z_index;
    result.style.font_size = pseudo.font_size;
    result.style.line_height = pseudo.line_height;
    return result;
}

bool is_table_container(display_mode display) noexcept
{
    return display == display_mode::table || display == display_mode::inline_table;
}

bool is_table_row_group(display_mode display) noexcept
{
    return display == display_mode::table_row_group
        || display == display_mode::table_header_group
        || display == display_mode::table_footer_group;
}

bool is_table_track(display_mode display) noexcept
{
    return display == display_mode::table_column
        || display == display_mode::table_column_group;
}

bool has_table_row_descendant(const native_document& document, const dom_node& node)
{
    for (const auto* child : document.composed_children(node)) {
        if (child->style.display == display_mode::table_row) return true;
        if (is_table_row_group(child->style.display)
            && has_table_row_descendant(document, *child)) {
            return true;
        }
    }
    return false;
}

struct paint_z_index_update final {
    int32_t z_index{0};
    bool contains_retained_canvas{false};
};

paint_z_index_update update_paint_z_index(
    const native_document& document,
    dom_node& node) noexcept
{
    auto descendant_z_index = 0;
    // Canvas paint order is structural. A width/height reset clears its
    // display list before the application redraws, but it does not move the
    // element or an authored ::after overlay to a different CSS paint phase.
    // Basing this bit on the current command list made the cached layout state
    // alternate between backdrop and overlay across ordinary chart redraws.
    auto contains_retained_canvas = node.tag == "canvas"
        && node.visible
        && node.style.display != display_mode::none;
    for (auto* child : document.composed_children(node)) {
        const auto child_update = update_paint_z_index(document, *child);
        descendant_z_index = std::max(
            descendant_z_index,
            child_update.z_index);
        contains_retained_canvas =
            contains_retained_canvas || child_update.contains_retained_canvas;
    }
    const auto establishes_atomic_stacking_context =
        node.style.position == position_mode::fixed
        || node.style.opacity < 0.999F
        || node.style.transform_rotate_degrees != 0
        || node.style.transform_scale_x != 1.0F
        || node.style.transform_scale_y != 1.0F;
    node.paint_z_index = node.style.z_index != 0
        ? node.style.z_index
        // Relative/absolute positioning with z-index:auto does not establish a
        // stacking context. Portal wrappers commonly use that shape, so their
        // positioned tooltip descendants still participate in the ancestor
        // context. Fixed/transform/opacity contexts and retained canvases remain
        // atomic composition boundaries.
        : !establishes_atomic_stacking_context && !contains_retained_canvas
            ? descendant_z_index : 0;
    node.contains_retained_canvas = contains_retained_canvas;
    return {node.paint_z_index, contains_retained_canvas};
}

void collect_positive_stacking_nodes(
    const native_document& document,
    dom_node& node,
    std::vector<dom_node*>& result)
{
    if (node.style.z_index > 0) {
        result.push_back(&node);
    }
    for (auto* child : document.composed_children(node)) {
        collect_positive_stacking_nodes(document, *child, result);
    }
}

void collect_fixed_positioned_nodes(
    const native_document& document,
    dom_node& node,
    std::vector<dom_node*>& result)
{
    if (node.style.position == position_mode::fixed) {
        result.push_back(&node);
    }
    for (auto* child : document.composed_children(node)) {
        collect_fixed_positioned_nodes(document, *child, result);
    }
}

size_t count_retained_canvases(
    const native_document& document,
    const dom_node& node)
{
    // Count visible canvas elements rather than non-empty display lists. The
    // latter can be transiently empty between reset and redraw and must not
    // change the stable backdrop/canvas/overlay partition.
    auto count = node.tag == "canvas"
        && node.visible
        && node.style.display != display_mode::none
        ? size_t{1U}
        : size_t{0U};
    for (const auto* child : document.composed_children(node)) {
        count += count_retained_canvases(document, *child);
    }
    return count;
}

void update_retained_canvas_paint_phase(
    const native_document& document,
    dom_node& node,
    bool& retained_canvas_seen,
    size_t& retained_canvases_remaining)
{
    // The presenter has one DOM backdrop before all retained canvases and one
    // overlay after them. Normal DOM between canvases must stay in the backdrop
    // so a later canvas can paint above its own host background. Only DOM after
    // the final canvas can safely use the global overlay by document order.
    node.paints_after_retained_canvas =
        retained_canvas_seen && retained_canvases_remaining == 0U;
    if (node.tag == "canvas"
        && node.visible
        && node.style.display != display_mode::none) {
        retained_canvas_seen = true;
        if (retained_canvases_remaining != 0U) {
            --retained_canvases_remaining;
        }
    }
    const auto& composed_paint_order = document.composed_children(node);
#if WEBSCENE_NATIVE_ENGINE_RETAINED_PAINT_ORDER_CONTROL
    auto paint_order = composed_paint_order;
    const auto requires_sort = std::any_of(
        paint_order.begin(),
        paint_order.end(),
        [](const auto* child) { return child->style.z_index != 0; });
#else
    const auto requires_sort = std::any_of(
        composed_paint_order.begin(),
        composed_paint_order.end(),
        [](const auto* child) { return child->style.z_index != 0; });
    if (!requires_sort) {
        for (auto* child : composed_paint_order) {
            update_retained_canvas_paint_phase(
                document,
                *child,
                retained_canvas_seen,
                retained_canvases_remaining);
        }
        return;
    }
    auto paint_order = composed_paint_order;
#endif
    if (requires_sort) {
        std::stable_sort(
            paint_order.begin(),
            paint_order.end(),
            [](const auto* left, const auto* right) {
                // A descendant stacking level is scoped to its ancestor; it
                // must not reorder the ancestor among its own siblings.
                return left->paint_z_index < right->paint_z_index;
            });
    }
    for (auto* child : paint_order) {
        update_retained_canvas_paint_phase(
            document,
            *child,
            retained_canvas_seen,
            retained_canvases_remaining);
    }
}

bool display_tree_allows_render(
    const native_document& document,
    const dom_node& node) noexcept
{
    for (auto* current = &node; current != nullptr;
        current = document.composed_parent(*current)) {
        if (current->style.display == display_mode::none) return false;
    }
    return true;
}

bool stacking_ancestors_allow_hit(
    const native_document& document,
    const dom_node& node,
    const dom_node& root,
    float x,
    float y,
    bool& visibility_hidden,
    bool& pointer_events_none)
{
    std::vector<const dom_node*> ancestors;
    for (auto* ancestor = document.composed_parent(node);
        ancestor != nullptr;
        ancestor = document.composed_parent(*ancestor)) {
        ancestors.push_back(ancestor);
        if (ancestor == &root) break;
    }
    if (ancestors.empty() || ancestors.back() != &root) return false;
    for (auto iterator = ancestors.rbegin(); iterator != ancestors.rend(); ++iterator) {
        const auto& ancestor = **iterator;
        if (ancestor.style.display == display_mode::none) return false;
        visibility_hidden = ancestor.style.visibility_specified
            ? ancestor.style.visibility_hidden
            : visibility_hidden;
        pointer_events_none = ancestor.style.pointer_events_specified
            ? ancestor.style.pointer_events_none
            : pointer_events_none;
    }
    // A viewport-fixed box is taken out of each untransformed ancestor's
    // overflow clip. Portal implementations deliberately place a fixed child
    // menu inside an overflow:auto parent menu, so applying every DOM ancestor
    // clip here makes the child fall through to content behind the portal.
    auto escaped_to_viewport = node.style.position == position_mode::fixed;
    for (const auto* ancestor : ancestors) {
        // Document hit testing is viewport-based. A body/root box can have a
        // zero content height when every visible child is positioned; its own
        // overflow must not replace the browsing-context viewport clip.
        if (!escaped_to_viewport && ancestor != &root && ancestor->style.clip) {
            const auto inside = x >= ancestor->layout.x && y >= ancestor->layout.y
                && x <= ancestor->layout.x + ancestor->layout.width
                && y <= ancestor->layout.y + ancestor->layout.height;
            if (!inside) return false;
        }
        if (ancestor->style.position == position_mode::fixed) {
            escaped_to_viewport = true;
        }
    }
    return true;
}

float parse_number(std::string_view value, float fallback = 0)
{
    std::string copy(value);
    char* end = nullptr;
    const auto result = std::strtof(copy.c_str(), &end);
    return end != copy.c_str() && std::isfinite(result) ? result : fallback;
}

enum class calc_unit : uint8_t {
    number,
    pixels,
    percent,
    viewport_width,
    viewport_height,
    invalid
};

struct calc_value final {
    double value{0};
    calc_unit unit{calc_unit::invalid};
    double pixel_offset{0};
    enum class bound_kind : uint8_t { none, minimum, maximum } bound{bound_kind::none};
    double pixel_bound{0};
};

class calc_parser final {
public:
    explicit calc_parser(std::string_view source) : source_(source) {}

    std::optional<calc_value> parse()
    {
        auto result = expression();
        whitespace();
        return result.unit != calc_unit::invalid && position_ == source_.size()
            ? std::optional<calc_value>(result)
            : std::nullopt;
    }

private:
    calc_value expression()
    {
        auto result = term();
        while (result.unit != calc_unit::invalid) {
            whitespace();
            if (!consume('+') && !consume('-')) break;
            const auto operation = source_[position_ - 1U];
            auto right = term();
            result = add(result, right, operation == '-' ? -1.0 : 1.0);
        }
        return result;
    }

    calc_value term()
    {
        auto result = factor();
        while (result.unit != calc_unit::invalid) {
            whitespace();
            if (!consume('*') && !consume('/')) break;
            const auto operation = source_[position_ - 1U];
            auto right = factor();
            result = operation == '*' ? multiply(result, right) : divide(result, right);
        }
        return result;
    }

    calc_value factor()
    {
        whitespace();
        if (consume('+')) return factor();
        if (consume('-')) {
            auto value = factor();
            value.value = -value.value;
            return value;
        }
        if (consume('(')) {
            auto value = expression();
            whitespace();
            return consume(')') ? value : invalid();
        }
        if (position_ < source_.size()
            && (std::isalpha(static_cast<unsigned char>(source_[position_]))
                || source_[position_] == '-')) {
            const auto start = position_;
            while (position_ < source_.size()
                && (std::isalnum(static_cast<unsigned char>(source_[position_]))
                    || source_[position_] == '-')) ++position_;
            const auto name = source_.substr(start, position_ - start);
            whitespace();
            if (!consume('(')) return invalid();
            if (name == "calc") {
                auto value = expression();
                whitespace();
                return consume(')') ? value : invalid();
            }
            if (name == "max" || name == "min") {
                auto result = expression();
                whitespace();
                while (consume(',')) {
                    auto next = expression();
                    if (!combine_extrema(result, next, name == "max")) return invalid();
                    whitespace();
                }
                return consume(')') ? result : invalid();
            }
            return invalid();
        }

        const auto start = position_;
        char* end = nullptr;
        std::string remaining(source_.substr(position_));
        const auto number = std::strtod(remaining.c_str(), &end);
        if (end == remaining.c_str() || !std::isfinite(number)) return invalid();
        position_ += static_cast<size_t>(end - remaining.c_str());
        auto unit = calc_unit::number;
        if (source_.substr(position_).starts_with("px")) {
            position_ += 2U;
            unit = calc_unit::pixels;
        } else if (source_.substr(position_).starts_with("vw")) {
            position_ += 2U;
            unit = calc_unit::viewport_width;
        } else if (source_.substr(position_).starts_with("vh")) {
            position_ += 2U;
            unit = calc_unit::viewport_height;
        } else if (position_ < source_.size() && source_[position_] == '%') {
            ++position_;
            unit = calc_unit::percent;
        }
        static_cast<void>(start);
        return {number, unit};
    }

    static bool viewport_unit(calc_unit unit)
    {
        return unit == calc_unit::viewport_width
            || unit == calc_unit::viewport_height;
    }

    static bool combine_extrema(calc_value& result, calc_value next, bool maximum)
    {
        if (result.unit == next.unit || result.value == 0 || next.value == 0) {
                    if (result.unit == calc_unit::number && result.value == 0
                        && next.unit != calc_unit::number) result.unit = next.unit;
                    if (next.unit == calc_unit::number && next.value == 0
                        && result.unit != calc_unit::number) next.unit = result.unit;
            const auto next_is_selected = maximum
                        ? next.value > result.value
                            || (next.value == result.value
                                && next.pixel_offset > result.pixel_offset)
                        : next.value < result.value
                            || (next.value == result.value
                                && next.pixel_offset < result.pixel_offset);
            if (next_is_selected) result = next;
            return true;
        }

        auto* viewport = viewport_unit(result.unit) ? &result
            : viewport_unit(next.unit) ? &next : nullptr;
        const auto* pixels = result.unit == calc_unit::pixels ? &result
            : next.unit == calc_unit::pixels ? &next : nullptr;
        if (viewport == nullptr || pixels == nullptr
            || viewport->pixel_offset != 0
            || pixels->bound != calc_value::bound_kind::none) return false;
        const auto requested_bound = maximum
            ? calc_value::bound_kind::maximum
            : calc_value::bound_kind::minimum;
        if (viewport->bound != calc_value::bound_kind::none
            && viewport->bound != requested_bound) return false;
        if (viewport->bound == calc_value::bound_kind::none) {
            viewport->bound = requested_bound;
            viewport->pixel_bound = pixels->value;
        } else {
            viewport->pixel_bound = maximum
                ? std::max(viewport->pixel_bound, pixels->value)
                : std::min(viewport->pixel_bound, pixels->value);
        }
        result = *viewport;
        return true;
    }

    static bool compatible(calc_value left, calc_value right)
    {
        return left.unit == right.unit
            || left.value == 0
            || right.value == 0;
    }

    static bool add_compatible(calc_value left, calc_value right)
    {
        return compatible(left, right)
            || (left.unit == calc_unit::percent && right.unit == calc_unit::pixels)
            || (left.unit == calc_unit::pixels && right.unit == calc_unit::percent)
            || (left.unit == calc_unit::viewport_width && right.unit == calc_unit::pixels)
            || (left.unit == calc_unit::pixels && right.unit == calc_unit::viewport_width)
            || (left.unit == calc_unit::viewport_height && right.unit == calc_unit::pixels)
            || (left.unit == calc_unit::pixels && right.unit == calc_unit::viewport_height);
    }

    static calc_value add(calc_value left, calc_value right, double sign)
    {
        if (!add_compatible(left, right)) return invalid();
        if ((left.unit == calc_unit::percent
                || left.unit == calc_unit::viewport_width
                || left.unit == calc_unit::viewport_height)
            && right.unit == calc_unit::pixels) {
            left.pixel_offset += right.value * sign;
            return left;
        }
        if (left.unit == calc_unit::pixels
            && (right.unit == calc_unit::percent
                || right.unit == calc_unit::viewport_width
                || right.unit == calc_unit::viewport_height)) {
            right.value *= sign;
            right.pixel_offset = left.value + right.pixel_offset * sign;
            return right;
        }
        if (left.value == 0 && left.unit != right.unit) left.unit = right.unit;
        if (right.value == 0 && right.unit != left.unit) right.unit = left.unit;
        left.value += right.value * sign;
        left.pixel_offset += right.pixel_offset * sign;
        return left;
    }

    static calc_value multiply(calc_value left, calc_value right)
    {
        if (left.unit != calc_unit::number && right.unit != calc_unit::number) return invalid();
        if (left.unit == calc_unit::number) {
            right.value *= left.value;
            right.pixel_offset *= left.value;
            return right;
        }
        left.value *= right.value;
        left.pixel_offset *= right.value;
        return left;
    }

    static calc_value divide(calc_value left, calc_value right)
    {
        if (right.unit != calc_unit::number || std::abs(right.value) < 1e-12) return invalid();
        left.value /= right.value;
        left.pixel_offset /= right.value;
        return left;
    }

    static calc_value invalid() { return {0, calc_unit::invalid}; }

    void whitespace()
    {
        while (position_ < source_.size()
            && std::isspace(static_cast<unsigned char>(source_[position_]))) ++position_;
    }

    bool consume(char expected)
    {
        whitespace();
        if (position_ >= source_.size() || source_[position_] != expected) return false;
        ++position_;
        return true;
    }

    std::string_view source_;
    size_t position_{0};
};

uint8_t parse_hex_pair(char high, char low)
{
    const auto digit = [](char value) -> uint8_t {
        if (value >= '0' && value <= '9') return static_cast<uint8_t>(value - '0');
        if (value >= 'a' && value <= 'f') return static_cast<uint8_t>(value - 'a' + 10);
        if (value >= 'A' && value <= 'F') return static_cast<uint8_t>(value - 'A' + 10);
        return 0;
    };
    return static_cast<uint8_t>((digit(high) << 4U) | digit(low));
}

uint32_t resolved_foreground(const dom_node& node)
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        const auto* animation = current->animation_runtime();
        if (animation != nullptr && animation->color_animation_active) {
            return animation->painted_foreground_rgba;
        }
        if ((current->style.foreground_rgba & 0xFFU) != 0U) {
            return animation != nullptr && animation->color_animation_initialized
                ? animation->painted_foreground_rgba
                : current->style.foreground_rgba;
        }
    }
    return 0xD1D4DCFFU;
}

void append_xml_escaped(std::string_view value, std::string& output, bool attribute)
{
    for (const auto character : value) {
        switch (character) {
        case '&': output += "&amp;"; break;
        case '<': output += "&lt;"; break;
        case '>': output += "&gt;"; break;
        case '"': output += attribute ? "&quot;" : "\""; break;
        case '\'': output += attribute ? "&apos;" : "'"; break;
        default: output.push_back(character); break;
        }
    }
}

void append_resolved_svg_color(const dom_node& node, std::string& output)
{
    const auto rgba = resolved_foreground(node);
    char color[10]{};
    std::snprintf(
        color,
        sizeof(color),
        "#%02x%02x%02x",
        static_cast<unsigned>((rgba >> 24U) & 0xFFU),
        static_cast<unsigned>((rgba >> 16U) & 0xFFU),
        static_cast<unsigned>((rgba >> 8U) & 0xFFU));
    output += color;
}

void append_svg_paint_value(
    const dom_node& node,
    std::string_view value,
    std::string& output)
{
    auto lower_value = std::string(value);
    std::transform(
        lower_value.begin(),
        lower_value.end(),
        lower_value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    if (lower_value == "currentcolor") {
        append_resolved_svg_color(node, output);
    } else {
        append_xml_escaped(value, output, true);
    }
}

void serialize_svg_subtree(const dom_node& node, std::string& output, bool root)
{
    if (node.tag == "#text") {
        append_xml_escaped(node.text_content, output, false);
        return;
    }
    if (node.tag.empty() || node.tag.front() == '#') return;

    output.push_back('<');
    output += node.tag;
    bool has_xmlns = false;
    bool has_id = false;
    bool has_class = false;
    bool has_color = false;
    const auto css_fill = node.style.textual().svg_fill;
    const auto css_stroke = node.style.textual().svg_stroke;
    for (const auto& [name, value] : node.attributes) {
        if (name == "xmlns") has_xmlns = true;
        else if (name == "id") has_id = true;
        else if (name == "class") has_class = true;
        else if (name == "color") {
            has_color = true;
            if (root) continue;
        }
        if (name == "fill" && !css_fill.empty()) continue;
        if (name == "stroke" && !css_stroke.empty()) continue;
        if (name.starts_with("frame-") || name.starts_with("object-")) continue;
        output.push_back(' ');
        output += name;
        output += "=\"";
        if (name == "fill" || name == "stroke") {
            // SVG.Skia does not resolve currentColor from the serialized root
            // color consistently. Resolve the inherited CSS color while the
            // live DOM/cascade is still authoritative so menu icons do not
            // silently fall back to black in the immutable scene.
            append_svg_paint_value(node, value, output);
        } else {
            append_xml_escaped(value, output, true);
        }
        output.push_back('"');
    }
    if (root && !has_xmlns) output += " xmlns=\"http://www.w3.org/2000/svg\"";
    if (!has_id && !node.id_attribute.empty()) {
        output += " id=\"";
        append_xml_escaped(node.id_attribute, output, true);
        output.push_back('"');
    }
    if (!has_class && !node.class_name.empty()) {
        output += " class=\"";
        append_xml_escaped(node.class_name, output, true);
        output.push_back('"');
    }
    if (!css_fill.empty()) {
        output += " fill=\"";
        append_svg_paint_value(node, css_fill, output);
        output.push_back('"');
    }
    if (!css_stroke.empty()) {
        output += " stroke=\"";
        append_svg_paint_value(node, css_stroke, output);
        output.push_back('"');
    }
    if (root) {
        // CSS fill/stroke can be inherited through an HTML menu row before the
        // SVG subtree begins. Materialize that inherited value on the SVG root
        // when no local presentation attribute or author declaration exists.
        if (css_fill.empty() && !node.attributes.contains("fill")) {
            for (auto* ancestor = node.parent; ancestor != nullptr; ancestor = ancestor->parent) {
                if (ancestor->style.textual().svg_fill.empty()) continue;
                output += " fill=\"";
                append_svg_paint_value(
                    node,
                    ancestor->style.textual().svg_fill,
                    output);
                output.push_back('"');
                break;
            }
        }
        if (css_stroke.empty() && !node.attributes.contains("stroke")) {
            for (auto* ancestor = node.parent; ancestor != nullptr; ancestor = ancestor->parent) {
                if (ancestor->style.textual().svg_stroke.empty()) continue;
                output += " stroke=\"";
                append_svg_paint_value(
                    node,
                    ancestor->style.textual().svg_stroke,
                    output);
                output.push_back('"');
                break;
            }
        }
        output += " color=\"";
        append_resolved_svg_color(node, output);
        output.push_back('"');
    } else if (has_color) {
        // The authored non-root color was already emitted above.
    }
    output.push_back('>');
    if (!node.text_content.empty()) append_xml_escaped(node.text_content, output, false);
    for (const auto* child : node.children) serialize_svg_subtree(*child, output, false);
    output += "</";
    output += node.tag;
    output.push_back('>');
}

float resolved_font_size(const dom_node& node)
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        if (current->style.font_size >= 0) return current->style.font_size;
    }
    return 14.0F;
}

float resolved_line_height(const dom_node& node, float font_size)
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        if (current->style.line_height == -2.0F) return font_size * 1.125F;
        if (current->style.line_height >= 0) return current->style.line_height;
    }
    // CSS `normal` follows the platform font's natural line box. The system
    // UI faces used by browser chrome resolve to an 18px line box at 16px;
    // 1.2 produced a 19.2px box and visibly displaced centered toolbar text.
    return font_size * 1.125F;
}

#if defined(WEBSCENE_NATIVE_ENGINE_FONT_FAMILY_VIEW_CONTROL)
std::string resolved_font_family(const dom_node& node)
#else
std::string_view resolved_font_family(const dom_node& node)
#endif
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        if (!current->style.textual().font_family.empty()) {
            return current->style.textual().font_family;
        }
    }
    return "sans-serif";
}

std::string resolved_font_smoothing(const dom_node& node)
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        const auto& value = current->style.textual().font_smoothing;
        if (!value.empty() && value != "inherit" && value != "unset") return value;
    }
    return "auto";
}

int32_t resolved_font_weight(const dom_node& node)
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        if (current->style.font_weight > 0) return current->style.font_weight;
    }
    return 400;
}

float resolved_letter_spacing(const dom_node& node)
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        if (current->style.letter_spacing_specified) return current->style.letter_spacing;
    }
    return 0;
}

float resolved_word_spacing(const dom_node& node)
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        if (current->style.word_spacing_specified) return current->style.word_spacing;
    }
    return 0;
}

std::string resolved_text_align(const dom_node& node)
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        if (!current->style.textual().text_align.empty()) {
            return current->style.textual().text_align;
        }
    }
    return "start";
}

std::string_view resolved_text_transform_name(const dom_node& node)
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        if (!current->style.textual().text_transform.empty()) {
            return current->style.textual().text_transform;
        }
    }
    return "none";
}

std::string resolved_text_transform(const dom_node& node, std::string value)
{
    const auto transform = resolved_text_transform_name(node);
    if (transform == "uppercase") {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
    } else if (transform == "lowercase") {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    } else if (transform == "capitalize") {
        auto word_start = true;
        for (auto& character : value) {
            if (std::isspace(static_cast<unsigned char>(character))) {
                word_start = true;
            } else if (word_start) {
                character = static_cast<char>(std::toupper(
                    static_cast<unsigned char>(character)));
                word_start = false;
            }
        }
    }
    return value;
}

bool resolved_white_space_wraps(const dom_node& node)
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        if (current->style.textual().white_space.empty()) continue;
        return current->style.textual().white_space != "nowrap"
            && current->style.textual().white_space != "pre";
    }
    return true;
}

bool has_visible_text(const std::string& value)
{
    return std::any_of(value.begin(), value.end(), [](unsigned char character) {
        return !std::isspace(character);
    });
}

bool resolved_collapses_whitespace(const dom_node& node)
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        if (current->style.textual().white_space.empty()) continue;
        return current->style.textual().white_space != "pre"
            && current->style.textual().white_space != "pre-wrap"
            && current->style.textual().white_space != "break-spaces";
    }
    return true;
}

bool is_collapsible_whitespace_text(const dom_node& node)
{
    return node.tag == "#text"
        && !node.generated_pseudo_box
        && resolved_collapses_whitespace(node)
        && !has_visible_text(node.text_content);
}

std::string collapsed_text(const dom_node& node, std::string value)
{
    if (!resolved_collapses_whitespace(node)) return value;
    std::istringstream source(value);
    std::string result;
    for (std::string word; source >> word;) {
        if (!result.empty()) result.push_back(' ');
        result += word;
    }
    return result;
}

bool is_collapsed_select(const dom_node& node)
{
    if (node.tag != "select" || node.attributes.contains("multiple")) return false;
    const auto authored_size = node.attributes.find("size");
    return authored_size == node.attributes.end()
        || parse_number(authored_size->second, 0) <= 1.0F;
}

template <typename Collection>
void collect_option_nodes(const dom_node& root, Collection& result)
{
    for (const auto* child : root.children) {
        if (child == nullptr) continue;
        if (child->tag == "option") result.push_back(child);
        collect_option_nodes(*child, result);
    }
}

void append_dom_text(const dom_node& node, std::string& result)
{
    if (node.tag == "#text") result += node.text_content;
    for (const auto* child : node.children) {
        if (child != nullptr) append_dom_text(*child, result);
    }
}

std::string option_label(const dom_node& option)
{
    if (const auto label = option.attributes.find("label");
        label != option.attributes.end()) {
        return label->second;
    }
    std::string result;
    append_dom_text(option, result);
    return collapsed_text(option, std::move(result));
}

const dom_node* selected_option_for(const dom_node& select)
{
    if (!is_collapsed_select(select)
        || select.form_control().selection_explicitly_empty) return nullptr;
    std::vector<const dom_node*> options;
    collect_option_nodes(select, options);
    for (const auto* option : options) {
        if (option->form_control().selectedness_initialized
            && option->form_control().selectedness) return option;
    }
    for (const auto* option : options) {
        if (option->attributes.contains("selected")) return option;
    }
    return options.empty() ? nullptr : options.front();
}

float fallback_text_width(std::string_view value, float font_size)
{
    if (font_size <= 0) return 0;
    auto width = 0.0F;
    for (size_t index = 0; index < value.size(); ++index) {
        const auto character = static_cast<unsigned char>(value[index]);
        if ((character & 0xC0U) == 0x80U) continue;
        float em = 0.55F;
        if (character >= 0x80U) em = 0.56F;
        else if (std::isspace(character)) em = 0.28F;
        else if (std::strchr("ilIj", character) != nullptr) em = 0.24F;
        else if (std::strchr("|!.'`,:;", character) != nullptr) em = 0.28F;
        else if (std::strchr("tfr", character) != nullptr) em = 0.35F;
        else if (std::strchr("mwMW@#%&", character) != nullptr) em = 0.82F;
        else if (std::strchr("CGOQ", character) != nullptr) em = 0.72F;
        else if (std::isupper(character)) em = 0.62F;
        else if (std::isdigit(character)) em = 0.56F;
        else if (std::ispunct(character)) em = 0.40F;
        width += std::max(1.0F, font_size * em);
    }
    return width;
}

uint32_t append_scene_string(
    const std::string& value,
    std::vector<webscene_scene_string>& strings,
    std::vector<char>& bytes)
{
    const auto index = static_cast<uint32_t>(strings.size());
    const auto offset = static_cast<uint32_t>(bytes.size());
    bytes.insert(bytes.end(), value.begin(), value.end());
    strings.push_back(webscene_scene_string{offset, static_cast<uint32_t>(value.size())});
    return index;
}

} // namespace

#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_SIZE_BRANCH_BENCHMARK)
thread_local std::array<uint64_t, 17U>
    intrinsic_size_branch_counts_for_benchmark{};
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_VIEW_BOX_BENCHMARK)
thread_local std::array<uint64_t, 4U>
    intrinsic_view_box_parse_counts_for_benchmark{};
#endif

display_mode blockified_display(const dom_node& node) noexcept
{
    const auto parent_display = node.parent == nullptr
        ? display_mode::none
        : node.parent->style.display;
    const auto flex_or_grid_parent = parent_display == display_mode::flex
        || parent_display == display_mode::inline_flex
        || parent_display == display_mode::grid
        || parent_display == display_mode::inline_grid;
    if (!flex_or_grid_parent) return node.style.display;
    switch (node.style.display) {
    case display_mode::inline_flow:
    case display_mode::inline_block:
        return display_mode::block;
    case display_mode::inline_flex:
        return display_mode::flex;
    case display_mode::inline_grid:
        return display_mode::grid;
    case display_mode::inline_table:
        return display_mode::table;
    default:
        return node.style.display;
    }
}

// These responsibility-focused fragments intentionally remain one translation
// unit so the refactor cannot alter inlining or production code generation.
#include "webscene_native_dom_tree.inc"
#include "webscene_native_dom_layout.inc"
#include "webscene_native_dom_scene.inc"
#include "webscene_native_dom_metrics.inc"
#include "webscene_native_dom_animations.inc"
#include "webscene_native_dom_css_values.inc"
} // namespace webscene_native
