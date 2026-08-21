    static std::string resolve_resource_url(std::string value, const std::string& base)
    {
        if (value.empty()) return {};
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
            value.erase(value.begin());
        }
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
            value.pop_back();
        }
        const auto scheme = value.find(':');
        if (scheme != std::string::npos
            && scheme > 0U
            && std::all_of(value.begin(), value.begin() + static_cast<std::ptrdiff_t>(scheme),
                [](unsigned char character) {
                    return std::isalnum(character) || character == '+' || character == '-' || character == '.';
                })) {
            return value;
        }
        const auto base_scheme = base.find("://");
        if (value.starts_with("//")) {
            return base_scheme == std::string::npos
                ? value
                : base.substr(0U, base_scheme) + ":" + value;
        }
        if (base_scheme == std::string::npos) return value;
        const auto authority_end = base.find('/', base_scheme + 3U);
        const auto origin = authority_end == std::string::npos ? base : base.substr(0U, authority_end);
        if (value.starts_with('/')) return origin + value;

        const auto value_suffix_offset = value.find_first_of("?#");
        const auto value_suffix = value_suffix_offset == std::string::npos
            ? std::string{}
            : value.substr(value_suffix_offset);
        if (value_suffix_offset != std::string::npos) value.resize(value_suffix_offset);
        const auto preserve_trailing_slash = value.ends_with('/');
        auto clean_base = base.substr(0U, base.find_first_of("?#"));
        const auto slash = clean_base.rfind('/');
        auto combined = (slash == std::string::npos ? clean_base + '/' : clean_base.substr(0U, slash + 1U)) + value;
        const auto combined_authority_end = combined.find('/', base_scheme + 3U);
        if (combined_authority_end == std::string::npos) return combined;
        const auto prefix = combined.substr(0U, combined_authority_end);
        const auto suffix = combined.substr(combined_authority_end + 1U);
        std::vector<std::string> segments;
        size_t cursor = 0U;
        while (cursor <= suffix.size()) {
            auto end = suffix.find('/', cursor);
            if (end == std::string::npos) end = suffix.size();
            const auto segment = suffix.substr(cursor, end - cursor);
            if (segment == "..") {
                if (!segments.empty()) segments.pop_back();
            } else if (!segment.empty() && segment != ".") {
                segments.push_back(segment);
            }
            if (end == suffix.size()) break;
            cursor = end + 1U;
        }
        std::ostringstream result;
        result << prefix << '/';
        for (size_t index = 0U; index < segments.size(); ++index) {
            if (index != 0U) result << '/';
            result << segments[index];
        }
        if (preserve_trailing_slash && !segments.empty()) result << '/';
        result << value_suffix;
        return result.str();
    }

    static std::string resource_leaf_name(std::string value)
    {
        if (const auto suffix = value.find_first_of("?#"); suffix != std::string::npos) {
            value.resize(suffix);
        }
        const auto slash = value.find_last_of("/\\");
        return slash == std::string::npos ? value : value.substr(slash + 1U);
    }

    static std::string resource_kind_name(uint32_t kind)
    {
        switch (kind) {
        case WEBSCENE_RESOURCE_DOCUMENT: return "document";
        case WEBSCENE_RESOURCE_SCRIPT: return "script";
        case WEBSCENE_RESOURCE_STYLESHEET: return "stylesheet";
        case WEBSCENE_RESOURCE_IMAGE: return "image";
        default: return "kind-" + std::to_string(kind);
        }
    }

    static std::string resource_scheme(const std::string& address)
    {
        const auto separator = address.find(':');
        return separator == std::string::npos
            ? "filesystem"
            : lower_html_name(address.substr(0U, separator));
    }

    std::shared_ptr<const resource_response> load_resource_single_flight(
        uint32_t kind,
        const std::string& resolved_name,
        const std::string& entity_tag,
        int64_t last_modified_unix_seconds,
        bool& loaded)
    {
        loaded = false;
        if (!load_resource_callback || resolved_name.empty()) return {};

        const auto key = compilation_cache_directory
            + '\n' + std::to_string(kind)
            + '\n' + resolved_name
            + '\n' + entity_tag
            + '\n' + std::to_string(last_modified_unix_seconds);
        std::shared_ptr<process_resource_load_entry> entry;
        auto producer = false;
        {
            std::lock_guard lock(process_resource_load_mutex);
            const auto now = std::chrono::steady_clock::now();
            std::erase_if(process_resource_loads, [now](const auto& item) {
                return item.second->ready.load(std::memory_order_acquire)
                    && item.second->expires_at <= now;
            });
            if (const auto known = process_resource_loads.find(key);
                known != process_resource_loads.end()) {
                entry = known->second;
                entry->access = ++process_resource_load_access;
            } else {
                entry = std::make_shared<process_resource_load_entry>();
                entry->access = ++process_resource_load_access;
                process_resource_loads.emplace(key, entry);
                producer = true;
            }
        }

        if (!producer) {
            std::unique_lock lock(entry->mutex);
            if (!entry->ready.load(std::memory_order_acquire)) {
                ++process_resource_load_waiter_count;
                entry->condition.wait(lock, [&entry] {
                    return entry->ready.load(std::memory_order_acquire);
                });
            }
            loaded = entry->loaded;
            if (loaded && entry->response != nullptr) {
                ++process_resource_memory_hit_count;
                process_resource_shared_byte_count += entry->response->content.size();
            }
            return entry->response;
        }

        ++process_resource_load_leader_count;
        resource_response response;
        try {
            loaded = load_resource_callback(
                kind,
                resolved_name,
                entity_tag,
                last_modified_unix_seconds,
                response);
        } catch (...) {
            loaded = false;
        }
        auto shared_response = loaded
            ? std::make_shared<const resource_response>(std::move(response))
            : std::shared_ptr<const resource_response>{};
        {
            std::lock_guard lock(entry->mutex);
            entry->loaded = loaded;
            entry->response = shared_response;
            entry->expires_at = std::chrono::steady_clock::now()
                + (loaded && shared_response != nullptr && shared_response->cacheable
                    ? std::chrono::milliseconds(250)
                    : std::chrono::milliseconds(0));
            entry->ready.store(true, std::memory_order_release);
        }
        entry->condition.notify_all();

        {
            std::lock_guard lock(process_resource_load_mutex);
            while (process_resource_loads.size() > maximum_process_resource_load_entries) {
                auto oldest = process_resource_loads.end();
                for (auto candidate = process_resource_loads.begin();
                    candidate != process_resource_loads.end();
                    ++candidate) {
                    if (!candidate->second->ready.load(std::memory_order_acquire)
                        || candidate->second.get() == entry.get()) {
                        continue;
                    }
                    if (oldest == process_resource_loads.end()
                        || candidate->second->access < oldest->second->access) {
                        oldest = candidate;
                    }
                }
                if (oldest == process_resource_loads.end()) break;
                process_resource_loads.erase(oldest);
            }
        }
        return shared_response;
    }

    bool load_text_resource(
        const std::string& value,
        const std::string& base,
        uint32_t kind,
        std::string& content,
        std::string& resolved_name,
        std::shared_ptr<const immutable_text_source>* immutable_content = nullptr)
    {
        if (immutable_content != nullptr) immutable_content->reset();
        const auto assign_content = [&](std::shared_ptr<const immutable_text_source> value) {
            if (value == nullptr) {
                content.clear();
                return;
            }
            if (immutable_content != nullptr) {
                *immutable_content = std::move(value);
                content.clear();
            } else {
                content.assign(value->data(), value->size());
            }
        };
        resolved_name = resolve_resource_url(value, base);
        record_feature(
            "resource",
            "type:" + resource_kind_name(kind),
            "supported",
            {},
            "resource-loader");
        record_feature(
            "resource",
            "scheme:" + resource_scheme(resolved_name),
            "supported",
            {},
            "resource-loader");
        ++resource_cache_request_count;
        const auto cache_key = resource_key_digest(kind, resolved_name);
        cached_text_resource cached;
        auto has_cached = false;
        auto has_prefetched = false;
        prefetched_resource prefetched;
        const auto prefetch_key = resource_prefetch_key(kind, resolved_name);
        if (auto known = prefetched_resources.find(prefetch_key);
            known != prefetched_resources.end()) {
            prefetched = known->second.get();
            prefetched_resources.erase(known);
            cached = std::move(prefetched.cached);
            has_cached = prefetched.has_cached;
            has_prefetched = true;
            if (prefetched.from_fresh_cache) {
                record_feature(
                    "resource", "cache:fresh-prefetch", "supported", {}, "resource-loader");
                assign_content(std::move(cached.content));
                ++resource_cache_hit_count;
                resource_cache_bytes_read_count += immutable_content != nullptr
                    && *immutable_content != nullptr
                    ? (*immutable_content)->size()
                    : content.size();
                return true;
            }
        } else {
            has_cached = read_resource_cache(cache_key, kind, cached);
        }
        const auto now_unix_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (has_cached && cached.fresh_until_unix_seconds > now_unix_seconds) {
            record_feature(
                "resource", "cache:fresh", "supported", {}, "resource-loader");
            assign_content(std::move(cached.content));
            ++resource_cache_hit_count;
            resource_cache_bytes_read_count += immutable_content != nullptr
                && *immutable_content != nullptr
                ? (*immutable_content)->size()
                : content.size();
            return true;
        }
        if (load_resource_callback && !resolved_name.empty()) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            binding_callback_timer timer(profile_startup ? &startup_resource_read : nullptr);
#endif
            auto loaded = false;
            std::shared_ptr<const resource_response> response;
            if (has_prefetched) {
                response = std::move(prefetched.response);
                loaded = prefetched.loaded;
            } else {
                response = load_resource_single_flight(
                    kind,
                    resolved_name,
                    has_cached ? cached.entity_tag : std::string{},
                    has_cached ? cached.last_modified_unix_seconds : 0,
                    loaded);
            }
            if (loaded && response != nullptr) {
                if (response->not_modified && has_cached) {
                    record_feature(
                        "resource", "cache:revalidated-304", "supported", {}, "resource-loader");
                    auto shared_content = std::move(cached.content);
                    assign_content(shared_content);
                    ++resource_cache_hit_count;
                    resource_cache_bytes_read_count += shared_content != nullptr
                        ? shared_content->size()
                        : 0U;
                    if (response->cacheable) {
                        write_resource_cache(
                            cache_key,
                            kind,
                            shared_content,
                            response->entity_tag.empty() ? cached.entity_tag : response->entity_tag,
                            response->last_modified_unix_seconds == 0
                                ? cached.last_modified_unix_seconds
                                : response->last_modified_unix_seconds,
                            response->fresh_until_unix_seconds);
                    } else {
                        remove_resource_cache(cache_key);
                    }
                    return true;
                }
                auto shared_content = immutable_text_source::from_string(
                    std::shared_ptr<const std::string>(
                        response,
                        &response->content));
                assign_content(shared_content);
                record_feature(
                    "resource",
                    response->cacheable ? "network:cacheable-200" : "network:uncacheable-200",
                    "supported",
                    {},
                    "resource-loader");
                ++resource_cache_miss_count;
                if (response->cacheable) {
                    write_resource_cache(
                        cache_key,
                        kind,
                        shared_content,
                        response->entity_tag,
                        response->last_modified_unix_seconds,
                        response->fresh_until_unix_seconds);
                } else {
                    remove_resource_cache(cache_key);
                }
                return true;
            }
            if (has_cached) {
                record_feature(
                    "resource", "cache:stale-fallback", "supported", {}, "resource-loader");
                auto shared_content = std::move(cached.content);
                assign_content(shared_content);
                ++resource_cache_hit_count;
                resource_cache_bytes_read_count += shared_content != nullptr
                    ? shared_content->size()
                    : 0U;
                // Back off briefly after a failed revalidation. Without this,
                // a permanently missing optional asset is retried on every
                // engine generation even though its permitted stale body is
                // already serving successfully.
                write_resource_cache(
                    cache_key,
                    kind,
                    shared_content,
                    cached.entity_tag,
                    cached.last_modified_unix_seconds,
                    now_unix_seconds + 300);
                return true;
            }
        }
        ++resource_cache_miss_count;
        const auto path = resolve_resource_path(value);
        if (path.empty() || !profiled_read_text_file(path, content)) {
            record_feature(
                "resource", "load:failed", "unsupported", {}, "resource-loader");
            return false;
        }
        record_feature(
            "resource", "filesystem:read", "supported", {}, "resource-loader");
        resolved_name = path.string();
        auto shared_content = immutable_text_source::from_string(
            std::make_shared<const std::string>(std::move(content)));
        write_resource_cache(cache_key, kind, shared_content, {}, 0, 0);
        assign_content(std::move(shared_content));
        return true;
    }

    static std::string resource_prefetch_key(uint32_t kind, const std::string& resolved_name)
    {
        return std::to_string(kind) + ':' + resolved_name;
    }

    void start_resource_prefetch(
        const std::string& value,
        const std::string& base,
        uint32_t kind)
    {
        if (!load_resource_callback || prefetched_resources.size() >= maximum_resource_prefetches) {
            return;
        }
        const auto resolved_name = resolve_resource_url(value, base);
        if (resolved_name.empty()) return;
        const auto key = resource_prefetch_key(kind, resolved_name);
        if (prefetched_resources.contains(key)) return;

        cached_text_resource cached;
        const auto cache_key = resource_key_digest(kind, resolved_name);
        const auto has_cached = read_resource_cache(cache_key, kind, cached);
        const auto now_unix_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (has_cached && cached.fresh_until_unix_seconds > now_unix_seconds) {
            prefetched_resource result;
            result.cached = std::move(cached);
            result.has_cached = true;
            result.from_fresh_cache = true;
            std::promise<prefetched_resource> promise;
            auto future = promise.get_future();
            promise.set_value(std::move(result));
            prefetched_resources.emplace(key, std::move(future));
            return;
        }

        auto entity_tag = has_cached ? cached.entity_tag : std::string{};
        const auto last_modified = has_cached ? cached.last_modified_unix_seconds : 0;
        prefetched_resources.emplace(
            key,
            std::async(
                std::launch::async,
                [this, kind, resolved_name,
                    entity_tag = std::move(entity_tag), last_modified,
                    cached = std::move(cached), has_cached]() mutable {
                    prefetched_resource result;
                    result.cached = std::move(cached);
                    result.has_cached = has_cached;
                    result.response = load_resource_single_flight(
                        kind,
                        resolved_name,
                        entity_tag,
                        last_modified,
                        result.loaded);
                    return result;
                }));
    }

    void prefetch_connected_resources()
    {
        if (!load_resource_callback || connected_resources.empty()) return;
        for (auto& task : connected_resources) {
            if (prefetched_resources.size() >= maximum_resource_prefetches) return;
            if (task.node == nullptr) continue;
            auto task_context = task.context.Get(isolate);
            if (task_context.IsEmpty()) continue;
            v8::Context::Scope task_context_scope(task_context);
            const auto& base = current_base_address();
            if (task.node->tag == "link") {
                const auto href = task.node->attributes.find("href");
                if (href != task.node->attributes.end()
                    && !object_urls.contains(href->second)) {
                    start_resource_prefetch(
                        href->second,
                        base,
                        WEBSCENE_RESOURCE_STYLESHEET);
                }
            } else if (task.node->tag == "script") {
                const auto source = task.node->attributes.find("src");
                if (source != task.node->attributes.end() && !source->second.empty()) {
                    start_resource_prefetch(
                        source->second,
                        base,
                        WEBSCENE_RESOURCE_SCRIPT);
                }
            } else if (task.node->tag == "img") {
                const auto source = task.node->attributes.find("src");
                if (source != task.node->attributes.end() && !source->second.empty()) {
                    start_resource_prefetch(
                        source->second,
                        base,
                        WEBSCENE_RESOURCE_IMAGE);
                }
            }
        }
    }

    std::filesystem::path resolve_resource_path(std::string value) const
    {
        if (resource_root.empty() || value.empty()) return {};
        if (const auto suffix = value.find_first_of("?#"); suffix != std::string::npos) {
            value.resize(suffix);
        }
        std::replace(value.begin(), value.end(), '\\', '/');
        if (const auto scheme = value.find("://"); scheme != std::string::npos) {
            const auto path_start = value.find('/', scheme + 3U);
            value = path_start == std::string::npos ? std::string{} : value.substr(path_start + 1U);
        } else {
            while (value.starts_with('/')) value.erase(value.begin());
            while (value.starts_with("./")) value.erase(0U, 2U);
        }
        const auto root_name = resource_root.filename().generic_string();
        if (!root_name.empty()) {
            const auto marker = root_name + '/';
            if (const auto offset = value.find(marker); offset != std::string::npos) {
                value.erase(0U, offset + marker.size());
            }
        }
        const auto relative = std::filesystem::path(value).lexically_normal();
        if (relative.empty() || relative.is_absolute()) return {};
        for (const auto& part : relative) {
            if (part == "..") return {};
        }
        return resource_root / relative;
    }

    bool load_connected_resource(dom_node& node, v8::Local<v8::Object> wrapper)
    {
        auto local_context = isolate->GetCurrentContext();
        const auto dispatch_resource_event = [&](const char* type) {
            v8::Local<v8::Value> dispatcher;
            if (!wrapper->Get(
                    local_context,
                    js_string(isolate, "dispatchEvent")).ToLocal(&dispatcher)
                || !dispatcher->IsFunction()) {
                return true;
            }
            auto event = create_event_instance(local_context);
            event->Set(
                local_context,
                js_string(isolate, "type"),
                js_string(isolate, type)).Check();
            event->Set(
                local_context,
                js_string(isolate, "bubbles"),
                v8::False(isolate)).Check();
            event->Set(
                local_context,
                js_string(isolate, "cancelable"),
                v8::False(isolate)).Check();
            v8::TryCatch try_catch(isolate);
            v8::Local<v8::Value> arguments[]{event};
            if (!dispatcher.As<v8::Function>()->Call(
                    local_context,
                    wrapper,
                    1,
                    arguments).IsEmpty()) {
                return true;
            }
            frame_last_error_value = std::string("Connected resource ") + type
                + " event failed: " + describe_exception(try_catch, local_context);
            ++frame_script_error_count;
            return false;
        };
        if (node.tag == "link") {
            const auto href_iterator = node.attributes.find("href");
            if (href_iterator != node.attributes.end()) {
                std::string stylesheet;
                std::string stylesheet_name;
                const auto& base = current_base_address();
                const auto object_url = object_urls.find(href_iterator->second);
                const auto loaded = object_url != object_urls.end()
                    ? (stylesheet = object_url->second,
                        stylesheet_name = href_iterator->second,
                        true)
                    : load_text_resource(
                        href_iterator->second,
                        base,
                        WEBSCENE_RESOURCE_STYLESHEET,
                        stylesheet,
                        stylesheet_name);
                if (loaded) {
                    const auto first_new_rule = add_stylesheet(
                        std::move(stylesheet),
                        stylesheet_name);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                    binding_callback_timer recascade_timer(
                        profile_startup ? &startup_stylesheet_recascade : nullptr);
#endif
                    const auto variable_recascade =
                        make_css_variable_recascade_plan(first_new_rule);
                    const auto apply_to_existing = [&](
                        const auto& recurse,
                        dom_node& existing) -> void {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                        if (profile_startup) ++startup_stylesheet_recascade_nodes;
#endif
                        auto applied = false;
                        if (!filter_stylesheet_candidates
                            || has_appended_css_rule_candidate(
                                existing,
                                first_new_rule)) {
                            applied = apply_appended_css_rules(
                                existing,
                                first_new_rule);
                        }
                        if (!variable_recascade.variables.empty()) {
                            applied |= reapply_css_variable_dependent_rules(
                                existing,
                                variable_recascade);
                        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                        if (profile_startup && applied) {
                            ++startup_stylesheet_candidate_nodes;
                        }
#else
                        static_cast<void>(applied);
#endif
                        for (auto* child : existing.children) {
                            if (child != nullptr) recurse(recurse, *child);
                        }
                    };
                    apply_to_existing(apply_to_existing, document.body());
                } else {
                    frame_last_error_value = "Unable to load connected stylesheet: " + href_iterator->second;
                    ++frame_script_error_count;
                    return false;
                }
            }
        } else if (node.tag == "script") {
            const auto source_iterator = node.attributes.find("src");
            if (source_iterator == node.attributes.end() || source_iterator->second.empty()) return true;
            std::string source;
            std::shared_ptr<const immutable_text_source> immutable_source;
            std::string script_name;
            const auto& base = current_base_address();
            if (!load_text_resource(
                    source_iterator->second,
                    base,
                    WEBSCENE_RESOURCE_SCRIPT,
                    source,
                    script_name,
                    &immutable_source)) {
                frame_last_error_value = "Unable to load connected script: " + source_iterator->second;
                ++frame_script_error_count;
                return false;
            }
            std::string error;
            // A dynamically inserted external script and its load event are one
            // browser task. Promise reactions must not run between evaluating
            // the script and dispatching load, otherwise Webpack can re-enter a
            // module graph while an export is still in its TDZ.
            const auto script_source = immutable_source != nullptr
                ? immutable_source->view()
                : std::string_view(source);
            if (!execute_in_context(
                    local_context,
                    script_source,
                    script_name,
                    error,
                    false,
                    std::move(immutable_source))) {
                frame_last_error_value = "Connected script failed: " + error;
                ++frame_script_error_count;
                return false;
            }
            loaded_resource_names.push_back(resource_leaf_name(script_name));
            ++frame_script_execution_count;
        } else if (node.tag == "img") {
            const auto source = node.attributes.find("src");
            if (source == node.attributes.end() || source->second.empty()) {
                node.clear_replaced_image();
                return true;
            }
            auto& image = node.mutable_replaced_image();
            if (!image.complete || image.source != source->second) {
                const auto mark_broken_image = [&]() {
                    image.source = source->second;
                    image.resolved_source.clear();
                    image.markup.clear();
                    image.view_box.clear();
                    image.natural_width = 0;
                    image.natural_height = 0;
                    image.complete = true;
                    wrapper->Set(
                        local_context,
                        js_string(isolate, "complete"),
                        v8::True(isolate)).Check();
                    wrapper->Set(
                        local_context,
                        js_string(isolate, "currentSrc"),
                        js_string(isolate, source->second.c_str())).Check();
                    wrapper->Set(
                        local_context,
                        js_string(isolate, "naturalWidth"),
                        v8::Integer::New(isolate, 0)).Check();
                    wrapper->Set(
                        local_context,
                        js_string(isolate, "naturalHeight"),
                        v8::Integer::New(isolate, 0)).Check();
                    document.mark_dirty();
                    return dispatch_resource_event("error");
                };
                std::string markup;
                std::string resolved_source;
                const auto& base = current_base_address();
                if (!load_text_resource(
                        source->second,
                        base,
                        WEBSCENE_RESOURCE_IMAGE,
                        markup,
                        resolved_source)) {
                    return mark_broken_image();
                }
                const auto view_box = svg_view_box(markup);
                if (view_box.empty()) {
                    return mark_broken_image();
                }
                float view_x = 0;
                float view_y = 0;
                float view_width = 0;
                float view_height = 0;
                std::istringstream view_box_stream(view_box);
                view_box_stream
                    >> view_x >> view_y >> view_width >> view_height;
                image.source = source->second;
                image.resolved_source = std::move(resolved_source);
                image.markup = std::move(markup);
                image.view_box = view_box;
                image.natural_width = std::max(0.0F, view_width);
                image.natural_height = std::max(0.0F, view_height);
                image.complete = true;
                wrapper->Set(
                    local_context,
                    js_string(isolate, "complete"),
                    v8::True(isolate)).Check();
                wrapper->Set(
                    local_context,
                    js_string(isolate, "currentSrc"),
                    js_string(isolate, image.resolved_source.c_str())).Check();
                wrapper->Set(
                    local_context,
                    js_string(isolate, "naturalWidth"),
                    v8::Number::New(isolate, image.natural_width)).Check();
                wrapper->Set(
                    local_context,
                    js_string(isolate, "naturalHeight"),
                    v8::Number::New(isolate, image.natural_height)).Check();
                document.mark_dirty();
            }
        }
        return dispatch_resource_event("load");
    }
