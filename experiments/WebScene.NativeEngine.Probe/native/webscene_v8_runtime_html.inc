    static void get_inner_html(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_property));
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr) return;
        std::string html;
        const auto& children = node->tag == "template" && node->template_contents != nullptr
            ? node->template_contents->children
            : node->children;
        for (const auto* child : children) {
            if (child != nullptr) append_serialized_html(*child, html);
        }
        info.GetReturnValue().Set(js_string(info.GetIsolate(), html.c_str()));
    }

    static void append_escaped_html(std::string_view value, std::string& result, bool attribute)
    {
        for (const auto character : value) {
            switch (character) {
                case '&': result += "&amp;"; break;
                case '<': result += "&lt;"; break;
                case '>': result += "&gt;"; break;
                case '"': result += attribute ? "&quot;" : "\""; break;
                default: result.push_back(character); break;
            }
        }
    }

    static void append_serialized_html(const dom_node& node, std::string& result)
    {
        if (node.kind == dom_node_kind::text) {
            append_escaped_html(node.text_content, result, false);
            return;
        }
        if (node.kind == dom_node_kind::comment) {
            result += "<!--";
            result += node.text_content;
            result += "-->";
            return;
        }
        if (node.tag.empty() || node.tag.front() == '#') return;
        result.push_back('<');
        result += node.tag;
        for (const auto& [name, value] : node.attributes) {
            if (name == "namespace" || name == "object-html") continue;
            result.push_back(' ');
            result += name;
            result += "=\"";
            append_escaped_html(value, result, true);
            result.push_back('"');
        }
        result.push_back('>');
        const auto void_element = node.tag == "area" || node.tag == "base" || node.tag == "br"
            || node.tag == "col" || node.tag == "embed" || node.tag == "hr"
            || node.tag == "img" || node.tag == "input" || node.tag == "link"
            || node.tag == "meta" || node.tag == "param" || node.tag == "source"
            || node.tag == "track" || node.tag == "wbr";
        if (void_element) return;
        if (!node.text_content.empty()) append_escaped_html(node.text_content, result, false);
        for (const auto* child : node.children) {
            if (child != nullptr) append_serialized_html(*child, result);
        }
        result += "</";
        result += node.tag;
        result.push_back('>');
    }

    static std::string lower_html_name(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    void record_html_attribute_feature(std::string name, std::string source)
    {
        name = lower_html_name(std::move(name));
        auto classification = std::string("supported");
        auto semantic_slice = std::string{};
        if (name == "role" || name.starts_with("aria-")) {
            classification = "partially-supported";
            semantic_slice = "retained for selectors and DOM inspection; native platform accessibility projection is not certified";
        } else if (name == "allowfullscreen"
            || name == "allowtransparency"
            || name == "autocomplete"
            || name == "draggable"
            || name == "frameborder"
            || name == "inputmode"
            || name == "maxlength"
            || name == "placeholder"
            || name == "scope"
            || name == "scrolling") {
            classification = "unsupported";
            semantic_slice = "retained as an attribute but its browser behavior is not implemented";
        } else if (name == "type") {
            classification = "partially-supported";
            semantic_slice = "button and retained text-input types only";
        } else if (name == "xmlns" || name == "version") {
            classification = "partially-supported";
            semantic_slice = "retained for SVG/DOM inspection; XML namespace processing is not implemented";
        } else if (name == "clip-rule"
            || name == "cliprule"
            || name == "cx"
            || name == "cy"
            || name == "d"
            || name == "fill"
            || name == "fill-rule"
            || name == "fillrule"
            || name == "r"
            || name == "rx"
            || name == "stroke"
            || name == "stroke-linecap"
            || name == "stroke-width"
            || name == "transform"
            || name == "viewbox"
            || name == "x"
            || name == "y") {
            classification = "partially-supported";
            semantic_slice = "retained for the native SVG icon paint subset";
        }
        record_feature(
            "html",
            "attribute:" + name,
            std::move(classification),
            std::move(semantic_slice),
            std::move(source));
    }

    static std::string decode_html_text(std::string value)
    {
        std::string result;
        result.reserve(value.size());
        size_t cursor = 0;
        while (cursor < value.size()) {
            if (value[cursor] != '&') {
                result.push_back(value[cursor++]);
                continue;
            }
            const auto semicolon = value.find(';', cursor + 1U);
            if (semicolon == std::string::npos || semicolon - cursor > 32U) {
                result.push_back(value[cursor++]);
                continue;
            }
            const auto entity = std::string_view(value).substr(
                cursor + 1U, semicolon - cursor - 1U);
            uint32_t codepoint = 0U;
            auto recognized = true;
            if (!entity.empty() && entity.front() == '#') {
                const auto hex = entity.size() > 1U
                    && (entity[1] == 'x' || entity[1] == 'X');
                const auto digits = entity.substr(hex ? 2U : 1U);
                if (digits.empty()) {
                    recognized = false;
                } else {
                    char* end = nullptr;
                    const auto digits_copy = std::string(digits);
                    const auto parsed = std::strtoul(
                        digits_copy.c_str(), &end, hex ? 16 : 10);
                    recognized = end != digits_copy.c_str() && end != nullptr && *end == '\0';
                    codepoint = recognized ? static_cast<uint32_t>(parsed) : 0U;
                }
            } else if (entity == "lt") codepoint = '<';
            else if (entity == "gt") codepoint = '>';
            else if (entity == "amp") codepoint = '&';
            else if (entity == "quot") codepoint = '"';
            else if (entity == "apos") codepoint = '\'';
            else if (entity == "nbsp") codepoint = 0x00A0U;
            else recognized = false;
            if (!recognized) {
                result.append(value, cursor, semicolon - cursor + 1U);
            } else {
                if (codepoint == 0U || codepoint > 0x10FFFFU
                    || (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
                    codepoint = 0xFFFDU;
                }
                append_utf8_codepoint(result, codepoint);
            }
            cursor = semicolon + 1U;
        }
        return result;
    }

    static uint64_t inline_style_mask(std::string_view name)
    {
        if (name == "width") return inline_width;
        if (name == "height") return inline_height;
        if (name == "min-width") return inline_min_width;
        if (name == "min-height") return inline_min_height;
        if (name == "max-width") return inline_max_width;
        if (name == "max-height") return inline_max_height;
        if (name == "left" || name == "inset-inline-start") return inline_left;
        if (name == "top" || name == "inset-block-start") return inline_top;
        if (name == "right" || name == "inset-inline-end") return inline_right;
        if (name == "bottom" || name == "inset-block-end") return inline_bottom;
        if (name == "inset") {
            return inline_left | inline_top | inline_right | inline_bottom;
        }
        if (name == "display") return inline_display;
        if (name == "position") return inline_position;
        if (name == "float" || name == "cssFloat") return inline_float;
        if (name == "z-index" || name == "zIndex") return inline_z_index;
        if (name == "flex-direction" || name == "flexDirection") {
            return inline_flex_direction;
        }
        if (name == "flex-flow" || name == "flexFlow") {
            return inline_flex_direction | inline_flex_wrap;
        }
        if (name == "flex") {
            return inline_flex_grow | inline_flex_shrink | inline_flex_basis;
        }
        if (name == "flex-grow") return inline_flex_grow;
        if (name == "flex-shrink") return inline_flex_shrink;
        if (name == "flex-basis") return inline_flex_basis;
        if (name == "flex-wrap" || name == "flexWrap") return inline_flex_wrap;
        if (name == "grid-template-columns" || name == "grid-template-rows"
            || name == "grid-area" || name == "grid-row"
            || name == "grid-row-start" || name == "grid-row-end"
            || name == "grid-column" || name == "grid-column-start"
            || name == "grid-column-end") {
            return inline_grid;
        }
        if (name == "transition") return inline_transition;
        if (name == "transition-property") return inline_transition_property;
        if (name == "transition-duration") return inline_transition_duration;
        if (name == "transition-delay") return inline_transition_delay;
        if (name == "transition-timing-function") return inline_transition_timing;
        if (name == "background" || name == "background-color") return inline_background;
        if (name == "overflow" || name == "overflow-x" || name == "overflow-y") return inline_overflow;
        if (name == "visibility") return inline_visibility;
        if (name == "pointer-events") return inline_pointer_events;
        if (name == "color") return inline_color;
        if (name == "fill") return inline_svg_fill;
        if (name == "stroke") return inline_svg_stroke;
        if (name == "cursor") return inline_cursor;
        if (name == "font-size") return inline_font_size;
        if (name == "font-family") return inline_font_family;
        if (name == "-webkit-font-smoothing" || name == "webkit-font-smoothing"
            || name == "webkitFontSmoothing") {
            return inline_font_smoothing;
        }
        if (name == "font-weight") return inline_font_weight;
        if (name == "line-height") return inline_line_height;
        if (name == "letter-spacing") return inline_letter_spacing;
        if (name == "word-spacing") return inline_word_spacing;
        if (name == "text-align") return inline_text_align;
        if (name == "white-space") return inline_white_space;
        if (name == "padding" || name.starts_with("padding-")) return inline_padding;
        const auto canonical_name = canonical_css_property_name(name);
        if (canonical_name == "margin" || canonical_name.starts_with("margin-")) {
            return inline_margin;
        }
        if (name == "align-items") return inline_align_items;
        if (name == "align-self") return inline_align_self;
        if (name == "justify-content") return inline_justify_content;
        if (name == "gap" || name == "row-gap" || name == "column-gap"
            || name == "rowGap" || name == "columnGap") return inline_gap;
        if (name == "border-spacing" || name == "borderSpacing"
            || name == "border-collapse" || name == "borderCollapse") {
            return inline_table_border_model;
        }
        if (name == "box-sizing") return inline_box_sizing;
        if (name == "box-shadow" || name == "boxShadow") return inline_box_shadow;
        if (canonical_name == "border"
            || canonical_name == "border-width"
            || canonical_name == "border-style"
            || canonical_name == "border-color"
            || (canonical_name.starts_with("border-")
                && (canonical_name.ends_with("-width")
                    || canonical_name.ends_with("-style")
                    || canonical_name.ends_with("-color")))
            || canonical_name == "border-top"
            || canonical_name == "border-right"
            || canonical_name == "border-bottom"
            || canonical_name == "border-left") {
            return inline_border;
        }
        if (name == "border-radius"
            || name == "border-top-left-radius" || name == "border-top-right-radius"
            || name == "border-bottom-right-radius" || name == "border-bottom-left-radius"
            || name == "border-start-start-radius" || name == "border-start-end-radius"
            || name == "border-end-end-radius" || name == "border-end-start-radius") {
            return inline_border_radius;
        }
        if (name == "transform") return inline_transform;
        if (name == "transform-origin" || name == "transformOrigin") {
            return inline_transform_origin;
        }
        if (name == "opacity") return inline_opacity;
        return 0U;
    }

    static size_t html_tag_end(const std::string& html, size_t open)
    {
        char quote = 0;
        for (auto cursor = open + 1U; cursor < html.size(); ++cursor) {
            const auto character = html[cursor];
            if (quote != 0) {
                if (character == quote) quote = 0;
            } else if (character == '\'' || character == '"') {
                quote = character;
            } else if (character == '>') {
                return cursor;
            }
        }
        return std::string::npos;
    }

    static size_t raw_text_closing_tag(
        const std::string& html,
        size_t cursor,
        std::string_view tag)
    {
        for (auto candidate = html.find("</", cursor);
             candidate != std::string::npos;
             candidate = html.find("</", candidate + 2U)) {
            auto index = candidate + 2U;
            while (index < html.size()
                && std::isspace(static_cast<unsigned char>(html[index]))) ++index;
            if (index + tag.size() > html.size()) continue;
            auto matches = true;
            for (size_t offset = 0; offset < tag.size(); ++offset) {
                const auto character = static_cast<unsigned char>(html[index + offset]);
                if (static_cast<char>(std::tolower(character)) != tag[offset]) {
                    matches = false;
                    break;
                }
            }
            if (!matches) continue;
            const auto boundary = index + tag.size();
            if (boundary >= html.size()
                || std::isspace(static_cast<unsigned char>(html[boundary]))
                || html[boundary] == '>') {
                return candidate;
            }
        }
        return std::string::npos;
    }

    static std::vector<std::pair<std::string, std::string>> parse_html_attributes(
        std::string_view opening,
        size_t cursor)
    {
        std::vector<std::pair<std::string, std::string>> result;
        while (cursor < opening.size()) {
            while (cursor < opening.size()
                && std::isspace(static_cast<unsigned char>(opening[cursor]))) ++cursor;
            if (cursor >= opening.size() || opening[cursor] == '>' || opening[cursor] == '/') break;
            const auto name_begin = cursor;
            while (cursor < opening.size()
                && !std::isspace(static_cast<unsigned char>(opening[cursor]))
                && opening[cursor] != '=' && opening[cursor] != '>' && opening[cursor] != '/') ++cursor;
            auto name = std::string(opening.substr(name_begin, cursor - name_begin));
            while (cursor < opening.size()
                && std::isspace(static_cast<unsigned char>(opening[cursor]))) ++cursor;
            std::string value;
            if (cursor < opening.size() && opening[cursor] == '=') {
                ++cursor;
                while (cursor < opening.size()
                    && std::isspace(static_cast<unsigned char>(opening[cursor]))) ++cursor;
                if (cursor < opening.size() && (opening[cursor] == '\'' || opening[cursor] == '"')) {
                    const auto quote = opening[cursor++];
                    const auto value_begin = cursor;
                    while (cursor < opening.size() && opening[cursor] != quote) ++cursor;
                    value = std::string(opening.substr(value_begin, cursor - value_begin));
                    if (cursor < opening.size()) ++cursor;
                } else {
                    const auto value_begin = cursor;
                    while (cursor < opening.size()
                        && !std::isspace(static_cast<unsigned char>(opening[cursor]))
                        && opening[cursor] != '>') ++cursor;
                    value = std::string(opening.substr(value_begin, cursor - value_begin));
                }
            }
            if (!name.empty()) result.emplace_back(std::move(name), decode_html_text(std::move(value)));
        }
        return result;
    }

    void activate_html5ever_subtree(dom_node& root, bool activate_style_elements = true)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_HTML5EVER)
        std::vector<dom_node*> stylesheets;
        const auto visit = [&](const auto& self, dom_node& node) -> void {
            if (node.kind == dom_node_kind::element) {
                record_feature(
                    "html", "element:" + node.tag, "supported", {}, "html5ever");
                for (const auto& [name, value] : node.attributes) {
                    static_cast<void>(value);
                    record_html_attribute_feature(name, "html5ever");
                }
                apply_css_rules(node);
                if (node.tag == "script") {
                    // Scripts created by fragment parsing are inert, and the
                    // document parser executes its authored scripts through
                    // the navigation pipeline. In either case, reconnecting
                    // the resulting DOM node must never execute it again.
                    node.script_state = script_execution_state::already_started;
                }
                if (const auto style = node.attributes.find("style");
                    style != node.attributes.end()) {
                    uint64_t inline_property_mask = 0U;
                    for (const auto& declaration : parse_css_declarations(style->second, true)) {
                        inventory_css_value_functions(
                            declaration.value, "inline-style-parser");
                        if (!store_inline_declaration(node, declaration)) continue;
                        apply_css_declaration(node, declaration);
                        inline_property_mask |= inline_style_mask(declaration.name);
                    }
                    node.style.inline_property_mask |= inline_property_mask;
                    detect_css_compositions(node);
                }
                if (node.tag == "input") {
                    const auto value = node.attributes.find("value");
                    if (value != node.attributes.end()) {
                        auto& form = node.mutable_form_control();
                        form.value = value->second;
                        form.value_initialized = true;
                        form.dirty_value = false;
                        form.selection_start = form.value.size();
                        form.selection_end = form.value.size();
                        form.selection_direction = text_selection_direction::none;
                    }
                } else if (node.tag == "textarea") {
                    reset_textarea_value(node);
                }
                if (node.tag == "iframe") {
                    const auto source = node.attributes.find("src");
                    if (source != node.attributes.end()) {
                        const auto object_url = object_urls.find(source->second);
                        if (object_url != object_urls.end()) {
                            node.attributes["object-html"] = object_url->second;
                        }
                    }
                    node.style.width = {100, length_unit::percent};
                    node.style.height = {100, length_unit::percent};
                    node.style.background_rgba = 0x131722FFU;
                    node.style.inline_property_mask |=
                        inline_width | inline_height | inline_background;
                    if (node.attributes.contains("object-html")) {
                        pending_frame_hydrations.push_back(&node);
                    }
                }
                if (activate_style_elements && node.tag == "style") {
                    node.text_content.clear();
                    for (const auto* child : node.children) {
                        if (child != nullptr && child->kind == dom_node_kind::text) {
                            node.text_content += child->text_content;
                        }
                    }
                    stylesheets.push_back(&node);
                }
            }
            for (auto* child : node.children) {
                if (child != nullptr) self(self, *child);
            }
            if (node.template_contents != nullptr) self(self, *node.template_contents);
        };
        visit(visit, root);
        for (auto* stylesheet : stylesheets) {
            if (stylesheet != nullptr && !stylesheet->text_content.empty()) {
                activate_connected_stylesheet(*stylesheet);
            }
        }
#else
        static_cast<void>(root);
        static_cast<void>(activate_style_elements);
#endif
    }

    void parse_inner_html(
        dom_node& parent,
        const std::string& html,
        const dom_node* context_element = nullptr)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_HTML5EVER)
        const auto& context = context_element == nullptr ? parent : *context_element;
        const auto context_name = context.kind == dom_node_kind::element
            ? std::string_view(context.tag)
            : std::string_view("body");
        const auto context_namespace = context.namespace_uri().empty()
            ? std::string_view("http://www.w3.org/1999/xhtml")
            : context.namespace_uri();
        const auto result = parse_html_fragment(
            document,
            parent,
            html,
            context_name,
            context_namespace);
        html_parse_duration_ns += result.duration_ns;
        html_parse_error_count += result.parse_error_count;
        html_parse_callback_count += result.callback_count;
        html_parse_element_count += result.element_count;
        html_parse_text_append_count += result.text_append_count;
        html_parse_comment_count += result.comment_count;
        html_parse_doctype_count += result.doctype_count;
        html_parse_rust_allocation_count += result.rust_allocation_count;
        html_parse_rust_peak_bytes = std::max(
            html_parse_rust_peak_bytes, result.rust_peak_bytes);
        html_parse_rust_retained_bytes += result.rust_retained_bytes;
        if (!result) {
            last_error = result.error.empty()
                ? "html5ever tree construction failed with status "
                    + std::to_string(result.status)
                : result.error;
            return;
        }
        activate_html5ever_subtree(parent);
        recascade_connected_subtree(parent);
        if (is_connected(parent)) {
            enqueue_connected_image_resources_subtree(
                parent,
                isolate->GetCurrentContext());
        }
        document.mark_dirty();
        for (auto* child : parent.children) {
            if (child != nullptr) {
                notify_custom_elements_subtree(*child, "connected");
            }
        }
        return;
