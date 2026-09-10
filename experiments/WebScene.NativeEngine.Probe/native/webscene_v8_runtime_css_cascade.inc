    void apply_scrollbar_css_declaration(
        dom_node& node,
        int pseudo_kind,
        const css_declaration& declaration)
    {
        auto& style = node.style;
        const auto contains_variable = declaration.value.find("var(") != std::string::npos;
        auto resolved_value = std::string{};
        const auto& value = contains_variable
            ? (resolved_value = resolve_css_value(node, declaration.value))
            : declaration.value;
        if (value.empty() && contains_variable) return;
        const auto lower = lower_html_name(trim_css(value));
        auto& scrollbar = style.mutable_scrollbar();
        if (pseudo_kind == 3) {
            if (declaration.name == "display") {
                if (style.scrollbar_visibility_important && !declaration.important) return;
                style.scrollbar_hidden = lower == "none";
                style.scrollbar_visibility_important = declaration.important;
            } else if (declaration.name == "width") {
                const auto length = native_document::parse_length(value);
                if (length.unit == length_unit::pixels) {
                    scrollbar.width = std::max(0.0F, length.value);
                    scrollbar.overlay_inset = 0;
                }
            } else if (declaration.name == "height") {
                const auto length = native_document::parse_length(value);
                if (length.unit == length_unit::pixels) {
                    scrollbar.height = std::max(0.0F, length.value);
                    scrollbar.overlay_inset = 0;
                }
            }
            return;
        }
        if (pseudo_kind == 4) {
            if (declaration.name == "background" || declaration.name == "background-color") {
                scrollbar.thumb_rgba = lower == "initial" ? 0U
                    : native_document::parse_color(value);
            } else if (declaration.name == "border-radius") {
                scrollbar.thumb_radius = std::max(
                    0.0F, native_document::parse_length(value).value);
            } else if (declaration.name == "border" || declaration.name == "border-width") {
                std::istringstream stream(value);
                for (std::string token; stream >> token;) {
                    if (!token.empty() && (std::isdigit(static_cast<unsigned char>(token.front()))
                            || token.front() == '.')) {
                        scrollbar.thumb_border_width = std::max(
                            0.0F, native_document::parse_length(token).value);
                        break;
                    }
                }
            }
            return;
        }
        if (pseudo_kind == 5) {
            if (declaration.name == "background" || declaration.name == "background-color") {
                scrollbar.track_rgba = lower == "initial" ? 0U
                    : native_document::parse_color(value);
            } else if (declaration.name == "border-radius") {
                scrollbar.track_radius = std::max(
                    0.0F, native_document::parse_length(value).value);
            }
        }
    }

    static bool layout_length_equal(css_length left, css_length right) noexcept
    {
        return left.value == right.value
            && left.unit == right.unit
            && left.pixel_offset == right.pixel_offset;
    }

    static bool is_explicit_css_color_token(std::string_view value,uint32_t parsed) noexcept
    { return css::is_explicit_color_token(value,parsed); }

    static bool pseudo_layout_equal(
        const node_style::pseudo_element& left,
        const node_style::pseudo_element& right) noexcept
    {
        const std::array left_lengths{
            left.width, left.height, left.left, left.top, left.right, left.bottom,
            left.margin_left, left.margin_top, left.margin_right, left.margin_bottom,
            left.border_left_width, left.border_top_width,
            left.border_right_width, left.border_bottom_width};
        const std::array right_lengths{
            right.width, right.height, right.left, right.top, right.right, right.bottom,
            right.margin_left, right.margin_top, right.margin_right, right.margin_bottom,
            right.border_left_width, right.border_top_width,
            right.border_right_width, right.border_bottom_width};
        for (size_t index = 0; index < left_lengths.size(); ++index) {
            if (!layout_length_equal(left_lengths[index], right_lengths[index])) return false;
        }
        return left.display == right.display
            && left.position == right.position
            && left.align_self == right.align_self
            && left.font_size == right.font_size
            && left.line_height == right.line_height
            && left.content == right.content
            && left.generated == right.generated
            && left.display_none == right.display_none
            && left.align_self_specified == right.align_self_specified
            && left.border_box == right.border_box;
    }

    static bool grid_layout_equal(
        const node_style::grid_data& left,
        const node_style::grid_data& right) noexcept
    {
        const auto tracks_equal = [](const auto& left_tracks, const auto& right_tracks) {
            if (left_tracks.size() != right_tracks.size()) return false;
            for (size_t index = 0; index < left_tracks.size(); ++index) {
                const auto& left_track = left_tracks[index];
                const auto& right_track = right_tracks[index];
                if (!layout_length_equal(left_track.minimum, right_track.minimum)
                    || !layout_length_equal(left_track.maximum, right_track.maximum)
                    || left_track.fraction != right_track.fraction
                    || left_track.kind != right_track.kind) {
                    return false;
                }
            }
            return true;
        };
        return tracks_equal(left.template_columns, right.template_columns)
            && tracks_equal(left.template_rows, right.template_rows)
            && tracks_equal(left.auto_columns, right.auto_columns)
            && left.subgrid_columns == right.subgrid_columns
            && left.two_columns == right.two_columns
            && left.auto_flow_column == right.auto_flow_column
            && left.fractional_rows == right.fractional_rows
            && left.span_all == right.span_all
            && left.column_start == right.column_start
            && left.area_value == right.area_value
            && left.row_value == right.row_value
            && left.row_start_value == right.row_start_value
            && left.row_end_value == right.row_end_value
            && left.column_value == right.column_value
            && left.column_start_value == right.column_start_value
            && left.column_end_value == right.column_end_value;
    }

    static bool computed_layout_style_equal(
        const node_style& left,
        const node_style& right) noexcept
    {
        const std::array left_lengths{
            left.width, left.height, left.min_width, left.min_height,
            left.max_width, left.max_height, left.left, left.top, left.right, left.bottom,
            left.padding_left, left.padding_top, left.padding_right, left.padding_bottom,
            left.margin_left, left.margin_top, left.margin_right, left.margin_bottom,
            left.row_gap, left.column_gap,
            left.border_left_width, left.border_top_width,
            left.border_right_width, left.border_bottom_width,
            left.flex_basis};
        const std::array right_lengths{
            right.width, right.height, right.min_width, right.min_height,
            right.max_width, right.max_height, right.left, right.top, right.right, right.bottom,
            right.padding_left, right.padding_top, right.padding_right, right.padding_bottom,
            right.margin_left, right.margin_top, right.margin_right, right.margin_bottom,
            right.row_gap, right.column_gap,
            right.border_left_width, right.border_top_width,
            right.border_right_width, right.border_bottom_width,
            right.flex_basis};
        for (size_t index = 0; index < left_lengths.size(); ++index) {
            if (!layout_length_equal(left_lengths[index], right_lengths[index])) return false;
        }
        const auto& left_text = left.textual();
        const auto& right_text = right.textual();
        const auto& left_table = left.table();
        const auto& right_table = right.table();
        return left.display == right.display
            && left.position == right.position
            && left.floating == right.floating
            && left.direction == right.direction
            && left.align_items == right.align_items
            && left.align_self == right.align_self
            && left.justify_content == right.justify_content
            && left.overflow_x == right.overflow_x
            && left.overflow_y == right.overflow_y
            && left.flex_grow == right.flex_grow
            && left.flex_shrink == right.flex_shrink
            && left.font_size == right.font_size
            && left.line_height == right.line_height
            && left.font_weight == right.font_weight
            && left.letter_spacing == right.letter_spacing
            && left.word_spacing == right.word_spacing
            && left.letter_spacing_specified == right.letter_spacing_specified
            && left.word_spacing_specified == right.word_spacing_specified
            && left.clip == right.clip
            && left.scroll_x_enabled == right.scroll_x_enabled
            && left.scroll_y_enabled == right.scroll_y_enabled
            && left.scrollbar_hidden == right.scrollbar_hidden
            && left.flex_wrap == right.flex_wrap
            && left.flex_reverse == right.flex_reverse
            && left.align_self_specified == right.align_self_specified
            && left.border_box == right.border_box
            && left.margin_left_auto == right.margin_left_auto
            && left.margin_top_auto == right.margin_top_auto
            && left.margin_right_auto == right.margin_right_auto
            && left.margin_bottom_auto == right.margin_bottom_auto
            && left.table_layout_fixed == right.table_layout_fixed
            && left_text.font_family == right_text.font_family
            && left_text.text_align == right_text.text_align
            && left_text.vertical_align == right_text.vertical_align
            && left_text.text_transform == right_text.text_transform
            && left_text.white_space == right_text.white_space
            && left_text.list_style_position == right_text.list_style_position
            && left_text.list_style_type == right_text.list_style_type
            && layout_length_equal(
                left.transform_translate_x,
                right.transform_translate_x)
            && layout_length_equal(
                left.transform_translate_y,
                right.transform_translate_y)
            && layout_length_equal(
                left.transform_origin_x,
                right.transform_origin_x)
            && layout_length_equal(
                left.transform_origin_y,
                right.transform_origin_y)
            && left.transform_scale_x == right.transform_scale_x
            && left.transform_scale_y == right.transform_scale_y
            && left.transform_rotate_degrees == right.transform_rotate_degrees
            && layout_length_equal(
                left_table.border_spacing_horizontal,
                right_table.border_spacing_horizontal)
            && layout_length_equal(
                left_table.border_spacing_vertical,
                right_table.border_spacing_vertical)
            && left_table.border_collapsed == right_table.border_collapsed
            && grid_layout_equal(left.grid(), right.grid())
            && pseudo_layout_equal(left.before_pseudo(), right.before_pseudo())
            && pseudo_layout_equal(left.after_pseudo(), right.after_pseudo());
    }

    static void append_utf8_codepoint(std::string& result, uint32_t codepoint)
    { css::append_utf8_codepoint(result,codepoint); }

    static std::string decode_css_content(std::string value)
    { return css::decode_content(std::move(value)); }

    template<typename Style>
    static bool apply_corner_radius_declaration(std::string_view name,const std::string& value,Style& style)
    { return css::apply_corner_radius_declaration(name,value,style); }

    static bool apply_box_shadow_value(node_style& style,const std::string& value,bool& complete)
    { return css::apply_box_shadow_value(style,value,complete); }

    void apply_pseudo_css_declaration(
        dom_node& node,
        node_style::pseudo_element& pseudo,
        const css_declaration& declaration)
    {
        feature_decision decision(
            *this,
            "css",
            "pseudo-property:" + declaration.name,
            "pseudo-style-application");
        const auto contains_variable = declaration.value.find("var(") != std::string::npos;
        auto resolved_value = std::string{};
        const auto& value = contains_variable
            ? (resolved_value = resolve_css_value(node, declaration.value))
            : declaration.value;
        if (value.empty() && contains_variable) {
            decision.classification = "invalid-authoring";
            decision.semantic_slice = "unresolved custom property at computed-value time";
            return;
        }
        const auto& name = declaration.name;
        if (contains_variable && is_paint_property(name)) {
            record_composition("css-custom-property-to-paint", node.id);
        }
        const auto result = css::apply_pseudo_value(
            pseudo, node.style.foreground_rgba, name, value);
        decision.classification = result.classification;
        decision.semantic_slice = result.semantic_slice;
    }

    static std::optional<std::string> first_css_url(const std::string& value)
    { return css::first_css_url(value); }

    static std::string svg_view_box(const std::string& markup)
    {
        const auto lower = lower_html_name(markup);
        const auto svg = lower.find("<svg");
        if (svg == std::string::npos) return {};
        const auto tag_end = lower.find('>', svg + 4U);
        if (tag_end == std::string::npos) return {};
        const auto view_box = lower.find("viewbox", svg + 4U);
        if (view_box != std::string::npos && view_box < tag_end) {
            const auto equals = markup.find('=', view_box + 7U);
            if (equals != std::string::npos && equals < tag_end) {
                auto value_start = equals + 1U;
                while (value_start < tag_end
                    && std::isspace(static_cast<unsigned char>(markup[value_start]))) {
                    ++value_start;
                }
                if (value_start < tag_end) {
                    const auto quote = markup[value_start];
                    if (quote == '\'' || quote == '"') {
                        const auto value_end = markup.find(quote, value_start + 1U);
                        if (value_end != std::string::npos && value_end <= tag_end) {
                            return markup.substr(
                                value_start + 1U,
                                value_end - value_start - 1U);
                        }
                    }
                }
            }
        }

        // SVG 2 permits an intrinsic viewport to be supplied by numeric width
        // and height attributes without a viewBox. Convert that bounded form
        // to the scene's canonical viewBox representation so ordinary bundled
        // CSS artwork (checker patterns, badges, and icons) retains its aspect
        // ratio instead of being discarded.
        const auto opening = markup.substr(svg, tag_end - svg + 1U);
        const auto parse_dimension = [](std::string value) -> std::optional<float> {
            value = trim_css(value);
            if (value.ends_with("px")) value.resize(value.size() - 2U);
            if (value.empty()) return std::nullopt;
            char* end = nullptr;
            const auto parsed = std::strtof(value.c_str(), &end);
            if (end == value.c_str() || *end != '\0' || !std::isfinite(parsed)
                || parsed <= 0.0F) {
                return std::nullopt;
            }
            return parsed;
        };
        const auto width = parse_dimension(extract_html_attribute(opening, "width"));
        const auto height = parse_dimension(extract_html_attribute(opening, "height"));
        if (!width.has_value() || !height.has_value()) return {};
        std::ostringstream synthesized;
        synthesized << "0 0 " << *width << ' ' << *height;
        return std::move(synthesized).str();
    }

    static void apply_background_position(node_style& style,const std::string& value)
    { css::apply_background_position(style,value); }

    static std::vector<std::string> split_css_component_list(std::string_view value,char separator)
    { return css::split_css_component_list(value,separator); }

    static std::vector<std::string> split_transition_tokens(std::string_view value)
    { return css::split_value_tokens(value); }
    static uint8_t margin_sides(std::string_view name) { return css::margin_sides(name); }
    static bool apply_margin_declaration(node_style& style,std::string_view name,
        const std::string& value,uint8_t allowed=15U)
    { return css::apply_margin_declaration(style,name,value,allowed); }
    static bool apply_padding_declaration(node_style& style,std::string_view name,const std::string& value)
    { return css::apply_padding_declaration(style,name,value); }

    static css_length parse_inset_length(const std::string& value) { return css::parse_inset_length(value); }
    static bool apply_inset_declaration(node_style& style,std::string_view name,const std::string& value)
    { return css::apply_inset_declaration(style,name,value); }
    static bool apply_border_declaration(node_style& style,std::string_view name,const std::string& value)
    { return css::apply_border_declaration(style,name,value); }

    static bool is_css_time(std::string_view value) { return css::is_css_time(value); }
    static float parse_css_time_ms(std::string value) { return css::parse_css_time_ms(std::move(value)); }
    static void parse_transition_timing(const std::string& value,node_style::transition_timing& timing)
    { css::parse_transition_timing(value,timing); }

    static std::string serialize_computed_time_list(const std::string& value)
    {
        const auto times = split_css_component_list(value, ',');
        std::string result;
        for (const auto& time : times) {
            if (!result.empty()) result += ", ";
            result += serialize_css_number(
                static_cast<double>(parse_css_time_ms(time)) / 1000.0) + "s";
        }
        return result.empty() ? std::string{"0s"} : result;
    }

    static void configure_style_transitions(node_style& style) { css::configure_style_transitions(style); }
    static void apply_transition_shorthand(node_style& style,const std::string& value)
    { css::apply_transition_shorthand(style,value); }

    static bool apply_grid_placement_declaration(node_style& style,std::string_view name,const std::string& value)
    { return css::apply_grid_placement_declaration(style,name,value); }

    static void apply_animation_shorthand(node_style& style,const std::string& value)
    { css::apply_animation_shorthand(style,value); }

    void configure_style_keyframe_animation(dom_node& node)
    {
        css::configure_keyframes(node.style,opacity_keyframes);
        if (!node.style.animations().opacity_keyframe_animation_signature.empty())
            record_composition("css-opacity-keyframe-animation",node.id);
        if (!node.style.animations().rotation_keyframe_animation_signature.empty())
            record_composition("css-rotation-keyframe-animation",node.id);
    }

    void recompute_cascaded_line_height(dom_node& node, const std::vector<const css_rule*>& rules)
    {
        // Relative lengths compute against the final font-size, irrespective
        // of declaration order. Unlike a number, an em/% value then inherits
        // as a length. Include font shorthands because they reset line-height.
        std::optional<std::string> winning_value;
        bool winning_important = false;
        const auto consider = [&](const css_declaration& declaration) {
            if (declaration.name != "line-height" && declaration.name != "font") return;
            if (winning_important && !declaration.important) return;
            auto value = resolve_css_value(node, declaration.value);
            if (value.empty()) return;
            if (declaration.name == "font") {
                if (value == "inherit" || value == "unset") value = "inherit";
                else if (const auto font = parse_font_shorthand(node, value); font.has_value())
                    value = font->line_height_token;
                else return;
            }
            winning_value = std::move(value);
            winning_important = declaration.important;
        };
        for (const auto* rule : rules)
            for (const auto& declaration : rule->declarations()) consider(declaration);
        for (const auto& [name, value] : node.authored_style().declarations)
            consider({name, value, node.authored_style().important_declarations.contains(name)});
        if (winning_value.has_value()) {
            const auto font_size = node.style.font_size >= 0
                ? node.style.font_size : inherited_font_size(node);
            node.style.line_height = resolved_declared_line_height(node, *winning_value, font_size);
        }
    }

    void apply_css_declaration(dom_node& node, const css_declaration& declaration, bool inline_origin = false)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (profile_css) {
            ++css_profile_declaration_applications;
        }
