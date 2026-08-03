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
    if (shadow_dom_storage_ != nullptr) {
        result.shadow_dom_role_count = shadow_dom_storage_->node_data.size();
        result.shadow_dom_storage_bytes = sizeof(shadow_dom_storage)
            + shadow_dom_storage_->node_data.bucket_count() * sizeof(void*)
            + shadow_dom_storage_->node_data.size()
                * (sizeof(decltype(shadow_dom_storage_->node_data)::value_type)
                    + 2U * sizeof(void*));
        for (const auto& [_, state] : shadow_dom_storage_->node_data) {
            result.shadow_dom_storage_bytes +=
                state.assigned_nodes.capacity() * sizeof(dom_node*);
        }
    }
    for (const auto& [key, _] : text_measurement_cache_) {
        result.text_measurement_cache_storage_bytes +=
            key.text.capacity() + key.family.capacity() + 2U;
    }
    std::unordered_set<const node_style::animation_data*> animation_allocations;
    std::unordered_set<const node_style::background_image_data*> background_allocations;
    std::unordered_set<const node_style::grid_data*> grid_allocations;
    std::unordered_set<const node_style::textual_style_data*> textual_allocations;
    for (const auto& node : nodes_) {
        if (node != nullptr) {
            switch (node->kind) {
            case dom_node_kind::element: ++result.element_node_count; break;
            case dom_node_kind::text: ++result.text_node_count; break;
            case dom_node_kind::comment: ++result.comment_node_count; break;
            case dom_node_kind::document_type: ++result.document_type_node_count; break;
            default: ++result.other_node_count; break;
            }
        }
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
            + canvas.commands.capacity() * sizeof(webscene_canvas_command)
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
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
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
            + grid->template_columns.capacity() * sizeof(node_style::grid_data::track)
            + grid->template_rows.capacity() * sizeof(node_style::grid_data::track)
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
#if !defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
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

bool native_document::participates_in_animation_frame(
    const dom_node& node) const noexcept
{
    if (!is_connected(node)) return false;
    for (auto* current = &node;
         current != nullptr;
         current = current->parent) {
        if (current->style.display == display_mode::none) {
            return false;
        }
    }
    return true;
}

bool native_document::dirty() const noexcept
{
    return dirty_;
}

uint64_t native_document::scene_generation() const noexcept
{
    return scene_generation_;
}

void native_document::mark_scene_changed() noexcept
{
    ++scene_generation_;
}

void native_document::mark_dirty() noexcept
{
    mark_scene_changed();
    dirty_ = true;
    globally_dirty_ = true;
    active_animation_demand_cache_valid_ = false;
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
    mark_scene_changed();
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
