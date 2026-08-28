    static std::string_view trim_css_view(std::string_view value)
    {
        while (!value.empty() && is_css_whitespace(static_cast<unsigned char>(value.front()))) {
            value.remove_prefix(1U);
        }
        while (!value.empty() && is_css_whitespace(static_cast<unsigned char>(value.back()))) {
            value.remove_suffix(1U);
        }
        return value;
    }

    static std::string trim_css(std::string_view value)
    {
        return std::string(trim_css_view(value));
    }

    static size_t matching_css_brace(const std::string& css, size_t open, size_t end)
    {
        size_t depth = 1U;
        char quote = 0;
        for (size_t index = open + 1U; index < end; ++index) {
            const auto character = css[index];
            if (quote != 0) {
                if (character == quote && (index == 0 || css[index - 1U] != '\\')) quote = 0;
                continue;
            }
            if (character == '\'' || character == '"') quote = character;
            else if (character == '{') ++depth;
            else if (character == '}' && --depth == 0U) return index;
        }
        return std::string::npos;
    }

    static std::string copy_ascii_lower(std::string_view value)
    {
        auto result = std::string(value);
        for (auto& character : result) {
            if (character >= 'A' && character <= 'Z') {
                character = static_cast<char>(character + ('a' - 'A'));
            }
        }
        return result;
    }

    static void append_css_syntax_declaration(
        std::vector<css_declaration>& result,
        std::string_view raw_name,
        std::string_view raw_value,
        bool important,
        bool preserve_empty_custom_properties = false)
    {
        auto name = raw_name.starts_with("--")
            ? std::string(raw_name)
            : copy_ascii_lower(raw_name);
        auto value = std::string(raw_value);
        const auto custom = name.starts_with("--");
        if (custom && !valid_custom_property_name(name)) return;
        if (!custom && name.starts_with("-")
            && name != "-moz-transform"
            && name != "-webkit-transform"
            && name != "-webkit-font-smoothing") return;
        if (custom && value.empty() && preserve_empty_custom_properties) value = " ";
        if (!name.empty() && !value.empty()) {
            result.push_back({std::move(name), std::move(value), important});
        }
    }

    static std::vector<css_declaration> parse_css_declarations(
        std::string_view body,
        bool preserve_empty_custom_properties = false)
    {
        std::vector<css_declaration> result;
#if defined(WEBSCENE_NATIVE_ENGINE_CSSPARSER)
        class declaration_sink final : public css_syntax_sink {
        public:
            declaration_sink(
                std::vector<css_declaration>& declarations,
                bool preserve_empty_custom_properties)
                : declarations_(declarations),
                  preserve_empty_custom_properties_(preserve_empty_custom_properties)
            {
            }

            bool begin_rule(
                uint32_t,
                bool,
                size_t,
                std::string_view,
                std::string_view,
                size_t&) override
            {
                return false;
            }

            bool declaration(
                std::string_view name,
                std::string_view value,
                bool important) override
            {
                append_css_syntax_declaration(
                    declarations_, name, value, important,
                    preserve_empty_custom_properties_);
                return true;
            }

            bool end_rule(size_t, size_t) override { return false; }

        private:
            std::vector<css_declaration>& declarations_;
            bool preserve_empty_custom_properties_;
        };

        result.reserve(body.size() / 64U);
        declaration_sink sink(result, preserve_empty_custom_properties);
        const auto parsed = stream_css_syntax_declarations(body, sink);
        if (!parsed) result.clear();
#else
        size_t cursor = 0;
        while (cursor < body.size()) {
            const auto separator = body.find(':', cursor);
            if (separator == std::string_view::npos) break;
            auto end = body.find(';', separator + 1U);
            if (end == std::string_view::npos) end = body.size();
            auto name = trim_css(body.substr(cursor, separator - cursor));
            auto value = trim_css(body.substr(separator + 1U, end - separator - 1U));
            auto lower_value = lower_html_name(value);
            const auto important_marker = lower_value.ends_with("!important")
                ? value.size() - std::string_view("!important").size()
                : std::string::npos;
            const auto important = important_marker != std::string::npos;
            if (important) {
                value = trim_css(std::string_view(value).substr(0, important_marker));
            }
            const auto custom = name.starts_with("--");
            if (custom && !valid_custom_property_name(name)) {
                cursor = end + 1U;
                continue;
            }
            if (!custom && name.starts_with("-")
                && name != "-moz-transform"
                && name != "-webkit-transform"
                && name != "-webkit-font-smoothing") {
                cursor = end + 1U;
                continue;
            }
            if (custom && value.empty() && preserve_empty_custom_properties) {
                value = " ";
            }
            if (!name.empty() && !value.empty()) {
                result.push_back({std::move(name), std::move(value), important});
            }
            cursor = end + 1U;
        }
#endif
        return result;
    }

    static bool store_inline_declaration(
        dom_node& node,
        const css_declaration& declaration)
    {
        const auto existing_important =
            node.authored_style().important_declarations.contains(declaration.name);
        if (existing_important && !declaration.important) {
            return false;
        }

        auto& authored = node.mutable_authored_style();
        authored.declarations[declaration.name] = serialize_cssom_specified_value(
            declaration.name,
            declaration.value);
        if (declaration.important) {
            authored.important_declarations.insert(declaration.name);
        } else {
            authored.important_declarations.erase(declaration.name);
        }
        return true;
    }

    static void strip_hover_state_from_trigger(std::string& trigger)
    {
        // A functional pseudo containing :hover changes truth when hover does,
        // but its other arguments are not prerequisites for invalidation.
        // Removing the complete function deliberately broadens the structural
        // trigger while retaining class/id/tag/attribute filtering.
        for (;;) {
            auto removed_function = false;
            for (size_t colon = 0; colon < trigger.size(); ++colon) {
                if (trigger[colon] != ':' || colon + 1U >= trigger.size()
                    || trigger[colon + 1U] == ':') {
                    continue;
                }
                auto open = colon + 1U;
                while (open < trigger.size()
                    && (std::isalnum(static_cast<unsigned char>(trigger[open]))
                        || trigger[open] == '-' || trigger[open] == '_')) {
                    ++open;
                }
                if (open >= trigger.size() || trigger[open] != '(') continue;
                auto depth = 1;
                char quote = 0;
                auto close = open + 1U;
                for (; close < trigger.size() && depth > 0; ++close) {
                    const auto character = trigger[close];
                    if (quote != 0) {
                        if (character == quote
                            && trigger[close - 1U] != '\\') {
                            quote = 0;
                        }
                        continue;
                    }
                    if (character == '\'' || character == '"') quote = character;
                    else if (character == '(') ++depth;
                    else if (character == ')') --depth;
                }
                if (depth != 0) break;
                if (trigger.find(":hover", open + 1U) < close - 1U) {
                    trigger.erase(colon, close - colon);
                    removed_function = true;
                    break;
                }
                colon = close - 1U;
            }
            if (!removed_function) break;
        }
        for (size_t position = 0;
            (position = trigger.find(":hover", position)) != std::string::npos;) {
            trigger.erase(position, 6U);
        }
    }

    void index_hover_selector_dependencies(const css_rule& rule)
    {
        const auto& selector = rule.selector();
        size_t search = 0U;
        while ((search = selector.find(":hover", search)) != std::string::npos) {
            const auto token_end = search + 6U;
            if (token_end < selector.size()) {
                const auto next = selector[token_end];
                if (std::isalnum(static_cast<unsigned char>(next))
                    || next == '-' || next == '_') {
                    search = token_end;
                    continue;
                }
            }

            auto compound_begin = size_t{0};
            auto bracket_depth = 0;
            auto parenthesis_depth = 0;
            char quote = 0;
            for (size_t index = 0; index < search; ++index) {
                const auto character = selector[index];
                if (quote != 0) {
                    if (character == quote
                        && (index == 0U || selector[index - 1U] != '\\')) {
                        quote = 0;
                    }
                    continue;
                }
                if (character == '\\') {
                    index = skip_css_escape_sequence(selector, index) - 1U;
                    continue;
                }
                if (character == '\'' || character == '"') {
                    quote = character;
                    continue;
                }
                if (character == '[') ++bracket_depth;
                else if (character == ']') --bracket_depth;
                else if (character == '(') ++parenthesis_depth;
                else if (character == ')') --parenthesis_depth;
                else if (bracket_depth == 0 && parenthesis_depth == 0
                    && (std::isspace(static_cast<unsigned char>(character))
                        || character == '>' || character == '+'
                        || character == '~' || character == ',')) {
                    compound_begin = index + 1U;
                }
            }
            while (compound_begin < search
                && std::isspace(
                    static_cast<unsigned char>(selector[compound_begin]))) {
                ++compound_begin;
            }
            auto compound_end = compound_begin;
            bracket_depth = 0;
            parenthesis_depth = 0;
            quote = 0;
            for (; compound_end < selector.size(); ++compound_end) {
                const auto character = selector[compound_end];
                if (quote != 0) {
                    if (character == quote
                        && (compound_end == 0U
                            || selector[compound_end - 1U] != '\\')) {
                        quote = 0;
                    }
                    continue;
                }
                if (character == '\\') {
                    compound_end =
                        skip_css_escape_sequence(selector, compound_end) - 1U;
                    continue;
                }
                if (character == '\'' || character == '"') {
                    quote = character;
                    continue;
                }
                if (character == '[') ++bracket_depth;
                else if (character == ']') --bracket_depth;
                else if (character == '(') ++parenthesis_depth;
                else if (character == ')') --parenthesis_depth;
                else if (bracket_depth == 0 && parenthesis_depth == 0
                    && (std::isspace(static_cast<unsigned char>(character))
                        || character == '>' || character == '+'
                        || character == '~' || character == ',')) {
                    break;
                }
            }

            auto trigger = selector.substr(
                compound_begin,
                compound_end - compound_begin);
            strip_hover_state_from_trigger(trigger);
            if (const auto pseudo_element = trigger.find("::");
                pseudo_element != std::string::npos) {
                trigger.resize(pseudo_element);
            }
            if (trigger.ends_with(":before")) trigger.resize(trigger.size() - 7U);
            else if (trigger.ends_with(":after")) trigger.resize(trigger.size() - 6U);
            if (trigger.empty()) trigger = "*";

            auto scope = hover_invalidation_scope::subject;
            for (auto position = compound_end; position < selector.size(); ++position) {
                const auto character = selector[position];
                if (character == '+' || character == '~') {
                    scope = hover_invalidation_scope::siblings;
                    break;
                }
                if (character == '>'
                    || std::isspace(static_cast<unsigned char>(character))) {
                    scope = hover_invalidation_scope::subtree;
                }
            }
            if (scope == hover_invalidation_scope::subject
                && std::any_of(
                    rule.declarations().begin(),
                    rule.declarations().end(),
                    [](const css_declaration& declaration) {
                        return declaration.name.starts_with("--");
                    })) {
                scope = hover_invalidation_scope::subtree;
            }
            hover_selector_dependencies.push_back({
                std::move(trigger),
                scope});
            search = token_end;
        }
    }

    static std::unordered_set<std::string> css_variable_references(
        std::string_view value)
    {
        std::unordered_set<std::string> references;
        for (size_t search = 0U;
             (search = value.find("var(", search)) != std::string_view::npos;) {
            auto cursor = search + 4U;
            while (cursor < value.size()
                && std::isspace(static_cast<unsigned char>(value[cursor]))) {
                ++cursor;
            }
            const auto start = cursor;
            while (cursor < value.size()
                && value[cursor] != ',' && value[cursor] != ')'
                && !std::isspace(static_cast<unsigned char>(value[cursor]))) {
                ++cursor;
            }
            if (cursor > start
                && value.substr(start, cursor - start).starts_with("--")) {
                references.emplace(value.substr(start, cursor - start));
            }
            search += 4U;
        }
        return references;
    }

    void index_css_rule(size_t index)
    {
        const auto& rule = css_rules[index];
        const auto& selector = rule.selector();
        if (selector.find("::selection") != std::string::npos) {
            return;
        }
        std::unordered_set<std::string> variable_references;
        for (const auto& declaration : rule.declarations()) {
            auto declaration_references = css_variable_references(declaration.value);
            variable_references.insert(
                std::make_move_iterator(declaration_references.begin()),
                std::make_move_iterator(declaration_references.end()));
        }
        for (auto& reference : variable_references) {
            css_rules_by_variable_reference[std::move(reference)].push_back(index);
        }
        index_hover_selector_dependencies(rule);
        if (selector.find(":disabled") != std::string::npos
            || selector.find(":enabled") != std::string::npos) {
            css_attribute_dependencies.emplace("disabled");
        }
        if (selector.find(":checked") != std::string::npos) {
            css_attribute_dependencies.emplace("checked");
            css_attribute_dependencies.emplace("selected");
            css_attribute_dependencies.emplace("type");
        }
        for (size_t open = 0; (open = selector.find('[', open)) != std::string::npos;) {
            auto cursor = open + 1U;
            while (cursor < selector.size()
                && std::isspace(static_cast<unsigned char>(selector[cursor]))) ++cursor;
            const auto start = cursor;
            while (cursor < selector.size()
                && (std::isalnum(static_cast<unsigned char>(selector[cursor]))
                    || selector[cursor] == '-' || selector[cursor] == '_')) ++cursor;
            if (cursor > start) {
                css_attribute_dependencies.emplace(selector.substr(start, cursor - start));
            }
            open = cursor;
        }
        size_t compound_begin = 0;
        int bracket_depth = 0;
        int parenthesis_depth = 0;
        for (size_t offset = selector.size(); offset > 0; --offset) {
            const auto position = offset - 1U;
            const auto character = selector[position];
            if (character == ']') ++bracket_depth;
            else if (character == '[') --bracket_depth;
            else if (character == ')') ++parenthesis_depth;
            else if (character == '(') --parenthesis_depth;
            else if (bracket_depth == 0 && parenthesis_depth == 0
                && (std::isspace(static_cast<unsigned char>(character))
                    || character == '>' || character == '+' || character == '~')) {
                compound_begin = position + 1U;
                break;
            }
        }
        // An attribute in an ancestor compound can change which descendants
        // match. Attribute selectors in the rightmost compound only require
        // recascading the mutated subject.
        for (size_t open = 0;
             (open = selector.find('[', open)) != std::string_view::npos;) {
            auto cursor = open + 1U;
            while (cursor < selector.size()
                && std::isspace(static_cast<unsigned char>(selector[cursor]))) {
                ++cursor;
            }
            const auto start = cursor;
            while (cursor < selector.size()
                && (std::isalnum(static_cast<unsigned char>(selector[cursor]))
                    || selector[cursor] == '-' || selector[cursor] == '_')) {
                ++cursor;
            }
            if (cursor > start
                && (open < compound_begin
                    || selector.find(":has(") != std::string_view::npos)) {
                css_descendant_attribute_dependencies.emplace(
                    selector.substr(start, cursor - start));
            }
            open = cursor;
        }
        auto compound = std::string_view(selector).substr(compound_begin);
        while (!compound.empty()
            && std::isspace(static_cast<unsigned char>(compound.front()))) {
            compound.remove_prefix(1U);
        }

        size_t pseudo = compound.size();
        bracket_depth = 0;
        for (size_t position = 0; position < compound.size(); ++position) {
            if (compound[position] == '[') ++bracket_depth;
            else if (compound[position] == ']') --bracket_depth;
            else if (compound[position] == ':' && bracket_depth == 0) {
                pseudo = position;
                break;
            }
        }
        compound = compound.substr(0, pseudo);

        const auto token_end = [&](size_t start) {
            auto end = start;
            while (end < compound.size()
                && compound[end] != '.' && compound[end] != '#'
                && compound[end] != '[' && compound[end] != ':') ++end;
            return end;
        };
        bracket_depth = 0;
        for (size_t position = 0; position < compound.size(); ++position) {
            if (compound[position] == '[') ++bracket_depth;
            else if (compound[position] == ']') --bracket_depth;
            else if (bracket_depth == 0
                && (compound[position] == '#' || compound[position] == '.')) {
                const auto end = token_end(position + 1U);
                const auto key = std::string(compound.substr(position + 1U, end - position - 1U));
                if (!key.empty()) {
                    auto& target = compound[position] == '#'
                        ? css_rules_by_id[key]
                        : css_rules_by_class[key];
                    target.push_back(index);
                    return;
                }
            }
        }
        if (!compound.empty() && std::isalpha(static_cast<unsigned char>(compound.front()))) {
            size_t end = 1U;
            while (end < compound.size()
                && (std::isalnum(static_cast<unsigned char>(compound[end]))
                    || compound[end] == '-')) ++end;
            css_rules_by_tag[std::string(compound.substr(0, end))].push_back(index);
            return;
        }
        if (const auto open = compound.find('['); open != std::string_view::npos) {
            auto cursor = open + 1U;
            while (cursor < compound.size()
                && std::isspace(static_cast<unsigned char>(compound[cursor]))) {
                ++cursor;
            }
            const auto start = cursor;
            while (cursor < compound.size()
                && (std::isalnum(static_cast<unsigned char>(compound[cursor]))
                    || compound[cursor] == '-' || compound[cursor] == '_')) {
                ++cursor;
            }
            if (cursor > start) {
                css_rules_by_attribute[
                    std::string(compound.substr(start, cursor - start))].push_back(index);
                return;
            }
        }
        if (trim_css_view(selector) == ":root") {
            css_rules_by_tag["html"].push_back(index);
            return;
        }
        if (trim_css_view(selector) == ":focus") {
            css_focus_rules.push_back(index);
            return;
        }
        unindexed_css_rules.push_back(index);
    }

    bool attribute_requires_recascade(std::string_view name) const
    {
        if (name == "id" || name == "class" || name == "style"
            || name == "type" || name == "hidden") return true;
        return css_attribute_dependencies.contains(std::string(name));
    }

    bool attribute_may_affect_descendants(std::string_view name) const
    {
        return css_descendant_attribute_dependencies.contains(std::string(name));
    }

    bool media_query_matches(std::string query) const
    {
        std::transform(query.begin(), query.end(), query.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        const auto [
            viewport_width,
            viewport_height,
            device_scale_factor,
            preferred_color_scheme] = viewport_provider();
        static_cast<void>(device_scale_factor);
        size_t alternative_start = 0;
        while (alternative_start <= query.size()) {
            auto alternative_end = query.find(',', alternative_start);
            if (alternative_end == std::string::npos) alternative_end = query.size();
            auto alternative = trim_css(std::string_view(query).substr(
                alternative_start,
                alternative_end - alternative_start));
            const auto negated = alternative.starts_with("not ");
            if (negated) alternative = trim_css(std::string_view(alternative).substr(4U));
            auto matches = true;
            if (alternative.starts_with("all and")) {
                alternative = trim_css(std::string_view(alternative).substr(7U));
            } else if (alternative.starts_with("screen and")) {
                alternative = trim_css(std::string_view(alternative).substr(10U));
            } else if (alternative == "all" || alternative == "screen"
                || alternative.starts_with("(")) {
            } else if (alternative == "print" || alternative.starts_with("print and")) {
                matches = false;
            } else {
                matches = false;
            }

            size_t condition_start = 0;
            while (matches && (condition_start = alternative.find('(', condition_start))
                != std::string::npos) {
                const auto condition_end = alternative.find(')', condition_start + 1U);
                if (condition_end == std::string::npos) break;
                const auto condition = trim_css(std::string_view(alternative).substr(
                    condition_start + 1U,
                    condition_end - condition_start - 1U));
                const auto separator = condition.find(':');
                const auto feature = trim_css(std::string_view(condition).substr(0, separator));
                const auto value = separator == std::string::npos
                    ? std::string{} : trim_css(std::string_view(condition).substr(separator + 1U));
                const auto number = std::strtof(value.c_str(), nullptr);
                if (feature == "max-width") matches = viewport_width <= number;
                else if (feature == "min-width") matches = viewport_width >= number;
                else if (feature == "max-height") matches = viewport_height <= number;
                else if (feature == "min-height") matches = viewport_height >= number;
                else if (feature == "orientation") {
                    matches = value == "landscape"
                        ? viewport_width >= viewport_height
                        : value == "portrait" && viewport_height > viewport_width;
                } else if (feature == "hover" || feature == "any-hover") {
                    matches = value == "hover";
                } else if (feature == "pointer" || feature == "any-pointer") {
                    matches = value == "fine";
                } else if (feature == "prefers-reduced-motion") {
                    matches = value == "no-preference";
                } else if (feature == "prefers-color-scheme") {
                    matches = value == (
                        preferred_color_scheme == WEBSCENE_PREFERRED_COLOR_SCHEME_DARK
                            ? "dark"
                            : "light");
                } else {
                    matches = false;
                }
                condition_start = condition_end + 1U;
            }
            if (negated) matches = !matches;
            if (matches) return true;
            if (alternative_end == query.size()) break;
            alternative_start = alternative_end + 1U;
        }
        return false;
    }

    bool css_rule_media_matches(const css_rule& rule) const
    {
        return std::all_of(
            rule.media_queries().begin(),
            rule.media_queries().end(),
            [this](const std::string& query) { return media_query_matches(query); });
    }

    static uint64_t css_rule_payload_hash(
        std::string_view selector,
        const std::vector<css_declaration>& declarations,
        const std::vector<std::string>& media_queries)
    {
        auto hash = uint64_t{1469598103934665603ULL};
        const auto append = [&](std::string_view value) {
            for (const auto character : value) {
                hash ^= static_cast<unsigned char>(character);
                hash *= 1099511628211ULL;
            }
            hash ^= 0xffU;
            hash *= 1099511628211ULL;
        };
        append(selector);
        for (const auto& declaration : declarations) {
            append(declaration.name);
            append(declaration.value);
            hash ^= declaration.important ? 1U : 0U;
            hash *= 1099511628211ULL;
        }
        for (const auto& query : media_queries) append(query);
        return hash;
    }

    static bool css_rule_payload_matches(
        const css_rule_payload& payload,
        std::string_view selector,
        const std::vector<css_declaration>& declarations,
        const std::vector<std::string>& media_queries)
    {
        if (payload.selector != selector
            || payload.declarations.size() != declarations.size()
            || payload.media_queries != media_queries) {
            return false;
        }
        for (size_t index = 0; index < declarations.size(); ++index) {
            const auto& left = payload.declarations[index];
            const auto& right = declarations[index];
            if (left.name != right.name || left.value != right.value
                || left.important != right.important) {
                return false;
            }
        }
        return true;
    }

    static std::shared_ptr<const css_rule_payload> intern_css_rule_payload(
        std::string selector,
        const std::vector<css_declaration>& declarations,
        const std::vector<std::string>& media_queries
#if defined(WEBSCENE_NATIVE_ENGINE_SERVO_SELECTORS)
        , const selector_syntax_selector* parsed_selector = nullptr
#endif
        )
    {
#if defined(WEBSCENE_NATIVE_ENGINE_SERVO_SELECTORS)
        static_cast<void>(parsed_selector);
#endif
        const auto hash = css_rule_payload_hash(
            selector,
            declarations,
            media_queries);
        std::lock_guard lock(shared_css_rule_payload_mutex);
        auto& candidates = shared_css_rule_payloads[hash];
        for (auto iterator = candidates.begin(); iterator != candidates.end();) {
            auto candidate = iterator->lock();
            if (candidate == nullptr) {
                iterator = candidates.erase(iterator);
                continue;
            }
            if (css_rule_payload_matches(
                    *candidate,
                    selector,
                    declarations,
                    media_queries)) {
                return candidate;
            }
            ++iterator;
        }
        auto payload = std::make_shared<css_rule_payload>();
        payload->selector = std::move(selector);
#if defined(WEBSCENE_NATIVE_ENGINE_SERVO_SELECTORS)
        // The syntax parser's output is used for validation and indexing, but
        // matching also needs the native pre-tokenized compound representation.
        payload->compiled_selector = compile_css_selector(payload->selector);
#else
        payload->compiled_selector = compile_css_selector(payload->selector);
#endif
        payload->specificity = payload->compiled_selector.specificity;
        payload->declarations = declarations;
        payload->media_queries = media_queries;
        candidates.emplace_back(payload);
        return payload;
    }

    void append_css_rule(
        std::string selector,
        const std::vector<css_declaration>& declarations,
        const std::vector<std::string>& media_queries
#if defined(WEBSCENE_NATIVE_ENGINE_SERVO_SELECTORS)
        , const selector_syntax_selector* parsed_selector = nullptr
#endif
        )
    {
        const auto index = css_rules.size();
        css_rule rule;
        rule.stylesheet_owner_id = active_stylesheet_owner_id;
        rule.shadow_scope_root_id = active_stylesheet_shadow_scope_root_id;
        rule.payload = intern_css_rule_payload(
            std::move(selector),
            declarations,
            media_queries
#if defined(WEBSCENE_NATIVE_ENGINE_SERVO_SELECTORS)
            , parsed_selector
#endif
            );
        rule.media_matches = css_rule_media_matches(rule);
        css_rules.push_back(std::move(rule));
        index_css_rule(index);
    }

    void inventory_css_length_units(
        std::string_view value,
        std::string_view source)
    {
        char quote = 0;
        for (size_t index = 0; index < value.size();) {
            if (quote != 0) {
                if (value[index] == quote
                    && (index == 0U || value[index - 1U] != '\\')) quote = 0;
                ++index;
                continue;
            }
            if (value[index] == '\'' || value[index] == '"') {
                quote = value[index++];
                continue;
            }
            if (!std::isdigit(static_cast<unsigned char>(value[index]))
                && !(value[index] == '.' && index + 1U < value.size()
                    && std::isdigit(static_cast<unsigned char>(value[index + 1U])))) {
                ++index;
                continue;
            }
            if (index > 0U) {
                const auto previous = value[index - 1U];
                if (std::isalnum(static_cast<unsigned char>(previous))
                    || previous == '_' || previous == '#') {
                    ++index;
                    continue;
                }
            }
            while (index < value.size()
                && (std::isdigit(static_cast<unsigned char>(value[index]))
                    || value[index] == '.')) ++index;
            const auto unit_start = index;
            while (index < value.size()
                && std::isalpha(static_cast<unsigned char>(value[index]))) ++index;
            if (unit_start == index) continue;
            const auto unit = lower_html_name(
                std::string(value.substr(unit_start, index - unit_start)));
            if (unit != "em" && unit != "rem"
                && unit != "vw" && unit != "vh"
                && unit != "vmin" && unit != "vmax"
                && unit != "ch" && unit != "ex"
                && unit != "cm" && unit != "mm"
                && unit != "in" && unit != "pt"
                && unit != "pc" && unit != "q") {
                continue;
            }
            record_feature(
                "css",
                "length-unit:" + unit,
                unit == "em" || unit == "rem" || unit == "vw" || unit == "vh"
                    ? "partially-supported" : "unsupported",
                unit == "em" || unit == "rem"
                    ? "default 16px root metric"
                    : unit == "vw" || unit == "vh"
                        ? "top-level logical viewport dimensions"
                        : "the native length parser treats this numeric dimension as px",
                std::string(source));
        }
    }

    void inventory_css_value_functions(
        std::string_view value,
        std::string_view source = "stylesheet-parser")
    {
        inventory_css_length_units(value, source);
        char quote = 0;
        for (size_t index = 0; index < value.size();) {
            const auto character = value[index];
            if (quote != 0) {
                if (character == quote && (index == 0U || value[index - 1U] != '\\')) {
                    quote = 0;
                }
                ++index;
                continue;
            }
            if (character == '\'' || character == '"') {
                quote = character;
                ++index;
                continue;
            }
            if (!std::isalpha(static_cast<unsigned char>(character))
                && character != '-') {
                ++index;
                continue;
            }
            const auto name_start = index++;
            while (index < value.size()
                && (std::isalnum(static_cast<unsigned char>(value[index]))
                    || value[index] == '-')) ++index;
            auto after_name = index;
            while (after_name < value.size()
                && std::isspace(static_cast<unsigned char>(value[after_name]))) ++after_name;
            if (after_name >= value.size() || value[after_name] != '(') continue;

            auto name = lower_html_name(std::string(value.substr(name_start, index - name_start)));
            if (std::none_of(name.begin(), name.end(), [](unsigned char character) {
                    return std::isalpha(character);
                })) {
                continue;
            }
            auto classification = std::string("unsupported");
            auto semantic_slice = std::string{};
            if (name == "var") {
                classification = "partially-supported";
                semantic_slice = "inherited custom-property substitution with fallback; no registered typed properties";
            } else if (name == "calc" || name == "min" || name == "max") {
                classification = "partially-supported";
                semantic_slice = "length arithmetic over px, percent, and unitless zero";
            } else if (name == "rgb" || name == "rgba") {
                classification = "partially-supported";
                semantic_slice = "legacy comma-separated integer RGB channels and numeric alpha";
            } else if (name == "translate" || name == "translatex" || name == "translatey"
                || name == "scale" || name == "scalex" || name == "scaley"
                || name == "rotate") {
                classification = "partially-supported";
                semantic_slice = "one occurrence per 2D transform function in the native fixed transform projection";
            } else if (name == "cubic-bezier") {
                classification = "partially-supported";
                semantic_slice = "transition timing functions with four numeric arguments";
            } else if (name == "url") {
                classification = "partially-supported";
                semantic_slice = "first URL-backed SVG CSS background layer";
            }
            record_feature(
                "css",
                "function:" + name,
                std::move(classification),
                std::move(semantic_slice),
                std::string(source));
        }
    }

    void inventory_css_selector(std::string_view selector)
    {
        auto record_selector_feature = [this](
                                           std::string feature,
                                           std::string classification = "supported",
                                           std::string semantic_slice = {}) {
            record_feature(
                "css",
                std::move(feature),
                std::move(classification),
                std::move(semantic_slice),
                "selector-parser");
        };

        bool saw_type = false;
        bool saw_class = false;
        bool saw_id = false;
        bool saw_universal = false;
        int bracket_depth = 0;
        int parenthesis_depth = 0;
        char quote = 0;
        for (size_t index = 0; index < selector.size(); ++index) {
            const auto character = selector[index];
            if (quote != 0) {
                if (character == quote && (index == 0U || selector[index - 1U] != '\\')) {
                    quote = 0;
                }
                continue;
            }
            if (character == '\'' || character == '"') {
                quote = character;
                continue;
            }
            if (character == '[') {
                ++bracket_depth;
                const auto close = selector.find(']', index + 1U);
                if (close == std::string_view::npos) {
                    record_selector_feature(
                        "selector:attribute",
                        "invalid-authoring",
                        "unterminated attribute selector");
                    break;
                }
                const auto condition = selector.substr(index + 1U, close - index - 1U);
                const auto operator_index = condition.find_first_of("~|^$*=");
                if (operator_index == std::string_view::npos) {
                    record_selector_feature("selector:attribute-presence");
                } else {
                    auto operator_name = std::string(1U, condition[operator_index]);
                    if (condition[operator_index] != '='
                        && operator_index + 1U < condition.size()
                        && condition[operator_index + 1U] == '=') {
                        operator_name.push_back('=');
                    }
                    if (operator_name == "=") {
                        record_selector_feature("selector:attribute-equals");
                    } else if (operator_name == "~=" || operator_name == "|="
                        || operator_name == "^=" || operator_name == "$="
                        || operator_name == "*=") {
                        record_selector_feature(
                            "selector:attribute-operator:" + operator_name,
                            "partially-supported",
                            "case-sensitive matching without selector flags");
                    } else {
                        record_selector_feature(
                            "selector:attribute-operator:" + operator_name,
                            "unsupported");
                    }
                }
                index = close;
                --bracket_depth;
                continue;
            }
            if (character == '(') {
                ++parenthesis_depth;
                continue;
            }
            if (character == ')') {
                if (parenthesis_depth > 0) --parenthesis_depth;
                continue;
            }
            if (bracket_depth != 0) continue;
            if (character == ':') {
                const auto pseudo_element_marker = index + 1U < selector.size()
                    && selector[index + 1U] == ':';
                if (pseudo_element_marker) ++index;
                const auto name_start = index + 1U;
                auto name_end = name_start;
                while (name_end < selector.size()
                    && (std::isalnum(static_cast<unsigned char>(selector[name_end]))
                        || selector[name_end] == '-')) ++name_end;
                auto name = lower_html_name(std::string(
                    selector.substr(name_start, name_end - name_start)));
                const auto pseudo_element = pseudo_element_marker
                    || name == "before" || name == "after"
                    || name == "first-letter" || name == "first-line";
                if (pseudo_element) {
                    record_selector_feature(
                        "selector:pseudo-element:" + name,
                        name == "before" || name == "after" ? "supported" : "unsupported");
                } else {
                    static const std::unordered_set<std::string> supported_pseudo_classes{
                        "root", "first-child", "last-child", "only-child",
                        "first-of-type", "last-of-type", "only-of-type", "empty",
                        "enabled", "disabled", "checked", "hover", "focus", "focus-visible"};
                    if (name == "not" || name == "is" || name == "where") {
                        record_selector_feature(
                            "selector:pseudo-class:" + name,
                            "partially-supported",
                            "comma-separated compound-selector arguments");
                    } else if (name == "nth-child") {
                        record_selector_feature(
                            "selector:pseudo-class:nth-child",
                            "partially-supported",
                            "integer, odd, even, and An+B forms without an of-selector");
                    } else {
                        record_selector_feature(
                            "selector:pseudo-class:" + name,
                            supported_pseudo_classes.contains(name) ? "supported" : "unsupported");
                    }
                }
                index = name_end == 0U ? index : name_end - 1U;
                continue;
            }
            if (parenthesis_depth != 0) continue;
            if (character == '>') {
                record_selector_feature("selector:combinator:child");
            } else if (character == '+') {
                record_selector_feature("selector:combinator:adjacent-sibling");
            } else if (character == '~') {
                record_selector_feature("selector:combinator:general-sibling");
            } else if (character == '#') {
                saw_id = true;
            } else if (character == '.') {
                saw_class = true;
            } else if (character == '*') {
                saw_universal = true;
            } else if (character == '|' || character == '\\') {
                record_selector_feature(
                    character == '|' ? "selector:namespace" : "selector:escaped-identifier",
                    "unsupported");
            } else if (std::isalpha(static_cast<unsigned char>(character))) {
                const auto previous = index == 0U ? ' ' : selector[index - 1U];
                if (index == 0U || std::isspace(static_cast<unsigned char>(previous))
                    || previous == '>' || previous == '+' || previous == '~' || previous == ',') {
                    saw_type = true;
                }
            } else if (std::isspace(static_cast<unsigned char>(character))) {
                auto next = index + 1U;
                while (next < selector.size()
                    && std::isspace(static_cast<unsigned char>(selector[next]))) ++next;
                auto previous = index;
                while (previous > 0U
                    && std::isspace(static_cast<unsigned char>(selector[previous - 1U]))) --previous;
                if (previous > 0U && next < selector.size()
                    && selector[previous - 1U] != '>' && selector[previous - 1U] != '+'
                    && selector[previous - 1U] != '~' && selector[next] != '>'
                    && selector[next] != '+' && selector[next] != '~') {
                    record_selector_feature("selector:combinator:descendant");
                }
            }
        }
        if (saw_type) record_selector_feature("selector:type");
        if (saw_class) record_selector_feature("selector:class");
        if (saw_id) record_selector_feature("selector:id");
        if (saw_universal) record_selector_feature("selector:universal");
    }

    bool inventory_media_query(std::string query)
    {
        std::transform(query.begin(), query.end(), query.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        auto supported = true;
        size_t alternative_start = 0U;
        while (alternative_start <= query.size()) {
            auto alternative_end = query.find(',', alternative_start);
            if (alternative_end == std::string::npos) alternative_end = query.size();
            auto alternative = trim_css(std::string_view(query).substr(
                alternative_start, alternative_end - alternative_start));
            if (alternative.starts_with("not ")) {
                alternative = trim_css(std::string_view(alternative).substr(4U));
            }
            const auto first_condition = alternative.find('(');
            auto media_type = trim_css(std::string_view(alternative).substr(0U, first_condition));
            if (media_type.ends_with(" and")) media_type.resize(media_type.size() - 4U);
            media_type = trim_css(media_type);
            if (!media_type.empty()) {
                const auto known = media_type == "all" || media_type == "screen" || media_type == "print";
                record_feature(
                    "css",
                    "media-type:" + media_type,
                    known ? "supported" : "unsupported",
                    {},
                    "media-query-parser");
                supported = supported && known;
            }
            size_t condition_start = 0U;
            while ((condition_start = alternative.find('(', condition_start))
                != std::string::npos) {
                const auto condition_end = alternative.find(')', condition_start + 1U);
                if (condition_end == std::string::npos) {
                    supported = false;
                    break;
                }
                const auto condition = trim_css(std::string_view(alternative).substr(
                    condition_start + 1U, condition_end - condition_start - 1U));
                const auto separator = condition.find(':');
                const auto feature = trim_css(std::string_view(condition).substr(0U, separator));
                static const std::unordered_set<std::string> supported_features{
                    "max-width", "min-width", "max-height", "min-height", "orientation",
                    "hover", "any-hover", "pointer", "any-pointer",
                    "prefers-reduced-motion", "prefers-color-scheme"};
                const auto known = separator != std::string::npos
                    && supported_features.contains(feature);
                record_feature(
                    "css",
                    "media-feature:" + (feature.empty() ? std::string("<missing>") : feature),
                    known ? "supported" : "unsupported",
                    {},
                    "media-query-parser");
                supported = supported && known;
                condition_start = condition_end + 1U;
            }
            if (alternative_end == query.size()) break;
            alternative_start = alternative_end + 1U;
        }
        return supported;
    }

    static uint32_t css_selector_specificity(std::string_view selector)
    {
        // Pack the CSS (ID, class/attribute/pseudo-class, type/pseudo-element)
        // tuple so ordinary integer ordering retains the cascade's lexicographic order.
        uint32_t ids = 0;
        uint32_t classes = 0;
        uint32_t elements = 0;
        bool compound_start = true;
        for (size_t index = 0; index < selector.size();) {
            const auto character = selector[index];
            if (std::isspace(static_cast<unsigned char>(character))
                || character == '>' || character == '+' || character == '~') {
                compound_start = true;
                ++index;
                continue;
            }
            if (character == '#') {
                ++ids;
                ++index;
                while (index < selector.size()
                    && (std::isalnum(static_cast<unsigned char>(selector[index]))
                        || selector[index] == '-' || selector[index] == '_')) ++index;
                compound_start = false;
                continue;
            }
            if (character == '.') {
                ++classes;
                ++index;
                while (index < selector.size()
                    && (std::isalnum(static_cast<unsigned char>(selector[index]))
                        || selector[index] == '-' || selector[index] == '_')) ++index;
                compound_start = false;
                continue;
            }
            if (character == '[') {
                ++classes;
                char quote = 0;
                for (++index; index < selector.size(); ++index) {
                    if (quote != 0) {
                        if (selector[index] == quote) quote = 0;
                    } else if (selector[index] == '\'' || selector[index] == '"') {
                        quote = selector[index];
                    } else if (selector[index] == ']') {
                        ++index;
                        break;
                    }
                }
                compound_start = false;
                continue;
            }
            if (character == ':') {
                auto pseudo_element = index + 1U < selector.size()
                    && selector[index + 1U] == ':';
                if (pseudo_element) {
                    index += 2U;
                } else {
                    ++index;
                }
                const auto name_start = index;
                while (index < selector.size()
                    && (std::isalnum(static_cast<unsigned char>(selector[index]))
                        || selector[index] == '-')) ++index;
                std::string name(selector.substr(name_start, index - name_start));
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char value) {
                    return static_cast<char>(std::tolower(value));
                });
                pseudo_element = pseudo_element || name == "before" || name == "after";
                if (pseudo_element) ++elements;
                else if (name != "where") ++classes;
                if (index < selector.size() && selector[index] == '(') {
                    int depth = 1;
                    for (++index; index < selector.size() && depth > 0; ++index) {
                        if (selector[index] == '(') ++depth;
                        else if (selector[index] == ')') --depth;
                    }
                }
                compound_start = false;
                continue;
            }
            if (compound_start
                && std::isalpha(static_cast<unsigned char>(character))) {
                ++elements;
                while (index < selector.size()
                    && (std::isalnum(static_cast<unsigned char>(selector[index]))
                        || selector[index] == '-')) ++index;
                compound_start = false;
                continue;
            }
            compound_start = false;
            ++index;
        }
        return std::min(ids, 0xffU) << 16U
            | std::min(classes, 0xffU) << 8U
            | std::min(elements, 0xffU);
    }

    void sort_css_candidates(std::vector<size_t>& candidates) const
    {
        // Declarations are applied in ascending precedence; later declarations
        // therefore replace earlier ones while equal specificity retains source order.
        std::sort(
            candidates.begin(),
            candidates.end(),
            [this](size_t left, size_t right) {
                const auto left_specificity = css_rules[left].specificity();
                const auto right_specificity = css_rules[right].specificity();
                return left_specificity != right_specificity
                    ? left_specificity < right_specificity
                    : left < right;
            });
    }

    bool refresh_css_media_matches()
    {
        auto changed = false;
        for (auto& rule : css_rules) {
            const auto matches = css_rule_media_matches(rule);
            changed = changed || matches != rule.media_matches;
            rule.media_matches = matches;
        }
        return changed;
    }

    bool refresh_media_environment()
    {
        auto isolate_locker = lock_shared_isolate();
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        auto changed = false;
        const auto refresh_context = [&](v8::Local<v8::Context> local_context) {
            if (local_context.IsEmpty()) return;
            activate_css_cascade(local_context);
            v8::Context::Scope context_scope(local_context);
            if (!refresh_css_media_matches()) return;
            changed = true;
            rebuild_css_rule_indexes_and_root_variables();
            apply_css_rules_subtree(active_root());
        };
        refresh_context(context.Get(isolate));
        for (auto& [id, persistent_context] : actual_frame_contexts) {
            static_cast<void>(id);
            if (!persistent_context.IsEmpty()) {
                refresh_context(persistent_context.Get(isolate));
            }
        }
        activate_css_cascade(context.Get(isolate));
        if (changed) document.mark_dirty();
        return true;
    }

    static std::string resolve_css_resource_urls(
        std::string value,
        const std::string& stylesheet_address)
    {
        if (value.empty() || stylesheet_address.empty()) return value;

        auto lowercase = lower_html_name(value);
        size_t search = 0U;
        while ((search = lowercase.find("url(", search)) != std::string::npos) {
            const auto argument_begin = search + 4U;
            auto argument_end = argument_begin;
            char quote = 0;
            for (; argument_end < value.size(); ++argument_end) {
                const auto character = value[argument_end];
                if (quote != 0) {
                    if (character == quote
                        && (argument_end == argument_begin
                            || value[argument_end - 1U] != '\\')) {
                        quote = 0;
                    }
                    continue;
                }
                if (character == '\'' || character == '"') {
                    quote = character;
                } else if (character == ')') {
                    break;
                }
            }
            if (argument_end >= value.size()) break;

            auto address = trim_css(std::string_view(value).substr(
                argument_begin,
                argument_end - argument_begin));
            if (address.size() >= 2U
                && ((address.front() == '\'' && address.back() == '\'')
                    || (address.front() == '"' && address.back() == '"'))) {
                address = address.substr(1U, address.size() - 2U);
            }
            if (address.empty() || address.starts_with('#')) {
                search = argument_end + 1U;
                continue;
            }

            auto resolved = resolve_resource_url(address, stylesheet_address);
            std::string escaped;
            escaped.reserve(resolved.size());
            for (const auto character : resolved) {
                if (character == '\\' || character == '"') escaped.push_back('\\');
                escaped.push_back(character);
            }
            const auto replacement = "url(\"" + escaped + "\")";
            value.replace(search, argument_end - search + 1U, replacement);
            lowercase = lower_html_name(value);
            search += replacement.size();
        }
        return value;
    }

    void parse_opacity_keyframes(
        const std::string& css,
        std::string name,
        size_t begin,
        size_t end)
    {
        css_opacity_keyframes definition;
        size_t cursor = begin;
        while (cursor < end) {
            const auto open = css.find('{', cursor);
            if (open == std::string::npos || open >= end) break;
            const auto close = matching_css_brace(css, open, end);
            if (close == std::string::npos) break;
            const auto selector = trim_css(
                std::string_view(css).substr(cursor, open - cursor));
            const auto declarations = parse_css_declarations(
                std::string_view(css).substr(open + 1U, close - open - 1U));
            const auto opacity = std::find_if(
                declarations.begin(), declarations.end(), [](const auto& declaration) {
                    return declaration.name == "opacity";
                });
            const auto transform = std::find_if(
                declarations.begin(), declarations.end(), [](const auto& declaration) {
                    return declaration.name == "transform";
                });
            const auto rotation_degrees = [&]() -> std::optional<float> {
                if (transform == declarations.end()) return std::nullopt;
                auto value = lower_html_name(trim_css(transform->value));
                const auto rotate = value.find("rotate(");
                if (rotate == std::string::npos) return std::nullopt;
                const auto close = value.find(')', rotate + 7U);
                if (close == std::string::npos) return std::nullopt;
                auto angle = trim_css(value.substr(rotate + 7U, close - rotate - 7U));
                auto multiplier = 1.0F;
                if (angle.ends_with("turn")) {
                    angle.resize(angle.size() - 4U);
                    multiplier = 360.0F;
                } else if (angle.ends_with("deg")) {
                    angle.resize(angle.size() - 3U);
                } else if (angle.ends_with("rad")) {
                    angle.resize(angle.size() - 3U);
                    multiplier = 57.29577951308232F;
                } else return std::nullopt;
                return std::strtof(angle.c_str(), nullptr) * multiplier;
            }();
            for (auto component : split_css_component_list(selector, ',')) {
                component = lower_html_name(trim_css(std::move(component)));
                float offset = -1;
                if (component == "from") offset = 0;
                else if (component == "to") offset = 1;
                else if (component.ends_with('%')) {
                    component.pop_back();
                    offset = std::strtof(component.c_str(), nullptr) / 100.0F;
                }
                if (offset < 0 || offset > 1) continue;
                if (opacity != declarations.end()) {
                    definition.opacity_stops.push_back({
                        offset,
                        std::clamp(std::strtof(opacity->value.c_str(), nullptr), 0.0F, 1.0F)});
                }
                if (rotation_degrees.has_value()) {
                    definition.rotation_stops.push_back({offset, *rotation_degrees});
                }
            }
            cursor = close + 1U;
        }
        const auto normalize = [](auto& stops) {
            std::stable_sort(
                stops.begin(), stops.end(),
                [](const auto& left, const auto& right) { return left.offset < right.offset; });
            using stop_type = typename std::decay_t<decltype(stops)>::value_type;
            std::vector<stop_type> unique;
            for (const auto& stop : stops) {
                if (!unique.empty()
                    && std::abs(unique.back().offset - stop.offset) < 0.0001F) {
                    unique.back() = stop;
                } else {
                    unique.push_back(stop);
                }
            }
            stops = std::move(unique);
        };
        normalize(definition.opacity_stops);
        normalize(definition.rotation_stops);
        if (definition.rotation_stops.size() == 1U
            && definition.rotation_stops.front().offset > 0) {
            definition.rotation_stops.insert(
                definition.rotation_stops.begin(), {0, 0});
        }
        if (definition.opacity_stops.size() >= 2U
            || definition.rotation_stops.size() >= 2U) {
            opacity_keyframes[lower_html_name(trim_css(std::move(name)))] =
                std::move(definition);
        }
    }

    void append_parsed_css_style_rule(
        std::string prelude,
        std::vector<css_declaration> declarations,
        const std::vector<std::string>& inherited_media,
        const std::string& stylesheet_address)
    {
        for (auto& declaration : declarations) {
            declaration.value = resolve_css_resource_urls(
                std::move(declaration.value),
                stylesheet_address);
            inventory_css_value_functions(declaration.value);
            if (declaration.name == "background-image") {
                if (const auto url = first_css_url(declaration.value); url.has_value()) {
                    start_resource_prefetch(*url, {}, WEBSCENE_RESOURCE_IMAGE);
                }
            }
        }
        // The shared selector parser intentionally implements document-tree
        // selectors and rejects Shadow DOM-only pseudo classes.  Preserve the
        // production milestone's exact `:host` form here; cascade scope checks
        // match it only against the owning host, so it never enters ordinary
        // selector matching or leaks into the document tree.
        if (trim_css_view(prelude) == ":host") {
            inventory_css_selector(":host");
            append_css_rule(":host", declarations, inherited_media);
            return;
        }
#if defined(WEBSCENE_NATIVE_ENGINE_SERVO_SELECTORS)
        const auto parsed_selectors = parse_selector_syntax(prelude);
        if (!parsed_selectors) return;
        for (const auto& parsed_selector : parsed_selectors.selectors) {
            inventory_css_selector(parsed_selector.serialized);
            append_css_rule(
                parsed_selector.serialized,
                declarations,
                inherited_media,
                &parsed_selector);
        }
#else
        size_t selector_cursor = 0U;
        int bracket_depth = 0;
        int parenthesis_depth = 0;
        char quote = 0;
        for (size_t index = 0U; index <= prelude.size(); ++index) {
            const auto at_end = index == prelude.size();
            const auto character = at_end ? ',' : prelude[index];
            if (!at_end && quote != 0) {
                if (character == quote
                    && (index == 0U || prelude[index - 1U] != '\\')) quote = 0;
                continue;
            }
            if (!at_end && (character == '\'' || character == '"')) {
                quote = character;
                continue;
            }
            if (!at_end && character == '[') ++bracket_depth;
            else if (!at_end && character == ']') --bracket_depth;
            else if (!at_end && character == '(') ++parenthesis_depth;
            else if (!at_end && character == ')') --parenthesis_depth;
            else if (character == ',' && bracket_depth == 0 && parenthesis_depth == 0) {
                auto selector = trim_css(std::string_view(prelude).substr(
                    selector_cursor, index - selector_cursor));
                if (!selector.empty()) {
                    inventory_css_selector(selector);
                    append_css_rule(std::move(selector), declarations, inherited_media);
                }
                selector_cursor = index + 1U;
            }
        }
#endif
    }

    void parse_css_rules(
        const std::string& css,
        size_t begin,
        size_t end,
        const std::vector<std::string>& inherited_media = {},
        const std::string& stylesheet_address = {})
    {
        size_t cursor = begin;
        while (cursor < end) {
            const auto open = css.find('{', cursor);
            if (open == std::string::npos || open >= end) break;
            const auto close = matching_css_brace(css, open, end);
            if (close == std::string::npos) break;
            const auto prelude = trim_css(std::string_view(css).substr(cursor, open - cursor));
            if (prelude.starts_with("@keyframes")
                || prelude.starts_with("@-webkit-keyframes")) {
                const auto prefix = prelude.starts_with("@keyframes")
                    ? std::string_view("@keyframes")
                    : std::string_view("@-webkit-keyframes");
                auto name = trim_css(std::string_view(prelude).substr(prefix.size()));
                parse_opacity_keyframes(css, name, open + 1U, close);
                record_feature(
                    "css",
                    "at-rule:@keyframes",
                    "partially-supported",
                    "opacity and rotate() keyframes with host-clock timing",
                    "stylesheet-parser");
            } else if (prelude.starts_with("@font-face")) {
                record_feature(
                    "css",
                    "at-rule:@font-face",
                    "supported",
                    "font-family and first src url are registered by the native host",
                    "stylesheet-parser");
            } else if (prelude.starts_with("@media")) {
                auto nested_media = inherited_media;
                auto query = trim_css(std::string_view(prelude).substr(6U));
                const auto supported = inventory_media_query(query);
                record_feature(
                    "css",
                    "at-rule:@media",
                    supported ? "supported" : "unsupported",
                    supported ? std::string{} : "unsupported media type or condition",
                    "stylesheet-parser");
                nested_media.push_back(std::move(query));
                parse_css_rules(css, open + 1U, close, nested_media, stylesheet_address);
            } else if (prelude.starts_with("@supports")) {
                auto condition = trim_css(std::string_view(prelude).substr(9U));
                auto negated = false;
                if (condition.starts_with("not ")) {
                    negated = true;
                    condition = trim_css(std::string_view(condition).substr(4U));
                }
                // This bounded runtime only advertises selector() forms whose
                // state is implemented by its selector engine. Unknown support
                // conditions remain false instead of accidentally enabling a
                // fallback block.
                const auto supported = condition == "selector(:focus-visible)";
                record_feature(
                    "css",
                    "at-rule:@supports",
                    supported ? "supported" : "unsupported",
                    supported ? "selector(:focus-visible)" : "condition is not evaluated",
                    "stylesheet-parser");
                if (supported != negated) {
                    parse_css_rules(css, open + 1U, close, inherited_media, stylesheet_address);
                }
            } else if (prelude.starts_with("@layer")) {
                record_feature(
                    "css",
                    "at-rule:@layer",
                    "partially-supported",
                    "nested rules are parsed without cascade-layer ordering",
                    "stylesheet-parser");
                parse_css_rules(css, open + 1U, close, inherited_media, stylesheet_address);
            } else if (prelude.starts_with("@container")) {
                record_feature(
                    "css",
                    "at-rule:@container",
                    "unsupported",
                    "container conditions are not evaluated",
                    "stylesheet-parser");
                parse_css_rules(css, open + 1U, close, inherited_media, stylesheet_address);
            } else if (!prelude.empty() && prelude.front() != '@') {
                auto declarations = parse_css_declarations(
                    std::string_view(css).substr(open + 1U, close - open - 1U));
                append_parsed_css_style_rule(
                    prelude,
                    std::move(declarations),
                    inherited_media,
                    stylesheet_address);
            } else if (!prelude.empty() && prelude.front() == '@') {
                auto name_end = prelude.find_first_of(" (\t\r\n");
                if (name_end == std::string::npos) name_end = prelude.size();
                record_feature(
                    "css",
                    "at-rule:" + lower_html_name(prelude.substr(0U, name_end)),
                    "unsupported",
                    {},
                    "stylesheet-parser");
            }
            cursor = close + 1U;
        }
    }

