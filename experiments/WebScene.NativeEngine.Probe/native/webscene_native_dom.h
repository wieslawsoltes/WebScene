#pragma once

#include "webscene_native_engine.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <memory_resource>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace webscene_native {

enum class length_unit : uint8_t {
    automatic,
    pixels,
    percent,
    em,
    rem,
    viewport_width,
    viewport_height,
    viewport_width_capped,
    viewport_height_capped,
    viewport_width_floored,
    viewport_height_floored,
    max_content,
    min_content,
    fit_content,
    stretch
};

struct css_length final {
    float value{0};
    length_unit unit{length_unit::automatic};
    // CSS calc() can combine a percentage with an absolute offset, for example
    // calc(50% - 32px). Keep both terms until the containing size is known.
    float pixel_offset{0};
};

struct layout_rect final {
    float x{0};
    float y{0};
    float width{0};
    float height{0};
};

enum class display_mode : uint8_t {
    block,
    flex,
    inline_flow,
    inline_block,
    inline_flex,
    grid,
    inline_grid,
    table,
    inline_table,
    table_row_group,
    table_header_group,
    table_footer_group,
    table_row,
    table_cell,
    table_column_group,
    table_column,
    table_caption,
    list_item,
    contents,
    none
};

enum class position_mode : uint8_t {
    normal,
    relative,
    absolute,
    fixed
};

enum class flex_direction : uint8_t {
    row,
    column
};

enum class align_mode : uint8_t {
    stretch,
    start,
    center,
    end,
    baseline
};

enum class justify_mode : uint8_t {
    start,
    center,
    end,
    space_between,
    space_around,
    space_evenly
};

enum class overflow_mode : uint8_t {
    visible,
    hidden,
    clip,
    automatic,
    scroll
};

enum class float_mode : uint8_t {
    none,
    left,
    right
};

struct node_style final {
    struct opacity_keyframe final {
        float offset{0};
        float opacity{1};
    };
    struct rotation_keyframe final {
        float offset{0};
        float degrees{0};
    };

    struct transition_timing final {
        float duration_ms{0};
        float delay_ms{0};
        float x1{0.25F};
        float y1{0.1F};
        float x2{0.25F};
        float y2{1.0F};
    };

    struct animation_data final {
        std::string transition_property_value{"all"};
        std::string transition_duration_value{"0s"};
        std::string transition_delay_value{"0s"};
        std::string transition_timing_function_value{"ease"};
        transition_timing transform_transition{};
        transition_timing left_transition{};
        transition_timing top_transition{};
        transition_timing opacity_transition{};
        transition_timing color_transition{};
        std::string animation_name_value{"none"};
        std::string animation_duration_value{"0s"};
        std::string animation_delay_value{"0s"};
        std::string animation_timing_function_value{"ease"};
        std::string animation_iteration_count_value{"1"};
        std::string opacity_keyframe_animation_signature;
        std::vector<opacity_keyframe> opacity_keyframes;
        std::string rotation_keyframe_animation_signature;
        std::vector<rotation_keyframe> rotation_keyframes;
        float opacity_keyframe_duration_ms{0};
        float opacity_keyframe_delay_ms{0};
        float opacity_keyframe_iterations{1};
        float opacity_keyframe_x1{0.25F};
        float opacity_keyframe_y1{0.1F};
        float opacity_keyframe_x2{0.25F};
        float opacity_keyframe_y2{1.0F};
    };

    const animation_data& animations() const noexcept
    {
        static const animation_data defaults;
        return animation_state == nullptr ? defaults : *animation_state;
    }

    bool has_animation_data() const noexcept
    {
        return animation_state != nullptr;
    }

    const animation_data* animation_data_identity() const noexcept
    {
        return animation_state.get();
    }

    animation_data& mutable_animations()
    {
        if (animation_state == nullptr) {
            animation_state = std::make_shared<animation_data>();
        } else if (animation_state.use_count() != 1) {
            animation_state = std::make_shared<animation_data>(*animation_state);
        }
        return *animation_state;
    }

    void clear_animations() noexcept
    {
        animation_state.reset();
    }

    struct custom_property_data final {
        std::unordered_map<std::string, std::string> values;
        std::unordered_set<std::string> important;
    };

    const custom_property_data& custom_properties() const noexcept
    {
        static const custom_property_data empty;
        return custom_property_state == nullptr ? empty : *custom_property_state;
    }

    custom_property_data& mutable_custom_properties()
    {
        if (custom_property_state == nullptr) {
            custom_property_state = std::make_shared<custom_property_data>();
        } else if (custom_property_state.use_count() != 1) {
            custom_property_state =
                std::make_shared<custom_property_data>(*custom_property_state);
        }
        return *custom_property_state;
    }

    bool has_custom_properties() const noexcept
    {
        return custom_property_state != nullptr
            && (!custom_property_state->values.empty()
                || !custom_property_state->important.empty());
    }

    const custom_property_data* custom_property_data_identity() const noexcept
    {
        return custom_property_state.get();
    }

    void clear_custom_properties() noexcept
    {
        custom_property_state.reset();
    }

    void move_custom_properties_from(node_style& source) noexcept
    {
        custom_property_state = std::move(source.custom_property_state);
    }

    struct background_image_data final {
        std::string image_value{"none"};
        std::string image_markup;
        std::string image_view_box;
        std::string repeat{"repeat"};
        std::string position_x{"0%"};
        std::string position_y{"0%"};
        std::string position_value{"0% 0%"};
        std::string size_x{"auto"};
        std::string size_y{"auto"};
        std::string size_value{"auto"};
    };

    const background_image_data& background_image() const noexcept
    {
        static const background_image_data defaults;
        return background_image_state == nullptr
            ? defaults
            : *background_image_state;
    }

    background_image_data& mutable_background_image()
    {
        if (background_image_state == nullptr) {
            background_image_state = std::make_shared<background_image_data>();
        } else if (background_image_state.use_count() != 1) {
            background_image_state =
                std::make_shared<background_image_data>(*background_image_state);
        }
        return *background_image_state;
    }

    bool has_background_image_data() const noexcept
    {
        return background_image_state != nullptr;
    }

    const background_image_data* background_image_data_identity() const noexcept
    {
        return background_image_state.get();
    }

    void clear_background_image() noexcept
    {
        background_image_state.reset();
    }

    struct grid_data final {
        struct track final {
            enum class sizing : uint8_t {
                fixed,
                automatic,
                min_content,
                fractional,
                minmax
            };

            css_length minimum{};
            css_length maximum{};
            float fraction{0};
            sizing kind{sizing::automatic};
        };

        std::vector<track> template_columns;
        std::vector<track> template_rows;
        std::vector<track> auto_columns;
        bool subgrid_columns{false};
        bool two_columns{false};
        bool auto_flow_column{false};
        bool fractional_rows{false};
        bool span_all{false};
        int32_t column_start{0};
        std::string area_value{"auto"};
        std::string row_value{"auto"};
        std::string row_start_value{"auto"};
        std::string row_end_value{"auto"};
        std::string column_value{"auto"};
        std::string column_start_value{"auto"};
        std::string column_end_value{"auto"};
    };

    const grid_data& grid() const noexcept
    {
        static const grid_data defaults;
        return grid_state == nullptr ? defaults : *grid_state;
    }

    grid_data& mutable_grid()
    {
        if (grid_state == nullptr) {
            grid_state = std::make_shared<grid_data>();
        } else if (grid_state.use_count() != 1) {
            grid_state = std::make_shared<grid_data>(*grid_state);
        }
        return *grid_state;
    }

    bool has_grid_data() const noexcept
    {
        return grid_state != nullptr;
    }

    const grid_data* grid_data_identity() const noexcept
    {
        return grid_state.get();
    }

    void clear_grid() noexcept
    {
        grid_state.reset();
    }

