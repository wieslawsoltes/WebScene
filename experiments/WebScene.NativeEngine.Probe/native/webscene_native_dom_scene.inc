void native_document::build_scene(
    std::vector<webscene_scene_command>& commands,
    std::vector<webscene_scene_string>& strings,
    std::vector<char>& string_bytes) const
{
    commands.clear();
    commands.reserve(nodes_.size());
    strings.clear();
    string_bytes.clear();
    append_scene(*body_, commands, strings, string_bytes, false, true);
    std::vector<dom_node*> fixed;
    for (auto* child : composed_children(*body_)) {
        collect_fixed_positioned_nodes(*this, *child, fixed);
    }
    std::stable_sort(
        fixed.begin(),
        fixed.end(),
        [](const auto* left, const auto* right) {
            return left->paint_z_index < right->paint_z_index;
        });
    for (const auto* fixed_node : fixed) {
        if (!display_tree_allows_render(*this, *fixed_node)) continue;
        auto inherited_visibility_hidden = false;
        std::vector<const dom_node*> ancestors;
        for (auto* ancestor = composed_parent(*fixed_node);
             ancestor != nullptr;
             ancestor = composed_parent(*ancestor)) {
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
    std::vector<webscene_canvas_layout>& layouts) const
{
    layouts.clear();
    for (const auto& node : nodes_) {
        if (node->tag != "canvas"
            || !is_connected(*node)
            || !node->visible
            || !display_tree_allows_render(*this, *node)) {
            continue;
        }
        const auto width = node->attributes.find("width");
        const auto height = node->attributes.find("height");
        layouts.push_back(webscene_canvas_layout{
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
    std::vector<webscene_canvas_layer>& layers,
    std::vector<webscene_canvas_command>& canvas_commands,
    std::vector<webscene_scene_string>& strings,
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
            || !display_tree_allows_render(*this, *node)
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
            strings.push_back(webscene_scene_string{
                byte_offset,
                static_cast<uint32_t>(value.size())});
        }

        layers.push_back(webscene_canvas_layer{
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
    std::vector<webscene_scene_command>& commands,
    std::vector<webscene_scene_string>& strings,
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
    if (!node.visible
        || node.style.display == display_mode::none
        || opacity <= 0.001F) {
        return;
    }
    const auto has_opacity_group = opacity < 0.999F;
    if (has_opacity_group) {
        commands.push_back(webscene_scene_command{
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
        commands.push_back(webscene_scene_command{
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
        commands.push_back(webscene_scene_command{
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
        for (auto* current = &node; current != nullptr; current = composed_parent(*current)) {
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
        float top_left_y;
        float top_right_y;
        float bottom_right_y;
        float bottom_left_y;

        bool any() const noexcept
        {
            return top_left > 0 || top_right > 0 || bottom_right > 0 || bottom_left > 0
                || top_left_y > 0 || top_right_y > 0
                || bottom_right_y > 0 || bottom_left_y > 0;
        }

        bool elliptical() const noexcept
        {
            return std::abs(top_left - top_left_y) > 0.01F
                || std::abs(top_right - top_right_y) > 0.01F
                || std::abs(bottom_right - bottom_right_y) > 0.01F
                || std::abs(bottom_left - bottom_left_y) > 0.01F;
        }
    };
    const auto resolve_radii = [&](css_length top_left, css_length top_right,
                                   css_length bottom_right, css_length bottom_left,
                                   css_length top_left_y, css_length top_right_y,
                                   css_length bottom_right_y, css_length bottom_left_y,
                                   float width, float height) {
        resolved_radii result{
            std::max(0.0F, resolve_length(node, top_left, width, 0)),
            std::max(0.0F, resolve_length(node, top_right, width, 0)),
            std::max(0.0F, resolve_length(node, bottom_right, width, 0)),
            std::max(0.0F, resolve_length(node, bottom_left, width, 0)),
            std::max(0.0F, resolve_length(node, top_left_y, height, 0)),
            std::max(0.0F, resolve_length(node, top_right_y, height, 0)),
            std::max(0.0F, resolve_length(node, bottom_right_y, height, 0)),
            std::max(0.0F, resolve_length(node, bottom_left_y, height, 0))};
        auto scale = 1.0F;
        const auto constrain = [&](float available, float sum) {
            if (sum > available && sum > 0) scale = std::min(scale, available / sum);
        };
        constrain(width, result.top_left + result.top_right);
        constrain(width, result.bottom_left + result.bottom_right);
        constrain(height, result.top_left_y + result.bottom_left_y);
        constrain(height, result.top_right_y + result.bottom_right_y);
        if (scale < 1.0F) {
            result.top_left *= scale;
            result.top_right *= scale;
            result.bottom_right *= scale;
            result.bottom_left *= scale;
            result.top_left_y *= scale;
            result.top_right_y *= scale;
            result.bottom_right_y *= scale;
            result.bottom_left_y *= scale;
        }
        return result;
    };
    const auto append_ellipse_metadata = [&](const resolved_radii& radii,
                                             float x, float y,
                                             float width, float height) {
        if (!radii.elliptical()) return;
        // Kind 32 is an ABI-preserving companion for the immediately following
        // rounded command. Geometry is duplicated for damage tracking; the four
        // existing radius fields carry vertical radii. Older presenters ignore
        // this command and retain their established circular fallback.
        commands.push_back(webscene_scene_command{
            32U,
            0U,
            x,
            y,
            width,
            height,
            0U,
            node.id,
            radii.top_left_y,
            radii.top_right_y,
            radii.bottom_right_y,
            radii.bottom_left_y,
            0});
    };
    const auto pack_round_rect = [](float packed_radius, float stroke_width) {
        const auto radius_bits = static_cast<uint32_t>(std::clamp(
            std::lround(packed_radius * 100.0F), 0L, 65535L));
        const auto width_bits = static_cast<uint32_t>(std::clamp(
            std::lround(stroke_width * 100.0F), 0L, 65535L));
        return (radius_bits << 16U) | width_bits;
    };
    const auto append_pseudo = [&](const node_style::pseudo_element& pseudo) {
        if (!paint_self || !pseudo.generated || pseudo.display_none || pseudo.visibility_hidden) return;
        const auto after_retained_canvas = &pseudo == &node.style.after_pseudo()
            && node.contains_retained_canvas
            && !(pseudo.z_index < 0 && pseudo.position != position_mode::normal);
        const auto pseudo_paints_in_foreground = paint_box_in_foreground
            || after_retained_canvas
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
                pseudo.elliptical_border_radius
                    ? pseudo.border_top_left_radius_y : pseudo.border_top_left_radius,
                pseudo.elliptical_border_radius
                    ? pseudo.border_top_right_radius_y : pseudo.border_top_right_radius,
                pseudo.elliptical_border_radius
                    ? pseudo.border_bottom_right_radius_y : pseudo.border_bottom_right_radius,
                pseudo.elliptical_border_radius
                    ? pseudo.border_bottom_left_radius_y : pseudo.border_bottom_left_radius,
                width,
                height);
            append_ellipse_metadata(radii, x, y, width, height);
            commands.push_back(webscene_scene_command{
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
        if (width > 0 && height > 0) {
            const auto top = std::clamp(
                resolve_length(node, pseudo.border_top_width, height, 0),
                0.0F,
                height);
            const auto right = std::clamp(
                resolve_length(node, pseudo.border_right_width, width, 0),
                0.0F,
                width);
            const auto bottom = std::clamp(
                resolve_length(node, pseudo.border_bottom_width, height, 0),
                0.0F,
                height);
            const auto left = std::clamp(
                resolve_length(node, pseudo.border_left_width, width, 0),
                0.0F,
                width);
            const auto uniform_width = top > 0
                && std::abs(top - right) < 0.01F
                && std::abs(top - bottom) < 0.01F
                && std::abs(top - left) < 0.01F;
            const auto uniform_color = pseudo.border_top_rgba == pseudo.border_right_rgba
                && pseudo.border_top_rgba == pseudo.border_bottom_rgba
                && pseudo.border_top_rgba == pseudo.border_left_rgba;
            const auto border_radii = resolve_radii(
                pseudo.border_top_left_radius,
                pseudo.border_top_right_radius,
                pseudo.border_bottom_right_radius,
                pseudo.border_bottom_left_radius,
                pseudo.elliptical_border_radius
                    ? pseudo.border_top_left_radius_y : pseudo.border_top_left_radius,
                pseudo.elliptical_border_radius
                    ? pseudo.border_top_right_radius_y : pseudo.border_top_right_radius,
                pseudo.elliptical_border_radius
                    ? pseudo.border_bottom_right_radius_y : pseudo.border_bottom_right_radius,
                pseudo.elliptical_border_radius
                    ? pseudo.border_bottom_left_radius_y : pseudo.border_bottom_left_radius,
                width,
                height);
            if (border_radii.any() && uniform_width && uniform_color
                && (pseudo.border_top_rgba & 0xFFU) != 0U) {
                const auto inset = top * 0.5F;
                const auto stroke_x = x + inset;
                const auto stroke_y = y + inset;
                const auto stroke_width = std::max(0.0F, width - top);
                const auto stroke_height = std::max(0.0F, height - top);
                const auto stroke_radii = resolved_radii{
                    std::max(0.0F, border_radii.top_left - inset),
                    std::max(0.0F, border_radii.top_right - inset),
                    std::max(0.0F, border_radii.bottom_right - inset),
                    std::max(0.0F, border_radii.bottom_left - inset),
                    std::max(0.0F, border_radii.top_left_y - inset),
                    std::max(0.0F, border_radii.top_right_y - inset),
                    std::max(0.0F, border_radii.bottom_right_y - inset),
                    std::max(0.0F, border_radii.bottom_left_y - inset)};
                append_ellipse_metadata(
                    stroke_radii,
                    stroke_x,
                    stroke_y,
                    stroke_width,
                    stroke_height);
                commands.push_back(webscene_scene_command{
                    pseudo_paints_in_foreground ? 11U : 8U,
                    pack_round_rect(0, top),
                    stroke_x,
                    stroke_y,
                    stroke_width,
                    stroke_height,
                    with_pseudo_opacity(pseudo.border_top_rgba),
                    node.id,
                    stroke_radii.top_left,
                    stroke_radii.top_right,
                    stroke_radii.bottom_right,
                    stroke_radii.bottom_left,
                    top});
            } else {
                const auto append_pseudo_border_rect = [&] (
                    float rect_x,
                    float rect_y,
                    float rect_width,
                    float rect_height,
                    uint32_t rgba) {
                    if ((rgba & 0xFFU) == 0U || rect_width <= 0 || rect_height <= 0) return;
                    commands.push_back(webscene_scene_command{
                        pseudo_paints_in_foreground ? 9U : 1U,
                        0U,
                        rect_x,
                        rect_y,
                        rect_width,
                        rect_height,
                        with_pseudo_opacity(rgba),
                        node.id});
                };
                append_pseudo_border_rect(x, y, width, top, pseudo.border_top_rgba);
                append_pseudo_border_rect(
                    x + width - right, y, right, height, pseudo.border_right_rgba);
                append_pseudo_border_rect(
                    x, y + height - bottom, width, bottom, pseudo.border_bottom_rgba);
                append_pseudo_border_rect(x, y, left, height, pseudo.border_left_rgba);
            }
        }
        const auto outline = std::max(
            0.0F,
            resolve_length(node, pseudo.outline_width, width, 0));
        if ((pseudo.outline_rgba & 0xFFU) != 0U
            && outline > 0 && width > 0 && height > 0) {
            const auto radii = resolve_radii(
                pseudo.border_top_left_radius,
                pseudo.border_top_right_radius,
                pseudo.border_bottom_right_radius,
                pseudo.border_bottom_left_radius,
                pseudo.elliptical_border_radius
                    ? pseudo.border_top_left_radius_y : pseudo.border_top_left_radius,
                pseudo.elliptical_border_radius
                    ? pseudo.border_top_right_radius_y : pseudo.border_top_right_radius,
                pseudo.elliptical_border_radius
                    ? pseudo.border_bottom_right_radius_y : pseudo.border_bottom_right_radius,
                pseudo.elliptical_border_radius
                    ? pseudo.border_bottom_left_radius_y : pseudo.border_bottom_left_radius,
                width,
                height);
            if (radii.any()) {
                const auto half_outline = outline * 0.5F;
                const auto outline_x = x - half_outline;
                const auto outline_y = y - half_outline;
                const auto outline_width = width + outline;
                const auto outline_height = height + outline;
                const auto outline_radii = resolved_radii{
                    radii.top_left + half_outline,
                    radii.top_right + half_outline,
                    radii.bottom_right + half_outline,
                    radii.bottom_left + half_outline,
                    radii.top_left_y + half_outline,
                    radii.top_right_y + half_outline,
                    radii.bottom_right_y + half_outline,
                    radii.bottom_left_y + half_outline};
                append_ellipse_metadata(
                    outline_radii,
                    outline_x,
                    outline_y,
                    outline_width,
                    outline_height);
                commands.push_back(webscene_scene_command{
                    pseudo_paints_in_foreground ? 11U : 8U,
                    pack_round_rect(0, outline),
                    outline_x,
                    outline_y,
                    outline_width,
                    outline_height,
                    with_pseudo_opacity(pseudo.outline_rgba),
                    node.id,
                    outline_radii.top_left,
                    outline_radii.top_right,
                    outline_radii.bottom_right,
                    outline_radii.bottom_left,
                    outline});
            } else {
                const auto append_outline_rect = [&](float rect_x, float rect_y,
                                                     float rect_width, float rect_height) {
                    if (rect_width <= 0 || rect_height <= 0) return;
                    commands.push_back(webscene_scene_command{
                        pseudo_paints_in_foreground ? 9U : 1U,
                        0U,
                        rect_x,
                        rect_y,
                        rect_width,
                        rect_height,
                        with_pseudo_opacity(pseudo.outline_rgba),
                        node.id});
                };
                append_outline_rect(x - outline, y - outline, width + outline * 2.0F, outline);
                append_outline_rect(x + width, y, outline, height);
                append_outline_rect(x - outline, y + height, width + outline * 2.0F, outline);
                append_outline_rect(x - outline, y, outline, height);
            }
        }
        if (has_visible_text(pseudo.content)) {
            std::ostringstream resource;
            resource << font_size << '\t' << line_height << '\t'
                << resolved_font_weight(node) << '\t' << resolved_text_align(node) << '\t'
                << resolved_font_family(node) << '\t'
                << resolved_font_smoothing(node) << '\t' << pseudo.content;
            commands.push_back(webscene_scene_command{
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
        node.style.border_top_left_radius_y(),
        node.style.border_top_right_radius_y(),
        node.style.border_bottom_right_radius_y(),
        node.style.border_bottom_left_radius_y(),
        node.layout.width,
        node.layout.height);
    if (paint_self
        && node.style.box_shadow_present
        && node.layout.width > 0
        && node.layout.height > 0) {
        const auto spread = node.style.box_shadow_spread_radius;
        const auto shadow_width = node.layout.width + spread * 2.0F;
        const auto shadow_height = node.layout.height + spread * 2.0F;
        if (shadow_width > 0 && shadow_height > 0) {
            const auto shadow_radii = resolved_radii{
                std::max(0.0F, radii.top_left + spread),
                std::max(0.0F, radii.top_right + spread),
                std::max(0.0F, radii.bottom_right + spread),
                std::max(0.0F, radii.bottom_left + spread),
                std::max(0.0F, radii.top_left_y + spread),
                std::max(0.0F, radii.top_right_y + spread),
                std::max(0.0F, radii.bottom_right_y + spread),
                std::max(0.0F, radii.bottom_left_y + spread)};
            const auto shadow_x = node.layout.x + node.style.box_shadow_offset_x - spread;
            const auto shadow_y = node.layout.y + node.style.box_shadow_offset_y - spread;
            append_ellipse_metadata(
                shadow_radii, shadow_x, shadow_y, shadow_width, shadow_height);
            commands.push_back(webscene_scene_command{
                paint_box_in_foreground ? 18U : 17U,
                0U,
                shadow_x,
                shadow_y,
                shadow_width,
                shadow_height,
                with_opacity(node.style.box_shadow_rgba),
                node.id,
                shadow_radii.top_left,
                shadow_radii.top_right,
                shadow_radii.bottom_right,
                shadow_radii.bottom_left,
                node.style.box_shadow_blur_radius});
        }
    }
    const auto background_rgba = node.style.background_current_color
        ? resolved_foreground(node)
        : node.style.background_rgba;
    if (paint_self
        && (background_rgba & 0xFFU) != 0U
        && node.layout.width > 0
        && node.layout.height > 0) {
        append_ellipse_metadata(
            radii,
            node.layout.x,
            node.layout.y,
            node.layout.width,
            node.layout.height);
        commands.push_back(webscene_scene_command{
            radii.any()
                ? paint_box_in_foreground ? 10U : 7U
                : paint_box_in_foreground ? 9U : 1U,
            node.style.clip ? 1U : 0U,
            node.layout.x,
            node.layout.y,
            node.layout.width,
            node.layout.height,
            with_opacity(background_rgba),
            node.id,
            radii.top_left,
            radii.top_right,
            radii.bottom_right,
            radii.bottom_left,
            0});
    }
    const auto& background_image = node.style.background_image();
    auto background_image_value_lower = background_image.image_value;
    std::transform(
        background_image_value_lower.begin(),
        background_image_value_lower.end(),
        background_image_value_lower.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    if (paint_self
        && background_image_value_lower.starts_with("linear-gradient(")
        && node.layout.width > 0
        && node.layout.height > 0) {
        append_ellipse_metadata(
            radii,
            node.layout.x,
            node.layout.y,
            node.layout.width,
            node.layout.height);
        commands.push_back(webscene_scene_command{
            paint_box_in_foreground ? 22U : 21U,
            append_scene_string(background_image.image_value, strings, string_bytes),
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
            commands.push_back(webscene_scene_command{
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
    const auto& replaced_image = node.replaced_image();
    if (paint_self
        && node.tag == "img"
        && !replaced_image.markup.empty()
        && !replaced_image.view_box.empty()
        && node.layout.width > 0
        && node.layout.height > 0) {
        const auto resource_index = append_scene_string(
            replaced_image.view_box + '\t' + replaced_image.markup,
            strings,
            string_bytes);
        commands.push_back(webscene_scene_command{
            6U,
            resource_index,
            node.layout.x,
            node.layout.y,
            node.layout.width,
            node.layout.height,
            0U,
            node.id});
    }
    const auto append_border_rect = [&](float x, float y, float width, float height, uint32_t rgba) {
        if ((rgba & 0xFFU) == 0U || width <= 0 || height <= 0) return;
        commands.push_back(webscene_scene_command{
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
                const auto border_x = node.layout.x + left_inset;
                const auto border_y = node.layout.y + top_inset;
                const auto border_width = std::max(
                    0.0F, node.layout.width - left_inset - right_inset);
                const auto border_height = std::max(
                    0.0F, node.layout.height - top_inset - bottom_inset);
                const auto border_radii = resolved_radii{
                    std::max(0.0F, radii.top_left - left_inset),
                    std::max(0.0F, radii.top_right - right_inset),
                    std::max(0.0F, radii.bottom_right - right_inset),
                    std::max(0.0F, radii.bottom_left - left_inset),
                    std::max(0.0F, radii.top_left_y - top_inset),
                    std::max(0.0F, radii.top_right_y - top_inset),
                    std::max(0.0F, radii.bottom_right_y - bottom_inset),
                    std::max(0.0F, radii.bottom_left_y - bottom_inset)};
                append_ellipse_metadata(
                    border_radii, border_x, border_y, border_width, border_height);
                commands.push_back(webscene_scene_command{
                    paint_box_in_foreground ? 11U : 8U,
                    pack_round_rect(0, rounded_stroke_width)
                        | (partition_colors ? border_color_partition_flag : 0U)
                        | (all_sides && uniform_width && uniform_color ? 0U : side_flags),
                    border_x,
                    border_y,
                    border_width,
                    border_height,
                    with_opacity(color),
                    node.id,
                    border_radii.top_left,
                    border_radii.top_right,
                    border_radii.bottom_right,
                    border_radii.bottom_left,
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
        if (outline > 0 && radii.any()) {
            const auto half_outline = outline * 0.5F;
            const auto outline_x = node.layout.x - half_outline;
            const auto outline_y = node.layout.y - half_outline;
            const auto outline_width = node.layout.width + outline;
            const auto outline_height = node.layout.height + outline;
            const auto outline_radii = resolved_radii{
                radii.top_left + half_outline,
                radii.top_right + half_outline,
                radii.bottom_right + half_outline,
                radii.bottom_left + half_outline,
                radii.top_left_y + half_outline,
                radii.top_right_y + half_outline,
                radii.bottom_right_y + half_outline,
                radii.bottom_left_y + half_outline};
            append_ellipse_metadata(
                outline_radii,
                outline_x,
                outline_y,
                outline_width,
                outline_height);
            commands.push_back(webscene_scene_command{
                paint_box_in_foreground ? 11U : 8U,
                pack_round_rect(0, outline),
                outline_x,
                outline_y,
                outline_width,
                outline_height,
                with_opacity(node.style.outline_rgba),
                node.id,
                outline_radii.top_left,
                outline_radii.top_right,
                outline_radii.bottom_right,
                outline_radii.bottom_left,
                outline});
        } else {
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
    }
    const auto clip_contents = node.style.clip
        && node.layout.width > 0
        && node.layout.height > 0;
    if (clip_contents) {
        // Kinds 12/13 bracket the element's pseudo/content/descendant paint.
        // The box background and border intentionally remain outside its own
        // overflow clip, matching CSS background/border painting semantics.
        append_ellipse_metadata(
            radii,
            node.layout.x,
            node.layout.y,
            node.layout.width,
            node.layout.height);
        commands.push_back(webscene_scene_command{
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
        // Keep existing LI marker rasterization stable. A generic list-item's
        // square is geometry so its correctness does not depend on font glyphs.
        if (paint_self
            && node.tag != "li"
            && resolved_list_style(node, false) == "square") {
            const auto font_size = resolved_font_size(node);
            const auto line_height = resolved_line_height(node, font_size);
            const auto marker_size = std::max(3.0F, std::round(font_size * 0.3125F));
            commands.push_back(webscene_scene_command{
                9U,
                0U,
                node.list_marker_layout.x,
                node.list_marker_layout.y
                    + std::max(0.0F, (line_height - marker_size) * 0.5F),
                marker_size,
                marker_size,
                with_opacity(resolved_foreground(node)),
                node.id,
                0,
                0,
                0,
                0,
                0});
        } else {
            node_style::pseudo_element marker{};
            marker.generated = true;
            marker.content = list_marker_text(node);
            marker.layout = node.list_marker_layout;
            append_pseudo(marker);
        }
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
            commands.push_back(webscene_scene_command{
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
            commands.push_back(webscene_scene_command{
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

    if (paint_self && is_collapsed_select(node)) {
        const auto font_size = resolved_font_size(node);
        const auto line_height = resolved_line_height(node, font_size);
        const auto padding_left = resolve_length(
            node, node.style.padding_left, node.layout.width, 0);
        const auto padding_right = resolve_length(
            node, node.style.padding_right, node.layout.width, 0);
        const auto affordance_width = std::max(12.0F, font_size * 1.25F);
        const auto text_x = node.layout.x + padding_left;
        const auto text_y = node.layout.y
            + std::max(0.0F, (node.layout.height - line_height) * 0.5F);
        const auto text_width = std::max(
            0.0F,
            node.layout.width - padding_left - padding_right - affordance_width);
        if (const auto* selected = selected_option_for(node); selected != nullptr) {
            std::ostringstream resource;
            resource << font_size << '\t' << line_height << '\t'
                << resolved_font_weight(node) << '\t' << "start" << '\t'
                << resolved_font_family(node) << '\t'
                << resolved_font_smoothing(node) << '\t' << option_label(*selected);
            commands.push_back(webscene_scene_command{
                3U,
                append_scene_string(resource.str(), strings, string_bytes),
                text_x,
                text_y,
                text_width,
                line_height,
                with_opacity(resolved_foreground(node)),
                node.id});
        }
        const auto center_x = node.layout.x + node.layout.width
            - padding_right - affordance_width * 0.5F;
        const auto center_y = node.layout.y + node.layout.height * 0.5F;
        const auto arrow_color = with_opacity(resolved_foreground(node));
        commands.push_back(webscene_scene_command{
            2U, 100U,
            center_x - 3.5F, center_y - 1.5F,
            center_x, center_y + 2.0F,
            arrow_color, node.id});
        commands.push_back(webscene_scene_command{
            2U, 100U,
            center_x, center_y + 2.0F,
            center_x + 3.5F, center_y - 1.5F,
            arrow_color, node.id});
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
                << resolved_font_family(node) << '\t'
                << resolved_font_smoothing(node) << '\t' << node.form_control().value;
            commands.push_back(webscene_scene_command{
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
            commands.push_back(webscene_scene_command{
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
                    << resolved_font_smoothing(node) << '\t'
                    << resolved_text_transform(node, fragment.text);
                commands.push_back(webscene_scene_command{
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
        if (height <= 0 && node.tag == "#text" && composed_parent(node) != nullptr) {
            y = composed_parent(node)->layout.y;
            height = composed_parent(node)->layout.height;
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
                << resolved_font_smoothing(node) << '\t'
                << lines[line_index];
            commands.push_back(webscene_scene_command{
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
        if ((box.width <= 0 || box.height <= 0) && composed_parent(node) != nullptr) {
            box = composed_parent(node)->layout;
        }
        if (box.width > 0 && box.height > 0) {
            const auto view_box = node.attributes.contains("viewBox")
                ? node.attributes.at("viewBox")
                : std::string("0 0 ") + std::to_string(std::max(1.0F, box.width))
                    + " " + std::to_string(std::max(1.0F, box.height));
            const auto preserve_aspect_ratio = node.attributes.contains("preserveAspectRatio")
                ? node.attributes.at("preserveAspectRatio")
                : std::string("xMidYMid meet");
            std::string markup;
            markup.reserve(256U);
            serialize_svg_subtree(node, markup, true);
            const auto resource_index = append_scene_string(
                view_box + '\t' + preserve_aspect_ratio + '\t' + markup,
                strings,
                string_bytes);
            commands.push_back(webscene_scene_command{
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
                0});
        }
        if (clip_contents) {
            commands.push_back(webscene_scene_command{13U, 0U, 0, 0, 0, 0, 0U, node.id});
        }
        if (has_rotate_transform) {
            commands.push_back(webscene_scene_command{20U, 0U, 0, 0, 0, 0, 0U, node.id});
        }
        if (has_scale_transform) {
            commands.push_back(webscene_scene_command{16U, 0U, 0, 0, 0, 0, 0U, node.id});
        }
        if (has_opacity_group) {
            commands.push_back(webscene_scene_command{
                31U, 0U, node.layout.x, node.layout.y,
                node.layout.width, node.layout.height, 0U, node.id});
        }
        return;
    }

    if (paint_self && node.tag == "path" && node.attributes.contains("d")) {
        const dom_node* svg = composed_parent(node);
        while (svg != nullptr && svg->tag != "svg") svg = composed_parent(*svg);
        if (svg != nullptr) {
            auto box = svg->layout;
            if ((box.width <= 0 || box.height <= 0) && composed_parent(*svg) != nullptr) {
                box = composed_parent(*svg)->layout;
            }
            const auto view_box = svg->attributes.contains("viewBox")
                ? svg->attributes.at("viewBox")
                : std::string("0 0 ") + std::to_string(std::max(1.0F, box.width))
                    + " " + std::to_string(std::max(1.0F, box.height));
            const auto preserve_aspect_ratio = svg->attributes.contains("preserveAspectRatio")
                ? svg->attributes.at("preserveAspectRatio")
                : std::string("xMidYMid meet");
            const auto transform = node.attributes.contains("transform")
                ? node.attributes.at("transform") : std::string{};
            const auto stroke_width = node.attributes.contains("stroke-width")
                ? node.attributes.at("stroke-width") : std::string("1");
            std::string resource = view_box + '\t' + preserve_aspect_ratio + '\t'
                + stroke_width + '\t'
                + transform + '\t' + node.attributes.at("d");
            const auto resource_index = append_scene_string(resource, strings, string_bytes);
            const auto fill = node.attributes.contains("fill")
                ? node.attributes.at("fill")
                : svg->attributes.contains("fill") ? svg->attributes.at("fill") : "currentColor";
            if (fill != "none") {
                const auto color = fill == "currentColor"
                    ? resolved_foreground(node)
                    : parse_color(fill);
                commands.push_back(webscene_scene_command{
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
                    0});
            }
            const auto stroke = node.attributes.contains("stroke")
                ? node.attributes.at("stroke")
                : svg->attributes.contains("stroke") ? svg->attributes.at("stroke") : std::string{};
            if (!stroke.empty() && stroke != "none") {
                const auto color = stroke == "currentColor"
                    ? resolved_foreground(node)
                    : parse_color(stroke);
                commands.push_back(webscene_scene_command{
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
                    0});
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
    const auto& children = composed_children(node);
    paint_order.reserve(
        children.size()
        + (before_paints_after_content ? 1U : 0U)
        + (!after_paints_behind_content ? 1U : 0U));
    if (before_paints_after_content) {
        paint_order.push_back(local_paint_entry{
            nullptr,
            &node.style.before_pseudo(),
            node.style.before_pseudo().z_index,
            0U});
    }
    for (size_t index = 0; index < children.size(); ++index) {
        const auto* child = children[index];
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
            children.size() + 1U});
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
        commands.push_back(webscene_scene_command{13U, 0U, 0, 0, 0, 0, 0U, node.id});
    }
    // Scroll geometry is a DOM/CSS contract, while the presence of an actual
    // scrollbar is a host-renderer contract. Emit lightweight overlay rails
    // after the overflow clip so they remain visible above the clipped content.
    // They do not consume clientWidth/clientHeight, matching overlay scrollbars
    // on the certified macOS host.
    if (paint_self && !node.style.scrollbar_hidden
        && node.layout.width > 0 && node.layout.height > 0) {
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
            commands.push_back(webscene_scene_command{
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
        commands.push_back(webscene_scene_command{20U, 0U, 0, 0, 0, 0, 0U, node.id});
    }
    if (has_scale_transform) {
        commands.push_back(webscene_scene_command{16U, 0U, 0, 0, 0, 0, 0U, node.id});
    }
    if (has_opacity_group) {
        commands.push_back(webscene_scene_command{
            31U, 0U, node.layout.x, node.layout.y,
            node.layout.width, node.layout.height, 0U, node.id});
    }
}
