    bool execute(const std::string& source, const std::string& document_name)
    {
        auto isolate_locker = lock_shared_isolate();
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        auto local_context = context.Get(isolate);
        if (!execute_in_context(local_context, source, document_name, last_error)) {
            return false;
        }
        if (!drain_tasks()) {
            return false;
        }
        if (!promote_pending_promise_error()) return false;
        last_error.clear();
        return true;
    }

    bool load_url(const std::string& url)
    {
        std::string html;
        std::string resolved_url;
        if (!load_text_resource(
                url,
                {},
                WEBSCENE_RESOURCE_DOCUMENT,
                html,
                resolved_url)) {
            last_error = "Unable to load WebScene document: " + url;
            return false;
        }
        document_base_address = resolved_url;
        const auto stylesheets = parse_frame_stylesheets(html);
        const auto scripts = parse_frame_scripts(html);
        for (const auto& href : stylesheets) {
            start_resource_prefetch(
                href,
                document_base_address,
                WEBSCENE_RESOURCE_STYLESHEET);
        }
        for (const auto& script : scripts) {
            if (!script.source.empty()) {
                start_resource_prefetch(
                    script.source,
                    document_base_address,
                    WEBSCENE_RESOURCE_SCRIPT);
            }
        }

        auto isolate_locker = lock_shared_isolate();
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        auto local_context = context.Get(isolate);
        v8::Context::Scope context_scope(local_context);
        set_context_location(local_context, local_context->Global(), resolved_url);
        auto outer_document = document_object.Get(isolate);
        outer_document->Set(
            local_context,
            js_string(isolate, "readyState"),
            js_string(isolate, "loading")).Check();
        outer_document->Set(
            local_context,
            js_string(isolate, "currentScript"),
            v8::Null(isolate)).Check();

        auto& viewport_root = document.body();
        record_feature(
            "html",
            "element:body",
            "supported",
            {},
            "html-element");
        detach_all_children(viewport_root);
        reset_inspector_context_group_for_navigation(local_context, resolved_url);

#if defined(WEBSCENE_NATIVE_ENGINE_HTML5EVER)
        const auto parsed_document = parse_html_document(document, viewport_root, html);
        html_parse_duration_ns += parsed_document.duration_ns;
        html_parse_error_count += parsed_document.parse_error_count;
        html_parse_callback_count += parsed_document.callback_count;
        html_parse_element_count += parsed_document.element_count;
        html_parse_text_append_count += parsed_document.text_append_count;
        html_parse_comment_count += parsed_document.comment_count;
        html_parse_doctype_count += parsed_document.doctype_count;
        html_parse_rust_allocation_count += parsed_document.rust_allocation_count;
        html_parse_rust_peak_bytes = std::max(
            html_parse_rust_peak_bytes, parsed_document.rust_peak_bytes);
        html_parse_rust_retained_bytes += parsed_document.rust_retained_bytes;
        if (!parsed_document) {
            last_error = parsed_document.error.empty()
                ? "html5ever document parsing failed with status "
                    + std::to_string(parsed_document.status)
                : parsed_document.error;
            return false;
        }
        auto* html_element_pointer = static_cast<dom_node*>(nullptr);
        for (auto* child : viewport_root.children) {
            if (child != nullptr && child->tag == "html") {
                html_element_pointer = child;
                break;
            }
        }
        auto* head_element_pointer = static_cast<dom_node*>(nullptr);
        auto* body_pointer = static_cast<dom_node*>(nullptr);
        if (html_element_pointer != nullptr) {
            for (auto* child : html_element_pointer->children) {
                if (child == nullptr) continue;
                if (child->tag == "head") head_element_pointer = child;
                else if (child->tag == "body") body_pointer = child;
            }
        }
        if (html_element_pointer == nullptr
            || head_element_pointer == nullptr
            || body_pointer == nullptr) {
            last_error = "html5ever did not construct the required HTML/HEAD/BODY tree";
            return false;
        }
        auto& html_element = *html_element_pointer;
        auto& head_element = *head_element_pointer;
        auto& body = *body_pointer;
#else
        // Keep the native viewport root stable for scene/layout ownership, but
        // expose the browser-shaped HTML/HEAD/BODY tree to CSS and script. This
        // prevents BODY declarations from contaminating documentElement while
        // avoiding a second viewport/scroll owner in the native renderer.
        auto& html_element = document.create_element("html");
        auto& head_element = document.create_element("head");
        auto& body = document.create_element("body");
        html_element.style.width = {100, length_unit::percent};
        body.style.width = {100, length_unit::percent};
        document.append_child(viewport_root, html_element);
        document.append_child(html_element, head_element);
        document.append_child(html_element, body);
        record_feature(
            "html",
            "element:html",
            "supported",
            {},
            "html-element");

        document_object.Get(isolate)->SetAlignedPointerInInternalField(
            0,
            &html_element,
            v8::kEmbedderDataTypeTagDefault);
        local_context->SetAlignedPointerInEmbedderData(
            1,
            &html_element,
            v8::kEmbedderDataTypeTagDefault);

        const auto html_open = html.find("<html");
        const auto html_close = html_open == std::string::npos
            ? std::string::npos
            : html_tag_end(html, html_open);
        if (html_close != std::string::npos) {
            for (auto [attribute_name, value] : parse_html_attributes(
                std::string_view(html).substr(html_open, html_close - html_open + 1U),
                5U)) {
                const auto lower_name = lower_html_name(attribute_name);
                record_html_attribute_feature(lower_name, "html-parser");
                if (lower_name == "class") html_element.class_name = value;
                else if (lower_name == "id") html_element.id_attribute = value;
                html_element.attributes[lower_name] = std::move(value);
            }
        }
        const auto body_open = html.find("<body");
        const auto body_close = body_open == std::string::npos
            ? std::string::npos
            : html_tag_end(html, body_open);
        if (body_close != std::string::npos) {
            for (auto [attribute_name, value] : parse_html_attributes(
                std::string_view(html).substr(body_open, body_close - body_open + 1U),
                5U)) {
                const auto lower_name = lower_html_name(attribute_name);
                record_html_attribute_feature(lower_name, "html-parser");
                if (lower_name == "class") {
                    body.class_name += value;
                }
                else if (lower_name == "id") body.id_attribute = value;
                body.attributes[lower_name] = std::move(value);
            }
        }
        if (!html_element.class_name.empty()) {
            html_element.attributes["class"] = html_element.class_name;
        }
        if (!body.class_name.empty()) body.attributes["class"] = body.class_name;
#endif
        html_element.style.width = {100, length_unit::percent};
        body.style.width = {100, length_unit::percent};
        record_feature(
            "html",
            "element:html",
            "supported",
            {},
#if defined(WEBSCENE_NATIVE_ENGINE_HTML5EVER)
            "html5ever");
#else
            "html-element");