#if defined(WEBSCENE_NATIVE_ENGINE_CSSPARSER)
    void append_css_syntax_keyframe(
        css_opacity_keyframes& definition,
        std::string selector,
        const std::vector<css_declaration>& declarations)
    {
        const auto opacity = std::find_if(
            declarations.begin(), declarations.end(), [](const auto& declaration) {
                return declaration.name == "opacity";
            });
        const auto transform = std::find_if(
            declarations.begin(), declarations.end(), [](const auto& declaration) {
                return declaration.name == "transform";
            });
        const auto rotation_degrees = [&]() -> std::optional<float> {
            if (transform == declarations.end()) return std::nullopt;
            auto value = lower_html_name(trim_css(transform->value));
            const auto rotate = value.find("rotate(");
            if (rotate == std::string::npos) return std::nullopt;
            const auto close = value.find(')', rotate + 7U);
            if (close == std::string::npos) return std::nullopt;
            auto angle = trim_css(value.substr(rotate + 7U, close - rotate - 7U));
            auto multiplier = 1.0F;
            if (angle.ends_with("turn")) {
                angle.resize(angle.size() - 4U);
                multiplier = 360.0F;
            } else if (angle.ends_with("deg")) {
                angle.resize(angle.size() - 3U);
            } else if (angle.ends_with("rad")) {
                angle.resize(angle.size() - 3U);
                multiplier = 57.29577951308232F;
            } else return std::nullopt;
            return std::strtof(angle.c_str(), nullptr) * multiplier;
        }();
        for (auto component : split_css_component_list(selector, ',')) {
            component = lower_html_name(trim_css(std::move(component)));
            float offset = -1;
            if (component == "from") offset = 0;
            else if (component == "to") offset = 1;
            else if (component.ends_with('%')) {
                component.pop_back();
                offset = std::strtof(component.c_str(), nullptr) / 100.0F;
            }
            if (offset < 0 || offset > 1) continue;
            if (opacity != declarations.end()) {
                definition.opacity_stops.push_back({
                    offset,
                    std::clamp(std::strtof(opacity->value.c_str(), nullptr), 0.0F, 1.0F)});
            }
            if (rotation_degrees.has_value()) {
                definition.rotation_stops.push_back({offset, *rotation_degrees});
            }
        }
    }

    void finish_css_syntax_keyframes(
        std::string name,
        css_opacity_keyframes definition)
    {
        const auto normalize = [](auto& stops) {
            std::stable_sort(
                stops.begin(), stops.end(),
                [](const auto& left, const auto& right) { return left.offset < right.offset; });
            using stop_type = typename std::decay_t<decltype(stops)>::value_type;
            std::vector<stop_type> unique;
            for (const auto& stop : stops) {
                if (!unique.empty()
                    && std::abs(unique.back().offset - stop.offset) < 0.0001F) {
                    unique.back() = stop;
                } else {
                    unique.push_back(stop);
                }
            }
            stops = std::move(unique);
        };
        normalize(definition.opacity_stops);
        normalize(definition.rotation_stops);
        if (definition.rotation_stops.size() == 1U
            && definition.rotation_stops.front().offset > 0) {
            definition.rotation_stops.insert(definition.rotation_stops.begin(), {0, 0});
        }
        if (definition.opacity_stops.size() >= 2U
            || definition.rotation_stops.size() >= 2U) {
            opacity_keyframes[lower_html_name(trim_css(std::move(name)))] =
                std::move(definition);
        }
    }

    void parse_css_rules_with_cssparser(
        std::string_view css,
        const std::string& stylesheet_address)
    {
        class stylesheet_sink final : public css_syntax_sink {
        public:
            stylesheet_sink(
                implementation& owner,
                const std::string& stylesheet_address,
                size_t input_size)
                : owner_(owner), stylesheet_address_(stylesheet_address)
            {
                stack_.reserve(8U);
                completed_keyframes_.reserve(input_size / 4096U);
            }

            bool begin_rule(
                uint32_t kind,
                bool,
                size_t parent_index,
                std::string_view raw_name,
                std::string_view prelude,
                size_t& rule_index) override
            {
                const auto expected_parent = stack_.empty()
                    ? css_syntax_no_parent
                    : stack_.back().rule_index;
                if (parent_index != expected_parent) return false;

                rule_index = next_rule_index_++;
                frame current;
                current.rule_index = rule_index;
                current.kind = kind;
                current.active = stack_.empty() || stack_.back().children_active;
                current.children_active = current.active;

                if (!stack_.empty() && stack_.back().keyframes
                    && kind != css_syntax_style_rule) {
                    current.active = false;
                    current.children_active = false;
                }

                if (kind == css_syntax_style_rule) {
                    current.prelude = std::string(prelude);
                    current.children_active = false;
                    // Two declarations is the common generated-rule shape and
                    // avoids the former IR's exact-size allocation without
                    // over-reserving every small rule.
                    current.declarations.reserve(2U);
                    stack_.push_back(std::move(current));
                    return true;
                }

                if (!current.active) {
                    current.children_active = false;
                    stack_.push_back(std::move(current));
                    return true;
                }

                const auto name = copy_ascii_lower(raw_name);
                if (name == "keyframes" || name == "-webkit-keyframes") {
                    current.keyframes = true;
                    current.prelude = std::string(prelude);
                } else if (name == "font-face") {
                    current.children_active = false;
                    owner_.record_feature(
                        "css", "at-rule:@font-face", "supported",
                        "font-family and first src url are registered by the native host",
                        "stylesheet-parser");
                } else if (name == "media") {
                    current.media_query = trim_css(prelude);
                    const auto supported = owner_.inventory_media_query(current.media_query);
                    owner_.record_feature(
                        "css", "at-rule:@media",
                        supported ? "supported" : "unsupported",
                        supported ? std::string{} : "unsupported media type or condition",
                        "stylesheet-parser");
                } else if (name == "supports") {
                    auto condition = lower_html_name(trim_css(prelude));
                    auto negated = false;
                    if (condition.starts_with("not ")) {
                        negated = true;
                        condition = trim_css(std::string_view(condition).substr(4U));
                    }
                    const auto supported = condition == "selector(:focus-visible)";
                    owner_.record_feature(
                        "css", "at-rule:@supports",
                        supported ? "supported" : "unsupported",
                        supported ? "selector(:focus-visible)" : "condition is not evaluated",
                        "stylesheet-parser");
                    current.children_active = supported != negated;
                } else if (name == "layer") {
                    owner_.record_feature(
                        "css", "at-rule:@layer", "partially-supported",
                        "nested rules are parsed without cascade-layer ordering",
                        "stylesheet-parser");
                } else if (name == "container") {
                    owner_.record_feature(
                        "css", "at-rule:@container", "unsupported",
                        "container conditions are not evaluated",
                        "stylesheet-parser");
                } else {
                    owner_.record_feature(
                        "css", "at-rule:@" + name, "unsupported", {},
                        "stylesheet-parser");
                    current.children_active = false;
                }
                stack_.push_back(std::move(current));
                return true;
            }

            bool declaration(
                std::string_view name,
                std::string_view value,
                bool important) override
            {
                if (stack_.empty()) return false;
                auto& current = stack_.back();
                ++current.observed_declarations;
                if (current.active && current.kind == css_syntax_style_rule) {
                    append_css_syntax_declaration(
                        current.declarations, name, value, important);
                }
                return true;
            }

            bool end_rule(size_t rule_index, size_t declaration_count) override
            {
                if (stack_.empty() || stack_.back().rule_index != rule_index
                    || stack_.back().observed_declarations != declaration_count) {
                    return false;
                }
                auto current = std::move(stack_.back());
                stack_.pop_back();
                if (!current.active) return true;

                if (current.kind == css_syntax_style_rule) {
                    if (!stack_.empty() && stack_.back().keyframes) {
                        owner_.append_css_syntax_keyframe(
                            stack_.back().keyframe_definition,
                            std::move(current.prelude),
                            current.declarations);
                        return true;
                    }
                    std::vector<std::string> inherited_media;
                    inherited_media.reserve(stack_.size());
                    for (const auto& ancestor : stack_) {
                        if (!ancestor.media_query.empty()) {
                            inherited_media.push_back(ancestor.media_query);
                        }
                    }
                    owner_.append_parsed_css_style_rule(
                        std::move(current.prelude),
                        std::move(current.declarations),
                        inherited_media,
                        stylesheet_address_);
                } else if (current.keyframes) {
                    completed_keyframes_.emplace_back(
                        std::move(current.prelude),
                        std::move(current.keyframe_definition));
                }
                return true;
            }

            bool complete() const noexcept { return stack_.empty(); }

            std::vector<std::pair<std::string, css_opacity_keyframes>>& keyframes()
            {
                return completed_keyframes_;
            }

        private:
            struct frame final {
                size_t rule_index{css_syntax_no_parent};
                uint32_t kind{css_syntax_style_rule};
                std::string prelude;
                std::string media_query;
                std::vector<css_declaration> declarations;
                css_opacity_keyframes keyframe_definition;
                size_t observed_declarations{0U};
                bool active{false};
                bool children_active{false};
                bool keyframes{false};
            };

            implementation& owner_;
            const std::string& stylesheet_address_;
            std::vector<frame> stack_;
            std::vector<std::pair<std::string, css_opacity_keyframes>> completed_keyframes_;
            size_t next_rule_index_{0U};
        };

        const auto first_new_rule = css_rules.size();
        stylesheet_sink sink(*this, stylesheet_address, css.size());
        const auto parsed = stream_css_syntax_stylesheet(css, sink);
        if (!parsed || !sink.complete()) {
            css_rules.resize(first_new_rule);
            return;
        }
        for (auto& [name, definition] : sink.keyframes()) {
            finish_css_syntax_keyframes(std::move(name), std::move(definition));
            record_feature(
                "css", "at-rule:@keyframes", "partially-supported",
                "opacity and rotate() keyframes with host-clock timing",
                "stylesheet-parser");
        }
    }
