    implementation(
        native_document& document_value,
        std::function<v8_dom_runtime::viewport_metrics()> viewport_provider_value,
        std::string compilation_cache_directory_value,
        resource_loader resource_loader_value,
        std::function<void()> host_request_available_value,
        std::function<void()> interop_callback_available_value,
        v8_dom_runtime::interop_callback_sink_v3
            interop_callback_sink_value)
        : document(document_value)
        , viewport_provider(std::move(viewport_provider_value))
        , load_resource_callback(std::move(resource_loader_value))
        , host_request_available(std::move(host_request_available_value))
        , interop_callback_available(std::move(interop_callback_available_value))
        , interop_callback_sink(std::move(interop_callback_sink_value))
        , compilation_cache_directory(std::move(compilation_cache_directory_value))
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        , profile_bindings(std::getenv("WEBSCENE_PROBE_PROFILE_BINDINGS") != nullptr)
        , profile_resize_cpu(
            std::getenv("WEBSCENE_PROBE_PROFILE_RESIZE_CPU") != nullptr)
        , profile_startup(std::getenv("WEBSCENE_PROBE_PROFILE_STARTUP") != nullptr)
        , profile_css(std::getenv("WEBSCENE_PROBE_PROFILE_CSS") != nullptr)
#endif
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (profile_startup) {
            startup_profile_started = std::chrono::steady_clock::now();
        }
