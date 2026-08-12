    static void dom_parser_parse(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        const auto xml = info.Length() > 0 ? to_utf8(info.GetIsolate(), info[0]) : std::string{};
        const auto mime_type = info.Length() > 1 ? to_utf8(info.GetIsolate(), info[1]) : std::string{};
        if (mime_type == "text/html") {
#if defined(WEBSCENE_NATIVE_ENGINE_HTML5EVER)
            auto& document_root = self->document.create_node(
                dom_node_kind::internal, "#document-root");
            document_root.visible = false;
            const auto parsed = parse_html_document(
                self->document, document_root, xml);
            self->html_parse_duration_ns += parsed.duration_ns;
            self->html_parse_error_count += parsed.parse_error_count;
            self->html_parse_callback_count += parsed.callback_count;
            self->html_parse_element_count += parsed.element_count;
            self->html_parse_text_append_count += parsed.text_append_count;
            self->html_parse_comment_count += parsed.comment_count;
            self->html_parse_doctype_count += parsed.doctype_count;
            self->html_parse_rust_allocation_count += parsed.rust_allocation_count;
            self->html_parse_rust_peak_bytes = std::max(
                self->html_parse_rust_peak_bytes, parsed.rust_peak_bytes);
            self->html_parse_rust_retained_bytes += parsed.rust_retained_bytes;
            auto* html = static_cast<dom_node*>(nullptr);
            for (auto* child : document_root.children) {
                if (child != nullptr && child->tag == "html") {
                    html = child;
                    break;
                }
            }
            if (!parsed || html == nullptr) {
                info.GetIsolate()->ThrowException(v8::Exception::Error(
                    js_string(info.GetIsolate(), "DOMParser HTML tree construction failed")));
                return;
            }
            self->activate_html5ever_subtree(*html, false);
            auto local_context = info.GetIsolate()->GetCurrentContext();
            self->detached_document_roots.insert(html->id);
            info.GetReturnValue().Set(wrap_detached_document(
                *self, *html, local_context, "about:blank"));
#else
            auto& html = self->document.create_element("html");
            auto& head = self->document.create_element("head");
            auto& body = self->document.create_element("body");
            self->document.append_child(html, head);
            self->document.append_child(html, body);
            self->parse_inner_html(body, xml);
            auto local_context = info.GetIsolate()->GetCurrentContext();
            self->detached_document_roots.insert(html.id);
            info.GetReturnValue().Set(wrap_detached_document(
                *self, html, local_context, "about:blank"));
#endif
            return;
        }
        auto& root = self->document.create_element("#document-root");
        root.visible = false;
        root.xml_mode = true;
        std::vector<dom_node*> stack{&root};
        size_t cursor = 0;
        size_t error_position = std::string::npos;
        std::string error_message;
        size_t root_element_count = 0;
        const auto skip_space = [&](size_t& position, size_t limit) {
            while (position < limit
                && std::isspace(static_cast<unsigned char>(xml[position]))) ++position;
        };
        const auto valid_name_start = [](unsigned char value) {
            return std::isalpha(value) || value == '_' || value == ':';
        };
        const auto valid_name_part = [&](unsigned char value) {
            return valid_name_start(value) || std::isdigit(value)
                || value == '-' || value == '.';
        };
        const auto fail = [&](size_t position, std::string message) {
            if (error_position == std::string::npos) {
                error_position = position;
                error_message = std::move(message);
            }
        };
        const auto append_text = [&](std::string value, bool cdata = false) {
            if (value.empty() || stack.empty()) return;
            if (stack.size() == 1U) {
                if (!std::all_of(value.begin(), value.end(), [](unsigned char character) {
                        return std::isspace(character);
                    })) {
                    fail(cursor, "text is not allowed outside the document element");
                }
                return;
            }
            auto& text_node = self->document.create_node(
                dom_node_kind::text, "#text");
            text_node.xml_mode = true;
            text_node.text_content = cdata
                ? std::move(value)
                : decode_html_text(std::move(value));
            self->document.append_child(*stack.back(), text_node);
        };
        while (cursor < xml.size() && error_position == std::string::npos) {
            const auto open = xml.find('<', cursor);
            if (open == std::string::npos) {
                append_text(xml.substr(cursor));
                cursor = xml.size();
                break;
            }
            if (open > cursor) append_text(xml.substr(cursor, open - cursor));
            if (error_position != std::string::npos) break;
            if (xml.compare(open, 4, "<!--") == 0) {
                const auto end = xml.find("-->", open + 4U);
                if (end == std::string::npos) {
                    fail(open, "unterminated comment");
                    break;
                }
                cursor = end + 3U;
                continue;
            }
            if (xml.compare(open, 9, "<![CDATA[") == 0) {
                if (stack.size() == 1U) {
                    fail(open, "CDATA is not allowed outside the document element");
                    break;
                }
                const auto end = xml.find("]]>", open + 9U);
                if (end == std::string::npos) {
                    fail(open, "unterminated CDATA section");
                    break;
                }
                append_text(xml.substr(open + 9U, end - open - 9U), true);
                cursor = end + 3U;
                continue;
            }
            if (xml.compare(open, 2, "<?") == 0) {
                const auto end = xml.find("?>", open + 2U);
                if (end == std::string::npos) {
                    fail(open, "unterminated processing instruction");
                    break;
                }
                cursor = end + 2U;
                continue;
            }
            const auto end = xml.find('>', open + 1U);
            if (end == std::string::npos) {
                fail(open, "unterminated markup");
                break;
            }
            if (xml.compare(open, 2, "<!") == 0) {
                cursor = end + 1U;
                continue;
            }
            if (xml.compare(open, 2, "</") == 0) {
                auto position = open + 2U;
                skip_space(position, end);
                const auto name_start = position;
                while (position < end
                    && valid_name_part(static_cast<unsigned char>(xml[position]))) ++position;
                const auto tag = xml.substr(name_start, position - name_start);
                skip_space(position, end);
                if (tag.empty() || position != end || stack.size() <= 1U
                    || stack.back()->tag != tag) {
                    fail(name_start, "mismatched closing tag");
                    break;
                }
                stack.pop_back();
                cursor = end + 1U;
                continue;
            }
            auto position = open + 1U;
            if (position >= end
                || !valid_name_start(static_cast<unsigned char>(xml[position]))) {
                fail(position, "invalid element name");
                break;
            }
            const auto name_start = position++;
            while (position < end
                && valid_name_part(static_cast<unsigned char>(xml[position]))) ++position;
            const auto tag = xml.substr(name_start, position - name_start);
            if (stack.size() == 1U && ++root_element_count > 1U) {
                fail(name_start, "multiple document elements");
                break;
            }
            auto& child = self->document.create_element(tag);
            child.xml_mode = true;
            child.set_namespace("");
            auto self_closing = false;
            while (position < end && error_position == std::string::npos) {
                skip_space(position, end);
                if (position >= end) break;
                if (xml[position] == '/') {
                    self_closing = true;
                    ++position;
                    skip_space(position, end);
                    if (position != end) fail(position, "unexpected content after '/'");
                    break;
                }
                if (!valid_name_start(static_cast<unsigned char>(xml[position]))) {
                    fail(position, "invalid attribute name");
                    break;
                }
                const auto attribute_start = position++;
                while (position < end
                    && valid_name_part(static_cast<unsigned char>(xml[position]))) ++position;
                const auto attribute_name = xml.substr(
                    attribute_start, position - attribute_start);
                skip_space(position, end);
                if (position >= end || xml[position] != '=') {
                    fail(position, "attribute value is required");
                    break;
                }
                ++position;
                skip_space(position, end);
                if (position >= end || (xml[position] != '\'' && xml[position] != '"')) {
                    fail(position, "attribute value must be quoted");
                    break;
                }
                const auto quote = xml[position++];
                const auto value_start = position;
                while (position < end && xml[position] != quote) ++position;
                if (position >= end) {
                    fail(value_start, "unterminated attribute value");
                    break;
                }
                auto attribute_value = decode_html_text(
                    xml.substr(value_start, position - value_start));
                ++position;
                child.attributes[attribute_name] = attribute_value;
                if (attribute_name == "xmlns") child.set_namespace(attribute_value);
            }
            if (error_position != std::string::npos) break;
            if (const auto id = child.attributes.find("id"); id != child.attributes.end()) {
                child.id_attribute = id->second;
            }
            if (const auto classes = child.attributes.find("class"); classes != child.attributes.end()) {
                child.class_name = classes->second;
            }
            self->document.append_child(*stack.back(), child);
            if (!self_closing) stack.push_back(&child);
            cursor = end + 1U;
        }
        if (error_position == std::string::npos && stack.size() != 1U) {
            fail(xml.size(), "unclosed element");
        }
        if (error_position == std::string::npos && root_element_count != 1U) {
            fail(0U, "document element is required");
        }
        auto* result_root = &root;
        if (error_position != std::string::npos) {
            auto& parser_root = self->document.create_element("#document-root");
            parser_root.visible = false;
            parser_root.xml_mode = true;
            auto& parser_error = self->document.create_element("parsererror");
            parser_error.xml_mode = true;
            parser_error.set_namespace("");
            auto& message = self->document.create_node(dom_node_kind::text, "#text");
            message.xml_mode = true;
            const auto line = static_cast<size_t>(std::count(
                xml.begin(), xml.begin() + std::min(error_position, xml.size()), '\n')) + 1U;
            const auto previous_newline = error_position == 0U
                ? std::string::npos
                : xml.rfind('\n', std::min(error_position, xml.size()) - 1U);
            const auto column = error_position
                - (previous_newline == std::string::npos ? 0U : previous_newline + 1U) + 1U;
            message.text_content = "XML Parsing Error: " + error_message
                + " at line " + std::to_string(line)
                + " column " + std::to_string(column);
            self->document.append_child(parser_error, message);
            self->document.append_child(parser_root, parser_error);
            result_root = &parser_root;
        }
        auto local_context = info.GetIsolate()->GetCurrentContext();
        self->detached_document_roots.insert(result_root->id);
        info.GetReturnValue().Set(wrap_detached_document(
            *self, *result_root, local_context, "about:blank"));
    }

    static void parsed_document_get_elements(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = info.Data()->IsExternal()
            ? static_cast<dom_node*>(info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault))
            : nullptr;
        if (root == nullptr) return;
        const auto tag = info.Length() > 0 ? to_utf8(info.GetIsolate(), info[0]) : std::string{};
        info.GetReturnValue().Set(self->selector_results(*root, tag));
    }

    static void parsed_document_query_selector_all(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = info.Data()->IsExternal()
            ? static_cast<dom_node*>(info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault))
            : nullptr;
        if (root == nullptr) return;
        const auto selector = info.Length() > 0 ? to_utf8(info.GetIsolate(), info[0]) : std::string{};
        info.GetReturnValue().Set(self->selector_results(*root, selector));
    }

    static void parsed_document_query_selector(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = info.Data()->IsExternal()
            ? static_cast<dom_node*>(info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault))
            : nullptr;
        if (root == nullptr) return;
        const auto selector = info.Length() > 0 ? to_utf8(info.GetIsolate(), info[0]) : std::string{};
        auto* match = self->query_selector_node(*root, selector, true);
        info.GetReturnValue().Set(match == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*match)));
    }

    static void return_this(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        info.GetReturnValue().Set(info.This());
    }

    static void dom_matrix_scale_self(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        const auto isolate = info.GetIsolate();
        const auto context = isolate->GetCurrentContext();
        const auto scale_x = info.Length() > 0
            ? info[0]->NumberValue(context).FromMaybe(1)
            : 1;
        const auto scale_y = info.Length() > 1
            ? info[1]->NumberValue(context).FromMaybe(scale_x)
            : scale_x;
        const auto a = object_number(isolate, context, info.This(), "a", 1) * scale_x;
        const auto b = object_number(isolate, context, info.This(), "b", 0) * scale_x;
        const auto c = object_number(isolate, context, info.This(), "c", 0) * scale_y;
        const auto d = object_number(isolate, context, info.This(), "d", 1) * scale_y;
        set_object_number(isolate, context, info.This(), "a", a);
        set_object_number(isolate, context, info.This(), "b", b);
        set_object_number(isolate, context, info.This(), "c", c);
        set_object_number(isolate, context, info.This(), "d", d);
        set_object_number(isolate, context, info.This(), "m11", a);
        set_object_number(isolate, context, info.This(), "m12", b);
        set_object_number(isolate, context, info.This(), "m21", c);
        set_object_number(isolate, context, info.This(), "m22", d);
        info.GetReturnValue().Set(info.This());
    }

    static void dom_matrix_constructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        auto result = info.IsConstructCall() ? info.This() : v8::Object::New(isolate);
        const char* ones[] = {"a", "d", "m11", "m22", "m33", "m44"};
        for (const auto* name : ones) {
            result->Set(local_context, js_string(isolate, name), v8::Number::New(isolate, 1)).Check();
        }
        const char* zeros[] = {"b", "c", "e", "f", "m12", "m13", "m14", "m21", "m23", "m24", "m31", "m32", "m34", "m41", "m42", "m43"};
        for (const auto* name : zeros) {
            result->Set(local_context, js_string(isolate, name), v8::Number::New(isolate, 0)).Check();
        }
        result->Set(local_context, js_string(isolate, "is2D"), v8::True(isolate)).Check();
        result->Set(local_context, js_string(isolate, "isIdentity"), v8::True(isolate)).Check();
        const char* methods[] = {
            "multiply", "preMultiplySelf", "multiplySelf", "translate", "translateSelf", "scale", "scale3d",
            "rotate", "rotateAxisAngle", "rotateSelf", "skewX", "skewY", "invertSelf", "inverse"};
        for (const auto* name : methods) {
            result->Set(local_context, js_string(isolate, name), v8::Function::New(local_context, return_this).ToLocalChecked()).Check();
        }
        result->Set(
            local_context,
            js_string(isolate, "scaleSelf"),
            v8::Function::New(local_context, dom_matrix_scale_self).ToLocalChecked()).Check();
        info.GetReturnValue().Set(result);
    }

    static layout_rect transformed_client_bounds(
        const native_document& document,
        const dom_node& node)
    {
        struct point final { double x; double y; };
        std::array<point, 4> corners{{
            {node.layout.x, node.layout.y},
            {node.layout.x + node.layout.width, node.layout.y},
            {node.layout.x + node.layout.width, node.layout.y + node.layout.height},
            {node.layout.x, node.layout.y + node.layout.height}}};
        const auto resolve_origin = [](css_length value, float available) {
            if (value.unit == length_unit::percent) {
                return static_cast<double>(available) * value.value / 100.0
                    + value.pixel_offset;
            }
            return static_cast<double>(value.value);
        };
        constexpr double degrees_to_radians = 0.017453292519943295;
        for (auto* current = &node; current != nullptr;
             current = document.composed_parent(*current)) {
            const auto origin_x = static_cast<double>(current->layout.x)
                + resolve_origin(current->style.transform_origin_x, current->layout.width);
            const auto origin_y = static_cast<double>(current->layout.y)
                + resolve_origin(current->style.transform_origin_y, current->layout.height);
            const auto radians = static_cast<double>(
                current->painted_transform_rotation_value()) * degrees_to_radians;
            const auto cosine = std::cos(radians);
            const auto sine = std::sin(radians);
            for (auto& corner : corners) {
                const auto scaled_x = (corner.x - origin_x)
                    * current->painted_transform_scale_x_value();
                const auto scaled_y = (corner.y - origin_y)
                    * current->painted_transform_scale_y_value();
                corner.x = origin_x + scaled_x * cosine - scaled_y * sine;
                corner.y = origin_y + scaled_x * sine + scaled_y * cosine;
            }
        }
        auto left = corners[0].x;
        auto right = corners[0].x;
        auto top = corners[0].y;
        auto bottom = corners[0].y;
        for (const auto& corner : corners) {
            left = std::min(left, corner.x);
            right = std::max(right, corner.x);
            top = std::min(top, corner.y);
            bottom = std::max(bottom, corner.y);
        }
        return {
            static_cast<float>(left),
            static_cast<float>(top),
            static_cast<float>(right - left),
            static_cast<float>(bottom - top)};
    }

    static void get_bounding_client_rect(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_geometry));
        self->ensure_layout();
        auto* node = unwrap_node(info.This());
        if (node == nullptr) return;
        const auto has_no_box = !self->is_connected(*node) || [self](const dom_node* candidate) {
            for (auto* current = candidate; current != nullptr;
                 current = self->document.composed_parent(*current)) {
                if (current->style.display == display_mode::none) return true;
            }
            return false;
        }(node);
        // The native tree keeps the live HTML document element beneath the
        // viewport-bearing BODY root.  Geometry exposed to script must match
        // the browser box for that live document element, not the internal
        // zero-height wrapper used by the collapsed native document model.
        if (node->tag == "html" && node->parent == &self->document.body()) {
            node = &self->document.body();
        }
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto result = v8::Object::New(info.GetIsolate());
        const auto set = [&](const char* name, double value) {
            result->Set(local_context, js_string(info.GetIsolate(), name), v8::Number::New(info.GetIsolate(), value)).Check();
        };
        const auto bounds = has_no_box
            ? layout_rect{}
            : transformed_client_bounds(self->document, *node);
        set("x", bounds.x);
        set("y", bounds.y);
        set("left", bounds.x);
        set("top", bounds.y);
        set("width", bounds.width);
        set("height", bounds.height);
        set("right", bounds.x + bounds.width);
        set("bottom", bounds.y + bounds.height);
        info.GetReturnValue().Set(result);
    }

    static void get_client_rects(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_geometry));
        self->ensure_layout();
        auto* node = unwrap_node(info.This());
        if (node == nullptr) return;
        if (!self->is_connected(*node)) {
            info.GetReturnValue().Set(v8::Array::New(info.GetIsolate()));
            return;
        }
        for (auto* current = node; current != nullptr;
             current = self->document.composed_parent(*current)) {
            if (current->style.display != display_mode::none) continue;
            info.GetReturnValue().Set(v8::Array::New(info.GetIsolate()));
            return;
        }
        if (node->tag == "html" && node->parent == &self->document.body()) {
            node = &self->document.body();
        }
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto rect = v8::Object::New(info.GetIsolate());
        const auto set = [&](const char* name, double value) {
            rect->Set(local_context, js_string(info.GetIsolate(), name), v8::Number::New(info.GetIsolate(), value)).Check();
        };
        const auto bounds = transformed_client_bounds(self->document, *node);
        set("x", bounds.x);
        set("y", bounds.y);
        set("left", bounds.x);
        set("top", bounds.y);
        set("width", bounds.width);
        set("height", bounds.height);
        set("right", bounds.x + bounds.width);
        set("bottom", bounds.y + bounds.height);
        auto result = v8::Array::New(info.GetIsolate(), 1);
        result->Set(local_context, 0, rect).Check();
        info.GetReturnValue().Set(result);
    }

    static void set_attribute(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_mutation));
        auto* node = unwrap_node(info.This());
        if (node == nullptr || info.Length() < 2) return;
        const auto raw_name = to_utf8(info.GetIsolate(), info[0]);
        const auto name = node->xml_mode ? raw_name : lower_html_name(raw_name);
        const auto value = to_wtf8(info.GetIsolate(), info[1]);
        const auto previous = node->attributes.find(name);
        const std::optional<std::string> old_value = previous == node->attributes.end()
            ? std::nullopt
            : std::optional<std::string>(previous->second);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (self->resize_profile_active) {
            ++self->last_resize_attribute_counts[name];
        }