#endif

    size_t add_stylesheet(
        std::string css,
        const std::string& stylesheet_address = {},
        uint32_t stylesheet_owner_id = 0,
        uint32_t shadow_scope_root_id = 0)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        binding_callback_timer timer(profile_startup ? &startup_css_parse : nullptr);
#endif
        const auto first_new_rule = css_rules.size();
#if defined(WEBSCENE_NATIVE_ENGINE_CSSPARSER)
        const auto previous_owner_id = active_stylesheet_owner_id;
        const auto previous_shadow_scope_root_id =
            active_stylesheet_shadow_scope_root_id;
        active_stylesheet_owner_id = stylesheet_owner_id;
        active_stylesheet_shadow_scope_root_id = shadow_scope_root_id;
        parse_css_rules_with_cssparser(css, stylesheet_address);
        active_stylesheet_owner_id = previous_owner_id;
        active_stylesheet_shadow_scope_root_id = previous_shadow_scope_root_id;
#else
        size_t comment = 0;
        while ((comment = css.find("/*", comment)) != std::string::npos) {
            const auto end = css.find("*/", comment + 2U);
            if (end == std::string::npos) {
                css.erase(comment);
                break;
            }
            css.erase(comment, end - comment + 2U);
        }
        const auto previous_owner_id = active_stylesheet_owner_id;
        const auto previous_shadow_scope_root_id =
            active_stylesheet_shadow_scope_root_id;
        active_stylesheet_owner_id = stylesheet_owner_id;
        active_stylesheet_shadow_scope_root_id = shadow_scope_root_id;
        parse_css_rules(css, 0, css.size(), {}, stylesheet_address);
        active_stylesheet_owner_id = previous_owner_id;
        active_stylesheet_shadow_scope_root_id = previous_shadow_scope_root_id;
