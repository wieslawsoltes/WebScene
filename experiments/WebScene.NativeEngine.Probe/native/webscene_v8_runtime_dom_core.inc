    std::string describe_exception(
        v8::TryCatch& try_catch,
        v8::Local<v8::Context> exception_context = {})
    {
        std::ostringstream result;
        result << to_utf8(isolate, try_catch.Exception());
        auto message = try_catch.Message();
        if (!message.IsEmpty()) {
            result << " at " << to_utf8(isolate, message->GetScriptOrigin().ResourceName());
            auto local_context = exception_context.IsEmpty()
                ? isolate->GetCurrentContext()
                : exception_context;
            const auto line = message->GetLineNumber(local_context).FromMaybe(0);
            if (line > 0) result << ':' << line;
        }
        auto stack = try_catch.StackTrace(exception_context.IsEmpty()
            ? isolate->GetCurrentContext()
            : exception_context);
        v8::Local<v8::Value> stack_value;
        if (stack.ToLocal(&stack_value) && stack_value->IsString()) {
            const auto stack_text = to_utf8(isolate, stack_value);
            if (!stack_text.empty()) result << "\n" << stack_text;
        }
        return result.str();
    }

    void ensure_layout(const dom_node* reusable_client_geometry_subject = nullptr)
    {
        flush_pending_style_recascades();
        if (!document.dirty()) return;
        if (reusable_client_geometry_subject != nullptr
            && document.can_reuse_client_geometry(
                *reusable_client_geometry_subject)) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            ++client_geometry_layout_reuse_count;
#endif
            return;
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        binding_callback_timer timer(profile_startup ? &startup_layout : nullptr);
        auto* forced_layout_stats = static_cast<binding_callback_stats*>(nullptr);
        if (profile_bindings) {
            auto subject = std::string{"unscoped"};
            if (reusable_client_geometry_subject != nullptr) {
                subject = reusable_client_geometry_subject->tag;
                if (!reusable_client_geometry_subject->id_attribute.empty()) {
                    subject += '#';
                    subject += reusable_client_geometry_subject->id_attribute;
                }
                if (!reusable_client_geometry_subject->class_name.empty()) {
                    subject += '.';
                    subject += reusable_client_geometry_subject->class_name.substr(0, 96);
                }
            }
            forced_layout_stats = &startup_forced_layout_profiles[std::move(subject)];
        }
        binding_callback_timer forced_layout_timer(forced_layout_stats);
#endif
        const auto [width, height, device_scale_factor, preferred_color_scheme] =
            viewport_provider();
        static_cast<void>(device_scale_factor);
        static_cast<void>(preferred_color_scheme);
        document.layout(width, height);
    }

    void refresh_all_shadow_distributions()
    {
        document.refresh_shadow_distributions();
    }

    static void throw_dom_exception(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        const char* message,
        const char* name)
    {
        auto* isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        auto exception = v8::Object::New(isolate);
        exception->Set(
            local_context, js_string(isolate, "message"), js_string(isolate, message)).Check();
        exception->Set(
            local_context, js_string(isolate, "name"), js_string(isolate, name)).Check();
        v8::Local<v8::Value> constructor;
        if (local_context->Global()->Get(
                local_context, js_string(isolate, "DOMException"))
                .ToLocal(&constructor)
            && constructor->IsFunction()) {
            v8::Local<v8::Value> arguments[] = {
                js_string(isolate, message),
                js_string(isolate, name)};
            v8::Local<v8::Object> constructed;
            if (constructor.As<v8::Function>()->NewInstance(
                    local_context, 2, arguments).ToLocal(&constructed)) {
                isolate->ThrowException(constructed);
                return;
            }
            v8::Local<v8::Value> prototype;
            if (constructor.As<v8::Function>()->Get(
                    local_context, js_string(isolate, "prototype")).ToLocal(&prototype)
                && prototype->IsObject()) {
                exception->SetPrototype(local_context, prototype).Check();
            }
        }
        isolate->ThrowException(exception);
    }

    static void attach_shadow(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* host = unwrap_node(info.This());
        if (self == nullptr || host == nullptr
            || host->kind != dom_node_kind::element) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "attachShadow requires an Element")));
            return;
        }
        static const std::unordered_set<std::string_view> standard_shadow_hosts{
            "article", "aside", "blockquote", "body", "div", "footer",
            "h1", "h2", "h3", "h4", "h5", "h6", "header", "main",
            "nav", "p", "section", "span"};
        if (host->tag.find('-') == std::string::npos
            && !standard_shadow_hosts.contains(host->tag)) {
            throw_dom_exception(
                info, "This element cannot host a shadow tree", "NotSupportedError");
            return;
        }
        if (self->document.shadow_dom(*host) != nullptr
            && self->document.shadow_dom(*host)->root != nullptr) {
            throw_dom_exception(
                info, "This element already hosts a shadow tree", "NotSupportedError");
            return;
        }
        if (info.Length() < 1 || !info[0]->IsObject()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "attachShadow requires ShadowRootInit")));
            return;
        }
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto options = info[0].As<v8::Object>();
        v8::Local<v8::Value> raw_mode;
        if (!options->Get(local_context, js_string(info.GetIsolate(), "mode"))
                .ToLocal(&raw_mode)) {
            return;
        }
        const auto mode = to_utf8(info.GetIsolate(), raw_mode);
        if (mode != "open" && mode != "closed") {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "ShadowRoot mode must be 'open' or 'closed'")));
            return;
        }
        const auto open = mode == "open";
        v8::Local<v8::Value> raw_delegates_focus;
        const auto delegates_focus = options->Get(
                local_context,
                js_string(info.GetIsolate(), "delegatesFocus"))
                .ToLocal(&raw_delegates_focus)
            && raw_delegates_focus->BooleanValue(info.GetIsolate());
        auto& root = self->document.create_node(
            dom_node_kind::document_fragment, "#document-fragment");
        root.parent = host;
        // The ShadowRoot itself generates no box, but its logical children are
        // parsed and mutated as visible participants in the host projection.
        root.visible = true;
        auto& host_state = self->document.mutable_shadow_dom(*host);
        host_state.root = &root;
        host_state.open = open;
        host_state.delegates_focus = delegates_focus;
        auto& root_state = self->document.mutable_shadow_dom(root);
        root_state.host = host;
        root_state.open = open;
        root_state.delegates_focus = delegates_focus;
        self->document.refresh_shadow_distributions();
        self->document.mark_dirty();
        info.GetReturnValue().Set(self->wrap_node(root));
    }

    static void get_shadow_root(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* host = unwrap_node(info.Holder());
        const auto* state = host == nullptr ? nullptr : self->document.shadow_dom(*host);
        auto* root = state != nullptr && state->open
            ? state->root
            : nullptr;
        info.GetReturnValue().Set(root == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*root)));
    }

    static void get_shadow_host(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = unwrap_node(info.Holder());
        const auto* state = root == nullptr ? nullptr : self->document.shadow_dom(*root);
        auto* host = state != nullptr
            ? state->host
            : nullptr;
        info.GetReturnValue().Set(host == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*host)));
    }

    static void get_shadow_mode(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = unwrap_node(info.Holder());
        const auto* state = root == nullptr ? nullptr : self->document.shadow_dom(*root);
        info.GetReturnValue().Set(js_string(
            info.GetIsolate(), state != nullptr && !state->open ? "closed" : "open"));
    }

    static void get_shadow_delegates_focus(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = unwrap_node(info.Holder());
        const auto* state = root == nullptr ? nullptr : self->document.shadow_dom(*root);
        info.GetReturnValue().Set(v8::Boolean::New(
            info.GetIsolate(),
            state != nullptr && state->delegates_focus));
    }

    static void get_assigned_slot(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->refresh_all_shadow_distributions();
        auto* node = unwrap_node(info.Holder());
        const auto* state = node == nullptr ? nullptr : self->document.shadow_dom(*node);
        auto* slot = state != nullptr
            ? state->assigned_slot
            : nullptr;
        if (slot != nullptr) {
            const auto* root = self->document.containing_shadow_root(*slot);
            const auto* root_state = root == nullptr
                ? nullptr
                : self->document.shadow_dom(*root);
            if (root_state != nullptr && !root_state->open) slot = nullptr;
        }
        info.GetReturnValue().Set(slot == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*slot)));
    }

    static bool assigned_nodes_flatten_requested(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (info.Length() == 0 || !info[0]->IsObject()) return false;
        v8::Local<v8::Value> flatten;
        return info[0].As<v8::Object>()->Get(
                info.GetIsolate()->GetCurrentContext(),
                js_string(info.GetIsolate(), "flatten"))
                .ToLocal(&flatten)
            && flatten->BooleanValue(info.GetIsolate());
    }

    static void append_flattened_slot_nodes(
        native_document& document,
        dom_node& slot,
        std::vector<dom_node*>& result)
    {
        const auto* state = document.shadow_dom(slot);
        const auto& candidates = state != nullptr && !state->assigned_nodes.empty()
            ? state->assigned_nodes
            : slot.children;
        for (auto* candidate : candidates) {
            if (candidate != nullptr
                && candidate->kind == dom_node_kind::element
                && candidate->tag == "slot") {
                append_flattened_slot_nodes(document, *candidate, result);
            } else if (candidate != nullptr) {
                result.push_back(candidate);
            }
        }
    }

    static std::vector<dom_node*> slot_nodes_for_options(
        native_document& document,
        dom_node* slot,
        bool flatten)
    {
        if (slot == nullptr || document.containing_shadow_root(*slot) == nullptr) return {};
        const auto* state = document.shadow_dom(*slot);
        if (!flatten) return state == nullptr
            ? std::vector<dom_node*>{}
            : state->assigned_nodes;
        std::vector<dom_node*> result;
        append_flattened_slot_nodes(document, *slot, result);
        return result;
    }

    static void slot_assigned_nodes(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->refresh_all_shadow_distributions();
        auto* slot = unwrap_node(info.This());
        const auto nodes = slot_nodes_for_options(
            self->document, slot, assigned_nodes_flatten_requested(info));
        const auto count = nodes.size();
        auto result = v8::Array::New(info.GetIsolate(), static_cast<int>(count));
        auto local_context = info.GetIsolate()->GetCurrentContext();
        for (uint32_t index = 0; index < count; ++index) {
            result->Set(
                local_context, index,
                self->wrap_node(*nodes[index])).Check();
        }
        info.GetReturnValue().Set(result);
    }

    static void slot_assigned_elements(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->refresh_all_shadow_distributions();
        auto* slot = unwrap_node(info.This());
        const auto nodes = slot_nodes_for_options(
            self->document, slot, assigned_nodes_flatten_requested(info));
        std::vector<dom_node*> elements;
        for (auto* node : nodes) {
            if (node != nullptr && node->kind == dom_node_kind::element) {
                elements.push_back(node);
            }
        }
        auto result = v8::Array::New(
            info.GetIsolate(), static_cast<int>(elements.size()));
        auto local_context = info.GetIsolate()->GetCurrentContext();
        for (uint32_t index = 0; index < elements.size(); ++index) {
            result->Set(
                local_context, index, self->wrap_node(*elements[index])).Check();
        }
        info.GetReturnValue().Set(result);
    }

    v8::Local<v8::Object> style_sheet_for(dom_node& node)
    {
        const auto key = wrapper_key(node);
        const auto known = stylesheet_wrappers.find(key);
        if (known != stylesheet_wrappers.end() && !known->second.IsEmpty()) {
            return known->second.Get(isolate);
        }
        const auto local_context = isolate->GetCurrentContext();
        auto result = v8::Object::New(isolate);
        result->Set(
            local_context,
            js_string(isolate, "ownerNode"),
            wrap_node(node)).Check();
        stylesheet_wrappers.emplace(key, v8::Global<v8::Object>(isolate, result));
        return result;
    }

    static void get_style_sheet(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (self == nullptr || node == nullptr || !self->is_connected(*node)) {
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
            return;
        }
        info.GetReturnValue().Set(self->style_sheet_for(*node));
    }

    static void get_shadow_style_sheets(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = unwrap_node(info.Holder());
        std::vector<dom_node*> styles;
        if (self != nullptr && root != nullptr && self->is_connected(*root)) {
            const auto collect = [&](const auto& visit, dom_node& parent) -> void {
                for (auto* child : parent.children) {
                    if (child == nullptr) continue;
                    if (child->kind == dom_node_kind::element && child->tag == "style") {
                        styles.push_back(child);
                    }
                    visit(visit, *child);
                }
            };
            collect(collect, *root);
        }
        const auto local_context = info.GetIsolate()->GetCurrentContext();
        auto result = v8::Array::New(
            info.GetIsolate(), static_cast<int>(styles.size()));
        for (uint32_t index = 0; index < styles.size(); ++index) {
            result->Set(
                local_context, index,
                self->style_sheet_for(*styles[index])).Check();
        }
        info.GetReturnValue().Set(result);
    }

    static void get_document_style_sheets(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = static_cast<dom_node*>(info.Holder()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        if (root == nullptr) root = &self->active_root();
        std::vector<dom_node*> styles;
        const auto collect = [&](const auto& visit, dom_node& parent) -> void {
            for (auto* child : parent.children) {
                if (child == nullptr) continue;
                auto is_sheet = child->kind == dom_node_kind::element
                    && child->tag == "style";
                if (!is_sheet && child->kind == dom_node_kind::element
                    && child->tag == "link") {
                    const auto rel = child->attributes.find("rel");
                    if (rel != child->attributes.end()) {
                        const auto tokens = lower_html_name(rel->second);
                        size_t cursor = 0;
                        while (cursor < tokens.size()) {
                            while (cursor < tokens.size()
                                && std::isspace(static_cast<unsigned char>(tokens[cursor]))) {
                                ++cursor;
                            }
                            const auto begin = cursor;
                            while (cursor < tokens.size()
                                && !std::isspace(static_cast<unsigned char>(tokens[cursor]))) {
                                ++cursor;
                            }
                            if (tokens.substr(begin, cursor - begin) == "stylesheet") {
                                is_sheet = true;
                                break;
                            }
                        }
                    }
                }
                if (is_sheet) styles.push_back(child);
                visit(visit, *child);
            }
        };
        collect(collect, *root);
        const auto local_context = info.GetIsolate()->GetCurrentContext();
        auto result = v8::Array::New(
            info.GetIsolate(), static_cast<int>(styles.size()));
        for (uint32_t index = 0; index < styles.size(); ++index) {
            result->Set(
                local_context, index,
                self->style_sheet_for(*styles[index])).Check();
        }
        info.GetReturnValue().Set(result);
    }

    static void get_root_node(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (self == nullptr || node == nullptr) return;
        auto composed = false;
        if (info.Length() > 0 && info[0]->IsObject()) {
            auto local_context = info.GetIsolate()->GetCurrentContext();
            v8::Local<v8::Value> value;
            composed = info[0].As<v8::Object>()->Get(
                    local_context, js_string(info.GetIsolate(), "composed"))
                    .ToLocal(&value)
                && value->BooleanValue(info.GetIsolate());
        }
        auto* root = node;
        while (root->parent != nullptr) {
            if (self->document.is_shadow_root(*root) && !composed) break;
            root = root->parent;
        }
        if (root == &self->active_root()) {
            auto local_context = info.GetIsolate()->GetCurrentContext();
            v8::Local<v8::Value> document_value;
            if (local_context->Global()->Get(
                    local_context, js_string(info.GetIsolate(), "document"))
                    .ToLocal(&document_value)) {
                info.GetReturnValue().Set(document_value);
                return;
            }
        }
        info.GetReturnValue().Set(self->wrap_node(*root));
    }

    void mark_cssom_layout_dirty(
        dom_node& node,
        std::string_view canonical_property_name)
    {
        // These properties affect painting or interaction, but never box
        // measurement or flow. Publish the scene without synchronously
        // traversing layout again.
        // Transforms are not flow-affecting, but CSSOM geometry observes
        // their painted bounds and therefore requires a fresh layout.
        constexpr std::array paint_only_properties{
            std::string_view{"background"},
            std::string_view{"background-color"},
            std::string_view{"border-radius"},
            std::string_view{"box-shadow"},
            std::string_view{"color"},
            std::string_view{"cursor"},
            std::string_view{"opacity"},
            std::string_view{"pointer-events"},
            std::string_view{"visibility"},
            std::string_view{"-webkit-font-smoothing"}
        };
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto force_layout_invalidation =
            std::getenv("WEBSCENE_FORCE_LAYOUT_INVALIDATION") != nullptr;
#else
        constexpr auto force_layout_invalidation = false;
#endif
        if (!force_layout_invalidation && std::find(
                paint_only_properties.begin(),
                paint_only_properties.end(),
                canonical_property_name) != paint_only_properties.end()) {
            document.mark_scene_changed();
            return;
        }
        const auto changes_only_positioned_subtree =
            canonical_property_name == "width"
            || canonical_property_name == "height"
            || canonical_property_name == "min-width"
            || canonical_property_name == "min-height"
            || canonical_property_name == "max-width"
            || canonical_property_name == "max-height"
            || canonical_property_name == "left"
            || canonical_property_name == "top"
            || canonical_property_name == "right"
            || canonical_property_name == "bottom";
        dom_node* isolated_geometry_root = nullptr;
        if (changes_only_positioned_subtree) {
            for (auto* current = &node; current != nullptr;
                 current = current->parent) {
                if (current->style.position != position_mode::absolute
                    && current->style.position != position_mode::fixed) {
                    continue;
                }
                isolated_geometry_root = current;
                break;
            }
        }
        if (isolated_geometry_root != nullptr) {
            // A descendant can resize an auto-sized positioned ancestor, but
            // that subtree remains outside normal flow. Keep its pending work
            // scoped so geometry reads elsewhere do not traverse the document.
            document.mark_out_of_flow_geometry_dirty(*isolated_geometry_root);
        } else {
            document.mark_dirty();
        }
    }

    css_cascade_state take_active_css_cascade()
    {
        css_cascade_state state;
        state.rules = std::move(css_rules);
        state.stylesheet_owner_id = active_stylesheet_owner_id;
        state.stylesheet_shadow_scope_root_id = active_stylesheet_shadow_scope_root_id;
        state.opacity_keyframes = std::move(opacity_keyframes);
        state.rules_by_class = std::move(css_rules_by_class);
        state.rules_by_id = std::move(css_rules_by_id);
        state.rules_by_tag = std::move(css_rules_by_tag);
        state.rules_by_attribute = std::move(css_rules_by_attribute);
        state.rules_by_variable_reference = std::move(css_rules_by_variable_reference);
        state.focus_rules = std::move(css_focus_rules);
        state.unindexed_rules = std::move(unindexed_css_rules);
        state.attribute_dependencies = std::move(css_attribute_dependencies);
        state.descendant_attribute_dependencies =
            std::move(css_descendant_attribute_dependencies);
        state.hover_dependencies = std::move(hover_selector_dependencies);
        state.variables = std::move(css_variables);
        state.important_variables = std::move(important_css_variables);
        active_stylesheet_owner_id = 0U;
        active_stylesheet_shadow_scope_root_id = 0U;
        return state;
    }

    void restore_active_css_cascade(css_cascade_state state)
    {
        css_rules = std::move(state.rules);
        active_stylesheet_owner_id = state.stylesheet_owner_id;
        active_stylesheet_shadow_scope_root_id =
            state.stylesheet_shadow_scope_root_id;
        opacity_keyframes = std::move(state.opacity_keyframes);
        css_rules_by_class = std::move(state.rules_by_class);
        css_rules_by_id = std::move(state.rules_by_id);
        css_rules_by_tag = std::move(state.rules_by_tag);
        css_rules_by_attribute = std::move(state.rules_by_attribute);
        css_rules_by_variable_reference =
            std::move(state.rules_by_variable_reference);
        css_focus_rules = std::move(state.focus_rules);
        unindexed_css_rules = std::move(state.unindexed_rules);
        css_attribute_dependencies = std::move(state.attribute_dependencies);
        css_descendant_attribute_dependencies =
            std::move(state.descendant_attribute_dependencies);
        hover_selector_dependencies = std::move(state.hover_dependencies);
        css_variables = std::move(state.variables);
        important_css_variables = std::move(state.important_variables);
    }

    void activate_css_cascade(dom_node* root)
    {
        if (root == nullptr) root = &document.body();
        if (active_css_cascade_root == nullptr) {
            active_css_cascade_root = &document.body();
        }
        if (active_css_cascade_root == root) return;

        auto incoming = inactive_css_cascades.extract(root);
        inactive_css_cascades.insert_or_assign(
            active_css_cascade_root,
            take_active_css_cascade());
        if (!incoming.empty()) {
            restore_active_css_cascade(std::move(incoming.mapped()));
        }
        active_css_cascade_root = root;
    }

    void activate_css_cascade(v8::Local<v8::Context> local_context)
    {
        if (local_context.IsEmpty()) {
            activate_css_cascade(&document.body());
            return;
        }
        activate_css_cascade(static_cast<dom_node*>(
            local_context->GetAlignedPointerFromEmbedderData(
                1,
                v8::kEmbedderDataTypeTagDefault)));
    }

    dom_node* css_cascade_root_for_node(dom_node& node)
    {
        // A same-origin caller can mutate a node from another browsing
        // context. V8's current context then identifies the caller, not the
        // node's stylesheet realm. Resolve the nearest registered cascade
        // root from the node's own ancestry before applying any recascade.
        for (auto* current = &node; current != nullptr; current = current->parent) {
            if (current == active_css_cascade_root
                || inactive_css_cascades.contains(current)) {
                return current;
            }
        }
        return const_cast<dom_node*>(active_css_cascade_root);
    }

    v8::Local<v8::Context> context_for_node(const dom_node& node)
    {
        for (auto* ancestor = node.parent; ancestor != nullptr; ancestor = ancestor->parent) {
            if (ancestor->tag != "iframe") continue;
            const auto owner = actual_frame_contexts.find(ancestor->id);
            if (owner != actual_frame_contexts.end() && !owner->second.IsEmpty()) {
                return owner->second.Get(isolate);
            }
        }
        return context.Get(isolate);
    }

    dom_node& active_root()
    {
        auto local_context = isolate->GetCurrentContext();
        auto* root = static_cast<dom_node*>(local_context->GetAlignedPointerFromEmbedderData(
            1,
            v8::kEmbedderDataTypeTagDefault));
        if (root == nullptr) root = &document.body();
        activate_css_cascade(root);
        return *root;
    }

    static dom_node& exposed_body_for_root(dom_node& root)
    {
        auto* html = root.tag == "html" ? &root : nullptr;
        if (html == nullptr) {
            const auto found = std::find_if(
                root.children.begin(),
                root.children.end(),
                [](const auto* child) { return child != nullptr && child->tag == "html"; });
            if (found != root.children.end()) html = *found;
        }
        if (html != nullptr) {
            const auto found = std::find_if(
                html->children.begin(),
                html->children.end(),
                [](const auto* child) { return child != nullptr && child->tag == "body"; });
            if (found != html->children.end()) return **found;
        }
        return root;
    }

    bool in_frame_context() const
    {
        if (context.IsEmpty()) return false;
        const auto current_context = isolate->GetCurrentContext();
        return !current_context.IsEmpty()
            && current_context != context.Get(isolate);
    }

    const std::string& current_base_address() const
    {
        const auto current_context = isolate->GetCurrentContext();
        if (!current_context.IsEmpty()) {
            const auto* root = static_cast<const dom_node*>(
                current_context->GetAlignedPointerFromEmbedderData(
                    1,
                    v8::kEmbedderDataTypeTagDefault));
            if (const auto found = frame_base_addresses.find(root);
                found != frame_base_addresses.end()) {
                return found->second;
            }
        }
        return document_base_address;
    }

    bool is_document_object(v8::Local<v8::Object> candidate) const
    {
        if (!document_object.IsEmpty()
            && candidate->StrictEquals(document_object.Get(isolate))) return true;
        for (const auto& [_, document_value] : frame_documents) {
            if (!document_value.IsEmpty()
                && candidate->StrictEquals(document_value.Get(isolate))) return true;
        }
        for (const auto& [_, document_value] : actual_frame_documents) {
            if (!document_value.IsEmpty()
                && candidate->StrictEquals(document_value.Get(isolate))) return true;
        }
        return false;
    }

    uint64_t wrapper_key(const dom_node& node) const
    {
        return static_cast<uint64_t>(node.id) | (in_frame_context() ? (1ULL << 63U) : 0ULL);
    }

    template <typename Callback>
    void visit_subtree(dom_node& root, Callback&& callback) const
    {
        callback(root);
        for (auto* child : root.children) {
            if (child != nullptr) visit_subtree(*child, callback);
        }
        const auto* shadow = document.shadow_dom(root);
        if (shadow != nullptr && shadow->root != nullptr) {
            visit_subtree(*shadow->root, callback);
        }
    }

#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
    void cancel_detached_frame_context_tasks(
        v8::Local<v8::Context> local_context)
    {
        uint64_t cancelled_animation_frames = 0;
        const auto cancelled_timers = std::erase_if(
            timers,
            [&](const auto& timer) {
                if (timer.context.Get(isolate) != local_context) return false;
                if (timer.animation_frame) ++cancelled_animation_frames;
                inspector_async_task_canceled(timer.id);
                return true;
            });
        if (work_metrics_enabled.load(std::memory_order_relaxed)) {
            work.animation_frames_cancelled += cancelled_animation_frames;
            work.timers_cancelled +=
                cancelled_timers - cancelled_animation_frames;
        }

        // A frame callback can synchronously remove its own iframe. Prevent a
        // repeating task that is currently outside `timers` from being queued
        // again after the callback returns.
        if (active_timer_id != 0U
            && isolate->GetCurrentContext() == local_context) {
            active_timer_cancelled = true;
        }

        std::erase_if(
            pending_window_messages,
            [&](const auto& task) {
                return task.target_context.Get(isolate) == local_context;
            });
        std::erase_if(
            pending_programmatic_scroll_events,
            [&](const auto& task) {
                return task.context.Get(isolate) == local_context;
            });
        std::erase_if(
            connected_resources,
            [&](const auto& task) {
                return task.context.Get(isolate) == local_context;
            });
        std::erase_if(
            resize_observers,
            [&](const auto& observer) {
                return observer != nullptr
                    && observer->context.Get(isolate) == local_context;
            });
        for (auto iterator = websocket_bindings.begin();
             iterator != websocket_bindings.end();) {
            if (iterator->second.context.Get(isolate) != local_context) {
                ++iterator;
                continue;
            }
            static_cast<void>(websocket_transport.close(
                iterator->first,
                1001U,
                "Frame browsing context was discarded"));
            iterator = websocket_bindings.erase(iterator);
        }
    }

    void destroy_detached_frame_contexts(dom_node& root)
    {
        std::unordered_set<uint32_t> node_ids;
        std::vector<v8::Local<v8::Context>> detached_contexts;
        visit_subtree(root, [&](const dom_node& node) {
            node_ids.insert(node.id);
        });
        for (const auto id : node_ids) {
            const auto found = actual_frame_contexts.find(id);
            if (found != actual_frame_contexts.end()
                && !found->second.IsEmpty()) {
                const auto local_context = found->second.Get(isolate);
                const auto* frame_root = static_cast<const dom_node*>(
                    local_context->GetAlignedPointerFromEmbedderData(
                        1,
                        v8::kEmbedderDataTypeTagDefault));
                frame_base_addresses.erase(frame_root);
                if (active_css_cascade_root == frame_root) {
                    activate_css_cascade(&document.body());
                }
                inactive_css_cascades.erase(frame_root);
                detached_contexts.push_back(local_context);
                cancel_detached_frame_context_tasks(local_context);
                destroy_inspector_context(local_context);
                if (!frame_context.IsEmpty()
                    && frame_context.Get(isolate) == local_context) {
                    frame_context.Reset();
                    frame_base_address.clear();
                }
            }
            actual_frame_contexts.erase(id);
            frame_hydration_states.erase(id);
            actual_frame_windows.erase(id);
            actual_frame_documents.erase(id);
            frame_session_storage.erase(id);
            frame_load_listeners.erase(id);
        }
        for (auto& [type, listeners] : frame_event_listeners) {
            static_cast<void>(type);
            std::erase_if(listeners, [&](const auto& listener) {
                const auto listener_context = listener.context.Get(isolate);
                return std::any_of(
                    detached_contexts.begin(),
                    detached_contexts.end(),
                    [&](const auto context) {
                        return listener_context == context;
                    });
            });
        }
    }
#endif

    void weaken_detached_subtree_wrappers(dom_node& root)
    {
        // Detaching an iframe discards its browsing context immediately. DOM
        // wrappers may remain alive for JavaScript references, but Inspector
        // execution contexts and remote objects must not survive until the
        // detached native subtree is eventually reclaimed.
        // A persistent V8 handle is a GC root. Keeping every historical DOM
        // wrapper strong therefore retains React's expando graph forever after
        // the corresponding native node is removed. Model a detached DOM tree
        // as one weakly rooted object graph: any JavaScript reference to any
        // wrapper keeps the complete tree alive, while an unreachable tree can
        // be reclaimed by V8 as a cycle.
        for (const auto frame_realm : {false, true}) {
            if (frame_realm && frame_context.IsEmpty()) continue;
            auto local_context = frame_realm ? frame_context.Get(isolate) : context.Get(isolate);
            if (local_context.IsEmpty()) continue;
            v8::Context::Scope context_scope(local_context);
            std::vector<v8::Local<v8::Object>> wrappers;
            const auto collect = [&](auto& cache, uint64_t key) {
                const auto known = cache.find(key);
                if (known == cache.end() || known->second.IsEmpty()) return;
                wrappers.push_back(known->second.Get(isolate));
            };
            visit_subtree(root, [&](dom_node& node) {
                const auto key = static_cast<uint64_t>(node.id)
                    | (frame_realm ? (1ULL << 63U) : 0ULL);
                collect(node_wrappers, key);
                collect(class_list_wrappers, key);
                collect(style_wrappers, key);
                collect(computed_style_wrappers, key);
                collect(canvas_contexts, key);
            });
            if (wrappers.empty()) continue;

            auto anchor = v8::Object::New(isolate);
            auto members = v8::Array::New(isolate, static_cast<int>(wrappers.size()));
            for (size_t index = 0; index < wrappers.size(); ++index) {
                members->Set(
                    local_context,
                    static_cast<uint32_t>(index),
                    wrappers[index]).Check();
            }
            anchor->Set(
                local_context,
                js_string(isolate, "members"),
                members).Check();
            const auto anchor_key = v8::Private::ForApi(
                isolate,
                js_string(isolate, "webscene.detached-dom-anchor"));
            for (auto wrapper : wrappers) {
                wrapper->SetPrivate(local_context, anchor_key, anchor).Check();
            }

            const auto weaken = [](auto& cache, uint64_t key) {
                const auto known = cache.find(key);
                if (known != cache.end() && !known->second.IsEmpty()
                    && !known->second.IsWeak()) {
                    known->second.SetWeak();
                }
            };
            visit_subtree(root, [&](dom_node& node) {
                const auto key = static_cast<uint64_t>(node.id)
                    | (frame_realm ? (1ULL << 63U) : 0ULL);
                weaken(node_wrappers, key);
                weaken(class_list_wrappers, key);
                weaken(style_wrappers, key);
                weaken(computed_style_wrappers, key);
                weaken(canvas_contexts, key);
            });
        }
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        destroy_detached_frame_contexts(root);
#endif
        register_detached_subtree(root);
    }

    bool root_has_weak_wrapper(const dom_node& root) const
    {
        const auto weak = [](const auto& cache, uint64_t key) {
            const auto known = cache.find(key);
            return known != cache.end()
                && !known->second.IsEmpty()
                && known->second.IsWeak();
        };
        for (const auto frame_realm : {false, true}) {
            const auto key = static_cast<uint64_t>(root.id)
                | (frame_realm ? (1ULL << 63U) : 0ULL);
            if (weak(node_wrappers, key)
                || weak(class_list_wrappers, key)
                || weak(style_wrappers, key)
                || weak(computed_style_wrappers, key)
                || weak(canvas_contexts, key)) {
                return true;
            }
        }
        return false;
    }

    bool retain_reconnected_subtree_wrappers(dom_node& root)
    {
        if (!is_connected(root)) return false;

        // A style recascade is also used to initialize newly inserted DOM, so
        // it is the common connection boundary for every mutation API. Most
        // recascades, however, are ordinary class/style changes on an already
        // connected tree. Do not walk that tree unless it contains a root that
        // was actually weakened while detached (or its own wrapper is weak
        // because it was extracted from a tracked detached ancestor).
        auto reconnects_tracked_root = root_has_weak_wrapper(root);
        if (!reconnects_tracked_root) {
            for (const auto root_id : detached_dom_roots) {
                const auto* detached_root = document.find_by_native_id(root_id);
                if (detached_root != nullptr
                    && (detached_root == &root
                        || subtree_contains(root, detached_root))) {
                    reconnects_tracked_root = true;
                    break;
                }
            }
        }
        if (!reconnects_tracked_root) return false;

#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++reconnected_wrapper_retention_subtrees;
#endif
        for (const auto frame_realm : {false, true}) {
            if (frame_realm && frame_context.IsEmpty()) continue;
            auto local_context = frame_realm ? frame_context.Get(isolate) : context.Get(isolate);
            if (local_context.IsEmpty()) continue;
            v8::Context::Scope context_scope(local_context);
            const auto anchor_key = v8::Private::ForApi(
                isolate,
                js_string(isolate, "webscene.detached-dom-anchor"));
            const auto retain = [&](auto& cache, uint64_t key) {
                const auto known = cache.find(key);
                if (known == cache.end() || known->second.IsEmpty()) return;
                if (known->second.IsWeak()) known->second.ClearWeak();
                known->second.Get(isolate)->DeletePrivate(local_context, anchor_key).Check();
            };
            visit_subtree(root, [&](dom_node& node) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                if (!frame_realm) ++reconnected_wrapper_retention_nodes;
#endif
                const auto key = static_cast<uint64_t>(node.id)
                    | (frame_realm ? (1ULL << 63U) : 0ULL);
                retain(node_wrappers, key);
                retain(class_list_wrappers, key);
                retain(style_wrappers, key);
                retain(computed_style_wrappers, key);
                retain(canvas_contexts, key);
            });
        }
        std::erase_if(detached_dom_roots, [&](const auto root_id) {
            const auto* candidate = document.find_by_native_id(root_id);
            return candidate == nullptr
                || (is_connected(*candidate)
                    && (candidate == &root || subtree_contains(root, candidate)));
        });
        return true;
    }

    void notify_custom_elements_subtree(dom_node& root, const char* phase)
    {
        if (!custom_elements_active) return;
        auto local_context = isolate->GetCurrentContext();
        v8::Local<v8::Value> raw_callback;
        if (!local_context->Global()->Get(
                local_context,
                js_string(isolate, "__webSceneCustomElementsNotifySubtree"))
                .ToLocal(&raw_callback)
            || !raw_callback->IsFunction()) {
            return;
        }
        v8::TryCatch try_catch(isolate);
        std::vector<dom_node*> reaction_roots{&root};
        visit_subtree(root, [&](dom_node& node) {
            if (&node != &root && document.is_shadow_root(node)) {
                reaction_roots.push_back(&node);
            }
        });
        for (auto* reaction_root : reaction_roots) {
            v8::Local<v8::Value> arguments[]{
                wrap_node(*reaction_root),
                js_string(isolate, phase)
            };
            static_cast<void>(raw_callback.As<v8::Function>()->Call(
                local_context,
                local_context->Global(),
                2,
                arguments));
            if (try_catch.HasCaught()) break;
        }
        if (try_catch.HasCaught()) {
            last_error = "Custom element subtree reaction failed: "
                + describe_reported_exception(try_catch, local_context);
        }
    }

    void notify_custom_element_attribute(
        dom_node& element,
        const std::string& name,
        const std::optional<std::string>& old_value,
        const std::optional<std::string>& new_value,
        const std::optional<std::string>& namespace_uri = std::nullopt)
    {
        if (!custom_elements_active) return;
        auto local_context = isolate->GetCurrentContext();
        v8::Local<v8::Value> raw_callback;
        if (!local_context->Global()->Get(
                local_context,
                js_string(isolate, "__webSceneCustomElementsNotifyAttribute"))
                .ToLocal(&raw_callback)
            || !raw_callback->IsFunction()) {
            return;
        }
        const auto dom_value = [&](const std::optional<std::string>& value) {
            return value.has_value()
                ? v8::Local<v8::Value>(js_dom_string(isolate, *value))
                : v8::Local<v8::Value>(v8::Null(isolate));
        };
        v8::Local<v8::Value> arguments[]{
            wrap_node(element),
            js_dom_string(isolate, name),
            dom_value(old_value),
            dom_value(new_value),
            dom_value(namespace_uri)
        };
        v8::TryCatch try_catch(isolate);
        static_cast<void>(raw_callback.As<v8::Function>()->Call(
            local_context,
            local_context->Global(),
            5,
            arguments));
        if (try_catch.HasCaught()) {
            last_error = "Custom element attribute reaction failed: "
                + describe_reported_exception(try_catch, local_context);
        }
    }

    void detach_all_children(dom_node& parent)
    {
        auto children = parent.children;
        auto stylesheets_changed = false;
        for (auto* child : children) {
            if (child != nullptr) {
                if (is_connected(*child)) {
                    notify_custom_elements_subtree(*child, "disconnected");
                }
                stylesheets_changed |= deactivate_connected_stylesheets(*child, false);
            }
        }
        document.remove_all_children(parent);
        for (auto* child : children) {
            if (child != nullptr) weaken_detached_subtree_wrappers(*child);
        }
        synchronize_textarea_value_from_children(parent);
        if (stylesheets_changed) recascade_after_stylesheet_change();
    }

    void retire_provisional_frame_document(dom_node& frame)
    {
        const auto known_body = provisional_frame_bodies.find(frame.id);
        if (known_body == provisional_frame_bodies.end()
            || known_body->second == nullptr) {
            return;
        }

        auto* root = known_body->second;
        while (root->parent != nullptr && root->parent != &frame) {
            root = root->parent;
        }
        if (root->parent != &frame) {
            provisional_frame_bodies.erase(known_body);
            return;
        }
        // Accessing contentDocument/contentWindow creates the iframe's initial
        // about:blank document synchronously. Navigation replaces that
        // document; it must not remain as a rendered sibling in front of the
        // loaded document. WebScene retains both browsing-context documents in
        // one native tree so existing JavaScript references stay valid. Retire
        // the old root from layout and paint while leaving normal iframe
        // teardown responsible for reclaiming the complete browsing context.
        root->mutable_authored_style().declarations["display"] = "none";
        root->style.inline_property_mask |= inline_display;
        root->style.display = display_mode::none;
        root->visible = false;
        static_cast<void>(deactivate_connected_stylesheets(*root, false));
        provisional_frame_bodies.erase(known_body);
        document.mark_dirty();
    }

    void register_detached_subtree(dom_node& root)
    {
        if (&root == &document.body() || root.parent != nullptr) return;
        for (const auto known_id : detached_dom_roots) {
            const auto* known = document.find_by_native_id(known_id);
            if (known != nullptr && subtree_contains(*known, &root)) return;
        }
        std::vector<uint32_t> retained_roots;
        retained_roots.reserve(detached_dom_roots.size() + 1U);
        for (const auto known_id : detached_dom_roots) {
            const auto* known = document.find_by_native_id(known_id);
            if (known != nullptr && !subtree_contains(root, known)) {
                retained_roots.push_back(known_id);
            }
        }
        size_t count = 0U;
        visit_subtree(root, [&](const dom_node&) { ++count; });
        retained_roots.push_back(root.id);
        detached_dom_roots = std::move(retained_roots);
        detached_nodes_since_gc += count;
        if (detached_nodes_since_gc >= detached_dom_gc_threshold) {
            if (!detached_dom_gc_requested
                && detached_dom_gc_scan_index
                    >= detached_dom_gc_scan_roots.size()) {
                detached_dom_gc_retry_count = 0U;
            }
            detached_dom_gc_requested = true;
        }
    }

    bool subtree_has_live_wrappers(dom_node& root) const
    {
        auto live = false;
        const auto inspect = [&](const auto& cache, uint64_t key) {
            const auto known = cache.find(key);
            return known != cache.end() && !known->second.IsEmpty();
        };
        visit_subtree(root, [&](dom_node& node) {
            for (const auto frame_realm : {false, true}) {
                const auto key = static_cast<uint64_t>(node.id)
                    | (frame_realm ? (1ULL << 63U) : 0ULL);
                live = live
                    || inspect(node_wrappers, key)
                    || inspect(detached_documents, node.id)
                    || inspect(class_list_wrappers, key)
                    || inspect(style_wrappers, key)
                    || inspect(computed_style_wrappers, key)
                    || inspect(canvas_contexts, key);
            }
        });
        return live;
    }

    void release_detached_subtrees(const std::vector<dom_node*>& roots)
    {
        std::unordered_set<dom_node*> nodes;
        std::unordered_set<uint32_t> node_ids;
        for (auto* root : roots) {
            if (root == nullptr) continue;
            visit_subtree(*root, [&](dom_node& node) {
                nodes.insert(&node);
                node_ids.insert(node.id);
            });
        }
        if (nodes.empty()) return;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++detached_dom_release_batches;
        detached_dom_release_roots += roots.size();
#endif
        const auto erase_wrappers = [&](auto& cache) {
            std::erase_if(cache, [&](const auto& entry) {
                return node_ids.contains(static_cast<uint32_t>(entry.first));
            });
        };
        erase_wrappers(node_wrappers);
        erase_wrappers(class_list_wrappers);
        erase_wrappers(style_wrappers);
        erase_wrappers(computed_style_wrappers);
        erase_wrappers(canvas_contexts);
        std::erase_if(detached_documents, [&](const auto& entry) {
            return node_ids.contains(static_cast<uint32_t>(entry.first));
        });
        std::erase_if(detached_document_roots, [&](const auto id) {
            return node_ids.contains(static_cast<uint32_t>(id));
        });
        std::erase_if(canvas_states, [&](const auto& entry) {
            return node_ids.contains(static_cast<uint32_t>(entry.first));
        });
        std::erase_if(pending_frame_hydrations, [&](const auto* node) {
            return nodes.contains(const_cast<dom_node*>(node));
        });
        std::erase_if(connected_resources, [&](const auto& resource) {
            return nodes.contains(resource.node);
        });
        std::erase_if(frame_base_addresses, [&](const auto& entry) {
            return nodes.contains(const_cast<dom_node*>(entry.first));
        });
        if (nodes.contains(const_cast<dom_node*>(active_css_cascade_root))) {
            activate_css_cascade(&document.body());
        }
        std::erase_if(inactive_css_cascades, [&](const auto& entry) {
            return nodes.contains(const_cast<dom_node*>(entry.first));
        });
        for (auto& observer : resize_observers) {
            std::erase_if(observer->nodes, [&](const auto* node) {
                return nodes.contains(const_cast<dom_node*>(node));
            });
            for (const auto id : node_ids) {
                observer->delivered_sizes.erase(id);
                observer->observed_boxes.erase(id);
            }
        }
        std::erase_if(provisional_frame_bodies, [&](const auto& entry) {
            return node_ids.contains(entry.first) || nodes.contains(entry.second);
        });
        for (const auto id : node_ids) {
            frame_documents.erase(id);
            frame_windows.erase(id);
            frame_window_scroll_offsets.erase(id);
            actual_frame_windows.erase(id);
            if (const auto found = actual_frame_contexts.find(id);
                found != actual_frame_contexts.end()
                && !found->second.IsEmpty()) {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
                destroy_inspector_context(found->second.Get(isolate));
#endif
            }
            actual_frame_contexts.erase(id);
            frame_hydration_states.erase(id);
            actual_frame_documents.erase(id);
            frame_session_storage.erase(id);
            frame_load_listeners.erase(id);
        }
        if (!frame_context.IsEmpty()) {
            auto local_frame_context = frame_context.Get(isolate);
            auto* frame_root = static_cast<dom_node*>(
                local_frame_context->GetAlignedPointerFromEmbedderData(
                    1,
                    v8::kEmbedderDataTypeTagDefault));
            if (nodes.contains(frame_root)) {
                frame_context.Reset();
                frame_base_address.clear();
            }
        }
        for (auto& [type, listeners] : frame_event_listeners) {
            static_cast<void>(type);
            std::erase_if(listeners, [&](const auto& listener) {
                return node_ids.contains(listener.target);
            });
        }
        if (nodes.contains(active_element)) active_element = nullptr;
        if (nodes.contains(pointer_capture_target)) pointer_capture_target = nullptr;
        if (nodes.contains(pointer_down_target)) pointer_down_target = nullptr;
        if (nodes.contains(scrollbar_drag_target)) scrollbar_drag_target = nullptr;
        if (nodes.contains(hover_target)) hover_target = nullptr;
        if (nodes.contains(current_related_target)) current_related_target = nullptr;
        released_detached_dom_nodes += document.erase_detached_subtrees(roots);
        synchronize_external_dom_memory_accounting();
    }

    void synchronize_external_dom_memory_accounting()
    {
        if (isolate == nullptr) return;
        const auto node_bytes = document.node_count() * sizeof(dom_node);
        const auto bounded_bytes = static_cast<int64_t>(std::min<size_t>(
            node_bytes,
            static_cast<size_t>(std::numeric_limits<int64_t>::max())));
        const auto delta = bounded_bytes - external_dom_memory_accounted_bytes;
        if (delta == 0) return;
        isolate->AdjustAmountOfExternalAllocatedMemory(delta);
        external_dom_memory_accounted_bytes = bounded_bytes;
    }

    void release_external_dom_memory_accounting()
    {
        if (isolate == nullptr || external_dom_memory_accounted_bytes == 0) return;
        isolate->AdjustAmountOfExternalAllocatedMemory(
            -external_dom_memory_accounted_bytes);
        external_dom_memory_accounted_bytes = 0;
    }

    void collect_detached_dom_if_requested()
    {
        if (detached_dom_gc_scan_index >= detached_dom_gc_scan_roots.size()) {
            detached_dom_gc_scan_roots.clear();
            detached_dom_gc_scan_index = 0U;
            if (!detached_dom_gc_requested) return;

            // A mutation can request collection while its V8 HandleScope is
            // still alive. Defer the first collection attempt to the next
            // pump so ephemeral wrappers from that completed JavaScript turn
            // are no longer conservatively retained.
            if (!detached_dom_gc_deferred_once) {
                detached_dom_gc_deferred_once = true;
                return;
            }
            detached_dom_gc_deferred_once = false;

            // Account the C++ DOM as external memory so V8's normal allocation
            // pressure participates in deciding when weak wrappers are
            // collected. At a genuinely idle boundary, offer a short deadline
            // for incremental GC work. LowMemoryNotification is deliberately
            // not used here: it means system memory pressure and performs an
            // aggressive collect-all pass rather than browser-style idle work.
            detached_dom_gc_requested = false;
            detached_nodes_since_gc = 0U;
            detached_dom_gc_cycle_retained_roots = 0U;
            detached_dom_gc_scan_roots = detached_dom_roots;
            synchronize_external_dom_memory_accounting();
            constexpr auto idle_gc_budget_seconds = 0.001;
            v8::platform::RunIdleTasks(
                v8_platform.get(),
                isolate,
                idle_gc_budget_seconds);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            ++detached_dom_idle_gc_notifications;
#endif
        }

        const auto slice_started = std::chrono::steady_clock::now();
        constexpr auto maximum_slice_scan_time = std::chrono::milliseconds(1);
        std::vector<uint32_t> removed_root_ids;
        std::vector<dom_node*> released_roots;
        removed_root_ids.reserve(maximum_detached_dom_release_roots_per_slice);
        released_roots.reserve(maximum_detached_dom_release_roots_per_slice);
        size_t inspected_roots = 0U;
        while (detached_dom_gc_scan_index < detached_dom_gc_scan_roots.size()
            && inspected_roots < maximum_detached_dom_release_roots_per_slice) {
            const auto root_id =
                detached_dom_gc_scan_roots[detached_dom_gc_scan_index++];
            ++inspected_roots;
            auto* root = document.find_by_native_id(root_id);
            if (root == nullptr) {
                removed_root_ids.push_back(root_id);
                continue;
            }
            if (is_connected(*root)) {
                retain_reconnected_subtree_wrappers(*root);
                removed_root_ids.push_back(root_id);
                continue;
            }
            if (subtree_has_live_wrappers(*root)) {
                ++detached_dom_gc_cycle_retained_roots;
                if (std::chrono::steady_clock::now() - slice_started
                    >= maximum_slice_scan_time) {
                    break;
                }
                continue;
            }
            removed_root_ids.push_back(root_id);
            released_roots.push_back(root);
            if (std::chrono::steady_clock::now() - slice_started
                >= maximum_slice_scan_time) {
                break;
            }
        }
        if (!removed_root_ids.empty()) {
            std::erase_if(detached_dom_roots, [&](const auto root_id) {
                return std::find(
                    removed_root_ids.begin(),
                    removed_root_ids.end(),
                    root_id) != removed_root_ids.end();
            });
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++detached_dom_release_slices;
        detached_dom_release_max_roots_per_slice = std::max<uint64_t>(
            detached_dom_release_max_roots_per_slice,
            released_roots.size());
#endif
        release_detached_subtrees(released_roots);

        if (detached_dom_gc_scan_index >= detached_dom_gc_scan_roots.size()) {
            detached_dom_gc_scan_roots.clear();
            detached_dom_gc_scan_index = 0U;
            // V8 can conservatively retain weak handles during the first
            // collection immediately following a large script turn. Retry one
            // later idle cycle when that happened at scale; a genuinely live
            // detached wrapper is then retained without creating a GC loop.
            if (detached_dom_gc_cycle_retained_roots > 1U
                && detached_dom_gc_retry_count == 0U
                && !detached_dom_gc_requested) {
                detached_dom_gc_retry_count = 1U;
                detached_dom_gc_deferred_once = false;
                detached_dom_gc_requested = true;
            }
        }
    }

    bool has_pending_detached_dom_collection() const noexcept
    {
        return detached_dom_gc_requested
            || detached_dom_gc_scan_index < detached_dom_gc_scan_roots.size();
    }

    v8::Local<v8::Object> wrap_node(dom_node& node)
    {
        const auto key = wrapper_key(node);
        auto known = node_wrappers.find(key);
        if (known != node_wrappers.end() && !known->second.IsEmpty()) {
            if (is_connected(node) && known->second.IsWeak()) known->second.ClearWeak();
            return known->second.Get(isolate);
        }
        if (known != node_wrappers.end()) node_wrappers.erase(known);
        auto local_context = isolate->GetCurrentContext();
#if defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
        auto selected_template = node.kind == dom_node_kind::element
            ? (node.namespace_uri() == dom_node::html_namespace_uri
                ? (node.tag == "slot"
                    ? html_slot_element_template.Get(isolate)
                    : node.tag == "form"
                        ? html_form_element_template.Get(isolate)
                    : node.tag == "select"
                        ? html_select_element_template.Get(isolate)
                    : node.tag == "script"
                        ? html_script_element_template.Get(isolate)
                    : node.tag == "style"
                        ? html_style_element_template.Get(isolate)
                    : node.tag == "td" || node.tag == "th"
                        ? html_table_cell_element_template.Get(isolate)
                    : html_element_template.Get(isolate))
                : element_template.Get(isolate))
            : node.kind == dom_node_kind::document_fragment
                ? (document.is_shadow_root(node)
                    ? shadow_root_template.Get(isolate)
                    : document_fragment_template.Get(isolate))
                : node.kind == dom_node_kind::text
                    ? text_template.Get(isolate)
                    : node.kind == dom_node_kind::comment
                        ? comment_template.Get(isolate)
                        : node.kind == dom_node_kind::processing_instruction
                            ? processing_instruction_template.Get(isolate)
                            : node_template.Get(isolate);
        auto object = selected_template->InstanceTemplate()
#else
        auto object = element_template.Get(isolate)->InstanceTemplate()
#endif
            ->NewInstance(local_context)
            .ToLocalChecked();
#if !defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
        v8::Local<v8::Value> element_constructor;
        const auto has_element_constructor = local_context->Global()
            ->Get(local_context, js_string(isolate, "Element"))
            .ToLocal(&element_constructor)
            && element_constructor->IsFunction();
        if (has_element_constructor) {
            v8::Local<v8::Value> element_prototype;
            if (element_constructor.As<v8::Function>()
                    ->Get(
                        local_context,
                        js_string(isolate, "prototype"))
                    .ToLocal(&element_prototype)
                && element_prototype->IsObject()) {
                object->SetPrototype(local_context, element_prototype).Check();
            }
        }
#endif
        object->SetAlignedPointerInInternalField(
            0,
            &node,
            v8::kEmbedderDataTypeTagDefault);
#if defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
        object->SetInternalField(1, v8::Undefined(isolate));
#endif
        if (node.tag == "button" || node.tag == "fieldset" || node.tag == "input"
            || node.tag == "object" || node.tag == "output" || node.tag == "select"
            || node.tag == "textarea") {
            object->SetNativeDataProperty(
                local_context,
                js_string(isolate, "form"),
                get_form_owner).Check();
        }
        auto& wrapper = node_wrappers[key];
        wrapper.Reset(isolate, object);
        if (!is_connected(node)) wrapper.SetWeak();
        return object;
    }

    v8::Local<v8::Object> wrap_style(dom_node& node)
    {
        const auto key = wrapper_key(node);
        auto known = style_wrappers.find(key);
        if (known != style_wrappers.end() && !known->second.IsEmpty()) {
            if (is_connected(node) && known->second.IsWeak()) known->second.ClearWeak();
            return known->second.Get(isolate);
        }
        if (known != style_wrappers.end()) style_wrappers.erase(known);
        auto object = style_template.Get(isolate)
            ->NewInstance(isolate->GetCurrentContext())
            .ToLocalChecked();
        object->SetAlignedPointerInInternalField(
            0,
            &node,
            v8::kEmbedderDataTypeTagDefault);
        object->SetInternalField(1, v8::False(isolate));
        auto& wrapper = style_wrappers[key];
        wrapper.Reset(isolate, object);
        if (!is_connected(node)) wrapper.SetWeak();
        return object;
    }

    v8::Local<v8::Object> wrap_class_list(dom_node& node)
    {
        const auto key = wrapper_key(node);
        auto known = class_list_wrappers.find(key);
        if (known != class_list_wrappers.end() && !known->second.IsEmpty()) {
            if (is_connected(node) && known->second.IsWeak()) known->second.ClearWeak();
            return known->second.Get(isolate);
        }
        if (known != class_list_wrappers.end()) class_list_wrappers.erase(known);
        auto local_context = isolate->GetCurrentContext();
        auto data = v8::External::New(
            isolate,
            &node,
            v8::kExternalPointerTypeTagDefault);
        auto object = v8::Object::New(isolate);
        object->Set(
            local_context,
            js_string(isolate, "add"),
            v8::Function::New(local_context, class_list_add, data).ToLocalChecked()).Check();
        object->Set(
            local_context,
            js_string(isolate, "remove"),
            v8::Function::New(local_context, class_list_remove, data).ToLocalChecked()).Check();
        object->Set(
            local_context,
            js_string(isolate, "contains"),
            v8::Function::New(local_context, class_list_contains, data).ToLocalChecked()).Check();
        object->Set(
            local_context,
            js_string(isolate, "toggle"),
            v8::Function::New(local_context, class_list_toggle, data).ToLocalChecked()).Check();
        auto value_getter = v8::Function::New(
            local_context,
            class_list_value_getter_bridge,
            data).ToLocalChecked();
        auto value_setter = v8::Function::New(
            local_context,
            class_list_value_setter_bridge,
            data).ToLocalChecked();
        v8::PropertyDescriptor value_descriptor(value_getter, value_setter);
        value_descriptor.set_enumerable(true);
        value_descriptor.set_configurable(true);
        object->DefineProperty(
            local_context,
            js_string(isolate, "value"),
            value_descriptor).Check();
        auto& wrapper = class_list_wrappers[key];
        wrapper.Reset(isolate, object);
        if (!is_connected(node)) wrapper.SetWeak();
        return object;
    }

    v8::Local<v8::Object> wrap_computed_style(dom_node& node)
    {
        const auto key = wrapper_key(node);
        auto known = computed_style_wrappers.find(key);
        if (known != computed_style_wrappers.end() && !known->second.IsEmpty()) {
            if (is_connected(node) && known->second.IsWeak()) known->second.ClearWeak();
            return known->second.Get(isolate);
        }
        if (known != computed_style_wrappers.end()) computed_style_wrappers.erase(known);
        auto object = style_template.Get(isolate)
            ->NewInstance(isolate->GetCurrentContext())
            .ToLocalChecked();
        object->SetAlignedPointerInInternalField(
            0,
            &node,
            v8::kEmbedderDataTypeTagDefault);
        object->SetInternalField(1, v8::True(isolate));
        auto& wrapper = computed_style_wrappers[key];
        wrapper.Reset(isolate, object);
        if (!is_connected(node)) wrapper.SetWeak();
        return object;
    }

    void enqueue_connected_resource_if_needed(
        dom_node& node,
        v8::Local<v8::Object> wrapper,
        v8::Local<v8::Context> resource_context)
    {
        for (auto* ancestor = &node; ancestor != nullptr; ancestor = ancestor->parent) {
            if (detached_document_roots.contains(ancestor->id)) return;
        }
        if (node.tag == "script") {
            const auto authored_type = node.attributes.find("type");
            if (authored_type != node.attributes.end()) {
                auto type = lower_html_name(authored_type->second);
                const auto first = type.find_first_not_of(" \t\r\n\f");
                const auto last = type.find_last_not_of(" \t\r\n\f");
                type = first == std::string::npos
                    ? std::string{}
                    : type.substr(first, last - first + 1U);
                const auto executable = type.empty() || type == "module"
                    || type == "text/javascript"
                    || type == "application/javascript"
                    || type == "application/ecmascript"
                    || type == "text/ecmascript";
                if (!executable) return;
            }
            if (!is_connected(node)
                || node.script_state == script_execution_state::already_started) {
                return;
            }
            // The already-started flag is set before evaluation, matching the
            // browser's one-shot lifecycle even when execution reports an
            // error or script code synchronously reparents its own element.
            node.script_state = script_execution_state::already_started;
            const auto source_iterator = node.attributes.find("src");
            if (source_iterator == node.attributes.end()
                || source_iterator->second.empty()) {
                std::string source;
                append_node_text(node, source);
                if (!source.empty()) {
                    const auto name = (in_frame_context()
                            ? frame_base_address
                            : document_base_address)
                        + "#dynamic-inline-script-" + std::to_string(node.id);
                    v8::Local<v8::Value> document_value;
                    v8::Local<v8::Value> previous_current_script;
                    auto restore_current_script = false;
                    auto document_object_value = resource_context->Global()->Get(
                        resource_context,
                        js_string(isolate, "document"));
                    if (document_object_value.ToLocal(&document_value)
                        && document_value->IsObject()) {
                        restore_current_script = document_value.As<v8::Object>()->Get(
                            resource_context,
                            js_string(isolate, "currentScript")).ToLocal(
                                &previous_current_script);
                        document_value.As<v8::Object>()->Set(
                            resource_context,
                            js_string(isolate, "currentScript"),
                            wrapper).Check();
                    }
                    std::string error;
                    const auto executed = execute_in_context(
                        resource_context,
                        source,
                        name,
                        error,
                        false);
                    if (!document_value.IsEmpty() && document_value->IsObject()) {
                        document_value.As<v8::Object>()->Set(
                            resource_context,
                            js_string(isolate, "currentScript"),
                            restore_current_script
                                ? previous_current_script
                                : v8::Null(isolate)).Check();
                    }
                    if (!executed) {
                        frame_last_error_value = "Connected inline script failed: " + error;
                        ++frame_script_error_count;
                        return;
                    }
                    ++frame_script_execution_count;
                }
                return;
            }
        }
        if (node.tag == "img") {
            const auto source = node.attributes.find("src");
            if (source != node.attributes.end()) {
                const auto& image = node.replaced_image();
                if (image.complete && image.source == source->second) return;
            }
            // Assigning src schedules an image load even while the element is
            // detached. Appending that same element must not enqueue a second
            // load or dispatch a duplicate load event. A pending task reads the
            // node's latest src, so coalescing also handles src replacement.
            if (std::any_of(
                    connected_resources.begin(),
                    connected_resources.end(),
                    [&node](const auto& task) {
                        return task.node == &node;
                    })) {
                return;
            }
        }
        auto resource_element =
            node.tag == "link" || node.tag == "script" || node.tag == "img";
        v8::Local<v8::Value> onload;
        const auto has_onload = wrapper->Get(
                resource_context,
                js_string(isolate, "onload")).ToLocal(&onload)
            && onload->IsFunction();
        if (!resource_element && !has_onload) return;
        connected_resources.push_back(connected_resource_task{
            &node,
            v8::Global<v8::Context>(isolate, resource_context),
            v8::Global<v8::Object>(isolate, wrapper)});
    }

    void enqueue_connected_resources_subtree(
        dom_node& root,
        v8::Local<v8::Object> wrapper,
        v8::Local<v8::Context> resource_context)
    {
        enqueue_connected_resource_if_needed(root, wrapper, resource_context);
        for (auto* child : root.children) {
            if (child == nullptr) continue;
            enqueue_connected_resources_subtree(
                *child,
                wrap_node(*child),
                resource_context);
        }
    }

    void enqueue_connected_image_resources_subtree(
        dom_node& root,
        v8::Local<v8::Context> resource_context)
    {
        if (root.tag == "img") {
            enqueue_connected_resource_if_needed(
                root,
                wrap_node(root),
                resource_context);
        }
        for (auto* child : root.children) {
            if (child != nullptr) {
                enqueue_connected_image_resources_subtree(
                    *child,
                    resource_context);
            }
        }
    }

    bool install_embed_fallback(dom_node& node)
    {
        if (!webscene::embed_fallback::enabled() || node.attributes.contains("frame-html")
            || node.attributes.contains("srcdoc")) return false;
        const auto source = node.attributes.find("src");
        if (source == node.attributes.end()) return false;
        const auto resolved = resolve_resource_url(source->second, current_base_address());
        const auto title = node.attributes.find("title");
        auto html = webscene::embed_fallback::document(resolved,
            title == node.attributes.end() ? std::string_view{} : title->second);
        if (!html) return false;
        node.attributes["object-html"] = std::move(*html);
        node.attributes["data-webscene-embed-fallback"] = "youtube";
        node.attributes["data-webscene-remote-result"] = "loaded";
        return true;
    }

    void enqueue_iframe_hydration_if_needed(dom_node& node)
    {
        if (!is_connected(node)) return;
        if (node.tag != "iframe") {
            for (auto* child : node.children) {
                if (child != nullptr) {
                    enqueue_iframe_hydration_if_needed(*child);
                }
            }
            return;
        }
        // Intercept before either document prefetch or script preparation. A
        // fallback is an engine-owned document; no player resources are loaded.
        const auto fallback = install_embed_fallback(node);
        const auto source = node.attributes.find("src");
        if (!fallback && source != node.attributes.end()) {
            if (node.attributes.erase("data-webscene-embed-fallback") != 0) {
                node.attributes.erase("object-html");
                node.attributes.erase("data-webscene-remote-result");
            }
            const auto object = object_urls.find(source->second);
            if (object != object_urls.end()) {
                node.attributes["object-html"] = object->second;
            } else {
                const auto& base = current_base_address();
                const auto resolved = resolve_resource_url(source->second, base);
                if (resolved.starts_with("http://")
                    || resolved.starts_with("https://")) {
                    node.attributes["data-webscene-remote-attempt"] = resolved;
                    node.attributes["src"] = resolved;
                    if (prefetch_iframe_documents) {
                        start_resource_prefetch(
                            resolved,
                            {},
                            WEBSCENE_RESOURCE_DOCUMENT);
                        start_frame_preparation(node, resolved);
                        node.attributes["data-webscene-remote-result"] = "pending";
                    } else {
                        std::string html;
                        std::string resolved_name;
                        if (load_text_resource(
                                resolved,
                                {},
                                WEBSCENE_RESOURCE_DOCUMENT,
                                html,
                                resolved_name)) {
                            node.attributes["object-html"] = std::move(html);
                            node.attributes["data-webscene-remote-result"] = "loaded";
                        } else {
                            node.attributes["data-webscene-remote-result"] = "failed";
                        }
                    }
                }
            }
        }
        const auto remote_result = node.attributes.find("data-webscene-remote-result");
        const auto remote_pending = remote_result != node.attributes.end()
            && remote_result->second == "pending";
        if (!node.attributes.contains("object-html")
            && !node.attributes.contains("frame-html")
            && !remote_pending) return;
        if (std::find(pending_frame_hydrations.begin(), pending_frame_hydrations.end(), &node)
            == pending_frame_hydrations.end()) {
            pending_frame_hydrations.push_back(&node);
        }
    }

    v8::Local<v8::Object> frame_document(dom_node& node)
    {
        auto known = frame_documents.find(node.id);
        if (known != frame_documents.end()) return known->second.Get(isolate);
        auto local_context = isolate->GetCurrentContext();
        auto object = frame_document_template.Get(isolate)->NewInstance(local_context).ToLocalChecked();
        object->Set(local_context, js_string(isolate, "readyState"), js_string(isolate, "complete")).Check();
        object->Set(local_context, js_string(isolate, "oninput"), v8::Null(isolate)).Check();
        const auto source = node.attributes.contains("src")
            ? resolve_resource_url(node.attributes["src"], document_base_address)
            : std::string("about:blank");
        set_document_address(local_context, object, source);
        auto& html = document.create_element("html");
        auto& head = document.create_element("head");
        auto& body = document.create_element("body");
        document.append_child(node, html);
        document.append_child(html, head);
        document.append_child(html, body);
        object->SetAlignedPointerInInternalField(
            0,
            &html,
            v8::kEmbedderDataTypeTagDefault);
        provisional_frame_bodies[node.id] = &body;
        frame_documents[node.id].Reset(isolate, object);
        return object;
    }

    v8::Local<v8::Object> frame_window(dom_node& node)
    {
        auto known = frame_windows.find(node.id);
        if (known != frame_windows.end()) return known->second.Get(isolate);
        auto local_context = context.Get(isolate);
        auto object = frame_window_template.Get(isolate)->NewInstance(local_context).ToLocalChecked();
        object->SetAlignedPointerInInternalField(
            0,
            &node,
            v8::kEmbedderDataTypeTagDefault);
        auto location = v8::Object::New(isolate);
        const auto source = node.attributes.contains("src") ? node.attributes["src"] : "about:blank";
        location->Set(local_context, js_string(isolate, "href"), js_string(isolate, source.c_str())).Check();
        object->Set(local_context, js_string(isolate, "location"), location).Check();
        object->Set(local_context, js_string(isolate, "document"), frame_document(node)).Check();
        object->Set(local_context, js_string(isolate, "window"), object).Check();
        object->Set(local_context, js_string(isolate, "self"), object).Check();
        object->Set(local_context, js_string(isolate, "parent"), local_context->Global()).Check();
        install_screen(local_context, object);
        frame_windows[node.id].Reset(isolate, object);
        return object;
    }

    bool compiled_css_selector_list_matches(
        const dom_node& node,
        const compiled_css_selector_list& selectors,
        const dom_node* scope_root = nullptr) const
    {
        return std::any_of(
            selectors.selectors.begin(),
            selectors.selectors.end(),
            [&](const auto& selector) {
                return !selector.compounds.empty()
                    && compiled_css_selector_matches(
                        node,
                        selector,
                        selector.compounds.size() - 1U,
                        scope_root);
            });
    }

    void collect_selector_matches(
        dom_node& node,
        const compiled_css_selector_list& selector,
        bool include_node,
        std::vector<dom_node*>& result,
        const dom_node* scope_root) const
    {
        if (include_node
            && !node.tag.empty()
            && node.tag.front() != '#'
            && compiled_css_selector_list_matches(
                node,
                selector,
                scope_root)) result.push_back(&node);
        for (auto* child : node.children) {
            if (child != nullptr) collect_selector_matches(
                *child,
                selector,
                true,
                result,
                scope_root);
        }
    }

    dom_node* find_selector_match(
        dom_node& node,
        const compiled_css_selector_list& selector,
        bool include_node,
        const dom_node* scope_root) const
    {
        if (include_node
            && !node.tag.empty()
            && node.tag.front() != '#'
            && compiled_css_selector_list_matches(
                node,
                selector,
                scope_root)) {
            return &node;
        }
        for (auto* child : node.children) {
            if (child == nullptr) continue;
            if (auto* match = find_selector_match(
                    *child,
                    selector,
                    true,
                    scope_root);
                match != nullptr) {
                return match;
            }
        }
        return nullptr;
    }

    dom_node* find_element_by_id(dom_node& root, const std::string& id)
    {
        const auto generation = document.scene_generation();
        if (element_id_index_generation != generation) {
            element_id_indexes.clear();
            element_named_indexes.clear();
            document_named_form_indexes.clear();
            element_id_index_generation = generation;
        }
        auto [root_index, inserted] = element_id_indexes.try_emplace(&root);
        if (inserted) {
            const auto collect = [&](const auto& recurse, dom_node& node) -> void {
                if (!node.id_attribute.empty()) {
                    root_index->second.try_emplace(node.id_attribute, &node);
                }
                for (auto* child : node.children) {
                    if (child != nullptr) recurse(recurse, *child);
                }
            };
            collect(collect, root);
        }
        const auto match = root_index->second.find(id);
        return match == root_index->second.end()
            ? nullptr
            : match->second;
    }

    dom_node* find_named_element(
        dom_node& root,
        const std::string& name,
        bool forms_only)
    {
        const auto generation = document.scene_generation();
        if (element_id_index_generation != generation) {
            element_id_indexes.clear();
            element_named_indexes.clear();
            document_named_form_indexes.clear();
            element_id_index_generation = generation;
        }
        auto& indexes = forms_only
            ? document_named_form_indexes
            : element_named_indexes;
        auto [root_index, inserted] = indexes.try_emplace(&root);
        if (inserted) {
            const auto collect = [&](const auto& recurse, dom_node& node) -> void {
                if (!forms_only || node.tag == "form") {
                    if (!node.id_attribute.empty()) {
                        root_index->second.try_emplace(node.id_attribute, &node);
                    }
                    const auto authored_name = node.attributes.find("name");
                    if (authored_name != node.attributes.end()
                        && !authored_name->second.empty()) {
                        root_index->second.try_emplace(authored_name->second, &node);
                    }
                }
                for (auto* child : node.children) {
                    if (child != nullptr) recurse(recurse, *child);
                }
            };
            collect(collect, root);
        }
        const auto match = root_index->second.find(name);
        return match == root_index->second.end()
            ? nullptr
            : match->second;
    }

    bool is_valid_dom_selector_list(std::string_view selector) const
    {
#if defined(WEBSCENE_NATIVE_ENGINE_SERVO_SELECTORS)
        // DOM selector APIs validate and then immediately match the same selector.
        // Reuse the compiled-selector cache so frequently repeated querySelector,
        // matches, and closest calls do not cross the Rust FFI and allocate a full
        // Servo parse result on every invocation.
        return !cached_css_selector_list(selector).selectors.empty();
#else
        selector = trim_css_view(selector);
        if (selector.empty()) return false;

        static const std::unordered_set<std::string> recognized_pseudo_classes{
            "root", "scope", "empty",
            "first-child", "last-child", "only-child",
            "first-of-type", "last-of-type", "only-of-type",
            "nth-child", "nth-last-child", "nth-of-type", "nth-last-of-type",
            "not", "is", "where", "has",
            "hover", "active", "focus", "focus-visible", "focus-within",
            "disabled", "enabled", "checked", "indeterminate", "default",
            "required", "optional", "valid", "invalid", "in-range", "out-of-range",
            "read-only", "read-write", "placeholder-shown", "autofill",
            "link", "visited", "any-link", "local-link", "target", "target-within",
            "lang", "dir", "defined", "fullscreen", "modal", "open",
            "picture-in-picture", "user-valid", "user-invalid", "blank"};
        static const std::unordered_set<std::string> recognized_pseudo_elements{
            "before", "after", "first-letter", "first-line", "selection", "marker",
            "placeholder", "backdrop", "file-selector-button", "cue", "cue-region",
            "grammar-error", "spelling-error", "target-text"};
        static const std::unordered_set<std::string> selector_argument_pseudos{
            "not", "is", "where", "has"};
        static const std::unordered_set<std::string> value_argument_pseudos{
            "nth-child", "nth-last-child", "nth-of-type", "nth-last-of-type", "lang", "dir"};

        auto bracket_depth = 0;
        auto parenthesis_depth = 0;
        char quote = 0;
        auto item_has_token = false;
        for (size_t index = 0; index < selector.size(); ++index) {
            const auto character = selector[index];
            if (quote != 0) {
                if (character == quote && (index == 0U || selector[index - 1U] != '\\')) quote = 0;
                continue;
            }
            if (character == '\\') {
                index = skip_css_escape_sequence(selector, index) - 1U;
                item_has_token = true;
                continue;
            }
            if (character == '\'' || character == '"') {
                quote = character;
                item_has_token = true;
                continue;
            }
            if (character == '[') {
                ++bracket_depth;
                item_has_token = true;
                continue;
            }
            if (character == ']') {
                if (--bracket_depth < 0) return false;
                continue;
            }
            if (bracket_depth != 0) continue;
            if (character == '(') {
                ++parenthesis_depth;
                continue;
            }
            if (character == ')') {
                if (--parenthesis_depth < 0) return false;
                continue;
            }
            if (character == ',' && parenthesis_depth == 0) {
                if (!item_has_token) return false;
                item_has_token = false;
                continue;
            }
            if (std::isspace(static_cast<unsigned char>(character))) continue;
            if (character == '{' || character == '}' || character == '<' || character == '%') {
                return false;
            }
            if (character == '.' || character == '#') {
                auto cursor = index + 1U;
                if (cursor >= selector.size()) return false;
                const auto identifier = read_css_identifier(selector, cursor);
                if (identifier.empty()) return false;
                index = cursor - 1U;
                item_has_token = true;
                continue;
            }
            if (character != ':') {
                item_has_token = true;
                continue;
            }

            auto cursor = index + 1U;
            auto is_pseudo_element = cursor < selector.size() && selector[cursor] == ':';
            if (is_pseudo_element) ++cursor;
            auto name = lower_html_name(read_css_identifier(selector, cursor));
            if (name.empty()) return false;
            is_pseudo_element = is_pseudo_element || name == "before" || name == "after";
            if (is_pseudo_element
                ? !recognized_pseudo_elements.contains(name)
                : !recognized_pseudo_classes.contains(name)) return false;

            const auto has_argument = cursor < selector.size() && selector[cursor] == '(';
            const auto requires_argument = selector_argument_pseudos.contains(name)
                || value_argument_pseudos.contains(name);
            if (requires_argument != has_argument || (is_pseudo_element && has_argument)) return false;
            if (has_argument) {
                const auto argument_start = cursor + 1U;
                auto nested_depth = 1;
                char argument_quote = 0;
                auto close = argument_start;
                for (; close < selector.size() && nested_depth > 0; ++close) {
                    const auto nested = selector[close];
                    if (argument_quote != 0) {
                        if (nested == argument_quote
                            && (close == 0U || selector[close - 1U] != '\\')) argument_quote = 0;
                    } else if (nested == '\\') {
                        close = skip_css_escape_sequence(selector, close) - 1U;
                    } else if (nested == '\'' || nested == '"') {
                        argument_quote = nested;
                    } else if (nested == '(') {
                        ++nested_depth;
                    } else if (nested == ')') {
                        --nested_depth;
                    }
                }
                if (nested_depth != 0 || close <= argument_start + 1U
                    || trim_css_view(selector.substr(argument_start, close - argument_start - 1U)).empty()) {
                    return false;
                }
            }
            index = cursor - 1U;
            item_has_token = true;
        }
        return quote == 0 && bracket_depth == 0 && parenthesis_depth == 0 && item_has_token;
#endif
    }

    static void throw_selector_syntax_error(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        std::string_view selector)
    {
        auto* isolate = info.GetIsolate();
        const auto context = isolate->GetCurrentContext();
        v8::Local<v8::Value> constructor_value;
        if (context->Global()->Get(context, js_string(isolate, "DOMException")).ToLocal(&constructor_value)
            && constructor_value->IsFunction()) {
            const auto message = "'" + std::string(selector) + "' is not a valid selector.";
            v8::Local<v8::Value> arguments[]{
                js_string(isolate, message.c_str()),
                js_string(isolate, "SyntaxError")};
            v8::Local<v8::Object> exception;
            if (constructor_value.As<v8::Function>()->NewInstance(context, 2, arguments).ToLocal(&exception)) {
                isolate->ThrowException(exception);
                return;
            }
        }
        isolate->ThrowException(v8::Exception::SyntaxError(
            js_string(isolate, "The selector is not valid.")));
    }

    std::vector<dom_node*> query_selector_nodes(
        dom_node& root,
        std::string_view selector,
        bool include_root) const
    {
        std::vector<dom_node*> result;
        const auto& compiled = cached_css_selector_list(selector);
        collect_selector_matches(root, compiled, include_root, result, &root);
        return result;
    }

    dom_node* query_selector_node(
        dom_node& root,
        std::string_view selector,
        bool include_root) const
    {
        const auto& compiled = cached_css_selector_list(selector);
        return find_selector_match(root, compiled, include_root, &root);
    }

    v8::Local<v8::Array> selector_results(
        dom_node& root,
        const std::string& selector,
        bool include_root = true)
    {
        auto matches = query_selector_nodes(root, selector, include_root);
        auto result = v8::Array::New(isolate, static_cast<int>(matches.size()));
        auto local_context = isolate->GetCurrentContext();
        for (uint32_t index = 0; index < matches.size(); ++index) {
            result->Set(local_context, index, wrap_node(*matches[index])).Check();
        }
        return result;
    }

    v8::Local<v8::Array> class_results(dom_node& root, const std::string& names)
    {
        std::vector<std::string> required;
        std::istringstream stream(names);
        for (std::string name; stream >> name;) required.push_back(std::move(name));
        auto matches = document.query_selector_all(root, "*");
        std::erase_if(matches, [this, &required](const auto* node) {
            return node == nullptr || required.empty()
                || !std::all_of(required.begin(), required.end(), [this, node](const auto& name) {
                    return has_class(*node, name);
                });
        });
        auto result = v8::Array::New(isolate, static_cast<int>(matches.size()));
        auto local_context = isolate->GetCurrentContext();
        for (uint32_t index = 0; index < matches.size(); ++index) {
            result->Set(local_context, index, wrap_node(*matches[index])).Check();
        }
        result->Set(
            local_context,
            js_string(isolate, "item"),
            v8::Function::New(local_context, collection_item).ToLocalChecked()).Check();
        return result;
    }

    static void collection_item(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto local_context = info.GetIsolate()->GetCurrentContext();
        const auto index = info.Length() > 0
            ? info[0]->Uint32Value(local_context).FromMaybe(0)
            : 0U;
        v8::Local<v8::Value> value;
        info.GetReturnValue().Set(
            info.This()->Get(local_context, index).ToLocal(&value)
                && !value->IsUndefined()
                ? value
                : v8::Local<v8::Value>(v8::Null(info.GetIsolate())));
    }

    static dom_node* html_collection_root(v8::Local<v8::Object> object)
    {
        return object->InternalFieldCount() < 1
            ? nullptr
            : static_cast<dom_node*>(object->GetAlignedPointerFromInternalField(
                0,
                v8::kEmbedderDataTypeTagDefault));
    }

    std::vector<dom_node*> document_link_nodes(dom_node& root) const
    {
        return query_selector_nodes(root, "a[href], area[href]", true);
    }

    dom_node* document_link_named_item(dom_node& root, std::string_view name) const
    {
        if (name.empty()) return nullptr;
        for (auto* node : document_link_nodes(root)) {
            if (node == nullptr) continue;
            if (node->id_attribute == name) return node;
            const auto authored_name = node->attributes.find("name");
            if (authored_name != node->attributes.end()
                && authored_name->second == name) return node;
        }
        return nullptr;
    }

    static void html_collection_constructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (info.IsConstructCall()
            && info.Length() == 1
            && info[0]->IsExternal()) {
            info.This()->SetAlignedPointerInInternalField(
                0,
                info[0].As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault),
                v8::kEmbedderDataTypeTagDefault);
            info.GetReturnValue().Set(info.This());
            return;
        }
        info.GetIsolate()->ThrowException(v8::Exception::TypeError(
            js_string(info.GetIsolate(), "Illegal constructor")));
    }

    v8::Local<v8::FunctionTemplate> ensure_html_collection_template()
    {
        if (!html_collection_template.IsEmpty()) {
            return html_collection_template.Get(isolate);
        }
        auto html_collection = v8::FunctionTemplate::New(
            isolate,
            html_collection_constructor);
        html_collection->SetClassName(js_string(isolate, "HTMLCollection"));
        html_collection->InstanceTemplate()->SetInternalFieldCount(1);
        auto signature = v8::Signature::New(isolate, html_collection);
        auto length_symbol = v8::Symbol::New(
            isolate,
            js_string(isolate, "WebScene.HTMLCollection.length"));
        html_collection->InstanceTemplate()->SetNativeDataProperty(
            length_symbol,
            get_html_collection_length,
            nullptr,
            js_string(isolate, "length"),
            v8::PropertyAttribute::None);
        html_collection->PrototypeTemplate()->SetAccessorProperty(
            js_string(isolate, "length"),
            v8::FunctionTemplate::New(
                isolate,
                html_collection_length_get,
                length_symbol,
                signature,
                0),
            v8::Local<v8::FunctionTemplate>(),
            v8::PropertyAttribute::None);
        html_collection->InstanceTemplate()->SetHandler(
            v8::IndexedPropertyHandlerConfiguration(html_collection_indexed_getter));
        html_collection->InstanceTemplate()->SetHandler(
            v8::NamedPropertyHandlerConfiguration(
                html_collection_named_getter,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                {},
                v8::PropertyHandlerFlags::kNonMasking));
        html_collection->PrototypeTemplate()->Set(
            js_string(isolate, "item"),
            v8::FunctionTemplate::New(
                isolate,
                html_collection_item,
                v8::Local<v8::Value>(),
                signature,
                1,
                v8::ConstructorBehavior::kThrow));
        html_collection->PrototypeTemplate()->Set(
            js_string(isolate, "namedItem"),
            v8::FunctionTemplate::New(
                isolate,
                html_collection_named_item,
                v8::Local<v8::Value>(),
                signature,
                1,
                v8::ConstructorBehavior::kThrow));
        html_collection->PrototypeTemplate()->Set(
            v8::Symbol::GetToStringTag(isolate),
            js_string(isolate, "HTMLCollection"),
            static_cast<v8::PropertyAttribute>(
                v8::PropertyAttribute::ReadOnly
                | v8::PropertyAttribute::DontEnum));
        html_collection_template.Reset(isolate, html_collection);
        return html_collection;
    }

    static void get_html_collection_constructor(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        info.GetReturnValue().Set(self->ensure_html_collection_template()
            ->GetFunction(info.GetIsolate()->GetCurrentContext()).ToLocalChecked());
    }

    static void get_html_collection_length(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = html_collection_root(info.Holder());
        if (self == nullptr || root == nullptr) return;
        info.GetReturnValue().Set(v8::Integer::NewFromUnsigned(
            info.GetIsolate(),
            static_cast<uint32_t>(self->document_link_nodes(*root).size())));
    }

    static void html_collection_length_get(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto symbol = info.Data().As<v8::Symbol>();
        v8::Local<v8::Value> value;
        if (info.This()->Get(
                info.GetIsolate()->GetCurrentContext(),
                symbol).ToLocal(&value)) {
            info.GetReturnValue().Set(value);
        }
    }

    static v8::Intercepted html_collection_indexed_getter(
        uint32_t index,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = html_collection_root(info.Holder());
        if (self == nullptr || root == nullptr) return v8::Intercepted::kNo;
        const auto links = self->document_link_nodes(*root);
        if (index >= links.size()) return v8::Intercepted::kNo;
        info.GetReturnValue().Set(self->wrap_node(*links[index]));
        return v8::Intercepted::kYes;
    }

    static v8::Intercepted html_collection_named_getter(
        v8::Local<v8::Name> property,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        if (!property->IsString()) return v8::Intercepted::kNo;
        auto* self = current(info.GetIsolate());
        auto* root = html_collection_root(info.Holder());
        if (self == nullptr || root == nullptr) return v8::Intercepted::kNo;
        const auto name = to_utf8(info.GetIsolate(), property.As<v8::Value>());
        auto* match = self->document_link_named_item(*root, name);
        if (match == nullptr) return v8::Intercepted::kNo;
        info.GetReturnValue().Set(self->wrap_node(*match));
        return v8::Intercepted::kYes;
    }

    static dom_node* require_html_collection(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* root = html_collection_root(info.This());
        if (root == nullptr) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Illegal invocation")));
        }
        return root;
    }

    static void html_collection_item(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = require_html_collection(info);
        if (self == nullptr || root == nullptr) return;
        const auto index = info.Length() > 0
            ? info[0]->Uint32Value(info.GetIsolate()->GetCurrentContext()).FromMaybe(0U)
            : 0U;
        const auto links = self->document_link_nodes(*root);
        info.GetReturnValue().Set(index < links.size()
            ? v8::Local<v8::Value>(self->wrap_node(*links[index]))
            : v8::Local<v8::Value>(v8::Null(info.GetIsolate())));
    }

    static void html_collection_named_item(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = require_html_collection(info);
        if (self == nullptr || root == nullptr) return;
        const auto name = info.Length() > 0
            ? to_utf8(info.GetIsolate(), info[0])
            : std::string{};
        auto* match = self->document_link_named_item(*root, name);
        info.GetReturnValue().Set(match == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*match)));
    }

    static void get_body(
        v8::Local<v8::Name> property,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->record_feature(
            "dom", "Document.location", "supported", {}, "dom-binding");
        auto* root = static_cast<dom_node*>(info.Holder()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        if (root == nullptr) root = &self->active_root();
        const auto property_name = to_utf8(info.GetIsolate(), property);
        if (root->xml_mode && root->tag == "#document-root") {
            if (property_name == "documentElement") {
                const auto found = std::find_if(
                    root->children.begin(),
                    root->children.end(),
                    [](const auto* child) {
                        return child != nullptr && !child->tag.starts_with('#');
                    });
                info.GetReturnValue().Set(found == root->children.end()
                    ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
                    : v8::Local<v8::Value>(self->wrap_node(**found)));
            } else {
                info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
            }
            return;
        }
        auto* html = root->tag == "html" ? root : nullptr;
        if (html == nullptr && root->tag == "body") {
            if (root->parent != nullptr && root->parent->tag == "html") {
                html = root->parent;
            } else {
                const auto found = std::find_if(
                    root->children.begin(),
                    root->children.end(),
                    [](const auto* child) { return child != nullptr && child->tag == "html"; });
                if (found != root->children.end()) html = *found;
            }
        }
        if (html != nullptr && property_name == "documentElement") {
            info.GetReturnValue().Set(self->wrap_node(*html));
            return;
        }
        if (html != nullptr) {
            const auto tag = property_name == "head" ? "head" : "body";
            const auto found = std::find_if(
                html->children.begin(),
                html->children.end(),
                [tag](const auto* child) { return child != nullptr && child->tag == tag; });
            if (found != html->children.end()) {
                info.GetReturnValue().Set(self->wrap_node(**found));
                return;
            }
        }
        info.GetReturnValue().Set(self->wrap_node(*root));
    }

    static dom_node* document_element_node(implementation& self, v8::Local<v8::Object> holder)
    {
        auto* root = static_cast<dom_node*>(holder->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        if (root == nullptr) root = &self.active_root();
        if (root->tag == "html") return root;
        if (root->tag == "body") {
            const auto html = std::find_if(
                root->children.begin(), root->children.end(), [](const auto* child) {
                    return child != nullptr && child->tag == "html";
                });
            return html == root->children.end() ? root : *html;
        }
        if (root->tag == "#document-root") {
            const auto element = std::find_if(
                root->children.begin(), root->children.end(), [](const auto* child) {
                    return child != nullptr && !child->tag.starts_with('#');
                });
            return element == root->children.end() ? nullptr : *element;
        }
        return root;
    }

    static void get_document_boundary_child(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* element = document_element_node(*self, info.Holder());
        info.GetReturnValue().Set(element == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*element)));
    }

    static void get_document_child_nodes(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* element = document_element_node(*self, info.Holder());
        auto result = v8::Array::New(info.GetIsolate(), element == nullptr ? 0 : 1);
        if (element != nullptr) {
            result->Set(
                info.GetIsolate()->GetCurrentContext(),
                0,
                self->wrap_node(*element)).Check();
        }
        info.GetReturnValue().Set(result);
    }

    static void get_document_implementation(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        info.GetReturnValue().Set(self->dom_implementation_object.Get(info.GetIsolate()));
    }

    static void get_document_doctype(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = static_cast<dom_node*>(info.Holder()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        if (root == nullptr) root = &self->active_root();
        auto* container = root->tag == "html" ? root->parent : root;
        if (container != nullptr) {
            for (auto* child : container->children) {
                if (child != nullptr && child->kind == dom_node_kind::document_type) {
                    info.GetReturnValue().Set(self->wrap_node(*child));
                    return;
                }
            }
        }
        info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
    }

    static void get_scrolling_element(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->record_feature(
            "dom",
            "Document.scrollingElement",
            "partially-supported",
            "standards-mode documentElement projection onto the native viewport scroll owner",
            "dom-binding");
        auto* root = static_cast<dom_node*>(info.Holder()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        if (root == nullptr) root = &self->active_root();
        auto* html = root->tag == "html" ? root : nullptr;
        if (html == nullptr) {
            const auto found = std::find_if(
                root->children.begin(),
                root->children.end(),
                [](const auto* child) { return child != nullptr && child->tag == "html"; });
            if (found != root->children.end()) html = *found;
        }
        info.GetReturnValue().Set(self->wrap_node(html == nullptr ? *root : *html));
    }

    static void get_document_location(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->record_feature(
            "dom", "Document.links", "supported", {}, "dom-binding");
        auto* root = static_cast<dom_node*>(info.Holder()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        if (root != nullptr && self->detached_document_roots.contains(root->id)) {
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
            return;
        }
        auto local_context = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Value> location;
        if (local_context->Global()->Get(
                local_context,
                js_string(info.GetIsolate(), "location")).ToLocal(&location)) {
            info.GetReturnValue().Set(location);
        }
    }

    static dom_node* document_element_for(
        v8::Isolate* isolate,
        v8::Local<v8::Object> holder)
    {
        auto* self = current(isolate);
        if (self == nullptr) return nullptr;
        auto* root = static_cast<dom_node*>(holder->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        if (root == nullptr) root = &self->active_root();
        if (root->tag == "html") return root;
        if (root->parent != nullptr && root->parent->tag == "html") {
            return root->parent;
        }
        const auto found = std::find_if(
            root->children.begin(),
            root->children.end(),
            [](const auto* child) { return child != nullptr && child->tag == "html"; });
        return found == root->children.end() ? root : *found;
    }

    static void get_document_dir(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* element = document_element_for(info.GetIsolate(), info.Holder());
        const auto authored = element == nullptr
            ? attribute_collection::const_iterator{}
            : element->attributes.find("dir");
        info.GetReturnValue().Set(js_dom_string(
            info.GetIsolate(),
            element == nullptr || authored == element->attributes.end()
                ? std::string{}
                : authored->second));
    }

    static void set_document_dir(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* element = document_element_for(info.GetIsolate(), info.Holder());
        if (self == nullptr || element == nullptr) return;
        const auto direction = to_wtf8(info.GetIsolate(), value);
        if (const auto authored = element->attributes.find("dir");
            authored != element->attributes.end() && authored->second == direction) {
            return;
        }
        element->attributes["dir"] = direction;
        if (self->attribute_requires_recascade("dir")) {
            self->recascade_connected_subtree(*element, "document.dir");
        } else {
            self->document.mark_dirty();
        }
    }

    static void get_document_hidden(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        const auto* self = current(info.GetIsolate());
        info.GetReturnValue().Set(v8::Boolean::New(
            info.GetIsolate(),
            self != nullptr && !self->document_visible));
    }

    static void get_document_visibility_state(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        const auto* self = current(info.GetIsolate());
        info.GetReturnValue().Set(js_string(
            info.GetIsolate(),
            self != nullptr && !self->document_visible ? "hidden" : "visible"));
    }

    static void get_document_links(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = static_cast<dom_node*>(info.Holder()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        if (root == nullptr) root = &self->active_root();
        auto isolate = info.GetIsolate();
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto private_key = v8::Private::ForApi(
            isolate,
            js_string(isolate, "WebScene.Document.links"));
        v8::Local<v8::Value> known;
        if (info.Holder()->GetPrivate(local_context, private_key).ToLocal(&known)
            && known->IsObject()) {
            info.GetReturnValue().Set(known);
            return;
        }
        v8::Local<v8::Value> constructor_value;
        if (!local_context->Global()->Get(
                local_context,
                js_string(isolate, "HTMLCollection")).ToLocal(&constructor_value)
            || !constructor_value->IsFunction()) return;
        v8::Local<v8::Value> arguments[]{v8::External::New(
            isolate,
            root,
            v8::kExternalPointerTypeTagDefault)};
        auto collection = constructor_value.As<v8::Function>()
            ->NewInstance(local_context, 1, arguments).ToLocalChecked();
        info.Holder()->SetPrivate(local_context, private_key, collection).Check();
        info.GetReturnValue().Set(collection);
    }

    static void get_document_cookie(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->record_feature(
            "dom",
            "Document.cookie",
            "partially-supported",
            "same-engine script-readable name/value jar; no HTTP cookie ingress, domain/path scoping, expiry, SameSite, Secure, or HttpOnly enforcement",
            "dom-binding");
        auto* root = static_cast<dom_node*>(info.Holder()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        if (root != nullptr && self->detached_document_roots.contains(root->id)) {
            info.GetReturnValue().Set(js_string(info.GetIsolate(), ""));
            return;
        }
        std::string value;
        for (const auto& [name, cookie_value] : self->document_cookies) {
            if (!value.empty()) value += "; ";
            value += name + "=" + cookie_value;
        }
        info.GetReturnValue().Set(js_string(info.GetIsolate(), value.c_str()));
    }

    static void set_document_cookie(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> raw_value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = static_cast<dom_node*>(info.Holder()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        if (root != nullptr && self->detached_document_roots.contains(root->id)) return;
        auto assignment = to_utf8(info.GetIsolate(), raw_value);
        const auto attributes = assignment.find(';');
        if (attributes != std::string::npos) assignment.resize(attributes);
        const auto equals = assignment.find('=');
        if (equals == std::string::npos) return;
        const auto trim = [](std::string value) {
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return std::string{};
            const auto last = value.find_last_not_of(" \t\r\n");
            return value.substr(first, last - first + 1U);
        };
        auto name = trim(assignment.substr(0U, equals));
        auto value = trim(assignment.substr(equals + 1U));
        if (name.empty()) return;
        const auto known = std::find_if(
            self->document_cookies.begin(),
            self->document_cookies.end(),
            [&name](const auto& entry) { return entry.first == name; });
        if (known == self->document_cookies.end()) {
            self->document_cookies.emplace_back(std::move(name), std::move(value));
        } else {
            known->second = std::move(value);
        }
    }

    static void create_html_document(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        const auto local_context = info.GetIsolate()->GetCurrentContext();
        self->record_feature(
            "dom",
            "DOMImplementation.createHTMLDocument",
            "supported",
            {},
            "dom-api");

        auto& html = self->document.create_element("html");
        auto& head = self->document.create_element("head");
        auto& body = self->document.create_element("body");
        self->document.append_child(html, head);
        self->document.append_child(html, body);
        self->detached_document_roots.insert(html.id);

        if (info.Length() > 0) {
            const auto title_text = to_utf8(info.GetIsolate(), info[0]);
            if (!title_text.empty()) {
                auto& title = self->document.create_element("title");
                auto& text = self->document.create_element("#text");
                text.text_content = title_text;
                self->document.append_child(title, text);
                self->document.append_child(head, title);
            }
        }

        info.GetReturnValue().Set(wrap_detached_document(
            *self, html, local_context, "about:blank"));
    }

    static v8::Local<v8::Object> wrap_detached_document(
        implementation& self,
        dom_node& root,
        v8::Local<v8::Context> local_context,
        const char* address)
    {
        self.detached_document_roots.insert(root.id);
        self.register_detached_subtree(root);
        const auto known = self.detached_documents.find(root.id);
        if (known != self.detached_documents.end() && !known->second.IsEmpty()) {
            return known->second.Get(self.isolate);
        }
        auto detached = self.document_template.Get(self.isolate)
            ->NewInstance(local_context).ToLocalChecked();
        detached->SetAlignedPointerInInternalField(
            0,
            &root,
            v8::kEmbedderDataTypeTagDefault);
        detached->Set(
            local_context,
            js_string(self.isolate, "oninput"),
            v8::Null(self.isolate)).Check();
        self.set_document_address(local_context, detached, address);
        v8::Local<v8::Value> constructor;
        if (local_context->Global()->Get(
                local_context, js_string(self.isolate, "Document"))
                .ToLocal(&constructor)
            && constructor->IsFunction()) {
            v8::Local<v8::Value> prototype;
            if (constructor.As<v8::Function>()->Get(
                    local_context, js_string(self.isolate, "prototype"))
                    .ToLocal(&prototype)
                && prototype->IsObject()) {
                detached->SetPrototype(local_context, prototype).Check();
            }
        }
        auto& retained = self.detached_documents[root.id];
        retained.Reset(self.isolate, detached);
        retained.SetWeak();
        return detached;
    }

    v8::Local<v8::Object> create_empty_document(v8::Local<v8::Context> local_context)
    {
        auto& root = document.create_element("#document-root");
        root.visible = false;
        detached_document_roots.insert(root.id);
        return wrap_detached_document(*this, root, local_context, "about:blank");
    }

    static void document_constructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (!info.IsConstructCall()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(),
                    "Failed to construct 'Document': Please use the 'new' operator")));
            return;
        }
        auto* self = current(info.GetIsolate());
        if (self != nullptr) {
            info.GetReturnValue().Set(self->create_empty_document(
                info.GetIsolate()->GetCurrentContext()));
        }
    }

    static void clone_document(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self != nullptr) {
            info.GetReturnValue().Set(self->create_empty_document(
                info.GetIsolate()->GetCurrentContext()));
        }
    }

    static void create_xml_document(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        const auto local_context = info.GetIsolate()->GetCurrentContext();
        auto& root = self->document.create_element("#document-root");
        root.visible = false;
        root.xml_mode = true;
        self->detached_document_roots.insert(root.id);
        if (info.Length() > 1) {
            const auto qualified_name = to_utf8(info.GetIsolate(), info[1]);
            if (!qualified_name.empty()) {
                auto& element = self->document.create_element(qualified_name);
                element.xml_mode = true;
                if (info.Length() > 0) {
                    const auto namespace_uri = to_utf8(info.GetIsolate(), info[0]);
                    if (!namespace_uri.empty()) element.set_namespace(namespace_uri);
                }
                self->document.append_child(root, element);
            }
        }
        info.GetReturnValue().Set(wrap_detached_document(
            *self,
            root,
            local_context,
            "about:blank"));
    }

    static void create_element(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto tag = info.Length() > 0 ? to_utf8(info.GetIsolate(), info[0]) : std::string("div");
        auto* document_root = info.This()->InternalFieldCount() > 0
            ? static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
                0,
                v8::kEmbedderDataTypeTagDefault))
            : nullptr;
        const auto xml_mode = document_root != nullptr && document_root->xml_mode;
        if (!xml_mode) tag = lower_html_name(tag);
        self->record_feature(
            "html",
            "element:" + tag,
            "supported",
            {},
            "html-element");
        auto& node = self->document.create_element(std::move(tag));
        node.xml_mode = xml_mode;
        self->apply_css_rules(node);
        info.GetReturnValue().Set(self->wrap_node(node));
    }

    static void create_element_ns(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto tag = info.Length() > 1 ? to_utf8(info.GetIsolate(), info[1]) : std::string("svg");
        self->record_feature(
            "html",
            "element:" + lower_html_name(tag),
            "supported",
            {},
            "html-element");
        auto& node = self->document.create_element(std::move(tag));
        node.set_namespace(info.Length() > 0
            ? to_utf8(info.GetIsolate(), info[0])
            : "http://www.w3.org/2000/svg");
        self->apply_css_rules(node);
        info.GetReturnValue().Set(self->wrap_node(node));
    }

    static void create_text_node(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto& node = self->document.create_element("#text");
        auto* document_root = info.This()->InternalFieldCount() > 0
            ? static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
                0,
                v8::kEmbedderDataTypeTagDefault))
            : nullptr;
        node.xml_mode = document_root != nullptr && document_root->xml_mode;
        node.text_content = info.Length() > 0 ? to_wtf8(info.GetIsolate(), info[0]) : std::string{};
        info.GetReturnValue().Set(self->wrap_node(node));
    }

    static void text_constructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (!info.IsConstructCall()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(),
                    "Failed to construct 'Text': Please use the 'new' operator")));
            return;
        }
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        auto& node = self->document.create_element("#text");
        node.text_content = info.Length() > 0
            ? to_wtf8(info.GetIsolate(), info[0])
            : std::string{};
        info.GetReturnValue().Set(self->wrap_node(node));
    }

    static void create_comment(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto& node = self->document.create_element("#comment");
        auto* document_root = info.This()->InternalFieldCount() > 0
            ? static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
                0,
                v8::kEmbedderDataTypeTagDefault))
            : nullptr;
        node.xml_mode = document_root != nullptr && document_root->xml_mode;
        node.visible = false;
        node.text_content = info.Length() > 0 ? to_wtf8(info.GetIsolate(), info[0]) : std::string{};
        info.GetReturnValue().Set(self->wrap_node(node));
    }

    static void comment_constructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (!info.IsConstructCall()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(),
                    "Failed to construct 'Comment': Please use the 'new' operator")));
            return;
        }
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        auto& node = self->document.create_element("#comment");
        node.text_content = info.Length() > 0
            ? to_wtf8(info.GetIsolate(), info[0])
            : std::string{};
        info.GetReturnValue().Set(self->wrap_node(node));
    }

    static void create_processing_instruction(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (info.Length() < 2) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(),
                    "Document.createProcessingInstruction requires 2 arguments")));
            return;
        }
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        const auto target = to_wtf8(info.GetIsolate(), info[0]);
        const auto data = to_wtf8(info.GetIsolate(), info[1]);
        if (target.empty()) {
            throw_dom_exception(info, "The target is not a valid XML name", "InvalidCharacterError");
            return;
        }
        if (data.find("?>") != std::string::npos) {
            throw_dom_exception(info, "The data contains an invalid processing-instruction terminator", "InvalidCharacterError");
            return;
        }
        auto& node = self->document.create_element("#processing-instruction");
        node.set_namespace({}, target);
        node.text_content = data;
        info.GetReturnValue().Set(self->wrap_node(node));
    }

    static attr_state* unwrap_attr(v8::Local<v8::Object> object)
    {
        if (object.IsEmpty() || object->InternalFieldCount() < 2) return nullptr;
        const auto value = object->GetInternalField(1).As<v8::Value>();
        return value->IsExternal()
            ? static_cast<attr_state*>(value.As<v8::External>()->Value(
                v8::kExternalPointerTypeTagDefault))
            : nullptr;
    }

    static std::string attribute_attachment_key(const dom_node& owner, std::string_view name)
    {
        return std::to_string(owner.id) + ":" + std::string(name);
    }

    v8::Local<v8::Object> wrap_attr(attr_state& state)
    {
        const auto known = attr_wrappers.find(state.id);
        if (known != attr_wrappers.end() && !known->second.IsEmpty()) {
            return known->second.Get(isolate);
        }
        auto object = attr_template.Get(isolate)->InstanceTemplate()
            ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
        object->SetAlignedPointerInInternalField(
            0, nullptr, v8::kEmbedderDataTypeTagDefault);
        object->SetInternalField(1, v8::External::New(
            isolate, &state, v8::kExternalPointerTypeTagDefault));
        attr_wrappers.emplace(state.id, v8::Global<v8::Object>(isolate, object));
        return object;
    }

    attr_state& create_attr_state(
        std::string name,
        std::string namespace_uri = {},
        dom_node* owner = nullptr,
        std::string value = {})
    {
        auto state = std::make_unique<attr_state>();
        state->id = next_attr_id++;
        state->namespace_uri = std::move(namespace_uri);
        state->name = std::move(name);
        state->value = std::move(value);
        state->owner = owner;
        auto* result = state.get();
        attr_states.emplace(result->id, std::move(state));
        if (owner != nullptr) {
            attached_attributes[attribute_attachment_key(*owner, result->name)] = result;
        }
        return *result;
    }

    attr_state& attached_attr(dom_node& owner, std::string_view name)
    {
        const auto key = attribute_attachment_key(owner, name);
        const auto known = attached_attributes.find(key);
        if (known != attached_attributes.end()) return *known->second;
        const auto value = owner.attributes.find(name);
        return create_attr_state(
            std::string(name), {}, &owner,
            value == owner.attributes.end() ? std::string{} : value->second);
    }

    void detach_attr(dom_node& owner, std::string_view name, std::string value)
    {
        const auto key = attribute_attachment_key(owner, name);
        const auto known = attached_attributes.find(key);
        if (known == attached_attributes.end()) return;
        known->second->value = std::move(value);
        known->second->owner = nullptr;
        attached_attributes.erase(known);
    }

    static void create_attribute_with_namespace(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        bool namespaced)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        const auto name_index = namespaced ? 1 : 0;
        if (info.Length() <= name_index) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Attribute creation requires a name")));
            return;
        }
        auto name = to_utf8(info.GetIsolate(), info[name_index]);
        auto* document_root = info.This()->InternalFieldCount() > 0
            ? static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
                0, v8::kEmbedderDataTypeTagDefault))
            : nullptr;
        if (!namespaced && (document_root == nullptr || !document_root->xml_mode)) {
            name = lower_html_name(name);
        }
        const auto namespace_uri = namespaced && !info[0]->IsNullOrUndefined()
            ? to_wtf8(info.GetIsolate(), info[0])
            : std::string{};
        auto& state = self->create_attr_state(std::move(name), namespace_uri);
        self->record_feature(
            "dom",
            namespaced ? "Document.createAttributeNS" : "Document.createAttribute",
            "supported", {}, "dom-api");
        info.GetReturnValue().Set(self->wrap_attr(state));
    }

    static void create_attribute(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        create_attribute_with_namespace(info, false);
    }

    static void create_attribute_ns(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        create_attribute_with_namespace(info, true);
    }

    static void get_attr_name(
        v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        const auto* state = unwrap_attr(info.Holder());
        if (state != nullptr) info.GetReturnValue().Set(js_dom_string(info.GetIsolate(), state->name));
    }

    static void get_attr_namespace_uri(
        v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        const auto* state = unwrap_attr(info.Holder());
        info.GetReturnValue().Set(state == nullptr || state->namespace_uri.empty()
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(js_dom_string(info.GetIsolate(), state->namespace_uri)));
    }

    static void get_attr_value(
        v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        const auto* state = unwrap_attr(info.Holder());
        if (state == nullptr) return;
        auto value = state->value;
        if (state->owner != nullptr) {
            const auto match = state->owner->attributes.find(state->name);
            if (match != state->owner->attributes.end()) value = match->second;
        }
        info.GetReturnValue().Set(js_dom_string(info.GetIsolate(), value));
    }

    static void set_attr_value(
        v8::Local<v8::Name>, v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* state = unwrap_attr(info.Holder());
        if (self == nullptr || state == nullptr) return;
        const auto text = to_wtf8(info.GetIsolate(), value);
        if (state->owner == nullptr) {
            state->value = text;
            return;
        }
        const auto old_value = state->owner->attributes.contains(state->name)
            ? std::optional<std::string>(state->owner->attributes.at(state->name))
            : std::nullopt;
        state->owner->attributes[state->name] = text;
        if (state->name == "id") state->owner->id_attribute = text;
        else if (state->name == "class") state->owner->class_name = text;
        if (state->name == "class") {
            self->recascade_dynamic_state_subject(*state->owner, "Attr.value-class");
        } else {
            self->document.mark_dirty();
        }
        self->notify_custom_element_attribute(
            *state->owner, state->name, old_value, text,
            state->namespace_uri.empty()
                ? std::nullopt
                : std::optional<std::string>(state->namespace_uri));
    }

    static void get_attr_owner_element(
        v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        const auto* state = unwrap_attr(info.Holder());
        info.GetReturnValue().Set(state == nullptr || state->owner == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*state->owner)));
    }

    static void get_attr_node_type(
        v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), 2));
    }

    static void get_attr_owner_document(
        v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        info.GetReturnValue().Set(self->document_object.Get(info.GetIsolate()));
    }

    static void get_attr_specified(
        v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        info.GetReturnValue().Set(v8::True(info.GetIsolate()));
    }

    static void set_attribute_node(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* owner = unwrap_node(info.This());
        auto* state = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_attr(info[0].As<v8::Object>())
            : nullptr;
        if (self == nullptr || owner == nullptr || state == nullptr) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "setAttributeNode requires an Attr")));
            return;
        }
        if (state->owner != nullptr && state->owner != owner) {
            throw_dom_exception(info, "The attribute is already in use", "InUseAttributeError");
            return;
        }
        attr_state* previous = nullptr;
        if (owner->attributes.contains(state->name)) {
            previous = &self->attached_attr(*owner, state->name);
        }
        const auto value = state->owner == nullptr
            ? state->value
            : owner->attributes.at(state->name);
        const auto old_value = owner->attributes.contains(state->name)
            ? std::optional<std::string>(owner->attributes.at(state->name))
            : std::nullopt;
        owner->attributes[state->name] = value;
        if (state->name == "id") owner->id_attribute = value;
        else if (state->name == "class") owner->class_name = value;
        if (state->name == "class") {
            self->recascade_dynamic_state_subject(*owner, "setAttributeNode-class");
        } else {
            self->document.mark_dirty();
        }
        self->notify_custom_element_attribute(
            *owner, state->name, old_value, value,
            state->namespace_uri.empty()
                ? std::nullopt
                : std::optional<std::string>(state->namespace_uri));
        if (previous != nullptr && previous != state) {
            previous->value = old_value.value_or(std::string{});
            previous->owner = nullptr;
        }
        state->owner = owner;
        self->attached_attributes[attribute_attachment_key(*owner, state->name)] = state;
        info.GetReturnValue().Set(previous == nullptr || previous == state
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_attr(*previous)));
    }

    static void remove_attribute_node(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* owner = unwrap_node(info.This());
        auto* state = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_attr(info[0].As<v8::Object>())
            : nullptr;
        if (self == nullptr || owner == nullptr || state == nullptr
            || state->owner != owner || !owner->attributes.contains(state->name)) {
            throw_dom_exception(info, "The attribute was not found", "NotFoundError");
            return;
        }
        const auto old_value = owner->attributes.at(state->name);
        owner->attributes.erase(state->name);
        if (state->name == "id") owner->id_attribute.clear();
        else if (state->name == "class") owner->class_name.clear();
        state->value = old_value;
        state->owner = nullptr;
        self->attached_attributes.erase(attribute_attachment_key(*owner, state->name));
        if (state->name == "class") {
            self->recascade_dynamic_state_subject(*owner, "removeAttributeNode-class");
        } else {
            self->document.mark_dirty();
        }
        self->notify_custom_element_attribute(
            *owner, state->name, old_value, std::nullopt,
            state->namespace_uri.empty()
                ? std::nullopt
                : std::optional<std::string>(state->namespace_uri));
        info.GetReturnValue().Set(info[0]);
    }

    static void create_document_fragment(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto& node = self->document.create_element("#document-fragment");
        auto* document_root = info.This()->InternalFieldCount() > 0
            ? static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
                0,
                v8::kEmbedderDataTypeTagDefault))
            : nullptr;
        node.xml_mode = document_root != nullptr && document_root->xml_mode;
        node.visible = false;
        info.GetReturnValue().Set(self->wrap_node(node));
    }

    static dom_node& clone_dom_subtree(
        implementation& self,
        const dom_node& original,
        bool deep)
    {
        auto& clone = self.document.create_node(original.kind, original.tag);
        clone.set_namespace(
            original.namespace_uri(),
            original.namespace_prefix());
        clone.id_attribute = original.id_attribute;
        clone.class_name = original.class_name;
        clone.text_content = original.text_content;
        clone.script_state = original.script_state;
        clone.xml_mode = original.xml_mode;
        if (original.has_form_control()) {
            clone.mutable_form_control() = original.form_control();
        }
        if (original.replaced_image().complete) {
            // Cloning a decoded image preserves its current request state.
            // Component renderers commonly decode an off-tree image and then
            // clone it into the visible subtree; dropping this state leaves
            // the clone permanently at its fallback background.
            clone.mutable_replaced_image() = original.replaced_image();
        }
        clone.attributes = original.attributes;
        clone.style = original.style;
        if (original.has_authored_style()) {
            clone.mutable_authored_style() = original.authored_style();
        }
        clone.visible = true;
        if (deep) {
            for (const auto* child : original.children) {
                if (child == nullptr) continue;
                auto& child_clone = clone_dom_subtree(self, *child, true);
                self.document.append_child(clone, child_clone);
            }
            if (original.template_contents != nullptr) {
                auto& clone_contents = self.document.parser_template_contents(clone);
                for (const auto* child : original.template_contents->children) {
                    if (child == nullptr) continue;
                    auto& child_clone = clone_dom_subtree(self, *child, true);
                    self.document.append_child(clone_contents, child_clone);
                }
            }
        }
        return clone;
    }

    static void clone_node(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* source = unwrap_node(info.This());
        if (source == nullptr) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "cloneNode requires a native DOM node")));
            return;
        }
        const auto deep = info.Length() > 0 && info[0]->BooleanValue(info.GetIsolate());
        auto& clone = clone_dom_subtree(*self, *source, deep);
        self->notify_custom_elements_subtree(clone, "connected");
        info.GetReturnValue().Set(self->wrap_node(clone));
    }

    static void document_import_node(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* source = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_node(info[0].As<v8::Object>())
            : nullptr;
        if (self == nullptr || source == nullptr) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "importNode requires a native DOM node")));
            return;
        }
        const auto deep = info.Length() > 1
            && info[1]->BooleanValue(info.GetIsolate());
        auto& clone = clone_dom_subtree(*self, *source, deep);
        info.GetReturnValue().Set(self->wrap_node(clone));
    }

    static void get_element_by_id(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        const auto id = info.Length() > 0 ? to_wtf8(info.GetIsolate(), info[0]) : std::string{};
        auto* root = static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        auto* node = self->find_element_by_id(
            root == nullptr ? self->active_root() : *root,
            id);
        if (node == nullptr) {
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
        } else {
            info.GetReturnValue().Set(self->wrap_node(*node));
        }
    }

    static void document_query_selector_all(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_query));
        const auto selector = info.Length() > 0 ? to_wtf8(info.GetIsolate(), info[0]) : std::string{};
        if (!self->is_valid_dom_selector_list(selector)) {
            throw_selector_syntax_error(info, selector);
            return;
        }
        auto* root = static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        auto& query_root = root == nullptr ? self->active_root() : *root;
        const auto include_root = &query_root != &self->document.body()
            || std::none_of(
                query_root.children.begin(),
                query_root.children.end(),
                [](const auto* child) { return child != nullptr && child->tag == "html"; });
        info.GetReturnValue().Set(self->selector_results(
            query_root,
            selector,
            include_root));
    }

    static void document_query_selector(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_query));
        const auto selector = info.Length() > 0 ? to_wtf8(info.GetIsolate(), info[0]) : std::string{};
        if (!self->is_valid_dom_selector_list(selector)) {
            throw_selector_syntax_error(info, selector);
            return;
        }
        auto* root = static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        auto& query_root = root == nullptr ? self->active_root() : *root;
        const auto include_root = &query_root != &self->document.body()
            || std::none_of(
                query_root.children.begin(),
                query_root.children.end(),
                [](const auto* child) { return child != nullptr && child->tag == "html"; });
        auto* match = self->query_selector_node(query_root, selector, include_root);
        info.GetReturnValue().Set(match == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*match)));
    }

    static void document_element_from_point(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->ensure_layout();
        auto* root = static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        const auto local_context = info.GetIsolate()->GetCurrentContext();
        const auto x = info.Length() > 0
            ? static_cast<float>(info[0]->NumberValue(local_context).FromMaybe(0)) : 0;
        const auto y = info.Length() > 1
            ? static_cast<float>(info[1]->NumberValue(local_context).FromMaybe(0)) : 0;
        auto* node = self->document.hit_test(
            root == nullptr ? self->active_root() : *root,
            x,
            y);
        info.GetReturnValue().Set(node == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*node)));
    }

    static void document_caret_range_from_point(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->ensure_layout();
        auto* root = static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        const auto local_context = info.GetIsolate()->GetCurrentContext();
        const auto x = info.Length() > 0
            ? static_cast<float>(info[0]->NumberValue(local_context).FromMaybe(0)) : 0;
        const auto y = info.Length() > 1
            ? static_cast<float>(info[1]->NumberValue(local_context).FromMaybe(0)) : 0;
        auto* hit = self->document.hit_test(
            root == nullptr ? self->active_root() : *root,
            x,
            y);
        if (hit == nullptr) {
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
            return;
        }

        const auto find_text_node = [&](const auto& recurse, dom_node& node) -> dom_node* {
            if (node.tag == "#text" && !node.text_content.empty()) return &node;
            dom_node* fallback = nullptr;
            for (auto* child : node.children) {
                if (child == nullptr) continue;
                auto* candidate = recurse(recurse, *child);
                if (candidate == nullptr) continue;
                if (fallback == nullptr) fallback = candidate;
                const auto& box = candidate->layout;
                if (x >= box.x && x <= box.x + box.width
                    && y >= box.y && y <= box.y + box.height) {
                    return candidate;
                }
            }
            return fallback;
        };
        auto* text = find_text_node(find_text_node, *hit);
        auto* container = text == nullptr ? hit : text;
        const auto offset = text == nullptr
            ? 0U
            : self->document.text_caret_offset_at_x(*text, x);

        auto range = v8::Object::New(info.GetIsolate());
        range->Set(
            local_context,
            js_string(info.GetIsolate(), "startContainer"),
            self->wrap_node(*container)).Check();
        range->Set(
            local_context,
            js_string(info.GetIsolate(), "startOffset"),
            v8::Integer::NewFromUnsigned(
                info.GetIsolate(),
                static_cast<uint32_t>(offset))).Check();
        range->Set(
            local_context,
            js_string(info.GetIsolate(), "endContainer"),
            self->wrap_node(*container)).Check();
        range->Set(
            local_context,
            js_string(info.GetIsolate(), "endOffset"),
            v8::Integer::NewFromUnsigned(
                info.GetIsolate(),
                static_cast<uint32_t>(offset))).Check();
        range->Set(
            local_context,
            js_string(info.GetIsolate(), "collapsed"),
            v8::True(info.GetIsolate())).Check();
        info.GetReturnValue().Set(range);
    }

    static void document_get_elements_by_tag_name(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        const auto tag = info.Length() > 0 ? to_utf8(info.GetIsolate(), info[0]) : std::string{};
        auto* root = static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        auto& query_root = root == nullptr ? self->active_root() : *root;
        const auto include_root = &query_root != &self->document.body()
            || std::none_of(
                query_root.children.begin(),
                query_root.children.end(),
                [](const auto* child) { return child != nullptr && child->tag == "html"; });
        info.GetReturnValue().Set(self->selector_results(query_root, tag, include_root));
    }

    static void document_get_elements_by_class_name(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_query));
        const auto names = info.Length() > 0 ? to_utf8(info.GetIsolate(), info[0]) : std::string{};
        auto* root = static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        info.GetReturnValue().Set(self->class_results(
            root == nullptr ? self->active_root() : *root,
            names));
    }

    static void document_get_elements_by_name(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_query));
        const auto name = info.Length() > 0
            ? to_wtf8(info.GetIsolate(), info[0])
            : std::string{};
        auto* root = static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        auto& query_root = root == nullptr ? self->active_root() : *root;
        std::vector<dom_node*> matches;
        const auto collect = [&](const auto& visit, dom_node& parent) -> void {
            for (auto* child : parent.children) {
                if (child == nullptr) continue;
                if (child->kind == dom_node_kind::element) {
                    const auto authored_name = child->attributes.find("name");
                    if (authored_name != child->attributes.end()
                        && authored_name->second == name) {
                        matches.push_back(child);
                    }
                }
                visit(visit, *child);
            }
        };
        if (query_root.kind == dom_node_kind::element) {
            const auto authored_name = query_root.attributes.find("name");
            if (authored_name != query_root.attributes.end()
                && authored_name->second == name) {
                matches.push_back(&query_root);
            }
        }
        collect(collect, query_root);
        const auto local_context = info.GetIsolate()->GetCurrentContext();
        auto result = v8::Array::New(
            info.GetIsolate(), static_cast<int>(matches.size()));
        for (uint32_t index = 0; index < matches.size(); ++index) {
            result->Set(
                local_context, index,
                self->wrap_node(*matches[index])).Check();
        }
        info.GetReturnValue().Set(result);
    }

    static void element_query_selector_all(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_query));
        auto* root = unwrap_node(info.This());
        if (root == nullptr) return;
        const auto selector = info.Length() > 0 ? to_wtf8(info.GetIsolate(), info[0]) : std::string{};
        if (!self->is_valid_dom_selector_list(selector)) {
            throw_selector_syntax_error(info, selector);
            return;
        }
        info.GetReturnValue().Set(self->selector_results(*root, selector, false));
    }

    static void element_query_selector(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_query));
        auto* root = unwrap_node(info.This());
        if (root == nullptr) return;
        const auto selector = info.Length() > 0 ? to_wtf8(info.GetIsolate(), info[0]) : std::string{};
        if (!self->is_valid_dom_selector_list(selector)) {
            throw_selector_syntax_error(info, selector);
            return;
        }
        auto* match = self->query_selector_node(*root, selector, false);
        info.GetReturnValue().Set(match == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*match)));
    }

    static void element_get_elements_by_tag_name(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = unwrap_node(info.This());
        if (root == nullptr) return;
        const auto tag = info.Length() > 0 ? to_utf8(info.GetIsolate(), info[0]) : std::string{};
        info.GetReturnValue().Set(self->selector_results(*root, tag, false));
    }

    static void element_get_elements_by_class_name(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_query));
        auto* root = unwrap_node(info.This());
        if (root == nullptr) return;
        const auto names = info.Length() > 0 ? to_utf8(info.GetIsolate(), info[0]) : std::string{};
        info.GetReturnValue().Set(self->class_results(*root, names));
    }

    static void element_contains(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = unwrap_node(info.This());
        auto* candidate = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_node(info[0].As<v8::Object>())
            : nullptr;
        for (auto* node = candidate; root != nullptr && node != nullptr;
            node = self->document.dom_parent(*node)) {
            if (node == root) {
                info.GetReturnValue().Set(v8::True(info.GetIsolate()));
                return;
            }
        }
        info.GetReturnValue().Set(v8::False(info.GetIsolate()));
    }

    static void document_contains(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr || info.Length() < 1 || !info[0]->IsObject()) {
            info.GetReturnValue().Set(v8::False(info.GetIsolate()));
            return;
        }
        if (info[0].As<v8::Object>()->StrictEquals(info.This())) {
            info.GetReturnValue().Set(v8::True(info.GetIsolate()));
            return;
        }
        auto* candidate = unwrap_node(info[0].As<v8::Object>());
        auto* root = info.This()->InternalFieldCount() > 0
            ? static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
                0,
                v8::kEmbedderDataTypeTagDefault))
            : nullptr;
        if (root == nullptr) root = &self->active_root();
        for (auto* node = candidate; node != nullptr;
            node = self->document.dom_parent(*node)) {
            if (node == root) {
                info.GetReturnValue().Set(v8::True(info.GetIsolate()));
                return;
            }
        }
        info.GetReturnValue().Set(v8::False(info.GetIsolate()));
    }

    static void element_compare_document_position(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* reference = unwrap_node(info.This());
        auto* other = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_node(info[0].As<v8::Object>())
            : nullptr;
        const auto main_document = self->document_object.Get(info.GetIsolate());
        const auto reference_is_document = !main_document.IsEmpty()
            && info.This()->StrictEquals(main_document);
        const auto other_is_document = !main_document.IsEmpty()
            && info.Length() > 0
            && info[0]->StrictEquals(main_document);
        if (reference_is_document != other_is_document) {
            auto* candidate = reference_is_document ? other : reference;
            auto connected = false;
            for (auto* ancestor = candidate; ancestor != nullptr;
                ancestor = self->document.dom_parent(*ancestor)) {
                if (ancestor == &self->active_root()) {
                    connected = true;
                    break;
                }
            }
            if (connected) {
                info.GetReturnValue().Set(v8::Integer::New(
                    info.GetIsolate(), reference_is_document ? 4 | 16 : 2 | 8));
                return;
            }
        }
        if (reference == other && reference != nullptr) {
            info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), 0));
            return;
        }
        if (reference == nullptr || other == nullptr) {
            info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), 1 | 32));
            return;
        }

        const auto build_path = [self](dom_node* node) {
            std::vector<dom_node*> path;
            for (; node != nullptr; node = self->document.dom_parent(*node)) {
                path.push_back(node);
            }
            std::reverse(path.begin(), path.end());
            return path;
        };
        const auto reference_path = build_path(reference);
        const auto other_path = build_path(other);
        if (reference_path.empty() || other_path.empty()
            || reference_path.front() != other_path.front()) {
            const auto order = other->id < reference->id ? 2 : 4;
            info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), 1 | 32 | order));
            return;
        }

        size_t shared = 0U;
        const auto maximum_shared = std::min(reference_path.size(), other_path.size());
        while (shared < maximum_shared && reference_path[shared] == other_path[shared]) {
            ++shared;
        }
        if (shared == other_path.size()) {
            info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), 2 | 8));
            return;
        }
        if (shared == reference_path.size()) {
            info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), 4 | 16));
            return;
        }

        auto* parent = reference_path[shared - 1U];
        auto* reference_child = reference_path[shared];
        auto* other_child = other_path[shared];
        for (auto* child : parent->children) {
            if (child == other_child) {
                info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), 2));
                return;
            }
            if (child == reference_child) {
                info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), 4));
                return;
            }
        }
        info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), 1 | 32));
    }

    static void element_matches(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (node == nullptr || info.Length() < 1) {
            info.GetReturnValue().Set(v8::False(info.GetIsolate()));
            return;
        }
        const auto selector = to_wtf8(info.GetIsolate(), info[0]);
        if (!self->is_valid_dom_selector_list(selector)) {
            throw_selector_syntax_error(info, selector);
            return;
        }
        info.GetReturnValue().Set(v8::Boolean::New(
            info.GetIsolate(),
            self->compiled_css_selector_list_matches(
                *node,
                self->cached_css_selector_list(selector),
                node)));
    }

    static bool nodes_are_equal(const dom_node& left, const dom_node& right)
    {
        if (left.tag != right.tag
            || left.text_content != right.text_content
            || left.attributes != right.attributes
            || left.children.size() != right.children.size()) return false;
        for (size_t index = 0; index < left.children.size(); ++index) {
            const auto* left_child = left.children[index];
            const auto* right_child = right.children[index];
            if (left_child == nullptr || right_child == nullptr) {
                if (left_child != right_child) return false;
                continue;
            }
            if (!nodes_are_equal(*left_child, *right_child)) return false;
        }
        return true;
    }

    static void element_is_same_node(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* other = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_node(info[0].As<v8::Object>())
            : nullptr;
        info.GetReturnValue().Set(v8::Boolean::New(
            info.GetIsolate(),
            unwrap_node(info.This()) == other && other != nullptr));
    }

    static void element_is_equal_node(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.This());
        auto* other = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_node(info[0].As<v8::Object>())
            : nullptr;
        info.GetReturnValue().Set(v8::Boolean::New(
            info.GetIsolate(),
            node != nullptr && other != nullptr && nodes_are_equal(*node, *other)));
    }

    static void element_closest(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (node == nullptr || info.Length() < 1) {
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
            return;
        }
        const auto selector = to_wtf8(info.GetIsolate(), info[0]);
        if (!self->is_valid_dom_selector_list(selector)) {
            throw_selector_syntax_error(info, selector);
            return;
        }
        for (auto* candidate = node; candidate != nullptr;
            candidate = self->document.dom_parent(*candidate)) {
            if (self->compiled_css_selector_list_matches(
                    *candidate,
                    self->cached_css_selector_list(selector))) {
                info.GetReturnValue().Set(self->wrap_node(*candidate));
                return;
            }
        }
        info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
    }

    static void element_focus(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (self->is_programmatically_focusable(node)) {
            webscene_input_event synthetic{};
            self->set_active_element(node, synthetic);
        }
    }

    static void element_blur(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (self->active_element == node) {
            webscene_input_event synthetic{};
            self->set_active_element(nullptr, synthetic);
        }
    }

    static void element_click(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (node == nullptr || node->attributes.contains("disabled")) return;
        checkable_click_activation checkable_activation;
        auto form_control_changed = false;
        if (node->tag == "input") {
            if (!self->begin_checkable_click_activation(*node, checkable_activation)) {
                form_control_changed =
                    self->apply_non_checkable_form_control_click_default(*node);
            }
        } else if (node->tag == "option") {
            if (node->parent != nullptr) {
                for (auto* sibling : node->parent->children) {
                    if (sibling != nullptr && sibling->tag == "option") {
                        sibling->attributes.erase("selected");
                    }
                }
            }
            node->attributes["selected"] = std::string{};
            self->recascade_connected_subtree(*node, "id-property");
        }
        webscene_input_event synthetic{};
        synthetic.kind = WEBSCENE_INPUT_POINTER_UP;
        synthetic.x = node->layout.x + node->layout.width / 2.0;
        synthetic.y = node->layout.y + node->layout.height / 2.0;
        bool default_prevented = false;
        if (self->dispatch_input_event_type(
                synthetic,
                "click",
                *node,
                &default_prevented)) {
            if (checkable_activation.active) {
                if (default_prevented) {
                    self->restore_checkable_click_activation(checkable_activation);
                } else {
                    form_control_changed = checkable_activation.changed;
                }
            }
            if (!default_prevented) self->queue_external_navigation(*node);
            if (form_control_changed && self->is_connected(*node)) {
                self->dispatch_input_event_type(synthetic, "input", *node);
                self->dispatch_input_event_type(synthetic, "change", *node);
            }
        }
    }

    static void element_set_selection_range(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto local_context = info.GetIsolate()->GetCurrentContext();
        const auto start = info.Length() > 0 && info[0]->IsNumber()
            ? info[0]->Int32Value(local_context).FromMaybe(0)
            : 0;
        const auto end = info.Length() > 1 && info[1]->IsNumber()
            ? info[1]->Int32Value(local_context).FromMaybe(start)
            : start;
        info.This()->Set(
            local_context,
            js_string(info.GetIsolate(), "selectionStart"),
            v8::Integer::New(info.GetIsolate(), std::max(0, start))).Check();
        info.This()->Set(
            local_context,
            js_string(info.GetIsolate(), "selectionEnd"),
            v8::Integer::New(info.GetIsolate(), std::max(start, end))).Check();
        if (info.Length() > 2) {
            info.This()->Set(
                local_context,
                js_string(info.GetIsolate(), "selectionDirection"),
                info[2]).Check();
        }
    }

    static void element_select(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (node != nullptr) {
            webscene_input_event synthetic{};
            self->set_active_element(node, synthetic);
        }
        auto local_context = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Value> value;
        auto length = 0;
        if (info.This()->Get(
                local_context,
                js_string(info.GetIsolate(), "value")).ToLocal(&value)
            && !value->IsNullOrUndefined()) {
            length = static_cast<int>(to_utf8(info.GetIsolate(), value).size());
        }
        v8::Local<v8::Value> arguments[] = {
            v8::Integer::New(info.GetIsolate(), 0),
            v8::Integer::New(info.GetIsolate(), length)};
        info.This()->Set(
            local_context,
            js_string(info.GetIsolate(), "selectionStart"),
            arguments[0]).Check();
        info.This()->Set(
            local_context,
            js_string(info.GetIsolate(), "selectionEnd"),
            arguments[1]).Check();
        info.This()->Set(
            local_context,
            js_string(info.GetIsolate(), "selectionDirection"),
            js_string(info.GetIsolate(), "none")).Check();
    }

    static void get_active_element(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = static_cast<dom_node*>(info.Holder()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        if (root == nullptr) root = &self->active_root();
        dom_node* node = self->document.is_shadow_root(*root)
            ? nullptr
            : &exposed_body_for_root(*root);
        if (self->active_element != nullptr) {
            const auto* active_shadow_root =
                self->document.containing_shadow_root(*self->active_element);
            if (!self->document.is_shadow_root(*root)
                && active_shadow_root != nullptr
                && self->focus_document_owner(root)
                    == self->focus_document_owner(self->active_element)) {
                const auto* state = self->document.shadow_dom(*active_shadow_root);
                if (state != nullptr && state->host != nullptr) node = state->host;
                info.GetReturnValue().Set(self->wrap_node(*node));
                return;
            }
            const auto root_owner = self->focus_document_owner(root);
            const auto active_owner = self->focus_document_owner(self->active_element);
            if (root_owner == active_owner
                && self->subtree_contains(*root, self->active_element)) {
                node = self->active_element;
            } else if (root_owner == 0U && active_owner != 0U) {
                if (auto* owner = find_node_by_native_id(*root, active_owner); owner != nullptr) {
                    node = owner;
                }
            }
        }
        info.GetReturnValue().Set(node == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*node)));
    }

    static void get_provisional_frame_active_element(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* frame = provisional_frame_owner(info.Holder());
        if (frame == nullptr) return;
        const auto known = self->provisional_frame_bodies.find(frame->id);
        if (known == self->provisional_frame_bodies.end() || known->second == nullptr) return;
        auto* root = known->second;
        auto* node = self->active_element != nullptr
                && self->subtree_contains(*root, self->active_element)
            ? self->active_element
            : root;
        info.GetReturnValue().Set(self->wrap_node(*node));
    }

    static void get_provisional_frame_default_view(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* frame = provisional_frame_owner(info.Holder());
        if (frame != nullptr) info.GetReturnValue().Set(self->frame_window(*frame));
    }

    static dom_node* provisional_frame_owner(v8::Local<v8::Object> document)
    {
        auto* node = unwrap_node(document);
        for (auto* ancestor = node; ancestor != nullptr; ancestor = ancestor->parent) {
            if (ancestor->tag == "iframe") return ancestor;
        }
        return node != nullptr && node->tag == "iframe" ? node : nullptr;
    }

    static void get_provisional_frame_element(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* frame = unwrap_node(info.Holder());
        if (frame != nullptr) info.GetReturnValue().Set(self->wrap_node(*frame));
    }

    static void document_has_focus(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        info.GetReturnValue().Set(v8::True(info.GetIsolate()));
    }

    static void document_exec_command(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* isolate = info.GetIsolate();
        auto* self = current(isolate);
        bool document_receiver = self != nullptr && self->is_document_object(info.This());
        if (self != nullptr && !document_receiver) {
            for (const auto& [_, value] : self->detached_documents) {
                if (!value.IsEmpty() && info.This()->StrictEquals(value.Get(isolate))) {
                    document_receiver = true;
                    break;
                }
            }
        }
        if (!document_receiver || info.Length() == 0) {
            isolate->ThrowException(v8::Exception::TypeError(js_string(isolate,
                "Document.execCommand requires a Document receiver and a command argument")));
            return;
        }
        const auto context = isolate->GetCurrentContext();
        v8::Local<v8::String> command;
        if (!info[0]->ToString(context).ToLocal(&command)) return;
        if (info.Length() > 2 && !info[2]->IsUndefined()) {
            v8::Local<v8::String> value;
            if (!info[2]->ToString(context).ToLocal(&value)) return;
        }
        auto* root = unwrap_node(info.This());
        if (root != nullptr && (root->xml_mode || root->tag == "#document-root")) {
            throw_dom_exception(info, "execCommand is only supported on HTML documents",
                "InvalidStateError");
            return;
        }
        // This component runtime has no legacy editing command executor. The
        // command algorithm returns false for unsupported/disabled commands;
        // never claim a successful copy or dispatch an event without executing
        // it. Clipboard helpers can then use the implemented async clipboard API.
        self->record_feature("web-api", "Document.execCommand", "unsupported",
            "legacy editing commands return false; use navigator.clipboard for copying",
            "web-api-binding");
        info.GetReturnValue().Set(false);
    }

    static void return_empty_string(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        info.GetReturnValue().Set(js_string(info.GetIsolate(), ""));
    }

    static void location_to_string(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto local_context = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Value> href;
        if (info.This()->Get(
                local_context,
                js_string(info.GetIsolate(), "href")).ToLocal(&href)) {
            info.GetReturnValue().Set(href);
        }
    }

    static void return_null(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
    }

    static void get_selection(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto context = info.GetIsolate()->GetCurrentContext();
        auto selection = v8::Object::New(info.GetIsolate());
        auto no_op = v8::Function::New(context, console_log).ToLocalChecked();
        selection->Set(context, js_string(info.GetIsolate(), "removeAllRanges"), no_op).Check();
        selection->Set(context, js_string(info.GetIsolate(), "empty"), no_op).Check();
        selection->Set(context, js_string(info.GetIsolate(), "collapse"), no_op).Check();
        selection->Set(
            context,
            js_string(info.GetIsolate(), "toString"),
            v8::Function::New(context, return_empty_string).ToLocalChecked()).Check();
        selection->Set(context, js_string(info.GetIsolate(), "rangeCount"), v8::Integer::New(info.GetIsolate(), 0)).Check();
        info.GetReturnValue().Set(selection);
    }

    static bool insertion_would_create_cycle(
        const dom_node* parent,
        const dom_node* child) noexcept
    {
        if (parent == nullptr || child == nullptr) return false;
        for (auto* ancestor = parent; ancestor != nullptr; ancestor = ancestor->parent) {
            if (ancestor == child) return true;
        }
        return false;
    }

    static void throw_hierarchy_request(v8::Isolate* isolate, std::string_view operation)
    {
        const auto message = std::string(operation)
            + " cannot insert an ancestor into its descendant";
        isolate->ThrowException(v8::Exception::TypeError(js_string(
            isolate,
            message.c_str())));
    }

    static void append_child(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_mutation),
            self->profile_named("Node.appendChild"));
        auto* parent = unwrap_node(info.This());
        auto* child = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_node(info[0].As<v8::Object>())
            : nullptr;
        if (parent == nullptr || child == nullptr) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "appendChild requires a native node")));
            return;
        }
        if (parent != nullptr && child != nullptr && child->tag == "#document-fragment") {
            if (std::any_of(
                    child->children.begin(),
                    child->children.end(),
                    [parent](const auto* fragment_child) {
                        return insertion_would_create_cycle(parent, fragment_child);
                    })) {
                throw_hierarchy_request(info.GetIsolate(), "appendChild");
                return;
            }
            auto children = std::move(child->children);
            child->children.clear();
            for (auto* fragment_child : children) {
                if (fragment_child == nullptr) continue;
                fragment_child->parent = nullptr;
                fragment_child->visible = true;
                if (!self->document.append_child(*parent, *fragment_child)) continue;
                self->recascade_connected_subtree(*fragment_child);
                self->recascade_connected_node(*parent);
                self->activate_connected_stylesheet(*fragment_child);
                self->enqueue_connected_resources_subtree(
                    *fragment_child,
                    self->wrap_node(*fragment_child),
                    info.GetIsolate()->GetCurrentContext());
                self->enqueue_iframe_hydration_if_needed(*fragment_child);
                self->notify_custom_elements_subtree(*fragment_child, "connected");
            }
            synchronize_textarea_value_from_children(*parent);
            self->document.mark_dirty();
            info.GetReturnValue().Set(info[0]);
            return;
        }
        if (insertion_would_create_cycle(parent, child)) {
            throw_hierarchy_request(info.GetIsolate(), "appendChild");
            return;
        }
        if (child != nullptr && child->parent != nullptr) {
            if (self->is_connected(*child)) {
                self->notify_custom_elements_subtree(*child, "disconnected");
            }
            auto* old_parent = child->parent;
            std::erase(old_parent->children, child);
            synchronize_textarea_value_from_children(*old_parent);
            child->parent = nullptr;
        }
        if (!self->document.append_child(*parent, *child)) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "appendChild requires a detached native node")));
            return;
        }
        self->recascade_connected_subtree(*child);
        // Appending a child changes structural selectors on the container
        // itself. In particular, a dynamically populated `:empty` button must
        // stop matching before layout (TradingView's market-status pill uses
        // exactly this pattern).
        self->recascade_connected_node(*parent);
        self->activate_connected_stylesheet(*child);
        self->enqueue_connected_resources_subtree(
            *child,
            info[0].As<v8::Object>(),
            info.GetIsolate()->GetCurrentContext());
        self->enqueue_iframe_hydration_if_needed(*child);
        self->notify_custom_elements_subtree(*child, "connected");
        synchronize_textarea_value_from_children(*parent);
        info.GetReturnValue().Set(info[0]);
    }

    static void append_nodes(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_mutation),
            self->profile_named("ParentNode.append"));
        auto* parent = unwrap_node(info.This());
        if (parent == nullptr) return;

        std::vector<dom_node*> candidates;
        candidates.reserve(static_cast<size_t>(info.Length()));
        for (int index = 0; index < info.Length(); ++index) {
            dom_node* candidate = nullptr;
            if (info[index]->IsObject()) {
                candidate = unwrap_node(info[index].As<v8::Object>());
            }
            if (candidate == nullptr) {
                auto& text = self->document.create_element("#text");
                text.text_content = to_wtf8(info.GetIsolate(), info[index]);
                candidate = &text;
            }
            if (candidate->tag == "#document-fragment") {
                for (auto* child : candidate->children) {
                    if (insertion_would_create_cycle(parent, child)) {
                        throw_hierarchy_request(info.GetIsolate(), "append");
                        return;
                    }
                }
            } else if (insertion_would_create_cycle(parent, candidate)) {
                throw_hierarchy_request(info.GetIsolate(), "append");
                return;
            }
            candidates.push_back(candidate);
        }

        std::vector<dom_node*> inserted;
        for (auto* candidate : candidates) {
            if (candidate == nullptr) continue;
            if (candidate->tag == "#document-fragment") {
                auto children = std::move(candidate->children);
                candidate->children.clear();
                for (auto* child : children) {
                    if (child == nullptr) continue;
                    child->parent = nullptr;
                    child->visible = true;
                    if (self->document.append_child(*parent, *child)) inserted.push_back(child);
                }
                continue;
            }
            if (candidate->parent != nullptr) {
                if (self->is_connected(*candidate)) {
                    self->notify_custom_elements_subtree(*candidate, "disconnected");
                }
                auto* old_parent = candidate->parent;
                std::erase(old_parent->children, candidate);
                synchronize_textarea_value_from_children(*old_parent);
                candidate->parent = nullptr;
            }
            candidate->visible = true;
            if (self->document.append_child(*parent, *candidate)) inserted.push_back(candidate);
        }

        for (auto* child : inserted) {
            if (child == nullptr) continue;
            self->recascade_connected_subtree(*child);
            self->enqueue_connected_resources_subtree(
                *child,
                self->wrap_node(*child),
                info.GetIsolate()->GetCurrentContext());
            self->enqueue_iframe_hydration_if_needed(*child);
            self->notify_custom_elements_subtree(*child, "connected");
        }
        self->recascade_connected_node(*parent);
        self->activate_connected_stylesheet(*parent);
        synchronize_textarea_value_from_children(*parent);
        self->document.mark_dirty();
    }

    static void remove_child(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_mutation),
            self->profile_named("Node.removeChild"));
        auto* parent = unwrap_node(info.This());
        auto* child = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_node(info[0].As<v8::Object>())
            : nullptr;
        if (parent == nullptr || child == nullptr || child->parent != parent) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "removeChild requires a child of this native node")));
            return;
        }
        if (!self->blur_active_element_before_detach(*child)) return;
        if (self->is_connected(*child)) {
            self->notify_custom_elements_subtree(*child, "disconnected");
        }
        self->deactivate_connected_stylesheets(*child);
        std::erase(parent->children, child);
        child->parent = nullptr;
        self->weaken_detached_subtree_wrappers(*child);
        synchronize_textarea_value_from_children(*parent);
        self->document.mark_dirty();
        info.GetReturnValue().Set(info[0]);
    }

    static void replace_child(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* parent = unwrap_node(info.This());
        auto* replacement = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_node(info[0].As<v8::Object>())
            : nullptr;
        auto* previous = info.Length() > 1 && info[1]->IsObject()
            ? unwrap_node(info[1].As<v8::Object>())
            : nullptr;
        if (parent == nullptr || replacement == nullptr || previous == nullptr
            || previous->parent != parent) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "replaceChild requires a child of this native node")));
            return;
        }
        if (replacement == previous) {
            info.GetReturnValue().Set(info[1]);
            return;
        }
        std::vector<dom_node*> replacements;
        if (replacement->tag == "#document-fragment") {
            replacements = replacement->children;
        } else {
            replacements.push_back(replacement);
        }
        if (std::any_of(
                replacements.begin(),
                replacements.end(),
                [parent](const auto* candidate) {
                    return insertion_would_create_cycle(parent, candidate);
                })) {
            throw_hierarchy_request(info.GetIsolate(), "replaceChild");
            return;
        }
        if (!self->blur_active_element_before_detach(*previous)) return;
        for (auto* candidate : replacements) {
            if (candidate == nullptr || candidate == previous) continue;
            if (candidate->parent != nullptr) {
                if (self->is_connected(*candidate)) {
                    self->notify_custom_elements_subtree(*candidate, "disconnected");
                }
                auto* old_parent = candidate->parent;
                std::erase(old_parent->children, candidate);
                synchronize_textarea_value_from_children(*old_parent);
                candidate->parent = nullptr;
            }
        }
        if (replacement->tag == "#document-fragment") {
            replacement->children.clear();
        }
        const auto iterator = std::find(parent->children.begin(), parent->children.end(), previous);
        if (iterator == parent->children.end()) return;
        if (self->is_connected(*previous)) {
            self->notify_custom_elements_subtree(*previous, "disconnected");
        }
        self->deactivate_connected_stylesheets(*previous, false);
        const auto insertion_index = static_cast<size_t>(
            iterator - parent->children.begin());
        parent->children.erase(iterator);
        previous->parent = nullptr;
        self->weaken_detached_subtree_wrappers(*previous);
        auto next_index = insertion_index;
        for (auto* candidate : replacements) {
            if (candidate == nullptr || candidate == previous) continue;
            candidate->parent = parent;
            candidate->visible = true;
            parent->children.insert(
                parent->children.begin()
                    + static_cast<std::ptrdiff_t>(next_index++),
                candidate);
            self->recascade_connected_subtree(*candidate);
            self->activate_connected_stylesheet(*candidate);
            self->enqueue_connected_resources_subtree(
                *candidate,
                self->wrap_node(*candidate),
                info.GetIsolate()->GetCurrentContext());
            self->enqueue_iframe_hydration_if_needed(*candidate);
            self->notify_custom_elements_subtree(*candidate, "connected");
        }
        synchronize_textarea_value_from_children(*parent);
        self->document.mark_dirty();
        info.GetReturnValue().Set(info[1]);
    }

    static void replace_children(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* parent = unwrap_node(info.This());
        if (parent == nullptr) return;
        for (int index = 0; index < info.Length(); ++index) {
            if (!info[index]->IsObject()) continue;
            auto* child = unwrap_node(info[index].As<v8::Object>());
            if (insertion_would_create_cycle(parent, child)) {
                throw_hierarchy_request(info.GetIsolate(), "replaceChildren");
                return;
            }
        }
        if (!self->blur_active_descendant_before_replacing_children(*parent)) return;
        self->detach_all_children(*parent);
        for (int index = 0; index < info.Length(); ++index) {
            if (!info[index]->IsObject()) continue;
            auto* child = unwrap_node(info[index].As<v8::Object>());
            if (child == nullptr) continue;
            if (child->parent != nullptr) {
                if (self->is_connected(*child)) {
                    self->notify_custom_elements_subtree(*child, "disconnected");
                }
                auto* old_parent = child->parent;
                std::erase(old_parent->children, child);
                synchronize_textarea_value_from_children(*old_parent);
                child->parent = nullptr;
            }
            self->document.append_child(*parent, *child);
        }
        self->recascade_connected_subtree(*parent);
        self->activate_connected_stylesheet(*parent);
        self->notify_custom_elements_subtree(*parent, "connected");
        synchronize_textarea_value_from_children(*parent);
    }

    static void remove_element(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (node == nullptr || node->parent == nullptr) return;
        auto* parent = node->parent;
        if (!self->blur_active_element_before_detach(*node)) return;
        if (self->is_connected(*node)) {
            self->notify_custom_elements_subtree(*node, "disconnected");
        }
        self->deactivate_connected_stylesheets(*node);
        std::erase(node->parent->children, node);
        node->parent = nullptr;
        node->visible = false;
        self->weaken_detached_subtree_wrappers(*node);
        synchronize_textarea_value_from_children(*parent);
        self->document.mark_dirty();
    }

    static void replace_with(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_mutation),
            self->profile_named("ChildNode.replaceWith"));
        auto* replaced = unwrap_node(info.This());
        if (replaced == nullptr || replaced->parent == nullptr) return;
        auto* parent = replaced->parent;

        std::vector<dom_node*> replacements;
        for (int argument = 0; argument < info.Length(); ++argument) {
            dom_node* replacement = nullptr;
            if (info[argument]->IsObject()) {
                replacement = unwrap_node(info[argument].As<v8::Object>());
            } else {
                auto& text = self->document.create_element("#text");
                text.text_content = to_wtf8(info.GetIsolate(), info[argument]);
                replacement = &text;
            }
            if (replacement == nullptr) continue;
            if (replacement != replaced
                && insertion_would_create_cycle(parent, replacement)) {
                throw_hierarchy_request(info.GetIsolate(), "replaceWith");
                return;
            }
            if (replacement->tag == "#document-fragment") {
                for (auto* child : replacement->children) {
                    if (insertion_would_create_cycle(parent, child)) {
                        throw_hierarchy_request(info.GetIsolate(), "replaceWith");
                        return;
                    }
                }
            }
            replacements.push_back(replacement);
        }

        if (replacements.size() == 1U && replacements.front() == replaced) return;
        if (!self->blur_active_element_before_detach(*replaced)) return;

        // Detach every incoming node before locating the final replacement
        // point. This preserves identity when the source comes from Monaco's
        // temporary parsing container and avoids stale sibling indices when a
        // node is moved within the same parent.
        std::vector<dom_node*> flattened;
        for (auto* replacement : replacements) {
            if (replacement == nullptr || replacement == replaced) continue;
            if (replacement->tag == "#document-fragment") {
                auto children = std::move(replacement->children);
                replacement->children.clear();
                for (auto* child : children) {
                    if (child == nullptr) continue;
                    child->parent = nullptr;
                    flattened.push_back(child);
                }
                continue;
            }
            if (replacement->parent != nullptr) {
                if (self->is_connected(*replacement)) {
                    self->notify_custom_elements_subtree(
                        *replacement, "disconnected");
                }
                auto* old_parent = replacement->parent;
                std::erase(old_parent->children, replacement);
                synchronize_textarea_value_from_children(*old_parent);
                replacement->parent = nullptr;
            }
            flattened.push_back(replacement);
        }

        const auto position = std::find(
            parent->children.begin(), parent->children.end(), replaced);
        if (position == parent->children.end()) return;
        auto insertion_index =
            static_cast<size_t>(position - parent->children.begin());
        if (self->is_connected(*replaced)) {
            self->notify_custom_elements_subtree(*replaced, "disconnected");
        }
        self->deactivate_connected_stylesheets(*replaced);
        parent->children.erase(position);
        replaced->parent = nullptr;
        replaced->visible = false;
        self->weaken_detached_subtree_wrappers(*replaced);

        for (auto* replacement : flattened) {
            replacement->parent = parent;
            replacement->visible = true;
            parent->children.insert(
                parent->children.begin()
                    + static_cast<std::ptrdiff_t>(insertion_index++),
                replacement);
            self->recascade_connected_subtree(*replacement);
            self->activate_connected_stylesheet(*replacement);
            self->enqueue_connected_resources_subtree(
                *replacement,
                self->wrap_node(*replacement),
                info.GetIsolate()->GetCurrentContext());
            self->enqueue_iframe_hydration_if_needed(*replacement);
            self->notify_custom_elements_subtree(*replacement, "connected");
        }
        synchronize_textarea_value_from_children(*parent);
        self->document.mark_dirty();
    }

    static void insert_adjacent_html(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_mutation),
            self->profile_named("Element.insertAdjacentHTML"));
        auto* reference = unwrap_node(info.This());
        if (reference == nullptr || info.Length() < 2) return;
        const auto position = lower_html_name(to_utf8(info.GetIsolate(), info[0]));

        dom_node* parent = nullptr;
        size_t insertion_index = 0U;
        if (position == "afterbegin" || position == "beforeend") {
            parent = reference;
            insertion_index =
                position == "afterbegin" ? 0U : parent->children.size();
        } else if ((position == "beforebegin" || position == "afterend")
            && reference->parent != nullptr) {
            parent = reference->parent;
            const auto found = std::find(
                parent->children.begin(), parent->children.end(), reference);
            if (found == parent->children.end()) return;
            insertion_index =
                static_cast<size_t>(found - parent->children.begin())
                + (position == "afterend" ? 1U : 0U);
        } else {
            info.GetIsolate()->ThrowException(v8::Exception::SyntaxError(
                js_string(info.GetIsolate(), "Invalid insertAdjacentHTML position")));
            return;
        }

        auto& fragment = self->document.create_element("#document-fragment");
        fragment.visible = false;
        const auto html = to_utf8(info.GetIsolate(), info[1]);
        if (!html.empty()) self->parse_inner_html(fragment, html, parent);
        auto children = std::move(fragment.children);
        fragment.children.clear();
        for (auto* child : children) {
            if (child == nullptr) continue;
            child->parent = parent;
            child->visible = true;
            parent->children.insert(
                parent->children.begin()
                    + static_cast<std::ptrdiff_t>(insertion_index++),
                child);
            self->recascade_connected_subtree(*child);
            self->activate_connected_stylesheet(*child);
            self->enqueue_connected_resources_subtree(
                *child,
                self->wrap_node(*child),
                info.GetIsolate()->GetCurrentContext());
            self->enqueue_iframe_hydration_if_needed(*child);
            self->notify_custom_elements_subtree(*child, "connected");
        }
        synchronize_textarea_value_from_children(*parent);
        self->document.mark_dirty();
    }

    static void insert_adjacent_element(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_mutation),
            self->profile_named("Element.insertAdjacentElement"));
        auto* reference = unwrap_node(info.This());
        auto* child = info.Length() > 1 && info[1]->IsObject()
            ? unwrap_node(info[1].As<v8::Object>())
            : nullptr;
        if (reference == nullptr || child == nullptr || reference == child || info.Length() < 1) return;
        const auto position = to_utf8(info.GetIsolate(), info[0]);
        dom_node* parent = nullptr;
        size_t index = 0;
        if (position == "afterbegin" || position == "beforeend") {
            parent = reference;
            index = position == "afterbegin" ? 0U : parent->children.size();
        } else if ((position == "beforebegin" || position == "afterend")
            && reference->parent != nullptr) {
            parent = reference->parent;
            const auto iterator = std::find(parent->children.begin(), parent->children.end(), reference);
            if (iterator == parent->children.end()) return;
            index = static_cast<size_t>(iterator - parent->children.begin())
                + (position == "afterend" ? 1U : 0U);
        } else {
            return;
        }
        if (insertion_would_create_cycle(parent, child)) {
            throw_hierarchy_request(info.GetIsolate(), "insertAdjacentElement");
            return;
        }
        if (child->parent != nullptr) {
            if (self->is_connected(*child)) {
                self->notify_custom_elements_subtree(*child, "disconnected");
            }
            auto* old_parent = child->parent;
            const auto old = std::find(old_parent->children.begin(), old_parent->children.end(), child);
            if (old != old_parent->children.end()) {
                const auto old_index = static_cast<size_t>(old - old_parent->children.begin());
                old_parent->children.erase(old);
                synchronize_textarea_value_from_children(*old_parent);
                if (old_parent == parent && old_index < index) --index;
            }
            child->parent = nullptr;
        }
        index = std::min(index, parent->children.size());
        child->parent = parent;
        parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(index), child);
        self->document.mark_dirty();
        self->recascade_connected_subtree(*child);
        self->activate_connected_stylesheet(*child);
        self->notify_custom_elements_subtree(*child, "connected");
        synchronize_textarea_value_from_children(*parent);
        info.GetReturnValue().Set(info[1]);
    }

    static void prepend_child(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* parent = unwrap_node(info.This());
        auto* child = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_node(info[0].As<v8::Object>()) : nullptr;
        if (parent == nullptr || child == nullptr || parent == child) return;
        if (insertion_would_create_cycle(parent, child)) {
            throw_hierarchy_request(info.GetIsolate(), "prepend");
            return;
        }
        if (child->parent != nullptr) {
            if (self->is_connected(*child)) {
                self->notify_custom_elements_subtree(*child, "disconnected");
            }
            auto* old_parent = child->parent;
            std::erase(old_parent->children, child);
            synchronize_textarea_value_from_children(*old_parent);
            child->parent = nullptr;
        }
        child->parent = parent;
        parent->children.insert(parent->children.begin(), child);
        self->document.mark_dirty();
        self->recascade_connected_subtree(*child);
        self->activate_connected_stylesheet(*child);
        self->notify_custom_elements_subtree(*child, "connected");
        synchronize_textarea_value_from_children(*parent);
        info.GetReturnValue().Set(info[0]);
    }

    static void insert_sibling(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        bool after)
    {
        auto* self = current(info.GetIsolate());
        auto* reference = unwrap_node(info.This());
        auto* child = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_node(info[0].As<v8::Object>()) : nullptr;
        if (reference == nullptr || reference->parent == nullptr || child == nullptr
            || reference == child) return;
        auto* parent = reference->parent;
        if (insertion_would_create_cycle(parent, child)) {
            throw_hierarchy_request(info.GetIsolate(), after ? "after" : "before");
            return;
        }
        if (child->parent != nullptr) {
            if (self->is_connected(*child)) {
                self->notify_custom_elements_subtree(*child, "disconnected");
            }
            auto* old_parent = child->parent;
            std::erase(old_parent->children, child);
            synchronize_textarea_value_from_children(*old_parent);
            child->parent = nullptr;
        }
        const auto position = std::find(parent->children.begin(), parent->children.end(), reference);
        if (position == parent->children.end()) return;
        child->parent = parent;
        parent->children.insert(position + (after ? 1 : 0), child);
        self->document.mark_dirty();
        self->recascade_connected_subtree(*child);
        self->activate_connected_stylesheet(*child);
        self->notify_custom_elements_subtree(*child, "connected");
        synchronize_textarea_value_from_children(*parent);
    }

    static void insert_before_self(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        insert_sibling(info, false);
    }

    static void insert_after_self(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        insert_sibling(info, true);
    }

    static void insert_before(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_mutation),
            self->profile_named("Node.insertBefore"));
        self->record_feature(
            "dom",
            "method:Node.insertBefore",
            "supported",
            {},
            "dom-binding");
        auto* parent = unwrap_node(info.This());
        auto* child = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_node(info[0].As<v8::Object>())
            : nullptr;
        auto* before = info.Length() > 1 && info[1]->IsObject()
            ? unwrap_node(info[1].As<v8::Object>())
            : nullptr;
        if (parent == nullptr || child == nullptr) return;
        if (child->tag == "#document-fragment") {
            if (std::any_of(
                    child->children.begin(),
                    child->children.end(),
                    [parent](const auto* fragment_child) {
                        return insertion_would_create_cycle(parent, fragment_child);
                    })) {
                throw_hierarchy_request(info.GetIsolate(), "insertBefore");
                return;
            }
            auto position = before == nullptr
                ? parent->children.end()
                : std::find(parent->children.begin(), parent->children.end(), before);
            if (before != nullptr && position == parent->children.end()) {
                info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                    js_string(info.GetIsolate(), "insertBefore reference is not a child")));
                return;
            }
            auto insertion_index = static_cast<size_t>(position - parent->children.begin());
            auto children = std::move(child->children);
            child->children.clear();
            for (auto* fragment_child : children) {
                if (fragment_child == nullptr) continue;
                fragment_child->parent = parent;
                fragment_child->visible = true;
                parent->children.insert(
                    parent->children.begin() + static_cast<std::ptrdiff_t>(insertion_index++),
                    fragment_child);
                self->recascade_connected_subtree(*fragment_child);
                self->activate_connected_stylesheet(*fragment_child);
                self->notify_custom_elements_subtree(
                    *fragment_child, "connected");
            }
            synchronize_textarea_value_from_children(*parent);
            self->document.mark_dirty();
            info.GetReturnValue().Set(info[0]);
            return;
        }
        if (insertion_would_create_cycle(parent, child)) {
            throw_hierarchy_request(info.GetIsolate(), "insertBefore");
            return;
        }
        // The DOM pre-insertion algorithm validates the reference before it
        // mutates either tree. In particular, insertBefore(node, node) on the
        // node's current parent is a no-op. Detaching first made that valid
        // self-reference disappear from parent->children, then incorrectly
        // threw and left the node detached. It also made every rejected
        // foreign reference partially mutate the source tree.
        if (before != nullptr) {
            const auto reference = std::find(
                parent->children.begin(), parent->children.end(), before);
            if (reference == parent->children.end()) {
                info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                    js_string(info.GetIsolate(), "insertBefore reference is not a child")));
                return;
            }
            if (before == child) {
                self->record_feature(
                    "dom",
                    "insertBefore:self-reference",
                    "supported",
                    {},
                    "dom-pre-insertion");
                self->record_composition(
                    "dom-insertbefore-self-reference",
                    child->id);
                info.GetReturnValue().Set(info[0]);
                return;
            }
        }
        if (child->parent != nullptr) {
            if (self->is_connected(*child)) {
                self->notify_custom_elements_subtree(*child, "disconnected");
            }
            auto* old_parent = child->parent;
            std::erase(old_parent->children, child);
            synchronize_textarea_value_from_children(*old_parent);
            child->parent = nullptr;
        }
        if (before == nullptr) {
            if (self->document.append_child(*parent, *child)) {
                self->recascade_connected_subtree(*child);
                self->activate_connected_stylesheet(*child);
                self->notify_custom_elements_subtree(*child, "connected");
                synchronize_textarea_value_from_children(*parent);
                info.GetReturnValue().Set(info[0]);
            }
            return;
        }
        const auto position = std::find(parent->children.begin(), parent->children.end(), before);
        // `before` was validated before detaching `child`; because the
        // self-reference case returned above, it must still be present.
        if (position == parent->children.end()) return;
        child->parent = parent;
        parent->children.insert(position, child);
        self->document.mark_dirty();
        self->recascade_connected_subtree(*child);
        self->activate_connected_stylesheet(*child);
        self->notify_custom_elements_subtree(*child, "connected");
        synchronize_textarea_value_from_children(*parent);
        info.GetReturnValue().Set(info[0]);
    }
