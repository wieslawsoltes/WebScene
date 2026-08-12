    static void get_style(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_property));
        auto* node = unwrap_node(info.Holder());
        if (node != nullptr) info.GetReturnValue().Set(self->wrap_style(*node));
    }

    static void get_id(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_property));
        auto* node = unwrap_node(info.Holder());
        if (node != nullptr) info.GetReturnValue().Set(js_dom_string(info.GetIsolate(), node->id_attribute));
    }

    static void set_id(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_property));
        auto* node = unwrap_node(info.Holder());
        if (node != nullptr) {
            self->record_feature(
                "html", "attribute:id", "supported", {}, "html-attribute");
            const auto previous = node->attributes.find("id");
            const std::optional<std::string> old_value =
                previous == node->attributes.end()
                    ? std::nullopt
                    : std::optional<std::string>(previous->second);
            node->id_attribute = to_wtf8(info.GetIsolate(), value);
            node->attributes["id"] = node->id_attribute;
            self->recascade_connected_subtree(*node, "className-property");
            self->notify_custom_element_attribute(
                *node, "id", old_value, node->id_attribute);
        }
    }

    static void get_class_name(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_property));
        auto* node = unwrap_node(info.Holder());
        if (node != nullptr) info.GetReturnValue().Set(js_dom_string(info.GetIsolate(), node->class_name));
    }

    static void set_class_name(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_property));
        auto* node = unwrap_node(info.Holder());
        if (node != nullptr) {
            self->record_feature(
                "html", "attribute:class", "supported", {}, "html-attribute");
            const auto class_name = to_wtf8(info.GetIsolate(), value);
            const auto previous = node->attributes.find("class");
            const std::optional<std::string> old_value =
                previous == node->attributes.end()
                    ? std::nullopt
                    : std::optional<std::string>(previous->second);
            if (node->class_name == class_name
                && node->attributes.contains("class")) {
                self->notify_custom_element_attribute(
                    *node, "class", old_value, class_name);
                return;
            }
            node->class_name = class_name;
            node->attributes["class"] = node->class_name;
            self->recascade_connected_subtree(*node, "setAttribute");
            self->notify_custom_element_attribute(
                *node, "class", old_value, node->class_name);
        }
    }

    static void get_tag_name(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_property));
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        auto tag = node->tag;
        const auto html = node->namespace_uri().empty()
            || node->namespace_uri() == dom_node::html_namespace_uri;
        if (!node->xml_mode && html && node->kind == dom_node_kind::element) {
            std::transform(tag.begin(), tag.end(), tag.begin(), [](unsigned char value) {
                return static_cast<char>(std::toupper(value));
            });
        }
        info.GetReturnValue().Set(js_string(info.GetIsolate(), tag.c_str()));
    }

    static void get_local_name(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_property));
        auto* node = unwrap_node(info.Holder());
        if (node != nullptr) info.GetReturnValue().Set(js_string(info.GetIsolate(), node->tag.c_str()));
    }

    static void get_element_node_type(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.Holder());
        const auto type = node != nullptr && node->kind == dom_node_kind::text
            ? 3
            : node != nullptr && node->kind == dom_node_kind::comment
                ? 8
            : node != nullptr && node->kind == dom_node_kind::processing_instruction
                ? 7
            : node != nullptr && node->kind == dom_node_kind::document_fragment
                ? 11
            : node != nullptr && node->kind == dom_node_kind::document_type
                ? 10
                : 1;
        info.GetReturnValue().Set(v8::Integer::New(
            info.GetIsolate(),
            type));
    }

    static void get_processing_instruction_target(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node != nullptr) {
            info.GetReturnValue().Set(js_dom_string(
                info.GetIsolate(), node->namespace_prefix()));
        }
    }

    static void get_native_node_id(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.Holder());
        info.GetReturnValue().Set(v8::Integer::NewFromUnsigned(
            info.GetIsolate(),
            node == nullptr ? 0U : node->id));
    }

    static void append_node_text(const dom_node& node, std::string& value)
    {
        if (node.kind == dom_node_kind::comment
            || node.kind == dom_node_kind::processing_instruction
            || node.kind == dom_node_kind::document_type) return;
        value += node.text_content;
        for (const auto* child : node.children) append_node_text(*child, value);
    }

    static std::string normalize_textarea_newlines(std::string_view value)
    {
        std::string normalized;
        normalized.reserve(value.size());
        for (size_t index = 0; index < value.size(); ++index) {
            if (value[index] == '\r') {
                if (index + 1U < value.size() && value[index + 1U] == '\n') ++index;
                normalized.push_back('\n');
            } else {
                normalized.push_back(value[index]);
            }
        }
        return normalized;
    }

    static std::string textarea_default_value(const dom_node& node)
    {
        std::string value;
        append_node_text(node, value);
        return value;
    }

    static void reset_textarea_value(dom_node& node)
    {
        auto& control = node.mutable_form_control();
        control.value = normalize_textarea_newlines(textarea_default_value(node));
        control.value_initialized = true;
        control.dirty_value = false;
        control.selection_start = control.value.size();
        control.selection_end = control.value.size();
        control.selection_direction = text_selection_direction::none;
    }

    static void synchronize_textarea_value_from_children(dom_node& node)
    {
        if (node.tag == "textarea" && !node.form_control().dirty_value) {
            reset_textarea_value(node);
        }
    }

    static void get_text_content(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        if (node->kind == dom_node_kind::comment
            || node->kind == dom_node_kind::processing_instruction) {
            info.GetReturnValue().Set(js_dom_string(info.GetIsolate(), node->text_content));
            return;
        }
        std::string value;
        append_node_text(*node, value);
        info.GetReturnValue().Set(js_dom_string(info.GetIsolate(), value));
    }

    static bool is_inline_text_display(display_mode display)
    {
        return display == display_mode::inline_flow
            || display == display_mode::inline_block
            || display == display_mode::inline_flex
            || display == display_mode::inline_grid
            || display == display_mode::inline_table
            || display == display_mode::contents;
    }

    static void append_inner_text(const dom_node& node, std::string& value)
    {
        value += node.text_content;
        auto previous_was_block = false;
        for (const auto* child : node.children) {
            if (child->style.display == display_mode::none) continue;
            std::string child_text;
            append_inner_text(*child, child_text);
            if (child_text.empty()) continue;
            const auto child_is_block = !is_inline_text_display(child->style.display);
            if (!value.empty()
                && (previous_was_block || child_is_block)
                && value.back() != '\n') {
                value.push_back('\n');
            }
            value += child_text;
            previous_was_block = child_is_block;
        }
    }

    static void get_inner_text(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        self->ensure_layout();
        std::string value;
        append_inner_text(*node, value);
        info.GetReturnValue().Set(js_dom_string(info.GetIsolate(), value));
    }

    static void set_text_content(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> raw_value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        if (node->tag != "#text") {
            if (!self->blur_active_descendant_before_replacing_children(*node)) return;
            self->detach_all_children(*node);
        }
        const auto text = raw_value->IsNullOrUndefined()
            ? std::string{}
            : to_wtf8(info.GetIsolate(), raw_value);
        if (node->tag == "textarea") {
            node->text_content.clear();
            if (!text.empty()) {
                auto& text_node = self->document.create_node(
                    dom_node_kind::text, "#text");
                text_node.text_content = text;
                self->document.append_child(*node, text_node);
            }
            synchronize_textarea_value_from_children(*node);
        } else {
            node->text_content = text;
            if (node->kind == dom_node_kind::text && node->parent != nullptr) {
                synchronize_textarea_value_from_children(*node->parent);
            }
        }
        self->activate_connected_stylesheet(*node);
        self->document.mark_dirty();
    }

    static void get_document_node_type(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), 9));
    }

    static void get_document_node_name(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        info.GetReturnValue().Set(js_string(info.GetIsolate(), "#document"));
    }

    static void get_default_view(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = static_cast<dom_node*>(info.Holder()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        if (root != nullptr && self->detached_document_roots.contains(root->id)) {
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
            return;
        }
        const auto frame_document_root = root != nullptr && root->tag == "body"
            && root->parent != nullptr && root->parent->tag == "iframe";
        if (frame_document_root && !self->frame_context.IsEmpty()) {
            info.GetReturnValue().Set(self->frame_context.Get(info.GetIsolate())->Global());
            return;
        }
        info.GetReturnValue().Set(self->context.Get(info.GetIsolate())->Global());
    }

    static void get_namespace_uri(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr || node->kind != dom_node_kind::element) {
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
            return;
        }
        info.GetReturnValue().Set(js_dom_string(info.GetIsolate(), node->namespace_uri()));
    }

    static void get_attributes(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        std::vector<std::pair<std::string, std::string>> attributes;
        if (!node->id_attribute.empty()) attributes.emplace_back("id", node->id_attribute);
        if (!node->class_name.empty()) attributes.emplace_back("class", node->class_name);
        for (const auto& attribute : node->attributes) {
            if (attribute.first == "id" || attribute.first == "class") continue;
            attributes.push_back(attribute);
        }
        auto* self = current(info.GetIsolate());
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto result = v8::Array::New(info.GetIsolate(), static_cast<int>(attributes.size()));
        for (uint32_t index = 0; index < attributes.size(); ++index) {
            auto& state = self->attached_attr(*node, attributes[index].first);
            result->Set(local_context, index, self->wrap_attr(state)).Check();
        }
        result->Set(
            local_context,
            js_string(info.GetIsolate(), "item"),
            v8::Function::New(local_context, collection_item).ToLocalChecked()).Check();
        info.GetReturnValue().Set(result);
    }

    static void get_children(
        v8::Local<v8::Name> property,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_property));
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        const auto property_name = to_utf8(info.GetIsolate(), property);
        std::vector<dom_node*> children;
        children.reserve(node->children.size());
        for (auto* child : node->children) {
            if (child == nullptr) continue;
            if (property_name == "children" && child->kind != dom_node_kind::element) continue;
            children.push_back(child);
        }
        auto result = v8::Array::New(info.GetIsolate(), static_cast<int>(children.size()));
        auto local_context = info.GetIsolate()->GetCurrentContext();
        for (uint32_t index = 0; index < children.size(); ++index) {
            result->Set(local_context, index, self->wrap_node(*children[index])).Check();
        }
        info.GetReturnValue().Set(result);
    }

    static void get_template_content(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr || node->tag != "template") {
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
            return;
        }
        auto& contents = self->document.parser_template_contents(*node);
        info.GetReturnValue().Set(self->wrap_node(contents));
    }

    static void get_parent_node(
        v8::Local<v8::Name> property,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_property));
        auto* node = unwrap_node(info.Holder());
        const auto parent_element = to_utf8(info.GetIsolate(), property) == "parentElement";
        const auto active_document_element = node != nullptr && node->tag == "html"
            && (node == &self->active_root() || node->parent == &self->active_root());
        if (active_document_element) {
            if (parent_element) {
                info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
                return;
            }
            auto local_context = info.GetIsolate()->GetCurrentContext();
            v8::Local<v8::Value> document_value;
            if (local_context->Global()->Get(
                    local_context,
                    js_string(info.GetIsolate(), "document")).ToLocal(&document_value)) {
                info.GetReturnValue().Set(document_value);
            }
            return;
        }
        info.GetReturnValue().Set(node == nullptr || node->parent == nullptr
                || self->document.is_shadow_root(*node)
                || (parent_element && node->parent->kind != dom_node_kind::element)
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*node->parent)));
    }

    static void get_first_child(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_property));
        auto* node = unwrap_node(info.Holder());
        info.GetReturnValue().Set(node == nullptr || node->children.empty()
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*node->children.front())));
    }

    static void get_first_element_child(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_property));
        auto* node = unwrap_node(info.Holder());
        dom_node* child = nullptr;
        if (node != nullptr) {
            const auto found = std::find_if(
                node->children.begin(), node->children.end(),
                [](const auto* candidate) {
                    return candidate != nullptr && !candidate->tag.starts_with('#');
                });
            if (found != node->children.end()) child = *found;
        }
        info.GetReturnValue().Set(child == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*child)));
    }

    static void get_last_child(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        info.GetReturnValue().Set(node == nullptr || node->children.empty()
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*node->children.back())));
    }

    static void get_last_element_child(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        dom_node* child = nullptr;
        if (node != nullptr) {
            const auto found = std::find_if(
                node->children.rbegin(), node->children.rend(),
                [](const auto* candidate) {
                    return candidate != nullptr && !candidate->tag.starts_with('#');
                });
            if (found != node->children.rend()) child = *found;
        }
        info.GetReturnValue().Set(child == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*child)));
    }

    static dom_node* sibling_of(dom_node* node, int offset)
    {
        if (node == nullptr || node->parent == nullptr) return nullptr;
        const auto position = std::find(
            node->parent->children.begin(), node->parent->children.end(), node);
        if (position == node->parent->children.end()) return nullptr;
        if (offset < 0) return position == node->parent->children.begin() ? nullptr : *(position - 1);
        return position + 1 == node->parent->children.end() ? nullptr : *(position + 1);
    }

    static void get_next_sibling(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* sibling = sibling_of(unwrap_node(info.Holder()), 1);
        info.GetReturnValue().Set(sibling == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*sibling)));
    }

    static void get_previous_sibling(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* sibling = sibling_of(unwrap_node(info.Holder()), -1);
        info.GetReturnValue().Set(sibling == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*sibling)));
    }

    static dom_node* element_sibling_of(dom_node* node, int offset)
    {
        auto* sibling = sibling_of(node, offset);
        while (sibling != nullptr && sibling->tag.starts_with('#')) {
            sibling = sibling_of(sibling, offset);
        }
        return sibling;
    }

    static void get_next_element_sibling(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* sibling = element_sibling_of(unwrap_node(info.Holder()), 1);
        info.GetReturnValue().Set(sibling == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*sibling)));
    }

    static void get_previous_element_sibling(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* sibling = element_sibling_of(unwrap_node(info.Holder()), -1);
        info.GetReturnValue().Set(sibling == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*sibling)));
    }

    static void get_is_connected(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        info.GetReturnValue().Set(v8::Boolean::New(
            info.GetIsolate(),
            node != nullptr && self->is_connected(*node)));
    }

    static void get_owner_svg_element(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        auto* parent = node == nullptr ? nullptr : node->parent;
        while (parent != nullptr && parent->tag != "svg") parent = parent->parent;
        info.GetReturnValue().Set(parent == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*parent)));
    }

    static std::string dataset_attribute_name(v8::Isolate* isolate, v8::Local<v8::Name> property)
    {
        const auto key = to_utf8(isolate, property);
        std::string name = "data-";
        name.reserve(key.size() + 5U);
        for (const auto character : key) {
            if (std::isupper(static_cast<unsigned char>(character))) {
                name.push_back('-');
                name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
            } else {
                name.push_back(character);
            }
        }
        return name;
    }

    static std::string dataset_property_name(std::string_view attribute)
    {
        std::string property;
        bool uppercase_next = false;
        for (const auto character : attribute.substr(5U)) {
            if (character == '-') {
                uppercase_next = true;
            } else if (uppercase_next) {
                property.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
                uppercase_next = false;
            } else {
                property.push_back(character);
            }
        }
        return property;
    }

    static v8::Intercepted dataset_getter(
        v8::Local<v8::Name> property,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr || property->IsSymbol()) return v8::Intercepted::kNo;
        const auto match = node->attributes.find(dataset_attribute_name(info.GetIsolate(), property));
        if (match != node->attributes.end()) {
            info.GetReturnValue().Set(js_string(info.GetIsolate(), match->second.c_str()));
        }
        return v8::Intercepted::kYes;
    }

    static v8::Intercepted dataset_setter(
        v8::Local<v8::Name> property,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr || property->IsSymbol()) return v8::Intercepted::kNo;
        const auto name = dataset_attribute_name(info.GetIsolate(), property);
        self->record_html_attribute_feature(name, "dataset-binding");
        node->attributes[name] = to_utf8(info.GetIsolate(), value);
        if (self->attribute_requires_recascade(name)) {
            self->recascade_connected_subtree(*node, "setAttributeNS");
        } else {
            self->document.mark_dirty();
        }
        return v8::Intercepted::kYes;
    }

    static v8::Intercepted dataset_query(
        v8::Local<v8::Name> property,
        const v8::PropertyCallbackInfo<v8::Integer>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr || property->IsSymbol()) return v8::Intercepted::kNo;
        if (!node->attributes.contains(dataset_attribute_name(info.GetIsolate(), property))) {
            return v8::Intercepted::kNo;
        }
        info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), v8::None));
        return v8::Intercepted::kYes;
    }

    static v8::Intercepted dataset_deleter(
        v8::Local<v8::Name> property,
        const v8::PropertyCallbackInfo<v8::Boolean>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr || property->IsSymbol()) return v8::Intercepted::kNo;
        const auto name = dataset_attribute_name(info.GetIsolate(), property);
        node->attributes.erase(name);
        if (self->attribute_requires_recascade(name)) {
            self->recascade_connected_subtree(*node, "removeAttribute");
        } else {
            self->document.mark_dirty();
        }
        info.GetReturnValue().Set(true);
        return v8::Intercepted::kYes;
    }

    static void dataset_enumerator(const v8::PropertyCallbackInfo<v8::Array>& info)
    {
        auto* node = unwrap_node(info.Holder());
        auto result = v8::Array::New(info.GetIsolate());
        if (node != nullptr) {
            auto context = info.GetIsolate()->GetCurrentContext();
            uint32_t index = 0;
            for (const auto& [name, value] : node->attributes) {
                static_cast<void>(value);
                if (!name.starts_with("data-")) continue;
                result->Set(
                    context,
                    index++,
                    js_string(info.GetIsolate(), dataset_property_name(name).c_str())).Check();
            }
        }
        info.GetReturnValue().Set(result);
    }

    static void get_dataset(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_property));
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        auto isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        auto dataset = v8::ObjectTemplate::New(isolate);
        dataset->SetInternalFieldCount(1);
        dataset->SetHandler(v8::NamedPropertyHandlerConfiguration(
            dataset_getter,
            dataset_setter,
            dataset_query,
            dataset_deleter,
            dataset_enumerator,
            v8::Local<v8::Value>(),
            v8::PropertyHandlerFlags::kOnlyInterceptStrings));
        auto result = dataset->NewInstance(local_context).ToLocalChecked();
        result->SetAlignedPointerInInternalField(
            0,
            node,
            v8::kEmbedderDataTypeTagDefault);
        info.GetReturnValue().Set(result);
    }

    static void get_owner_document(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_property));
        auto isolate = info.GetIsolate();
        v8::Local<v8::Value> document_value;
        if (isolate->GetCurrentContext()->Global()->Get(
                isolate->GetCurrentContext(),
                js_string(isolate, "document")).ToLocal(&document_value)) {
            info.GetReturnValue().Set(document_value);
        }
    }

    static dom_node* class_list_node(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        return info.Data()->IsExternal()
            ? static_cast<dom_node*>(info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault))
            : nullptr;
    }

    static void class_list_value_getter_bridge(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_property));
        const auto* node = class_list_node(info);
        if (node == nullptr) return;
        info.GetReturnValue().Set(js_dom_string(
            info.GetIsolate(),
            node->class_name));
    }

    static void class_list_value_setter_bridge(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_mutation));
        auto* node = class_list_node(info);
        if (node == nullptr || info.Length() < 1) return;
        self->record_feature(
            "html", "attribute:class", "supported", {}, "html-attribute");
        const auto value = to_wtf8(info.GetIsolate(), info[0]);
        if (node->class_name == value
            && node->attributes.contains("class")) {
            return;
        }
        node->class_name = value;
        node->attributes["class"] = node->class_name;
        self->recascade_connected_subtree(*node, "classList.value");
    }

    static bool has_class(const dom_node& node, std::string_view value)
    {
        const std::string_view classes(node.class_name);
        const auto html_whitespace = [](unsigned char character) {
            return character == ' ' || character == '\t' || character == '\n'
                || character == '\f' || character == '\r';
        };
        size_t cursor = 0;
        while (cursor < classes.size()) {
            while (cursor < classes.size()
                && html_whitespace(static_cast<unsigned char>(classes[cursor]))) ++cursor;
            const auto start = cursor;
            while (cursor < classes.size()
                && !html_whitespace(static_cast<unsigned char>(classes[cursor]))) ++cursor;
            if (classes.substr(start, cursor - start) == value) return true;
        }
        return false;
    }

    static bool remove_class(dom_node& node, const std::string& removed)
    {
        if (!has_class(node, removed)) return false;
        const auto html_whitespace = [](unsigned char character) {
            return character == ' ' || character == '\t' || character == '\n'
                || character == '\f' || character == '\r';
        };
        std::string result;
        size_t cursor = 0;
        while (cursor < node.class_name.size()) {
            while (cursor < node.class_name.size()
                && html_whitespace(static_cast<unsigned char>(node.class_name[cursor]))) ++cursor;
            const auto start = cursor;
            while (cursor < node.class_name.size()
                && !html_whitespace(static_cast<unsigned char>(node.class_name[cursor]))) ++cursor;
            const auto item = node.class_name.substr(start, cursor - start);
            if (item.empty() || item == removed) continue;
            if (!result.empty()) result.push_back(' ');
            result += item;
        }
        node.class_name = std::move(result);
        if (node.class_name.empty()) node.attributes.erase("class");
        else node.attributes["class"] = node.class_name;
        return true;
    }

    static void class_list_add(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_mutation));
        auto* node = class_list_node(info);
        if (node == nullptr) return;
        auto changed = false;
        for (int index = 0; index < info.Length(); ++index) {
            const auto value = to_utf8(info.GetIsolate(), info[index]);
            if (!value.empty() && !has_class(*node, value)) {
                if (!node->class_name.empty()) node->class_name.push_back(' ');
                node->class_name += value;
                node->attributes["class"] = node->class_name;
                changed = true;
            }
        }
        if (changed) self->recascade_connected_subtree(*node, "classList.add");
    }

    static void class_list_remove(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_mutation));
        auto* node = class_list_node(info);
        if (node == nullptr || info.Length() < 1) return;
        auto changed = false;
        for (int index = 0; index < info.Length(); ++index) {
            changed |= remove_class(*node, to_utf8(info.GetIsolate(), info[index]));
        }
        if (changed) self->recascade_connected_subtree(*node, "classList.remove");
    }

    static void class_list_contains(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_property));
        auto* node = class_list_node(info);
        const auto value = info.Length() > 0 ? to_utf8(info.GetIsolate(), info[0]) : std::string{};
        info.GetReturnValue().Set(v8::Boolean::New(
            info.GetIsolate(),
            node != nullptr && has_class(*node, value)));
    }

    static void class_list_toggle(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_mutation));
        auto* node = class_list_node(info);
        if (node == nullptr || info.Length() < 1) return;
        const auto value = to_utf8(info.GetIsolate(), info[0]);
        const auto present = has_class(*node, value);
        const auto enabled = info.Length() > 1 ? info[1]->BooleanValue(info.GetIsolate()) : !present;
        auto changed = false;
        if (enabled && !present) {
            if (!node->class_name.empty()) node->class_name.push_back(' ');
            node->class_name += value;
            node->attributes["class"] = node->class_name;
            changed = true;
        } else if (!enabled && present) {
            changed = remove_class(*node, value);
        }
        if (changed) self->recascade_connected_subtree(*node, "classList.toggle");
        info.GetReturnValue().Set(v8::Boolean::New(info.GetIsolate(), enabled));
    }

    static void get_class_list(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_property));
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        info.GetReturnValue().Set(self->wrap_class_list(*node));
    }

    static void range_select_node_contents(const v8::FunctionCallbackInfo<v8::Value>&)
    {
    }

    static void fragment_remove_child(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (info.Length() > 0) info.GetReturnValue().Set(info[0]);
    }

    static void range_create_contextual_fragment(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        const auto html = info.Length() > 0 ? to_utf8(info.GetIsolate(), info[0]) : std::string{};
        auto& fragment_root = self->document.create_element("#fragment");
        self->parse_inner_html(fragment_root, html);
        // A contextual fragment is a real queryable DOM node. Reuse the native
        // node wrapper so querySelector(All), children and removeChild all have
        // the same behavior as the rest of the native DOM.
        info.GetReturnValue().Set(self->wrap_node(fragment_root));
    }

    static void create_range(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto range = v8::Object::New(info.GetIsolate());
        range->Set(
            local_context,
            js_string(info.GetIsolate(), "selectNodeContents"),
            v8::Function::New(local_context, range_select_node_contents).ToLocalChecked()).Check();
        range->Set(
            local_context,
            js_string(info.GetIsolate(), "createContextualFragment"),
            v8::Function::New(local_context, range_create_contextual_fragment).ToLocalChecked()).Check();
        info.GetReturnValue().Set(range);
    }

    static void get_client_width(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_geometry));
        auto* node = unwrap_node(info.Holder());
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (self->resize_profile_active && node != nullptr) {
            ++self->last_resize_geometry_counts[
                "width:" + node->tag + "#" + node->id_attribute + "." + node->class_name];
        }
