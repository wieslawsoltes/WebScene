#include "htmlml_native_dom.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_set>

namespace htmlml_native {
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
    if (node.tag != "li") return {};
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
            if (sibling->tag == "li") ++value;
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

bool has_table_row_descendant(const dom_node& node)
{
    for (const auto* child : node.children) {
        if (child->style.display == display_mode::table_row) return true;
        if (is_table_row_group(child->style.display) && has_table_row_descendant(*child)) {
            return true;
        }
    }
    return false;
}

struct paint_z_index_update final {
    int32_t z_index{0};
    bool contains_retained_canvas{false};
};

paint_z_index_update update_paint_z_index(dom_node& node) noexcept
{
    auto descendant_z_index = 0;
    auto contains_retained_canvas =
        node.tag == "canvas" && !node.canvas().commands.empty();
    for (auto* child : node.children) {
        const auto child_update = update_paint_z_index(*child);
        descendant_z_index = std::max(
            descendant_z_index,
            child_update.z_index);
        contains_retained_canvas =
            contains_retained_canvas || child_update.contains_retained_canvas;
    }
    node.paint_z_index = node.style.z_index != 0
        ? node.style.z_index
        // A positioned layout area is painted atomically among its siblings.
        // Descendant levels still order content inside that area, but must not
        // lift the entire area above a later positioned sibling. A retained
        // canvas subtree is likewise a bounded host composition: its elevated
        // legend remains above its own canvas without lifting the complete
        // chart above later overlay DOM.
        : node.style.position == position_mode::normal && !contains_retained_canvas
            ? descendant_z_index : 0;
    return {node.paint_z_index, contains_retained_canvas};
}

void collect_positive_stacking_nodes(dom_node& node, std::vector<dom_node*>& result)
{
    if (node.style.z_index > 0) {
        result.push_back(&node);
    }
    for (auto* child : node.children) {
        collect_positive_stacking_nodes(*child, result);
    }
}

void collect_fixed_positioned_nodes(dom_node& node, std::vector<dom_node*>& result)
{
    if (node.style.position == position_mode::fixed) {
        result.push_back(&node);
    }
    for (auto* child : node.children) {
        collect_fixed_positioned_nodes(*child, result);
    }
}

void update_retained_canvas_paint_phase(dom_node& node, bool& retained_canvas_seen)
{
    node.paints_after_retained_canvas = retained_canvas_seen;
    if (node.tag == "canvas" && !node.canvas().commands.empty()) {
        retained_canvas_seen = true;
    }
    auto paint_order = node.children;
    if (std::any_of(paint_order.begin(), paint_order.end(), [](const auto* child) {
        return child->style.z_index != 0;
    })) {
        std::stable_sort(
            paint_order.begin(),
            paint_order.end(),
            [](const auto* left, const auto* right) {
                // A descendant stacking level is scoped to its ancestor; it
                // must not reorder the ancestor among its own siblings.
                return left->style.z_index < right->style.z_index;
            });
    }
    for (auto* child : paint_order) {
        update_retained_canvas_paint_phase(*child, retained_canvas_seen);
    }
}

bool stacking_ancestors_allow_hit(
    const dom_node& node,
    const dom_node& root,
    float x,
    float y,
    bool& visibility_hidden,
    bool& pointer_events_none)
{
    std::vector<const dom_node*> ancestors;
    for (auto* ancestor = node.parent; ancestor != nullptr; ancestor = ancestor->parent) {
        ancestors.push_back(ancestor);
        if (ancestor == &root) break;
    }
    if (ancestors.empty() || ancestors.back() != &root) return false;
    for (auto iterator = ancestors.rbegin(); iterator != ancestors.rend(); ++iterator) {
        const auto& ancestor = **iterator;
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

enum class calc_unit : uint8_t { number, pixels, percent, invalid };

struct calc_value final {
    double value{0};
    calc_unit unit{calc_unit::invalid};
    double pixel_offset{0};
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
                    if (!compatible(result, next)) return invalid();
                    if (result.unit == calc_unit::number && result.value == 0
                        && next.unit != calc_unit::number) result.unit = next.unit;
                    if (next.unit == calc_unit::number && next.value == 0
                        && result.unit != calc_unit::number) next.unit = result.unit;
                    result.value = name == "max"
                        ? std::max(result.value, next.value)
                        : std::min(result.value, next.value);
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
        } else if (position_ < source_.size() && source_[position_] == '%') {
            ++position_;
            unit = calc_unit::percent;
        }
        static_cast<void>(start);
        return {number, unit};
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
            || (left.unit == calc_unit::pixels && right.unit == calc_unit::percent);
    }

    static calc_value add(calc_value left, calc_value right, double sign)
    {
        if (!add_compatible(left, right)) return invalid();
        if (left.unit == calc_unit::percent && right.unit == calc_unit::pixels) {
            left.pixel_offset += right.value * sign;
            return left;
        }
        if (left.unit == calc_unit::pixels && right.unit == calc_unit::percent) {
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

float resolved_transform_rotation(const dom_node& node)
{
    float result = 0;
    for (auto* current = &node; current != nullptr; current = current->parent) {
        const auto* animation = current->animation_runtime();
        result += animation != nullptr && animation->transform_animation_initialized
            ? animation->painted_transform_rotate_degrees
            : current->style.transform_rotate_degrees;
    }
    return result;
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
        if (current->style.line_height >= 0) return current->style.line_height;
    }
    return font_size * 1.2F;
}

std::string resolved_font_family(const dom_node& node)
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        if (!current->style.textual().font_family.empty()) {
            return current->style.textual().font_family;
        }
    }
    return "sans-serif";
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

std::string resolved_text_transform(const dom_node& node, std::string value)
{
    auto transform = std::string{"none"};
    for (auto* current = &node; current != nullptr; current = current->parent) {
        if (!current->style.textual().text_transform.empty()) {
            transform = current->style.textual().text_transform;
            break;
        }
    }
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
    std::vector<htmlml_scene_string>& strings,
    std::vector<char>& bytes)
{
    const auto index = static_cast<uint32_t>(strings.size());
    const auto offset = static_cast<uint32_t>(bytes.size());
    bytes.insert(bytes.end(), value.begin(), value.end());
    strings.push_back(htmlml_scene_string{offset, static_cast<uint32_t>(value.size())});
    return index;
}

} // namespace

native_document::native_document(
    htmlml_text_measure_callback text_measure_callback,
    void* text_measure_user_data)
    : text_measure_callback_(text_measure_callback)
    , text_measure_user_data_(text_measure_user_data)
{
    clear();
}

float native_document::measure_text_width(
    std::string_view value,
    const dom_node& node) const
{
    const auto transformed = resolved_text_transform(node, std::string(value));
    const auto font_size = resolved_font_size(node);
    const auto family = resolved_font_family(node);
    const auto weight = resolved_font_weight(node);
    const auto letter_spacing = resolved_letter_spacing(node);
    const auto word_spacing = resolved_word_spacing(node);
    text_measurement_key key{
        transformed,
        family,
        font_size,
        weight,
        letter_spacing,
        word_spacing};
    if (const auto cached = text_measurement_cache_.find(key);
        cached != text_measurement_cache_.end()) {
        return cached->second;
    }
    if (text_measurement_cache_.size() >= 16'384U) {
        text_measurement_cache_.clear();
    }
    auto measured = fallback_text_width(transformed, font_size);
    if (text_measure_callback_ != nullptr) {
        htmlml_text_metrics metrics{};
        metrics.struct_size = sizeof(metrics);
        if (text_measure_callback_(
                text_measure_user_data_,
                transformed.data(),
                transformed.size(),
                family.data(),
                family.size(),
                font_size,
                weight,
                letter_spacing,
                word_spacing,
                &metrics) != 0U
            && std::isfinite(metrics.advance_width)
            && metrics.advance_width >= 0) {
            measured = metrics.advance_width;
        }
    }
    text_measurement_cache_.emplace(std::move(key), measured);
    return measured;
}

std::vector<std::string> native_document::wrap_text_lines(
    const std::string& value,
    float available_width,
    const dom_node& node,
    bool allow_wrap) const
{
    if (!has_visible_text(value)) return {};
    if (!allow_wrap) {
        std::istringstream source(value);
        std::string collapsed;
        for (std::string word; source >> word;) {
            if (!collapsed.empty()) collapsed.push_back(' ');
            collapsed += word;
        }
        return collapsed.empty() ? std::vector<std::string>{} : std::vector<std::string>{collapsed};
    }
    std::istringstream source(value);
    std::vector<std::string> lines;
    std::string current;
    std::string word;
    while (source >> word) {
        while (measure_text_width(word, node) > available_width && word.size() > 1U) {
            if (!current.empty()) {
                lines.push_back(std::move(current));
                current.clear();
            }
            size_t split = 1U;
            while (split < word.size()
                && measure_text_width(
                    std::string_view(word).substr(0, split + 1U), node) <= available_width) {
                ++split;
            }
            lines.push_back(word.substr(0, split));
            word.erase(0, split);
        }
        if (word.empty()) continue;
        if (current.empty()) {
            current = std::move(word);
        } else if (measure_text_width(current + " " + word, node) <= available_width) {
            current.push_back(' ');
            current += word;
        } else {
            lines.push_back(std::move(current));
            current = std::move(word);
        }
    }
    if (!current.empty()) lines.push_back(std::move(current));
    if (lines.empty()) lines.emplace_back();
    return lines;
}

dom_node& native_document::body() noexcept
{
    return *body_;
}

const dom_node& native_document::body() const noexcept
{
    return *body_;
}

dom_node& native_document::create_element(std::string tag)
{
    auto* storage = static_cast<dom_node*>(
        node_pool_.allocate(sizeof(dom_node), alignof(dom_node)));
    try {
        std::construct_at(storage);
    } catch (...) {
        node_pool_.deallocate(storage, sizeof(dom_node), alignof(dom_node));
        throw;
    }
    node_pointer node(storage, node_deleter{&node_pool_});
    node->id = next_node_id_++;
    node->tag = std::move(tag);
    if (node->tag == "#text") {
        node->style.display = display_mode::inline_flow;
    } else if (node->tag == "style" || node->tag == "script" || node->tag == "head"
        || node->tag == "meta" || node->tag == "link" || node->tag == "title"
        || node->tag == "base" || node->tag == "template") {
        // HTML metadata and scripting nodes do not generate boxes. Keep this
        // in the DOM layer as well as the V8 UA defaults so native callers and
        // reparents cannot accidentally turn their text into scene commands.
        node->style.display = display_mode::none;
    }
    auto* result = node.get();
    nodes_.push_back(std::move(node));
    mark_dirty();
    return *result;
}

bool native_document::append_child(dom_node& parent, dom_node& child)
{
    if (&parent == &child || child.parent != nullptr) {
        return false;
    }
    child.parent = &parent;
    child.visible = true;
    parent.children.push_back(&child);
    mark_dirty();
    return true;
}

void native_document::remove_all_children(dom_node& parent)
{
    for (auto* child : parent.children) {
        child->parent = nullptr;
        child->visible = false;
    }
    parent.children.clear();
    mark_dirty();
}

size_t native_document::erase_detached_subtree(dom_node& root)
{
    if (&root == body_ || root.parent != nullptr) return 0U;
    std::unordered_set<dom_node*> removed;
    const auto collect = [&](const auto& self, dom_node& node) -> void {
        if (!removed.insert(&node).second) return;
        for (auto* child : node.children) {
            if (child != nullptr) self(self, *child);
        }
    };
    collect(collect, root);
    const auto before = nodes_.size();
    std::erase_if(nodes_, [&](const auto& node) { return removed.contains(node.get()); });
    if (nodes_.size() != before) mark_dirty();
    return before - nodes_.size();
}

dom_node* native_document::find_by_native_id(uint32_t id) noexcept
{
    const auto match = std::find_if(nodes_.begin(), nodes_.end(), [id](const auto& node) {
        return node->id == id;
    });
    return match == nodes_.end() ? nullptr : match->get();
}

dom_node* native_document::find_by_id(const std::string& id) noexcept
{
    const auto match = std::find_if(nodes_.begin(), nodes_.end(), [&id](const auto& node) {
        return node->id_attribute == id;
    });
    return match == nodes_.end() ? nullptr : match->get();
}

std::vector<dom_node*> native_document::query_selector_all(
    dom_node& root,
    const std::string& selector)
{
    std::vector<dom_node*> result;
    collect_matches(root, selector, result);
    return result;
}

dom_node* native_document::hit_test(dom_node& root, float x, float y)
{
    const auto nearest_element = [](dom_node* hit) {
        while (hit != nullptr && hit->tag == "#text") hit = hit->parent;
        return hit;
    };
    std::vector<dom_node*> fixed;
    for (auto* child : root.children) {
        collect_fixed_positioned_nodes(*child, fixed);
    }
    std::stable_sort(
        fixed.begin(),
        fixed.end(),
        [](const auto* left, const auto* right) {
            return left->style.z_index < right->style.z_index;
        });
    for (auto iterator = fixed.rbegin(); iterator != fixed.rend(); ++iterator) {
        auto visibility_hidden = false;
        auto pointer_events_none = false;
        if (!stacking_ancestors_allow_hit(
                **iterator,
                root,
                x,
                y,
                visibility_hidden,
                pointer_events_none)) {
            continue;
        }
        if (auto* hit = hit_test_node(
                **iterator,
                x,
                y,
                visibility_hidden,
                pointer_events_none);
            hit != nullptr) {
            return nearest_element(hit);
        }
    }
    std::vector<dom_node*> elevated;
    for (auto* child : root.children) {
        collect_positive_stacking_nodes(*child, elevated);
    }
    std::stable_sort(
        elevated.begin(),
        elevated.end(),
        [](const auto* left, const auto* right) {
            return left->style.z_index < right->style.z_index;
        });
    for (auto iterator = elevated.rbegin(); iterator != elevated.rend(); ++iterator) {
        auto visibility_hidden = false;
        auto pointer_events_none = false;
        if (!stacking_ancestors_allow_hit(
                **iterator,
                root,
                x,
                y,
                visibility_hidden,
                pointer_events_none)) {
            continue;
        }
        if (auto* hit = hit_test_node(
                **iterator,
                x,
                y,
                visibility_hidden,
                pointer_events_none);
            hit != nullptr) {
            return nearest_element(hit);
        }
    }
    return nearest_element(hit_test_node(root, x, y, false, false, true));
}

dom_node* native_document::hit_test_node(
    dom_node& node,
    float x,
    float y,
    bool inherited_visibility_hidden,
    bool inherited_pointer_events_none,
    bool ignore_own_clip) noexcept
{
    if (!node.visible) return nullptr;
    const auto visibility_hidden = node.style.visibility_specified
        ? node.style.visibility_hidden
        : inherited_visibility_hidden;
    const auto pointer_events_none = node.style.pointer_events_specified
        ? node.style.pointer_events_none
        : inherited_pointer_events_none;
    const auto resolve_origin = [](css_length value, float available, float fallback) {
        if (value.unit == length_unit::pixels) return value.value;
        if (value.unit == length_unit::percent) {
            return available * value.value / 100.0F + value.pixel_offset;
        }
        return fallback;
    };
    const auto origin_x = node.layout.x + resolve_origin(
        node.style.transform_origin_x,
        node.layout.width,
        node.layout.width / 2.0F);
    const auto origin_y = node.layout.y + resolve_origin(
        node.style.transform_origin_y,
        node.layout.height,
        node.layout.height / 2.0F);
    const auto* animation = node.animation_runtime();
    const auto painted_scale_x =
        animation != nullptr && animation->transform_animation_initialized
        ? animation->painted_transform_scale_x
        : node.style.transform_scale_x;
    const auto painted_scale_y =
        animation != nullptr && animation->transform_animation_initialized
        ? animation->painted_transform_scale_y
        : node.style.transform_scale_y;
    const auto transformed_left = origin_x
        + (node.layout.x - origin_x) * painted_scale_x;
    const auto transformed_right = origin_x
        + (node.layout.x + node.layout.width - origin_x) * painted_scale_x;
    const auto transformed_top = origin_y
        + (node.layout.y - origin_y) * painted_scale_y;
    const auto transformed_bottom = origin_y
        + (node.layout.y + node.layout.height - origin_y) * painted_scale_y;
    const auto left = std::min(transformed_left, transformed_right);
    const auto right = std::max(transformed_left, transformed_right);
    const auto top = std::min(transformed_top, transformed_bottom);
    const auto bottom = std::max(transformed_top, transformed_bottom);
    const auto inside = x >= left && y >= top && x <= right && y <= bottom;
    if (inside || !node.style.clip || ignore_own_clip) {
        // Within a stacking context, larger z-index descendants are painted
        // and hit-tested above later zero-z DOM siblings. Component libraries place
        // its pane legend (including the Volume expander) at z-index:6 while
        // the chart canvas is a later sibling at z-index:0.
        auto upper_bound = std::numeric_limits<int32_t>::max();
        while (true) {
            auto next_z = std::numeric_limits<int32_t>::min();
            for (const auto* child : node.children) {
                const auto z_index = node.style.z_index != 0
                    ? child->paint_z_index
                    : child->style.z_index;
                if (z_index < upper_bound) {
                    next_z = std::max(next_z, z_index);
                }
            }
            if (next_z == std::numeric_limits<int32_t>::min()) break;
            for (auto iterator = node.children.rbegin(); iterator != node.children.rend(); ++iterator) {
                const auto z_index = node.style.z_index != 0
                    ? (*iterator)->paint_z_index
                    : (*iterator)->style.z_index;
                if (z_index != next_z) continue;
                if (auto* hit = hit_test_node(
                        **iterator,
                        x,
                        y,
                        visibility_hidden,
                        pointer_events_none);
                    hit != nullptr) {
                    return hit;
                }
            }
            upper_bound = next_z;
        }
    }
    return inside && !visibility_hidden && !pointer_events_none ? &node : nullptr;
}

void native_document::clear()
{
    nodes_.clear();
    // A complete navigation invalidates every node, so no pooled allocation
    // remains live. Return all node chunks to the upstream allocator instead
    // of retaining the previous document's high-water mark.
    node_pool_.release();
    transition_events_.clear();
    last_animation_advance_timestamp_ms_ =
        std::numeric_limits<double>::quiet_NaN();
    next_node_id_ = 1;
    body_ = &create_element("body");
    body_->style.width = {100, length_unit::percent};
    body_->style.height = {100, length_unit::percent};
    body_->style.background_rgba = 0x131722FFU;
    mark_dirty();
}

void native_document::layout(float viewport_width, float viewport_height)
{
    viewport_width_ = std::max(1.0F, viewport_width);
    viewport_height_ = std::max(1.0F, viewport_height);
    body_->layout = {
        0,
        0,
        std::max(1.0F, viewport_width),
        std::max(1.0F, viewport_height)};
    body_->visible = true;
    layout_children(*body_);
    update_paint_z_index(*body_);
    auto retained_canvas_seen = false;
    update_retained_canvas_paint_phase(*body_, retained_canvas_seen);
    ++layout_passes_;
    dirty_ = false;
    globally_dirty_ = false;
    out_of_flow_geometry_dirty_roots_.clear();
}

void native_document::layout_children(dom_node& parent)
{
    if (parent.style.has_pseudo_elements()) {
        parent.style.mutable_before_pseudo().layout = {};
        parent.style.mutable_after_pseudo().layout = {};
    }
    parent.list_marker_layout = {};
    const auto has_before = pseudo_generates_box(parent.style.before_pseudo());
    const auto has_after = pseudo_generates_box(parent.style.after_pseudo());
    const auto marker_text = list_marker_text(parent);
    const auto has_inside_marker = !marker_text.empty()
        && resolved_list_style(parent, true) == "inside";
    if (parent.children.empty() && !has_before && !has_after && !has_inside_marker) {
        return;
    }

    const auto border_left = resolve_length(
        parent, parent.style.border_left_width, parent.layout.width, 0);
    const auto border_right = resolve_length(
        parent, parent.style.border_right_width, parent.layout.width, 0);
    const auto border_top = resolve_length(
        parent, parent.style.border_top_width, parent.layout.height, 0);
    const auto border_bottom = resolve_length(
        parent, parent.style.border_bottom_width, parent.layout.height, 0);
    const auto padding_left = resolve_length(
        parent, parent.style.padding_left, parent.layout.width, 0);
    const auto padding_right = resolve_length(
        parent, parent.style.padding_right, parent.layout.width, 0);
    const auto padding_top = resolve_length(
        parent, parent.style.padding_top, parent.layout.height, 0);
    const auto padding_bottom = resolve_length(
        parent, parent.style.padding_bottom, parent.layout.height, 0);
    const layout_rect content{
        parent.layout.x + border_left + padding_left,
        parent.layout.y + border_top + padding_top,
        std::max(0.0F, parent.layout.width
            - border_left - border_right - padding_left - padding_right),
        std::max(0.0F, parent.layout.height
            - border_top - border_bottom - padding_top - padding_bottom)};
    parent.scroll_viewport_width = content.width;
    parent.scroll_viewport_height = content.height;
    parent.scroll_left = std::clamp(
        parent.scroll_left,
        0.0F,
        std::max(0.0F, parent.scroll_content_width - content.width));
    parent.scroll_top = std::clamp(
        parent.scroll_top,
        0.0F,
        std::max(0.0F, parent.scroll_content_height - content.height));
    if (parent.style.display == display_mode::contents) {
        for (auto* child : parent.children) {
            child->visible = parent.visible && child->style.display != display_mode::none;
            if (!child->visible) {
                child->layout = {};
                continue;
            }
            if (is_out_of_flow(child->style.position)) continue;
            layout_child(*child, parent.layout, content);
        }
        return;
    }

    // CSS table fixup suppresses descendants of column boxes. These boxes do
    // not generate independent layout containers even when authored with an
    // overriding display on their children.
    if (is_table_track(parent.style.display)) {
        for (auto* child : parent.children) {
            child->visible = false;
            child->layout = {};
            child->scroll_content_width = 0;
            child->scroll_content_height = 0;
        }
        parent.scroll_content_width = 0;
        parent.scroll_content_height = 0;
        return;
    }

    // A table root with cells or other table-internal children but no authored
    // row still generates anonymous rows. Keep those boxes virtual: the DOM
    // remains unchanged while the participating children share a row grid.
    if (is_table_container(parent.style.display) && !has_table_row_descendant(parent)) {
        std::vector<float> explicit_columns;
        const auto collect_columns = [&](const auto& self, const dom_node& node) -> void {
            for (const auto* child : node.children) {
                if (child->style.display == display_mode::table_column) {
                    auto width = is_specified(child->style.width)
                        ? resolve_length(*child, child->style.width, content.width, 0)
                        : 0.0F;
                    explicit_columns.push_back(width);
                } else if (child->style.display == display_mode::table_column_group) {
                    self(self, *child);
                }
            }
        };
        collect_columns(collect_columns, parent);

        std::vector<std::vector<dom_node*>> direct_rows;
        std::vector<dom_node*> current_row;
        const auto flush_row = [&] {
            if (current_row.empty()) return;
            direct_rows.push_back(std::move(current_row));
            current_row.clear();
        };
        for (auto* child : parent.children) {
            if (is_table_track(child->style.display)) {
                flush_row();
                continue;
            }
            if (is_table_row_group(child->style.display)
                || child->style.display == display_mode::table_caption) {
                flush_row();
                continue;
            }
            if (child->style.display != display_mode::none) current_row.push_back(child);
        }
        flush_row();

        auto table_width = std::accumulate(
            explicit_columns.begin(), explicit_columns.end(), 0.0F);
        if (explicit_columns.empty()) {
            for (const auto& row : direct_rows) {
                auto fixed_width = 0.0F;
                auto percentage = 0.0F;
                for (const auto* child : row) {
                    if (child->style.width.unit == length_unit::percent) {
                        percentage += child->style.width.value / 100.0F;
                    } else {
                        fixed_width += intrinsic_size(*child, true, content.width);
                    }
                }
                const auto candidate = percentage > 0.0F && percentage < 1.0F
                    ? fixed_width / (1.0F - percentage)
                    : fixed_width;
                table_width = std::max(table_width, candidate);
            }
        }
        if (is_specified(parent.style.width)) table_width = content.width;
        table_width = std::max(0.0F, table_width);
        if (!is_specified(parent.style.width) && table_width > 0.0F) {
            parent.layout.width = table_width + border_left + border_right
                + padding_left + padding_right;
        }

        auto cursor_y = content.y;
        const auto layout_anonymous_row = [&](const std::vector<dom_node*>& row) {
            if (row.empty()) return 0.0F;
            auto widths = explicit_columns;
            if (widths.size() < row.size()) widths.resize(row.size(), 0.0F);
            for (size_t index = 0; index < row.size(); ++index) {
                if (index < explicit_columns.size() && explicit_columns[index] > 0) continue;
                const auto* child = row[index];
                widths[index] = child->style.width.unit == length_unit::percent
                    ? resolve_length(*child, child->style.width, table_width, 0)
                    : intrinsic_size(*child, true, table_width);
            }
            auto used = std::accumulate(widths.begin(), widths.end(), 0.0F);
            if (table_width <= 0.0F) table_width = used;
            if (used < table_width) {
                std::vector<size_t> automatic;
                for (size_t index = 0; index < row.size(); ++index) {
                    if (!is_specified(row[index]->style.width)
                        && (index >= explicit_columns.size()
                            || explicit_columns[index] <= 0)) automatic.push_back(index);
                }
                if (!automatic.empty()) {
                    const auto share = (table_width - used)
                        / static_cast<float>(automatic.size());
                    for (const auto index : automatic) widths[index] += share;
                    used = table_width;
                }
            }
            auto row_height = 0.0F;
            for (const auto* child : row) {
                row_height = std::max(
                    row_height,
                    intrinsic_size(*child, false, content.height));
            }
            if (!direct_rows.empty() && content.height > 0) {
                // Anonymous rows share an authored table height. Without this
                // minimum, empty fixed-width cells collapse to zero height and
                // the table background paints through them.
                row_height = std::max(
                    row_height,
                    content.height / static_cast<float>(direct_rows.size()));
            }
            auto cursor_x = content.x;
            for (size_t index = 0; index < row.size(); ++index) {
                auto* child = row[index];
                child->visible = parent.visible && child->style.display != display_mode::none;
                auto& child_table = child->mutable_table_layout();
                child_table.column_index = index;
                child_table.column_span = 1;
                child_table.cell_height = row_height;
                layout_child(*child, parent.layout, {
                    cursor_x,
                    cursor_y,
                    widths[index],
                    row_height});
                cursor_x += widths[index];
            }
            return row_height;
        };

        size_t direct_row_index = 0;
        current_row.clear();
        dom_node* preceding_non_caption = nullptr;
        for (auto* child : parent.children) {
            child->visible = parent.visible && child->style.display != display_mode::none;
            if (!child->visible || is_table_track(child->style.display)) {
                child->layout = {};
                continue;
            }
            if (is_table_row_group(child->style.display)) {
                child->mutable_table_layout().column_widths = explicit_columns;
                layout_child(*child, parent.layout, {
                    content.x,
                    cursor_y,
                    table_width,
                    intrinsic_size(*child, false, content.height)});
                cursor_y += child->layout.height;
                preceding_non_caption = child;
                continue;
            }
            if (child->style.display == display_mode::table_caption) {
                const auto height = intrinsic_size(*child, false, content.height);
                const auto x = preceding_non_caption == nullptr
                    ? content.x : preceding_non_caption->layout.x;
                layout_child(*child, parent.layout, {x, cursor_y, table_width, height});
                cursor_y += child->layout.height;
                continue;
            }
            if (direct_row_index < direct_rows.size()
                && child == direct_rows[direct_row_index].front()) {
                const auto& row = direct_rows[direct_row_index++];
                cursor_y += layout_anonymous_row(row);
                preceding_non_caption = row.back();
            }
        }
        parent.mutable_table_layout().column_widths = explicit_columns;
        parent.scroll_content_width = table_width;
        parent.scroll_content_height = std::max(0.0F, cursor_y - content.y);
        if (!is_specified(parent.style.height)) {
            parent.layout.height = parent.scroll_content_height
                + border_top + border_bottom + padding_top + padding_bottom;
        }
        return;
    }

    if (is_table_container(parent.style.display) && has_table_row_descendant(parent)) {
        struct table_cell_placement final {
            dom_node* cell{nullptr};
            size_t column{0};
            size_t column_span{1};
            size_t row_span{1};
        };
        struct table_row_placement final {
            dom_node* row{nullptr};
            std::vector<table_cell_placement> cells;
        };

        std::vector<table_row_placement> rows;
        std::function<void(dom_node&)> collect_rows = [&](dom_node& node) {
            for (auto* child : node.children) {
                if (child->style.display == display_mode::none
                    || is_table_track(child->style.display)) continue;
                if (child->style.display == display_mode::table_row) {
                    rows.push_back({child, {}});
                } else if (is_table_row_group(child->style.display)) {
                    collect_rows(*child);
                }
            }
        };
        collect_rows(parent);

        std::vector<size_t> occupied_rows;
        size_t column_count = 0;
        for (auto& row : rows) {
            size_t column = 0;
            for (auto* child : row.row->children) {
                if (child->style.display != display_mode::table_cell) continue;
                while (column < occupied_rows.size() && occupied_rows[column] > 0) ++column;
                const auto parse_span = [&](std::string_view name) {
                    auto found = child->attributes.find(std::string(name));
                    if (found == child->attributes.end()) {
                        const auto reflected = name == "colspan" ? "colSpan" : "rowSpan";
                        found = child->attributes.find(reflected);
                    }
                    return found == child->attributes.end()
                        ? size_t{1}
                        : std::max<size_t>(1, static_cast<size_t>(parse_number(found->second, 1)));
                };
                const auto column_span = parse_span("colspan");
                const auto row_span = parse_span("rowspan");
                if (occupied_rows.size() < column + column_span) {
                    occupied_rows.resize(column + column_span, 0);
                }
                row.cells.push_back({child, column, column_span, row_span});
                auto& child_table = child->mutable_table_layout();
                child_table.column_index = column;
                child_table.column_span = column_span;
                child_table.row_span = row_span;
                for (size_t track = column; track < column + column_span; ++track) {
                    occupied_rows[track] = std::max(occupied_rows[track], row_span);
                }
                column += column_span;
            }
            column_count = std::max(column_count, occupied_rows.size());
            for (auto& remaining_rows : occupied_rows) {
                if (remaining_rows > 0) --remaining_rows;
            }
        }

        std::vector<float> columns(column_count, 0.0F);
        std::vector<bool> fixed_columns(column_count, false);
        std::vector<bool> percentage_columns(column_count, false);
        std::vector<bool> specified_columns(column_count, false);
        for (const auto& row : rows) {
            for (const auto& placement : row.cells) {
                auto cell_width = intrinsic_size(*placement.cell, true, content.width);
                if (parent.style.table_layout_fixed
                    && is_specified(placement.cell->style.width)) {
                    cell_width = resolve_length(
                        *placement.cell,
                        placement.cell->style.width,
                        content.width,
                        cell_width);
                    for (size_t track = placement.column;
                        track < std::min(column_count, placement.column + placement.column_span);
                        ++track) {
                        specified_columns[track] = true;
                        fixed_columns[track] = fixed_columns[track]
                            || placement.cell->style.width.unit == length_unit::pixels;
                        percentage_columns[track] = percentage_columns[track]
                            || placement.cell->style.width.unit == length_unit::percent;
                    }
                }
                auto assigned = 0.0F;
                for (size_t track = placement.column;
                    track < std::min(column_count, placement.column + placement.column_span);
                    ++track) {
                    assigned += columns[track];
                }
                const auto shortfall = std::max(0.0F, cell_width - assigned);
                if (shortfall <= 0 || placement.column_span == 0) continue;
                const auto share = shortfall / static_cast<float>(placement.column_span);
                for (size_t track = placement.column;
                    track < std::min(column_count, placement.column + placement.column_span);
                    ++track) {
                    columns[track] += share;
                }
            }
        }
        const auto intrinsic_width = std::accumulate(columns.begin(), columns.end(), 0.0F);
        if (!columns.empty() && content.width > intrinsic_width) {
            const auto excess = content.width - intrinsic_width;
            if (parent.style.table_layout_fixed) {
                std::vector<size_t> distribution_columns;
                for (size_t column = 0; column < columns.size(); ++column) {
                    if (!specified_columns[column]) distribution_columns.push_back(column);
                }
                if (distribution_columns.empty()) {
                    for (size_t column = 0; column < columns.size(); ++column) {
                        if (fixed_columns[column] && !percentage_columns[column]) {
                            distribution_columns.push_back(column);
                        }
                    }
                }
                if (distribution_columns.empty()) {
                    distribution_columns.resize(columns.size());
                    std::iota(distribution_columns.begin(), distribution_columns.end(), 0U);
                }
                auto distribution_width = 0.0F;
                for (const auto column : distribution_columns) {
                    distribution_width += columns[column];
                }
                if (distribution_width > 0) {
                    for (const auto column : distribution_columns) {
                        columns[column] += excess * columns[column] / distribution_width;
                    }
                } else {
                    const auto share = excess
                        / static_cast<float>(distribution_columns.size());
                    for (const auto column : distribution_columns) columns[column] += share;
                }
            } else if (intrinsic_width > 0) {
                for (auto& column : columns) column += excess * column / intrinsic_width;
            } else {
                const auto share = content.width / static_cast<float>(columns.size());
                std::fill(columns.begin(), columns.end(), share);
            }
        }
        parent.mutable_table_layout().column_widths = columns;

        for (auto& row : rows) {
            auto height = is_specified(row.row->style.height)
                ? resolve_length(*row.row, row.row->style.height, content.height, 0)
                : 0.0F;
            for (const auto& placement : row.cells) {
                if (placement.row_span == 1) {
                    height = std::max(
                        height,
                        intrinsic_size(*placement.cell, false, content.height));
                }
            }
            row.row->mutable_table_layout().row_height = height;
        }
        for (size_t row_index = 0; row_index < rows.size(); ++row_index) {
            for (const auto& placement : rows[row_index].cells) {
                auto span_height = 0.0F;
                const auto row_end = std::min(rows.size(), row_index + placement.row_span);
                for (size_t span_row = row_index; span_row < row_end; ++span_row) {
                    span_height += rows[span_row].row->table_layout().row_height;
                }
                const auto required = intrinsic_size(*placement.cell, false, content.height);
                if (required > span_height && row_end > row_index) {
                    const auto extra = (required - span_height)
                        / static_cast<float>(row_end - row_index);
                    for (size_t span_row = row_index; span_row < row_end; ++span_row) {
                        rows[span_row].row->mutable_table_layout().row_height += extra;
                    }
                }
            }
        }
        for (size_t row_index = 0; row_index < rows.size(); ++row_index) {
            for (const auto& placement : rows[row_index].cells) {
                auto& cell_table = placement.cell->mutable_table_layout();
                cell_table.cell_height = 0;
                const auto row_end = std::min(rows.size(), row_index + placement.row_span);
                for (size_t span_row = row_index; span_row < row_end; ++span_row) {
                    cell_table.cell_height += rows[span_row].row->table_layout().row_height;
                }
            }
        }

        auto cursor_y = content.y;
        for (auto* child : parent.children) {
            child->visible = parent.visible && child->style.display != display_mode::none;
            if (!child->visible || is_table_track(child->style.display)) {
                child->layout = {};
                continue;
            }
            if (child->style.display == display_mode::table_caption) {
                const auto height = intrinsic_size(*child, false, content.height);
                layout_child(*child, parent.layout, {content.x, cursor_y, content.width, height});
                cursor_y += child->layout.height;
                continue;
            }
            auto height = 0.0F;
            if (child->style.display == display_mode::table_row) {
                height = child->table_layout().row_height;
            } else if (is_table_row_group(child->style.display)) {
                for (const auto& row : rows) {
                    if (row.row->parent == child) height += row.row->table_layout().row_height;
                }
            } else {
                height = intrinsic_size(*child, false, content.height);
            }
            child->mutable_table_layout().column_widths = columns;
            layout_child(*child, parent.layout, {content.x, cursor_y, content.width, height});
            cursor_y += child->layout.height;
        }
        parent.scroll_content_width = std::max(content.width, intrinsic_width);
        parent.scroll_content_height = std::max(content.height, cursor_y - content.y);
        return;
    }

    if (is_table_row_group(parent.style.display)
        && std::none_of(parent.children.begin(), parent.children.end(), [](const auto* child) {
            return child->style.display == display_mode::table_row;
        })) {
        std::vector<dom_node*> cells;
        std::vector<dom_node*> captions;
        for (auto* child : parent.children) {
            if (child->style.display == display_mode::none
                || is_table_track(child->style.display)) {
                child->visible = false;
                child->layout = {};
            } else if (child->style.display == display_mode::table_caption) {
                captions.push_back(child);
            } else if (!child->tag.starts_with('#')) {
                cells.push_back(child);
            }
        }
        auto widths = parent.table_layout().column_widths;
        if (widths.size() < cells.size()) widths.resize(cells.size(), 0.0F);
        for (size_t index = 0; index < cells.size(); ++index) {
            if (widths[index] <= 0) {
                widths[index] = intrinsic_size(*cells[index], true, content.width);
            }
        }
        auto row_height = 0.0F;
        for (const auto* cell : cells) {
            row_height = std::max(row_height, intrinsic_size(*cell, false, content.height));
        }
        auto cursor_x = content.x;
        for (size_t index = 0; index < cells.size(); ++index) {
            auto* cell = cells[index];
            cell->visible = parent.visible;
            auto& cell_table = cell->mutable_table_layout();
            cell_table.column_index = index;
            cell_table.column_span = 1;
            cell_table.cell_height = row_height;
            layout_child(*cell, parent.layout, {
                cursor_x,
                content.y,
                widths[index],
                row_height});
            cursor_x += widths[index];
        }
        const auto caption_x = cells.empty() ? content.x : cells.back()->layout.x;
        for (auto* caption : captions) {
            caption->visible = parent.visible;
            layout_child(*caption, parent.layout, {
                caption_x,
                content.y,
                cells.empty() ? content.width : cells.back()->layout.width,
                intrinsic_size(*caption, false, content.height)});
        }
        parent.mutable_table_layout().column_widths = widths;
        parent.scroll_content_width = std::max(content.width, cursor_x - content.x);
        parent.scroll_content_height = row_height;
        if (!is_specified(parent.style.height)) {
            parent.layout.height = row_height
                + border_top + border_bottom + padding_top + padding_bottom;
        }
        return;
    }

    if (is_table_row_group(parent.style.display)) {
        auto cursor_y = content.y;
        for (auto* child : parent.children) {
            child->visible = parent.visible && child->style.display != display_mode::none;
            if (!child->visible) {
                child->layout = {};
                continue;
            }
            if (child->style.display != display_mode::table_row) continue;
            child->mutable_table_layout().column_widths = parent.table_layout().column_widths;
            layout_child(*child, parent.layout, {
                content.x,
                cursor_y,
                content.width,
                child->table_layout().row_height});
            cursor_y += child->layout.height;
        }
        parent.scroll_content_width = content.width;
        parent.scroll_content_height = std::max(content.height, cursor_y - content.y);
        return;
    }

    if (parent.style.display == display_mode::table_row) {
        const auto& parent_table = parent.table_layout();
        std::vector<float> offsets(parent_table.column_widths.size() + 1, content.x);
        for (size_t column = 0; column < parent_table.column_widths.size(); ++column) {
            offsets[column + 1] = offsets[column] + parent_table.column_widths[column];
        }
        for (auto* child : parent.children) {
            child->visible = parent.visible && child->style.display != display_mode::none;
            if (!child->visible) {
                child->layout = {};
                continue;
            }
            const auto& child_table = child->table_layout();
            if (child->style.display != display_mode::table_cell
                || child_table.column_index >= parent_table.column_widths.size()) continue;
            const auto end = std::min(
                parent_table.column_widths.size(),
                child_table.column_index + child_table.column_span);
            layout_child(*child, parent.layout, {
                offsets[child_table.column_index],
                content.y,
                offsets[end] - offsets[child_table.column_index],
                child_table.cell_height > 0 ? child_table.cell_height : content.height});
            if (parent.style.align_items == align_mode::center && !child->children.empty()) {
                auto content_top = std::numeric_limits<float>::max();
                auto content_bottom = std::numeric_limits<float>::lowest();
                for (const auto* descendant : child->children) {
                    if (!descendant->visible || is_out_of_flow(descendant->style.position)) continue;
                    content_top = std::min(content_top, descendant->layout.y);
                    content_bottom = std::max(
                        content_bottom,
                        descendant->layout.y + descendant->layout.height);
                }
                if (content_bottom >= content_top) {
                    const auto target_top = child->layout.y
                        + std::max(0.0F, (child->layout.height - (content_bottom - content_top)) * 0.5F);
                    const auto offset_y = target_top - content_top;
                    std::function<void(dom_node&)> shift_subtree = [&](dom_node& node) {
                        node.layout.y += offset_y;
                        for (auto& fragment : node.text_layout_fragments) fragment.y += offset_y;
                        for (auto* descendant : node.children) shift_subtree(*descendant);
                    };
                    for (auto* descendant : child->children) shift_subtree(*descendant);
                }
            }
        }
        auto improper_x = offsets.empty() ? content.x : offsets.back();
        auto improper_height = content.height;
        for (auto* child : parent.children) {
            if (child->style.display == display_mode::none
                || child->style.display == display_mode::table_cell
                || child->tag.starts_with('#')) continue;
            const auto width = intrinsic_size(*child, true, content.width);
            improper_height = std::max(
                improper_height,
                intrinsic_size(*child, false, content.height));
            child->visible = parent.visible;
            layout_child(*child, parent.layout, {
                improper_x,
                content.y,
                width,
                improper_height});
            // Anonymous cells in the separated-border model retain the UA
            // table's 2px inter-cell spacing. This also collapses intervening
            // whitespace text rather than turning it into its own box.
            improper_x += width + 2.0F;
        }
        if (parent.table_layout().column_widths.empty()) {
            parent.layout.height = improper_height;
        }
        parent.scroll_content_width = offsets.empty() ? content.width : offsets.back() - content.x;
        parent.scroll_content_width = std::max(
            parent.scroll_content_width,
            improper_x - content.x);
        parent.scroll_content_height = std::max(content.height, improper_height);
        return;
    }
    const auto outer_authored_size = [&](const dom_node& node, bool horizontal_axis, float available) {
        const auto authored = horizontal_axis ? node.style.width : node.style.height;
        const auto intrinsic_keyword = authored.unit == length_unit::max_content
                || authored.unit == length_unit::min_content
                || authored.unit == length_unit::fit_content;
        auto size = intrinsic_keyword
            ? intrinsic_size(node, horizontal_axis, available)
            : resolve_length(node, authored, available, 0);
        if (!node.style.border_box && !intrinsic_keyword) {
            size += horizontal_axis
                ? resolve_length(node, node.style.padding_left, available, 0)
                    + resolve_length(node, node.style.padding_right, available, 0)
                    + resolve_length(node, node.style.border_left_width, available, 0)
                    + resolve_length(node, node.style.border_right_width, available, 0)
                : resolve_length(node, node.style.padding_top, available, 0)
                    + resolve_length(node, node.style.padding_bottom, available, 0)
                    + resolve_length(node, node.style.border_top_width, available, 0)
                    + resolve_length(node, node.style.border_bottom_width, available, 0);
        }
        return size;
    };
    // Generated boxes are real participants immediately inside the originating
    // element. The specialized text-run fast path only traverses DOM children,
    // so use the general item layout whenever either pseudo exists; otherwise a
    // block ::before is omitted and the originating text paints over it.
    const auto inline_formatting_context = !has_before && !has_after && !has_inside_marker
        && !is_flex_container(parent.style.display)
        && std::all_of(
            parent.children.begin(),
            parent.children.end(),
            [](const dom_node* child) {
                return child->style.display == display_mode::none
                    || is_out_of_flow(child->style.position)
                    || is_inline_level(child->style.display);
            });
    if (inline_formatting_context) {
        std::vector<dom_node*> text_runs;
        struct positioned_inline_item final {
            dom_node* node;
            size_t preceding_text_runs;
        };
        std::vector<positioned_inline_item> positioned_items;
        std::function<bool(dom_node&)> collect_runs = [&](dom_node& node) {
            node.text_layout_fragments.clear();
            node.visible = parent.visible && node.style.display != display_mode::none;
            if (!node.visible) return true;
            if (is_out_of_flow(node.style.position)) {
                positioned_items.push_back({&node, text_runs.size()});
                return true;
            }
            if (node.tag == "#text") {
                // Whitespace-only DOM nodes still contribute one collapsed
                // boundary between adjacent inline descendants. Retain them in
                // the run stream so `text <strong>text</strong>` does not become
                // `texttext`; they produce no standalone glyph box.
                if (!node.text_content.empty()) text_runs.push_back(&node);
                return true;
            }
            // Generated content belongs to the inline subtree at the point
            // where its originating element participates. Flattening only the
            // DOM text descendants drops child ::before/::after content (for
            // example an authored separator space between adjacent status
            // fragments), so let the general inline-item path place it.
            if (pseudo_generates_box(node.style.before_pseudo())
                || pseudo_generates_box(node.style.after_pseudo())) {
                return false;
            }
            // Inline flex/grid boxes establish their own formatting contexts. Flattening
            // their descendants into the surrounding text run loses their padding,
            // alignment and line-box height.
            if (is_flex_container(node.style.display)
                || is_grid_container(node.style.display)) {
                return false;
            }
            if (is_specified(node.style.width) || is_specified(node.style.height)) return false;
            for (auto* child : node.children) {
                if (child->style.display != display_mode::none
                    && !is_out_of_flow(child->style.position)
                    && !is_inline_level(child->style.display)) {
                    return false;
                }
                if (!collect_runs(*child)) return false;
            }
            return true;
        };
        auto supported = true;
        for (auto* child : parent.children) {
            if (!collect_runs(*child)) {
                supported = false;
                break;
            }
        }
        if (supported && !text_runs.empty() && content.width > 0) {
            auto cursor_x = 0.0F;
            auto cursor_y = 0.0F;
            auto current_line_height = 0.0F;
            auto pending_space = false;
            std::vector<layout_rect> static_anchors(text_runs.size() + 1U);
            static_anchors[0] = {
                content.x,
                content.y,
                0,
                resolved_line_height(parent, resolved_font_size(parent))};
            const auto start_new_line = [&] {
                cursor_x = 0;
                cursor_y += current_line_height > 0
                    ? current_line_height
                    : resolved_line_height(parent, resolved_font_size(parent));
                current_line_height = 0;
            };
            const auto append_fragment = [&](dom_node& run, std::string text, float width) {
                const auto font_size = resolved_font_size(run);
                const auto line_height = resolved_line_height(run, font_size);
                if (!run.text_layout_fragments.empty()) {
                    auto& previous = run.text_layout_fragments.back();
                    if (std::abs(previous.y - (content.y + cursor_y)) < 0.01F
                        && std::abs(previous.x + previous.width - (content.x + cursor_x)) < 0.01F) {
                        previous.text += text;
                        previous.width += width;
                        cursor_x += width;
                        current_line_height = std::max(current_line_height, line_height);
                        return;
                    }
                }
                run.text_layout_fragments.push_back({
                    content.x + cursor_x,
                    content.y + cursor_y,
                    width,
                    line_height,
                    std::move(text)});
                cursor_x += width;
                current_line_height = std::max(current_line_height, line_height);
            };
            for (size_t run_index = 0; run_index < text_runs.size(); ++run_index) {
                auto* run = text_runs[run_index];
                const auto font_size = resolved_font_size(*run);
                const auto allow_wrap = resolved_white_space_wraps(*run);
                const auto transformed_text = resolved_text_transform(*run, run->text_content);
                std::string word;
                const auto flush_word = [&] {
                    if (word.empty()) return;
                    auto word_width = measure_text_width(word, *run);
                    auto space_width = pending_space && cursor_x > 0
                        ? measure_text_width(" ", *run) : 0.0F;
                    if (allow_wrap && cursor_x > 0
                        && cursor_x + space_width + word_width > content.width) {
                        start_new_line();
                        space_width = 0;
                    }
                    if (space_width > 0) append_fragment(*run, " ", space_width);
                    while (allow_wrap && word_width > content.width && !word.empty()) {
                        size_t characters = 1U;
                        while (characters < word.size()
                            && measure_text_width(
                                std::string_view(word).substr(0, characters + 1U),
                                *run) <= content.width) {
                            ++characters;
                        }
                        const auto part = word.substr(0, characters);
                        const auto part_width = measure_text_width(part, *run);
                        append_fragment(*run, part, part_width);
                        word.erase(0, characters);
                        word_width = measure_text_width(word, *run);
                        if (!word.empty()) start_new_line();
                    }
                    if (!word.empty()) append_fragment(*run, word, word_width);
                    word.clear();
                    pending_space = false;
                };
                for (const auto character : transformed_text) {
                    if (std::isspace(static_cast<unsigned char>(character))) {
                        flush_word();
                        pending_space = true;
                    } else {
                        word.push_back(character);
                    }
                }
                flush_word();
                static_anchors[run_index + 1U] = {
                    content.x + cursor_x,
                    content.y + cursor_y,
                    0,
                    current_line_height > 0
                        ? current_line_height
                        : resolved_line_height(*run, font_size)};
            }

            std::function<bool(dom_node&, layout_rect&)> update_inline_box =
                [&](dom_node& node, layout_rect& bounds) {
                    auto has_bounds = false;
                    const auto include = [&](const layout_rect& item) {
                        if (!has_bounds) {
                            bounds = item;
                            has_bounds = true;
                            return;
                        }
                        const auto right = std::max(bounds.x + bounds.width, item.x + item.width);
                        const auto bottom = std::max(bounds.y + bounds.height, item.y + item.height);
                        bounds.x = std::min(bounds.x, item.x);
                        bounds.y = std::min(bounds.y, item.y);
                        bounds.width = right - bounds.x;
                        bounds.height = bottom - bounds.y;
                    };
                    for (const auto& fragment : node.text_layout_fragments) {
                        include({fragment.x, fragment.y, fragment.width, fragment.height});
                    }
                    for (auto* child : node.children) {
                        layout_rect child_bounds{};
                        if (update_inline_box(*child, child_bounds)) include(child_bounds);
                    }
                    node.layout = has_bounds
                        ? bounds
                        : layout_rect{content.x + cursor_x, content.y + cursor_y, 0, 0};
                    return has_bounds;
                };
            for (auto* child : parent.children) {
                layout_rect child_bounds{};
                update_inline_box(*child, child_bounds);
            }

            for (const auto& item : positioned_items) {
                auto& child = *item.node;
                const auto anchor = static_anchors[std::min(
                    item.preceding_text_runs,
                    static_anchors.size() - 1U)];
                auto* containing_node = body_;
                if (child.style.position == position_mode::absolute) {
                    for (auto* ancestor = child.parent;
                        ancestor != nullptr && ancestor != body_;
                        ancestor = ancestor->parent) {
                        if (ancestor->style.position != position_mode::normal) {
                            containing_node = ancestor;
                            break;
                        }
                    }
                }
                const auto& containing = containing_node->layout;
                const auto has_left = is_specified(child.style.left);
                const auto has_top = is_specified(child.style.top);
                const auto has_right = is_specified(child.style.right);
                const auto has_bottom = is_specified(child.style.bottom);
                const auto left = has_left
                    ? resolve_length(child, child.style.left, containing.width, 0) : 0;
                const auto top = has_top
                    ? resolve_length(child, child.style.top, containing.height, 0) : 0;
                const auto right = has_right
                    ? resolve_length(child, child.style.right, containing.width, 0) : 0;
                const auto bottom = has_bottom
                    ? resolve_length(child, child.style.bottom, containing.height, 0) : 0;
                const auto margin_left = resolve_length(
                    child, child.style.margin_left, containing.width, 0);
                const auto margin_right = resolve_length(
                    child, child.style.margin_right, containing.width, 0);
                const auto margin_top = resolve_length(
                    child, child.style.margin_top, containing.height, 0);
                const auto margin_bottom = resolve_length(
                    child, child.style.margin_bottom, containing.height, 0);
                const auto inline_static_position = is_inline_level(child.style.display);
                const auto static_x = inline_static_position ? anchor.x : content.x;
                const auto static_y = inline_static_position
                    ? anchor.y
                    : anchor.y + std::max(
                        anchor.height,
                        resolved_line_height(parent, resolved_font_size(parent)));
                layout_rect assigned{};
                assigned.width = is_specified(child.style.width)
                    ? outer_authored_size(child, true, containing.width)
                    : has_left && has_right
                        ? std::max(0.0F, containing.width - left - right)
                        : intrinsic_size(child, true, containing.width);
                assigned.height = is_specified(child.style.height)
                    ? outer_authored_size(child, false, containing.height)
                    : has_top && has_bottom
                        ? std::max(0.0F, containing.height - top - bottom)
                        : intrinsic_size(child, false, containing.height);
                assigned.x = containing.x + (has_left
                    ? left + margin_left
                    : has_right
                        ? containing.width - right - margin_right - assigned.width
                        : static_x - containing.x + margin_left);
                assigned.y = containing.y + (has_top
                    ? top + margin_top
                    : has_bottom
                        ? containing.height - bottom - margin_bottom - assigned.height
                        : static_y - containing.y + margin_top);
                layout_child(child, containing, assigned);
            }
            return;
        }
    }
    const auto& parent_grid = parent.style.grid();
    if (is_grid_container(parent.style.display) && parent_grid.two_columns) {
        struct grid_row final {
            dom_node* first{nullptr};
            dom_node* second{nullptr};
            bool spanning{false};
        };
        std::vector<grid_row> rows;
        dom_node* pending = nullptr;
        for (auto* child : parent.children) {
            child->visible = parent.visible && child->style.display != display_mode::none;
            if (!child->visible) {
                child->layout = {};
                continue;
            }
            if (is_out_of_flow(child->style.position)) continue;
            if (child->style.grid().span_all) {
                if (pending != nullptr) rows.push_back({pending, nullptr, false});
                pending = nullptr;
                rows.push_back({child, nullptr, true});
            } else if (pending == nullptr) {
                pending = child;
            } else {
                rows.push_back({pending, child, false});
                pending = nullptr;
            }
        }
        if (pending != nullptr) rows.push_back({pending, nullptr, false});

        const auto column_gap = resolve_length(
            parent, parent.style.column_gap, content.width, 0);
        const auto row_gap = resolve_length(
            parent, parent.style.row_gap, content.height, 0);
        auto first_column_width = parent_grid.template_columns.size() >= 2U
            ? resolve_length(
                parent, parent_grid.template_columns[0], content.width, 0)
            : 0.0F;
        if (parent_grid.template_columns.size() < 2U) {
            for (const auto& row : rows) {
                if (row.spanning || row.first == nullptr) continue;
                const auto margins = resolve_length(
                        *row.first, row.first->style.margin_left, content.width, 0)
                    + resolve_length(
                        *row.first, row.first->style.margin_right, content.width, 0);
                first_column_width = std::max(
                    first_column_width,
                    intrinsic_size(*row.first, true, content.width) + margins);
            }
            first_column_width = std::clamp(
                first_column_width,
                0.0F,
                content.width * 0.6F);
        }
        const auto second_column_width = parent_grid.template_columns.size() >= 2U
            ? resolve_length(
                parent, parent_grid.template_columns[1], content.width, 0)
            : std::max(0.0F, content.width - column_gap - first_column_width);
        auto row_y = content.y;
        for (size_t row_index = 0; row_index < rows.size(); ++row_index) {
            const auto& row = rows[row_index];
            auto row_height = 0.0F;
            const auto measure_height = [&](const dom_node* child) {
                if (child == nullptr) return 0.0F;
                const auto margins = resolve_length(
                        *child, child->style.margin_top, content.height, 0)
                    + resolve_length(
                        *child, child->style.margin_bottom, content.height, 0);
                const auto authored = is_specified(child->style.height)
                    ? outer_authored_size(*child, false, content.height)
                    : intrinsic_size(*child, false, content.height);
                return authored + margins;
            };
            row_height = row_index < parent_grid.template_rows.size()
                ? resolve_length(
                    parent, parent_grid.template_rows[row_index], content.height, 0)
                : std::max(measure_height(row.first), measure_height(row.second));
            const auto arrange = [&](dom_node* child, float x, float available_width) {
                if (child == nullptr) return;
                const auto margin_left = resolve_length(
                    *child, child->style.margin_left, available_width, 0);
                const auto margin_right = resolve_length(
                    *child, child->style.margin_right, available_width, 0);
                const auto margin_top = resolve_length(
                    *child, child->style.margin_top, row_height, 0);
                const auto margin_bottom = resolve_length(
                    *child, child->style.margin_bottom, row_height, 0);
                auto width = is_specified(child->style.width)
                    ? outer_authored_size(*child, true, available_width)
                    : std::max(0.0F, available_width - margin_left - margin_right);
                auto height = is_specified(child->style.height)
                    ? outer_authored_size(*child, false, row_height)
                    : std::max(0.0F, row_height - margin_top - margin_bottom);
                layout_child(*child, parent.layout, {
                    x + margin_left,
                    row_y + margin_top,
                    width,
                    height});
            };
            if (row.spanning) {
                arrange(row.first, content.x, content.width);
            } else {
                arrange(row.first, content.x, first_column_width);
                arrange(
                    row.second,
                    content.x + first_column_width + column_gap,
                    second_column_width);
            }
            row_y += row_height + (row_index + 1U < rows.size() ? row_gap : 0.0F);
        }
        return;
    }
    if (is_grid_container(parent.style.display) && parent_grid.fractional_rows) {
        std::vector<dom_node*> flow_children;
        for (auto* child : parent.children) {
            child->visible = parent.visible && child->style.display != display_mode::none;
            if (!child->visible) {
                child->layout = {};
                continue;
            }
            if (!is_out_of_flow(child->style.position)) flow_children.push_back(child);
        }
        if (!flow_children.empty()) {
            auto trailing_height = 0.0F;
            for (size_t index = 1; index < flow_children.size(); ++index) {
                const auto* child = flow_children[index];
                trailing_height += (is_specified(child->style.height)
                        ? outer_authored_size(*child, false, content.height)
                        : intrinsic_size(*child, false, content.height))
                    + resolve_length(*child, child->style.margin_top, content.height, 0)
                    + resolve_length(*child, child->style.margin_bottom, content.height, 0);
            }
            auto row_y = content.y;
            for (size_t index = 0; index < flow_children.size(); ++index) {
                auto* child = flow_children[index];
                const auto margin_left = resolve_length(
                    *child, child->style.margin_left, content.width, 0);
                const auto margin_right = resolve_length(
                    *child, child->style.margin_right, content.width, 0);
                const auto margin_top = resolve_length(
                    *child, child->style.margin_top, content.height, 0);
                const auto margin_bottom = resolve_length(
                    *child, child->style.margin_bottom, content.height, 0);
                auto height = is_specified(child->style.height)
                    ? outer_authored_size(*child, false, content.height)
                    : intrinsic_size(*child, false, content.height);
                if (index == 0U) {
                    height = std::max(
                        height,
                        std::max(0.0F, content.height - trailing_height - margin_top - margin_bottom));
                }
                layout_child(*child, parent.layout, {
                    content.x + margin_left,
                    row_y + margin_top,
                    std::max(0.0F, content.width - margin_left - margin_right),
                    height});
                row_y += margin_top + child->layout.height + margin_bottom;
            }
            return;
        }
    }
    std::optional<dom_node> marker_item;
    std::optional<dom_node> before_item;
    std::optional<dom_node> after_item;
    std::vector<dom_node*> layout_items;
    layout_items.reserve(parent.children.size() + 3U);
    if (has_inside_marker) {
        marker_item.emplace(make_list_marker_layout_node(parent, marker_text));
        layout_items.push_back(&*marker_item);
    }
    if (has_before) {
        before_item.emplace(make_pseudo_layout_node(parent, parent.style.before_pseudo()));
        layout_items.push_back(&*before_item);
    }
    layout_items.insert(layout_items.end(), parent.children.begin(), parent.children.end());
    if (has_after) {
        after_item.emplace(make_pseudo_layout_node(parent, parent.style.after_pseudo()));
        layout_items.push_back(&*after_item);
    }

    const auto flex_container = is_flex_container(parent.style.display);
    const auto participates_in_flow = [&](const dom_node* child) {
        return child->style.display != display_mode::none
            && !is_out_of_flow(child->style.position)
            && !(flex_container && is_collapsible_whitespace_text(*child));
    };
    const auto inline_flow = !flex_container && std::all_of(
        layout_items.begin(),
        layout_items.end(),
        [](const dom_node* child) {
            return child->style.display == display_mode::none
                || is_out_of_flow(child->style.position)
                || is_inline_level(child->style.display);
        });
    const auto horizontal = (flex_container && parent.style.direction == flex_direction::row)
        || inline_flow;
    const auto main_available = horizontal ? content.width : content.height;
    const auto constrain_size = [&](const dom_node& node, bool horizontal_axis, float size, float available) {
        const auto& minimum = horizontal_axis ? node.style.min_width : node.style.min_height;
        const auto& maximum = horizontal_axis ? node.style.max_width : node.style.max_height;
        const auto box_adjustment = node.style.border_box ? 0.0F : horizontal_axis
            ? resolve_length(node, node.style.padding_left, available, 0)
                + resolve_length(node, node.style.padding_right, available, 0)
                + resolve_length(node, node.style.border_left_width, available, 0)
                + resolve_length(node, node.style.border_right_width, available, 0)
            : resolve_length(node, node.style.padding_top, available, 0)
                + resolve_length(node, node.style.padding_bottom, available, 0)
                + resolve_length(node, node.style.border_top_width, available, 0)
                + resolve_length(node, node.style.border_bottom_width, available, 0);
        if (is_specified(maximum)) {
            size = std::min(
                size,
                resolve_length(node, maximum, available, size) + box_adjustment);
        }
        if (is_specified(minimum)) {
            size = std::max(
                size,
                resolve_length(node, minimum, available, 0) + box_adjustment);
        }
        return std::max(0.0F, size);
    };
    std::vector<dom_node*> ordered_children = layout_items;
    if (flex_container && parent.style.flex_reverse) {
        std::reverse(ordered_children.begin(), ordered_children.end());
    }
    float fixed = 0;
    float grow = 0;
    float automatic_count = 0;
    size_t automatic_main_margin_count = 0U;
    std::unordered_map<const dom_node*, float> flex_base_main_sizes;
    const auto flow_count = static_cast<size_t>(std::count_if(
        ordered_children.begin(),
        ordered_children.end(),
        [&](const dom_node* child) { return participates_in_flow(child); }));
    const auto main_gap = flex_container
        ? resolve_length(
            parent,
            horizontal ? parent.style.column_gap : parent.style.row_gap,
            main_available,
            0)
        : 0.0F;
    if (flow_count > 1U) fixed += main_gap * static_cast<float>(flow_count - 1U);

    for (const auto* child : ordered_children) {
        if (!participates_in_flow(child)) {
            continue;
        }
        if (flex_container) {
            automatic_main_margin_count += horizontal
                ? static_cast<size_t>(child->style.margin_left_auto)
                    + static_cast<size_t>(child->style.margin_right_auto)
                : static_cast<size_t>(child->style.margin_top_auto)
                    + static_cast<size_t>(child->style.margin_bottom_auto);
        }
        const auto margin_start = resolve_length(
            *child,
            horizontal ? child->style.margin_left : child->style.margin_top,
            main_available,
            0);
        const auto margin_end = resolve_length(
            *child,
            horizontal ? child->style.margin_right : child->style.margin_bottom,
            main_available,
            0);
        fixed += margin_start + margin_end;
        const auto authored_main = horizontal ? child->style.width : child->style.height;
        const auto flex_main = flex_container && is_specified(child->style.flex_basis)
            ? child->style.flex_basis
            : authored_main;
        if (is_specified(flex_main)) {
            const auto basis = constrain_size(
                *child,
                horizontal,
                flex_main.unit == authored_main.unit && flex_main.value == authored_main.value
                    ? outer_authored_size(*child, horizontal, main_available)
                    : resolve_length(*child, flex_main, main_available, 0),
                main_available);
            flex_base_main_sizes.emplace(child, basis);
            fixed += basis;
            if (flex_container) {
                grow += std::max(0.0F, child->style.flex_grow);
            }
        } else {
            const auto intrinsic = constrain_size(
                *child,
                horizontal,
                intrinsic_size(*child, horizontal, main_available),
                main_available);
            if (intrinsic > 0) {
                flex_base_main_sizes.emplace(child, intrinsic);
                fixed += intrinsic;
                grow += std::max(0.0F, child->style.flex_grow);
                continue;
            }
            const auto child_grow = std::max(0.0F, child->style.flex_grow);
            grow += child_grow;
            // Auto-width inline boxes shrink to their contents. In particular,
            // a portal host span whose only child is position:fixed has a zero
            // width box; stretching it across the row makes it intercept input
            // outside the visible popup.
            if (child_grow == 0 && !is_inline_level(child->style.display)) {
                automatic_count += 1.0F;
            }
            flex_base_main_sizes.emplace(child, 0.0F);
        }
    }

    struct flex_line final {
        std::vector<const dom_node*> items;
        float outer_main_size{0};
    };
    std::vector<flex_line> wrapped_flex_lines;
    std::unordered_map<const dom_node*, size_t> wrapped_flex_line_indices;
    if (flex_container && horizontal && parent.style.flex_wrap) {
        for (const auto* child : ordered_children) {
            if (!participates_in_flow(child)) continue;
            const auto known = flex_base_main_sizes.find(child);
            const auto base = known == flex_base_main_sizes.end() ? 0.0F : known->second;
            const auto margins = resolve_length(
                    *child, child->style.margin_left, main_available, 0)
                + resolve_length(*child, child->style.margin_right, main_available, 0);
            if (wrapped_flex_lines.empty()) wrapped_flex_lines.emplace_back();
            auto* line = &wrapped_flex_lines.back();
            const auto candidate = line->outer_main_size
                + (line->items.empty() ? 0.0F : main_gap)
                + margins + base;
            // An oversized item still establishes a line of its own and is
            // flexed after collection. It must not create an empty first line.
            if (!line->items.empty() && candidate > main_available + 0.01F) {
                wrapped_flex_lines.emplace_back();
                line = &wrapped_flex_lines.back();
            }
            if (!line->items.empty()) line->outer_main_size += main_gap;
            line->outer_main_size += margins + base;
            line->items.push_back(child);
            wrapped_flex_line_indices.emplace(child, wrapped_flex_lines.size() - 1U);
        }
    }

    float cursor = 0;
    const auto remaining = std::max(0.0F, main_available - fixed);
    const auto overflow = flex_container && !parent.style.flex_wrap
        ? std::max(0.0F, fixed - main_available) : 0.0F;
    std::unordered_map<const dom_node*, float> flex_shrunk_main_sizes;
    const auto resolve_flex_shrink = [&](const std::vector<const dom_node*>& line_items,
                                         float line_overflow) {
        if (line_overflow <= 0.001F) return;
        struct shrink_item final {
            const dom_node* node;
            float base;
            float target;
            float minimum;
            float weight;
            bool frozen;
        };
        std::vector<shrink_item> items;
        for (const auto* child : line_items) {
            const auto known = flex_base_main_sizes.find(child);
            const auto base = known == flex_base_main_sizes.end() ? 0.0F : known->second;
            const auto& minimum_length = horizontal
                ? child->style.min_width : child->style.min_height;
            const auto minimum = is_specified(minimum_length)
                ? resolve_length(*child, minimum_length, main_available, 0)
                : 0.0F;
            const auto weight = std::max(0.0F, child->style.flex_shrink) * base;
            items.push_back({
                child,
                base,
                base,
                std::min(base, std::max(0.0F, minimum)),
                weight,
                weight <= 0 || base <= minimum + 0.001F});
        }

        auto remaining_overflow = line_overflow;
        for (size_t pass = 0; pass <= items.size() && remaining_overflow > 0.001F; ++pass) {
            auto active_weight = 0.0F;
            for (const auto& item : items) {
                if (!item.frozen) active_weight += item.weight;
            }
            if (active_weight <= 0) break;

            auto consumed = 0.0F;
            auto froze_item = false;
            for (auto& item : items) {
                if (item.frozen) continue;
                const auto requested = remaining_overflow * item.weight / active_weight;
                const auto available_reduction = std::max(0.0F, item.target - item.minimum);
                const auto reduction = std::min(requested, available_reduction);
                item.target -= reduction;
                consumed += reduction;
                if (reduction + 0.001F < requested
                    || item.target <= item.minimum + 0.001F) {
                    item.target = item.minimum;
                    item.frozen = true;
                    froze_item = true;
                }
            }
            remaining_overflow = std::max(0.0F, remaining_overflow - consumed);
            if (!froze_item || consumed <= 0.001F) break;
        }
        for (const auto& item : items) {
            flex_shrunk_main_sizes.insert_or_assign(item.node, item.target);
        }
    };
    if (overflow > 0) {
        std::vector<const dom_node*> line_items;
        for (const auto* child : ordered_children) {
            if (participates_in_flow(child)) line_items.push_back(child);
        }
        resolve_flex_shrink(line_items, overflow);
    }
    for (const auto& line : wrapped_flex_lines) {
        resolve_flex_shrink(
            line.items,
            std::max(0.0F, line.outer_main_size - main_available));

        const auto line_free = std::max(0.0F, main_available - line.outer_main_size);
        auto line_grow = 0.0F;
        for (const auto* child : line.items) {
            line_grow += std::max(0.0F, child->style.flex_grow);
        }
        if (line_free > 0.001F && line_grow > 0.0F) {
            for (const auto* child : line.items) {
                const auto known = flex_base_main_sizes.find(child);
                const auto base = known == flex_base_main_sizes.end() ? 0.0F : known->second;
                flex_shrunk_main_sizes.insert_or_assign(
                    child,
                    base + line_free * std::max(0.0F, child->style.flex_grow) / line_grow);
            }
        }
    }
    const auto automatic_margin_share = flex_container && grow == 0
        && automatic_count == 0 && automatic_main_margin_count > 0U
        ? remaining / static_cast<float>(automatic_main_margin_count)
        : 0.0F;
    const auto justify_free = flex_container && grow == 0 && automatic_count == 0
        && automatic_main_margin_count == 0U
        ? remaining : 0.0F;
    float justify_gap = 0;
    if (parent.style.justify_content == justify_mode::center) cursor = justify_free * 0.5F;
    else if (parent.style.justify_content == justify_mode::end) cursor = justify_free;
    else if (parent.style.justify_content == justify_mode::space_between) {
        if (flow_count > 1) justify_gap = justify_free / static_cast<float>(flow_count - 1);
    } else if (parent.style.justify_content == justify_mode::space_around) {
        if (flow_count > 0) {
            justify_gap = justify_free / static_cast<float>(flow_count);
            cursor = justify_gap * 0.5F;
        }
    } else if (parent.style.justify_content == justify_mode::space_evenly) {
        if (flow_count > 0) {
            justify_gap = justify_free / static_cast<float>(flow_count + 1U);
            cursor = justify_gap;
        }
    }
    if (inline_flow && !flex_container) {
        const auto alignment = resolved_text_align(parent);
        if (alignment == "right" || alignment == "end") cursor = remaining;
        else if (alignment == "center") cursor = remaining * 0.5F;
    }
    size_t placed_flow_count = 0U;
    auto wrapped_cross_offset = 0.0F;
    auto wrapped_line_cross_size = 0.0F;
    const auto wrapped_cross_gap = flex_container && horizontal && parent.style.flex_wrap
        ? resolve_length(parent, parent.style.row_gap, content.height, 0) : 0.0F;
    const auto wrapped_has_definite_cross_size = flex_container && horizontal
        && parent.style.flex_wrap && is_specified(parent.style.height)
        && !wrapped_flex_lines.empty();
    const auto stretched_wrapped_line_cross_size = wrapped_has_definite_cross_size
        ? std::max(
            0.0F,
            (content.height
                - wrapped_cross_gap * static_cast<float>(wrapped_flex_lines.size() - 1U))
                / static_cast<float>(wrapped_flex_lines.size()))
        : 0.0F;
    size_t active_wrapped_line = 0U;
    size_t placed_in_wrapped_line = 0U;
    auto maximum_flow_right = content.x;
    auto maximum_flow_bottom = content.y;
    auto float_band_y = content.y;
    auto float_band_height = 0.0F;
    auto left_float_edge = content.x;
    auto right_float_edge = content.x + content.width;
    auto has_float_band = false;
    auto maximum_float_bottom = content.y;
    for (auto* child : ordered_children) {
        // Out-of-flow generated boxes can have empty content while still
        // painting a background or border. They are not anonymous flex
        // whitespace and must reach absolute/fixed positioning below.
        if (flex_container
            && !is_out_of_flow(child->style.position)
            && is_collapsible_whitespace_text(*child)) {
            child->visible = false;
            child->layout = {};
            continue;
        }
        child->visible = parent.visible
            && child->style.display != display_mode::none;
        if (!child->visible) {
            child->layout = {};
            continue;
        }

        // Floats do not alter flex-item layout, but ordinary block formatting
        // contexts place consecutive left/right floats into the same available
        // line until their outer widths no longer fit.  This bounded float
        // projection covers the common media/badge and legacy reference
        // construction while keeping them outside the normal block cursor.
        if (!flex_container && child->style.floating != float_mode::none
            && !is_out_of_flow(child->style.position)) {
            const auto margin_left = resolve_length(
                *child, child->style.margin_left, content.width, 0);
            const auto margin_right = resolve_length(
                *child, child->style.margin_right, content.width, 0);
            const auto margin_top = resolve_length(
                *child, child->style.margin_top, content.height, 0);
            const auto margin_bottom = resolve_length(
                *child, child->style.margin_bottom, content.height, 0);
            auto width = is_specified(child->style.width)
                ? outer_authored_size(*child, true, content.width)
                : intrinsic_size(*child, true, content.width);
            auto height = is_specified(child->style.height)
                ? outer_authored_size(*child, false, content.height)
                : intrinsic_size(*child, false, content.height);
            width = constrain_size(*child, true, width, content.width);
            height = constrain_size(*child, false, height, content.height);
            const auto outer_width = margin_left + width + margin_right;
            const auto outer_height = margin_top + height + margin_bottom;
            const auto normal_flow_y = content.y + cursor;
            if (!has_float_band || normal_flow_y > float_band_y + 0.01F) {
                float_band_y = normal_flow_y;
                float_band_height = 0;
                left_float_edge = content.x;
                right_float_edge = content.x + content.width;
                has_float_band = true;
            }
            if (right_float_edge - left_float_edge + 0.01F < outer_width
                && float_band_height > 0) {
                float_band_y += float_band_height;
                float_band_height = 0;
                left_float_edge = content.x;
                right_float_edge = content.x + content.width;
            }
            const auto x = child->style.floating == float_mode::left
                ? left_float_edge + margin_left
                : right_float_edge - margin_right - width;
            layout_child(*child, parent.layout, {
                x - parent.scroll_left,
                float_band_y + margin_top - parent.scroll_top,
                width,
                height});
            if (child->style.floating == float_mode::left) {
                left_float_edge += outer_width;
            } else {
                right_float_edge -= outer_width;
            }
            float_band_height = std::max(float_band_height, outer_height);
            maximum_flow_right = std::max(
                maximum_flow_right,
                x + child->layout.width + margin_right + parent.scroll_left);
            maximum_flow_bottom = std::max(
                maximum_flow_bottom,
                float_band_y + outer_height + parent.scroll_top);
            maximum_float_bottom = std::max(
                maximum_float_bottom,
                float_band_y + outer_height);
            continue;
        }

        if (is_out_of_flow(child->style.position)) {
            // Absolute-positioned portal content is laid out against its nearest
            // positioned ancestor, not against an intervening zero-size wrapper.
            // Complex hosted components use that pattern for full-width centered rails.
            auto* containing_node = body_;
            if (child->style.position == position_mode::absolute) {
                for (auto* ancestor = &parent;
                    ancestor != nullptr && ancestor != body_;
                    ancestor = ancestor->parent) {
                    if (ancestor->style.position != position_mode::normal) {
                        containing_node = ancestor;
                        break;
                    }
                }
            }
            const auto& containing = containing_node->layout;
            layout_rect assigned{};
            const auto has_left = is_specified(child->style.left);
            const auto has_top = is_specified(child->style.top);
            const auto has_right = is_specified(child->style.right);
            const auto has_bottom = is_specified(child->style.bottom);
            const auto left = has_left
                ? resolve_length(*child, child->style.left, containing.width, 0)
                : 0;
            const auto top = has_top
                ? resolve_length(*child, child->style.top, containing.height, 0)
                : 0;
            const auto right = has_right
                ? resolve_length(*child, child->style.right, containing.width, 0)
                : 0;
            const auto bottom = has_bottom
                ? resolve_length(*child, child->style.bottom, containing.height, 0)
                : 0;
            const auto margin_left = resolve_length(
                *child,
                child->style.margin_left,
                containing.width,
                0);
            const auto margin_right = resolve_length(
                *child,
                child->style.margin_right,
                containing.width,
                0);
            const auto margin_top = resolve_length(
                *child,
                child->style.margin_top,
                containing.height,
                0);
            const auto margin_bottom = resolve_length(
                *child,
                child->style.margin_bottom,
                containing.height,
                0);
            const auto intrinsic_width = intrinsic_size(*child, true, containing.width);
            const auto intrinsic_height = intrinsic_size(*child, false, containing.height);
            assigned.width = is_specified(child->style.width)
                ? outer_authored_size(*child, true, containing.width)
                : has_left && has_right
                    ? std::max(0.0F, containing.width - left - right)
                    : intrinsic_width;
            assigned.height = is_specified(child->style.height)
                ? outer_authored_size(*child, false, containing.height)
                : has_top && has_bottom
                    ? std::max(0.0F, containing.height - top - bottom)
                    : intrinsic_height;
            // Absolutely/fixed positioned boxes still participate in the CSS
            // min/max size constraints. Dialogs commonly combine
            // `width: 100%` with `max-width` and then position the capped box.
            assigned.width = constrain_size(
                *child,
                true,
                assigned.width,
                containing.width);
            assigned.height = constrain_size(
                *child,
                false,
                assigned.height,
                containing.height);
            assigned.x = containing.x + (has_left
                ? left + margin_left
                : has_right
                    ? containing.width - right - margin_right - assigned.width
                    : content.x - containing.x + (horizontal ? cursor : 0) + margin_left);
            assigned.y = containing.y + (has_top
                ? top + margin_top
                : has_bottom
                    ? containing.height - bottom - margin_bottom - assigned.height
                    : content.y - containing.y + (horizontal ? 0 : cursor) + margin_top);
            if (child->style.position != position_mode::fixed) {
                assigned.x -= parent.scroll_left;
                assigned.y -= parent.scroll_top;
            }
            layout_child(*child, containing, assigned);
            continue;
        }

        if (placed_flow_count > 0U
            && !(flex_container && horizontal && parent.style.flex_wrap)) {
            cursor += main_gap;
        }
        layout_rect assigned{};
        auto vertical_margin_bottom = 0.0F;
        if (horizontal) {
            if (flex_container && parent.style.flex_wrap) {
                const auto line_known = wrapped_flex_line_indices.find(child);
                const auto line_index = line_known == wrapped_flex_line_indices.end()
                    ? active_wrapped_line : line_known->second;
                if (line_index != active_wrapped_line) {
                    active_wrapped_line = line_index;
                    placed_in_wrapped_line = 0U;
                    cursor = 0.0F;
                    wrapped_cross_offset = wrapped_has_definite_cross_size
                        ? static_cast<float>(line_index)
                            * (stretched_wrapped_line_cross_size + wrapped_cross_gap)
                        : wrapped_cross_offset + wrapped_line_cross_size + wrapped_cross_gap;
                    wrapped_line_cross_size = 0.0F;
                }
                if (placed_in_wrapped_line > 0U) cursor += main_gap;
            }
            const auto margin_left = child->style.margin_left_auto
                ? automatic_margin_share
                : resolve_length(*child, child->style.margin_left, content.width, 0);
            const auto margin_right = child->style.margin_right_auto
                ? automatic_margin_share
                : resolve_length(*child, child->style.margin_right, content.width, 0);
            const auto margin_top = resolve_length(
                *child, child->style.margin_top, content.height, 0);
            const auto margin_bottom = resolve_length(
                *child, child->style.margin_bottom, content.height, 0);
            const auto intrinsic_width = intrinsic_size(*child, true, content.width);
            const auto intrinsic_height = intrinsic_size(*child, false, content.height);
            auto width = flex_container && is_specified(child->style.flex_basis)
                ? resolve_length(*child, child->style.flex_basis, content.width, 0)
                    + (grow > 0
                        ? remaining * std::max(0.0F, child->style.flex_grow) / grow
                        : 0.0F)
                : is_specified(child->style.width)
                ? outer_authored_size(*child, true, content.width)
                    + (flex_container && grow > 0
                        ? remaining * std::max(0.0F, child->style.flex_grow) / grow
                        : 0.0F)
                : intrinsic_width > 0
                    ? intrinsic_width + (grow > 0
                        ? remaining * std::max(0.0F, child->style.flex_grow) / grow
                        : 0.0F)
                : grow > 0
                    ? remaining * std::max(0.0F, child->style.flex_grow) / grow
                    : is_inline_level(child->style.display) ? 0.0F
                    : automatic_count > 0 ? remaining / automatic_count : remaining;
            if (inline_flow && !flex_container) {
                width = std::min(
                    width,
                    std::max(
                        0.0F,
                        content.width - cursor - margin_left - margin_right));
            }
            width = constrain_size(*child, true, width, content.width);
            if (const auto shrunk = flex_shrunk_main_sizes.find(child);
                shrunk != flex_shrunk_main_sizes.end()) {
                width = constrain_size(*child, true, shrunk->second, content.width);
            }
            const auto definite_authored_height = is_specified(child->style.height)
                && (child->style.height.unit != length_unit::percent
                    || is_specified(parent.style.height));
            const auto available_line_cross_size = wrapped_has_definite_cross_size
                ? stretched_wrapped_line_cross_size : content.height;
            auto height = flex_container && is_specified(child->style.flex_basis)
                ? resolve_length(*child, child->style.flex_basis, content.height, 0)
                    + (grow > 0
                        ? remaining * std::max(0.0F, child->style.flex_grow) / grow
                        : 0.0F)
                : definite_authored_height
                ? outer_authored_size(*child, false, content.height)
                : (flex_container
                        && (child->style.align_self_specified
                            ? child->style.align_self
                            : parent.style.align_items) == align_mode::stretch)
                    || intrinsic_height <= 0
                    ? std::max(0.0F, available_line_cross_size - margin_top - margin_bottom)
                    : std::min(
                        std::max(0.0F, available_line_cross_size - margin_top - margin_bottom),
                        intrinsic_height);
            height = constrain_size(*child, false, height, content.height);
            if (flex_container && parent.style.flex_wrap) {
                wrapped_line_cross_size = std::max(
                    wrapped_line_cross_size,
                    wrapped_has_definite_cross_size
                        ? stretched_wrapped_line_cross_size
                        : margin_top + height + margin_bottom);
            }
            const auto alignment = child->style.align_self_specified
                ? child->style.align_self : parent.style.align_items;
            const auto y = alignment == align_mode::center
                ? content.y + wrapped_cross_offset + margin_top
                    + (available_line_cross_size - margin_top - margin_bottom - height) * 0.5F
                : alignment == align_mode::end
                    ? content.y + wrapped_cross_offset + available_line_cross_size
                        - margin_bottom - height
                    : content.y + wrapped_cross_offset + margin_top;
            assigned = {
                content.x + cursor + margin_left,
                y,
                width,
                height};
            cursor += margin_left + width + margin_right + justify_gap;
            if (flex_container && parent.style.flex_wrap) ++placed_in_wrapped_line;
        } else {
            auto margin_left = resolve_length(
                *child, child->style.margin_left, content.width, 0);
            auto margin_right = resolve_length(
                *child, child->style.margin_right, content.width, 0);
            const auto margin_top = child->style.margin_top_auto
                ? automatic_margin_share
                : resolve_length(*child, child->style.margin_top, content.height, 0);
            const auto margin_bottom = child->style.margin_bottom_auto
                ? automatic_margin_share
                : resolve_length(*child, child->style.margin_bottom, content.height, 0);
            vertical_margin_bottom = margin_bottom;
            const auto intrinsic_height = intrinsic_size(*child, false, content.height);
            auto height = is_specified(child->style.height)
                ? outer_authored_size(*child, false, content.height)
                    + (flex_container && grow > 0
                        ? remaining * std::max(0.0F, child->style.flex_grow) / grow
                        : 0.0F)
                : intrinsic_height > 0
                    ? intrinsic_height + (flex_container && grow > 0
                        ? remaining * std::max(0.0F, child->style.flex_grow) / grow
                        : 0.0F)
                : !flex_container
                    ? 0.0F
                : grow > 0
                    ? remaining * std::max(0.0F, child->style.flex_grow) / grow
                    : automatic_count > 0 ? remaining / automatic_count : remaining;
            height = constrain_size(*child, false, height, content.height);
            if (const auto shrunk = flex_shrunk_main_sizes.find(child);
                shrunk != flex_shrunk_main_sizes.end()) {
                height = constrain_size(*child, false, shrunk->second, content.height);
            }
            auto width = is_specified(child->style.width)
                ? outer_authored_size(*child, true, content.width)
                : std::max(0.0F, content.width - margin_left - margin_right);
            width = constrain_size(*child, true, width, content.width);
            const auto horizontal_margin_is_auto = child->style.margin_left_auto
                || child->style.margin_right_auto;
            if (horizontal_margin_is_auto) {
                const auto remaining_inline_space = std::max(
                    0.0F,
                    content.width - width
                        - (child->style.margin_left_auto ? 0.0F : margin_left)
                        - (child->style.margin_right_auto ? 0.0F : margin_right));
                if (child->style.margin_left_auto && child->style.margin_right_auto) {
                    margin_left = remaining_inline_space * 0.5F;
                    margin_right = remaining_inline_space * 0.5F;
                } else if (child->style.margin_left_auto) {
                    margin_left = remaining_inline_space;
                } else {
                    margin_right = remaining_inline_space;
                }
            }
            const auto alignment = child->style.align_self_specified
                ? child->style.align_self : parent.style.align_items;
            const auto x = horizontal_margin_is_auto
                ? content.x + margin_left
                : alignment == align_mode::center
                ? content.x + margin_left
                    + (content.width - margin_left - margin_right - width) * 0.5F
                : alignment == align_mode::end
                    ? content.x + content.width - margin_right - width
                    : content.x + margin_left;
            assigned = {
                x,
                content.y + cursor + margin_top,
                width,
                height};
        }
        auto positioned = assigned;
        if (child->style.position == position_mode::relative) {
            if (is_specified(child->style.left)) {
                positioned.x += resolve_length(
                    *child, child->style.left, content.width, 0);
            } else if (is_specified(child->style.right)) {
                positioned.x -= resolve_length(
                    *child, child->style.right, content.width, 0);
            }
            if (is_specified(child->style.top)) {
                // A percentage relative inset is auto when the containing
                // block's height is indefinite. This applies to the entire
                // calc() expression, including any pixel offset mixed into it.
                if (child->style.top.unit != length_unit::percent
                    || is_specified(parent.style.height)) {
                    positioned.y += resolve_length(
                        *child, child->style.top, content.height, 0);
                }
            } else if (is_specified(child->style.bottom)) {
                if (child->style.bottom.unit != length_unit::percent
                    || is_specified(parent.style.height)) {
                    positioned.y -= resolve_length(
                        *child, child->style.bottom, content.height, 0);
                }
            }
        }
        positioned.x -= parent.scroll_left;
        positioned.y -= parent.scroll_top;
        layout_child(*child, parent.layout, positioned);
        // The retained native viewport owns scrolling while a loaded document
        // exposes an HTML element beneath it.  The HTML border box remains
        // viewport-sized, but its BODY can establish a larger scrollable
        // overflow area.  Propagate that root overflow to the viewport owner;
        // otherwise document.scrollingElement reports a permanently bounded
        // 0px range even though the document content is much taller.
        const auto propagates_document_overflow = &parent == body_
            && child->tag == "html";
        const auto flow_width = propagates_document_overflow
            ? std::max(child->layout.width, child->scroll_content_width)
            : child->layout.width;
        const auto flow_height = propagates_document_overflow
            ? std::max(child->layout.height, child->scroll_content_height)
            : child->layout.height;
        maximum_flow_right = std::max(
            maximum_flow_right,
            assigned.x + flow_width + parent.scroll_left);
        maximum_flow_bottom = std::max(
            maximum_flow_bottom,
            assigned.y + flow_height + parent.scroll_top);
        if (!horizontal) {
            cursor = assigned.y - content.y
                + flow_height
                + vertical_margin_bottom
                + justify_gap;
        }
        ++placed_flow_count;
    }
    if (flex_container && horizontal && !parent.style.flex_wrap) {
        std::function<std::optional<float>(const dom_node&)> first_text_baseline =
            [&](const dom_node& node) -> std::optional<float> {
                if (node.visible && node.tag == "#text" && has_visible_text(node.text_content)) {
                    const auto font_size = resolved_font_size(node);
                    const auto line_height = resolved_line_height(node, font_size);
                    // CSS baseline alignment uses the font's alphabetic ascent,
                    // not the visual midpoint of the glyph box. The native
                    // document currently receives width measurements only, so
                    // use a 90% em ascent fallback calibrated against the
                    // platform sans-serif metrics used by the Chrome oracle.
                    // Keeping the half-leading separate preserves line-height.
                    return node.layout.y
                        + std::max(0.0F, line_height - font_size) * 0.5F
                        + font_size * 0.9F;
                }
                for (const auto* descendant : node.children) {
                    if (const auto baseline = first_text_baseline(*descendant)) return baseline;
                }
                return std::nullopt;
            };
        const auto uses_baseline = [&](const dom_node& node) {
            return (node.style.align_self_specified
                ? node.style.align_self : parent.style.align_items) == align_mode::baseline;
        };
        auto target_baseline = -std::numeric_limits<float>::infinity();
        for (const auto* child : ordered_children) {
            if (!participates_in_flow(child) || !child->visible || !uses_baseline(*child)) continue;
            target_baseline = std::max(
                target_baseline,
                first_text_baseline(*child).value_or(child->layout.y + child->layout.height));
        }
        if (std::isfinite(target_baseline)) {
            const auto shift_subtree_y = [&](const auto& self, dom_node& node, float offset) -> void {
                node.layout.y += offset;
                for (auto& fragment : node.text_layout_fragments) fragment.y += offset;
                for (auto* descendant : node.children) self(self, *descendant, offset);
            };
            for (auto* child : ordered_children) {
                if (!participates_in_flow(child) || !child->visible || !uses_baseline(*child)) continue;
                const auto baseline = first_text_baseline(*child)
                    .value_or(child->layout.y + child->layout.height);
                shift_subtree_y(shift_subtree_y, *child, target_baseline - baseline);
            }
        }
    }
    if (inline_flow && !flex_container) {
        // Inline-level boxes participate in a line box, not a flex cross axis.
        // A full-line-height generated inline box plus smaller siblings using
        // `vertical-align: middle` is a common loader-centering composition.
        auto line_box_height = content.height;
        for (const auto* child : ordered_children) {
            if (!participates_in_flow(child) || !child->visible) continue;
            line_box_height = std::max(
                line_box_height,
                child->layout.height
                    + resolve_length(
                        *child, child->style.margin_top, content.height, 0)
                    + resolve_length(
                        *child, child->style.margin_bottom, content.height, 0));
        }
        const auto shift_subtree_y = [&](const auto& self, dom_node& node, float offset) -> void {
            node.layout.y += offset;
            for (auto& fragment : node.text_layout_fragments) fragment.y += offset;
            for (auto* descendant : node.children) self(self, *descendant, offset);
        };
        for (auto* child : ordered_children) {
            if (!participates_in_flow(child) || !child->visible
                || child->style.textual().vertical_align != "middle") continue;
            const auto margin_top = resolve_length(
                *child, child->style.margin_top, line_box_height, 0);
            const auto margin_bottom = resolve_length(
                *child, child->style.margin_bottom, line_box_height, 0);
            const auto target_y = content.y + margin_top
                + (line_box_height - margin_top - margin_bottom
                    - child->layout.height) * 0.5F;
            shift_subtree_y(shift_subtree_y, *child, target_y - child->layout.y);
        }
    }
    parent.scroll_content_width = horizontal
        ? std::max(content.width, cursor)
        : std::max(content.width, maximum_flow_right - content.x);
    parent.scroll_content_height = horizontal
        ? std::max(
            content.height,
            parent.style.flex_wrap
                ? wrapped_cross_offset + wrapped_line_cross_size
                : maximum_flow_bottom - content.y)
        : std::max(
            content.height,
            std::max(cursor, maximum_float_bottom - content.y));
    // Content can shrink while the user is already scrolled. Re-clamp against
    // the freshly computed extent in the same layout pass; clamping only at
    // the start used the previous extent and left a stale, apparently endless
    // offset until another unrelated relayout occurred.
    parent.scroll_left = std::clamp(
        parent.scroll_left,
        0.0F,
        std::max(0.0F, parent.scroll_content_width - content.width));
    parent.scroll_top = std::clamp(
        parent.scroll_top,
        0.0F,
        std::max(0.0F, parent.scroll_content_height - content.height));
    if (before_item.has_value()) {
        parent.style.mutable_before_pseudo().layout = before_item->layout;
    }
    if (after_item.has_value()) {
        parent.style.mutable_after_pseudo().layout = after_item->layout;
    }
    if (marker_item.has_value()) {
        parent.list_marker_layout = marker_item->layout;
    }
}

float native_document::intrinsic_size(
    const dom_node& node,
    bool horizontal,
    float available)
{
    const auto padding = horizontal
        ? resolve_length(node, node.style.padding_left, available, 0)
            + resolve_length(node, node.style.padding_right, available, 0)
            + resolve_length(node, node.style.border_left_width, available, 0)
            + resolve_length(node, node.style.border_right_width, available, 0)
        : resolve_length(node, node.style.padding_top, available, 0)
            + resolve_length(node, node.style.padding_bottom, available, 0)
            + resolve_length(node, node.style.border_top_width, available, 0)
            + resolve_length(node, node.style.border_bottom_width, available, 0);
    const auto constrain = [&](float size) {
        const auto& minimum = horizontal ? node.style.min_width : node.style.min_height;
        const auto& maximum = horizontal ? node.style.max_width : node.style.max_height;
        const auto box_adjustment = node.style.border_box ? 0.0F : padding;
        if (is_specified(maximum)) {
            size = std::min(
                size,
                resolve_length(node, maximum, available, size) + box_adjustment);
        }
        if (is_specified(minimum)) {
            size = std::max(
                size,
                resolve_length(node, minimum, available, 0) + box_adjustment);
        }
        return std::max(0.0F, size);
    };
    const auto authored = horizontal ? node.style.width : node.style.height;
    // A percentage size is indefinite while shrink-to-fitting an auto-sized
    // ancestor. Resolving it against the viewport makes fixed popups with
    // descendants such as `width: 100%` or `height: 100%` measure as the whole
    // viewport. Use the descendant content contribution in that case; once the
    // ancestor has a definite box, layout_children resolves the percentage.
    if (authored.unit == length_unit::pixels
        || authored.unit == length_unit::em
        || authored.unit == length_unit::rem
        || authored.unit == length_unit::viewport_width
        || authored.unit == length_unit::viewport_height) {
        return constrain(resolve_length(node, authored, available, 0)
            + (node.style.border_box ? 0.0F : padding));
    }
    if ((is_table_container(node.style.display) && has_table_row_descendant(node))
        || is_table_row_group(node.style.display)
        || node.style.display == display_mode::table_row) {
        std::vector<const dom_node*> rows;
        std::function<void(const dom_node&)> collect_rows = [&](const dom_node& current) {
            for (const auto* child : current.children) {
                if (child->style.display == display_mode::table_row) rows.push_back(child);
                else if (is_table_row_group(child->style.display)) collect_rows(*child);
            }
        };
        if (node.style.display == display_mode::table_row) rows.push_back(&node);
        else collect_rows(node);
        if (horizontal) {
            size_t count = 0;
            for (const auto* row : rows) {
                size_t row_count = 0;
                for (const auto* cell : row->children) {
                    if (cell->style.display != display_mode::table_cell) continue;
                    auto span = cell->attributes.find("colspan");
                    if (span == cell->attributes.end()) span = cell->attributes.find("colSpan");
                    row_count += span == cell->attributes.end()
                        ? 1U : std::max<size_t>(1, static_cast<size_t>(parse_number(span->second, 1)));
                }
                count = std::max(count, row_count);
            }
            std::vector<float> columns(count, 0.0F);
            for (const auto* row : rows) {
                size_t column = 0;
                for (const auto* cell : row->children) {
                    if (cell->style.display != display_mode::table_cell) continue;
                    auto span_value = cell->attributes.find("colspan");
                    if (span_value == cell->attributes.end()) {
                        span_value = cell->attributes.find("colSpan");
                    }
                    const auto span = span_value == cell->attributes.end()
                        ? 1U : std::max<size_t>(1, static_cast<size_t>(parse_number(span_value->second, 1)));
                    const auto share = intrinsic_size(*cell, true, available)
                        / static_cast<float>(span);
                    for (size_t track = column; track < std::min(count, column + span); ++track) {
                        columns[track] = std::max(columns[track], share);
                    }
                    column += span;
                }
            }
            return constrain(
                std::accumulate(columns.begin(), columns.end(), 0.0F) + padding);
        }
        auto height = 0.0F;
        for (const auto* row : rows) {
            auto row_height = is_specified(row->style.height)
                ? resolve_length(*row, row->style.height, available, 0) : 0.0F;
            for (const auto* cell : row->children) {
                if (cell->style.display == display_mode::table_cell) {
                    row_height = std::max(row_height, intrinsic_size(*cell, false, available));
                }
            }
            height += row_height;
        }
        return constrain(height + padding);
    }
    if (node.tag == "input") {
        auto type = std::string{"text"};
        if (const auto value = node.attributes.find("type"); value != node.attributes.end()) {
            type = value->second;
            std::transform(type.begin(), type.end(), type.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
        }
        if (type == "hidden") return constrain(0);
        const auto font_size = resolved_font_size(node);
        if (!horizontal) {
            return constrain(resolved_line_height(node, font_size) + padding);
        }
        if (type == "checkbox" || type == "radio" || type == "color" || type == "range") {
            return constrain(resolved_line_height(node, font_size) + padding);
        }
        if (type == "button" || type == "submit" || type == "reset") {
            auto label = node.form_control().value;
            if (label.empty()) {
                if (const auto value = node.attributes.find("value"); value != node.attributes.end()) {
                    label = value->second;
                }
            }
            return constrain(
                measure_text_width(label.empty() ? "Submit" : label, node)
                + font_size * 2.0F + padding);
        }
        auto columns = 20.0F;
        if (const auto size = node.attributes.find("size"); size != node.attributes.end()) {
            columns = std::max(1.0F, parse_number(size->second, columns));
        }
        // Text-like input elements are replaced controls with a default
        // intrinsic inline size of 20 columns. Their percentage width is
        // indefinite while an auto-sized wrapper is shrink-to-fit, so the
        // replaced-element contribution must survive that measurement pass.
        // Portaled time controls rely on this before a max-width cap is
        // applied to the wrapper and used to size its portaled listbox.
        return constrain(columns * measure_text_width("0", node) + padding);
    }
    if (node.tag == "svg" || node.tag == "img" || node.tag == "canvas") {
        const auto attribute = node.attributes.find(horizontal ? "width" : "height");
        if (attribute != node.attributes.end()) {
            const auto intrinsic = parse_number(attribute->second, 0);
            if (intrinsic > 0) return constrain(intrinsic + padding);
        }
        if (node.tag == "svg") {
            const auto view_box = node.attributes.find("viewBox");
            if (view_box != node.attributes.end()) {
                std::istringstream values(view_box->second);
                float x = 0;
                float y = 0;
                float width = 0;
                float height = 0;
                if (values >> x >> y >> width >> height) {
                    const auto intrinsic = horizontal ? width : height;
                    if (intrinsic > 0) return constrain(intrinsic + padding);
                }
            }
        }
    }
    const auto has_before = pseudo_generates_box(node.style.before_pseudo());
    const auto has_after = pseudo_generates_box(node.style.after_pseudo());
    const auto marker_text = list_marker_text(node);
    const auto has_inside_marker = !marker_text.empty()
        && resolved_list_style(node, true) == "inside";
    if (node.children.empty() && !has_before && !has_after && !has_inside_marker) {
        if (!has_visible_text(node.text_content)) {
            const auto measured = horizontal && is_collapsible_whitespace_text(node)
                ? measure_text_width(" ", node)
                : 0;
            return constrain(measured);
        }
        const auto font_size = resolved_font_size(node);
        return constrain(padding + (horizontal
            ? measure_text_width(
                collapsed_text(
                    node,
                    resolved_text_transform(node, node.text_content)),
                node)
            : resolved_line_height(node, font_size)));
    }

    if (is_grid_container(node.style.display) && node.style.grid().two_columns) {
        float first_column = 0;
        float second_column = 0;
        float row_height = 0;
        float total_height = 0;
        bool awaiting_second = false;
        for (const auto* child : node.children) {
            if (child->style.display == display_mode::none
                || is_out_of_flow(child->style.position)) continue;
            const auto child_width = intrinsic_size(*child, true, available)
                + resolve_length(*child, child->style.margin_left, available, 0)
                + resolve_length(*child, child->style.margin_right, available, 0);
            const auto child_height = intrinsic_size(*child, false, available)
                + resolve_length(*child, child->style.margin_top, available, 0)
                + resolve_length(*child, child->style.margin_bottom, available, 0);
            if (child->style.grid().span_all) {
                if (awaiting_second) {
                    total_height += row_height;
                    row_height = 0;
                    awaiting_second = false;
                }
                total_height += child_height;
                first_column = std::max(first_column, child_width);
                continue;
            }
            if (!awaiting_second) {
                first_column = std::max(first_column, child_width);
                row_height = child_height;
                awaiting_second = true;
            } else {
                second_column = std::max(second_column, child_width);
                row_height = std::max(row_height, child_height);
                total_height += row_height;
                row_height = 0;
                awaiting_second = false;
            }
        }
        if (awaiting_second) total_height += row_height;
        return constrain(padding + (horizontal
            ? first_column + second_column
            : total_height));
    }

    std::optional<dom_node> marker_item;
    std::optional<dom_node> before_item;
    std::optional<dom_node> after_item;
    std::vector<const dom_node*> intrinsic_items;
    intrinsic_items.reserve(node.children.size() + 3U);
    if (has_inside_marker) {
        marker_item.emplace(make_list_marker_layout_node(node, marker_text));
        intrinsic_items.push_back(&*marker_item);
    }
    if (has_before) {
        before_item.emplace(make_pseudo_layout_node(node, node.style.before_pseudo()));
        intrinsic_items.push_back(&*before_item);
    }
    intrinsic_items.insert(intrinsic_items.end(), node.children.begin(), node.children.end());
    if (has_after) {
        after_item.emplace(make_pseudo_layout_node(node, node.style.after_pseudo()));
        intrinsic_items.push_back(&*after_item);
    }

    const auto flex_container = is_flex_container(node.style.display);
    const auto inline_flow = !flex_container && std::all_of(
        intrinsic_items.begin(),
        intrinsic_items.end(),
        [](const dom_node* child) {
            return child->style.display == display_mode::none
                || is_out_of_flow(child->style.position)
                || is_inline_level(child->style.display);
        });
    const auto children_horizontal = (flex_container && node.style.direction == flex_direction::row)
        || inline_flow;
    float result = 0;
    size_t accumulated_children = 0U;
    for (const auto* child : intrinsic_items) {
        if (child->style.display == display_mode::none
            || is_out_of_flow(child->style.position)
            || (flex_container && is_collapsible_whitespace_text(*child))) continue;
        const auto child_size = intrinsic_size(*child, horizontal, available);
        const auto child_margin = horizontal
            ? resolve_length(*child, child->style.margin_left, available, 0)
                + resolve_length(*child, child->style.margin_right, available, 0)
            : resolve_length(*child, child->style.margin_top, available, 0)
                + resolve_length(*child, child->style.margin_bottom, available, 0);
        const auto accumulates = horizontal == children_horizontal;
        if (accumulates && flex_container && accumulated_children > 0U) {
            result += resolve_length(
                node,
                horizontal ? node.style.column_gap : node.style.row_gap,
                available,
                0);
        }
        result = accumulates
            ? result + child_size + child_margin
            : std::max(result, child_size + child_margin);
        ++accumulated_children;
    }
    return constrain(result + padding);
}

void native_document::layout_child(dom_node& child, const layout_rect&, layout_rect assigned)
{
    // A CSS transform changes the painted and hit-test box, not the space the
    // element occupies in its parent's flow. Percentage translations resolve
    // against the transformed element's own border box.
    const auto* animation = child.animation_runtime();
    const auto painted_translate_x =
        animation != nullptr && animation->transform_animation_initialized
        ? animation->painted_transform_translate_x
        : child.style.transform_translate_x;
    const auto painted_translate_y =
        animation != nullptr && animation->transform_animation_initialized
        ? animation->painted_transform_translate_y
        : child.style.transform_translate_y;
    assigned.x += resolve_length(child, painted_translate_x, assigned.width, 0);
    assigned.y += resolve_length(child, painted_translate_y, assigned.height, 0);
    child.text_layout_fragments.clear();
    child.layout = {
        assigned.x,
        assigned.y,
        std::max(0.0F, assigned.width),
        std::max(0.0F, assigned.height)};
    const auto automatic_height = !is_specified(child.style.height);
    if (automatic_height && child.tag == "#text" && has_visible_text(child.text_content)) {
        const auto font_size = resolved_font_size(child);
        const auto line_height = resolved_line_height(child, font_size);
        const auto lines = wrap_text_lines(
            child.text_content,
            child.layout.width,
            child,
            resolved_white_space_wraps(child));
        child.layout.height = std::max(
            child.layout.height,
            static_cast<float>(lines.size()) * line_height);
    }
    layout_children(child);
    const auto parent_constrains_height = child.parent != nullptr
        && (is_specified(child.parent->style.height)
            || is_specified(child.parent->style.max_height));
    const auto constrained_overflow_height = child.style.overflow_y != overflow_mode::visible
        && (is_specified(child.style.max_height)
            || (child.parent != nullptr
                && is_flex_container(child.parent->style.display)
                && parent_constrains_height));
    const auto constrained_column_flex_item = child.parent != nullptr
        && is_flex_container(child.parent->style.display)
        && child.parent->style.direction == flex_direction::column
        && parent_constrains_height;
    const auto table_track_sized = is_table_row_group(child.style.display)
        || child.style.display == display_mode::table_row
        || child.style.display == display_mode::table_cell;
    if (automatic_height && !constrained_overflow_height && !constrained_column_flex_item
        && !table_track_sized && !child.children.empty()) {
        auto content_bottom = child.layout.y;
        for (const auto* descendant : child.children) {
            if (descendant == nullptr || !descendant->visible
                || is_out_of_flow(descendant->style.position)) continue;
            content_bottom = std::max(
                content_bottom,
                descendant->layout.y + descendant->layout.height
                    + resolve_length(
                        *descendant,
                        descendant->style.margin_bottom,
                        child.layout.height,
                        0));
        }
        const auto padding_bottom = resolve_length(
            child,
            child.style.padding_bottom,
            child.layout.height,
            0);
        const auto border_bottom = resolve_length(
            child,
            child.style.border_bottom_width,
            child.layout.height,
            0);
        child.layout.height = std::max(
            child.layout.height,
            content_bottom - child.layout.y + padding_bottom + border_bottom);

        // A one-track auto-sized grid takes its max-content contribution from
        // the content inside transparent/scroll wrappers. Those wrappers can
        // initially resolve percentage heights against the first intrinsic
        // estimate, so considering only direct child boxes leaves the grid at
        // that stale estimate and clips later inline reflow. Include in-flow
        // descendants, then perform one definitive layout at the expanded size.
        if (is_grid_container(child.style.display) && !child.style.grid().two_columns) {
            std::function<float(const dom_node&)> deepest_flow_bottom =
                [&](const dom_node& node) {
                    auto bottom = node.layout.y;
                    for (const auto* descendant : node.children) {
                        if (descendant == nullptr || !descendant->visible
                            || is_out_of_flow(descendant->style.position)) continue;
                        bottom = std::max(
                            bottom,
                            std::max(
                                descendant->layout.y + descendant->layout.height,
                                deepest_flow_bottom(*descendant)));
                    }
                    return bottom;
                };
            const auto expanded_height = deepest_flow_bottom(child) - child.layout.y
                + padding_bottom + border_bottom;
            if (expanded_height > child.layout.height + 0.01F) {
                child.layout.height = expanded_height;
                layout_children(child);
            }
        }
    }
}

void native_document::build_scene(
    std::vector<htmlml_scene_command>& commands,
    std::vector<htmlml_scene_string>& strings,
    std::vector<char>& string_bytes) const
{
    commands.clear();
    commands.reserve(nodes_.size());
    strings.clear();
    string_bytes.clear();
    append_scene(*body_, commands, strings, string_bytes, false, true);
    std::vector<dom_node*> fixed;
    for (auto* child : body_->children) {
        collect_fixed_positioned_nodes(*child, fixed);
    }
    for (const auto* fixed_node : fixed) {
        auto inherited_visibility_hidden = false;
        std::vector<const dom_node*> ancestors;
        for (auto* ancestor = fixed_node->parent;
             ancestor != nullptr;
             ancestor = ancestor->parent) {
            ancestors.push_back(ancestor);
        }
        for (auto iterator = ancestors.rbegin(); iterator != ancestors.rend(); ++iterator) {
            if ((*iterator)->style.visibility_specified) {
                inherited_visibility_hidden = (*iterator)->style.visibility_hidden;
            }
        }
        append_scene(
            *fixed_node,
            commands,
            strings,
            string_bytes,
            inherited_visibility_hidden,
            true);
    }
}

void native_document::build_canvas_layouts(
    std::vector<htmlml_canvas_layout>& layouts) const
{
    layouts.clear();
    for (const auto& node : nodes_) {
        if (node->tag != "canvas" || !is_connected(*node) || !node->visible) continue;
        const auto width = node->attributes.find("width");
        const auto height = node->attributes.find("height");
        layouts.push_back(htmlml_canvas_layout{
            node->id,
            0U,
            node->layout.x,
            node->layout.y,
            node->layout.width,
            node->layout.height,
            static_cast<uint32_t>(std::max(
                0.0F,
                width == node->attributes.end() ? 0.0F : parse_number(width->second))),
            static_cast<uint32_t>(std::max(
                0.0F,
                height == node->attributes.end() ? 0.0F : parse_number(height->second)))});
    }
}

void native_document::build_canvas_display_lists(
    std::vector<htmlml_canvas_layer>& layers,
    std::vector<htmlml_canvas_command>& canvas_commands,
    std::vector<htmlml_scene_string>& strings,
    std::vector<char>& string_bytes) const
{
    layers.clear();
    canvas_commands.clear();
    strings.clear();
    string_bytes.clear();

    uint32_t z_order = 0;
    for (const auto& node : nodes_) {
        const auto& canvas = node->canvas();
        if (node->tag != "canvas"
            || !is_connected(*node)
            || !node->visible
            || canvas.commands.empty()) {
            continue;
        }

        const auto width = node->attributes.find("width");
        const auto height = node->attributes.find("height");
        const auto command_offset = static_cast<uint32_t>(canvas_commands.size());
        const auto string_offset = static_cast<uint32_t>(strings.size());
        canvas_commands.insert(
            canvas_commands.end(),
            canvas.commands.begin(),
            canvas.commands.end());
        for (const auto& value : canvas.strings) {
            const auto byte_offset = static_cast<uint32_t>(string_bytes.size());
            string_bytes.insert(string_bytes.end(), value.begin(), value.end());
            strings.push_back(htmlml_scene_string{
                byte_offset,
                static_cast<uint32_t>(value.size())});
        }

        layers.push_back(htmlml_canvas_layer{
            node->id,
            z_order++,
            command_offset,
            static_cast<uint32_t>(canvas.commands.size()),
            string_offset,
            static_cast<uint32_t>(canvas.strings.size()),
            0U,
            node->layout.x,
            node->layout.y,
            node->layout.width,
            node->layout.height,
            static_cast<uint32_t>(std::max(
                0.0F,
                width == node->attributes.end() ? 300.0F : parse_number(width->second, 300.0F))),
            static_cast<uint32_t>(std::max(
                0.0F,
                height == node->attributes.end() ? 150.0F : parse_number(height->second, 150.0F))),
            canvas.generation});
    }
}

void native_document::append_scene(
    const dom_node& node,
    std::vector<htmlml_scene_command>& commands,
    std::vector<htmlml_scene_string>& strings,
    std::vector<char>& string_bytes,
    bool inherited_visibility_hidden,
    bool defer_fixed_descendants) const
{
    const auto* animation = node.animation_runtime();
    const auto opacity = std::clamp(
        animation != nullptr && animation->opacity_animation_initialized
            ? animation->painted_opacity
            : node.style.opacity,
        0.0F,
        1.0F);
    if (!node.visible || opacity <= 0.001F) {
        return;
    }
    const auto has_opacity_group = opacity < 0.999F;
    if (has_opacity_group) {
        commands.push_back(htmlml_scene_command{
            30U,
            0U,
            node.layout.x,
            node.layout.y,
            node.layout.width,
            node.layout.height,
            static_cast<uint32_t>(std::clamp(
                std::lround(opacity * 255.0F),
                0L,
                255L)),
            node.id});
    }
    const auto resolve_transform_origin = [](css_length value, float available, float fallback) {
        if (value.unit == length_unit::pixels) return value.value;
        if (value.unit == length_unit::percent) {
            return available * value.value / 100.0F + value.pixel_offset;
        }
        return fallback;
    };
    const auto transform_origin_x = node.layout.x + resolve_transform_origin(
        node.style.transform_origin_x,
        node.layout.width,
        node.layout.width * 0.5F);
    const auto transform_origin_y = node.layout.y + resolve_transform_origin(
        node.style.transform_origin_y,
        node.layout.height,
        node.layout.height * 0.5F);
    const auto painted_scale_x =
        animation != nullptr && animation->transform_animation_initialized
        ? animation->painted_transform_scale_x
        : node.style.transform_scale_x;
    const auto painted_scale_y =
        animation != nullptr && animation->transform_animation_initialized
        ? animation->painted_transform_scale_y
        : node.style.transform_scale_y;
    const auto painted_rotation =
        animation != nullptr && animation->transform_animation_initialized
        ? animation->painted_transform_rotate_degrees
        : node.style.transform_rotate_degrees;
    const auto has_scale_transform =
        std::abs(painted_scale_x - 1.0F) > 0.0001F
        || std::abs(painted_scale_y - 1.0F) > 0.0001F;
    if (has_scale_transform) {
        commands.push_back(htmlml_scene_command{
            15U,
            0U,
            transform_origin_x,
            transform_origin_y,
            painted_scale_x,
            painted_scale_y,
            0U,
            node.id});
    }
    const auto has_rotate_transform = std::abs(painted_rotation) > 0.0001F;
    if (has_rotate_transform) {
        commands.push_back(htmlml_scene_command{
            19U,
            0U,
            transform_origin_x,
            transform_origin_y,
            0,
            0,
            0U,
            node.id,
            0,
            0,
            0,
            0,
            painted_rotation});
    }
    const auto with_opacity = [](uint32_t rgba) { return rgba; };
    // Unlike display:none, visibility:hidden does not prune descendants. A child
    // may explicitly restore visibility:visible; component libraries use that contract
    // to switch between its collapsed and expanded responsive toolbar wrappers.
    const auto visibility_hidden = node.style.visibility_specified
        ? node.style.visibility_hidden
        : inherited_visibility_hidden;
    const auto paint_self = !visibility_hidden;
    const auto paint_box_in_foreground = [&] {
        auto fixed_layer = false;
        auto outermost_z_index = 0;
        for (auto* current = &node; current != nullptr; current = current->parent) {
            // A fixed popup participates in the elevated overlay layer even
            // when a backdrop child uses z-index:-1 relative to that popup.
            if (current->style.position == position_mode::fixed) fixed_layer = true;
            // A negative descendant stays inside its ancestor's stacking context.
            // Its own level orders it behind siblings in that context; it must not
            // demote an otherwise elevated legend or dialog behind retained canvases.
            if (current->style.z_index != 0) {
                outermost_z_index = current->style.z_index;
            }
            // Fixed-position dialogs and menus form an elevated paint layer in
            // component UI even when its authored z-index is auto. Its DOM
            // backgrounds must be emitted after retained chart canvases, just
            // like their text and SVG foreground commands.
        }
        return node.paints_after_retained_canvas || fixed_layer || outermost_z_index > 0;
    }();
    struct resolved_radii final {
        float top_left;
        float top_right;
        float bottom_right;
        float bottom_left;

        bool any() const noexcept
        {
            return top_left > 0 || top_right > 0 || bottom_right > 0 || bottom_left > 0;
        }
    };
    const auto resolve_radii = [&](css_length top_left, css_length top_right,
                                   css_length bottom_right, css_length bottom_left,
                                   float width, float height) {
        const auto limit = std::max(0.0F, std::min(width, height) * 0.5F);
        const auto reference = std::min(width, height);
        return resolved_radii{
            std::clamp(resolve_length(node, top_left, reference, 0), 0.0F, limit),
            std::clamp(resolve_length(node, top_right, reference, 0), 0.0F, limit),
            std::clamp(resolve_length(node, bottom_right, reference, 0), 0.0F, limit),
            std::clamp(resolve_length(node, bottom_left, reference, 0), 0.0F, limit)};
    };
    const auto append_pseudo = [&](const node_style::pseudo_element& pseudo) {
        if (!paint_self || !pseudo.generated || pseudo.display_none || pseudo.visibility_hidden) return;
        const auto pseudo_paints_in_foreground = paint_box_in_foreground
            || (pseudo.z_index > 0 && pseudo.position != position_mode::normal);
        const auto pseudo_opacity = std::clamp(pseudo.opacity, 0.0F, 1.0F);
        if (pseudo_opacity <= 0.001F) return;
        const auto with_pseudo_opacity = [&](uint32_t rgba) {
            const auto alpha = static_cast<uint32_t>(std::clamp(
                std::lround(static_cast<float>(rgba & 0xFFU) * pseudo_opacity),
                0L,
                255L));
            return with_opacity((rgba & 0xFFFFFF00U) | alpha);
        };
        const auto inherited_font_size = resolved_font_size(node);
        const auto font_size = pseudo.font_size >= 0 ? pseudo.font_size : inherited_font_size;
        auto line_height = pseudo.line_height >= 0
            ? pseudo.line_height
            : resolved_line_height(node, font_size);
        if (line_height > 0 && line_height <= 4) line_height *= font_size;
        const auto x = pseudo.layout.x;
        const auto y = pseudo.layout.y;
        const auto width = pseudo.layout.width;
        const auto height = pseudo.layout.height;
        const auto pseudo_background = pseudo.background_current_color
            ? (pseudo.foreground_rgba & 0xFFU) != 0U
                ? pseudo.foreground_rgba
                : resolved_foreground(node)
            : pseudo.background_rgba;
        if ((pseudo_background & 0xFFU) != 0U && width > 0 && height > 0) {
            const auto radii = resolve_radii(
                pseudo.border_top_left_radius,
                pseudo.border_top_right_radius,
                pseudo.border_bottom_right_radius,
                pseudo.border_bottom_left_radius,
                width,
                height);
            commands.push_back(htmlml_scene_command{
                radii.any()
                    ? pseudo_paints_in_foreground ? 10U : 7U
                    : pseudo_paints_in_foreground ? 9U : 1U,
                0U,
                x,
                y,
                width,
                height,
                with_pseudo_opacity(pseudo_background),
                node.id,
                radii.top_left,
                radii.top_right,
                radii.bottom_right,
                radii.bottom_left,
                0});
        }
        if (has_visible_text(pseudo.content)) {
            std::ostringstream resource;
            resource << font_size << '\t' << line_height << '\t'
                << resolved_font_weight(node) << '\t' << resolved_text_align(node) << '\t'
                << resolved_font_family(node) << '\t' << pseudo.content;
            commands.push_back(htmlml_scene_command{
                3U,
                append_scene_string(resource.str(), strings, string_bytes),
                x,
                y,
                width,
                height,
                (pseudo.foreground_rgba & 0xFFU) != 0U
                    ? with_pseudo_opacity(pseudo.foreground_rgba)
                    : with_pseudo_opacity(resolved_foreground(node)),
                node.id});
        }
    };
    const auto radii = resolve_radii(
        node.style.border_top_left_radius,
        node.style.border_top_right_radius,
        node.style.border_bottom_right_radius,
        node.style.border_bottom_left_radius,
        node.layout.width,
        node.layout.height);
    const auto pack_round_rect = [](float packed_radius, float stroke_width) {
        const auto radius_bits = static_cast<uint32_t>(std::clamp(
            std::lround(packed_radius * 100.0F), 0L, 65535L));
        const auto width_bits = static_cast<uint32_t>(std::clamp(
            std::lround(stroke_width * 100.0F), 0L, 65535L));
        return (radius_bits << 16U) | width_bits;
    };
    if (paint_self
        && node.style.box_shadow_present
        && node.layout.width > 0
        && node.layout.height > 0) {
        const auto spread = node.style.box_shadow_spread_radius;
        const auto shadow_width = node.layout.width + spread * 2.0F;
        const auto shadow_height = node.layout.height + spread * 2.0F;
        if (shadow_width > 0 && shadow_height > 0) {
            commands.push_back(htmlml_scene_command{
                paint_box_in_foreground ? 18U : 17U,
                0U,
                node.layout.x + node.style.box_shadow_offset_x - spread,
                node.layout.y + node.style.box_shadow_offset_y - spread,
                shadow_width,
                shadow_height,
                with_opacity(node.style.box_shadow_rgba),
                node.id,
                std::max(0.0F, radii.top_left + spread),
                std::max(0.0F, radii.top_right + spread),
                std::max(0.0F, radii.bottom_right + spread),
                std::max(0.0F, radii.bottom_left + spread),
                node.style.box_shadow_blur_radius});
        }
    }
    if (paint_self
        && (node.style.background_rgba & 0xFFU) != 0U
        && node.layout.width > 0
        && node.layout.height > 0) {
        commands.push_back(htmlml_scene_command{
            radii.any()
                ? paint_box_in_foreground ? 10U : 7U
                : paint_box_in_foreground ? 9U : 1U,
            node.style.clip ? 1U : 0U,
            node.layout.x,
            node.layout.y,
            node.layout.width,
            node.layout.height,
            with_opacity(node.style.background_rgba),
            node.id,
            radii.top_left,
            radii.top_right,
            radii.bottom_right,
            radii.bottom_left,
            0});
    }
    const auto& background_image = node.style.background_image();
    if (paint_self
        && !background_image.image_markup.empty()
        && !background_image.image_view_box.empty()
        && node.layout.width > 0
        && node.layout.height > 0) {
        std::istringstream view_box_stream(background_image.image_view_box);
        float view_x = 0;
        float view_y = 0;
        float view_width = 0;
        float view_height = 0;
        if (view_box_stream >> view_x >> view_y >> view_width >> view_height
            && view_width > 0 && view_height > 0) {
            const auto resolve_size = [](const std::string& token, float available) {
                if (token.empty() || token == "auto" || token == "cover" || token == "contain") {
                    return -1.0F;
                }
                if (token.ends_with('%')) {
                    return available * std::strtof(token.c_str(), nullptr) / 100.0F;
                }
                return std::max(0.0F, native_document::parse_length(token).value);
            };
            auto image_width = view_width;
            auto image_height = view_height;
            if (background_image.size_x == "cover"
                || background_image.size_x == "contain") {
                const auto scale_x = node.layout.width / view_width;
                const auto scale_y = node.layout.height / view_height;
                const auto scale = background_image.size_x == "cover"
                    ? std::max(scale_x, scale_y)
                    : std::min(scale_x, scale_y);
                image_width = view_width * scale;
                image_height = view_height * scale;
            } else {
                const auto declared_width = resolve_size(
                    background_image.size_x, node.layout.width);
                const auto declared_height = resolve_size(
                    background_image.size_y, node.layout.height);
                if (declared_width >= 0 && declared_height >= 0) {
                    image_width = declared_width;
                    image_height = declared_height;
                } else if (declared_width >= 0) {
                    image_width = declared_width;
                    image_height = view_height * declared_width / view_width;
                } else if (declared_height >= 0) {
                    image_height = declared_height;
                    image_width = view_width * declared_height / view_height;
                }
            }
            const auto resolve_position = [](const std::string& token,
                                             float available,
                                             float image_size,
                                             bool horizontal) {
                if (token == "center") return (available - image_size) * 0.5F;
                if (token == (horizontal ? "right" : "bottom")) return available - image_size;
                if (token == (horizontal ? "left" : "top")) return 0.0F;
                if (token.ends_with('%')) {
                    return (available - image_size)
                        * std::strtof(token.c_str(), nullptr) / 100.0F;
                }
                return native_document::parse_length(token).value;
            };
            const auto image_x = node.layout.x + resolve_position(
                background_image.position_x,
                node.layout.width,
                image_width,
                true);
            const auto image_y = node.layout.y + resolve_position(
                background_image.position_y,
                node.layout.height,
                image_height,
                false);
            const auto resource_index = append_scene_string(
                background_image.image_view_box + '\t'
                    + background_image.image_markup,
                strings,
                string_bytes);
            commands.push_back(htmlml_scene_command{
                6U,
                resource_index,
                image_x,
                image_y,
                image_width,
                image_height,
                0U,
                node.id});
        }
    }
    const auto append_border_rect = [&](float x, float y, float width, float height, uint32_t rgba) {
        if ((rgba & 0xFFU) == 0U || width <= 0 || height <= 0) return;
        commands.push_back(htmlml_scene_command{
            paint_box_in_foreground ? 9U : 1U,
            0U,
            x,
            y,
            width,
            height,
            with_opacity(rgba),
            node.id});
    };
    if (paint_self && node.layout.width > 0 && node.layout.height > 0) {
        const auto top = std::clamp(
            resolve_length(node, node.style.border_top_width, node.layout.height, 0),
            0.0F,
            node.layout.height);
        const auto right = std::clamp(
            resolve_length(node, node.style.border_right_width, node.layout.width, 0),
            0.0F,
            node.layout.width);
        const auto bottom = std::clamp(
            resolve_length(node, node.style.border_bottom_width, node.layout.height, 0),
            0.0F,
            node.layout.height);
        const auto left = std::clamp(
            resolve_length(node, node.style.border_left_width, node.layout.width, 0),
            0.0F,
            node.layout.width);
        const auto uniform_width = std::abs(top - right) < 0.01F
            && std::abs(top - bottom) < 0.01F
            && std::abs(top - left) < 0.01F;
        const auto uniform_color = node.style.border_top_rgba == node.style.border_right_rgba
            && node.style.border_top_rgba == node.style.border_bottom_rgba
            && node.style.border_top_rgba == node.style.border_left_rgba;
        const auto rounded_stroke_width = std::max({top, right, bottom, left});
        const auto compatible_rounded_widths = rounded_stroke_width > 0
            && (top <= 0 || std::abs(top - rounded_stroke_width) < 0.01F)
            && (right <= 0 || std::abs(right - rounded_stroke_width) < 0.01F)
            && (bottom <= 0 || std::abs(bottom - rounded_stroke_width) < 0.01F)
            && (left <= 0 || std::abs(left - rounded_stroke_width) < 0.01F);
        if (radii.any() && compatible_rounded_widths) {
            constexpr uint32_t border_top_flag = 1U << 28U;
            constexpr uint32_t border_right_flag = 1U << 29U;
            constexpr uint32_t border_bottom_flag = 1U << 30U;
            constexpr uint32_t border_left_flag = 1U << 31U;
            constexpr uint32_t border_color_partition_flag = 1U << 27U;
            const auto inset = rounded_stroke_width * 0.5F;
            const auto left_inset = left > 0 ? inset : 0.0F;
            const auto right_inset = right > 0 ? inset : 0.0F;
            const auto top_inset = top > 0 ? inset : 0.0F;
            const auto bottom_inset = bottom > 0 ? inset : 0.0F;
            const auto all_sides = top > 0 && right > 0 && bottom > 0 && left > 0;
            const auto partition_colors = all_sides && !uniform_color;
            const auto append_rounded_border = [&](uint32_t color, uint32_t side_flags) {
                if ((color & 0xFFU) == 0U || side_flags == 0U) return;
                commands.push_back(htmlml_scene_command{
                    paint_box_in_foreground ? 11U : 8U,
                    pack_round_rect(0, rounded_stroke_width)
                        | (partition_colors ? border_color_partition_flag : 0U)
                        | (all_sides && uniform_width && uniform_color ? 0U : side_flags),
                    node.layout.x + left_inset,
                    node.layout.y + top_inset,
                    std::max(0.0F, node.layout.width - left_inset - right_inset),
                    std::max(0.0F, node.layout.height - top_inset - bottom_inset),
                    with_opacity(color),
                    node.id,
                    std::max(0.0F, radii.top_left - std::max(top_inset, left_inset)),
                    std::max(0.0F, radii.top_right - std::max(top_inset, right_inset)),
                    std::max(0.0F, radii.bottom_right - std::max(bottom_inset, right_inset)),
                    std::max(0.0F, radii.bottom_left - std::max(bottom_inset, left_inset)),
                    rounded_stroke_width});
            };
            const std::array<uint32_t, 4> colors{
                node.style.border_top_rgba,
                node.style.border_right_rgba,
                node.style.border_bottom_rgba,
                node.style.border_left_rgba};
            for (size_t color_index = 0; color_index < colors.size(); ++color_index) {
                const auto color = colors[color_index];
                auto already_emitted = false;
                for (size_t previous = 0; previous < color_index; ++previous) {
                    already_emitted = already_emitted || colors[previous] == color;
                }
                if (already_emitted) continue;
                const auto side_flags =
                    (top > 0 && node.style.border_top_rgba == color ? border_top_flag : 0U)
                    | (right > 0 && node.style.border_right_rgba == color ? border_right_flag : 0U)
                    | (bottom > 0 && node.style.border_bottom_rgba == color ? border_bottom_flag : 0U)
                    | (left > 0 && node.style.border_left_rgba == color ? border_left_flag : 0U);
                append_rounded_border(color, side_flags);
            }
        } else {
            append_border_rect(
                node.layout.x,
                node.layout.y,
                node.layout.width,
                top,
                node.style.border_top_rgba);
            append_border_rect(
                node.layout.x + node.layout.width - right,
                node.layout.y,
                right,
                node.layout.height,
                node.style.border_right_rgba);
            append_border_rect(
                node.layout.x,
                node.layout.y + node.layout.height - bottom,
                node.layout.width,
                bottom,
                node.style.border_bottom_rgba);
            append_border_rect(
                node.layout.x,
                node.layout.y,
                left,
                node.layout.height,
                node.style.border_left_rgba);
        }
    }
    if (paint_self && node.layout.width > 0 && node.layout.height > 0
        && (node.style.outline_rgba & 0xFFU) != 0U) {
        const auto outline = std::max(
            0.0F,
            resolve_length(node, node.style.outline_width, node.layout.width, 0));
        append_border_rect(
            node.layout.x - outline,
            node.layout.y - outline,
            node.layout.width + outline * 2.0F,
            outline,
            node.style.outline_rgba);
        append_border_rect(
            node.layout.x + node.layout.width,
            node.layout.y,
            outline,
            node.layout.height,
            node.style.outline_rgba);
        append_border_rect(
            node.layout.x - outline,
            node.layout.y + node.layout.height,
            node.layout.width + outline * 2.0F,
            outline,
            node.style.outline_rgba);
        append_border_rect(
            node.layout.x - outline,
            node.layout.y,
            outline,
            node.layout.height,
            node.style.outline_rgba);
    }
    const auto clip_contents = node.style.clip
        && node.layout.width > 0
        && node.layout.height > 0;
    if (clip_contents) {
        // Kinds 12/13 bracket the element's pseudo/content/descendant paint.
        // The box background and border intentionally remain outside its own
        // overflow clip, matching CSS background/border painting semantics.
        commands.push_back(htmlml_scene_command{
            12U,
            0U,
            node.layout.x,
            node.layout.y,
            node.layout.width,
            node.layout.height,
            0U,
            node.id,
            radii.top_left,
            radii.top_right,
            radii.bottom_right,
            radii.bottom_left,
            0});
    }
    if (node.list_marker_layout.width > 0 || node.list_marker_layout.height > 0) {
        node_style::pseudo_element marker{};
        marker.generated = true;
        marker.content = list_marker_text(node);
        marker.layout = node.list_marker_layout;
        append_pseudo(marker);
    }
    const auto before_paints_after_content = node.style.before_pseudo().z_index > 0
        && node.style.before_pseudo().position != position_mode::normal;
    if (!before_paints_after_content) {
        append_pseudo(node.style.before_pseudo());
    }
    // Positioned negative-z pseudo-elements participate behind the element's
    // content. Hosted controls use this for hover/active button surfaces; painting
    // ::after in its ordinary DOM position would cover the button SVG instead.
    const auto after_paints_behind_content = node.style.after_pseudo().z_index < 0
        && node.style.after_pseudo().position != position_mode::normal;
    if (after_paints_behind_content) {
        append_pseudo(node.style.after_pseudo());
    }
    const auto& canvas = node.canvas();
    if (paint_self
        && node.tag == "canvas"
        && canvas.commands.empty()
        && (!canvas.rects.empty() || !canvas.lines.empty())) {
        const auto width_iterator = node.attributes.find("width");
        const auto height_iterator = node.attributes.find("height");
        const auto bitmap_width = width_iterator == node.attributes.end()
            ? 300.0F
            : std::max(1.0F, parse_number(width_iterator->second, 300.0F));
        const auto bitmap_height = height_iterator == node.attributes.end()
            ? 150.0F
            : std::max(1.0F, parse_number(height_iterator->second, 150.0F));
        const auto display_width = node.layout.width > 0 ? node.layout.width : bitmap_width;
        const auto display_height = node.layout.height > 0 ? node.layout.height : bitmap_height;
        const auto scale_x = display_width / bitmap_width;
        const auto scale_y = display_height / bitmap_height;
        for (const auto& rect : canvas.rects) {
            if ((rect.rgba & 0xFFU) == 0U || rect.width <= 0 || rect.height <= 0) continue;
            commands.push_back(htmlml_scene_command{
                1U,
                0U,
                node.layout.x + rect.x * scale_x,
                node.layout.y + rect.y * scale_y,
                rect.width * scale_x,
                rect.height * scale_y,
                with_opacity(rect.rgba),
                node.id});
        }
        for (const auto& line : canvas.lines) {
            if ((line.rgba & 0xFFU) == 0U) continue;
            commands.push_back(htmlml_scene_command{
                2U,
                static_cast<uint32_t>(std::max(1.0F, line.line_width * 100.0F)),
                node.layout.x + line.x1 * scale_x,
                node.layout.y + line.y1 * scale_y,
                node.layout.x + line.x2 * scale_x,
                node.layout.y + line.y2 * scale_y,
                with_opacity(line.rgba),
                node.id});
        }
    }

    if (paint_self && (node.tag == "input" || node.tag == "textarea")) {
        const auto font_size = resolved_font_size(node);
        const auto line_height = resolved_line_height(node, font_size);
        const auto padding_left = resolve_length(
            node, node.style.padding_left, node.layout.width, 0);
        const auto padding_right = resolve_length(
            node, node.style.padding_right, node.layout.width, 0);
        const auto text_x = node.layout.x + padding_left;
        const auto text_y = node.layout.y + std::max(0.0F, (node.layout.height - line_height) * 0.5F);
        const auto text_width = std::max(
            0.0F, node.layout.width - padding_left - padding_right);
        if (!node.form_control().value.empty()) {
            std::ostringstream resource;
            resource << font_size << '\t' << line_height << '\t'
                << resolved_font_weight(node) << '\t' << "start" << '\t'
                << resolved_font_family(node) << '\t' << node.form_control().value;
            commands.push_back(htmlml_scene_command{
                3U,
                append_scene_string(resource.str(), strings, string_bytes),
                text_x,
                text_y,
                text_width,
                line_height,
                with_opacity(resolved_foreground(node)),
                node.id});
        }
        if (node.form_control().input_focused && node.form_control().caret_visible) {
            const auto caret_index = std::min(
                node.form_control().selection_end,
                node.form_control().value.size());
            const auto caret_x = std::min(
                text_x + measure_text_width(
                    std::string_view(node.form_control().value).substr(0, caret_index),
                    node),
                node.layout.x + node.layout.width - padding_right);
            commands.push_back(htmlml_scene_command{
                14U,
                100U,
                caret_x,
                text_y + 1.0F,
                caret_x,
                text_y + std::max(1.0F, line_height - 1.0F),
                with_opacity(resolved_foreground(node)),
                node.id});
        }
    }

    if (paint_self && has_visible_text(node.text_content)) {
        const auto font_size = resolved_font_size(node);
        const auto line_height = resolved_line_height(node, font_size);
        if (!node.text_layout_fragments.empty()) {
            for (const auto& fragment : node.text_layout_fragments) {
                std::ostringstream resource;
                resource << font_size << '\t' << fragment.height << '\t'
                    << resolved_font_weight(node) << '\t' << resolved_text_align(node) << '\t'
                    << resolved_font_family(node) << '\t'
                    << resolved_text_transform(node, fragment.text);
                commands.push_back(htmlml_scene_command{
                    3U,
                    append_scene_string(resource.str(), strings, string_bytes),
                    fragment.x,
                    fragment.y,
                    fragment.width,
                    fragment.height,
                    with_opacity(resolved_foreground(node)),
                    node.id});
            }
        } else {
        const auto padding_left = resolve_length(
            node, node.style.padding_left, node.layout.width, 0);
        const auto padding_right = resolve_length(
            node, node.style.padding_right, node.layout.width, 0);
        const auto padding_top = resolve_length(
            node, node.style.padding_top, node.layout.height, 0);
        const auto padding_bottom = resolve_length(
            node, node.style.padding_bottom, node.layout.height, 0);
        auto x = node.layout.x + padding_left;
        auto y = node.layout.y + padding_top;
        auto width = std::max(0.0F, node.layout.width - padding_left - padding_right);
        auto height = std::max(0.0F, node.layout.height - padding_top - padding_bottom);
        if (width <= 0) {
            width = measure_text_width(
                resolved_text_transform(node, node.text_content),
                node);
        }
        if (height <= 0 && node.tag == "#text" && node.parent != nullptr) {
            y = node.parent->layout.y;
            height = node.parent->layout.height;
        }
        if (height <= 0) height = line_height;
        const auto wraps = resolved_white_space_wraps(node);
        const auto transformed_text = resolved_text_transform(node, node.text_content);
        const auto lines = wrap_text_lines(
            transformed_text,
            width,
            node,
            wraps);
        const auto command_width = wraps
            ? width
            : std::max(width, measure_text_width(transformed_text, node));
        for (size_t line_index = 0; line_index < lines.size(); ++line_index) {
            std::ostringstream resource;
            resource << font_size << '\t' << line_height << '\t'
                << resolved_font_weight(node) << '\t' << resolved_text_align(node) << '\t'
                << resolved_font_family(node) << '\t'
                << lines[line_index];
            commands.push_back(htmlml_scene_command{
                3U,
                append_scene_string(resource.str(), strings, string_bytes),
                x,
                y + static_cast<float>(line_index) * line_height,
                command_width,
                line_height,
                with_opacity(resolved_foreground(node)),
                node.id});
        }
        }
    }

    if (node.tag == "svg" && paint_self) {
        auto box = node.layout;
        if ((box.width <= 0 || box.height <= 0) && node.parent != nullptr) {
            box = node.parent->layout;
        }
        if (box.width > 0 && box.height > 0) {
            const auto view_box = node.attributes.contains("viewBox")
                ? node.attributes.at("viewBox")
                : std::string("0 0 ") + std::to_string(std::max(1.0F, box.width))
                    + " " + std::to_string(std::max(1.0F, box.height));
            std::string markup;
            markup.reserve(256U);
            serialize_svg_subtree(node, markup, true);
            const auto resource_index = append_scene_string(
                view_box + '\t' + markup,
                strings,
                string_bytes);
            commands.push_back(htmlml_scene_command{
                6U,
                resource_index,
                box.x,
                box.y,
                box.width,
                box.height,
                with_opacity(resolved_foreground(node)),
                node.id,
                0,
                0,
                0,
                0,
                resolved_transform_rotation(node)});
        }
        if (clip_contents) {
            commands.push_back(htmlml_scene_command{13U, 0U, 0, 0, 0, 0, 0U, node.id});
        }
        if (has_rotate_transform) {
            commands.push_back(htmlml_scene_command{20U, 0U, 0, 0, 0, 0, 0U, node.id});
        }
        if (has_scale_transform) {
            commands.push_back(htmlml_scene_command{16U, 0U, 0, 0, 0, 0, 0U, node.id});
        }
        if (has_opacity_group) {
            commands.push_back(htmlml_scene_command{
                31U, 0U, node.layout.x, node.layout.y,
                node.layout.width, node.layout.height, 0U, node.id});
        }
        return;
    }

    if (paint_self && node.tag == "path" && node.attributes.contains("d")) {
        const dom_node* svg = node.parent;
        while (svg != nullptr && svg->tag != "svg") svg = svg->parent;
        if (svg != nullptr) {
            auto box = svg->layout;
            if ((box.width <= 0 || box.height <= 0) && svg->parent != nullptr) {
                box = svg->parent->layout;
            }
            const auto view_box = svg->attributes.contains("viewBox")
                ? svg->attributes.at("viewBox")
                : std::string("0 0 ") + std::to_string(std::max(1.0F, box.width))
                    + " " + std::to_string(std::max(1.0F, box.height));
            const auto transform = node.attributes.contains("transform")
                ? node.attributes.at("transform") : std::string{};
            const auto stroke_width = node.attributes.contains("stroke-width")
                ? node.attributes.at("stroke-width") : std::string("1");
            std::string resource = view_box + '\t' + stroke_width + '\t'
                + transform + '\t' + node.attributes.at("d");
            const auto resource_index = append_scene_string(resource, strings, string_bytes);
            const auto fill = node.attributes.contains("fill")
                ? node.attributes.at("fill")
                : svg->attributes.contains("fill") ? svg->attributes.at("fill") : "currentColor";
            if (fill != "none") {
                const auto color = fill == "currentColor"
                    ? resolved_foreground(node)
                    : parse_color(fill);
                commands.push_back(htmlml_scene_command{
                    4U,
                    resource_index,
                    box.x,
                    box.y,
                    box.width,
                    box.height,
                    with_opacity(color == 0 ? resolved_foreground(node) : color),
                    node.id,
                    0,
                    0,
                    0,
                    0,
                    resolved_transform_rotation(node)});
            }
            const auto stroke = node.attributes.contains("stroke")
                ? node.attributes.at("stroke")
                : svg->attributes.contains("stroke") ? svg->attributes.at("stroke") : std::string{};
            if (!stroke.empty() && stroke != "none") {
                const auto color = stroke == "currentColor"
                    ? resolved_foreground(node)
                    : parse_color(stroke);
                commands.push_back(htmlml_scene_command{
                    5U,
                    resource_index,
                    box.x,
                    box.y,
                    box.width,
                    box.height,
                    with_opacity(color == 0 ? resolved_foreground(node) : color),
                    node.id,
                    0,
                    0,
                    0,
                    0,
                    resolved_transform_rotation(node)});
            }
        }
    }
    struct local_paint_entry final {
        const dom_node* child{};
        const node_style::pseudo_element* pseudo{};
        int z_index{};
        size_t source_order{};
    };
    std::vector<local_paint_entry> paint_order;
    paint_order.reserve(
        node.children.size()
        + (before_paints_after_content ? 1U : 0U)
        + (!after_paints_behind_content ? 1U : 0U));
    if (before_paints_after_content) {
        paint_order.push_back(local_paint_entry{
            nullptr,
            &node.style.before_pseudo(),
            node.style.before_pseudo().z_index,
            0U});
    }
    for (size_t index = 0; index < node.children.size(); ++index) {
        const auto* child = node.children[index];
        paint_order.push_back(local_paint_entry{
            child,
            nullptr,
            child->paint_z_index,
            index + 1U});
    }
    if (!after_paints_behind_content) {
        paint_order.push_back(local_paint_entry{
            nullptr,
            &node.style.after_pseudo(),
            node.style.after_pseudo().z_index,
            node.children.size() + 1U});
    }
    std::stable_sort(
        paint_order.begin(),
        paint_order.end(),
        [](const auto& left, const auto& right) {
            return left.z_index < right.z_index
                || (left.z_index == right.z_index
                    && left.source_order < right.source_order);
        });
    for (const auto& entry : paint_order) {
        if (entry.child != nullptr) {
            if (defer_fixed_descendants
                && entry.child->style.position == position_mode::fixed) {
                continue;
            }
            append_scene(
                *entry.child,
                commands,
                strings,
                string_bytes,
                visibility_hidden,
                defer_fixed_descendants);
        } else if (entry.pseudo != nullptr) {
            append_pseudo(*entry.pseudo);
        }
    }
    if (clip_contents) {
        commands.push_back(htmlml_scene_command{13U, 0U, 0, 0, 0, 0, 0U, node.id});
    }
    // Scroll geometry is a DOM/CSS contract, while the presence of an actual
    // scrollbar is a host-renderer contract. Emit lightweight overlay rails
    // after the overflow clip so they remain visible above the clipped content.
    // They do not consume clientWidth/clientHeight, matching overlay scrollbars
    // on the certified macOS host.
    if (paint_self && node.layout.width > 0 && node.layout.height > 0) {
        const auto border_left = resolve_length(
            node, node.style.border_left_width, node.layout.width, 0);
        const auto border_right = resolve_length(
            node, node.style.border_right_width, node.layout.width, 0);
        const auto border_top = resolve_length(
            node, node.style.border_top_width, node.layout.height, 0);
        const auto border_bottom = resolve_length(
            node, node.style.border_bottom_width, node.layout.height, 0);
        const auto padding_left = resolve_length(
            node, node.style.padding_left, node.layout.width, 0);
        const auto padding_right = resolve_length(
            node, node.style.padding_right, node.layout.width, 0);
        const auto padding_top = resolve_length(
            node, node.style.padding_top, node.layout.height, 0);
        const auto padding_bottom = resolve_length(
            node, node.style.padding_bottom, node.layout.height, 0);
        const auto viewport_x = node.layout.x + border_left + padding_left;
        const auto viewport_y = node.layout.y + border_top + padding_top;
        const auto viewport_width = std::max(
            0.0F,
            node.layout.width - border_left - border_right - padding_left - padding_right);
        const auto viewport_height = std::max(
            0.0F,
            node.layout.height - border_top - border_bottom - padding_top - padding_bottom);
        const auto maximum_x = std::max(
            0.0F, node.scroll_content_width - node.scroll_viewport_width);
        const auto maximum_y = std::max(
            0.0F, node.scroll_content_height - node.scroll_viewport_height);
        constexpr auto rail_rgba = 0x7F7F7F40U;
        constexpr auto thumb_rgba = 0xA0A0A0D0U;
        constexpr auto thickness = 6.0F;
        constexpr auto inset = 2.0F;
        constexpr auto minimum_thumb = 18.0F;
        const auto append_overlay_rect = [&](float x, float y, float width, float height,
                                             uint32_t rgba, float radius) {
            if (width <= 0 || height <= 0) return;
            commands.push_back(htmlml_scene_command{
                radius > 0 ? 10U : 9U,
                0U,
                x,
                y,
                width,
                height,
                rgba,
                node.id,
                radius,
                radius,
                radius,
                radius,
                0});
        };
        const auto root_viewport_scroll_x = node.parent == nullptr
            && node.style.overflow_x == overflow_mode::visible;
        const auto root_viewport_scroll_y = node.parent == nullptr
            && node.style.overflow_y == overflow_mode::visible;
        if ((node.style.scroll_y_enabled || root_viewport_scroll_y) && maximum_y > 0.01F
            && viewport_width >= thickness + inset * 2
            && viewport_height > 0) {
            const auto rail_x = viewport_x + viewport_width - thickness - inset;
            const auto rail_y = viewport_y + inset;
            const auto rail_height = std::max(0.0F, viewport_height - inset * 2);
            const auto thumb_height = std::min(
                rail_height,
                std::max(
                    minimum_thumb,
                    rail_height * viewport_height
                        / std::max(viewport_height, node.scroll_content_height)));
            const auto thumb_travel = std::max(0.0F, rail_height - thumb_height);
            const auto thumb_y = rail_y + thumb_travel
                * std::clamp(node.scroll_top / maximum_y, 0.0F, 1.0F);
            append_overlay_rect(
                rail_x, rail_y, thickness, rail_height, rail_rgba, thickness * 0.5F);
            append_overlay_rect(
                rail_x, thumb_y, thickness, thumb_height, thumb_rgba, thickness * 0.5F);
        }
        if ((node.style.scroll_x_enabled || root_viewport_scroll_x) && maximum_x > 0.01F
            && viewport_height >= thickness + inset * 2
            && viewport_width > 0) {
            const auto rail_x = viewport_x + inset;
            const auto rail_y = viewport_y + viewport_height - thickness - inset;
            const auto rail_width = std::max(0.0F, viewport_width - inset * 2);
            const auto thumb_width = std::min(
                rail_width,
                std::max(
                    minimum_thumb,
                    rail_width * viewport_width
                        / std::max(viewport_width, node.scroll_content_width)));
            const auto thumb_travel = std::max(0.0F, rail_width - thumb_width);
            const auto thumb_x = rail_x + thumb_travel
                * std::clamp(node.scroll_left / maximum_x, 0.0F, 1.0F);
            append_overlay_rect(
                rail_x, rail_y, rail_width, thickness, rail_rgba, thickness * 0.5F);
            append_overlay_rect(
                thumb_x, rail_y, thumb_width, thickness, thumb_rgba, thickness * 0.5F);
        }
    }
    if (has_rotate_transform) {
        commands.push_back(htmlml_scene_command{20U, 0U, 0, 0, 0, 0, 0U, node.id});
    }
    if (has_scale_transform) {
        commands.push_back(htmlml_scene_command{16U, 0U, 0, 0, 0, 0, 0U, node.id});
    }
    if (has_opacity_group) {
        commands.push_back(htmlml_scene_command{
            31U, 0U, node.layout.x, node.layout.y,
            node.layout.width, node.layout.height, 0U, node.id});
    }
}

uint64_t native_document::layout_passes() const noexcept
{
    return layout_passes_;
}

size_t native_document::node_count() const noexcept
{
    return nodes_.size();
}

native_document::allocation_metrics native_document::read_allocation_metrics() const noexcept
{
    allocation_metrics result{};
    result.node_count = nodes_.size();
    result.node_object_size_bytes = sizeof(dom_node);
    result.node_object_bytes = nodes_.size() * sizeof(dom_node);
    result.node_pool_reserved_bytes = node_pool_upstream_.reserved_bytes();
    result.node_pool_peak_bytes = node_pool_upstream_.peak_bytes();
    result.text_measurement_cache_entry_count = text_measurement_cache_.size();
    result.text_measurement_cache_storage_bytes =
        text_measurement_cache_.bucket_count() * sizeof(void*)
        + text_measurement_cache_.size()
            * (sizeof(decltype(text_measurement_cache_)::value_type)
                + 2U * sizeof(void*));
    for (const auto& [key, _] : text_measurement_cache_) {
        result.text_measurement_cache_storage_bytes +=
            key.text.capacity() + key.family.capacity() + 2U;
    }
    std::unordered_set<const node_style::animation_data*> animation_allocations;
    std::unordered_set<const node_style::background_image_data*> background_allocations;
    std::unordered_set<const node_style::grid_data*> grid_allocations;
    std::unordered_set<const node_style::textual_style_data*> textual_allocations;
    for (const auto& node : nodes_) {
        if (node != nullptr && node->has_table_layout()) {
            ++result.table_layout_node_count;
            result.table_layout_storage_bytes +=
                sizeof(dom_node::table_layout_data)
                + node->table_layout().column_widths.capacity() * sizeof(float);
        }
        if (node != nullptr && node->has_form_control()) {
            ++result.form_control_node_count;
            result.form_control_storage_bytes +=
                sizeof(dom_node::form_control_data)
                + node->form_control().value.capacity() + 1U;
        }
        if (node != nullptr && !node->attributes.empty()) {
            ++result.attribute_node_count;
            result.attribute_entry_count += node->attributes.size();
            result.attribute_storage_bytes += node->attributes.storage_bytes();
        }
        if (node != nullptr && node->style.has_pseudo_elements()) {
            ++result.pseudo_element_pair_count;
        }
        if (node != nullptr && node->style.has_animation_data()) {
            animation_allocations.insert(node->style.animation_data_identity());
        }
        if (node != nullptr && node->style.has_custom_properties()) {
            const auto& custom = node->style.custom_properties();
            ++result.custom_property_node_count;
            result.custom_property_entry_count +=
                custom.values.size();
            const auto map_hash_bytes = [](const auto& values) {
                using value_type = typename std::decay_t<decltype(values)>::value_type;
                return values.bucket_count() * sizeof(void*)
                    + values.size() * (sizeof(value_type) + 2U * sizeof(void*));
            };
            result.custom_property_storage_bytes +=
                sizeof(node_style::custom_property_data) + 2U * sizeof(void*)
                + map_hash_bytes(custom.values)
                + map_hash_bytes(custom.important);
            for (const auto& [name, value] : custom.values) {
                result.custom_property_storage_bytes +=
                    name.capacity() + value.capacity() + 2U;
            }
            for (const auto& name : custom.important) {
                result.custom_property_storage_bytes += name.capacity() + 1U;
            }
        }
        if (node != nullptr && node->style.has_background_image_data()) {
            background_allocations.insert(
                node->style.background_image_data_identity());
        }
        if (node != nullptr && node->style.has_grid_data()) {
            grid_allocations.insert(node->style.grid_data_identity());
        }
        if (node != nullptr && node->style.has_textual_data()) {
            textual_allocations.insert(node->style.textual_data_identity());
        }
        if (node != nullptr && node->has_authored_style()) {
            const auto& authored = node->authored_style();
            ++result.authored_style_node_count;
            result.authored_style_entry_count += authored.declarations.size();
            const auto hash_bytes = [](const auto& values) {
                using value_type = typename std::decay_t<decltype(values)>::value_type;
                return values.bucket_count() * sizeof(void*)
                    + values.size() * (sizeof(value_type) + 2U * sizeof(void*));
            };
            result.authored_style_storage_bytes +=
                sizeof(dom_node::authored_style_data)
                + hash_bytes(authored.declarations)
                + hash_bytes(authored.important_declarations);
            for (const auto& [name, value] : authored.declarations) {
                result.authored_style_storage_bytes +=
                    name.capacity() + value.capacity() + 2U;
            }
            for (const auto& name : authored.important_declarations) {
                result.authored_style_storage_bytes += name.capacity() + 1U;
            }
        }
        if (node != nullptr && node->has_animation_runtime()) {
            const auto* animation = node->animation_runtime();
            ++result.animation_runtime_count;
            result.animation_runtime_storage_bytes +=
                sizeof(dom_node::animation_runtime_data) + 2U * sizeof(void*)
                + animation->opacity_keyframe_animation_signature.capacity() + 1U
                + animation->rotation_keyframe_animation_signature.capacity() + 1U;
        }
        if (node == nullptr || !node->has_canvas_data()) continue;
        ++result.canvas_node_count;
        const auto& canvas = node->canvas();
        result.canvas_storage_bytes += sizeof(canvas_node_data)
            + canvas.rects.capacity() * sizeof(canvas_rect_command)
            + canvas.lines.capacity() * sizeof(canvas_line_command)
            + canvas.commands.capacity() * sizeof(htmlml_canvas_command)
            + canvas.strings.capacity() * sizeof(std::string);
        for (const auto& value : canvas.strings) {
            result.canvas_storage_bytes += value.capacity() + 1U;
        }
        const auto hash_bytes = [](const auto& values) {
            using value_type = typename std::decay_t<decltype(values)>::value_type;
            return values.bucket_count() * sizeof(void*)
                + values.size() * (sizeof(value_type) + 2U * sizeof(void*));
        };
        result.canvas_storage_bytes += hash_bytes(canvas.string_indices);
#if defined(HTMLML_NATIVE_ENGINE_CERTIFICATION)
        result.canvas_storage_bytes +=
            hash_bytes(canvas.probable_volume_by_generation)
            + hash_bytes(canvas.fill_rect_color_calls);
#endif
    }
    // shared_ptr control blocks are implementation-defined. Two pointers are
    // the conservative allocator/control-block allowance used by this
    // diagnostic; the inline shared_ptr itself is already in dom_node.
    result.pseudo_element_storage_bytes =
        result.pseudo_element_pair_count
        * (sizeof(node_style::pseudo_element_pair) + 2U * sizeof(void*));
    result.animation_data_count = animation_allocations.size();
    for (const auto* animations : animation_allocations) {
        result.animation_storage_bytes +=
            sizeof(node_style::animation_data) + 2U * sizeof(void*);
        const auto string_bytes = [](const std::string& value) {
            return value.capacity() + 1U;
        };
        result.animation_storage_bytes +=
            string_bytes(animations->transition_property_value)
            + string_bytes(animations->transition_duration_value)
            + string_bytes(animations->transition_delay_value)
            + string_bytes(animations->transition_timing_function_value)
            + string_bytes(animations->animation_name_value)
            + string_bytes(animations->animation_duration_value)
            + string_bytes(animations->animation_delay_value)
            + string_bytes(animations->animation_timing_function_value)
            + string_bytes(animations->animation_iteration_count_value)
            + string_bytes(animations->opacity_keyframe_animation_signature)
            + string_bytes(animations->rotation_keyframe_animation_signature)
            + animations->opacity_keyframes.capacity()
                * sizeof(node_style::opacity_keyframe)
            + animations->rotation_keyframes.capacity()
                * sizeof(node_style::rotation_keyframe);
    }
    result.background_image_data_count = background_allocations.size();
    for (const auto* background : background_allocations) {
        result.background_image_storage_bytes +=
            sizeof(node_style::background_image_data) + 2U * sizeof(void*);
        result.background_image_storage_bytes +=
            background->image_value.capacity() + 1U
            + background->image_markup.capacity() + 1U
            + background->image_view_box.capacity() + 1U
            + background->repeat.capacity() + 1U
            + background->position_x.capacity() + 1U
            + background->position_y.capacity() + 1U
            + background->size_x.capacity() + 1U
            + background->size_y.capacity() + 1U;
    }
    result.grid_data_count = grid_allocations.size();
    for (const auto* grid : grid_allocations) {
        result.grid_storage_bytes +=
            sizeof(node_style::grid_data) + 2U * sizeof(void*)
            + grid->template_columns.capacity() * sizeof(css_length)
            + grid->template_rows.capacity() * sizeof(css_length)
            + grid->area_value.capacity() + 1U
            + grid->row_value.capacity() + 1U
            + grid->row_start_value.capacity() + 1U
            + grid->row_end_value.capacity() + 1U
            + grid->column_value.capacity() + 1U
            + grid->column_start_value.capacity() + 1U
            + grid->column_end_value.capacity() + 1U;
    }
    result.textual_style_data_count = textual_allocations.size();
    for (const auto* textual : textual_allocations) {
        result.textual_style_storage_bytes +=
            sizeof(node_style::textual_style_data) + 2U * sizeof(void*)
            + textual->font_family.capacity() + 1U
            + textual->text_align.capacity() + 1U
            + textual->vertical_align.capacity() + 1U
            + textual->text_transform.capacity() + 1U
            + textual->white_space.capacity() + 1U
            + textual->cursor.capacity() + 1U
            + textual->svg_fill.capacity() + 1U
            + textual->svg_stroke.capacity() + 1U
            + textual->list_style_position.capacity() + 1U
            + textual->list_style_type.capacity() + 1U;
    }
    return result;
}

size_t native_document::count_tag(const std::string& tag) const noexcept
{
    return static_cast<size_t>(std::count_if(nodes_.begin(), nodes_.end(), [this, &tag](const auto& node) {
        return node->tag == tag && is_connected(*node);
    }));
}

size_t native_document::sum_attribute_bytes(
    const std::string& tag,
    const std::string& attribute) const noexcept
{
    size_t result = 0;
    for (const auto& node : nodes_) {
        if (node->tag != tag || !is_connected(*node)) continue;
        const auto value = node->attributes.find(attribute);
        if (value != node->attributes.end()) result += value->second.size();
    }
    return result;
}

std::string native_document::first_attribute(
    const std::string& tag,
    const std::string& attribute) const
{
    for (const auto& node : nodes_) {
        if (node->tag != tag || !is_connected(*node)) continue;
        const auto value = node->attributes.find(attribute);
        if (value != node->attributes.end() && !value->second.empty()) return value->second;
    }
    return {};
}

std::string native_document::describe_busiest_canvas() const
{
#if !defined(HTMLML_NATIVE_ENGINE_CERTIFICATION)
    return "certification telemetry disabled at compile time";
#else
    const dom_node* busiest = nullptr;
    size_t maximum_commands = 0;
    size_t attached_rects = 0;
    size_t attached_lines = 0;
    size_t detached_rects = 0;
    size_t detached_lines = 0;
    uint64_t fill_rect_calls = 0;
    uint64_t probable_volume_fill_rect_calls = 0;
    uint64_t fill_calls = 0;
    uint64_t path_argument_fill_calls = 0;
    uint64_t draw_image_calls = 0;
    uint64_t canvas_draw_image_calls = 0;
    uint64_t self_draw_image_calls = 0;
    uint64_t fill_text_calls = 0;
    uint64_t stroke_text_calls = 0;
    uint64_t clear_rect_calls = 0;
    uint64_t full_clear_calls = 0;
    uint64_t full_clear_reset_calls = 0;
    uint64_t full_clear_current_clip_calls = 0;
    uint64_t full_clear_saved_clip_calls = 0;
    uint64_t clear_bounds_rejected_calls = 0;
    uint64_t maximum_clear_stack_depth = 0;
    std::unordered_map<uint32_t, uint64_t> fill_rect_color_calls;
    std::vector<std::tuple<uint32_t, uint64_t, uint64_t>> volume_generations;
    for (const auto& node : nodes_) {
        if (node->tag != "canvas") continue;
        const auto& canvas = node->canvas();
        const auto commands = canvas.rects.size() + canvas.lines.size();
        const auto connected = is_connected(*node);
        auto& rect_count = connected ? attached_rects : detached_rects;
        auto& line_count = connected ? attached_lines : detached_lines;
        rect_count += canvas.rects.size();
        line_count += canvas.lines.size();
        fill_rect_calls += canvas.fill_rect_calls;
        probable_volume_fill_rect_calls += canvas.probable_volume_fill_rect_calls;
        for (const auto& [generation, count] : canvas.probable_volume_by_generation) {
            volume_generations.emplace_back(node->id, generation, count);
        }
        fill_calls += canvas.fill_calls;
        path_argument_fill_calls += canvas.path_argument_fill_calls;
        draw_image_calls += canvas.draw_image_calls;
        canvas_draw_image_calls += canvas.canvas_draw_image_calls;
        self_draw_image_calls += canvas.self_draw_image_calls;
        fill_text_calls += canvas.fill_text_calls;
        stroke_text_calls += canvas.stroke_text_calls;
        clear_rect_calls += canvas.clear_rect_calls;
        full_clear_calls += canvas.full_clear_calls;
        full_clear_reset_calls += canvas.full_clear_reset_calls;
        full_clear_current_clip_calls += canvas.full_clear_current_clip_calls;
        full_clear_saved_clip_calls += canvas.full_clear_saved_clip_calls;
        clear_bounds_rejected_calls += canvas.clear_bounds_rejected_calls;
        maximum_clear_stack_depth = std::max(
            maximum_clear_stack_depth,
            canvas.max_clear_stack_depth);
        for (const auto& [color, count] : canvas.fill_rect_color_calls) {
            fill_rect_color_calls[color] += count;
        }
        if (connected && commands > maximum_commands) {
            maximum_commands = commands;
            busiest = node.get();
        }
    }
    std::vector<const dom_node*> ancestry;
    for (auto* node = busiest; node != nullptr; node = node->parent) ancestry.push_back(node);
    std::reverse(ancestry.begin(), ancestry.end());
    std::ostringstream result;
    result << "canvas ops: fillRect=" << fill_rect_calls
        << "(volume-like=" << probable_volume_fill_rect_calls << ')'
        << ", fill=" << fill_calls
        << "(Path2D=" << path_argument_fill_calls << ')'
        << ", drawImage=" << draw_image_calls
        << "(canvas=" << canvas_draw_image_calls
        << ", self=" << self_draw_image_calls << ')'
        << ", fillText=" << fill_text_calls
        << ", strokeText=" << stroke_text_calls
        << ", clearRect=" << clear_rect_calls
        << "(full=" << full_clear_calls
        << ", reset=" << full_clear_reset_calls
        << ", currentClip=" << full_clear_current_clip_calls
        << ", savedClip=" << full_clear_saved_clip_calls
        << ", boundsRejected=" << clear_bounds_rejected_calls
        << ", maxStack=" << maximum_clear_stack_depth << ')'
        << ", volumeGenerations=[";
    std::sort(volume_generations.begin(), volume_generations.end());
    constexpr size_t reported_volume_generations = 32U;
    const auto first_volume_generation = volume_generations.size() > reported_volume_generations
        ? volume_generations.size() - reported_volume_generations
        : 0U;
    if (first_volume_generation > 0U) result << "...";
    for (size_t index = first_volume_generation; index < volume_generations.size(); ++index) {
        if (index != first_volume_generation || first_volume_generation > 0U) result << ',';
        const auto [node_id, generation, count] = volume_generations[index];
        result << node_id << 'g' << generation << ':' << count;
    }
    result << "]"
        << ", fillRectColors=[";
    std::vector<std::pair<uint32_t, uint64_t>> ordered_fill_rect_colors(
        fill_rect_color_calls.begin(),
        fill_rect_color_calls.end());
    std::sort(
        ordered_fill_rect_colors.begin(),
        ordered_fill_rect_colors.end(),
        [](const auto& left, const auto& right) { return left.second > right.second; });
    for (size_t index = 0; index < std::min<size_t>(8U, ordered_fill_rect_colors.size()); ++index) {
        if (index != 0) result << ',';
        result << "0x" << std::hex << ordered_fill_rect_colors[index].first
            << std::dec << ':' << ordered_fill_rect_colors[index].second;
    }
    result << ']'
        << "; retained attached=" << attached_rects << "R/" << attached_lines << "L"
        << ", detached=" << detached_rects << "R/" << detached_lines << "L"
        << ", DOM graphics=" << count_tag("svg") << "svg/"
        << count_tag("path") << "path/" << count_tag("circle") << "circle"
        << " | busiest canvas commands=" << maximum_commands;
    for (const auto* node : ancestry) {
        result << " -> " << node->tag << '#' << node->id;
        if (!node->id_attribute.empty()) result << "[id=" << node->id_attribute << ']';
        if (!node->class_name.empty()) result << "[class=" << node->class_name << ']';
        result << " layout=" << node->layout.x << ',' << node->layout.y << ','
            << node->layout.width << ',' << node->layout.height
            << " display=" << static_cast<int>(node->style.display)
            << " position=" << static_cast<int>(node->style.position);
    }
    result << " | canvases:";
    for (const auto& node : nodes_) {
        if (node->tag != "canvas" || !is_connected(*node)) continue;
        const auto width = node->attributes.find("width");
        const auto height = node->attributes.find("height");
        result << ' ' << node->id << '@' << node->layout.x << ',' << node->layout.y
            << ',' << node->layout.width << ',' << node->layout.height
            << "[bitmap=" << (width == node->attributes.end() ? "" : width->second)
            << 'x' << (height == node->attributes.end() ? "" : height->second) << ']';
    }
    result << " | reachable-svg:";
    for (const auto& node : nodes_) {
        if (node->tag != "svg") continue;
        auto* root = node.get();
        while (root->parent != nullptr) root = root->parent;
        if (root != body_) continue;
        const auto width = node->attributes.find("width");
        const auto height = node->attributes.find("height");
        result << ' ' << node->id << '@' << node->layout.x << ',' << node->layout.y
            << ',' << node->layout.width << ',' << node->layout.height
            << "[attr=" << (width == node->attributes.end() ? "" : width->second)
            << 'x' << (height == node->attributes.end() ? "" : height->second)
            << ",visible=" << node->visible << ",parent="
            << (node->parent == nullptr ? 0U : node->parent->id) << ']';
    }
    result << " | detached-roots:";
    for (const auto& node : nodes_) {
        if (node.get() == body_ || node->parent != nullptr || node->children.empty()) continue;
        result << " {" << node->tag << '#' << node->id;
        if (!node->class_name.empty()) result << "[class=" << node->class_name << ']';
        result << " children=" << node->children.size() << '}';
    }
    for (const auto& node : nodes_) {
        if (node->tag != "body" || node->parent == nullptr) continue;
        result << " | frame-body-children:";
        for (const auto* child : node->children) {
            result << " {" << child->tag << '#' << child->id;
            if (!child->id_attribute.empty()) result << "[id=" << child->id_attribute << ']';
            if (!child->class_name.empty()) result << "[class=" << child->class_name << ']';
            result << " layout=" << child->layout.x << ',' << child->layout.y << ','
                << child->layout.width << ',' << child->layout.height
                << " display=" << static_cast<int>(child->style.display)
                << " position=" << static_cast<int>(child->style.position)
                << " font=" << child->style.font_size
                << " line=" << child->style.line_height
                << " children=" << child->children.size()
                << " text=" << child->text_content.size();
            if (!child->attributes.empty()) {
                result << " attrs=";
                for (const auto& [name, value] : child->attributes) {
                    result << name << '=' << value.substr(0, 80) << ';';
                }
            }
            if (child->id == 27U) {
                result << " descendants=";
                for (const auto* grandchild : child->children) {
                    result << grandchild->tag << '#' << grandchild->id
                        << '(' << grandchild->layout.width << 'x' << grandchild->layout.height
                        << ",font=" << grandchild->style.font_size
                        << ",line=" << grandchild->style.line_height
                        << ",text=" << grandchild->text_content.size()
                        << ",children=" << grandchild->children.size() << ");";
                }
            }
            result << '}';
        }
        break;
    }
    const dom_node* markup_root = nullptr;
    for (const auto& node : nodes_) {
        if (node->class_name.find("chart-markup-table") != std::string::npos) {
            markup_root = node.get();
            break;
        }
    }
    if (markup_root != nullptr) {
        result << " | markup-tree:";
        std::function<void(const dom_node&, int)> append_tree =
            [&](const dom_node& node, int depth) {
                if (depth > 5) return;
                result << " {" << depth << ':' << node.tag << '#' << node.id;
                if (!node.class_name.empty()) result << "[class=" << node.class_name << ']';
                result << " layout=" << node.layout.x << ',' << node.layout.y << ','
                    << node.layout.width << ',' << node.layout.height
                    << " display=" << static_cast<int>(node.style.display)
                    << " position=" << static_cast<int>(node.style.position)
                    << " children=" << node.children.size() << '}';
                for (const auto* child : node.children) append_tree(*child, depth + 1);
            };
        append_tree(*markup_root, 0);
    }
    for (const auto& node : nodes_) {
        if (node->class_name.find("js-rootresizer__contents") == std::string::npos) continue;
        result << " | layout-tree:";
        std::function<void(const dom_node&, int)> append_layout_tree =
            [&](const dom_node& current, int depth) {
                if (depth > 4) return;
                result << " {" << depth << ':' << current.tag << '#' << current.id;
                if (!current.class_name.empty()) result << "[class=" << current.class_name << ']';
                result << " layout=" << current.layout.x << ',' << current.layout.y << ','
                    << current.layout.width << ',' << current.layout.height
                    << " position=" << static_cast<int>(current.style.position)
                    << " pointer-none=" << current.style.pointer_events_none
                    << " children=" << current.children.size() << '}';
                for (const auto* child : current.children) append_layout_tree(*child, depth + 1);
            };
        append_layout_tree(*node, 0);
        break;
    }
    result << " | aria-controls:";
    for (const auto& node : nodes_) {
        const auto label = node->attributes.find("aria-label");
        if (label == node->attributes.end() || label->second.empty()
            || node->parent == nullptr || !node->visible
            || node->layout.width <= 0 || node->layout.height <= 0) {
            continue;
        }
        result << " {" << node->tag << '#' << node->id
            << " label=" << label->second.substr(0, 48);
        if (!node->class_name.empty()) result << " class=" << node->class_name.substr(0, 96);
        result << " layout=" << node->layout.x << ',' << node->layout.y << ','
            << node->layout.width << ',' << node->layout.height
            << " display=" << static_cast<int>(node->style.display)
            << " position=" << static_cast<int>(node->style.position)
            << " children=" << node->children.size() << '}';
    }
    result << " | end-aria";
    return result.str();
#endif
}

layout_rect native_document::busiest_canvas_layout() const noexcept
{
    const dom_node* busiest = nullptr;
    size_t maximum_commands = 0;
    for (const auto& node : nodes_) {
        if (node->tag != "canvas" || !is_connected(*node)) continue;
        const auto& canvas = node->canvas();
        const auto commands = canvas.rects.size() + canvas.lines.size();
        if (commands > maximum_commands) {
            maximum_commands = commands;
            busiest = node.get();
        }
    }
    return busiest == nullptr ? layout_rect{} : busiest->layout;
}

bool native_document::is_connected(const dom_node& node) const noexcept
{
    auto* current = &node;
    // Mutation entry points reject hierarchy cycles, but retain a finite guard
    // here because scene publication must never hang on malformed native state.
    for (size_t depth = 0; current != nullptr && depth <= nodes_.size(); ++depth) {
        if (current == body_) return true;
        current = current->parent;
    }
    return false;
}

bool native_document::dirty() const noexcept
{
    return dirty_;
}

void native_document::mark_dirty() noexcept
{
    dirty_ = true;
    globally_dirty_ = true;
    out_of_flow_geometry_dirty_roots_.clear();
}

void native_document::mark_out_of_flow_geometry_dirty(dom_node& node) noexcept
{
    if (node.parent == nullptr
        || (node.style.position != position_mode::absolute
            && node.style.position != position_mode::fixed)) {
        mark_dirty();
        return;
    }
    dirty_ = true;
    if (globally_dirty_) return;

    // Keep the smallest non-overlapping set of invalid roots. A descendant of
    // an already-invalid root adds no information; when the new root contains
    // an existing one, replace the narrower entry.
    for (auto* current = &node; current != nullptr; current = current->parent) {
        if (std::find(
                out_of_flow_geometry_dirty_roots_.begin(),
                out_of_flow_geometry_dirty_roots_.end(),
                current) != out_of_flow_geometry_dirty_roots_.end()) {
            return;
        }
    }
    std::erase_if(
        out_of_flow_geometry_dirty_roots_,
        [&node](const auto* existing) {
            for (auto* current = existing; current != nullptr; current = current->parent) {
                if (current == &node) return true;
            }
            return false;
        });
    out_of_flow_geometry_dirty_roots_.push_back(&node);
}

bool native_document::can_reuse_client_geometry(const dom_node& node) const noexcept
{
    if (!dirty_) return true;
    if (globally_dirty_ || layout_passes_ == 0
        || out_of_flow_geometry_dirty_roots_.empty()) {
        return false;
    }
    // Width/height/inset changes on an out-of-flow box do not change the
    // client box of its ancestors or unrelated branches. Its own box and all
    // descendants still require the pending layout.
    for (auto* current = &node; current != nullptr; current = current->parent) {
        if (std::find(
                out_of_flow_geometry_dirty_roots_.begin(),
                out_of_flow_geometry_dirty_roots_.end(),
                current) != out_of_flow_geometry_dirty_roots_.end()) {
            return false;
        }
    }
    return true;
}

void native_document::signal_animation_frame(double timestamp_ms) noexcept
{
    if (std::isfinite(timestamp_ms) && timestamp_ms >= animation_frame_timestamp_ms_) {
        animation_frame_timestamp_ms_ = timestamp_ms;
    }
}

void native_document::update_style_animations(dom_node& node)
{
    if (!node.has_animation_runtime() && !node.style.has_animation_data()) {
        return;
    }
    auto& animation = node.mutable_animation_runtime();
    const auto length_equal = [](css_length left, css_length right) {
        return left.unit == right.unit
            && std::abs(left.value - right.value) < 0.001F
            && std::abs(left.pixel_offset - right.pixel_offset) < 0.001F;
    };
    const auto transform_target_changed = !length_equal(
            node.style.transform_translate_x,
            animation.transform_animation_target_translate_x)
        || !length_equal(
            node.style.transform_translate_y,
            animation.transform_animation_target_translate_y)
        || std::abs(node.style.transform_scale_x
            - animation.transform_animation_target_scale_x) >= 0.001F
        || std::abs(node.style.transform_scale_y
            - animation.transform_animation_target_scale_y) >= 0.001F
        || std::abs(node.style.transform_rotate_degrees
            - animation.transform_animation_target_degrees) >= 0.001F;
    if (!animation.transform_animation_initialized) {
        animation.transform_animation_initialized = true;
        animation.painted_transform_translate_x = node.style.transform_translate_x;
        animation.painted_transform_translate_y = node.style.transform_translate_y;
        animation.painted_transform_scale_x = node.style.transform_scale_x;
        animation.painted_transform_scale_y = node.style.transform_scale_y;
        animation.painted_transform_rotate_degrees = node.style.transform_rotate_degrees;
        animation.transform_animation_target_translate_x = node.style.transform_translate_x;
        animation.transform_animation_target_translate_y = node.style.transform_translate_y;
        animation.transform_animation_target_scale_x = node.style.transform_scale_x;
        animation.transform_animation_target_scale_y = node.style.transform_scale_y;
        animation.transform_animation_target_degrees = node.style.transform_rotate_degrees;
    } else if (transform_target_changed) {
        if (animation.transform_animation_active) {
            transition_events_.push_back({
                node.id,
                "transitioncancel",
                "transform",
                std::clamp(
                    static_cast<float>(animation_frame_timestamp_ms_
                        - animation.transform_animation_started_ms
                        - animation.transform_animation_delay_ms),
                    0.0F,
                    animation.transform_animation_duration_ms) / 1000.0F});
        }
        if (node.style.animations().transform_transition.duration_ms <= 0) {
        animation.painted_transform_translate_x = node.style.transform_translate_x;
        animation.painted_transform_translate_y = node.style.transform_translate_y;
        animation.painted_transform_scale_x = node.style.transform_scale_x;
        animation.painted_transform_scale_y = node.style.transform_scale_y;
        animation.painted_transform_rotate_degrees = node.style.transform_rotate_degrees;
        animation.transform_animation_target_translate_x = node.style.transform_translate_x;
        animation.transform_animation_target_translate_y = node.style.transform_translate_y;
        animation.transform_animation_target_scale_x = node.style.transform_scale_x;
        animation.transform_animation_target_scale_y = node.style.transform_scale_y;
        animation.transform_animation_target_degrees = node.style.transform_rotate_degrees;
        animation.transform_animation_active = false;
        } else {
            animation.transform_animation_from_translate_x = animation.painted_transform_translate_x;
            animation.transform_animation_from_translate_y = animation.painted_transform_translate_y;
            animation.transform_animation_target_translate_x = node.style.transform_translate_x;
            animation.transform_animation_target_translate_y = node.style.transform_translate_y;
            animation.transform_animation_from_scale_x = animation.painted_transform_scale_x;
            animation.transform_animation_from_scale_y = animation.painted_transform_scale_y;
            animation.transform_animation_target_scale_x = node.style.transform_scale_x;
            animation.transform_animation_target_scale_y = node.style.transform_scale_y;
            animation.transform_animation_from_degrees = animation.painted_transform_rotate_degrees;
            animation.transform_animation_target_degrees = node.style.transform_rotate_degrees;
            animation.transform_animation_duration_ms = node.style.animations().transform_transition.duration_ms;
            animation.transform_animation_delay_ms = node.style.animations().transform_transition.delay_ms;
            animation.transform_animation_x1 = node.style.animations().transform_transition.x1;
            animation.transform_animation_y1 = node.style.animations().transform_transition.y1;
            animation.transform_animation_x2 = node.style.animations().transform_transition.x2;
            animation.transform_animation_y2 = node.style.animations().transform_transition.y2;
            animation.transform_animation_started_ms = animation_frame_timestamp_ms_;
            animation.transform_animation_active = true;
            animation.transform_animation_start_event_sent = animation.transform_animation_delay_ms <= 0;
            transition_events_.push_back({node.id, "transitionrun", "transform", 0});
            if (animation.transform_animation_start_event_sent) {
                transition_events_.push_back({
                    node.id,
                    "transitionstart",
                    "transform",
                    std::min(-animation.transform_animation_delay_ms,
                        animation.transform_animation_duration_ms) / 1000.0F});
            }
        }
    }

    if (animation.opacity_keyframe_animation_signature
        != node.style.animations().opacity_keyframe_animation_signature) {
        animation.opacity_keyframe_animation_signature =
            node.style.animations().opacity_keyframe_animation_signature;
        animation.opacity_keyframe_animation_started_ms = animation_frame_timestamp_ms_;
        animation.opacity_keyframe_animation_active =
            !animation.opacity_keyframe_animation_signature.empty()
            && node.style.animations().opacity_keyframes.size() >= 2U
            && node.style.animations().opacity_keyframe_duration_ms > 0
            && node.style.animations().opacity_keyframe_iterations != 0;
        if (!animation.opacity_keyframe_animation_active) {
            animation.painted_opacity = node.style.opacity;
        }
    }
    if (animation.rotation_keyframe_animation_signature
        != node.style.animations().rotation_keyframe_animation_signature) {
        animation.rotation_keyframe_animation_signature =
            node.style.animations().rotation_keyframe_animation_signature;
        animation.rotation_keyframe_animation_started_ms = animation_frame_timestamp_ms_;
        animation.rotation_keyframe_animation_active =
            !animation.rotation_keyframe_animation_signature.empty()
            && node.style.animations().rotation_keyframes.size() >= 2U
            && node.style.animations().opacity_keyframe_duration_ms > 0
            && node.style.animations().opacity_keyframe_iterations != 0;
        if (!animation.rotation_keyframe_animation_active) {
            animation.painted_transform_rotate_degrees = node.style.transform_rotate_degrees;
        }
    }

    if (!animation.opacity_animation_initialized) {
        animation.opacity_animation_initialized = true;
        animation.painted_opacity = node.style.opacity;
        animation.opacity_animation_target = node.style.opacity;
    } else if (std::abs(node.style.opacity - animation.opacity_animation_target) >= 0.0001F) {
        if (animation.opacity_animation_active) {
            transition_events_.push_back({
                node.id,
                "transitioncancel",
                "opacity",
                std::clamp(
                    static_cast<float>(animation_frame_timestamp_ms_
                        - animation.opacity_animation_started_ms - animation.opacity_animation_delay_ms),
                    0.0F,
                    animation.opacity_animation_duration_ms) / 1000.0F});
        }
        animation.opacity_animation_target = node.style.opacity;
        if (node.style.animations().opacity_transition.duration_ms <= 0) {
            animation.painted_opacity = node.style.opacity;
            animation.opacity_animation_active = false;
        } else {
            animation.opacity_animation_from = animation.painted_opacity;
            animation.opacity_animation_duration_ms = node.style.animations().opacity_transition.duration_ms;
            animation.opacity_animation_delay_ms = node.style.animations().opacity_transition.delay_ms;
            animation.opacity_animation_x1 = node.style.animations().opacity_transition.x1;
            animation.opacity_animation_y1 = node.style.animations().opacity_transition.y1;
            animation.opacity_animation_x2 = node.style.animations().opacity_transition.x2;
            animation.opacity_animation_y2 = node.style.animations().opacity_transition.y2;
            animation.opacity_animation_started_ms = animation_frame_timestamp_ms_;
            animation.opacity_animation_active = true;
            animation.opacity_animation_start_event_sent = animation.opacity_animation_delay_ms <= 0;
            transition_events_.push_back({node.id, "transitionrun", "opacity", 0});
            if (animation.opacity_animation_start_event_sent) {
                transition_events_.push_back({
                    node.id,
                    "transitionstart",
                    "opacity",
                    std::min(-animation.opacity_animation_delay_ms,
                        animation.opacity_animation_duration_ms) / 1000.0F});
            }
        }
    }

    const auto resolved_color = [&]() {
        for (auto* current = &node; current != nullptr; current = current->parent) {
            if ((current->style.foreground_rgba & 0xFFU) != 0U) {
                return current->style.foreground_rgba;
            }
        }
        return 0xD1D4DCFFU;
    }();
    if (!animation.color_animation_initialized) {
        animation.color_animation_initialized = true;
        animation.painted_foreground_rgba = resolved_color;
        animation.color_animation_target_rgba = resolved_color;
    } else if (resolved_color != animation.color_animation_target_rgba) {
        if (animation.color_animation_active) {
            transition_events_.push_back({
                node.id,
                "transitioncancel",
                "color",
                std::clamp(
                    static_cast<float>(animation_frame_timestamp_ms_
                        - animation.color_animation_started_ms - animation.color_animation_delay_ms),
                    0.0F,
                    animation.color_animation_duration_ms) / 1000.0F});
        }
        animation.color_animation_target_rgba = resolved_color;
        if (node.style.animations().color_transition.duration_ms <= 0) {
            animation.painted_foreground_rgba = resolved_color;
            animation.color_animation_active = false;
        } else {
            animation.color_animation_from_rgba = animation.painted_foreground_rgba;
            animation.color_animation_duration_ms = node.style.animations().color_transition.duration_ms;
            animation.color_animation_delay_ms = node.style.animations().color_transition.delay_ms;
            animation.color_animation_x1 = node.style.animations().color_transition.x1;
            animation.color_animation_y1 = node.style.animations().color_transition.y1;
            animation.color_animation_x2 = node.style.animations().color_transition.x2;
            animation.color_animation_y2 = node.style.animations().color_transition.y2;
            animation.color_animation_started_ms = animation_frame_timestamp_ms_;
            animation.color_animation_active = true;
            animation.color_animation_start_event_sent = animation.color_animation_delay_ms <= 0;
            transition_events_.push_back({node.id, "transitionrun", "color", 0});
            if (animation.color_animation_start_event_sent) {
                transition_events_.push_back({
                    node.id,
                    "transitionstart",
                    "color",
                    std::min(-animation.color_animation_delay_ms,
                        animation.color_animation_duration_ms) / 1000.0F});
            }
        }

    }
}

bool native_document::advance_animations() noexcept
{
    // The worker can revisit active animations several times between host
    // requestAnimationFrame ticks. Re-evaluating an unchanged timestamp creates
    // redundant scene work and, on large component trees, can starve discrete
    // pointer input behind identical animation publications.
    if (animation_frame_timestamp_ms_ == last_animation_advance_timestamp_ms_) {
        return false;
    }
    last_animation_advance_timestamp_ms_ = animation_frame_timestamp_ms_;

    const auto cubic = [](float t, float first, float second) {
        const auto inverse = 1.0F - t;
        return 3.0F * inverse * inverse * t * first
            + 3.0F * inverse * t * t * second
            + t * t * t;
    };
    const auto cubic_derivative = [](float t, float first, float second) {
        const auto inverse = 1.0F - t;
        return 3.0F * inverse * inverse * first
            + 6.0F * inverse * t * (second - first)
            + 3.0F * t * t * (1.0F - second);
    };
    const auto timing_value = [&](float progress, float x1, float y1, float x2, float y2) {
        // CSS cubic-bezier timing is parameterized by x, so solve x(t) for the
        // elapsed-time progress before sampling y(t). Newton converges quickly
        // for ordinary curves; bisection keeps unusual but valid curves stable.
        auto parameter = progress;
        for (auto iteration = 0; iteration < 5; ++iteration) {
            const auto error = cubic(
                parameter,
                x1, x2) - progress;
            const auto derivative = cubic_derivative(
                parameter,
                x1, x2);
            if (std::abs(error) < 0.0001F || std::abs(derivative) < 0.0001F) break;
            parameter = std::clamp(parameter - error / derivative, 0.0F, 1.0F);
        }
        auto lower = 0.0F;
        auto upper = 1.0F;
        for (auto iteration = 0; iteration < 8; ++iteration) {
            const auto sampled = cubic(
                parameter,
                x1, x2);
            if (std::abs(sampled - progress) < 0.0001F) break;
            if (sampled < progress) lower = parameter;
            else upper = parameter;
            parameter = (lower + upper) * 0.5F;
        }
        return cubic(
            parameter,
            y1, y2);
    };
    const auto interpolate_length = [](css_length from, css_length target, float progress) {
        const auto normalized = [](css_length value) {
            if (value.unit == length_unit::automatic) value.unit = length_unit::pixels;
            return value;
        };
        from = normalized(from);
        target = normalized(target);
        if (from.unit == target.unit) {
            return css_length{
                from.value + (target.value - from.value) * progress,
                from.unit,
                from.pixel_offset + (target.pixel_offset - from.pixel_offset) * progress};
        }
        const auto is_linear_length = [](css_length value) {
            return value.unit == length_unit::pixels || value.unit == length_unit::percent;
        };
        if (is_linear_length(from) && is_linear_length(target)) {
            const auto percent = [](css_length value) {
                return value.unit == length_unit::percent ? value.value : 0.0F;
            };
            const auto pixels = [](css_length value) {
                return (value.unit == length_unit::pixels ? value.value : 0.0F)
                    + value.pixel_offset;
            };
            return css_length{
                percent(from) + (percent(target) - percent(from)) * progress,
                length_unit::percent,
                pixels(from) + (pixels(target) - pixels(from)) * progress};
        }
        return progress >= 1.0F ? target : from;
    };
    auto advanced = false;
    auto layout_changed = false;
    for (auto& owner : nodes_) {
        auto& node = *owner;
        auto* animation_pointer = node.animation_runtime();
        if (animation_pointer == nullptr) continue;
        auto& animation = *animation_pointer;
        if (animation.transform_animation_active) {
            const auto previous_translate_x = animation.painted_transform_translate_x;
            const auto previous_translate_y = animation.painted_transform_translate_y;
            const auto elapsed_ms = static_cast<float>(
                animation_frame_timestamp_ms_ - animation.transform_animation_started_ms
                - animation.transform_animation_delay_ms);
        if (elapsed_ms >= 0 && !animation.transform_animation_start_event_sent) {
            animation.transform_animation_start_event_sent = true;
            transition_events_.push_back({node.id, "transitionstart", "transform", 0});
        }
        const auto progress = std::clamp(
            elapsed_ms / std::max(0.001F, animation.transform_animation_duration_ms),
            0.0F,
            1.0F);
        const auto eased = timing_value(
            progress,
            animation.transform_animation_x1,
            animation.transform_animation_y1,
            animation.transform_animation_x2,
            animation.transform_animation_y2);
        animation.painted_transform_translate_x = interpolate_length(
            animation.transform_animation_from_translate_x,
            animation.transform_animation_target_translate_x,
            eased);
        animation.painted_transform_translate_y = interpolate_length(
            animation.transform_animation_from_translate_y,
            animation.transform_animation_target_translate_y,
            eased);
            const auto translated = [](css_length left, css_length right) {
                return left.unit != right.unit
                    || std::abs(left.value - right.value) >= 0.001F
                    || std::abs(left.pixel_offset - right.pixel_offset) >= 0.001F;
            };
            layout_changed = layout_changed
                || translated(previous_translate_x, animation.painted_transform_translate_x)
                || translated(previous_translate_y, animation.painted_transform_translate_y);
        animation.painted_transform_scale_x = animation.transform_animation_from_scale_x
            + (animation.transform_animation_target_scale_x
                - animation.transform_animation_from_scale_x) * eased;
        animation.painted_transform_scale_y = animation.transform_animation_from_scale_y
            + (animation.transform_animation_target_scale_y
                - animation.transform_animation_from_scale_y) * eased;
        animation.painted_transform_rotate_degrees =
            animation.transform_animation_from_degrees
            + (animation.transform_animation_target_degrees
                - animation.transform_animation_from_degrees) * eased;
        advanced = true;
        if (progress >= 1.0F) {
            animation.painted_transform_translate_x = animation.transform_animation_target_translate_x;
            animation.painted_transform_translate_y = animation.transform_animation_target_translate_y;
            animation.painted_transform_scale_x = animation.transform_animation_target_scale_x;
            animation.painted_transform_scale_y = animation.transform_animation_target_scale_y;
            animation.painted_transform_rotate_degrees = animation.transform_animation_target_degrees;
            animation.transform_animation_active = false;
            transition_events_.push_back({
                node.id,
                "transitionend",
                "transform",
                animation.transform_animation_duration_ms / 1000.0F});
        }
        }

        if (animation.opacity_animation_active) {
            const auto elapsed_ms = static_cast<float>(
                animation_frame_timestamp_ms_ - animation.opacity_animation_started_ms
                - animation.opacity_animation_delay_ms);
            if (elapsed_ms >= 0 && !animation.opacity_animation_start_event_sent) {
                animation.opacity_animation_start_event_sent = true;
                transition_events_.push_back({node.id, "transitionstart", "opacity", 0});
            }
            const auto progress = std::clamp(
                elapsed_ms / std::max(0.001F, animation.opacity_animation_duration_ms),
                0.0F,
                1.0F);
            const auto eased = timing_value(
                progress,
                animation.opacity_animation_x1,
                animation.opacity_animation_y1,
                animation.opacity_animation_x2,
                animation.opacity_animation_y2);
            animation.painted_opacity = animation.opacity_animation_from
                + (animation.opacity_animation_target - animation.opacity_animation_from) * eased;
            advanced = true;
            if (progress >= 1) {
                animation.painted_opacity = animation.opacity_animation_target;
                animation.opacity_animation_active = false;
                transition_events_.push_back({
                    node.id,
                    "transitionend",
                    "opacity",
                    animation.opacity_animation_duration_ms / 1000.0F});
            }
        }

        if (animation.color_animation_active) {
            const auto elapsed_ms = static_cast<float>(
                animation_frame_timestamp_ms_ - animation.color_animation_started_ms
                - animation.color_animation_delay_ms);
            if (elapsed_ms >= 0 && !animation.color_animation_start_event_sent) {
                animation.color_animation_start_event_sent = true;
                transition_events_.push_back({node.id, "transitionstart", "color", 0});
            }
            const auto progress = std::clamp(
                elapsed_ms / std::max(0.001F, animation.color_animation_duration_ms),
                0.0F,
                1.0F);
            const auto eased = timing_value(
                progress,
                animation.color_animation_x1,
                animation.color_animation_y1,
                animation.color_animation_x2,
                animation.color_animation_y2);
            const auto interpolate_color = [](uint32_t from, uint32_t target, float amount) {
                uint32_t result = 0;
                for (auto shift : {24U, 16U, 8U, 0U}) {
                    const auto first = static_cast<float>((from >> shift) & 0xFFU);
                    const auto last = static_cast<float>((target >> shift) & 0xFFU);
                    const auto channel = static_cast<uint32_t>(std::clamp(
                        std::lround(first + (last - first) * amount), 0L, 255L));
                    result |= channel << shift;
                }
                return result;
            };
            animation.painted_foreground_rgba = interpolate_color(
                animation.color_animation_from_rgba,
                animation.color_animation_target_rgba,
                eased);
            advanced = true;
            if (progress >= 1) {
                animation.painted_foreground_rgba = animation.color_animation_target_rgba;
                animation.color_animation_active = false;
                transition_events_.push_back({
                    node.id,
                    "transitionend",
                    "color",
                    animation.color_animation_duration_ms / 1000.0F});
            }
        }

        if (animation.opacity_keyframe_animation_active) {
            const auto elapsed_ms = static_cast<float>(
                animation_frame_timestamp_ms_
                - animation.opacity_keyframe_animation_started_ms
                - node.style.animations().opacity_keyframe_delay_ms);
            if (elapsed_ms < 0) {
                animation.painted_opacity = node.style.opacity;
                advanced = true;
            } else {
                const auto duration = std::max(
                    0.001F, node.style.animations().opacity_keyframe_duration_ms);
                const auto iterations = node.style.animations().opacity_keyframe_iterations;
                const auto finite = std::isfinite(iterations);
                const auto total_duration = finite
                    ? duration * std::max(0.0F, iterations)
                    : 0.0F;
                if (finite && elapsed_ms >= total_duration) {
                    // The bounded slice implements the initial fill-mode (`none`).
                    animation.painted_opacity = node.style.opacity;
                    animation.opacity_keyframe_animation_active = false;
                    advanced = true;
                } else {
                    auto cycle_progress = std::fmod(elapsed_ms, duration) / duration;
                    if (cycle_progress < 0) cycle_progress += 1.0F;
                    const auto& stops = node.style.animations().opacity_keyframes;
                    auto right = std::lower_bound(
                        stops.begin(), stops.end(), cycle_progress,
                        [](const node_style::opacity_keyframe& stop, float progress) {
                            return stop.offset < progress;
                        });
                    if (right == stops.begin()) {
                        animation.painted_opacity = right->opacity;
                    } else if (right == stops.end()) {
                        animation.painted_opacity = stops.back().opacity;
                    } else {
                        const auto& from = *(right - 1);
                        const auto span = std::max(
                            0.0001F, right->offset - from.offset);
                        const auto segment_progress = std::clamp(
                            (cycle_progress - from.offset) / span, 0.0F, 1.0F);
                        const auto eased = timing_value(
                            segment_progress,
                            node.style.animations().opacity_keyframe_x1,
                            node.style.animations().opacity_keyframe_y1,
                            node.style.animations().opacity_keyframe_x2,
                            node.style.animations().opacity_keyframe_y2);
                        animation.painted_opacity = from.opacity
                            + (right->opacity - from.opacity) * eased;
                    }
                    advanced = true;
                }
            }
        }

        if (animation.rotation_keyframe_animation_active) {
            const auto elapsed_ms = static_cast<float>(
                animation_frame_timestamp_ms_
                - animation.rotation_keyframe_animation_started_ms
                - node.style.animations().opacity_keyframe_delay_ms);
            if (elapsed_ms < 0) {
                animation.painted_transform_rotate_degrees =
                    node.style.transform_rotate_degrees;
                advanced = true;
            } else {
                const auto duration = std::max(
                    0.001F, node.style.animations().opacity_keyframe_duration_ms);
                const auto iterations = node.style.animations().opacity_keyframe_iterations;
                const auto finite = std::isfinite(iterations);
                const auto total_duration = finite
                    ? duration * std::max(0.0F, iterations)
                    : 0.0F;
                if (finite && elapsed_ms >= total_duration) {
                    animation.painted_transform_rotate_degrees =
                        node.style.transform_rotate_degrees;
                    animation.rotation_keyframe_animation_active = false;
                    advanced = true;
                } else {
                    auto cycle_progress = std::fmod(elapsed_ms, duration) / duration;
                    if (cycle_progress < 0) cycle_progress += 1.0F;
                    const auto& stops = node.style.animations().rotation_keyframes;
                    auto right = std::lower_bound(
                        stops.begin(), stops.end(), cycle_progress,
                        [](const node_style::rotation_keyframe& stop, float progress) {
                            return stop.offset < progress;
                        });
                    if (right == stops.begin()) {
                        animation.painted_transform_rotate_degrees = right->degrees;
                    } else if (right == stops.end()) {
                        animation.painted_transform_rotate_degrees = stops.back().degrees;
                    } else {
                        const auto& from = *(right - 1);
                        const auto span = std::max(
                            0.0001F, right->offset - from.offset);
                        const auto segment_progress = std::clamp(
                            (cycle_progress - from.offset) / span, 0.0F, 1.0F);
                        const auto eased = timing_value(
                            segment_progress,
                            node.style.animations().opacity_keyframe_x1,
                            node.style.animations().opacity_keyframe_y1,
                            node.style.animations().opacity_keyframe_x2,
                            node.style.animations().opacity_keyframe_y2);
                        animation.painted_transform_rotate_degrees = from.degrees
                            + (right->degrees - from.degrees) * eased;
                    }
                    advanced = true;
                }
            }
        }
    }
    // Opacity, color, scale, and rotation are paint-only. Their interpolated
    // values are consumed directly by build_scene(), so they must not repeat the
    // entire document's intrinsic sizing, shaping, and layout. Translation is
    // still reflected in the retained layout coordinates and therefore remains
    // a geometry invalidation until translated boxes are represented separately.
    if (layout_changed) mark_dirty();
    return advanced;
}

bool native_document::has_active_animations() const noexcept
{
    return std::any_of(nodes_.begin(), nodes_.end(), [](const auto& node) {
        const auto* animation = node->animation_runtime();
        return animation != nullptr
            && (animation->transform_animation_active
                || animation->opacity_animation_active
                || animation->color_animation_active
                || animation->opacity_keyframe_animation_active
                || animation->rotation_keyframe_animation_active);
    });
}

std::vector<native_document::transition_event_record>
native_document::take_transition_events()
{
    auto events = std::move(transition_events_);
    transition_events_.clear();
    return events;
}

css_length native_document::parse_length(const std::string& value)
{
    if (value.empty() || value == "auto") {
        return {};
    }
    if (value.starts_with("calc(") || value.starts_with("min(") || value.starts_with("max(")) {
        if (const auto calculated = calc_parser(value).parse(); calculated.has_value()) {
            return {
                static_cast<float>(calculated->value),
                calculated->unit == calc_unit::percent
                    ? length_unit::percent
                    : length_unit::pixels,
                static_cast<float>(calculated->pixel_offset)};
        }
        return {};
    }
    if (value == "max-content") return {0, length_unit::max_content};
    if (value == "min-content") return {0, length_unit::min_content};
    if (value == "fit-content") return {0, length_unit::fit_content};
    if (value.size() > 1 && value.back() == '%') {
        return {parse_number(std::string_view(value).substr(0, value.size() - 1)), length_unit::percent};
    }
    auto length = value.size();
    auto unit = length_unit::pixels;
    auto multiplier = 1.0F;
    if (length >= 3 && value.ends_with("rem")) {
        length -= 3;
        unit = length_unit::rem;
    } else if (length >= 2 && value.ends_with("em")) {
        length -= 2;
        unit = length_unit::em;
    } else if (length >= 2 && value.ends_with("vw")) {
        length -= 2;
        unit = length_unit::viewport_width;
    } else if (length >= 2 && value.ends_with("vh")) {
        length -= 2;
        unit = length_unit::viewport_height;
    } else if (length >= 2 && value.ends_with("in")) {
        length -= 2;
        multiplier = 96.0F;
    } else if (length >= 2 && value.ends_with("cm")) {
        length -= 2;
        multiplier = 96.0F / 2.54F;
    } else if (length >= 2 && value.ends_with("mm")) {
        length -= 2;
        multiplier = 96.0F / 25.4F;
    } else if (length >= 2 && value.ends_with("pt")) {
        length -= 2;
        multiplier = 96.0F / 72.0F;
    } else if (length >= 2 && value.ends_with("pc")) {
        length -= 2;
        multiplier = 16.0F;
    } else if (length >= 1 && value.ends_with("q")) {
        length -= 1;
        multiplier = 96.0F / 101.6F;
    } else if (length >= 2 && value[length - 2] == 'p' && value[length - 1] == 'x') {
        length -= 2;
    }
    return {
        parse_number(std::string_view(value).substr(0, length)) * multiplier,
        unit};
}

void native_document::parse_transform_translate(
    const std::string& value,
    css_length& translate_x,
    css_length& translate_y,
    float& scale_x,
    float& scale_y,
    float& rotate_degrees)
{
    translate_x = {};
    translate_y = {};
    scale_x = 1;
    scale_y = 1;
    rotate_degrees = 0;
    if (value.empty() || value == "none") return;

    const auto parse_arguments = [&](std::string_view function) {
        const auto open = value.find(std::string(function) + "(");
        if (open == std::string::npos) return std::vector<std::string>{};
        const auto start = open + function.size() + 1U;
        const auto close = value.find(')', start);
        if (close == std::string::npos) return std::vector<std::string>{};
        auto arguments = value.substr(start, close - start);
        std::replace(arguments.begin(), arguments.end(), ',', ' ');
        std::istringstream stream(arguments);
        std::vector<std::string> result;
        for (std::string argument; stream >> argument;) result.push_back(std::move(argument));
        return result;
    };

    if (const auto arguments = parse_arguments("translate"); !arguments.empty()) {
        translate_x = parse_length(arguments[0]);
        translate_y = arguments.size() > 1 ? parse_length(arguments[1]) : css_length{};
    } else {
        if (const auto arguments = parse_arguments("translateX"); !arguments.empty()) {
            translate_x = parse_length(arguments[0]);
        }
        if (const auto arguments = parse_arguments("translateY"); !arguments.empty()) {
            translate_y = parse_length(arguments[0]);
        }
    }

    if (const auto arguments = parse_arguments("scale"); !arguments.empty()) {
        scale_x = parse_number(arguments[0]);
        scale_y = arguments.size() > 1 ? parse_number(arguments[1]) : scale_x;
    } else {
        if (const auto arguments = parse_arguments("scaleX"); !arguments.empty()) {
            scale_x = parse_number(arguments[0]);
        }
        if (const auto arguments = parse_arguments("scaleY"); !arguments.empty()) {
            scale_y = parse_number(arguments[0]);
        }
    }

    if (const auto arguments = parse_arguments("rotate"); !arguments.empty()) {
        auto angle = arguments[0];
        if (angle.ends_with("deg")) angle.resize(angle.size() - 3U);
        else if (angle.ends_with("turn")) {
            angle.resize(angle.size() - 4U);
            rotate_degrees = parse_number(angle) * 360.0F;
            return;
        }
        rotate_degrees = parse_number(angle);
    }
}

void native_document::parse_transform_origin(
    const std::string& value,
    css_length& origin_x,
    css_length& origin_y)
{
    auto normalized = value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    std::istringstream stream(normalized);
    std::string first;
    std::string second;
    stream >> first >> second;

    const auto horizontal = [](const std::string& token) {
        if (token == "left") return css_length{0, length_unit::percent};
        if (token == "center") return css_length{50, length_unit::percent};
        if (token == "right") return css_length{100, length_unit::percent};
        return parse_length(token);
    };
    const auto vertical = [](const std::string& token) {
        if (token == "top") return css_length{0, length_unit::percent};
        if (token == "center") return css_length{50, length_unit::percent};
        if (token == "bottom") return css_length{100, length_unit::percent};
        return parse_length(token);
    };
    const auto is_vertical_keyword = [](const std::string& token) {
        return token == "top" || token == "bottom";
    };
    const auto is_horizontal_keyword = [](const std::string& token) {
        return token == "left" || token == "right";
    };

    if (first.empty()) {
        origin_x = {50, length_unit::percent};
        origin_y = {50, length_unit::percent};
    } else if (second.empty()) {
        if (is_vertical_keyword(first)) {
            origin_x = {50, length_unit::percent};
            origin_y = vertical(first);
        } else {
            origin_x = horizontal(first);
            origin_y = {50, length_unit::percent};
        }
    } else if (is_vertical_keyword(first) && is_horizontal_keyword(second)) {
        origin_x = horizontal(second);
        origin_y = vertical(first);
    } else {
        origin_x = horizontal(first);
        origin_y = vertical(second);
    }
}

uint32_t native_document::parse_color(const std::string& value)
{
    if (value == "transparent" || value.empty()) return 0;
    if (value == "black") return 0x000000FFU;
    if (value == "white") return 0xFFFFFFFFU;
    if (value == "red") return 0xFF0000FFU;
    if (value == "green") return 0x008000FFU;
    if (value == "lime") return 0x00FF00FFU;
    if (value == "blue") return 0x0000FFFFU;
    if (value == "yellow") return 0xFFFF00FFU;
    if (value == "cyan" || value == "aqua") return 0x00FFFFFFU;
    if (value == "magenta" || value == "fuchsia") return 0xFF00FFFFU;
    if (value == "gray" || value == "grey") return 0x808080FFU;
    if (value.starts_with("rgb(")) {
        int red = 0;
        int green = 0;
        int blue = 0;
        if (std::sscanf(value.c_str(), "rgb(%d, %d, %d)", &red, &green, &blue) == 3
            || std::sscanf(value.c_str(), "rgb(%d,%d,%d)", &red, &green, &blue) == 3) {
            return static_cast<uint32_t>(std::clamp(red, 0, 255)) << 24U
                | static_cast<uint32_t>(std::clamp(green, 0, 255)) << 16U
                | static_cast<uint32_t>(std::clamp(blue, 0, 255)) << 8U
                | 0xFFU;
        }
    }
    if (value.starts_with("rgba(")) {
        int red = 0;
        int green = 0;
        int blue = 0;
        float alpha = 0;
        if (std::sscanf(value.c_str(), "rgba(%d, %d, %d, %f)", &red, &green, &blue, &alpha) == 4
            || std::sscanf(value.c_str(), "rgba(%d,%d,%d,%f)", &red, &green, &blue, &alpha) == 4) {
            return static_cast<uint32_t>(std::clamp(red, 0, 255)) << 24U
                | static_cast<uint32_t>(std::clamp(green, 0, 255)) << 16U
                | static_cast<uint32_t>(std::clamp(blue, 0, 255)) << 8U
                | static_cast<uint32_t>(std::clamp(alpha, 0.0F, 1.0F) * 255.0F);
        }
    }
    if ((value.size() == 4 || value.size() == 5) && value.front() == '#') {
        const auto expand = [](char digit) {
            return parse_hex_pair(digit, digit);
        };
        return static_cast<uint32_t>(expand(value[1])) << 24U
            | static_cast<uint32_t>(expand(value[2])) << 16U
            | static_cast<uint32_t>(expand(value[3])) << 8U
            | (value.size() == 5 ? expand(value[4]) : 0xFFU);
    }
    if (value.size() == 7 && value.front() == '#') {
        return static_cast<uint32_t>(parse_hex_pair(value[1], value[2])) << 24U
            | static_cast<uint32_t>(parse_hex_pair(value[3], value[4])) << 16U
            | static_cast<uint32_t>(parse_hex_pair(value[5], value[6])) << 8U
            | 0xFFU;
    }
    if (value.size() == 9 && value.front() == '#') {
        return static_cast<uint32_t>(parse_hex_pair(value[1], value[2])) << 24U
            | static_cast<uint32_t>(parse_hex_pair(value[3], value[4])) << 16U
            | static_cast<uint32_t>(parse_hex_pair(value[5], value[6])) << 8U
            | parse_hex_pair(value[7], value[8]);
    }
    return 0;
}

float native_document::resolve_length(
    const dom_node& context,
    css_length value,
    float available,
    float fallback) const
{
    if (value.unit == length_unit::em) {
        return value.value * resolved_font_size(context);
    }
    if (value.unit == length_unit::rem) {
        auto* root = &context;
        while (root->parent != nullptr) root = root->parent;
        return value.value * resolved_font_size(*root);
    }
    return resolve_length(value, available, fallback);
}

float native_document::resolve_length(css_length value, float available, float fallback) const
{
    switch (value.unit) {
    case length_unit::pixels:
        return value.value;
    case length_unit::percent:
        return available * value.value / 100.0F + value.pixel_offset;
    case length_unit::em:
    case length_unit::rem:
        // Callers resolving element-authored CSS lengths must use the
        // context-aware overload. Preserve the root default here only for
        // non-element fallback paths.
        return value.value * 14.0F;
    case length_unit::viewport_width:
        return viewport_width_ * value.value / 100.0F;
    case length_unit::viewport_height:
        return viewport_height_ * value.value / 100.0F;
    case length_unit::max_content:
    case length_unit::min_content:
    case length_unit::fit_content:
        return fallback;
    case length_unit::automatic:
    default:
        return fallback;
    }
}

bool native_document::is_specified(css_length value)
{
    return value.unit != length_unit::automatic;
}

bool native_document::matches_selector(const dom_node& node, const std::string& selector)
{
    if (selector.empty()) return false;
    const auto selector_end = selector.find_first_of(" [:");
    const auto simple = selector.substr(0, selector_end);
    if (simple.empty() || simple == "*") return true;
    if (simple.front() == '#') return node.id_attribute == simple.substr(1);
    if (simple.front() == '.') {
        const auto wanted = simple.substr(1);
        size_t start = 0;
        while (start < node.class_name.size()) {
            const auto end = node.class_name.find(' ', start);
            const auto count = end == std::string::npos ? std::string::npos : end - start;
            if (node.class_name.substr(start, count) == wanted) return true;
            if (end == std::string::npos) break;
            start = end + 1;
        }
        return false;
    }
    return node.tag == simple;
}

void native_document::collect_matches(
    dom_node& node,
    const std::string& selector,
    std::vector<dom_node*>& result)
{
    if (matches_selector(node, selector)) result.push_back(&node);
    for (auto* child : node.children) {
        collect_matches(*child, selector, result);
    }
}

} // namespace htmlml_native