#endif
        record_feature(
            "html",
            "element:body",
            "supported",
            {},
#if defined(WEBSCENE_NATIVE_ENGINE_HTML5EVER)
            "html5ever");
#else
            "html-element");
#endif

        document_object.Get(isolate)->SetAlignedPointerInInternalField(
            0,
            &html_element,
            v8::kEmbedderDataTypeTagDefault);
        local_context->SetAlignedPointerInEmbedderData(
            1,
            &html_element,
            v8::kEmbedderDataTypeTagDefault);
        css_rules.clear();
        opacity_keyframes.clear();
        css_rules_by_class.clear();
        css_rules_by_id.clear();
        css_rules_by_tag.clear();
        css_rules_by_attribute.clear();
        css_focus_rules.clear();
        unindexed_css_rules.clear();
        css_attribute_dependencies.clear();
        hover_selector_dependencies.clear();
        css_variables.clear();
        important_css_variables.clear();

        for (auto stylesheet : parse_inline_stylesheets(html)) {
            add_stylesheet(std::move(stylesheet), document_base_address);
        }
        for (const auto& href : stylesheets) {
            std::string stylesheet;
            std::string stylesheet_url;
            if (!load_text_resource(
                    href,
                    document_base_address,
                    WEBSCENE_RESOURCE_STYLESHEET,
                    stylesheet,
                    stylesheet_url)) {
                last_error = "Unable to load document stylesheet: " + href;
                return false;
            }
            add_stylesheet(std::move(stylesheet), stylesheet_url);
        }

        apply_css_rules(html_element);
        apply_css_rules(head_element);
        apply_css_rules(body);
        const auto apply_inline_style_attribute = [&](dom_node& node) {
            const auto style = node.attributes.find("style");
            if (style == node.attributes.end()) return;
            uint64_t inline_property_mask = 0U;
            for (const auto& declaration : parse_css_declarations(style->second, true)) {
                inventory_css_value_functions(declaration.value, "inline-style-parser");
                if (!store_inline_declaration(node, declaration)) continue;
                apply_css_declaration(node, declaration);
                inline_property_mask |= inline_style_mask(declaration.name);
            }
            node.style.inline_property_mask |= inline_property_mask;
            detect_css_compositions(node);
        };
        apply_inline_style_attribute(html_element);
        apply_inline_style_attribute(body);

