#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
    void update_component_readiness()
    {
        if (runtime_ != nullptr
            && component_ready_.load(std::memory_order_relaxed) == 0U
            && runtime_->component_ready()) {
            component_ready_.store(1U, std::memory_order_relaxed);
        }
    }
#endif

    bool publish_scene()
    {
        scene_publication_attempts_.fetch_add(1, std::memory_order_relaxed);
        const auto publication_started = std::chrono::steady_clock::now();
        auto next = std::make_shared<scene>();
        next->commands.reserve(command_count_ + 4U);

        uint64_t hash = 1469598103934665603ULL;
        const auto width = static_cast<float>(viewport_width_);
        const auto height = static_cast<float>(viewport_height_);
        uint64_t base_revision = 0;
        uint64_t acknowledged_dom_hash = 0;
        float acknowledged_width = 0;
        float acknowledged_height = 0;
        std::unordered_map<uint32_t, canvas_layer_version> acknowledged_layers;
        std::shared_ptr<const scene> acknowledged_scene;
        {
            std::lock_guard lock(acknowledgement_->mutex);
            const auto ordered_consumer =
                ordered_scene_consumer_.load(std::memory_order_acquire);
            const auto maximum_pending_scenes = ordered_consumer ? 2U : 1U;
            if (acknowledgement_->pending_scenes.size()
                >= maximum_pending_scenes) {
                if (!ordered_consumer && acknowledgement_->revision == 0U) {
                    // Preserve the legacy latest-only startup behavior: before
                    // a renderer establishes its checkpoint base, a newer full
                    // checkpoint may replace the unacquired initial scene.
                    acknowledgement_->pending_scenes.clear();
                } else {
                    blocked_scene_publications_.fetch_add(
                        1,
                        std::memory_order_relaxed);
                    return false;
                }
            }
            if (!acknowledgement_->pending_scenes.empty()) {
                const auto& base = acknowledgement_->pending_scenes.back();
                base_revision = base->header.revision;
                acknowledged_dom_hash = base->dom_hash;
                acknowledged_width = base->header.viewport_width;
                acknowledged_height = base->header.viewport_height;
                acknowledged_layers = base->full_layer_versions;
                acknowledged_scene =
                    (base->header.flags & scene_flag_dom_replacement) != 0U
                        ? base : acknowledgement_->value;
            } else {
                base_revision = acknowledgement_->revision;
                acknowledged_dom_hash = acknowledgement_->dom_hash;
                acknowledged_width = acknowledgement_->viewport_width;
                acknowledged_height = acknowledgement_->viewport_height;
                acknowledged_layers = acknowledgement_->layer_versions;
                acknowledged_scene = acknowledgement_->value;
            }
        }
        uint32_t scene_flags = base_revision == 0 ? scene_flag_checkpoint : 0U;
        const auto collect_runtime_work_metrics =
            runtime_work_metrics_enabled_.load(std::memory_order_relaxed);
        if (collect_runtime_work_metrics) {
            scene_builds_.fetch_add(1, std::memory_order_relaxed);
        }
        if (native_scene_active_) {
            if (document_.dirty()) {
                const auto layout_started = std::chrono::steady_clock::now();
                document_.layout(width, height);
                last_layout_nanoseconds_.store(
                    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - layout_started).count()),
                    std::memory_order_relaxed);
            }
            const auto scene_build_started = std::chrono::steady_clock::now();
            document_.build_scene(
                next->commands,
                next->canvas_strings,
                next->canvas_string_bytes);
            last_scene_build_nanoseconds_.store(
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - scene_build_started).count()),
                std::memory_order_relaxed);
            dom_nodes_.store(document_.node_count(), std::memory_order_relaxed);
            layout_passes_.store(document_.layout_passes(), std::memory_order_relaxed);
            iframe_nodes_.store(document_.count_tag("iframe"), std::memory_order_relaxed);
            iframe_html_bytes_.store(
                document_.sum_attribute_bytes("iframe", "object-html")
                    + document_.sum_attribute_bytes("iframe", "frame-html"),
                std::memory_order_relaxed);
            canvas_nodes_.store(document_.count_tag("canvas"), std::memory_order_relaxed);
            const auto busiest_canvas = document_.busiest_canvas_layout();
            busiest_canvas_width_milli_.store(
                static_cast<uint64_t>(std::max(0.0F, busiest_canvas.width) * 1000.0F),
                std::memory_order_relaxed);
            busiest_canvas_height_milli_.store(
                static_cast<uint64_t>(std::max(0.0F, busiest_canvas.height) * 1000.0F),
                std::memory_order_relaxed);
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
            if (runtime_ != nullptr) {
                update_component_readiness();
                if (component_ready_.load(std::memory_order_relaxed) != 0U) {
                    scene_flags |= scene_flag_component_ready;
                }
            }
