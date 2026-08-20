native_document::native_document(
    webscene_text_measure_callback text_measure_callback,
    void* text_measure_user_data)
    : text_measure_callback_(text_measure_callback)
    , text_measure_user_data_(text_measure_user_data)
{
    clear();
}

webscene_text_metrics native_document::measure_text(
    std::string_view value,
    const dom_node& node) const
{
    const auto transformed = resolved_text_transform(node, std::string(value));
    return measure_text(
        transformed,
        resolved_font_family(node),
        resolved_font_size(node),
        resolved_font_weight(node),
        resolved_letter_spacing(node),
        resolved_word_spacing(node));
}

webscene_text_metrics native_document::measure_text(
    std::string_view value,
    std::string_view family,
    float font_size,
    int32_t font_weight,
    float letter_spacing,
    float word_spacing) const
{
    text_measurement_key key{
        std::string(value),
        std::string(family),
        font_size,
        font_weight,
        letter_spacing,
        word_spacing};
    if (const auto cached = text_measurement_cache_.find(key);
        cached != text_measurement_cache_.end()) {
        return cached->second;
    }
    if (text_measurement_cache_.size() >= 16'384U) {
        text_measurement_cache_.clear();
    }
    webscene_text_metrics measured{
        sizeof(webscene_text_metrics),
        fallback_text_width(value, font_size),
        font_size * 0.9F,
        font_size * 0.1F,
        0.0F,
        0.0F,
        fallback_text_width(value, font_size),
        font_size * 0.9F,
        font_size * 0.1F};
    if (text_measure_callback_ != nullptr) {
        webscene_text_metrics metrics{};
        metrics.struct_size = sizeof(metrics);
        if (text_measure_callback_(
                text_measure_user_data_,
                value.data(),
                value.size(),
                family.data(),
                family.size(),
                font_size,
                font_weight,
                letter_spacing,
                word_spacing,
                &metrics) != 0U
            && std::isfinite(metrics.advance_width)
            && metrics.advance_width >= 0
            && std::isfinite(metrics.ascent)
            && metrics.ascent >= 0
            && std::isfinite(metrics.descent)
            && metrics.descent >= 0
            && std::isfinite(metrics.leading)) {
            measured = metrics;
        }
    }
    text_measurement_cache_.emplace(std::move(key), measured);
    return measured;
}

float native_document::measure_inline_content_width(const dom_node& node) const
{
    auto width = node.text_content.empty()
        ? 0.0F
        : measure_text_width(node.text_content, node);
    for (const auto* child : composed_children(node)) {
        if (child == nullptr || !child->visible
            || child->style.display == display_mode::none) {
            continue;
        }
        if (child->tag == "#text"
            || child->style.display == display_mode::inline_flow) {
            width += measure_inline_content_width(*child);
        } else {
            width += child->layout.width;
        }
    }
    return width;
}