#if defined(WEBSCENE_NATIVE_ENGINE_HTML5EVER)
        activate_html5ever_subtree(html_element, false);
        recascade_connected_subtree(html_element);
#else
        auto body_html = extract_element_contents(html, "body");
        if (body_html.empty()) body_html = html;
        body_html = strip_html_elements(std::move(body_html), "script");
        body_html = strip_html_elements(std::move(body_html), "style");
        parse_inner_html(body, body_html);
        append_script_elements(body, scripts);
#endif

        const auto report_document_script_error = [&](const std::string& error) {
            std::lock_guard lock(console_message_mutex);
            constexpr size_t maximum_console_messages = 1024U;
            if (console_messages.size() >= maximum_console_messages) {
                console_messages.pop_front();
            }
            console_messages.push_back("error\n" + error);
        };
        const auto execute_group = [&](bool defer) {
            for (const auto& script : scripts) {
                if (script.defer != defer) continue;
                std::string source = script.code;
                std::shared_ptr<const immutable_text_source> immutable_source;
                auto name = resolved_url + "#inline-script-" + std::to_string(script.index + 1U);
                if (!script.source.empty()) {
                    if (!load_text_resource(
                            script.source,
                            document_base_address,
                            WEBSCENE_RESOURCE_SCRIPT,
                            source,
                            name,
                            &immutable_source)) {
                        frame_last_error_value = "Unable to load document script: " + script.source;
                        ++frame_script_error_count;
                        report_document_script_error(frame_last_error_value);
                        continue;
                    }
                }
                set_current_script(local_context, document_object.Get(isolate), name);
                std::string error;
                const auto script_source = immutable_source != nullptr
                    ? immutable_source->view()
                    : std::string_view(source);
                if (!execute_in_context(
                        local_context,
                        script_source,
                        name,
                        error,
                        true,
                        std::move(immutable_source))) {
                    frame_last_error_value = "Document script #" + std::to_string(script.index)
                        + " failed: " + error;
                    ++frame_script_error_count;
                    report_document_script_error(frame_last_error_value);
                    continue;
                }
                loaded_resource_names.push_back(resource_leaf_name(name));
                ++frame_script_execution_count;
            }
            return true;
        };
        if (!execute_group(false)) return false;
        outer_document->Set(
            local_context,
            js_string(isolate, "readyState"),
            js_string(isolate, "interactive")).Check();
        if (!execute_group(true)) return false;

        const auto dispatch_lifecycle_event = [&](
            v8::Local<v8::Object> target,
            const char* type) {
            auto event = create_event_instance(local_context);
            event->Set(
                local_context,
                js_string(isolate, "type"),
                js_string(isolate, type)).Check();
            event->Set(
                local_context,
                js_string(isolate, "bubbles"),
                v8::Boolean::New(
                    isolate,
                    std::string_view(type) == "DOMContentLoaded")).Check();
            event->Set(
                local_context,
                js_string(isolate, "cancelable"),
                v8::False(isolate)).Check();
            event->Set(
                local_context,
                js_string(isolate, "defaultPrevented"),
                v8::False(isolate)).Check();
            v8::Local<v8::Value> dispatcher_value;
            if (!target->Get(
                    local_context,
                    js_string(isolate, "dispatchEvent")).ToLocal(&dispatcher_value)
                || !dispatcher_value->IsFunction()) {
                frame_last_error_value =
                    std::string("Document ") + type + " dispatcher is unavailable";
                ++frame_script_error_count;
                report_document_script_error(frame_last_error_value);
                return false;
            }
            v8::TryCatch try_catch(isolate);
            v8::Local<v8::Value> arguments[] = {event};
            if (dispatcher_value.As<v8::Function>()->Call(
                    local_context,
                    target,
                    1,
                    arguments).IsEmpty()) {
                frame_last_error_value = std::string("Document ") + type
                    + " dispatch failed: "
                    + describe_exception(try_catch, local_context);
                ++frame_script_error_count;
                report_document_script_error(frame_last_error_value);
                return false;
            }
            perform_microtask_checkpoint();
            return true;
        };

        outer_document->Set(
            local_context,
            js_string(isolate, "currentScript"),
            v8::Null(isolate)).Check();
        if (!dispatch_lifecycle_event(outer_document, "DOMContentLoaded")) return false;
        outer_document->Set(
            local_context,
            js_string(isolate, "readyState"),
            js_string(isolate, "complete")).Check();
        if (!dispatch_lifecycle_event(local_context->Global(), "load")) return false;
        document.mark_dirty();
        last_error.clear();
        return true;
    }
