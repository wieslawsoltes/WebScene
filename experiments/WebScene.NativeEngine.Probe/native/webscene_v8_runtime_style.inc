    static bool clear_inline_style(dom_node& node, const std::string& name)
    {
        const auto canonical_name = canonical_css_property_name(name);
        auto& authored = node.mutable_authored_style();
        authored.declarations.erase(canonical_name);
        authored.important_declarations.erase(canonical_name);
        if (apply_grid_placement_declaration(node.style, name, "auto")) {
            // Removing a placement declaration restores its affected
            // longhands to their computed initial value.
            constexpr std::array grid_properties{
                "grid-template-columns",
                "grid-template-rows",
                "grid-area",
                "grid-row",
                "grid-row-start",
                "grid-row-end",
                "grid-column",
                "grid-column-start",
                "grid-column-end"
            };
            const auto has_authored_grid = std::any_of(
                grid_properties.begin(),
                grid_properties.end(),
                [&](std::string_view property) {
                    return authored.declarations.contains(std::string(property));
                });
            if (!has_authored_grid) node.style.inline_property_mask &= ~inline_grid;
        } else if (apply_inset_declaration(node.style, name, "auto")) {
            node.style.inline_property_mask &=
                ~(inline_left | inline_top | inline_right | inline_bottom);
        } else if (apply_margin_declaration(node.style, name, "0px")) {
            node.style.inline_property_mask &= ~inline_margin;
        } else if (apply_padding_declaration(node.style, name, "0px")) {
            node.style.inline_property_mask &= ~inline_padding;
        } else if (apply_border_declaration(node.style, name, "")) {
            node.style.inline_property_mask &= ~inline_border;
        } else if (name == "width") {
            node.style.width = {};
            node.style.inline_property_mask &= ~inline_width;
        } else if (name == "height") {
            node.style.height = {};
            node.style.inline_property_mask &= ~inline_height;
        } else if (name == "minWidth" || name == "min-width") {
            node.style.min_width = {};
            node.style.inline_property_mask &= ~inline_min_width;
        } else if (name == "minHeight" || name == "min-height") {
            node.style.min_height = {};
            node.style.inline_property_mask &= ~inline_min_height;
        } else if (name == "maxWidth" || name == "max-width") {
            node.style.max_width = {};
            node.style.inline_property_mask &= ~inline_max_width;
        } else if (name == "maxHeight" || name == "max-height") {
            node.style.max_height = {};
            node.style.inline_property_mask &= ~inline_max_height;
        } else if (name == "left") {
            node.style.left = {};
            node.style.inline_property_mask &= ~inline_left;
        } else if (name == "top") {
            node.style.top = {};
            node.style.inline_property_mask &= ~inline_top;
        } else if (name == "right") {
            node.style.right = {};
            node.style.inline_property_mask &= ~inline_right;
        } else if (name == "bottom") {
            node.style.bottom = {};
            node.style.inline_property_mask &= ~inline_bottom;
        } else if (name == "display") {
            node.style.display = default_display_for_node(node);
            node.style.inline_property_mask &= ~inline_display;
        } else if (name == "position") {
            node.style.position = position_mode::normal;
            node.style.inline_property_mask &= ~inline_position;
        } else if (name == "cssFloat" || name == "float") {
            node.style.floating = float_mode::none;
            node.style.inline_property_mask &= ~inline_float;
        } else if (name == "zIndex" || name == "z-index") {
            node.style.z_index = 0;
            node.style.z_index_auto = true;
            node.style.inline_property_mask &= ~inline_z_index;
        } else if (name == "flexFlow" || name == "flex-flow") {
            node.style.direction = flex_direction::row;
            node.style.flex_reverse = false;
            node.style.flex_wrap = false;
            node.style.inline_property_mask &=
                ~(inline_flex_direction | inline_flex_wrap);
        } else if (name == "flexDirection" || name == "flex-direction") {
            node.style.direction = flex_direction::row;
            node.style.flex_reverse = false;
            node.style.inline_property_mask &= ~inline_flex_direction;
        } else if (name == "flexGrow" || name == "flex-grow" || name == "flex") {
            node.style.flex_grow = 0;
            node.style.inline_property_mask &= ~inline_flex_grow;
            if (name == "flex") {
                node.style.flex_shrink = 1;
                node.style.inline_property_mask &= ~inline_flex_shrink;
            }
        } else if (name == "flexShrink" || name == "flex-shrink") {
            node.style.flex_shrink = 1;
            node.style.inline_property_mask &= ~inline_flex_shrink;
        } else if (name == "flexBasis" || name == "flex-basis") {
            node.style.flex_basis = {};
            node.style.inline_property_mask &= ~inline_flex_basis;
        } else if (name == "flexWrap" || name == "flex-wrap") {
            node.style.flex_wrap = false;
            node.style.inline_property_mask &= ~inline_flex_wrap;
        } else if (name == "alignItems" || name == "align-items") {
            node.style.align_items = align_mode::stretch;
            node.style.inline_property_mask &= ~inline_align_items;
        } else if (name == "alignSelf" || name == "align-self") {
            node.style.align_self = align_mode::stretch;
            node.style.align_self_specified = false;
            node.style.inline_property_mask &= ~inline_align_self;
        } else if (name == "justifyContent" || name == "justify-content") {
            node.style.justify_content = justify_mode::start;
            node.style.inline_property_mask &= ~inline_justify_content;
        } else if (name == "gap" || name == "rowGap" || name == "row-gap"
            || name == "columnGap" || name == "column-gap") {
            if (name == "gap" || name == "rowGap" || name == "row-gap") {
                node.style.row_gap = {};
            }
            if (name == "gap" || name == "columnGap" || name == "column-gap") {
                node.style.column_gap = {};
            }
            node.style.inline_property_mask &= ~inline_gap;
        } else if (canonical_name == "border-spacing"
            || canonical_name == "border-collapse") {
            auto& table = node.style.mutable_table();
            if (canonical_name == "border-spacing") {
                table.border_spacing_horizontal = {2, length_unit::pixels};
                table.border_spacing_vertical = {2, length_unit::pixels};
            } else {
                table.border_collapsed = false;
            }
            if (!authored.declarations.contains("border-spacing")
                && !authored.declarations.contains("border-collapse")) {
                node.style.inline_property_mask &= ~inline_table_border_model;
            }
        } else if (name == "boxSizing" || name == "box-sizing") {
            node.style.border_box = false;
            node.style.inline_property_mask &= ~inline_box_sizing;
        } else if (name == "boxShadow" || name == "box-shadow") {
            node.style.box_shadow_offset_x = 0;
            node.style.box_shadow_offset_y = 0;
            node.style.box_shadow_blur_radius = 0;
            node.style.box_shadow_spread_radius = 0;
            node.style.box_shadow_rgba = 0;
            node.style.box_shadow_present = false;
            node.style.inline_property_mask &= ~inline_box_shadow;
        } else if (name == "borderRadius" || name == "border-radius") {
            node.style.border_top_left_radius = {};
            node.style.border_top_right_radius = {};
            node.style.border_bottom_right_radius = {};
            node.style.border_bottom_left_radius = {};
            node.style.clear_vertical_corner_radii();
            node.style.inline_property_mask &= ~inline_border_radius;
        } else if (name == "transform") {
            node.style.transform_translate_x = {};
            node.style.transform_translate_y = {};
            node.style.transform_scale_x = 1;
            node.style.transform_scale_y = 1;
            node.style.transform_rotate_degrees = 0;
            node.style.transform_specified = false;
            node.style.inline_property_mask &= ~inline_transform;
        } else if (name == "transformOrigin" || name == "transform-origin") {
            node.style.transform_origin_x = {50, length_unit::percent};
            node.style.transform_origin_y = {50, length_unit::percent};
            node.style.transform_origin_specified = false;
            node.style.inline_property_mask &= ~inline_transform_origin;
        } else if (name == "transition" || name == "transitionProperty"
            || name == "transition-property" || name == "transitionDuration"
            || name == "transition-duration" || name == "transitionDelay"
            || name == "transition-delay" || name == "transitionTimingFunction"
            || name == "transition-timing-function") {
            auto& animations = node.style.mutable_animations();
            animations.transition_property_value = "all";
            animations.transition_duration_value = "0s";
            animations.transition_delay_value = "0s";
            animations.transition_timing_function_value = "ease";
            configure_style_transitions(node.style);
            const auto has_authored_transition = std::any_of(
                node.authored_style().declarations.begin(),
                node.authored_style().declarations.end(),
                [](const auto& entry) {
                    return (inline_style_mask(entry.first) & inline_transition) != 0U;
                });
            if (!has_authored_transition) node.style.inline_property_mask &= ~inline_transition;
        } else if (name == "animation" || name == "animationName"
            || name == "animation-name" || name == "animationDuration"
            || name == "animation-duration" || name == "animationDelay"
            || name == "animation-delay" || name == "animationTimingFunction"
            || name == "animation-timing-function" || name == "animationIterationCount"
            || name == "animation-iteration-count") {
            auto& animations = node.style.mutable_animations();
            animations.animation_name_value = "none";
            animations.animation_duration_value = "0s";
            animations.animation_delay_value = "0s";
            animations.animation_timing_function_value = "ease";
            animations.animation_iteration_count_value = "1";
        } else if (name == "opacity") {
            node.style.opacity = 1;
            node.style.inline_property_mask &= ~inline_opacity;
        } else if (name == "background" || name == "backgroundColor"
            || name == "background-color") {
            node.style.background_rgba = 0;
            node.style.background_current_color = false;
            node.style.inline_property_mask &= ~inline_background;
        } else if (name == "overflow") {
            node.style.overflow_x = overflow_mode::visible;
            node.style.overflow_y = overflow_mode::visible;
            refresh_overflow_state(node.style);
            node.style.inline_property_mask &= ~inline_overflow;
        } else if (name == "visibility") {
            node.style.visibility_hidden = false;
            node.style.visibility_specified = false;
            node.style.inline_property_mask &= ~inline_visibility;
        } else if (name == "pointerEvents" || name == "pointer-events") {
            node.style.pointer_events_none = false;
            node.style.pointer_events_specified = false;
            node.style.inline_property_mask &= ~inline_pointer_events;
        } else if (name == "cursor") {
            if (auto* textual = node.style.mutable_textual_if_present()) {
                textual->cursor.clear();
            }
            node.style.inline_property_mask &= ~inline_cursor;
        } else if (name == "color") {
            node.style.foreground_rgba = 0;
            node.style.inline_property_mask &= ~inline_color;
        } else if (name == "font") {
            node.style.font_size = -1;
            node.style.line_height = -1;
            node.style.font_weight = 0;
            if (auto* textual = node.style.mutable_textual_if_present()) {
                textual->font_family.clear();
            }
            node.style.inline_property_mask &= ~(
                inline_font_size | inline_line_height | inline_font_weight | inline_font_family);
        } else if (name == "fontSize" || name == "font-size") {
            node.style.font_size = -1;
            node.style.inline_property_mask &= ~inline_font_size;
        } else if (name == "fontFamily" || name == "font-family") {
            if (auto* textual = node.style.mutable_textual_if_present()) {
                textual->font_family.clear();
            }
            node.style.inline_property_mask &= ~inline_font_family;
        } else if (name == "-webkit-font-smoothing"
            || name == "webkit-font-smoothing"
            || name == "webkitFontSmoothing") {
            if (auto* textual = node.style.mutable_textual_if_present()) {
                textual->font_smoothing.clear();
            }
            node.style.inline_property_mask &= ~inline_font_smoothing;
        } else if (name == "fontWeight" || name == "font-weight") {
            node.style.font_weight = 0;
            node.style.inline_property_mask &= ~inline_font_weight;
        } else if (name == "lineHeight" || name == "line-height") {
            node.style.line_height = -1;
            node.style.inline_property_mask &= ~inline_line_height;
        } else if (name == "letterSpacing" || name == "letter-spacing") {
            node.style.letter_spacing = 0;
            node.style.letter_spacing_specified = false;
            node.style.inline_property_mask &= ~inline_letter_spacing;
        } else if (name == "wordSpacing" || name == "word-spacing") {
            node.style.word_spacing = 0;
            node.style.word_spacing_specified = false;
            node.style.inline_property_mask &= ~inline_word_spacing;
        } else if (name == "textAlign" || name == "text-align") {
            if (auto* textual = node.style.mutable_textual_if_present()) {
                textual->text_align.clear();
            }
            node.style.inline_property_mask &= ~inline_text_align;
        } else if (name == "whiteSpace" || name == "white-space") {
            if (auto* textual = node.style.mutable_textual_if_present()) {
                textual->white_space.clear();
            }
            node.style.inline_property_mask &= ~inline_white_space;
        } else {
            return false;
        }
        return true;
    }

    static std::string serialized_inline_style(const dom_node& node)
    {
        const auto& authored = node.authored_style();
        std::vector<std::pair<std::string, std::string>> declarations(
            authored.declarations.begin(),
            authored.declarations.end());
        std::sort(declarations.begin(), declarations.end());
        std::string result;
        for (const auto& [name, value] : declarations) {
            if (!result.empty()) result.push_back(' ');
            result += name;
            result += ": ";
            result += value;
            if (authored.important_declarations.contains(name)) {
                result += " !important";
            }
            result.push_back(';');
        }
        return result;
    }

    static std::optional<std::string> expanded_inline_box_longhand(
        const dom_node& node,
        std::string_view canonical_name)
    {
        const auto serialize_length = [](const css_length& length) {
            if (length.unit == length_unit::automatic) return std::string{};
            if (length.unit == length_unit::max_content) return std::string{"max-content"};
            if (length.unit == length_unit::min_content) return std::string{"min-content"};
            if (length.unit == length_unit::fit_content) return std::string{"fit-content"};
            if (length.unit == length_unit::percent
                && std::abs(length.pixel_offset) > 0.000001F) {
                return std::string{"calc("} + serialize_css_number(length.value) + "% "
                    + (length.pixel_offset < 0 ? "- " : "+ ")
                    + serialize_css_number(std::abs(length.pixel_offset)) + "px)";
            }
            const auto suffix = length.unit == length_unit::percent ? "%"
                : length.unit == length_unit::em ? "em"
                : length.unit == length_unit::rem ? "rem"
                : length.unit == length_unit::viewport_width ? "vw"
                : length.unit == length_unit::viewport_height ? "vh" : "px";
            return serialize_css_number(length.value) + suffix;
        };
        const auto has = [&](uint64_t mask) {
            return (node.style.inline_property_mask & mask) != 0U;
        };
        const auto& authored = node.authored_style().declarations;
        if (has(inline_padding)) {
            node_style specified;
            auto applied = false;
            constexpr std::array properties{
                "padding", "padding-block", "padding-inline",
                "padding-block-start", "padding-block-end",
                "padding-inline-start", "padding-inline-end",
                "padding-top", "padding-right", "padding-bottom", "padding-left"
            };
            for (const auto* property : properties) {
                const auto declaration = authored.find(property);
                if (declaration == authored.end()) continue;
                applied = apply_padding_declaration(
                    specified, property, declaration->second) || applied;
            }
            if (applied) {
                if (canonical_name == "padding-left") return serialize_length(specified.padding_left);
                if (canonical_name == "padding-top") return serialize_length(specified.padding_top);
                if (canonical_name == "padding-right") return serialize_length(specified.padding_right);
                if (canonical_name == "padding-bottom") return serialize_length(specified.padding_bottom);
            }
        }
        if (has(inline_margin)) {
            node_style specified;
            auto applied = false;
            constexpr std::array properties{
                "margin", "margin-block", "margin-inline",
                "margin-block-start", "margin-block-end",
                "margin-inline-start", "margin-inline-end",
                "margin-top", "margin-right", "margin-bottom", "margin-left"
            };
            for (const auto* property : properties) {
                const auto declaration = authored.find(property);
                if (declaration == authored.end()) continue;
                applied = apply_margin_declaration(
                    specified, property, declaration->second) || applied;
            }
            if (applied) {
                if (canonical_name == "margin-left") return specified.margin_left_auto
                    ? std::string{"auto"} : serialize_length(specified.margin_left);
                if (canonical_name == "margin-top") return specified.margin_top_auto
                    ? std::string{"auto"} : serialize_length(specified.margin_top);
                if (canonical_name == "margin-right") return specified.margin_right_auto
                    ? std::string{"auto"} : serialize_length(specified.margin_right);
                if (canonical_name == "margin-bottom") return specified.margin_bottom_auto
                    ? std::string{"auto"} : serialize_length(specified.margin_bottom);
            }
        }
        if (has(inline_border)) {
            node_style specified;
            auto applied = false;
            constexpr std::array properties{
                "border", "border-width",
                "border-top", "border-right", "border-bottom", "border-left",
                "border-top-width", "border-right-width",
                "border-bottom-width", "border-left-width"
            };
            for (const auto* property : properties) {
                const auto declaration = authored.find(property);
                if (declaration == authored.end()) continue;
                applied = apply_border_declaration(
                    specified, property, declaration->second) || applied;
            }
            if (applied) {
                if (canonical_name == "border-left-width") {
                    return serialize_length(specified.border_left_width);
                }
                if (canonical_name == "border-top-width") {
                    return serialize_length(specified.border_top_width);
                }
                if (canonical_name == "border-right-width") {
                    return serialize_length(specified.border_right_width);
                }
                if (canonical_name == "border-bottom-width") {
                    return serialize_length(specified.border_bottom_width);
                }
            }
        }
        return std::nullopt;
    }

    static void sync_style_attribute(dom_node& node)
    {
        node.attributes["style"] = serialized_inline_style(node);
    }

    static void get_style_css_text(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.Holder());
        info.GetReturnValue().Set(js_dom_string(
            info.GetIsolate(),
            node == nullptr ? std::string{} : serialized_inline_style(*node)));
    }

    static void set_style_css_text(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> raw_value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        const auto previous = node->attributes.find("style");
        const std::optional<std::string> old_value = previous == node->attributes.end()
            ? std::nullopt
            : std::optional<std::string>(previous->second);
        node->clear_authored_style();
        node->style.clear_grid();
        node->style.clear_table();
        node->style.inline_property_mask = 0U;
        node->style.important_property_mask = 0U;
        const auto value = to_utf8(info.GetIsolate(), raw_value);
        uint64_t inline_property_mask = 0U;
        for (const auto& declaration : parse_css_declarations(value, true)) {
            if (!store_inline_declaration(*node, declaration)) continue;
            self->apply_css_declaration(*node, declaration);
            inline_property_mask |= inline_style_mask(declaration.name);
        }
        node->style.inline_property_mask |= inline_property_mask;
        sync_style_attribute(*node);
        self->recascade_connected_subtree(*node, "style.cssText");
        self->notify_custom_element_attribute(
            *node, "style", old_value, node->attributes.at("style"));
    }

    static void style_set_property(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (info.Length() < 2) return;
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::style));
        auto* node = unwrap_node(info.This());
        if (node == nullptr) return;
        const auto previous = node->attributes.find("style");
        const std::optional<std::string> old_value = previous == node->attributes.end()
            ? std::nullopt
            : std::optional<std::string>(previous->second);
        const auto name = to_utf8(info.GetIsolate(), info[0]);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (self->resize_profile_active) {
            const auto canonical_name = canonical_css_property_name(name);
            ++self->last_resize_style_property_counts[canonical_name];
            ++self->last_resize_style_target_counts[
                canonical_name + "@" + node->tag + "#" + node->id_attribute
                    + "." + node->class_name + "/position-"
                    + std::to_string(static_cast<uint32_t>(node->style.position))];
        }