    struct pseudo_element final {
        css_length width{};
        css_length height{};
        css_length left{};
        css_length top{};
        css_length right{};
        css_length bottom{};
        css_length padding_left{};
        css_length padding_top{};
        css_length padding_right{};
        css_length padding_bottom{};
        css_length margin_left{};
        css_length margin_top{};
        css_length margin_right{};
        css_length margin_bottom{};
        css_length border_left_width{};
        css_length border_top_width{};
        css_length border_right_width{};
        css_length border_bottom_width{};
        css_length border_top_left_radius{};
        css_length border_top_right_radius{};
        css_length border_bottom_right_radius{};
        css_length border_bottom_left_radius{};
        css_length border_top_left_radius_y{};
        css_length border_top_right_radius_y{};
        css_length border_bottom_right_radius_y{};
        css_length border_bottom_left_radius_y{};
        css_length outline_width{};
        layout_rect layout{};
        display_mode display{display_mode::inline_flow};
        position_mode position{position_mode::normal};
        align_mode align_self{align_mode::stretch};
        int32_t z_index{0};
        float font_size{-1};
        float line_height{-1};
        float opacity{1};
        uint32_t background_rgba{0};
        background_image_data background_image{};
        uint32_t foreground_rgba{0};
        uint32_t border_left_rgba{0};
        uint32_t border_top_rgba{0};
        uint32_t border_right_rgba{0};
        uint32_t border_bottom_rgba{0};
        uint32_t outline_rgba{0};
        bool background_current_color{false};
        bool border_left_current_color{true};
        bool border_top_current_color{true};
        bool border_right_current_color{true};
        bool border_bottom_current_color{true};
        std::string content;
        bool generated{false};
        bool display_none{false};
        bool visibility_hidden{false};
        bool align_self_specified{false};
        bool border_box{false};
        bool elliptical_border_radius{false};
    };

    struct pseudo_element_pair final {
        pseudo_element before;
        pseudo_element after;
    };

    const pseudo_element& before_pseudo() const noexcept
    {
        static const pseudo_element empty;
        return pseudo_elements == nullptr ? empty : pseudo_elements->before;
    }

    const pseudo_element& after_pseudo() const noexcept
    {
        static const pseudo_element empty;
        return pseudo_elements == nullptr ? empty : pseudo_elements->after;
    }

    pseudo_element& mutable_before_pseudo()
    {
        ensure_unique_pseudo_elements();
        return pseudo_elements->before;
    }

    pseudo_element& mutable_after_pseudo()
    {
        ensure_unique_pseudo_elements();
        return pseudo_elements->after;
    }

    void clear_pseudo_elements() noexcept
    {
        pseudo_elements.reset();
    }

    void move_pseudo_elements_from(node_style& source) noexcept
    {
        pseudo_elements = std::move(source.pseudo_elements);
    }

    bool has_pseudo_elements() const noexcept
    {
        return pseudo_elements != nullptr;
    }

    css_length width{};
    css_length height{};
    css_length min_width{};
    css_length min_height{};
    css_length max_width{};
    css_length max_height{};
    css_length left{};
    css_length top{};
    css_length right{};
    css_length bottom{};
    css_length padding_left{};
    css_length padding_top{};
    css_length padding_right{};
    css_length padding_bottom{};
    css_length margin_left{};
    css_length margin_top{};
    css_length margin_right{};
    css_length margin_bottom{};
    css_length row_gap{};
    css_length column_gap{};
    css_length border_left_width{};
    css_length border_top_width{};
    css_length border_right_width{};
    css_length border_bottom_width{};
    css_length outline_width{};
    css_length border_top_left_radius{};
    css_length border_top_right_radius{};
    css_length border_bottom_right_radius{};
    css_length border_bottom_left_radius{};
    css_length transform_translate_x{};
    css_length transform_translate_y{};
    css_length transform_origin_x{50, length_unit::percent};
    css_length transform_origin_y{50, length_unit::percent};
    float transform_scale_x{1};
    float transform_scale_y{1};
    float transform_rotate_degrees{0};
    bool transform_specified{false};
    // Unlike transform_specified, an authored `transform: none` does not
    // establish a stacking context. Track that distinction without changing
    // computed-style and transition semantics.
    bool transform_stacking_context{false};
    // layout/paint containment establishes an atomic stacking context. Keep
    // this hot because paint-order and hit-testing consult it every frame.
    bool contain_stacking_context{false};
    // Retain whether transform-origin won the cascade independently from its
    // computed value. The initial 50% 50% value is otherwise indistinguishable
    // from an explicitly authored origin when detecting CSS compositions.
    bool transform_origin_specified{false};
    display_mode display{display_mode::block};
    position_mode position{position_mode::normal};
    float_mode floating{float_mode::none};
    flex_direction direction{flex_direction::row};
    align_mode align_items{align_mode::stretch};
    align_mode align_self{align_mode::stretch};
    justify_mode justify_content{justify_mode::start};
    overflow_mode overflow_x{overflow_mode::visible};
    overflow_mode overflow_y{overflow_mode::visible};
    float flex_grow{0};
    float flex_shrink{1};
    css_length flex_basis{};
    float opacity{1};
    uint32_t background_rgba{0};
    // currentColor is a used-value dependency, not a transparent color. Keep
    // it deferred so inherited color and declaration order resolve correctly.
    bool background_current_color{false};
    uint32_t foreground_rgba{0};
    uint32_t border_left_rgba{0};
    uint32_t border_top_rgba{0};
    uint32_t border_right_rgba{0};
    uint32_t border_bottom_rgba{0};
    // The initial value of every border-*-color longhand is currentColor.
    // Keep that dependency deferred so declaration order and inherited color
    // are resolved at paint time rather than collapsed to transparent.
    bool border_left_current_color{true};
    bool border_top_current_color{true};
    bool border_right_current_color{true};
    bool border_bottom_current_color{true};
    uint32_t outline_rgba{0};
    float box_shadow_offset_x{0};
    float box_shadow_offset_y{0};
    float box_shadow_blur_radius{0};
    float box_shadow_spread_radius{0};
    uint32_t box_shadow_rgba{0};
    bool box_shadow_present{false};
    // Negative means unspecified/inherited. Zero is a valid CSS value and is
    // used by visually hidden accessibility content.
    float font_size{-1};
    float line_height{-1};
    int32_t font_weight{0};
    float letter_spacing{0};
    float word_spacing{0};
    bool letter_spacing_specified{false};
    bool word_spacing_specified{false};
    int32_t z_index{0};
    // Keep the computed `auto` keyword distinct from its paint stacking level.
    // Collapsing both to integer zero made CSSOM unable to distinguish an
    // unspecified/root-inherited z-index from an authored `z-index: 0`.
    bool z_index_auto{true};
    struct table_style_data final {
        css_length border_spacing_horizontal{2, length_unit::pixels};
        css_length border_spacing_vertical{2, length_unit::pixels};
        bool border_collapsed{false};
    };

    struct textual_style_data final {
        struct scrollbar_style_data final {
            float width{6};
            float height{6};
            float overlay_inset{2};
            float thumb_border_width{0};
            float thumb_radius{3};
            float track_radius{3};
            uint32_t thumb_rgba{0xA0A0A0D0U};
            uint32_t track_rgba{0x7F7F7F40U};
        } scrollbar;
        std::string font_family;
        // Non-standard but widely deployed on macOS. Keep the authored token
        // so inherited text runs can select the browser-compatible glyph
        // edging mode without making it part of the hot style record.
        std::string font_smoothing;
        std::string text_align;
        std::string vertical_align;
        std::string text_transform;
        std::string white_space;
        std::string contain_value;
        // Authored cursor token. Cursor is inherited, so an empty value means
        // the host projection resolves the nearest declaration or `auto`.
        std::string cursor;
        // SVG paint values remain tokens so currentColor can resolve against
        // the live inherited foreground when the scene is serialized.
        std::string svg_fill;
        std::string svg_stroke;
        std::string list_style_position;
        std::string list_style_type;
        // Vertical corner radii are cold: circular radii use the four hot
        // horizontal values above. Elliptical declarations pay for this state
        // through the already-indirected extended style block.
        css_length border_top_left_radius_y{};
        css_length border_top_right_radius_y{};
        css_length border_bottom_right_radius_y{};
        css_length border_bottom_left_radius_y{};
        bool elliptical_border_radius{false};
        table_style_data table;
    };