#endif
    }

    ~implementation()
    {
        // Resource prefetch tasks capture this runtime and may still be inside
        // the host loader when teardown begins. Join them while the loader and
        // all of its bridge state are still alive.
        for (auto& [key, future] : prefetched_resources) {
            static_cast<void>(key);
            if (!future.valid()) continue;
            try {
                static_cast<void>(future.get());
            } catch (...) {
            }
        }
        prefetched_resources.clear();
        // Stop and join every network worker before releasing persistent V8
        // handles or disposing the isolate. Socket callbacks never touch V8,
        // but they may still be appending to the native delivery queue.
        websocket_transport.shutdown();
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        shutdown_inspector();
#endif
        auto isolate_locker = lock_shared_isolate();
        websocket_bindings.clear();
        if (isolate != nullptr && !compilation_cache_directory.empty()) {
            v8::Isolate::Scope isolate_scope(isolate);
            v8::HandleScope handle_scope(isolate);
            for (auto& [key, entry] : compilation_cache) {
                static_cast<void>(key);
                if (entry.source_bytes < minimum_hot_cache_source_bytes) continue;
                const auto script = entry.script.Get(isolate);
                if (script.IsEmpty()) continue;
                std::unique_ptr<v8::ScriptCompiler::CachedData> generated(
                    v8::ScriptCompiler::CreateCodeCache(script));
                if (generated) write_compilation_cache(entry.key_digest, *generated);
            }
        }
        resize_listeners.clear();
        timers.clear();
        pending_window_messages.clear();
        pending_programmatic_scroll_events.clear();
        pending_interop_promises.clear();
        pending_callback_promises.clear();
        interop_handles.clear();
        pending_promise_rejections.clear();
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        if (auto* inspector_state = inspector_state_if_created()) {
            inspector_state->promise_rejections.clear();
        }
#endif
        connected_resources.clear();
        resize_observers.clear();
        frame_documents.clear();
        frame_windows.clear();
        frame_window_scroll_offsets.clear();
        frame_load_listeners.clear();
        frame_event_listeners.clear();
        actual_frame_windows.clear();
        actual_frame_contexts.clear();
        actual_frame_documents.clear();
        provisional_frame_bodies.clear();
        canvas_contexts.clear();
        canvas_states.clear();
        for (auto& [key, entry] : compilation_cache) {
            static_cast<void>(key);
            entry.script.Reset();
        }
        compilation_cache.clear();
        compilation_cache_order.clear();
        computed_style_wrappers.clear();
        class_list_wrappers.clear();
        style_wrappers.clear();
        stylesheet_wrappers.clear();
        detached_documents.clear();
        node_wrappers.clear();
        attr_wrappers.clear();
        attached_attributes.clear();
        attr_states.clear();
        dom_implementation_object.Reset();
        document_object.Reset();
        style_template.Reset();
        frame_document_template.Reset();
        frame_window_template.Reset();
        html_collection_template.Reset();
        document_template.Reset();
        element_template.Reset();
#if defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
        html_element_template.Reset();
        html_form_element_template.Reset();
        html_select_element_template.Reset();
        html_script_element_template.Reset();
        html_slot_element_template.Reset();
        html_style_element_template.Reset();
        shadow_root_template.Reset();
        document_fragment_template.Reset();
        processing_instruction_template.Reset();
        comment_template.Reset();
        text_template.Reset();
        character_data_template.Reset();
        attr_template.Reset();
        node_template.Reset();
        event_target_template.Reset();
#endif
        frame_context.Reset();
        context.Reset();
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (cpu_profiler != nullptr) {
            cpu_profiler->Dispose();
            cpu_profiler = nullptr;
        }
#endif
        if (isolate != nullptr && shared_isolate == nullptr) {
            isolate->Dispose();
        }
        isolate = nullptr;
        delete allocator;
        allocator = nullptr;
        // The final shared owner disposes the isolate. Release the recursive
        // V8 lock first so its destructor never observes an already-disposed
        // isolate.
        isolate_locker.reset();
        release_shared_isolate(shared_isolate);
        shared_isolate.reset();
    }

    static implementation* current(v8::Isolate* isolate)
    {
        const auto local_context = isolate->GetCurrentContext();
        if (!local_context.IsEmpty()) {
            if (auto* owner = static_cast<implementation*>(
                    local_context->GetAlignedPointerFromEmbedderData(
                        runtime_context_embedder_slot,
                        v8::kEmbedderDataTypeTagDefault));
                owner != nullptr) {
                return owner;
            }
        }
        return static_cast<implementation*>(isolate->GetData(0));
    }

    struct feature_observation final {
        std::string category;
        std::string feature;
        std::string classification;
        std::string semantic_slice;
        std::string source;
        std::atomic<uint64_t> count{0};

        feature_observation(
            std::string category_value,
            std::string feature_value,
            std::string classification_value,
            std::string semantic_slice_value,
            std::string source_value)
            : category(std::move(category_value)),
              feature(std::move(feature_value)),
              classification(std::move(classification_value)),
              semantic_slice(std::move(semantic_slice_value)),
              source(std::move(source_value))
        {
        }

        feature_observation(const feature_observation&) = delete;
        feature_observation& operator=(const feature_observation&) = delete;
    };

    struct feature_decision final {
        implementation& owner;
        std::string category;
        std::string feature;
        std::string classification{"supported"};
        std::string semantic_slice;
        std::string source;

        feature_decision(
            implementation& owner_value,
            std::string category_value,
            std::string feature_value,
            std::string source_value)
            : owner(owner_value),
              category(std::move(category_value)),
              feature(std::move(feature_value)),
              source(std::move(source_value))
        {
        }

        feature_decision(const feature_decision&) = delete;
        feature_decision& operator=(const feature_decision&) = delete;

        ~feature_decision()
        {
            owner.record_feature(
                std::move(category),
                std::move(feature),
                std::move(classification),
                std::move(semantic_slice),
                std::move(source));
        }
    };

    struct composition_feature final {
        std::string_view category;
        std::string_view feature;
    };

    struct composition_detector final {
        std::string_view id;
        std::string_view category;
        std::vector<composition_feature> features;
    };

    static const std::vector<composition_detector>& composition_detectors()
    {
        static const std::vector<composition_detector> detectors{
            {
                "css-transform-origin-independent-cascade",
                "css-computed-style",
                {{"css", "property:transform"}, {"css", "property:transform-origin"}}
            },
            {
                "css-transform-transition",
                "css-computed-style-temporal",
                {{"css", "property:transform"}, {"css", "property:transition"}}
            },
            {
                "css-shadow-radius-overflow",
                "css-computed-style-subtree",
                {
                    {"css", "property:box-shadow"},
                    {"css", "property:border-radius"},
                    {"css", "property:overflow"}
                }
            },
            {
                "css-flex-overflow-constrained-size",
                "css-computed-style",
                {
                    {"css", "property:display"},
                    {"css", "property:overflow"},
                    {"css", "property:height"}
                }
            },
            {
                "css-grid-gap-positioning",
                "css-computed-style-subtree",
                {
                    {"css", "property:grid-template-columns"},
                    {"css", "property:gap"},
                    {"css", "property:position"}
                }
            },
            {
                "css-custom-property-to-paint",
                "css-value-resolution",
                {
                    {"css", "custom-property"},
                    {"css", "function:var"},
                    {"css", "paint-property"}
                }
            },
            {
                "dom-insertbefore-self-reference",
                "dom-tree-mutation",
                {
                    {"dom", "method:Node.insertBefore"},
                    {"dom", "insertBefore:self-reference"}
                }
            }
        };
        return detectors;
    }

    void record_composition(std::string_view id, uint32_t node_id)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        std::lock_guard lock(feature_observation_mutex);
        composition_observed_nodes[std::string(id)].insert(node_id);