size_t native_document::text_caret_offset_at_x(
    const dom_node& node,
    float x) const
{
    const auto& text = node.text_content;
    if (text.empty()) return 0;

    auto text_x = node.layout.x;
    if (!node.text_layout_fragments.empty()) {
        text_x = node.text_layout_fragments.front().x;
    }
    const auto relative_x = std::max(0.0F, x - text_x);

    size_t byte_offset = 0;
    size_t utf16_offset = 0;
    auto previous_width = 0.0F;
    while (byte_offset < text.size()) {
        const auto first = static_cast<uint8_t>(text[byte_offset]);
        size_t byte_length = 1U;
        size_t utf16_length = 1U;
        if ((first & 0xe0U) == 0xc0U) {
            byte_length = 2U;
        } else if ((first & 0xf0U) == 0xe0U) {
            byte_length = 3U;
        } else if ((first & 0xf8U) == 0xf0U) {
            byte_length = 4U;
            utf16_length = 2U;
        }
        byte_length = std::min(byte_length, text.size() - byte_offset);
        const auto next_byte_offset = byte_offset + byte_length;
        const auto next_width = measure_text_width(
            std::string_view(text).substr(0, next_byte_offset),
            node);
        if (relative_x < (previous_width + next_width) * 0.5F) {
            return utf16_offset;
        }
        byte_offset = next_byte_offset;
        utf16_offset += utf16_length;
        previous_width = next_width;
    }
    return utf16_offset;
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

bool native_document::has_shadow_dom() const noexcept
{
    return shadow_dom_storage_ != nullptr;
}

const native_document::shadow_dom_data* native_document::shadow_dom(
    const dom_node& node) const noexcept
{
    if (shadow_dom_storage_ == nullptr) return nullptr;
    const auto found = shadow_dom_storage_->node_data.find(node.id);
    return found == shadow_dom_storage_->node_data.end() ? nullptr : &found->second;
}

native_document::shadow_dom_data& native_document::mutable_shadow_dom(dom_node& node)
{
    if (shadow_dom_storage_ == nullptr) {
        shadow_dom_storage_ = std::make_unique<shadow_dom_storage>();
    }
    return shadow_dom_storage_->node_data[node.id];
}

bool native_document::is_shadow_root(const dom_node& node) const noexcept
{
    const auto* state = shadow_dom(node);
    return node.kind == dom_node_kind::document_fragment
        && state != nullptr
        && state->host != nullptr;
}

dom_node* native_document::containing_shadow_root(dom_node& node) const noexcept
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        if (is_shadow_root(*current)) return current;
    }
    return nullptr;
}

const dom_node* native_document::containing_shadow_root(
    const dom_node& node) const noexcept
{
    return containing_shadow_root(const_cast<dom_node&>(node));
}

const std::vector<dom_node*>& native_document::composed_children(
    const dom_node& node) const noexcept
{
    if (shadow_dom_storage_ == nullptr) return node.children;
    const auto* state = shadow_dom(node);
    if (state != nullptr && state->root != nullptr) return state->root->children;
    if (node.tag == "slot"
        && containing_shadow_root(node) != nullptr
        && state != nullptr
        && !state->assigned_nodes.empty()) {
        return state->assigned_nodes;
    }
    return node.children;
}

std::vector<dom_node*>& native_document::composed_children(dom_node& node) noexcept
{
    return const_cast<std::vector<dom_node*>&>(
        static_cast<const native_document&>(*this).composed_children(node));
}

void native_document::refresh_shadow_distributions()
{
    if (shadow_dom_storage_ == nullptr) return;

    // Collect first because assigning slot roles can grow and rehash the cold
    // side table. Hosts are ordinary stable node-pool pointers.
    std::vector<dom_node*> hosts;
    hosts.reserve(shadow_dom_storage_->node_data.size());
    for (auto& [_, state] : shadow_dom_storage_->node_data) {
        if (state.root != nullptr) hosts.push_back(state.root->parent);
        state.assigned_slot = nullptr;
        state.assigned_nodes.clear();
    }
    for (auto* host : hosts) {
        if (host == nullptr) continue;
        const auto* host_state = shadow_dom(*host);
        auto* root = host_state == nullptr ? nullptr : host_state->root;
        if (root == nullptr) continue;

        std::vector<dom_node*> slots;
        const auto collect_slots = [&](const auto& self, dom_node& node) -> void {
            if (node.kind == dom_node_kind::element && node.tag == "slot") {
                slots.push_back(&node);
            }
            for (auto* child : node.children) {
                if (child != nullptr) self(self, *child);
            }
        };
        collect_slots(collect_slots, *root);
        for (auto* child : host->children) {
            if (child == nullptr) continue;
            if (child->kind != dom_node_kind::element
                && child->kind != dom_node_kind::text) {
                continue;
            }
            std::string_view slot_name;
            if (child->kind == dom_node_kind::element) {
                const auto authored = child->attributes.find("slot");
                if (authored != child->attributes.end()) slot_name = authored->second;
            }
            const auto match = std::find_if(
                slots.begin(), slots.end(), [&](const auto* slot) {
                    const auto name = slot->attributes.find("name");
                    const auto candidate = name == slot->attributes.end()
                        ? std::string_view{}
                        : std::string_view(name->second);
                    return candidate == slot_name;
                });
            if (match == slots.end()) continue;
            mutable_shadow_dom(**match).assigned_nodes.push_back(child);
            mutable_shadow_dom(*child).assigned_slot = *match;
        }
    }
}