    const table_style_data& table() const noexcept
    {
        static const table_style_data defaults;
        return textual_state == nullptr ? defaults : textual_state->table;
    }

    table_style_data& mutable_table()
    {
        return mutable_textual().table;
    }

    void clear_table()
    {
        if (textual_state != nullptr) mutable_textual().table = {};
    }

    const textual_style_data& textual() const noexcept
    {
        static const textual_style_data empty;
        return textual_state == nullptr ? empty : *textual_state;
    }

    textual_style_data& mutable_textual()
    {
        if (textual_state == nullptr) {
            textual_state = std::make_shared<textual_style_data>();
        } else if (textual_state.use_count() != 1) {
            textual_state =
                std::make_shared<textual_style_data>(*textual_state);
        }
        return *textual_state;
    }

    const textual_style_data::scrollbar_style_data& scrollbar() const noexcept
    {
        static const textual_style_data::scrollbar_style_data defaults;
        return textual_state == nullptr ? defaults : textual_state->scrollbar;
    }

    textual_style_data::scrollbar_style_data& mutable_scrollbar()
    {
        return mutable_textual().scrollbar;
    }

    void reset_scrollbar_style()
    {
        if (auto* data = mutable_textual_if_present(); data != nullptr) {
            data->scrollbar = {};
        }
    }

    textual_style_data* mutable_textual_if_present()
    {
        if (textual_state == nullptr) return nullptr;
        if (textual_state.use_count() != 1) {
            textual_state =
                std::make_shared<textual_style_data>(*textual_state);
        }
        return textual_state.get();
    }

    bool has_textual_data() const noexcept
    {
        // This is an allocation-accounting predicate, not a semantic
        // "contains a non-empty token" predicate. A payload that has been
        // allocated and subsequently cleared still consumes its object and
        // shared_ptr control-block storage and must remain visible to metrics.
        return textual_state != nullptr;
    }

    const textual_style_data* textual_data_identity() const noexcept
    {
        return textual_state.get();
    }

    css_length border_top_left_radius_y() const noexcept
    {
        return textual_state != nullptr && textual_state->elliptical_border_radius
            ? textual_state->border_top_left_radius_y
            : border_top_left_radius;
    }

    css_length border_top_right_radius_y() const noexcept
    {
        return textual_state != nullptr && textual_state->elliptical_border_radius
            ? textual_state->border_top_right_radius_y
            : border_top_right_radius;
    }

    css_length border_bottom_right_radius_y() const noexcept
    {
        return textual_state != nullptr && textual_state->elliptical_border_radius
            ? textual_state->border_bottom_right_radius_y
            : border_bottom_right_radius;
    }

    css_length border_bottom_left_radius_y() const noexcept
    {
        return textual_state != nullptr && textual_state->elliptical_border_radius
            ? textual_state->border_bottom_left_radius_y
            : border_bottom_left_radius;
    }

    void set_vertical_corner_radii(
        css_length top_left,
        css_length top_right,
        css_length bottom_right,
        css_length bottom_left)
    {
        const auto equal = [](css_length first, css_length second) {
            return first.value == second.value
                && first.unit == second.unit
                && first.pixel_offset == second.pixel_offset;
        };
        const auto elliptical = !equal(top_left, border_top_left_radius)
            || !equal(top_right, border_top_right_radius)
            || !equal(bottom_right, border_bottom_right_radius)
            || !equal(bottom_left, border_bottom_left_radius);
        if (!elliptical) {
            if (auto* data = mutable_textual_if_present(); data != nullptr) {
                data->elliptical_border_radius = false;
            }
            return;
        }
        auto& data = mutable_textual();
        data.border_top_left_radius_y = top_left;
        data.border_top_right_radius_y = top_right;
        data.border_bottom_right_radius_y = bottom_right;
        data.border_bottom_left_radius_y = bottom_left;
        data.elliptical_border_radius = true;
    }

    void clear_vertical_corner_radii()
    {
        if (auto* data = mutable_textual_if_present(); data != nullptr) {
            data->elliptical_border_radius = false;
        }
    }
    uint64_t inline_property_mask{0};
    uint64_t important_property_mask{0};
    bool clip : 1 {false};
    bool scroll_x_enabled : 1 {false};
    bool scroll_y_enabled : 1 {false};
    bool scrollbar_hidden : 1 {false};
    bool scrollbar_visibility_important : 1 {false};
    bool visibility_hidden : 1 {false};
    bool visibility_specified : 1 {false};
    bool pointer_events_none : 1 {false};
    bool pointer_events_specified : 1 {false};
    bool flex_wrap : 1 {false};
    bool flex_reverse : 1 {false};
    bool align_self_specified : 1 {false};
    bool border_box : 1 {false};
    // Margin parsing passes these four flags by reference, so unlike the other
    // hot boolean style state they remain addressable scalar values.
    bool margin_left_auto{false};
    bool margin_top_auto{false};
    bool margin_right_auto{false};
    bool margin_bottom_auto{false};
    bool table_layout_fixed : 1 {false};
private:
    void ensure_unique_pseudo_elements()
    {
        if (pseudo_elements == nullptr) {
            pseudo_elements = std::make_shared<pseudo_element_pair>();
        } else if (pseudo_elements.use_count() != 1) {
            pseudo_elements = std::make_shared<pseudo_element_pair>(*pseudo_elements);
        }
    }

    // Generated pseudo-elements are absent from most DOM nodes. Keeping two
    // complete pseudo boxes inline cost 496 bytes on every element. Copy-on-
    // write preserves cheap style cloning while paying for this cold state
    // only when ::before or ::after participates in the cascade.
    std::shared_ptr<pseudo_element_pair> pseudo_elements;
    // Transition/keyframe state is similarly cold. A complete animation_data
    // block is retained only for styles that author transition/animation
    // properties; style clones share it until a declaration mutates it.
    std::shared_ptr<animation_data> animation_state;
    // Custom properties are authored by a minority of nodes. Copy-on-write
    // keeps style snapshots cheap without embedding two empty hash tables in
    // every node.
    std::shared_ptr<custom_property_data> custom_property_state;
    // URL-backed artwork and its sizing/positioning tokens are absent from
    // most nodes. Keep the complete background-image block out of the hot
    // computed-style footprint.
    std::shared_ptr<background_image_data> background_image_state;
    // Grid track and placement data is likewise sparse in ordinary component
    // trees and large enough to keep out of every computed style.
    std::shared_ptr<grid_data> grid_state;
    // Keyword and authored-token strings are empty on most elements. Keeping
    // ten std::string objects inline cost 240 bytes on every DOM node.
    // Copy-on-write retains cheap style cloning and allocates the token block
    // only when one of these properties participates in the cascade.
    std::shared_ptr<textual_style_data> textual_state;
};

struct canvas_rect_command final {
    float x{0};
    float y{0};
    float width{0};
    float height{0};
    uint32_t rgba{0};
};

struct canvas_line_command final {
    float x1{0};
    float y1{0};
    float x2{0};
    float y2{0};
    float line_width{1};
    uint32_t rgba{0};
};

struct text_layout_fragment final {
    float x{0};
    float y{0};
    float width{0};
    float height{0};
    std::string text;
};

