    void apply_scrollbar_css_declaration(dom_node& node,int kind,const css_declaration& declaration)
    { css::apply_scrollbar_declaration(node,kind,declaration,css_variables); }

    static bool is_explicit_css_color_token(std::string_view value,uint32_t parsed) noexcept
    { return css::is_explicit_color_token(value,parsed); }

    static bool computed_layout_style_equal(const node_style& left,const node_style& right) noexcept
    { return css::computed_layout_style_equal(left,right); }

    static void append_utf8_codepoint(std::string& result, uint32_t codepoint)
    { css::append_utf8_codepoint(result,codepoint); }

    static std::string decode_css_content(std::string value)
    { return css::decode_content(std::move(value)); }

    template<typename Style>
    static bool apply_corner_radius_declaration(std::string_view name,const std::string& value,Style& style)
    { return css::apply_corner_radius_declaration(name,value,style); }

    static bool apply_box_shadow_value(node_style& style,const std::string& value,bool& complete)
    { return css::apply_box_shadow_value(style,value,complete); }

    void apply_pseudo_css_declaration(
        dom_node& node,
        node_style::pseudo_element& pseudo,
        const css_declaration& declaration)
    {
        feature_decision decision(
            *this,
            "css",
            "pseudo-property:" + declaration.name,
            "pseudo-style-application");
        css::apply_pseudo_declaration(node,pseudo,declaration,css_variables,decision,
            [&](bool contains_variable) {
                if(contains_variable && is_paint_property(declaration.name))
                    record_composition("css-custom-property-to-paint",node.id);
            });
    }

    static std::optional<std::string> first_css_url(const std::string& value)
    { return css::first_css_url(value); }

    static std::string svg_view_box(const std::string& markup)
    {
        const auto lower = lower_html_name(markup);
        const auto svg = lower.find("<svg");
        if (svg == std::string::npos) return {};
        const auto tag_end = lower.find('>', svg + 4U);
        if (tag_end == std::string::npos) return {};
        const auto view_box = lower.find("viewbox", svg + 4U);
        if (view_box != std::string::npos && view_box < tag_end) {
            const auto equals = markup.find('=', view_box + 7U);
            if (equals != std::string::npos && equals < tag_end) {
                auto value_start = equals + 1U;
                while (value_start < tag_end
                    && std::isspace(static_cast<unsigned char>(markup[value_start]))) {
                    ++value_start;
                }
                if (value_start < tag_end) {
                    const auto quote = markup[value_start];
                    if (quote == '\'' || quote == '"') {
                        const auto value_end = markup.find(quote, value_start + 1U);
                        if (value_end != std::string::npos && value_end <= tag_end) {
                            return markup.substr(
                                value_start + 1U,
                                value_end - value_start - 1U);
                        }
                    }
                }
            }
        }

        // SVG 2 permits an intrinsic viewport to be supplied by numeric width
        // and height attributes without a viewBox. Convert that bounded form
        // to the scene's canonical viewBox representation so ordinary bundled
        // CSS artwork (checker patterns, badges, and icons) retains its aspect
        // ratio instead of being discarded.
        const auto opening = markup.substr(svg, tag_end - svg + 1U);
        const auto parse_dimension = [](std::string value) -> std::optional<float> {
            value = trim_css(value);
            if (value.ends_with("px")) value.resize(value.size() - 2U);
            if (value.empty()) return std::nullopt;
            char* end = nullptr;
            const auto parsed = std::strtof(value.c_str(), &end);
            if (end == value.c_str() || *end != '\0' || !std::isfinite(parsed)
                || parsed <= 0.0F) {
                return std::nullopt;
            }
            return parsed;
        };
        const auto width = parse_dimension(extract_html_attribute(opening, "width"));
        const auto height = parse_dimension(extract_html_attribute(opening, "height"));
        if (!width.has_value() || !height.has_value()) return {};
        std::ostringstream synthesized;
        synthesized << "0 0 " << *width << ' ' << *height;
        return std::move(synthesized).str();
    }

    static void apply_background_position(node_style& style,const std::string& value)
    { css::apply_background_position(style,value); }

    static std::vector<std::string> split_css_component_list(std::string_view value,char separator)
    { return css::split_css_component_list(value,separator); }

    static std::vector<std::string> split_transition_tokens(std::string_view value)
    { return css::split_value_tokens(value); }
    static uint8_t margin_sides(std::string_view name) { return css::margin_sides(name); }
    static bool apply_margin_declaration(node_style& style,std::string_view name,
        const std::string& value,uint8_t allowed=15U)
    { return css::apply_margin_declaration(style,name,value,allowed); }
    static bool apply_padding_declaration(node_style& style,std::string_view name,const std::string& value)
    { return css::apply_padding_declaration(style,name,value); }

    static css_length parse_inset_length(const std::string& value) { return css::parse_inset_length(value); }
    static bool apply_inset_declaration(node_style& style,std::string_view name,const std::string& value)
    { return css::apply_inset_declaration(style,name,value); }
    static bool apply_border_declaration(node_style& style,std::string_view name,const std::string& value)
    { return css::apply_border_declaration(style,name,value); }

    static bool is_css_time(std::string_view value) { return css::is_css_time(value); }
    static float parse_css_time_ms(std::string value) { return css::parse_css_time_ms(std::move(value)); }
    static void parse_transition_timing(const std::string& value,node_style::transition_timing& timing)
    { css::parse_transition_timing(value,timing); }

    static std::string serialize_computed_time_list(const std::string& value)
    {
        const auto times = split_css_component_list(value, ',');
        std::string result;
        for (const auto& time : times) {
            if (!result.empty()) result += ", ";
            result += serialize_css_number(
                static_cast<double>(parse_css_time_ms(time)) / 1000.0) + "s";
        }
        return result.empty() ? std::string{"0s"} : result;
    }

    static void configure_style_transitions(node_style& style) { css::configure_style_transitions(style); }
    static void apply_transition_shorthand(node_style& style,const std::string& value)
    { css::apply_transition_shorthand(style,value); }

    static bool apply_grid_placement_declaration(node_style& style,std::string_view name,const std::string& value)
    { return css::apply_grid_placement_declaration(style,name,value); }

    static void apply_animation_shorthand(node_style& style,const std::string& value)
    { css::apply_animation_shorthand(style,value); }

    void configure_style_keyframe_animation(dom_node& node)
    {
        css::configure_keyframes(node.style,opacity_keyframes);
        if (!node.style.animations().opacity_keyframe_animation_signature.empty())
            record_composition("css-opacity-keyframe-animation",node.id);
        if (!node.style.animations().rotation_keyframe_animation_signature.empty())
            record_composition("css-rotation-keyframe-animation",node.id);
    }

    void recompute_cascaded_line_height(dom_node& node,const std::vector<const css_rule*>& rules)
    { css::recompute_cascaded_line_height(node,rules,css_variables); }

    void apply_css_declaration(dom_node& node, const css_declaration& declaration, bool inline_origin = false)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (profile_css) {
            ++css_profile_declaration_applications;
        }