#endif
        const auto property_mask = inline_style_mask(name);
        const auto has_important_rule =
            (node->style.important_property_mask & property_mask) != 0U;
        const auto value = to_utf8(info.GetIsolate(), info[1]);
        self->inventory_css_value_functions(value, "cssom-style");
        if (name.starts_with("--")) {
            if (!valid_custom_property_name(name)) return;
            if (value.empty()) {
                node->style.mutable_custom_properties().values.erase(name);
                auto& authored = node->mutable_authored_style();
                authored.declarations.erase(name);
                authored.important_declarations.erase(name);
            } else {
                node->style.mutable_custom_properties().values[name] = value;
                auto& authored = node->mutable_authored_style();
                authored.declarations[name] = value;
                authored.important_declarations.erase(name);
            }
            sync_style_attribute(*node);
            self->recascade_connected_subtree(*node);
            self->notify_custom_element_attribute(
                *node, "style", old_value, node->attributes.at("style"));
            return;
        }
        if (value.empty()) {
            clear_inline_style(*node, name);
            sync_style_attribute(*node);
            self->recascade_connected_subtree(*node);
            self->notify_custom_element_attribute(
                *node, "style", old_value, node->attributes.at("style"));
            return;
        }
        if (!valid_cssom_declaration_value(name, value)) return;
        const auto canonical_name = canonical_css_property_name(name);
        auto& authored = node->mutable_authored_style();
        authored.declarations[canonical_name] = serialize_cssom_specified_value(
            canonical_name,
            value);
        authored.important_declarations.erase(canonical_name);
        if (apply_grid_placement_declaration(node->style, name, value)) {
            // Placement CSSOM is stored as computed tokens; layout consumes the
            // existing numeric grid-column projection when applicable.
            node->style.inline_property_mask |= inline_grid;
        } else if (apply_inset_declaration(node->style, name, value)) {
            node->style.inline_property_mask |=
                inline_left | inline_top | inline_right | inline_bottom;
        } else if (apply_margin_declaration(node->style, name, value)) {
            node->style.inline_property_mask |= inline_margin;
        } else if (apply_padding_declaration(node->style, name, value)) {
            node->style.inline_property_mask |= inline_padding;
        } else if (apply_border_declaration(node->style, name, value)) {
            node->style.inline_property_mask |= inline_border;
        } else if (name == "width") {
            node->style.width = native_document::parse_length(value);
            node->style.inline_property_mask |= inline_width;
        } else if (name == "height") {
            node->style.height = native_document::parse_length(value);
            node->style.inline_property_mask |= inline_height;
        } else if (name == "minWidth") {
            node->style.min_width = native_document::parse_length(value);
            node->style.inline_property_mask |= inline_min_width;
        } else if (name == "minHeight") {
            node->style.min_height = native_document::parse_length(value);
            node->style.inline_property_mask |= inline_min_height;
        } else if (name == "maxWidth") {
            node->style.max_width = value == "none" ? css_length{} : native_document::parse_length(value);
            node->style.inline_property_mask |= inline_max_width;
        } else if (name == "maxHeight") {
            node->style.max_height = value == "none" ? css_length{} : native_document::parse_length(value);
            node->style.inline_property_mask |= inline_max_height;
        } else if (name == "min-width") {
            node->style.min_width = native_document::parse_length(value);
            node->style.inline_property_mask |= inline_min_width;
        } else if (name == "min-height") {
            node->style.min_height = native_document::parse_length(value);
            node->style.inline_property_mask |= inline_min_height;
        } else if (name == "max-width") {
            node->style.max_width = value == "none" ? css_length{} : native_document::parse_length(value);
            node->style.inline_property_mask |= inline_max_width;
        } else if (name == "max-height") {
            node->style.max_height = value == "none" ? css_length{} : native_document::parse_length(value);
            node->style.inline_property_mask |= inline_max_height;
        } else if (name == "position") {
            node->style.position = value == "absolute" ? position_mode::absolute
                : value == "fixed" ? position_mode::fixed
                : value == "relative" ? position_mode::relative
                : position_mode::normal;
            node->style.inline_property_mask |= inline_position;
        } else if (name == "float" || name == "cssFloat") {
            node->style.floating = value == "left" ? float_mode::left
                : value == "right" ? float_mode::right
                : float_mode::none;
            node->style.inline_property_mask |= inline_float;
        } else if (name == "z-index") {
            node->style.z_index_auto = value == "auto" || value == "initial"
                || value == "unset";
            node->style.z_index = node->style.z_index_auto
                ? 0 : std::atoi(value.c_str());
            node->style.inline_property_mask |= inline_z_index;
        } else if (name == "display") {
            node->style.display = parse_display_mode(value);
            node->style.inline_property_mask |= inline_display;
        } else if (name == "flex-direction") {
            node->style.direction = value == "row" || value == "row-reverse"
                ? flex_direction::row : flex_direction::column;
            node->style.flex_reverse = value == "row-reverse" || value == "column-reverse";
            node->style.inline_property_mask |= inline_flex_direction;
        } else if (name == "flex-flow") {
            const auto flow = parse_flex_flow(value);
            node->style.direction = flow.direction;
            node->style.flex_reverse = flow.reverse;
            node->style.flex_wrap = flow.wrap;
            node->style.inline_property_mask |=
                inline_flex_direction | inline_flex_wrap;
        } else if (name == "flex-grow") {
            node->style.flex_grow = std::max(0.0F, std::strtof(value.c_str(), nullptr));
            node->style.inline_property_mask |= inline_flex_grow;
        } else if (name == "flex-shrink") {
            node->style.flex_shrink = std::max(0.0F, std::strtof(value.c_str(), nullptr));
            node->style.inline_property_mask |= inline_flex_shrink;
        } else if (name == "flex-basis") {
            node->style.flex_basis = native_document::parse_length(value);
            node->style.inline_property_mask |= inline_flex_basis;
        } else if (name == "flex-wrap") {
            node->style.flex_wrap = value == "wrap" || value == "wrap-reverse";
            node->style.inline_property_mask |= inline_flex_wrap;
        } else if (name == "align-items") {
            node->style.align_items = value == "center" ? align_mode::center
                : value == "flex-start" || value == "start" ? align_mode::start
                : value == "flex-end" || value == "end" ? align_mode::end
                : value == "baseline" || value == "first baseline" ? align_mode::baseline
                : align_mode::stretch;
            node->style.inline_property_mask |= inline_align_items;
        } else if (name == "align-self") {
            node->style.align_self_specified = value != "auto";
            node->style.align_self = value == "center" ? align_mode::center
                : value == "flex-start" || value == "start" ? align_mode::start
                : value == "flex-end" || value == "end" ? align_mode::end
                : value == "baseline" || value == "first baseline" ? align_mode::baseline
                : align_mode::stretch;
            node->style.inline_property_mask |= inline_align_self;
        } else if (name == "justify-content") {
            node->style.justify_content = value == "center" ? justify_mode::center
                : value == "flex-end" || value == "end" ? justify_mode::end
                : value == "space-between" ? justify_mode::space_between
                : value == "space-around" ? justify_mode::space_around
                : value == "space-evenly" ? justify_mode::space_evenly
                : justify_mode::start;
            node->style.inline_property_mask |= inline_justify_content;
        } else if (name == "gap" || name == "row-gap" || name == "column-gap") {
            std::istringstream stream(value);
            std::string first;
            std::string second;
            stream >> first >> second;
            if (name == "gap") {
                node->style.row_gap = native_document::parse_length(first);
                node->style.column_gap = native_document::parse_length(
                    second.empty() ? first : second);
            } else if (name == "row-gap") {
                node->style.row_gap = native_document::parse_length(first);
            } else {
                node->style.column_gap = native_document::parse_length(first);
            }
            node->style.inline_property_mask |= inline_gap;
        } else if (name == "border-spacing") {
            std::istringstream stream(value);
            std::string horizontal;
            std::string vertical;
            stream >> horizontal >> vertical;
            auto& table = node->style.mutable_table();
            table.border_spacing_horizontal = native_document::parse_length(horizontal);
            table.border_spacing_vertical = native_document::parse_length(
                vertical.empty() ? horizontal : vertical);
            node->style.inline_property_mask |= inline_table_border_model;
        } else if (name == "border-collapse") {
            node->style.mutable_table().border_collapsed = value == "collapse";
            node->style.inline_property_mask |= inline_table_border_model;
        } else if (name == "box-sizing") {
            node->style.border_box = value == "border-box";
            node->style.inline_property_mask |= inline_box_sizing;
        } else if (name == "box-shadow") {
            auto complete = true;
            apply_box_shadow_value(node->style, value, complete);
            node->style.inline_property_mask |= inline_box_shadow;
        } else if (name == "border-radius") {
            apply_corner_radius_declaration(
                name,
                value,
                node->style);
            node->style.inline_property_mask |= inline_border_radius;
        } else if (name == "transform") {
            native_document::parse_transform_translate(
                value,
                node->style.transform_translate_x,
                node->style.transform_translate_y,
                node->style.transform_scale_x,
                node->style.transform_scale_y,
                node->style.transform_rotate_degrees);
            node->style.transform_specified = true;
            node->style.inline_property_mask |= inline_transform;
        } else if (name == "transform-origin") {
            native_document::parse_transform_origin(
                value,
                node->style.transform_origin_x,
                node->style.transform_origin_y);
            node->style.transform_origin_specified = true;
            node->style.inline_property_mask |= inline_transform_origin;
        } else if (name == "transition") {
            apply_transition_shorthand(node->style, value);
            node->style.inline_property_mask |= inline_transition;
        } else if (name == "transition-property") {
            node->style.mutable_animations().transition_property_value = value;
            configure_style_transitions(node->style);
            node->style.inline_property_mask |= inline_transition_property;
        } else if (name == "transition-duration") {
            node->style.mutable_animations().transition_duration_value = value;
            configure_style_transitions(node->style);
            node->style.inline_property_mask |= inline_transition_duration;
        } else if (name == "transition-delay") {
            node->style.mutable_animations().transition_delay_value = value;
            configure_style_transitions(node->style);
            node->style.inline_property_mask |= inline_transition_delay;
        } else if (name == "transition-timing-function") {
            node->style.mutable_animations().transition_timing_function_value = value;
            configure_style_transitions(node->style);
            node->style.inline_property_mask |= inline_transition_timing;
        } else if (name == "opacity") {
            node->style.opacity = std::clamp(std::strtof(value.c_str(), nullptr), 0.0F, 1.0F);
            node->style.inline_property_mask |= inline_opacity;
        } else if (name == "visibility") {
            node->style.visibility_hidden = value == "hidden" || value == "collapse";
            node->style.visibility_specified = value != "inherit" && value != "unset";
            node->style.inline_property_mask |= inline_visibility;
        } else if (name == "pointer-events") {
            node->style.pointer_events_none = value == "none";
            node->style.pointer_events_specified = value != "inherit" && value != "unset";
            node->style.inline_property_mask |= inline_pointer_events;
        } else if (name == "cursor") {
            node->style.mutable_textual().cursor = value;
            node->style.inline_property_mask |= inline_cursor;
        }
        else if (name == "background" || name == "background-color") {
            node->style.background_current_color =
                lower_html_name(value) == "currentcolor";
            node->style.background_rgba = node->style.background_current_color
                ? 0U : native_document::parse_color(value);
            node->style.inline_property_mask |= inline_background;
        } else if (name == "color") {
            node->style.foreground_rgba = native_document::parse_color(value);
            node->style.inline_property_mask |= inline_color;
        } else if (name == "font") {
            if (value == "inherit" || value == "unset") {
                node->style.font_size = -1;
                node->style.line_height = -1;
                node->style.font_weight = 0;
                node->style.mutable_textual().font_family.clear();
                node->style.inline_property_mask |= inline_font_size | inline_line_height
                    | inline_font_weight | inline_font_family;
            } else if (const auto font = parse_font_shorthand(*node, value); font.has_value()) {
                node->style.font_size = font->font_size;
                node->style.line_height = font->line_height;
                node->style.font_weight = font->font_weight;
                node->style.mutable_textual().font_family =
                    font->font_family;
                node->style.inline_property_mask |= inline_font_size | inline_line_height
                    | inline_font_weight | inline_font_family;
            }
        } else if (name == "font-size") {
            node->style.font_size = resolved_declared_font_size(*node, value);
            node->style.inline_property_mask |= inline_font_size;
        } else if (name == "font-family") {
            node->style.mutable_textual().font_family =
                value == "inherit" || value == "unset"
                ? std::string{} : value;
            node->style.inline_property_mask |= inline_font_family;
        } else if (name == "-webkit-font-smoothing"
            || name == "webkit-font-smoothing") {
            node->style.mutable_textual().font_smoothing =
                value == "inherit" || value == "unset"
                ? std::string{} : value;
            node->style.inline_property_mask |= inline_font_smoothing;
        } else if (name == "font-weight") {
            node->style.font_weight = value == "inherit" || value == "unset" ? 0
                : value == "bold" ? 700
                : value == "normal" ? 400
                : std::max(1, static_cast<int>(std::lround(
                    native_document::parse_length(value).value)));
            node->style.inline_property_mask |= inline_font_weight;
        } else if (name == "line-height") {
            const auto font_size = node->style.font_size >= 0
                ? node->style.font_size : inherited_font_size(*node);
            node->style.line_height = resolved_declared_line_height(*node, value, font_size);
            node->style.inline_property_mask |= inline_line_height;
        } else if (name == "letter-spacing" || name == "word-spacing") {
            const auto length = native_document::parse_length(value);
            const auto font_size = node->style.font_size >= 0
                ? node->style.font_size : inherited_font_size(*node);
            const auto spacing = value == "normal" ? 0
                : length.unit == length_unit::em ? length.value * font_size
                : length.unit == length_unit::rem ? length.value * 14.0F
                : length.value;
            const auto specified = value != "inherit" && value != "unset";
            if (name == "letter-spacing") {
                node->style.letter_spacing = spacing;
                node->style.letter_spacing_specified = specified;
                node->style.inline_property_mask |= inline_letter_spacing;
            } else {
                node->style.word_spacing = spacing;
                node->style.word_spacing_specified = specified;
                node->style.inline_property_mask |= inline_word_spacing;
            }
        } else if (name == "text-align") {
            node->style.mutable_textual().text_align = value;
            node->style.inline_property_mask |= inline_text_align;
        } else if (name == "white-space") {
            node->style.mutable_textual().white_space = value;
            node->style.inline_property_mask |= inline_white_space;
        }
        self->recompute_inline_font_relative_metrics(*node);
        sync_style_attribute(*node);
        self->notify_custom_element_attribute(
            *node, "style", old_value, node->attributes.at("style"));
        if (has_important_rule) self->apply_css_rules(*node);
        else {
            self->configure_style_keyframe_animation(*node);
            self->document.update_style_animations(*node);
            self->detect_css_compositions(*node);
            self->mark_cssom_layout_dirty(*node, canonical_name);
        }
    }

    static void style_get_property(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (info.Length() < 1) {
            info.GetReturnValue().Set(js_string(info.GetIsolate(), ""));
            return;
        }
        auto* node = unwrap_node(info.This());
        if (node == nullptr) {
            info.GetReturnValue().Set(js_string(info.GetIsolate(), ""));
            return;
        }
        const auto name = to_utf8(info.GetIsolate(), info[0]);
        if (name.starts_with("--") && !valid_custom_property_name(name)) {
            info.GetReturnValue().Set(js_string(info.GetIsolate(), ""));
            return;
        }
        const auto computed = info.This()->InternalFieldCount() > 1
            && info.This()->GetInternalField(1).As<v8::Value>()->BooleanValue(info.GetIsolate());
        auto* self = current(info.GetIsolate());
        if (computed && !self->is_connected(*node)) {
            info.GetReturnValue().Set(js_string(info.GetIsolate(), ""));
            return;
        }
        if (computed) self->ensure_layout();
        std::string value;
        if (!computed) {
            const auto& authored_style = node->authored_style().declarations;
            const auto authored = authored_style.find(
                canonical_css_property_name(name));
            if (authored != authored_style.end()) value = authored->second;
            else if (const auto expanded = expanded_inline_box_longhand(
                         *node, canonical_css_property_name(name));
                     expanded.has_value()) {
                value = *expanded;
            }
        } else if (name.starts_with("--")) {
            const std::string* known_value = nullptr;
            for (auto* current_node = node; current_node != nullptr;
                 current_node = current_node->parent) {
                const auto& custom = current_node->style.custom_properties().values;
                const auto known = custom.find(name);
                if (known != custom.end()) {
                    known_value = &known->second;
                    break;
                }
            }
            if (known_value == nullptr) {
                auto* self = current(info.GetIsolate());
                const auto known = self->css_variables.find(name);
                if (known != self->css_variables.end()) known_value = &known->second;
            }
            value = known_value == nullptr ? "" : *known_value;
        } else {
            const auto format_length = [](const css_length& length) {
                if (length.unit == length_unit::automatic) return std::string {};
                if (length.unit == length_unit::max_content) return std::string{"max-content"};
                if (length.unit == length_unit::min_content) return std::string{"min-content"};
                if (length.unit == length_unit::fit_content) return std::string{"fit-content"};
                std::ostringstream stream;
                if (length.unit == length_unit::percent
                    && std::abs(length.pixel_offset) > 0.000001F) {
                    stream << "calc(" << length.value << "% "
                        << (length.pixel_offset < 0 ? "- " : "+ ")
                        << std::abs(length.pixel_offset) << "px)";
                    return stream.str();
                }
                stream << length.value;
                stream << (length.unit == length_unit::percent ? "%" : "px");
                return stream.str();
            };
            const auto format_pixels = [](float pixels) {
                std::ostringstream stream;
                stream << pixels << "px";
                return stream.str();
            };
            const auto format_computed_length = [&](const css_length& length, float reference) {
                if (length.unit == length_unit::em
                    || length.unit == length_unit::rem
                    || length.unit == length_unit::viewport_width
                    || length.unit == length_unit::viewport_height
                    || length.unit == length_unit::viewport_width_capped
                    || length.unit == length_unit::viewport_height_capped
                    || length.unit == length_unit::viewport_width_floored
                    || length.unit == length_unit::viewport_height_floored) {
                    return format_pixels(resolve_connected_css_length(
                        *node, length, reference));
                }
                return format_length(length);
            };
            const auto format_box_length = [&](const css_length& length) {
                return length.unit == length_unit::automatic
                    ? std::string{"0px"} : format_computed_length(length, 0);
            };
            const auto resolve_transform_origin = [](css_length length, float available) {
                return length.unit == length_unit::percent
                    ? available * length.value / 100.0F + length.pixel_offset
                    : length.value;
            };
            float connected_used_value = 0.0F;
            if (try_connected_used_geometry_value(
                    *node,
                    canonical_css_property_name(name),
                    connected_used_value)) {
                value = format_pixels(connected_used_value);
            } else if (name == "z-index") {
                value = node->style.z_index_auto
                    ? "auto" : std::to_string(node->style.z_index);
            } else if (name == "position") {
                value = node->style.position == position_mode::absolute ? "absolute"
                    : node->style.position == position_mode::fixed ? "fixed"
                    : node->style.position == position_mode::relative ? "relative"
                    : "static";
            } else if (name == "float") {
                value = node->style.floating == float_mode::left ? "left"
                    : node->style.floating == float_mode::right ? "right" : "none";
            } else if (name == "overflow" || name == "overflow-x" || name == "overflow-y") {
                // CSSOM returns the computed initial value rather than an
                // absent/undefined value. Modal scroll-lock implementations
                // unconditionally calls toLowerCase() on this result.
                const auto [x, y] = computed_overflow(node->style);
                value = name == "overflow-x" ? serialize_overflow_mode(x)
                    : name == "overflow-y" ? serialize_overflow_mode(y)
                    : x == y ? serialize_overflow_mode(x)
                    : serialize_overflow_mode(x) + " " + serialize_overflow_mode(y);
            } else if (name == "visibility") {
                auto* current_node = node;
                while (current_node != nullptr
                    && !current_node->style.visibility_specified) {
                    current_node = current_node->parent;
                }
                value = current_node != nullptr
                    && current_node->style.visibility_hidden
                    ? "hidden" : "visible";
            } else if (name == "padding-left") {
                value = format_box_length(node->style.padding_left);
            } else if (name == "padding-top") {
                value = format_box_length(node->style.padding_top);
            } else if (name == "padding-right") {
                value = format_box_length(node->style.padding_right);
            } else if (name == "padding-bottom") {
                value = format_box_length(node->style.padding_bottom);
            } else if (name == "left" || name == "inset-inline-start") {
                value = format_length(node->style.left);
            } else if (name == "top" || name == "inset-block-start") {
                value = format_length(node->style.top);
            } else if (name == "right" || name == "inset-inline-end") {
                value = format_length(node->style.right);
            } else if (name == "bottom" || name == "inset-block-end") {
                value = format_length(node->style.bottom);
            } else if (name == "margin-left") {
                value = format_box_length(node->style.margin_left);
            } else if (name == "margin-top") {
                value = format_box_length(node->style.margin_top);
            } else if (name == "margin-right") {
                value = format_box_length(node->style.margin_right);
            } else if (name == "margin-bottom") {
                value = format_box_length(node->style.margin_bottom);
            } else if (name == "gap") {
                value = format_computed_length(node->style.row_gap, node->layout.height)
                    + " "
                    + format_computed_length(node->style.column_gap, node->layout.width);
            } else if (name == "row-gap") {
                value = format_computed_length(node->style.row_gap, node->layout.height);
            } else if (name == "column-gap") {
                value = format_computed_length(node->style.column_gap, node->layout.width);
            } else if (name == "border-spacing") {
                value = format_computed_length(
                        node->style.table().border_spacing_horizontal,
                        node->layout.width)
                    + " "
                    + format_computed_length(
                        node->style.table().border_spacing_vertical,
                        node->layout.height);
            } else if (name == "border-collapse") {
                value = node->style.table().border_collapsed ? "collapse" : "separate";
            } else if (name == "flex-direction") {
                value = flex_direction_name(node->style);
            } else if (name == "flex-flow") {
                value = flex_direction_name(node->style) + " "
                    + flex_wrap_name(node->style);
            } else if (name == "flex-basis") {
                value = format_computed_length(node->style.flex_basis, node->layout.width);
            } else if (name == "flex-wrap") {
                value = flex_wrap_name(node->style);
            } else if (name == "grid-area") {
                value = node->style.grid().area_value;
            } else if (name == "grid-row") {
                value = node->style.grid().row_value;
            } else if (name == "grid-row-start") {
                value = node->style.grid().row_start_value;
            } else if (name == "grid-row-end") {
                value = node->style.grid().row_end_value;
            } else if (name == "grid-column") {
                value = node->style.grid().column_value;
            } else if (name == "grid-column-start") {
                value = node->style.grid().column_start_value;
            } else if (name == "grid-column-end") {
                value = node->style.grid().column_end_value;
            } else if (name == "background-image") {
                value = node->style.background_image().image_value;
            } else if (name == "background-repeat") {
                value = node->style.background_image().repeat;
            } else if (name == "background-position") {
                value = node->style.background_image().position_x + " "
                    + node->style.background_image().position_y;
            } else if (name == "background-size") {
                value = node->style.background_image().size_x + " "
                    + node->style.background_image().size_y;
            } else if (name == "box-shadow") {
                if (!node->style.box_shadow_present) {
                    value = "none";
                } else {
                    const auto color = node->style.box_shadow_rgba;
                    std::ostringstream stream;
                    stream << node->style.box_shadow_offset_x << "px "
                        << node->style.box_shadow_offset_y << "px "
                        << node->style.box_shadow_blur_radius << "px "
                        << node->style.box_shadow_spread_radius << "px rgba("
                        << ((color >> 24U) & 0xFFU) << ", "
                        << ((color >> 16U) & 0xFFU) << ", "
                        << ((color >> 8U) & 0xFFU) << ", "
                        << std::setprecision(6)
                        << static_cast<double>(color & 0xFFU) / 255.0 << ')';
                    value = stream.str();
                }
            } else if (name == "outline-width") {
                value = format_box_length(node->style.outline_width);
            } else if (name == "outline-style") {
                value = node->style.outline_width.value > 0
                    && (node->style.outline_rgba & 0xFFU) != 0U ? "solid" : "none";
            } else if (name == "border-top-width") {
                value = format_box_length(node->style.border_top_width);
            } else if (name == "border-top-style") {
                value = node->style.border_top_width.value > 0 ? "solid" : "none";
            } else if (name == "width") {
                value = computed
                    ? format_pixels(connected_computed_dimension(
                        *node, node->style.width, true))
                    : format_length(node->style.width);
            } else if (name == "height") {
                value = computed
                    ? format_pixels(connected_computed_dimension(
                        *node, node->style.height, false))
                    : format_length(node->style.height);
            } else if (name == "min-width") {
                value = format_computed_length(node->style.min_width, node->layout.width);
            } else if (name == "min-height") {
                value = format_computed_length(node->style.min_height, node->layout.height);
            } else if (name == "max-width") {
                value = node->style.max_width.value == 0
                    ? "none"
                    : format_computed_length(node->style.max_width, node->layout.width);
            } else if (name == "max-height") {
                value = node->style.max_height.value == 0
                    ? "none"
                    : format_computed_length(node->style.max_height, node->layout.height);
            } else if (name == "transform-origin") {
                value = format_pixels(resolve_transform_origin(
                    node->style.transform_origin_x, node->layout.width)) + " "
                    + format_pixels(resolve_transform_origin(
                        node->style.transform_origin_y, node->layout.height));
            } else if (name == "transform") {
                if (node->style.transform_specified
                    || node->transform_animation_active_value()
                    || node->rotation_keyframe_animation_active_value()) {
                    constexpr double degrees_to_radians = 0.017453292519943295;
                    const auto radians = static_cast<double>(
                        node->painted_transform_rotation_value())
                        * degrees_to_radians;
                    const auto cosine = std::cos(radians);
                    const auto sine = std::sin(radians);
                    const auto resolve = [](css_length length, float available) {
                        return length.unit == length_unit::percent
                            ? static_cast<double>(available) * length.value / 100.0
                                + length.pixel_offset
                            : static_cast<double>(length.value);
                    };
                    const auto format_number = [](double number, bool preserve_negative_zero = false) {
                        if (std::abs(number) < 0.000001) {
                            number = preserve_negative_zero && std::signbit(number)
                                ? -0.0
                                : 0.0;
                        }
                        std::ostringstream stream;
                        stream << std::setprecision(8) << number;
                        return stream.str();
                    };
                    const auto a = cosine * node->painted_transform_scale_x_value();
                    const auto b = sine * node->painted_transform_scale_x_value();
                    const auto c = -sine * node->painted_transform_scale_y_value();
                    const auto d = cosine * node->painted_transform_scale_y_value();
                    const auto e = resolve(
                        node->painted_transform_translate_x_value(), node->layout.width);
                    const auto f = resolve(
                        node->painted_transform_translate_y_value(), node->layout.height);
                    value = "matrix(" + format_number(a) + ", " + format_number(b, true)
                        + ", " + format_number(c) + ", " + format_number(d)
                        + ", " + format_number(e) + ", " + format_number(f) + ")";
                } else {
                    value = "none";
                }
            } else if (name == "transition-property") {
                value = node->style.animations().transition_property_value;
            } else if (name == "transition-duration") {
                value = computed
                    ? serialize_computed_time_list(
                        node->style.animations().transition_duration_value)
                    : node->style.animations().transition_duration_value;
            } else if (name == "transition-delay") {
                value = computed
                    ? serialize_computed_time_list(
                        node->style.animations().transition_delay_value)
                    : node->style.animations().transition_delay_value;
            } else if (name == "transition-timing-function") {
                value = node->style.animations().transition_timing_function_value;
            } else if (name == "opacity") {
                value = serialize_css_number(node->painted_opacity_value());
            } else if (name == "color") {
                uint32_t color = 0;
                for (auto* current_node = node;
                    current_node != nullptr && (color & 0xFFU) == 0U;
                    current_node = current_node->parent) {
                    if (current_node->color_animation_active_value()) {
                        color = current_node->painted_foreground_value();
                    } else if ((current_node->style.foreground_rgba & 0xFFU) != 0U) {
                        color = current_node->painted_foreground_value();
                    }
                }
                if ((color & 0xFFU) == 0U) color = 0x000000FFU;
                std::ostringstream stream;
                const auto red = static_cast<unsigned>((color >> 24U) & 0xFFU);
                const auto green = static_cast<unsigned>((color >> 16U) & 0xFFU);
                const auto blue = static_cast<unsigned>((color >> 8U) & 0xFFU);
                const auto alpha = static_cast<unsigned>(color & 0xFFU);
                if (alpha == 0xFFU) {
                    stream << "rgb(" << red << ", " << green << ", " << blue << ')';
                } else {
                    stream << "rgba(" << red << ", " << green << ", " << blue << ", "
                        << std::setprecision(6) << static_cast<double>(alpha) / 255.0 << ')';
                }
                value = stream.str();
            } else if (name == "white-space") {
                value = node->style.textual().white_space;
            } else if (name == "cursor") {
                value = resolved_cursor(*node);
            } else if (name == "font-size") {
                value = serialize_css_number(node->style.font_size >= 0
                    ? node->style.font_size : inherited_font_size(*node)) + "px";
            } else if (name == "font-family") {
                value = resolved_font_family(*node);
            } else if (name == "-webkit-font-smoothing"
                || name == "webkit-font-smoothing") {
                value = resolved_font_smoothing(*node);
            } else if (name == "font-weight") {
                value = std::to_string(resolved_font_weight(*node));
            } else if (name == "letter-spacing") {
                value = serialize_css_number(resolved_letter_spacing(*node)) + "px";
            } else if (name == "word-spacing") {
                value = serialize_css_number(resolved_word_spacing(*node)) + "px";
            } else if (name == "line-height") {
                auto line_height = node->style.line_height;
                for (auto* ancestor = node->parent;
                    line_height == -1.0F && ancestor != nullptr;
                    ancestor = ancestor->parent) {
                    line_height = ancestor->style.line_height;
                }
                if (line_height == -2.0F) {
                    value = "normal";
                } else if (line_height < 0) {
                    const auto font_size = node->style.font_size >= 0
                        ? node->style.font_size : inherited_font_size(*node);
                    line_height = font_size * 1.125F;
                    value = serialize_css_number(line_height) + "px";
                } else {
                    value = serialize_css_number(line_height) + "px";
                }
            }
        }
        if (computed && value.empty()
            && inline_style_mask(canonical_css_property_name(name)) == 0U) {
            const auto& authored_style = node->authored_style().declarations;
            const auto authored = authored_style.find(
                canonical_css_property_name(name));
            if (authored != authored_style.end()) value = authored->second;
        }
        // CSSStyleDeclaration.getPropertyValue() returns the empty string for
        // unsupported or unset declarations; it must never leak `undefined`.
        info.GetReturnValue().Set(js_string(info.GetIsolate(), value.c_str()));
    }

    static void style_remove_property(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (info.Length() < 1) return;
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::style));
        auto* node = unwrap_node(info.This());
        if (node == nullptr) return;
        const auto previous = node->attributes.find("style");
        const std::optional<std::string> old_value = previous == node->attributes.end()
            ? std::nullopt
            : std::optional<std::string>(previous->second);
        const auto name = to_utf8(info.GetIsolate(), info[0]);
        if (name.starts_with("--")) {
            if (!valid_custom_property_name(name)) return;
            node->style.mutable_custom_properties().values.erase(name);
            auto& authored = node->mutable_authored_style();
            authored.declarations.erase(name);
            authored.important_declarations.erase(name);
            sync_style_attribute(*node);
            self->recascade_connected_subtree(*node);
            self->notify_custom_element_attribute(
                *node, "style", old_value, node->attributes.at("style"));
            return;
        }
        clear_inline_style(*node, name);
        sync_style_attribute(*node);
        self->recascade_connected_subtree(*node);
        self->notify_custom_element_attribute(
            *node, "style", old_value, node->attributes.at("style"));
    }

    static void get_style_property(
        v8::Local<v8::Name> property,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::style));
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        const auto raw_name = to_utf8(info.GetIsolate(), property);
        const auto name = css_property_idl_name(raw_name);
        if (raw_name.find('-') != std::string::npos) {
            self->record_feature(
                "css",
                "CSSStyleDeclaration.named-property:" + raw_name,
                "supported",
                {},
                "cssom-binding");
        }
        const auto computed = info.Holder()->InternalFieldCount() > 1
            && info.Holder()->GetInternalField(1).As<v8::Value>()->BooleanValue(info.GetIsolate());
        if (computed && !self->is_connected(*node)) {
            info.GetReturnValue().Set(js_string(info.GetIsolate(), ""));
            return;
        }
        if (computed) self->ensure_layout();
        std::string value;
        if (!computed) {
            const auto& authored_style = node->authored_style().declarations;
            const auto authored = authored_style.find(
                canonical_css_property_name(name));
            if (authored != authored_style.end()) value = authored->second;
            else if (const auto expanded = expanded_inline_box_longhand(
                         *node, canonical_css_property_name(name));
                     expanded.has_value()) {
                value = *expanded;
            }
            info.GetReturnValue().Set(js_string(info.GetIsolate(), value.c_str()));
            return;
        }
        const auto format_length = [](const css_length& length) {
            if (length.unit == length_unit::automatic) return std::string {};
            if (length.unit == length_unit::max_content) return std::string{"max-content"};
            if (length.unit == length_unit::min_content) return std::string{"min-content"};
            if (length.unit == length_unit::fit_content) return std::string{"fit-content"};
            std::ostringstream stream;
            if (length.unit == length_unit::percent
                && std::abs(length.pixel_offset) > 0.000001F) {
                stream << "calc(" << length.value << "% "
                    << (length.pixel_offset < 0 ? "- " : "+ ")
                    << std::abs(length.pixel_offset) << "px)";
                return stream.str();
            }
            stream << length.value;
            stream << (length.unit == length_unit::percent ? "%" : "px");
            return stream.str();
        };
        const auto format_pixels = [](float pixels) {
            std::ostringstream stream;
            stream << pixels << "px";
            return stream.str();
        };
        const auto format_computed_length = [&](const css_length& length, float reference) {
            if (length.unit == length_unit::em
                || length.unit == length_unit::rem
                || length.unit == length_unit::viewport_width
                || length.unit == length_unit::viewport_height
                || length.unit == length_unit::viewport_width_capped
                || length.unit == length_unit::viewport_height_capped
                || length.unit == length_unit::viewport_width_floored
                || length.unit == length_unit::viewport_height_floored) {
                return format_pixels(resolve_connected_css_length(
                    *node, length, reference));
            }
            return format_length(length);
        };
        const auto format_box_length = [&](const css_length& length) {
            return length.unit == length_unit::automatic
                ? std::string{"0px"} : format_computed_length(length, 0);
        };
        const auto format_corner_radius = [&](const css_length& horizontal,
                                              const css_length& vertical) {
            const auto horizontal_value = format_computed_length(
                horizontal, node->layout.width);
            const auto vertical_value = format_computed_length(
                vertical, node->layout.height);
            return horizontal_value == vertical_value
                ? horizontal_value
                : horizontal_value + " " + vertical_value;
        };
        const auto compress_corner_axis = [](const std::array<std::string, 4>& values) {
            if (values[0] == values[1] && values[0] == values[2]
                && values[0] == values[3]) {
                return values[0];
            }
            if (values[0] == values[2] && values[1] == values[3]) {
                return values[0] + " " + values[1];
            }
            if (values[1] == values[3]) {
                return values[0] + " " + values[1] + " " + values[2];
            }
            return values[0] + " " + values[1] + " " + values[2] + " " + values[3];
        };
        const auto resolve_transform_origin = [](css_length length, float available) {
            return length.unit == length_unit::percent
                ? available * length.value / 100.0F + length.pixel_offset
                : length.value;
        };
        const auto serialize_color = [](uint32_t color) {
            std::ostringstream stream;
            const auto red = static_cast<unsigned>((color >> 24U) & 0xFFU);
            const auto green = static_cast<unsigned>((color >> 16U) & 0xFFU);
            const auto blue = static_cast<unsigned>((color >> 8U) & 0xFFU);
            const auto alpha = static_cast<unsigned>(color & 0xFFU);
            if (alpha == 0xFFU) {
                stream << "rgb(" << red << ", " << green << ", " << blue << ')';
            } else {
                stream << "rgba(" << red << ", " << green << ", " << blue << ", "
                    << std::setprecision(6) << static_cast<double>(alpha) / 255.0 << ')';
            }
            return stream.str();
        };
        float connected_used_value = 0.0F;
        if (try_connected_used_geometry_value(
                *node,
                canonical_css_property_name(name),
                connected_used_value)) {
            value = format_pixels(connected_used_value);
        } else if (name == "width") {
            value = computed
                ? format_pixels(connected_computed_dimension(
                    *node, node->style.width, true))
                : format_length(node->style.width);
        } else if (name == "height") {
            value = computed
                ? format_pixels(connected_computed_dimension(
                    *node, node->style.height, false))
                : format_length(node->style.height);
        } else if (name == "left" || name == "insetInlineStart") {
            value = format_length(node->style.left);
        } else if (name == "top") {
            value = format_length(node->style.top);
        } else if (name == "right" || name == "insetInlineEnd") {
            value = format_length(node->style.right);
        } else if (name == "bottom") {
            value = format_length(node->style.bottom);
        } else if (name == "minWidth") {
            value = format_computed_length(node->style.min_width, node->layout.width);
        } else if (name == "minHeight") {
            value = format_computed_length(node->style.min_height, node->layout.height);
        } else if (name == "maxWidth") {
            value = node->style.max_width.value == 0
                ? "none"
                : format_computed_length(node->style.max_width, node->layout.width);
        } else if (name == "maxHeight") {
            value = node->style.max_height.value == 0
                ? "none"
                : format_computed_length(node->style.max_height, node->layout.height);
        } else if (name == "display") {
            value = display_mode_name(node->style.display);
        } else if (name == "direction") {
            value = "ltr";
        } else if (name == "position") {
            value = node->style.position == position_mode::absolute ? "absolute"
                : node->style.position == position_mode::fixed ? "fixed"
                : node->style.position == position_mode::relative ? "relative"
                : "static";
        } else if (name == "cssFloat") {
            value = node->style.floating == float_mode::left ? "left"
                : node->style.floating == float_mode::right ? "right" : "none";
        } else if (name == "flexDirection") {
            value = flex_direction_name(node->style);
        } else if (name == "flexFlow") {
            value = flex_direction_name(node->style) + " "
                + flex_wrap_name(node->style);
        } else if (name == "flexGrow") {
            value = serialize_css_number(node->style.flex_grow);
        } else if (name == "flexShrink") {
            value = serialize_css_number(node->style.flex_shrink);
        } else if (name == "flexBasis") {
            value = format_computed_length(node->style.flex_basis, node->layout.width);
        } else if (name == "flexWrap") {
            value = flex_wrap_name(node->style);
        } else if (name == "alignItems") {
            value = node->style.align_items == align_mode::center ? "center"
                : node->style.align_items == align_mode::start ? "flex-start"
                : node->style.align_items == align_mode::end ? "flex-end"
                : node->style.align_items == align_mode::baseline ? "baseline" : "stretch";
        } else if (name == "alignSelf") {
            value = !node->style.align_self_specified ? "auto"
                : node->style.align_self == align_mode::center ? "center"
                : node->style.align_self == align_mode::start ? "flex-start"
                : node->style.align_self == align_mode::end ? "flex-end"
                : node->style.align_self == align_mode::baseline ? "baseline" : "stretch";
        } else if (name == "justifyContent") {
            value = node->style.justify_content == justify_mode::center ? "center"
                : node->style.justify_content == justify_mode::end ? "flex-end"
                : node->style.justify_content == justify_mode::space_between
                    ? "space-between"
                : node->style.justify_content == justify_mode::space_around
                    ? "space-around"
                : node->style.justify_content == justify_mode::space_evenly
                    ? "space-evenly" : "flex-start";
        } else if (name == "gap") {
            value = format_computed_length(node->style.row_gap, node->layout.height)
                + " "
                + format_computed_length(node->style.column_gap, node->layout.width);
        } else if (name == "rowGap") {
            value = format_computed_length(node->style.row_gap, node->layout.height);
        } else if (name == "columnGap") {
            value = format_computed_length(node->style.column_gap, node->layout.width);
        } else if (name == "borderSpacing") {
            value = format_computed_length(
                    node->style.table().border_spacing_horizontal,
                    node->layout.width)
                + " "
                + format_computed_length(
                    node->style.table().border_spacing_vertical,
                    node->layout.height);
        } else if (name == "borderCollapse") {
            value = node->style.table().border_collapsed ? "collapse" : "separate";
        } else if (name == "paddingLeft") {
            value = format_box_length(node->style.padding_left);
        } else if (name == "paddingTop") {
            value = format_box_length(node->style.padding_top);
        } else if (name == "paddingRight") {
            value = format_box_length(node->style.padding_right);
        } else if (name == "paddingBottom") {
            value = format_box_length(node->style.padding_bottom);
        } else if (name == "marginLeft") {
            value = format_box_length(node->style.margin_left);
        } else if (name == "marginTop") {
            value = format_box_length(node->style.margin_top);
        } else if (name == "marginRight") {
            value = format_box_length(node->style.margin_right);
        } else if (name == "marginBottom") {
            value = format_box_length(node->style.margin_bottom);
        } else if (name == "borderTopWidth") {
            value = format_box_length(node->style.border_top_width);
        } else if (name == "borderRightWidth") {
            value = format_box_length(node->style.border_right_width);
        } else if (name == "borderBottomWidth") {
            value = format_box_length(node->style.border_bottom_width);
        } else if (name == "borderLeftWidth") {
            value = format_box_length(node->style.border_left_width);
        } else if (name == "borderTopColor") {
            value = serialize_color(node->style.border_top_rgba);
        } else if (name == "borderRightColor") {
            value = serialize_color(node->style.border_right_rgba);
        } else if (name == "borderBottomColor") {
            value = serialize_color(node->style.border_bottom_rgba);
        } else if (name == "borderLeftColor") {
            value = serialize_color(node->style.border_left_rgba);
        } else if (name == "borderTopLeftRadius") {
            value = format_corner_radius(
                node->style.border_top_left_radius,
                node->style.border_top_left_radius_y());
        } else if (name == "borderTopRightRadius") {
            value = format_corner_radius(
                node->style.border_top_right_radius,
                node->style.border_top_right_radius_y());
        } else if (name == "borderBottomRightRadius") {
            value = format_corner_radius(
                node->style.border_bottom_right_radius,
                node->style.border_bottom_right_radius_y());
        } else if (name == "borderBottomLeftRadius") {
            value = format_corner_radius(
                node->style.border_bottom_left_radius,
                node->style.border_bottom_left_radius_y());
        } else if (name == "gridArea") {
            value = node->style.grid().area_value;
        } else if (name == "gridRow") {
            value = node->style.grid().row_value;
        } else if (name == "gridRowStart") {
            value = node->style.grid().row_start_value;
        } else if (name == "gridRowEnd") {
            value = node->style.grid().row_end_value;
        } else if (name == "gridColumn") {
            value = node->style.grid().column_value;
        } else if (name == "gridColumnStart") {
            value = node->style.grid().column_start_value;
        } else if (name == "gridColumnEnd") {
            value = node->style.grid().column_end_value;
        } else if (name == "verticalAlign") {
            value = node->style.textual().vertical_align.empty()
                ? "baseline"
                : node->style.textual().vertical_align;
        } else if (name == "boxSizing") {
            value = node->style.border_box ? "border-box" : "content-box";
        } else if (name == "borderRadius") {
            const std::array<std::string, 4> horizontal{
                format_computed_length(node->style.border_top_left_radius, node->layout.width),
                format_computed_length(node->style.border_top_right_radius, node->layout.width),
                format_computed_length(node->style.border_bottom_right_radius, node->layout.width),
                format_computed_length(node->style.border_bottom_left_radius, node->layout.width)};
            const std::array<std::string, 4> vertical{
                format_computed_length(node->style.border_top_left_radius_y(), node->layout.height),
                format_computed_length(node->style.border_top_right_radius_y(), node->layout.height),
                format_computed_length(node->style.border_bottom_right_radius_y(), node->layout.height),
                format_computed_length(node->style.border_bottom_left_radius_y(), node->layout.height)};
            value = compress_corner_axis(horizontal);
            if (horizontal != vertical) value += " / " + compress_corner_axis(vertical);
        } else if (name == "boxShadow") {
            if (!node->style.box_shadow_present) {
                value = "none";
            } else {
                const auto color = node->style.box_shadow_rgba;
                std::ostringstream stream;
                stream << node->style.box_shadow_offset_x << "px "
                    << node->style.box_shadow_offset_y << "px "
                    << node->style.box_shadow_blur_radius << "px "
                    << node->style.box_shadow_spread_radius << "px "
                    << serialize_color(color);
                value = stream.str();
            }
        } else if (name == "transformOrigin") {
            value = format_pixels(resolve_transform_origin(
                node->style.transform_origin_x, node->layout.width)) + " "
                + format_pixels(resolve_transform_origin(
                    node->style.transform_origin_y, node->layout.height));
        } else if (name == "transform") {
            if (!node->style.transform_specified
                && !node->transform_animation_active_value()
                && !node->rotation_keyframe_animation_active_value()) {
                value = "none";
            } else {
                constexpr double degrees_to_radians = 0.017453292519943295;
                const auto radians = static_cast<double>(
                    node->painted_transform_rotation_value()) * degrees_to_radians;
                const auto cosine = std::cos(radians);
                const auto sine = std::sin(radians);
                const auto resolve = [](css_length length, float available) {
                    return length.unit == length_unit::percent
                        ? static_cast<double>(available) * length.value / 100.0
                            + length.pixel_offset
                        : static_cast<double>(length.value);
                };
                const auto format_number = [](double number, bool preserve_negative_zero = false) {
                    if (std::abs(number) < 0.000001) {
                        number = preserve_negative_zero && std::signbit(number)
                            ? -0.0
                            : 0.0;
                    }
                    std::ostringstream stream;
                    stream << std::setprecision(8) << number;
                    return stream.str();
                };
                const auto a = cosine * node->painted_transform_scale_x_value();
                const auto b = sine * node->painted_transform_scale_x_value();
                const auto c = -sine * node->painted_transform_scale_y_value();
                const auto d = cosine * node->painted_transform_scale_y_value();
                const auto e = resolve(
                    node->painted_transform_translate_x_value(), node->layout.width);
                const auto f = resolve(
                    node->painted_transform_translate_y_value(), node->layout.height);
                value = "matrix(" + format_number(a) + ", " + format_number(b, true)
                    + ", " + format_number(c) + ", " + format_number(d)
                    + ", " + format_number(e) + ", " + format_number(f) + ")";
            }
        } else if (name == "transition" ) {
            value = node->style.animations().transition_property_value + " "
                + node->style.animations().transition_duration_value + " "
                + node->style.animations().transition_timing_function_value + " "
                + node->style.animations().transition_delay_value;
        } else if (name == "transitionProperty" || name == "transition-property") {
            value = node->style.animations().transition_property_value;
        } else if (name == "transitionDuration" || name == "transition-duration") {
            value = computed
                ? serialize_computed_time_list(node->style.animations().transition_duration_value)
                : node->style.animations().transition_duration_value;
        } else if (name == "transitionDelay" || name == "transition-delay") {
            value = computed
                ? serialize_computed_time_list(node->style.animations().transition_delay_value)
                : node->style.animations().transition_delay_value;
        } else if (name == "transitionTimingFunction"
            || name == "transition-timing-function") {
            value = node->style.animations().transition_timing_function_value;
        } else if (name == "animation") {
            value = node->style.animations().animation_name_value + " "
                + node->style.animations().animation_duration_value + " "
                + node->style.animations().animation_timing_function_value + " "
                + node->style.animations().animation_delay_value + " "
                + node->style.animations().animation_iteration_count_value;
        } else if (name == "animationName" || name == "animation-name") {
            value = node->style.animations().animation_name_value;
        } else if (name == "animationDuration" || name == "animation-duration") {
            value = node->style.animations().animation_duration_value;
        } else if (name == "animationDelay" || name == "animation-delay") {
            value = node->style.animations().animation_delay_value;
        } else if (name == "animationTimingFunction"
            || name == "animation-timing-function") {
            value = node->style.animations().animation_timing_function_value;
        } else if (name == "animationIterationCount"
            || name == "animation-iteration-count") {
            value = node->style.animations().animation_iteration_count_value;
        } else if (name == "zIndex") {
            value = node->style.z_index_auto
                ? "auto" : std::to_string(node->style.z_index);
        } else if (name == "opacity") {
            std::ostringstream stream;
            stream << std::setprecision(6) << node->painted_opacity_value();
            value = stream.str();
        } else if (name == "backgroundImage") {
            value = node->style.background_image().image_value;
        } else if (name == "backgroundRepeat") {
            value = node->style.background_image().repeat;
        } else if (name == "backgroundPosition") {
            value = node->style.background_image().position_x + " "
                + node->style.background_image().position_y;
        } else if (name == "backgroundSize") {
            value = node->style.background_image().size_x + " "
                + node->style.background_image().size_y;
        } else if (name == "background" || name == "backgroundColor") {
            auto background = node->style.background_rgba;
            if (node->style.background_current_color) {
                background = 0U;
                for (auto* current = node; current != nullptr; current = current->parent) {
                    const auto foreground = current->painted_foreground_value();
                    if ((foreground & 0xFFU) != 0U) {
                        background = foreground;
                        break;
                    }
                }
                if ((background & 0xFFU) == 0U) background = 0xD1D4DCFFU;
            }
            value = serialize_color(background);
        } else if (name == "borderTopWidth") {
            value = serialize_css_number(node->style.border_top_width.value) + "px";
        } else if (name == "borderTopStyle") {
            value = node->style.border_top_width.value > 0 ? "solid" : "none";
        } else if (name == "borderTopColor") {
            value = serialize_color(node->style.border_top_rgba);
        } else if (name == "outlineColor") {
            value = serialize_color(node->style.outline_rgba);
        } else if (name == "outlineWidth") {
            value = serialize_css_number(node->style.outline_width.value) + "px";
        } else if (name == "outlineStyle") {
            value = node->style.outline_width.value > 0
                && (node->style.outline_rgba & 0xFFU) != 0U ? "solid" : "none";
        } else if (name == "overflow") {
            const auto [x, y] = computed_overflow(node->style);
            value = x == y ? serialize_overflow_mode(x)
                : serialize_overflow_mode(x) + " " + serialize_overflow_mode(y);
        } else if (name == "overflowX") {
            value = serialize_overflow_mode(computed_overflow(node->style).first);
        } else if (name == "overflowY") {
            value = serialize_overflow_mode(computed_overflow(node->style).second);
        } else if (name == "color") {
            auto color = node->color_animation_active_value()
                ? node->painted_foreground_value()
                : (node->style.foreground_rgba & 0xFFU) != 0U
                    ? node->painted_foreground_value()
                    : 0U;
            for (auto* ancestor = node->parent; (color & 0xFFU) == 0U && ancestor != nullptr;
                 ancestor = ancestor->parent) {
                color = ancestor->color_animation_active_value()
                    ? ancestor->painted_foreground_value()
                    : (ancestor->style.foreground_rgba & 0xFFU) != 0U
                        ? ancestor->painted_foreground_value()
                        : 0U;
            }
            if ((color & 0xFFU) == 0U) color = 0x000000FFU;
            value = serialize_color(color);
        } else if (name == "fontSize") {
            value = serialize_css_number(node->style.font_size >= 0
                ? node->style.font_size : inherited_font_size(*node)) + "px";
        } else if (name == "fontFamily") {
            value = resolved_font_family(*node);
        } else if (name == "webkitFontSmoothing") {
            value = resolved_font_smoothing(*node);
        } else if (name == "fontWeight") {
            value = std::to_string(resolved_font_weight(*node));
        } else if (name == "letterSpacing") {
            value = serialize_css_number(resolved_letter_spacing(*node)) + "px";
        } else if (name == "wordSpacing") {
            value = serialize_css_number(resolved_word_spacing(*node)) + "px";
        } else if (name == "lineHeight") {
            const auto& authored_style = node->authored_style().declarations;
            const auto authored = authored_style.find("line-height");
            if ((node->style.important_property_mask & inline_line_height) == 0U
                && authored != authored_style.end()
                && authored->second == "normal") {
                value = "normal";
                info.GetReturnValue().Set(js_string(info.GetIsolate(), value.c_str()));
                return;
            }
            auto line_height = node->style.line_height;
            for (auto* ancestor = node->parent;
                line_height == -1.0F && ancestor != nullptr;
                ancestor = ancestor->parent) {
                line_height = ancestor->style.line_height;
            }
            if (line_height == -2.0F) {
                value = "normal";
            } else if (line_height < 0) {
                const auto font_size = node->style.font_size >= 0
                    ? node->style.font_size : inherited_font_size(*node);
                line_height = font_size * 1.125F;
                value = serialize_css_number(line_height) + "px";
            } else {
                value = serialize_css_number(line_height) + "px";
            }
        } else if (name == "textAlign") {
            value = node->style.textual().text_align;
        } else if (name == "whiteSpace") {
            value = node->style.textual().white_space;
        } else if (name == "visibility") {
            auto* current = node;
            while (current != nullptr && !current->style.visibility_specified) {
                current = current->parent;
            }
            value = current != nullptr && current->style.visibility_hidden
                ? "hidden" : "visible";
        } else if (name == "pointerEvents") {
            const auto* current = node;
            while (current != nullptr && !current->style.pointer_events_specified) {
                current = current->parent;
            }
            value = current != nullptr && current->style.pointer_events_none ? "none" : "auto";
        } else if (name == "cursor") {
            value = resolved_cursor(*node);
        }
        if (computed && value.empty()
            && inline_style_mask(canonical_css_property_name(name)) == 0U) {
            const auto& authored_style = node->authored_style().declarations;
            const auto authored = authored_style.find(
                canonical_css_property_name(name));
            if (authored != authored_style.end()) value = authored->second;
        }
        info.GetReturnValue().Set(js_string(info.GetIsolate(), value.c_str()));
    }

    static void set_style_property(
        v8::Local<v8::Name> property,
        v8::Local<v8::Value> raw_value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::style));
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        const auto previous = node->attributes.find("style");
        const std::optional<std::string> old_value = previous == node->attributes.end()
            ? std::nullopt
            : std::optional<std::string>(previous->second);
        const auto raw_name = to_utf8(info.GetIsolate(), property);
        const auto name = css_property_idl_name(raw_name);
        if (raw_name.find('-') != std::string::npos) {
            self->record_feature(
                "css",
                "CSSStyleDeclaration.named-property:" + raw_name,
                "supported",
                {},
                "cssom-binding");
        }
        const auto property_mask = inline_style_mask(name);
        const auto has_important_rule =
            (node->style.important_property_mask & property_mask) != 0U;
        const auto value = to_utf8(info.GetIsolate(), raw_value);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (self->resize_profile_active) {
            const auto canonical_name = canonical_css_property_name(name);
            ++self->last_resize_style_property_counts[canonical_name];
            ++self->last_resize_style_target_counts[
                canonical_name + "@" + node->tag + "#" + node->id_attribute
                    + "." + node->class_name + "/position-"
                    + std::to_string(static_cast<uint32_t>(node->style.position))];
        }
