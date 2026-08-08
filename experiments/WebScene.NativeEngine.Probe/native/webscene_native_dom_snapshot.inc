namespace {

webscene_dom_string_ref append_dom_snapshot_string(
    std::string_view value,
    std::vector<char>& bytes)
{
    const auto offset = static_cast<uint32_t>(bytes.size());
    bytes.insert(bytes.end(), value.begin(), value.end());
    return {offset, static_cast<uint32_t>(value.size())};
}

std::string dom_snapshot_number(float value)
{
    if (!std::isfinite(value)) value = 0;
    if (std::abs(value) < 0.00001F) value = 0;
    std::ostringstream stream;
    stream << std::setprecision(7) << value;
    return stream.str();
}

std::string dom_snapshot_pixels(float value)
{
    return dom_snapshot_number(value) + "px";
}

std::string dom_snapshot_color(uint32_t color)
{
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
}

std::string dom_snapshot_display(display_mode value)
{
    switch (value) {
    case display_mode::block: return "block";
    case display_mode::flex: return "flex";
    case display_mode::inline_flow: return "inline";
    case display_mode::inline_block: return "inline-block";
    case display_mode::inline_flex: return "inline-flex";
    case display_mode::grid: return "grid";
    case display_mode::inline_grid: return "inline-grid";
    case display_mode::table: return "table";
    case display_mode::inline_table: return "inline-table";
    case display_mode::table_row_group: return "table-row-group";
    case display_mode::table_header_group: return "table-header-group";
    case display_mode::table_footer_group: return "table-footer-group";
    case display_mode::table_row: return "table-row";
    case display_mode::table_cell: return "table-cell";
    case display_mode::table_column_group: return "table-column-group";
    case display_mode::table_column: return "table-column";
    case display_mode::table_caption: return "table-caption";
    case display_mode::list_item: return "list-item";
    case display_mode::contents: return "contents";
    case display_mode::none: return "none";
    }
    return "block";
}

std::string dom_snapshot_overflow(overflow_mode value)
{
    switch (value) {
    case overflow_mode::hidden: return "hidden";
    case overflow_mode::clip: return "clip";
    case overflow_mode::scroll: return "scroll";
    case overflow_mode::automatic: return "auto";
    default: return "visible";
    }
}

} // namespace