#endif
            {
                auto frame_html = document_.first_attribute("iframe", "frame-html");
                if (frame_html.empty()) {
                    frame_html = document_.first_attribute("iframe", "object-html");
                }
                std::lock_guard lock(iframe_html_mutex_);
                iframe_html_ = std::move(frame_html);
            }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            const auto diagnostics_now = std::chrono::steady_clock::now();
            if (!scene_diagnostics_initialized_
                || diagnostics_now - last_scene_diagnostics_update_
                    >= std::chrono::milliseconds(250)) {
                auto next_diagnostics = document_.describe_busiest_canvas();
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
                if (runtime_ != nullptr) {
                    if (runtime_diagnostics_.empty()
                        && component_ready_.load(std::memory_order_relaxed) != 0U) {
                        runtime_diagnostics_ = runtime_->diagnostics();
                    }
                    if (!runtime_diagnostics_.empty()) {
                        next_diagnostics += " | runtime=" + runtime_diagnostics_;
                    }
                    next_diagnostics += " | " + runtime_->event_diagnostics();
                }
#endif
                {
                    std::lock_guard lock(scene_diagnostics_mutex_);
                    scene_diagnostics_ = std::move(next_diagnostics);
                }
                scene_diagnostics_initialized_ = true;
                last_scene_diagnostics_update_ = diagnostics_now;
            }
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8)
#endif
            {
                std::lock_guard lock(canvas_layout_mutex_);
                document_.build_canvas_layouts(canvas_layouts_);
            }
            std::vector<webscene_canvas_layer> all_layers;
            std::vector<webscene_canvas_command> all_canvas_commands;
            std::vector<webscene_scene_string> all_canvas_strings;
            std::vector<char> all_canvas_string_bytes;
            document_.build_canvas_display_lists(
                all_layers,
                all_canvas_commands,
                all_canvas_strings,
                all_canvas_string_bytes);

            uint64_t dom_hash = 1469598103934665603ULL;
            for (const auto& command : next->commands) {
                dom_hash = mix_hash(dom_hash, static_cast<uint64_t>(command.kind) << 32U | command.flags);
                dom_hash = mix_hash(dom_hash, static_cast<uint64_t>(command.node_id) << 32U | command.rgba);
                dom_hash = mix_hash(
                    dom_hash,
                    static_cast<uint64_t>(std::bit_cast<uint32_t>(command.x)) << 32U
                        | std::bit_cast<uint32_t>(command.y));
                dom_hash = mix_hash(
                    dom_hash,
                    static_cast<uint64_t>(std::bit_cast<uint32_t>(command.width)) << 32U
                        | std::bit_cast<uint32_t>(command.height));
                dom_hash = mix_hash(
                    dom_hash,
                    static_cast<uint64_t>(std::bit_cast<uint32_t>(command.radius_top_left)) << 32U
                        | std::bit_cast<uint32_t>(command.radius_top_right));
                dom_hash = mix_hash(
                    dom_hash,
                    static_cast<uint64_t>(std::bit_cast<uint32_t>(command.radius_bottom_right)) << 32U
                        | std::bit_cast<uint32_t>(command.radius_bottom_left));
                // SVG rotation is carried in stroke_width for the immutable
                // scene ABI. Omitting it made the DOM animate internally while
                // every intermediate scene compared equal and was discarded.
                dom_hash = mix_hash(
                    dom_hash,
                    std::bit_cast<uint32_t>(command.stroke_width));
            }
            for (const auto byte : next->canvas_string_bytes) {
                dom_hash = mix_hash(dom_hash, static_cast<uint8_t>(byte));
            }
            next->dom_hash = dom_hash;
            const auto viewport_changed = width != acknowledged_width || height != acknowledged_height;
            if (base_revision == 0 || viewport_changed || dom_hash != acknowledged_dom_hash) {
                scene_flags |= scene_flag_dom_replacement;
                const auto localized = base_revision != 0
                    && !viewport_changed
                    && acknowledged_scene != nullptr
                    && append_localized_dom_damage(
                        *acknowledged_scene,
                        *next,
                        width,
                        height,
                        next->damage_rects);
                if (!localized) {
                    next->damage_rects.push_back(webscene_damage_rect{0, 0, width, height});
                }
            } else {
                next->commands.clear();
                next->canvas_strings.clear();
                next->canvas_string_bytes.clear();
            }

            const auto append_damage = [&](float x, float y, float damage_width, float damage_height) {
                if (damage_width <= 0 || damage_height <= 0) return;
                next->damage_rects.push_back(webscene_damage_rect{x, y, damage_width, damage_height});
            };
            for (auto layer : all_layers) {
                const auto content_hash = canvas_layer_content_hash(
                    layer,
                    all_canvas_commands,
                    all_canvas_strings,
                    all_canvas_string_bytes);
                const canvas_layer_version version{
                    layer.generation,
                    content_hash,
                    layer.command_count,
                    layer.string_count,
                    layer.x,
                    layer.y,
                    layer.width,
                    layer.height};
                next->full_layer_versions[layer.node_id] = version;
                const auto old = acknowledged_layers.find(layer.node_id);
                if (base_revision != 0
                    && old != acknowledged_layers.end()
                    && old->second.visually_equals(version)) {
                    continue;
                }

                if (old != acknowledged_layers.end()) {
                    append_damage(old->second.x, old->second.y, old->second.width, old->second.height);
                }
                append_damage(layer.x, layer.y, layer.width, layer.height);

                const auto source_command_offset = layer.command_offset;
                const auto source_string_offset = layer.string_offset;
                layer.flags = canvas_layer_flag_replace;
                layer.command_offset = static_cast<uint32_t>(next->canvas_commands.size());
                layer.string_offset = static_cast<uint32_t>(next->canvas_strings.size());
                next->canvas_commands.insert(
                    next->canvas_commands.end(),
                    all_canvas_commands.begin() + source_command_offset,
                    all_canvas_commands.begin() + source_command_offset + layer.command_count);
                for (uint32_t index = 0; index < layer.string_count; ++index) {
                    const auto& source_string = all_canvas_strings[source_string_offset + index];
                    const auto byte_offset = static_cast<uint32_t>(next->canvas_string_bytes.size());
                    next->canvas_string_bytes.insert(
                        next->canvas_string_bytes.end(),
                        all_canvas_string_bytes.begin() + source_string.byte_offset,
                        all_canvas_string_bytes.begin() + source_string.byte_offset + source_string.byte_length);
                    next->canvas_strings.push_back(webscene_scene_string{
                        byte_offset,
                        source_string.byte_length});
                }
                next->canvas_layers.push_back(layer);
            }
            for (const auto& [node_id, old] : acknowledged_layers) {
                if (next->full_layer_versions.contains(node_id)) continue;
                append_damage(old.x, old.y, old.width, old.height);
                webscene_canvas_layer removed{};
                removed.node_id = node_id;
                removed.flags = canvas_layer_flag_remove;
                removed.x = old.x;
                removed.y = old.y;
                removed.width = old.width;
                removed.height = old.height;
                removed.generation = old.generation + 1U;
                next->canvas_layers.push_back(removed);
            }

            hash = mix_hash(hash, dom_hash);
            for (const auto& layer : all_layers) {
                const auto version =
                    next->full_layer_versions.find(layer.node_id);
                hash = mix_hash(
                    hash,
                    version == next->full_layer_versions.end()
                        ? layer.generation
                        : version->second.content_hash);
                hash = mix_hash(
                    hash,
                    static_cast<uint64_t>(layer.node_id) << 32U | layer.command_count);
                hash = mix_hash(
                    hash,
                    static_cast<uint64_t>(layer.string_count) << 32U | layer.command_offset);
            }
        } else {
            scene_flags |= scene_flag_dom_replacement;
            next->damage_rects.push_back(webscene_damage_rect{0, 0, width, height});
            const auto horizontal_step = width / 96.0F;
            const auto vertical_step = height / 24.0F;
            for (uint32_t index = 0; index < command_count_; ++index) {
                const auto column = static_cast<float>(index % 96U);
                const auto row = static_cast<float>((index / 96U) % 24U);
                const auto wave = static_cast<float>(std::sin(
                    static_cast<double>(index) * 0.075 + scroll_x_ * 0.001));
                const auto x = column * horizontal_step;
                const auto y = row * vertical_step + wave * 3.0F
                    + static_cast<float>(std::fmod(scroll_y_, 11.0));
                const auto color = (index % 2U == 0U) ? 0x26A69AFFU : 0xEF5350FFU;
                next->commands.push_back(webscene_scene_command{
                    1U,
                    0U,
                    x,
                    y,
                    horizontal_step * 0.72F,
                    vertical_step * 0.65F,
                    color,
                    index + 1U,
                    0,
                    0,
                    0,
                    0,
                    0});
                hash = mix_hash(hash, static_cast<uint64_t>(index) << 32U | color);
            }
            next->dom_hash = hash;
        }

        if (collect_runtime_work_metrics && next->damage_rects.empty()) {
            no_damage_scene_builds_.fetch_add(1, std::memory_order_relaxed);
        }
        if (collect_runtime_work_metrics
            && (scene_flags & scene_flag_checkpoint) != 0U) {
            full_checkpoint_scene_builds_.fetch_add(
                1,
                std::memory_order_relaxed);
        }
        const auto revision = next_revision_++;
        next->header = webscene_scene_header{
            revision,
            base_revision,
            last_input_sequence_,
            width,
            height,
            static_cast<uint32_t>(next->commands.size()),
            static_cast<uint32_t>(next->canvas_layers.size()),
            static_cast<uint32_t>(next->damage_rects.size()),
            scene_flags,
            mix_hash(
                mix_hash(hash, std::bit_cast<uint32_t>(width)),
                std::bit_cast<uint32_t>(height))};
        const auto latest_scene_bytes = retained_scene_bytes(*next);
        latest_scene_bytes_.store(latest_scene_bytes, std::memory_order_relaxed);
        next->published_timestamp_nanoseconds = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        auto published_scene = std::shared_ptr<const scene>(std::move(next));
        {
            std::lock_guard lock(acknowledgement_->mutex);
            acknowledgement_->pending_scenes.push_back(published_scene);
        }
        std::atomic_store_explicit(
            &latest_,
            std::move(published_scene),
            std::memory_order_release);
        published_scenes_.fetch_add(1, std::memory_order_relaxed);
        const auto publication_nanoseconds =
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - publication_started).count());
        last_scene_publication_nanoseconds_.store(
            publication_nanoseconds,
            std::memory_order_relaxed);
        store_maximum(maximum_scene_publication_nanoseconds_, publication_nanoseconds);
        if (scene_published_callback_ != nullptr) {
            scene_published_callback_(
                scene_published_user_data_,
                revision,
                last_input_sequence_,
                width,
                height);
        }
        return true;
    }
