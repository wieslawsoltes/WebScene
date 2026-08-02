void native_document::layout(float viewport_width, float viewport_height)
{
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
    update_retained_canvas_paint_phase(*this, *body_, retained_canvas_seen);
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
                for (const auto& row : rows) {
                    if (composed_parent(*row.row) == child) {
                        height += row.row->table_layout().row_height;
                    }
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
        for (auto* child : composed_children(parent)) {
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
                offsets[end] - offsets[child_table.column_index],
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
            composed_children(parent).begin(),
            composed_children(parent).end(),
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
            // Inline-block and inline-table descendants are atomic inline
            // boxes. Flattening their text into the surrounding line discards
            // their own padding, background, and border geometry.
            if (node.style.display != display_mode::inline_flow) return false;
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
            for (auto* child : composed_children(node)) {
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
        for (auto* child : composed_children(parent)) {
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
    const auto flex_container = is_flex_container(parent.style.display);
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
        [](const dom_node* child) {
            return child->style.display == display_mode::none
                || is_out_of_flow(child->style.position)
                || is_inline_level(child->style.display);
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
            const auto shift_subtree_y = [&](const auto& self, dom_node& node, float offset) -> void {
                node.layout.y += offset;
                for (auto& fragment : node.text_layout_fragments) fragment.y += offset;
                for (auto* descendant : composed_children(node)) self(self, *descendant, offset);
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
            for (auto* descendant : composed_children(node)) self(self, *descendant, offset);
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
    if ((is_table_container(node.style.display)
            && has_table_row_descendant(*this, node))
        || is_table_row_group(node.style.display)
        || node.style.display == display_mode::table_row) {
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
            std::vector<float> columns(count, 0.0F);
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
            return constrain(
                std::accumulate(columns.begin(), columns.end(), 0.0F) + padding);
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
    if (composed_children(node).empty() && !has_before && !has_after && !has_inside_marker) {
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
        for (const auto* child : composed_children(node)) {
            if (child->style.display == display_mode::none
                || is_out_of_flow(child->style.position)
                || is_collapsible_whitespace_text(*child)) continue;
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
}