struct canvas_node_data final {
    std::vector<canvas_rect_command> rects;
    std::vector<canvas_line_command> lines;
    uint64_t generation{1};
    std::vector<webscene_canvas_command> commands;
    std::vector<std::string> strings;
    std::unordered_map<std::string, uint32_t> string_indices;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    uint64_t fill_rect_calls{0};
    uint64_t probable_volume_fill_rect_calls{0};
    std::unordered_map<uint64_t, uint64_t> probable_volume_by_generation;
    uint64_t fill_calls{0};
    uint64_t path_argument_fill_calls{0};
    uint64_t draw_image_calls{0};
    uint64_t canvas_draw_image_calls{0};
    uint64_t self_draw_image_calls{0};
    uint64_t fill_text_calls{0};
    uint64_t stroke_text_calls{0};
    uint64_t clear_rect_calls{0};
    uint64_t full_clear_calls{0};
    uint64_t full_clear_reset_calls{0};
    uint64_t full_clear_current_clip_calls{0};
    uint64_t full_clear_saved_clip_calls{0};
    uint64_t clear_bounds_rejected_calls{0};
    uint64_t max_clear_stack_depth{0};
    std::unordered_map<uint32_t, uint64_t> fill_rect_color_calls;
#endif
};

// Element attribute sets are generally tiny. A node-based unordered_map costs
// three pointers plus allocator/hash nodes even when empty, and scatters the
// common 1--4 attributes across allocations. A compact insertion-ordered
// vector reduces every dom_node's inline size and keeps lookup data contiguous.
// Attribute names are normalized by the HTML binding before reaching this
// container; XML case preservation therefore remains unchanged.
class attribute_collection final {
public:
    using value_type = std::pair<std::string, std::string>;
    using storage_type = std::vector<value_type>;
    using iterator = storage_type::iterator;
    using const_iterator = storage_type::const_iterator;

    iterator begin() noexcept { return values_.begin(); }
    iterator end() noexcept { return values_.end(); }
    const_iterator begin() const noexcept { return values_.begin(); }
    const_iterator end() const noexcept { return values_.end(); }

    bool empty() const noexcept { return values_.empty(); }
    size_t size() const noexcept { return values_.size(); }
    size_t storage_bytes() const noexcept
    {
        auto result = values_.capacity() * sizeof(value_type);
        for (const auto& [name, value] : values_) {
            result += name.capacity() + value.capacity() + 2U;
        }
        return result;
    }

    iterator find(std::string_view name) noexcept
    {
        return std::find_if(values_.begin(), values_.end(), [name](const auto& entry) {
            return entry.first == name;
        });
    }

    const_iterator find(std::string_view name) const noexcept
    {
        return std::find_if(values_.begin(), values_.end(), [name](const auto& entry) {
            return entry.first == name;
        });
    }

    bool contains(std::string_view name) const noexcept
    {
        return find(name) != end();
    }

    std::string& operator[](std::string_view name)
    {
        if (auto known = find(name); known != end()) return known->second;
        values_.emplace_back(std::string(name), std::string{});
        return values_.back().second;
    }

    std::string& at(std::string_view name)
    {
        if (auto known = find(name); known != end()) return known->second;
        throw std::out_of_range("attribute not found");
    }

    const std::string& at(std::string_view name) const
    {
        if (auto known = find(name); known != end()) return known->second;
        throw std::out_of_range("attribute not found");
    }

    size_t erase(std::string_view name)
    {
        const auto known = find(name);
        if (known == end()) return 0;
        values_.erase(known);
        return 1;
    }

    bool operator==(const attribute_collection& other) const noexcept
    {
        if (values_.size() != other.values_.size()) return false;
        return std::all_of(values_.begin(), values_.end(), [&other](const auto& entry) {
            const auto match = other.find(entry.first);
            return match != other.end() && match->second == entry.second;
        });
    }

private:
    storage_type values_;
};

enum class text_selection_direction : uint8_t {
    none,
    forward,
    backward
};

enum class dom_node_kind : uint8_t {
    element,
    text,
    comment,
    document_fragment,
    document_type,
    processing_instruction,
    internal
};

enum class script_execution_state : uint8_t {
    ready,
    already_started
};

struct dom_node final {
    static constexpr std::string_view html_namespace_uri =
        "http://www.w3.org/1999/xhtml";

    struct namespace_data final {
        std::string uri;
        std::string prefix;
    };

    struct authored_style_data final {
        std::unordered_map<std::string, std::string> declarations;
        std::unordered_set<std::string> important_declarations;
    };

    struct table_layout_data final {
        std::vector<float> column_widths;
        size_t column_index{0};
        size_t column_span{1};
        size_t row_span{1};
        float row_height{0};
        float cell_height{0};
        float column_spacing{0};
        float row_spacing{0};
    };

    struct grid_layout_data final {
        std::vector<float> column_widths;
        float column_gap{0};
    };

    struct form_control_data final {
        std::string value;
        size_t selection_start{0};
        size_t selection_end{0};
        text_selection_direction selection_direction{text_selection_direction::none};
        bool selection_explicitly_set{false};
        bool selectedness_initialized{false};
        bool selectedness{false};
        bool selection_explicitly_empty{false};
        bool checkedness_initialized{false};
        bool checkedness{false};
        bool value_initialized{false};
        bool dirty_value{false};
        bool input_focused{false};
        bool caret_visible{false};
    };

    struct replaced_image_data final {
        std::string source;
        std::string resolved_source;
        std::string markup;
        std::string view_box;
        float natural_width{0};
        float natural_height{0};
        bool complete{false};
    };

    // Painted transition/keyframe state is needed only by nodes which have
    // authored animation data. Keeping it inline made every static DOM node
    // pay for three transition machines and two keyframe signatures.
    struct animation_runtime_data final {
        css_length painted_transform_translate_x{};
        css_length painted_transform_translate_y{};
        css_length transform_animation_from_translate_x{};
        css_length transform_animation_from_translate_y{};
        css_length transform_animation_target_translate_x{};
        css_length transform_animation_target_translate_y{};
        float painted_transform_scale_x{1};
        float painted_transform_scale_y{1};
        float transform_animation_from_scale_x{1};
        float transform_animation_from_scale_y{1};
        float transform_animation_target_scale_x{1};
        float transform_animation_target_scale_y{1};
        float painted_transform_rotate_degrees{0};
        float transform_animation_from_degrees{0};
        float transform_animation_target_degrees{0};
        float transform_animation_duration_ms{0};
        float transform_animation_delay_ms{0};
        float transform_animation_x1{0.25F};
        float transform_animation_y1{0.1F};
        float transform_animation_x2{0.25F};
        float transform_animation_y2{1.0F};
        double transform_animation_started_ms{0};
        bool transform_animation_initialized{false};
        bool transform_animation_active{false};
        bool transform_animation_start_event_sent{false};
        css_length painted_left{};
        css_length left_animation_from{};
        css_length left_animation_target{};
        float left_animation_duration_ms{0};
        float left_animation_delay_ms{0};
        float left_animation_x1{0.25F};
        float left_animation_y1{0.1F};
        float left_animation_x2{0.25F};
        float left_animation_y2{1.0F};
        double left_animation_started_ms{0};
        bool left_animation_initialized{false};
        bool left_animation_active{false};
        bool left_animation_start_event_sent{false};
        css_length painted_top{};
        css_length top_animation_from{};
        css_length top_animation_target{};
        float top_animation_duration_ms{0};
        float top_animation_delay_ms{0};
        float top_animation_x1{0.25F};
        float top_animation_y1{0.1F};
        float top_animation_x2{0.25F};
        float top_animation_y2{1.0F};
        double top_animation_started_ms{0};
        bool top_animation_initialized{false};
        bool top_animation_active{false};
        bool top_animation_start_event_sent{false};
        float painted_opacity{1};
        float opacity_animation_from{1};
        float opacity_animation_target{1};
        float opacity_animation_duration_ms{0};
        float opacity_animation_delay_ms{0};
        float opacity_animation_x1{0.25F};
        float opacity_animation_y1{0.1F};
        float opacity_animation_x2{0.25F};
        float opacity_animation_y2{1.0F};
        double opacity_animation_started_ms{0};
        bool opacity_animation_initialized{false};
        bool opacity_animation_active{false};
        bool opacity_animation_start_event_sent{false};
        std::string opacity_keyframe_animation_signature;
        double opacity_keyframe_animation_started_ms{0};
        bool opacity_keyframe_animation_active{false};
        std::string rotation_keyframe_animation_signature;
        double rotation_keyframe_animation_started_ms{0};
        bool rotation_keyframe_animation_active{false};
        uint32_t painted_foreground_rgba{0};
        uint32_t color_animation_from_rgba{0};
        uint32_t color_animation_target_rgba{0};
        float color_animation_duration_ms{0};
        float color_animation_delay_ms{0};
        float color_animation_x1{0.25F};
        float color_animation_y1{0.1F};
        float color_animation_x2{0.25F};
        float color_animation_y2{1.0F};
        double color_animation_started_ms{0};
        bool color_animation_initialized{false};
        bool color_animation_active{false};
        bool color_animation_start_event_sent{false};
    };