#else
        static_cast<void>(id);
        static_cast<void>(node_id);
#endif
    }

    static bool nonzero_length(const css_length& value)
    {
        return std::abs(value.value) > 0.000001F
            || std::abs(value.pixel_offset) > 0.000001F;
    }

    static bool has_rounded_corner(const node_style& style)
    {
        return nonzero_length(style.border_top_left_radius)
            || nonzero_length(style.border_top_right_radius)
            || nonzero_length(style.border_bottom_right_radius)
            || nonzero_length(style.border_bottom_left_radius);
    }

    static bool has_constrained_size(const node_style& style)
    {
        return nonzero_length(style.width) || nonzero_length(style.height)
            || nonzero_length(style.min_width) || nonzero_length(style.min_height)
            || nonzero_length(style.max_width) || nonzero_length(style.max_height);
    }

    static bool has_nonvisible_overflow(const node_style& style)
    {
        return style.overflow_x != overflow_mode::visible
            || style.overflow_y != overflow_mode::visible;
    }

    void detect_css_compositions(dom_node& node)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (node.style.transform_specified && node.style.transform_origin_specified) {
            record_composition("css-transform-origin-independent-cascade", node.id);
        }
        if (node.style.transform_specified
            && node.style.animations().transform_transition.duration_ms > 0) {
            record_composition("css-transform-transition", node.id);
        }
        if (node.style.animations().opacity_transition.duration_ms > 0) {
            record_composition("css-opacity-transition", node.id);
        }
        if (node.style.animations().color_transition.duration_ms > 0) {
            record_composition("css-color-transition", node.id);
        }
        if (node.style.box_shadow_present && has_rounded_corner(node.style)) {
            auto clipped = has_nonvisible_overflow(node.style);
            // A shadow is also affected by clipping established on an ancestor.
            // Bound the walk so this remains a local CSS composition detector,
            // rather than claiming whole-document paint-order completeness.
            auto* ancestor = node.parent;
            for (size_t depth = 0; !clipped && ancestor != nullptr && depth < 4U;
                 ++depth, ancestor = ancestor->parent) {
                clipped = has_nonvisible_overflow(ancestor->style);
            }
            if (clipped) record_composition("css-shadow-radius-overflow", node.id);
        }
        if (node.style.display == display_mode::flex && has_constrained_size(node.style)
            && has_nonvisible_overflow(node.style)) {
            record_composition("css-flex-overflow-constrained-size", node.id);
        }

        const auto grid_with_gap = [](const dom_node& candidate) {
            return candidate.style.display == display_mode::grid
                && !candidate.style.grid().template_columns.empty()
                && (nonzero_length(candidate.style.row_gap)
                    || nonzero_length(candidate.style.column_gap));
        };
        if (grid_with_gap(node)) {
            const auto positioned_child = std::any_of(
                node.children.begin(), node.children.end(), [](const auto* child) {
                    return child != nullptr
                        && child->style.position != position_mode::normal;
                });
            if (node.style.position != position_mode::normal || positioned_child) {
                record_composition("css-grid-gap-positioning", node.id);
            }
        } else if (node.style.position != position_mode::normal
            && node.parent != nullptr && grid_with_gap(*node.parent)) {
            record_composition("css-grid-gap-positioning", node.parent->id);
        }
