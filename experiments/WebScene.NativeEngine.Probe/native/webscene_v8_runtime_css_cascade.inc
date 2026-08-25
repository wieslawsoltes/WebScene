    static void apply_scrollbar_css_declaration(
        node_style& style,
        const css_declaration& declaration)
    {
        if (declaration.name != "display"
            || (style.scrollbar_visibility_important && !declaration.important)) {
            return;
        }
        style.scrollbar_hidden = trim_css(declaration.value) == "none";
        style.scrollbar_visibility_important = declaration.important;
    }

    static bool layout_length_equal(css_length left, css_length right) noexcept
    {
        return left.value == right.value
            && left.unit == right.unit
            && left.pixel_offset == right.pixel_offset;
    }

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
    {
        if (codepoint <= 0x7FU) {
            result.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FFU) {
            result.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
            result.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
        } else if (codepoint <= 0xFFFFU) {
            result.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
            result.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
        } else {
            result.push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
            result.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
        }
    }

    static std::string decode_css_content(std::string value)
    {
        if (value == "none" || value == "normal") return {};
        if (value.size() >= 2U
            && (value.front() == '\'' || value.front() == '"')
            && value.back() == value.front()) {
            value = value.substr(1U, value.size() - 2U);
        }
        std::string result;
        for (size_t index = 0; index < value.size(); ++index) {
            if (value[index] != '\\' || index + 1U >= value.size()) {
                result.push_back(value[index]);
                continue;
            }
            size_t end = index + 1U;
            while (end < value.size() && end - index <= 6U
                && std::isxdigit(static_cast<unsigned char>(value[end]))) ++end;
            if (end > index + 1U) {
                const auto codepoint = static_cast<uint32_t>(std::strtoul(
                    value.substr(index + 1U, end - index - 1U).c_str(), nullptr, 16));
                append_utf8_codepoint(result, codepoint);
                if (end < value.size() && std::isspace(static_cast<unsigned char>(value[end]))) ++end;
                index = end - 1U;
            } else {
                result.push_back(value[++index]);
            }
        }
        return result;
    }

    static bool apply_corner_radius_values(
        std::string_view name,
        const std::string& value,
        css_length& top_left,
        css_length& top_right,
        css_length& bottom_right,
        css_length& bottom_left,
        css_length& top_left_y,
        css_length& top_right_y,
        css_length& bottom_right_y,
        css_length& bottom_left_y)
    {
        std::vector<css_length> horizontal;
        std::vector<css_length> vertical;
        auto* target = &horizontal;
        size_t token_start = std::string::npos;
        int parenthesis_depth = 0;
        size_t slash_count = 0U;
        for (size_t index = 0; index <= value.size(); ++index) {
            const auto character = index < value.size() ? value[index] : ' ';
            if (character == '(') ++parenthesis_depth;
            else if (character == ')' && parenthesis_depth > 0) --parenthesis_depth;
            const auto slash = parenthesis_depth == 0 && character == '/';
            const auto separator = parenthesis_depth == 0
                && (std::isspace(static_cast<unsigned char>(character)) || slash);
            if (!separator && token_start == std::string::npos) token_start = index;
            if (separator && token_start != std::string::npos) {
                target->push_back(native_document::parse_length(
                    value.substr(token_start, index - token_start)));
                token_start = std::string::npos;
            }
            if (slash) {
                ++slash_count;
                target = &vertical;
            }
        }

        const auto assign = [&](css_length& horizontal_radius,
                                css_length& vertical_radius) {
            if (horizontal.empty() || horizontal.size() > 2U
                || slash_count != 0U || !vertical.empty()) return;
            horizontal_radius = horizontal[0];
            vertical_radius = horizontal.size() > 1U ? horizontal[1] : horizontal[0];
        };
        if (name == "border-top-left-radius" || name == "border-start-start-radius") {
            assign(top_left, top_left_y);
            return true;
        }
        if (name == "border-top-right-radius" || name == "border-start-end-radius") {
            assign(top_right, top_right_y);
            return true;
        }
        if (name == "border-bottom-right-radius" || name == "border-end-end-radius") {
            assign(bottom_right, bottom_right_y);
            return true;
        }
        if (name == "border-bottom-left-radius" || name == "border-end-start-radius") {
            assign(bottom_left, bottom_left_y);
            return true;
        }
        if (name != "border-radius") return false;
        if (horizontal.empty() || horizontal.size() > 4U || vertical.size() > 4U
            || slash_count > 1U || (slash_count == 1U && vertical.empty())) return true;
        const auto expand = [](const std::vector<css_length>& values,
                               css_length& first,
                               css_length& second,
                               css_length& third,
                               css_length& fourth) {
            first = values[0];
            second = values.size() > 1U ? values[1] : values[0];
            third = values.size() > 2U ? values[2] : values[0];
            fourth = values.size() > 3U ? values[3]
                : values.size() > 1U ? values[1] : values[0];
        };
        expand(horizontal, top_left, top_right, bottom_right, bottom_left);
        if (vertical.empty()) {
            top_left_y = top_left;
            top_right_y = top_right;
            bottom_right_y = bottom_right;
            bottom_left_y = bottom_left;
        } else {
            expand(vertical, top_left_y, top_right_y, bottom_right_y, bottom_left_y);
        }
        return true;
    }

    static bool apply_corner_radius_declaration(
        std::string_view name,
        const std::string& value,
        node_style& style)
    {
        auto top_left_y = style.border_top_left_radius_y();
        auto top_right_y = style.border_top_right_radius_y();
        auto bottom_right_y = style.border_bottom_right_radius_y();
        auto bottom_left_y = style.border_bottom_left_radius_y();
        if (!apply_corner_radius_values(
                name,
                value,
                style.border_top_left_radius,
                style.border_top_right_radius,
                style.border_bottom_right_radius,
                style.border_bottom_left_radius,
                top_left_y,
                top_right_y,
                bottom_right_y,
                bottom_left_y)) {
            return false;
        }
        style.set_vertical_corner_radii(
            top_left_y,
            top_right_y,
            bottom_right_y,
            bottom_left_y);
        return true;
    }

    static bool apply_corner_radius_declaration(
        std::string_view name,
        const std::string& value,
        node_style::pseudo_element& pseudo)
    {
        auto top_left_y = pseudo.elliptical_border_radius
            ? pseudo.border_top_left_radius_y : pseudo.border_top_left_radius;
        auto top_right_y = pseudo.elliptical_border_radius
            ? pseudo.border_top_right_radius_y : pseudo.border_top_right_radius;
        auto bottom_right_y = pseudo.elliptical_border_radius
            ? pseudo.border_bottom_right_radius_y : pseudo.border_bottom_right_radius;
        auto bottom_left_y = pseudo.elliptical_border_radius
            ? pseudo.border_bottom_left_radius_y : pseudo.border_bottom_left_radius;
        if (!apply_corner_radius_values(
                name,
                value,
                pseudo.border_top_left_radius,
                pseudo.border_top_right_radius,
                pseudo.border_bottom_right_radius,
                pseudo.border_bottom_left_radius,
                top_left_y,
                top_right_y,
                bottom_right_y,
                bottom_left_y)) {
            return false;
        }
        pseudo.border_top_left_radius_y = top_left_y;
        pseudo.border_top_right_radius_y = top_right_y;
        pseudo.border_bottom_right_radius_y = bottom_right_y;
        pseudo.border_bottom_left_radius_y = bottom_left_y;
        const auto differs = [](css_length first, css_length second) {
            return first.value != second.value
                || first.unit != second.unit
                || first.pixel_offset != second.pixel_offset;
        };
        pseudo.elliptical_border_radius =
            differs(top_left_y, pseudo.border_top_left_radius)
            || differs(top_right_y, pseudo.border_top_right_radius)
            || differs(bottom_right_y, pseudo.border_bottom_right_radius)
            || differs(bottom_left_y, pseudo.border_bottom_left_radius);
        return true;
    }

    static bool apply_box_shadow_value(
        node_style& style,
        const std::string& authored_value,
        bool& complete)
    {
        style.box_shadow_present = false;
        style.box_shadow_offset_x = 0;
        style.box_shadow_offset_y = 0;
        style.box_shadow_blur_radius = 0;
        style.box_shadow_spread_radius = 0;
        style.box_shadow_rgba = 0;
        complete = true;

        const auto value = trim_css(authored_value);
        if (value.empty() || value == "none") return true;

        auto first_shadow_end = value.size();
        auto parenthesis_depth = 0;
        for (size_t index = 0; index < value.size(); ++index) {
            if (value[index] == '(') ++parenthesis_depth;
            else if (value[index] == ')' && parenthesis_depth > 0) --parenthesis_depth;
            else if (value[index] == ',' && parenthesis_depth == 0) {
                first_shadow_end = index;
                complete = false;
                break;
            }
        }
        const auto shadow = trim_css(std::string_view(value).substr(0, first_shadow_end));
        if (shadow.find("inset") != std::string::npos) {
            complete = false;
            return false;
        }

        std::vector<std::string> tokens;
        size_t token_start = std::string::npos;
        parenthesis_depth = 0;
        for (size_t index = 0; index <= shadow.size(); ++index) {
            const auto at_end = index == shadow.size();
            const auto character = at_end ? ' ' : shadow[index];
            if (!at_end && character == '(') ++parenthesis_depth;
            else if (!at_end && character == ')' && parenthesis_depth > 0) --parenthesis_depth;
            const auto separator = parenthesis_depth == 0
                && std::isspace(static_cast<unsigned char>(character));
            if (!separator && token_start == std::string::npos) token_start = index;
            if (separator && token_start != std::string::npos) {
                tokens.emplace_back(shadow.substr(token_start, index - token_start));
                token_start = std::string::npos;
            }
        }

        std::vector<float> lengths;
        auto color = 0x000000FFU;
        auto color_seen = false;
        for (const auto& token : tokens) {
            const auto lower = lower_html_name(token);
            const auto starts_numeric = !token.empty()
                && (std::isdigit(static_cast<unsigned char>(token.front()))
                    || token.front() == '.' || token.front() == '-' || token.front() == '+');
            const auto color_token = token.starts_with('#')
                || lower.starts_with("rgb(") || lower.starts_with("rgba(")
                || lower == "transparent" || (!starts_numeric && lower != "inset");
            if (color_token) {
                color = native_document::parse_color(token);
                color_seen = true;
            } else {
                const auto length = native_document::parse_length(token);
                if (length.unit != length_unit::pixels) complete = false;
                lengths.push_back(length.value);
            }
        }
        if (lengths.size() < 2U || lengths.size() > 4U) return false;

        style.box_shadow_offset_x = lengths[0];
        style.box_shadow_offset_y = lengths[1];
        style.box_shadow_blur_radius = lengths.size() > 2U
            ? std::max(0.0F, lengths[2]) : 0.0F;
        style.box_shadow_spread_radius = lengths.size() > 3U ? lengths[3] : 0.0F;
        style.box_shadow_rgba = color_seen ? color : 0x000000FFU;
        style.box_shadow_present = (style.box_shadow_rgba & 0xFFU) != 0U;
        return true;
    }

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
        if (name == "content") {
            pseudo.generated = value != "none" && value != "normal";
            pseudo.content = decode_css_content(value);
        } else if (name == "display") {
            pseudo.display_none = value == "none";
            pseudo.display = value == "block" ? display_mode::block
                : value == "flex" ? display_mode::flex
                : value == "inline-flex" ? display_mode::inline_flex
                : value == "grid" ? display_mode::grid
                : value == "inline-grid" ? display_mode::inline_grid
                : value == "table" ? display_mode::table
                : value == "inline-table" ? display_mode::inline_table
                : value == "inline-block" ? display_mode::inline_block
                : display_mode::inline_flow;
        } else if (name == "align-self") {
            pseudo.align_self_specified = value != "auto";
            pseudo.align_self = value == "center" ? align_mode::center
                : value == "flex-start" || value == "start" ? align_mode::start
                : value == "flex-end" || value == "end" ? align_mode::end
                : align_mode::stretch;
        } else if (name == "visibility") {
            pseudo.visibility_hidden = value == "hidden" || value == "collapse";
        } else if (name == "position") {
            pseudo.position = value == "absolute" ? position_mode::absolute
                : value == "fixed" ? position_mode::fixed
                : value == "relative" ? position_mode::relative
                : position_mode::normal;
        } else if (name == "z-index") {
            pseudo.z_index = value == "auto" ? 0 : std::atoi(value.c_str());
        } else if (name == "inset") {
            std::vector<css_length> values;
            std::istringstream stream(value);
            for (std::string token; stream >> token;) {
                values.push_back(native_document::parse_length(token));
            }
            if (!values.empty()) {
                pseudo.top = values[0];
                pseudo.right = values.size() > 1 ? values[1] : values[0];
                pseudo.bottom = values.size() > 2 ? values[2] : values[0];
                pseudo.left = values.size() > 3 ? values[3]
                    : values.size() > 1 ? values[1] : values[0];
            }
        } else if (name == "width") pseudo.width = native_document::parse_length(value);
        else if (name == "height") pseudo.height = native_document::parse_length(value);
        else if (name == "left" || name == "inset-inline-start") {
            pseudo.left = native_document::parse_length(value);
        } else if (name == "right" || name == "inset-inline-end") {
            pseudo.right = native_document::parse_length(value);
        } else if (name == "top" || name == "inset-block-start") {
            pseudo.top = native_document::parse_length(value);
        } else if (name == "bottom" || name == "inset-block-end") {
            pseudo.bottom = native_document::parse_length(value);
        } else if (name == "margin") {
            std::vector<css_length> values;
            std::istringstream stream(value);
            for (std::string token; stream >> token;) {
                values.push_back(native_document::parse_length(token));
            }
            if (!values.empty()) {
                pseudo.margin_top = values[0];
                pseudo.margin_right = values.size() > 1 ? values[1] : values[0];
                pseudo.margin_bottom = values.size() > 2 ? values[2] : values[0];
                pseudo.margin_left = values.size() > 3 ? values[3]
                    : values.size() > 1 ? values[1] : values[0];
            }
        } else if (name == "margin-left" || name == "margin-inline-start") {
            pseudo.margin_left = native_document::parse_length(value);
        } else if (name == "margin-right" || name == "margin-inline-end") {
            pseudo.margin_right = native_document::parse_length(value);
        } else if (name == "margin-top" || name == "margin-block-start") {
            pseudo.margin_top = native_document::parse_length(value);
        } else if (name == "margin-bottom" || name == "margin-block-end") {
            pseudo.margin_bottom = native_document::parse_length(value);
        } else if (name == "box-sizing") {
            pseudo.border_box = value == "border-box";
        } else if (canonical_css_property_name(name).starts_with("border")
            && name.find("radius") == std::string::npos) {
            node_style border_style{};
            border_style.foreground_rgba = (pseudo.foreground_rgba & 0xFFU) != 0U
                ? pseudo.foreground_rgba : node.style.foreground_rgba;
            border_style.border_left_width = pseudo.border_left_width;
            border_style.border_top_width = pseudo.border_top_width;
            border_style.border_right_width = pseudo.border_right_width;
            border_style.border_bottom_width = pseudo.border_bottom_width;
            border_style.border_left_rgba = pseudo.border_left_rgba;
            border_style.border_top_rgba = pseudo.border_top_rgba;
            border_style.border_right_rgba = pseudo.border_right_rgba;
            border_style.border_bottom_rgba = pseudo.border_bottom_rgba;
            if (apply_border_declaration(border_style, name, value)) {
                pseudo.border_left_width = border_style.border_left_width;
                pseudo.border_top_width = border_style.border_top_width;
                pseudo.border_right_width = border_style.border_right_width;
                pseudo.border_bottom_width = border_style.border_bottom_width;
                pseudo.border_left_rgba = border_style.border_left_rgba;
                pseudo.border_top_rgba = border_style.border_top_rgba;
                pseudo.border_right_rgba = border_style.border_right_rgba;
                pseudo.border_bottom_rgba = border_style.border_bottom_rgba;
            } else {
                decision.classification = "unsupported";
            }
        } else if (apply_corner_radius_declaration(
            name,
            value,
            pseudo)) {
        } else if (name == "outline") {
            pseudo.outline_width = {};
            pseudo.outline_rgba = 0;
            auto remaining = value;
            auto color_start = remaining.find("rgba(");
            if (color_start == std::string::npos) color_start = remaining.find("rgb(");
            if (color_start != std::string::npos) {
                const auto color_end = remaining.find(')', color_start);
                if (color_end != std::string::npos) {
                    pseudo.outline_rgba = native_document::parse_color(
                        remaining.substr(color_start, color_end - color_start + 1U));
                    remaining.erase(color_start, color_end - color_start + 1U);
                }
            }
            std::istringstream stream(remaining);
            for (std::string token; stream >> token;) {
                const auto lower = lower_html_name(token);
                if (lower == "none") {
                    pseudo.outline_width = {};
                    pseudo.outline_rgba = 0;
                } else if (lower == "thin") {
                    pseudo.outline_width = {1, length_unit::pixels};
                } else if (lower == "medium") {
                    pseudo.outline_width = {3, length_unit::pixels};
                } else if (lower == "thick") {
                    pseudo.outline_width = {5, length_unit::pixels};
                } else if (!token.empty()
                    && (std::isdigit(static_cast<unsigned char>(token.front()))
                        || token.front() == '.' || token.front() == '-')) {
                    pseudo.outline_width = native_document::parse_length(token);
                } else {
                    const auto color = native_document::parse_color(token);
                    if (color != 0U || lower == "transparent") {
                        pseudo.outline_rgba = color;
                    }
                }
            }
        } else if (name == "outline-width") {
            pseudo.outline_width = native_document::parse_length(value);
        } else if (name == "outline-color") {
            pseudo.outline_rgba = native_document::parse_color(value);
        } else if (name == "background" || name == "background-color") {
            pseudo.background_current_color = value == "currentcolor" || value == "currentColor";
            pseudo.background_rgba = pseudo.background_current_color
                ? 0U
                : native_document::parse_color(value);
        } else if (name == "opacity") {
            pseudo.opacity = std::clamp(std::strtof(value.c_str(), nullptr), 0.0F, 1.0F);
        } else if (name == "color") {
            pseudo.foreground_rgba = native_document::parse_color(value);
        } else if (name == "font-size") {
            pseudo.font_size = std::max(0.0F, native_document::parse_length(value).value);
        } else if (name == "line-height") {
            if (value == "normal" || value == "initial" || value == "revert") {
                pseudo.line_height = -2.0F;
            } else if (value != "inherit" && value != "unset") {
                pseudo.line_height = std::max(0.0F, native_document::parse_length(value).value);
            } else {
                decision.classification = "partially-supported";
                decision.semantic_slice = "explicit numeric lengths";
            }
        } else {
            decision.classification = "unsupported";
        }
    }

    static std::optional<std::string> first_css_url(const std::string& value)
    {
        const auto lower = lower_html_name(value);
        const auto start = lower.find("url(");
        if (start == std::string::npos) return std::nullopt;
        const auto end = value.find(')', start + 4U);
        if (end == std::string::npos) return std::nullopt;
        auto url = trim_css(value.substr(start + 4U, end - start - 4U));
        if (url.size() >= 2U
            && ((url.front() == '\'' && url.back() == '\'')
                || (url.front() == '"' && url.back() == '"'))) {
            url = url.substr(1U, url.size() - 2U);
        }
        return url.empty() ? std::nullopt : std::optional<std::string>{std::move(url)};
    }

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

    static void apply_background_position(node_style& style, const std::string& value)
    {
        auto& background = style.mutable_background_image();
        std::istringstream stream(value);
        std::string first;
        std::string second;
        stream >> first >> second;
        if (first.empty()) return;
        const auto vertical = [](const std::string& token) {
            return token == "top" || token == "bottom";
        };
        const auto horizontal = [](const std::string& token) {
            return token == "left" || token == "right";
        };
        if (vertical(first)) {
            background.position_y = first;
            background.position_x = second.empty() ? "center" : second;
        } else {
            background.position_x = first;
            background.position_y = second.empty()
                ? (horizontal(first) ? "center" : "0%")
                : second;
        }
    }

    static std::vector<std::string> split_css_component_list(
        std::string_view value,
        char separator)
    {
        std::vector<std::string> result;
        size_t start = 0;
        int depth = 0;
        char quote = 0;
        for (size_t index = 0; index <= value.size(); ++index) {
            const auto character = index < value.size() ? value[index] : separator;
            if (quote != 0) {
                if (character == quote && (index == 0 || value[index - 1] != '\\')) quote = 0;
                continue;
            }
            if (character == '\'' || character == '"') {
                quote = character;
                continue;
            }
            if (character == '(') ++depth;
            else if (character == ')' && depth > 0) --depth;
            if (character == separator && depth == 0) {
                auto component = trim_css(std::string(value.substr(start, index - start)));
                if (!component.empty()) result.push_back(std::move(component));
                start = index + 1U;
            }
        }
        return result;
    }

    static std::vector<std::string> split_transition_tokens(std::string_view value)
    {
        std::vector<std::string> result;
        size_t start = std::string_view::npos;
        int depth = 0;
        for (size_t index = 0; index <= value.size(); ++index) {
            const auto character = index < value.size() ? value[index] : ' ';
            if (character == '(') ++depth;
            else if (character == ')' && depth > 0) --depth;
            if (std::isspace(static_cast<unsigned char>(character)) && depth == 0) {
                if (start != std::string_view::npos) {
                    result.emplace_back(value.substr(start, index - start));
                    start = std::string_view::npos;
                }
            } else if (start == std::string_view::npos) {
                start = index;
            }
        }
        return result;
    }

    static bool apply_margin_declaration(
        node_style& style,
        std::string_view raw_name,
        const std::string& value)
    {
        const auto name = canonical_css_property_name(raw_name);
        const auto assign = [&](css_length& length, bool& automatic, const std::string& token) {
            length = native_document::parse_length(token);
            automatic = lower_html_name(trim_css(token)) == "auto";
        };
        if (name == "margin") {
            const auto tokens = split_transition_tokens(value);
            if (tokens.empty() || tokens.size() > 4U) return true;
            const auto& top = tokens[0];
            const auto& right = tokens.size() > 1U ? tokens[1] : tokens[0];
            const auto& bottom = tokens.size() > 2U ? tokens[2] : tokens[0];
            const auto& left = tokens.size() > 3U ? tokens[3]
                : tokens.size() > 1U ? tokens[1] : tokens[0];
            assign(style.margin_top, style.margin_top_auto, top);
            assign(style.margin_right, style.margin_right_auto, right);
            assign(style.margin_bottom, style.margin_bottom_auto, bottom);
            assign(style.margin_left, style.margin_left_auto, left);
            return true;
        }
        if (name == "margin-block" || name == "margin-inline") {
            const auto tokens = split_transition_tokens(value);
            if (tokens.empty() || tokens.size() > 2U) return true;
            const auto& start = tokens[0];
            const auto& end = tokens.size() > 1U ? tokens[1] : tokens[0];
            if (name == "margin-block") {
                assign(style.margin_top, style.margin_top_auto, start);
                assign(style.margin_bottom, style.margin_bottom_auto, end);
            } else {
                assign(style.margin_left, style.margin_left_auto, start);
                assign(style.margin_right, style.margin_right_auto, end);
            }
            return true;
        }
        if (name == "margin-left" || name == "margin-inline-start") {
            assign(style.margin_left, style.margin_left_auto, value);
            return true;
        }
        if (name == "margin-right" || name == "margin-inline-end") {
            assign(style.margin_right, style.margin_right_auto, value);
            return true;
        }
        if (name == "margin-top" || name == "margin-block-start") {
            assign(style.margin_top, style.margin_top_auto, value);
            return true;
        }
        if (name == "margin-bottom" || name == "margin-block-end") {
            assign(style.margin_bottom, style.margin_bottom_auto, value);
            return true;
        }
        return false;
    }

    static bool apply_padding_declaration(
        node_style& style,
        std::string_view raw_name,
        const std::string& value)
    {
        const auto name = canonical_css_property_name(raw_name);
        const auto assign = [](css_length& length, const std::string& token) {
            length = native_document::parse_length(token);
        };
        if (name == "padding") {
            const auto tokens = split_transition_tokens(value);
            if (tokens.empty() || tokens.size() > 4U) return true;
            const auto& top = tokens[0];
            const auto& right = tokens.size() > 1U ? tokens[1] : tokens[0];
            const auto& bottom = tokens.size() > 2U ? tokens[2] : tokens[0];
            const auto& left = tokens.size() > 3U ? tokens[3]
                : tokens.size() > 1U ? tokens[1] : tokens[0];
            assign(style.padding_top, top);
            assign(style.padding_right, right);
            assign(style.padding_bottom, bottom);
            assign(style.padding_left, left);
            return true;
        }
        if (name == "padding-block" || name == "padding-inline") {
            const auto tokens = split_transition_tokens(value);
            if (tokens.empty() || tokens.size() > 2U) return true;
            const auto& start = tokens[0];
            const auto& end = tokens.size() > 1U ? tokens[1] : tokens[0];
            if (name == "padding-block") {
                assign(style.padding_top, start);
                assign(style.padding_bottom, end);
            } else {
                assign(style.padding_left, start);
                assign(style.padding_right, end);
            }
            return true;
        }
        if (name == "padding-left" || name == "padding-inline-start") {
            assign(style.padding_left, value);
            return true;
        }
        if (name == "padding-right" || name == "padding-inline-end") {
            assign(style.padding_right, value);
            return true;
        }
        if (name == "padding-top" || name == "padding-block-start") {
            assign(style.padding_top, value);
            return true;
        }
        if (name == "padding-bottom" || name == "padding-block-end") {
            assign(style.padding_bottom, value);
            return true;
        }
        return false;
    }

    static css_length parse_inset_length(const std::string& value)
    {
        const auto normalized = lower_html_name(value);
        if (normalized == "auto" || normalized == "initial"
            || normalized == "unset" || normalized == "revert"
            || normalized == "revert-layer") {
            return {};
        }
        return native_document::parse_length(value);
    }

    static bool apply_inset_declaration(
        node_style& style,
        std::string_view raw_name,
        const std::string& value)
    {
        if (canonical_css_property_name(raw_name) != "inset") return false;
        const auto tokens = split_transition_tokens(value);
        if (tokens.empty() || tokens.size() > 4U) return true;
        const auto& top = tokens[0];
        const auto& right = tokens.size() > 1U ? tokens[1] : tokens[0];
        const auto& bottom = tokens.size() > 2U ? tokens[2] : tokens[0];
        const auto& left = tokens.size() > 3U ? tokens[3]
            : tokens.size() > 1U ? tokens[1] : tokens[0];
        style.top = parse_inset_length(top);
        style.right = parse_inset_length(right);
        style.bottom = parse_inset_length(bottom);
        style.left = parse_inset_length(left);
        return true;
    }

    static bool apply_border_declaration(
        node_style& style,
        std::string_view raw_name,
        const std::string& value)
    {
        const auto name = canonical_css_property_name(raw_name);
        const auto parse_color = [&](const std::string& token) {
            return lower_html_name(token) == "currentcolor"
                ? style.foreground_rgba
                : native_document::parse_color(token);
        };
        const auto parse_width = [](const std::string& token, css_length& width) {
            if (token == "thin") {
                width = {1, length_unit::pixels};
                return true;
            }
            if (token == "medium") {
                width = {3, length_unit::pixels};
                return true;
            }
            if (token == "thick") {
                width = {5, length_unit::pixels};
                return true;
            }
            if (!token.empty()
                && (std::isdigit(static_cast<unsigned char>(token.front()))
                    || token.front() == '.' || token.front() == '-')) {
                width = native_document::parse_length(token);
                return true;
            }
            return false;
        };
        const auto parse_side = [&](css_length& width, uint32_t& color) {
            if (value.empty() || value == "none") {
                width = {};
                color = 0;
                return;
            }
            auto remaining = value;
            auto color_start = remaining.find("rgba(");
            if (color_start == std::string::npos) color_start = remaining.find("rgb(");
            if (color_start != std::string::npos) {
                const auto color_end = remaining.find(')', color_start);
                if (color_end != std::string::npos) {
                    color = native_document::parse_color(
                        remaining.substr(color_start, color_end - color_start + 1U));
                    remaining.erase(color_start, color_end - color_start + 1U);
                }
            }
            std::istringstream stream(remaining);
            for (std::string token; stream >> token;) {
                if (token == "none") {
                    width = {};
                    color = 0;
                } else if (!parse_width(token, width)
                    && token != "solid" && token != "dashed" && token != "dotted"
                    && token != "double" && token != "hidden") {
                    const auto parsed = parse_color(token);
                    if (parsed != 0U || token == "transparent"
                        || lower_html_name(token) == "currentcolor") {
                        color = parsed;
                    }
                }
            }
        };
        const auto assign_widths = [&](const std::vector<std::string>& tokens) {
            if (tokens.empty() || tokens.size() > 4U) return;
            const auto& top = tokens[0];
            const auto& right = tokens.size() > 1U ? tokens[1] : tokens[0];
            const auto& bottom = tokens.size() > 2U ? tokens[2] : tokens[0];
            const auto& left = tokens.size() > 3U ? tokens[3]
                : tokens.size() > 1U ? tokens[1] : tokens[0];
            parse_width(top, style.border_top_width);
            parse_width(right, style.border_right_width);
            parse_width(bottom, style.border_bottom_width);
            parse_width(left, style.border_left_width);
        };
        const auto assign_colors = [&](const std::vector<std::string>& tokens) {
            if (tokens.empty() || tokens.size() > 4U) return;
            const auto& top = tokens[0];
            const auto& right = tokens.size() > 1U ? tokens[1] : tokens[0];
            const auto& bottom = tokens.size() > 2U ? tokens[2] : tokens[0];
            const auto& left = tokens.size() > 3U ? tokens[3]
                : tokens.size() > 1U ? tokens[1] : tokens[0];
            style.border_top_rgba = parse_color(top);
            style.border_right_rgba = parse_color(right);
            style.border_bottom_rgba = parse_color(bottom);
            style.border_left_rgba = parse_color(left);
        };
        if (name == "border") {
            parse_side(style.border_top_width, style.border_top_rgba);
            parse_side(style.border_right_width, style.border_right_rgba);
            parse_side(style.border_bottom_width, style.border_bottom_rgba);
            parse_side(style.border_left_width, style.border_left_rgba);
            return true;
        }
        if (name == "border-top") {
            parse_side(style.border_top_width, style.border_top_rgba);
            return true;
        }
        if (name == "border-right") {
            parse_side(style.border_right_width, style.border_right_rgba);
            return true;
        }
        if (name == "border-bottom") {
            parse_side(style.border_bottom_width, style.border_bottom_rgba);
            return true;
        }
        if (name == "border-left") {
            parse_side(style.border_left_width, style.border_left_rgba);
            return true;
        }
        if (name == "border-width") {
            if (value.empty()) {
                style.border_top_width = {};
                style.border_right_width = {};
                style.border_bottom_width = {};
                style.border_left_width = {};
            } else {
                assign_widths(split_transition_tokens(value));
            }
            return true;
        }
        if (name == "border-color") {
            if (value.empty()) {
                style.border_top_rgba = 0;
                style.border_right_rgba = 0;
                style.border_bottom_rgba = 0;
                style.border_left_rgba = 0;
            } else if (value.find("rgb(") != std::string::npos
                || value.find("rgba(") != std::string::npos) {
                const auto color = native_document::parse_color(value);
                style.border_top_rgba = color;
                style.border_right_rgba = color;
                style.border_bottom_rgba = color;
                style.border_left_rgba = color;
            } else {
                assign_colors(split_transition_tokens(value));
            }
            return true;
        }
        if (name == "border-style") {
            if (value.empty() || value == "none") {
                style.border_top_width = {};
                style.border_right_width = {};
                style.border_bottom_width = {};
                style.border_left_width = {};
            }
            return true;
        }
        const auto set_width = [&](std::string_view candidate, css_length& width) {
            if (name != candidate) return false;
            if (value.empty()) width = {};
            else parse_width(value, width);
            return true;
        };
        if (set_width("border-top-width", style.border_top_width)
            || set_width("border-right-width", style.border_right_width)
            || set_width("border-bottom-width", style.border_bottom_width)
            || set_width("border-left-width", style.border_left_width)) {
            return true;
        }
        const auto set_color = [&](std::string_view candidate, uint32_t& color) {
            if (name != candidate) return false;
            color = value.empty() ? 0 : parse_color(value);
            return true;
        };
        return set_color("border-top-color", style.border_top_rgba)
            || set_color("border-right-color", style.border_right_rgba)
            || set_color("border-bottom-color", style.border_bottom_rgba)
            || set_color("border-left-color", style.border_left_rgba);
    }

    static bool is_css_time(std::string_view value)
    {
        return value.ends_with("ms") || value.ends_with('s');
    }

    static float parse_css_time_ms(std::string value)
    {
        value = trim_css(std::move(value));
        auto multiplier = 1.0F;
        if (value.ends_with("ms")) value.resize(value.size() - 2U);
        else if (value.ends_with('s')) {
            value.pop_back();
            multiplier = 1000.0F;
        } else return 0;
        return std::strtof(value.c_str(), nullptr) * multiplier;
    }

    static void parse_transition_timing(
        const std::string& value,
        node_style::transition_timing& timing)
    {
        const auto lower = lower_html_name(value);
        if (lower == "linear") {
            timing.x1 = 0; timing.y1 = 0; timing.x2 = 1; timing.y2 = 1;
        } else if (lower == "ease-in") {
            timing.x1 = 0.42F; timing.y1 = 0; timing.x2 = 1; timing.y2 = 1;
        } else if (lower == "ease-out") {
            timing.x1 = 0; timing.y1 = 0; timing.x2 = 0.58F; timing.y2 = 1;
        } else if (lower == "ease-in-out") {
            timing.x1 = 0.42F; timing.y1 = 0; timing.x2 = 0.58F; timing.y2 = 1;
        } else if (lower.starts_with("cubic-bezier(") && lower.ends_with(')')) {
            auto points = lower.substr(13U, lower.size() - 14U);
            std::replace(points.begin(), points.end(), ',', ' ');
            std::istringstream stream(points);
            float x1 = 0.25F, y1 = 0.1F, x2 = 0.25F, y2 = 1;
            if (stream >> x1 >> y1 >> x2 >> y2) {
                timing.x1 = std::clamp(x1, 0.0F, 1.0F);
                timing.y1 = y1;
                timing.x2 = std::clamp(x2, 0.0F, 1.0F);
                timing.y2 = y2;
            }
        }
    }

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

    static void configure_style_transitions(node_style& style)
    {
        if (!style.has_animation_data()) return;
        auto& animations = style.mutable_animations();
        const auto properties = split_css_component_list(animations.transition_property_value, ',');
        const auto durations = split_css_component_list(animations.transition_duration_value, ',');
        const auto delays = split_css_component_list(animations.transition_delay_value, ',');
        const auto timings = split_css_component_list(
            animations.transition_timing_function_value, ',');
        const auto resolve = [&](std::string_view property) {
            node_style::transition_timing result;
            for (size_t index = 0; index < properties.size(); ++index) {
                const auto candidate = lower_html_name(trim_css(properties[index]));
                if (candidate != property && candidate != "all") continue;
                if (!durations.empty()) {
                    result.duration_ms = std::max(
                        0.0F, parse_css_time_ms(durations[index % durations.size()]));
                }
                if (!delays.empty()) {
                    result.delay_ms = parse_css_time_ms(delays[index % delays.size()]);
                }
                if (!timings.empty()) {
                    parse_transition_timing(timings[index % timings.size()], result);
                }
                return result;
            }
            return result;
        };
        animations.transform_transition = resolve("transform");
        animations.opacity_transition = resolve("opacity");
        animations.color_transition = resolve("color");
    }

    static void apply_transition_shorthand(node_style& style, const std::string& value)
    {
        std::vector<std::string> properties;
        std::vector<std::string> durations;
        std::vector<std::string> delays;
        std::vector<std::string> timings;
        for (const auto& item : split_css_component_list(value, ',')) {
            auto property = std::string("all");
            auto duration = std::string("0s");
            auto delay = std::string("0s");
            auto timing = std::string("ease");
            auto saw_time = false;
            for (const auto& token : split_transition_tokens(item)) {
                const auto lower = lower_html_name(token);
                if (is_css_time(lower)) {
                    if (!saw_time) duration = lower;
                    else delay = lower;
                    saw_time = true;
                } else if (lower == "linear" || lower == "ease" || lower == "ease-in"
                    || lower == "ease-out" || lower == "ease-in-out"
                    || lower.starts_with("cubic-bezier(")) {
                    timing = lower;
                } else if (lower != "normal") {
                    property = lower;
                }
            }
            properties.push_back(std::move(property));
            durations.push_back(std::move(duration));
            delays.push_back(std::move(delay));
            timings.push_back(std::move(timing));
        }
        const auto join = [](const std::vector<std::string>& values) {
            std::string result;
            for (const auto& value : values) {
                if (!result.empty()) result += ", ";
                result += value;
            }
            return result;
        };
        auto& animations = style.mutable_animations();
        animations.transition_property_value = join(properties);
        animations.transition_duration_value = join(durations);
        animations.transition_delay_value = join(delays);
        animations.transition_timing_function_value = join(timings);
        configure_style_transitions(style);
    }

    static bool apply_grid_placement_declaration(
        node_style& style,
        std::string_view raw_name,
        const std::string& value)
    {
        const auto name = canonical_css_property_name(raw_name);
        if (name != "grid-area" && name != "grid-row"
            && name != "grid-row-start" && name != "grid-row-end"
            && name != "grid-column" && name != "grid-column-start"
            && name != "grid-column-end") {
            return false;
        }
        auto& grid = style.mutable_grid();
        const auto update_column_layout = [&] {
            grid.column_value = grid.column_start_value == "auto"
                && grid.column_end_value == "auto"
                ? "auto"
                : grid.column_start_value + " / " + grid.column_end_value;
            grid.span_all = grid.column_end_value != "auto";
            const auto& start = grid.column_start_value;
            grid.column_start = !start.empty()
                && std::all_of(start.begin(), start.end(), [](unsigned char character) {
                    return std::isdigit(character) != 0;
                }) ? std::atoi(start.c_str()) : 0;
        };
        if (name == "grid-row-start") {
            grid.row_start_value = value;
            return true;
        }
        if (name == "grid-row-end") {
            grid.row_end_value = value;
            return true;
        }
        if (name == "grid-column-start") {
            grid.column_start_value = value;
            update_column_layout();
            return true;
        }
        if (name == "grid-column-end") {
            grid.column_end_value = value;
            update_column_layout();
            return true;
        }
        const auto components = split_css_component_list(value, '/');
        const auto maximum = name == "grid-area" ? 4U : 2U;
        if (components.empty() || components.size() > maximum) return true;
        const auto component = [&](size_t index) {
            return index < components.size() ? components[index] : std::string{"auto"};
        };
        if (name == "grid-area") {
            grid.area_value = value;
            grid.row_start_value = component(0);
            grid.column_start_value = component(1);
            grid.row_end_value = component(2);
            grid.column_end_value = component(3);
            grid.row_value =
                grid.row_start_value + " / " + grid.row_end_value;
            update_column_layout();
            return true;
        }
        if (name == "grid-row") {
            grid.row_value = value;
            grid.row_start_value = component(0);
            grid.row_end_value = component(1);
            return true;
        }

        grid.column_start_value = component(0);
        grid.column_end_value = component(1);
        update_column_layout();
        // Preserve the authored one-component shorthand serialization rather
        // than inflating `2` to `2 / auto`.
        grid.column_value = value;
        return true;
    }

    static void apply_animation_shorthand(node_style& style, const std::string& value)
    {
        auto name = std::string("none");
        auto duration = std::string("0s");
        auto delay = std::string("0s");
        auto timing = std::string("ease");
        auto iterations = std::string("1");
        auto saw_time = false;
        const auto first = split_css_component_list(value, ',');
        for (const auto& token : split_transition_tokens(
                 first.empty() ? std::string_view{} : std::string_view(first.front()))) {
            const auto lower = lower_html_name(token);
            if (is_css_time(lower)) {
                if (!saw_time) duration = lower;
                else delay = lower;
                saw_time = true;
            } else if (lower == "linear" || lower == "ease" || lower == "ease-in"
                || lower == "ease-out" || lower == "ease-in-out"
                || lower.starts_with("cubic-bezier(")) {
                timing = lower;
            } else if (lower == "infinite"
                || std::all_of(lower.begin(), lower.end(), [](unsigned char character) {
                    return std::isdigit(character) || character == '.';
                })) {
                iterations = lower;
            } else if (lower != "normal" && lower != "none"
                && lower != "forwards" && lower != "backwards" && lower != "both"
                && lower != "running" && lower != "paused"
                && lower != "alternate" && lower != "alternate-reverse"
                && lower != "reverse") {
                name = token;
            }
        }
        auto& animations = style.mutable_animations();
        animations.animation_name_value = name;
        animations.animation_duration_value = duration;
        animations.animation_delay_value = delay;
        animations.animation_timing_function_value = timing;
        animations.animation_iteration_count_value = iterations;
    }

    void configure_style_keyframe_animation(dom_node& node)
    {
        auto& style = node.style;
        if (!style.has_animation_data()) return;
        auto& animations = style.mutable_animations();
        animations.opacity_keyframes.clear();
        animations.opacity_keyframe_animation_signature.clear();
        animations.rotation_keyframes.clear();
        animations.rotation_keyframe_animation_signature.clear();
        if (animations.animation_name_value == "none") return;
        const auto names = split_css_component_list(animations.animation_name_value, ',');
        if (names.empty()) return;
        const auto name = lower_html_name(trim_css(names.front()));
        const auto definition = opacity_keyframes.find(name);
        if (name == "none" || definition == opacity_keyframes.end()) return;
        const auto durations = split_css_component_list(animations.animation_duration_value, ',');
        const auto delays = split_css_component_list(animations.animation_delay_value, ',');
        const auto timings = split_css_component_list(
            animations.animation_timing_function_value, ',');
        const auto iteration_counts = split_css_component_list(
            animations.animation_iteration_count_value, ',');
        animations.opacity_keyframe_duration_ms = durations.empty()
            ? 0 : std::max(0.0F, parse_css_time_ms(durations.front()));
        animations.opacity_keyframe_delay_ms = delays.empty()
            ? 0 : parse_css_time_ms(delays.front());
        const auto iteration = iteration_counts.empty()
            ? std::string("1") : lower_html_name(trim_css(iteration_counts.front()));
        animations.opacity_keyframe_iterations = iteration == "infinite"
            ? std::numeric_limits<float>::infinity()
            : std::max(0.0F, std::strtof(iteration.c_str(), nullptr));
        node_style::transition_timing animation_timing;
        if (!timings.empty()) parse_transition_timing(timings.front(), animation_timing);
        animations.opacity_keyframe_x1 = animation_timing.x1;
        animations.opacity_keyframe_y1 = animation_timing.y1;
        animations.opacity_keyframe_x2 = animation_timing.x2;
        animations.opacity_keyframe_y2 = animation_timing.y2;
        animations.opacity_keyframes = definition->second.opacity_stops;
        animations.rotation_keyframes = definition->second.rotation_stops;
        if (animations.opacity_keyframe_duration_ms <= 0
            || animations.opacity_keyframe_iterations == 0
            || (animations.opacity_keyframes.size() < 2U
                && animations.rotation_keyframes.size() < 2U)) return;
        std::ostringstream signature;
        signature << name << '|' << animations.opacity_keyframe_duration_ms << '|'
            << animations.opacity_keyframe_delay_ms << '|'
            << animations.opacity_keyframe_iterations << '|'
            << animations.opacity_keyframe_x1 << ',' << animations.opacity_keyframe_y1 << ','
            << animations.opacity_keyframe_x2 << ',' << animations.opacity_keyframe_y2;
        const auto base_signature = signature.str();
        if (animations.opacity_keyframes.size() >= 2U) {
            signature.str(base_signature);
            signature.clear();
            for (const auto& stop : animations.opacity_keyframes) {
                signature << '|' << stop.offset << ':' << stop.opacity;
            }
            animations.opacity_keyframe_animation_signature = signature.str();
            record_composition("css-opacity-keyframe-animation", node.id);
        }
        if (animations.rotation_keyframes.size() >= 2U) {
            signature.str(base_signature);
            signature.clear();
            for (const auto& stop : animations.rotation_keyframes) {
                signature << '|' << stop.offset << ':' << stop.degrees;
            }
            animations.rotation_keyframe_animation_signature = signature.str();
            record_composition("css-rotation-keyframe-animation", node.id);
        }
    }

    void apply_css_declaration(dom_node& node, const css_declaration& declaration)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (profile_css) {
            ++css_profile_declaration_applications;
        }