dom_node* native_document::composed_parent(dom_node& node) const noexcept
{
    if (shadow_dom_storage_ == nullptr) return node.parent;
    const auto* state = shadow_dom(node);
    if (state != nullptr && state->assigned_slot != nullptr) {
        return state->assigned_slot;
    }
    // A direct light child without a matching slot has no flat-tree parent.
    // Its logical DOM parent remains the host and is still used by DOM queries
    // and the ordinary event path.
    if (node.parent != nullptr) {
        const auto* parent_state = shadow_dom(*node.parent);
        if (parent_state != nullptr && parent_state->root != nullptr) return nullptr;
    }
    if (node.parent != nullptr && is_shadow_root(*node.parent)) {
        return shadow_dom(*node.parent)->host;
    }
    if (is_shadow_root(node)) return state->host;
    return node.parent;
}

const dom_node* native_document::composed_parent(const dom_node& node) const noexcept
{
    return composed_parent(const_cast<dom_node&>(node));
}

dom_node* native_document::event_parent(dom_node& node) const noexcept
{
    if (shadow_dom_storage_ == nullptr) return node.parent;
    const auto* state = shadow_dom(node);
    if (state != nullptr && state->assigned_slot != nullptr) {
        return state->assigned_slot;
    }
    if (is_shadow_root(node)) return state->host;
    return node.parent;
}

const dom_node* native_document::event_parent(const dom_node& node) const noexcept
{
    return event_parent(const_cast<dom_node&>(node));
}

dom_node* native_document::dom_parent(dom_node& node) const noexcept
{
    return is_shadow_root(node) ? nullptr : node.parent;
}

const dom_node* native_document::dom_parent(const dom_node& node) const noexcept
{
    return dom_parent(const_cast<dom_node&>(node));
}

dom_node& native_document::create_element(std::string tag)
{
    auto kind = dom_node_kind::element;
    if (tag == "#text") kind = dom_node_kind::text;
    else if (tag == "#comment") kind = dom_node_kind::comment;
    else if (tag == "#document-fragment" || tag == "#fragment") {
        kind = dom_node_kind::document_fragment;
    } else if (tag == "#doctype") kind = dom_node_kind::document_type;
    else if (tag == "#processing-instruction") {
        kind = dom_node_kind::processing_instruction;
    } else if (!tag.empty() && tag.front() == '#') kind = dom_node_kind::internal;
    return create_node(kind, std::move(tag));
}