#endif
        const auto& name = declaration.name;
        if (name == "-moz-transform" || name == "-webkit-transform"
            || name == "grid-gap" || name == "grid-row-gap" || name == "grid-column-gap") {
            feature_decision alias_decision(
                *this,
                "css",
                "property:" + name,
                "style-application");
            auto normalized = declaration;
            normalized.name = name.starts_with("grid-")
                ? canonical_css_property_name(name) : "transform";
            apply_css_declaration(node, normalized, inline_origin);
            return;
        }
        if (name.starts_with("--")) {
            css::apply_custom_property(node,declaration);
            record_feature("css","custom-property","supported",{},"style-application");
            return;
        }
        feature_decision decision(
            *this,
            "css",
            "property:" + name,
            "style-application");
        css::apply_declaration(document,node,declaration,css_variables,inline_origin,decision,
            [this](const std::string& url,std::string& markup,std::string& resolved,std::string& view_box) {
                if(!load_text_resource(url,current_base_address(),WEBSCENE_RESOURCE_IMAGE,markup,resolved))
                    return false;
                view_box=svg_view_box(markup);
                return true;
            },[&](bool contains_variable) {
                if(contains_variable && is_paint_property(name))
                    record_composition("css-custom-property-to-paint",node.id);
            });
    }

    void recompute_inline_font_relative_metrics(dom_node& node)
    { css::recompute_inline_font_relative_metrics(node); }

    void apply_css_rules(dom_node& node)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        binding_callback_timer timer(profile_startup ? &startup_css_apply : nullptr);
        const auto trace_css = profile_css;
        const auto css_apply_started = trace_css
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
#endif
        // Only elements are CSS selector subjects. Text nodes generate anonymous
        // inline boxes; comments, doctypes, fragments, and other DOM-only nodes
        // never generate boxes. In particular, do not replace the display:none
        // assigned when a preserved comment was created with the default block
        // display for an unknown tag during a subtree recascade.
        if (node.kind != dom_node_kind::element) {
            const auto display_changed = node.style.display
                != (node.kind == dom_node_kind::text
                    ? display_mode::inline_flow
                    : display_mode::none);
            node.style.display = node.kind == dom_node_kind::text
                ? display_mode::inline_flow
                : display_mode::none;
            if (display_changed) document.mark_dirty();
            else document.mark_scene_changed();
            return;
        }
        const auto previous_style = node.style;
        css::reset_cascaded_style(node,css_variables);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto reset_completed = trace_css
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
#endif
        auto candidates = css::collect_candidates(node, &node == active_element,
            css_rules_by_tag, css_rules_by_id, css_rules_by_attribute,
            css_focus_rules, unindexed_css_rules,
            [&](std::vector<size_t>& candidates, std::string_view class_name) {
                const auto append_candidates = [&](const auto& index, const auto& key) {
                    const auto match = index.find(key);
                    if (match != index.end())
                        candidates.insert(candidates.end(), match->second.begin(), match->second.end());
                };
#if defined(WEBSCENE_NATIVE_ENGINE_MEDIA_REFRESH_BENCHMARK_COUNTERS)
                media_refresh_class_lookups.fetch_add(1U, std::memory_order_relaxed);
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_CSS_CLASS_LOOKUP_VIEW_EXPERIMENT)
                append_candidates(
                    css_rules_by_class,
                    class_name);
#else
#if defined(WEBSCENE_NATIVE_ENGINE_MEDIA_REFRESH_BENCHMARK_COUNTERS)
                media_refresh_owned_class_lookup_keys.fetch_add(1U, std::memory_order_relaxed);
                media_refresh_owned_class_lookup_bytes.fetch_add(
                    class_name.size(), std::memory_order_relaxed);
#endif
                append_candidates(
                    css_rules_by_class,
                    std::string(class_name));
#endif
            });
        sort_css_candidates(candidates);
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (trace_css) css_profile_candidate_checks += candidates.size();
#endif
        auto matching=css::match_candidates(document,node,css_rules,candidates,
            [this](const dom_node& subject,const css_rule&,const std::string& selector) {
                return css_selector_matches(subject,selector);
            },[this](const dom_node& subject,const css_rule& rule) {
                return css_rule_matches(subject,rule);
            });
        auto& matched_rules=matching.ordinary;
        auto& matched_pseudo_rules=matching.pseudo;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (trace_css) css_profile_matched_rules += matched_rules.size();
        const auto matching_completed = trace_css
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
#endif
        css::apply_matched_declarations(node,matched_rules,
            [&](const css_declaration& declaration,bool inline_origin) {
                apply_css_declaration(node,declaration,inline_origin);
            });
        // Inline relative line-height is authored at a higher origin, but its
        // computed pixel value depends on the winning font-size. Re-resolve it
        // only after stylesheet declarations have completed their cascade.
        recompute_cascaded_line_height(node, matched_rules);
        recompute_inline_font_relative_metrics(node);
        // Pseudo-elements inherit the originating element's completed
        // computed style. Apply them only after its custom and ordinary
        // declarations so `::before { background:var(--active-bg) }` resolves
        // the state variable authored on the active element.
        bool backdrop_important=false;
        if(node.dialog_state)node.dialog_state->backdrop_rgba=0;
        for (const auto& [pseudo_kind, rule] : matched_pseudo_rules) {
            if(pseudo_kind==7) {
                for(const auto& declaration:rule->declarations())
                    (void)css::apply_backdrop_declaration(node,declaration,css_variables,backdrop_important);
                continue;
            }
            if (pseudo_kind >= 3 && pseudo_kind <= 6) {
                for (const auto& declaration : rule->declarations()) {
                    apply_scrollbar_css_declaration(node, pseudo_kind, declaration);
                }
                continue;
            }
            auto& pseudo = pseudo_kind == 1
                ? node.style.mutable_before_pseudo()
                : node.style.mutable_after_pseudo();
            for (const auto& declaration : rule->declarations()) {
                apply_pseudo_css_declaration(node, pseudo, declaration);
            }
        }
        // The pseudo-element's used rectangle is layout state, not computed
        // style. A paint-only recascade rebuilds the computed pseudo style but
        // does not schedule layout, so retain the last used rectangle until a
        // layout-affecting change below marks the document dirty. The ensuing
        // layout pass will replace it with newly resolved geometry.
        if (node.style.before_pseudo().generated
            && previous_style.before_pseudo().generated) {
            node.style.mutable_before_pseudo().layout =
                previous_style.before_pseudo().layout;
        }
        if (node.style.after_pseudo().generated
            && previous_style.after_pseudo().generated) {
            node.style.mutable_after_pseudo().layout =
                previous_style.after_pseudo().layout;
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto declarations_completed = trace_css
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
#endif
        configure_style_keyframe_animation(node);
        document.update_style_animations(node);
        detect_css_compositions(node);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto force_layout_invalidation =
            std::getenv("WEBSCENE_FORCE_RECASCADE_LAYOUT_INVALIDATION") != nullptr;
#else
        constexpr auto force_layout_invalidation = false;
#endif
        if (force_layout_invalidation
            || !computed_layout_style_equal(previous_style, node.style)) {
            document.mark_dirty();
        } else {
            document.mark_scene_changed();
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (trace_css) {
            const auto completed = std::chrono::steady_clock::now();
            css_profile_reset_nanoseconds +=
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    reset_completed - css_apply_started).count();
            css_profile_matching_nanoseconds +=
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    matching_completed - reset_completed).count();
            css_profile_declarations_nanoseconds +=
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    declarations_completed - matching_completed).count();
            css_profile_finalize_nanoseconds +=
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    completed - declarations_completed).count();
        }