    uint32_t id{0};
    dom_node_kind kind{dom_node_kind::element};
    std::string tag;
    std::string id_attribute;
    std::string class_name;
    std::string text_content;
    // XML documents preserve qualified/tag and attribute name case. HTML nodes
    // continue to apply the ASCII case-insensitive name rules at the binding.
    bool xml_mode{false};
    attribute_collection attributes;
    std::string_view namespace_uri() const noexcept
    {
        if (namespace_state != nullptr) return namespace_state->uri;
        return kind == dom_node_kind::element ? html_namespace_uri : std::string_view{};
    }

    std::string_view namespace_prefix() const noexcept
    {
        return namespace_state == nullptr
            ? std::string_view{}
            : std::string_view(namespace_state->prefix);
    }

    void set_namespace(std::string_view uri, std::string_view prefix = {})
    {
        if (kind == dom_node_kind::element
            && uri == html_namespace_uri
            && prefix.empty()) {
            namespace_state.reset();
            return;
        }
        if (namespace_state == nullptr) {
            namespace_state = std::make_unique<namespace_data>();
        }
        namespace_state->uri.assign(uri);
        namespace_state->prefix.assign(prefix);
    }

    // HTML is represented by the null state, so ordinary elements pay one
    // pointer rather than two inline std::string objects. Foreign/XML
    // namespaces remain lossless and allocate only on the uncommon path.
    std::unique_ptr<namespace_data> namespace_state;
    const authored_style_data& authored_style() const noexcept
    {
        static const authored_style_data empty;
        return authored_style_state == nullptr ? empty : *authored_style_state;
    }

    authored_style_data& mutable_authored_style()
    {
        if (authored_style_state == nullptr) {
            authored_style_state = std::make_unique<authored_style_data>();
        }
        return *authored_style_state;
    }

    bool has_authored_style() const noexcept
    {
        return authored_style_state != nullptr
            && (!authored_style_state->declarations.empty()
                || !authored_style_state->important_declarations.empty());
    }

    void clear_authored_style() noexcept
    {
        authored_style_state.reset();
    }

    std::unique_ptr<authored_style_data> authored_style_state;
    dom_node* parent{nullptr};
    // HTMLTemplateElement content is a separate document fragment. It is
    // allocated by native_document and therefore has the same stable address
    // and lifetime as ordinary nodes.
    dom_node* template_contents{nullptr};
    std::vector<dom_node*> children;
    node_style style{};
    layout_rect layout{};
    layout_rect list_marker_layout{};
    // Highest stacking level represented by this paint subtree. A node with
    // its own z-index remains the root of that stacking context; transparent
    // wrappers inherit the highest positive descendant for scene ordering.
    int32_t paint_z_index{0};
    bool paints_after_retained_canvas{false};
    bool contains_retained_canvas{false};
    std::vector<text_layout_fragment> text_layout_fragments;
    // Resolved table geometry is projected onto the semantic table boxes so
    // row groups and rows can arrange against one shared column grid. Ordinary
    // nodes never need this vector or the five placement values.
    const table_layout_data& table_layout() const noexcept
    {
        static const table_layout_data empty;
        return table_layout_state == nullptr ? empty : *table_layout_state;
    }

    table_layout_data& mutable_table_layout()
    {
        if (table_layout_state == nullptr) {
            table_layout_state = std::make_unique<table_layout_data>();
        }
        return *table_layout_state;
    }

    bool has_table_layout() const noexcept
    {
        return table_layout_state != nullptr;
    }

    std::unique_ptr<table_layout_data> table_layout_state;
    const grid_layout_data& grid_layout() const noexcept
    {
        static const grid_layout_data empty;
        return grid_layout_state == nullptr ? empty : *grid_layout_state;
    }

    grid_layout_data& mutable_grid_layout()
    {
        if (grid_layout_state == nullptr) {
            grid_layout_state = std::make_unique<grid_layout_data>();
        }
        return *grid_layout_state;
    }

    bool has_grid_layout() const noexcept
    {
        return grid_layout_state != nullptr;
    }

    void clear_grid_layout() noexcept
    {
        grid_layout_state.reset();
    }

    std::unique_ptr<grid_layout_data> grid_layout_state;
    // Live value/selection/checked/focus state is absent from ordinary DOM
    // nodes. Option selectedness and checkedness remain separate from their
    // authored attributes inside this cold record.
    const form_control_data& form_control() const noexcept
    {
        static const form_control_data empty;
        return form_control_state == nullptr ? empty : *form_control_state;
    }

    form_control_data& mutable_form_control()
    {
        if (form_control_state == nullptr) {
            form_control_state = std::make_unique<form_control_data>();
        }
        return *form_control_state;
    }

    bool has_form_control() const noexcept
    {
        return form_control_state != nullptr;
    }

    std::unique_ptr<form_control_data> form_control_state;
    const replaced_image_data& replaced_image() const noexcept
    {
        static const replaced_image_data empty;
        return replaced_image_state == nullptr
            ? empty
            : *replaced_image_state;
    }

    replaced_image_data& mutable_replaced_image()
    {
        if (replaced_image_state == nullptr) {
            replaced_image_state = std::make_unique<replaced_image_data>();
        }
        return *replaced_image_state;
    }

    void clear_replaced_image() noexcept
    {
        replaced_image_state.reset();
    }

    std::unique_ptr<replaced_image_data> replaced_image_state;
    const canvas_node_data& canvas() const noexcept
    {
        static const canvas_node_data empty;
        return canvas_data == nullptr ? empty : *canvas_data;
    }

    canvas_node_data& mutable_canvas()
    {
        if (canvas_data == nullptr) {
            canvas_data = std::make_unique<canvas_node_data>();
        }
        return *canvas_data;
    }

    bool has_canvas_data() const noexcept
    {
        return canvas_data != nullptr;
    }

    std::unique_ptr<canvas_node_data> canvas_data;
    const animation_runtime_data* animation_runtime() const noexcept
    {
        return animation_runtime_state.get();
    }

    animation_runtime_data* animation_runtime() noexcept
    {
        return animation_runtime_state.get();
    }

    animation_runtime_data& mutable_animation_runtime()
    {
        if (animation_runtime_state == nullptr) {
            animation_runtime_state = std::make_unique<animation_runtime_data>();
        }
        return *animation_runtime_state;
    }

    bool has_animation_runtime() const noexcept
    {
        return animation_runtime_state != nullptr;
    }

    css_length painted_transform_translate_x_value() const noexcept
    {
        return animation_runtime_state != nullptr
                && animation_runtime_state->transform_animation_initialized
            ? animation_runtime_state->painted_transform_translate_x
            : style.transform_translate_x;
    }

    css_length painted_transform_translate_y_value() const noexcept
    {
        return animation_runtime_state != nullptr
                && animation_runtime_state->transform_animation_initialized
            ? animation_runtime_state->painted_transform_translate_y
            : style.transform_translate_y;
    }