#else
        std::vector<dom_node*> stack{&parent};
        size_t cursor = 0;
        while (cursor < html.size()) {
            if (stack.back()->tag == "script" || stack.back()->tag == "style") {
                const auto raw_end = raw_text_closing_tag(html, cursor, stack.back()->tag);
                const auto content_end = raw_end == std::string::npos ? html.size() : raw_end;
                if (content_end > cursor) {
                    stack.back()->text_content.append(html, cursor, content_end - cursor);
                    cursor = content_end;
                    continue;
                }
                if (raw_end == std::string::npos) break;
            }
            const auto open = html.find('<', cursor);
            if (open == std::string::npos) {
                const auto value = decode_html_text(html.substr(cursor));
                if (std::any_of(value.begin(), value.end(), [](unsigned char character) {
                    return !std::isspace(character);
                })) {
                    if (stack.back()->tag == "style") {
                        stack.back()->text_content += value;
                    } else {
                        auto& text = document.create_element("#text");
                        text.text_content = value;
                        document.append_child(*stack.back(), text);
                    }
                }
                break;
            }
            if (open > cursor) {
                const auto value = decode_html_text(html.substr(cursor, open - cursor));
                if (std::any_of(value.begin(), value.end(), [](unsigned char character) {
                    return !std::isspace(character);
                })) {
                    if (stack.back()->tag == "style") {
                        stack.back()->text_content += value;
                    } else {
                        auto& text = document.create_element("#text");
                        text.text_content = value;
                        document.append_child(*stack.back(), text);
                    }
                }
            }
            if (html.compare(open, 4U, "<!--") == 0) {
                const auto close = html.find("-->", open + 4U);
                cursor = close == std::string::npos ? html.size() : close + 3U;
                continue;
            }
            const auto close = html_tag_end(html, open);
            if (close == std::string::npos) break;
            if (open + 1U < html.size() && html[open + 1U] == '!') {
                cursor = close + 1U;
                continue;
            }
            if (open + 1U < html.size() && html[open + 1U] == '/') {
                auto name_begin = open + 2U;
                while (name_begin < close
                    && std::isspace(static_cast<unsigned char>(html[name_begin]))) ++name_begin;
                auto name_end = name_begin;
                while (name_end < close
                    && !std::isspace(static_cast<unsigned char>(html[name_end]))
                    && html[name_end] != '>') ++name_end;
                const auto name = lower_html_name(html.substr(name_begin, name_end - name_begin));
                while (stack.size() > 1U) {
                    const auto matched = stack.back()->tag == name;
                    if (matched && name == "style") {
                        std::string stylesheet = stack.back()->text_content;
                        for (const auto* child : stack.back()->children) {
                            if (child != nullptr && child->tag == "#text") {
                                stylesheet += child->text_content;
                            }
                        }
                        // A connected STYLE parsed through innerHTML must
                        // invalidate boxes that predate it, including the
                        // collapsed virtual HTML/BODY root. Merely appending
                        // rules makes following siblings match but leaves root
                        // font metrics and already-parsed siblings stale.
                        if (!stylesheet.empty()) {
                            stack.back()->text_content = std::move(stylesheet);
                            activate_connected_stylesheet(*stack.back());
                        }
                    }
                    stack.pop_back();
                    if (matched) break;
                }
                cursor = close + 1U;
                continue;
            }

            auto name_begin = open + 1U;
            while (name_begin < close
                && std::isspace(static_cast<unsigned char>(html[name_begin]))) ++name_begin;
            auto name_end = name_begin;
            while (name_end < close
                && !std::isspace(static_cast<unsigned char>(html[name_end]))
                && html[name_end] != '/' && html[name_end] != '>') ++name_end;
            const auto name = lower_html_name(html.substr(name_begin, name_end - name_begin));
            if (name.empty()) {
                cursor = close + 1U;
                continue;
            }

            record_feature(
                "html",
                "element:" + name,
                "supported",
                {},
                "html-element");
            auto& node = document.create_element(name);
            if (name == "script") {
                node.script_state = script_execution_state::already_started;
            }
            const auto svg = name == "svg"
                || stack.back()->tag == "svg"
                || stack.back()->attributes.contains("namespace");
            if (svg) node.attributes["namespace"] = "http://www.w3.org/2000/svg";
            for (auto [attribute_name, value] : parse_html_attributes(
                std::string_view(html).substr(open, close - open + 1U),
                name_end - open)) {
                const auto lower_name = lower_html_name(attribute_name);
                record_html_attribute_feature(lower_name, "html-parser");
                if (lower_name == "class") {
                    node.class_name = value;
                    node.attributes["class"] = value;
                } else if (lower_name == "id") {
                    node.id_attribute = value;
                    node.attributes["id"] = value;
                }
                else {
                    // HTML attribute names are ASCII case-insensitive. SVG's
                    // canonical viewBox spelling is retained for its native
                    // paint projection; non-ASCII code points are untouched by
                    // lower_html_name.
                    const auto stored_name = svg && lower_name == "viewbox"
                        ? std::string("viewBox")
                        : lower_name;
                    node.attributes[stored_name] = value;
                }
            }
            if (name == "input") {
                const auto value = node.attributes.find("value");
                if (value != node.attributes.end()) {
                    node.mutable_form_control().value = value->second;
                    node.mutable_form_control().value_initialized = true;
                    node.mutable_form_control().dirty_value = false;
                    node.mutable_form_control().selection_start = node.mutable_form_control().value.size();
                    node.mutable_form_control().selection_end = node.mutable_form_control().value.size();
                    node.mutable_form_control().selection_direction = text_selection_direction::none;
                }
            }
            document.append_child(*stack.back(), node);
            apply_css_rules(node);
            if (const auto style = node.attributes.find("style"); style != node.attributes.end()) {
                uint64_t inline_property_mask = 0U;
                for (const auto& declaration : parse_css_declarations(style->second, true)) {
                    inventory_css_value_functions(declaration.value, "inline-style-parser");
                    if (!store_inline_declaration(node, declaration)) continue;
                    apply_css_declaration(node, declaration);
                    inline_property_mask |= inline_style_mask(declaration.name);
                }
                node.style.inline_property_mask |= inline_property_mask;
                detect_css_compositions(node);
            }
            if (name == "iframe") {
                const auto source = node.attributes.find("src");
                if (source != node.attributes.end()) {
                    const auto object_url = object_urls.find(source->second);
                    if (object_url != object_urls.end()) node.attributes["object-html"] = object_url->second;
                }
                node.style.width = {100, length_unit::percent};
                node.style.height = {100, length_unit::percent};
                node.style.background_rgba = 0x131722FFU;
                node.style.inline_property_mask |= inline_width | inline_height | inline_background;
                if (node.attributes.contains("object-html")) pending_frame_hydrations.push_back(&node);
            }

            const auto self_closing = close > open && html[close - 1U] == '/';
            const auto void_element = name == "area" || name == "base" || name == "br"
                || name == "col" || name == "embed" || name == "hr" || name == "img"
                || name == "input" || name == "link" || name == "meta" || name == "param"
                || name == "source" || name == "track" || name == "wbr";
            if (!self_closing && !void_element) stack.push_back(&node);
            cursor = close + 1U;
        }
        // Structural pseudo-classes are evaluated against the completed child
        // list. During parsing, the first of several siblings temporarily also
        // matches :last-child; without this final recascade, rules such as
        // `.dot:not(:last-child)` remain stale even though Element.matches()
        // observes the finished tree.
        recascade_connected_subtree(parent);
        if (is_connected(parent)) {
            enqueue_connected_image_resources_subtree(
                parent,
                isolate->GetCurrentContext());
        }
        document.mark_dirty();
        for (auto* child : parent.children) {
            if (child != nullptr) {
                notify_custom_elements_subtree(*child, "connected");
            }
        }