#endif
        const auto& name = declaration.name;
        if (name == "-moz-transform" || name == "-webkit-transform"
            || name == "grid-gap" || name == "grid-row-gap" || name == "grid-column-gap") {
            feature_decision alias_decision(
                *this,
                "css",
                "property:" + name,
                "style-application");
            auto normalized = declaration;
            normalized.name = name.starts_with("grid-")
                ? canonical_css_property_name(name) : "transform";
            apply_css_declaration(node, normalized, inline_origin);
            return;
        }
        if (name.starts_with("--")) {
            css::apply_custom_property(node,declaration);
            record_feature("css","custom-property","supported",{},"style-application");
            return;
        }
        feature_decision decision(
            *this,
            "css",
            "property:" + name,
            "style-application");
        const auto contains_variable = declaration.value.find("var(") != std::string::npos;
        auto resolved_value = std::string{};
        const auto& value = contains_variable
            ? (resolved_value = resolve_css_value(node, declaration.value))
            : declaration.value;
        // An unresolved var() without a fallback invalidates the declaration;
        // it does not become numeric zero or an empty keyword.
        if (value.empty() && contains_variable) {
            decision.classification = "invalid-authoring";
            decision.semantic_slice = "unresolved custom property at computed-value time";
            return;
        }
        if (contains_variable && is_paint_property(name)) {
            record_composition("css-custom-property-to-paint", node.id);
        }
        if (margin_sides(name)!=0U) {
            css::apply_margin(node,declaration,value,inline_origin);
            return;
        }
        const auto property_mask = inline_style_mask(name);
        if (declaration.important && property_mask != 0U) {
            node.style.important_property_mask |= property_mask;
        }
        const auto is_inline = [&](uint64_t property) {
            return !declaration.important
                && ((node.style.inline_property_mask | node.style.important_property_mask)
                    & property) != 0U;
        };
        if (name == "all") {
            if (value != "unset" || declaration.important) {
                decision.classification = "unsupported";
                decision.semantic_slice =
                    "non-important unset across modeled properties, excluding custom properties";
                return;
            }

            css::apply_all_unset(node);
            decision.classification = "partially-supported";
            decision.semantic_slice =
                "non-important unset across modeled properties, excluding custom properties";
            return;
        }
        if (name == "color" && !is_inline(inline_color)
            && (value == "inherit" || value == "unset" || value == "initial")) {
            node.style.foreground_rgba = value == "initial" ? 0x000000FFU : 0U;
            return;
        }
        if ((name == "background" || name == "background-color")
            && !is_inline(inline_background)
            && (value == "unset" || value == "initial" || value == "inherit")) {
            const auto explicit_document_element = node.tag == "html"
                && node.parent == &document.body();
            node.style.background_rgba = value == "inherit" && node.parent != nullptr
                && !explicit_document_element
                ? node.parent->style.background_rgba
                : 0U;
            node.style.background_current_color = false;
            return;
        }
        const auto parse_declared_color = [&](const std::string& color_value) {
            return native_document::parse_color(color_value);
        };
        const auto parse_border = [&] (
            css_length& width,
            uint32_t& color,
            bool& current_color) {
            // A border shorthand resets its omitted color longhand to the
            // initial currentColor value. Keep that dependency deferred.
            color = 0;
            current_color = true;
            // Functional colors are single CSS components. Splitting their
            // arguments at spaces turns color-mix percentages into border widths.
            for (const auto& token : split_transition_tokens(value)) {
                if (token == "none") {
                    width = {};
                    color = 0;
                } else if (token == "thin") {
                    width = {1, length_unit::pixels};
                } else if (token == "medium") {
                    width = {3, length_unit::pixels};
                } else if (token == "thick") {
                    width = {5, length_unit::pixels};
                } else if (std::isdigit(static_cast<unsigned char>(token.front()))
                    || token.front() == '.' || token.front() == '-') {
                    width = native_document::parse_length(token);
                } else {
                    const auto parsed = parse_declared_color(token);
                    if (is_explicit_css_color_token(token, parsed)) {
                        color = parsed;
                        current_color = lower_html_name(token) == "currentcolor";
                    }
                }
            }
        };
        const auto parse_four_border_values = [&](bool colors) {
            if (colors) {
                // Functional colors contain spaces after commas and therefore
                // form one CSS component value even though a whitespace stream
                // would split them into several apparent border sides.
                const auto single = parse_declared_color(value);
                if (single != 0U || value == "transparent"
                    || lower_html_name(value) == "currentcolor") {
                    node.style.border_top_rgba = single;
                    node.style.border_right_rgba = single;
                    node.style.border_bottom_rgba = single;
                    node.style.border_left_rgba = single;
                    const auto current = lower_html_name(value) == "currentcolor";
                    node.style.border_top_current_color = current;
                    node.style.border_right_current_color = current;
                    node.style.border_bottom_current_color = current;
                    node.style.border_left_current_color = current;
                    return;
                }
            }
            std::vector<std::string> values;
            std::istringstream stream(value);
            for (std::string token; stream >> token;) values.push_back(std::move(token));
            if (values.empty()) return;
            const auto top = values[0];
            const auto right = values.size() > 1 ? values[1] : values[0];
            const auto bottom = values.size() > 2 ? values[2] : values[0];
            const auto left = values.size() > 3 ? values[3]
                : values.size() > 1 ? values[1] : values[0];
            if (colors) {
                node.style.border_top_rgba = parse_declared_color(top);
                node.style.border_right_rgba = parse_declared_color(right);
                node.style.border_bottom_rgba = parse_declared_color(bottom);
                node.style.border_left_rgba = parse_declared_color(left);
                node.style.border_top_current_color =
                    lower_html_name(top) == "currentcolor";
                node.style.border_right_current_color =
                    lower_html_name(right) == "currentcolor";
                node.style.border_bottom_current_color =
                    lower_html_name(bottom) == "currentcolor";
                node.style.border_left_current_color =
                    lower_html_name(left) == "currentcolor";
            } else {
                node.style.border_top_width = native_document::parse_length(top);
                node.style.border_right_width = native_document::parse_length(right);
                node.style.border_bottom_width = native_document::parse_length(bottom);
                node.style.border_left_width = native_document::parse_length(left);
            }
        };
        if (name == "border-radius"
            || name == "border-top-left-radius"
            || name == "border-top-right-radius"
            || name == "border-bottom-right-radius"
            || name == "border-bottom-left-radius"
            || name == "border-start-start-radius"
            || name == "border-start-end-radius"
            || name == "border-end-end-radius"
            || name == "border-end-start-radius") {
            if (!is_inline(inline_border_radius)) {
                apply_corner_radius_declaration(
                    name,
                    value,
                    node.style);
            }
            return;
        } else if (name == "transform") {
            if (!is_inline(inline_transform)) {
                native_document::parse_transform_translate(
                    value,
                    node.style.transform_translate_x,
                    node.style.transform_translate_y,
                    node.style.transform_scale_x,
                    node.style.transform_scale_y,
                    node.style.transform_rotate_degrees);
                node.style.transform_specified = true;
                node.style.transform_stacking_context =
                    lower_html_name(value) != "none";
            }
            return;
        } else if (name == "transform-origin") {
            if (!is_inline(inline_transform_origin)) {
                native_document::parse_transform_origin(
                    value,
                    node.style.transform_origin_x,
                    node.style.transform_origin_y);
                node.style.transform_origin_specified = true;
            }
            return;
        } else if (name == "transition") {
            apply_transition_shorthand(node.style, value);
            return;
        } else if (name == "transition-property") {
            node.style.mutable_animations().transition_property_value = value;
            configure_style_transitions(node.style);
            return;
        } else if (name == "transition-duration") {
            node.style.mutable_animations().transition_duration_value = value;
            configure_style_transitions(node.style);
            return;
        } else if (name == "transition-delay") {
            node.style.mutable_animations().transition_delay_value = value;
            configure_style_transitions(node.style);
            return;
        } else if (name == "transition-timing-function") {
            node.style.mutable_animations().transition_timing_function_value = value;
            configure_style_transitions(node.style);
            return;
        } else if (name == "animation") {
            apply_animation_shorthand(node.style, value);
            decision.classification = "partially-supported";
            decision.semantic_slice = "first animation; opacity @keyframes";
            return;
        } else if (name == "animation-name") {
            node.style.mutable_animations().animation_name_value = value;
            decision.classification = "partially-supported";
            decision.semantic_slice = "first animation; opacity @keyframes";
            return;
        } else if (name == "animation-duration") {
            node.style.mutable_animations().animation_duration_value = value;
            return;
        } else if (name == "animation-delay") {
            node.style.mutable_animations().animation_delay_value = value;
            return;
        } else if (name == "animation-timing-function") {
            node.style.mutable_animations().animation_timing_function_value = value;
            return;
        } else if (name == "animation-iteration-count") {
            node.style.mutable_animations().animation_iteration_count_value = value;
            return;
        } else if (inline_style_mask(name) == inline_border
            && is_inline(inline_border)) {
            return;
        } else if (name == "border") {
            parse_border(node.style.border_top_width, node.style.border_top_rgba,
                node.style.border_top_current_color);
            parse_border(node.style.border_right_width, node.style.border_right_rgba,
                node.style.border_right_current_color);
            parse_border(node.style.border_bottom_width, node.style.border_bottom_rgba,
                node.style.border_bottom_current_color);
            parse_border(node.style.border_left_width, node.style.border_left_rgba,
                node.style.border_left_current_color);
            return;
        } else if (name == "border-top" || name == "border-block-start") {
            parse_border(node.style.border_top_width, node.style.border_top_rgba,
                node.style.border_top_current_color);
            return;
        } else if (name == "border-right" || name == "border-inline-end") {
            parse_border(node.style.border_right_width, node.style.border_right_rgba,
                node.style.border_right_current_color);
            return;
        } else if (name == "border-bottom" || name == "border-block-end") {
            parse_border(node.style.border_bottom_width, node.style.border_bottom_rgba,
                node.style.border_bottom_current_color);
            return;
        } else if (name == "border-left" || name == "border-inline-start") {
            parse_border(node.style.border_left_width, node.style.border_left_rgba,
                node.style.border_left_current_color);
            return;
        } else if (name == "border-inline") {
            parse_border(node.style.border_left_width, node.style.border_left_rgba,
                node.style.border_left_current_color);
            parse_border(node.style.border_right_width, node.style.border_right_rgba,
                node.style.border_right_current_color);
            return;
        } else if (name == "border-block") {
            parse_border(node.style.border_top_width, node.style.border_top_rgba,
                node.style.border_top_current_color);
            parse_border(node.style.border_bottom_width, node.style.border_bottom_rgba,
                node.style.border_bottom_current_color);
            return;
        } else if (name == "border-color") {
            parse_four_border_values(true);
            return;
        } else if (name == "border-width") {
            parse_four_border_values(false);
            return;
        } else if (name == "outline") {
            node.style.outline_width = {};
            node.style.outline_rgba = 0;
            auto ignored_current_color = false;
            parse_border(
                node.style.outline_width,
                node.style.outline_rgba,
                ignored_current_color);
            return;
        } else if (name == "outline-width") {
            node.style.outline_width = native_document::parse_length(value);
            return;
        } else if (name == "outline-color") {
            node.style.outline_rgba = native_document::parse_color(value);
            return;
        } else if (name == "border-style" && value == "none") {
            node.style.border_top_width = {};
            node.style.border_right_width = {};
            node.style.border_bottom_width = {};
            node.style.border_left_width = {};
            return;
        } else if (name == "border-top-color"
            || name == "border-block-start-color") {
            node.style.border_top_rgba = parse_declared_color(value);
            node.style.border_top_current_color =
                lower_html_name(value) == "currentcolor";
            return;
        } else if (name == "border-right-color"
            || name == "border-inline-end-color") {
            node.style.border_right_rgba = parse_declared_color(value);
            node.style.border_right_current_color =
                lower_html_name(value) == "currentcolor";
            return;
        } else if (name == "border-bottom-color"
            || name == "border-block-end-color") {
            node.style.border_bottom_rgba = parse_declared_color(value);
            node.style.border_bottom_current_color =
                lower_html_name(value) == "currentcolor";
            return;
        } else if (name == "border-left-color"
            || name == "border-inline-start-color") {
            node.style.border_left_rgba = parse_declared_color(value);
            node.style.border_left_current_color =
                lower_html_name(value) == "currentcolor";
            return;
        } else if (name == "border-inline-color") {
            node.style.border_left_rgba = parse_declared_color(value);
            node.style.border_right_rgba = node.style.border_left_rgba;
            node.style.border_left_current_color =
                lower_html_name(value) == "currentcolor";
            node.style.border_right_current_color =
                node.style.border_left_current_color;
            return;
        } else if (name == "border-block-color") {
            node.style.border_top_rgba = parse_declared_color(value);
            node.style.border_bottom_rgba = node.style.border_top_rgba;
            node.style.border_top_current_color =
                lower_html_name(value) == "currentcolor";
            node.style.border_bottom_current_color =
                node.style.border_top_current_color;
            return;
        } else if (name == "border-top-width"
            || name == "border-block-start-width") {
            node.style.border_top_width = native_document::parse_length(value);
            return;
        } else if (name == "border-right-width"
            || name == "border-inline-end-width") {
            node.style.border_right_width = native_document::parse_length(value);
            return;
        } else if (name == "border-bottom-width"
            || name == "border-block-end-width") {
            node.style.border_bottom_width = native_document::parse_length(value);
            return;
        } else if (name == "border-left-width"
            || name == "border-inline-start-width") {
            node.style.border_left_width = native_document::parse_length(value);
            return;
        } else if (name == "border-inline-width") {
            node.style.border_left_width = native_document::parse_length(value);
            node.style.border_right_width = node.style.border_left_width;
            return;
        } else if (name == "border-block-width") {
            node.style.border_top_width = native_document::parse_length(value);
            node.style.border_bottom_width = node.style.border_top_width;
            return;
        }
        if (css::apply_box_metrics(node,name,value,is_inline)) {
        } else if (css::apply_structure_value(document,node,name,value,decision,is_inline)) {
        } else if (css::apply_grid_value(node,name,value,decision,is_inline)) {
        } else if (css::apply_flex_value(node,name,value,is_inline)) {
        } else if (css::apply_paint_value(node,name,value,decision,is_inline,
            [this](const std::string& url,std::string& markup,std::string& resolved,std::string& view_box) {
                if(!load_text_resource(url,current_base_address(),WEBSCENE_RESOURCE_IMAGE,markup,resolved))
                    return false;
                view_box=svg_view_box(markup);
                return true;
            })) {
        } else if ((name == "overflow" || name == "overflow-x" || name == "overflow-y")
            && !is_inline(inline_overflow)) {
            const auto explicit_document_element = node.tag == "html"
                && node.parent == &document.body();
            if (value == "inherit" && node.parent != nullptr
                && !explicit_document_element) {
                if (name == "overflow") {
                    node.style.overflow_x = node.parent->style.overflow_x;
                    node.style.overflow_y = node.parent->style.overflow_y;
                } else if (name == "overflow-x") {
                    node.style.overflow_x = node.parent->style.overflow_x;
                } else {
                    node.style.overflow_y = node.parent->style.overflow_y;
                }
            } else if (name == "overflow") {
                std::istringstream stream(value);
                std::string x;
                std::string y;
                stream >> x >> y;
                node.style.overflow_x = parse_overflow_mode(x);
                node.style.overflow_y = parse_overflow_mode(y.empty() ? x : y);
            } else if (name == "overflow-x") {
                node.style.overflow_x = parse_overflow_mode(value);
            } else {
                node.style.overflow_y = parse_overflow_mode(value);
            }
            refresh_overflow_state(node.style);
        } else if (name == "contain" && !is_inline(inline_contain)) {
            auto contain = lower_html_name(value);
            if (contain == "inherit" && node.parent != nullptr) {
                contain = node.parent->style.textual().contain_value;
            }
            if (contain == "initial" || contain == "unset" || contain == "revert"
                || contain.empty()) {
                contain = "none";
            }
            node.style.mutable_textual().contain_value = contain;
            node.style.contain_stacking_context = contain == "content"
                || contain == "strict"
                || (" " + contain + " ").find(" layout ") != std::string::npos
                || (" " + contain + " ").find(" paint ") != std::string::npos;
        } else if (name == "visibility" && !is_inline(inline_visibility)) {
            node.style.visibility_specified = value != "inherit" && value != "unset";
            node.style.visibility_hidden = value == "hidden" || value == "collapse";
        } else if (name == "pointer-events" && !is_inline(inline_pointer_events)) {
            node.style.pointer_events_specified = value != "inherit" && value != "unset";
            node.style.pointer_events_none = value == "none";
        } else if (name == "opacity" && !is_inline(inline_opacity)) {
            node.style.opacity = std::clamp(std::strtof(value.c_str(), nullptr), 0.0F, 1.0F);
        } else if (name == "color" && !is_inline(inline_color)) {
            const auto parsed = native_document::parse_color(value);
            if (is_explicit_css_color_token(value, parsed)) {
                node.style.foreground_rgba = parsed;
            }
        } else if (name == "fill" && !is_inline(inline_svg_fill)) {
            node.style.mutable_textual().svg_fill =
                value == "inherit" || value == "unset"
                ? std::string{} : value;
            decision.classification = "partially-supported";
            decision.semantic_slice = "solid SVG paint, none, and currentColor";
        } else if (name == "stroke" && !is_inline(inline_svg_stroke)) {
            node.style.mutable_textual().svg_stroke =
                value == "inherit" || value == "unset"
                ? std::string{} : value;
            decision.classification = "partially-supported";
            decision.semantic_slice = "solid SVG paint, none, and currentColor";
        } else if (name == "text-anchor" && !is_inline(inline_svg_text_anchor)) {
            if (value == "start" || value == "middle" || value == "end") {
                node.style.mutable_textual().svg_text_anchor = value;
            } else if (value == "initial") {
                node.style.mutable_textual().svg_text_anchor = "start";
            } else if (value == "inherit" || value == "unset") {
                node.style.mutable_textual().svg_text_anchor.clear();
            }
        } else if (name == "cursor" && !is_inline(inline_cursor)) {
            node.style.mutable_textual().cursor =
                value == "initial" ? "auto" : value;
            decision.classification = "partially-supported";
            decision.semantic_slice = "host cursor projection for common keyword cursors";
        } else if (name == "font") {
            if (value == "inherit" || value == "unset") {
                node.style.font_size = -1;
                node.style.line_height = -1;
                node.style.font_weight = 0;
                node.style.mutable_textual().font_family.clear();
                decision.classification = "partially-supported";
                decision.semantic_slice =
                    "inherit/unset for size, line-height, weight, and family";
            } else if (const auto font = parse_font_shorthand(node, value); font.has_value()) {
                if (!is_inline(inline_font_size)) node.style.font_size = font->font_size;
                if (!is_inline(inline_line_height)) node.style.line_height = font->line_height;
                if (!is_inline(inline_font_weight)) node.style.font_weight = font->font_weight;
                if (!is_inline(inline_font_family)) {
                    node.style.mutable_textual().font_family =
                        font->font_family;
                }
                decision.classification = "partially-supported";
                decision.semantic_slice = font->complete
                    ? "font-size, line-height, weight, and family"
                    : "font-size, line-height, weight, and family; style/variant/stretch retained without native shaping";
            } else {
                decision.classification = "unsupported";
                decision.semantic_slice = "unparsed font shorthand";
            }
        } else if (name == "font-size" && !is_inline(inline_font_size)) {
            node.style.font_size = resolved_declared_font_size(node, value);
        } else if (name == "font-family" && !is_inline(inline_font_family)) {
            node.style.mutable_textual().font_family =
                value == "inherit" || value == "unset"
                ? std::string{} : value;
        } else if ((name == "-webkit-font-smoothing" || name == "webkit-font-smoothing")
            && !is_inline(inline_font_smoothing)) {
            node.style.mutable_textual().font_smoothing =
                value == "inherit" || value == "unset"
                ? std::string{} : value;
        } else if (name == "font-weight" && !is_inline(inline_font_weight)) {
            node.style.font_weight = value == "inherit" || value == "unset" ? 0
                : value == "bold" ? 700
                : value == "normal" ? 400
                : std::max(1, static_cast<int>(std::lround(
                    native_document::parse_length(value).value)));
        } else if (name == "letter-spacing" && !is_inline(inline_letter_spacing)) {
            node.style.letter_spacing_specified = value != "inherit" && value != "unset";
            const auto length = native_document::parse_length(value);
            const auto font_size = node.style.font_size >= 0
                ? node.style.font_size : inherited_font_size(node);
            node.style.letter_spacing = value == "normal" ? 0
                : length.unit == length_unit::em ? length.value * font_size
                : length.unit == length_unit::rem ? length.value * 14.0F
                : length.value;
        } else if (name == "word-spacing" && !is_inline(inline_word_spacing)) {
            node.style.word_spacing_specified = value != "inherit" && value != "unset";
            const auto length = native_document::parse_length(value);
            const auto font_size = node.style.font_size >= 0
                ? node.style.font_size : inherited_font_size(node);
            node.style.word_spacing = value == "normal" ? 0
                : length.unit == length_unit::em ? length.value * font_size
                : length.unit == length_unit::rem ? length.value * 14.0F
                : length.value;
        } else if (name == "line-height" && !is_inline(inline_line_height)) {
            const auto font_size = node.style.font_size >= 0
                ? node.style.font_size : inherited_font_size(node);
            node.style.line_height = resolved_declared_line_height(node, value, font_size);
        } else if (name == "text-align" && !is_inline(inline_text_align)) {
            node.style.mutable_textual().text_align = value;
        } else if (name == "text-transform") {
            node.style.mutable_textual().text_transform =
                value == "inherit" || value == "unset"
                ? std::string{} : value;
            if (value != "none" && value != "uppercase" && value != "lowercase"
                && value != "capitalize" && value != "inherit" && value != "unset") {
                decision.classification = "unsupported";
                decision.semantic_slice = "ASCII none, uppercase, lowercase, and capitalize";
            } else {
                decision.classification = "partially-supported";
                decision.semantic_slice = "ASCII none, uppercase, lowercase, and capitalize";
            }
        } else if (name == "white-space" && !is_inline(inline_white_space)) {
            node.style.mutable_textual().white_space =
                value == "inherit" || value == "unset"
                ? std::string{} : value;
        } else if (name == "list-style-position") {
            node.style.mutable_textual().list_style_position = value;
        } else if (name == "list-style-type") {
            node.style.mutable_textual().list_style_type = value;
        } else if (name == "list-style") {
            std::istringstream stream(value);
            std::string token;
            while (stream >> token) {
                if (token == "inside" || token == "outside") {
                    node.style.mutable_textual().list_style_position = token;
                } else if (token == "none" || token == "decimal"
                    || token == "decimal-leading-zero" || token == "disc"
                    || token == "circle" || token == "square") {
                    node.style.mutable_textual().list_style_type = token;
                }
            }
        } else if (name == "border-style") {
            decision.classification = "partially-supported";
            decision.semantic_slice = "none";
        } else if (name == "vertical-align") {
            node.style.mutable_textual().vertical_align = value;
            decision.classification = "partially-supported";
            decision.semantic_slice = "middle on table rows and inline line boxes";
        } else if (property_mask == 0U) {
            decision.classification = "unsupported";
        }
    }

    void recompute_inline_font_relative_metrics(dom_node& node)
    {
        const auto line_height = node.authored_style().declarations.find("line-height");
        if ((node.style.inline_property_mask & inline_line_height) != 0U
            && (node.style.important_property_mask & inline_line_height) == 0U
            && line_height != node.authored_style().declarations.end()) {
            const auto font_size = node.style.font_size >= 0
                ? node.style.font_size : inherited_font_size(node);
            node.style.line_height = resolved_declared_line_height(
                node,
                line_height->second,
                font_size);
        }
    }

    void apply_css_rules(dom_node& node)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        binding_callback_timer timer(profile_startup ? &startup_css_apply : nullptr);
        const auto trace_css = profile_css;
        const auto css_apply_started = trace_css
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
#endif
        // Only elements are CSS selector subjects. Text nodes generate anonymous
        // inline boxes; comments, doctypes, fragments, and other DOM-only nodes
        // never generate boxes. In particular, do not replace the display:none
        // assigned when a preserved comment was created with the default block
        // display for an unknown tag during a subtree recascade.
        if (node.kind != dom_node_kind::element) {
            const auto display_changed = node.style.display
                != (node.kind == dom_node_kind::text
                    ? display_mode::inline_flow
                    : display_mode::none);
            node.style.display = node.kind == dom_node_kind::text
                ? display_mode::inline_flow
                : display_mode::none;
            if (display_changed) document.mark_dirty();
            else document.mark_scene_changed();
            return;
        }
        const auto previous_style = node.style;
        node.style.important_property_mask = 0;
        node.style.important_margin_sides = 0;
        node.style.scrollbar_hidden = false;
        node.style.scrollbar_visibility_important = false;
        node.style.reset_scrollbar_style();
        if ((node.style.inline_property_mask & inline_position) == 0U) {
            node.style.position = position_mode::normal;
        }
        if ((node.style.inline_property_mask & inline_float) == 0U) {
            node.style.floating = float_mode::none;
        }
        if ((node.style.inline_property_mask & inline_z_index) == 0U) {
            node.style.z_index = 0;
            node.style.z_index_auto = true;
        }
        if ((node.style.inline_property_mask & inline_grid) == 0U) {
            node.style.clear_grid();
        }
        if ((node.style.inline_property_mask & inline_width) == 0U) node.style.width = {};
        if ((node.style.inline_property_mask & inline_height) == 0U) node.style.height = {};
        if ((node.style.inline_property_mask & inline_min_width) == 0U) node.style.min_width = {};
        if ((node.style.inline_property_mask & inline_min_height) == 0U) node.style.min_height = {};
        if ((node.style.inline_property_mask & inline_max_width) == 0U) node.style.max_width = {};
        if ((node.style.inline_property_mask & inline_max_height) == 0U) node.style.max_height = {};
        if ((node.style.inline_property_mask & inline_left) == 0U) node.style.left = {};
        if ((node.style.inline_property_mask & inline_top) == 0U) node.style.top = {};
        if ((node.style.inline_property_mask & inline_right) == 0U) node.style.right = {};
        if ((node.style.inline_property_mask & inline_bottom) == 0U) node.style.bottom = {};
        if ((node.style.inline_property_mask & inline_display) == 0U) {
            node.style.display = default_display_for_node(node);
        }
        if ((node.style.inline_property_mask & inline_padding) == 0U) {
            node.style.padding_left = {};
            node.style.padding_top = {};
            node.style.padding_right = {};
            node.style.padding_bottom = {};
        }
        {
            node.style.margin_left = {};
            node.style.margin_top = {};
            node.style.margin_right = {};
            node.style.margin_bottom = {};
            node.style.margin_left_auto = false;
            node.style.margin_top_auto = false;
            node.style.margin_right_auto = false;
            node.style.margin_bottom_auto = false;
            // Rebuild from authored inline values, not a previous computed
            // value that may have been replaced by an important stylesheet.
            for (const auto important : {false, true}) {
                for (const auto& [name, value] : node.authored_style().declarations) {
                    if (margin_sides(name) != 0U
                        && node.authored_style().important_declarations.contains(name) == important) {
                        apply_margin_declaration(node.style, name, resolve_css_value(node, value));
                    }
                }
            }
        }
        if ((node.style.inline_property_mask & inline_border) == 0U) {
            node.style.border_left_width = {};
            node.style.border_top_width = {};
            node.style.border_right_width = {};
            node.style.border_bottom_width = {};
            node.style.border_left_rgba = 0;
            node.style.border_top_rgba = 0;
            node.style.border_right_rgba = 0;
            node.style.border_bottom_rgba = 0;
            node.style.border_left_current_color = true;
            node.style.border_top_current_color = true;
            node.style.border_right_current_color = true;
            node.style.border_bottom_current_color = true;
        }
        node.style.outline_width = {};
        node.style.outline_rgba = 0;
        if ((node.style.inline_property_mask & inline_border_radius) == 0U) {
            node.style.border_top_left_radius = {};
            node.style.border_top_right_radius = {};
            node.style.border_bottom_right_radius = {};
            node.style.border_bottom_left_radius = {};
            node.style.clear_vertical_corner_radii();
        }
        if ((node.style.inline_property_mask & inline_transform) == 0U) {
            node.style.transform_translate_x = {};
            node.style.transform_translate_y = {};
            node.style.transform_scale_x = 1;
            node.style.transform_scale_y = 1;
            node.style.transform_rotate_degrees = 0;
            node.style.transform_specified = false;
            node.style.transform_stacking_context = false;
        }
        if ((node.style.inline_property_mask & inline_transform_origin) == 0U) {
            node.style.transform_origin_x = {50, length_unit::percent};
            node.style.transform_origin_y = {50, length_unit::percent};
            node.style.transform_origin_specified = false;
        }
        if ((node.style.inline_property_mask & inline_contain) == 0U) {
            node.style.contain_stacking_context = false;
            node.style.mutable_textual().contain_value = "none";
        }
        node.style.clear_animations();
        if ((node.style.inline_property_mask & inline_flex_direction) == 0U) {
            node.style.direction = flex_direction::row;
            node.style.flex_reverse = false;
        }
        if ((node.style.inline_property_mask & inline_align_items) == 0U) {
            node.style.align_items = align_mode::stretch;
        }
        if ((node.style.inline_property_mask & inline_align_self) == 0U) {
            node.style.align_self = align_mode::stretch;
            node.style.align_self_specified = false;
        }
        if ((node.style.inline_property_mask & inline_justify_content) == 0U) {
            node.style.justify_content = justify_mode::start;
        }
        if ((node.style.inline_property_mask & inline_gap) == 0U) {
            node.style.row_gap = {};
            node.style.column_gap = {};
        }
        if ((node.style.inline_property_mask & inline_table_border_model) == 0U) {
            node.style.clear_table();
        }
        if ((node.style.inline_property_mask & inline_flex_wrap) == 0U) node.style.flex_wrap = false;
        if ((node.style.inline_property_mask & inline_flex_grow) == 0U) node.style.flex_grow = 0;
        if ((node.style.inline_property_mask & inline_flex_shrink) == 0U) node.style.flex_shrink = 1;
        if ((node.style.inline_property_mask & inline_flex_basis) == 0U) {
            node.style.flex_basis = {};
        }
        if ((node.style.inline_property_mask & inline_box_sizing) == 0U) node.style.border_box = false;
        if ((node.style.inline_property_mask & inline_box_shadow) == 0U) {
            shadow_value_builder::clear(node.style);
        }
        if ((node.style.inline_property_mask & inline_background) == 0U) {
            node.style.background_rgba = 0;
            node.style.background_current_color = false;
        }
        if ((node.style.inline_property_mask & inline_background_image) == 0U) {
            node.style.clear_background_image();
        }
        if ((node.style.inline_property_mask & inline_overflow) == 0U) {
            node.style.overflow_x = overflow_mode::visible;
            node.style.overflow_y = overflow_mode::visible;
            refresh_overflow_state(node.style);
        }
        if ((node.style.inline_property_mask & inline_pointer_events) == 0U) {
            node.style.pointer_events_none = false;
            node.style.pointer_events_specified = false;
        }
        if ((node.style.inline_property_mask & inline_opacity) == 0U) node.style.opacity = 1;
        if ((node.style.inline_property_mask & inline_color) == 0U) node.style.foreground_rgba = 0;
        if ((node.style.inline_property_mask & inline_font_size) == 0U) node.style.font_size = -1;
        if (auto* textual = node.style.mutable_textual_if_present();
            textual != nullptr) {
            if ((node.style.inline_property_mask & inline_font_family) == 0U) {
                textual->font_family.clear();
            }
            if ((node.style.inline_property_mask & inline_font_smoothing) == 0U) {
                textual->font_smoothing.clear();
            }
            textual->list_style_position.clear();
            textual->list_style_type.clear();
            if ((node.style.inline_property_mask & inline_text_align) == 0U) {
                textual->text_align.clear();
            }
            if ((node.style.inline_property_mask & inline_white_space) == 0U) {
                textual->white_space.clear();
            }
        }
        if ((node.style.inline_property_mask & inline_font_weight) == 0U) node.style.font_weight = 0;
        if ((node.style.inline_property_mask & inline_line_height) == 0U) node.style.line_height = -1;
        if ((node.style.inline_property_mask & inline_letter_spacing) == 0U) {
            node.style.letter_spacing = 0;
            node.style.letter_spacing_specified = false;
        }
        if ((node.style.inline_property_mask & inline_word_spacing) == 0U) {
            node.style.word_spacing = 0;
            node.style.word_spacing_specified = false;
        }
        css::seed_inline_custom_properties(node);
        node.style.clear_pseudo_elements();
        // Recompute stylesheet visibility from the current class/selector set.
        // React reuses toolbar nodes while replacing their responsive classes;
        // retaining an earlier `visibility:hidden` makes the new variant stale.
        if ((node.style.inline_property_mask & inline_visibility) == 0U) {
            node.style.visibility_hidden = false;
            node.style.visibility_specified = false;
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto reset_completed = trace_css
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
#endif
        std::vector<size_t> candidates = unindexed_css_rules;
        const auto append_candidates = [&](const auto& index, const auto& key) {
            const auto match = index.find(key);
            if (match != index.end()) {
                candidates.insert(candidates.end(), match->second.begin(), match->second.end());
            }
        };
        append_candidates(css_rules_by_tag, node.tag);
        const auto has_explicit_document_element = node.tag == "body"
            && std::any_of(node.children.begin(), node.children.end(), [](const auto* child) {
                return child != nullptr && child->tag == "html";
            });
        if (node.tag == "body"
            && (node.parent == nullptr || node.parent->tag == "iframe")
            && !has_explicit_document_element) {
            append_candidates(css_rules_by_tag, "html");
        }
        if (!node.id_attribute.empty()) append_candidates(css_rules_by_id, node.id_attribute);
        for (const auto& [name, value] : node.attributes) {
            static_cast<void>(value);
            append_candidates(css_rules_by_attribute, name);
        }
        if (&node == active_element) {
            candidates.insert(
                candidates.end(),
                css_focus_rules.begin(),
                css_focus_rules.end());
        }
        const std::string_view classes(node.class_name);
        size_t class_cursor = 0;
        while (class_cursor < classes.size()) {
            while (class_cursor < classes.size()
                && std::isspace(static_cast<unsigned char>(classes[class_cursor]))) ++class_cursor;
            const auto start = class_cursor;
            while (class_cursor < classes.size()
                && !std::isspace(static_cast<unsigned char>(classes[class_cursor]))) ++class_cursor;
            if (class_cursor > start) {
#if defined(WEBSCENE_NATIVE_ENGINE_MEDIA_REFRESH_BENCHMARK_COUNTERS)
                media_refresh_class_lookups.fetch_add(1U, std::memory_order_relaxed);
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_CSS_CLASS_LOOKUP_VIEW_EXPERIMENT)
                append_candidates(
                    css_rules_by_class,
                    classes.substr(start, class_cursor - start));
#else
#if defined(WEBSCENE_NATIVE_ENGINE_MEDIA_REFRESH_BENCHMARK_COUNTERS)
                media_refresh_owned_class_lookup_keys.fetch_add(1U, std::memory_order_relaxed);
                media_refresh_owned_class_lookup_bytes.fetch_add(
                    class_cursor - start, std::memory_order_relaxed);
#endif
                append_candidates(
                    css_rules_by_class,
                    std::string(classes.substr(start, class_cursor - start)));
#endif
            }
        }
        sort_css_candidates(candidates);
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (trace_css) css_profile_candidate_checks += candidates.size();
#endif
        const auto* node_shadow_root = document.containing_shadow_root(node);
        const auto rule_is_in_scope = [&](const css_rule& rule) {
            if (rule.shadow_scope_root_id == 0U) return node_shadow_root == nullptr;
            auto* scope_root = document.find_by_native_id(rule.shadow_scope_root_id);
            if (scope_root == nullptr) return false;
            const auto* scope = document.shadow_dom(*scope_root);
            const auto host_selector = trim_css_view(rule.selector()) == ":host";
            if (host_selector) return scope != nullptr && scope->host == &node;
            if (scope != nullptr && scope->host == &node) return false;
            return node_shadow_root == scope_root;
        };
        std::vector<const css_rule*> matched_rules;
        std::vector<std::pair<int, const css_rule*>> matched_pseudo_rules;
        for (const auto index : candidates) {
            const auto& rule = css_rules[index];
            if (!rule.media_matches) continue;
            if (!rule_is_in_scope(rule)) continue;
            std::string pseudo_origin;
            const auto pseudo_kind = split_pseudo_element_selector(rule.selector(), pseudo_origin);
            if (pseudo_kind != 0) {
                if (!pseudo_origin.empty() && css_selector_matches(node, pseudo_origin)) {
                    matched_pseudo_rules.emplace_back(pseudo_kind, &rule);
                }
                continue;
            }
            if (trim_css_view(rule.selector()) != ":host"
                && !css_rule_matches(node, rule)) continue;
            matched_rules.push_back(&rule);
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (trace_css) css_profile_matched_rules += matched_rules.size();
        const auto matching_completed = trace_css
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
#endif
        // CSS custom properties are cascaded before dependent declarations are
        // computed. Applying each rule eagerly made a base button height resolve
        // with the default medium size before its later `.small-*` rule set the
        // size token.
        for (const auto* rule : matched_rules) {
            for (const auto& declaration : rule->declarations()) {
                if (declaration.name.starts_with("--")) {
                    apply_css_declaration(node, declaration);
                }
            }
        }
        for (const auto* rule : matched_rules) {
            for (const auto& declaration : rule->declarations()) {
                if (declaration.name.starts_with("--")) continue;
                apply_css_declaration(node, declaration);
            }
        }
        // An inline declaration can be authored before the stylesheet that
        // defines one of its var() references is connected. Its first
        // computed-value attempt is then invalid, but the authored tokens must
        // remain live and be recomputed when the variable becomes available.
        // Replay variable-dependent inline declarations and explicit dimension
        // inheritance, whose computed values can change with the parent.
        // Temporarily removing the inline guard lets a declaration update its own
        // computed value while the important-origin mask still prevents a
        // normal inline declaration from overriding an author !important rule.
        for (const auto& [name, value] : node.authored_style().declarations) {
            const auto inherited_dimension = (name == "width" || name == "height")
                && trim_css_view(value) == "inherit";
            if (name.starts_with("--")
                || (value.find("var(") == std::string::npos && !inherited_dimension)) continue;
            const auto property_mask = inline_style_mask(name);
            const auto retained_inline_mask = node.style.inline_property_mask;
            node.style.inline_property_mask &= ~property_mask;
            apply_css_declaration(
                node,
                {name, value,
                    node.authored_style().important_declarations.contains(name)}, true);
            node.style.inline_property_mask = retained_inline_mask;
        }
        // Inline transition declarations are stored separately from the hot
        // style object. Recascade clears cold animation state before applying
        // stylesheet rules, so restore the inline origin afterward while
        // retaining per-longhand !important precedence.
        constexpr std::array transition_properties{
            "transition",
            "transition-property",
            "transition-duration",
            "transition-delay",
            "transition-timing-function"
        };
        for (const auto* property : transition_properties) {
            const auto authored = node.authored_style().declarations.find(property);
            if (authored == node.authored_style().declarations.end()) continue;
            const auto inline_important =
                node.authored_style().important_declarations.contains(property);
            const auto property_mask = inline_style_mask(property);
            if (!inline_important
                && (node.style.important_property_mask & property_mask) != 0U) continue;
            apply_css_declaration(node, {property, authored->second, inline_important});
        }
        // Inline relative line-height is authored at a higher origin, but its
        // computed pixel value depends on the winning font-size. Re-resolve it
        // only after stylesheet declarations have completed their cascade.
        recompute_cascaded_line_height(node, matched_rules);
        recompute_inline_font_relative_metrics(node);
        // Pseudo-elements inherit the originating element's completed
        // computed style. Apply them only after its custom and ordinary
        // declarations so `::before { background:var(--active-bg) }` resolves
        // the state variable authored on the active element.
        for (const auto& [pseudo_kind, rule] : matched_pseudo_rules) {
            if (pseudo_kind >= 3 && pseudo_kind <= 6) {
                for (const auto& declaration : rule->declarations()) {
                    apply_scrollbar_css_declaration(node, pseudo_kind, declaration);
                }
                continue;
            }
            auto& pseudo = pseudo_kind == 1
                ? node.style.mutable_before_pseudo()
                : node.style.mutable_after_pseudo();
            for (const auto& declaration : rule->declarations()) {
                apply_pseudo_css_declaration(node, pseudo, declaration);
            }
        }
        // The pseudo-element's used rectangle is layout state, not computed
        // style. A paint-only recascade rebuilds the computed pseudo style but
        // does not schedule layout, so retain the last used rectangle until a
        // layout-affecting change below marks the document dirty. The ensuing
        // layout pass will replace it with newly resolved geometry.
        if (node.style.before_pseudo().generated
            && previous_style.before_pseudo().generated) {
            node.style.mutable_before_pseudo().layout =
                previous_style.before_pseudo().layout;
        }
        if (node.style.after_pseudo().generated
            && previous_style.after_pseudo().generated) {
            node.style.mutable_after_pseudo().layout =
                previous_style.after_pseudo().layout;
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto declarations_completed = trace_css
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
#endif
        configure_style_keyframe_animation(node);
        document.update_style_animations(node);
        detect_css_compositions(node);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto force_layout_invalidation =
            std::getenv("WEBSCENE_FORCE_RECASCADE_LAYOUT_INVALIDATION") != nullptr;
#else
        constexpr auto force_layout_invalidation = false;
#endif
        if (force_layout_invalidation
            || !computed_layout_style_equal(previous_style, node.style)) {
            document.mark_dirty();
        } else {
            document.mark_scene_changed();
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (trace_css) {
            const auto completed = std::chrono::steady_clock::now();
            css_profile_reset_nanoseconds +=
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    reset_completed - css_apply_started).count();
            css_profile_matching_nanoseconds +=
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    matching_completed - reset_completed).count();
            css_profile_declarations_nanoseconds +=
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    declarations_completed - matching_completed).count();
            css_profile_finalize_nanoseconds +=
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    completed - declarations_completed).count();
        }