#endif
        for (auto index = first_new_rule; index < css_rules.size(); ++index) {
            const auto& rule = css_rules[index];
            if (!rule.media_matches || rule.shadow_scope_root_id != 0U) continue;
            if (rule.selector() != ":root" && rule.selector() != "html") continue;
            for (const auto& declaration : rule.declarations()) {
                if (!declaration.name.starts_with("--")) continue;
                if (!declaration.important
                    && important_css_variables.contains(declaration.name)) continue;
                css_variables[declaration.name] = declaration.value;
                if (declaration.important) important_css_variables.insert(declaration.name);
                else important_css_variables.erase(declaration.name);
            }
        }
        return first_new_rule;
    }

    std::string resolve_css_value(const dom_node& node, std::string value) const
    {
        // Component-library dimensions frequently expand through several
        // custom-property layers and then repeat those variables across five
        // size terms. A fixed substitution limit truncated large component stylesheets'
        // button height before it reached a numeric calc() expression.
        for (int depth = 0; depth < 128; ++depth) {
            const auto start = value.find("var(");
            if (start == std::string::npos) break;
            size_t close = std::string::npos;
            int parenthesis_depth = 1;
            for (size_t index = start + 4U; index < value.size(); ++index) {
                if (value[index] == '(') ++parenthesis_depth;
                else if (value[index] == ')' && --parenthesis_depth == 0) {
                    close = index;
                    break;
                }
            }
            if (close == std::string::npos) break;
            const auto content = value.substr(start + 4U, close - start - 4U);
            size_t comma = std::string::npos;
            parenthesis_depth = 0;
            for (size_t index = 0; index < content.size(); ++index) {
                if (content[index] == '(') ++parenthesis_depth;
                else if (content[index] == ')') --parenthesis_depth;
                else if (content[index] == ',' && parenthesis_depth == 0) {
                    comma = index;
                    break;
                }
            }
            const auto name = trim_css(std::string_view(content).substr(0, comma));
            const std::string* known_value = nullptr;
            for (auto* current = &node; current != nullptr; current = current->parent) {
                const auto& custom = current->style.custom_properties().values;
                const auto known = custom.find(name);
                if (known != custom.end()) {
                    known_value = &known->second;
                    break;
                }
            }
            if (known_value == nullptr) {
                const auto known = css_variables.find(name);
                if (known != css_variables.end()) known_value = &known->second;
            }
            const auto replacement = known_value != nullptr
                ? *known_value
                : comma == std::string::npos ? std::string{} : trim_css(std::string_view(content).substr(comma + 1U));
            value.replace(start, close - start + 1U, replacement);
        }
        return trim_css(value);
    }

    static bool is_css_hex_digit(unsigned char character)
    {
        return (character >= '0' && character <= '9')
            || (character >= 'a' && character <= 'f')
            || (character >= 'A' && character <= 'F');
    }

    static size_t skip_css_escape_sequence(std::string_view text, size_t slash)
    {
        auto cursor = slash + 1U;
        if (cursor >= text.size()) return cursor;
        if (text[cursor] == '\r') {
            ++cursor;
            if (cursor < text.size() && text[cursor] == '\n') ++cursor;
            return cursor;
        }
        if (text[cursor] == '\n' || text[cursor] == '\f') return cursor + 1U;
        const auto hex_start = cursor;
        while (cursor < text.size()
            && cursor - hex_start < 6U
            && is_css_hex_digit(static_cast<unsigned char>(text[cursor]))) ++cursor;
        if (cursor > hex_start) {
            if (cursor < text.size() && text[cursor] == '\r') {
                ++cursor;
                if (cursor < text.size() && text[cursor] == '\n') ++cursor;
            } else if (cursor < text.size()
                && std::isspace(static_cast<unsigned char>(text[cursor]))) ++cursor;
        } else {
            ++cursor;
        }
        return cursor;
    }

    static std::string read_css_identifier(std::string_view text, size_t& cursor)
    {
        std::string result;
        while (cursor < text.size()) {
            const auto character = static_cast<unsigned char>(text[cursor]);
            if (character == '\\') {
                ++cursor;
                if (cursor >= text.size()) {
                    append_utf8_codepoint(result, 0xfffdU);
                    break;
                }
                if (text[cursor] == '\r') {
                    ++cursor;
                    if (cursor < text.size() && text[cursor] == '\n') ++cursor;
                    continue;
                }
                if (text[cursor] == '\n' || text[cursor] == '\f') {
                    ++cursor;
                    continue;
                }
                const auto hex_start = cursor;
                uint32_t codepoint = 0U;
                while (cursor < text.size()
                    && cursor - hex_start < 6U
                    && is_css_hex_digit(static_cast<unsigned char>(text[cursor]))) {
                    const auto digit = static_cast<unsigned char>(text[cursor++]);
                    codepoint = codepoint * 16U
                        + (digit >= '0' && digit <= '9'
                            ? digit - '0'
                            : static_cast<unsigned char>(std::tolower(digit)) - 'a' + 10U);
                }
                if (cursor > hex_start) {
                    if (cursor < text.size() && text[cursor] == '\r') {
                        ++cursor;
                        if (cursor < text.size() && text[cursor] == '\n') ++cursor;
                    } else if (cursor < text.size()
                        && std::isspace(static_cast<unsigned char>(text[cursor]))) ++cursor;
                    append_utf8_codepoint(
                        result,
                        codepoint == 0U || codepoint > 0x10ffffU
                            || (codepoint >= 0xd800U && codepoint <= 0xdfffU)
                            ? 0xfffdU
                            : codepoint);
                } else {
                    result.push_back(text[cursor++]);
                }
                continue;
            }
            if (character == 0U) {
                append_utf8_codepoint(result, 0xfffdU);
                ++cursor;
                continue;
            }
            if (!std::isalnum(character) && character != '-' && character != '_'
                && character < 0x80U) break;
            result.push_back(text[cursor++]);
        }
        return result;
    }

    static bool css_attribute_selector_matches(
        const dom_node& node,
        std::string_view condition)
    {
        const auto equal = condition.find('=');
        auto name_end = equal;
        auto attribute_operator = std::string_view{};
        if (equal != std::string::npos) {
            if (equal > 0U && (condition[equal - 1U] == '~'
                || condition[equal - 1U] == '|'
                || condition[equal - 1U] == '^'
                || condition[equal - 1U] == '$'
                || condition[equal - 1U] == '*')) {
                name_end = equal - 1U;
                attribute_operator = condition.substr(equal - 1U, 2U);
            } else {
                attribute_operator = condition.substr(equal, 1U);
            }
        }
        const auto raw_name = trim_css(std::string_view(condition).substr(0, name_end));
        size_t name_cursor = 0U;
        auto name = read_css_identifier(raw_name, name_cursor);
        if (name.empty() || name_cursor != raw_name.size()) return false;
        if (!node.xml_mode) name = lower_html_name(name);
        const auto attribute = node.attributes.find(name);
        if (attribute == node.attributes.end()) return false;
        if (equal == std::string::npos) return true;

        auto wanted = trim_css(std::string_view(condition).substr(equal + 1U));
        if (wanted.size() >= 2U && (wanted.front() == '\'' || wanted.front() == '"')) {
            wanted = wanted.substr(1U, wanted.size() - 2U);
        }
        const auto& actual = attribute->second;
        if (attribute_operator == "=") return actual == wanted;
        if (wanted.empty() && (attribute_operator == "^="
            || attribute_operator == "$=" || attribute_operator == "*=")) {
            return false;
        }
        if (attribute_operator == "^=") return actual.starts_with(wanted);
        if (attribute_operator == "$=") return actual.ends_with(wanted);
        if (attribute_operator == "*=") return actual.find(wanted) != std::string::npos;
        if (attribute_operator == "|=") {
            return actual == wanted || actual.starts_with(wanted + "-");
        }
        if (attribute_operator == "~=") {
            std::istringstream words(actual);
            for (std::string word; words >> word;) {
                if (word == wanted) return true;
            }
            return false;
        }
        return true;
    }

    static size_t find_css_attribute_close(
        std::string_view selector,
        size_t cursor)
    {
        char quote = 0;
        for (; cursor < selector.size(); ++cursor) {
            const auto character = selector[cursor];
            if (character == '\\') {
                cursor = skip_css_escape_sequence(selector, cursor) - 1U;
                continue;
            }
            if (quote != 0) {
                if (character == quote) quote = 0;
                continue;
            }
            if (character == '\'' || character == '"') {
                quote = character;
            } else if (character == ']') {
                return cursor;
            }
        }
        return std::string_view::npos;
    }

    static bool css_nth_expression_matches(
        std::string_view argument,
        int one_based_position)
    {
        auto expression = lower_html_name(trim_css(argument));
        std::erase_if(expression, [](unsigned char character) {
            return std::isspace(character);
        });
        auto coefficient = 0;
        auto offset = 0;
        const auto parse_integer = [](std::string_view value, int& result) {
            if (value.empty()) return false;
            if (value.front() == '+') value.remove_prefix(1U);
            if (value.empty()) return false;
            const auto parsed = std::from_chars(
                value.data(), value.data() + value.size(), result);
            return parsed.ec == std::errc{}
                && parsed.ptr == value.data() + value.size();
        };
        if (expression == "odd") {
            coefficient = 2;
            offset = 1;
        } else if (expression == "even") {
            coefficient = 2;
        } else if (const auto n = expression.find('n'); n != std::string::npos) {
            const auto coefficient_text = std::string_view(expression).substr(0, n);
            if (coefficient_text.empty() || coefficient_text == "+") {
                coefficient = 1;
            } else if (coefficient_text == "-") {
                coefficient = -1;
            } else if (!parse_integer(coefficient_text, coefficient)) {
                return false;
            }
            const auto offset_text = std::string_view(expression).substr(n + 1U);
            if (!offset_text.empty() && !parse_integer(offset_text, offset)) return false;
        } else if (!parse_integer(expression, offset)) {
            return false;
        }
        if (coefficient == 0) return one_based_position == offset;
        const auto difference = one_based_position - offset;
        return !((coefficient > 0 && difference < 0)
            || (coefficient < 0 && difference > 0))
            && difference % coefficient == 0;
    }

    bool is_actually_disabled(const dom_node& node) const
    {
        if (node.attributes.contains("disabled")) return true;
        if (node.tag == "option") {
            for (auto* ancestor = document.dom_parent(node); ancestor != nullptr;
                ancestor = document.dom_parent(*ancestor)) {
                if ((ancestor->tag == "optgroup" || ancestor->tag == "select")
                    && ancestor->attributes.contains("disabled")) return true;
                if (ancestor->tag == "select") break;
            }
        }
        if (node.tag != "button" && node.tag != "input"
            && node.tag != "select" && node.tag != "textarea") return false;
        for (auto* fieldset = document.dom_parent(node); fieldset != nullptr;
            fieldset = document.dom_parent(*fieldset)) {
            if (fieldset->tag != "fieldset"
                || !fieldset->attributes.contains("disabled")) continue;
            const dom_node* first_legend = nullptr;
            for (const auto* child : fieldset->children) {
                if (child != nullptr && child->tag == "legend") {
                    first_legend = child;
                    break;
                }
            }
            auto inside_first_legend = false;
            for (auto* ancestor = &node; ancestor != fieldset;
                ancestor = document.dom_parent(*ancestor)) {
                if (ancestor == first_legend) {
                    inside_first_legend = true;
                    break;
                }
            }
            if (!inside_first_legend) return true;
        }
        return false;
    }

    static compiled_css_compound compile_css_compound_selector(
        std::string_view selector)
    {
        compiled_css_compound result;
        selector = trim_css_view(selector);
        if (selector.empty()) return result;

        size_t cursor = 0U;
        if (selector.empty()) {
            // A bare structural pseudo-class has an implicit universal selector.
        } else if (selector[cursor] == '*') {
            ++cursor;
        } else if (std::isalpha(static_cast<unsigned char>(selector[cursor]))
            || selector[cursor] == '\\') {
            result.tag = read_css_identifier(selector, cursor);
        }
        while (cursor < selector.size()) {
            const auto marker = selector[cursor++];
            if (marker == '.' || marker == '#') {
                auto wanted = read_css_identifier(selector, cursor);
                if (wanted.empty()) return result;
                result.identities.emplace_back(marker, std::move(wanted));
            } else if (marker == '[') {
                const auto close = find_css_attribute_close(selector, cursor);
                if (close == std::string::npos) return result;
                result.attributes.emplace_back(selector.substr(cursor, close - cursor));
                cursor = close + 1U;
            } else if (marker == ':') {
                if (cursor < selector.size() && selector[cursor] == ':') {
                    result.pseudo_element = true;
                    ++cursor;
                }
                auto name = read_css_identifier(selector, cursor);
                if (name.empty()) return result;
                if (name == "before" || name == "after"
                    || name == "first-letter" || name == "first-line") {
                    result.pseudo_element = true;
                }
                auto argument = std::string{};
                if (cursor < selector.size() && selector[cursor] == '(') {
                    const auto argument_start = ++cursor;
                    auto depth = 1;
                    char quote = 0;
                    while (cursor < selector.size() && depth > 0) {
                        const auto value = selector[cursor];
                        if (quote != 0) {
                            if (value == quote
                                && (cursor == argument_start
                                    || selector[cursor - 1U] != '\\')) {
                                quote = 0;
                            }
                        } else if (value == '\'' || value == '"') {
                            quote = value;
                        } else if (value == '\\') {
                            cursor = skip_css_escape_sequence(selector, cursor) - 1U;
                        } else if (value == '(') {
                            ++depth;
                        } else if (value == ')') {
                            --depth;
                        }
                        if (depth > 0) ++cursor;
                    }
                    if (depth != 0) return result;
                    argument.assign(
                        selector.substr(argument_start, cursor - argument_start));
                    ++cursor;
                }
                result.pseudos.push_back(
                    compiled_css_pseudo{std::move(name), std::move(argument)});
            } else {
                return result;
            }
        }
        result.valid = true;
        return result;
    }

    bool css_compound_selector_matches(
        const dom_node& node,
        const compiled_css_compound& selector,
        const dom_node* scope_root = nullptr) const
    {
        if (!selector.valid) return false;
        // The probe intentionally collapses each browsing context's HTML and
        // BODY boxes into a single native root node.  Browser CSS still targets
        // that box with selectors such as `html.theme-dark`, so let the virtual
        // root participate as both tags.  A frame document is parented beneath
        // its owning iframe in the unified native tree.
        const auto explicit_document_element = node.tag == "html"
            && node.parent != nullptr
            && (node.parent == &document.body() || node.parent->tag == "iframe");
        const auto has_explicit_document_element = node.tag == "body"
            && std::any_of(node.children.begin(), node.children.end(), [](const auto* child) {
                return child != nullptr && child->tag == "html";
            });
        const auto is_document_root = explicit_document_element
            || (node.tag == "body"
                && (node.parent == nullptr || node.parent->tag == "iframe")
                && !has_explicit_document_element);
        // A pseudo-element is a different CSS box, not the originating DOM
        // element.  Treating it as the element makes rules such as
        // `.scroller::-webkit-scrollbar { display: none }` hide the scroller
        // itself, which is catastrophic for toolbar layout.
        if (selector.pseudo_element) {
            return false;
        }

        if (!selector.tag.empty()) {
            auto wanted_tag = selector.tag;
            if (!node.xml_mode) wanted_tag = lower_html_name(wanted_tag);
            if (wanted_tag != node.tag
                && !(wanted_tag == "html" && is_document_root && node.tag == "body")) return false;
        }
        for (const auto& [marker, wanted] : selector.identities) {
            if (marker == '#') {
                if (node.id_attribute != wanted) return false;
            } else if (!has_class(node, wanted)) {
                return false;
            }
        }
        for (const auto& attribute : selector.attributes) {
            if (!css_attribute_selector_matches(node, attribute)) return false;
        }

        const auto is_element = [](const dom_node* candidate) {
            return candidate != nullptr && !candidate->tag.starts_with('#');
        };
        std::vector<const dom_node*> element_siblings;
        auto element_siblings_ready = false;
        const auto siblings = [&]() -> const std::vector<const dom_node*>& {
            if (element_siblings_ready) return element_siblings;
            element_siblings_ready = true;
            if (node.parent != nullptr) {
                element_siblings.reserve(node.parent->children.size());
                for (const auto* child : node.parent->children) {
                    if (is_element(child)) element_siblings.push_back(child);
                }
            }
            return element_siblings;
        };

        for (const auto& pseudo : selector.pseudos) {
            const std::string_view name(pseudo.name);
            const std::string_view argument(pseudo.argument);
            const auto form_control = node.tag == "button" || node.tag == "input"
                || node.tag == "select" || node.tag == "textarea"
                || node.tag == "option" || node.tag == "optgroup" || node.tag == "fieldset";
            if (name == "root") {
                if (!is_document_root) return false;
            } else if (name == "scope") {
                if (scope_root == nullptr ? !is_document_root : scope_root != &node) return false;
            } else if (name == "first-child") {
                const auto& values = siblings();
                const auto position = std::find(values.begin(), values.end(), &node);
                if (node.parent == nullptr || position != values.begin()) return false;
            } else if (name == "last-child") {
                const auto& values = siblings();
                const auto position = std::find(values.begin(), values.end(), &node);
                if (node.parent == nullptr || position == values.end()
                    || position + 1 != values.end()) return false;
            } else if (name == "only-child") {
                if (node.parent == nullptr || siblings().size() != 1U) return false;
            } else if (name == "nth-child" || name == "nth-last-child"
                || name == "nth-of-type" || name == "nth-last-of-type") {
                if (node.parent == nullptr) return false;
                auto values = siblings();
                if (name == "nth-of-type" || name == "nth-last-of-type") {
                    std::erase_if(values, [&](const auto* sibling) {
                        return sibling->tag != node.tag;
                    });
                }
                const auto position = std::find(values.begin(), values.end(), &node);
                if (position == values.end()) return false;
                const auto from_end = name == "nth-last-child"
                    || name == "nth-last-of-type";
                const auto one_based_position = static_cast<int>(from_end
                    ? std::distance(position, values.end())
                    : std::distance(values.begin(), position) + 1);
                if (!css_nth_expression_matches(argument, one_based_position)) return false;
            } else if (name == "first-of-type" || name == "last-of-type"
                || name == "only-of-type") {
                if (node.parent == nullptr) return false;
                std::vector<const dom_node*> same_type;
                for (const auto* sibling : siblings()) {
                    if (sibling->tag == node.tag) same_type.push_back(sibling);
                }
                const auto same_position = std::find(same_type.begin(), same_type.end(), &node);
                if (name == "first-of-type" && same_position != same_type.begin()) return false;
                if (name == "last-of-type"
                    && (same_position == same_type.end() || same_position + 1 != same_type.end())) return false;
                if (name == "only-of-type" && same_type.size() != 1U) return false;
            } else if (name == "empty") {
                const auto has_text = std::any_of(
                    node.text_content.begin(),
                    node.text_content.end(),
                    [](unsigned char character) { return !std::isspace(character); });
                if (!node.children.empty() || has_text) return false;
            } else if (name == "enabled") {
                if (!form_control || is_actually_disabled(node)) return false;
            } else if (name == "disabled") {
                if (!form_control || !is_actually_disabled(node)) return false;
            } else if (name == "valid") {
                if (!form_control || node.tag == "fieldset" || node.tag == "optgroup"
                    || node.tag == "option") return false;
                if (node.attributes.contains("required")) {
                    const auto value = node.attributes.find("value");
                    if (value == node.attributes.end() || value->second.empty()) return false;
                }
            } else if (name == "invalid") {
                if (!form_control || !node.attributes.contains("required")) return false;
                const auto value = node.attributes.find("value");
                if (value != node.attributes.end() && !value->second.empty()) return false;
            } else if (name == "lang") {
                auto wanted = lower_html_name(trim_css(argument));
                if (wanted.size() >= 2U
                    && (wanted.front() == '\'' || wanted.front() == '"')
                    && wanted.back() == wanted.front()) {
                    wanted = wanted.substr(1U, wanted.size() - 2U);
                } else {
                    size_t language_cursor = 0U;
                    auto decoded = read_css_identifier(wanted, language_cursor);
                    if (decoded.empty() || language_cursor != wanted.size()) return false;
                    wanted = lower_html_name(decoded);
                }
                auto matched = false;
                for (auto* language_node = &node; language_node != nullptr;
                    language_node = document.dom_parent(*language_node)) {
                    auto language = language_node->attributes.find("lang");
                    if (language == language_node->attributes.end()) {
                        language = language_node->attributes.find("xml:lang");
                    }
                    if (language == language_node->attributes.end()) continue;
                    const auto actual = lower_html_name(language->second);
                    matched = actual == wanted || actual.starts_with(wanted + "-");
                    break;
                }
                if (!matched) return false;
            } else if (name == "dir") {
                auto wanted = lower_html_name(trim_css(argument));
                if (wanted.size() >= 2U
                    && (wanted.front() == '\'' || wanted.front() == '"')
                    && wanted.back() == wanted.front()) {
                    wanted = wanted.substr(1U, wanted.size() - 2U);
                }
                auto actual = std::string("ltr");
                for (auto* direction_node = &node; direction_node != nullptr;
                    direction_node = document.dom_parent(*direction_node)) {
                    const auto authored = direction_node->attributes.find("dir");
                    if (authored == direction_node->attributes.end()) continue;
                    const auto candidate = lower_html_name(trim_css(authored->second));
                    if (candidate == "ltr" || candidate == "rtl") {
                        actual = candidate;
                        break;
                    }
                }
                if ((wanted != "ltr" && wanted != "rtl") || wanted != actual) {
                    return false;
                }
            } else if (name == "target") {
                const auto local_context = isolate->GetCurrentContext();
                v8::Local<v8::Value> location_value;
                v8::Local<v8::Value> hash_value;
                if (!local_context->Global()->Get(
                        local_context, js_string(isolate, "location")).ToLocal(&location_value)
                    || !location_value->IsObject()
                    || !location_value.As<v8::Object>()->Get(
                        local_context, js_string(isolate, "hash")).ToLocal(&hash_value)) {
                    return false;
                }
                auto hash = to_utf8(isolate, hash_value);
                if (!hash.empty() && hash.front() == '#') hash.erase(0U, 1U);
                if (hash.empty() || node.id_attribute != hash) return false;
            } else if (name == "checked") {
                const auto input_type = node.attributes.find("type");
                const auto checkable_input = node.tag == "input"
                    && input_type != node.attributes.end()
                    && (input_type->second == "checkbox" || input_type->second == "radio");
                const auto checked_input = checkable_input
                    && (node.form_control().checkedness_initialized
                        ? node.form_control().checkedness
                        : node.attributes.contains("checked"));
                const auto selected_option = node.tag == "option"
                    && option_is_selected(const_cast<dom_node&>(node));
                if (!checked_input
                    && !selected_option) return false;
            } else if (name == "hover") {
                auto* hovered = hover_target;
                bool matches_hover = false;
                for (; hovered != nullptr; hovered = hovered->parent) {
                    if (hovered == &node) {
                        matches_hover = true;
                        break;
                    }
                }
                if (!matches_hover) return false;
            } else if (name == "focus") {
                if (active_element != &node) return false;
            } else if (name == "focus-visible") {
                if (active_element != &node || (!focus_visible && !is_text_control(&node))) {
                    return false;
                }
            } else if (name == "focus-within") {
                auto matches_focus_within = false;
                for (auto* focused = active_element; focused != nullptr;
                    focused = document.event_parent(*focused)) {
                    if (focused == &node) {
                        matches_focus_within = true;
                        break;
                    }
                }
                if (!matches_focus_within) return false;
            } else if (name == "not") {
                size_t start = 0;
                while (start <= argument.size()) {
                    auto end = argument.find(',', start);
                    if (end == std::string::npos) end = argument.size();
                    if (css_selector_matches(
                            node,
                            trim_css_view(argument.substr(start, end - start)),
                            scope_root)) {
                        return false;
                    }
                    if (end == argument.size()) break;
                    start = end + 1U;
                }
            } else if (name == "is" || name == "where") {
                bool any = false;
                size_t start = 0;
                while (start <= argument.size()) {
                    auto end = argument.find(',', start);
                    if (end == std::string::npos) end = argument.size();
                    any = any || css_selector_matches(
                        node,
                        trim_css_view(argument.substr(start, end - start)),
                        scope_root);
                    if (end == argument.size()) break;
                    start = end + 1U;
                }
                if (!any) return false;
            } else if (name == "has") {
                bool any = false;
                size_t start = 0;
                while (start <= argument.size()) {
                    auto end = argument.find(',', start);
                    if (end == std::string::npos) end = argument.size();
                    const auto relative = trim_css_view(
                        argument.substr(start, end - start));
                    if (!relative.empty()) {
                        if (relative.front() == '>') {
                            const auto child_selector = trim_css_view(relative.substr(1U));
                            any = !child_selector.empty() && std::any_of(
                                node.children.begin(),
                                node.children.end(),
                                [&](const auto* child) {
                                    return is_element(child)
                                        && css_selector_matches(
                                            *child, child_selector, &node);
                                });
                        } else if (relative.front() != '+' && relative.front() != '~') {
                            any = query_selector_node(
                                const_cast<dom_node&>(node), relative, false) != nullptr;
                        }
                    }
                    if (any || end == argument.size()) break;
                    start = end + 1U;
                }
                if (!any) return false;
            } else {
                // Stateful and vendor pseudo-classes are not active unless the
                // native DOM explicitly models that state.  Matching them as
                // the base element applies hover/focus/active CSS constantly.
                return false;
            }
        }
        return true;
    }

    bool css_compound_selector_matches(
        const dom_node& node,
        std::string_view selector,
        const dom_node* scope_root = nullptr) const
    {
        return css_compound_selector_matches(
            node,
            compile_css_compound_selector(selector),
            scope_root);
    }

    struct css_selector_split final {
        std::string_view left;
        std::string_view right;
        char combinator{' '};
        bool found{false};
    };

    static css_selector_split split_css_selector(std::string_view selector)
    {
        selector = trim_css_view(selector);
        int bracket_depth = 0;
        int parenthesis_depth = 0;
        char quote = 0;
        css_selector_split last{{}, selector, ' ', false};
        for (size_t index = 0; index < selector.size(); ++index) {
            const auto value = selector[index];
            if (quote != 0) {
                if (value == quote && (index == 0U || selector[index - 1U] != '\\')) quote = 0;
                continue;
            }
            if (value == '\\') {
                index = skip_css_escape_sequence(selector, index) - 1U;
                continue;
            }
            if (value == '\'' || value == '"') {
                quote = value;
                continue;
            }
            if (value == '[') ++bracket_depth;
            else if (value == ']') --bracket_depth;
            else if (value == '(') ++parenthesis_depth;
            else if (value == ')') --parenthesis_depth;
            if (bracket_depth != 0 || parenthesis_depth != 0) continue;
            if (value == '>' || value == '+' || value == '~') {
                auto right = index + 1U;
                while (right < selector.size()
                    && std::isspace(static_cast<unsigned char>(selector[right]))) ++right;
                last = {
                    trim_css_view(selector.substr(0, index)),
                    trim_css_view(selector.substr(right)),
                    value,
                    true};
                continue;
            }
            if (!std::isspace(static_cast<unsigned char>(value))) continue;
            const auto whitespace_start = index;
            while (index + 1U < selector.size()
                && std::isspace(static_cast<unsigned char>(selector[index + 1U]))) ++index;
            const auto right = index + 1U;
            if (right >= selector.size()) continue;
            const auto previous = whitespace_start == 0U ? 0 : selector[whitespace_start - 1U];
            const auto next = selector[right];
            if (previous == '>' || previous == '+' || previous == '~'
                || next == '>' || next == '+' || next == '~') continue;
            last = {
                trim_css_view(selector.substr(0, whitespace_start)),
                trim_css_view(selector.substr(right)),
                ' ',
                true};
        }
        return last;
    }

    static compiled_css_selector compile_css_selector(std::string_view selector)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_SERVO_SELECTORS)
        const auto parsed = parse_selector_syntax(selector);
        if (!parsed || parsed.selectors.size() != 1U) return {};
        const auto& source = parsed.selectors.front();
        compiled_css_selector result{
            source.compounds,
            source.combinators,
            source.specificity,
            {}};