#endif
    }

    static void set_inner_html(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> raw_value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_mutation));
        auto* parent = unwrap_node(info.Holder());
        if (parent == nullptr) return;
        const auto html = to_utf8(info.GetIsolate(), raw_value);
        auto* output = parent;
#if defined(WEBSCENE_NATIVE_ENGINE_HTML5EVER)
        if (parent->tag == "template") {
            output = &self->document.parser_template_contents(*parent);
        }
#endif
        if (!self->blur_active_descendant_before_replacing_children(*output)) return;
        self->detach_all_children(*output);
        output->text_content.clear();
        if (!html.empty()) self->parse_inner_html(*output, html, parent);
        else synchronize_textarea_value_from_children(*output);
    }

    static void get_content_window(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr || node->tag != "iframe" || node->parent == nullptr) {
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
            return;
        }
        const auto actual = self->actual_frame_windows.find(node->id);
        if (actual != self->actual_frame_windows.end()) {
            info.GetReturnValue().Set(actual->second.Get(info.GetIsolate()));
            return;
        }
        info.GetReturnValue().Set(self->frame_window(*node));
    }

    static void get_content_document(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (node == nullptr || node->tag != "iframe" || node->parent == nullptr) {
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
            return;
        }
        const auto actual = self->actual_frame_documents.find(node->id);
        if (actual != self->actual_frame_documents.end()) {
            info.GetReturnValue().Set(actual->second.Get(info.GetIsolate()));
            return;
        }
        info.GetReturnValue().Set(self->frame_document(*node));
    }

    static void frame_document_open(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = provisional_frame_owner(info.This());
        if (node == nullptr) return;
        node->attributes["frame-html"].clear();
        const auto body = self->provisional_frame_bodies.find(node->id);
        if (body != self->provisional_frame_bodies.end() && body->second != nullptr) {
            self->detach_all_children(*body->second);
        }
    }

    static void frame_document_write(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* node = provisional_frame_owner(info.This());
        if (node != nullptr && info.Length() > 0) {
            node->attributes["frame-html"] += to_utf8(info.GetIsolate(), info[0]);
        }
    }

    static void frame_document_close(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = provisional_frame_owner(info.This());
        if (node == nullptr) return;
        const auto html = node->attributes["frame-html"];
        const auto body = self->provisional_frame_bodies.find(node->id);
        if (body != self->provisional_frame_bodies.end() && body->second != nullptr) {
            self->detach_all_children(*body->second);
            auto body_html = extract_element_contents(html, "body");
            if (body_html.empty()) body_html = html;
            self->parse_inner_html(*body->second, body_html);
        }
        // Documents containing executable/resource-bearing markup still need
        // their independent frame realm. Plain document.write replacement is
        // complete synchronously in the initial about:blank realm.
        const auto normalized_html = lower_html_name(html);
        if (normalized_html.find("<script") != std::string::npos
            || normalized_html.find("<link") != std::string::npos
            || normalized_html.find("onload") != std::string::npos) {
            self->pending_frame_hydrations.push_back(node);
        }
    }

    static void frame_window_add_event_listener(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (node == nullptr || info.Length() < 2 || !info[1]->IsFunction()) return;
        const auto type = to_utf8(info.GetIsolate(), info[0]);
        if (type == "innerWindowLoad") {
            self->frame_load_listeners[node->id].Reset(info.GetIsolate(), info[1].As<v8::Function>());
        }
    }