#else
        static_cast<void>(node);
#endif
    }

    static bool is_paint_property(std::string_view name)
    {
        return name == "background" || name == "background-color" || name == "color"
            || name == "fill" || name == "stroke"
            || name == "box-shadow" || name == "border" || name == "border-color"
            || name == "border-left" || name == "border-top" || name == "border-right"
            || name == "border-bottom" || name == "border-left-color"
            || name == "border-top-color" || name == "border-right-color"
            || name == "border-bottom-color" || name == "outline"
            || name == "outline-color";
    }

    static std::string resolved_cursor(dom_node& node)
    {
        for (auto* current = &node; current != nullptr; current = current->parent) {
            if (!current->style.textual().cursor.empty()
                && current->style.textual().cursor != "inherit"
                && current->style.textual().cursor != "unset") {
                return current->style.textual().cursor;
            }
        }
        return "auto";
    }

    static uint32_t cursor_kind_for(dom_node& node)
    {
        const auto value = lower_html_name(resolved_cursor(node));
        if (value == "pointer" || value == "hand") return WEBSCENE_CURSOR_POINTER;
        if (value == "text" || value == "vertical-text") return WEBSCENE_CURSOR_TEXT;
        if (value == "crosshair") return WEBSCENE_CURSOR_CROSSHAIR;
        if (value == "wait" || value == "progress") return WEBSCENE_CURSOR_WAIT;
        if (value == "move" || value == "grab" || value == "grabbing"
            || value == "all-scroll") return WEBSCENE_CURSOR_MOVE;
        if (value == "not-allowed" || value == "no-drop") return WEBSCENE_CURSOR_NOT_ALLOWED;
        if (value == "help") return WEBSCENE_CURSOR_HELP;
        return WEBSCENE_CURSOR_DEFAULT;
    }

    void record_feature(
        std::string category,
        std::string feature,
        std::string classification,
        std::string semantic_slice = {},
        std::string source = {})
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto key = category + '\x1f' + feature + '\x1f' + classification
            + '\x1f' + semantic_slice + '\x1f' + source;
        if (const auto cached = feature_observation_writer_cache.find(key);
            cached != feature_observation_writer_cache.end()) {
            cached->second->count.fetch_add(1U, std::memory_order_relaxed);
            return;
        }
        std::lock_guard lock(feature_observation_mutex);
        auto [entry, inserted] = feature_observations.try_emplace(
            key,
            std::move(category),
            std::move(feature),
            std::move(classification),
            std::move(semantic_slice),
            std::move(source));
        static_cast<void>(inserted);
        feature_observation_writer_cache.emplace(key, &entry->second);
        entry->second.count.fetch_add(1U, std::memory_order_relaxed);
#else
        static_cast<void>(category);
        static_cast<void>(feature);
        static_cast<void>(classification);
        static_cast<void>(semantic_slice);
        static_cast<void>(source);