dom_node& native_document::create_node(dom_node_kind kind, std::string name)
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
    node->kind = kind;
    node->tag = std::move(name);
    if (kind == dom_node_kind::text) {
        node->style.display = display_mode::inline_flow;
    } else if (kind != dom_node_kind::element) {
        node->style.display = display_mode::none;
        node->visible = false;
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
    if (native_id_index_.size() < result->id) {
        native_id_index_.resize(result->id, nullptr);
    }
    native_id_index_[result->id - 1U] = result;
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

bool native_document::parser_remove_from_parent(dom_node& child) noexcept
{
    if (child.parent == nullptr) return true;
    auto& siblings = child.parent->children;
    const auto known = std::find(siblings.begin(), siblings.end(), &child);
    if (known == siblings.end()) return false;
    siblings.erase(known);
    child.parent = nullptr;
    return true;
}

bool native_document::parser_append_child(dom_node& parent, dom_node& child) noexcept
{
    if (&parent == &child || !parser_remove_from_parent(child)) return false;
    child.parent = &parent;
    child.visible = child.kind == dom_node_kind::element
        || child.kind == dom_node_kind::text;
    parent.children.push_back(&child);
    return true;
}

bool native_document::parser_insert_before(dom_node& sibling, dom_node& child) noexcept
{
    auto* parent = sibling.parent;
    if (parent == nullptr || &sibling == &child) return false;
    if (!parser_remove_from_parent(child)) return false;
    const auto position = std::find(parent->children.begin(), parent->children.end(), &sibling);
    if (position == parent->children.end()) return false;
    child.parent = parent;
    child.visible = child.kind == dom_node_kind::element
        || child.kind == dom_node_kind::text;
    parent->children.insert(position, &child);
    return true;
}

bool native_document::parser_reparent_children(
    dom_node& source,
    dom_node& destination) noexcept
{
    if (&source == &destination) return false;
    for (auto* child : source.children) {
        if (child == nullptr) continue;
        child->parent = &destination;
        destination.children.push_back(child);
    }
    source.children.clear();
    return true;
}

dom_node& native_document::parser_template_contents(dom_node& element)
{
    if (element.template_contents != nullptr) return *element.template_contents;
    auto& fragment = create_node(dom_node_kind::document_fragment, "#document-fragment");
    element.template_contents = &fragment;
    return fragment;
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
        if (node.template_contents != nullptr) self(self, *node.template_contents);
        if (const auto* state = shadow_dom(node);
            state != nullptr && state->root != nullptr) {
            self(self, *state->root);
        }
        for (auto* child : node.children) {
            if (child != nullptr) self(self, *child);
        }
    };
    collect(collect, root);
    const auto before = nodes_.size();
    for (const auto* node : removed) {
        if (node->id == 0U || node->id > native_id_index_.size()) continue;
        native_id_index_[node->id - 1U] = nullptr;
    }
    if (shadow_dom_storage_ != nullptr) {
        for (const auto* node : removed) {
            shadow_dom_storage_->node_data.erase(node->id);
        }
        if (shadow_dom_storage_->node_data.empty()) shadow_dom_storage_.reset();
    }
    std::erase_if(nodes_, [&](const auto& node) { return removed.contains(node.get()); });
    while (!native_id_index_.empty() && native_id_index_.back() == nullptr) {
        native_id_index_.pop_back();
    }
    if (nodes_.size() != before) mark_dirty();
    return before - nodes_.size();
}

dom_node* native_document::find_by_native_id(uint32_t id) noexcept
{
    if (id == 0U || id > native_id_index_.size()) return nullptr;
    return native_id_index_[id - 1U];
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
    const auto nearest_element = [this](dom_node* hit) {
        while (hit != nullptr && hit->tag == "#text") hit = composed_parent(*hit);
        return hit;
    };
    std::vector<dom_node*> fixed;
    for (auto* child : composed_children(root)) {
        collect_fixed_positioned_nodes(*this, *child, fixed);
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
                *this,
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
    for (auto* child : composed_children(root)) {
        collect_positive_stacking_nodes(*this, *child, elevated);
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
                *this,
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
    if (!node.visible || node.style.display == display_mode::none) return nullptr;
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
            const auto& children = composed_children(node);
            for (const auto* child : children) {
                const auto z_index = node.style.z_index != 0
                    ? child->paint_z_index
                    : child->style.z_index;
                if (z_index < upper_bound) {
                    next_z = std::max(next_z, z_index);
                }
            }
            if (next_z == std::numeric_limits<int32_t>::min()) break;
            for (auto iterator = children.rbegin(); iterator != children.rend(); ++iterator) {
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
    shadow_dom_storage_.reset();
    nodes_.clear();
    native_id_index_.clear();
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
