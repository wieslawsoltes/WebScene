void native_document::layout(float viewport_width, float viewport_height)
{
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    const auto intrinsic_cache_disabled =
        std::getenv("WEBSCENE_DISABLE_INTRINSIC_SIZE_CACHE") != nullptr;
#else
    constexpr auto intrinsic_cache_disabled = false;
#endif
    intrinsic_size_cache_generation_ = 0;
    if (!intrinsic_cache_disabled) {
        if (intrinsic_size_cache_ == nullptr) {
            intrinsic_size_cache_ = std::make_unique<std::unordered_map<
                intrinsic_size_key,
                intrinsic_size_cache_entry,
                intrinsic_size_key_hash>>();
        }
        // The key is bounded to two axes per stable node. This clear is only a
        // detached-node lifetime cap, not a viewport-dependent resize cycle.
        if (intrinsic_size_cache_->size() >= 16'384U) intrinsic_size_cache_->clear();
        if (intrinsic_size_cache_->empty()) {
            intrinsic_size_cache_->reserve(
                std::min<size_t>(nodes_.size() * 2U, 16'384U));
        }
        if (++intrinsic_size_cache_next_generation_ == 0) {
            intrinsic_size_cache_->clear();
            ++intrinsic_size_cache_next_generation_;
        }
        intrinsic_size_cache_generation_ = intrinsic_size_cache_next_generation_;
    }
    if (shadow_dom_storage_ != nullptr) {
        refresh_shadow_distributions();
        // Nodes omitted by slot distribution must not retain geometry from an
        // earlier projection. Clear only shadow-related logical subtrees; the
        // composed layout below immediately restores every projected node.
        const auto clear_projection_layout = [&](const auto& self, dom_node& node) -> void {
            node.visible = false;
            node.layout = {};
            node.text_layout_fragments.clear();
            node.scroll_content_width = 0.0F;
            node.scroll_content_height = 0.0F;
            node.scroll_viewport_width = 0.0F;
            node.scroll_viewport_height = 0.0F;
            if (node.style.before_pseudo().generated) {
                node.style.mutable_before_pseudo().layout = {};
            }
            if (node.style.after_pseudo().generated) {
                node.style.mutable_after_pseudo().layout = {};
            }
            node.list_marker_layout = {};
            if (const auto* state = shadow_dom(node);
                state != nullptr && state->root != nullptr) {
                self(self, *state->root);
            }
            for (auto* child : node.children) {
                if (child != nullptr) self(self, *child);
            }
        };
        for (auto& [_, state] : shadow_dom_storage_->node_data) {
            if (state.root == nullptr || state.root->parent == nullptr) continue;
            for (auto* child : state.root->parent->children) {
                if (child != nullptr) clear_projection_layout(clear_projection_layout, *child);
            }
            clear_projection_layout(clear_projection_layout, *state.root);
        }
    }
    viewport_width_ = std::max(1.0F, viewport_width);
    viewport_height_ = std::max(1.0F, viewport_height);
    body_->layout = {
        0,
        0,
        std::max(1.0F, viewport_width),
        std::max(1.0F, viewport_height)};
    body_->visible = true;
    layout_children(*body_);
    update_paint_z_index(*this, *body_);
    auto retained_canvas_seen = false;
    auto retained_canvases_remaining = count_retained_canvases(*this, *body_);
    update_retained_canvas_paint_phase(
        *this,
        *body_,
        retained_canvas_seen,
        retained_canvases_remaining);
    intrinsic_size_cache_generation_ = 0;
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
    if (is_collapsed_select(parent)) {
        const auto clear_option_layout = [&](const auto& self, dom_node& node) -> void {
            node.visible = false;
            node.layout = {};
            node.text_layout_fragments.clear();
            node.scroll_content_width = 0.0F;
            node.scroll_content_height = 0.0F;
            node.scroll_viewport_width = 0.0F;
            node.scroll_viewport_height = 0.0F;
            for (auto* child : node.children) {
                if (child != nullptr) self(self, *child);
            }
        };
        for (auto* child : parent.children) {
            if (child != nullptr) clear_option_layout(clear_option_layout, *child);
        }
        parent.scroll_content_width = parent.layout.width;
        parent.scroll_content_height = parent.layout.height;
        parent.scroll_viewport_width = parent.layout.width;
        parent.scroll_viewport_height = parent.layout.height;
        return;
    }
    if (composed_children(parent).empty() && !has_before && !has_after && !has_inside_marker) {
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
    // HTML LI marker geometry retains its established path. Generic authored
    // list-item boxes need an outside marker even though they have no LI tag.
    if (!marker_text.empty() && !has_inside_marker && parent.tag != "li") {
        auto marker = make_list_marker_layout_node(parent, marker_text);
        const auto marker_width = measure_text_width(marker_text, marker);
        const auto marker_font_size = resolved_font_size(marker);
        const auto marker_line_height = resolved_line_height(marker, marker_font_size);
        parent.list_marker_layout = {
            content.x - marker_width,
            content.y,
            marker_width,
            marker_line_height};
    }
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
        for (auto* child : composed_children(parent)) {
            child->visible = parent.visible && child->style.display != display_mode::none;
            // Collapsible indentation around flattened children generates no
            // box. Giving it the contents rectangle lets an invisible text node
            // mask the real descendant during hit testing.
            if (!child->visible || is_collapsible_whitespace_text(*child)) {
                child->visible = false;
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
        for (auto* child : composed_children(parent)) {
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
    if (is_table_container(parent.style.display)
        && !has_table_row_descendant(*this, parent)) {
        std::vector<float> explicit_columns;
        const auto collect_columns = [&](const auto& self, const dom_node& node) -> void {
            for (const auto* child : composed_children(node)) {
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
        for (auto* child : composed_children(parent)) {
            if (is_table_track(child->style.display)) {
                flush_row();
                continue;
            }
            if (is_table_row_group(child->style.display)
                || child->style.display == display_mode::table_caption) {
                flush_row();
                continue;
            }
            // Collapsible whitespace between table-internal boxes does not
            // generate an anonymous cell. Treating source indentation as a
            // column inflates shrink-to-fit tables and shifts every later cell.
            if (is_collapsible_whitespace_text(*child)) continue;
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
        for (auto* child : composed_children(parent)) {
            child->visible = parent.visible && child->style.display != display_mode::none;
            if (!child->visible || is_table_track(child->style.display)
                || is_collapsible_whitespace_text(*child)) {
                child->visible = false;
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

    if (is_table_container(parent.style.display)
        && has_table_row_descendant(*this, parent)) {
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
            for (auto* child : composed_children(node)) {
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
            for (auto* child : composed_children(*row.row)) {
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

        std::vector<float> authored_column_widths;
        const auto collect_column_widths = [&](const auto& self,
                                               const dom_node& node) -> void {
            for (const auto* child : composed_children(node)) {
                if (child->style.display == display_mode::table_column_group) {
                    self(self, *child);
                    continue;
                }
                if (child->style.display != display_mode::table_column) continue;
                const auto span_value = child->attributes.find("span");
                const auto span = span_value == child->attributes.end()
                    ? size_t{1}
                    : std::max<size_t>(
                        1,
                        static_cast<size_t>(parse_number(span_value->second, 1)));
                const auto width = is_specified(child->style.width)
                    ? resolve_length(*child, child->style.width, content.width, 0)
                    : 0.0F;
                authored_column_widths.insert(
                    authored_column_widths.end(), span, width);
            }
        };
        collect_column_widths(collect_column_widths, parent);
        column_count = std::max(column_count, authored_column_widths.size());
        const auto border_collapsed = parent.style.table().border_collapsed;
        const auto column_spacing = border_collapsed ? 0.0F : std::max(
            0.0F,
            resolve_length(
                parent,
                parent.style.table().border_spacing_horizontal,
                content.width,
                2.0F));
        const auto row_spacing = border_collapsed ? 0.0F : std::max(
            0.0F,
            resolve_length(
                parent,
                parent.style.table().border_spacing_vertical,
                content.height,
                2.0F));
        const auto track_available_width = std::max(
            0.0F,
            content.width - column_spacing * static_cast<float>(column_count + 1U));
        std::vector<float> columns(column_count, 0.0F);
        std::copy(
            authored_column_widths.begin(),
            authored_column_widths.end(),
            columns.begin());
        std::vector<bool> fixed_columns(column_count, false);
        std::vector<bool> percentage_columns(column_count, false);
        std::vector<bool> specified_columns(column_count, false);
        for (const auto& row : rows) {
            for (const auto& placement : row.cells) {
                auto cell_width = intrinsic_size(
                    *placement.cell, true, track_available_width);
                if (parent.style.table_layout_fixed
                    && is_specified(placement.cell->style.width)) {
                    cell_width = resolve_length(
                        *placement.cell,
                        placement.cell->style.width,
                        track_available_width,
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
        if (!columns.empty() && track_available_width > intrinsic_width) {
            const auto excess = track_available_width - intrinsic_width;
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
                const auto share = track_available_width
                    / static_cast<float>(columns.size());
                std::fill(columns.begin(), columns.end(), share);
            }
        }
        auto& parent_table = parent.mutable_table_layout();
        parent_table.column_widths = columns;
        parent_table.column_spacing = column_spacing;
        parent_table.row_spacing = row_spacing;

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
                    if (span_row + 1U < row_end) cell_table.cell_height += row_spacing;
                }
            }
        }

        auto cursor_y = content.y + (rows.empty() ? 0.0F : row_spacing);
        for (auto* child : composed_children(parent)) {
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
                auto row_count = size_t{0};
                for (const auto& row : rows) {
                    if (composed_parent(*row.row) == child) {
                        height += row.row->table_layout().row_height;
                        ++row_count;
                    }
                }
                if (row_count > 1U) {
                    height += row_spacing * static_cast<float>(row_count - 1U);
                }
            } else {
                height = intrinsic_size(*child, false, content.height);
            }
            auto& child_table = child->mutable_table_layout();
            child_table.column_widths = columns;
            child_table.column_spacing = column_spacing;
            child_table.row_spacing = row_spacing;
            layout_child(*child, parent.layout, {content.x, cursor_y, content.width, height});
            cursor_y += child->layout.height + row_spacing;
        }
        std::vector<float> column_offsets(
            columns.size() + 1U,
            content.x + (columns.empty() ? 0.0F : column_spacing));
        for (size_t column = 0; column < columns.size(); ++column) {
            column_offsets[column + 1U] = column_offsets[column]
                + columns[column] + column_spacing;
        }
        size_t column_cursor = 0;
        const auto table_track_height = std::max(0.0F, cursor_y - content.y);
        const auto layout_column_tracks = [&](const auto& self,
                                              dom_node& track_parent) -> void {
            for (auto* track : composed_children(track_parent)) {
                if (track->style.display == display_mode::table_column_group) {
                    const auto start = column_cursor;
                    self(self, *track);
                    const auto end = std::min(column_cursor, columns.size());
                    track->layout = {
                        column_offsets[std::min(start, columns.size())],
                        content.y,
                        column_offsets[end]
                            - column_offsets[std::min(start, columns.size())]
                            - (end > start ? column_spacing : 0.0F),
                        table_track_height};
                    track->visible = false;
                    continue;
                }
                if (track->style.display != display_mode::table_column) continue;
                const auto span_value = track->attributes.find("span");
                const auto span = span_value == track->attributes.end()
                    ? size_t{1}
                    : std::max<size_t>(
                        1,
                        static_cast<size_t>(parse_number(span_value->second, 1)));
                const auto start = std::min(column_cursor, columns.size());
                const auto end = std::min(columns.size(), column_cursor + span);
                track->layout = {
                    column_offsets[start],
                    content.y,
                    column_offsets[end] - column_offsets[start]
                        - (end > start ? column_spacing : 0.0F),
                    table_track_height};
                track->visible = false;
                track->scroll_content_width = track->layout.width;
                track->scroll_content_height = track->layout.height;
                column_cursor += span;
            }
        };
        layout_column_tracks(layout_column_tracks, parent);
        const auto spaced_intrinsic_width = intrinsic_width
            + (columns.empty() ? 0.0F
                : column_spacing * static_cast<float>(columns.size() + 1U));
        parent.scroll_content_width = std::max(content.width, spaced_intrinsic_width);
        parent.scroll_content_height = std::max(content.height, cursor_y - content.y);
        return;
    }

    if (is_table_row_group(parent.style.display)
        && std::none_of(composed_children(parent).begin(), composed_children(parent).end(), [](const auto* child) {
            return child->style.display == display_mode::table_row;
        })) {
        std::vector<dom_node*> cells;
        std::vector<dom_node*> captions;
        for (auto* child : composed_children(parent)) {
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
        auto placed_rows = size_t{0};
        for (auto* child : composed_children(parent)) {
            child->visible = parent.visible && child->style.display != display_mode::none;
            if (!child->visible) {
                child->layout = {};
                continue;
            }
            if (child->style.display != display_mode::table_row) continue;
            if (placed_rows > 0U) cursor_y += parent.table_layout().row_spacing;
            auto& child_table = child->mutable_table_layout();
            child_table.column_widths = parent.table_layout().column_widths;
            child_table.column_spacing = parent.table_layout().column_spacing;
            child_table.row_spacing = parent.table_layout().row_spacing;
            layout_child(*child, parent.layout, {
                content.x,
                cursor_y,
                content.width,
                child->table_layout().row_height});
            cursor_y += child->layout.height;
            ++placed_rows;
        }
        parent.scroll_content_width = content.width;
        parent.scroll_content_height = std::max(content.height, cursor_y - content.y);
        return;
    }

    if (parent.style.display == display_mode::table_row) {
        const auto& parent_table = parent.table_layout();
        std::vector<float> offsets(
            parent_table.column_widths.size() + 1,
            content.x + (parent_table.column_widths.empty()
                ? 0.0F : parent_table.column_spacing));
        for (size_t column = 0; column < parent_table.column_widths.size(); ++column) {
            offsets[column + 1] = offsets[column]
                + parent_table.column_widths[column]
                + parent_table.column_spacing;
        }
        for (auto* child : composed_children(parent)) {
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
                offsets[end] - offsets[child_table.column_index]
                    - (end > child_table.column_index
                        ? parent_table.column_spacing : 0.0F),
                child_table.cell_height > 0 ? child_table.cell_height : content.height});
            if (parent.style.align_items == align_mode::center && !composed_children(*child).empty()) {
                auto content_top = std::numeric_limits<float>::max();
                auto content_bottom = std::numeric_limits<float>::lowest();
                for (const auto* descendant : composed_children(*child)) {
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
                        for (auto* descendant : composed_children(node)) shift_subtree(*descendant);
                    };
                    for (auto* descendant : composed_children(*child)) shift_subtree(*descendant);
                }
            }
        }
        auto improper_x = offsets.empty() ? content.x : offsets.back();
        auto improper_height = content.height;
        for (auto* child : composed_children(parent)) {
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
        parent.scroll_content_width = offsets.empty() ? content.width
            : offsets.back() - content.x;
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
        if (!intrinsic_keyword) {
            const auto box_edges = horizontal_axis
                ? resolve_length(node, node.style.padding_left, available, 0)
                    + resolve_length(node, node.style.padding_right, available, 0)
                    + resolve_length(node, node.style.border_left_width, available, 0)
                    + resolve_length(node, node.style.border_right_width, available, 0)
                : resolve_length(node, node.style.padding_top, available, 0)
                    + resolve_length(node, node.style.padding_bottom, available, 0)
                    + resolve_length(node, node.style.border_top_width, available, 0)
                    + resolve_length(node, node.style.border_bottom_width, available, 0);
            if (node.style.border_box) size = std::max(size, box_edges);
            else size += box_edges;
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
            composed_children(parent).begin(),
            composed_children(parent).end(),
            [](const dom_node* child) {
                return child->style.display == display_mode::none
                    || is_out_of_flow(child->style.position)
                    || is_inline_level(child->style.display);
            });
    if (inline_formatting_context) {
        std::vector<dom_node*> text_runs;
        std::deque<dom_node> generated_whitespace_runs;
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
            // Relative positioning moves an inline box's painted/CSSOM
            // geometry without changing its normal-flow advance. Flattening
            // the descendant text into its ancestor loses that offset (most
            // visibly for small raised price fractions), so retain the box as
            // an atomic inline participant and let the general path apply its
            // relative inset after assigning the flow position.
            if (node.style.position == position_mode::relative
                && (is_specified(node.style.left)
                    || is_specified(node.style.right)
                    || is_specified(node.style.top)
                    || is_specified(node.style.bottom))) {
                return false;
            }
            if (node.tag == "#text") {
                // Whitespace-only DOM nodes still contribute one collapsed
                // boundary between adjacent inline descendants. Retain them in
                // the run stream so `text <strong>text</strong>` does not become
                // `texttext`; they produce no standalone glyph box.
                if (!node.text_content.empty()) text_runs.push_back(&node);
                return true;
            }
            // BR is inline-level but forces a line break. The flattened text-run
            // path has no sentinel for forced breaks, so retain it as an item.
            if (node.tag == "br") return false;
            // Inline-block and inline-table descendants are atomic inline
            // boxes. Flattening their text into the surrounding line discards
            // their own padding, background, and border geometry.
            if (node.style.display != display_mode::inline_flow) return false;
            const auto append_generated_whitespace = [&](const auto& pseudo) {
                if (!pseudo_generates_box(pseudo)) return true;
                // Whitespace-only generated inline content is a line-breaking
                // boundary, not an atomic inline box. Keeping it in the
                // flattened run stream lets adjacent styled spans fragment
                // across lines while preserving the authored separator.
                if (pseudo.display != display_mode::inline_flow
                    || is_out_of_flow(pseudo.position)
                    || std::any_of(
                        pseudo.content.begin(),
                        pseudo.content.end(),
                        [](unsigned char character) { return !std::isspace(character); })) {
                    return false;
                }
                generated_whitespace_runs.push_back(
                    make_pseudo_layout_node(node, pseudo));
                text_runs.push_back(&generated_whitespace_runs.back());
                return true;
            };
            if (!append_generated_whitespace(node.style.before_pseudo())) return false;
            // Inline flex/grid boxes establish their own formatting contexts. Flattening
            // their descendants into the surrounding text run loses their padding,
            // alignment and line-box height.
            if (is_flex_container(node.style.display)
                || is_grid_container(node.style.display)) {
                return false;
            }
            if (is_specified(node.style.width) || is_specified(node.style.height)) return false;
            for (auto* child : composed_children(node)) {
                if (child->style.display != display_mode::none
                    && !is_out_of_flow(child->style.position)
                    && !is_inline_level(child->style.display)) {
                    return false;
                }
                if (!collect_runs(*child)) return false;
            }
            if (!append_generated_whitespace(node.style.after_pseudo())) return false;
            return true;
        };
        auto supported = true;
        for (auto* child : composed_children(parent)) {
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
            dom_node* pending_space_run = nullptr;
            std::vector<layout_rect> static_anchors(text_runs.size() + 1U);
            static_anchors[0] = {
                content.x,
                content.y,
                0,
                0};
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
                        // The host shaper's advance is not necessarily additive:
                        // kerning, ligatures and platform-font calibration can make
                        // measure("one two") differ from the sum of separately
                        // measured words and spaces. The scene paints the merged
                        // fragment as one shaped run, so keep its layout advance in
                        // lockstep with that exact run as it is assembled.
                        auto combined_text = previous.text + text;
                        const auto combined_width = measure_text_width(combined_text, run);
                        cursor_x += combined_width - previous.width;
                        previous.text = std::move(combined_text);
                        previous.width = combined_width;
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
                const auto allow_wrap = resolved_white_space_wraps(*run);
                const auto transformed_text = resolved_text_transform(*run, run->text_content);
                std::string word;
                const auto flush_word = [&] {
                    if (word.empty()) return;
                    auto word_width = measure_text_width(word, *run);
                    auto space_width = pending_space && cursor_x > 0
                        ? measure_text_width(" ", *run) : 0.0F;
                    // Intrinsic sizing and the incremental word/space shaper can
                    // differ at styled inline boundaries. An auto-sized flex item
                    // is using that intrinsic contribution as its main size, so it
                    // must not wrap solely because the two measurement paths land
                    // on opposite sides of the edge. A genuinely flex-shrunk item
                    // still exceeds this font-relative allowance and wraps.
                    const auto intrinsic_flex_item = parent.parent != nullptr
                        && is_flex_container(parent.parent->style.display)
                        && !is_specified(parent.style.width);
                    const auto inline_wrap_epsilon = intrinsic_flex_item
                        ? std::max(0.25F, resolved_font_size(parent) * 0.35F)
                        : 0.25F;
                    if (allow_wrap && cursor_x > 0
                        && cursor_x + space_width + word_width
                            > content.width + inline_wrap_epsilon) {
                        start_new_line();
                        space_width = 0;
                    }
                    if (space_width > 0) {
                        append_fragment(
                            pending_space_run != nullptr ? *pending_space_run : *run,
                            " ",
                            space_width);
                    }
                    if (!word.empty()) append_fragment(*run, word, word_width);
                    word.clear();
                    pending_space = false;
                    pending_space_run = nullptr;
                };
                for (const auto character : transformed_text) {
                    if (std::isspace(static_cast<unsigned char>(character))) {
                        flush_word();
                        pending_space = true;
                        pending_space_run = run;
                    } else {
                        word.push_back(character);
                    }
                }
                flush_word();
                static_anchors[run_index + 1U] = {
                    content.x + cursor_x,
                    content.y + cursor_y,
                    0,
                    current_line_height};
            }

            // The flattened inline fast path places fragments from the content
            // edge as it assembles each line. Apply the block's inline-axis
            // alignment after every line has its final shaped advance; keeping
            // this in layout (rather than centering each paint command inside
            // its own glyph-width box) also makes CSSOM geometry and positioned
            // inline static anchors agree with the painted result.
            const auto text_alignment = resolved_text_align(parent);
            if (text_alignment == "center" || text_alignment == "right"
                || text_alignment == "end") {
                struct flattened_line_alignment final {
                    float y{0};
                    float right{0};
                    float offset{0};
                };
                std::vector<flattened_line_alignment> lines;
                for (const auto* run : text_runs) {
                    for (const auto& fragment : run->text_layout_fragments) {
                        auto line = std::find_if(
                            lines.begin(),
                            lines.end(),
                            [&](const auto& candidate) {
                                return std::abs(candidate.y - fragment.y) < 0.01F;
                            });
                        if (line == lines.end()) {
                            lines.push_back({fragment.y, fragment.x + fragment.width, 0});
                        } else {
                            line->right = std::max(line->right, fragment.x + fragment.width);
                        }
                    }
                }
                for (auto& line : lines) {
                    const auto used_width = std::max(0.0F, line.right - content.x);
                    const auto free_width = std::max(0.0F, content.width - used_width);
                    line.offset = text_alignment == "center"
                        ? free_width * 0.5F
                        : free_width;
                }
                const auto offset_for_y = [&](float y) {
                    const auto line = std::find_if(
                        lines.begin(),
                        lines.end(),
                        [&](const auto& candidate) {
                            return std::abs(candidate.y - y) < 0.01F;
                        });
                    return line == lines.end() ? 0.0F : line->offset;
                };
                for (auto* run : text_runs) {
                    for (auto& fragment : run->text_layout_fragments) {
                        fragment.x += offset_for_y(fragment.y);
                    }
                }
                for (auto& anchor : static_anchors) {
                    anchor.x += offset_for_y(anchor.y);
                }
            }

            // A whitespace-only pseudo is flattened into a synthetic text run
            // so it can participate in collapsing and wrapping. Its fragment
            // still belongs to the originating inline box for CSSOM geometry:
            // getBoundingClientRect() on `span::after { content: " " }` must
            // include the generated advance even though the pseudo has no DOM
            // node of its own.
            for (auto& generated : generated_whitespace_runs) {
                if (generated.parent == nullptr) continue;
                auto& originating_fragments = generated.parent->text_layout_fragments;
                originating_fragments.insert(
                    originating_fragments.end(),
                    generated.text_layout_fragments.begin(),
                    generated.text_layout_fragments.end());
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
                    for (auto* child : composed_children(node)) {
                        layout_rect child_bounds{};
                        if (update_inline_box(*child, child_bounds)) include(child_bounds);
                    }
                    node.layout = has_bounds
                        ? bounds
                        : layout_rect{content.x + cursor_x, content.y + cursor_y, 0, 0};
                    return has_bounds;
                };
            for (auto* child : composed_children(parent)) {
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
                    for (auto* ancestor = composed_parent(child);
                        ancestor != nullptr && ancestor != body_;
                        ancestor = composed_parent(*ancestor)) {
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
                    : anchor.y + anchor.height;
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
                // Inline formatting collects out-of-flow descendants into a
                // deferred positioned pass. They retain the same min/max used
                // size rules as positioned children of block/flex formatting
                // contexts; omitting this clamp lets fixed popups ignore their
                // viewport-bounded max-height and grow with their contents.
                const auto constrain_positioned_size = [&](bool horizontal, float size) {
                    const auto available = horizontal
                        ? containing.width : containing.height;
                    const auto& minimum = horizontal
                        ? child.style.min_width : child.style.min_height;
                    const auto& maximum = horizontal
                        ? child.style.max_width : child.style.max_height;
                    const auto box_adjustment = child.style.border_box ? 0.0F
                        : horizontal
                            ? resolve_length(child, child.style.padding_left, available, 0)
                                + resolve_length(child, child.style.padding_right, available, 0)
                                + resolve_length(child, child.style.border_left_width, available, 0)
                                + resolve_length(child, child.style.border_right_width, available, 0)
                            : resolve_length(child, child.style.padding_top, available, 0)
                                + resolve_length(child, child.style.padding_bottom, available, 0)
                                + resolve_length(child, child.style.border_top_width, available, 0)
                                + resolve_length(child, child.style.border_bottom_width, available, 0);
                    if (is_specified(maximum)) {
                        size = std::min(
                            size,
                            resolve_length(child, maximum, available, size)
                                + box_adjustment);
                    }
                    if (is_specified(minimum)) {
                        size = std::max(
                            size,
                            resolve_length(child, minimum, available, 0)
                                + box_adjustment);
                    }
                    return std::max(0.0F, size);
                };
                assigned.width = constrain_positioned_size(true, assigned.width);
                assigned.height = constrain_positioned_size(false, assigned.height);
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
    const auto implicit_column_grid = parent_grid.auto_flow_column
        && !parent_grid.auto_columns.empty();
    if (is_grid_container(parent.style.display)
        && (parent_grid.two_columns || implicit_column_grid)) {
        using grid_track = node_style::grid_data::track;
        struct grid_item final {
            dom_node* node{nullptr};
            size_t row{0};
            size_t column{0};
            size_t row_span{1};
            size_t column_span{1};
        };
        auto effective_columns = parent_grid.template_columns;
        if (effective_columns.empty() && implicit_column_grid) {
            const auto implicit_count = std::max<size_t>(
                1U,
                std::count_if(
                    composed_children(parent).begin(),
                    composed_children(parent).end(),
                    [](const dom_node* child) {
                        return child != nullptr
                            && child->style.display != display_mode::none
                            && !is_out_of_flow(child->style.position)
                            && !is_collapsible_whitespace_text(*child);
                    }));
            effective_columns.reserve(implicit_count);
            for (size_t index = 0; index < implicit_count; ++index) {
                effective_columns.push_back(
                    parent_grid.auto_columns[index % parent_grid.auto_columns.size()]);
            }
        }
        const auto column_count = std::max<size_t>(
            implicit_column_grid ? 1U : 2U,
            effective_columns.size());
        const auto parse_line = [](std::string_view value) -> std::optional<int32_t> {
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos) return std::nullopt;
            const auto last = value.find_last_not_of(" \t\r\n");
            value = value.substr(first, last - first + 1U);
            if (value.empty() || value == "auto" || value.starts_with("span")) {
                return std::nullopt;
            }
            int32_t result = 0;
            const auto parsed = std::from_chars(
                value.data(), value.data() + value.size(), result);
            return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size()
                ? std::optional<int32_t>{result}
                : std::nullopt;
        };
        const auto parse_span = [&](std::string_view start_value,
                                    std::string_view end_value,
                                    size_t track_count) {
            const auto start = parse_line(start_value);
            const auto end = parse_line(end_value);
            if (start.has_value() && end.has_value()) {
                const auto normalized_end = *end < 0
                    ? static_cast<int32_t>(track_count) + *end + 2
                    : *end;
                return static_cast<size_t>(std::max(1, normalized_end - *start));
            }
            const auto span_value = start_value.find("span") != std::string_view::npos
                ? start_value : end_value;
            const auto marker = span_value.find("span");
            if (marker != std::string_view::npos) {
                const auto digits = span_value.find_first_of("0123456789", marker + 4U);
                if (digits != std::string_view::npos) {
                    return std::max<size_t>(
                        1U,
                        static_cast<size_t>(parse_number(
                            std::string(span_value.substr(digits)), 1.0F)));
                }
            }
            return size_t{1};
        };
        std::vector<std::vector<bool>> occupied;
        const auto ensure_rows = [&](size_t count) {
            while (occupied.size() < count) occupied.emplace_back(column_count, false);
        };
        const auto fits = [&](size_t row, size_t column, size_t row_span, size_t column_span) {
            if (column >= column_count || column + column_span > column_count) return false;
            for (size_t candidate_row = row; candidate_row < row + row_span; ++candidate_row) {
                if (candidate_row >= occupied.size()) continue;
                for (size_t candidate_column = column;
                     candidate_column < column + column_span;
                     ++candidate_column) {
                    if (occupied[candidate_row][candidate_column]) return false;
                }
            }
            return true;
        };
        std::vector<grid_item> items;
        auto auto_row = size_t{0};
        auto auto_column = size_t{0};
        for (auto* child : composed_children(parent)) {
            child->visible = parent.visible && child->style.display != display_mode::none;
            // CSS Grid does not generate anonymous grid items for
            // whitespace-only text. Counting indentation here creates extra
            // implicit rows and therefore extra gutters.
            if (!child->visible || is_collapsible_whitespace_text(*child)) {
                child->visible = false;
                child->layout = {};
                continue;
            }
            if (is_out_of_flow(child->style.position)) continue;
            const auto& placement = child->style.grid();
            auto column_span = std::min(
                column_count,
                parse_span(
                    placement.column_start_value,
                    placement.column_end_value,
                    column_count));
            auto row_span = parse_span(
                placement.row_start_value,
                placement.row_end_value,
                std::max<size_t>(parent_grid.template_rows.size(), 1U));
            const auto authored_column = parse_line(placement.column_start_value);
            const auto authored_row = parse_line(placement.row_start_value);
            auto row = authored_row.has_value() && *authored_row > 0
                ? static_cast<size_t>(*authored_row - 1) : auto_row;
            auto column = authored_column.has_value() && *authored_column > 0
                ? static_cast<size_t>(*authored_column - 1) : auto_column;
            if (authored_column.has_value() && *authored_column < 0) {
                column = static_cast<size_t>(std::max<int32_t>(
                    0, static_cast<int32_t>(column_count) + *authored_column + 1));
            }
            if (column + column_span > column_count) {
                column = column_count - column_span;
            }
            while (!fits(row, column, row_span, column_span)) {
                if (!authored_column.has_value()) {
                    ++column;
                    if (column + column_span > column_count) {
                        column = 0;
                        ++row;
                    }
                } else {
                    ++row;
                }
            }
            ensure_rows(row + row_span);
            for (size_t candidate_row = row; candidate_row < row + row_span; ++candidate_row) {
                for (size_t candidate_column = column;
                     candidate_column < column + column_span;
                     ++candidate_column) {
                    occupied[candidate_row][candidate_column] = true;
                }
            }
            items.push_back({child, row, column, row_span, column_span});
            auto_row = row;
            auto_column = column + column_span;
            if (auto_column >= column_count) {
                auto_column = 0;
                ++auto_row;
            }
        }

        const auto column_gap = resolve_length(
            parent, parent.style.column_gap, content.width, 0);
        const auto row_gap = resolve_length(
            parent, parent.style.row_gap, content.height, 0);
        std::vector<float> column_widths(column_count, 0.0F);
        auto fractional_weight = 0.0F;
        for (size_t column = 0; column < effective_columns.size(); ++column) {
            const auto& track = effective_columns[column];
            if (track.kind == grid_track::sizing::fixed) {
                column_widths[column] = resolve_length(
                    parent, track.maximum, content.width, 0);
            } else if (track.kind == grid_track::sizing::minmax) {
                column_widths[column] = resolve_length(
                    parent, track.minimum, content.width, 0);
                if (track.fraction <= 0 && is_specified(track.maximum)) {
                    column_widths[column] = std::max(
                        column_widths[column],
                        resolve_length(parent, track.maximum, content.width, 0));
                }
            }
            fractional_weight += track.fraction;
        }
        for (const auto& item : items) {
            if (item.column_span != 1U) continue;
            const auto authored_track = item.column < effective_columns.size()
                ? &effective_columns[item.column] : nullptr;
            if (authored_track != nullptr
                && authored_track->kind != grid_track::sizing::automatic) continue;
            const auto margins = resolve_length(
                    *item.node, item.node->style.margin_left, content.width, 0)
                + resolve_length(
                    *item.node, item.node->style.margin_right, content.width, 0);
            column_widths[item.column] = std::max(
                column_widths[item.column],
                intrinsic_size(*item.node, true, content.width) + margins);
        }
        const auto total_gap = column_gap * static_cast<float>(column_count - 1U);
        const auto committed_width = std::accumulate(
            column_widths.begin(), column_widths.end(), total_gap);
        const auto fractional_space = std::max(0.0F, content.width - committed_width);
        if (fractional_weight > 0) {
            for (size_t column = 0; column < effective_columns.size(); ++column) {
                const auto& track = effective_columns[column];
                if (track.fraction <= 0) continue;
                column_widths[column] = std::max(
                    column_widths[column],
                    fractional_space * track.fraction / fractional_weight);
            }
        } else if (committed_width < content.width && !column_widths.empty()) {
            column_widths.back() += content.width - committed_width;
        }

        const auto row_count = std::max<size_t>(
            occupied.size(), parent_grid.template_rows.size());
        std::vector<float> row_heights(row_count, 0.0F);
        auto fractional_row_weight = 0.0F;
        for (size_t row = 0; row < parent_grid.template_rows.size(); ++row) {
            const auto& track = parent_grid.template_rows[row];
            if (track.kind == grid_track::sizing::fixed) {
                row_heights[row] = resolve_length(parent, track.maximum, content.height, 0);
            } else if (track.kind == grid_track::sizing::minmax) {
                row_heights[row] = resolve_length(parent, track.minimum, content.height, 0);
            }
            fractional_row_weight += track.fraction;
        }
        for (const auto& item : items) {
            if (item.row_span != 1U || item.row >= row_heights.size()) continue;
            const auto authored_track = item.row < parent_grid.template_rows.size()
                ? &parent_grid.template_rows[item.row] : nullptr;
            if (authored_track != nullptr
                && authored_track->kind == grid_track::sizing::fixed) continue;
            const auto margins = resolve_length(
                    *item.node, item.node->style.margin_top, content.height, 0)
                + resolve_length(
                    *item.node, item.node->style.margin_bottom, content.height, 0);
            const auto authored = is_specified(item.node->style.height)
                ? outer_authored_size(*item.node, false, content.height)
                : intrinsic_size(*item.node, false, content.height);
            row_heights[item.row] = std::max(row_heights[item.row], authored + margins);
        }
        const auto total_row_gap = row_count > 0
            ? row_gap * static_cast<float>(row_count - 1U) : 0.0F;
        const auto committed_height = std::accumulate(
            row_heights.begin(), row_heights.end(), total_row_gap);
        const auto fractional_row_space = std::max(0.0F, content.height - committed_height);
        if (fractional_row_weight > 0) {
            for (size_t row = 0; row < parent_grid.template_rows.size(); ++row) {
                const auto& track = parent_grid.template_rows[row];
                if (track.fraction <= 0) continue;
                row_heights[row] = std::max(
                    row_heights[row],
                    fractional_row_space * track.fraction / fractional_row_weight);
            }
        }

        std::vector<float> column_offsets(column_count, content.x);
        for (size_t column = 1; column < column_count; ++column) {
            column_offsets[column] = column_offsets[column - 1U]
                + column_widths[column - 1U] + column_gap;
        }
        std::vector<float> row_offsets(row_count, content.y);
        for (size_t row = 1; row < row_count; ++row) {
            row_offsets[row] = row_offsets[row - 1U] + row_heights[row - 1U] + row_gap;
        }
        for (const auto& item : items) {
            const auto available_width = std::accumulate(
                    column_widths.begin() + static_cast<std::ptrdiff_t>(item.column),
                    column_widths.begin() + static_cast<std::ptrdiff_t>(item.column + item.column_span),
                    0.0F)
                + column_gap * static_cast<float>(item.column_span - 1U);
            const auto available_height = std::accumulate(
                    row_heights.begin() + static_cast<std::ptrdiff_t>(item.row),
                    row_heights.begin() + static_cast<std::ptrdiff_t>(item.row + item.row_span),
                    0.0F)
                + row_gap * static_cast<float>(item.row_span - 1U);
            const auto arrange = [&](dom_node* child) {
                const auto margin_left = resolve_length(
                    *child, child->style.margin_left, available_width, 0);
                const auto margin_right = resolve_length(
                    *child, child->style.margin_right, available_width, 0);
                const auto margin_top = resolve_length(
                    *child, child->style.margin_top, available_height, 0);
                const auto margin_bottom = resolve_length(
                    *child, child->style.margin_bottom, available_height, 0);
                auto width = is_specified(child->style.width)
                    ? outer_authored_size(*child, true, available_width)
                    : std::max(0.0F, available_width - margin_left - margin_right);
                auto height = is_specified(child->style.height)
                    ? outer_authored_size(*child, false, available_height)
                    : std::max(0.0F, available_height - margin_top - margin_bottom);
                layout_child(*child, parent.layout, {
                    column_offsets[item.column] + margin_left,
                    row_offsets[item.row] + margin_top,
                    width,
                    height});
            };
            arrange(item.node);
        }
        parent.scroll_content_width = std::max(
            content.width,
            std::accumulate(column_widths.begin(), column_widths.end(), total_gap));
        parent.scroll_content_height = std::max(
            content.height,
            std::accumulate(row_heights.begin(), row_heights.end(), total_row_gap));
        return;
    }
    if (is_grid_container(parent.style.display) && parent_grid.fractional_rows) {
        std::vector<dom_node*> flow_children;
        const auto append_grid_child = [&](const auto& self, dom_node* child) -> void {
            if (child == nullptr) return;
            child->visible = parent.visible && child->style.display != display_mode::none;
            if (!child->visible) {
                child->layout = {};
                return;
            }
            if (child->style.display == display_mode::contents
                && !is_out_of_flow(child->style.position)) {
                child->layout = {};
                child->scroll_content_width = 0;
                child->scroll_content_height = 0;
                for (auto* descendant : composed_children(*child)) {
                    self(self, descendant);
                }
                return;
            }
            if (!is_out_of_flow(child->style.position)) flow_children.push_back(child);
        };
        for (auto* child : composed_children(parent)) {
            append_grid_child(append_grid_child, child);
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
    const auto flex_container = is_flex_container(parent.style.display);
    const auto inline_item_contains_block_box = [&](const dom_node* child) {
        if (child == nullptr || child->style.display != display_mode::inline_flow) return false;
        return std::any_of(
            composed_children(*child).begin(),
            composed_children(*child).end(),
            [](const dom_node* descendant) {
                return descendant->style.display != display_mode::none
                    && !is_out_of_flow(descendant->style.position)
                    && !is_inline_level(descendant->style.display);
            });
    };
    std::vector<dom_node*> layout_items;
    layout_items.reserve(composed_children(parent).size() + 3U);
    if (has_inside_marker) {
        marker_item.emplace(make_list_marker_layout_node(parent, marker_text));
        layout_items.push_back(&*marker_item);
    }
    if (has_before) {
        before_item.emplace(make_pseudo_layout_node(parent, parent.style.before_pseudo()));
        layout_items.push_back(&*before_item);
    }
    const auto append_layout_item = [&](const auto& self, dom_node* child) -> void {
        if (flex_container
            && child != nullptr
            && child->style.display == display_mode::contents
            && !is_out_of_flow(child->style.position)) {
            // display:contents contributes no principal box. Its children are
            // flex items of the nearest flex formatting context while their
            // DOM ancestry remains unchanged for inheritance and selectors.
            child->visible = parent.visible;
            child->layout = {};
            child->scroll_content_width = 0;
            child->scroll_content_height = 0;
            for (auto* descendant : composed_children(*child)) self(self, descendant);
            return;
        }
        if (child != nullptr) layout_items.push_back(child);
    };
    for (auto* child : composed_children(parent)) {
        append_layout_item(append_layout_item, child);
    }
    if (has_after) {
        after_item.emplace(make_pseudo_layout_node(parent, parent.style.after_pseudo()));
        layout_items.push_back(&*after_item);
    }

    const auto inline_flow = !flex_container && std::all_of(
        layout_items.begin(),
        layout_items.end(),
        [&](const dom_node* child) {
                return child->style.display == display_mode::none
                || is_out_of_flow(child->style.position)
                || (is_inline_level(child->style.display)
                    && !inline_item_contains_block_box(child));
        });
    const auto horizontal = (flex_container && parent.style.direction == flex_direction::row)
        || inline_flow;
    std::vector<dom_node*> ordered_children = layout_items;
    if (flex_container && parent.style.flex_reverse) {
        std::reverse(ordered_children.begin(), ordered_children.end());
    }
    const auto is_collapsed_inline_boundary = [&](const dom_node* child) {
        if (!inline_flow || !is_collapsible_whitespace_text(*child)) return false;
        const auto current = std::find(ordered_children.begin(), ordered_children.end(), child);
        const auto has_visible_before = std::any_of(
            ordered_children.begin(), current, [](const dom_node* candidate) {
                return candidate->style.display != display_mode::none
                    && !is_out_of_flow(candidate->style.position)
                    && !is_collapsible_whitespace_text(*candidate);
            });
        const auto after = current == ordered_children.end() ? current : current + 1;
        const auto has_visible_after = std::any_of(
            after, ordered_children.end(), [](const dom_node* candidate) {
                return candidate->style.display != display_mode::none
                    && !is_out_of_flow(candidate->style.position)
                    && !is_collapsible_whitespace_text(*candidate);
            });
        return !has_visible_before || !has_visible_after;
    };
    const auto participates_in_flow = [&](const dom_node* child) {
        return child->style.display != display_mode::none
            && !is_out_of_flow(child->style.position)
            && !(flex_container && is_collapsible_whitespace_text(*child))
            && !is_collapsed_inline_boundary(child);
    };
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
    float fixed = 0;
    float grow = 0;
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
    const auto contains_replaced_content = [&](const auto& self,
                                                const dom_node& candidate) -> bool {
        if (candidate.tag == "svg" || candidate.tag == "img"
            || candidate.tag == "canvas" || candidate.tag == "input"
            || candidate.tag == "select" || candidate.tag == "textarea") {
            return true;
        }
        return std::any_of(
            composed_children(candidate).begin(),
            composed_children(candidate).end(),
            [&](const dom_node* descendant) {
                return descendant != nullptr && self(self, *descendant);
            });
    };
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
                : contains_replaced_content(contains_replaced_content, *child)
                ? constrain_size(
                    *child,
                    horizontal,
                    intrinsic_size(*child, horizontal, main_available),
                    main_available)
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
        && automatic_main_margin_count > 0U
        ? remaining / static_cast<float>(automatic_main_margin_count)
        : 0.0F;
    const auto justify_free = flex_container && grow == 0
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
    auto inline_line_offset = 0.0F;
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
        if (!is_out_of_flow(child->style.position)
            && ((flex_container && is_collapsible_whitespace_text(*child))
                || is_collapsed_inline_boundary(child))) {
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

        if (inline_flow && !flex_container && child->tag == "br") {
            const auto font_size = resolved_font_size(*child);
            const auto line_height = resolved_line_height(*child, font_size);
            child->layout = {
                content.x + cursor,
                content.y + inline_line_offset,
                0,
                line_height};
            child->scroll_content_width = 0;
            child->scroll_content_height = 0;
            maximum_flow_bottom = std::max(
                maximum_flow_bottom,
                child->layout.y + child->layout.height);
            cursor = 0;
            inline_line_offset += line_height;
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
                x + child->layout.width + margin_right);
            maximum_flow_bottom = std::max(
                maximum_flow_bottom,
                float_band_y + outer_height);
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
                    ancestor = composed_parent(*ancestor)) {
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
            const auto flex_static_main_offset = [&] {
                // An auto-inset out-of-flow child keeps the position its
                // margin box would have occupied in normal flow. `cursor` is
                // the inline-axis cursor for inline formatting and the
                // block-axis cursor for ordinary block formatting.
                if (!flex_container) return cursor;
                if (parent.style.justify_content == justify_mode::center) {
                    return horizontal
                        ? (content.width - assigned.width) * 0.5F
                        : (content.height - assigned.height) * 0.5F;
                }
                if (parent.style.justify_content == justify_mode::end) {
                    return horizontal
                        ? content.width - assigned.width
                        : content.height - assigned.height;
                }
                return 0.0F;
            }();
            assigned.x = containing.x + (has_left
                ? left + margin_left
                : has_right
                    ? containing.width - right - margin_right - assigned.width
                    : content.x - containing.x
                        + (horizontal ? flex_static_main_offset : 0.0F)
                        + margin_left);
            assigned.y = containing.y + (has_top
                ? top + margin_top
                : has_bottom
                    ? containing.height - bottom - margin_bottom - assigned.height
                    : content.y - containing.y
                        + (horizontal ? 0.0F : flex_static_main_offset)
                        + margin_top);
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
                    : 0.0F;
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
            // flex-basis sizes the main axis only. In a row it must not leak
            // into the cross-axis height (for example, an 80px watchlist
            // column basis inside a 31px-tall row).
            auto height = definite_authored_height
                ? outer_authored_size(*child, false, content.height)
                : flex_container
                    && ((child->style.align_self_specified
                            ? child->style.align_self
                            : parent.style.align_items) == align_mode::stretch
                        || intrinsic_height <= 0)
                    ? std::max(0.0F, available_line_cross_size - margin_top - margin_bottom)
                    : flex_container
                        ? intrinsic_height
                        : std::min(
                            std::max(
                                0.0F,
                                available_line_cross_size - margin_top - margin_bottom),
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
            const auto line_offset = inline_flow && !flex_container
                ? inline_line_offset : wrapped_cross_offset;
            const auto y = alignment == align_mode::center
                ? content.y + line_offset + margin_top
                    + (available_line_cross_size - margin_top - margin_bottom - height) * 0.5F
                : alignment == align_mode::end
                    ? content.y + line_offset + available_line_cross_size
                        - margin_bottom - height
                    : content.y + line_offset + margin_top;
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
            auto height = flex_container && is_specified(child->style.flex_basis)
                ? resolve_length(*child, child->style.flex_basis, content.height, 0)
                    + (grow > 0
                        ? remaining * std::max(0.0F, child->style.flex_grow) / grow
                        : 0.0F)
                : is_specified(child->style.height)
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
                    : 0.0F;
            height = constrain_size(*child, false, height, content.height);
            if (const auto shrunk = flex_shrunk_main_sizes.find(child);
                shrunk != flex_shrunk_main_sizes.end()) {
                height = constrain_size(*child, false, shrunk->second, content.height);
            }
            const auto alignment = child->style.align_self_specified
                ? child->style.align_self : parent.style.align_items;
            const auto intrinsic_width = intrinsic_size(*child, true, content.width);
            auto width = is_specified(child->style.width)
                ? outer_authored_size(*child, true, content.width)
                : !flex_container && is_table_container(child->style.display)
                    ? std::min(
                        std::max(0.0F, content.width - margin_left - margin_right),
                        intrinsic_width)
                : flex_container && alignment != align_mode::stretch
                    ? std::min(
                        std::max(0.0F, content.width - margin_left - margin_right),
                        intrinsic_width)
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
        // exposes HTML and BODY structural boxes beneath it. Those boxes remain
        // viewport-sized, but descendants can establish a larger scrollable
        // overflow area. Propagate that overflow through both root boundaries;
        // otherwise wide descendants stop at BODY while tall content only
        // appears to work because an auto-height BODY expands vertically.
        const auto propagates_document_overflow =
            (&parent == body_ && child->tag == "html")
            || (parent.tag == "html" && child->tag == "body");
        const auto flow_width = propagates_document_overflow
            ? std::max(child->layout.width, child->scroll_content_width)
            : child->layout.width;
        const auto flow_height = propagates_document_overflow
            ? std::max(child->layout.height, child->scroll_content_height)
            : child->layout.height;
        // Scroll translation is applied to `positioned`; `assigned` remains in
        // the content's logical coordinate space. Extents must therefore use
        // `assigned` directly. Adding the current offset here makes scrollWidth
        // or scrollHeight grow on every relayout and creates an endless range.
        maximum_flow_right = std::max(
            maximum_flow_right,
            assigned.x + flow_width);
        maximum_flow_bottom = std::max(
            maximum_flow_bottom,
            assigned.y + flow_height);
        if (!horizontal) {
            cursor = assigned.y - content.y
                + flow_height
                + vertical_margin_bottom
                + justify_gap;
        }
        ++placed_flow_count;
    }
    // Cross-axis alignment moves an already laid-out subtree. Generated boxes
    // are stored on their originating element rather than in composed_children,
    // so they must follow the same translation explicitly. Leaving them behind
    // makes a pseudo-element background paint at the pre-alignment position
    // while the element, its text, and getBoundingClientRect() all report the
    // aligned position.
    const auto shift_subtree_y = [&](const auto& self, dom_node& node, float offset) -> void {
        node.layout.y += offset;
        for (auto& fragment : node.text_layout_fragments) fragment.y += offset;
        if (node.style.has_pseudo_elements()) {
            auto& before = node.style.mutable_before_pseudo();
            auto& after = node.style.mutable_after_pseudo();
            if (before.generated) before.layout.y += offset;
            if (after.generated) after.layout.y += offset;
        }
        if (node.list_marker_layout.width > 0 || node.list_marker_layout.height > 0) {
            node.list_marker_layout.y += offset;
        }
        for (auto* descendant : composed_children(node)) self(self, *descendant, offset);
    };
    if (flex_container && horizontal && !parent.style.flex_wrap) {
        std::function<std::optional<float>(const dom_node&)> first_text_baseline =
            [&](const dom_node& node) -> std::optional<float> {
                if (node.visible && node.tag == "#text" && has_visible_text(node.text_content)) {
                    const auto font_size = resolved_font_size(node);
                    const auto metrics = measure_text(node.text_content, node);
                    const auto glyph_height = metrics.ascent + metrics.descent;
                    const auto line_height = resolved_line_height(node, font_size);
                    // Match the compositor's alphabetic baseline exactly:
                    // CSS distributes the difference between line-height and
                    // the shaped font box equally above and below the glyphs.
                    return node.layout.y
                        + (line_height - glyph_height) * 0.5F
                        + metrics.ascent;
                }
                for (const auto* descendant : composed_children(node)) {
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
            for (auto* child : ordered_children) {
                if (!participates_in_flow(child) || !child->visible || !uses_baseline(*child)) continue;
                const auto baseline = first_text_baseline(*child)
                    .value_or(child->layout.y + child->layout.height);
                shift_subtree_y(shift_subtree_y, *child, target_baseline - baseline);
            }
        }
    }
    if (inline_flow && !flex_container) {
        const auto has_atomic_inline = std::any_of(
            ordered_children.begin(),
            ordered_children.end(),
            [&](const dom_node* child) {
                return participates_in_flow(child) && child->visible
                    && child->style.display == display_mode::inline_block;
            });
        if (has_atomic_inline) {
            // Inline-level boxes share an alphabetic baseline by default.
            // Atomic inline boxes without an in-flow line box synthesize that
            // baseline at their bottom margin edge; this is the browser rule
            // used by compact block-backed separator dots. Limit this pass to
            // lines which actually contain an atomic inline so ordinary nested
            // inline spans retain their established fragment geometry.
            const auto inline_item_baseline = [&](const auto& self,
                                                  const dom_node& node)
                -> std::optional<float> {
                if (node.style.display == display_mode::inline_flow
                    && node.tag != "#text") {
                    const auto font_size = resolved_font_size(node);
                    const auto metrics = measure_text("Hg", node);
                    const auto glyph_height = metrics.ascent + metrics.descent;
                    const auto line_height = resolved_line_height(node, font_size);
                    return node.layout.y
                        + (line_height - glyph_height) * 0.5F
                        + metrics.ascent;
                }
                if (node.tag == "#text" && has_visible_text(node.text_content)) {
                    const auto font_size = resolved_font_size(node);
                    const auto metrics = measure_text(node.text_content, node);
                    const auto glyph_height = metrics.ascent + metrics.descent;
                    const auto line_height = resolved_line_height(node, font_size);
                    return node.layout.y
                        + (line_height - glyph_height) * 0.5F
                        + metrics.ascent;
                }
                for (auto iterator = composed_children(node).rbegin();
                     iterator != composed_children(node).rend();
                     ++iterator) {
                    if (const auto baseline = self(self, **iterator)) return baseline;
                }
                return std::nullopt;
            };
            struct inline_baseline_item final {
                dom_node* node{nullptr};
                float baseline{0};
            };
            struct inline_baseline_line final {
                float y{0};
                float target{-std::numeric_limits<float>::infinity()};
                std::vector<inline_baseline_item> items;
            };
            std::vector<inline_baseline_line> baseline_lines;
            for (auto* child : ordered_children) {
                const auto& vertical_align = child->style.textual().vertical_align;
                if (!participates_in_flow(child) || !child->visible
                    || child->tag == "br"
                    || vertical_align == "top"
                    || vertical_align == "middle"
                    || vertical_align == "bottom") continue;
                const auto baseline = inline_item_baseline(
                    inline_item_baseline, *child).value_or(
                        child->layout.y + child->layout.height);
                auto line = std::find_if(
                    baseline_lines.begin(),
                    baseline_lines.end(),
                    [&](const auto& candidate) {
                        return std::abs(candidate.y - child->layout.y) < 0.01F;
                    });
                if (line == baseline_lines.end()) {
                    baseline_lines.push_back({child->layout.y, baseline, {{child, baseline}}});
                } else {
                    line->target = std::max(line->target, baseline);
                    line->items.push_back({child, baseline});
                }
            }
            for (const auto& line : baseline_lines) {
                if (!std::isfinite(line.target)) continue;
                for (const auto& item : line.items) {
                    shift_subtree_y(
                        shift_subtree_y,
                        *item.node,
                        line.target - item.baseline);
                }
            }
        }
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
            shift_subtree_y(
                shift_subtree_y, *child, target_y - child->layout.y);
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
    // Only nodes owned by this document have stable identities for the whole
    // layout pass. Pseudo-elements and list markers are represented by
    // short-lived stack nodes that deliberately reuse their owner's native ID.
    const auto stable_node = node.id != 0U
        && node.id <= native_id_index_.size()
        && native_id_index_[node.id - 1U] == &node;
    if (intrinsic_size_cache_ == nullptr
        || intrinsic_size_cache_generation_ == 0
        || !stable_node) {
        return compute_intrinsic_size(node, horizontal, available);
    }
    const intrinsic_size_key key{&node, horizontal};
    if (const auto cached = intrinsic_size_cache_->find(key);
        cached != intrinsic_size_cache_->end()
        && cached->second.generation == intrinsic_size_cache_generation_
        && cached->second.available == available) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++intrinsic_size_cache_hits_;
#endif
        return cached->second.size;
    }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    ++intrinsic_size_cache_misses_;
#endif
    const auto result = compute_intrinsic_size(node, horizontal, available);
    intrinsic_size_cache_->insert_or_assign(
        key,
        intrinsic_size_cache_entry{
            intrinsic_size_cache_generation_,
            available,
            result});
    return result;
}

float native_document::compute_intrinsic_size(
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
    const auto authored = horizontal ? node.style.width : node.style.height;
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
        if (!horizontal && node.style.display == display_mode::table_cell
            && is_specified(authored)) {
            size = std::max(
                size,
                resolve_length(node, authored, available, 0)
                    + (node.style.border_box ? 0.0F : padding));
        }
        return std::max(0.0F, size);
    };
    // A percentage size is indefinite while shrink-to-fitting an auto-sized
    // ancestor. Resolving it against the viewport makes fixed popups with
    // descendants such as `width: 100%` or `height: 100%` measure as the whole
    // viewport. Use the descendant content contribution in that case; once the
    // ancestor has a definite box, layout_children resolves the percentage.
    if (!(node.style.display == display_mode::table_cell && !horizontal)
        && (authored.unit == length_unit::pixels
        || authored.unit == length_unit::em
        || authored.unit == length_unit::rem
        || authored.unit == length_unit::viewport_width
        || authored.unit == length_unit::viewport_height)) {
        return constrain(resolve_length(node, authored, available, 0)
            + (node.style.border_box ? 0.0F : padding));
    }
    if ((is_table_container(node.style.display)
            && has_table_row_descendant(*this, node))
        || is_table_row_group(node.style.display)
        || node.style.display == display_mode::table_row) {
        std::vector<float> authored_column_widths;
        if (is_table_container(node.style.display)) {
            const auto collect_column_widths = [&](const auto& self,
                                                   const dom_node& current) -> void {
                for (const auto* child : composed_children(current)) {
                    if (child->style.display == display_mode::table_column_group) {
                        self(self, *child);
                        continue;
                    }
                    if (child->style.display != display_mode::table_column) continue;
                    auto span_value = child->attributes.find("span");
                    const auto span = span_value == child->attributes.end()
                        ? size_t{1}
                        : std::max<size_t>(
                            1,
                            static_cast<size_t>(parse_number(span_value->second, 1)));
                    const auto width = is_specified(child->style.width)
                        ? resolve_length(*child, child->style.width, available, 0)
                        : 0.0F;
                    authored_column_widths.insert(
                        authored_column_widths.end(), span, width);
                }
            };
            collect_column_widths(collect_column_widths, node);
        }
        std::vector<const dom_node*> rows;
        std::function<void(const dom_node&)> collect_rows = [&](const dom_node& current) {
            for (const auto* child : composed_children(current)) {
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
                for (const auto* cell : composed_children(*row)) {
                    if (cell->style.display != display_mode::table_cell) continue;
                    auto span = cell->attributes.find("colspan");
                    if (span == cell->attributes.end()) span = cell->attributes.find("colSpan");
                    row_count += span == cell->attributes.end()
                        ? 1U : std::max<size_t>(1, static_cast<size_t>(parse_number(span->second, 1)));
                }
                count = std::max(count, row_count);
            }
            count = std::max(count, authored_column_widths.size());
            std::vector<float> columns(count, 0.0F);
            std::copy(
                authored_column_widths.begin(),
                authored_column_widths.end(),
                columns.begin());
            for (const auto* row : rows) {
                size_t column = 0;
                for (const auto* cell : composed_children(*row)) {
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
            auto table_spacing = 0.0F;
            if (is_table_container(node.style.display)
                && !columns.empty()
                && !node.style.table().border_collapsed) {
                table_spacing = std::max(
                    0.0F,
                    resolve_length(
                        node,
                        node.style.table().border_spacing_horizontal,
                        available,
                        2.0F))
                    * static_cast<float>(columns.size() + 1U);
            }
            return constrain(
                std::accumulate(columns.begin(), columns.end(), 0.0F)
                    + table_spacing + padding);
        }
        auto height = 0.0F;
        for (const auto* row : rows) {
            auto row_height = is_specified(row->style.height)
                ? resolve_length(*row, row->style.height, available, 0) : 0.0F;
            for (const auto* cell : composed_children(*row)) {
                if (cell->style.display == display_mode::table_cell) {
                    row_height = std::max(row_height, intrinsic_size(*cell, false, available));
                }
            }
            height += row_height;
        }
        if (is_table_container(node.style.display)
            && !rows.empty()
            && !node.style.table().border_collapsed) {
            height += std::max(
                0.0F,
                resolve_length(
                    node,
                    node.style.table().border_spacing_vertical,
                    available,
                    2.0F))
                * static_cast<float>(rows.size() + 1U);
        }
        return constrain(height + padding);
    }
    if (is_collapsed_select(node)) {
        const auto font_size = resolved_font_size(node);
        if (!horizontal) {
            return constrain(resolved_line_height(node, font_size) + padding);
        }
        auto widest_label = 0.0F;
        std::vector<const dom_node*> options;
        collect_option_nodes(node, options);
        for (const auto* option : options) {
            widest_label = std::max(
                widest_label,
                measure_text_width(option_label(*option), node));
        }
        // Reserve a compact native disclosure affordance in addition to the
        // widest option. This state exists only for an actual SELECT element.
        return constrain(widest_label + font_size * 1.75F + padding);
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
    if (composed_children(node).empty() && !has_before && !has_after && !has_inside_marker) {
        if (!has_visible_text(node.text_content)) {
            const auto measured = horizontal && is_collapsible_whitespace_text(node)
                ? measure_text_width(" ", node)
                : 0;
            // Empty auto-sized boxes still contribute their padding and border
            // to their intrinsic border-box size. This matters in particular
            // for non-stretch flex items, whose empty content box may be zero
            // while their visible border box is not.
            return constrain(padding + measured);
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

    const auto& node_grid = node.style.grid();
    const auto intrinsic_implicit_column_grid = node_grid.auto_flow_column
        && !node_grid.auto_columns.empty();
    if (is_grid_container(node.style.display)
        && (node_grid.two_columns || intrinsic_implicit_column_grid)) {
        const auto& grid = node.style.grid();
        auto effective_columns = grid.template_columns;
        if (effective_columns.empty() && intrinsic_implicit_column_grid) {
            const auto implicit_count = std::max<size_t>(
                1U,
                std::count_if(
                    composed_children(node).begin(),
                    composed_children(node).end(),
                    [](const dom_node* child) {
                        return child != nullptr
                            && child->style.display != display_mode::none
                            && !is_out_of_flow(child->style.position)
                            && !is_collapsible_whitespace_text(*child);
                    }));
            effective_columns.reserve(implicit_count);
            for (size_t index = 0; index < implicit_count; ++index) {
                effective_columns.push_back(
                    grid.auto_columns[index % grid.auto_columns.size()]);
            }
        }
        const auto column_count = std::max<size_t>(
            intrinsic_implicit_column_grid ? 1U : 2U,
            effective_columns.size());
        struct intrinsic_grid_item final {
            const dom_node* node{nullptr};
            size_t row{0};
            size_t column{0};
            size_t row_span{1};
            size_t column_span{1};
        };
        const auto parse_line = [](std::string_view value) -> std::optional<int32_t> {
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos) return std::nullopt;
            const auto last = value.find_last_not_of(" \t\r\n");
            value = value.substr(first, last - first + 1U);
            if (value.empty() || value == "auto" || value.starts_with("span")) {
                return std::nullopt;
            }
            int32_t result = 0;
            const auto parsed = std::from_chars(
                value.data(), value.data() + value.size(), result);
            return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size()
                ? std::optional<int32_t>{result}
                : std::nullopt;
        };
        const auto span_for = [&](std::string_view start_value,
                                  std::string_view end_value,
                                  size_t count) {
            const auto start = parse_line(start_value);
            const auto end = parse_line(end_value);
            if (start.has_value() && end.has_value()) {
                const auto normalized_end = *end < 0
                    ? static_cast<int32_t>(count) + *end + 2 : *end;
                return static_cast<size_t>(std::max(1, normalized_end - *start));
            }
            const auto span_value = start_value.find("span") != std::string_view::npos
                ? start_value : end_value;
            const auto marker = span_value.find("span");
            if (marker != std::string_view::npos) {
                const auto digits = span_value.find_first_of("0123456789", marker + 4U);
                if (digits != std::string_view::npos) {
                    return std::max<size_t>(
                        1U,
                        static_cast<size_t>(parse_number(
                            std::string(span_value.substr(digits)), 1.0F)));
                }
            }
            return size_t{1};
        };
        std::vector<std::vector<bool>> occupied;
        std::vector<intrinsic_grid_item> items;
        auto auto_row = size_t{0};
        auto auto_column = size_t{0};
        for (const auto* child : composed_children(node)) {
            if (child->style.display == display_mode::none
                || is_out_of_flow(child->style.position)
                || is_collapsible_whitespace_text(*child)) continue;
            const auto& placement = child->style.grid();
            const auto column_span = std::min(
                column_count,
                span_for(
                    placement.column_start_value,
                    placement.column_end_value,
                    column_count));
            const auto row_span = span_for(
                placement.row_start_value,
                placement.row_end_value,
                std::max<size_t>(grid.template_rows.size(), 1U));
            const auto authored_column = parse_line(placement.column_start_value);
            const auto authored_row = parse_line(placement.row_start_value);
            auto row = authored_row.has_value() && *authored_row > 0
                ? static_cast<size_t>(*authored_row - 1) : auto_row;
            auto column = authored_column.has_value() && *authored_column > 0
                ? static_cast<size_t>(*authored_column - 1) : auto_column;
            if (column + column_span > column_count) column = column_count - column_span;
            const auto fits = [&](size_t candidate_row, size_t candidate_column) {
                if (candidate_column + column_span > column_count) return false;
                for (size_t row_index = candidate_row;
                     row_index < candidate_row + row_span;
                     ++row_index) {
                    if (row_index >= occupied.size()) continue;
                    for (size_t column_index = candidate_column;
                         column_index < candidate_column + column_span;
                         ++column_index) {
                        if (occupied[row_index][column_index]) return false;
                    }
                }
                return true;
            };
            while (!fits(row, column)) {
                if (!authored_column.has_value()) {
                    ++column;
                    if (column + column_span > column_count) {
                        column = 0;
                        ++row;
                    }
                } else {
                    ++row;
                }
            }
            while (occupied.size() < row + row_span) {
                occupied.emplace_back(column_count, false);
            }
            for (size_t row_index = row; row_index < row + row_span; ++row_index) {
                for (size_t column_index = column;
                     column_index < column + column_span;
                     ++column_index) occupied[row_index][column_index] = true;
            }
            items.push_back({child, row, column, row_span, column_span});
            auto_row = row;
            auto_column = column + column_span;
            if (auto_column >= column_count) {
                auto_column = 0;
                ++auto_row;
            }
        }
        std::vector<float> columns(column_count, 0.0F);
        std::vector<float> rows(std::max<size_t>(occupied.size(), grid.template_rows.size()), 0.0F);
        for (const auto& item : items) {
            const auto child_width = intrinsic_size(*item.node, true, available)
                + resolve_length(*item.node, item.node->style.margin_left, available, 0)
                + resolve_length(*item.node, item.node->style.margin_right, available, 0);
            const auto child_height = intrinsic_size(*item.node, false, available)
                + resolve_length(*item.node, item.node->style.margin_top, available, 0)
                + resolve_length(*item.node, item.node->style.margin_bottom, available, 0);
            const auto width_share = child_width / static_cast<float>(item.column_span);
            for (size_t column = item.column;
                 column < item.column + item.column_span;
                 ++column) columns[column] = std::max(columns[column], width_share);
            const auto height_share = child_height / static_cast<float>(item.row_span);
            for (size_t row = item.row; row < item.row + item.row_span; ++row) {
                rows[row] = std::max(rows[row], height_share);
            }
        }
        const auto column_gap = resolve_length(node, node.style.column_gap, available, 0);
        const auto row_gap = resolve_length(node, node.style.row_gap, available, 0);
        const auto total_width = std::accumulate(columns.begin(), columns.end(), 0.0F)
            + column_gap * static_cast<float>(columns.size() - 1U);
        const auto total_height = std::accumulate(rows.begin(), rows.end(), 0.0F)
            + (rows.empty() ? 0.0F : row_gap * static_cast<float>(rows.size() - 1U));
        return constrain(padding + (horizontal
            ? total_width
            : total_height));
    }

    std::optional<dom_node> marker_item;
    std::optional<dom_node> before_item;
    std::optional<dom_node> after_item;
    std::vector<const dom_node*> intrinsic_items;
    intrinsic_items.reserve(composed_children(node).size() + 3U);
    if (has_inside_marker) {
        marker_item.emplace(make_list_marker_layout_node(node, marker_text));
        intrinsic_items.push_back(&*marker_item);
    }
    if (has_before) {
        before_item.emplace(make_pseudo_layout_node(node, node.style.before_pseudo()));
        intrinsic_items.push_back(&*before_item);
    }
    intrinsic_items.insert(intrinsic_items.end(), composed_children(node).begin(), composed_children(node).end());
    if (has_after) {
        after_item.emplace(make_pseudo_layout_node(node, node.style.after_pseudo()));
        intrinsic_items.push_back(&*after_item);
    }

    const auto flex_container = is_flex_container(node.style.display);
    const auto inline_item_contains_block_box = [&](const dom_node* child) {
        if (child == nullptr || child->style.display != display_mode::inline_flow) return false;
        return std::any_of(
            composed_children(*child).begin(),
            composed_children(*child).end(),
            [](const dom_node* descendant) {
                return descendant->style.display != display_mode::none
                    && !is_out_of_flow(descendant->style.position)
                    && !is_inline_level(descendant->style.display);
            });
    };
    const auto inline_flow = !flex_container && std::all_of(
        intrinsic_items.begin(),
        intrinsic_items.end(),
        [&](const dom_node* child) {
            return child->style.display == display_mode::none
                || is_out_of_flow(child->style.position)
                || (is_inline_level(child->style.display)
                    && !inline_item_contains_block_box(child));
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
    const auto* parent = composed_parent(child);
    const auto parent_constrains_height = parent != nullptr
        && (is_specified(parent->style.height)
            || is_specified(parent->style.max_height));
    const auto constrained_overflow_height = child.style.overflow_y != overflow_mode::visible
        && (is_specified(child.style.max_height)
            || (parent != nullptr
                && is_flex_container(parent->style.display)
                && parent_constrains_height));
    const auto constrained_column_flex_item = parent != nullptr
        && is_flex_container(parent->style.display)
        && parent->style.direction == flex_direction::column
        && parent_constrains_height;
    const auto table_track_sized = is_table_row_group(child.style.display)
        || child.style.display == display_mode::table_row
        || child.style.display == display_mode::table_cell;
    if (automatic_height && !constrained_overflow_height && !constrained_column_flex_item
        && !table_track_sized && !composed_children(child).empty()) {
        auto content_bottom = child.layout.y;
        for (const auto* descendant : composed_children(child)) {
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
        const auto previous_height = child.layout.height;
        child.layout.height = std::max(
            child.layout.height,
            content_bottom - child.layout.y + padding_bottom + border_bottom);
        if (child.layout.height > previous_height + 0.01F) {
            // layout_children() established scroll geometry from the
            // provisional auto height. Wrapped content can then grow this box
            // without changing its public client/scroll metrics, leaving a
            // stale smaller viewport that paints a phantom overflow:auto rail.
            // Refresh the final content-box viewport and range immediately.
            const auto final_border_top = resolve_length(
                child,
                child.style.border_top_width,
                child.layout.height,
                0);
            const auto final_border_bottom = resolve_length(
                child,
                child.style.border_bottom_width,
                child.layout.height,
                0);
            const auto final_padding_top = resolve_length(
                child,
                child.style.padding_top,
                child.layout.height,
                0);
            const auto final_padding_bottom = resolve_length(
                child,
                child.style.padding_bottom,
                child.layout.height,
                0);
            child.scroll_viewport_height = std::max(
                0.0F,
                child.layout.height
                    - final_border_top - final_border_bottom
                    - final_padding_top - final_padding_bottom);
            child.scroll_content_height = std::max(
                child.scroll_content_height,
                child.scroll_viewport_height);
            child.scroll_top = std::clamp(
                child.scroll_top,
                0.0F,
                std::max(
                    0.0F,
                    child.scroll_content_height - child.scroll_viewport_height));
        }

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
                    for (const auto* descendant : composed_children(node)) {
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
    // The intrinsic contribution of auto-height contents is evaluated before
    // max-height, but the final used principal box must remain capped even
    // when overflow is visible.
    if (is_specified(child.style.max_height)) {
        const auto available_height = parent != nullptr
            ? parent->layout.height : viewport_height_;
        const auto capped_height = std::min(
            child.layout.height,
            resolve_length(
                child,
                child.style.max_height,
                available_height,
                child.layout.height));
        if (capped_height + 0.01F < child.layout.height) {
            child.layout.height = capped_height;
            // Descendant percentages and stretch alignment were resolved
            // against the pre-clamp intrinsic height. Reflow once against the
            // final used max-height so the principal box and its grid/flex
            // contents agree in the same layout pass.
            layout_children(child);
        } else {
            child.layout.height = capped_height;
        }
    }
}