#endif
        const auto is_browsing_context_root = node != nullptr
            && node->tag == "body"
            && (node->parent == nullptr || node->parent->tag == "iframe");
        if (node != nullptr && node->tag == "html"
            && node->parent == &self->document.body()) {
            node = &self->document.body();
        }
        self->ensure_layout(node);
        if (node != nullptr) {
            for (auto* current_node = node; current_node != nullptr;
                 current_node = self->document.composed_parent(*current_node)) {
                if (current_node->style.display != display_mode::none) continue;
                info.GetReturnValue().Set(v8::Number::New(info.GetIsolate(), 0));
                return;
            }
            const auto width = is_browsing_context_root
                ? self->viewport_provider().width
                : node->layout.width;
            info.GetReturnValue().Set(v8::Number::New(info.GetIsolate(), width));
        }
    }

    static void get_client_left(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node != nullptr && node->tag == "html"
            && node->parent == &self->document.body()) {
            node = &self->document.body();
        }
        self->ensure_layout(node);
        if (node == nullptr) return;
        info.GetReturnValue().Set(v8::Number::New(
            info.GetIsolate(),
            resolve_connected_css_length(
                *node,
                node->style.border_left_width,
                std::max(0.0F, node->layout.width))));
    }

    static void get_client_top(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node != nullptr && node->tag == "html"
            && node->parent == &self->document.body()) {
            node = &self->document.body();
        }
        self->ensure_layout(node);
        if (node == nullptr) return;
        info.GetReturnValue().Set(v8::Number::New(
            info.GetIsolate(),
            resolve_connected_css_length(
                *node,
                node->style.border_top_width,
                std::max(0.0F, node->layout.width))));
    }

    static void get_offset_width(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_geometry));
        auto* node = unwrap_node(info.Holder());
        self->ensure_layout(node);
        if (node == nullptr) return;
        for (auto* current_node = node; current_node != nullptr;
             current_node = self->document.composed_parent(*current_node)) {
            if (current_node->style.display != display_mode::none) continue;
            info.GetReturnValue().Set(v8::Number::New(info.GetIsolate(), 0));
            return;
        }
        const auto width =
            node->style.display == display_mode::inline_flow
                && node->namespace_uri() != "http://www.w3.org/2000/svg"
                && node->style.width.unit == length_unit::automatic
            ? self->document.measure_inline_content_width(*node)
            : node->layout.width;
        info.GetReturnValue().Set(v8::Number::New(info.GetIsolate(), width));
    }

    static void get_client_height(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_geometry));
        auto* node = unwrap_node(info.Holder());
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (self->resize_profile_active && node != nullptr) {
            ++self->last_resize_geometry_counts[
                "height:" + node->tag + "#" + node->id_attribute + "." + node->class_name];
        }