    float painted_transform_scale_x_value() const noexcept
    {
        return animation_runtime_state != nullptr
                && animation_runtime_state->transform_animation_initialized
            ? animation_runtime_state->painted_transform_scale_x
            : style.transform_scale_x;
    }

    float painted_transform_scale_y_value() const noexcept
    {
        return animation_runtime_state != nullptr
                && animation_runtime_state->transform_animation_initialized
            ? animation_runtime_state->painted_transform_scale_y
            : style.transform_scale_y;
    }

    float painted_transform_rotation_value() const noexcept
    {
        return animation_runtime_state != nullptr
                && animation_runtime_state->transform_animation_initialized
            ? animation_runtime_state->painted_transform_rotate_degrees
            : style.transform_rotate_degrees;
    }

    float painted_opacity_value() const noexcept
    {
        return animation_runtime_state != nullptr
                && animation_runtime_state->opacity_animation_initialized
            ? animation_runtime_state->painted_opacity
            : style.opacity;
    }

    uint32_t painted_foreground_value() const noexcept
    {
        return animation_runtime_state != nullptr
                && animation_runtime_state->color_animation_initialized
            ? animation_runtime_state->painted_foreground_rgba
            : style.foreground_rgba;
    }

    bool rotation_keyframe_animation_active_value() const noexcept
    {
        return animation_runtime_state != nullptr
            && animation_runtime_state->rotation_keyframe_animation_active;
    }

    bool transform_animation_active_value() const noexcept
    {
        return animation_runtime_state != nullptr
            && animation_runtime_state->transform_animation_active;
    }

    bool color_animation_active_value() const noexcept
    {
        return animation_runtime_state != nullptr
            && animation_runtime_state->color_animation_active;
    }

    std::unique_ptr<animation_runtime_data> animation_runtime_state;
    float scroll_left{0};
    float scroll_top{0};
    float scroll_content_width{0};
    float scroll_content_height{0};
    float scroll_viewport_width{0};
    float scroll_viewport_height{0};
    // Records that the current used block size came from a definite
    // containing-block/flex constraint rather than max-content expansion.
    // Descendant percentage and overflow sizing must follow the used size
    // even when this node's authored height remains `auto`.
    bool used_height_is_definite{false};
    // Script execution history is intrinsic DOM state. Keeping the byte in
    // existing tail padding preserves the fixed dom_node footprint while
    // preventing a connected script from executing again after a reparent.
    script_execution_state script_state{script_execution_state::ready};
    bool visible{true};
    // Temporary layout nodes created for ::before/::after are principal
    // generated boxes, not anonymous whitespace text.  Keep that distinction
    // even when content is empty so authored dimensions can participate in
    // flex/grid sizing.
    bool generated_pseudo_box{false};
};

display_mode blockified_display(const dom_node& node) noexcept;

class native_document final {
public:
    // Shadow DOM state is kept entirely outside dom_node. A light-DOM-only
    // document therefore preserves both sizeof(dom_node) and every per-node
    // allocation. The side table is instantiated only by attachShadow().
    struct shadow_dom_data final {
        dom_node* root{nullptr};
        dom_node* host{nullptr};
        dom_node* assigned_slot{nullptr};
        std::vector<dom_node*> assigned_nodes;
        bool open{true};
        bool delegates_focus{false};
    };

    struct allocation_metrics final {
        uint64_t node_count{0};
        uint64_t node_object_size_bytes{0};
        uint64_t node_object_bytes{0};
        uint64_t node_pool_reserved_bytes{0};
        uint64_t node_pool_peak_bytes{0};
        uint64_t layout_scratch_reserved_bytes{0};
        uint64_t layout_scratch_peak_bytes{0};
        uint64_t element_node_count{0};
        uint64_t text_node_count{0};
        uint64_t comment_node_count{0};
        uint64_t document_type_node_count{0};
        uint64_t other_node_count{0};
        uint64_t table_layout_node_count{0};
        uint64_t table_layout_storage_bytes{0};
        uint64_t form_control_node_count{0};
        uint64_t form_control_storage_bytes{0};
        uint64_t attribute_node_count{0};
        uint64_t attribute_entry_count{0};
        uint64_t attribute_storage_bytes{0};
        uint64_t pseudo_element_pair_count{0};
        uint64_t pseudo_element_storage_bytes{0};
        uint64_t animation_data_count{0};
        uint64_t animation_storage_bytes{0};
        uint64_t animation_runtime_count{0};
        uint64_t animation_runtime_storage_bytes{0};
        uint64_t custom_property_node_count{0};
        uint64_t custom_property_entry_count{0};
        uint64_t custom_property_storage_bytes{0};
        uint64_t background_image_data_count{0};
        uint64_t background_image_storage_bytes{0};
        uint64_t grid_data_count{0};
        uint64_t grid_storage_bytes{0};
        uint64_t textual_style_data_count{0};
        uint64_t textual_style_storage_bytes{0};
        uint64_t authored_style_node_count{0};
        uint64_t authored_style_entry_count{0};
        uint64_t authored_style_storage_bytes{0};
        uint64_t shadow_dom_role_count{0};
        uint64_t shadow_dom_storage_bytes{0};
        uint64_t canvas_node_count{0};
        uint64_t canvas_storage_bytes{0};
        uint64_t text_measurement_cache_entry_count{0};
        uint64_t text_measurement_cache_storage_bytes{0};
    };

    struct transition_event_record final {
        uint32_t node_id{0};
        std::string type;
        std::string property_name;
        float elapsed_time_seconds{0};
    };

    explicit native_document(
        webscene_text_measure_callback text_measure_callback = nullptr,
        void* text_measure_user_data = nullptr);

    dom_node& body() noexcept;
    const dom_node& body() const noexcept;
    dom_node& create_element(std::string tag);
    dom_node& create_node(dom_node_kind kind, std::string name = {});
    bool append_child(dom_node& parent, dom_node& child);
    bool parser_append_child(dom_node& parent, dom_node& child) noexcept;
    bool parser_insert_before(dom_node& sibling, dom_node& child) noexcept;
    bool parser_remove_from_parent(dom_node& child) noexcept;
    bool parser_reparent_children(dom_node& source, dom_node& destination) noexcept;
    dom_node& parser_template_contents(dom_node& element);
    bool has_shadow_dom() const noexcept;
    const shadow_dom_data* shadow_dom(const dom_node& node) const noexcept;
    shadow_dom_data& mutable_shadow_dom(dom_node& node);
    bool is_shadow_root(const dom_node& node) const noexcept;
    dom_node* containing_shadow_root(dom_node& node) const noexcept;
    const dom_node* containing_shadow_root(const dom_node& node) const noexcept;
    const std::vector<dom_node*>& composed_children(const dom_node& node) const noexcept;
    std::vector<dom_node*>& composed_children(dom_node& node) noexcept;
    void refresh_shadow_distributions();
    dom_node* composed_parent(dom_node& node) const noexcept;
    const dom_node* composed_parent(const dom_node& node) const noexcept;
    dom_node* event_parent(dom_node& node) const noexcept;
    const dom_node* event_parent(const dom_node& node) const noexcept;
    dom_node* dom_parent(dom_node& node) const noexcept;
    const dom_node* dom_parent(const dom_node& node) const noexcept;
    void remove_all_children(dom_node& parent);
    size_t erase_detached_subtree(dom_node& root);
    size_t erase_detached_subtrees(const std::vector<dom_node*>& roots);
    dom_node* find_by_native_id(uint32_t id) noexcept;
    dom_node* find_by_id(const std::string& id) noexcept;
    std::vector<dom_node*> query_selector_all(dom_node& root, const std::string& selector);
    dom_node* hit_test(dom_node& root, float x, float y);
    void clear();
    void layout(float viewport_width, float viewport_height);
    void build_scene(
        std::vector<webscene_scene_command>& commands,
        std::vector<webscene_scene_string>& strings,
        std::vector<char>& string_bytes) const;
    void build_canvas_layouts(std::vector<webscene_canvas_layout>& layouts) const;
    void build_canvas_display_lists(
        std::vector<webscene_canvas_layer>& layers,
        std::vector<webscene_canvas_command>& canvas_commands,
        std::vector<webscene_scene_string>& strings,
        std::vector<char>& string_bytes) const;
    void retain_canvas_for_export(dom_node& node) noexcept;
    bool release_canvas_export(uint32_t node_id) noexcept;