#endif
    }

    void record_canvas_feature(const char* feature)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        // Canvas methods are an exceptionally hot certification surface: a
        // chart frame can cross the V8/native boundary thousands of times.
        // The generic writer deliberately accepts dynamic strings, but paying
        // for five string copies plus a composite key on every draw call makes
        // feature telemetry part of the rendering budget. Canvas call sites
        // pass stable string literals, so cache by literal identity after the
        // first observation while preserving the exact invocation count.
        if (const auto cached = canvas_feature_observation_writer_cache.find(feature);
            cached != canvas_feature_observation_writer_cache.end()) {
            cached->second->count.fetch_add(1U, std::memory_order_relaxed);
            return;
        }
        auto category = std::string("canvas");
        auto feature_name =
            std::string("CanvasRenderingContext2D.") + feature;
        auto classification = std::string("supported");
        auto source = std::string("native-binding");
        const auto key = category + '\x1f' + feature_name + '\x1f'
            + classification + "\x1f\x1f" + source;
        std::lock_guard lock(feature_observation_mutex);
        auto [entry, inserted] = feature_observations.try_emplace(
            key,
            std::move(category),
            std::move(feature_name),
            std::move(classification),
            std::string{},
            std::move(source));
        static_cast<void>(inserted);
        feature_observation_writer_cache.emplace(key, &entry->second);
        canvas_feature_observation_writer_cache.emplace(feature, &entry->second);
        entry->second.count.fetch_add(1U, std::memory_order_relaxed);
#else
        static_cast<void>(feature);
#endif
    }

    static void append_json_string(std::ostringstream& output, std::string_view value)
    {
        output << '"';
        constexpr char hex[] = "0123456789abcdef";
        for (const auto character : value) {
            const auto byte = static_cast<unsigned char>(character);
            switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (byte < 0x20U) {
                    output << "\\u00" << hex[byte >> 4U] << hex[byte & 0x0FU];
                } else {
                    output << character;
                }
                break;
            }
        }
        output << '"';
    }

    std::string feature_use_json() const
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        return R"({"schema":"webscene-native-feature-use-v2","complete":false,"telemetryEnabled":false,"incompleteCategories":["telemetry-disabled"],"observations":[],"compositionDiscovery":{"schema":"webscene-native-composition-use-v1","complete":false,"incompleteCategories":["telemetry-disabled"],"detectors":[],"observations":[]}})";
#else
        std::lock_guard lock(feature_observation_mutex);
        std::unordered_set<std::string> dispatched_event_features;
        for (const auto& [key, observation] : feature_observations) {
            static_cast<void>(key);
            if (observation.classification == "supported"
                && observation.source == "event-dispatch") {
                dispatched_event_features.insert(
                    observation.category + '\x1f' + observation.feature);
            }
        }
        std::vector<const feature_observation*> ordered;
        ordered.reserve(feature_observations.size());
        for (const auto& [key, observation] : feature_observations) {
            static_cast<void>(key);
            if (observation.classification == "unobserved-code-path"
                && observation.source == "event-listener-registration"
                && dispatched_event_features.contains(
                    observation.category + '\x1f' + observation.feature)) {
                continue;
            }
            ordered.push_back(&observation);
        }
        std::sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
            return std::tie(
                left->category,
                left->feature,
                left->classification,
                left->semantic_slice,
                left->source)
                < std::tie(
                    right->category,
                    right->feature,
                    right->classification,
                    right->semantic_slice,
                    right->source);
        });
        std::ostringstream output;
        output << R"({"schema":"webscene-native-feature-use-v2","complete":false,"incompleteCategories":["html-attributes","css-values-functions-selectors-at-rules","dom-bindings-events","web-apis-storage","fonts-resources-cache"],"observations":[)";
        for (size_t index = 0; index < ordered.size(); ++index) {
            if (index != 0U) output << ',';
            const auto& observation = *ordered[index];
            output << "{\"category\":";
            append_json_string(output, observation.category);
            output << ",\"feature\":";
            append_json_string(output, observation.feature);
            output << ",\"classification\":";
            append_json_string(output, observation.classification);
            output << ",\"count\":"
                << observation.count.load(std::memory_order_relaxed);
            if (!observation.semantic_slice.empty()) {
                output << ",\"semanticSlice\":";
                append_json_string(output, observation.semantic_slice);
            }
            if (!observation.source.empty()) {
                output << ",\"source\":";
                append_json_string(output, observation.source);
            }
            output << '}';
        }
        output << R"(],"compositionDiscovery":{"schema":"webscene-native-composition-use-v1","complete":false,"incompleteCategories":["cross-element-layout-paint","dom-event-state","web-api-resource-state"],"detectors":[)";
        const auto& detectors = composition_detectors();
        for (size_t index = 0; index < detectors.size(); ++index) {
            if (index != 0U) output << ',';
            const auto& detector = detectors[index];
            output << "{\"id\":";
            append_json_string(output, detector.id);
            output << ",\"category\":";
            append_json_string(output, detector.category);
            output << ",\"features\":[";
            for (size_t feature_index = 0; feature_index < detector.features.size();
                 ++feature_index) {
                if (feature_index != 0U) output << ',';
                output << "{\"category\":";
                append_json_string(output, detector.features[feature_index].category);
                output << ",\"feature\":";
                append_json_string(output, detector.features[feature_index].feature);
                output << '}';
            }
            output << "]}";
        }
        output << R"(],"observations":[)";
        std::vector<std::pair<std::string, size_t>> compositions;
        compositions.reserve(composition_observed_nodes.size());
        for (const auto& [id, node_ids] : composition_observed_nodes) {
            if (!node_ids.empty()) compositions.emplace_back(id, node_ids.size());
        }
        std::sort(compositions.begin(), compositions.end());
        for (size_t index = 0; index < compositions.size(); ++index) {
            if (index != 0U) output << ',';
            output << "{\"id\":";
            append_json_string(output, compositions[index].first);
            output << ",\"count\":" << compositions[index].second
                << R"(,"source":"native-runtime-detector"})";
        }
        output << "]}}";
        return std::move(output).str();