#endif
        const auto is_browsing_context_root = node != nullptr
            && node->tag == "body"
            && (node->parent == nullptr || node->parent->tag == "iframe");
        if (node != nullptr && node->tag == "html"
            && node->parent == &self->document.body()) {
            node = &self->document.body();
        }
        self->ensure_layout(node);
        if (node != nullptr) {
            for (auto* current_node = node; current_node != nullptr;
                 current_node = self->document.composed_parent(*current_node)) {
                if (current_node->style.display != display_mode::none) continue;
                info.GetReturnValue().Set(v8::Number::New(info.GetIsolate(), 0));
                return;
            }
            const auto height = is_browsing_context_root
                ? self->viewport_provider().height
                : node->layout.height;
            info.GetReturnValue().Set(v8::Number::New(info.GetIsolate(), height));
        }
    }

    static void get_scroll_width(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->ensure_layout();
        auto* node = unwrap_node(info.Holder());
        if (node != nullptr && node->tag == "html"
            && node->parent == &self->document.body()) {
            node = &self->document.body();
        }
        if (node != nullptr) {
            info.GetReturnValue().Set(v8::Number::New(
                info.GetIsolate(),
                std::max(node->layout.width, node->scroll_content_width)));
        }
    }

    static void get_scroll_height(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->ensure_layout();
        auto* node = unwrap_node(info.Holder());
        if (node != nullptr && node->tag == "html"
            && node->parent == &self->document.body()) {
            node = &self->document.body();
        }
        if (node != nullptr) {
            info.GetReturnValue().Set(v8::Number::New(
                info.GetIsolate(),
                std::max(node->layout.height, node->scroll_content_height)));
        }
    }

    static void get_scroll_left(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node != nullptr && node->tag == "html"
            && node->parent == &self->document.body()) {
            node = &self->document.body();
        }
        if (node != nullptr) {
            info.GetReturnValue().Set(v8::Number::New(info.GetIsolate(), node->scroll_left));
        }
    }

    static void get_scroll_top(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node != nullptr && node->tag == "html"
            && node->parent == &self->document.body()) {
            node = &self->document.body();
        }
        if (node != nullptr) {
            info.GetReturnValue().Set(v8::Number::New(info.GetIsolate(), node->scroll_top));
        }
    }

    static void set_scroll_left(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        self->ensure_layout();
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        if (node->tag == "html" && node->parent == &self->document.body()) {
            node = &self->document.body();
        }
        const auto maximum = std::max(
            0.0F,
            node->scroll_content_width - node->scroll_viewport_width);
        const auto next = std::clamp(
            static_cast<float>(value->NumberValue(
                info.GetIsolate()->GetCurrentContext()).FromMaybe(0)),
            0.0F,
            maximum);
        if (std::abs(next - node->scroll_left) <= 0.01F) return;
        node->scroll_left = next;
        self->document.mark_dirty();
        self->enqueue_programmatic_scroll_event(*node);
    }

    static void set_scroll_top(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        self->ensure_layout();
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        if (node->tag == "html" && node->parent == &self->document.body()) {
            node = &self->document.body();
        }
        const auto maximum = std::max(
            0.0F,
            node->scroll_content_height - node->scroll_viewport_height);
        const auto next = std::clamp(
            static_cast<float>(value->NumberValue(
                info.GetIsolate()->GetCurrentContext()).FromMaybe(0)),
            0.0F,
            maximum);
        if (std::abs(next - node->scroll_top) <= 0.01F) return;
        node->scroll_top = next;
        self->document.mark_dirty();
        self->enqueue_programmatic_scroll_event(*node);
    }

    static void element_scroll_into_view(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->ensure_layout();
        auto* node = info.This()->IsObject()
            ? unwrap_node(info.This().As<v8::Object>())
            : nullptr;
        if (node == nullptr) return;

        auto block = std::string("start");
        auto inline_alignment = std::string("nearest");
        if (info.Length() > 0) {
            if (info[0]->IsBoolean()) {
                block = info[0]->BooleanValue(info.GetIsolate()) ? "start" : "end";
            } else if (info[0]->IsObject()) {
                auto context = info.GetIsolate()->GetCurrentContext();
                auto options = info[0].As<v8::Object>();
                v8::Local<v8::Value> value;
                if (options->Get(context, js_string(info.GetIsolate(), "block")).ToLocal(&value)
                    && value->IsString()) {
                    block = to_utf8(info.GetIsolate(), value);
                }
                if (options->Get(context, js_string(info.GetIsolate(), "inline")).ToLocal(&value)
                    && value->IsString()) {
                    inline_alignment = to_utf8(info.GetIsolate(), value);
                }
            }
        }

        const auto aligned_offset = [](
            std::string_view alignment,
            float item_start,
            float item_end,
            float viewport_start,
            float viewport_end) {
            if (alignment == "center") {
                return (item_start + item_end - viewport_start - viewport_end) * 0.5F;
            }
            if (alignment == "end") return item_end - viewport_end;
            if (alignment == "nearest") {
                if (item_start < viewport_start && item_end > viewport_end) return 0.0F;
                if (item_start < viewport_start) return item_start - viewport_start;
                if (item_end > viewport_end) return item_end - viewport_end;
                return 0.0F;
            }
            return item_start - viewport_start;
        };

        for (auto* ancestor = self->document.composed_parent(*node);
            ancestor != nullptr;
            ancestor = self->document.composed_parent(*ancestor)) {
            const auto maximum_x = std::max(
                0.0F,
                ancestor->scroll_content_width - ancestor->scroll_viewport_width);
            const auto maximum_y = std::max(
                0.0F,
                ancestor->scroll_content_height - ancestor->scroll_viewport_height);
            const auto viewport_width = ancestor->scroll_viewport_width > 0.0F
                ? ancestor->scroll_viewport_width
                : ancestor->layout.width;
            const auto viewport_height = ancestor->scroll_viewport_height > 0.0F
                ? ancestor->scroll_viewport_height
                : ancestor->layout.height;
            auto changed = false;
            const auto root_viewport_scroll_x = ancestor == &self->active_root()
                && ancestor->style.overflow_x == overflow_mode::visible;
            const auto root_viewport_scroll_y = ancestor == &self->active_root()
                && ancestor->style.overflow_y == overflow_mode::visible;
            if ((ancestor->style.scroll_x_enabled || root_viewport_scroll_x)
                && maximum_x > 0.0F) {
                const auto delta = aligned_offset(
                    inline_alignment,
                    node->layout.x,
                    node->layout.x + node->layout.width,
                    ancestor->layout.x,
                    ancestor->layout.x + viewport_width);
                const auto next = std::clamp(ancestor->scroll_left + delta, 0.0F, maximum_x);
                changed = changed || std::abs(next - ancestor->scroll_left) > 0.01F;
                ancestor->scroll_left = next;
            }
            if ((ancestor->style.scroll_y_enabled || root_viewport_scroll_y)
                && maximum_y > 0.0F) {
                const auto delta = aligned_offset(
                    block,
                    node->layout.y,
                    node->layout.y + node->layout.height,
                    ancestor->layout.y,
                    ancestor->layout.y + viewport_height);
                const auto next = std::clamp(ancestor->scroll_top + delta, 0.0F, maximum_y);
                changed = changed || std::abs(next - ancestor->scroll_top) > 0.01F;
                ancestor->scroll_top = next;
            }
            if (!changed) continue;
            self->document.mark_dirty();
            self->ensure_layout();
        }
    }

    static dom_node* find_offset_parent(native_document& document, dom_node& node)
    {
        if (node.style.position == position_mode::fixed) return nullptr;
        for (auto* ancestor = document.composed_parent(node);
            ancestor != nullptr;
            ancestor = document.composed_parent(*ancestor)) {
            if (ancestor->tag == "body" || ancestor->style.position != position_mode::normal) {
                return ancestor;
            }
        }
        return nullptr;
    }

    static void get_offset_left(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_geometry));
        self->ensure_layout();
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        const auto* offset_parent = find_offset_parent(self->document, *node);
        const auto parent_x = offset_parent == nullptr ? 0.0F : offset_parent->layout.x;
        auto restored_scroll = 0.0F;
        for (auto* ancestor = self->document.composed_parent(*node);
            ancestor != nullptr;
            ancestor = self->document.composed_parent(*ancestor)) {
            restored_scroll += ancestor->scroll_left;
            if (ancestor == offset_parent) break;
        }
        info.GetReturnValue().Set(v8::Number::New(
            info.GetIsolate(),
            node->layout.x - parent_x + restored_scroll));
    }

    static void get_offset_top(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_geometry));
        self->ensure_layout();
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        const auto* offset_parent = find_offset_parent(self->document, *node);
        const auto parent_y = offset_parent == nullptr ? 0.0F : offset_parent->layout.y;
        auto restored_scroll = 0.0F;
        for (auto* ancestor = self->document.composed_parent(*node);
            ancestor != nullptr;
            ancestor = self->document.composed_parent(*ancestor)) {
            restored_scroll += ancestor->scroll_top;
            if (ancestor == offset_parent) break;
        }
        info.GetReturnValue().Set(v8::Number::New(
            info.GetIsolate(),
            node->layout.y - parent_y + restored_scroll));
    }

    static void get_offset_parent(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        auto* offset_parent = node == nullptr
            ? nullptr
            : find_offset_parent(self->document, *node);
        info.GetReturnValue().Set(offset_parent == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*offset_parent)));
    }

    static double element_dimension(dom_node& node, const char* name, double fallback)
    {
        const auto iterator = node.attributes.find(name);
        if (iterator == node.attributes.end()) return fallback;
        try {
            return std::stod(iterator->second);
        } catch (...) {
            return fallback;
        }
    }

    static void mark_canvas_scene_changed()
    {
        auto* self = current(v8::Isolate::GetCurrent());
        if (self != nullptr) {
            self->document.mark_scene_changed();
        }
    }

    static void advance_canvas_generation(dom_node& node)
    {
        mark_canvas_scene_changed();
        auto& canvas = node.mutable_canvas();
        ++canvas.generation;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        constexpr uint64_t retained_diagnostic_generations = 64U;
        if (canvas.generation <= retained_diagnostic_generations) return;
        const auto oldest = canvas.generation - retained_diagnostic_generations;
        std::erase_if(
            canvas.probable_volume_by_generation,
            [oldest](const auto& entry) { return entry.first < oldest; });
#endif
    }

    static void get_element_width(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_backing_store));
        auto* node = unwrap_node(info.Holder());
        if (node != nullptr) info.GetReturnValue().Set(v8::Number::New(
            info.GetIsolate(),
            element_dimension(*node, "width", node->tag == "canvas" ? 300 : node->layout.width)));
    }

    static void reset_canvas_backing_store(implementation& self, dom_node& node)
    {
        if (node.tag != "canvas") return;
        auto& canvas = node.mutable_canvas();
        canvas.rects.clear();
        canvas.lines.clear();
        advance_canvas_generation(node);
        canvas.commands.clear();
        canvas.strings.clear();
        canvas.string_indices.clear();
        const auto state = self.canvas_states.find(self.wrapper_key(node));
        if (state == self.canvas_states.end() || state->second == nullptr) return;
        state->second->current_x = 0;
        state->second->current_y = 0;
        state->second->start_x = 0;
        state->second->start_y = 0;
        state->second->has_current = false;
        state->second->transform = {};
        state->second->stack.clear();
        state->second->path.clear();
        state->second->line_dash.clear();
        state->second->has_clip = false;
        state->second->has_emitted_paint_state = false;
    }

    static void set_element_width(v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_backing_store));
        auto* node = unwrap_node(info.Holder());
        if (node != nullptr) {
            node->attributes["width"] = to_utf8(info.GetIsolate(), value);
            reset_canvas_backing_store(*self, *node);
        }
    }

    static void get_element_height(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_backing_store));
        auto* node = unwrap_node(info.Holder());
        if (node != nullptr) info.GetReturnValue().Set(v8::Number::New(
            info.GetIsolate(),
            element_dimension(*node, "height", node->tag == "canvas" ? 150 : node->layout.height)));
    }

    static void set_element_height(v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_backing_store));
        auto* node = unwrap_node(info.Holder());
        if (node != nullptr) {
            node->attributes["height"] = to_utf8(info.GetIsolate(), value);
            reset_canvas_backing_store(*self, *node);
        }
    }

    static void get_tab_index(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        int value = 0;
        if (!try_tab_index(node, value)) {
            value = is_natively_focusable(node) ? 0 : -1;
        }
        info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), value));
    }

    static void set_tab_index(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        const auto tab_index = value->Int32Value(
            info.GetIsolate()->GetCurrentContext()).FromMaybe(0);
        self->record_html_attribute_feature("tabindex", "dom-property-set");
        node->attributes["tabindex"] = std::to_string(tab_index);
        self->recascade_connected_subtree(*node, "style.cssText");
    }

    static void get_table_cell_span(
        v8::Local<v8::Name> property,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        const auto property_name = to_utf8(info.GetIsolate(), property);
        const auto attribute_name = property_name == "rowSpan" ? "rowspan" : "colspan";
        auto found = node->attributes.find(attribute_name);
        if (found == node->attributes.end()) found = node->attributes.find(property_name);
        const auto span = found == node->attributes.end()
            ? 1
            : std::max(1, static_cast<int>(std::strtol(found->second.c_str(), nullptr, 10)));
        info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), span));
    }

    static void set_table_cell_span(
        v8::Local<v8::Name> property,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        const auto property_name = to_utf8(info.GetIsolate(), property);
        const auto attribute_name = property_name == "rowSpan" ? "rowspan" : "colspan";
        const auto span = std::max(
            1,
            value->Int32Value(info.GetIsolate()->GetCurrentContext()).FromMaybe(1));
        node->attributes[attribute_name] = std::to_string(span);
        self->document.mark_dirty();
    }

    static void get_element_type(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        const auto iterator = node->attributes.find("type");
        const auto value = iterator != node->attributes.end()
            ? iterator->second
            : node->tag == "input" ? std::string("text")
            : node->tag == "button" ? std::string("submit")
            : node->tag == "select" && node->attributes.contains("multiple")
                ? std::string("select-multiple")
            : node->tag == "select" ? std::string("select-one")
            : std::string{};
        info.GetReturnValue().Set(js_string(info.GetIsolate(), value.c_str()));
    }

    static void set_element_type(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        node->attributes["type"] = to_utf8(info.GetIsolate(), value);
        self->recascade_connected_subtree(*node);
    }

    static void get_reflected_string_attribute(
        v8::Local<v8::Name> property,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        auto name = to_utf8(info.GetIsolate(), property);
        if (!node->xml_mode) name = lower_html_name(name);
        if (!node->xml_mode && name == "htmlfor") name = "for";
        const auto found = node->attributes.find(name);
        info.GetReturnValue().Set(js_string(
            info.GetIsolate(),
            found == node->attributes.end() ? "" : found->second.c_str()));
    }

    static void set_reflected_string_attribute(
        v8::Local<v8::Name> property,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        auto name = to_utf8(info.GetIsolate(), property);
        if (!node->xml_mode) name = lower_html_name(name);
        if (!node->xml_mode && name == "htmlfor") name = "for";
        const auto previous = node->attributes.find(name);
        const std::optional<std::string> old_value = previous == node->attributes.end()
            ? std::nullopt
            : std::optional<std::string>(previous->second);
        const auto text = to_wtf8(info.GetIsolate(), value);
        node->attributes[name] = text;
        self->document.mark_dirty();
        self->notify_custom_element_attribute(*node, name, old_value, text);
    }

    static void get_default_value(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr || (node->tag != "input" && node->tag != "textarea")) return;
        if (node->tag == "textarea") {
            info.GetReturnValue().Set(js_dom_string(
                info.GetIsolate(), textarea_default_value(*node)));
            return;
        }
        const auto authored = node->attributes.find("value");
        info.GetReturnValue().Set(js_dom_string(
            info.GetIsolate(),
            authored == node->attributes.end() ? std::string{} : authored->second));
    }

    static void set_default_value(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr || (node->tag != "input" && node->tag != "textarea")) return;
        const auto text = to_wtf8(info.GetIsolate(), value);
        if (node->tag == "textarea") {
            if (!self->blur_active_descendant_before_replacing_children(*node)) return;
            self->detach_all_children(*node);
            node->text_content.clear();
            if (!text.empty()) {
                auto& text_node = self->document.create_node(
                    dom_node_kind::text, "#text");
                text_node.text_content = text;
                self->document.append_child(*node, text_node);
            }
            synchronize_textarea_value_from_children(*node);
        } else {
            node->attributes["value"] = text;
        }
        self->document.mark_dirty();
    }

    static void get_max_length(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr || (node->tag != "input" && node->tag != "textarea")) return;
        const auto authored = node->attributes.find("maxlength");
        const auto value = authored == node->attributes.end()
            ? -1
            : static_cast<int>(std::strtol(authored->second.c_str(), nullptr, 10));
        info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), value));
    }

    static void set_max_length(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr || (node->tag != "input" && node->tag != "textarea")) return;
        node->attributes["maxlength"] = std::to_string(
            value->Int32Value(info.GetIsolate()->GetCurrentContext()).FromMaybe(-1));
        self->document.mark_dirty();
    }

    static void get_read_only(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr || (node->tag != "input" && node->tag != "textarea")) return;
        info.GetReturnValue().Set(v8::Boolean::New(
            info.GetIsolate(), node->attributes.contains("readonly")));
    }

    static void set_read_only(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        set_boolean_attribute(value, info, "readonly");
    }

    static void collect_descendants_by_tag(
        dom_node& root,
        std::string_view tag,
        std::vector<dom_node*>& result)
    {
        for (auto* child : root.children) {
            if (child == nullptr) continue;
            if (child->tag == tag) result.push_back(child);
            collect_descendants_by_tag(*child, tag, result);
        }
    }

    static std::string option_value(const dom_node& option)
    {
        const auto authored = option.attributes.find("value");
        if (authored != option.attributes.end()) return authored->second;
        std::string value;
        append_node_text(option, value);
        std::string normalized;
        normalized.reserve(value.size());
        auto pending_space = false;
        const auto ascii_whitespace = [](unsigned char character) {
            return character == ' ' || character == '\t' || character == '\n'
                || character == '\f' || character == '\r';
        };
        for (const auto character : value) {
            if (ascii_whitespace(static_cast<unsigned char>(character))) {
                pending_space = !normalized.empty();
                continue;
            }
            if (pending_space) normalized.push_back(' ');
            normalized.push_back(character);
            pending_space = false;
        }
        return normalized;
    }

    static dom_node* containing_select(dom_node& option)
    {
        auto* select = option.parent;
        while (select != nullptr && select->tag != "select") select = select->parent;
        return select;
    }

    static bool option_is_selected(dom_node& option)
    {
        if (option.form_control().selectedness_initialized) {
            return option.form_control().selectedness;
        }
        auto* select = containing_select(option);
        if (select == nullptr) return option.attributes.contains("selected");
        if (select->form_control().selection_explicitly_empty) return false;
        std::vector<dom_node*> options;
        collect_descendants_by_tag(*select, "option", options);
        const auto has_live_selection = std::any_of(
            options.begin(), options.end(), [](const auto* candidate) {
                return candidate != nullptr
                    && candidate->form_control().selectedness_initialized
                    && candidate->form_control().selectedness;
            });
        if (has_live_selection) return false;
        const auto authored = std::find_if(
            options.begin(),
            options.end(),
            [](const auto* candidate) {
                return candidate != nullptr && candidate->attributes.contains("selected");
            });
        if (authored != options.end()) {
            return select->attributes.contains("multiple")
                ? option.attributes.contains("selected")
                : *authored == &option;
        }
        if (select->attributes.contains("multiple")) return false;
        return !options.empty() && options.front() == &option;
    }

    static void set_select_selected_index(dom_node& select, int32_t selected)
    {
        std::vector<dom_node*> options;
        collect_descendants_by_tag(select, "option", options);
        const auto valid = selected >= 0 && static_cast<size_t>(selected) < options.size();
        select.mutable_form_control().selection_explicitly_empty = !valid;
        for (uint32_t index = 0; index < options.size(); ++index) {
            options[index]->mutable_form_control().selectedness_initialized = true;
            options[index]->mutable_form_control().selectedness = valid && static_cast<int32_t>(index) == selected;
        }
    }

    static void get_select_options(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr || node->tag != "select") return;
        std::vector<dom_node*> options;
        collect_descendants_by_tag(*node, "option", options);
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto result = v8::Array::New(info.GetIsolate(), static_cast<int>(options.size()));
        for (uint32_t index = 0; index < options.size(); ++index) {
            result->Set(local_context, index, self->wrap_node(*options[index])).Check();
        }
        info.GetReturnValue().Set(result);
    }

    static bool is_form_associated_control(const dom_node& node)
    {
        return node.tag == "button" || node.tag == "fieldset" || node.tag == "input"
            || node.tag == "object" || node.tag == "output" || node.tag == "select"
            || node.tag == "textarea";
    }

    static void collect_form_controls(dom_node& root, std::vector<dom_node*>& result)
    {
        for (auto* child : root.children) {
            if (child == nullptr) continue;
            if (is_form_associated_control(*child)) result.push_back(child);
            collect_form_controls(*child, result);
        }
    }

    static void get_form_elements(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr || node->tag != "form") return;
        std::vector<dom_node*> controls;
        collect_form_controls(*node, controls);
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto result = v8::Array::New(info.GetIsolate(), static_cast<int>(controls.size()));
        for (uint32_t index = 0; index < controls.size(); ++index) {
            result->Set(local_context, index, self->wrap_node(*controls[index])).Check();
        }
        info.GetReturnValue().Set(result);
    }

    static void get_selected_index(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr || node->tag != "select") return;
        std::vector<dom_node*> options;
        collect_descendants_by_tag(*node, "option", options);
        int32_t selected = -1;
        for (uint32_t index = 0; index < options.size(); ++index) {
            if (!option_is_selected(*options[index])) continue;
            selected = static_cast<int32_t>(index);
            break;
        }
        info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), selected));
    }

    static void set_selected_index(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr || node->tag != "select") return;
        const auto selected = value->Int32Value(info.GetIsolate()->GetCurrentContext()).FromMaybe(-1);
        set_select_selected_index(*node, selected);
        self->document.mark_dirty();
    }

    static void get_form_value(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        if (node->tag == "option") {
            info.GetReturnValue().Set(js_string(info.GetIsolate(), option_value(*node).c_str()));
            return;
        }
        if (node->tag == "select") {
            std::vector<dom_node*> options;
            collect_descendants_by_tag(*node, "option", options);
            dom_node* selected = nullptr;
            for (auto* option : options) {
                if (!option_is_selected(*option)) continue;
                selected = option;
                break;
            }
            const auto value = selected == nullptr ? std::string{} : option_value(*selected);
            info.GetReturnValue().Set(js_string(info.GetIsolate(), value.c_str()));
            return;
        }
        if (node->tag == "progress" || node->tag == "meter") {
            const auto authored = node->attributes.find("value");
            const auto numeric = authored == node->attributes.end()
                ? 0.0
                : std::strtod(authored->second.c_str(), nullptr);
            info.GetReturnValue().Set(v8::Number::New(info.GetIsolate(), numeric));
            return;
        }
        if (node->tag == "input") {
            const auto type = node->attributes.find("type");
            if ((type != node->attributes.end())
                && (lower_html_name(type->second) == "checkbox"
                    || lower_html_name(type->second) == "radio")
                && !node->attributes.contains("value")
                && !node->mutable_form_control().value_initialized) {
                info.GetReturnValue().Set(js_string(info.GetIsolate(), "on"));
                return;
            }
        }
        if (!node->mutable_form_control().value_initialized) {
            if (node->tag == "textarea") {
                reset_textarea_value(*node);
            } else {
                const auto value = node->attributes.find("value");
                if (value != node->attributes.end()) {
                    node->mutable_form_control().value = value->second;
                }
                node->mutable_form_control().value_initialized = true;
                node->mutable_form_control().selection_start = node->mutable_form_control().value.size();
                node->mutable_form_control().selection_end = node->mutable_form_control().value.size();
                node->mutable_form_control().selection_direction = text_selection_direction::none;
            }
        }
        info.GetReturnValue().Set(js_string(info.GetIsolate(), node->mutable_form_control().value.c_str()));
    }

    static void get_form_owner(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        dom_node* owner = nullptr;
        const auto explicit_owner = node->attributes.find("form");
        if (explicit_owner != node->attributes.end() && !explicit_owner->second.empty()) {
            auto* candidate = self->document.find_by_id(explicit_owner->second);
            if (candidate != nullptr && candidate->tag == "form") owner = candidate;
        }
        if (owner == nullptr && explicit_owner == node->attributes.end()) {
            for (auto* ancestor = node->parent; ancestor != nullptr; ancestor = ancestor->parent) {
                if (ancestor->tag != "form") continue;
                owner = ancestor;
                break;
            }
        }
        info.GetReturnValue().Set(owner == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*owner)));
    }

    static void set_form_value(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        if (node->tag == "option") {
            node->attributes["value"] = value->IsNullOrUndefined()
                ? std::string{}
                : to_utf8(info.GetIsolate(), value);
            self->document.mark_dirty();
            return;
        }
        if (node->tag == "select") {
            const auto expected = value->IsNullOrUndefined()
                ? std::string{}
                : to_utf8(info.GetIsolate(), value);
            std::vector<dom_node*> options;
            collect_descendants_by_tag(*node, "option", options);
            bool matched = false;
            for (uint32_t index = 0; index < options.size(); ++index) {
                auto* option = options[index];
                if (!matched && option_value(*option) == expected) {
                    option->mutable_form_control().selectedness_initialized = true;
                    option->mutable_form_control().selectedness = true;
                    matched = true;
                } else {
                    option->mutable_form_control().selectedness_initialized = true;
                    option->mutable_form_control().selectedness = false;
                }
            }
            node->mutable_form_control().selection_explicitly_empty = !matched;
            self->document.mark_dirty();
            return;
        }
        if (node->tag == "progress" || node->tag == "meter") {
            node->attributes["value"] = to_utf8(info.GetIsolate(), value);
            self->document.mark_dirty();
            return;
        }
        node->mutable_form_control().value = value->IsNullOrUndefined()
            ? std::string{}
            : to_utf8(info.GetIsolate(), value);
        node->mutable_form_control().value_initialized = true;
        if (node->tag == "textarea") {
            node->mutable_form_control().dirty_value = true;
        }
        node->mutable_form_control().selection_start = std::min(node->mutable_form_control().selection_start, node->mutable_form_control().value.size());
        node->mutable_form_control().selection_end = std::min(node->mutable_form_control().selection_end, node->mutable_form_control().value.size());
        if (node->mutable_form_control().selection_start == node->mutable_form_control().selection_end) {
            node->mutable_form_control().selection_direction = text_selection_direction::none;
        }
        self->document.mark_dirty();
    }

    static void get_selection_start(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node != nullptr) {
            info.GetReturnValue().Set(v8::Integer::New(
                info.GetIsolate(), static_cast<int32_t>(node->mutable_form_control().selection_start)));
        }
    }

    static void set_selection_start(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        const auto index = value->Int32Value(info.GetIsolate()->GetCurrentContext()).FromMaybe(0);
        node->mutable_form_control().selection_start = std::min<size_t>(std::max(0, index), node->mutable_form_control().value.size());
        node->mutable_form_control().selection_end = std::max(node->mutable_form_control().selection_end, node->mutable_form_control().selection_start);
        if (node->mutable_form_control().selection_start == node->mutable_form_control().selection_end) {
            node->mutable_form_control().selection_direction = text_selection_direction::none;
        }
    }

    static void get_selection_end(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node != nullptr) {
            info.GetReturnValue().Set(v8::Integer::New(
                info.GetIsolate(), static_cast<int32_t>(node->mutable_form_control().selection_end)));
        }
    }

    static void set_selection_end(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        const auto index = value->Int32Value(info.GetIsolate()->GetCurrentContext()).FromMaybe(0);
        node->mutable_form_control().selection_end = std::min<size_t>(std::max(0, index), node->mutable_form_control().value.size());
        node->mutable_form_control().selection_start = std::min(node->mutable_form_control().selection_start, node->mutable_form_control().selection_end);
        if (node->mutable_form_control().selection_start == node->mutable_form_control().selection_end) {
            node->mutable_form_control().selection_direction = text_selection_direction::none;
        }
    }

    static void get_selection_direction(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node != nullptr) {
            const auto* direction =
                node->mutable_form_control().selection_direction == text_selection_direction::forward
                ? "forward"
                : node->mutable_form_control().selection_direction == text_selection_direction::backward
                    ? "backward"
                    : "none";
            info.GetReturnValue().Set(js_string(
                info.GetIsolate(), direction));
        }
    }

    static void set_selection_direction(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        const auto direction = to_utf8(info.GetIsolate(), value);
        node->mutable_form_control().selection_direction = direction == "forward"
            ? text_selection_direction::forward
            : direction == "backward"
                ? text_selection_direction::backward
                : text_selection_direction::none;
        if (node->mutable_form_control().selection_start == node->mutable_form_control().selection_end) {
            node->mutable_form_control().selection_direction = text_selection_direction::none;
        }
    }

    static void get_boolean_attribute(
        const v8::PropertyCallbackInfo<v8::Value>& info,
        const char* name)
    {
        auto* node = unwrap_node(info.Holder());
        info.GetReturnValue().Set(v8::Boolean::New(
            info.GetIsolate(),
            node != nullptr && node->attributes.contains(name)));
    }

    static void set_boolean_attribute(
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info,
        const char* name)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        if (value->BooleanValue(info.GetIsolate())) node->attributes[name] = std::string{};
        else node->attributes.erase(name);
        self->recascade_connected_subtree(*node);
    }

    static void get_checked(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr || node->tag != "input") return;
        info.GetReturnValue().Set(v8::Boolean::New(
            info.GetIsolate(),
            node->mutable_form_control().checkedness_initialized
                ? node->mutable_form_control().checkedness
                : node->attributes.contains("checked")));
    }

    static void set_checked(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr || node->tag != "input") return;
        node->mutable_form_control().checkedness_initialized = true;
        node->mutable_form_control().checkedness = value->BooleanValue(info.GetIsolate());
        self->document.mark_dirty();
    }

    static void get_selected(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr || node->tag != "option") return;
        info.GetReturnValue().Set(v8::Boolean::New(
            info.GetIsolate(), option_is_selected(*node)));
    }

    static void set_selected(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr || node->tag != "option") return;
        auto* select = containing_select(*node);
        if (!value->BooleanValue(info.GetIsolate())) {
            node->mutable_form_control().selectedness_initialized = true;
            node->mutable_form_control().selectedness = false;
            if (select != nullptr) {
                std::vector<dom_node*> options;
                collect_descendants_by_tag(*select, "option", options);
                select->mutable_form_control().selection_explicitly_empty = std::none_of(
                    options.begin(), options.end(), [](auto* option) {
                        return option != nullptr && option_is_selected(*option);
                    });
            }
            self->recascade_connected_subtree(*node, "style.setProperty-custom");
            return;
        }
        if (select != nullptr && !select->attributes.contains("multiple")) {
            const auto clear_selected = [&](const auto& recurse, dom_node& root) -> void {
                if (root.tag == "option") {
                    root.mutable_form_control().selectedness_initialized = true;
                    root.mutable_form_control().selectedness = false;
                }
                for (auto* child : root.children) {
                    if (child != nullptr) recurse(recurse, *child);
                }
            };
            clear_selected(clear_selected, *select);
        }
        node->mutable_form_control().selectedness_initialized = true;
        node->mutable_form_control().selectedness = true;
        if (select != nullptr) select->mutable_form_control().selection_explicitly_empty = false;
        self->recascade_connected_subtree(*node);
    }

    static void form_reset(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* form = unwrap_node(info.This());
        if (form == nullptr || form->tag != "form") return;
        const auto reset_subtree = [&](const auto& recurse, dom_node& node) -> void {
            if (node.tag == "select") node.mutable_form_control().selection_explicitly_empty = false;
            if (node.tag == "option") {
                node.mutable_form_control().selectedness_initialized = false;
                node.mutable_form_control().selectedness = false;
            }
            if (node.tag == "input" || node.tag == "textarea") {
                if (node.tag == "textarea") {
                    reset_textarea_value(node);
                } else {
                    const auto authored = node.attributes.find("value");
                    node.mutable_form_control().value = authored == node.attributes.end()
                        ? std::string{}
                        : authored->second;
                    node.mutable_form_control().value_initialized = true;
                    node.mutable_form_control().dirty_value = false;
                }
                if (node.tag == "input") {
                    node.mutable_form_control().checkedness_initialized = false;
                    node.mutable_form_control().checkedness = false;
                }
            }
            for (auto* child : node.children) {
                if (child != nullptr) recurse(recurse, *child);
            }
        };
        reset_subtree(reset_subtree, *form);
        self->document.mark_dirty();
    }

    static void get_multiple(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        get_boolean_attribute(info, "multiple");
    }

    static void set_multiple(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        set_boolean_attribute(value, info, "multiple");
    }

    static void get_disabled(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        get_boolean_attribute(info, "disabled");
    }

    static void set_disabled(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        set_boolean_attribute(value, info, "disabled");
    }

    static void get_element_url(v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        const auto name = to_utf8(info.GetIsolate(), property);
        const auto iterator = node->attributes.find(name);
        const auto url_reflected = (name == "href"
                && (node->tag == "a" || node->tag == "area" || node->tag == "link"))
            || (name == "src"
                && (node->tag == "audio" || node->tag == "embed"
                    || node->tag == "iframe" || node->tag == "img"
                    || node->tag == "input" || node->tag == "script"
                    || node->tag == "source" || node->tag == "track"
                    || node->tag == "video"));
        if (iterator != node->attributes.end() && url_reflected) {
            auto* self = current(info.GetIsolate());
            auto base = self->in_frame_context()
                ? self->frame_base_address : self->document_base_address;
            if (base.empty()) {
                auto local_context = info.GetIsolate()->GetCurrentContext();
                v8::Local<v8::Value> location;
                v8::Local<v8::Value> href;
                if (local_context->Global()->Get(
                        local_context,
                        js_string(info.GetIsolate(), "location")).ToLocal(&location)
                    && location->IsObject()
                    && location.As<v8::Object>()->Get(
                        local_context,
                        js_string(info.GetIsolate(), "href")).ToLocal(&href)
                    && !href->IsNullOrUndefined()) {
                    base = to_utf8(info.GetIsolate(), href);
                }
            }
            const auto resolved = resolve_resource_url(iterator->second, base);
            info.GetReturnValue().Set(js_string(info.GetIsolate(), resolved.c_str()));
            return;
        }
        info.GetReturnValue().Set(js_string(
            info.GetIsolate(),
            iterator == node->attributes.end() ? "" : iterator->second.c_str()));
    }

    static void set_element_src(v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        node->attributes["src"] = to_utf8(info.GetIsolate(), value);
        if (node->tag == "iframe") {
            self->enqueue_iframe_hydration_if_needed(*node);
            return;
        }
        if (node->tag != "img") return;
        node->clear_replaced_image();
        auto local_context = info.GetIsolate()->GetCurrentContext();
        info.Holder()->Set(
            local_context,
            js_string(info.GetIsolate(), "complete"),
            v8::False(info.GetIsolate())).Check();
        self->enqueue_connected_resource_if_needed(
            *node,
            info.Holder(),
            local_context);
    }

    static void set_element_href(v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node != nullptr) node->attributes["href"] = to_utf8(info.GetIsolate(), value);
    }

    static void get_anchor_hash(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        const auto iterator = node->attributes.find("href");
        if (iterator == node->attributes.end()) {
            info.GetReturnValue().Set(js_string(info.GetIsolate(), ""));
            return;
        }
        const auto fragment = iterator->second.find('#');
        const auto value = fragment == std::string::npos
                || fragment + 1U == iterator->second.size()
            ? std::string{}
            : iterator->second.substr(fragment);
        info.GetReturnValue().Set(js_string(info.GetIsolate(), value.c_str()));
    }

    static void set_anchor_hash(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        auto& href = node->attributes["href"];
        const auto fragment = href.find('#');
        if (fragment != std::string::npos) href.erase(fragment);
        auto next = to_utf8(info.GetIsolate(), value);
        if (!next.empty() && next.front() != '#') next.insert(next.begin(), '#');
        href += next;
        self->document.mark_dirty();
    }