#endif
    }

    bool has_appended_css_rule_candidate(
        const dom_node& node,
        size_t first_new_rule) const
    {
        if (node.tag == "#text" || first_new_rule >= css_rules.size()) return false;
        const auto contains_new_rule = [first_new_rule](
            const std::vector<size_t>& indexes) {
            return !indexes.empty() && indexes.back() >= first_new_rule;
        };
        const auto index_contains_new_rule = [&contains_new_rule](
            const auto& index,
            const std::string& key) {
            const auto match = index.find(key);
            return match != index.end() && contains_new_rule(match->second);
        };
        if (contains_new_rule(unindexed_css_rules)
            || index_contains_new_rule(css_rules_by_tag, node.tag)
            || (!node.id_attribute.empty()
                && index_contains_new_rule(css_rules_by_id, node.id_attribute))) {
            return true;
        }
        if (node.tag == "body"
            && (node.parent == nullptr || node.parent->tag == "iframe")
            && index_contains_new_rule(css_rules_by_tag, "html")) {
            return true;
        }
        for (const auto& [name, value] : node.attributes) {
            static_cast<void>(value);
            if (index_contains_new_rule(css_rules_by_attribute, name)) return true;
        }
        if (&node == active_element && contains_new_rule(css_focus_rules)) return true;
        const std::string_view classes(node.class_name);
        size_t cursor = 0U;
        while (cursor < classes.size()) {
            while (cursor < classes.size()
                && std::isspace(static_cast<unsigned char>(classes[cursor]))) ++cursor;
            const auto start = cursor;
            while (cursor < classes.size()
                && !std::isspace(static_cast<unsigned char>(classes[cursor]))) ++cursor;
            if (cursor > start
                && index_contains_new_rule(
                    css_rules_by_class,
                    std::string(classes.substr(start, cursor - start)))) {
                return true;
            }
        }
        return false;
    }

    bool apply_appended_css_rules(dom_node& node, size_t first_new_rule)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        binding_callback_timer timer(
            profile_startup ? &startup_css_incremental_apply : nullptr);