#endif
    }

    std::string event_listener_inventory_json() const
    {
#if !defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        return R"({"schema":"webscene-event-listener-inventory-v1","complete":false,"telemetryEnabled":false,"targets":[]})";
#else
        std::unordered_map<uint32_t, std::unordered_set<std::string>> by_target;
        for (const auto& [type, listeners] : frame_event_listeners) {
            for (const auto& listener : listeners) {
                if (listener.target == 0U || listener.callback.IsEmpty()) continue;
                by_target[listener.target].insert(type);
            }
        }
        std::vector<uint32_t> target_ids;
        target_ids.reserve(by_target.size());
        for (const auto& [target_id, types] : by_target) {
            static_cast<void>(types);
            target_ids.push_back(target_id);
        }
        std::sort(target_ids.begin(), target_ids.end());
        std::ostringstream output;
        output << R"({"schema":"webscene-event-listener-inventory-v1","complete":true,"targets":[)";
        for (size_t target_index = 0; target_index < target_ids.size(); ++target_index) {
            if (target_index != 0U) output << ',';
            const auto target_id = target_ids[target_index];
            std::vector<std::string> types(
                by_target.at(target_id).begin(),
                by_target.at(target_id).end());
            std::sort(types.begin(), types.end());
            output << R"({"nodeId":)" << target_id << R"(,"eventTypes":[)";
            for (size_t type_index = 0; type_index < types.size(); ++type_index) {
                if (type_index != 0U) output << ',';
                append_json_string(output, types[type_index]);
            }
            output << "]}";
        }
        output << "]}";
        return std::move(output).str();
#endif
    }

    static v8::Intercepted get_window_named_property(
        v8::Local<v8::Name> property,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        if (!property->IsString()) return v8::Intercepted::kNo;
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return v8::Intercepted::kNo;
        const auto name = to_utf8(info.GetIsolate(), property.As<v8::Value>());
        if (name.empty()) return v8::Intercepted::kNo;
        // This WebScene-owned readiness flag is installed by component code.
        // Before that assignment it cannot name a DOM element, so avoid an
        // otherwise full-tree named-property search on every readiness poll.
        if (name == "__webSceneComponentReady") return v8::Intercepted::kNo;
        auto* match = self->find_named_element(
            self->active_root(),
            name,
            false);
        if (match == nullptr) {
            static const std::unordered_set<std::string> known_web_api_globals{
                "localStorage", "indexedDB", "fetch", "XMLHttpRequest",
                "Worker", "SharedWorker", "ServiceWorker", "BroadcastChannel",
                "OffscreenCanvas", "requestIdleCallback", "cancelIdleCallback"};
            if (known_web_api_globals.contains(name)) {
                self->record_feature(
                    "web-api",
                    "global:" + name,
                    "unsupported",
                    "global binding is absent",
                    "unresolved-global-binding");
            }
            return v8::Intercepted::kNo;
        }
        info.GetReturnValue().Set(self->wrap_node(*match));
        return v8::Intercepted::kYes;
    }

    static v8::Intercepted get_document_named_property(
        v8::Local<v8::Name> property,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        if (!property->IsString()) return v8::Intercepted::kNo;
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return v8::Intercepted::kNo;
        auto* root = static_cast<dom_node*>(info.Holder()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        if (root == nullptr) root = &self->active_root();
        const auto name = to_utf8(info.GetIsolate(), property.As<v8::Value>());
        if (name.empty()) return v8::Intercepted::kNo;
        auto* match = self->find_named_element(*root, name, true);
        if (match == nullptr) return v8::Intercepted::kNo;
        info.GetReturnValue().Set(self->wrap_node(*match));
        return v8::Intercepted::kYes;
    }

    binding_callback_stats* profile(binding_category category) noexcept
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        return profile_bindings
            ? &binding_totals[static_cast<size_t>(category)]
            : nullptr;
#else
        static_cast<void>(category);
        return nullptr;
#endif
    }

    static dom_node* unwrap_node(v8::Local<v8::Object> object)
    {
        return object->InternalFieldCount() < 1
            ? nullptr
            : static_cast<dom_node*>(object->GetAlignedPointerFromInternalField(
                0,
                v8::kEmbedderDataTypeTagDefault));
    }

    void compact_retained_native_capacity() noexcept
    {
        // This runs only on the owning engine worker. The selected containers
        // retain stable post-startup state or queued work that remains valid
        // across a move; no JavaScript callback can observe the reallocation.
        try {
            for (auto& [type, listeners] : frame_event_listeners) {
                static_cast<void>(type);
                listeners.shrink_to_fit();
            }
            resize_listeners.shrink_to_fit();
            timers.shrink_to_fit();
            connected_resources.shrink_to_fit();
            resize_observers.shrink_to_fit();
            loaded_resource_names.shrink_to_fit();
            pending_frame_hydrations.shrink_to_fit();
            detached_dom_roots.shrink_to_fit();
            pending_promise_rejections.shrink_to_fit();
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
            if (auto* inspector_state = inspector_state_if_created()) {
                inspector_state->promise_rejections.shrink_to_fit();
            }
#endif
            document_cookies.shrink_to_fit();
            css_rules.shrink_to_fit();
            for (auto& [name, keyframes] : opacity_keyframes) {
                static_cast<void>(name);
                keyframes.opacity_stops.shrink_to_fit();
                keyframes.rotation_stops.shrink_to_fit();
            }
            const auto compact_index = [](auto& index) {
                for (auto& [key, values] : index) {
                    static_cast<void>(key);
                    values.shrink_to_fit();
                }
            };
            compact_index(css_rules_by_class);
            compact_index(css_rules_by_id);
            compact_index(css_rules_by_tag);
            compact_index(css_rules_by_attribute);
            css_focus_rules.shrink_to_fit();
            unindexed_css_rules.shrink_to_fit();
            hover_selector_dependencies.shrink_to_fit();
        } catch (...) {
            // Capacity compaction is opportunistic. A failed allocator request
            // must not suppress V8's low-memory notification or kill the
            // engine worker.
        }
    }