#endif
    }

    bool has_appended_css_rule_candidate(
        const dom_node& node,
        size_t first_new_rule) const
    {
        if (node.tag == "#text" || first_new_rule >= css_rules.size()) return false;
        const auto contains_new_rule = [first_new_rule](
            const std::vector<size_t>& indexes) {
            return !indexes.empty() && indexes.back() >= first_new_rule;
        };
        const auto index_contains_new_rule = [&contains_new_rule](
            const auto& index,
            const std::string& key) {
            const auto match = index.find(key);
            return match != index.end() && contains_new_rule(match->second);
        };
        if (contains_new_rule(unindexed_css_rules)
            || index_contains_new_rule(css_rules_by_tag, node.tag)
            || (!node.id_attribute.empty()
                && index_contains_new_rule(css_rules_by_id, node.id_attribute))) {
            return true;
        }
        if (node.tag == "body"
            && (node.parent == nullptr || node.parent->tag == "iframe")
            && index_contains_new_rule(css_rules_by_tag, "html")) {
            return true;
        }
        for (const auto& [name, value] : node.attributes) {
            static_cast<void>(value);
            if (index_contains_new_rule(css_rules_by_attribute, name)) return true;
        }
        if (&node == active_element && contains_new_rule(css_focus_rules)) return true;
        const std::string_view classes(node.class_name);
        size_t cursor = 0U;
        while (cursor < classes.size()) {
            while (cursor < classes.size()
                && std::isspace(static_cast<unsigned char>(classes[cursor]))) ++cursor;
            const auto start = cursor;
            while (cursor < classes.size()
                && !std::isspace(static_cast<unsigned char>(classes[cursor]))) ++cursor;
            if (cursor > start
                && index_contains_new_rule(
                    css_rules_by_class,
                    std::string(classes.substr(start, cursor - start)))) {
                return true;
            }
        }
        return false;
    }

    bool apply_appended_css_rules(dom_node& node, size_t first_new_rule)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        binding_callback_timer timer(
            profile_startup ? &startup_css_incremental_apply : nullptr);