#endif
        if (node.tag == "#text" || first_new_rule >= css_rules.size()) return false;

        std::vector<size_t> candidates;
        const auto append_new_candidates = [&](const std::vector<size_t>& source) {
            const auto first = std::lower_bound(
                source.begin(), source.end(), first_new_rule);
            candidates.insert(candidates.end(), first, source.end());
        };
        append_new_candidates(unindexed_css_rules);
        const auto append_candidates = [&](const auto& index, const std::string& key) {
            const auto match = index.find(key);
            if (match != index.end()) {
                append_new_candidates(match->second);
            }
        };
        append_candidates(css_rules_by_tag, node.tag);
        if (node.tag == "body"
            && (node.parent == nullptr || node.parent->tag == "iframe")) {
            append_candidates(css_rules_by_tag, "html");
        }
        if (!node.id_attribute.empty()) append_candidates(css_rules_by_id, node.id_attribute);
        for (const auto& [name, value] : node.attributes) {
            static_cast<void>(value);
            append_candidates(css_rules_by_attribute, name);
        }
        if (&node == active_element) {
            append_new_candidates(css_focus_rules);
        }
        const std::string_view classes(node.class_name);
        size_t class_cursor = 0;
        while (class_cursor < classes.size()) {
            while (class_cursor < classes.size()
                && std::isspace(static_cast<unsigned char>(classes[class_cursor]))) ++class_cursor;
            const auto start = class_cursor;
            while (class_cursor < classes.size()
                && !std::isspace(static_cast<unsigned char>(classes[class_cursor]))) ++class_cursor;
            if (class_cursor > start) {
                append_candidates(
                    css_rules_by_class,
                    std::string(classes.substr(start, class_cursor - start)));
            }
        }
        sort_css_candidates(candidates);
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

        if (filter_stylesheet_candidates && candidates.empty()) return false;

        std::vector<const css_rule*> matched_rules;
        std::vector<std::pair<int, const css_rule*>> matched_pseudo_rules;
        for (const auto index : candidates) {
            const auto& rule = css_rules[index];
            if (!rule.media_matches) continue;
            std::string pseudo_origin;
            const auto pseudo_kind = split_pseudo_element_selector(rule.selector(), pseudo_origin);
            if (pseudo_kind != 0) {
                if (!pseudo_origin.empty() && css_selector_matches(node, pseudo_origin)) {
                    matched_pseudo_rules.emplace_back(pseudo_kind, &rule);
                }
                continue;
            }
            if (css_rule_matches(node, rule)) matched_rules.push_back(&rule);
        }
        if (filter_stylesheet_candidates
            && matched_rules.empty()
            && matched_pseudo_rules.empty()) {
            return false;
        }
        for (const auto* rule : matched_rules) {
            for (const auto& declaration : rule->declarations()) {
                if (declaration.name.starts_with("--")) apply_css_declaration(node, declaration);
            }
        }
        for (const auto* rule : matched_rules) {
            for (const auto& declaration : rule->declarations()) {
                if (!declaration.name.starts_with("--")) apply_css_declaration(node, declaration);
            }
        }
        recompute_cascaded_line_height(node, matched_rules);
        recompute_inline_font_relative_metrics(node);
        bool backdrop_important=false;
        if(node.dialog_state)node.dialog_state->backdrop_rgba=0;
        for (const auto& [pseudo_kind, rule] : matched_pseudo_rules) {
            if(pseudo_kind==7) {
                for(const auto& declaration:rule->declarations())
                    (void)css::apply_backdrop_declaration(node,declaration,css_variables,backdrop_important);
                continue;
            }
            if (pseudo_kind >= 3 && pseudo_kind <= 6) {
                for (const auto& declaration : rule->declarations()) {
                    apply_scrollbar_css_declaration(node, pseudo_kind, declaration);
                }
                continue;
            }
            auto& pseudo = pseudo_kind == 1
                ? node.style.mutable_before_pseudo()
                : node.style.mutable_after_pseudo();
            for (const auto& declaration : rule->declarations()) {
                apply_pseudo_css_declaration(node, pseudo, declaration);
            }
        }
        configure_style_keyframe_animation(node);
        document.update_style_animations(node);
        detect_css_compositions(node);
        document.mark_dirty();
        return true;
    }

    std::unordered_set<std::string> appended_css_custom_properties(
        size_t first_new_rule) const
    {
        std::unordered_set<std::string> properties;
        for (auto index = first_new_rule; index < css_rules.size(); ++index) {
            for (const auto& declaration : css_rules[index].declarations()) {
                if (declaration.name.starts_with("--")) {
                    properties.insert(declaration.name);
                }
            }
        }
        return properties;
    }

    void expand_css_variable_dependencies(
        std::unordered_set<std::string>& properties) const
    {
        std::vector<std::string> pending(properties.begin(), properties.end());
        for (size_t cursor = 0U; cursor < pending.size(); ++cursor) {
            const auto indexed = css_rules_by_variable_reference.find(pending[cursor]);
            if (indexed == css_rules_by_variable_reference.end()) continue;
            for (const auto rule_index : indexed->second) {
                if (rule_index >= css_rules.size()) continue;
                for (const auto& declaration : css_rules[rule_index].declarations()) {
                    if (!declaration.name.starts_with("--")) continue;
                    const auto references = css_variable_references(declaration.value);
                    if (!references.contains(pending[cursor])) continue;
                    if (properties.insert(declaration.name).second) {
                        pending.push_back(declaration.name);
                    }
                }
            }
        }
    }

    struct css_variable_recascade_rule final {
        size_t index{0U};
        std::vector<std::string> properties;
    };

    struct css_variable_recascade_plan final {
        std::unordered_set<std::string> variables;
        std::vector<css_variable_recascade_rule> rules;
        std::unordered_map<std::string, std::vector<size_t>> rules_by_class;
        std::unordered_map<std::string, std::vector<size_t>> rules_by_id;
        std::unordered_map<std::string, std::vector<size_t>> rules_by_tag;
        std::unordered_map<std::string, std::vector<size_t>> rules_by_attribute;
        std::vector<size_t> focus_rules;
        std::vector<size_t> unindexed_rules;
    };

    css_variable_recascade_plan make_css_variable_recascade_plan(
        size_t first_new_rule) const
    {
        css_variable_recascade_plan plan;
        plan.variables = appended_css_custom_properties(first_new_rule);
        expand_css_variable_dependencies(plan.variables);
        if (plan.variables.empty()) return plan;

        std::vector<size_t> dependent_rules;
        for (const auto& variable : plan.variables) {
            const auto indexed = css_rules_by_variable_reference.find(variable);
            if (indexed != css_rules_by_variable_reference.end()) {
                dependent_rules.insert(
                    dependent_rules.end(),
                    indexed->second.begin(),
                    indexed->second.end());
            }
        }
        std::sort(dependent_rules.begin(), dependent_rules.end());
        dependent_rules.erase(
            std::unique(dependent_rules.begin(), dependent_rules.end()),
            dependent_rules.end());
        for (const auto index : dependent_rules) {
            if (index >= css_rules.size()) continue;
            css_variable_recascade_rule dependent{index, {}};
            for (const auto& declaration : css_rules[index].declarations()) {
                if (declaration.name.starts_with("--")) continue;
                const auto references = css_variable_references(declaration.value);
                if (std::any_of(
                        references.begin(),
                        references.end(),
                        [&](const auto& reference) {
                            return plan.variables.contains(reference);
                        })) {
                    dependent.properties.push_back(declaration.name);
                }
            }
            std::sort(dependent.properties.begin(), dependent.properties.end());
            dependent.properties.erase(
                std::unique(
                    dependent.properties.begin(), dependent.properties.end()),
                dependent.properties.end());
            if (!dependent.properties.empty()) {
                plan.rules.push_back(std::move(dependent));
            }
        }
        std::unordered_map<size_t, size_t> plan_index_by_rule;
        for (size_t index = 0U; index < plan.rules.size(); ++index) {
            plan_index_by_rule.emplace(plan.rules[index].index, index);
        }
        const auto project_index = [&](const auto& source, auto& target) {
            for (const auto& [key, rule_indexes] : source) {
                for (const auto rule_index : rule_indexes) {
                    const auto found = plan_index_by_rule.find(rule_index);
                    if (found != plan_index_by_rule.end()) {
                        target[key].push_back(found->second);
                    }
                }
            }
        };
        project_index(css_rules_by_class, plan.rules_by_class);
        project_index(css_rules_by_id, plan.rules_by_id);
        project_index(css_rules_by_tag, plan.rules_by_tag);
        project_index(css_rules_by_attribute, plan.rules_by_attribute);
        for (const auto rule_index : css_focus_rules) {
            const auto found = plan_index_by_rule.find(rule_index);
            if (found != plan_index_by_rule.end()) {
                plan.focus_rules.push_back(found->second);
            }
        }
        for (const auto rule_index : unindexed_css_rules) {
            const auto found = plan_index_by_rule.find(rule_index);
            if (found != plan_index_by_rule.end()) {
                plan.unindexed_rules.push_back(found->second);
            }
        }
        return plan;
    }

    bool reapply_css_variable_dependent_rules(
        dom_node& node,
        const css_variable_recascade_plan& plan)
    {
        if (node.tag == "#text" || plan.variables.empty()) return false;

        const auto references_affected_variable = [&](std::string_view value) {
            const auto references = css_variable_references(value);
            return std::any_of(
                references.begin(),
                references.end(),
                [&](const auto& reference) {
                    return plan.variables.contains(reference);
                });
        };

        auto filter_by_variable_dependency = true;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        filter_by_variable_dependency =
            std::getenv("WEBSCENE_PROBE_DISABLE_CSS_VARIABLE_DEPENDENCY_FILTER")
                == nullptr;
#endif
        std::unordered_set<std::string> variable_dependent_properties;
        if (filter_by_variable_dependency) {
            auto plan_candidates = plan.unindexed_rules;
            const auto append_plan_candidates = [&](const auto& index, const std::string& key) {
                const auto match = index.find(key);
                if (match != index.end()) {
                    plan_candidates.insert(
                        plan_candidates.end(), match->second.begin(), match->second.end());
                }
            };
            append_plan_candidates(plan.rules_by_tag, node.tag);
            if (node.tag == "body"
                && (node.parent == nullptr || node.parent->tag == "iframe")) {
                append_plan_candidates(plan.rules_by_tag, "html");
            }
            if (!node.id_attribute.empty()) {
                append_plan_candidates(plan.rules_by_id, node.id_attribute);
            }
            for (const auto& [name, value] : node.attributes) {
                static_cast<void>(value);
                append_plan_candidates(plan.rules_by_attribute, name);
            }
            if (&node == active_element) {
                plan_candidates.insert(
                    plan_candidates.end(),
                    plan.focus_rules.begin(),
                    plan.focus_rules.end());
            }
            std::istringstream classes(node.class_name);
            for (std::string class_name; classes >> class_name;) {
                append_plan_candidates(plan.rules_by_class, class_name);
            }
            std::sort(plan_candidates.begin(), plan_candidates.end());
            plan_candidates.erase(
                std::unique(plan_candidates.begin(), plan_candidates.end()),
                plan_candidates.end());
            for (const auto candidate : plan_candidates) {
                const auto& dependent = plan.rules[candidate];
                const auto& rule = css_rules[dependent.index];
                if (!rule.media_matches || !css_rule_matches(node, rule)) continue;
                variable_dependent_properties.insert(
                    dependent.properties.begin(),
                    dependent.properties.end());
            }
            for (const auto& [name, value] : node.authored_style().declarations) {
                if (!name.starts_with("--") && references_affected_variable(value)) {
                    variable_dependent_properties.insert(name);
                }
            }
            if (variable_dependent_properties.empty()) return false;
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (profile_startup) ++startup_stylesheet_variable_nodes;
#endif

        std::vector<size_t> candidates = unindexed_css_rules;
        const auto append_candidates = [&](const auto& index, const std::string& key) {
            const auto match = index.find(key);
            if (match != index.end()) {
                candidates.insert(candidates.end(), match->second.begin(), match->second.end());
            }
        };
        append_candidates(css_rules_by_tag, node.tag);
        if (node.tag == "body"
            && (node.parent == nullptr || node.parent->tag == "iframe")) {
            append_candidates(css_rules_by_tag, "html");
        }
        if (!node.id_attribute.empty()) append_candidates(css_rules_by_id, node.id_attribute);
        for (const auto& [name, value] : node.attributes) {
            static_cast<void>(value);
            append_candidates(css_rules_by_attribute, name);
        }
        if (&node == active_element) {
            candidates.insert(
                candidates.end(),
                css_focus_rules.begin(),
                css_focus_rules.end());
        }
        std::istringstream classes(node.class_name);
        for (std::string class_name; classes >> class_name;) {
            append_candidates(css_rules_by_class, class_name);
        }
        sort_css_candidates(candidates);
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

        std::vector<const css_rule*> matched_rules;
        for (const auto index : candidates) {
            const auto& rule = css_rules[index];
            if (!rule.media_matches || !css_rule_matches(node, rule)) continue;
            matched_rules.push_back(&rule);
            if (!filter_by_variable_dependency) {
                for (const auto& declaration : rule.declarations()) {
                    if (!declaration.name.starts_with("--")
                        && declaration.value.find("var(") != std::string::npos) {
                        variable_dependent_properties.insert(declaration.name);
                    }
                }
            }
        }
        // Replay the complete cascade for every property whose computed value
        // depends on a newly introduced custom property. Replaying only the
        // var() declaration lets it jump past later declarations of equal or
        // higher precedence (for example a responsive `display:none`).
        for (const auto* rule : matched_rules) {
            for (const auto& declaration : rule->declarations()) {
                if (variable_dependent_properties.contains(declaration.name)) {
                    apply_css_declaration(node, declaration);
                }
            }
        }
        for (const auto& [name, value] : node.authored_style().declarations) {
            if (!variable_dependent_properties.contains(name)
                || !references_affected_variable(value)) {
                continue;
            }
            const auto property_mask = inline_style_mask(name);
            const auto retained_inline_mask = node.style.inline_property_mask;
            node.style.inline_property_mask &= ~property_mask;
            apply_css_declaration(
                node,
                {name, value,
                    node.authored_style().important_declarations.contains(name)}, true);
            node.style.inline_property_mask = retained_inline_mask;
        }
        document.mark_dirty();
        return true;
    }

    // Re-run the complete cascade only on dimension consumers when explicit
    // inheritance does not require descendant style propagation.
    // Keep the ordinary subtree path for aliases, inherited values and pseudo
    // elements; this is intentionally a conservative, uncached fast path.
    bool recascade_dimension_custom_property(dom_node& root, const std::string& name)
    {
        if (!is_connected(root) || document.containing_shadow_root(root) != nullptr) return false;
        auto* previous_root = const_cast<dom_node*>(active_css_cascade_root);
        auto* cascade_root = css_cascade_root_for_node(root);
        if (cascade_root != nullptr) activate_css_cascade(cascade_root);
        const auto restore = [this, previous_root, cascade_root](void*) {
            if (previous_root != nullptr && previous_root != cascade_root)
                activate_css_cascade(previous_root);
        };
        std::unique_ptr<void, decltype(restore)> scope(this, restore);

        if (css_attribute_dependencies.contains("style")) return false;
        // The lightweight attribute-dependency index does not decode escapes.
        for (const auto& rule : css_rules) {
            if (rule.selector().find(static_cast<char>(92)) != std::string::npos
                || copy_ascii_lower(rule.selector()).find("style") != std::string::npos) return false;
        }
        const auto is_geometry_property = [](std::string_view property) {
            return property == "width" || property == "height"
                || property == "min-width" || property == "max-width"
                || property == "min-height" || property == "max-height"
                || property == "grid-template-columns" || property == "grid-template-rows"
                || property == "left" || property == "right"
                || property == "top" || property == "bottom";
        };
        // Non-inherited-by-default dimensions can still explicitly inherit.
        // Include variable-resolved inheritance, not just literal tokens. If
        // present, the ordinary ancestor-first subtree cascade propagates it.
        std::vector<const css_rule*> inheritance_rules;
        for (const auto& rule : css_rules) {
            bool potential = false;
            for (const auto& declaration : rule.declarations()) {
                if (!is_geometry_property(declaration.name)) continue;
                if (trim_css_view(declaration.value) == "inherit") return false;
                potential = potential || declaration.value.find("var(") != std::string::npos;
            }
            if (potential) {
                std::string origin;
                if (split_pseudo_element_selector(rule.selector(), origin) != 0) return false;
                inheritance_rules.push_back(&rule);
            }
        }
        const auto references = [&](std::string_view value) {
            return css_variable_references(value).contains(name);
        };
        // An inherited inline alias may be authored above the mutation root.
        for (auto* parent = root.parent; parent != nullptr; parent = parent->parent) {
            for (const auto& [property, value] : parent->authored_style().declarations) {
                if (references(value) && !is_geometry_property(property)) return false;
            }
        }
        std::vector<const css_rule*> rules;
        if (const auto found = css_rules_by_variable_reference.find(name);
            found != css_rules_by_variable_reference.end()) {
            for (const auto index : found->second) {
                const auto& rule = css_rules[index];
                std::string origin;
                if (rule.shadow_scope_root_id != 0U
                    || split_pseudo_element_selector(rule.selector(), origin) != 0)
                    return false;
                for (const auto& declaration : rule.declarations()) {
                    if (references(declaration.value) && !is_geometry_property(declaration.name))
                        return false;
                }
                rules.push_back(&rule);
            }
        }
        std::vector<dom_node*> nodes{&root};
        std::vector<dom_node*> consumers;
        for (size_t i = 0; i < nodes.size(); ++i) {
            auto& node = *nodes[i];
            // Shadow inheritance and independent browsing contexts retain their
            // existing full-cascade handling.
            if (document.shadow_dom(node) != nullptr) return false;
            for (auto* child : node.children) {
                if (child != nullptr && !inactive_css_cascades.contains(child))
                    nodes.push_back(child);
            }
            bool affected = &node == &root;
            for (const auto& [property, value] : node.authored_style().declarations) {
                if (is_geometry_property(property)
                    && trim_css_view(resolve_css_value(node, value)) == "inherit") return false;
                if (!references(value)) continue;
                if (!is_geometry_property(property)) return false;
                affected = true;
            }
            if (node.kind == dom_node_kind::element) {
                for (const auto* rule : inheritance_rules) {
                    if (!rule->media_matches || !css_rule_matches(node, *rule)) continue;
                    for (const auto& declaration : rule->declarations()) {
                        if (is_geometry_property(declaration.name)
                            && trim_css_view(resolve_css_value(node, declaration.value)) == "inherit")
                            return false;
                    }
                }
            }
            if (!affected && node.kind == dom_node_kind::element) {
                affected = std::any_of(rules.begin(), rules.end(), [&](const auto* rule) {
                    return rule->media_matches && css_rule_matches(node, *rule);
                });
            }
            if (affected) consumers.push_back(&node);
        }
        // No partial updates before all conservative fallback checks succeed.
        for (auto* node : consumers) apply_css_rules(*node);
        return true;
    }

    void apply_css_rules_subtree(dom_node& node)
    {
        apply_css_rules(node);
        for (auto* child : node.children) {
            if (child == nullptr) continue;
            // An iframe owns an independent stylesheet cascade. A recascade
            // of an outer sibling or ancestor must never walk through that
            // browsing-context boundary with the caller's active rules.
            if (child != active_css_cascade_root
                && inactive_css_cascades.contains(child)) {
                continue;
            }
            apply_css_rules_subtree(*child);
        }
        const auto* shadow = document.shadow_dom(node);
        if (shadow != nullptr && shadow->root != nullptr) {
            apply_css_rules_subtree(*shadow->root);
        }
    }

    bool is_connected(const dom_node& node)
    {
        for (auto* current = &node; current != nullptr; current = current->parent) {
            if (current == &document.body()) return true;
            if (current->parent == nullptr
                && detached_document_roots.contains(current->id)) return true;
            for (const auto& [frame_id, provisional_body] : provisional_frame_bodies) {
                if (current != provisional_body) continue;
                const auto* frame = document.find_by_native_id(frame_id);
                for (auto* owner = frame; owner != nullptr; owner = owner->parent) {
                    if (owner == &document.body()) return true;
                }
            }
        }
        return false;
    }

    void recascade_connected_subtree_now(
        dom_node& node,
        std::string_view reason = {})
    {
        webscene_frame_trace::scope cascade_trace(frame_trace, "recascade-start", "recascade-end", 0);
        if (!is_connected(node)) return;
        auto* previous_cascade_root = const_cast<dom_node*>(active_css_cascade_root);
        auto* node_cascade_root = css_cascade_root_for_node(node);
        if (node_cascade_root != nullptr) activate_css_cascade(node_cascade_root);
        retain_reconnected_subtree_wrappers(node);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto trace = profile_css;
        const auto candidate_checks_before = css_profile_candidate_checks;
        const auto matched_rules_before = css_profile_matched_rules;
        const auto declarations_before = css_profile_declaration_applications;
        const auto reset_nanoseconds_before = css_profile_reset_nanoseconds;
        const auto matching_nanoseconds_before = css_profile_matching_nanoseconds;
        const auto declaration_nanoseconds_before =
            css_profile_declarations_nanoseconds;
        const auto finalize_nanoseconds_before = css_profile_finalize_nanoseconds;
        if (trace
            && std::getenv("WEBSCENE_PROBE_TRACE_UNINDEXED_CSS") != nullptr
            && last_traced_unindexed_css_count != unindexed_css_rules.size()) {
            last_traced_unindexed_css_count = unindexed_css_rules.size();
            for (const auto index : unindexed_css_rules) {
                if (index < css_rules.size()) {
                    std::cerr << "[Native Engine Probe] CSS unindexed rule="
                        << index << ", selector=" << css_rules[index].selector() << '\n';
                }
            }
        }
        const auto started = trace ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        binding_callback_timer timer(profile_startup ? &startup_subtree_recascade : nullptr);
#else
        static_cast<void>(reason);
#endif
        apply_css_rules_subtree(node);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (trace) {
            const auto elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
            if (elapsed >= 0.5) {
                size_t descendants = 0;
                std::vector<const dom_node*> pending{&node};
                while (!pending.empty()) {
                    const auto* current = pending.back();
                    pending.pop_back();
                    ++descendants;
                    pending.insert(pending.end(), current->children.begin(), current->children.end());
                }
                std::cerr << "[Native Engine Probe] CSS recascade "
                    << std::fixed << std::setprecision(3) << elapsed << " ms, nodes="
                    << descendants
                    << ", candidates="
                    << (css_profile_candidate_checks - candidate_checks_before)
                    << ", matched="
                    << (css_profile_matched_rules - matched_rules_before)
                    << ", declarations="
                    << (css_profile_declaration_applications - declarations_before)
                    << ", stages-ms="
                    << (css_profile_reset_nanoseconds - reset_nanoseconds_before)
                        / 1'000'000.0
                    << "/"
                    << (css_profile_matching_nanoseconds - matching_nanoseconds_before)
                        / 1'000'000.0
                    << "/"
                    << (css_profile_declarations_nanoseconds
                            - declaration_nanoseconds_before)
                        / 1'000'000.0
                    << "/"
                    << (css_profile_finalize_nanoseconds - finalize_nanoseconds_before)
                        / 1'000'000.0
                    << ", rules=" << css_rules.size()
                    << ", unindexed=" << unindexed_css_rules.size()
                    << ", tag=" << node.tag << ", id=" << node.id_attribute
                    << ", reason=" << reason
                    << ", class=" << node.class_name.substr(0, 180) << '\n';
            }
        }
#endif
        if (previous_cascade_root != nullptr
            && previous_cascade_root != node_cascade_root) {
            activate_css_cascade(previous_cascade_root);
        }
    }

    void schedule_style_recascade(
        dom_node& root,
        bool subtree,
        std::string_view reason,
        std::string custom_property = {})
    {
        if (!is_connected(root)) return;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++style_recascade_schedule_requests;
#endif
        auto* cascade_root = css_cascade_root_for_node(root);
        for (auto& pending : pending_style_recascades) {
            if (pending.root != &root || pending.cascade_root != cascade_root) continue;
            pending.subtree = pending.subtree || subtree;
            if (pending.custom_property != custom_property) pending.custom_property.clear();
            if (pending.reason.empty() && !reason.empty()) pending.reason = reason;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            ++style_recascade_coalesced_requests;
#endif
            return;
        }
        for (auto& pending : pending_style_recascades) {
            if (!pending.subtree || pending.cascade_root != cascade_root
                || pending.root == nullptr || !subtree_contains(*pending.root, &root))
                continue;
            // An ancestor request now covers another mutation as well.
            pending.custom_property.clear();
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            ++style_recascade_coalesced_requests;
#endif
            return;
        }
        if (subtree) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            const auto pending_before = pending_style_recascades.size();
#endif
            if (std::any_of(pending_style_recascades.begin(), pending_style_recascades.end(),
                    [this, &root, cascade_root](const auto& pending) {
                        return pending.root != nullptr && pending.cascade_root == cascade_root
                            && subtree_contains(root, pending.root);
                    })) custom_property.clear();
            std::erase_if(
                pending_style_recascades,
                [this, &root, cascade_root](const pending_style_recascade& pending) {
                    return pending.root != nullptr
                        && pending.cascade_root == cascade_root
                        && subtree_contains(root, pending.root);
                });
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            style_recascade_coalesced_requests +=
                pending_before - pending_style_recascades.size();
#endif
        }
        pending_style_recascades.push_back(
            pending_style_recascade{
                &root,
                cascade_root,
                subtree,
                std::string(reason),
                std::move(custom_property)});
    }

    void flush_pending_style_recascades()
    {
        if (pending_style_recascades.empty()) return;
        auto pending = std::move(pending_style_recascades);
        pending_style_recascades.clear();
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++style_recascade_flush_batches;
        style_recascade_flush_roots += pending.size();
#endif
        for (const auto& recascade : pending) {
            if (recascade.root == nullptr || !is_connected(*recascade.root)) continue;
            if (recascade.subtree) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                ++style_recascade_flush_subtree_roots;
#endif
                if (recascade.custom_property.empty()
                    || !recascade_dimension_custom_property(*recascade.root, recascade.custom_property))
                    recascade_connected_subtree_now(*recascade.root, recascade.reason);
            } else {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                ++style_recascade_flush_node_roots;
#endif
                recascade_connected_node(*recascade.root);
            }
        }
    }

    void recascade_custom_property(dom_node& node, const std::string& name)
    {
        if (style_recascade_batch_depth != 0U) {
            schedule_style_recascade(node, true, "style.custom-property", name);
        } else if (!recascade_dimension_custom_property(node, name)) {
            recascade_connected_subtree_now(node, "style.custom-property");
        }
    }

    void recascade_connected_subtree(
        dom_node& node,
        std::string_view reason = {})
    {
        if (style_recascade_batch_depth != 0U) {
            binding_callback_timer timer(
                nullptr,
                profile_named("style-recascade/subtree-scheduled"));
            schedule_style_recascade(node, true, reason);
            return;
        }
        binding_callback_timer timer(
            nullptr,
            profile_named("style-recascade/subtree-immediate"));
        recascade_connected_subtree_now(node, reason);
    }

    void recascade_connected_node(dom_node& node)
    {
        if (!is_connected(node)) return;
        if (style_recascade_batch_depth != 0U) {
            binding_callback_timer timer(
                nullptr,
                profile_named("style-recascade/node-scheduled"));
            schedule_style_recascade(node, false, {});
            return;
        }
        binding_callback_timer timer(
            nullptr,
            profile_named("style-recascade/node-immediate"));
        auto* previous_cascade_root = const_cast<dom_node*>(active_css_cascade_root);
        auto* node_cascade_root = css_cascade_root_for_node(node);
        if (node_cascade_root != nullptr) activate_css_cascade(node_cascade_root);
        apply_css_rules(node);
        if (previous_cascade_root != nullptr
            && previous_cascade_root != node_cascade_root) {
            activate_css_cascade(previous_cascade_root);
        }
    }

    void recascade_hover_transition(
        dom_node* previous,
        dom_node* next,
        std::string_view reason)
    {
        if (previous == next || hover_selector_dependencies.empty()) return;

        std::vector<dom_node*> previous_path;
        std::vector<dom_node*> next_path;
        for (auto* node = previous; node != nullptr; node = node->parent) {
            previous_path.push_back(node);
        }
        for (auto* node = next; node != nullptr; node = node->parent) {
            next_path.push_back(node);
        }
        while (!previous_path.empty() && !next_path.empty()
            && previous_path.back() == next_path.back()) {
            previous_path.pop_back();
            next_path.pop_back();
        }
        std::vector<dom_node*> changed = std::move(previous_path);
        changed.insert(changed.end(), next_path.begin(), next_path.end());

        struct invalidation_request final
        {
            dom_node* root{nullptr};
            bool subtree{false};
        };
        std::vector<invalidation_request> requests;
        const auto add_request = [&](dom_node* root, bool subtree) {
            if (root == nullptr || !is_connected(*root)) return;
            for (auto& existing : requests) {
                if (existing.root != root) continue;
                existing.subtree = existing.subtree || subtree;
                return;
            }
            if (std::any_of(
                    requests.begin(),
                    requests.end(),
                    [this, root](const invalidation_request& existing) {
                        return existing.subtree
                            && subtree_contains(*existing.root, root);
                    })) {
                return;
            }
            if (subtree) {
                std::erase_if(
                    requests,
                    [this, root](const invalidation_request& existing) {
                        return subtree_contains(*root, existing.root);
                    });
            }
            requests.push_back({root, subtree});
        };

        for (auto* changed_node : changed) {
            if (changed_node == nullptr) continue;
            for (const auto& dependency : hover_selector_dependencies) {
                if (dependency.trigger_compound != "*"
                    && !css_compound_selector_matches(
                        *changed_node,
                        dependency.trigger_compound)) {
                    continue;
                }
                switch (dependency.scope) {
                case hover_invalidation_scope::subject:
                    add_request(changed_node, false);
                    break;
                case hover_invalidation_scope::subtree:
                    add_request(changed_node, true);
                    break;
                case hover_invalidation_scope::siblings:
                    add_request(
                        changed_node->parent == nullptr
                            ? changed_node
                            : changed_node->parent,
                        true);
                    break;
                }
            }
        }

#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto trace = profile_css;
        const auto started = trace
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        size_t recalculated_nodes = 0U;
#endif
        for (const auto& request : requests) {
            if (request.root == nullptr) continue;
            if (request.subtree) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                if (trace) {
                    visit_subtree(*request.root, [&](const dom_node&) {
                        ++recalculated_nodes;
                    });
                }
#endif
                recascade_connected_subtree(*request.root, reason);
            } else {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                if (trace) ++recalculated_nodes;
#endif
                apply_css_rules(*request.root);
            }
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (trace) {
            const auto elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
            std::cerr << "[Native Engine Probe] CSS hover invalidation "
                << std::fixed << std::setprecision(3) << elapsed
                << " ms, changed=" << changed.size()
                << ", dependencies=" << hover_selector_dependencies.size()
                << ", roots=" << requests.size()
                << ", nodes=" << recalculated_nodes
                << ", reason=" << reason << '\n';
        }
#endif
    }

    void rebuild_root_variables()
    {
#if defined(WEBSCENE_NATIVE_ENGINE_MEDIA_REFRESH_BENCHMARK_COUNTERS)
        media_refresh_root_variable_refreshes.fetch_add(1U, std::memory_order_relaxed);
#endif
        css::rebuild_root_variables(css_rules,css_variables,important_css_variables);
    }

    void rebuild_css_rule_indexes_and_root_variables()
    {
        css_rules_by_class.clear();
        css_rules_by_id.clear();
        css_rules_by_tag.clear();
        css_rules_by_attribute.clear();
        css_rules_by_variable_reference.clear();
        css_focus_rules.clear();
        unindexed_css_rules.clear();
        css_attribute_dependencies.clear();
        css_descendant_attribute_dependencies.clear();
        for (size_t index = 0; index < css_rules.size(); ++index) {
            index_css_rule(index);
        }
        rebuild_root_variables();
    }

    void recascade_after_stylesheet_change()
    {
        apply_css_rules_subtree(active_root());
        document.mark_dirty();
    }

    bool deactivate_connected_stylesheets(dom_node& root, bool recascade = true)
    {
        std::unordered_set<uint32_t> owners;
        visit_subtree(root, [&](const dom_node& node) {
            if (node.tag == "style") owners.insert(node.id);
        });
        if (owners.empty()) return false;
        const auto previous_size = css_rules.size();
        std::erase_if(css_rules, [&](const css_rule& rule) {
            return rule.stylesheet_owner_id != 0
                && owners.contains(rule.stylesheet_owner_id);
        });
        if (css_rules.size() == previous_size) return false;
        rebuild_css_rule_indexes_and_root_variables();
        if (recascade) recascade_after_stylesheet_change();
        return true;
    }

    void activate_connected_stylesheet(dom_node& node)
    {
        std::vector<dom_node*> styles;
        for (auto* ancestor = &node; ancestor != nullptr; ancestor = ancestor->parent) {
            if (ancestor->tag != "style") continue;
            styles.push_back(ancestor);
            break;
        }
        if (styles.empty()) {
            visit_subtree(node, [&](dom_node& candidate) {
                if (candidate.tag == "style") styles.push_back(&candidate);
            });
        }
        std::erase_if(styles, [&](const dom_node* style) {
            return style == nullptr || !is_connected(*style);
        });
        if (styles.empty()) return;

        for (auto* style : styles) deactivate_connected_stylesheets(*style, false);
        const auto& base = current_base_address();
        for (auto* style : styles) {
            std::string stylesheet;
            append_node_text(*style, stylesheet);
            if (!stylesheet.empty()) {
                const auto* shadow_root = document.containing_shadow_root(*style);
                add_stylesheet(
                    std::move(stylesheet),
                    base,
                    style->id,
                    shadow_root == nullptr ? 0U : shadow_root->id);
            }
        }
        recascade_after_stylesheet_change();
    }

    static std::vector<std::string> parse_frame_stylesheets(const std::string& html)
    {
        std::vector<std::string> result;
        size_t cursor = 0;
        while (true) {
            const auto open = html.find("<link", cursor);
            if (open == std::string::npos) break;
            const auto close = html.find('>', open + 5U);
            if (close == std::string::npos) break;
            const auto tag = html.substr(open, close - open + 1U);
            const auto relation = extract_html_attribute(tag, "rel");
            const auto href = extract_html_attribute(tag, "href");
            if (relation == "stylesheet" && !href.empty()) result.push_back(href);
            cursor = close + 1U;
        }
        return result;
    }