    uint64_t layout_passes() const noexcept;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    uint64_t intrinsic_size_cache_hits() const noexcept;
    uint64_t intrinsic_size_cache_misses() const noexcept;
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_SIZE_DIRECT_CACHE_BENCHMARK)
    uint64_t intrinsic_size_direct_cache_hits() const noexcept;
    uint64_t intrinsic_size_hash_lookups() const noexcept;
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_SIZE_BRANCH_BENCHMARK)
    std::array<uint64_t, 17U> intrinsic_size_branch_counts() const noexcept;
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_VIEW_BOX_BENCHMARK)
    std::array<uint64_t, 4U> intrinsic_view_box_parse_counts() const noexcept;
#endif
    size_t node_count() const noexcept;
    allocation_metrics read_allocation_metrics() const noexcept;
    size_t count_tag(const std::string& tag) const noexcept;
    size_t sum_attribute_bytes(const std::string& tag, const std::string& attribute) const noexcept;
    std::string first_attribute(const std::string& tag, const std::string& attribute) const;
    std::string describe_busiest_canvas() const;
    layout_rect busiest_canvas_layout() const noexcept;
    uint64_t scene_generation() const noexcept;
    void mark_scene_changed() noexcept;
    bool dirty() const noexcept;
    void mark_dirty() noexcept;
    void mark_out_of_flow_geometry_dirty(dom_node& node) noexcept;
    bool can_reuse_client_geometry(const dom_node& node) const noexcept;
    void signal_animation_frame(double timestamp_ms) noexcept;
    void update_style_animations(dom_node& node);
    bool advance_animations() noexcept;
    bool has_active_animations() const noexcept;
    std::vector<transition_event_record> take_transition_events();
    float measure_inline_content_width(const dom_node& node) const;
    size_t text_caret_offset_at_x(const dom_node& node, float x) const;
    webscene_text_metrics measure_text(
        std::string_view value,
        std::string_view family,
        float font_size,
        int32_t font_weight,
        float letter_spacing = 0.0F,
        float word_spacing = 0.0F) const;

    static css_length parse_length(const std::string& value);
    float resolve_used_length(
        const dom_node& context,
        css_length value,
        float available,
        float fallback) const
    {
        return resolve_length(context, value, available, fallback);
    }
    static void parse_transform_translate(
        const std::string& value,
        css_length& translate_x,
        css_length& translate_y,
        float& scale_x,
        float& scale_y,
        float& rotate_degrees);
    static void parse_transform_origin(
        const std::string& value,
        css_length& origin_x,
        css_length& origin_y);
    static uint32_t parse_color(const std::string& value);

private:
    struct shadow_dom_storage final {
        std::unordered_map<uint32_t, shadow_dom_data> node_data;
    };

    class tracking_memory_resource final : public std::pmr::memory_resource {
    public:
        size_t reserved_bytes() const noexcept { return reserved_bytes_; }
        size_t peak_bytes() const noexcept { return peak_bytes_; }

    private:
        void* do_allocate(size_t bytes, size_t alignment) override
        {
            auto* allocation =
                std::pmr::new_delete_resource()->allocate(bytes, alignment);
            reserved_bytes_ += bytes;
            peak_bytes_ = std::max(peak_bytes_, reserved_bytes_);
            return allocation;
        }

        void do_deallocate(
            void* allocation,
            size_t bytes,
            size_t alignment) override
        {
            std::pmr::new_delete_resource()->deallocate(
                allocation,
                bytes,
                alignment);
            reserved_bytes_ -= std::min(reserved_bytes_, bytes);
        }

        bool do_is_equal(
            const std::pmr::memory_resource& other) const noexcept override
        {
            return this == &other;
        }

        size_t reserved_bytes_{0};
        size_t peak_bytes_{0};
    };

    struct layout_scratch_storage final {
        tracking_memory_resource upstream;
        std::pmr::unsynchronized_pool_resource pool{&upstream};
    };

    // dom_node has one fixed allocation size and stable-address lifetime.
    // A general-purpose pmr pool still performs a pool/chunk search for every
    // deallocation on libc++, which made detached-tree sweeping proportional
    // to a very expensive allocator operation per node. Keep node pages and a
    // direct free list instead: reclamation becomes O(1), freed slots are
    // reused, and complete document teardown can still return every page.
    class node_memory_resource final : public std::pmr::memory_resource {
    public:
        explicit node_memory_resource(std::pmr::memory_resource* upstream)
            : upstream_(upstream)
        {
        }

        ~node_memory_resource() override { release(); }

        void release() noexcept
        {
            for (auto* allocation : chunks_) {
                upstream_->deallocate(
                    allocation,
                    chunk_bytes,
                    alignof(dom_node));
            }
            chunks_.clear();
            free_blocks_ = nullptr;
        }

    private:
        struct free_block final {
            free_block* next{nullptr};
        };

        static constexpr size_t blocks_per_chunk = 64U;
        static constexpr size_t block_bytes = sizeof(dom_node);
        static constexpr size_t chunk_bytes = block_bytes * blocks_per_chunk;

        void replenish()
        {
            auto* chunk = static_cast<std::byte*>(
                upstream_->allocate(chunk_bytes, alignof(dom_node)));
            try {
                chunks_.push_back(chunk);
            } catch (...) {
                upstream_->deallocate(chunk, chunk_bytes, alignof(dom_node));
                throw;
            }
            for (size_t index = 0; index < blocks_per_chunk; ++index) {
                auto* block = reinterpret_cast<free_block*>(
                    chunk + index * block_bytes);
                block->next = free_blocks_;
                free_blocks_ = block;
            }
        }

        void* do_allocate(size_t bytes, size_t alignment) override
        {
            if (bytes != block_bytes || alignment > alignof(dom_node)) {
                return upstream_->allocate(bytes, alignment);
            }
            if (free_blocks_ == nullptr) replenish();
            auto* result = free_blocks_;
            free_blocks_ = free_blocks_->next;
            return result;
        }

        void do_deallocate(
            void* allocation,
            size_t bytes,
            size_t alignment) override
        {
            if (allocation == nullptr) return;
            if (bytes != block_bytes || alignment > alignof(dom_node)) {
                upstream_->deallocate(allocation, bytes, alignment);
                return;
            }
            auto* block = static_cast<free_block*>(allocation);
            block->next = free_blocks_;
            free_blocks_ = block;
        }

        bool do_is_equal(
            const std::pmr::memory_resource& other) const noexcept override
        {
            return this == &other;
        }

        std::pmr::memory_resource* upstream_{nullptr};
        std::vector<void*> chunks_;
        free_block* free_blocks_{nullptr};
    };

    struct node_deleter final {
        std::pmr::memory_resource* resource{nullptr};

        void operator()(dom_node* node) const noexcept
        {
            if (node == nullptr) return;
            std::destroy_at(node);
            resource->deallocate(node, sizeof(dom_node), alignof(dom_node));
        }
    };

    using node_pointer = std::unique_ptr<dom_node, node_deleter>;

    struct text_measurement_key final {
        std::string text;
        std::string family;
        float font_size{0};
        int32_t font_weight{0};
        float letter_spacing{0};
        float word_spacing{0};

        bool operator==(const text_measurement_key&) const = default;
    };

    struct text_measurement_key_view final {
        std::string_view text;
        std::string_view family;
        float font_size{0};
        int32_t font_weight{0};
        float letter_spacing{0};
        float word_spacing{0};
    };

    struct text_measurement_key_hash final {
        using is_transparent = void;

        template <typename Key>
        size_t operator()(const Key& value) const noexcept
        {
            auto result = std::hash<std::string_view>{}(value.text);
            const auto mix = [&result](size_t next) {
                result ^= next + 0x9e3779b9U + (result << 6U) + (result >> 2U);
            };
            mix(std::hash<std::string_view>{}(value.family));
            mix(std::hash<float>{}(value.font_size));
            mix(std::hash<int32_t>{}(value.font_weight));
            mix(std::hash<float>{}(value.letter_spacing));
            mix(std::hash<float>{}(value.word_spacing));
            return result;
        }
    };

    struct text_measurement_key_equal final {
        using is_transparent = void;

        template <typename Left, typename Right>
        bool operator()(const Left& left, const Right& right) const noexcept
        {
            return std::string_view(left.text) == std::string_view(right.text)
                && std::string_view(left.family) == std::string_view(right.family)
                && left.font_size == right.font_size
                && left.font_weight == right.font_weight
                && left.letter_spacing == right.letter_spacing
                && left.word_spacing == right.word_spacing;
        }
    };

    struct intrinsic_size_key final {
        const dom_node* node{nullptr};
        bool horizontal{false};

        bool operator==(const intrinsic_size_key&) const = default;
    };

    struct intrinsic_size_key_hash final {
        size_t operator()(const intrinsic_size_key& value) const noexcept
        {
            auto result = std::hash<const dom_node*>{}(value.node);
            const auto mix = [&result](size_t next) {
                result ^= next + 0x9e3779b9U + (result << 6U) + (result >> 2U);
            };
            mix(std::hash<bool>{}(value.horizontal));
            return result;
        }
    };

    struct intrinsic_size_cache_entry final {
        uint64_t generation{0};
        float available{0};
        float size{0};
    };