#endif
        self->inventory_css_value_functions(value, "cssom-style");
        if (value.empty()) {
            clear_inline_style(*node, name);
            sync_style_attribute(*node);
            self->recascade_connected_subtree(*node);
            self->notify_custom_element_attribute(
                *node, "style", old_value, node->attributes.at("style"));
            return;
        }
        if (!valid_cssom_declaration_value(name, value)) return;
        const auto canonical_name = canonical_css_property_name(name);
        auto& authored = node->mutable_authored_style();
        authored.declarations[canonical_name] = serialize_cssom_specified_value(
            canonical_name,
            value);
        authored.important_declarations.erase(canonical_name);
        if (apply_grid_placement_declaration(node->style, name, value)) {
            // Placement CSSOM is stored as computed tokens; layout consumes the
            // existing numeric grid-column projection when applicable.
            node->style.inline_property_mask |= inline_grid;
        } else if (apply_inset_declaration(node->style, name, value)) {
            node->style.inline_property_mask |=
                inline_left | inline_top | inline_right | inline_bottom;
        } else if (apply_margin_declaration(node->style, name, value)) {
            node->style.inline_property_mask |= inline_margin;
        } else if (apply_padding_declaration(node->style, name, value)) {
            node->style.inline_property_mask |= inline_padding;
        } else if (apply_border_declaration(node->style, name, value)) {
            node->style.inline_property_mask |= inline_border;
        } else if (name == "width") {
            node->style.width = native_document::parse_length(value);
            node->style.inline_property_mask |= inline_width;
        } else if (name == "height") {
            node->style.height = native_document::parse_length(value);
            node->style.inline_property_mask |= inline_height;
        } else if (name == "minWidth") {
            node->style.min_width = native_document::parse_length(value);
            node->style.inline_property_mask |= inline_min_width;
        } else if (name == "minHeight") {
            node->style.min_height = native_document::parse_length(value);
            node->style.inline_property_mask |= inline_min_height;
        } else if (name == "maxWidth") {
            node->style.max_width = value == "none" ? css_length{} : native_document::parse_length(value);
            node->style.inline_property_mask |= inline_max_width;
        } else if (name == "maxHeight") {
            node->style.max_height = value == "none" ? css_length{} : native_document::parse_length(value);
            node->style.inline_property_mask |= inline_max_height;
        } else if (name == "left") {
            node->style.left = parse_inset_length(value);
            node->style.inline_property_mask |= inline_left;
        } else if (name == "top") {
            node->style.top = parse_inset_length(value);
            node->style.inline_property_mask |= inline_top;
        } else if (name == "right") {
            node->style.right = parse_inset_length(value);
            node->style.inline_property_mask |= inline_right;
        } else if (name == "bottom") {
            node->style.bottom = parse_inset_length(value);
            node->style.inline_property_mask |= inline_bottom;
        }
        else if (name == "display") {
            node->style.display = parse_display_mode(value);
            node->style.inline_property_mask |= inline_display;
        } else if (name == "position") {
            node->style.position = value == "absolute" ? position_mode::absolute
                : value == "fixed" ? position_mode::fixed
                : value == "relative" ? position_mode::relative
                : position_mode::normal;
            node->style.inline_property_mask |= inline_position;
        } else if (name == "cssFloat") {
            node->style.floating = value == "left" ? float_mode::left
                : value == "right" ? float_mode::right
                : float_mode::none;
            node->style.inline_property_mask |= inline_float;
        } else if (name == "zIndex") {
            node->style.z_index_auto = value == "auto" || value == "initial"
                || value == "unset";
            node->style.z_index = node->style.z_index_auto
                ? 0 : std::atoi(value.c_str());
            node->style.inline_property_mask |= inline_z_index;
        } else if (name == "flexDirection") {
            node->style.direction = value == "row" || value == "row-reverse"
                ? flex_direction::row : flex_direction::column;
            node->style.flex_reverse = value == "row-reverse" || value == "column-reverse";
            node->style.inline_property_mask |= inline_flex_direction;
        } else if (name == "flexFlow") {
            const auto flow = parse_flex_flow(value);
            node->style.direction = flow.direction;
            node->style.flex_reverse = flow.reverse;
            node->style.flex_wrap = flow.wrap;
            node->style.inline_property_mask |=
                inline_flex_direction | inline_flex_wrap;
        } else if (name == "flexGrow") {
            node->style.flex_grow = static_cast<float>(raw_value->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0));
            node->style.inline_property_mask |= inline_flex_grow;
        } else if (name == "flexShrink") {
            node->style.flex_shrink = std::max(
                0.0F,
                static_cast<float>(raw_value->NumberValue(
                    info.GetIsolate()->GetCurrentContext()).FromMaybe(1)));
            node->style.inline_property_mask |= inline_flex_shrink;
        } else if (name == "flexBasis") {
            node->style.flex_basis = native_document::parse_length(value);
            node->style.inline_property_mask |= inline_flex_basis;
        } else if (name == "flexWrap") {
            node->style.flex_wrap = value == "wrap" || value == "wrap-reverse";
            node->style.inline_property_mask |= inline_flex_wrap;
        } else if (name == "alignItems") {
            node->style.align_items = value == "center" ? align_mode::center
                : value == "flex-start" || value == "start" ? align_mode::start
                : value == "flex-end" || value == "end" ? align_mode::end
                : value == "baseline" || value == "first baseline" ? align_mode::baseline
                : align_mode::stretch;
            node->style.inline_property_mask |= inline_align_items;
        } else if (name == "alignSelf") {
            node->style.align_self_specified = value != "auto";
            node->style.align_self = value == "center" ? align_mode::center
                : value == "flex-start" || value == "start" ? align_mode::start
                : value == "flex-end" || value == "end" ? align_mode::end
                : value == "baseline" || value == "first baseline" ? align_mode::baseline
                : align_mode::stretch;
            node->style.inline_property_mask |= inline_align_self;
        } else if (name == "justifyContent") {
            node->style.justify_content = value == "center" ? justify_mode::center
                : value == "flex-end" || value == "end" ? justify_mode::end
                : value == "space-between" ? justify_mode::space_between
                : value == "space-around" ? justify_mode::space_around
                : value == "space-evenly" ? justify_mode::space_evenly
                : justify_mode::start;
            node->style.inline_property_mask |= inline_justify_content;
        } else if (name == "gap" || name == "rowGap" || name == "columnGap") {
            std::istringstream stream(value);
            std::string first;
            std::string second;
            stream >> first >> second;
            if (name == "gap") {
                node->style.row_gap = native_document::parse_length(first);
                node->style.column_gap = native_document::parse_length(
                    second.empty() ? first : second);
            } else if (name == "rowGap") {
                node->style.row_gap = native_document::parse_length(first);
            } else {
                node->style.column_gap = native_document::parse_length(first);
            }
            node->style.inline_property_mask |= inline_gap;
        } else if (name == "borderSpacing") {
            std::istringstream stream(value);
            std::string horizontal;
            std::string vertical;
            stream >> horizontal >> vertical;
            auto& table = node->style.mutable_table();
            table.border_spacing_horizontal = native_document::parse_length(horizontal);
            table.border_spacing_vertical = native_document::parse_length(
                vertical.empty() ? horizontal : vertical);
            node->style.inline_property_mask |= inline_table_border_model;
        } else if (name == "borderCollapse") {
            node->style.mutable_table().border_collapsed = value == "collapse";
            node->style.inline_property_mask |= inline_table_border_model;
        } else if (name == "boxSizing") {
            node->style.border_box = value == "border-box";
            node->style.inline_property_mask |= inline_box_sizing;
        } else if (name == "borderRadius") {
            apply_corner_radius_declaration(
                "border-radius",
                value,
                node->style);
            node->style.inline_property_mask |= inline_border_radius;
        } else if (name == "boxShadow") {
            auto complete = true;
            apply_box_shadow_value(node->style, value, complete);
            node->style.inline_property_mask |= inline_box_shadow;
        } else if (name == "transform") {
            native_document::parse_transform_translate(
                value,
                node->style.transform_translate_x,
                node->style.transform_translate_y,
                node->style.transform_scale_x,
                node->style.transform_scale_y,
                node->style.transform_rotate_degrees);
            node->style.transform_specified = true;
            node->style.inline_property_mask |= inline_transform;
        } else if (name == "transformOrigin") {
            native_document::parse_transform_origin(
                value,
                node->style.transform_origin_x,
                node->style.transform_origin_y);
            node->style.transform_origin_specified = true;
            node->style.inline_property_mask |= inline_transform_origin;
        } else if (name == "transition") {
            apply_transition_shorthand(node->style, value);
            node->style.inline_property_mask |= inline_transition;
        } else if (name == "transitionProperty") {
            node->style.mutable_animations().transition_property_value = value;
            configure_style_transitions(node->style);
            node->style.inline_property_mask |= inline_transition_property;
        } else if (name == "transitionDuration") {
            node->style.mutable_animations().transition_duration_value = value;
            configure_style_transitions(node->style);
            node->style.inline_property_mask |= inline_transition_duration;
        } else if (name == "transitionDelay") {
            node->style.mutable_animations().transition_delay_value = value;
            configure_style_transitions(node->style);
            node->style.inline_property_mask |= inline_transition_delay;
        } else if (name == "transitionTimingFunction") {
            node->style.mutable_animations().transition_timing_function_value = value;
            configure_style_transitions(node->style);
            node->style.inline_property_mask |= inline_transition_timing;
        } else if (name == "animation") {
            apply_animation_shorthand(node->style, value);
        } else if (name == "animationName") {
            node->style.mutable_animations().animation_name_value = value;
        } else if (name == "animationDuration") {
            node->style.mutable_animations().animation_duration_value = value;
        } else if (name == "animationDelay") {
            node->style.mutable_animations().animation_delay_value = value;
        } else if (name == "animationTimingFunction") {
            node->style.mutable_animations().animation_timing_function_value = value;
        } else if (name == "animationIterationCount") {
            node->style.mutable_animations().animation_iteration_count_value = value;
        } else if (name == "opacity") {
            node->style.opacity = raw_value->IsNullOrUndefined()
                ? 1.0F
                : std::clamp(std::strtof(value.c_str(), nullptr), 0.0F, 1.0F);
            node->style.inline_property_mask |= inline_opacity;
        } else if (name == "background" || name == "backgroundColor") {
            node->style.background_current_color =
                lower_html_name(value) == "currentcolor";
            node->style.background_rgba = node->style.background_current_color
                ? 0U : native_document::parse_color(value);
            node->style.inline_property_mask |= inline_background;
        } else if (name == "overflow") {
            std::istringstream stream(value);
            std::string x;
            std::string y;
            stream >> x >> y;
            node->style.overflow_x = parse_overflow_mode(x);
            node->style.overflow_y = parse_overflow_mode(y.empty() ? x : y);
            refresh_overflow_state(node->style);
            node->style.inline_property_mask |= inline_overflow;
        } else if (name == "visibility") {
            node->style.visibility_hidden = value == "hidden" || value == "collapse";
            node->style.visibility_specified = value != "inherit" && value != "unset";
            node->style.inline_property_mask |= inline_visibility;
        } else if (name == "pointerEvents") {
            node->style.pointer_events_none = value == "none";
            node->style.pointer_events_specified = value != "inherit" && value != "unset";
            node->style.inline_property_mask |= inline_pointer_events;
        } else if (name == "cursor") {
            node->style.mutable_textual().cursor = value;
            node->style.inline_property_mask |= inline_cursor;
        } else if (name == "color") {
            node->style.foreground_rgba = native_document::parse_color(value);
            node->style.inline_property_mask |= inline_color;
        } else if (name == "font") {
            if (value == "inherit" || value == "unset") {
                node->style.font_size = -1;
                node->style.line_height = -1;
                node->style.font_weight = 0;
                node->style.mutable_textual().font_family.clear();
                node->style.inline_property_mask |= inline_font_size | inline_line_height
                    | inline_font_weight | inline_font_family;
            } else if (const auto font = parse_font_shorthand(*node, value); font.has_value()) {
                node->style.font_size = font->font_size;
                node->style.line_height = font->line_height;
                node->style.font_weight = font->font_weight;
                node->style.mutable_textual().font_family =
                    font->font_family;
                node->style.inline_property_mask |= inline_font_size | inline_line_height
                    | inline_font_weight | inline_font_family;
            }
        } else if (name == "fontSize") {
            node->style.font_size = resolved_declared_font_size(*node, value);
            node->style.inline_property_mask |= inline_font_size;
        } else if (name == "fontFamily") {
            node->style.mutable_textual().font_family =
                value == "inherit" || value == "unset"
                ? std::string{} : value;
            node->style.inline_property_mask |= inline_font_family;
        } else if (name == "webkitFontSmoothing") {
            node->style.mutable_textual().font_smoothing =
                value == "inherit" || value == "unset"
                ? std::string{} : value;
            node->style.inline_property_mask |= inline_font_smoothing;
        } else if (name == "fontWeight") {
            node->style.font_weight = value == "inherit" || value == "unset" ? 0
                : value == "bold" ? 700
                : value == "normal" ? 400
                : std::max(1, static_cast<int>(std::lround(
                    native_document::parse_length(value).value)));
            node->style.inline_property_mask |= inline_font_weight;
        } else if (name == "letterSpacing" || name == "wordSpacing") {
            const auto length = native_document::parse_length(value);
            const auto font_size = node->style.font_size >= 0
                ? node->style.font_size : inherited_font_size(*node);
            const auto spacing = value == "normal" ? 0
                : length.unit == length_unit::em ? length.value * font_size
                : length.unit == length_unit::rem ? length.value * 14.0F
                : length.value;
            const auto specified = value != "inherit" && value != "unset";
            if (name == "letterSpacing") {
                node->style.letter_spacing = spacing;
                node->style.letter_spacing_specified = specified;
                node->style.inline_property_mask |= inline_letter_spacing;
            } else {
                node->style.word_spacing = spacing;
                node->style.word_spacing_specified = specified;
                node->style.inline_property_mask |= inline_word_spacing;
            }
        } else if (name == "lineHeight") {
            const auto font_size = node->style.font_size >= 0
                ? node->style.font_size : inherited_font_size(*node);
            node->style.line_height = resolved_declared_line_height(*node, value, font_size);
            node->style.inline_property_mask |= inline_line_height;
        } else if (name == "textAlign") {
            node->style.mutable_textual().text_align = value;
            node->style.inline_property_mask |= inline_text_align;
        } else if (name == "whiteSpace") {
            node->style.mutable_textual().white_space = value;
            node->style.inline_property_mask |= inline_white_space;
        }
        self->recompute_inline_font_relative_metrics(*node);
        sync_style_attribute(*node);
        self->notify_custom_element_attribute(
            *node, "style", old_value, node->attributes.at("style"));
        if (has_important_rule) self->apply_css_rules(*node);
        else {
            self->configure_style_keyframe_animation(*node);
            self->document.update_style_animations(*node);
            self->detect_css_compositions(*node);
            self->mark_cssom_layout_dirty(*node, canonical_name);
        }
    }

    static void get_computed_style(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_node(info[0].As<v8::Object>())
            : nullptr;
        if (node != nullptr) info.GetReturnValue().Set(self->wrap_computed_style(*node));
    }