#endif
        if (node.tag == "#text" || first_new_rule >= css_rules.size()) return false;

        std::vector<size_t> candidates;
        const auto append_new_candidates = [&](const std::vector<size_t>& source) {
            const auto first = std::lower_bound(
                source.begin(), source.end(), first_new_rule);
            candidates.insert(candidates.end(), first, source.end());
        };
        append_new_candidates(unindexed_css_rules);
        const auto append_candidates = [&](const auto& index, const std::string& key) {
            const auto match = index.find(key);
            if (match != index.end()) {
                append_new_candidates(match->second);
            }
        };
        append_candidates(css_rules_by_tag, node.tag);
        if (node.tag == "body"
            && (node.parent == nullptr || node.parent->tag == "iframe")) {
            append_candidates(css_rules_by_tag, "html");
        }
        if (!node.id_attribute.empty()) append_candidates(css_rules_by_id, node.id_attribute);
        for (const auto& [name, value] : node.attributes) {
            static_cast<void>(value);
            append_candidates(css_rules_by_attribute, name);
        }
        if (&node == active_element) {
            append_new_candidates(css_focus_rules);
        }
        const std::string_view classes(node.class_name);
        size_t class_cursor = 0;
        while (class_cursor < classes.size()) {
            while (class_cursor < classes.size()
                && std::isspace(static_cast<unsigned char>(classes[class_cursor]))) ++class_cursor;
            const auto start = class_cursor;
            while (class_cursor < classes.size()
                && !std::isspace(static_cast<unsigned char>(classes[class_cursor]))) ++class_cursor;
            if (class_cursor > start) {
                append_candidates(
                    css_rules_by_class,
                    std::string(classes.substr(start, class_cursor - start)));
            }
        }
        sort_css_candidates(candidates);
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

        if (filter_stylesheet_candidates && candidates.empty()) return false;

        std::vector<const css_rule*> matched_rules;
        std::vector<std::pair<int, const css_rule*>> matched_pseudo_rules;
        for (const auto index : candidates) {
            const auto& rule = css_rules[index];
            if (!rule.media_matches) continue;
            std::string pseudo_origin;
            const auto pseudo_kind = split_pseudo_element_selector(rule.selector(), pseudo_origin);
            if (pseudo_kind != 0) {
                if (!pseudo_origin.empty() && css_selector_matches(node, pseudo_origin)) {
                    matched_pseudo_rules.emplace_back(pseudo_kind, &rule);
                }
                continue;
            }
            if (css_rule_matches(node, rule)) matched_rules.push_back(&rule);
        }
        if (filter_stylesheet_candidates
            && matched_rules.empty()
            && matched_pseudo_rules.empty()) {
            return false;
        }
        for (const auto* rule : matched_rules) {
            for (const auto& declaration : rule->declarations()) {
                if (declaration.name.starts_with("--")) apply_css_declaration(node, declaration);
            }
        }
        for (const auto* rule : matched_rules) {
            for (const auto& declaration : rule->declarations()) {
                if (!declaration.name.starts_with("--")) apply_css_declaration(node, declaration);
            }
        }
        recompute_cascaded_line_height(node, matched_rules);
        recompute_inline_font_relative_metrics(node);
        for (const auto& [pseudo_kind, rule] : matched_pseudo_rules) {
            if (pseudo_kind >= 3 && pseudo_kind <= 6) {
                for (const auto& declaration : rule->declarations()) {
                    apply_scrollbar_css_declaration(node, pseudo_kind, declaration);
                }
                continue;
            }
            auto& pseudo = pseudo_kind == 1
                ? node.style.mutable_before_pseudo()
                : node.style.mutable_after_pseudo();
            for (const auto& declaration : rule->declarations()) {
                apply_pseudo_css_declaration(node, pseudo, declaration);
            }
        }
        configure_style_keyframe_animation(node);
        document.update_style_animations(node);
        detect_css_compositions(node);
        document.mark_dirty();
        return true;
    }

    std::unordered_set<std::string> appended_css_custom_properties(
        size_t first_new_rule) const
    {
        std::unordered_set<std::string> properties;
        for (auto index = first_new_rule; index < css_rules.size(); ++index) {
            for (const auto& declaration : css_rules[index].declarations()) {
                if (declaration.name.starts_with("--")) {
                    properties.insert(declaration.name);
                }
            }
        }
        return properties;
    }

    void expand_css_variable_dependencies(
        std::unordered_set<std::string>& properties) const
    {
        std::vector<std::string> pending(properties.begin(), properties.end());
        for (size_t cursor = 0U; cursor < pending.size(); ++cursor) {
            const auto indexed = css_rules_by_variable_reference.find(pending[cursor]);
            if (indexed == css_rules_by_variable_reference.end()) continue;
            for (const auto rule_index : indexed->second) {
                if (rule_index >= css_rules.size()) continue;
                for (const auto& declaration : css_rules[rule_index].declarations()) {
                    if (!declaration.name.starts_with("--")) continue;
                    const auto references = css_variable_references(declaration.value);
                    if (!references.contains(pending[cursor])) continue;
                    if (properties.insert(declaration.name).second) {
                        pending.push_back(declaration.name);
                    }
                }
            }
        }
    }

    struct css_variable_recascade_rule final {
        size_t index{0U};
        std::vector<std::string> properties;
    };

    struct css_variable_recascade_plan final {
        std::unordered_set<std::string> variables;
        std::vector<css_variable_recascade_rule> rules;
        std::unordered_map<std::string, std::vector<size_t>> rules_by_class;
        std::unordered_map<std::string, std::vector<size_t>> rules_by_id;
        std::unordered_map<std::string, std::vector<size_t>> rules_by_tag;
        std::unordered_map<std::string, std::vector<size_t>> rules_by_attribute;
        std::vector<size_t> focus_rules;
        std::vector<size_t> unindexed_rules;
    };

    css_variable_recascade_plan make_css_variable_recascade_plan(
        size_t first_new_rule) const
    {
        css_variable_recascade_plan plan;
        plan.variables = appended_css_custom_properties(first_new_rule);
        expand_css_variable_dependencies(plan.variables);
        if (plan.variables.empty()) return plan;

        std::vector<size_t> dependent_rules;
        for (const auto& variable : plan.variables) {
            const auto indexed = css_rules_by_variable_reference.find(variable);
            if (indexed != css_rules_by_variable_reference.end()) {
                dependent_rules.insert(
                    dependent_rules.end(),
                    indexed->second.begin(),
                    indexed->second.end());
            }
        }
        std::sort(dependent_rules.begin(), dependent_rules.end());
        dependent_rules.erase(
            std::unique(dependent_rules.begin(), dependent_rules.end()),
            dependent_rules.end());
        for (const auto index : dependent_rules) {
            if (index >= css_rules.size()) continue;
            css_variable_recascade_rule dependent{index, {}};
            for (const auto& declaration : css_rules[index].declarations()) {
                if (declaration.name.starts_with("--")) continue;
                const auto references = css_variable_references(declaration.value);
                if (std::any_of(
                        references.begin(),
                        references.end(),
                        [&](const auto& reference) {
                            return plan.variables.contains(reference);
                        })) {
                    dependent.properties.push_back(declaration.name);
                }
            }
            std::sort(dependent.properties.begin(), dependent.properties.end());
            dependent.properties.erase(
                std::unique(
                    dependent.properties.begin(), dependent.properties.end()),
                dependent.properties.end());
            if (!dependent.properties.empty()) {
                plan.rules.push_back(std::move(dependent));
            }
        }
        std::unordered_map<size_t, size_t> plan_index_by_rule;
        for (size_t index = 0U; index < plan.rules.size(); ++index) {
            plan_index_by_rule.emplace(plan.rules[index].index, index);
        }
        const auto project_index = [&](const auto& source, auto& target) {
            for (const auto& [key, rule_indexes] : source) {
                for (const auto rule_index : rule_indexes) {
                    const auto found = plan_index_by_rule.find(rule_index);
                    if (found != plan_index_by_rule.end()) {
                        target[key].push_back(found->second);
                    }
                }
            }
        };
        project_index(css_rules_by_class, plan.rules_by_class);
        project_index(css_rules_by_id, plan.rules_by_id);
        project_index(css_rules_by_tag, plan.rules_by_tag);
        project_index(css_rules_by_attribute, plan.rules_by_attribute);
        for (const auto rule_index : css_focus_rules) {
            const auto found = plan_index_by_rule.find(rule_index);
            if (found != plan_index_by_rule.end()) {
                plan.focus_rules.push_back(found->second);
            }
        }
        for (const auto rule_index : unindexed_css_rules) {
            const auto found = plan_index_by_rule.find(rule_index);
            if (found != plan_index_by_rule.end()) {
                plan.unindexed_rules.push_back(found->second);
            }
        }
        return plan;
    }

    bool reapply_css_variable_dependent_rules(
        dom_node& node,
        const css_variable_recascade_plan& plan)
    {
        if (node.tag == "#text" || plan.variables.empty()) return false;

        const auto references_affected_variable = [&](std::string_view value) {
            const auto references = css_variable_references(value);
            return std::any_of(
                references.begin(),
                references.end(),
                [&](const auto& reference) {
                    return plan.variables.contains(reference);
                });
        };

        auto filter_by_variable_dependency = true;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        filter_by_variable_dependency =
            std::getenv("WEBSCENE_PROBE_DISABLE_CSS_VARIABLE_DEPENDENCY_FILTER")
                == nullptr;
#endif
        std::unordered_set<std::string> variable_dependent_properties;
        if (filter_by_variable_dependency) {
            auto plan_candidates = plan.unindexed_rules;
            const auto append_plan_candidates = [&](const auto& index, const std::string& key) {
                const auto match = index.find(key);
                if (match != index.end()) {
                    plan_candidates.insert(
                        plan_candidates.end(), match->second.begin(), match->second.end());
                }
            };
            append_plan_candidates(plan.rules_by_tag, node.tag);
            if (node.tag == "body"
                && (node.parent == nullptr || node.parent->tag == "iframe")) {
                append_plan_candidates(plan.rules_by_tag, "html");
            }
            if (!node.id_attribute.empty()) {
                append_plan_candidates(plan.rules_by_id, node.id_attribute);
            }
            for (const auto& [name, value] : node.attributes) {
                static_cast<void>(value);
                append_plan_candidates(plan.rules_by_attribute, name);
            }
            if (&node == active_element) {
                plan_candidates.insert(
                    plan_candidates.end(),
                    plan.focus_rules.begin(),
                    plan.focus_rules.end());
            }
            std::istringstream classes(node.class_name);
            for (std::string class_name; classes >> class_name;) {
                append_plan_candidates(plan.rules_by_class, class_name);
            }
            std::sort(plan_candidates.begin(), plan_candidates.end());
            plan_candidates.erase(
                std::unique(plan_candidates.begin(), plan_candidates.end()),
                plan_candidates.end());
            for (const auto candidate : plan_candidates) {
                const auto& dependent = plan.rules[candidate];
                const auto& rule = css_rules[dependent.index];
                if (!rule.media_matches || !css_rule_matches(node, rule)) continue;
                variable_dependent_properties.insert(
                    dependent.properties.begin(),
                    dependent.properties.end());
            }
            for (const auto& [name, value] : node.authored_style().declarations) {
                if (!name.starts_with("--") && references_affected_variable(value)) {
                    variable_dependent_properties.insert(name);
                }
            }
            if (variable_dependent_properties.empty()) return false;
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (profile_startup) ++startup_stylesheet_variable_nodes;
#endif

        std::vector<size_t> candidates = unindexed_css_rules;
        const auto append_candidates = [&](const auto& index, const std::string& key) {
            const auto match = index.find(key);
            if (match != index.end()) {
                candidates.insert(candidates.end(), match->second.begin(), match->second.end());
            }
        };
        append_candidates(css_rules_by_tag, node.tag);
        if (node.tag == "body"
            && (node.parent == nullptr || node.parent->tag == "iframe")) {
            append_candidates(css_rules_by_tag, "html");
        }
        if (!node.id_attribute.empty()) append_candidates(css_rules_by_id, node.id_attribute);
        for (const auto& [name, value] : node.attributes) {
            static_cast<void>(value);
            append_candidates(css_rules_by_attribute, name);
        }
        if (&node == active_element) {
            candidates.insert(
                candidates.end(),
                css_focus_rules.begin(),
                css_focus_rules.end());
        }
        std::istringstream classes(node.class_name);
        for (std::string class_name; classes >> class_name;) {
            append_candidates(css_rules_by_class, class_name);
        }
        sort_css_candidates(candidates);
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

        std::vector<const css_rule*> matched_rules;
        for (const auto index : candidates) {
            const auto& rule = css_rules[index];
            if (!rule.media_matches || !css_rule_matches(node, rule)) continue;
            matched_rules.push_back(&rule);
            if (!filter_by_variable_dependency) {
                for (const auto& declaration : rule.declarations()) {
                    if (!declaration.name.starts_with("--")
                        && declaration.value.find("var(") != std::string::npos) {
                        variable_dependent_properties.insert(declaration.name);
                    }
                }
            }
        }
        // Replay the complete cascade for every property whose computed value
        // depends on a newly introduced custom property. Replaying only the
        // var() declaration lets it jump past later declarations of equal or
        // higher precedence (for example a responsive `display:none`).
        for (const auto* rule : matched_rules) {
            for (const auto& declaration : rule->declarations()) {
                if (variable_dependent_properties.contains(declaration.name)) {
                    apply_css_declaration(node, declaration);
                }
            }
        }
        for (const auto& [name, value] : node.authored_style().declarations) {
            if (!variable_dependent_properties.contains(name)
                || !references_affected_variable(value)) {
                continue;
            }
            const auto property_mask = inline_style_mask(name);
            const auto retained_inline_mask = node.style.inline_property_mask;
            node.style.inline_property_mask &= ~property_mask;
            apply_css_declaration(
                node,
                {name, value,
                    node.authored_style().important_declarations.contains(name)}, true);
            node.style.inline_property_mask = retained_inline_mask;
        }
        document.mark_dirty();
        return true;
    }

    // Re-run the complete cascade only on dimension consumers when explicit
    // inheritance does not require descendant style propagation.
    // Keep the ordinary subtree path for aliases, inherited values and pseudo
    // elements; this is intentionally a conservative, uncached fast path.
    bool recascade_dimension_custom_property(dom_node& root, const std::string& name)
    {
        if (!is_connected(root) || document.containing_shadow_root(root) != nullptr) return false;
        auto* previous_root = const_cast<dom_node*>(active_css_cascade_root);
        auto* cascade_root = css_cascade_root_for_node(root);
        if (cascade_root != nullptr) activate_css_cascade(cascade_root);
        const auto restore = [this, previous_root, cascade_root](void*) {
            if (previous_root != nullptr && previous_root != cascade_root)
                activate_css_cascade(previous_root);
        };
        std::unique_ptr<void, decltype(restore)> scope(this, restore);

        if (css_attribute_dependencies.contains("style")) return false;
        // The lightweight attribute-dependency index does not decode escapes.
        for (const auto& rule : css_rules) {
            if (rule.selector().find(static_cast<char>(92)) != std::string::npos
                || copy_ascii_lower(rule.selector()).find("style") != std::string::npos) return false;
        }
        const auto is_geometry_property = [](std::string_view property) {
            return property == "width" || property == "height"
                || property == "min-width" || property == "max-width"
                || property == "min-height" || property == "max-height"
                || property == "grid-template-columns" || property == "grid-template-rows"
                || property == "left" || property == "right"
                || property == "top" || property == "bottom";
        };
        // Non-inherited-by-default dimensions can still explicitly inherit.
        // Include variable-resolved inheritance, not just literal tokens. If
        // present, the ordinary ancestor-first subtree cascade propagates it.
        std::vector<const css_rule*> inheritance_rules;
        for (const auto& rule : css_rules) {
            bool potential = false;
            for (const auto& declaration : rule.declarations()) {
                if (!is_geometry_property(declaration.name)) continue;
                if (trim_css_view(declaration.value) == "inherit") return false;
                potential = potential || declaration.value.find("var(") != std::string::npos;
            }
            if (potential) {
                std::string origin;
                if (split_pseudo_element_selector(rule.selector(), origin) != 0) return false;
                inheritance_rules.push_back(&rule);
            }
        }
        const auto references = [&](std::string_view value) {
            return css_variable_references(value).contains(name);
        };
        // An inherited inline alias may be authored above the mutation root.
        for (auto* parent = root.parent; parent != nullptr; parent = parent->parent) {
            for (const auto& [property, value] : parent->authored_style().declarations) {
                if (references(value) && !is_geometry_property(property)) return false;
            }
        }
        std::vector<const css_rule*> rules;
        if (const auto found = css_rules_by_variable_reference.find(name);
            found != css_rules_by_variable_reference.end()) {
            for (const auto index : found->second) {
                const auto& rule = css_rules[index];
                std::string origin;
                if (rule.shadow_scope_root_id != 0U
                    || split_pseudo_element_selector(rule.selector(), origin) != 0)
                    return false;
                for (const auto& declaration : rule.declarations()) {
                    if (references(declaration.value) && !is_geometry_property(declaration.name))
                        return false;
                }
                rules.push_back(&rule);
            }
        }
        std::vector<dom_node*> nodes{&root};
        std::vector<dom_node*> consumers;
        for (size_t i = 0; i < nodes.size(); ++i) {
            auto& node = *nodes[i];
            // Shadow inheritance and independent browsing contexts retain their
            // existing full-cascade handling.
            if (document.shadow_dom(node) != nullptr) return false;
            for (auto* child : node.children) {
                if (child != nullptr && !inactive_css_cascades.contains(child))
                    nodes.push_back(child);
            }
            bool affected = &node == &root;
            for (const auto& [property, value] : node.authored_style().declarations) {
                if (is_geometry_property(property)
                    && trim_css_view(resolve_css_value(node, value)) == "inherit") return false;
                if (!references(value)) continue;
                if (!is_geometry_property(property)) return false;
                affected = true;
            }
            if (node.kind == dom_node_kind::element) {
                for (const auto* rule : inheritance_rules) {
                    if (!rule->media_matches || !css_rule_matches(node, *rule)) continue;
                    for (const auto& declaration : rule->declarations()) {
                        if (is_geometry_property(declaration.name)
                            && trim_css_view(resolve_css_value(node, declaration.value)) == "inherit")
                            return false;
                    }
                }
            }
            if (!affected && node.kind == dom_node_kind::element) {
                affected = std::any_of(rules.begin(), rules.end(), [&](const auto* rule) {
                    return rule->media_matches && css_rule_matches(node, *rule);
                });
            }
            if (affected) consumers.push_back(&node);
        }
        // No partial updates before all conservative fallback checks succeed.
        for (auto* node : consumers) apply_css_rules(*node);
        return true;
    }

    void apply_css_rules_subtree(dom_node& node)
    {
        apply_css_rules(node);
        for (auto* child : node.children) {
            if (child == nullptr) continue;
            // An iframe owns an independent stylesheet cascade. A recascade
            // of an outer sibling or ancestor must never walk through that
            // browsing-context boundary with the caller's active rules.
            if (child != active_css_cascade_root
                && inactive_css_cascades.contains(child)) {
                continue;
            }
            apply_css_rules_subtree(*child);
        }
        const auto* shadow = document.shadow_dom(node);
        if (shadow != nullptr && shadow->root != nullptr) {
            apply_css_rules_subtree(*shadow->root);
        }
    }

    bool is_connected(const dom_node& node)
    {
        for (auto* current = &node; current != nullptr; current = current->parent) {
            if (current == &document.body()) return true;
            if (current->parent == nullptr
                && detached_document_roots.contains(current->id)) return true;
            for (const auto& [frame_id, provisional_body] : provisional_frame_bodies) {
                if (current != provisional_body) continue;
                const auto* frame = document.find_by_native_id(frame_id);
                for (auto* owner = frame; owner != nullptr; owner = owner->parent) {
                    if (owner == &document.body()) return true;
                }
            }
        }
        return false;
    }

    void recascade_connected_subtree_now(
        dom_node& node,
        std::string_view reason = {})
    {
        webscene_frame_trace::scope cascade_trace(frame_trace, "recascade-start", "recascade-end", 0);
        if (!is_connected(node)) return;
        auto* previous_cascade_root = const_cast<dom_node*>(active_css_cascade_root);
        auto* node_cascade_root = css_cascade_root_for_node(node);
        if (node_cascade_root != nullptr) activate_css_cascade(node_cascade_root);
        retain_reconnected_subtree_wrappers(node);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto trace = profile_css;
        const auto candidate_checks_before = css_profile_candidate_checks;
        const auto matched_rules_before = css_profile_matched_rules;
        const auto declarations_before = css_profile_declaration_applications;
        const auto reset_nanoseconds_before = css_profile_reset_nanoseconds;
        const auto matching_nanoseconds_before = css_profile_matching_nanoseconds;
        const auto declaration_nanoseconds_before =
            css_profile_declarations_nanoseconds;
        const auto finalize_nanoseconds_before = css_profile_finalize_nanoseconds;
        if (trace
            && std::getenv("WEBSCENE_PROBE_TRACE_UNINDEXED_CSS") != nullptr
            && last_traced_unindexed_css_count != unindexed_css_rules.size()) {
            last_traced_unindexed_css_count = unindexed_css_rules.size();
            for (const auto index : unindexed_css_rules) {
                if (index < css_rules.size()) {
                    std::cerr << "[Native Engine Probe] CSS unindexed rule="
                        << index << ", selector=" << css_rules[index].selector() << '\n';
                }
            }
        }
        const auto started = trace ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        binding_callback_timer timer(profile_startup ? &startup_subtree_recascade : nullptr);
#else
        static_cast<void>(reason);
#endif
        apply_css_rules_subtree(node);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (trace) {
            const auto elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
            if (elapsed >= 0.5) {
                size_t descendants = 0;
                std::vector<const dom_node*> pending{&node};
                while (!pending.empty()) {
                    const auto* current = pending.back();
                    pending.pop_back();
                    ++descendants;
                    pending.insert(pending.end(), current->children.begin(), current->children.end());
                }
                std::cerr << "[Native Engine Probe] CSS recascade "
                    << std::fixed << std::setprecision(3) << elapsed << " ms, nodes="
                    << descendants
                    << ", candidates="
                    << (css_profile_candidate_checks - candidate_checks_before)
                    << ", matched="
                    << (css_profile_matched_rules - matched_rules_before)
                    << ", declarations="
                    << (css_profile_declaration_applications - declarations_before)
                    << ", stages-ms="
                    << (css_profile_reset_nanoseconds - reset_nanoseconds_before)
                        / 1'000'000.0
                    << "/"
                    << (css_profile_matching_nanoseconds - matching_nanoseconds_before)
                        / 1'000'000.0
                    << "/"
                    << (css_profile_declarations_nanoseconds
                            - declaration_nanoseconds_before)
                        / 1'000'000.0
                    << "/"
                    << (css_profile_finalize_nanoseconds - finalize_nanoseconds_before)
                        / 1'000'000.0
                    << ", rules=" << css_rules.size()
                    << ", unindexed=" << unindexed_css_rules.size()
                    << ", tag=" << node.tag << ", id=" << node.id_attribute
                    << ", reason=" << reason
                    << ", class=" << node.class_name.substr(0, 180) << '\n';
            }
        }
#endif
        if (previous_cascade_root != nullptr
            && previous_cascade_root != node_cascade_root) {
            activate_css_cascade(previous_cascade_root);
        }
    }

    void schedule_style_recascade(
        dom_node& root,
        bool subtree,
        std::string_view reason,
        std::string custom_property = {})
    {
        if (!is_connected(root)) return;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++style_recascade_schedule_requests;
#endif
        auto* cascade_root = css_cascade_root_for_node(root);
        for (auto& pending : pending_style_recascades) {
            if (pending.root != &root || pending.cascade_root != cascade_root) continue;
            pending.subtree = pending.subtree || subtree;
            if (pending.custom_property != custom_property) pending.custom_property.clear();
            if (pending.reason.empty() && !reason.empty()) pending.reason = reason;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            ++style_recascade_coalesced_requests;
#endif
            return;
        }
        for (auto& pending : pending_style_recascades) {
            if (!pending.subtree || pending.cascade_root != cascade_root
                || pending.root == nullptr || !subtree_contains(*pending.root, &root))
                continue;
            // An ancestor request now covers another mutation as well.
            pending.custom_property.clear();
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            ++style_recascade_coalesced_requests;
#endif
            return;
        }
        if (subtree) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            const auto pending_before = pending_style_recascades.size();
#endif
            if (std::any_of(pending_style_recascades.begin(), pending_style_recascades.end(),
                    [this, &root, cascade_root](const auto& pending) {
                        return pending.root != nullptr && pending.cascade_root == cascade_root
                            && subtree_contains(root, pending.root);
                    })) custom_property.clear();
            std::erase_if(
                pending_style_recascades,
                [this, &root, cascade_root](const pending_style_recascade& pending) {
                    return pending.root != nullptr
                        && pending.cascade_root == cascade_root
                        && subtree_contains(root, pending.root);
                });
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            style_recascade_coalesced_requests +=
                pending_before - pending_style_recascades.size();
#endif
        }
        pending_style_recascades.push_back(
            pending_style_recascade{
                &root,
                cascade_root,
                subtree,
                std::string(reason),
                std::move(custom_property)});
    }

    void flush_pending_style_recascades()
    {
        if (pending_style_recascades.empty()) return;
        auto pending = std::move(pending_style_recascades);
        pending_style_recascades.clear();
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++style_recascade_flush_batches;
        style_recascade_flush_roots += pending.size();
#endif
        for (const auto& recascade : pending) {
            if (recascade.root == nullptr || !is_connected(*recascade.root)) continue;
            if (recascade.subtree) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                ++style_recascade_flush_subtree_roots;
#endif
                if (recascade.custom_property.empty()
                    || !recascade_dimension_custom_property(*recascade.root, recascade.custom_property))
                    recascade_connected_subtree_now(*recascade.root, recascade.reason);
            } else {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                ++style_recascade_flush_node_roots;
#endif
                recascade_connected_node(*recascade.root);
            }
        }
    }

    void recascade_custom_property(dom_node& node, const std::string& name)
    {
        if (style_recascade_batch_depth != 0U) {
            schedule_style_recascade(node, true, "style.custom-property", name);
        } else if (!recascade_dimension_custom_property(node, name)) {
            recascade_connected_subtree_now(node, "style.custom-property");
        }
    }

    void recascade_connected_subtree(
        dom_node& node,
        std::string_view reason = {})
    {
        if (style_recascade_batch_depth != 0U) {
            binding_callback_timer timer(
                nullptr,
                profile_named("style-recascade/subtree-scheduled"));
            schedule_style_recascade(node, true, reason);
            return;
        }
        binding_callback_timer timer(
            nullptr,
            profile_named("style-recascade/subtree-immediate"));
        recascade_connected_subtree_now(node, reason);
    }

    void recascade_connected_node(dom_node& node)
    {
        if (!is_connected(node)) return;
        if (style_recascade_batch_depth != 0U) {
            binding_callback_timer timer(
                nullptr,
                profile_named("style-recascade/node-scheduled"));
            schedule_style_recascade(node, false, {});
            return;
        }
        binding_callback_timer timer(
            nullptr,
            profile_named("style-recascade/node-immediate"));
        auto* previous_cascade_root = const_cast<dom_node*>(active_css_cascade_root);
        auto* node_cascade_root = css_cascade_root_for_node(node);
        if (node_cascade_root != nullptr) activate_css_cascade(node_cascade_root);
        apply_css_rules(node);
        if (previous_cascade_root != nullptr
            && previous_cascade_root != node_cascade_root) {
            activate_css_cascade(previous_cascade_root);
        }
    }

    void recascade_hover_transition(
        dom_node* previous,
        dom_node* next,
        std::string_view reason)
    {
        if (previous == next || hover_selector_dependencies.empty()) return;

        std::vector<dom_node*> previous_path;
        std::vector<dom_node*> next_path;
        for (auto* node = previous; node != nullptr; node = node->parent) {
            previous_path.push_back(node);
        }
        for (auto* node = next; node != nullptr; node = node->parent) {
            next_path.push_back(node);
        }
        while (!previous_path.empty() && !next_path.empty()
            && previous_path.back() == next_path.back()) {
            previous_path.pop_back();
            next_path.pop_back();
        }
        std::vector<dom_node*> changed = std::move(previous_path);
        changed.insert(changed.end(), next_path.begin(), next_path.end());

        struct invalidation_request final
        {
            dom_node* root{nullptr};
            bool subtree{false};
        };
        std::vector<invalidation_request> requests;
        const auto add_request = [&](dom_node* root, bool subtree) {
            if (root == nullptr || !is_connected(*root)) return;
            for (auto& existing : requests) {
                if (existing.root != root) continue;
                existing.subtree = existing.subtree || subtree;
                return;
            }
            if (std::any_of(
                    requests.begin(),
                    requests.end(),
                    [this, root](const invalidation_request& existing) {
                        return existing.subtree
                            && subtree_contains(*existing.root, root);
                    })) {
                return;
            }
            if (subtree) {
                std::erase_if(
                    requests,
                    [this, root](const invalidation_request& existing) {
                        return subtree_contains(*root, existing.root);
                    });
            }
            requests.push_back({root, subtree});
        };

        for (auto* changed_node : changed) {
            if (changed_node == nullptr) continue;
            for (const auto& dependency : hover_selector_dependencies) {
                if (dependency.trigger_compound != "*"
                    && !css_compound_selector_matches(
                        *changed_node,
                        dependency.trigger_compound)) {
                    continue;
                }
                switch (dependency.scope) {
                case hover_invalidation_scope::subject:
                    add_request(changed_node, false);
                    break;
                case hover_invalidation_scope::subtree:
                    add_request(changed_node, true);
                    break;
                case hover_invalidation_scope::siblings:
                    add_request(
                        changed_node->parent == nullptr
                            ? changed_node
                            : changed_node->parent,
                        true);
                    break;
                }
            }
        }

#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto trace = profile_css;
        const auto started = trace
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        size_t recalculated_nodes = 0U;
#endif
        for (const auto& request : requests) {
            if (request.root == nullptr) continue;
            if (request.subtree) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                if (trace) {
                    visit_subtree(*request.root, [&](const dom_node&) {
                        ++recalculated_nodes;
                    });
                }
#endif
                recascade_connected_subtree(*request.root, reason);
            } else {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                if (trace) ++recalculated_nodes;
#endif
                apply_css_rules(*request.root);
            }
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (trace) {
            const auto elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
            std::cerr << "[Native Engine Probe] CSS hover invalidation "
                << std::fixed << std::setprecision(3) << elapsed
                << " ms, changed=" << changed.size()
                << ", dependencies=" << hover_selector_dependencies.size()
                << ", roots=" << requests.size()
                << ", nodes=" << recalculated_nodes
                << ", reason=" << reason << '\n';
        }
#endif
    }

    void rebuild_root_variables()
    {
#if defined(WEBSCENE_NATIVE_ENGINE_MEDIA_REFRESH_BENCHMARK_COUNTERS)
        media_refresh_root_variable_refreshes.fetch_add(1U, std::memory_order_relaxed);
#endif
        css::rebuild_root_variables(css_rules,css_variables,important_css_variables);
    }

    void rebuild_css_rule_indexes_and_root_variables()
    {
        css_rules_by_class.clear();
        css_rules_by_id.clear();
        css_rules_by_tag.clear();
        css_rules_by_attribute.clear();
        css_rules_by_variable_reference.clear();
        css_focus_rules.clear();
        unindexed_css_rules.clear();
        css_attribute_dependencies.clear();
        css_descendant_attribute_dependencies.clear();
        for (size_t index = 0; index < css_rules.size(); ++index) {
            index_css_rule(index);
        }
        rebuild_root_variables();
    }

    void recascade_after_stylesheet_change()
    {
        apply_css_rules_subtree(active_root());
        document.mark_dirty();
    }

    bool deactivate_connected_stylesheets(dom_node& root, bool recascade = true)
    {
        std::unordered_set<uint32_t> owners;
        visit_subtree(root, [&](const dom_node& node) {
            if (node.tag == "style") owners.insert(node.id);
        });
        if (owners.empty()) return false;
        const auto previous_size = css_rules.size();
        std::erase_if(css_rules, [&](const css_rule& rule) {
            return rule.stylesheet_owner_id != 0
                && owners.contains(rule.stylesheet_owner_id);
        });
        if (css_rules.size() == previous_size) return false;
        rebuild_css_rule_indexes_and_root_variables();
        if (recascade) recascade_after_stylesheet_change();
        return true;
    }

    void activate_connected_stylesheet(dom_node& node)
    {
        std::vector<dom_node*> styles;
        for (auto* ancestor = &node; ancestor != nullptr; ancestor = ancestor->parent) {
            if (ancestor->tag != "style") continue;
            styles.push_back(ancestor);
            break;
        }
        if (styles.empty()) {
            visit_subtree(node, [&](dom_node& candidate) {
                if (candidate.tag == "style") styles.push_back(&candidate);
            });
        }
        std::erase_if(styles, [&](const dom_node* style) {
            return style == nullptr || !is_connected(*style);
        });
        if (styles.empty()) return;

        for (auto* style : styles) deactivate_connected_stylesheets(*style, false);
        const auto& base = current_base_address();
        for (auto* style : styles) {
            std::string stylesheet;
            append_node_text(*style, stylesheet);
            if (!stylesheet.empty()) {
                const auto* shadow_root = document.containing_shadow_root(*style);
                add_stylesheet(
                    std::move(stylesheet),
                    base,
                    style->id,
                    shadow_root == nullptr ? 0U : shadow_root->id);
            }
        }
        recascade_after_stylesheet_change();
    }

    static std::vector<std::string> parse_frame_stylesheets(const std::string& html)
    {
        std::vector<std::string> result;
        size_t cursor = 0;
        while (true) {
            const auto open = html.find("<link", cursor);
            if (open == std::string::npos) break;
            const auto close = html.find('>', open + 5U);
            if (close == std::string::npos) break;
            const auto tag = html.substr(open, close - open + 1U);
            const auto relation = extract_html_attribute(tag, "rel");
            const auto href = extract_html_attribute(tag, "href");
            if (relation == "stylesheet" && !href.empty()) result.push_back(href);
            cursor = close + 1U;
        }
        return result;
    }