#if !defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_SIZE_HASH_CACHE_CONTROL)
    struct intrinsic_size_direct_node_cache final {
        uint64_t generation{0};
        std::array<float, 2U> available{};
        std::array<float, 2U> size{};
    };
#endif

    webscene_text_metrics measure_text(
        std::string_view value,
        const dom_node& node) const;
    float measure_text_width(
        std::string_view value,
        const dom_node& node) const
    {
        return measure_text(value, node).advance_width;
    }
    std::vector<std::string> wrap_text_lines(
        const std::string& value,
        float available_width,
        const dom_node& node,
        bool allow_wrap) const;
    float resolve_length(
        const dom_node& context,
        css_length value,
        float available,
        float fallback) const;
    float resolve_length(css_length value, float available, float fallback) const;
    static bool is_specified(css_length value);
    float intrinsic_size(
        const dom_node& node,
        bool horizontal,
        float available);
    float min_content_inline_size(
        const dom_node& node,
        float available);
    float compute_intrinsic_size(
        const dom_node& node,
        bool horizontal,
        float available);
    void layout_children(dom_node& parent);
    void layout_child(
        dom_node& child,
        const layout_rect& available,
        layout_rect assigned,
        bool assigned_height_is_definite = false);
    void append_scene(
        const dom_node& node,
        std::vector<webscene_scene_command>& commands,
        std::vector<webscene_scene_string>& strings,
        std::vector<char>& string_bytes,
        bool inherited_visibility_hidden,
        bool defer_fixed_descendants) const;
    static bool matches_selector(const dom_node& node, const std::string& selector);
    static void collect_matches(
        dom_node& node,
        const std::string& selector,
        std::vector<dom_node*>& result);
    dom_node* hit_test_node(
        dom_node& node,
        float x,
        float y,
        bool inherited_visibility_hidden,
        bool inherited_pointer_events_none,
        bool ignore_own_clip = false) noexcept;
    bool is_connected(const dom_node& node) const noexcept;
    bool participates_in_animation_frame(
        const dom_node& node) const noexcept;
    bool intersects_visible_paint_area(
        const dom_node& node) const noexcept;

    // DOM nodes require stable addresses but are frequently created and
    // detached in component workloads. Allocate fixed-size nodes in bounded
    // chunks so freed slots are reused without returning to the process
    // allocator for every mutation. nodes_ is destroyed before node_pool_.
    tracking_memory_resource node_pool_upstream_;
    node_memory_resource node_pool_{&node_pool_upstream_};
    std::vector<node_pointer> nodes_;
    std::unique_ptr<shadow_dom_storage> shadow_dom_storage_;
    // Native IDs are monotonically assigned and are used on hot event and
    // detached-wrapper paths. Keep a sparse direct index rather than scanning
    // every live allocation for each lookup. Detached tail entries are
    // trimmed when possible so short-lived text-node churn does not retain an
    // ever-growing pointer table.
    std::vector<dom_node*> native_id_index_;
#if !defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_SIZE_HASH_CACHE_CONTROL)
    // Mirror the native-ID index so intrinsic lookup remains direct without
    // making every DOM node pay a cross-library object-footprint tax. The
    // shared generation keeps each two-axis entry to 24 bytes instead of the
    // previous 32-byte pair of per-axis generations.
    std::unique_ptr<std::vector<intrinsic_size_direct_node_cache>>
        intrinsic_size_direct_cache_;
#endif
    dom_node* body_{nullptr};
    uint32_t retained_export_canvas_id_{0};
    float viewport_width_{1};
    float viewport_height_{1};
    uint32_t next_node_id_{1};
    uint64_t layout_passes_{0};
    double animation_frame_timestamp_ms_{0};
    double last_animation_advance_timestamp_ms_{
        std::numeric_limits<double>::quiet_NaN()};
    mutable bool active_animation_demand_cache_{false};
    mutable bool active_animation_demand_cache_valid_{false};
    std::vector<transition_event_record> transition_events_;
    webscene_text_measure_callback text_measure_callback_{nullptr};
    void* text_measure_user_data_{nullptr};
    mutable std::unordered_map<
        text_measurement_key,
        webscene_text_metrics,
        text_measurement_key_hash,
        text_measurement_key_equal> text_measurement_cache_;
#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_SIZE_HASH_CACHE_CONTROL)
    std::unique_ptr<std::unordered_map<
        intrinsic_size_key,
        intrinsic_size_cache_entry,
        intrinsic_size_key_hash>> intrinsic_size_cache_;
#endif
    // Layout containers are short-lived but recur at every resize/animation
    // pass. Cache their allocator blocks per document so identical passes do
    // not repeatedly enter the process allocator. The storage is lazy to keep
    // never-laid-out documents pay-for-use and the document footprint bounded.
    std::unique_ptr<layout_scratch_storage> layout_scratch_;
    uint64_t intrinsic_size_cache_generation_{0};
    uint64_t intrinsic_size_cache_next_generation_{0};
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    uint64_t intrinsic_size_cache_hits_{0};
    uint64_t intrinsic_size_cache_misses_{0};
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_SIZE_DIRECT_CACHE_BENCHMARK)
    uint64_t intrinsic_size_direct_cache_hits_{0};
    uint64_t intrinsic_size_hash_lookups_{0};
#endif
    bool dirty_{true};
    bool globally_dirty_{true};
    uint64_t scene_generation_{1};
    std::vector<dom_node*> out_of_flow_geometry_dirty_roots_;
};

// Compatibility work must remain pay-for-use. Guard the 64-bit cold-path
// footprint so new web-facing state cannot silently become a per-node or
// per-document tax on every hosted component. Use upper budgets rather than
// ABI-specific equalities: libc++, libstdc++, and MSVC intentionally use
// different std::string and container representations.
static_assert(
    sizeof(void*) != 8 || sizeof(dom_node) <= 1024,
    "dom_node exceeded its cross-library 64-bit footprint budget");
static_assert(
    sizeof(void*) != 8 || sizeof(dom_node::form_control_data) <= 64,
    "form-control state exceeded its cross-library 64-bit footprint budget");
static_assert(
    sizeof(void*) != 8 || sizeof(native_document) <= 384,
    "native_document exceeded its cross-library 64-bit footprint budget");

} // namespace webscene_native