void native_document::build_dom_snapshot(
    std::vector<webscene_dom_node_snapshot>& nodes,
    std::vector<webscene_dom_attribute_snapshot>& attributes,
    std::vector<webscene_dom_property_snapshot>& properties,
    std::vector<char>& string_bytes) const
{
    nodes.clear();
    attributes.clear();
    properties.clear();
    string_bytes.clear();
    nodes.reserve(nodes_.size());

    const auto add_property = [&](std::string_view name, std::string value) {
        properties.push_back(webscene_dom_property_snapshot{
            append_dom_snapshot_string(name, string_bytes),
            append_dom_snapshot_string(value, string_bytes)});
    };

    const auto collect = [&](
        const auto& self,
        const dom_node& node,
        uint32_t snapshot_parent_id) -> void {
        const auto attribute_offset = static_cast<uint32_t>(attributes.size());
        for (const auto& [name, value] : node.attributes) {
            attributes.push_back(webscene_dom_attribute_snapshot{
                append_dom_snapshot_string(name, string_bytes),
                append_dom_snapshot_string(value, string_bytes)});
        }

        const auto property_offset = static_cast<uint32_t>(properties.size());
        webscene_dom_box_snapshot box{};
        box.x = node.layout.x;
        box.y = node.layout.y;
        box.width = node.layout.width;
        box.height = node.layout.height;
        if (node.kind == dom_node_kind::element) {
            const auto horizontal_basis = node.parent == nullptr
                ? node.layout.width
                : node.parent->layout.width;
            const auto vertical_basis = node.parent == nullptr
                ? node.layout.height
                : node.parent->layout.height;
            const auto resolve_x = [&](css_length value) {
                return std::max(0.0F, resolve_length(node, value, horizontal_basis, 0));
            };
            const auto resolve_y = [&](css_length value) {
                return std::max(0.0F, resolve_length(node, value, vertical_basis, 0));
            };
            box.margin_left = resolve_x(node.style.margin_left);
            box.margin_top = resolve_y(node.style.margin_top);
            box.margin_right = resolve_x(node.style.margin_right);
            box.margin_bottom = resolve_y(node.style.margin_bottom);
            box.border_left = resolve_x(node.style.border_left_width);
            box.border_top = resolve_y(node.style.border_top_width);
            box.border_right = resolve_x(node.style.border_right_width);
            box.border_bottom = resolve_y(node.style.border_bottom_width);
            box.padding_left = resolve_x(node.style.padding_left);
            box.padding_top = resolve_y(node.style.padding_top);
            box.padding_right = resolve_x(node.style.padding_right);
            box.padding_bottom = resolve_y(node.style.padding_bottom);

            add_property("display", dom_snapshot_display(node.style.display));
            add_property("position",
                node.style.position == position_mode::absolute ? "absolute"
                : node.style.position == position_mode::fixed ? "fixed"
                : node.style.position == position_mode::relative ? "relative"
                : "static");
            add_property("box-sizing", node.style.border_box ? "border-box" : "content-box");
            add_property("width", dom_snapshot_pixels(node.style.border_box
                ? node.layout.width
                : std::max(0.0F,
                    node.layout.width - box.border_left - box.border_right
                        - box.padding_left - box.padding_right)));
            add_property("height", dom_snapshot_pixels(node.style.border_box
                ? node.layout.height
                : std::max(0.0F,
                    node.layout.height - box.border_top - box.border_bottom
                        - box.padding_top - box.padding_bottom)));
            add_property("margin-left", dom_snapshot_pixels(box.margin_left));
            add_property("margin-top", dom_snapshot_pixels(box.margin_top));
            add_property("margin-right", dom_snapshot_pixels(box.margin_right));
            add_property("margin-bottom", dom_snapshot_pixels(box.margin_bottom));
            add_property("border-left-width", dom_snapshot_pixels(box.border_left));
            add_property("border-top-width", dom_snapshot_pixels(box.border_top));
            add_property("border-right-width", dom_snapshot_pixels(box.border_right));
            add_property("border-bottom-width", dom_snapshot_pixels(box.border_bottom));
            add_property("padding-left", dom_snapshot_pixels(box.padding_left));
            add_property("padding-top", dom_snapshot_pixels(box.padding_top));
            add_property("padding-right", dom_snapshot_pixels(box.padding_right));
            add_property("padding-bottom", dom_snapshot_pixels(box.padding_bottom));
            add_property("background-color", dom_snapshot_color(node.style.background_rgba));
            add_property("color", dom_snapshot_color(node.style.foreground_rgba));
            add_property("font-family", resolved_font_family(node));
            add_property("font-size", dom_snapshot_pixels(resolved_font_size(node)));
            add_property("font-weight", std::to_string(resolved_font_weight(node)));
            add_property("line-height", dom_snapshot_pixels(
                resolved_line_height(node, resolved_font_size(node))));
            add_property("opacity", dom_snapshot_number(node.painted_opacity_value()));
            add_property("overflow-x", dom_snapshot_overflow(node.style.overflow_x));
            add_property("overflow-y", dom_snapshot_overflow(node.style.overflow_y));
            add_property("visibility", node.style.visibility_hidden ? "hidden" : "visible");
            add_property("pointer-events", node.style.pointer_events_none ? "none" : "auto");
            add_property("z-index", node.style.z_index_auto
                ? "auto" : std::to_string(node.style.z_index));
        }

        uint32_t node_type = 1U;
        switch (node.kind) {
        case dom_node_kind::text: node_type = 3U; break;
        case dom_node_kind::comment: node_type = 8U; break;
        case dom_node_kind::document_fragment: node_type = 11U; break;
        case dom_node_kind::document_type: node_type = 10U; break;
        case dom_node_kind::processing_instruction: node_type = 7U; break;
        case dom_node_kind::internal: node_type = 0U; break;
        case dom_node_kind::element: break;
        }
        const auto has_element_text = node.kind == dom_node_kind::element
            && !node.text_content.empty();
        nodes.push_back(webscene_dom_node_snapshot{
            node.id,
            snapshot_parent_id,
            node_type,
            node.visible ? 1U : 0U,
            static_cast<uint32_t>(node.children.size())
                + (has_element_text ? 1U : 0U)
                + (node.template_contents != nullptr ? 1U : 0U),
            attribute_offset,
            static_cast<uint32_t>(attributes.size()) - attribute_offset,
            property_offset,
            static_cast<uint32_t>(properties.size()) - property_offset,
            append_dom_snapshot_string(node.tag, string_bytes),
            append_dom_snapshot_string(
                node.kind == dom_node_kind::text || node.kind == dom_node_kind::comment
                    ? std::string_view(node.text_content)
                    : std::string_view{},
                string_bytes),
            append_dom_snapshot_string(node.namespace_uri(), string_bytes),
            box});
        if (has_element_text) {
            // The renderer stores simple element text inline on the element.
            // Expose browser-compatible #text children without allocating live
            // DOM objects; a reserved bit keeps their ids deterministic while
            // remaining in CDP's positive signed integer range.
            nodes.push_back(webscene_dom_node_snapshot{
                node.id | 0x40000000U,
                node.id,
                3U,
                node.visible ? 1U : 0U,
                0U,
                static_cast<uint32_t>(attributes.size()),
                0U,
                static_cast<uint32_t>(properties.size()),
                0U,
                append_dom_snapshot_string("#text", string_bytes),
                append_dom_snapshot_string(node.text_content, string_bytes),
                append_dom_snapshot_string({}, string_bytes),
                {}});
        }
        for (const auto* child : node.children) {
            if (child != nullptr) self(self, *child, node.id);
        }
        if (node.template_contents != nullptr) {
            self(self, *node.template_contents, node.id);
        }
    };
    collect(collect, *body_, 0U);
}