#else
        std::vector<std::string> reversed_compounds;
        std::vector<char> reversed_combinators;
        auto remaining = trim_css_view(selector);
        while (!remaining.empty()) {
            const auto split = split_css_selector(remaining);
            reversed_compounds.emplace_back(trim_css_view(split.right));
            if (!split.found || split.left.empty()) break;
            reversed_combinators.push_back(split.combinator);
            remaining = split.left;
        }

        compiled_css_selector result;
        result.compounds.assign(
            reversed_compounds.rbegin(),
            reversed_compounds.rend());
        result.combinators.assign(
            reversed_combinators.rbegin(),
            reversed_combinators.rend());
        result.specificity = css_selector_specificity(selector);
#endif
        result.compiled_compounds.reserve(result.compounds.size());
        for (const auto& compound : result.compounds) {
            result.compiled_compounds.push_back(
                compile_css_compound_selector(compound));
        }
        return result;
    }

    static compiled_css_selector_list compile_css_selector_list(
        std::string_view selector)
    {
        compiled_css_selector_list result;
#if defined(WEBSCENE_NATIVE_ENGINE_SERVO_SELECTORS)
        const auto parsed = parse_selector_syntax(selector);
        if (!parsed) return result;
        result.selectors.reserve(parsed.selectors.size());
        for (const auto& source : parsed.selectors) {
            compiled_css_selector compiled{
                source.compounds,
                source.combinators,
                source.specificity,
                {}};
            compiled.compiled_compounds.reserve(compiled.compounds.size());
            for (const auto& compound : compiled.compounds) {
                compiled.compiled_compounds.push_back(
                    compile_css_compound_selector(compound));
            }
            result.selectors.push_back(std::move(compiled));
        }
#else
        size_t start = 0;
        int bracket_depth = 0;
        int parenthesis_depth = 0;
        char quote = 0;
        for (size_t index = 0; index <= selector.size(); ++index) {
            const auto at_end = index == selector.size();
            const auto character = at_end ? ',' : selector[index];
            if (!at_end && quote != 0) {
                if (character == quote
                    && (index == 0U || selector[index - 1U] != '\\')) {
                    quote = 0;
                }
                continue;
            }
            if (!at_end && character == '\\') {
                index = skip_css_escape_sequence(selector, index) - 1U;
                continue;
            }
            if (!at_end && (character == '\'' || character == '"')) {
                quote = character;
                continue;
            }
            if (!at_end && character == '[') ++bracket_depth;
            else if (!at_end && character == ']') --bracket_depth;
            else if (!at_end && character == '(') ++parenthesis_depth;
            else if (!at_end && character == ')') --parenthesis_depth;
            else if (character == ',' && bracket_depth == 0 && parenthesis_depth == 0) {
                result.selectors.push_back(compile_css_selector(
                    selector.substr(start, index - start)));
                start = index + 1U;
            }
        }
#endif
        return result;
    }

    const compiled_css_selector_list& cached_css_selector_list(
        std::string_view selector) const
    {
        if (const auto known = compiled_css_selector_lists.find(selector);
            known != compiled_css_selector_lists.end()) {
            return known->second;
        }
        if (compiled_css_selector_lists.size()
            >= maximum_compiled_css_selector_lists) {
            compiled_css_selector_lists.clear();
        }
        auto key = std::string(selector);
        return compiled_css_selector_lists.emplace(
            key,
            compile_css_selector_list(key)).first->second;
    }

    static const dom_node* previous_element_sibling(const dom_node& node)
    {
        if (node.parent == nullptr) return nullptr;
        const auto position = std::find(
            node.parent->children.begin(),
            node.parent->children.end(),
            &node);
        if (position == node.parent->children.begin()
            || position == node.parent->children.end()) {
            return nullptr;
        }
        auto sibling = position;
        while (sibling != node.parent->children.begin()) {
            --sibling;
            if (*sibling != nullptr && !(*sibling)->tag.starts_with('#')) {
                return *sibling;
            }
        }
        return nullptr;
    }

    bool css_selector_matches(
        const dom_node& node,
        std::string_view selector,
        const dom_node* scope_root = nullptr) const
    {
        return compiled_css_selector_list_matches(
            node,
            cached_css_selector_list(selector),
            scope_root);
    }

    bool compiled_css_selector_matches(
        const dom_node& node,
        const compiled_css_selector& selector,
        size_t component,
        const dom_node* scope_root = nullptr) const
    {
        if (selector.compounds.empty()
            || selector.compiled_compounds.size() != selector.compounds.size()
            || component >= selector.compiled_compounds.size()
            || !css_compound_selector_matches(
                node,
                selector.compiled_compounds[component],
                scope_root)) {
            return false;
        }
        if (component == 0U) return true;
        if (selector.combinators.size() < component) return false;

        const auto combinator = selector.combinators[component - 1U];
        if (combinator == '>') {
            const auto* parent = document.dom_parent(node);
            return parent != nullptr
                && compiled_css_selector_matches(
                    *parent,
                    selector,
                    component - 1U,
                    scope_root);
        }
        if (combinator == '+') {
            const auto* sibling = previous_element_sibling(node);
            return sibling != nullptr
                && compiled_css_selector_matches(
                    *sibling,
                    selector,
                    component - 1U,
                    scope_root);
        }
        if (combinator == '~') {
            for (auto* sibling = previous_element_sibling(node); sibling != nullptr;
                sibling = previous_element_sibling(*sibling)) {
                if (compiled_css_selector_matches(
                        *sibling,
                        selector,
                        component - 1U,
                        scope_root)) {
                    return true;
                }
            }
            return false;
        }
        for (auto* ancestor = document.dom_parent(node); ancestor != nullptr;
            ancestor = document.dom_parent(*ancestor)) {
            if (compiled_css_selector_matches(
                    *ancestor,
                    selector,
                    component - 1U,
                    scope_root)) {
                return true;
            }
        }
        return false;
    }

    bool css_rule_matches(
        const dom_node& node,
        const css_rule& rule,
        const dom_node* scope_root = nullptr) const
    {
        return !rule.compiled_selector().compounds.empty()
            && compiled_css_selector_matches(
                node,
                rule.compiled_selector(),
                rule.compiled_selector().compounds.size() - 1U,
                scope_root);
    }

    bool css_selector_list_matches(
        const dom_node& node,
        std::string_view selector,
        const dom_node* scope_root = nullptr) const
    {
        return compiled_css_selector_list_matches(
            node,
            cached_css_selector_list(selector),
            scope_root);
    }

    static int split_pseudo_element_selector(const std::string& selector, std::string& origin)
    {
        const auto split_suffix = [&](std::string_view suffix, int kind) {
            if (!selector.ends_with(suffix)) return 0;
            origin = trim_css(std::string_view(selector).substr(0, selector.size() - suffix.size()));
            return kind;
        };
        if (const auto kind = split_suffix("::before", 1); kind != 0) return kind;
        if (const auto kind = split_suffix("::after", 2); kind != 0) return kind;
        if (const auto kind = split_suffix("::-webkit-scrollbar", 3); kind != 0) return kind;
        if (const auto kind = split_suffix(":before", 1); kind != 0) return kind;
        return split_suffix(":after", 2);
    }