#endif
        const auto& name = declaration.name;
        if (name == "-moz-transform" || name == "-webkit-transform"
            || name == "grid-gap") {
            feature_decision alias_decision(
                *this,
                "css",
                "property:" + name,
                "style-application");
            auto normalized = declaration;
            normalized.name = name == "grid-gap" ? "gap" : "transform";
            apply_css_declaration(node, normalized);
            return;
        }
        if (name.starts_with("--")) {
            const auto& custom = node.style.custom_properties();
            const auto existing_is_important =
                custom.important.contains(name);
            const auto existing_is_inline =
                node.authored_style().declarations.contains(name)
                && custom.values.contains(name);
            const auto inline_is_important =
                existing_is_inline
                && node.authored_style().important_declarations.contains(name);
            if (inline_is_important
                || (!declaration.important && existing_is_important)
                || (!declaration.important && existing_is_inline)) {
                record_feature(
                    "css",
                    "custom-property",
                    "supported",
                    {},
                    "style-application");
                return;
            }
            auto& mutable_custom = node.style.mutable_custom_properties();
            mutable_custom.values[name] = declaration.value;
            if (declaration.important) mutable_custom.important.insert(name);
            else mutable_custom.important.erase(name);
            record_feature(
                "css",
                "custom-property",
                "supported",
                {},
                "style-application");
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

            // `all: unset` is a common component-control reset. Start from the
            // modeled initial/inherited sentinels, then restore declarations
            // from the higher-priority inline origin. Custom properties are
            // deliberately excluded from `all` by CSS Cascade and therefore
            // survive the reset. Generated pseudo-element state belongs to its
            // own selector subject and must not be cleared with the principal
            // element's declarations.
            auto previous = node.style;
            auto reset = node_style{};
            reset.display = default_display_for_node(node);
            reset.inline_property_mask = previous.inline_property_mask;
            reset.important_property_mask = previous.important_property_mask;
            reset.move_custom_properties_from(previous);
            reset.move_pseudo_elements_from(previous);

            const auto has_inline = [&](std::initializer_list<std::string_view> names) {
                return std::any_of(names.begin(), names.end(), [&](std::string_view candidate) {
                    return node.authored_style().declarations.contains(
                        std::string(candidate));
                });
            };
            if (is_inline(inline_width)) reset.width = previous.width;
            if (is_inline(inline_height)) reset.height = previous.height;
            if (is_inline(inline_min_width)) reset.min_width = previous.min_width;
            if (is_inline(inline_min_height)) reset.min_height = previous.min_height;
            if (is_inline(inline_max_width)) reset.max_width = previous.max_width;
            if (is_inline(inline_max_height)) reset.max_height = previous.max_height;
            if (is_inline(inline_left)) reset.left = previous.left;
            if (is_inline(inline_top)) reset.top = previous.top;
            if (is_inline(inline_right)) reset.right = previous.right;
            if (is_inline(inline_bottom)) reset.bottom = previous.bottom;
            if (is_inline(inline_display)) reset.display = previous.display;
            if (is_inline(inline_position)) reset.position = previous.position;
            if (is_inline(inline_padding)) {
                reset.padding_left = previous.padding_left;
                reset.padding_top = previous.padding_top;
                reset.padding_right = previous.padding_right;
                reset.padding_bottom = previous.padding_bottom;
            }
            if (is_inline(inline_margin)) {
                reset.margin_left = previous.margin_left;
                reset.margin_top = previous.margin_top;
                reset.margin_right = previous.margin_right;
                reset.margin_bottom = previous.margin_bottom;
                reset.margin_left_auto = previous.margin_left_auto;
                reset.margin_top_auto = previous.margin_top_auto;
                reset.margin_right_auto = previous.margin_right_auto;
                reset.margin_bottom_auto = previous.margin_bottom_auto;
            }
            if (is_inline(inline_gap)) {
                reset.row_gap = previous.row_gap;
                reset.column_gap = previous.column_gap;
            }
            if (is_inline(inline_table_border_model)) {
                reset.mutable_table() = previous.table();
            }
            if (is_inline(inline_flex_direction)) {
                reset.direction = previous.direction;
                reset.flex_reverse = previous.flex_reverse;
            }
            if (is_inline(inline_flex_grow)) reset.flex_grow = previous.flex_grow;
            if (is_inline(inline_flex_shrink)) reset.flex_shrink = previous.flex_shrink;
            if (is_inline(inline_flex_basis)) reset.flex_basis = previous.flex_basis;
            if (is_inline(inline_flex_wrap)) reset.flex_wrap = previous.flex_wrap;
            if (is_inline(inline_align_items)) reset.align_items = previous.align_items;
            if (is_inline(inline_align_self)) {
                reset.align_self = previous.align_self;
                reset.align_self_specified = previous.align_self_specified;
            }
            if (is_inline(inline_justify_content)) {
                reset.justify_content = previous.justify_content;
            }
            if (is_inline(inline_box_sizing)) reset.border_box = previous.border_box;
            if (is_inline(inline_border_radius)) {
                reset.border_top_left_radius = previous.border_top_left_radius;
                reset.border_top_right_radius = previous.border_top_right_radius;
                reset.border_bottom_right_radius = previous.border_bottom_right_radius;
                reset.border_bottom_left_radius = previous.border_bottom_left_radius;
                reset.set_vertical_corner_radii(
                    previous.border_top_left_radius_y(),
                    previous.border_top_right_radius_y(),
                    previous.border_bottom_right_radius_y(),
                    previous.border_bottom_left_radius_y());
            }
            if (is_inline(inline_box_shadow)) {
                reset.box_shadow_offset_x = previous.box_shadow_offset_x;
                reset.box_shadow_offset_y = previous.box_shadow_offset_y;
                reset.box_shadow_blur_radius = previous.box_shadow_blur_radius;
                reset.box_shadow_spread_radius = previous.box_shadow_spread_radius;
                reset.box_shadow_rgba = previous.box_shadow_rgba;
                reset.box_shadow_present = previous.box_shadow_present;
            }
            if (is_inline(inline_transform)) {
                reset.transform_translate_x = previous.transform_translate_x;
                reset.transform_translate_y = previous.transform_translate_y;
                reset.transform_scale_x = previous.transform_scale_x;
                reset.transform_scale_y = previous.transform_scale_y;
                reset.transform_rotate_degrees = previous.transform_rotate_degrees;
                reset.transform_specified = previous.transform_specified;
            }
            if (is_inline(inline_transform_origin)) {
                reset.transform_origin_x = previous.transform_origin_x;
                reset.transform_origin_y = previous.transform_origin_y;
                reset.transform_origin_specified = previous.transform_origin_specified;
            }
            if (is_inline(inline_background)) {
                reset.background_rgba = previous.background_rgba;
                reset.background_current_color = previous.background_current_color;
            }
            if (is_inline(inline_overflow)) {
                reset.overflow_x = previous.overflow_x;
                reset.overflow_y = previous.overflow_y;
                reset.clip = previous.clip;
                reset.scroll_x_enabled = previous.scroll_x_enabled;
                reset.scroll_y_enabled = previous.scroll_y_enabled;
            }
            if (is_inline(inline_visibility)) {
                reset.visibility_hidden = previous.visibility_hidden;
                reset.visibility_specified = previous.visibility_specified;
            }
            if (is_inline(inline_pointer_events)) {
                reset.pointer_events_none = previous.pointer_events_none;
                reset.pointer_events_specified = previous.pointer_events_specified;
            }
            if (is_inline(inline_opacity)) reset.opacity = previous.opacity;
            if (is_inline(inline_color)) reset.foreground_rgba = previous.foreground_rgba;
            if (is_inline(inline_font_size)) reset.font_size = previous.font_size;
            if (is_inline(inline_font_family)) {
                reset.mutable_textual().font_family =
                    previous.textual().font_family;
            }
            if (is_inline(inline_font_smoothing)) {
                reset.mutable_textual().font_smoothing =
                    previous.textual().font_smoothing;
            }
            if (is_inline(inline_font_weight)) reset.font_weight = previous.font_weight;
            if (is_inline(inline_line_height)) reset.line_height = previous.line_height;
            if (is_inline(inline_letter_spacing)) {
                reset.letter_spacing = previous.letter_spacing;
                reset.letter_spacing_specified = previous.letter_spacing_specified;
            }
            if (is_inline(inline_word_spacing)) {
                reset.word_spacing = previous.word_spacing;
                reset.word_spacing_specified = previous.word_spacing_specified;
            }
            if (is_inline(inline_text_align)) {
                reset.mutable_textual().text_align =
                    previous.textual().text_align;
            }
            if (is_inline(inline_white_space)) {
                reset.mutable_textual().white_space =
                    previous.textual().white_space;
            }

            // These modeled properties do not yet have dedicated inline-mask
            // bits, so preserve their applied values by authored declaration.
            if (has_inline({"background-image", "background-repeat",
                    "background-position", "background-size"})) {
                reset.mutable_background_image() = previous.background_image();
            }
            if (has_inline({"border", "border-top", "border-right", "border-bottom",
                    "border-left", "border-width", "border-color",
                    "border-top-width", "border-right-width", "border-bottom-width",
                    "border-left-width", "border-top-color", "border-right-color",
                    "border-bottom-color", "border-left-color"})) {
                reset.border_left_width = previous.border_left_width;
                reset.border_top_width = previous.border_top_width;
                reset.border_right_width = previous.border_right_width;
                reset.border_bottom_width = previous.border_bottom_width;
                reset.border_left_rgba = previous.border_left_rgba;
                reset.border_top_rgba = previous.border_top_rgba;
                reset.border_right_rgba = previous.border_right_rgba;
                reset.border_bottom_rgba = previous.border_bottom_rgba;
            }
            if (has_inline({"outline", "outline-width", "outline-color"})) {
                reset.outline_width = previous.outline_width;
                reset.outline_rgba = previous.outline_rgba;
            }
            if (has_inline({"transition", "transition-property", "transition-duration",
                    "transition-delay", "transition-timing-function"})) {
                auto& reset_animations = reset.mutable_animations();
                reset_animations.transition_property_value = previous.animations().transition_property_value;
                reset_animations.transition_duration_value = previous.animations().transition_duration_value;
                reset_animations.transition_delay_value = previous.animations().transition_delay_value;
                reset_animations.transition_timing_function_value =
                    previous.animations().transition_timing_function_value;
                configure_style_transitions(reset);
            }
            if (has_inline({"z-index"})) {
                reset.z_index = previous.z_index;
                reset.z_index_auto = previous.z_index_auto;
            }
            if (has_inline({"flex-basis", "flex"})) reset.flex_basis = previous.flex_basis;
            if (has_inline({"grid-template-columns", "grid-template-rows",
                    "grid-auto-columns", "grid-auto-flow",
                    "grid-area", "grid-row", "grid-row-start", "grid-row-end",
                    "grid-column", "grid-column-start", "grid-column-end"})) {
                reset.mutable_grid() = previous.grid();
            }
            if (has_inline({"table-layout"})) reset.table_layout_fixed = previous.table_layout_fixed;
            if (has_inline({"text-transform"})) {
                reset.mutable_textual().text_transform =
                    previous.textual().text_transform;
            }
            if (has_inline({"cursor"})) {
                reset.mutable_textual().cursor =
                    previous.textual().cursor;
            }
            if (has_inline({"list-style", "list-style-position", "list-style-type"})) {
                reset.mutable_textual().list_style_position =
                    previous.textual().list_style_position;
                reset.mutable_textual().list_style_type =
                    previous.textual().list_style_type;
            }

            node.style = std::move(reset);
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
            return lower_html_name(color_value) == "currentcolor"
                ? node.style.foreground_rgba
                : native_document::parse_color(color_value);
        };
        const auto parse_border = [&](css_length& width, uint32_t& color) {
            auto remaining = value;
            auto color_start = remaining.find("rgba(");
            if (color_start == std::string::npos) color_start = remaining.find("rgb(");
            if (color_start != std::string::npos) {
                const auto color_end = remaining.find(')', color_start);
                if (color_end != std::string::npos) {
                    color = native_document::parse_color(
                        remaining.substr(color_start, color_end - color_start + 1U));
                    remaining.erase(color_start, color_end - color_start + 1U);
                }
            }
            std::istringstream stream(remaining);
            for (std::string token; stream >> token;) {
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
                    if (parsed != 0U || token == "transparent"
                        || lower_html_name(token) == "currentcolor") color = parsed;
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
            parse_border(node.style.border_top_width, node.style.border_top_rgba);
            parse_border(node.style.border_right_width, node.style.border_right_rgba);
            parse_border(node.style.border_bottom_width, node.style.border_bottom_rgba);
            parse_border(node.style.border_left_width, node.style.border_left_rgba);
            return;
        } else if (name == "border-top" || name == "border-block-start") {
            parse_border(node.style.border_top_width, node.style.border_top_rgba);
            return;
        } else if (name == "border-right" || name == "border-inline-end") {
            parse_border(node.style.border_right_width, node.style.border_right_rgba);
            return;
        } else if (name == "border-bottom" || name == "border-block-end") {
            parse_border(node.style.border_bottom_width, node.style.border_bottom_rgba);
            return;
        } else if (name == "border-left" || name == "border-inline-start") {
            parse_border(node.style.border_left_width, node.style.border_left_rgba);
            return;
        } else if (name == "border-inline") {
            parse_border(node.style.border_left_width, node.style.border_left_rgba);
            parse_border(node.style.border_right_width, node.style.border_right_rgba);
            return;
        } else if (name == "border-block") {
            parse_border(node.style.border_top_width, node.style.border_top_rgba);
            parse_border(node.style.border_bottom_width, node.style.border_bottom_rgba);
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
            parse_border(node.style.outline_width, node.style.outline_rgba);
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
            return;
        } else if (name == "border-right-color"
            || name == "border-inline-end-color") {
            node.style.border_right_rgba = parse_declared_color(value);
            return;
        } else if (name == "border-bottom-color"
            || name == "border-block-end-color") {
            node.style.border_bottom_rgba = parse_declared_color(value);
            return;
        } else if (name == "border-left-color"
            || name == "border-inline-start-color") {
            node.style.border_left_rgba = parse_declared_color(value);
            return;
        } else if (name == "border-inline-color") {
            node.style.border_left_rgba = parse_declared_color(value);
            node.style.border_right_rgba = node.style.border_left_rgba;
            return;
        } else if (name == "border-block-color") {
            node.style.border_top_rgba = parse_declared_color(value);
            node.style.border_bottom_rgba = node.style.border_top_rgba;
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
        if (name == "width" && !is_inline(inline_width)) {
            node.style.width = value == "inherit" && node.parent != nullptr
                ? node.parent->style.width
                : value == "initial" || value == "unset" || value == "revert"
                    ? css_length{}
                    : native_document::parse_length(value);
        } else if (name == "height" && !is_inline(inline_height)) {
            node.style.height = value == "inherit" && node.parent != nullptr
                ? node.parent->style.height
                : value == "initial" || value == "unset" || value == "revert"
                    ? css_length{}
                    : native_document::parse_length(value);
        } else if (name == "min-width" && !is_inline(inline_min_width)) {
            node.style.min_width = native_document::parse_length(value);
        } else if (name == "min-height" && !is_inline(inline_min_height)) {
            node.style.min_height = native_document::parse_length(value);
        } else if (name == "max-width" && !is_inline(inline_max_width)) {
            node.style.max_width = value == "none" || value == "fit-content"
                || value == "max-content" || value == "min-content"
                ? css_length{} : native_document::parse_length(value);
        } else if (name == "max-height" && !is_inline(inline_max_height)) {
            node.style.max_height = value == "none" || value == "fit-content"
                || value == "max-content" || value == "min-content"
                ? css_length{} : native_document::parse_length(value);
        } else if ((name == "left" || name == "inset-inline-start") && !is_inline(inline_left)) {
            node.style.left = parse_inset_length(value);
        } else if ((name == "top" || name == "inset-block-start") && !is_inline(inline_top)) {
            node.style.top = parse_inset_length(value);
        } else if ((name == "right" || name == "inset-inline-end") && !is_inline(inline_right)) {
            node.style.right = parse_inset_length(value);
        } else if ((name == "bottom" || name == "inset-block-end") && !is_inline(inline_bottom)) {
            node.style.bottom = parse_inset_length(value);
        }
        else if (name == "inset") {
            std::vector<css_length> values;
            std::istringstream stream(value);
            for (std::string token; stream >> token;) {
                values.push_back(native_document::parse_length(token));
            }
            if (!values.empty()) {
                const auto top = values[0];
                const auto right = values.size() > 1 ? values[1] : values[0];
                const auto bottom = values.size() > 2 ? values[2] : values[0];
                const auto left = values.size() > 3 ? values[3]
                    : values.size() > 1 ? values[1] : values[0];
                if (!is_inline(inline_left)) node.style.left = left;
                if (!is_inline(inline_top)) node.style.top = top;
                if (!is_inline(inline_right)) node.style.right = right;
                if (!is_inline(inline_bottom)) node.style.bottom = bottom;
            }
        } else if (name == "padding" && !is_inline(inline_padding)) {
            std::vector<css_length> values;
            size_t cursor = 0;
            while (cursor < value.size()) {
                while (cursor < value.size()
                    && std::isspace(static_cast<unsigned char>(value[cursor]))) ++cursor;
                if (cursor >= value.size()) break;
                auto end = value.find(' ', cursor);
                if (end == std::string::npos) end = value.size();
                values.push_back(native_document::parse_length(
                    value.substr(cursor, end - cursor)));
                cursor = end + 1U;
            }
            if (!values.empty()) {
                node.style.padding_top = values[0];
                node.style.padding_right = values.size() > 1 ? values[1] : values[0];
                node.style.padding_bottom = values.size() > 2 ? values[2] : values[0];
                node.style.padding_left = values.size() > 3 ? values[3]
                    : values.size() > 1 ? values[1] : values[0];
            }
        } else if ((name == "padding-block" || name == "padding-inline")
            && !is_inline(inline_padding)) {
            std::istringstream stream(value);
            std::string first;
            std::string second;
            stream >> first >> second;
            if (!first.empty()) {
                const auto start = native_document::parse_length(first);
                const auto end = native_document::parse_length(second.empty() ? first : second);
                if (name == "padding-block") {
                    node.style.padding_top = start;
                    node.style.padding_bottom = end;
                } else {
                    node.style.padding_left = start;
                    node.style.padding_right = end;
                }
            }
        } else if ((name == "padding-left" || name == "padding-inline-start")
            && !is_inline(inline_padding)) {
            node.style.padding_left = native_document::parse_length(value);
        } else if ((name == "padding-right" || name == "padding-inline-end")
            && !is_inline(inline_padding)) {
            node.style.padding_right = native_document::parse_length(value);
        } else if ((name == "padding-top" || name == "padding-block-start")
            && !is_inline(inline_padding)) {
            node.style.padding_top = native_document::parse_length(value);
        } else if ((name == "padding-bottom" || name == "padding-block-end")
            && !is_inline(inline_padding)) {
            node.style.padding_bottom = native_document::parse_length(value);
        } else if (name == "margin" && !is_inline(inline_margin)) {
            std::vector<css_length> values;
            std::vector<bool> automatic;
            size_t cursor = 0;
            while (cursor < value.size()) {
                while (cursor < value.size()
                    && std::isspace(static_cast<unsigned char>(value[cursor]))) ++cursor;
                if (cursor >= value.size()) break;
                auto end = value.find(' ', cursor);
                if (end == std::string::npos) end = value.size();
                const auto token = value.substr(cursor, end - cursor);
                values.push_back(native_document::parse_length(token));
                automatic.push_back(token == "auto");
                cursor = end + 1U;
            }
            if (!values.empty()) {
                node.style.margin_top = values[0];
                node.style.margin_right = values.size() > 1 ? values[1] : values[0];
                node.style.margin_bottom = values.size() > 2 ? values[2] : values[0];
                node.style.margin_left = values.size() > 3 ? values[3]
                    : values.size() > 1 ? values[1] : values[0];
                node.style.margin_top_auto = automatic[0];
                node.style.margin_right_auto = automatic.size() > 1 ? automatic[1] : automatic[0];
                node.style.margin_bottom_auto = automatic.size() > 2 ? automatic[2] : automatic[0];
                node.style.margin_left_auto = automatic.size() > 3 ? automatic[3]
                    : automatic.size() > 1 ? automatic[1] : automatic[0];
            }
        } else if ((name == "margin-block" || name == "margin-inline")
            && !is_inline(inline_margin)) {
            std::istringstream stream(value);
            std::string first;
            std::string second;
            stream >> first >> second;
            if (!first.empty()) {
                const auto start = native_document::parse_length(first);
                const auto end = native_document::parse_length(second.empty() ? first : second);
                if (name == "margin-block") {
                    node.style.margin_top = start;
                    node.style.margin_bottom = end;
                    node.style.margin_top_auto = first == "auto";
                    node.style.margin_bottom_auto = (second.empty() ? first : second) == "auto";
                } else {
                    node.style.margin_left = start;
                    node.style.margin_right = end;
                    node.style.margin_left_auto = first == "auto";
                    node.style.margin_right_auto = (second.empty() ? first : second) == "auto";
                }
            }
        } else if ((name == "margin-left" || name == "margin-inline-start")
            && !is_inline(inline_margin)) {
            node.style.margin_left = native_document::parse_length(value);
            node.style.margin_left_auto = value == "auto";
        } else if ((name == "margin-right" || name == "margin-inline-end")
            && !is_inline(inline_margin)) {
            node.style.margin_right = native_document::parse_length(value);
            node.style.margin_right_auto = value == "auto";
        } else if ((name == "margin-top" || name == "margin-block-start")
            && !is_inline(inline_margin)) {
            node.style.margin_top = native_document::parse_length(value);
            node.style.margin_top_auto = value == "auto";
        } else if ((name == "margin-bottom" || name == "margin-block-end")
            && !is_inline(inline_margin)) {
            node.style.margin_bottom = native_document::parse_length(value);
            node.style.margin_bottom_auto = value == "auto";
        } else if (name == "gap" && !is_inline(inline_gap)) {
            std::istringstream stream(value);
            std::string row;
            std::string column;
            stream >> row >> column;
            node.style.row_gap = row == "normal" ? css_length{}
                : native_document::parse_length(row);
            node.style.column_gap = column.empty() ? node.style.row_gap
                : column == "normal" ? css_length{}
                : native_document::parse_length(column);
        } else if (name == "row-gap" && !is_inline(inline_gap)) {
            node.style.row_gap = value == "normal" ? css_length{}
                : native_document::parse_length(value);
        } else if (name == "column-gap" && !is_inline(inline_gap)) {
            node.style.column_gap = value == "normal" ? css_length{}
                : native_document::parse_length(value);
        } else if (name == "border-spacing"
            && !is_inline(inline_table_border_model)) {
            std::istringstream stream(value);
            std::string horizontal;
            std::string vertical;
            stream >> horizontal >> vertical;
            if (horizontal.empty()) {
                decision.classification = "invalid-authoring";
            } else {
                auto& table = node.style.mutable_table();
                table.border_spacing_horizontal = native_document::parse_length(horizontal);
                table.border_spacing_vertical = native_document::parse_length(
                    vertical.empty() ? horizontal : vertical);
            }
        } else if (name == "border-collapse"
            && !is_inline(inline_table_border_model)) {
            if (value == "collapse" || value == "separate") {
                node.style.mutable_table().border_collapsed = value == "collapse";
            } else {
                decision.classification = "unsupported";
                decision.semantic_slice = "collapse and separate keywords";
            }
        } else if (name == "display" && !is_inline(inline_display)) {
            node.style.display = parse_display_mode(value);
        } else if (name == "table-layout") {
            node.style.table_layout_fixed = value == "fixed";
            if (value != "fixed" && value != "auto") {
                decision.classification = "unsupported";
                decision.semantic_slice = "auto and fixed keywords";
            }
        } else if (name == "grid-template-columns" && !is_inline(inline_grid)) {
            auto& grid = node.style.mutable_grid();
            grid.template_columns = parse_simple_grid_tracks(value);
            grid.two_columns = grid.template_columns.size() > 1U
                || (grid.template_columns.empty() && has_multiple_grid_columns(value));
            decision.classification = "partially-supported";
            decision.semantic_slice =
                "component grids with fixed, auto, fractional, and minmax track lengths";
        } else if (name == "grid-template-rows" && !is_inline(inline_grid)) {
            auto& grid = node.style.mutable_grid();
            grid.template_rows = parse_simple_grid_tracks(value);
            grid.fractional_rows = std::any_of(
                grid.template_rows.begin(),
                grid.template_rows.end(),
                [](const auto& track) { return track.fraction > 0; });
            decision.classification = "partially-supported";
            decision.semantic_slice = "fractional component rows and explicit fixed track lengths";
        } else if (name == "grid-auto-columns" && !is_inline(inline_grid)) {
            auto& grid = node.style.mutable_grid();
            grid.auto_columns = parse_simple_grid_tracks(value);
            decision.classification = "partially-supported";
            decision.semantic_slice =
                "fixed, auto, fractional, and minmax implicit column track lengths";
        } else if (name == "grid-auto-flow" && !is_inline(inline_grid)) {
            auto& grid = node.style.mutable_grid();
            grid.auto_flow_column = value == "column" || value == "column dense";
            decision.classification = "partially-supported";
            decision.semantic_slice = "row and column auto-flow without dense backfill";
        } else if (!is_inline(inline_grid)
            && apply_grid_placement_declaration(node.style, name, value)) {
            decision.classification = "supported";
            decision.semantic_slice =
                "grid-area, grid-row, grid-column, and placement longhand CSSOM values";
        } else if (name == "position" && !is_inline(inline_position)) {
            const auto explicit_document_element = node.tag == "html"
                && node.parent == &document.body();
            node.style.position = value == "inherit" && node.parent != nullptr
                    && !explicit_document_element
                ? node.parent->style.position
                : value == "absolute" ? position_mode::absolute
                : value == "fixed" ? position_mode::fixed
                : value == "relative" ? position_mode::relative
                : position_mode::normal;
        } else if (name == "float" && !is_inline(inline_float)) {
            node.style.floating = value == "left" ? float_mode::left
                : value == "right" ? float_mode::right
                : float_mode::none;
            decision.classification = "partially-supported";
            decision.semantic_slice =
                "left/right block floats sharing a bounded formatting-context line";
        } else if (name == "z-index" && !is_inline(inline_z_index)) {
            if (value == "inherit") {
                const auto explicit_document_element = node.tag == "html"
                    && node.parent == &document.body();
                if (!explicit_document_element && node.parent != nullptr) {
                    node.style.z_index = node.parent->style.z_index;
                    node.style.z_index_auto = node.parent->style.z_index_auto;
                } else {
                    node.style.z_index = 0;
                    node.style.z_index_auto = true;
                }
            } else {
                node.style.z_index_auto = value == "auto" || value == "initial"
                    || value == "unset";
                node.style.z_index = node.style.z_index_auto
                    ? 0 : std::atoi(value.c_str());
            }
        } else if (name == "flex-direction" && !is_inline(inline_flex_direction)) {
            node.style.direction = value == "row" || value == "row-reverse"
                ? flex_direction::row : flex_direction::column;
            node.style.flex_reverse = value == "row-reverse" || value == "column-reverse";
        } else if (name == "flex-flow"
            && (!is_inline(inline_flex_direction) || !is_inline(inline_flex_wrap))) {
            const auto flow = parse_flex_flow(value);
            if (!is_inline(inline_flex_direction)) {
                node.style.direction = flow.direction;
                node.style.flex_reverse = flow.reverse;
            }
            if (!is_inline(inline_flex_wrap)) node.style.flex_wrap = flow.wrap;
        } else if (name == "flex-wrap" && !is_inline(inline_flex_wrap)) {
            node.style.flex_wrap = value == "wrap" || value == "wrap-reverse";
        } else if (name == "align-items" && !is_inline(inline_align_items)) {
            node.style.align_items = value == "center" ? align_mode::center
                : value == "flex-start" || value == "start" ? align_mode::start
                : value == "flex-end" || value == "end" ? align_mode::end
                : value == "baseline" || value == "first baseline" ? align_mode::baseline
                : align_mode::stretch;
        } else if (name == "vertical-align" && node.tag == "tr" && value == "middle"
            && !is_inline(inline_align_items)) {
            // A flex-backed row uses cross-axis alignment to preserve the table-row
            // meaning of vertical-align: middle for its cells.
            node.style.mutable_textual().vertical_align = value;
            node.style.align_items = align_mode::center;
        } else if (name == "align-self" && !is_inline(inline_align_self)) {
            node.style.align_self_specified = value != "auto";
            node.style.align_self = value == "center" ? align_mode::center
                : value == "flex-start" || value == "start" ? align_mode::start
                : value == "flex-end" || value == "end" ? align_mode::end
                : value == "baseline" || value == "first baseline" ? align_mode::baseline
                : align_mode::stretch;
        } else if (name == "justify-content" && !is_inline(inline_justify_content)) {
            node.style.justify_content = value == "center" ? justify_mode::center
                : value == "flex-end" || value == "end" ? justify_mode::end
                : value == "space-between" ? justify_mode::space_between
                : value == "space-around" ? justify_mode::space_around
                : value == "space-evenly" ? justify_mode::space_evenly
                : justify_mode::start;
        } else if (name == "flex-grow" && !is_inline(inline_flex_grow)) {
            node.style.flex_grow = std::strtof(value.c_str(), nullptr);
        } else if (name == "flex-shrink" && !is_inline(inline_flex_shrink)) {
            node.style.flex_shrink = std::max(0.0F, std::strtof(value.c_str(), nullptr));
        } else if (name == "flex-basis" && !is_inline(inline_flex_basis)) {
            node.style.flex_basis = native_document::parse_length(value);
        } else if (name == "flex"
            && (!is_inline(inline_flex_grow) || !is_inline(inline_flex_shrink)
                || !is_inline(inline_flex_basis))) {
            if (value == "none") {
                if (!is_inline(inline_flex_grow)) node.style.flex_grow = 0;
                if (!is_inline(inline_flex_shrink)) node.style.flex_shrink = 0;
                if (!is_inline(inline_flex_basis)) node.style.flex_basis = {};
            } else {
                std::istringstream stream(value);
                std::string grow_value;
                std::string shrink_value;
                std::string basis_value;
                stream >> grow_value >> shrink_value >> basis_value;
                if (!is_inline(inline_flex_grow) && !grow_value.empty()) {
                    node.style.flex_grow = grow_value == "auto" ? 1.0F
                        : std::max(0.0F, std::strtof(grow_value.c_str(), nullptr));
                }
                if (!is_inline(inline_flex_shrink)) {
                    node.style.flex_shrink = shrink_value.empty()
                        ? 1.0F : std::max(0.0F, std::strtof(shrink_value.c_str(), nullptr));
                }
                if (!is_inline(inline_flex_basis) && !basis_value.empty()) {
                    node.style.flex_basis = native_document::parse_length(basis_value);
                }
            }
        } else if (name == "box-sizing" && !is_inline(inline_box_sizing)) {
            node.style.border_box = value == "border-box";
        } else if (name == "box-shadow" && !is_inline(inline_box_shadow)) {
            auto complete = true;
            if (!apply_box_shadow_value(node.style, value, complete)) {
                decision.classification = "unsupported";
                decision.semantic_slice = "outer shadows with two to four pixel lengths";
            } else if (!complete) {
                decision.classification = "partially-supported";
                decision.semantic_slice = "first outer shadow with pixel lengths";
            }
        } else if (name == "background-image") {
            auto& background = node.style.mutable_background_image();
            background.image_value = value;
            background.image_markup.clear();
            background.image_view_box.clear();
            if (value == "none") return;
            const auto url = first_css_url(value);
            if (!url.has_value()) {
                decision.classification = "unsupported";
                decision.semantic_slice = "single URL-backed SVG layer";
                return;
            }
            std::string markup;
            std::string resolved_url;
            const auto& base = current_base_address();
            if (!load_text_resource(
                    *url,
                    base,
                    WEBSCENE_RESOURCE_IMAGE,
                    markup,
                    resolved_url)) {
                decision.classification = "unsupported";
                decision.semantic_slice = "URL-backed SVG resource load failed";
                return;
            }
            const auto view_box = svg_view_box(markup);
            if (view_box.empty()) {
                decision.classification = "unsupported";
                decision.semantic_slice =
                    "SVG images with an explicit viewBox or numeric width and height";
                return;
            }
            background.image_value = "url(\"" + resolved_url + "\")";
            background.image_markup = std::move(markup);
            background.image_view_box = view_box;
            decision.classification = "partially-supported";
            decision.semantic_slice =
                "first URL-backed SVG layer with explicit viewBox or numeric width and height";
        } else if (name == "background-repeat") {
            node.style.mutable_background_image().repeat = value;
            if (value != "no-repeat") {
                decision.classification = "partially-supported";
                decision.semantic_slice = "no-repeat";
            }
        } else if (name == "background-position") {
            apply_background_position(node.style, value);
            decision.classification = "partially-supported";
            decision.semantic_slice = "two-value keywords, percentages, and pixel lengths";
        } else if (name == "background-size") {
            auto& background = node.style.mutable_background_image();
            std::istringstream stream(value);
            stream >> background.size_x >> background.size_y;
            if (background.size_x.empty()) background.size_x = "auto";
            if (background.size_y.empty()) {
                background.size_y = background.size_x == "cover"
                    || background.size_x == "contain"
                    ? background.size_x : "auto";
            }
            decision.classification = "partially-supported";
            decision.semantic_slice = "cover, contain, auto, percentages, and pixel lengths";
        } else if ((name == "background" || name == "background-color")
            && !is_inline(inline_background)) {
            node.style.background_current_color =
                lower_html_name(value) == "currentcolor";
            const auto parsed = parse_declared_color(value);
            if (parsed != 0U || value == "transparent"
                || lower_html_name(value) == "currentcolor") node.style.background_rgba = parsed;
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
            if (parsed != 0U || value == "transparent") node.style.foreground_rgba = parsed;
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
        node.style.scrollbar_hidden = false;
        node.style.scrollbar_visibility_important = false;
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
        if ((node.style.inline_property_mask & inline_margin) == 0U) {
            node.style.margin_left = {};
            node.style.margin_top = {};
            node.style.margin_right = {};
            node.style.margin_bottom = {};
            node.style.margin_left_auto = false;
            node.style.margin_top_auto = false;
            node.style.margin_right_auto = false;
            node.style.margin_bottom_auto = false;
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
        }
        if ((node.style.inline_property_mask & inline_transform_origin) == 0U) {
            node.style.transform_origin_x = {50, length_unit::percent};
            node.style.transform_origin_y = {50, length_unit::percent};
            node.style.transform_origin_specified = false;
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
            node.style.box_shadow_offset_x = 0;
            node.style.box_shadow_offset_y = 0;
            node.style.box_shadow_blur_radius = 0;
            node.style.box_shadow_spread_radius = 0;
            node.style.box_shadow_rgba = 0;
            node.style.box_shadow_present = false;
        }
        if ((node.style.inline_property_mask & inline_background) == 0U) {
            node.style.background_rgba = 0;
            node.style.background_current_color = false;
        }
        node.style.clear_background_image();
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
        node.style.clear_custom_properties();
        for (const auto& [name, value] : node.authored_style().declarations) {
            if (!name.starts_with("--")) continue;
            auto& custom = node.style.mutable_custom_properties();
            custom.values[name] = value;
            if (node.authored_style().important_declarations.contains(name)) {
                custom.important.insert(name);
            }
        }
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
        const auto append_candidates = [&](const auto& index, const std::string& key) {
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
                append_candidates(
                    css_rules_by_class,
                    std::string(classes.substr(start, class_cursor - start)));
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
        // Replay only variable-dependent inline declarations here. Temporarily
        // removing their inline guard lets the declaration update its own
        // computed value while the important-origin mask still prevents a
        // normal inline declaration from overriding an author !important rule.
        for (const auto& [name, value] : node.authored_style().declarations) {
            if (name.starts_with("--") || value.find("var(") == std::string::npos) continue;
            const auto property_mask = inline_style_mask(name);
            const auto retained_inline_mask = node.style.inline_property_mask;
            node.style.inline_property_mask &= ~property_mask;
            apply_css_declaration(
                node,
                {name, value,
                    node.authored_style().important_declarations.contains(name)});
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
        recompute_inline_font_relative_metrics(node);
        // Pseudo-elements inherit the originating element's completed
        // computed style. Apply them only after its custom and ordinary
        // declarations so `::before { background:var(--active-bg) }` resolves
        // the state variable authored on the active element.
        for (const auto& [pseudo_kind, rule] : matched_pseudo_rules) {
            if (pseudo_kind == 3) {
                for (const auto& declaration : rule->declarations()) {
                    apply_scrollbar_css_declaration(node.style, declaration);
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
        recompute_inline_font_relative_metrics(node);
        for (const auto& [pseudo_kind, rule] : matched_pseudo_rules) {
            if (pseudo_kind == 3) {
                for (const auto& declaration : rule->declarations()) {
                    apply_scrollbar_css_declaration(node.style, declaration);
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
                    node.authored_style().important_declarations.contains(name)});
            node.style.inline_property_mask = retained_inline_mask;
        }
        document.mark_dirty();
        return true;
    }

    void apply_css_rules_subtree(dom_node& node)
    {
        apply_css_rules(node);
        for (auto* child : node.children) apply_css_rules_subtree(*child);
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
        if (!is_connected(node)) return;
        retain_connected_subtree_wrappers(node);
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
    }

    void schedule_style_recascade(
        dom_node& root,
        bool subtree,
        std::string_view reason)
    {
        if (!is_connected(root)) return;
        retain_connected_subtree_wrappers(root);
        for (auto& pending : pending_style_recascades) {
            if (pending.root != &root) continue;
            pending.subtree = pending.subtree || subtree;
            if (pending.reason.empty() && !reason.empty()) pending.reason = reason;
            return;
        }
        if (std::any_of(
                pending_style_recascades.begin(),
                pending_style_recascades.end(),
                [this, &root](const pending_style_recascade& pending) {
                    return pending.subtree
                        && pending.root != nullptr
                        && subtree_contains(*pending.root, &root);
                })) {
            return;
        }
        if (subtree) {
            std::erase_if(
                pending_style_recascades,
                [this, &root](const pending_style_recascade& pending) {
                    return pending.root != nullptr
                        && subtree_contains(root, pending.root);
                });
        }
        pending_style_recascades.push_back(
            pending_style_recascade{&root, subtree, std::string(reason)});
    }

    void flush_pending_style_recascades()
    {
        if (pending_style_recascades.empty()) return;
        auto pending = std::move(pending_style_recascades);
        pending_style_recascades.clear();
        for (const auto& recascade : pending) {
            if (recascade.root == nullptr || !is_connected(*recascade.root)) continue;
            if (recascade.subtree) {
                recascade_connected_subtree_now(*recascade.root, recascade.reason);
            } else {
                apply_css_rules(*recascade.root);
            }
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
        apply_css_rules(node);
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
        css_variables.clear();
        important_css_variables.clear();
        for (size_t index = 0; index < css_rules.size(); ++index) {
            index_css_rule(index);
            const auto& rule = css_rules[index];
            if (!rule.media_matches || rule.shadow_scope_root_id != 0U
                || (rule.selector() != ":root" && rule.selector() != "html")) continue;
            for (const auto& declaration : rule.declarations()) {
                if (!declaration.name.starts_with("--")) continue;
                if (!declaration.important
                    && important_css_variables.contains(declaration.name)) continue;
                css_variables[declaration.name] = declaration.value;
                if (declaration.important) important_css_variables.insert(declaration.name);
                else important_css_variables.erase(declaration.name);
            }
        }
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