#endif
        self->record_html_attribute_feature(name, "setAttribute");
        if (name == "class"
            && node->class_name == value
            && node->attributes.contains("class")) {
            self->notify_custom_element_attribute(*node, name, old_value, value);
            return;
        }
        node->attributes[name] = value;
        if (name == "id") node->id_attribute = value;
        else if (name == "class") node->class_name = value;
        else if (name == "value" && node->tag == "input") {
            auto& control = node->mutable_form_control();
            const auto previous_default = old_value.value_or(std::string{});
            if (!control.value_initialized || control.value == previous_default) {
                control.value = value;
                control.value_initialized = true;
                control.selection_start = value.size();
                control.selection_end = value.size();
                control.selection_direction = text_selection_direction::none;
            }
        }
        else if (name == "style") {
            node->clear_authored_style();
            node->style.clear_grid();
            node->style.clear_table();
            node->style.inline_property_mask = 0U;
            node->style.important_property_mask = 0U;
            uint64_t inline_property_mask = 0U;
            for (const auto& declaration : parse_css_declarations(value, true)) {
                if (!store_inline_declaration(*node, declaration)) continue;
                self->apply_css_declaration(*node, declaration);
                inline_property_mask |= inline_style_mask(declaration.name);
            }
            node->style.inline_property_mask |= inline_property_mask;
        }
        else if (name == "src" && node->tag == "iframe") {
            self->enqueue_iframe_hydration_if_needed(*node);
        }
        else if (name == "src" && node->tag == "img") {
            node->clear_replaced_image();
            self->enqueue_connected_resource_if_needed(
                *node,
                info.This(),
                info.GetIsolate()->GetCurrentContext());
        }
        if (self->attribute_requires_recascade(name)) {
            self->recascade_connected_subtree(*node, "style.setProperty-clear");
        } else {
            self->document.mark_dirty();
        }
        self->notify_custom_element_attribute(*node, name, old_value, value);
    }

    static void set_attribute_ns(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (info.Length() < 3) return;
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (node == nullptr) return;
        const auto raw_name = to_utf8(info.GetIsolate(), info[1]);
        const auto name = node->xml_mode ? raw_name : lower_html_name(raw_name);
        const std::optional<std::string> namespace_uri =
            info[0]->IsNullOrUndefined()
                ? std::nullopt
                : std::optional<std::string>(to_wtf8(info.GetIsolate(), info[0]));
        const auto value = to_wtf8(info.GetIsolate(), info[2]);
        const auto previous = node->attributes.find(name);
        const std::optional<std::string> old_value = previous == node->attributes.end()
            ? std::nullopt
            : std::optional<std::string>(previous->second);
        self->record_html_attribute_feature(name, "setAttributeNS");
        node->attributes[name] = value;
        if (name == "id") node->id_attribute = value;
        else if (name == "class") node->class_name = value;
        if (self->attribute_requires_recascade(name)) {
            self->recascade_connected_subtree(*node, "style.removeProperty-custom");
        } else {
            self->document.mark_dirty();
        }
        self->notify_custom_element_attribute(
            *node, name, old_value, value, namespace_uri);
    }

    static void remove_attribute(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (node == nullptr || info.Length() < 1) return;
        const auto raw_name = to_utf8(info.GetIsolate(), info[0]);
        const auto name = node->xml_mode ? raw_name : lower_html_name(raw_name);
        self->record_html_attribute_feature(name, "removeAttribute");
        if (!node->attributes.contains(name)) {
            return;
        }
        const auto old_value = std::optional<std::string>(node->attributes.at(name));
        self->detach_attr(*node, name, *old_value);
        node->attributes.erase(name);
        if (name == "id") node->id_attribute.clear();
        else if (name == "class") node->class_name.clear();
        else if (name == "src" && node->tag == "img") {
            node->clear_replaced_image();
        }
        else if (name == "style") {
            node->clear_authored_style();
            node->style.inline_property_mask = 0U;
            node->style.important_property_mask = 0U;
        }
        if (self->attribute_requires_recascade(name)) {
            self->recascade_connected_subtree(*node);
        } else {
            self->document.mark_dirty();
        }
        self->notify_custom_element_attribute(
            *node, name, old_value, std::nullopt);
    }

    static void get_attribute(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_property));
        auto* node = unwrap_node(info.This());
        if (node == nullptr || info.Length() < 1) return;
        const auto raw_name = to_utf8(info.GetIsolate(), info[0]);
        const auto name = node->xml_mode ? raw_name : lower_html_name(raw_name);
        const auto match = node->attributes.find(name);
        if (match == node->attributes.end()) {
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
        } else {
            info.GetReturnValue().Set(js_dom_string(info.GetIsolate(), match->second));
        }
    }

    static void get_attribute_ns(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_property));
        auto* node = unwrap_node(info.This());
        if (node == nullptr || info.Length() < 2) return;
        const auto raw_name = to_utf8(info.GetIsolate(), info[1]);
        const auto name = node->xml_mode ? raw_name : lower_html_name(raw_name);
        const auto match = node->attributes.find(name);
        if (match == node->attributes.end()) {
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
        } else {
            info.GetReturnValue().Set(js_dom_string(info.GetIsolate(), match->second));
        }
    }

    static void remove_attribute_ns(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (node == nullptr || info.Length() < 2) return;
        const auto raw_name = to_utf8(info.GetIsolate(), info[1]);
        const auto name = node->xml_mode ? raw_name : lower_html_name(raw_name);
        const std::optional<std::string> namespace_uri =
            info[0]->IsNullOrUndefined()
                ? std::nullopt
                : std::optional<std::string>(to_wtf8(info.GetIsolate(), info[0]));
        self->record_html_attribute_feature(name, "removeAttributeNS");
        const auto match = node->attributes.find(name);
        if (match == node->attributes.end()) return;
        const auto old_value = std::optional<std::string>(match->second);
        self->detach_attr(*node, name, *old_value);
        node->attributes.erase(name);
        if (name == "id") node->id_attribute.clear();
        else if (name == "class") node->class_name.clear();
        else if (name == "src" && node->tag == "img") node->clear_replaced_image();
        else if (name == "style") {
            node->clear_authored_style();
            node->style.inline_property_mask = 0U;
            node->style.important_property_mask = 0U;
        }
        if (self->attribute_requires_recascade(name)) {
            self->recascade_connected_subtree(*node);
        } else {
            self->document.mark_dirty();
        }
        self->notify_custom_element_attribute(
            *node, name, old_value, std::nullopt, namespace_uri);
    }

    static void toggle_attribute(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (node == nullptr || info.Length() < 1) return;
        const auto raw_name = to_utf8(info.GetIsolate(), info[0]);
        const auto name = node->xml_mode ? raw_name : lower_html_name(raw_name);
        const bool present = node->attributes.contains(name);
        const bool force_present = info.Length() < 2 || info[1]->IsUndefined()
            ? !present
            : info[1]->BooleanValue(info.GetIsolate());
        if (force_present == present) {
            info.GetReturnValue().Set(present);
            return;
        }
        if (force_present) {
            node->attributes[name] = {};
            if (name == "id") node->id_attribute.clear();
            else if (name == "class") node->class_name.clear();
            self->notify_custom_element_attribute(
                *node, name, std::nullopt, std::string{});
        } else {
            const auto old_value = std::optional<std::string>(node->attributes.at(name));
            node->attributes.erase(name);
            if (name == "id") node->id_attribute.clear();
            else if (name == "class") node->class_name.clear();
            self->notify_custom_element_attribute(
                *node, name, old_value, std::nullopt);
        }
        if (self->attribute_requires_recascade(name)) {
            self->recascade_connected_subtree(*node);
        } else {
            self->document.mark_dirty();
        }
        info.GetReturnValue().Set(force_present);
    }

    static void has_attribute(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_property));
        auto* node = unwrap_node(info.This());
        const auto raw_name = node != nullptr && info.Length() > 0
            ? to_utf8(info.GetIsolate(), info[0])
            : std::string{};
        const auto name = node != nullptr && !node->xml_mode
            ? lower_html_name(raw_name)
            : raw_name;
        info.GetReturnValue().Set(v8::Boolean::New(
            info.GetIsolate(),
            node != nullptr && node->attributes.contains(name)));
    }

    static std::string extract_html_attribute(const std::string& html, const std::string& name)
    {
        const auto marker = name + "=";
        const auto position = html.find(marker);
        if (position == std::string::npos) return {};
        auto value_start = position + marker.size();
        if (value_start >= html.size()) return {};
        const auto quote = html[value_start];
        if (quote != '\'' && quote != '"') return {};
        ++value_start;
        const auto value_end = html.find(quote, value_start);
        return value_end == std::string::npos
            ? std::string{}
            : html.substr(value_start, value_end - value_start);
    }

    static std::vector<frame_script> parse_frame_scripts(const std::string& html)
    {
        std::vector<frame_script> result;
        size_t cursor = 0;
        size_t index = 0;
        while (true) {
            const auto open = html.find("<script", cursor);
            if (open == std::string::npos) break;
            const auto open_end = html.find('>', open);
            if (open_end == std::string::npos) break;
            const auto close = html.find("</script>", open_end + 1U);
            if (close == std::string::npos) break;
            const auto attributes = html.substr(open, open_end - open + 1U);
            result.push_back(frame_script{
                extract_html_attribute(attributes, "src"),
                html.substr(open_end + 1U, close - open_end - 1U),
                attributes.find("defer") != std::string::npos,
                index++});
            cursor = close + 9U;
        }
        return result;
    }

    void append_script_elements(dom_node& parent, const std::vector<frame_script>& scripts)
    {
        for (const auto& script : scripts) {
            record_feature(
                "html",
                "element:script",
                "supported",
                {},
                "html-element");
            auto& element = document.create_element("script");
            element.script_state = script_execution_state::already_started;
            if (!script.source.empty()) {
                record_html_attribute_feature("src", "html-parser");
                element.attributes["src"] = script.source;
            }
            if (script.defer) {
                record_html_attribute_feature("defer", "html-parser");
                element.attributes["defer"] = std::string{};
            }
            element.text_content = script.code;
            apply_css_rules(element);
            document.append_child(parent, element);
        }
    }

    static std::vector<std::string> parse_inline_stylesheets(const std::string& html)
    {
        std::vector<std::string> result;
        size_t cursor = 0U;
        while (true) {
            const auto open = html.find("<style", cursor);
            if (open == std::string::npos) break;
            const auto open_end = html.find('>', open);
            if (open_end == std::string::npos) break;
            const auto close = html.find("</style>", open_end + 1U);
            if (close == std::string::npos) break;
            result.push_back(html.substr(open_end + 1U, close - open_end - 1U));
            cursor = close + 8U;
        }
        return result;
    }

    static std::string extract_element_contents(const std::string& html, const std::string& tag)
    {
        const auto open = html.find("<" + tag);
        if (open == std::string::npos) return {};
        const auto open_end = html.find('>', open);
        if (open_end == std::string::npos) return {};
        const auto close = html.find("</" + tag + ">", open_end + 1U);
        return close == std::string::npos
            ? std::string{}
            : html.substr(open_end + 1U, close - open_end - 1U);
    }

    static std::string strip_html_elements(std::string html, const std::string& tag)
    {
        size_t cursor = 0U;
        const auto opening = "<" + tag;
        const auto closing = "</" + tag + ">";
        while (true) {
            const auto open = html.find(opening, cursor);
            if (open == std::string::npos) break;
            const auto close = html.find(closing, open + opening.size());
            if (close == std::string::npos) {
                html.erase(open);
                break;
            }
            html.erase(open, close + closing.size() - open);
            cursor = open;
        }
        return html;
    }

    void install_screen(
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Object> global)
    {
        auto screen_template = v8::FunctionTemplate::New(isolate);
        screen_template->SetClassName(js_string(isolate, "Screen"));
        screen_template->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "width"),
            get_screen_width);
        screen_template->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "height"),
            get_screen_height);
        screen_template->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "availWidth"),
            get_screen_width);
        screen_template->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "availHeight"),
            get_screen_height);
        screen_template->InstanceTemplate()->Set(
            js_string(isolate, "colorDepth"),
            v8::Integer::New(isolate, 24),
            v8::PropertyAttribute::ReadOnly);
        screen_template->InstanceTemplate()->Set(
            js_string(isolate, "pixelDepth"),
            v8::Integer::New(isolate, 24),
            v8::PropertyAttribute::ReadOnly);
        auto screen_constructor =
            screen_template->GetFunction(local_context).ToLocalChecked();
        auto screen = screen_constructor->NewInstance(local_context).ToLocalChecked();
        global->Set(
            local_context,
            js_string(isolate, "Screen"),
            screen_constructor).Check();
        global->Set(
            local_context,
            js_string(isolate, "screen"),
            screen).Check();
    }

    void set_context_location(
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Object> global,
        const std::string& href)
    {
        auto location = v8::Object::New(isolate);
        const auto scheme_end = href.find(':');
        const auto authority_start = href.find("://");
        const auto authority_end = authority_start == std::string::npos
            ? std::string::npos
            : href.find_first_of("/?#", authority_start + 3U);
        const auto host = authority_start == std::string::npos
            ? std::string{}
            : href.substr(
                authority_start + 3U,
                (authority_end == std::string::npos ? href.size() : authority_end)
                    - authority_start - 3U);
        const auto query_start = href.find(
            '?',
            authority_end == std::string::npos ? 0U : authority_end);
        const auto hash_start = href.find(
            '#',
            authority_end == std::string::npos ? 0U : authority_end);
        const auto path_end = std::min(
            query_start == std::string::npos ? href.size() : query_start,
            hash_start == std::string::npos ? href.size() : hash_start);
        const auto path = authority_end == std::string::npos
            || authority_end >= path_end
            || href[authority_end] != '/'
            ? std::string("/")
            : href.substr(authority_end, path_end - authority_end);
        const auto search = query_start == std::string::npos
            ? std::string{}
            : href.substr(
                query_start,
                (hash_start == std::string::npos ? href.size() : hash_start)
                    - query_start);
        const auto hash = hash_start == std::string::npos
            ? std::string{}
            : href.substr(hash_start);
        location->Set(local_context, js_string(isolate, "href"), js_string(isolate, href.c_str())).Check();
        location->Set(
            local_context,
            js_string(isolate, "protocol"),
            js_string(isolate, scheme_end == std::string::npos ? "" : href.substr(0U, scheme_end + 1U).c_str())).Check();
        location->Set(local_context, js_string(isolate, "host"), js_string(isolate, host.c_str())).Check();
        const auto port_separator = host.find_last_of(':');
        const auto has_port = port_separator != std::string::npos
            && host.find(']') == std::string::npos;
        const auto hostname = has_port ? host.substr(0U, port_separator) : host;
        const auto port = has_port ? host.substr(port_separator + 1U) : std::string{};
        location->Set(
            local_context,
            js_string(isolate, "hostname"),
            js_string(isolate, hostname.c_str())).Check();
        location->Set(
            local_context,
            js_string(isolate, "port"),
            js_string(isolate, port.c_str())).Check();
        const auto origin = scheme_end == std::string::npos || host.empty()
            ? std::string("null")
            : href.substr(0U, scheme_end + 1U) + "//" + host;
        location->Set(
            local_context,
            js_string(isolate, "origin"),
            js_string(isolate, origin.c_str())).Check();
        location->Set(local_context, js_string(isolate, "pathname"), js_string(isolate, path.c_str())).Check();
        location->Set(local_context, js_string(isolate, "search"), js_string(isolate, search.c_str())).Check();
        location->Set(local_context, js_string(isolate, "hash"), js_string(isolate, hash.c_str())).Check();
        location->Set(
            local_context,
            js_string(isolate, "toString"),
            v8::Function::New(local_context, location_to_string).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "location"), location).Check();
        v8::Local<v8::Value> document_value;
        if (global->Get(
                local_context,
                js_string(isolate, "document")).ToLocal(&document_value)
            && document_value->IsObject()) {
            set_document_address(local_context, document_value.As<v8::Object>(), href);
        }
    }

    void set_document_address(
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Object> document_value,
        const std::string& href)
    {
        // Document.URL is the browser-facing serialization used by the hosted
        // hosted analytics branch. documentURI and baseURI share the
        // fetched document address in this profile because WebScene does not
        // currently implement authored <base> mutation.
        for (const auto* property : {"URL", "documentURI", "baseURI"}) {
            document_value->Set(
                local_context,
                js_string(isolate, property),
                js_string(isolate, href.c_str())).Check();
        }
    }

    void set_current_script(
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Object> document_value,
        const std::string& script_url)
    {
        auto current_script = v8::Object::New(isolate);
        current_script->Set(
            local_context,
            js_string(isolate, "src"),
            js_string(isolate, script_url.c_str())).Check();
        current_script->Set(
            local_context,
            js_string(isolate, "tagName"),
            js_string(isolate, "SCRIPT")).Check();
        current_script->Set(
            local_context,
            js_string(isolate, "nonce"),
            js_string(isolate, "")).Check();
        current_script->Set(
            local_context,
            js_string(isolate, "getAttribute"),
            v8::Function::New(local_context, current_script_get_attribute).ToLocalChecked()).Check();
        document_value->Set(local_context, js_string(isolate, "currentScript"), current_script).Check();
    }

    static bool read_text_file(const std::filesystem::path& path, std::string& result)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) return false;
        const auto end = stream.tellg();
        if (end < 0) return false;
        result.resize(static_cast<size_t>(end));
        stream.seekg(0, std::ios::beg);
        if (!result.empty()) {
            stream.read(result.data(), static_cast<std::streamsize>(result.size()));
        }
        return stream.good() || stream.eof();
    }

    bool profiled_read_text_file(const std::filesystem::path& path, std::string& result)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        binding_callback_timer timer(profile_startup ? &startup_resource_read : nullptr);
#endif
        return read_text_file(path, result);
    }
